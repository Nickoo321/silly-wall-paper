// Cycle director (brief reference/briefs/CYCLE-DIRECTOR.md, phase 1 = fade only).
//
// A stage list the app walks forever. A STAGE = one ini (full or partial),
// composed onto a declared base (FluidConfig{} defaults, or stage_N_base),
// applied IN MEMORY (never written to settings.ini), with a jittered dwell.
// The old mood conductor is ABSORBED here (brief SURVEY + DECISION
// 2026-09-25): moods.cpp and scenes.cpp are gone.
// A switch between two FLUID stages is a LERP (the conductor's transition):
//   hue bridge (CommandHueShift to the angle that lands the field on the next
//   stage's band, forward only) -> LerpLook's ~45 fluid floats on a smoothstep
//   over stage_N_lerp s (default 4) -> non-numeric fields flip at t=0.4 ->
//   the hue returns home from t=0.7 -> the full stage config at t=1.
// Every other switch (a look change, or transition=fade|cut) goes via black:
//   DWELL -> FADE_OUT (fade 1 -> 0, the frame dims through the sdrScale)
//         -> black point: apply the stage, compile its look PSO if needed,
//            ResetLookState() (clear dye/velocity, reseed acid)
//         -> WARMUP (black; the sim is sub-stepped N x per shown frame until
//            the look's measured warm-up in SIM seconds has passed and any hue
//            release glide is home)
//         -> FADE_IN (0 -> 1) -> DWELL.
// Fades below 0.01 render black (SetBlackOut) instead of a near-zero frame.
// Fluid stages also get the conductor's dark-screen trigger (past min_dwell,
// the field >= early_switch_darkpct dark for 10 s -> switch now) and may run a
// journey (stage_N_journey=<name>, or the stage file's [journey] file=).
//
// [cycle] in settings.ini (or the --ini file under --shot); enabled=0 (the
// default) leaves every code path untouched -> parity.
//   enabled=0|1  loop=1  order=alternate_random|fixed  seed=N (0 = wall clock)
//   fade_out=1.5  fade_in=2.5  warmup_steps=8  warmup_step_hz=144
//   stage_count=N  current=K (written back: resume here after a restart)
//   stage_K_file=<path, relative to the ini's folder, or absolute>
//   stage_K_base=<optional base ini under the file>
//   stage_K_dwell=<s, default 600>  stage_K_transition=cut|fade|lerp
//   stage_K_fade=<s total, split 3:5>  stage_K_fade_out / stage_K_fade_in
//   stage_K_warmup=<sim s; default = the measured per-look value>
//   stage_K_jitter=<+- fraction of dwell, default 0.3>
//   stage_K_weight=<relative draw weight in alternate_random, default 1>
//   stage_K_lerp=<s, fluid->fluid transition, default 4>
//   stage_K_journey=<journeys\<name>.txt, fluid stages only>
//   early_switch_darkpct=92  min_dwell=60  (fluid stages' dark-screen trigger)
// Phase 1: lerp exists only fluid->fluid (the absorbed conductor path); a
// transition=lerp across looks runs as a fade (logged). The generic keys.inc
// lerp is phase 2.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "fluid.h"

enum CycleLook       { CYCLE_LOOK_FLUID = 0, CYCLE_LOOK_ACID = 1, CYCLE_LOOK_INK = 2 };
enum CycleTransition { CYCLE_TR_DEFAULT = -1, CYCLE_TR_CUT = 0, CYCLE_TR_FADE = 1, CYCLE_TR_LERP = 2 };
enum CycleOrder      { CYCLE_ORDER_FIXED = 0, CYCLE_ORDER_ALT_RANDOM = 1 };
enum CyclePhase      { CYCLE_OFF = 0, CYCLE_DWELL, CYCLE_FADE_OUT, CYCLE_WARMUP, CYCLE_FADE_IN,
                       CYCLE_LERP };   // fluid -> fluid, no black

struct CycleStage {
    std::wstring file;             // as written (relative to the ini folder, or absolute)
    std::wstring base;             // optional declared base; empty = FluidConfig{} defaults
    float dwellSec   = 600.0f;
    int   transition = CYCLE_TR_DEFAULT;
    float fadeOutSec = -1.0f;      // -1 = [cycle] fade_out
    float fadeInSec  = -1.0f;      // -1 = [cycle] fade_in
    float warmupSec  = -1.0f;      // SIM seconds; -1 = the measured per-look default
    float jitter     = -1.0f;      // -1 = 0.3 (+- fraction of the dwell)
    float weight     = 1.0f;       // alternate_random: relative draw weight
    float lerpSec    = -1.0f;      // -1 = [cycle] lerp (4 s)
    std::wstring journey;          // journeys\<name>.txt; empty = the file's [journey] file=
    // resolved by CycleLoad / CycleSet (not persisted)
    std::wstring name;             // file stem, for menus and logs
    std::wstring path, basePath;   // absolute
    int   look = CYCLE_LOOK_FLUID; // from the composed config
    bool  ok = false;              // the file exists
};

struct CycleConfig {
    bool     enabled = false;
    bool     loop = true;
    int      order = CYCLE_ORDER_ALT_RANDOM;
    unsigned seed = 0;             // 0 = wall clock
    float    fadeOutSec = 1.5f;
    float    fadeInSec  = 2.5f;
    int      warmupSteps = 8;      // sim steps per shown frame while black
    float    warmupStepHz = 144.0f;
    float    lerpSec = 4.0f;       // fluid -> fluid transition length
    float    earlyDarkPct = 92.0f; // dark-screen trigger (fluid stages)
    float    minDwellSec = 60.0f;
    std::vector<CycleStage> stages;
};

struct CycleStatus {
    int   stage = -1;              // 0-based current stage (-1 none)
    int   next = -1;               // target while transitioning, else -1
    int   phase = CYCLE_OFF;
    float remainingSec = 0.0f;     // dwell left (DWELL), else phase time left
    bool  transitioning = false;
    float fade = 1.0f;
    float warmSimSec = 0.0f;       // sim seconds warmed so far (WARMUP)
};

// What the shell does with the next shown frame.
struct CycleFrame {
    float fade = 1.0f;             // renderer.SetFade
    bool  black = false;           // renderer.SetBlackOut
    int   extraSteps = 0;          // SimOnlyStep calls BEFORE this frame's Frame()
    float stepDt = 1.0f / 144.0f;  // dt of each extra step
};

extern bool g_cycleActive;         // true while the director owns the look

// ---- lifecycle (main.cpp) ---------------------------------------------------
void CycleLoad(const wchar_t* ini);          // [cycle] + stage resolution
// CLI overrides (shot mode / tests): < 0 / 0 = keep the ini's value
void CycleOverride(float dwellSec, int order, unsigned seed, int startStage1Based,
                   bool forceEnable, float jitter = -1.0f);
// Boot: when enabled, compose the start stage into cfg (before the renderer
// exists) and enter WARMUP. Returns true when cycling.
bool CycleBoot(FluidConfig& cfg);
// Per shown frame, BEFORE renderer.Frame(): advances the state machine by the
// frame's dt, applies stages at the black point, returns what to render.
CycleFrame CycleTick(FluidRenderer& r, float dt);
bool CycleCoverageWanted();                  // renderer.SetCoverageWanted() value
void CycleSetLogger(void (*fn)(const char*));
void CycleDescribe(char* out, size_t cap);   // one status line for the shot log
// A preset picked by hand while cycling: the director lets go (session only).
void CycleManualOverride(const char* why);

// ---- commands (tray, WM_COMMAND, UI) ---------------------------------------
void CycleNext();
void CyclePrev();
void CycleSetEnabled(bool on, bool persist);  // persist -> [cycle] enabled in settings.ini
std::wstring CycleStageLabel(int i);          // "3. acid-rise-2hue (oil)"

// ---- the small API for the Settings UI (phase 1b Modes page) ---------------
const CycleConfig& CycleGet();
void CycleSet(const CycleConfig& c);          // replace list; persisted to settings.ini
void CycleJump(int stage);                    // 0-based; fades out to it
CycleStatus CycleState();
