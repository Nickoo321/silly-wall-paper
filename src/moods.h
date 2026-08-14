// Mood conductor: choreographed transitions between named look recipes
// (moods\*.ini, same partial-overlay format as presets). Replaces the old
// instant-swap interlude cycling. Transition phases:
//   DWELL -> SHIFT (hue-shift glides the field toward the next mood's hue)
//         -> EMIT  (discrete params flip; new emission paints bridge hues)
//         -> RETURN (shift rotates home; new mood dwells)
// Switching is timed with jitter AND composition-aware (field mostly decayed
// back to dark = discreet moment to leave early).
#pragma once
#include <string>
#include <vector>
#include "fluid.h"

struct MoodSettings {
    bool  enabled = false;
    float dwellMinutes = 3.0f;
    float transitionSec = 4.0f;
    float jitter = 0.3f;               // ± fraction on dwell time
    float earlySwitchDarkPct = 92.0f;  // field this % dark = decayed, ok to go
    float minDwellSec = 60.0f;         // never switch before this
};

extern MoodSettings g_moodSettings;

void InitMoods();                              // scan moods dir, read [moods], built-ins
void MoodsApplyBase(FluidConfig& cfg);         // boot: overlay explicitly-chosen base mood
void UpdateMoods(FluidRenderer& r, float dt);  // per frame from the main loop
void MoodsSetEnabled(bool on);                 // persists to ini + coverage override
void MoodsNext(FluidRenderer& r);              // manual "next mood now"
void MoodsForceMood(FluidRenderer& r, int index);
int  MoodsCurrentIndex();
int  MoodsNextIndex();                     // transition target while switching, else -1
const std::vector<std::wstring>& MoodsNames();
void MoodsAdoptPath(FluidRenderer& r, const std::wstring& path);  // scene-apply sync

// One managed recipe folder (%APPDATA%\FluidWallpaper\moods) — shared by the
// conductor, the Looks window, the tray menu, and the settings mood editor.
void MoodsGetDirectory(wchar_t out[MAX_PATH]);
const std::wstring& MoodsCurrentName();        // empty when no moods
void MoodsCurrentPath(wchar_t out[MAX_PATH]);  // empty string when none
void MoodsRescan();                            // re-scan dir, keep current by name
void MoodsSaveCurrent(FluidRenderer& r);       // live config -> current mood file
int  MoodsCreateFromLive(FluidRenderer& r);    // new "Mood N.ini" from live config
bool MoodsDeleteCurrent();                     // false when it's the last mood
// cycle skip-list ([moods] skip=name1;name2 in settings.ini)
bool MoodsIsSkipped(int i);
void MoodsSetSkipped(int i, bool skip);
// UI caches for the settings window markers (current mood file contents)
bool MoodsLocksKey(const wchar_t* section, const wchar_t* key);
void MoodsRefreshUiCache(FluidRenderer& r);    // rebuild lock set + cached config
const FluidConfig* MoodsCachedConfig();        // nullptr when unavailable
