// Shared app state (defined in main.cpp) + the settings window entry point
// (settings.cpp). Keeps the tray shell, renderer, and settings UI in sync.
#pragma once
#include <windows.h>
#include <string>
#include "fluid.h"

extern bool     g_pauseOnFullscreen;
extern bool     g_pauseOnMaximized;
extern float    g_hdrPeakNits;      // -1 = panel-reported max, 0 = off, else nits
extern int      g_gamutMode;        // 0 sRGB, 1 P3, 2 BT.2020
extern float    g_maxNits;          // panel-reported max luminance
extern float    g_currentFps;       // smoothed achieved framerate (main loop)

void RequestExit();                 // main.cpp — clean shutdown from any UI
void TogglePause();
bool IsManualPaused();

// preset API (main.cpp) for the scenes window
void ApplyPresetPath(const std::wstring& path);
void LoadConfigFromFile(const wchar_t* ini, FluidConfig& cfg);  // partial overlay load
void SaveCurrentAsPresetFile();     // auto-named snapshot
void GetPresetsDirectory(wchar_t out[MAX_PATH]);
void PersistShellSettings();        // pauses/HDR/cycle keys -> settings.ini
void PersistFullConfigNow();        // full live config -> settings.ini
// full-config ini writer (main.cpp). includeShell=false skips the machine/
// shell keys moods must not carry: sim_res, dye_res, fps_limit, mirror_second.
void WriteConfigToIni(const wchar_t* path, const FluidConfig& cfg, bool includeShell);

void ShowScenesWindow();            // scenes.cpp — RGB-suite style manager
extern wchar_t  g_iniPath[MAX_PATH];
// Where config VALUES are read from. Identical to g_iniPath in normal mode;
// --shot --ini <path> points it at a throwaway copy so a capture run can be
// driven from an arbitrary ini. Folder layout (moods\, presets\, journeys\)
// always derives from g_iniPath, so it stays at the default location.
extern wchar_t  g_configIniPath[MAX_PATH];
// --shot: never write an ini, a mood file, or a journey file. The capture run
// must leave the user's live configuration byte-for-byte untouched.
extern bool     g_configReadOnly;
extern FluidRenderer* g_renderer;

void ShowSettingsWindow();          // settings.cpp — creates or focuses the panel
void CloseSettingsWindow();         // settings.cpp — closes it (preset loads reopen fresh)
void ShowAnalyzerWindow();          // analyzer.cpp — nits heat-map inspector
