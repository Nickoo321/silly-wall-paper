// The settings window's ONE adapter onto the cycle / preset-folder machinery.
//
// Today it forwards to the mood conductor (moods.h). Branch `cycle` deletes the conductor and
// lands src/animators.h (CycleState / CyclePause / CycleStageBase ...); merging it with ui1 is
// then a change to ui_cycle.cpp only. Nothing else in src/ui/ (keys.inc included) may call the
// Moods* API or touch g_moodSettings directly.
#pragma once
#include <string>

bool*  UiCycleEnabledPtr();          // [moods] enabled   (keys.inc row pointers)
float* UiCycleDwellPtr();            // [moods] dwell_minutes
float* UiCycleTransitionPtr();       // [moods] transition_seconds
float* UiCycleJitterPtr();           // [moods] jitter
bool   UiCycleIsPtr(const void* p);  // one of the four above (pointer-exception list)
float  UiCycleDefault(const void* p);// code default for one of the four
void   UiCycleSetEnabled(bool on);   // persists + tells the renderer (coverage readback)
bool   UiCycleOn();

std::wstring UiPresetsDir();         // the one preset folder (today %APPDATA%\FluidWallpaper\moods)
void   UiPresetsRescan();            // after a Save / Save as
