// Cycle director -- see cycle.h for the state machine and the [cycle] keys,
// animators.h for the freeze / pause contract with the Settings UI.
// Phase 1 (brief CYCLE-DIRECTOR.md): fade through black between looks; the
// absorbed mood-conductor lerp between two fluid stages.

#include "cycle.h"
#include "animators.h"
#include "app_state.h"
#include "journey.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstring>

bool g_cycleActive = false;

namespace {

// Measured warm-up, in SIM seconds, from a black reset (ResetLookState: clear
// dye + velocity, reseed the acid population) to a formed frame. Measured
// 2026-09-25 on branch cycle: 1280x720 series from a cold start, 0.5 s steps,
// plus a 180 s monotone run for the long-run band. "Formed" = mean_lum AND
// (oil) the droplet count inside the look's own long-run band:
//   fluid (we-look-live)  16 s: the startup burst (mean_lum 0.18..0.44 for the
//                         first 10 s) has decayed to the dwell level (0.015 at
//                         16 s vs a 0.012..0.08 dwell median); at 10 s the
//                         first fade-in frame was still 5-14x the dwell median
//   liquid_acid           32 s: the droplet bulk fill dips 1130 -> 780 at 10 s
//                         and is back in the long-run band (1124..1536) by
//                         ~30 s; lapd-look-candidate's dye masses reach 95% of
//                         their level at 34 s (0.04 -> 0.16 mean_lum)
//   ink (ink-inverted)     8 s: clear water until the first drop lands at ~7.5 s
const float kWarmupDefault[3] = { 16.0f, 32.0f, 8.0f };   // fluid, liquid_acid, ink
const float kWarmupExtraCap = 30.0f;   // max extra sim s waiting for the hue glide
// User 2026-09-25: "it should really keep moving, but like up to 4 mins per
// definite stage" -> every dwell (per stage, [cycle] dwell, CLI, and the
// jittered target) is clamped to this.
const float kMaxDwellSec = 240.0f;
// An overlay stage's own fades (it snaps a fold on/off, no look switch, no
// warm-up): short, unless the stage sets stage_N_fade_out / _fade_in.
const float kSoftFadeOut = 0.6f, kSoftFadeIn = 0.9f;

CycleConfig  s_cfg;
std::wstring s_iniPath;              // where [cycle] was read from
int   s_phase = CYCLE_OFF;
int   s_cur = -1, s_next = -1;
int   s_saved = -1;                  // [cycle] current (0-based), resume point
float s_phaseT = 0.0f, s_dwellT = 0.0f, s_dwellTarget = 600.0f, s_warmSim = 0.0f;
float s_darkSince = -1.0f;           // dwell time when the sustained-dark streak began
float s_fade = 1.0f, s_fadeFrom = 1.0f;
bool  s_stopAfter = false;           // turn off once the transition lands
std::vector<int> s_history;          // visited stages, for Previous
uint64_t s_rng = 0;
bool  s_rngSeeded = false;
double s_clock = 0.0;                // director clock (sum of frame dt): freeze/pause timeouts
// the fluid -> fluid lerp (the absorbed conductor transition)
enum { LERP_SHIFT = 0, LERP_EMIT, LERP_RETURN };
int   s_lerpSub = LERP_SHIFT;
FluidConfig s_from, s_target;
// the journey of the current fluid stage
bool  s_journeyWasActive = false;
// overlay stages (stage_N_overlay=1): a partial ini applied ON TOP of the look
// stage s_base for its dwell, then removed by restoring s_preOverlay
int   s_base = -1;                   // the look stage under the current overlay
FluidConfig s_preOverlay;
float s_prePeak = -1.0f;
int   s_preGamut = 2;
bool  s_soft = false;                // this FADE_OUT/FADE_IN is an overlay on/off
int   s_softStage = -1;              // the overlay stage whose fade keys apply
// CLI overrides
float    s_dwellOverride = -1.0f;
float    s_jitterOverride = -1.0f;
int      s_orderOverride = -1;
unsigned s_seedOverride = 0;
int      s_startStage = -1;
bool     s_forceEnable = false;
// the user's own shell values, restored for stages that do not carry them
float s_userPeak = -1.0f;
int   s_userGamut = 2;
bool  s_haveUser = false;
// CyclePause (Settings window open) + animator freezes
bool   s_paused = false;
double s_pauseUntil = 0.0;
bool   s_frozen[ANIM_COUNT] = {};
double s_frozenUntil[ANIM_COUNT] = {};
void (*s_logger)(const char*) = nullptr;

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (s_logger) s_logger(buf);
    else fputs(buf, stdout);
}

const char* LookName(int look) {
    return look == CYCLE_LOOK_ACID ? "liquid_acid" : (look == CYCLE_LOOK_INK ? "ink" : "fluid");
}
const char* PhaseName(int p) {
    switch (p) {
    case CYCLE_DWELL:    return "dwell";
    case CYCLE_FADE_OUT: return "fade_out";
    case CYCLE_WARMUP:   return "warmup";
    case CYCLE_FADE_IN:  return "fade_in";
    case CYCLE_LERP:     return "lerp";
    default:             return "off";
    }
}

uint32_t NextRand() {                    // splitmix64: private stream, never rand()
    s_rng += 0x9E3779B97F4A7C15ull;
    uint64_t z = s_rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
float NextUnit() { return (float)(NextRand() >> 8) * (1.0f / 16777216.0f); }
void SeedRng() {
    const unsigned seed = s_seedOverride ? s_seedOverride : s_cfg.seed;
    s_rng = seed ? (uint64_t)seed * 0x2545F4914F6CDD1Dull
                 : (GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 32));
    s_rngSeeded = true;
    Log("[cycle] order=%s seed=%u%s\n",
        s_cfg.order == CYCLE_ORDER_FIXED ? "fixed" : "alternate_random",
        seed, seed ? "" : " (wall clock)");
}

std::wstring IniStr(const wchar_t* sec, const wchar_t* key, const wchar_t* ini) {
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(sec, key, L"", buf, MAX_PATH * 2, ini);
    std::wstring s = buf;                // trim: the reference inis use "key = value"
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t')) a++;
    return s.substr(a);
}
float IniF(const wchar_t* sec, const wchar_t* key, float def, const wchar_t* ini) {
    std::wstring s = IniStr(sec, key, ini);
    return s.empty() ? def : (float)_wtof(s.c_str());
}

std::wstring FolderOf(const std::wstring& path) {
    size_t sl = path.find_last_of(L"\\/");
    return sl == std::wstring::npos ? std::wstring(L".") : path.substr(0, sl);
}
std::wstring Resolve(const std::wstring& rel, const std::wstring& ini) {
    if (rel.empty()) return rel;
    std::wstring p = rel;
    for (auto& ch : p) if (ch == L'/') ch = L'\\';
    const bool abs = (p.size() > 1 && p[1] == L':') || (p.size() > 1 && p[0] == L'\\' && p[1] == L'\\');
    if (!abs) p = FolderOf(ini) + L"\\" + p;
    wchar_t full[MAX_PATH * 2] = {};
    if (GetFullPathNameW(p.c_str(), MAX_PATH * 2, full, nullptr)) p = full;
    return p;
}
std::wstring Stem(const std::wstring& path) {
    size_t sl = path.find_last_of(L"\\/");
    std::wstring n = sl == std::wstring::npos ? path : path.substr(sl + 1);
    size_t dot = n.rfind(L'.');
    if (dot != std::wstring::npos) n.resize(dot);
    return n;
}
bool Exists(const std::wstring& p) {
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The look a stage lands on, from [look] alone (the same rules as
// LoadConfigFromIni: base first, the file over it) -- no full config load.
int LookOf(const CycleStage& st) {
    bool acid = false, ink = false;
    auto apply = [&](const std::wstring& ini) {
        if (!Exists(ini)) return;
        std::wstring style = IniStr(L"look", L"style", ini.c_str());
        if (!style.empty()) {
            acid = _wcsicmp(style.c_str(), L"liquid_acid") == 0;
            ink  = _wcsicmp(style.c_str(), L"ink") == 0;
        }
        acid = GetPrivateProfileIntW(L"look", L"liquid_acid", acid ? 1 : 0, ini.c_str()) != 0;
        ink  = GetPrivateProfileIntW(L"look", L"ink", ink ? 1 : 0, ini.c_str()) != 0;
        if (ink) acid = false;
    };
    apply(st.basePath);
    apply(st.path);
    return ink ? CYCLE_LOOK_INK : (acid ? CYCLE_LOOK_ACID : CYCLE_LOOK_FLUID);
}

void ResolveStage(CycleStage& st, const std::wstring& ini) {
    st.path = Resolve(st.file, ini);
    st.basePath = Resolve(st.base, ini);
    st.name = Stem(st.file);
    st.ok = Exists(st.path) && (st.base.empty() || Exists(st.basePath));
    st.look = st.ok ? LookOf(st) : CYCLE_LOOK_FLUID;
}

int OkCount() {
    int n = 0;
    for (auto& s : s_cfg.stages) if (s.ok) n++;
    return n;
}
bool Valid(int i) { return i >= 0 && i < (int)s_cfg.stages.size() && s_cfg.stages[i].ok; }
bool IsOverlay(int i) { return Valid(i) && s_cfg.stages[i].overlay; }
bool IsFluid(int i) { return Valid(i) && !s_cfg.stages[i].overlay && s_cfg.stages[i].look == CYCLE_LOOK_FLUID; }
// the look stage actually running (an overlay's base)
int LookStage(int i) { return IsOverlay(i) ? s_base : i; }

float DwellOf(int i) {
    float v = s_dwellOverride > 0.0f ? s_dwellOverride
            : (Valid(i) && s_cfg.stages[i].dwellSec > 0.0f ? s_cfg.stages[i].dwellSec : s_cfg.dwellSec);
    return fminf(fmaxf(v, 1.0f), kMaxDwellSec);
}
float JitterOf(int i) {
    if (s_jitterOverride >= 0.0f) return s_jitterOverride;
    const float j = Valid(i) && s_cfg.stages[i].jitter >= 0.0f ? s_cfg.stages[i].jitter : s_cfg.jitter;
    return fminf(fmaxf(j, 0.0f), 0.9f);
}
float FadeOutOf(int i) {
    if (s_soft) {
        const float v = Valid(s_softStage) ? s_cfg.stages[s_softStage].fadeOutSec : -1.0f;
        return v >= 0.0f ? v : kSoftFadeOut;
    }
    if (!Valid(i)) return s_cfg.fadeOutSec;
    const CycleStage& st = s_cfg.stages[i];
    if (st.transition == CYCLE_TR_CUT) return 0.0f;
    return st.fadeOutSec >= 0.0f ? st.fadeOutSec : s_cfg.fadeOutSec;
}
float FadeInOf(int i) {
    if (s_soft) {
        const float v = Valid(s_softStage) ? s_cfg.stages[s_softStage].fadeInSec : -1.0f;
        return v >= 0.0f ? v : kSoftFadeIn;
    }
    if (!Valid(i)) return s_cfg.fadeInSec;
    const CycleStage& st = s_cfg.stages[i];
    if (st.transition == CYCLE_TR_CUT) return 0.0f;
    return st.fadeInSec >= 0.0f ? st.fadeInSec : s_cfg.fadeInSec;
}
float LerpOf(int i) {
    const float v = Valid(i) && s_cfg.stages[i].lerpSec >= 0.0f ? s_cfg.stages[i].lerpSec : s_cfg.lerpSec;
    return fmaxf(1.0f, v);               // the conductor's floor
}
float WarmupOf(int i) {
    if (!Valid(i)) return kWarmupDefault[0];
    const CycleStage& st = s_cfg.stages[i];
    return st.warmupSec >= 0.0f ? st.warmupSec : kWarmupDefault[st.look];
}

float Smooth(float x) {
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x * x * (3.0f - 2.0f * x);
}

// ---- the absorbed conductor transition (was moods.cpp LerpLook/FlipDiscrete)
float L(float a, float b, float t) { return a + (b - a) * t; }

// Whitelisted look floats -- lerped across the whole transition. Everything
// not listed here (bools, ints, palette array) flips at the midpoint.
void LerpLook(FluidConfig& o, const FluidConfig& a, const FluidConfig& b, float t) {
    o.densityDissipation  = L(a.densityDissipation,  b.densityDissipation,  t);
    o.velocityDissipation = L(a.velocityDissipation, b.velocityDissipation, t);
    o.pressureDissipation = L(a.pressureDissipation, b.pressureDissipation, t);
    o.decayFast           = L(a.decayFast,           b.decayFast,           t);
    o.decayThreshold      = L(a.decayThreshold,      b.decayThreshold,      t);
    o.dyeDiffusion        = L(a.dyeDiffusion,        b.dyeDiffusion,        t);
    o.maxBrightness       = L(a.maxBrightness,       b.maxBrightness,       t);
    o.satRestore          = L(a.satRestore,          b.satRestore,          t);
    o.curl                = L(a.curl,                b.curl,                t);
    o.baroclinic          = L(a.baroclinic,          b.baroclinic,          t);
    o.flowSpeed           = L(a.flowSpeed,           b.flowSpeed,           t);
    o.splatRadius         = L(a.splatRadius,         b.splatRadius,         t);
    o.colorCyclePeriod    = L(a.colorCyclePeriod,    b.colorCyclePeriod,    t);
    o.idleInterval        = L(a.idleInterval,        b.idleInterval,        t);
    o.idleBrightness      = L(a.idleBrightness,      b.idleBrightness,      t);
    o.hdrKnee             = L(a.hdrKnee,             b.hdrKnee,             t);
    o.hdrSaturation       = L(a.hdrSaturation,       b.hdrSaturation,       t);
    o.hdrBrightness       = L(a.hdrBrightness,       b.hdrBrightness,       t);
    o.hdrContrast         = L(a.hdrContrast,         b.hdrContrast,         t);
    o.postSaturation      = L(a.postSaturation,      b.postSaturation,      t);
    o.postContrast        = L(a.postContrast,        b.postContrast,        t);
    o.postBrightness      = L(a.postBrightness,      b.postBrightness,      t);
    o.postHue             = L(a.postHue,             b.postHue,             t);
    o.curveCenter         = L(a.curveCenter,         b.curveCenter,         t);
    o.curveWidth          = L(a.curveWidth,          b.curveWidth,          t);
    o.curveHeight         = L(a.curveHeight,         b.curveHeight,         t);
    o.shadowFloor         = L(a.shadowFloor,         b.shadowFloor,         t);
    o.shadowKnee          = L(a.shadowKnee,          b.shadowKnee,          t);
    o.hueCenter           = L(a.hueCenter,           b.hueCenter,           t);
    o.hueRange            = L(a.hueRange,            b.hueRange,            t);
    o.hueLinger           = L(a.hueLinger,           b.hueLinger,           t);
    o.wandererSpeed       = L(a.wandererSpeed,       b.wandererSpeed,       t);
    o.wandererBrightness  = L(a.wandererBrightness,  b.wandererBrightness,  t);
    o.wandererScale       = L(a.wandererScale,       b.wandererScale,       t);
    o.wandererResumeDelay = L(a.wandererResumeDelay, b.wandererResumeDelay, t);
    o.darkFloor           = L(a.darkFloor,           b.darkFloor,           t);
    o.darkLevel           = L(a.darkLevel,           b.darkLevel,           t);
    o.survDarkFloor       = L(a.survDarkFloor,       b.survDarkFloor,       t);
    o.contrastReq         = L(a.contrastReq,         b.contrastReq,         t);
    o.dartInterval        = L(a.dartInterval,        b.dartInterval,        t);
    o.dartSpeed           = L(a.dartSpeed,           b.dartSpeed,           t);
    o.hsStep              = L(a.hsStep,              b.hsStep,              t);
    o.hsLinger            = L(a.hsLinger,            b.hsLinger,            t);
    o.hsGlide             = L(a.hsGlide,             b.hsGlide,             t);
    o.hsOffTime           = L(a.hsOffTime,           b.hsOffTime,           t);
}

// Discrete look fields -- flipped at the transition midpoint.
void FlipDiscrete(FluidRenderer& r, const FluidConfig& t) {
    FluidConfig& c = r.Config();
    c.colorful           = t.colorful;
    c.moreColors         = t.moreColors;
    memcpy(c.splatColors, t.splatColors, sizeof(c.splatColors));
    c.pressureIterations = t.pressureIterations;
    c.shading            = t.shading;
    c.curveEnabled       = t.curveEnabled;
    c.hdrCompensation    = t.hdrCompensation;
    c.wanderers          = t.wanderers;
    c.wandererCount      = t.wandererCount;
    c.wandererMode       = t.wandererMode;
    c.autoPause          = t.autoPause;
    c.dartEnabled        = t.dartEnabled;
    c.hsEnabled          = t.hsEnabled;
    c.hsBurstSteps       = t.hsBurstSteps;
    c.idleSplats         = t.idleSplats;
    c.idleAmount         = t.idleAmount;
    c.holdToSplat        = t.holdToSplat;
    c.splatOnClick       = t.splatOnClick;
    c.showMouse          = t.showMouse;
    r.ReinitWanderers();
}

// FluidConfig{} + base + file; the shell keys never come from a stage.
void Compose(int i, const FluidConfig& live, FluidConfig& out, float& peak, int& gamut) {
    const CycleStage& st = s_cfg.stages[i];
    FluidConfig c;
    if (!st.basePath.empty()) LoadConfigFromFile(st.basePath.c_str(), c);
    LoadConfigFromFile(st.path.c_str(), c);
    c.simRes        = live.simRes;          // SHELL: never applied from a stage
    c.dyeRes        = live.dyeRes;
    c.fpsLimit      = live.fpsLimit;
    c.mirrorSecond  = live.mirrorSecond;
    c.gradientMode  = live.gradientMode;    // CLI debug switches
    c.calibratePage = live.calibratePage;
    c.stats         = live.stats;
    c.hdrPeakNits   = live.hdrPeakNits;     // resolved per frame by the shell
    c.gamutMode     = live.gamutMode;
    out = c;
    // [hdr] peak_nits / gamut are shell globals: the stage's value when it
    // carries one (file over base), else the user's own setting
    peak = s_userPeak;
    gamut = s_userGamut;
    for (const std::wstring* ini : { &st.basePath, &st.path }) {
        if (ini->empty()) continue;
        std::wstring pk = IniStr(L"hdr", L"peak_nits", ini->c_str());
        if (!pk.empty()) peak = (float)_wtof(pk.c_str());
        int gm = (int)GetPrivateProfileIntW(L"hdr", L"gamut", -1, ini->c_str());
        if (gm >= 0 && gm <= 2) gamut = gm;
    }
}

void CaptureUserShell() {
    if (s_haveUser) return;
    s_userPeak = g_hdrPeakNits;
    s_userGamut = g_gamutMode;
    s_haveUser = true;
}

void PersistCurrent() {
    if (g_configReadOnly || !g_iniPath[0] || s_cur < 0) return;
    if (_wcsicmp(s_iniPath.c_str(), g_iniPath) != 0) return;
    wchar_t v[16];
    swprintf_s(v, L"%d", s_cur + 1);
    WritePrivateProfileStringW(L"cycle", L"current", v, g_iniPath);
}

void PushHistory(int from, int to) {
    if (from >= 0 && from != to) {
        s_history.push_back(from);
        if (s_history.size() > 64) s_history.erase(s_history.begin());
    }
}

// A journey on a fluid stage: stage_N_journey, else the file's [journey] file=
void AttachJourney(int i) {
    JourneyDetach();
    s_journeyWasActive = false;
    if (!IsFluid(i)) return;                 // overlays never carry a journey
    const CycleStage& st = s_cfg.stages[i];
    if (!st.journey.empty()) JourneyAttachNamed(st.journey.c_str(), st.path.c_str());
    else                     JourneyAttach(st.path.c_str());
    s_journeyWasActive = JourneyActive();
}

// Leaving a journey: hand the held hue command back (the rotation must never
// stay locked; journey.cpp has no renderer access)
void DetachJourney(FluidRenderer& r) {
    if (JourneyActive() || s_journeyWasActive) {
        JourneyDetach();
        r.ReleaseHueShift(false);
    }
    s_journeyWasActive = false;
}

void GoOff(const char* why);

void EnterDwell(FluidRenderer& r, int i) {
    if (s_stopAfter) {                   // turned off mid-transition: it has landed
        GoOff("turned off; the transition has landed");
        return;
    }
    s_phase = CYCLE_DWELL;
    s_phaseT = 0.0f;
    s_dwellT = 0.0f;
    s_darkSince = -1.0f;
    const float j = JitterOf(i);
    s_dwellTarget = fminf(DwellOf(i) * (1.0f - j + 2.0f * j * NextUnit()), kMaxDwellSec);
    s_soft = false;
    s_softStage = -1;
    r.SetCoverageWanted(IsFluid(LookStage(i)));   // dark trigger + the next hue bridge
    AttachJourney(i);
    Log("[cycle] dwelling in %d %ls (%.1f s = %.0f s +-%.0f%%)%s\n",
        i + 1, s_cfg.stages[i].name.c_str(), s_dwellTarget, DwellOf(i), j * 100.0f,
        s_journeyWasActive ? " with a journey" : "");
}

void ApplyStage(FluidRenderer& r, int i) {
    FluidConfig c;
    float peak;
    int gamut;
    Compose(i, r.Config(), c, peak, gamut);
    r.Config() = c;
    g_hdrPeakNits = peak;
    g_gamutMode = gamut;
    r.EnsureLookResources();        // PSO compile on first use (the screen is black)
    const double compileMs = r.LastLookCompileMs() + r.PrecompilePostPsos();
    r.ResetLookState();
    s_base = -1;                    // a fresh composition drops any overlay
    PushHistory(s_cur, i);
    s_cur = i;
    s_next = -1;
    PersistCurrent();
    const CycleStage& st = s_cfg.stages[i];
    Log("[cycle] stage %d/%d %ls (%s) applied at black: peak=%.0f gamut=%d "
        "pso_compile=%.0f ms warmup=%.1f sim s%s\n",
        i + 1, (int)s_cfg.stages.size(), st.name.c_str(), LookName(st.look),
        peak, gamut, compileMs, WarmupOf(i),
        st.transition == CYCLE_TR_LERP ? " (transition=lerp across looks runs as a fade)" : "");
}

int PickNext() {
    const int n = (int)s_cfg.stages.size();
    if (n == 0 || OkCount() == 0) return -1;
    if (s_cfg.order == CYCLE_ORDER_FIXED) {
        if (s_cur >= 0 && !s_cfg.loop) {
            bool anyAfter = false;       // no loop: stop on the last valid stage
            for (int j = s_cur + 1; j < n; j++) if (Valid(j)) { anyAfter = true; break; }
            if (!anyAfter) return -1;
        }
        for (int k = 1; k <= n; k++) {
            int j = ((s_cur < 0 ? -1 : s_cur) + k) % n;
            if (j < 0) j += n;
            if (Valid(j)) return j;
        }
        return -1;
    }
    // alternate_random: weighted over the stages whose LOOK differs from the
    // running one; never the same file twice in a row. An overlay stage counts
    // as "the other look", so overlays never chain: after one, the next is a
    // look stage of a different look than the one under the overlay.
    if (!s_rngSeeded) SeedRng();
    const bool onOverlay = IsOverlay(s_cur);
    const int  ls = LookStage(s_cur);
    const int  curLook = Valid(ls) ? s_cfg.stages[ls].look : -1;
    const std::wstring curPath = Valid(s_cur) ? s_cfg.stages[s_cur].path : L"";
    auto samePath = [&](int j) { return _wcsicmp(s_cfg.stages[j].path.c_str(), curPath.c_str()) == 0; };
    std::vector<int> cands;
    for (int j = 0; j < n; j++) {
        if (!Valid(j) || j == s_cur || samePath(j)) continue;
        const bool ov = s_cfg.stages[j].overlay;
        if (onOverlay) { if (!ov && s_cfg.stages[j].look != curLook) cands.push_back(j); }
        else if (Valid(s_cur) ? (ov || s_cfg.stages[j].look != curLook) : !ov) cands.push_back(j);
    }
    if (cands.empty())
        for (int j = 0; j < n; j++)
            if (Valid(j) && j != s_cur && !samePath(j) && !s_cfg.stages[j].overlay)
                cands.push_back(j);
    if (cands.empty()) return -1;
    // weighted draw (stage_N_weight, default 1); logged so a seeded run shows it
    float total = 0.0f;
    for (int j : cands) total += fmaxf(s_cfg.stages[j].weight, 0.0f);
    const float u = NextUnit();
    int pick = cands.back();
    if (total > 0.0f) {
        float acc = 0.0f;
        for (int j : cands) {
            acc += fmaxf(s_cfg.stages[j].weight, 0.0f) / total;
            if (u < acc) { pick = j; break; }
        }
    } else {
        pick = cands[(size_t)(u * cands.size()) % cands.size()];
    }
    char list[512] = {};
    size_t len = 0;
    for (int j : cands) {
        const int n = _snprintf_s(list + len, sizeof(list) - len, _TRUNCATE, "%s%d:%g",
                                  len ? " " : "", j + 1, s_cfg.stages[j].weight);
        if (n < 0) break;
        len += (size_t)n;
    }
    Log("[cycle] draw u=%.4f among {%s} -> stage %d %ls\n", u, list, pick + 1,
        s_cfg.stages[pick].name.c_str());
    return pick;
}

int PickPrev() {
    const int n = (int)s_cfg.stages.size();
    if (n == 0 || OkCount() == 0) return -1;
    if (s_cfg.order == CYCLE_ORDER_ALT_RANDOM) {
        while (!s_history.empty()) {
            int j = s_history.back();
            s_history.pop_back();
            if (Valid(j) && j != s_cur) return j;
        }
        return PickNext();
    }
    for (int k = 1; k <= n; k++) {
        int j = ((s_cur < 0 ? 0 : s_cur) - k) % n;
        if (j < 0) j += n;
        if (Valid(j)) return j;
    }
    return -1;
}

void GoOff(const char* why) {
    s_phase = CYCLE_OFF;
    s_next = -1;
    s_fade = 1.0f;
    s_stopAfter = false;
    g_cycleActive = false;
    if (g_renderer) g_renderer->SetCoverageWanted(false);
    Log("[cycle] off (%s)\n", why);
}

// Does cur -> target lerp (both fluid, not forced to fade/cut)?
bool WantsLerp(int target) {
    // leaving an overlay always fades: the lerp's t=1 config would drop the
    // overlay's keys (a mirror fold) in one visible snap
    if (!IsFluid(s_cur) || !IsFluid(target)) return false;
    const int tr = s_cfg.stages[target].transition;
    return tr == CYCLE_TR_DEFAULT || tr == CYCLE_TR_LERP;
}

// The conductor's hue bridge + lerp start (moods.cpp BeginTransition, kept):
// rotate the visible field so it lands on the next stage's band. Forward only.
void BeginLerp(FluidRenderer& r, int target) {
    DetachJourney(r);
    float peak;
    int gamut;
    s_from = r.Config();
    Compose(target, s_from, s_target, peak, gamut);
    r.SetCoverageWanted(true);           // the bridge angle needs a fresh field hue
    const float dyeHue = r.FieldAvgHueDeg();
    const float curAng = r.HueAngleDeg();
    const float targetVisible = s_target.hueRange < 179.0f ? s_target.hueCenter
                                                           : dyeHue + curAng + s_target.postHue + 60.0f;
    const float targetAng = fmodf(targetVisible - dyeHue - s_target.postHue + 720.0f, 360.0f);
    const float cmd = curAng + fmodf(targetAng - fmodf(curAng + 720.0f, 360.0f) + 720.0f, 360.0f);
    const float sec = LerpOf(target);
    r.CommandHueShift(cmd, sec * 0.4f);
    s_next = target;
    s_phase = CYCLE_LERP;
    s_lerpSub = LERP_SHIFT;
    s_phaseT = 0.0f;
    Log("[cycle] lerp %ls -> %ls over %.1f s (hue bridge %.0f deg)\n",
        s_cfg.stages[s_cur].name.c_str(), s_cfg.stages[target].name.c_str(), sec,
        fmodf(cmd - curAng + 720.0f, 360.0f));
}

void FinishLerp(FluidRenderer& r) {
    const int target = s_next;
    float peak;
    int gamut;
    FluidConfig c;
    Compose(target, r.Config(), c, peak, gamut);   // exact stage config at t=1
    r.Config() = c;
    g_hdrPeakNits = peak;
    g_gamutMode = gamut;
    r.EnsureLookResources();
    PushHistory(s_cur, target);
    s_cur = target;
    s_next = -1;
    PersistCurrent();
    EnterDwell(r, target);
}

void BeginSwitch(FluidRenderer& r, int target) {
    if (!Valid(target)) return;
    // an overlay can only go on over a formed look: not mid black / hard fade
    if (IsOverlay(target) && (s_phase == CYCLE_WARMUP || (s_phase == CYCLE_FADE_OUT && !s_soft))) return;
    if (s_phase == CYCLE_FADE_OUT && s_soft) return;   // a soft switch is 0.6 s: let it land
    if (s_phase == CYCLE_WARMUP) {          // still black: re-target in place
        ApplyStage(r, target);
        r.ReleaseHueShift(false);
        s_phaseT = 0.0f;
        s_warmSim = 0.0f;
        return;
    }
    if (s_phase == CYCLE_FADE_OUT) {        // already on the way to black
        s_next = target;
        return;
    }
    if (s_phase == CYCLE_LERP) {            // land the lerp first, then go on
        r.Config() = s_target;
        r.ReleaseHueShift(false);
        FinishLerp(r);
    }
    if (s_phase == CYCLE_DWELL && WantsLerp(target)) {
        BeginLerp(r, target);
        return;
    }
    // Soft switch (short fade, no black hold, no sim reset): an overlay goes on
    // over the running look, or comes off back onto exactly its base stage.
    const bool haveLook = Valid(LookStage(s_cur));
    s_soft = haveLook && (IsOverlay(target) || (IsOverlay(s_cur) && target == s_base));
    s_softStage = s_soft ? (IsOverlay(target) ? target : s_cur) : -1;
    if (IsOverlay(target) && !haveLook) return;   // nothing to fold: never an overlay first
    // from DWELL (fade 1) or mid FADE_IN (fade s_fade): dim from where it is
    s_fadeFrom = (s_phase == CYCLE_FADE_IN) ? s_fade : 1.0f;
    s_next = target;
    s_phase = CYCLE_FADE_OUT;
    s_phaseT = 0.0f;
    JourneyDetach();                        // nothing drives keys during the fade;
    s_journeyWasActive = false;             // the hue is released at the black point
    Log("[cycle] switching %ls -> %ls (fade out %.2f s)\n",
        Valid(s_cur) ? s_cfg.stages[s_cur].name.c_str() : L"-",
        s_cfg.stages[target].name.c_str(), FadeOutOf(target));
}

// The bottom of a SOFT fade: put an overlay on (restoring any previous one
// first) or take it off, in memory, keeping the sim, the look and its PSOs.
void SoftPoint(FluidRenderer& r) {
    const int target = s_next;
    FluidConfig& c = r.Config();
    if (IsOverlay(s_cur)) {                  // restore the pre-overlay values
        c = s_preOverlay;
        g_hdrPeakNits = s_prePeak;
        g_gamutMode = s_preGamut;
    } else {
        s_base = s_cur;
    }
    if (IsOverlay(target)) {
        s_preOverlay = c;
        s_prePeak = g_hdrPeakNits;
        s_preGamut = g_gamutMode;
        const FluidConfig shell = c;
        LoadConfigFromFile(s_cfg.stages[target].path.c_str(), c);
        c.simRes = shell.simRes; c.dyeRes = shell.dyeRes;       // SHELL keys stay
        c.fpsLimit = shell.fpsLimit; c.mirrorSecond = shell.mirrorSecond;
        c.acid.enabled = shell.acid.enabled;                     // an overlay never
        c.ink.enabled = shell.ink.enabled;                       // switches the look
        r.ReinitWanderers();
        Log("[cycle] overlay %d %ls ON over %d %ls\n", target + 1, s_cfg.stages[target].name.c_str(),
            s_base + 1, Valid(s_base) ? s_cfg.stages[s_base].name.c_str() : L"-");
    } else {
        r.ReinitWanderers();
        Log("[cycle] overlay OFF, back on %d %ls (pre-overlay values restored)\n",
            target + 1, s_cfg.stages[target].name.c_str());
        s_base = -1;
    }
    PushHistory(s_cur, target);
    s_cur = target;
    s_next = -1;
    PersistCurrent();
    s_phase = CYCLE_FADE_IN;                 // no warm-up: nothing was reset
    s_phaseT = 0.0f;
}

void BlackPoint(FluidRenderer& r) {
    const int target = Valid(s_next) ? s_next : s_cur;
    ApplyStage(r, target);
    // The hue angle glides home (next full turn) during the black warm-up --
    // AGENTS: never zero m_hueAngle abruptly. WARMUP waits for it.
    r.ReleaseHueShift(false);
    s_phase = CYCLE_WARMUP;
    s_phaseT = 0.0f;
    s_warmSim = 0.0f;
}

void ApplyFreezeToRenderer(int a, bool on) {
    if (a >= ANIM_PALETTE && a <= ANIM_HUE_SHIFT && g_renderer)
        g_renderer->SetAnimatorFrozen(a, on);
}

} // namespace

// ---------------------------------------------------------------------------

void CycleSetLogger(void (*fn)(const char*)) { s_logger = fn; }

void CycleLoad(const wchar_t* ini) {
    s_cfg = CycleConfig{};
    s_iniPath = ini ? ini : L"";
    if (s_iniPath.empty()) return;
    const wchar_t* I = s_iniPath.c_str();
    const wchar_t* S = L"cycle";
    s_cfg.enabled = GetPrivateProfileIntW(S, L"enabled", 0, I) != 0;
    s_cfg.loop = GetPrivateProfileIntW(S, L"loop", 1, I) != 0;
    std::wstring order = IniStr(S, L"order", I);
    s_cfg.order = (_wcsicmp(order.c_str(), L"fixed") == 0) ? CYCLE_ORDER_FIXED : CYCLE_ORDER_ALT_RANDOM;
    s_cfg.seed = (unsigned)GetPrivateProfileIntW(S, L"seed", 0, I);
    s_cfg.fadeOutSec = fmaxf(IniF(S, L"fade_out", 1.5f, I), 0.0f);
    s_cfg.fadeInSec  = fmaxf(IniF(S, L"fade_in", 2.5f, I), 0.0f);
    s_cfg.warmupSteps = (int)GetPrivateProfileIntW(S, L"warmup_steps", 8, I);
    if (s_cfg.warmupSteps < 1) s_cfg.warmupSteps = 1;
    if (s_cfg.warmupSteps > 32) s_cfg.warmupSteps = 32;
    s_cfg.warmupStepHz = IniF(S, L"warmup_step_hz", 144.0f, I);
    if (s_cfg.warmupStepHz < 30.0f) s_cfg.warmupStepHz = 30.0f;
    s_cfg.lerpSec = fmaxf(IniF(S, L"lerp", 4.0f, I), 1.0f);
    s_cfg.dwellSec = IniF(S, L"dwell", 180.0f, I);
    if (s_cfg.dwellSec > kMaxDwellSec) {
        Log("[cycle] [cycle] dwell=%.0f clamped to %.0f s (the 4-minute maximum)\n", s_cfg.dwellSec, kMaxDwellSec);
        s_cfg.dwellSec = kMaxDwellSec;
    }
    s_cfg.jitter = fminf(fmaxf(IniF(S, L"jitter", 0.3f, I), 0.0f), 0.9f);
    s_cfg.earlyDarkPct = IniF(S, L"early_switch_darkpct", 92.0f, I);
    s_cfg.minDwellSec = IniF(S, L"min_dwell", 60.0f, I);
    const int count = (int)GetPrivateProfileIntW(S, L"stage_count", 0, I);
    for (int k = 1; k <= count && k <= 99; k++) {
        wchar_t key[48];
        auto K = [&](const wchar_t* suffix) { swprintf_s(key, L"stage_%d_%s", k, suffix); return key; };
        CycleStage st;
        st.file = IniStr(S, K(L"file"), I);
        if (st.file.empty()) continue;
        st.base = IniStr(S, K(L"base"), I);
        st.dwellSec = IniF(S, K(L"dwell"), -1.0f, I);
        if (st.dwellSec > kMaxDwellSec) {
            Log("[cycle] stage %d dwell=%.0f clamped to %.0f s (the 4-minute maximum)\n",
                k, st.dwellSec, kMaxDwellSec);
            st.dwellSec = kMaxDwellSec;
        }
        st.overlay = GetPrivateProfileIntW(S, K(L"overlay"), 0, I) != 0;
        std::wstring tr = IniStr(S, K(L"transition"), I);
        if (_wcsicmp(tr.c_str(), L"cut") == 0)       st.transition = CYCLE_TR_CUT;
        else if (_wcsicmp(tr.c_str(), L"fade") == 0) st.transition = CYCLE_TR_FADE;
        else if (_wcsicmp(tr.c_str(), L"lerp") == 0) st.transition = CYCLE_TR_LERP;
        const float total = IniF(S, K(L"fade"), -1.0f, I);
        st.fadeOutSec = IniF(S, K(L"fade_out"), total >= 0.0f ? total * 0.375f : -1.0f, I);
        st.fadeInSec  = IniF(S, K(L"fade_in"),  total >= 0.0f ? total * 0.625f : -1.0f, I);
        st.warmupSec = IniF(S, K(L"warmup"), -1.0f, I);
        st.jitter = IniF(S, K(L"jitter"), -1.0f, I);
        st.weight = fmaxf(IniF(S, K(L"weight"), 1.0f, I), 0.0f);
        st.lerpSec = IniF(S, K(L"lerp"), -1.0f, I);
        st.journey = IniStr(S, K(L"journey"), I);
        ResolveStage(st, s_iniPath);
        if (!st.ok) Log("[cycle] stage %d: file not found: %ls\n", k, st.path.c_str());
        s_cfg.stages.push_back(st);
    }
    const int cur = (int)GetPrivateProfileIntW(S, L"current", 0, I);
    s_saved = (cur >= 1 && cur <= (int)s_cfg.stages.size()) ? cur - 1 : -1;
    if (count > 0)
        Log("[cycle] %d stage(s) from %ls, %s\n", (int)s_cfg.stages.size(), I,
            s_cfg.enabled ? "enabled" : "disabled");
}

void CycleOverride(float dwellSec, int order, unsigned seed, int startStage1Based,
                   bool forceEnable, float jitter) {
    if (dwellSec > 0.0f) s_dwellOverride = dwellSec;
    if (jitter >= 0.0f) s_jitterOverride = jitter;
    if (order >= 0) { s_orderOverride = order; s_cfg.order = order; }
    if (seed) s_seedOverride = seed;
    if (startStage1Based >= 1) s_startStage = startStage1Based - 1;
    if (forceEnable) s_forceEnable = true;
}

bool CycleBoot(FluidConfig& cfg) {
    if (s_orderOverride >= 0) s_cfg.order = s_orderOverride;
    if (s_forceEnable) s_cfg.enabled = true;
    if (!s_cfg.enabled || OkCount() == 0) return false;
    CaptureUserShell();
    SeedRng();
    int start = -1;
    if (Valid(s_startStage)) start = s_startStage;
    else if (Valid(s_saved)) start = s_saved;
    else start = PickNext();                 // s_cur = -1: fixed -> first, random -> a look
    // an overlay needs a look under it: boot on the next look stage instead
    for (int k = 0; k < (int)s_cfg.stages.size() && IsOverlay(start); k++)
        start = (start + 1) % (int)s_cfg.stages.size();
    if (!Valid(start) || IsOverlay(start)) return false;
    FluidConfig c;
    float peak;
    int gamut;
    Compose(start, cfg, c, peak, gamut);
    cfg = c;
    g_hdrPeakNits = peak;
    g_gamutMode = gamut;
    s_cur = start;
    s_next = -1;
    s_phase = CYCLE_WARMUP;                  // the first frame is a fresh sim anyway
    s_phaseT = 0.0f;
    s_warmSim = 0.0f;
    s_fade = 0.0f;
    g_cycleActive = true;
    JourneyDetach();
    PersistCurrent();
    Log("[cycle] boot on stage %d/%d %ls (%s), warm-up %.1f sim s\n",
        start + 1, (int)s_cfg.stages.size(), s_cfg.stages[start].name.c_str(),
        LookName(s_cfg.stages[start].look), WarmupOf(start));
    return true;
}

CycleFrame CycleTick(FluidRenderer& r, float dt) {
    CycleFrame f;
    f.stepDt = 1.0f / s_cfg.warmupStepHz;
    if (dt < 0.0f) dt = 0.0f;
    s_clock += dt;
    AnimatorsTick();
    if (s_phase == CYCLE_OFF) { s_fade = 1.0f; return f; }
    const bool held = s_frozen[ANIM_TRANSITION];

    // advance the clock of the phase we are in
    switch (s_phase) {
    case CYCLE_DWELL: {
        // the stage's journey (fluid only); the transition hold freezes it
        if (JourneyActive()) {
            if (!held) JourneyUpdate(r, dt);
        } else if (s_journeyWasActive) {
            // ended in place: its held command goes back (never stays locked)
            s_journeyWasActive = false;
            r.ReleaseHueShift(false);
            Log("[cycle] journey ended; hue released\n");
        }
        if (s_paused) break;                 // Settings open: the dwell timer holds
        s_dwellT += dt;
        bool early = false;
        if (IsFluid(s_cur)) {                // fluid LOOK stages only (not overlays)
            // the conductor's dark-screen trigger: past min dwell, the field
            // has been >= early_switch_darkpct dark for 10 s -> go now
            if (r.CoverageDarkPct() >= s_cfg.earlyDarkPct) {
                if (s_darkSince < 0.0f) s_darkSince = s_dwellT;
            } else {
                s_darkSince = -1.0f;
            }
            early = s_dwellT >= s_cfg.minDwellSec && s_darkSince >= 0.0f &&
                    s_dwellT - s_darkSince >= 10.0f;
        }
        if (s_dwellT >= s_dwellTarget || early) {
            const int n = PickNext();
            if (early) Log("[cycle] dark-screen trigger after %.0f s\n", s_dwellT);
            if (Valid(n) && n != s_cur) BeginSwitch(r, n);
            else { s_dwellT = 0.0f; s_darkSince = -1.0f; }   // nowhere to go: dwell again
        }
        break;
    }
    case CYCLE_FADE_OUT: {
        s_phaseT += dt;
        const float fo = FadeOutOf(Valid(s_next) ? s_next : s_cur);
        if (fo <= 0.0f || s_phaseT >= fo) {
            if (s_soft) SoftPoint(r);
            else        BlackPoint(r);
        }
        break;
    }
    case CYCLE_FADE_IN: {
        s_phaseT += dt;
        const float fi = FadeInOf(s_cur);
        if (fi <= 0.0f || s_phaseT >= fi) EnterDwell(r, s_cur);
        break;
    }
    case CYCLE_LERP: {
        if (held) break;                     // transition hold: stays mid-lerp
        s_phaseT += dt;
        const float t = fminf(1.0f, s_phaseT / LerpOf(s_next));
        LerpLook(r.Config(), s_from, s_target, Smooth(t));
        if (s_lerpSub == LERP_SHIFT && t >= 0.4f) {
            FlipDiscrete(r, s_target);
            s_lerpSub = LERP_EMIT;
        } else if (s_lerpSub == LERP_EMIT && t >= 0.7f) {
            r.ReleaseHueShift(true);
            s_lerpSub = LERP_RETURN;
        } else if (s_lerpSub == LERP_RETURN && t >= 1.0f) {
            FinishLerp(r);
        }
        break;
    }
    default: break;
    }

    // what this frame shows
    switch (s_phase) {
    case CYCLE_FADE_OUT: {
        const float fo = FadeOutOf(Valid(s_next) ? s_next : s_cur);
        s_fade = s_fadeFrom * (1.0f - Smooth(fo > 0.0f ? s_phaseT / fo : 1.0f));
        break;
    }
    case CYCLE_WARMUP: {
        s_fade = 0.0f;
        f.extraSteps = s_cfg.warmupSteps - 1;
        s_warmSim += dt + f.extraSteps * f.stepDt;   // Frame() steps dt too
        const float need = WarmupOf(s_cur);
        const bool hueHome = !r.HueReturning();
        if (s_warmSim >= need && (hueHome || s_warmSim >= need + kWarmupExtraCap)) {
            r.PrecompilePostPsos();          // boot path: never compile on a visible frame
            Log("[cycle] warm-up done: %.2f sim s (need %.1f)%s -> fade in %.2f s\n",
                s_warmSim, need, hueHome ? "" : " [hue glide cap hit]", FadeInOf(s_cur));
            s_phase = CYCLE_FADE_IN;         // this frame is still black
            s_phaseT = 0.0f;
        }
        f.fade = 0.0f;
        f.black = true;
        return f;
    }
    case CYCLE_FADE_IN: {
        const float fi = FadeInOf(s_cur);
        s_fade = Smooth(fi > 0.0f ? s_phaseT / fi : 1.0f);
        break;
    }
    default:
        s_fade = 1.0f;                       // DWELL, LERP (no black), OFF
        break;
    }
    f.fade = s_fade;
    f.black = s_fade < 0.01f;               // below 0.01: black, not a dim frame
    return f;
}

bool CycleCoverageWanted() {
    return s_phase != CYCLE_OFF && (IsFluid(LookStage(s_cur)) || s_phase == CYCLE_LERP);
}

bool*  CycleUiEnabledPtr() { return &s_cfg.enabled; }
float* CycleUiDwellPtr()   { return &s_cfg.dwellSec; }
float* CycleUiLerpPtr()    { return &s_cfg.lerpSec; }
float* CycleUiJitterPtr()  { return &s_cfg.jitter; }

void CycleManualOverride(const char* why) {
    if (s_phase == CYCLE_OFF) return;
    if (g_renderer) DetachJourney(*g_renderer);
    GoOff(why);                              // session only: [cycle] enabled untouched
}

void CycleNext() {
    if (!g_renderer || s_phase == CYCLE_OFF) return;
    const int n = PickNext();
    Log("[cycle] next -> %d\n", n + 1);
    if (Valid(n) && n != s_cur) BeginSwitch(*g_renderer, n);
}

void CyclePrev() {
    if (!g_renderer || s_phase == CYCLE_OFF) return;
    const int n = PickPrev();
    Log("[cycle] previous -> %d\n", n + 1);
    if (Valid(n) && n != s_cur) BeginSwitch(*g_renderer, n);
}

void CycleJump(int stage) {
    if (!g_renderer || !Valid(stage)) return;
    if (s_phase == CYCLE_OFF) {
        CycleSetEnabled(true, false);
        if (s_phase == CYCLE_OFF) return;
    }
    if (stage == s_cur && s_phase == CYCLE_DWELL) return;
    Log("[cycle] jump -> %d\n", stage + 1);
    BeginSwitch(*g_renderer, stage);
}

void CycleSetEnabled(bool on, bool persist) {
    if (on) {
        s_stopAfter = false;
        s_cfg.enabled = true;
        if (s_phase == CYCLE_OFF && g_renderer && OkCount() > 0) {
            CaptureUserShell();
            if (!s_rngSeeded) SeedRng();
            // whatever is on screen is not a stage: leave it by a fade
            const int start = Valid(s_saved) && s_cur < 0 ? s_saved : PickNext();
            s_cur = -1;
            if (Valid(start)) {
                g_cycleActive = true;
                s_phase = CYCLE_DWELL;
                s_dwellT = 0.0f;
                Log("[cycle] on\n");
                BeginSwitch(*g_renderer, start);
            }
        }
    } else {
        s_cfg.enabled = false;
        if (s_phase == CYCLE_DWELL) {
            if (g_renderer) DetachJourney(*g_renderer);
            GoOff("turned off");
        } else if (s_phase != CYCLE_OFF) {
            s_stopAfter = true;                  // land the transition first
            Log("[cycle] turning off after this transition\n");
        }
    }
    if (persist && !g_configReadOnly && g_iniPath[0])
        WritePrivateProfileStringW(L"cycle", L"enabled", on ? L"1" : L"0", g_iniPath);
}

std::wstring CycleStageLabel(int i) {
    if (i < 0 || i >= (int)s_cfg.stages.size()) return L"";
    const CycleStage& st = s_cfg.stages[i];
    wchar_t buf[512];
    _snwprintf_s(buf, _TRUNCATE, L"%d. %s (%s)%s", i + 1, st.name.c_str(),
               st.overlay ? L"overlay"
                          : (st.look == CYCLE_LOOK_ACID ? L"oil" : (st.look == CYCLE_LOOK_INK ? L"ink" : L"WE fluid")),
               st.ok ? L"" : L" [missing]");
    return buf;
}

const CycleConfig& CycleGet() { return s_cfg; }

void CycleSet(const CycleConfig& c) {
    const bool wasEnabled = s_cfg.enabled;
    s_cfg.loop = c.loop;
    s_cfg.order = c.order;
    s_cfg.seed = c.seed;
    s_cfg.fadeOutSec = c.fadeOutSec;
    s_cfg.fadeInSec = c.fadeInSec;
    s_cfg.warmupSteps = c.warmupSteps < 1 ? 1 : (c.warmupSteps > 32 ? 32 : c.warmupSteps);
    s_cfg.warmupStepHz = c.warmupStepHz < 30.0f ? 30.0f : c.warmupStepHz;
    s_cfg.lerpSec = fmaxf(c.lerpSec, 1.0f);
    s_cfg.dwellSec = fminf(fmaxf(c.dwellSec, 1.0f), kMaxDwellSec);
    s_cfg.jitter = fminf(fmaxf(c.jitter, 0.0f), 0.9f);
    s_cfg.earlyDarkPct = c.earlyDarkPct;
    s_cfg.minDwellSec = c.minDwellSec;
    s_cfg.stages = c.stages;
    const std::wstring base = s_iniPath.empty() ? std::wstring(g_iniPath) : s_iniPath;
    for (auto& st : s_cfg.stages) ResolveStage(st, base);
    if (!Valid(s_cur)) s_cur = -1;
    if (!Valid(s_next)) s_next = -1;
    s_history.clear();
    // persist the whole [cycle] section (settings.ini only, never a stage file)
    if (!g_configReadOnly && g_iniPath[0] && _wcsicmp(base.c_str(), g_iniPath) == 0) {
        const wchar_t* P = g_iniPath;
        WritePrivateProfileStringW(L"cycle", nullptr, nullptr, P);   // drop the old section
        wchar_t v[64], key[48];
        auto putS = [&](const wchar_t* k, const wchar_t* s) { WritePrivateProfileStringW(L"cycle", k, s, P); };
        auto putF = [&](const wchar_t* k, float x) { swprintf_s(v, L"%g", x); putS(k, v); };
        auto putI = [&](const wchar_t* k, int x) { swprintf_s(v, L"%d", x); putS(k, v); };
        putI(L"enabled", c.enabled ? 1 : 0);
        putI(L"loop", s_cfg.loop ? 1 : 0);
        putS(L"order", s_cfg.order == CYCLE_ORDER_FIXED ? L"fixed" : L"alternate_random");
        putI(L"seed", (int)s_cfg.seed);
        putF(L"fade_out", s_cfg.fadeOutSec);
        putF(L"fade_in", s_cfg.fadeInSec);
        putI(L"warmup_steps", s_cfg.warmupSteps);
        putF(L"warmup_step_hz", s_cfg.warmupStepHz);
        putF(L"lerp", s_cfg.lerpSec);
        putF(L"dwell", s_cfg.dwellSec);
        putF(L"jitter", s_cfg.jitter);
        putF(L"early_switch_darkpct", s_cfg.earlyDarkPct);
        putF(L"min_dwell", s_cfg.minDwellSec);
        putI(L"stage_count", (int)s_cfg.stages.size());
        for (int k = 0; k < (int)s_cfg.stages.size(); k++) {
            const CycleStage& st = s_cfg.stages[k];
            auto K = [&](const wchar_t* suffix) { swprintf_s(key, L"stage_%d_%s", k + 1, suffix); return key; };
            putS(K(L"file"), st.file.c_str());
            if (!st.base.empty()) putS(K(L"base"), st.base.c_str());
            if (st.dwellSec > 0.0f) putF(K(L"dwell"), fminf(st.dwellSec, kMaxDwellSec));
            if (st.overlay) putS(K(L"overlay"), L"1");
            if (st.transition != CYCLE_TR_DEFAULT)
                putS(K(L"transition"), st.transition == CYCLE_TR_CUT ? L"cut"
                                     : (st.transition == CYCLE_TR_LERP ? L"lerp" : L"fade"));
            if (st.fadeOutSec >= 0.0f) putF(K(L"fade_out"), st.fadeOutSec);
            if (st.fadeInSec >= 0.0f)  putF(K(L"fade_in"), st.fadeInSec);
            if (st.warmupSec >= 0.0f)  putF(K(L"warmup"), st.warmupSec);
            if (st.jitter >= 0.0f)     putF(K(L"jitter"), st.jitter);
            if (st.weight != 1.0f)     putF(K(L"weight"), st.weight);
            if (st.lerpSec >= 0.0f)    putF(K(L"lerp"), st.lerpSec);
            if (!st.journey.empty())   putS(K(L"journey"), st.journey.c_str());
        }
        if (s_cur >= 0) putI(L"current", s_cur + 1);
    }
    if (c.enabled != wasEnabled) CycleSetEnabled(c.enabled, false);
}

CycleStatus CycleState() {
    CycleStatus s;
    s.stage = s_cur;
    s.next = (s_phase == CYCLE_FADE_OUT || s_phase == CYCLE_LERP) ? s_next : -1;
    s.phase = s_phase;
    s.fade = s_fade;
    s.transitioning = s_phase == CYCLE_FADE_OUT || s_phase == CYCLE_WARMUP ||
                      s_phase == CYCLE_FADE_IN || s_phase == CYCLE_LERP;
    s.warmSimSec = s_warmSim;
    switch (s_phase) {
    case CYCLE_DWELL:    s.remainingSec = fmaxf(s_dwellTarget - s_dwellT, 0.0f); break;
    case CYCLE_FADE_OUT: s.remainingSec = fmaxf(FadeOutOf(Valid(s_next) ? s_next : s_cur) - s_phaseT, 0.0f); break;
    case CYCLE_WARMUP:   s.remainingSec = 0.0f; break;   // sim-time bound, not wall time
    case CYCLE_FADE_IN:  s.remainingSec = fmaxf(FadeInOf(s_cur) - s_phaseT, 0.0f); break;
    case CYCLE_LERP:     s.remainingSec = fmaxf(LerpOf(s_next) - s_phaseT, 0.0f); break;
    default: break;
    }
    return s;
}

// Shot-log helper: one line describing where the director is.
void CycleDescribe(char* out, size_t cap) {
    const CycleStatus s = CycleState();
    _snprintf_s(out, cap, _TRUNCATE, "stage=%d(%ls,%s) phase=%s fade=%.4f warm=%.2f paused=%d",
                s.stage + 1, Valid(s.stage) ? s_cfg.stages[s.stage].name.c_str() : L"-",
                Valid(s.stage) ? LookName(s_cfg.stages[s.stage].look) : "-",
                PhaseName(s.phase), s.fade, s.warmSimSec, s_paused ? 1 : 0);
}

// ===========================================================================
// animators.h
// ===========================================================================

void CyclePause(float autoResumeSec) {
    if (!s_paused) Log("[cycle] paused (dwell timer held, auto-resume %.0f s)\n", autoResumeSec);
    s_paused = true;
    s_pauseUntil = s_clock + (double)fmaxf(autoResumeSec, 1.0f);
}
void CycleResume() {
    if (s_paused) Log("[cycle] resumed\n");
    s_paused = false;
}
bool CyclePaused() { return s_paused; }
float CyclePauseRemainingSec() { return s_paused ? (float)(s_pauseUntil - s_clock) : 0.0f; }

bool CycleStageBase(FluidConfig& out) {
    if (s_phase == CYCLE_OFF || !g_renderer) return false;
    const int i = (s_phase == CYCLE_LERP && Valid(s_next)) ? s_next : s_cur;
    if (!Valid(i)) return false;
    float peak;
    int gamut;
    Compose(i, g_renderer->Config(), out, peak, gamut);
    return true;
}

std::wstring CycleStageFile() {
    if (s_phase == CYCLE_OFF || !Valid(s_cur)) return L"";
    return s_cfg.stages[s_cur].path;
}

void CycleRevertStage() {
    if (s_phase != CYCLE_DWELL || !g_renderer || !Valid(s_cur)) return;
    FluidConfig c;
    float peak;
    int gamut;
    Compose(s_cur, g_renderer->Config(), c, peak, gamut);
    g_renderer->Config() = c;
    g_hdrPeakNits = peak;
    g_gamutMode = gamut;
    g_renderer->ReinitWanderers();
    g_renderer->EnsureLookResources();
    Log("[cycle] stage %d reverted to its composed base\n", s_cur + 1);
}

bool CycleHasStage(const std::wstring& file) {
    const std::wstring base = s_iniPath.empty() ? std::wstring(g_iniPath) : s_iniPath;
    const std::wstring abs = Resolve(file, base);
    for (auto& st : s_cfg.stages)
        if (_wcsicmp(st.path.c_str(), abs.c_str()) == 0) return true;
    return false;
}

void CycleSetStageIncluded(const std::wstring& file, bool included, float dwellSec) {
    const std::wstring base = s_iniPath.empty() ? std::wstring(g_iniPath) : s_iniPath;
    const std::wstring abs = Resolve(file, base);
    CycleConfig c = s_cfg;
    bool changed = false;
    if (included) {
        if (!CycleHasStage(file)) {
            CycleStage st;
            st.file = file;
            st.dwellSec = fminf(dwellSec, kMaxDwellSec);
            // a preset without [look] (the Mirror overlays) is an overlay stage
            wchar_t sec[64] = {};
            st.overlay = GetPrivateProfileSectionW(L"look", sec, 64, abs.c_str()) == 0;
            c.stages.push_back(st);
            changed = true;
        }
    } else {
        for (size_t k = 0; k < c.stages.size();) {
            if (_wcsicmp(c.stages[k].path.c_str(), abs.c_str()) == 0) {
                c.stages.erase(c.stages.begin() + k);
                changed = true;
            } else {
                k++;
            }
        }
    }
    if (changed) CycleSet(c);
}

void Freeze(Animator a, float autoReleaseSec) {
    if (a < 0 || a >= ANIM_COUNT) return;
    if (a == ANIM_CYCLE) { CyclePause(autoReleaseSec); return; }
    if (!s_frozen[a]) Log("[anim] freeze %ls (auto-release %.0f s)\n", AnimatorName(a), autoReleaseSec);
    s_frozen[a] = true;
    s_frozenUntil[a] = s_clock + (double)fmaxf(autoReleaseSec, 1.0f);
    ApplyFreezeToRenderer(a, true);
}

void Unfreeze(Animator a) {
    if (a < 0 || a >= ANIM_COUNT) return;
    if (a == ANIM_CYCLE) { CycleResume(); return; }
    if (s_frozen[a]) Log("[anim] unfreeze %ls\n", AnimatorName(a));
    s_frozen[a] = false;
    ApplyFreezeToRenderer(a, false);
}

void UnfreezeAll() {
    for (int a = 0; a < ANIM_COUNT; a++) Unfreeze((Animator)a);
}

bool IsFrozen(Animator a) {
    if (a == ANIM_CYCLE) return s_paused;
    return a >= 0 && a < ANIM_COUNT && s_frozen[a];
}

float FrozenRemainingSec(Animator a) {
    if (a == ANIM_CYCLE) return CyclePauseRemainingSec();
    if (a < 0 || a >= ANIM_COUNT || !s_frozen[a]) return 0.0f;
    return (float)(s_frozenUntil[a] - s_clock);
}

const wchar_t* AnimatorName(Animator a) {
    switch (a) {
    case ANIM_PALETTE:    return L"Hue rotation / palette sweep";
    case ANIM_HUE2:       return L"Second hue swing";
    case ANIM_RIG:        return L"Camera rig";
    case ANIM_HUE_SHIFT:  return L"Fluid hue-shift bursts";
    case ANIM_TRANSITION: return L"Stage transition + journey";
    case ANIM_CYCLE:      return L"Cycle dwell timer";
    default:              return L"?";
    }
}

void AnimatorsTick() {
    for (int a = 0; a < ANIM_COUNT; a++)
        if (a != ANIM_CYCLE && s_frozen[a] && s_clock >= s_frozenUntil[a]) {
            Log("[anim] %ls auto-released\n", AnimatorName((Animator)a));
            Unfreeze((Animator)a);
        }
    if (s_paused && s_clock >= s_pauseUntil) {
        Log("[cycle] pause auto-released\n");
        s_paused = false;
    }
}
