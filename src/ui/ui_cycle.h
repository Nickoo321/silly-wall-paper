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
