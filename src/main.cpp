// FluidWallpaper — app shell: wallpaper window (WorkerW), tray icon,
// fullscreen pause, live HDR detection. Rendering lives in fluid.cpp.
//
// GUI app. Flags:
//   --console          open a diagnostics console
//   --gradient         render the M1 HDR test gradient instead of the fluid
//   --stats            print dye-field readback stats every 5 s
//   --simres N         override simulation resolution (default 256)
//   --dyeres N         override dye resolution (default 1024)
//   --force-render     ignore fullscreen pause (automated testing)

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <string>
#include "fluid.h"
#include "app_state.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

void Fail(const char* what, HRESULT hr) {
    char buf[512];
    _snprintf_s(buf, _TRUNCATE, "%s failed (hr=0x%08lX)", what, (unsigned long)hr);
    fprintf(stderr, "FATAL: %s\n", buf);
    MessageBoxA(nullptr, buf, "Fluid Wallpaper - fatal error", MB_ICONERROR);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// App state + settings (persisted to %APPDATA%\FluidWallpaper\settings.ini)
// ---------------------------------------------------------------------------

static bool     g_running = true;
static bool     g_manualPause = false;
bool            g_pauseOnFullscreen = true;   // shared with settings.cpp
bool            g_pauseOnMaximized = true;
float           g_hdrPeakNits = -1.0f;        // -1 = panel max (auto), 0 = off, else nits
int             g_gamutMode = 2;              // 0 sRGB, 1 P3, 2 BT.2020
// preset interlude cycling
bool            g_cycleEnabled = false;
float           g_cycleBaseSec = 20.0f;       // time in the user's own settings
float           g_cycleInterludeSec = 6.0f;   // time in the injected preset
std::wstring    g_cyclePreset = L"*";         // preset filename, or "*" = random (shared)
static bool        g_inInterlude = false;
static ULONGLONG   g_nextCycleTick = 0;
static FluidConfig g_cycleBaseCfg;
FluidRenderer*  g_renderer = nullptr;
static bool     g_forceRender = false;
static float    g_fpsOverride = 0.0f;   // --fps test flag; 0 = use settings
float           g_currentFps = 0.0f;    // smoothed achieved framerate

void RequestExit()   { g_running = false; }
void TogglePause()   { g_manualPause = !g_manualPause; }
bool IsManualPaused() { return g_manualPause; }
static bool     g_fsPaused = false;
static HMONITOR g_monitor = nullptr;
wchar_t         g_iniPath[MAX_PATH] = {};

static void InitSettingsPath() {
    wchar_t* appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        wchar_t dir[MAX_PATH];
        _snwprintf_s(dir, _TRUNCATE, L"%s\\FluidWallpaper", appdata);
        CoTaskMemFree(appdata);
        CreateDirectoryW(dir, nullptr);
        _snwprintf_s(g_iniPath, _TRUNCATE, L"%s\\settings.ini", dir);
    }
}

static void LoadSettings() {
    if (!g_iniPath[0]) return;
    g_pauseOnFullscreen = GetPrivateProfileIntW(L"general", L"pause_on_fullscreen", 1, g_iniPath) != 0;
    g_pauseOnMaximized = GetPrivateProfileIntW(L"general", L"pause_on_maximized", 1, g_iniPath) != 0;
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"hdr", L"peak_nits", L"-1", buf, 64, g_iniPath);
    g_hdrPeakNits = (float)_wtof(buf);
    g_gamutMode = (int)GetPrivateProfileIntW(L"hdr", L"gamut", 2, g_iniPath);
    if (g_gamutMode < 0 || g_gamutMode > 2) g_gamutMode = 2;
    g_cycleEnabled = GetPrivateProfileIntW(L"cycle", L"enabled", 0, g_iniPath) != 0;
    GetPrivateProfileStringW(L"cycle", L"base_seconds", L"20", buf, 64, g_iniPath);
    g_cycleBaseSec = (float)_wtof(buf);
    GetPrivateProfileStringW(L"cycle", L"interlude_seconds", L"6", buf, 64, g_iniPath);
    g_cycleInterludeSec = (float)_wtof(buf);
    wchar_t pbuf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"cycle", L"preset", L"*", pbuf, MAX_PATH, g_iniPath);
    g_cyclePreset = pbuf;
}

static void SaveSettings() {
    if (!g_iniPath[0]) return;
    WritePrivateProfileStringW(L"general", L"pause_on_fullscreen",
                               g_pauseOnFullscreen ? L"1" : L"0", g_iniPath);
    WritePrivateProfileStringW(L"general", L"pause_on_maximized",
                               g_pauseOnMaximized ? L"1" : L"0", g_iniPath);
    wchar_t buf[32];
    swprintf_s(buf, L"%.0f", g_hdrPeakNits);
    WritePrivateProfileStringW(L"hdr", L"peak_nits", buf, g_iniPath);
    swprintf_s(buf, L"%d", g_gamutMode);
    WritePrivateProfileStringW(L"hdr", L"gamut", buf, g_iniPath);
    WritePrivateProfileStringW(L"cycle", L"enabled", g_cycleEnabled ? L"1" : L"0", g_iniPath);
    swprintf_s(buf, L"%.0f", g_cycleBaseSec);
    WritePrivateProfileStringW(L"cycle", L"base_seconds", buf, g_iniPath);
    swprintf_s(buf, L"%.0f", g_cycleInterludeSec);
    WritePrivateProfileStringW(L"cycle", L"interlude_seconds", buf, g_iniPath);
    WritePrivateProfileStringW(L"cycle", L"preset", g_cyclePreset.c_str(), g_iniPath);
}

// Full config from an ini file — every value the settings window writes.
// Absent keys keep whatever is already in cfg (lets presets be partial).
static void LoadConfigFromIni(const wchar_t* ini, FluidConfig& cfg) {
    if (!ini || !ini[0]) return;
    auto getF = [ini](const wchar_t* sec, const wchar_t* key, float def) {
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
        return buf[0] ? (float)_wtof(buf) : def;
    };
    auto getB = [ini](const wchar_t* sec, const wchar_t* key, bool def) {
        return GetPrivateProfileIntW(sec, key, def ? 1 : 0, ini) != 0;
    };
    auto getI = [ini](const wchar_t* sec, const wchar_t* key, int def) {
        return (int)GetPrivateProfileIntW(sec, key, def, ini);
    };
    cfg.densityDissipation  = getF(L"sim", L"density_diffusion", cfg.densityDissipation);
    cfg.velocityDissipation = getF(L"sim", L"velocity_diffusion", cfg.velocityDissipation);
    cfg.pressureDissipation = getF(L"sim", L"pressure_diffusion", cfg.pressureDissipation);
    cfg.decayFast           = getF(L"sim", L"decay_fast", cfg.decayFast);
    cfg.decayThreshold      = getF(L"sim", L"decay_threshold", cfg.decayThreshold);
    cfg.satRestore          = getF(L"sim", L"saturation_restore", cfg.satRestore);
    cfg.maxBrightness       = getF(L"sim", L"max_brightness", cfg.maxBrightness);
    cfg.curl                = getF(L"sim", L"vorticity", cfg.curl);
    cfg.splatRadius         = getF(L"sim", L"splat_radius", cfg.splatRadius);
    cfg.shading             = getB(L"sim", L"shading", cfg.shading);
    cfg.simRes              = getI(L"sim", L"sim_res", cfg.simRes);
    cfg.dyeRes              = getI(L"sim", L"dye_res", cfg.dyeRes);
    cfg.pressureIterations  = getI(L"sim", L"pressure_iterations", cfg.pressureIterations);
    cfg.hdrKnee             = getF(L"hdr", L"knee", cfg.hdrKnee);
    cfg.hdrSaturation       = getF(L"hdr", L"saturation", cfg.hdrSaturation);
    cfg.hdrBrightness       = getF(L"hdr", L"brightness", cfg.hdrBrightness);
    cfg.hdrContrast         = getF(L"hdr", L"contrast", cfg.hdrContrast);
    cfg.hdrCompensation     = getB(L"hdr", L"compensation", cfg.hdrCompensation);
    cfg.colorCyclePeriod    = getF(L"behavior", L"color_cycle_period", cfg.colorCyclePeriod);
    cfg.wanderers           = getB(L"behavior", L"wanderers", cfg.wanderers);
    cfg.wandererCount       = getI(L"behavior", L"wanderer_count", cfg.wandererCount);
    cfg.wandererMode        = getI(L"behavior", L"wanderer_mode", cfg.wandererMode);
    cfg.wandererSpeed       = getF(L"behavior", L"wanderer_speed", cfg.wandererSpeed);
    cfg.wandererBrightness  = getF(L"behavior", L"wanderer_brightness", cfg.wandererBrightness);
    cfg.wandererScale       = getF(L"behavior", L"wanderer_scale", cfg.wandererScale);
    cfg.wandererResumeDelay = getF(L"behavior", L"wanderer_resume_delay", cfg.wandererResumeDelay);
    cfg.autoPause           = getB(L"behavior", L"auto_pause", cfg.autoPause);
    cfg.darkFloor           = getF(L"behavior", L"dark_floor", cfg.darkFloor);
    cfg.darkLevel           = getF(L"behavior", L"dark_level", cfg.darkLevel);
    cfg.survDarkFloor       = getF(L"behavior", L"surv_dark_floor", cfg.survDarkFloor);
    cfg.contrastReq         = getF(L"behavior", L"contrast_req", cfg.contrastReq);
    cfg.dartEnabled         = getB(L"behavior", L"dart_enabled", cfg.dartEnabled);
    cfg.dartInterval        = getF(L"behavior", L"dart_interval", cfg.dartInterval);
    cfg.dartSpeed           = getF(L"behavior", L"dart_speed", cfg.dartSpeed);
    cfg.hsEnabled           = getB(L"behavior", L"hueshift_enabled", cfg.hsEnabled);
    cfg.hsStep              = getF(L"behavior", L"hueshift_step", cfg.hsStep);
    cfg.hsLinger            = getF(L"behavior", L"hueshift_linger", cfg.hsLinger);
    cfg.hsGlide             = getF(L"behavior", L"hueshift_glide", cfg.hsGlide);
    cfg.hsBurstSteps        = getI(L"behavior", L"hueshift_burst_steps", cfg.hsBurstSteps);
    cfg.hsOffTime           = getF(L"behavior", L"hueshift_off_time", cfg.hsOffTime);
    cfg.idleSplats          = getB(L"behavior", L"idle_splats", cfg.idleSplats);
    cfg.idleInterval        = getF(L"behavior", L"idle_interval", cfg.idleInterval);
    cfg.idleAmount          = getI(L"behavior", L"idle_amount", cfg.idleAmount);
    cfg.holdToSplat         = getB(L"behavior", L"hold_to_splat", cfg.holdToSplat);
    cfg.splatOnClick        = getB(L"behavior", L"splat_on_click", cfg.splatOnClick);
    cfg.showMouse           = getB(L"behavior", L"show_mouse", cfg.showMouse);
    cfg.fpsLimit            = getF(L"general", L"fps_limit", cfg.fpsLimit);
    cfg.colorful            = getB(L"color", L"colorful", cfg.colorful);
    cfg.moreColors          = getB(L"color", L"more_colors", cfg.moreColors);
    cfg.postSaturation      = getF(L"color", L"post_saturation", cfg.postSaturation);
    cfg.postContrast        = getF(L"color", L"post_contrast", cfg.postContrast);
    cfg.postBrightness      = getF(L"color", L"post_brightness", cfg.postBrightness);
    cfg.postHue             = getF(L"color", L"post_hue", cfg.postHue);
    cfg.hueCenter           = getF(L"color", L"hue_center", cfg.hueCenter);
    cfg.hueRange            = getF(L"color", L"hue_range", cfg.hueRange);
    for (int ci = 0; ci < 5; ci++) {
        wchar_t key[32], buf[64] = {};
        swprintf_s(key, L"splat_color_%d", ci + 1);
        GetPrivateProfileStringW(L"color", key, L"", buf, 64, ini);
        float r, g, b;
        if (swscanf_s(buf, L"%f %f %f", &r, &g, &b) == 3) {
            cfg.splatColors[ci * 3 + 0] = r;
            cfg.splatColors[ci * 3 + 1] = g;
            cfg.splatColors[ci * 3 + 2] = b;
        }
    }
    printf("config loaded: density=%.3f decay_fast=%.2f vorticity=%.0f sim=%d dye=%d\n",
           cfg.densityDissipation, cfg.decayFast, cfg.curl, cfg.simRes, cfg.dyeRes);
}

static void LoadFullConfig(FluidConfig& cfg) { LoadConfigFromIni(g_iniPath, cfg); }

// ---------------------------------------------------------------------------
// WorkerW: the layer behind the desktop icons
// ---------------------------------------------------------------------------

static HWND FindWallpaperHost() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;

    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);

    struct Ctx { HWND wallpaper = nullptr; } ctx;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
            c->wallpaper = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.wallpaper) {
        printf("WorkerW layout: classic (sibling WorkerW)\n");
        return ctx.wallpaper;
    }

    if (FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
        HWND workerw = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
        if (workerw) {
            printf("WorkerW layout: Win11 24H2 (WorkerW child of Progman)\n");
            return workerw;
        }
        printf("WorkerW layout: 24H2 without WorkerW child, using Progman\n");
        return progman;
    }

    printf("WorkerW layout: unknown, falling back to Progman\n");
    return progman;
}

static bool g_shuttingDown = false;
static bool g_wallpaperLost = false;

static LRESULT CALLBACK WallpaperWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (g_shuttingDown) {
            PostQuitMessage(0);
        } else {
            // Explorer restart tore down the WorkerW hierarchy (and us with
            // it). Don't quit — the main loop re-hooks into the new shell.
            printf("wallpaper window destroyed externally (Explorer restart?) - will reattach\n");
            g_wallpaperLost = true;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND CreateWallpaperWindow(HWND host, int width, int height) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WallpaperWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FluidWallpaperWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Fluid Wallpaper",
        WS_POPUP, 0, 0, width, height,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) Fail("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));

    SetParent(hwnd, host);
    POINT origin = { 0, 0 };
    ScreenToClient(host, &origin);
    SetWindowPos(hwnd, HWND_TOP, origin.x, origin.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return hwnd;
}

// ---------------------------------------------------------------------------
// Fullscreen detection
// ---------------------------------------------------------------------------

static bool IsShellOrOwnWindow(HWND hwnd) {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    return wcscmp(cls, L"Progman") == 0 ||
           wcscmp(cls, L"WorkerW") == 0 ||
           wcscmp(cls, L"Shell_TrayWnd") == 0 ||
           wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
           wcscmp(cls, L"FluidWallpaperWnd") == 0 ||
           wcscmp(cls, L"FluidWallpaperTray") == 0 ||
           wcscmp(cls, L"XamlExplorerHostIslandWindow") == 0 ||
           wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0;
}

static bool FullscreenAppActive() {
    HWND fg = GetForegroundWindow();
    if (!fg || IsShellOrOwnWindow(fg)) return false;
    if (MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) != g_monitor) return false;

    // Maximized windows are not fullscreen — including borderless
    // custom-titlebar apps (Electron, Windows Terminal). True fullscreen is a
    // non-maximized borderless popup sized to the monitor.
    if (IsZoomed(fg)) return false;
    LONG style = GetWindowLongW(fg, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION) return false;
    if (style & WS_THICKFRAME) return false;

    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(g_monitor, &mi)) return false;
    RECT r;
    if (!GetWindowRect(fg, &r)) return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

static bool MaximizedAppActive() {
    HWND fg = GetForegroundWindow();
    if (!fg || IsShellOrOwnWindow(fg)) return false;
    if (MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) != g_monitor) return false;
    return IsZoomed(fg) != FALSE;
}

// ---------------------------------------------------------------------------
// Live HDR state
// ---------------------------------------------------------------------------

static bool  g_hdrActive = false;
float        g_maxNits = 0.0f;    // shared with settings.cpp
static float g_sdrWhiteNits = 80.0f;

// The user's "SDR content brightness" slider (HDR mode only). SDR content —
// which is what the reference wallpaper was — renders at this level, so we
// match it for parity. 1000 units == 80 nits.
static float GetSdrWhiteNits(HMONITOR mon) {
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return 80.0f;

    UINT32 numPath = 0, numMode = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPath, &numMode) != ERROR_SUCCESS)
        return 80.0f;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPath);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(numMode);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPath, paths.data(),
                           &numMode, modes.data(), nullptr) != ERROR_SUCCESS)
        return 80.0f;

    for (UINT32 i = 0; i < numPath; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        if (wcscmp(src.viewGdiDeviceName, mi.szDevice) != 0) continue;

        DISPLAYCONFIG_SDR_WHITE_LEVEL wl = {};
        wl.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        wl.header.size = sizeof(wl);
        wl.header.adapterId = paths[i].targetInfo.adapterId;
        wl.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&wl.header) != ERROR_SUCCESS) break;
        return wl.SDRWhiteLevel / 1000.0f * 80.0f;
    }
    return 80.0f;
}

static bool QueryHDR(HMONITOR mon, float* maxNits) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return g_hdrActive;
    for (UINT ai = 0; ; ai++) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(ai, &adapter))) break;
        for (UINT oi = 0; ; oi++) {
            ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(oi, &output))) break;
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc = {};
            if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) &&
                desc.Monitor == mon) {
                if (maxNits) *maxNits = desc.MaxLuminance;
                return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }
        }
    }
    return g_hdrActive;
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------

static const UINT WM_TRAYICON = WM_APP + 1;
enum TrayCmd : UINT {
    CMD_PAUSE = 1, CMD_FSPAUSE = 2, CMD_EXIT = 3, CMD_MAXPAUSE = 4, CMD_SETTINGS = 5,
    CMD_ANALYZER = 6, CMD_SCENES = 7,
    CMD_PRESET_SAVE = 30, CMD_PRESET_FOLDER = 31, CMD_CYCLE_TOGGLE = 32,
    CMD_PRESET_BASE = 600, CMD_CYCLE_RANDOM = 699, CMD_CYCLE_BASE = 700,
    CMD_PEAK_OFF = 10, CMD_PEAK_AUTO = 11, CMD_PEAK_300 = 12, CMD_PEAK_600 = 13,
    CMD_PEAK_800 = 15, CMD_PEAK_1000 = 14,
    CMD_GAMUT_SRGB = 20, CMD_GAMUT_P3 = 21, CMD_GAMUT_2020 = 22,
};

static NOTIFYICONDATAW g_nid = {};

// presets (implementations further down; the menu needs them declared)
static std::vector<std::wstring> g_presetPaths;
static void GetPresetsDir(wchar_t out[MAX_PATH]);
static void ApplyPreset(const std::wstring& path);
static void SaveCurrentAsPreset();
static void ShowTrayMenuBody(HWND hwnd, HMENU presets);

static void ShowTrayMenu(HWND hwnd) {
    // enumerate preset files fresh each time the menu opens
    g_presetPaths.clear();
    HMENU presets = CreatePopupMenu();
    {
        wchar_t dir[MAX_PATH], pattern[MAX_PATH];
        GetPresetsDir(dir);
        swprintf_s(pattern, L"%s\\*.ini", dir);
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW(pattern, &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                std::wstring full = std::wstring(dir) + L"\\" + fd.cFileName;
                std::wstring name = fd.cFileName;
                size_t dot = name.rfind(L".ini");
                if (dot != std::wstring::npos) name.resize(dot);
                AppendMenuW(presets, MF_STRING,
                            CMD_PRESET_BASE + g_presetPaths.size(), name.c_str());
                g_presetPaths.push_back(full);
            } while (FindNextFileW(find, &fd) && g_presetPaths.size() < 50);
            FindClose(find);
        }
        if (g_presetPaths.empty())
            AppendMenuW(presets, MF_STRING | MF_GRAYED, 0, L"(no presets yet)");
        AppendMenuW(presets, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(presets, MF_STRING, CMD_PRESET_SAVE, L"Save current as new preset");
        AppendMenuW(presets, MF_STRING, CMD_PRESET_FOLDER, L"Open presets folder");

        // interlude cycling: base settings for N s, injected preset for M s
        AppendMenuW(presets, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(presets, MF_STRING | (g_cycleEnabled ? MF_CHECKED : 0),
                    CMD_CYCLE_TOGGLE, L"Cycle interludes (base → preset → base…)");
        HMENU cycleSel = CreatePopupMenu();
        AppendMenuW(cycleSel, MF_STRING | (g_cyclePreset == L"*" ? MF_CHECKED : 0),
                    CMD_CYCLE_RANDOM, L"Random preset each time");
        for (size_t i = 0; i < g_presetPaths.size(); i++) {
            const wchar_t* base = wcsrchr(g_presetPaths[i].c_str(), L'\\');
            std::wstring fname = base ? base + 1 : g_presetPaths[i];
            std::wstring label = fname;
            size_t dot = label.rfind(L".ini");
            if (dot != std::wstring::npos) label.resize(dot);
            AppendMenuW(cycleSel, MF_STRING | (g_cyclePreset == fname ? MF_CHECKED : 0),
                        CMD_CYCLE_BASE + i, label.c_str());
        }
        AppendMenuW(presets, MF_POPUP, (UINT_PTR)cycleSel, L"Interlude preset");
    }
    ShowTrayMenuBody(hwnd, presets);
}

static void ShowTrayMenuBody(HWND hwnd, HMENU presets) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_manualPause ? MF_CHECKED : 0), CMD_PAUSE, L"Pause");
    AppendMenuW(menu, MF_STRING | (g_pauseOnFullscreen ? MF_CHECKED : 0), CMD_FSPAUSE,
                L"Pause when a fullscreen app is running");
    AppendMenuW(menu, MF_STRING | (g_pauseOnMaximized ? MF_CHECKED : 0), CMD_MAXPAUSE,
                L"Pause when an app is maximized");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // HDR peak brightness (hot-spot target; only active while Windows HDR is on)
    HMENU peak = CreatePopupMenu();
    wchar_t autoLabel[64];
    swprintf_s(autoLabel, L"Windows-reported max (%.0f nits)", g_maxNits);
    AppendMenuW(peak, MF_STRING | (g_hdrPeakNits == 0.0f ? MF_CHECKED : 0), CMD_PEAK_OFF,
                L"Off — match SDR brightness");
    AppendMenuW(peak, MF_STRING | (g_hdrPeakNits < 0.0f ? MF_CHECKED : 0), CMD_PEAK_AUTO, autoLabel);
    AppendMenuW(peak, MF_STRING | (g_hdrPeakNits == 300.0f ? MF_CHECKED : 0), CMD_PEAK_300, L"300 nits");
    AppendMenuW(peak, MF_STRING | (g_hdrPeakNits == 600.0f ? MF_CHECKED : 0), CMD_PEAK_600, L"600 nits");
    AppendMenuW(peak, MF_STRING | (g_hdrPeakNits == 800.0f ? MF_CHECKED : 0), CMD_PEAK_800, L"800 nits");
    AppendMenuW(peak, MF_STRING | (g_hdrPeakNits == 1000.0f ? MF_CHECKED : 0), CMD_PEAK_1000,
                L"1000 nits (QD-OLED small window)");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)peak, L"HDR peak brightness");

    HMENU gamut = CreatePopupMenu();
    AppendMenuW(gamut, MF_STRING | (g_gamutMode == 0 ? MF_CHECKED : 0), CMD_GAMUT_SRGB, L"sRGB (standard)");
    AppendMenuW(gamut, MF_STRING | (g_gamutMode == 1 ? MF_CHECKED : 0), CMD_GAMUT_P3, L"Display-P3 (like the WE original)");
    AppendMenuW(gamut, MF_STRING | (g_gamutMode == 2 ? MF_CHECKED : 0), CMD_GAMUT_2020, L"BT.2020 (full QD-OLED)");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)gamut, L"Color gamut");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_SCENES, L"Scenes…");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)presets, L"Presets");
    AppendMenuW(menu, MF_STRING, CMD_SETTINGS, L"Settings…");
    AppendMenuW(menu, MF_STRING, CMD_ANALYZER, L"HDR analyzer (nits heat-map)…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static UINT g_taskbarCreatedMsg = 0;

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Explorer restarted: the tray was rebuilt, our icon is gone — re-add it.
    if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    switch (msg) {
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP ||
            LOWORD(lp) == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case CMD_PAUSE:
            g_manualPause = !g_manualPause;
            printf("manual pause: %s\n", g_manualPause ? "on" : "off");
            break;
        case CMD_FSPAUSE:
            g_pauseOnFullscreen = !g_pauseOnFullscreen;
            SaveSettings();
            printf("pause on fullscreen: %s\n", g_pauseOnFullscreen ? "on" : "off");
            break;
        case CMD_MAXPAUSE:
            g_pauseOnMaximized = !g_pauseOnMaximized;
            SaveSettings();
            printf("pause on maximized: %s\n", g_pauseOnMaximized ? "on" : "off");
            break;
        case CMD_PEAK_OFF:  g_hdrPeakNits = 0.0f;    SaveSettings(); break;
        case CMD_PEAK_AUTO: g_hdrPeakNits = -1.0f;   SaveSettings(); break;
        case CMD_PEAK_300:  g_hdrPeakNits = 300.0f;  SaveSettings(); break;
        case CMD_PEAK_600:  g_hdrPeakNits = 600.0f;  SaveSettings(); break;
        case CMD_PEAK_800:  g_hdrPeakNits = 800.0f;  SaveSettings(); break;
        case CMD_PEAK_1000: g_hdrPeakNits = 1000.0f; SaveSettings(); break;
        case CMD_SETTINGS:
            ShowSettingsWindow();
            break;
        case CMD_ANALYZER:
            ShowAnalyzerWindow();
            break;
        case CMD_SCENES:
            ShowScenesWindow();
            break;
        case CMD_PRESET_SAVE:
            SaveCurrentAsPreset();
            break;
        case CMD_CYCLE_TOGGLE:
            g_cycleEnabled = !g_cycleEnabled;
            g_inInterlude = false;
            g_nextCycleTick = 0;
            SaveSettings();
            printf("preset cycling: %s\n", g_cycleEnabled ? "on" : "off");
            break;
        case CMD_CYCLE_RANDOM:
            g_cyclePreset = L"*";
            SaveSettings();
            break;
        case CMD_PRESET_FOLDER: {
            wchar_t dir[MAX_PATH];
            GetPresetsDir(dir);
            ShellExecuteW(nullptr, L"open", dir, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        default:
            if (LOWORD(wp) >= CMD_PRESET_BASE && LOWORD(wp) < CMD_CYCLE_RANDOM &&
                LOWORD(wp) - CMD_PRESET_BASE < g_presetPaths.size()) {
                ApplyPreset(g_presetPaths[LOWORD(wp) - CMD_PRESET_BASE]);
            } else if (LOWORD(wp) >= CMD_CYCLE_BASE &&
                       LOWORD(wp) - CMD_CYCLE_BASE < g_presetPaths.size()) {
                const std::wstring& full = g_presetPaths[LOWORD(wp) - CMD_CYCLE_BASE];
                const wchar_t* base = wcsrchr(full.c_str(), L'\\');
                g_cyclePreset = base ? base + 1 : full;
                SaveSettings();
            }
            break;
        case CMD_GAMUT_SRGB: g_gamutMode = 0; SaveSettings(); break;
        case CMD_GAMUT_P3:   g_gamutMode = 1; SaveSettings(); break;
        case CMD_GAMUT_2020: g_gamutMode = 2; SaveSettings(); break;
        case CMD_EXIT:
            printf("tray: exit clicked\n");
            g_running = false;
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND CreateTrayWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FluidWallpaperTray";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Fluid Wallpaper Tray",
                                0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) Fail("CreateTrayWindow", HRESULT_FROM_WIN32(GetLastError()));

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));   // fluid vortex
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Fluid Wallpaper");
    // startup balloon so the icon is discoverable (Win11 hides new tray icons
    // behind the ^ overflow by default)
    wcscpy_s(g_nid.szInfoTitle, L"Fluid Wallpaper is running");
    wcscpy_s(g_nid.szInfo,
             L"Right-click this icon for settings and pause. "
             L"Tip: double-clicking FluidWallpaper.exe again also opens Settings.");
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;   // balloon only once
    return hwnd;
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void UpdateTrayTip() {
    if (!g_nid.hWnd) return;
    if (g_hdrActive)
        _snwprintf_s(g_nid.szTip, _TRUNCATE, L"Fluid Wallpaper — HDR on (%.0f nits)", g_maxNits);
    else
        _snwprintf_s(g_nid.szTip, _TRUNCATE, L"Fluid Wallpaper — HDR off (SDR)");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void ShowTrayBalloon(const wchar_t* title, const wchar_t* msg) {
    if (!g_nid.hWnd) return;
    g_nid.uFlags |= NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title);
    wcscpy_s(g_nid.szInfo, msg);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

// ---------------------------------------------------------------------------
// Presets: full settings.ini snapshots in %APPDATA%\FluidWallpaper\presets
// ---------------------------------------------------------------------------

static void GetPresetsDir(wchar_t out[MAX_PATH]) {
    wcscpy_s(out, MAX_PATH, g_iniPath);
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash) *slash = 0;
    wcscat_s(out, MAX_PATH, L"\\presets");
}

static void WriteTextFileUtf16(const wchar_t* path, const wchar_t* text) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WORD bom = 0xFEFF;
    WriteFile(f, &bom, 2, &written, nullptr);
    WriteFile(f, text, (DWORD)(wcslen(text) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
}

// The user's original Wallpaper Engine preset: project.json values + the WE
// right-panel color filter they ran it with. peak off / P3 = how WE looked.
static const wchar_t* kWEOriginalIni =
L"[hdr]\r\npeak_nits=0\r\ncompensation=1\r\nknee=0.60\r\nsaturation=1.20\r\nbrightness=1.08\r\ncontrast=1.00\r\ngamut=1\r\n"
L"[sim]\r\nvorticity=48\r\nsplat_radius=0.64\r\ndensity_diffusion=0.999\r\nvelocity_diffusion=0.999\r\n"
L"pressure_diffusion=0.85\r\npressure_iterations=20\r\ndecay_fast=1.000\r\ndecay_threshold=0.290\r\n"
L"saturation_restore=0.93\r\nmax_brightness=1.35\r\nshading=1\r\nsim_res=256\r\ndye_res=4096\r\n"
L"[behavior]\r\ncolor_cycle_period=19\r\nwanderers=1\r\nwanderer_count=2\r\nwanderer_mode=0\r\n"
L"wanderer_speed=246\r\nwanderer_brightness=0.10\r\nwanderer_scale=0.10\r\nwanderer_resume_delay=4.5\r\n"
L"auto_pause=1\r\ndark_floor=9\r\ndark_level=0.070\r\nsurv_dark_floor=8\r\ncontrast_req=30\r\n"
L"dart_enabled=1\r\ndart_interval=7\r\ndart_speed=967\r\n"
L"hueshift_enabled=1\r\nhueshift_step=83\r\nhueshift_linger=6.5\r\nhueshift_glide=7.2\r\n"
L"hueshift_burst_steps=2\r\nhueshift_off_time=10\r\n"
L"idle_splats=1\r\nidle_interval=9.6\r\nidle_amount=8\r\n"
L"hold_to_splat=1\r\nsplat_on_click=1\r\nshow_mouse=1\r\n"
L"[color]\r\ncolorful=1\r\nmore_colors=1\r\npost_saturation=1.14\r\npost_contrast=1.34\r\n"
L"post_brightness=1.06\r\npost_hue=14\r\n"
L"[general]\r\npause_on_fullscreen=1\r\npause_on_maximized=1\r\nfps_limit=60\r\n";

// Themed presets (partial inis — unspecified keys inherit the user's current
// settings). Sunny Embers: the orange-majority mood the user loves, pinned via
// the hue band so it can't drift away. Ember Depths: dark field, small red
// dots + blue accents (palette mode). Verdant: greens/teals.
static const wchar_t* kSunnyEmbersIni =
L"[color]\r\ncolorful=1\r\nhue_center=25\r\nhue_range=28\r\n"
L"post_saturation=1.15\r\npost_contrast=1.30\r\npost_brightness=1.05\r\npost_hue=0\r\n"
L"[sim]\r\ndensity_diffusion=0.9995\r\ndecay_fast=1.000\r\ndecay_threshold=0.290\r\n"
L"saturation_restore=0.93\r\nmax_brightness=0.90\r\nsplat_radius=0.64\r\nvorticity=48\r\n"
L"[behavior]\r\ncolor_cycle_period=45\r\nwanderer_brightness=0.10\r\ndark_floor=18\r\n"
L"idle_amount=4\r\nidle_interval=12\r\n";

static const wchar_t* kEmberDepthsIni =
L"[color]\r\ncolorful=0\r\nmore_colors=1\r\n"
L"splat_color_1=1.0000 0.0500 0.0500\r\nsplat_color_2=0.8000 0.0000 0.1000\r\n"
L"splat_color_3=0.1000 0.2000 1.0000\r\nsplat_color_4=1.0000 0.2000 0.0000\r\n"
L"splat_color_5=0.0000 0.5000 1.0000\r\n"
L"post_saturation=1.10\r\npost_contrast=1.40\r\npost_brightness=1.00\r\npost_hue=0\r\n"
L"[sim]\r\nsplat_radius=0.15\r\nmax_brightness=0.70\r\ndensity_diffusion=0.9990\r\n"
L"decay_fast=0.985\r\ndecay_threshold=0.200\r\nsaturation_restore=0.90\r\nvorticity=45\r\n"
L"[behavior]\r\nwanderer_count=3\r\nwanderer_brightness=0.12\r\ndark_floor=30\r\n"
L"idle_amount=3\r\nidle_interval=6\r\ncolor_cycle_period=19\r\n";

static const wchar_t* kVerdantIni =
L"[color]\r\ncolorful=1\r\nhue_center=120\r\nhue_range=35\r\n"
L"post_saturation=1.15\r\npost_contrast=1.30\r\npost_brightness=1.05\r\npost_hue=0\r\n"
L"[sim]\r\ndensity_diffusion=0.9992\r\ndecay_fast=1.000\r\ndecay_threshold=0.290\r\n"
L"saturation_restore=0.93\r\nmax_brightness=0.85\r\nsplat_radius=0.55\r\nvorticity=48\r\n"
L"[behavior]\r\ncolor_cycle_period=30\r\nwanderer_brightness=0.10\r\ndark_floor=20\r\n"
L"idle_amount=4\r\nidle_interval=10\r\n";

static void EnsureBuiltinPresets() {
    if (!g_iniPath[0]) return;
    wchar_t dir[MAX_PATH];
    GetPresetsDir(dir);
    CreateDirectoryW(dir, nullptr);

    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\WE Original.ini", dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        WriteTextFileUtf16(path, kWEOriginalIni);
    swprintf_s(path, L"%s\\Sunny Embers.ini", dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        WriteTextFileUtf16(path, kSunnyEmbersIni);
    swprintf_s(path, L"%s\\Ember Depths.ini", dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        WriteTextFileUtf16(path, kEmberDepthsIni);
    swprintf_s(path, L"%s\\Verdant.ini", dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        WriteTextFileUtf16(path, kVerdantIni);

    // migrate the earlier checkpoint into the presets folder (never deleted)
    swprintf_s(path, L"%s\\Deep Clouds.ini", dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t old[MAX_PATH];
        wcscpy_s(old, g_iniPath);
        wchar_t* slash = wcsrchr(old, L'\\');
        if (slash) *slash = 0;
        wcscat_s(old, L"\\preset-deep-clouds.ini");
        if (GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES)
            CopyFileW(old, path, TRUE);
    }
}

// Persist the complete merged config to settings.ini (presets can be partial,
// so after applying one, the resolved state must be written out in full).
static void SaveFullConfig(const FluidConfig& c) {
    if (!g_iniPath[0]) return;
    auto putF = [](const wchar_t* sec, const wchar_t* key, float v, int dec) {
        wchar_t b[48];
        swprintf_s(b, L"%.*f", dec, v);
        WritePrivateProfileStringW(sec, key, b, g_iniPath);
    };
    auto putI = [](const wchar_t* sec, const wchar_t* key, int v) {
        wchar_t b[32];
        swprintf_s(b, L"%d", v);
        WritePrivateProfileStringW(sec, key, b, g_iniPath);
    };
    putF(L"sim", L"density_diffusion", c.densityDissipation, 4);
    putF(L"sim", L"velocity_diffusion", c.velocityDissipation, 4);
    putF(L"sim", L"pressure_diffusion", c.pressureDissipation, 3);
    putI(L"sim", L"pressure_iterations", c.pressureIterations);
    putF(L"sim", L"decay_fast", c.decayFast, 3);
    putF(L"sim", L"decay_threshold", c.decayThreshold, 3);
    putF(L"sim", L"saturation_restore", c.satRestore, 3);
    putF(L"sim", L"max_brightness", c.maxBrightness, 2);
    putF(L"sim", L"vorticity", c.curl, 1);
    putF(L"sim", L"splat_radius", c.splatRadius, 3);
    putI(L"sim", L"shading", c.shading);
    putI(L"sim", L"sim_res", c.simRes);
    putI(L"sim", L"dye_res", c.dyeRes);
    putF(L"hdr", L"knee", c.hdrKnee, 2);
    putF(L"hdr", L"saturation", c.hdrSaturation, 2);
    putF(L"hdr", L"brightness", c.hdrBrightness, 2);
    putF(L"hdr", L"contrast", c.hdrContrast, 2);
    putI(L"hdr", L"compensation", c.hdrCompensation);
    putF(L"behavior", L"color_cycle_period", c.colorCyclePeriod, 0);
    putI(L"behavior", L"wanderers", c.wanderers);
    putI(L"behavior", L"wanderer_count", c.wandererCount);
    putI(L"behavior", L"wanderer_mode", c.wandererMode);
    putF(L"behavior", L"wanderer_speed", c.wandererSpeed, 0);
    putF(L"behavior", L"wanderer_brightness", c.wandererBrightness, 2);
    putF(L"behavior", L"wanderer_scale", c.wandererScale, 2);
    putF(L"behavior", L"wanderer_resume_delay", c.wandererResumeDelay, 1);
    putI(L"behavior", L"auto_pause", c.autoPause);
    putF(L"behavior", L"dark_floor", c.darkFloor, 0);
    putF(L"behavior", L"dark_level", c.darkLevel, 3);
    putF(L"behavior", L"surv_dark_floor", c.survDarkFloor, 0);
    putF(L"behavior", L"contrast_req", c.contrastReq, 0);
    putI(L"behavior", L"dart_enabled", c.dartEnabled);
    putF(L"behavior", L"dart_interval", c.dartInterval, 0);
    putF(L"behavior", L"dart_speed", c.dartSpeed, 0);
    putI(L"behavior", L"hueshift_enabled", c.hsEnabled);
    putF(L"behavior", L"hueshift_step", c.hsStep, 0);
    putF(L"behavior", L"hueshift_linger", c.hsLinger, 1);
    putF(L"behavior", L"hueshift_glide", c.hsGlide, 1);
    putI(L"behavior", L"hueshift_burst_steps", c.hsBurstSteps);
    putF(L"behavior", L"hueshift_off_time", c.hsOffTime, 0);
    putI(L"behavior", L"idle_splats", c.idleSplats);
    putF(L"behavior", L"idle_interval", c.idleInterval, 1);
    putI(L"behavior", L"idle_amount", c.idleAmount);
    putI(L"behavior", L"hold_to_splat", c.holdToSplat);
    putI(L"behavior", L"splat_on_click", c.splatOnClick);
    putI(L"behavior", L"show_mouse", c.showMouse);
    putF(L"general", L"fps_limit", c.fpsLimit, 0);
    putI(L"color", L"colorful", c.colorful);
    putI(L"color", L"more_colors", c.moreColors);
    putF(L"color", L"post_saturation", c.postSaturation, 2);
    putF(L"color", L"post_contrast", c.postContrast, 2);
    putF(L"color", L"post_brightness", c.postBrightness, 2);
    putF(L"color", L"post_hue", c.postHue, 0);
    putF(L"color", L"hue_center", c.hueCenter, 0);
    putF(L"color", L"hue_range", c.hueRange, 0);
    for (int ci = 0; ci < 5; ci++) {
        wchar_t key[32], val[64];
        swprintf_s(key, L"splat_color_%d", ci + 1);
        swprintf_s(val, L"%.4f %.4f %.4f",
                   c.splatColors[ci * 3], c.splatColors[ci * 3 + 1], c.splatColors[ci * 3 + 2]);
        WritePrivateProfileStringW(L"color", key, val, g_iniPath);
    }
    SaveSettings();   // shell globals: pauses, peak, gamut, cycle config
}

static void ApplyPreset(const std::wstring& path) {
    if (!g_renderer) return;
    CloseSettingsWindow();

    // a manual preset load resets any in-flight interlude
    g_inInterlude = false;
    g_nextCycleTick = 0;

    // merge the (possibly partial) preset over the current state
    FluidConfig fresh = g_renderer->Config();
    LoadConfigFromIni(path.c_str(), fresh);

    // shell globals: take the preset's value only when it specifies one
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"hdr", L"peak_nits", L"", buf, 64, path.c_str());
    if (buf[0]) g_hdrPeakNits = (float)_wtof(buf);
    int gm = (int)GetPrivateProfileIntW(L"hdr", L"gamut", g_gamutMode, path.c_str());
    if (gm >= 0 && gm <= 2) g_gamutMode = gm;

    int oldSim = g_renderer->Config().simRes;
    int oldDye = g_renderer->Config().dyeRes;
    g_renderer->Config() = fresh;
    if (fresh.simRes != oldSim || fresh.dyeRes != oldDye)
        g_renderer->SetResolutions(fresh.simRes, fresh.dyeRes);
    else
        g_renderer->ReinitWanderers();

    SaveFullConfig(fresh);
    UpdateTrayTip();

    const wchar_t* name = wcsrchr(path.c_str(), L'\\');
    ShowTrayBalloon(L"Preset applied", name ? name + 1 : path.c_str());
    printf("preset applied: %ls\n", path.c_str());
}

static void SaveCurrentAsPreset() {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    GetPresetsDir(dir);
    CreateDirectoryW(dir, nullptr);
    if (g_renderer) SaveFullConfig(g_renderer->Config());   // ini = live state
    for (int n = 1; n < 100; n++) {
        swprintf_s(path, L"%s\\Preset %d.ini", dir, n);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            CopyFileW(g_iniPath, path, TRUE);
            wchar_t msg[128];
            swprintf_s(msg, L"Saved as \"Preset %d\" — rename the file in the presets folder if you like.", n);
            ShowTrayBalloon(L"Preset saved", msg);
            return;
        }
    }
}

// wrappers for the scenes window (the statics above stay file-local)
void ApplyPresetPath(const std::wstring& path) { ApplyPreset(path); }
void SaveCurrentAsPresetFile() { SaveCurrentAsPreset(); }
void GetPresetsDirectory(wchar_t out[MAX_PATH]) { GetPresetsDir(out); }
void PersistShellSettings() { SaveSettings(); }
void PersistFullConfigNow() { if (g_renderer) SaveFullConfig(g_renderer->Config()); }

// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"FluidWallpaper_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Second launch = "open the settings of the running instance".
        HWND tray = FindWindowW(L"FluidWallpaperTray", nullptr);
        if (tray) {
            PostMessageW(tray, WM_COMMAND, CMD_SETTINGS, 0);
        } else {
            MessageBoxW(nullptr,
                        L"Fluid Wallpaper is already running but not responding.\n"
                        L"Check Task Manager for FluidWallpaper.exe.",
                        L"Fluid Wallpaper", MB_ICONINFORMATION);
        }
        return 0;
    }

    FluidConfig cfg;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"--console") == 0) {
                AllocConsole();
                FILE* f;
                freopen_s(&f, "CONOUT$", "w", stdout);
                freopen_s(&f, "CONOUT$", "w", stderr);
            } else if (wcscmp(argv[i], L"--gradient") == 0) {
                cfg.gradientMode = true;
            } else if (wcscmp(argv[i], L"--stats") == 0) {
                cfg.stats = true;
            } else if (wcscmp(argv[i], L"--force-render") == 0) {
                g_forceRender = true;
            } else if (wcscmp(argv[i], L"--simres") == 0 && i + 1 < argc) {
                cfg.simRes = _wtoi(argv[++i]);
            } else if (wcscmp(argv[i], L"--dyeres") == 0 && i + 1 < argc) {
                cfg.dyeRes = _wtoi(argv[++i]);
            } else if (wcscmp(argv[i], L"--fps") == 0 && i + 1 < argc) {
                g_fpsOverride = (float)_wtof(argv[++i]);
            }
        }
        LocalFree(argv);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitSettingsPath();
    LoadSettings();

    LoadFullConfig(cfg);
    EnsureBuiltinPresets();

    printf("FluidWallpaper - fluid simulation behind desktop icons\n");

    HWND host = FindWallpaperHost();
    if (!host) {
        MessageBoxW(nullptr, L"Could not find the desktop wallpaper layer (is Explorer running?)",
                    L"Fluid Wallpaper", MB_ICONERROR);
        return 1;
    }

    const int width  = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    printf("Primary monitor: %dx%d\n", width, height);

    HWND hwnd = CreateWallpaperWindow(host, width, height);
    g_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);

    FluidRenderer renderer;
    renderer.Init(hwnd, width, height, cfg);
    g_renderer = &renderer;

    CreateTrayWindow();

    g_hdrActive = QueryHDR(g_monitor, &g_maxNits);
    g_sdrWhiteNits = GetSdrWhiteNits(g_monitor);
    UpdateTrayTip();
    printf("HDR live state: %s (max %.0f nits, SDR white %.0f nits)\n",
           g_hdrActive ? "ON" : "OFF", g_maxNits, g_sdrWhiteNits);

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    ULONGLONG lastFsCheck = 0, lastHdrCheck = 0;
    bool wasPaused = false;

    MSG msg = {};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        // Survive Explorer restarts: wait for the new shell, then re-hook.
        // PresentBroken covers the race where the window died mid-frame
        // before its WM_DESTROY reached us.
        if (!g_wallpaperLost && renderer.PresentBroken()) {
            if (IsWindow(hwnd)) DestroyWindow(hwnd);   // triggers the lost flag
            else g_wallpaperLost = true;
        }
        if (g_wallpaperLost) {
            static ULONGLONG lastTry = 0;
            ULONGLONG now2 = GetTickCount64();
            if (now2 - lastTry >= 1000) {
                lastTry = now2;
                HWND newHost = FindWallpaperHost();
                if (newHost) {
                    hwnd = CreateWallpaperWindow(newHost, width, height);
                    g_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                    renderer.Reattach(hwnd);
                    g_wallpaperLost = false;
                    QueryPerformanceCounter(&prev);
                }
            }
            if (g_wallpaperLost) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
                continue;
            }
        }

        ULONGLONG tick = GetTickCount64();
        if (tick - lastFsCheck >= 500) {
            lastFsCheck = tick;
            g_fsPaused = !g_forceRender &&
                         ((g_pauseOnFullscreen && FullscreenAppActive()) ||
                          (g_pauseOnMaximized && MaximizedAppActive()));
        }
        if (tick - lastHdrCheck >= 2000) {
            lastHdrCheck = tick;
            bool hdr = QueryHDR(g_monitor, &g_maxNits);
            float sdrWhite = GetSdrWhiteNits(g_monitor);
            if (sdrWhite != g_sdrWhiteNits) g_sdrWhiteNits = sdrWhite;
            if (hdr != g_hdrActive) {
                g_hdrActive = hdr;
                UpdateTrayTip();
                renderer.ReassertColorSpace();   // HDR transitions can drop it
                SYSTEMTIME st;
                GetLocalTime(&st);
                printf("[%02d:%02d:%02d.%03d] Windows HDR turned %s (max %.0f nits, SDR white %.0f nits)\n",
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       hdr ? "ON" : "OFF", g_maxNits, g_sdrWhiteNits);
            }
        }

        bool paused = g_manualPause || g_fsPaused;
        if (paused != wasPaused) {
            wasPaused = paused;
            SYSTEMTIME st;
            GetLocalTime(&st);
            printf("[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            if (paused && !g_manualPause) {
                wchar_t cls[64] = L"?", title[64] = L"?";
                HWND fg = GetForegroundWindow();
                if (fg) { GetClassNameW(fg, cls, 64); GetWindowTextW(fg, title, 64); }
                printf("paused (%s: class='%ls' title='%ls')\n",
                       IsZoomed(fg) ? "maximized app" : "fullscreen app", cls, title);
            } else {
                printf(paused ? "paused (manual)\n" : "resumed\n");
            }
        }

        if (paused) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
            QueryPerformanceCounter(&prev);   // don't integrate the paused gap
            continue;
        }

        // Preset interlude cycling: N s of the user's settings, M s of the
        // chosen (or random) preset painting into the same fluid field, with
        // ±30% timing jitter so it feels organic rather than metronomic.
        if (g_cycleEnabled) {
            if (g_nextCycleTick == 0)
                g_nextCycleTick = tick + (ULONGLONG)(g_cycleBaseSec * 1000);
            if (tick >= g_nextCycleTick) {
                float jitter = 0.7f + 0.6f * ((float)rand() / RAND_MAX);
                if (!g_inInterlude) {
                    wchar_t dir[MAX_PATH];
                    GetPresetsDir(dir);
                    std::wstring path;
                    if (g_cyclePreset == L"*") {
                        std::vector<std::wstring> all;
                        wchar_t pattern[MAX_PATH];
                        swprintf_s(pattern, L"%s\\*.ini", dir);
                        WIN32_FIND_DATAW fd;
                        HANDLE find = FindFirstFileW(pattern, &fd);
                        if (find != INVALID_HANDLE_VALUE) {
                            do {
                                all.push_back(std::wstring(dir) + L"\\" + fd.cFileName);
                            } while (FindNextFileW(find, &fd));
                            FindClose(find);
                        }
                        if (!all.empty()) path = all[rand() % all.size()];
                    } else {
                        path = std::wstring(dir) + L"\\" + g_cyclePreset;
                        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
                            path.clear();
                    }
                    if (!path.empty()) {
                        g_cycleBaseCfg = renderer.Config();
                        FluidConfig il = g_cycleBaseCfg;
                        LoadConfigFromIni(path.c_str(), il);
                        // interludes change the look only — never resolution/perf
                        il.simRes = g_cycleBaseCfg.simRes;
                        il.dyeRes = g_cycleBaseCfg.dyeRes;
                        il.fpsLimit = g_cycleBaseCfg.fpsLimit;
                        renderer.Config() = il;
                        renderer.ReinitWanderers();
                        g_inInterlude = true;
                        g_nextCycleTick = tick + (ULONGLONG)(g_cycleInterludeSec * 1000 * jitter);
                    } else {
                        g_nextCycleTick = tick + 10000;
                    }
                } else {
                    renderer.Config() = g_cycleBaseCfg;
                    renderer.ReinitWanderers();
                    g_inInterlude = false;
                    g_nextCycleTick = tick + (ULONGLONG)(g_cycleBaseSec * 1000 * jitter);
                }
            }
        } else if (g_inInterlude) {
            // cycling switched off mid-interlude: restore the base look
            renderer.Config() = g_cycleBaseCfg;
            renderer.ReinitWanderers();
            g_inInterlude = false;
        }

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);

        // FPS cap from settings (Present is also vsynced)
        float fpsLim = g_fpsOverride > 0.0f ? g_fpsOverride : renderer.Config().fpsLimit;
        if (fpsLim < 10.0f) fpsLim = 10.0f;
        if (dt < 1.0f / (fpsLim + 2.0f)) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
            continue;
        }
        prev = now;
        if (dt > 0.0001f)
            g_currentFps = g_currentFps * 0.95f + (1.0f / dt) * 0.05f;

        // Mouse input, Wallpaper Engine style: cursor movement reaches the
        // wallpaper regardless of focus; clicks only when the desktop itself
        // is focused. Coordinates are primary-monitor px (our window is at 0,0).
        FrameInput fin;
        {
            static POINT prevCursor = { LONG_MIN, LONG_MIN };
            POINT cp;
            GetCursorPos(&cp);
            bool onMonitor = MonitorFromPoint(cp, MONITOR_DEFAULTTONULL) == g_monitor;
            HWND fg = GetForegroundWindow();
            wchar_t cls[64] = {};
            if (fg) GetClassNameW(fg, cls, 64);
            bool desktopFocused = wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;

            if (prevCursor.x != LONG_MIN && (cp.x != prevCursor.x || cp.y != prevCursor.y) && onMonitor) {
                fin.mouseMoved = true;
                fin.mouseDx = (float)(cp.x - prevCursor.x) * 5.0f;
                fin.mouseDy = (float)(cp.y - prevCursor.y) * 5.0f;
                fin.userInteracted = true;
            }
            if (desktopFocused && onMonitor && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                fin.mouseDown = true;
                fin.userInteracted = true;
            }
            fin.mouseX = (float)cp.x;
            fin.mouseY = (float)cp.y;
            prevCursor = cp;
        }

        // SDR mode: DWM maps scRGB 1.0 to panel white, so scale 1. HDR mode:
        // match the user's SDR-content brightness, like the reference did.
        float sdrScale = g_hdrActive ? (g_sdrWhiteNits / 80.0f) : 1.0f;
        float peak = g_hdrPeakNits < 0.0f ? g_maxNits : g_hdrPeakNits;   // -1 = panel max
        renderer.SetHdrOptions(peak, g_gamutMode);
        renderer.Frame(dt, sdrScale, g_hdrActive, fin);
    }

    printf("Shutting down...\n");
    g_shuttingDown = true;
    RemoveTrayIcon();
    renderer.Shutdown();
    if (!g_wallpaperLost) DestroyWindow(hwnd);
    ReleaseMutex(mutex);

    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_SENDCHANGE);
    return 0;
}
