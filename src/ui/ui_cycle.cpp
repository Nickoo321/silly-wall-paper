// Adapter: settings window -> cycle director (see ui_cycle.h). The mood conductor is gone
// (absorbed into src/cycle.cpp, brief CYCLE-DIRECTOR.md SURVEY + DECISION 2026-09-25);
// the four keys.inc rows now point at the director's [cycle] values.
// Phase 1b: the playlist, the composed stage base, the pause-for-editing, the freezes and the
// derived animators' live values all go through here (animators.h), so ui_window.cpp and
// ui_model.cpp never touch the director or the renderer directly. Headless runs can script
// the director's status and the live values (UiCycleScript) for the proof shots.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include "ui_cycle.h"
#include "ui_model.h"
#include "../cycle.h"
#include "../animators.h"
#include "../journey.h"
#include "../app_state.h"

bool*  UiCycleEnabledPtr()    { return CycleUiEnabledPtr(); }
float* UiCycleDwellPtr()      { return CycleUiDwellPtr(); }
float* UiCycleTransitionPtr() { return CycleUiLerpPtr(); }
float* UiCycleJitterPtr()     { return CycleUiJitterPtr(); }

bool UiCycleIsPtr(const void* p) {
    return p == CycleUiEnabledPtr() || p == CycleUiDwellPtr() ||
           p == CycleUiLerpPtr() || p == CycleUiJitterPtr();
}

float UiCycleDefault(const void* p) {
    static const CycleConfig d{};
    if (p == CycleUiDwellPtr())   return d.dwellSec;
    if (p == CycleUiLerpPtr())    return d.lerpSec;
    if (p == CycleUiJitterPtr())  return d.jitter;
    if (p == CycleUiEnabledPtr()) return d.enabled ? 1.0f : 0.0f;
    return 0.0f;
}

namespace {
bool              s_scripted = false;       // headless: the status below replaces CycleState()
UiCycleStatusView s_scriptStatus;
bool              s_liveScripted = false;   // headless: the live values below replace the getters
UiLiveValues      s_scriptLive;
UiLiveValues      s_prevLive;               // live mode: motion = changed within the last 2 s
bool              s_havePrev = false;
ULONGLONG         s_lastMove[UA_COUNT] = {};

unsigned LookOfStage(int cycleLook) {
    return cycleLook == CYCLE_LOOK_ACID ? LOOK_A : cycleLook == CYCLE_LOOK_INK ? LOOK_I : LOOK_F;
}
bool Valid(int i) { return i >= 0 && i < (int)CycleGet().stages.size(); }
bool SamePath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
} // namespace

// persists [cycle] enabled; the director fades out to its first stage (or lands the
// current transition and lets go)
void UiCycleSetEnabled(bool on) {
    if (s_scripted && !on) { s_scripted = false; s_scriptStatus = UiCycleStatusView(); }
    CycleSetEnabled(on, true);
}
bool UiCycleOn() { return s_scripted ? s_scriptStatus.on : CycleState().phase != CYCLE_OFF; }

std::wstring UiPresetsDir() {
    wchar_t dir[MAX_PATH];
    GetPresetsDirectory(dir);   // %APPDATA%\FluidWallpaper\presets (the tray's list too)
    return dir;
}

void UiPresetsRescan() {}   // nothing caches the folder: the tray enumerates it on open

// ============================================================================ phase 1b
UiCycleStatusView UiCycleStatus() {
    if (s_scripted) return s_scriptStatus;
    UiCycleStatusView v;
    CycleStatus s = CycleState();
    v.on = s.phase != CYCLE_OFF;
    v.stage = s.stage;
    v.next = s.next;
    v.count = (int)CycleGet().stages.size();
    v.phase = s.phase;
    v.remainingSec = s.remainingSec;
    v.transitioning = s.transitioning;
    v.lerp = s.phase == CYCLE_LERP;
    v.paused = CyclePaused();
    v.pauseLeftSec = CyclePauseRemainingSec();
    return v;
}

int UiCycleStageCount() { return (int)CycleGet().stages.size(); }

UiStageInfo UiCycleStage(int i) {
    UiStageInfo o;
    if (!Valid(i)) return o;
    const CycleConfig& c = CycleGet();
    const CycleStage& st = c.stages[i];
    o.name = st.name; o.file = st.file; o.path = st.path; o.basePath = st.basePath;
    o.look = LookOfStage(st.look);
    o.overlay = st.overlay; o.ok = st.ok;
    o.ownDwell = st.dwellSec > 0.0f;
    o.dwellSec = o.ownDwell ? st.dwellSec : c.dwellSec;
    o.weight = st.weight;
    return o;
}

int  UiCycleOrder() { return CycleGet().order; }
void UiCycleSetOrder(int order) {
    CycleConfig c = CycleGet();
    c.order = order == CYCLE_ORDER_FIXED ? CYCLE_ORDER_FIXED : CYCLE_ORDER_ALT_RANDOM;
    CycleSet(c);
}
void UiCycleNext() { CycleNext(); }        // no-ops headless (no renderer)
void UiCyclePrev() { CyclePrev(); }
void UiCycleJump(int i) { CycleJump(i); }

void UiCycleSetStageDwell(int i, float sec) {
    if (!Valid(i)) return;
    CycleConfig c = CycleGet();
    c.stages[i].dwellSec = fminf(fmaxf(sec, 10.0f), 240.0f);
    CycleSet(c);
}
void UiCycleSetStageWeight(int i, float w) {
    if (!Valid(i)) return;
    CycleConfig c = CycleGet();
    c.stages[i].weight = fmaxf(w, 0.0f);
    CycleSet(c);
}
void UiCycleMoveStage(int i, int dir) {
    int j = i + dir;
    if (!Valid(i) || !Valid(j)) return;
    CycleConfig c = CycleGet();
    std::swap(c.stages[i], c.stages[j]);
    CycleSet(c);
}
void UiCycleRemoveStage(int i) {
    if (!Valid(i)) return;
    CycleConfig c = CycleGet();
    c.stages.erase(c.stages.begin() + i);
    CycleSet(c);
}
void UiCycleAddStage(const std::wstring& path, const std::wstring& base, bool overlay, float dwellSec) {
    CycleConfig c = CycleGet();
    CycleStage st;
    st.file = path;                       // absolute: resolves the same from any ini folder
    st.base = base;                       // a partial "Save as" names its base preset here
    st.overlay = overlay;
    st.dwellSec = fminf(dwellSec, 240.0f);
    c.stages.push_back(st);
    CycleSet(c);
}
bool UiCycleHasFile(const std::wstring& path) {
    for (auto& st : CycleGet().stages) if (SamePath(st.path, path)) return true;
    return false;
}
void UiCycleSetFileIncluded(const std::wstring& path, bool on) { CycleSetStageIncluded(path, on, 180.0f); }
void UiCycleReplaceFile(const std::wstring& oldPath, const std::wstring& newPath) {
    CycleConfig c = CycleGet();
    bool changed = false;
    for (auto& st : c.stages)
        if (SamePath(st.path, oldPath)) { st.file = newPath; changed = true; }
    if (changed) CycleSet(c);
}

bool UiCycleStageBase(FluidConfig& out) {
    if (!s_scripted) return CycleStageBase(out);
    if (!s_scriptStatus.on || !Valid(s_scriptStatus.stage)) return false;
    const CycleStage& st = CycleGet().stages[s_scriptStatus.stage];
    out = FluidConfig{};                  // the director's Compose, minus the live shell keys
    if (!st.basePath.empty()) LoadConfigFromFile(st.basePath.c_str(), out);
    LoadConfigFromFile(st.path.c_str(), out);
    return true;
}
std::wstring UiCycleStageFile() {
    if (!s_scripted) return CycleStageFile();
    return (s_scriptStatus.on && Valid(s_scriptStatus.stage)) ? CycleGet().stages[s_scriptStatus.stage].path
                                                              : std::wstring();
}
void UiCycleRevertStage() { if (!s_scripted) CycleRevertStage(); }

void UiCyclePauseForEditing() {
    if (s_scripted || !g_renderer) return;
    if (CycleState().phase != CYCLE_OFF) CyclePause(600.0f);   // re-armed on input
}
void UiCycleResumeEditing() {
    if (s_scripted || !g_renderer) return;
    if (CyclePaused()) CycleResume();
}
bool UiJourneyActive() { return !s_scripted && g_renderer && JourneyActive(); }

void  UiFreeze(int a) { if (a >= 0 && a < UA_COUNT) Freeze((Animator)a, 900.0f); }
void  UiUnfreeze(int a) { if (a >= 0 && a < UA_COUNT) Unfreeze((Animator)a); }
void  UiUnfreezeAll() { UnfreezeAll(); }
bool  UiIsFrozen(int a) { return a >= 0 && a < UA_COUNT && IsFrozen((Animator)a); }
float UiFrozenLeftSec(int a) { return (a >= 0 && a < UA_COUNT) ? FrozenRemainingSec((Animator)a) : 0.0f; }
std::string UiAnimatorName(int a) { return UiNarrow(AnimatorName((Animator)a)); }

UiLiveValues UiLive() {
    if (s_liveScripted) {
        UiLiveValues v = s_scriptLive;
        for (int a = 0; a < UA_COUNT; a++) if (UiIsFrozen(a)) v.moving &= ~(1u << a);
        return v;
    }
    UiLiveValues v;
    if (!g_renderer) return v;
    const FluidRenderer& r = *g_renderer;
    v.valid = true;
    r.RigState(v.rig);
    v.hue2Deg = r.Hue2Deg();
    v.hueAngleDeg = r.HueAngleDeg();
    v.paletteHueDeg = r.PaletteHueDeg();
    v.sweepPos = r.PaletteSweepPos();
    ULONGLONG now = GetTickCount64();
    if (s_havePrev) {
        bool rig = false;
        for (int k = 0; k < 8; k++) if (fabsf(v.rig[k] - s_prevLive.rig[k]) > 1e-4f) rig = true;
        if (rig) s_lastMove[UA_RIG] = now;
        if (fabsf(v.hue2Deg - s_prevLive.hue2Deg) > 0.02f) s_lastMove[UA_HUE2] = now;
        if (fabsf(v.hueAngleDeg - s_prevLive.hueAngleDeg) > 0.02f) s_lastMove[UA_HUE_SHIFT] = now;
        if (fabsf(v.paletteHueDeg - s_prevLive.paletteHueDeg) > 0.005f ||
            fabsf(v.sweepPos - s_prevLive.sweepPos) > 1e-4f) s_lastMove[UA_PALETTE] = now;
    }
    s_prevLive = v;
    s_havePrev = true;
    for (int a = 0; a < UA_COUNT; a++)
        if (s_lastMove[a] && now - s_lastMove[a] < 2000 && !UiIsFrozen(a)) v.moving |= 1u << a;
    return v;
}

void UiCycleLoadHeadless(const wchar_t* ini) { CycleLoad(ini); }

std::string UiCycleScript(const std::string& cmdIn, FluidConfig& cfg) {
    char w[4][64] = {};
    int n = sscanf_s(cmdIn.c_str(), "%63s %63s %63s %63s", w[0], 64u, w[1], 64u, w[2], 64u, w[3], 64u);
    std::string c0 = n > 0 ? w[0] : "";
    auto animOf = [](const std::string& s) {
        return s == "palette" ? UA_PALETTE : s == "hue2" ? UA_HUE2 : s == "rig" ? UA_RIG
             : s == "hueshift" ? UA_HUE_SHIFT : s == "transition" ? UA_TRANSITION
             : s == "cycle" ? UA_CYCLE : -1;
    };
    if (c0 == "cycle") {
        std::string a = n > 1 ? w[1] : "";
        if (a == "off") { s_scripted = false; s_scriptStatus = UiCycleStatusView(); return "ok"; }
        if (a != "stage" || n < 4) return "usage: cycle stage N SECONDS [paused] [lerp]";
        int st = atoi(w[2]) - 1;
        if (!Valid(st)) return "no such stage (the --ini file carries the [cycle] list)";
        s_scripted = true;
        s_scriptStatus = UiCycleStatusView();
        s_scriptStatus.on = true;
        s_scriptStatus.scripted = true;
        s_scriptStatus.stage = st;
        s_scriptStatus.count = (int)CycleGet().stages.size();
        s_scriptStatus.phase = CYCLE_DWELL;
        s_scriptStatus.remainingSec = (float)atof(w[3]);
        s_scriptStatus.paused = cmdIn.find("paused") != std::string::npos;
        s_scriptStatus.pauseLeftSec = s_scriptStatus.paused ? 600.0f : 0.0f;
        if (cmdIn.find("lerp") != std::string::npos) {
            s_scriptStatus.phase = CYCLE_LERP;
            s_scriptStatus.lerp = true;
            s_scriptStatus.transitioning = true;
        }
        // what the director does at a stage's black point: Config := the composed stage
        FluidConfig base;
        if (UiCycleStageBase(base)) {
            const FluidConfig shell = cfg;
            cfg = base;
            cfg.simRes = shell.simRes; cfg.dyeRes = shell.dyeRes;
            cfg.fpsLimit = shell.fpsLimit; cfg.mirrorSecond = shell.mirrorSecond;
            // the stage's shell globals, as the director's Compose applies them
            const CycleStage& cst = CycleGet().stages[st];
            for (const std::wstring& f : { cst.basePath, cst.path }) {
                if (f.empty()) continue;
                wchar_t b[64] = {};
                GetPrivateProfileStringW(L"hdr", L"peak_nits", L"", b, 64, f.c_str());
                if (b[0]) g_hdrPeakNits = (float)_wtof(b);
                int gm = (int)GetPrivateProfileIntW(L"hdr", L"gamut", g_gamutMode, f.c_str());
                if (gm >= 0 && gm <= 2) g_gamutMode = gm;
            }
        }
        return "ok";
    }
    if (c0 == "live") {
        if (n < 3) return "usage: live rig.focus|rig.tilt|rig.lampx|rig.lampy|hue2|hueangle|palette V [moving]";
        std::string what = w[1];
        float v = (float)atof(w[2]);
        bool moving = n > 3 && std::string(w[3]) == "moving";
        if (!s_liveScripted) {             // unscripted values sit at their bases
            s_scriptLive = UiLiveValues();
            s_scriptLive.rig[0] = cfg.post.lightX;
            s_scriptLive.rig[1] = cfg.post.lightY;
            s_scriptLive.rig[4] = cfg.post.focusTiltAngle;
            s_scriptLive.rig[5] = cfg.post.cameraFocus;
            s_scriptLive.hue2Deg = cfg.acid.filmHue2;
        }
        s_liveScripted = true;
        s_scriptLive.valid = true;
        int a = -1;
        if (what == "rig.focus")      { s_scriptLive.rig[5] = v; a = UA_RIG; }
        else if (what == "rig.tilt")  { s_scriptLive.rig[4] = v; a = UA_RIG; }
        else if (what == "rig.lampx") { s_scriptLive.rig[0] = v; a = UA_RIG; }
        else if (what == "rig.lampy") { s_scriptLive.rig[1] = v; a = UA_RIG; }
        else if (what == "hue2")      { s_scriptLive.hue2Deg = v; a = UA_HUE2; }
        else if (what == "hueangle")  { s_scriptLive.hueAngleDeg = v; a = UA_HUE_SHIFT; }
        else if (what == "palette")   { s_scriptLive.paletteHueDeg = v; a = UA_PALETTE; }
        else return "unknown live value";
        if (moving) s_scriptLive.moving |= 1u << a;
        else        s_scriptLive.moving &= ~(1u << a);
        return "ok";
    }
    if (c0 == "freeze" || c0 == "unfreeze") {
        int a = n > 1 ? animOf(w[1]) : -1;
        if (a < 0) return "unknown animator";
        if (c0 == "freeze") UiFreeze(a); else UiUnfreeze(a);
        return "ok";
    }
    return "unknown command";
}
