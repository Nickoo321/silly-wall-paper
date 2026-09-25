// The settings window's ONE adapter onto the cycle / preset-folder machinery.
//
// It forwards to the cycle director (src/cycle.h, src/animators.h); the mood conductor it
// used to wrap is gone. Nothing else in src/ui/ (keys.inc included) may call the director's
// internals directly -- the richer model (CyclePause, CycleStageBase, freezes, in-cycle
// flags) is in src/animators.h for phase 1b.
#pragma once
#include <string>

bool*  UiCycleEnabledPtr();          // [cycle] enabled   (keys.inc row pointers)
float* UiCycleDwellPtr();            // [cycle] dwell     (s, default stage dwell, max 240)
float* UiCycleTransitionPtr();       // [cycle] lerp      (s, fluid -> fluid transition)
float* UiCycleJitterPtr();           // [cycle] jitter    (+- fraction of the dwell)
bool   UiCycleIsPtr(const void* p);  // one of the four above (pointer-exception list)
float  UiCycleDefault(const void* p);// code default for one of the four
void   UiCycleSetEnabled(bool on);   // persists + starts / lets go of the director
bool   UiCycleOn();                  // the director is running

std::wstring UiPresetsDir();         // the one preset folder (%APPDATA%\FluidWallpaper\presets)
void   UiPresetsRescan();            // after a Save / Save as (no-op: nothing caches it)

// ---- phase 1b: the director + animators as the settings window sees them -------------------
// Everything below forwards to src/animators.h / src/cycle.h when the wallpaper is running.
// Headless (--ui-shot) there is no renderer: the stage list is loaded from the --ini file, and
// the status + live animator values can be SCRIPTED (UiCycleScript) so the proof shots can show
// "stage 3, 2:10 left" or "the rig is moving the focus" without a render.
#include <vector>
#include "../fluid.h"

struct UiStageInfo {
    std::wstring name, file, path, basePath;  // stem, as written, absolute, absolute base
    unsigned look = 1;                        // LOOK_F / LOOK_A / LOOK_I (ui_model.h bits)
    bool  overlay = false, ok = false;
    float dwellSec = 180.0f;                  // effective (stage's own, else [cycle] dwell)
    bool  ownDwell = false;
    float weight = 1.0f;                      // tier mode: the multiplier inside the tier
    int   tier = -1;                          // stage_N_tier: -1 none, 0 proven, 1 moderate, 2 wild
    bool  fluid = false;                      // a WE look stage (alternates; not drawn by tier)
};
struct UiCycleStatusView {
    bool  on = false;
    int   stage = -1, next = -1, count = 0;   // 0-based
    int   phase = 0;                          // CyclePhase
    float remainingSec = 0.0f;
    bool  transitioning = false, lerp = false;
    bool  paused = false;                     // dwell timer held (window open)
    float pauseLeftSec = 0.0f;
    bool  scripted = false;
};
UiCycleStatusView UiCycleStatus();
int          UiCycleStageCount();
UiStageInfo  UiCycleStage(int i);
int          UiCycleOrder();                  // 0 fixed, 1 alternate_random
void         UiCycleSetOrder(int order);
void         UiCycleNext();
void         UiCyclePrev();
void         UiCycleJump(int i);
void         UiCycleSetStageDwell(int i, float sec);
void         UiCycleSetStageWeight(int i, float w);   // tier mode: the within-tier multiplier
void         UiCycleSetStageTier(int i, int tier);    // stage_N_tier (-1 clears it)
bool         UiCycleTierMode();                       // any stage carries a tier
void         UiCycleMoveStage(int i, int dir);           // -1 up, +1 down
void         UiCycleRemoveStage(int i);
// append a stage for a preset file (absolute); base = its [meta] base (may be empty)
void         UiCycleAddStage(const std::wstring& path, const std::wstring& base, bool overlay, float dwellSec);
bool         UiCycleHasFile(const std::wstring& path);
void         UiCycleSetFileIncluded(const std::wstring& path, bool on);
void         UiCycleReplaceFile(const std::wstring& oldPath, const std::wstring& newPath);   // rename
// composed base of the current stage (what "dirty" is measured against), its file, revert
bool         UiCycleStageBase(FluidConfig& out);
std::wstring UiCycleStageFile();
void         UiCycleRevertStage();
// window open => dwell timer paused (auto-resume after 10 min without input); close => resume
void         UiCyclePauseForEditing();
void         UiCycleResumeEditing();
bool         UiJourneyActive();

// freezes (animators.h Animator ids)
enum UiAnim { UA_PALETTE = 0, UA_HUE2, UA_RIG, UA_HUE_SHIFT, UA_TRANSITION, UA_CYCLE, UA_COUNT };
void         UiFreeze(int a);                 // 15 min auto-release
void         UiUnfreeze(int a);
void         UiUnfreezeAll();
bool         UiIsFrozen(int a);
float        UiFrozenLeftSec(int a);
std::string  UiAnimatorName(int a);

// live values of the DERIVED animators (the renderer's const getters) + which are moving now
struct UiLiveValues {
    bool  valid = false;
    float rig[8] = {};        // lampX lampY axisX axisY tiltDeg focus shiftX shiftY
    float hue2Deg = 0.0f;     // film_hue2 after the wobble
    float hueAngleDeg = 0.0f; // fluid hue-shift angle
    float paletteHueDeg = 0.0f;
    float sweepPos = -1.0f;
    unsigned moving = 0;      // bit (1 << UiAnim): changed within the last ~2 s
};
UiLiveValues UiLive();

// headless only
void         UiCycleLoadHeadless(const wchar_t* ini);
// "cycle stage N SECONDS [paused] [lerp]" | "cycle off" | "live rig.focus V [moving]" |
// "live rig.tilt V" | "live hue2 V [moving]" | "live hueangle V [moving]" | "live palette V [moving]"
// "freeze palette|hue2|rig|hueshift|transition" | "unfreeze ..."; returns "ok" or an error.
std::string  UiCycleScript(const std::string& cmd, FluidConfig& cfg);
