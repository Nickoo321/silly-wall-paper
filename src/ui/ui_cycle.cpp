// Adapter: settings window -> mood conductor (see ui_cycle.h). Replace the bodies with
// animators.h calls when branch `cycle` lands; nothing else in src/ui/ needs to change.
#include <windows.h>
#include "ui_cycle.h"
#include "../moods.h"

bool*  UiCycleEnabledPtr()    { return &g_moodSettings.enabled; }
float* UiCycleDwellPtr()      { return &g_moodSettings.dwellMinutes; }
float* UiCycleTransitionPtr() { return &g_moodSettings.transitionSec; }
float* UiCycleJitterPtr()     { return &g_moodSettings.jitter; }

bool UiCycleIsPtr(const void* p) {
    return p == &g_moodSettings.enabled || p == &g_moodSettings.dwellMinutes ||
           p == &g_moodSettings.transitionSec || p == &g_moodSettings.jitter;
}

float UiCycleDefault(const void* p) {
    static const MoodSettings d{};
    if (p == &g_moodSettings.dwellMinutes) return d.dwellMinutes;
    if (p == &g_moodSettings.transitionSec) return d.transitionSec;
    if (p == &g_moodSettings.jitter) return d.jitter;
    if (p == &g_moodSettings.enabled) return d.enabled ? 1.0f : 0.0f;
    return 0.0f;
}

void UiCycleSetEnabled(bool on) { MoodsSetEnabled(on); }
bool UiCycleOn() { return g_moodSettings.enabled; }

std::wstring UiPresetsDir() {
    wchar_t dir[MAX_PATH];
    MoodsGetDirectory(dir);
    return dir;
}

void UiPresetsRescan() { MoodsRescan(); }
