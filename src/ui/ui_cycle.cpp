// Adapter: settings window -> cycle director (see ui_cycle.h). The mood conductor is gone
// (absorbed into src/cycle.cpp, brief CYCLE-DIRECTOR.md SURVEY + DECISION 2026-09-25);
// the four keys.inc rows now point at the director's [cycle] values.
#include <windows.h>
#include "ui_cycle.h"
#include "../cycle.h"
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

// persists [cycle] enabled; the director fades out to its first stage (or lands the
// current transition and lets go)
void UiCycleSetEnabled(bool on) { CycleSetEnabled(on, true); }
bool UiCycleOn() { return CycleState().phase != CYCLE_OFF; }

std::wstring UiPresetsDir() {
    wchar_t dir[MAX_PATH];
    GetPresetsDirectory(dir);   // %APPDATA%\FluidWallpaper\presets (the tray's list too)
    return dir;
}

void UiPresetsRescan() {}   // nothing caches the folder: the tray enumerates it on open
