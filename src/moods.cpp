// Mood conductor implementation. See moods.h for the phase overview.
// Moods are partial ini overlays loaded over the LIVE config, so any key a
// mood doesn't specify simply keeps its current value. simRes/dyeRes/
// fpsLimit/mirrorSecond are always stripped: moods never touch them.

#include "moods.h"
#include "journey.h"
#include "app_state.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <set>
#include <unordered_set>

MoodSettings g_moodSettings;

namespace {

struct MoodEntry { std::wstring name, path; };

std::vector<MoodEntry>  s_moods;
std::vector<std::wstring> s_names;
int   s_current = -1, s_next = -1;
enum  Phase { DWELL, SHIFT, EMIT, RETURN } s_phase = DWELL;
float s_phaseT = 0.0f;
float s_dwellElapsed = 0.0f, s_dwellTarget = 60.0f;
float s_darkSince = -1.0f;   // dwell time when sustained-dark streak began
FluidConfig s_from, s_target;

std::set<std::wstring> s_skipNames;              // [moods] skip=name1;name2
std::unordered_set<std::wstring> s_lockKeys;     // "section/key" present in current mood file
FluidConfig s_moodCfg;                           // current mood resolved over live config
bool s_moodCfgValid = false;

void ScanMoods();   // defined below; RescanKeepCurrent uses it

float L(float a, float b, float t) { return a + (b - a) * t; }

// Whitelisted look floats — lerped across the whole transition. Everything
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

// Discrete look fields — flipped at the transition midpoint.
void FlipDiscrete(FluidRenderer& r) {
    FluidConfig& c = r.Config();
    const FluidConfig& t = s_target;
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

void BeginTransition(FluidRenderer& r, int next) {
    // leaving a journey mood: drop the journey and hand its held hue command
    // back — the bridge below re-commands the angle as usual
    if (JourneyActive()) {
        JourneyDetach();
        r.ReleaseHueShift(false);
    }
    s_next = next;
    r.SetCoverageWanted(true);   // the bridge angle needs a fresh field hue
    s_from = r.Config();
    s_target = s_from;
    LoadConfigFromFile(s_moods[next].path.c_str(), s_target);
    // moods never touch resolution, fps, mirror, or debug switches
    s_target.simRes = s_from.simRes;
    s_target.dyeRes = s_from.dyeRes;
    s_target.fpsLimit = s_from.fpsLimit;
    s_target.mirrorSecond = s_from.mirrorSecond;

    // Bridge angle: rotate the visible field so it lands on the next mood's
    // hue center (full-wheel targets just get a gentle +60° drift). Dye hue
    // drifts during the transition anyway — this is choreography, not math.
    float dyeHue = r.FieldAvgHueDeg();
    float curAng = r.HueAngleDeg();
    float targetVisible = s_target.hueRange < 179.0f ? s_target.hueCenter
                                                     : dyeHue + curAng + s_target.postHue + 60.0f;
    float targetAng = fmodf(targetVisible - dyeHue - s_target.postHue + 720.0f, 360.0f);
    float cmd = curAng + fmodf(targetAng - fmodf(curAng + 720.0f, 360.0f) + 720.0f, 360.0f);
    r.CommandHueShift(cmd, g_moodSettings.transitionSec * 0.4f);

    s_phase = SHIFT;
    s_phaseT = 0.0f;
    printf("[moods] transition: %ls -> %ls (bridge %.0f deg)\n",
           s_current >= 0 ? s_names[s_current].c_str() : L"?",
           s_names[next].c_str(),
           fmodf(cmd - curAng + 720.0f, 360.0f));
}

// Per-mood HDR peak: if the mood ini carries [hdr] peak_nits it overrides the
// shell value for the dwell; otherwise restore the user's settings.ini value.
// Session-only (matches ApplyPreset semantics — never written back here).
void ApplyMoodPeakNits(const wchar_t* moodPath) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"hdr", L"peak_nits", L"", buf, 64, moodPath);
    if (buf[0]) {
        g_hdrPeakNits = (float)_wtof(buf);
    } else {
        GetPrivateProfileStringW(L"hdr", L"peak_nits", L"-1", buf, 64, g_configIniPath);
        g_hdrPeakNits = (float)_wtof(buf);
    }
}

void FinishTransition(FluidRenderer& r) {
    r.Config() = s_target;
    s_current = s_next;
    s_phase = DWELL;
    s_dwellElapsed = 0.0f;
    s_darkSince = -1.0f;
    float j = g_moodSettings.jitter;
    s_dwellTarget = g_moodSettings.dwellMinutes * 60.0f *
                    (1.0f - j + 2.0f * j * ((float)rand() / RAND_MAX));
    printf("[moods] now dwelling in %ls (%.0f s)\n",
           s_names[s_current].c_str(),
           s_dwellTarget);
    // journey opt-in: the new mood's [journey] file= decides (empty -> detach)
    JourneyAttach(s_moods[s_current].path.c_str());
    ApplyMoodPeakNits(s_moods[s_current].path.c_str());
}

void MoodsDir(wchar_t out[MAX_PATH]) {
    wcscpy_s(out, MAX_PATH, g_iniPath);
    wchar_t* sl = wcsrchr(out, L'\\');
    if (sl) *(sl + 1) = 0;
    wcscat_s(out, MAX_PATH, L"moods");
}

std::wstring Lower(std::wstring r) {
    for (auto& ch : r) ch = (wchar_t)towlower(ch);
    return r;
}

// next mood in cycle order that isn't on the skip list; returns `from` when
// every other mood is skipped (caller then stays put)
int NextUnskipped(int from) {
    int n = (int)s_moods.size();
    for (int step = 1; step < n; step++) {
        int cand = (from + step) % n;
        if (!s_skipNames.count(s_moods[cand].name)) return cand;
    }
    return from;
}

void LoadSkipList() {
    s_skipNames.clear();
    wchar_t buf[4096];
    DWORD n = GetPrivateProfileStringW(L"moods", L"skip", L"", buf, 4096, g_configIniPath);
    std::wstring cur;
    for (DWORD i = 0; i < n; i++) {
        if (buf[i] == L';') {
            if (!cur.empty()) s_skipNames.insert(cur);
            cur.clear();
        } else {
            cur += buf[i];
        }
    }
    if (!cur.empty()) s_skipNames.insert(cur);
}

void PersistSkipList() {
    std::wstring joined;
    for (const auto& nm : s_skipNames) {
        if (!joined.empty()) joined += L';';
        joined += nm;
    }
    if (g_configReadOnly) return;   // --shot: never touch the live config
    WritePrivateProfileStringW(L"moods", L"skip", joined.c_str(), g_iniPath);
}

// lock cache: every "section/key" the current mood's ini actually specifies
void RefreshLockCache() {
    s_lockKeys.clear();
    if (s_current < 0 || s_current >= (int)s_moods.size()) return;
    const std::wstring& path = s_moods[s_current].path;
    wchar_t sections[4096];
    DWORD ns = GetPrivateProfileSectionNamesW(sections, 4096, path.c_str());
    for (DWORD off = 0; off + 1 < ns; ) {
        const wchar_t* sec = sections + off;
        off += (DWORD)wcslen(sec) + 1;
        wchar_t kvbuf[16384];
        DWORD nk = GetPrivateProfileSectionW(sec, kvbuf, 16384, path.c_str());
        for (DWORD ko = 0; ko + 1 < nk; ) {
            const wchar_t* kv = kvbuf + ko;
            ko += (DWORD)wcslen(kv) + 1;
            const wchar_t* eq = wcschr(kv, L'=');
            if (eq && eq != kv)
                s_lockKeys.insert(Lower(sec) + L"/" + Lower(std::wstring(kv, eq - kv)));
        }
    }
}

// re-scan the moods dir, keeping the current mood selected (by name — the
// sorted order shifts as files come and go)
void RescanKeepCurrent() {
    std::wstring cur;
    if (s_current >= 0 && s_current < (int)s_moods.size()) cur = s_moods[s_current].name;
    ScanMoods();
    s_current = -1;
    for (int i = 0; i < (int)s_moods.size(); i++)
        if (s_moods[i].name == cur) { s_current = i; break; }
    if (s_current < 0 && !s_moods.empty()) s_current = 0;
    if (s_next >= (int)s_moods.size()) s_next = -1;
}

// one-time fold of the legacy presets folder into the moods folder
void MigratePresetsOnce() {
    if (g_configReadOnly) return;   // --shot: no file copies
    if (GetPrivateProfileIntW(L"moods", L"migrated", 0, g_configIniPath)) return;
    wchar_t pdir[MAX_PATH], mdir[MAX_PATH], pattern[MAX_PATH];
    wcscpy_s(pdir, MAX_PATH, g_iniPath);
    wchar_t* sl = wcsrchr(pdir, L'\\');
    if (sl) *(sl + 1) = 0;
    wcscat_s(pdir, MAX_PATH, L"presets");
    MoodsDir(mdir);
    CreateDirectoryW(mdir, nullptr);
    swprintf_s(pattern, L"%s\\*.ini", pdir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    int copied = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring src = std::wstring(pdir) + L"\\" + fd.cFileName;
            std::wstring dst = std::wstring(mdir) + L"\\" + fd.cFileName;
            if (CopyFileW(src.c_str(), dst.c_str(), TRUE)) copied++;   // skip existing names
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    if (!g_configReadOnly) WritePrivateProfileStringW(L"moods", L"migrated", L"1", g_iniPath);
    if (copied) printf("[moods] migrated %d preset(s) into the moods folder\n", copied);
}

void WriteFileIfMissing(const wchar_t* path, const char* text) {
    if (g_configReadOnly) return;   // --shot: ship nothing, write nothing
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, text, (DWORD)strlen(text), &w, nullptr);
    CloseHandle(f);
}

const char* kNeonIni =
    "; Neon mood - vibrant, punchy, hot HDR cores. Quiz-calibrated 2026-07-24.\r\n"
    "[sim]\r\ndensity_diffusion=0.9990\r\ndecay_threshold=0.290\r\ndecay_fast=1.000\r\n"
    "velocity_diffusion=0.9990\r\npressure_diffusion=0.850\r\npressure_iterations=20\r\n"
    "saturation_restore=0.930\r\nmax_brightness=1.35\r\nvorticity=24.0\r\nsplat_radius=0.630\r\n"
    "dye_diffusion=0.305\r\n"
    "[hdr]\r\nknee=0.98\r\nsaturation=1.20\r\nbrightness=1.08\r\ncontrast=1.04\r\ncompensation=1\r\n"
    "[behavior]\r\ncolor_cycle_period=10\r\nwanderers=1\r\nwanderer_count=3\r\nwanderer_mode=0\r\n"
    "wanderer_speed=246\r\nwanderer_brightness=0.72\r\nwanderer_scale=0.10\r\nwanderer_resume_delay=4.5\r\n"
    "auto_pause=1\r\ndark_floor=30\r\ndark_level=0.070\r\nsurv_dark_floor=8\r\ncontrast_req=30\r\n"
    "dart_enabled=1\r\ndart_interval=7\r\ndart_speed=967\r\n"
    "hueshift_enabled=1\r\nhueshift_step=40\r\nhueshift_linger=10.0\r\nhueshift_glide=7.2\r\n"
    "hueshift_burst_steps=2\r\nhueshift_off_time=20\r\n"
    "idle_splats=1\r\nidle_interval=6.0\r\nidle_amount=3\r\n"
    "hold_to_splat=1\r\nsplat_on_click=1\r\nshow_mouse=1\r\n"
    "[color]\r\ncolorful=1\r\nmore_colors=0\r\npost_saturation=1.70\r\npost_contrast=1.34\r\n"
    "post_brightness=1.06\r\npost_hue=14\r\nhue_center=66\r\nhue_range=180\r\n"
    "shadow_floor=0.100\r\nshadow_knee=0.15\r\ncurve_enabled=0\r\n";

const char* kCloudsIni =
    "; Clouds mood - dense cream/orange marbling (WE reference look).\r\n"
    "; v2 2026-07-25: physics-weighted per user directive. Pending quiz round 2.\r\n"
    "[sim]\r\ndensity_diffusion=0.9990\r\ndecay_threshold=0.200\r\ndecay_fast=0.980\r\n"
    "velocity_diffusion=0.9990\r\npressure_diffusion=0.850\r\npressure_iterations=20\r\n"
    "saturation_restore=0.850\r\nmax_brightness=1.40\r\nvorticity=32.0\r\nsplat_radius=0.750\r\n"
    "dye_diffusion=0.400\r\n"
    "[hdr]\r\nknee=0.98\r\nsaturation=1.00\r\nbrightness=1.00\r\ncontrast=1.00\r\ncompensation=1\r\n"
    "[behavior]\r\ncolor_cycle_period=30\r\nwanderers=1\r\nwanderer_count=3\r\nwanderer_mode=0\r\n"
    "wanderer_speed=246\r\nwanderer_brightness=0.72\r\nwanderer_scale=0.10\r\nwanderer_resume_delay=4.5\r\n"
    "auto_pause=1\r\ndark_floor=30\r\ndark_level=0.070\r\nsurv_dark_floor=8\r\ncontrast_req=30\r\n"
    "dart_enabled=1\r\ndart_interval=7\r\ndart_speed=967\r\n"
    "hueshift_enabled=0\r\n"
    "idle_splats=1\r\nidle_interval=6.0\r\nidle_amount=3\r\n"
    "hold_to_splat=1\r\nsplat_on_click=1\r\nshow_mouse=1\r\n"
    "[color]\r\ncolorful=1\r\nmore_colors=0\r\npost_saturation=0.85\r\npost_contrast=1.10\r\n"
    "post_brightness=1.10\r\npost_hue=10\r\nhue_center=35\r\nhue_range=45\r\n"
    "shadow_floor=0.100\r\nshadow_knee=0.15\r\ncurve_enabled=0\r\n";

const char* kJourneyAuroraIni =
    "; Journey Aurora mood - Storm physics chassis, neutral color, journey-driven hue legs.\r\n"
    "; The [journey] section opts into journeys\\Aurora.txt: while dwelling here the\r\n"
    "; field travels that file's color-family legs instead of holding one static look.\r\n"
    "[sim]\r\ndensity_diffusion=0.9995\r\ndecay_threshold=0.300\r\ndecay_fast=1.000\r\n"
    "velocity_diffusion=0.9996\r\npressure_diffusion=0.610\r\npressure_iterations=10\r\n"
    "saturation_restore=0.770\r\nmax_brightness=1.25\r\nvorticity=20.0\r\nbaroclinic=60\r\n"
    "splat_radius=0.245\r\ndye_diffusion=0.205\r\n"
    "[hdr]\r\nknee=0.90\r\nsaturation=1.00\r\nbrightness=1.00\r\ncontrast=1.05\r\ncompensation=1\r\n"
    "[behavior]\r\ncolor_cycle_period=45\r\nwanderers=1\r\nwanderer_count=2\r\nwanderer_mode=0\r\n"
    "wanderer_speed=250\r\nwanderer_brightness=0.30\r\nwanderer_scale=0.10\r\nwanderer_resume_delay=4.5\r\n"
    "auto_pause=1\r\ndark_floor=45\r\ndark_level=0.100\r\nsurv_dark_floor=8\r\ncontrast_req=30\r\n"
    "dart_enabled=1\r\ndart_interval=7\r\ndart_speed=550\r\n"
    "hueshift_enabled=0\r\n"
    "idle_splats=1\r\nidle_interval=10.0\r\nidle_amount=2\r\nidle_brightness=0.5\r\n"
    "hold_to_splat=1\r\nsplat_on_click=1\r\nshow_mouse=1\r\n"
    "[color]\r\ncolorful=1\r\nmore_colors=0\r\npost_saturation=0.95\r\npost_contrast=1.10\r\n"
    "post_brightness=1.00\r\npost_hue=0\r\nhue_center=275\r\nhue_range=30\r\n"
    "shadow_floor=0.100\r\nshadow_knee=0.15\r\ncurve_enabled=0\r\n"
    "[journey]\r\nfile=Aurora\r\n";

const char* kJourneyDuetIni =
    "; Journey Duet mood - Storm physics chassis, neutral color, journey-driven hue legs.\r\n"
    "; The [journey] section opts into journeys\\Duet.txt: blue/purple choreography where\r\n"
    "; legs steer the emission band and the field rotation independently\r\n"
    "; (journey v2 shift legs: resultant color = emitted hue + field shift).\r\n"
    "[sim]\r\ndensity_diffusion=0.9995\r\ndecay_threshold=0.300\r\ndecay_fast=1.000\r\n"
    "velocity_diffusion=0.9996\r\npressure_diffusion=0.610\r\npressure_iterations=10\r\n"
    "saturation_restore=0.770\r\nmax_brightness=1.25\r\nvorticity=20.0\r\nbaroclinic=60\r\n"
    "splat_radius=0.245\r\ndye_diffusion=0.205\r\n"
    "[hdr]\r\nknee=0.90\r\nsaturation=1.00\r\nbrightness=1.00\r\ncontrast=1.05\r\ncompensation=1\r\n"
    "[behavior]\r\ncolor_cycle_period=45\r\nwanderers=1\r\nwanderer_count=2\r\nwanderer_mode=0\r\n"
    "wanderer_speed=250\r\nwanderer_brightness=0.30\r\nwanderer_scale=0.10\r\nwanderer_resume_delay=4.5\r\n"
    "auto_pause=1\r\ndark_floor=45\r\ndark_level=0.100\r\nsurv_dark_floor=8\r\ncontrast_req=30\r\n"
    "dart_enabled=1\r\ndart_interval=7\r\ndart_speed=550\r\n"
    "hueshift_enabled=0\r\n"
    "idle_splats=1\r\nidle_interval=10.0\r\nidle_amount=2\r\nidle_brightness=0.5\r\n"
    "hold_to_splat=1\r\nsplat_on_click=1\r\nshow_mouse=1\r\n"
    "[color]\r\ncolorful=1\r\nmore_colors=0\r\npost_saturation=0.95\r\npost_contrast=1.10\r\n"
    "post_brightness=1.00\r\npost_hue=0\r\nhue_center=257\r\nhue_range=30\r\n"
    "shadow_floor=0.100\r\nshadow_knee=0.15\r\ncurve_enabled=0\r\n"
    "[journey]\r\nfile=Duet\r\n";

void EnsureBuiltinMoods() {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    MoodsDir(dir);
    CreateDirectoryW(dir, nullptr);
    swprintf_s(path, L"%s\\Neon.ini", dir);
    WriteFileIfMissing(path, kNeonIni);
    swprintf_s(path, L"%s\\Clouds.ini", dir);
    WriteFileIfMissing(path, kCloudsIni);
    swprintf_s(path, L"%s\\Journey Aurora.ini", dir);
    WriteFileIfMissing(path, kJourneyAuroraIni);
    swprintf_s(path, L"%s\\Journey Duet.ini", dir);
    WriteFileIfMissing(path, kJourneyDuetIni);
}

void ScanMoods() {
    s_moods.clear();
    s_names.clear();
    wchar_t dir[MAX_PATH], pattern[MAX_PATH];
    MoodsDir(dir);
    swprintf_s(pattern, L"%s\\*.ini", dir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        size_t dot = name.rfind(L".ini");
        if (dot != std::wstring::npos) name.resize(dot);
        s_names.push_back(name);
        s_moods.push_back({ name, std::wstring(dir) + L"\\" + fd.cFileName });
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(s_moods.begin(), s_moods.end(),
              [](const MoodEntry& a, const MoodEntry& b) { return a.name < b.name; });
    s_names.clear();
    for (auto& m : s_moods) s_names.push_back(m.name);
}

} // namespace

void MoodsApplyBase(FluidConfig& cfg) {
    // Only when base_mood was explicitly persisted (scene-apply or manual
    // pick) — an absent key means "the user's own settings", never stomp them.
    if (s_moods.empty()) return;
    wchar_t buf[64];
    GetPrivateProfileStringW(L"moods", L"base_mood", L"", buf, 64, g_configIniPath);
    if (!buf[0]) return;
    for (int i = 0; i < (int)s_moods.size(); i++) {
        if (_wcsicmp(s_moods[i].name.c_str(), buf) != 0) continue;
        LoadConfigFromFile(s_moods[i].path.c_str(), cfg);
        printf("[moods] base mood %ls applied over settings\n", s_moods[i].name.c_str());
        return;
    }
}

void InitMoods() {
    EnsureBuiltinMoods();
    MigratePresetsOnce();
    ScanMoods();
    LoadSkipList();

    wchar_t buf[64];
    // migration: old [cycle] enabled=1 with no [moods] section -> enable moods
    GetPrivateProfileStringW(L"moods", L"enabled", L"", buf, 64, g_configIniPath);
    if (buf[0] == L'\0') {
        int oldCycle = GetPrivateProfileIntW(L"cycle", L"enabled", 0, g_configIniPath);
        if (oldCycle) {
            g_moodSettings.enabled = true;
            if (!g_configReadOnly)
                WritePrivateProfileStringW(L"moods", L"enabled", L"1", g_iniPath);
        }
    } else {
        g_moodSettings.enabled = wcstol(buf, nullptr, 10) != 0;
    }
    g_moodSettings.dwellMinutes = (float)GetPrivateProfileIntW(L"moods", L"dwell_minutes", 3, g_configIniPath);
    g_moodSettings.transitionSec = (float)GetPrivateProfileIntW(L"moods", L"transition_seconds", 4, g_configIniPath);
    GetPrivateProfileStringW(L"moods", L"jitter", L"0.3", buf, 64, g_configIniPath);
    g_moodSettings.jitter = (float)_wtof(buf);
    g_moodSettings.earlySwitchDarkPct = (float)GetPrivateProfileIntW(L"moods", L"early_switch_darkpct", 92, g_configIniPath);
    g_moodSettings.minDwellSec = (float)GetPrivateProfileIntW(L"moods", L"min_dwell_seconds", 60, g_configIniPath);

    // default to Neon when unset: the shipped live config is the Neon recipe,
    // so assuming Clouds would make the first "next mood" a no-op transition
    GetPrivateProfileStringW(L"moods", L"base_mood", L"Neon", buf, 64, g_configIniPath);
    s_current = 0;
    for (int i = 0; i < (int)s_moods.size(); i++)
        if (_wcsicmp(s_moods[i].name.c_str(), buf) == 0) { s_current = i; break; }
    // Only let the base mood override peak_nits when moods actually drive
    // the look (cycling on, or base_mood explicitly persisted). Otherwise the
    // ini's own [hdr] peak_nits is the truth — same rule as MoodsApplyBase.
    {
        wchar_t bm[64] = {};
        GetPrivateProfileStringW(L"moods", L"base_mood", L"", bm, 64, g_configIniPath);
        if (!s_moods.empty() && (g_moodSettings.enabled || bm[0]))
            ApplyMoodPeakNits(s_moods[s_current].path.c_str());
        else
            ApplyMoodPeakNits(L"");   // no mood file -> falls through to the ini value
    }
    if (s_moods.empty()) s_current = -1;
    RefreshLockCache();   // the config half of the UI cache needs the renderer;
                          // the settings window builds it via MoodsRefreshUiCache
    float j = g_moodSettings.jitter;
    s_dwellTarget = g_moodSettings.dwellMinutes * 60.0f *
                    (1.0f - j + 2.0f * j * ((float)rand() / RAND_MAX));
    // the app starts dwelling in the base mood directly (no transition), so
    // a journey-enabled base mood attaches here instead of in FinishTransition
    if (s_current >= 0) JourneyAttach(s_moods[s_current].path.c_str());
    printf("[moods] %zu mood(s), cycling %s, base: %ls\n", s_moods.size(),
           g_moodSettings.enabled ? "on" : "off",
           s_moods.empty() ? L"-" : s_moods[s_current].name.c_str());
}

void UpdateMoods(FluidRenderer& r, float dt) {
    if (s_moods.empty()) return;
    if (s_current < 0) s_current = 0;

    if (s_phase == DWELL) {
        // a running journey owns the dwell: legs glide/dwell INSTEAD of the
        // countdown, and the mood never auto-transitions out while active
        if (JourneyActive()) {
            JourneyUpdate(r, dt);
            return;
        }
        if (!g_moodSettings.enabled || s_moods.size() < 2) return;
        s_dwellElapsed += dt;
        // composition-aware early switch: the field has mostly decayed back
        // to dark (sustained 10 s) and we've dwelled long enough
        if (r.CoverageDarkPct() >= g_moodSettings.earlySwitchDarkPct) {
            if (s_darkSince < 0.0f) s_darkSince = s_dwellElapsed;
        } else {
            s_darkSince = -1.0f;
        }
        bool early = s_dwellElapsed >= g_moodSettings.minDwellSec &&
                     s_darkSince >= 0.0f && s_dwellElapsed - s_darkSince >= 10.0f;
        if (s_dwellElapsed >= s_dwellTarget || early) {
            int next = NextUnskipped(s_current);   // skipped moods are bypassed
            if (next != s_current) BeginTransition(r, next);
        }
        return;
    }

    // transition in flight (runs to completion even if cycling got disabled)
    s_phaseT += dt;
    float t = fminf(1.0f, s_phaseT / fmaxf(1.0f, g_moodSettings.transitionSec));
    float s = t * t * (3.0f - 2.0f * t);
    LerpLook(r.Config(), s_from, s_target, s);

    if (s_phase == SHIFT && t >= 0.4f) {
        FlipDiscrete(r);
        s_phase = EMIT;
    } else if (s_phase == EMIT && t >= 0.7f) {
        r.ReleaseHueShift(true);
        s_phase = RETURN;
    } else if (s_phase == RETURN && t >= 1.0f) {
        FinishTransition(r);
    }
}

void MoodsSetEnabled(bool on) {
    g_moodSettings.enabled = on;
    if (!g_configReadOnly)
        WritePrivateProfileStringW(L"moods", L"enabled", on ? L"1" : L"0", g_iniPath);
    if (g_renderer) g_renderer->SetCoverageWanted(on);
}

void MoodsNext(FluidRenderer& r) {
    if (s_moods.size() < 2) return;
    if (s_phase != DWELL) {   // finish the current transition instantly first
        r.Config() = s_target;
        r.ReleaseHueShift(false);
        FinishTransition(r);
    }
    int next = NextUnskipped(s_current);
    if (next != s_current) BeginTransition(r, next);
}

void MoodsForceMood(FluidRenderer& r, int index) {
    if (index < 0 || index >= (int)s_moods.size() || index == s_current) return;
    if (s_phase != DWELL) {
        r.Config() = s_target;
        r.ReleaseHueShift(false);
        FinishTransition(r);
    }
    if (!g_configReadOnly)
        WritePrivateProfileStringW(L"moods", L"base_mood", s_moods[index].name.c_str(), g_iniPath);
    BeginTransition(r, index);
}

int MoodsCurrentIndex() { return s_current; }
int MoodsNextIndex() { return s_phase != DWELL ? s_next : -1; }
const std::vector<std::wstring>& MoodsNames() { return s_names; }

void MoodsAdoptPath(FluidRenderer& r, const std::wstring& path) {
    // A mood file was applied outside the conductor (Looks window / tray).
    // Sync the conductor's bookkeeping so labels, dwell, and journeys match.
    for (int i = 0; i < (int)s_moods.size(); i++) {
        if (_wcsicmp(s_moods[i].path.c_str(), path.c_str()) != 0) continue;
        if (s_phase != DWELL) {   // land any in-flight transition first
            r.Config() = s_target;
            r.ReleaseHueShift(false);
        }
        s_current = i; s_next = -1; s_phase = DWELL;
        s_dwellElapsed = 0.0f; s_darkSince = -1.0f;
        float j = g_moodSettings.jitter;
        s_dwellTarget = g_moodSettings.dwellMinutes * 60.0f *
                        (1.0f - j + 2.0f * j * ((float)rand() / RAND_MAX));
        if (!g_configReadOnly)
            WritePrivateProfileStringW(L"moods", L"base_mood", s_moods[i].name.c_str(), g_iniPath);
        JourneyAttach(s_moods[i].path.c_str());   // self-detaches if no [journey]
        ApplyMoodPeakNits(s_moods[i].path.c_str());
        MoodsRefreshUiCache(r);                   // markers compare vs this mood now
        printf("[moods] adopted %ls via apply\n", s_moods[i].name.c_str());
        return;
    }
    JourneyDetach();   // applied something that's not a mood
    MoodsRefreshUiCache(r);
}

// ---------------------------------------------------------------------------
// Managed recipe folder + settings-window mood editor API

void MoodsGetDirectory(wchar_t out[MAX_PATH]) { MoodsDir(out); }

const std::wstring& MoodsCurrentName() {
    static const std::wstring kEmpty;
    if (s_current < 0 || s_current >= (int)s_names.size()) return kEmpty;
    return s_names[s_current];
}

void MoodsCurrentPath(wchar_t out[MAX_PATH]) {
    out[0] = 0;
    if (s_current >= 0 && s_current < (int)s_moods.size())
        wcscpy_s(out, MAX_PATH, s_moods[s_current].path.c_str());
}

void MoodsRescan() { RescanKeepCurrent(); }

void MoodsRefreshUiCache(FluidRenderer& r) {
    RefreshLockCache();
    s_moodCfgValid = false;
    if (s_current >= 0 && s_current < (int)s_moods.size() &&
        GetFileAttributesW(s_moods[s_current].path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        s_moodCfg = r.Config();   // absent keys compare equal to live: no false " *"
        LoadConfigFromFile(s_moods[s_current].path.c_str(), s_moodCfg);
        s_moodCfgValid = true;
    }
}

const FluidConfig* MoodsCachedConfig() { return s_moodCfgValid ? &s_moodCfg : nullptr; }

bool MoodsLocksKey(const wchar_t* section, const wchar_t* key) {
    if (!section || !key) return false;
    return s_lockKeys.count(Lower(section) + L"/" + Lower(key)) != 0;
}

bool MoodsIsSkipped(int i) {
    if (i < 0 || i >= (int)s_moods.size()) return false;
    return s_skipNames.count(s_moods[i].name) != 0;
}

void MoodsSetSkipped(int i, bool skip) {
    if (i < 0 || i >= (int)s_moods.size()) return;
    if (skip) s_skipNames.insert(s_moods[i].name);
    else      s_skipNames.erase(s_moods[i].name);
    PersistSkipList();
}

void MoodsSaveCurrent(FluidRenderer& r) {
    if (s_current < 0 || s_current >= (int)s_moods.size()) return;
    WriteConfigToIni(s_moods[s_current].path.c_str(), r.Config(), false);
    printf("[moods] saved live config into %ls\n", s_moods[s_current].name.c_str());
    MoodsRefreshUiCache(r);
}

int MoodsCreateFromLive(FluidRenderer& r) {
    wchar_t dir[MAX_PATH];
    MoodsDir(dir);
    CreateDirectoryW(dir, nullptr);
    std::wstring name, path;
    for (int n = 1; n < 1000; n++) {
        wchar_t p[MAX_PATH], nm[32];
        swprintf_s(p, L"%s\\Mood %d.ini", dir, n);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) {
            path = p;
            swprintf_s(nm, L"Mood %d", n);
            name = nm;
            break;
        }
    }
    if (path.empty()) return -1;
    WriteConfigToIni(path.c_str(), r.Config(), false);
    printf("[moods] created %ls from live config\n", name.c_str());
    RescanKeepCurrent();
    MoodsRefreshUiCache(r);
    for (int i = 0; i < (int)s_moods.size(); i++)
        if (s_moods[i].name == name) return i;
    return -1;
}

bool MoodsDeleteCurrent() {
    if (s_moods.size() <= 1) return false;   // never delete the last mood
    if (s_current < 0 || s_current >= (int)s_moods.size()) return false;
    std::wstring gone = s_moods[s_current].name;
    if (!DeleteFileW(s_moods[s_current].path.c_str())) return false;
    ScanMoods();
    if (s_moods.empty()) {
        s_current = -1;
        s_next = -1;
    } else {
        if (s_current >= (int)s_moods.size()) s_current = 0;
        if (s_next >= (int)s_moods.size()) s_next = -1;
    }
    printf("[moods] deleted %ls (%zu mood(s) left)\n", gone.c_str(), s_moods.size());
    return true;
}
