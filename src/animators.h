// animators.h -- the contract between the cycle director and the Settings UI
// (brief reference/briefs/UI-ANIMATORS-MODEL.md, DECIDED 2026-09-25).
// Landed first by the cycle side so the ui1/1b executor builds against it.
// Implementation: src/cycle.cpp (director, freezes), src/fluid.cpp (the
// derived clocks' freeze offsets + the const getters), src/moods.cpp (the
// conductor hold).
//
// BASE vs LIVE. Two kinds of animator:
//  (a) DERIVED at constant-upload time, never written to Config: the acid
//      palette clocks (hue_rotate_period, hue_sweep_period; the ink's
//      pair_sweep_period), the film_hue2 wobble, the camera rig (lamp, lens
//      axis, lid, focus, pixel-shift orbit), the fluid hue-shift angle
//      (m_hueAngle). Config IS the base; the UI shows the LIVE value through
//      the const getters on FluidRenderer (listed at the bottom).
//  (b) WRITTEN into Config: the mood conductor (LerpLook during SHIFT/EMIT/
//      RETURN), journeys (during DWELL). In DWELL Config == the mood target ==
//      base; during a transition the target is base and Config is live.
//
// FREEZE = hold the phase, never reset it. A derived clock reads
//      phase = fmod((m_time - frozenAccum) / P)
// where frozenAccum is the wall-clock time the animator has spent frozen:
// 0.0 when never frozen, so a never-frozen run is bit-identical (parity md5 +
// dxbc-cmp proven on branch cycle). Freezes AUTO-RELEASE after autoReleaseSec
// (default 15 min) and the UI releases all of them when its window closes
// (UnfreezeAll). Freezing the rig stops ALL camera motion.
#pragma once
#include <string>
#include "fluid.h"
#include "cycle.h"

enum Animator {
    ANIM_PALETTE = 0,    // acid hue_rotate_period + hue_sweep_period; ink pair_sweep_period
    ANIM_HUE2,           // [liquid_acid] film_hue2_wobble (the second hue's swing)
    ANIM_RIG,            // camera rig: lamp drift, lens axis, lid, focus readjusts, pixel-shift orbit
    ANIM_HUE_SHIFT,      // fluid hue-shift cycler (m_hueAngle bursts)
    ANIM_CONDUCTOR,      // mood conductor + journey (a hold in moods.cpp)
    ANIM_CYCLE,          // the director's DWELL timer (same as CyclePause)
    ANIM_COUNT
};

// ---- freezes ----------------------------------------------------------------
void  Freeze(Animator a, float autoReleaseSec = 900.0f);   // re-arms the timeout if already frozen
void  Unfreeze(Animator a);
void  UnfreezeAll();                                        // the UI calls this on window close
bool  IsFrozen(Animator a);
float FrozenRemainingSec(Animator a);                       // auto-release countdown, 0 if not frozen
const wchar_t* AnimatorName(Animator a);                    // "Hue rotation", "Camera rig", ...
// Called once per shown frame by the shell (via CycleTick) -- auto-release.
void  AnimatorsTick();

// ---- the director, as the UI sees it ------------------------------------------
// Settings window open => the DWELL timer holds (colour clocks keep running
// unless frozen). Auto-resumes after autoResumeSec of wall time; call
// CyclePause again on user input to re-arm it. Fades already under way finish.
void  CyclePause(float autoResumeSec = 600.0f);
void  CycleResume();
bool  CyclePaused();
float CyclePauseRemainingSec();
// CycleState() (cycle.h): stage, next, phase, remaining, transitioning, fade.
// The COMPOSED BASE of the current stage: FluidConfig{} + stage_N_base +
// stage_N_file, shell keys (sim/dye res, fps, mirror) from the live config.
// What "dirty" is measured against, and what Revert reloads. false when the
// director is off or has no current stage.
bool  CycleStageBase(FluidConfig& out);
// The current stage's file (absolute) -- Save writes a PARTIAL overlay of
// the keys that differ from CycleStageBase() into it. Empty when off.
std::wstring CycleStageFile();
// Re-apply the current stage in memory (Revert): Config := CycleStageBase(),
// no fade, no sim reset. No-op when off.
void  CycleRevertStage();

// ---- "in cycle" flags ---------------------------------------------------------
// The flags ARE the [cycle] stage list in settings.ini (preset files stay
// portable). One source of truth: order=alternate_random draws from it.
bool  CycleHasStage(const std::wstring& file);             // path as written or absolute
// Add (at the end, with dwellSec) or remove every entry for that file;
// persisted through CycleSet().
void  CycleSetStageIncluded(const std::wstring& file, bool included, float dwellSec = 600.0f);

// ---- derived-value getters (const, on FluidRenderer; cannot change md5/DXBC) ---
//   float PaletteHueDeg() const;   acid hue_rotate_period angle applied now (0..360)
//   float PaletteSweepPos() const; hue_sweep_period position in pairs (0..sweep_count), -1 off
//   float Hue2Deg() const;         film_hue2 after the wobble
//   float HueAngleDeg() const;     fluid hue-shift angle (existing)
//   void  RigState(float[8]) const;lamp, lens, tilt, focus, pixel shift (existing)
//   float AnimatorTime(int a) const; the clock a derived animator reads (m_time - frozen)
