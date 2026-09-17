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
//   --test-suspend     force one renderer suspend ~3 s in, resume ~8 s (test)
//
// Headless capture (never shows anything, never touches the live config):
//   --shot <out.png>   render OFFSCREEN and write <out>.png + <out>-hdr.png
//   --shot-size WxH    capture size (default 2560x1440)
//   --shot-delay N     seconds of wallpaper time to simulate first (default 40)
//   --shot-series N:S  N captures, S seconds apart, named by elapsed seconds
//   --seed N           seed every rand() behavior (default 1234) -> determinism
//   --ini <path>       read config from this file instead of the live ini
//   --hdr on|off       set the HDR state instead of querying the display
//   --sdr-white <nits> SDR-content brightness for HDR mode (default 240)
//   --panel-max <nits> stands in for the DXGI-reported max (peak_nits=-1 only)
//   --mouse-none       no mouse splats (the default in shot mode)

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cmath>
#include <share.h>
#include <vector>
#include <string>
#include "fluid.h"
#include "app_state.h"
#include "moods.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

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
FluidRenderer*  g_renderer = nullptr;
static bool     g_forceRender = false;
static bool     g_testSuspend = false;   // --test-suspend: scripted suspend/resume cycle
static float    g_fpsOverride = 0.0f;   // --fps test flag; 0 = use settings
float           g_currentFps = 0.0f;    // smoothed achieved framerate

void RequestExit()   { g_running = false; }
void TogglePause()   { g_manualPause = !g_manualPause; }
bool IsManualPaused() { return g_manualPause; }
static bool     g_fsPaused = false;
static HMONITOR g_monitor = nullptr;
wchar_t         g_iniPath[MAX_PATH] = {};
wchar_t         g_configIniPath[MAX_PATH] = {};   // == g_iniPath unless --ini
bool            g_configReadOnly = false;         // --shot: never write config

static void InitSettingsPath() {
    wchar_t* appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        wchar_t dir[MAX_PATH];
        _snwprintf_s(dir, _TRUNCATE, L"%s\\FluidWallpaper", appdata);
        CoTaskMemFree(appdata);
        CreateDirectoryW(dir, nullptr);
        _snwprintf_s(g_iniPath, _TRUNCATE, L"%s\\settings.ini", dir);
    }
    wcscpy_s(g_configIniPath, MAX_PATH, g_iniPath);
}

static void LoadSettings() {
    if (!g_configIniPath[0]) return;
    g_pauseOnFullscreen = GetPrivateProfileIntW(L"general", L"pause_on_fullscreen", 1, g_configIniPath) != 0;
    g_pauseOnMaximized = GetPrivateProfileIntW(L"general", L"pause_on_maximized", 1, g_configIniPath) != 0;
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"hdr", L"peak_nits", L"-1", buf, 64, g_configIniPath);
    g_hdrPeakNits = (float)_wtof(buf);
    g_gamutMode = (int)GetPrivateProfileIntW(L"hdr", L"gamut", 2, g_configIniPath);
    if (g_gamutMode < 0 || g_gamutMode > 2) g_gamutMode = 2;
}

static void SaveSettings() {
    if (!g_iniPath[0] || g_configReadOnly) return;
    WritePrivateProfileStringW(L"general", L"pause_on_fullscreen",
                               g_pauseOnFullscreen ? L"1" : L"0", g_iniPath);
    WritePrivateProfileStringW(L"general", L"pause_on_maximized",
                               g_pauseOnMaximized ? L"1" : L"0", g_iniPath);
    wchar_t buf[32];
    swprintf_s(buf, L"%.0f", g_hdrPeakNits);
    WritePrivateProfileStringW(L"hdr", L"peak_nits", buf, g_iniPath);
    swprintf_s(buf, L"%d", g_gamutMode);
    WritePrivateProfileStringW(L"hdr", L"gamut", buf, g_iniPath);
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
    cfg.baroclinic          = getF(L"sim", L"baroclinic", cfg.baroclinic);
    cfg.flowSpeed           = getF(L"sim", L"flow_speed", cfg.flowSpeed);
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
    cfg.idleBrightness      = getF(L"behavior", L"idle_brightness", cfg.idleBrightness);
    cfg.holdToSplat         = getB(L"behavior", L"hold_to_splat", cfg.holdToSplat);
    cfg.splatOnClick        = getB(L"behavior", L"splat_on_click", cfg.splatOnClick);
    cfg.showMouse           = getB(L"behavior", L"show_mouse", cfg.showMouse);
    cfg.fpsLimit            = getF(L"general", L"fps_limit", cfg.fpsLimit);
    cfg.mirrorSecond        = getB(L"general", L"mirror_second", cfg.mirrorSecond);
    cfg.colorful            = getB(L"color", L"colorful", cfg.colorful);
    cfg.moreColors          = getB(L"color", L"more_colors", cfg.moreColors);
    cfg.postSaturation      = getF(L"color", L"post_saturation", cfg.postSaturation);
    cfg.postContrast        = getF(L"color", L"post_contrast", cfg.postContrast);
    cfg.postBrightness      = getF(L"color", L"post_brightness", cfg.postBrightness);
    cfg.postHue             = getF(L"color", L"post_hue", cfg.postHue);
    cfg.hueCenter           = getF(L"color", L"hue_center", cfg.hueCenter);
    cfg.hueRange            = getF(L"color", L"hue_range", cfg.hueRange);
    cfg.hueLinger           = getF(L"color", L"hue_linger", cfg.hueLinger);
    cfg.dyeDiffusion        = getF(L"sim", L"dye_diffusion", cfg.dyeDiffusion);
    cfg.curveEnabled        = getB(L"color", L"curve_enabled", cfg.curveEnabled);
    cfg.curveCenter         = getF(L"color", L"curve_center", cfg.curveCenter);
    cfg.curveWidth          = getF(L"color", L"curve_width", cfg.curveWidth);
    cfg.curveHeight         = getF(L"color", L"curve_height", cfg.curveHeight);
    cfg.shadowFloor         = getF(L"color", L"shadow_floor", cfg.shadowFloor);
    cfg.shadowKnee          = getF(L"color", L"shadow_knee", cfg.shadowKnee);
    // ---- render look selector + "Liquid Acid" parameters -------------------
    // [look] style = fluid | liquid_acid   (default fluid — the normal look).
    // The settings-window checkbox writes the equivalent int key
    // [look] liquid_acid = 0|1; the string form wins when both are present.
    {
        wchar_t style[32] = {};
        GetPrivateProfileStringW(L"look", L"style", L"", style, 32, ini);
        if (style[0]) cfg.acid.enabled = (_wcsicmp(style, L"liquid_acid") == 0);
        cfg.acid.enabled = getB(L"look", L"liquid_acid", cfg.acid.enabled);
    }
    {
        LiquidAcidConfig& a = cfg.acid;
        const wchar_t* S = L"liquid_acid";
        a.blobCount    = getI(S, L"blob_count", a.blobCount);
        a.discFrac     = getF(S, L"disc_frac", a.discFrac);
        a.webFrac      = getF(S, L"web_frac", a.webFrac);
        a.bubbleFrac   = getF(S, L"bubble_frac", a.bubbleFrac);
        a.discMin      = getF(S, L"disc_min", a.discMin);
        a.discMax      = getF(S, L"disc_max", a.discMax);
        a.webMin       = getF(S, L"web_min", a.webMin);
        a.webMax       = getF(S, L"web_max", a.webMax);
        a.bubbleMin    = getF(S, L"bubble_min", a.bubbleMin);
        a.bubbleMax    = getF(S, L"bubble_max", a.bubbleMax);
        a.holeMin      = getF(S, L"hole_min", a.holeMin);
        a.holeMax      = getF(S, L"hole_max", a.holeMax);
        a.sizeBias     = getF(S, L"size_bias", a.sizeBias);
        a.bigBias      = getF(S, L"big_bias", a.bigBias);
        a.holeWeight   = getF(S, L"hole_weight", a.holeWeight);
        a.threshold    = getF(S, L"threshold", a.threshold);
        a.supportScale = getF(S, L"support_scale", a.supportScale);
        a.aaScale      = getF(S, L"aa_scale", a.aaScale);
        a.flowGain     = getF(S, L"flow_gain", a.flowGain);
        a.curlDrift    = getF(S, L"curl_drift", a.curlDrift);
        a.repulsion    = getF(S, L"repulsion", a.repulsion);
        a.buoyancy     = getF(S, L"buoyancy", a.buoyancy);
        a.damping      = getF(S, L"damping", a.damping);
        a.breathAmt    = getF(S, L"breath", a.breathAmt);
        a.wrapMargin   = getF(S, L"wrap_margin", a.wrapMargin);
        a.rimWidth     = getF(S, L"rim_width", a.rimWidth);
        a.rimInset     = getF(S, L"rim_inset", a.rimInset);
        a.rimDark      = getF(S, L"rim_dark", a.rimDark);
        a.meniscus     = getF(S, L"meniscus", a.meniscus);
        a.meniscusW    = getF(S, L"meniscus_width", a.meniscusW);
        a.meniscusOff  = getF(S, L"meniscus_offset", a.meniscusOff);
        a.refraction   = getF(S, L"refraction", a.refraction);
        a.translucency = getF(S, L"translucency", a.translucency);
        a.oilTexture   = getF(S, L"oil_texture", a.oilTexture);
        a.inkShading   = getF(S, L"ink_shading", a.inkShading);
        a.oilHdr       = getF(S, L"oil_hdr", a.oilHdr);
        a.rimHdr       = getF(S, L"rim_hdr", a.rimHdr);
        a.inkLevels    = getF(S, L"ink_levels", a.inkLevels);
        a.inkSoft      = getF(S, L"ink_soft", a.inkSoft);
        a.inkMix       = getF(S, L"ink_mix", a.inkMix);
        a.inkHueVary   = getF(S, L"ink_hue_vary", a.inkHueVary);
        a.inkGain      = getF(S, L"ink_gain", a.inkGain);
        a.inkBias      = getF(S, L"ink_bias", a.inkBias);
        a.seamStrength = getF(S, L"seam_strength", a.seamStrength);
        a.seamLo       = getF(S, L"seam_lo", a.seamLo);
        a.seamHi       = getF(S, L"seam_hi", a.seamHi);
        a.seamScale    = getF(S, L"seam_scale", a.seamScale);
        a.grainAmt     = getF(S, L"grain", a.grainAmt);
        a.grainScale   = getF(S, L"grain_scale", a.grainScale);
        a.speckle      = getF(S, L"speckle", a.speckle);
        a.speckScale   = getF(S, L"speckle_scale", a.speckScale);
        a.swarmHoles   = getF(S, L"swarm_holes", a.swarmHoles);
        a.swarmDrops   = getF(S, L"swarm_drops", a.swarmDrops);
        a.swarmDensity = getF(S, L"swarm_density", a.swarmDensity);
        a.swarmScaleA  = getF(S, L"swarm_scale_holes", a.swarmScaleA);
        a.swarmScaleB  = getF(S, L"swarm_scale_drops", a.swarmScaleB);
        a.swarmRMin    = getF(S, L"swarm_r_min", a.swarmRMin);
        a.swarmRMax    = getF(S, L"swarm_r_max", a.swarmRMax);
        a.swarmRimDark = getF(S, L"swarm_rim_dark", a.swarmRimDark);
        a.swarmDrift   = getF(S, L"swarm_drift", a.swarmDrift);
        a.swarmClump   = getF(S, L"swarm_clump", a.swarmClump);
        a.swarmDark    = getF(S, L"swarm_dark", a.swarmDark);
        {   // meniscus halo colour
            wchar_t buf[64] = {}; float r, g, b;
            GetPrivateProfileStringW(S, L"meniscus_color", L"", buf, 64, ini);
            if (swscanf_s(buf, L"%f %f %f", &r, &g, &b) == 3) {
                a.meniscusCol[0] = r; a.meniscusCol[1] = g; a.meniscusCol[2] = b;
            }
        }
        // oil_color_1..4 and ink_stop_1..4, "r g b" floats like splat_color_N
        for (int ci = 0; ci < 4; ci++) {
            wchar_t key[32], buf[64] = {};
            float r, g, b;
            swprintf_s(key, L"oil_color_%d", ci + 1);
            GetPrivateProfileStringW(S, key, L"", buf, 64, ini);
            if (swscanf_s(buf, L"%f %f %f", &r, &g, &b) == 3) {
                a.oilColors[ci * 3 + 0] = r; a.oilColors[ci * 3 + 1] = g; a.oilColors[ci * 3 + 2] = b;
            }
            buf[0] = 0;
            swprintf_s(key, L"ink_stop_%d", ci + 1);
            GetPrivateProfileStringW(S, key, L"", buf, 64, ini);
            if (swscanf_s(buf, L"%f %f %f", &r, &g, &b) == 3) {
                a.inkRamp[ci * 3 + 0] = r; a.inkRamp[ci * 3 + 1] = g; a.inkRamp[ci * 3 + 2] = b;
            }
        }
    }
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
    printf("config loaded: density=%.3f decay_fast=%.2f vorticity=%.0f sim=%d dye=%d look=%s\n",
           cfg.densityDissipation, cfg.decayFast, cfg.curl, cfg.simRes, cfg.dyeRes,
           cfg.acid.enabled ? "liquid_acid" : "fluid");
}

static void LoadFullConfig(FluidConfig& cfg) { LoadConfigFromIni(g_configIniPath, cfg); }

void LoadConfigFromFile(const wchar_t* ini, FluidConfig& cfg) { LoadConfigFromIni(ini, cfg); }

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
static bool g_destroyingMirror = false;   // intentional teardown, not shell loss
static HMONITOR g_monitor2 = nullptr;
static RECT g_monitor2Rect = {};

static BOOL CALLBACK FindSecondMonitor(HMONITOR mon, HDC, LPRECT rc, LPARAM) {
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    if (!(mi.dwFlags & MONITORINFOF_PRIMARY)) {
        g_monitor2 = mon;
        g_monitor2Rect = *rc;
    }
    return TRUE;
}

static LRESULT CALLBACK WallpaperWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (g_shuttingDown) {
            PostQuitMessage(0);
        } else if (g_destroyingMirror) {
            // deliberate mirror teardown — not a shell restart
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

static HWND CreateWallpaperWindowAt(HWND host, int screenX, int screenY,
                                    int width, int height) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WallpaperWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FluidWallpaperWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);   // fails harmlessly after the first call

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Fluid Wallpaper",
        WS_POPUP, 0, 0, width, height,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) Fail("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));

    SetParent(hwnd, host);
    POINT origin = { screenX, screenY };
    ScreenToClient(host, &origin);
    SetWindowPos(hwnd, HWND_TOP, origin.x, origin.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return hwnd;
}

static HWND CreateWallpaperWindow(HWND host, int width, int height) {
    return CreateWallpaperWindowAt(host, 0, 0, width, height);
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

static bool MonitorIsOurs(HMONITOR m) {
    if (m == g_monitor) return true;
    return g_monitor2 && m == g_monitor2 &&
           g_renderer && g_renderer->MirrorActive();
}

static bool FullscreenAppActive() {
    HWND fg = GetForegroundWindow();
    if (!fg || IsShellOrOwnWindow(fg)) return false;
    HMONITOR fgMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    if (!MonitorIsOurs(fgMon)) return false;

    // Maximized windows are not fullscreen — including borderless
    // custom-titlebar apps (Electron, Windows Terminal). True fullscreen is a
    // non-maximized borderless popup sized to the monitor.
    if (IsZoomed(fg)) return false;
    LONG style = GetWindowLongW(fg, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION) return false;
    if (style & WS_THICKFRAME) return false;

    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(fgMon, &mi)) return false;
    RECT r;
    if (!GetWindowRect(fg, &r)) return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

static bool MaximizedAppActive() {
    HWND fg = GetForegroundWindow();
    if (!fg || IsShellOrOwnWindow(fg)) return false;
    if (!MonitorIsOurs(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST))) return false;
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
    CMD_PRESET_SAVE = 30, CMD_PRESET_FOLDER = 31,
    CMD_MOODS_TOGGLE = 33, CMD_MOODS_NEXT = 34, CMD_MOOD_BASE = 800,
    CMD_PRESET_BASE = 600,
    CMD_PEAK_OFF = 10, CMD_PEAK_AUTO = 11, CMD_PEAK_300 = 12, CMD_PEAK_600 = 13,
    CMD_PEAK_800 = 15, CMD_PEAK_1000 = 14,
    CMD_GAMUT_SRGB = 20, CMD_GAMUT_P3 = 21, CMD_GAMUT_2020 = 22,
};

static NOTIFYICONDATAW g_nid = {};

// presets (implementations further down; the menu needs them declared)
static std::vector<std::wstring> g_presetPaths;
// backing store for owner-drawn (skipped) mood menu item text — the pointers
// handed to AppendMenuW must stay valid for the menu's lifetime
static std::vector<std::wstring> g_moodMenuLabels;
static void GetPresetsDir(wchar_t out[MAX_PATH]);
static void ApplyPreset(const std::wstring& path);
static void SaveCurrentAsPreset();
static void ShowTrayMenuBody(HWND hwnd, HMENU presets);

static void ShowTrayMenu(HWND hwnd) {
    // enumerate mood files fresh each time the menu opens (one recipe folder)
    g_presetPaths.clear();
    HMENU presets = CreatePopupMenu();
    {
        wchar_t dir[MAX_PATH], pattern[MAX_PATH];
        MoodsGetDirectory(dir);
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
            AppendMenuW(presets, MF_STRING | MF_GRAYED, 0, L"(no moods yet)");
        AppendMenuW(presets, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(presets, MF_STRING, CMD_PRESET_SAVE, L"Save current as new preset");
        AppendMenuW(presets, MF_STRING, CMD_PRESET_FOLDER, L"Open moods folder");
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

    // Mood conductor: choreographed cycling between moods\*.ini look recipes
    HMENU moods = CreatePopupMenu();
    AppendMenuW(moods, MF_STRING | (g_moodSettings.enabled ? MF_CHECKED : 0),
                CMD_MOODS_TOGGLE, L"Cycle moods");
    AppendMenuW(moods, MF_STRING, CMD_MOODS_NEXT, L"Next mood now");
    AppendMenuW(moods, MF_SEPARATOR, 0, nullptr);
    {
        const auto& names = MoodsNames();
        int cur = MoodsCurrentIndex();
        // skipped moods are owner-drawn with gray text: looks like MF_GRAYED
        // but stays clickable, so forcing a skipped mood keeps working
        g_moodMenuLabels.clear();
        g_moodMenuLabels.reserve(names.size() < 100 ? names.size() : 100);
        for (int i = 0; i < (int)names.size() && i < 100; i++) {
            if (MoodsIsSkipped(i)) {
                g_moodMenuLabels.push_back(names[i]);
                AppendMenuW(moods, MF_OWNERDRAW | (i == cur ? MF_CHECKED : 0),
                            CMD_MOOD_BASE + i, g_moodMenuLabels.back().c_str());
            } else {
                AppendMenuW(moods, MF_STRING | (i == cur ? MF_CHECKED : 0),
                            CMD_MOOD_BASE + i, names[i].c_str());
            }
        }
        if (names.empty())
            AppendMenuW(moods, MF_STRING | MF_GRAYED, 0, L"(no moods found)");
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)moods, L"Moods");

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

// menu font for the owner-drawn tray items, cached for the process lifetime
static HFONT TrayMenuFont() {
    static HFONT s_menuFont = nullptr;
    if (!s_menuFont) {
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            s_menuFont = CreateFontIndirectW(&ncm.lfMenuFont);
    }
    return s_menuFont;
}

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
    // owner-drawn (skipped) mood items: gray text, but still clickable
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lp;
        if (mi->CtlType == ODT_MENU && mi->itemData) {
            const wchar_t* text = (const wchar_t*)mi->itemData;
            HDC dc = GetDC(hwnd);
            HFONT oldFont = nullptr;
            if (HFONT f = TrayMenuFont()) oldFont = (HFONT)SelectObject(dc, f);
            SIZE sz = {};
            GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
            if (oldFont) SelectObject(dc, oldFont);
            ReleaseDC(hwnd, dc);
            mi->itemWidth = sz.cx + 34;   // check gutter + padding
            mi->itemHeight = sz.cy + 8 > 22 ? sz.cy + 8 : 22;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlType == ODT_MENU && di->itemData) {
            const wchar_t* text = (const wchar_t*)di->itemData;
            const bool sel = (di->itemState & ODS_SELECTED) != 0;
            FillRect(di->hDC, &di->rcItem,
                     GetSysColorBrush(sel ? COLOR_MENUHILIGHT : COLOR_MENU));
            SetBkMode(di->hDC, TRANSPARENT);
            const COLORREF fg = GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT);
            SetTextColor(di->hDC, fg);
            HFONT oldFont = nullptr;
            if (HFONT f = TrayMenuFont()) oldFont = (HFONT)SelectObject(di->hDC, f);
            RECT tr = di->rcItem;
            tr.left += 26;   // leave the check gutter empty
            if (di->itemState & ODS_CHECKED) {
                // owner-draw items get no stock checkmark — draw one
                HPEN pen = CreatePen(PS_SOLID, 1, fg);
                HPEN oldPen = (HPEN)SelectObject(di->hDC, pen);
                const int cx = di->rcItem.left + 12, cy = (tr.top + tr.bottom) / 2;
                MoveToEx(di->hDC, cx - 5, cy, nullptr);
                LineTo(di->hDC, cx - 1, cy + 4);
                LineTo(di->hDC, cx + 6, cy - 5);
                SelectObject(di->hDC, oldPen);
                DeleteObject(pen);
            }
            DrawTextW(di->hDC, text, -1, &tr, DT_SINGLELINE | DT_VCENTER);
            if (oldFont) SelectObject(di->hDC, oldFont);
            return TRUE;
        }
        break;
    }
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
        case CMD_MOODS_TOGGLE:
            MoodsSetEnabled(!g_moodSettings.enabled);
            printf("mood cycling: %s\n", g_moodSettings.enabled ? "on" : "off");
            break;
        case CMD_MOODS_NEXT:
            if (g_renderer) MoodsNext(*g_renderer);
            break;
        case CMD_PRESET_FOLDER: {
            wchar_t dir[MAX_PATH];
            MoodsGetDirectory(dir);
            ShellExecuteW(nullptr, L"open", dir, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        default:
            if (LOWORD(wp) >= CMD_PRESET_BASE && LOWORD(wp) < CMD_PRESET_BASE + 100 &&
                LOWORD(wp) - CMD_PRESET_BASE < g_presetPaths.size()) {
                ApplyPreset(g_presetPaths[LOWORD(wp) - CMD_PRESET_BASE]);
            } else if (LOWORD(wp) >= CMD_MOOD_BASE && LOWORD(wp) < CMD_MOOD_BASE + 100) {
                if (g_renderer) MoodsForceMood(*g_renderer, LOWORD(wp) - CMD_MOOD_BASE);
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
// True WE parity (rebuilt 2026-09-16): every key the look depends on, from WE
// config.json (wproperties + wec_* panel: sat 57 / con 67 / brs 53 / hue 54).
// sim_res/dye_res intentionally absent - presets never touch them.
L"[moods]\r\nenabled=0\r\n"
L"[hdr]\r\npeak_nits=700\r\ncompensation=1\r\nknee=0.70\r\nsaturation=1.20\r\nbrightness=1.08\r\ncontrast=1.00\r\ngamut=1\r\n"
L"[sim]\r\nvorticity=48\r\nsplat_radius=0.64\r\ndensity_diffusion=0.999\r\nvelocity_diffusion=0.999\r\n"
L"pressure_diffusion=0.85\r\npressure_iterations=20\r\ndecay_fast=1.000\r\ndecay_threshold=0.290\r\n"
L"saturation_restore=0.93\r\nmax_brightness=1.35\r\nshading=1\r\ndye_diffusion=0.000\r\nbaroclinic=0.000\r\nflow_speed=1.00\r\n"
L"[behavior]\r\ncolor_cycle_period=19\r\nwanderers=1\r\nwanderer_count=2\r\nwanderer_mode=0\r\n"
L"wanderer_speed=246\r\nwanderer_brightness=0.10\r\nwanderer_scale=0.10\r\nwanderer_resume_delay=4.5\r\n"
L"auto_pause=1\r\ndark_floor=9\r\ndark_level=0.070\r\nsurv_dark_floor=8\r\ncontrast_req=30\r\n"
L"dart_enabled=1\r\ndart_interval=7\r\ndart_speed=967\r\n"
L"hueshift_enabled=1\r\nhueshift_step=83\r\nhueshift_linger=6.5\r\nhueshift_glide=7.2\r\n"
L"hueshift_burst_steps=2\r\nhueshift_off_time=10\r\n"
L"idle_splats=1\r\nidle_interval=9.6\r\nidle_amount=8\r\nidle_brightness=1.50\r\n"
L"hold_to_splat=1\r\nsplat_on_click=1\r\nshow_mouse=1\r\n"
L"[color]\r\ncolorful=1\r\nmore_colors=1\r\npost_saturation=1.14\r\npost_contrast=1.34\r\n"
L"post_brightness=1.06\r\npost_hue=14.4\r\nhue_center=0\r\nhue_range=180\r\nhue_linger=0\r\n"
L"curve_enabled=0\r\nshadow_floor=0.100\r\nshadow_knee=0.15\r\n"
L"[general]\r\npause_on_fullscreen=1\r\npause_on_maximized=1\r\n";

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

// User-requested "super weird" scene: huge ember-colored bubbles pushed
// through the response-curve hump, so mid-brightness rims glow at max while
// splat cores drop back — bright outlines of every blob. Soft fronts via a
// touch of dye diffusion and a near-zero decay threshold.
static const wchar_t* kEmberRimsIni =
L"[color]\r\ncolorful=1\r\nhue_center=22\r\nhue_range=16\r\n"
L"curve_enabled=1\r\ncurve_center=0.30\r\ncurve_width=0.10\r\ncurve_height=1.30\r\n"
L"post_saturation=1.10\r\npost_contrast=1.15\r\npost_brightness=1.00\r\npost_hue=0\r\n"
L"[sim]\r\nsplat_radius=0.900\r\nmax_brightness=1.00\r\ndensity_diffusion=0.9990\r\n"
L"decay_fast=0.995\r\ndecay_threshold=0.020\r\nsaturation_restore=0.700\r\n"
L"vorticity=42\r\ndye_diffusion=0.050\r\n"
L"[behavior]\r\nwanderer_count=2\r\nwanderer_brightness=0.15\r\ndark_floor=25\r\n"
L"idle_amount=3\r\nidle_interval=8\r\ncolor_cycle_period=40\r\n";

static void EnsureBuiltinPresets() {
    if (!g_iniPath[0] || g_configReadOnly) return;
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
    swprintf_s(path, L"%s\\Ember Rims.ini", dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        WriteTextFileUtf16(path, kEmberRimsIni);

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

// Persist the complete merged config to an ini file (presets can be partial,
// so after applying one, the resolved state must be written out in full).
// includeShell=false skips the machine/shell keys (sim_res, dye_res,
// fps_limit, mirror_second) — used for mood files, which never touch them.
void WriteConfigToIni(const wchar_t* path, const FluidConfig& c, bool includeShell) {
    if (!path || !path[0] || g_configReadOnly) return;
    auto putF = [path](const wchar_t* sec, const wchar_t* key, float v, int dec) {
        wchar_t b[48];
        swprintf_s(b, L"%.*f", dec, v);
        WritePrivateProfileStringW(sec, key, b, path);
    };
    auto putI = [path](const wchar_t* sec, const wchar_t* key, int v) {
        wchar_t b[32];
        swprintf_s(b, L"%d", v);
        WritePrivateProfileStringW(sec, key, b, path);
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
    putF(L"sim", L"baroclinic", c.baroclinic, 1);
    putF(L"sim", L"flow_speed", c.flowSpeed, 2);
    putF(L"sim", L"splat_radius", c.splatRadius, 3);
    putI(L"sim", L"shading", c.shading);
    if (includeShell) {
        putI(L"sim", L"sim_res", c.simRes);
        putI(L"sim", L"dye_res", c.dyeRes);
    }
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
    putF(L"behavior", L"idle_brightness", c.idleBrightness, 2);
    putI(L"behavior", L"hold_to_splat", c.holdToSplat);
    putI(L"behavior", L"splat_on_click", c.splatOnClick);
    putI(L"behavior", L"show_mouse", c.showMouse);
    if (includeShell) {
        putF(L"general", L"fps_limit", c.fpsLimit, 0);
        putI(L"general", L"mirror_second", c.mirrorSecond);
    }
    putI(L"color", L"colorful", c.colorful);
    putI(L"color", L"more_colors", c.moreColors);
    putF(L"color", L"post_saturation", c.postSaturation, 2);
    putF(L"color", L"post_contrast", c.postContrast, 2);
    putF(L"color", L"post_brightness", c.postBrightness, 2);
    putF(L"color", L"post_hue", c.postHue, 0);
    putF(L"color", L"hue_center", c.hueCenter, 0);
    putF(L"color", L"hue_range", c.hueRange, 0);
    putF(L"color", L"hue_linger", c.hueLinger, 2);
    putF(L"sim", L"dye_diffusion", c.dyeDiffusion, 3);
    putI(L"color", L"curve_enabled", c.curveEnabled);
    putF(L"color", L"curve_center", c.curveCenter, 2);
    putF(L"color", L"curve_width", c.curveWidth, 2);
    putF(L"color", L"curve_height", c.curveHeight, 2);
    putF(L"color", L"shadow_floor", c.shadowFloor, 3);
    putF(L"color", L"shadow_knee", c.shadowKnee, 2);
    for (int ci = 0; ci < 5; ci++) {
        wchar_t key[32], val[64];
        swprintf_s(key, L"splat_color_%d", ci + 1);
        swprintf_s(val, L"%.4f %.4f %.4f",
                   c.splatColors[ci * 3], c.splatColors[ci * 3 + 1], c.splatColors[ci * 3 + 2]);
        WritePrivateProfileStringW(L"color", key, val, path);
    }
}

static void SaveFullConfig(const FluidConfig& c) {
    if (!g_iniPath[0]) return;
    WriteConfigToIni(g_iniPath, c, true);
    SaveSettings();   // shell globals: pauses, peak, gamut, cycle config
}

static void ApplyPreset(const std::wstring& path) {
    if (!g_renderer) return;
    CloseSettingsWindow();

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
    MoodsAdoptPath(*g_renderer, path);   // keep conductor label/journey in sync

    const wchar_t* name = wcsrchr(path.c_str(), L'\\');
    ShowTrayBalloon(L"Preset applied", name ? name + 1 : path.c_str());
    printf("preset applied: %ls\n", path.c_str());
}

static void SaveCurrentAsPreset() {
    // one managed recipe system: new snapshots are mood files (no shell keys)
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    MoodsGetDirectory(dir);
    CreateDirectoryW(dir, nullptr);
    if (g_renderer) SaveFullConfig(g_renderer->Config());   // ini = live state
    for (int n = 1; n < 100; n++) {
        swprintf_s(path, L"%s\\Preset %d.ini", dir, n);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            if (g_renderer) WriteConfigToIni(path, g_renderer->Config(), false);
            MoodsRescan();
            wchar_t msg[128];
            swprintf_s(msg, L"Saved as \"Preset %d\" — rename the file in the moods folder if you like.", n);
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
// --shot: headless deterministic capture
//
// Renders the wallpaper offscreen (no window, no WorkerW, no tray, no focus
// change, no writes to the live config) and dumps the display pass to PNG.
// The renderer runs on a FIXED timestep with no vsync and no sleeping, so
// "40 seconds of wallpaper" costs a few seconds of wall clock.
// ---------------------------------------------------------------------------

struct ShotOpts {
    std::wstring out;
    std::wstring ini;
    int      width = 2560, height = 1440;
    float    delaySec = 40.0f;
    int      seriesCount = 1;       // --shot-series N:interval
    float    seriesInterval = 0.0f;
    unsigned seed = 1234;
    bool     hdrOn = false;
    float    sdrWhiteNits = 240.0f;
    float    panelMaxNits = 1000.0f;   // stands in for the DXGI-reported max
    bool     mouseNone = true;
    int      yieldMs = 2;              // --shot-yield ms: sleep per simulated frame
    // --shot-pour X,Y,START,DUR : hold LMB at (X,Y) px from START for DUR seconds,
    // with a slow circular drift (radius 40 px, 0.5 rev/s) like a resting hand.
    bool     pour = false;
    float    pourX = 1280, pourY = 720, pourStart = 0, pourDur = 0;
};

static void ShotLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    // Mirror to a log file: this is a /SUBSYSTEM:WINDOWS exe, so stdout only
    // lands somewhere when the launching shell gave us a pipe or a file.
    wchar_t path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"FluidWallpaper-shot.log");
    if (FILE* f = _wfsopen(path, L"a", _SH_DENYNO)) {
        fputs(buf, f);
        fclose(f);
    }
}

static float LinearToSrgb(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}
static uint8_t ToByte(float srgb) {
    int v = (int)(srgb * 255.0f + 0.5f);
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// WIC PNG writer (24bpp BGR).
static bool WritePng(const wchar_t* path, const std::vector<uint8_t>& bgr, int w, int h) {
    ComPtr<IWICImagingFactory> fac;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fac)))) return false;
    ComPtr<IWICStream> stream;
    if (FAILED(fac->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return false;
    if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(&frame, &props))) return false;
    if (FAILED(frame->Initialize(props.Get()))) return false;
    if (FAILED(frame->SetSize((UINT)w, (UINT)h))) return false;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&fmt))) return false;
    const UINT stride = (UINT)w * 3;
    if (FAILED(frame->WritePixels((UINT)h, stride, stride * (UINT)h,
                                  const_cast<BYTE*>(bgr.data())))) return false;
    if (FAILED(frame->Commit())) return false;
    return SUCCEEDED(enc->Commit());
}

static void EnsureParentDir(const wchar_t* path) {
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, path);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return;
    *slash = 0;
    // create the chain (one level of nesting is the common case here)
    wchar_t parent[MAX_PATH];
    wcscpy_s(parent, MAX_PATH, dir);
    if (wchar_t* s2 = wcsrchr(parent, L'\\')) { *s2 = 0; CreateDirectoryW(parent, nullptr); }
    CreateDirectoryW(dir, nullptr);
}

// Encode the captured linear-scRGB frame two ways and report what's in it.
//   <stem>.png      display-referred SDR: /sdrScale, clip at 1.0, sRGB encode
//   <stem>-hdr.png  highlight-preserving: hue-preserving Reinhard on the max
//                   channel, so anything above SDR white stays visible
static void WriteShotPair(const std::wstring& stem, const std::vector<float>& rgba,
                          int w, int h, float sdrScale, float elapsed) {
    const size_t n = (size_t)w * h;
    std::vector<uint8_t> sdr(n * 3), hdr(n * 3);

    double lumSum = 0.0;
    size_t aboveWhite = 0, negative = 0;
    float maxScrgb = 0.0f;

    for (size_t p = 0; p < n; p++) {
        float r = rgba[p * 4 + 0], g = rgba[p * 4 + 1], b = rgba[p * 4 + 2];
        if (r != r) r = 0; if (g != g) g = 0; if (b != b) b = 0;   // NaN guard
        if (r < 0.0f || g < 0.0f || b < 0.0f) negative++;
        maxScrgb = fmaxf(maxScrgb, fmaxf(r, fmaxf(g, b)));
        // Rec.709 luminance of the scRGB value (1.0 = 80 nits)
        lumSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        if (fmaxf(r, fmaxf(g, b)) > sdrScale) aboveWhite++;

        // display-referred: SDR white -> 1.0, negatives (wide gamut) clip to 0
        float dr = fmaxf(0.0f, r / sdrScale);
        float dg = fmaxf(0.0f, g / sdrScale);
        float db = fmaxf(0.0f, b / sdrScale);
        sdr[p * 3 + 0] = ToByte(LinearToSrgb(fminf(db, 1.0f)));   // B
        sdr[p * 3 + 1] = ToByte(LinearToSrgb(fminf(dg, 1.0f)));   // G
        sdr[p * 3 + 2] = ToByte(LinearToSrgb(fminf(dr, 1.0f)));   // R

        // highlight-preserving: Reinhard on the max channel, ratios preserved
        float m = fmaxf(dr, fmaxf(dg, db));
        float k = m > 1e-6f ? (m / (1.0f + m)) / m : 0.0f;
        hdr[p * 3 + 0] = ToByte(LinearToSrgb(db * k));
        hdr[p * 3 + 1] = ToByte(LinearToSrgb(dg * k));
        hdr[p * 3 + 2] = ToByte(LinearToSrgb(dr * k));
    }

    std::wstring sdrPath = stem + L".png";
    std::wstring hdrPath = stem + L"-hdr.png";
    EnsureParentDir(sdrPath.c_str());
    bool okS = WritePng(sdrPath.c_str(), sdr, w, h);
    bool okH = WritePng(hdrPath.c_str(), hdr, w, h);

    const double meanLum = lumSum / (double)n;
    ShotLog("[shot] t=%.1fs %ls %dx%d  mean_lum=%.4f scRGB (%.1f nits)  "
            "above_sdr_white=%.2f%%  max_scRGB=%.3f (%.0f nits)  negative_px=%.2f%%  "
            "sdr=%s hdr=%s\n",
            elapsed, sdrPath.c_str(), w, h, meanLum, meanLum * 80.0,
            100.0 * aboveWhite / (double)n, maxScrgb, maxScrgb * 80.0f,
            100.0 * negative / (double)n,
            okS ? "ok" : "FAILED", okH ? "ok" : "FAILED");
}

// Cheap pre-scan so the normal launch path below stays untouched.
static bool ShotModeRequested() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; i < argc && !found; i++)
        if (wcscmp(argv[i], L"--shot") == 0) found = true;
    LocalFree(argv);
    return found;
}

static int RunShotMode() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    ShotOpts o;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            auto next = [&](void) -> const wchar_t* { return i + 1 < argc ? argv[++i] : nullptr; };
            if (wcscmp(argv[i], L"--shot") == 0) {
                if (const wchar_t* v = next()) o.out = v;
            } else if (wcscmp(argv[i], L"--shot-size") == 0) {
                if (const wchar_t* v = next()) {
                    int w = 0, h = 0;
                    if (swscanf_s(v, L"%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                        o.width = w; o.height = h;
                    }
                }
            } else if (wcscmp(argv[i], L"--shot-pour") == 0 && i + 1 < argc) {
                if (swscanf_s(argv[++i], L"%f,%f,%f,%f", &o.pourX, &o.pourY, &o.pourStart, &o.pourDur) == 4)
                    o.pour = true;
            } else if (wcscmp(argv[i], L"--shot-yield") == 0 && i + 1 < argc) {
                o.yieldMs = _wtoi(argv[++i]);
            } else if (wcscmp(argv[i], L"--shot-delay") == 0) {
                if (const wchar_t* v = next()) o.delaySec = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--shot-series") == 0) {
                if (const wchar_t* v = next()) {
                    int n = 0; float iv = 0;
                    if (swscanf_s(v, L"%d:%f", &n, &iv) == 2 && n > 0) {
                        o.seriesCount = n; o.seriesInterval = iv;
                    }
                }
            } else if (wcscmp(argv[i], L"--seed") == 0) {
                if (const wchar_t* v = next()) o.seed = (unsigned)_wtoi(v);
            } else if (wcscmp(argv[i], L"--ini") == 0) {
                if (const wchar_t* v = next()) o.ini = v;
            } else if (wcscmp(argv[i], L"--hdr") == 0) {
                if (const wchar_t* v = next()) o.hdrOn = (_wcsicmp(v, L"on") == 0);
            } else if (wcscmp(argv[i], L"--sdr-white") == 0) {
                if (const wchar_t* v = next()) o.sdrWhiteNits = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--panel-max") == 0) {
                if (const wchar_t* v = next()) o.panelMaxNits = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--mouse-none") == 0) {
                o.mouseNone = true;
            }
        }
        LocalFree(argv);
    }
    if (o.out.empty()) {
        ShotLog("[shot] ERROR: --shot needs an output .png path\n");
        return 2;
    }
    if (o.sdrWhiteNits < 1.0f) o.sdrWhiteNits = 80.0f;

    // Config: read-only for the whole run. Nothing below may write an ini, a
    // mood, a journey, or an autostart key.
    g_configReadOnly = true;
    InitSettingsPath();
    if (!o.ini.empty()) {
        if (GetFileAttributesW(o.ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
            ShotLog("[shot] ERROR: --ini file not found: %ls\n", o.ini.c_str());
            return 2;
        }
        wcscpy_s(g_configIniPath, MAX_PATH, o.ini.c_str());
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        ShotLog("[shot] ERROR: CoInitializeEx failed\n");
        return 3;
    }

    ShotLog("[shot] FluidWallpaper offscreen capture\n");
    ShotLog("[shot] ini=%ls\n", g_configIniPath);
    ShotLog("[shot] size=%dx%d seed=%u delay=%.1fs series=%d:%.1fs hdr=%s sdr_white=%.0f nits\n",
            o.width, o.height, o.seed, o.delaySec, o.seriesCount, o.seriesInterval,
            o.hdrOn ? "on" : "off", o.sdrWhiteNits);

    FluidConfig cfg;
    LoadSettings();          // shell globals: peak_nits, gamut (from the shot ini)
    LoadFullConfig(cfg);     // full look config
    EnsureBuiltinPresets();  // no-op while read-only
    srand(o.seed);           // mood dwell jitter is drawn during InitMoods
    InitMoods();
    MoodsApplyBase(cfg);

    // HDR state exactly as the shell would resolve it, without querying the
    // real display (see the main loop: sdrScale / peak / SetHdrOptions).
    const bool  hdrActive = o.hdrOn;
    g_maxNits = o.panelMaxNits;
    const float sdrScale = hdrActive ? (o.sdrWhiteNits / 80.0f) : 1.0f;
    ShotLog("[shot] HDR %s, sdrScale=%.4f, peak_nits=%.0f (ini), gamut=%d\n",
            hdrActive ? "ON" : "OFF", sdrScale, g_hdrPeakNits, g_gamutMode);

    FluidRenderer::SetRandomSeed(o.seed);
    FluidRenderer renderer;
    renderer.InitOffscreen(o.width, o.height, cfg);
    g_renderer = &renderer;
    renderer.SetCoverageWanted(g_moodSettings.enabled);

    const float dt = 1.0f / 144.0f;   // fixed timestep, no vsync, no sleeping
    FrameInput fin;                   // --mouse-none: all-zero, no user input
    (void)o.mouseNone;

    long long frames = 0;
    long long nextLog = 0;
    const DWORD wallStart = GetTickCount();

    // strip a trailing .png so series/-hdr names hang off a clean stem
    std::wstring stem = o.out;
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".png") == 0)
        stem.resize(stem.size() - 4);

    std::vector<float> pixels;
    for (int s = 0; s < o.seriesCount; s++) {
        const double target = o.delaySec + (double)s * o.seriesInterval;
        const long long want = (long long)llround(target * 144.0);
        while (frames < want) {
            fin = FrameInput{};
            if (o.pour) {
                const float t = frames / 144.0f;
                if (t >= o.pourStart && t < o.pourStart + o.pourDur) {
                    const float ang = (t - o.pourStart) * 3.14159265f;   // 0.5 rev/s
                    const float px = o.pourX + 40.0f * cosf(ang);
                    const float py = o.pourY + 40.0f * sinf(ang);
                    static float lx = 0, ly = 0; static bool have = false;
                    fin.mouseX = px; fin.mouseY = py;
                    if (have) { fin.mouseMoved = true; fin.mouseDx = (px - lx) * 5.0f; fin.mouseDy = (py - ly) * 5.0f; }
                    lx = px; ly = py; have = true;
                    fin.mouseDown = true; fin.userInteracted = true;
                }
            }
            UpdateMoods(renderer, dt);
            float peak = g_hdrPeakNits < 0.0f ? g_maxNits : g_hdrPeakNits;   // -1 = panel max
            renderer.SetHdrOptions(peak, g_gamutMode);
            renderer.Frame(dt, sdrScale, hdrActive, fin);
            frames++;
            // Leave the GPU some air: an unthrottled full-res sim starves the
            // compositor and Wallpaper Engine (the OLED went grey once when
            // two of these ran at once). Never run two shot processes together.
            if (o.yieldMs > 0) Sleep((DWORD)o.yieldMs);
            if (frames >= nextLog) {
                ShotLog("[shot] simulated %.1f s (%lld frames, %.1f s wall)\n",
                        frames / 144.0, frames, (GetTickCount() - wallStart) / 1000.0);
                nextLog = frames + 144 * 5;
            }
        }
        if (!renderer.CaptureOffscreen(pixels)) {
            ShotLog("[shot] ERROR: readback failed\n");
            renderer.Shutdown();
            CoUninitialize();
            return 4;
        }
        std::wstring shotStem = stem;
        if (o.seriesCount > 1) {
            wchar_t suffix[32];
            swprintf_s(suffix, L"-%03d", (int)llround(target));
            shotStem += suffix;
        }
        WriteShotPair(shotStem, pixels, o.width, o.height, sdrScale, (float)(frames / 144.0));
    }

    ShotLog("[shot] done: %lld frames simulated in %.1f s wall\n",
            frames, (GetTickCount() - wallStart) / 1000.0);
    g_renderer = nullptr;
    renderer.Shutdown();
    CoUninitialize();
    return 0;
}

// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Headless capture: no window, no WorkerW, no tray, no single-instance
    // handshake (a running wallpaper must not be disturbed), no config writes.
    if (ShotModeRequested()) return RunShotMode();

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
            } else if (wcscmp(argv[i], L"--calibrate") == 0 && i + 1 < argc) {
                cfg.gradientMode = true;   // skips sim resources; pattern path
                cfg.calibratePage = _wtoi(argv[++i]);
            } else if (wcscmp(argv[i], L"--stats") == 0) {
                cfg.stats = true;
            } else if (wcscmp(argv[i], L"--force-render") == 0) {
                g_forceRender = true;
            } else if (wcscmp(argv[i], L"--test-suspend") == 0) {
                g_testSuspend = true;
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

    // Opt the process into dark-mode common controls (same mechanism apps like
    // Notepad++ use: uxtheme ordinal 135 = SetPreferredAppMode(AllowDark)).
    // Makes the DarkMode_Explorer-themed buttons/checkboxes render dark faces.
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
        typedef int(WINAPI* FnSetPreferredAppMode)(int);
        if (auto setMode = (FnSetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135)))
            setMode(1);   // AllowDark
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitSettingsPath();
    LoadSettings();

    LoadFullConfig(cfg);
    EnsureBuiltinPresets();
    InitMoods();
    MoodsApplyBase(cfg);   // resume the explicitly-chosen mood over settings

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
    EnumDisplayMonitors(nullptr, nullptr, FindSecondMonitor, 0);
    if (g_monitor2)
        printf("second monitor found: (%ld,%ld)-(%ld,%ld)\n",
               g_monitor2Rect.left, g_monitor2Rect.top,
               g_monitor2Rect.right, g_monitor2Rect.bottom);

    FluidRenderer renderer;
    renderer.Init(hwnd, width, height, cfg);
    g_renderer = &renderer;
    renderer.SetCoverageWanted(g_moodSettings.enabled);

    CreateTrayWindow();

    g_hdrActive = QueryHDR(g_monitor, &g_maxNits);
    g_sdrWhiteNits = GetSdrWhiteNits(g_monitor);
    UpdateTrayTip();
    printf("HDR live state: %s (max %.0f nits, SDR white %.0f nits)\n",
           g_hdrActive ? "ON" : "OFF", g_maxNits, g_sdrWhiteNits);

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    // Fullscreen-pause suspend: after 20 s of continuous fullscreen/maximized
    // pause, tear the renderer down completely so the foreground app (game)
    // gets all RAM/VRAM back; rebuild transparently when the pause clears.
    // Manual pause alone never suspends — its resume should stay instant.
    static const ULONGLONG kSuspendAfterMs = 20000;
    static ULONGLONG fsPausedSince = 0;   // 0 = not fs-paused right now
    static bool g_suspended = false;      // renderer fully torn down
    static bool fsSuspended = false;      // suspension came from the fs trigger
    static FluidConfig savedCfg;          // live config snapshot for the resume
    const ULONGLONG bootTick = GetTickCount64();   // --test-suspend clock

    auto suspendRenderer = [&](const char* why) {
        savedCfg = renderer.Config();
        renderer.Shutdown();
        g_suspended = true;
        SYSTEMTIME st;
        GetLocalTime(&st);
        printf("[%02d:%02d:%02d.%03d] renderer suspended (%s)\n",
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, why);
    };
    auto resumeRenderer = [&]() {
        // same Init call as startup, then the startup state restore: resolved
        // HDR options (like the per-frame call site does), coverage override,
        // scRGB color space.
        renderer.Init(hwnd, width, height, savedCfg);
        float peak = g_hdrPeakNits < 0.0f ? g_maxNits : g_hdrPeakNits;
        renderer.SetHdrOptions(peak, g_gamutMode);
        renderer.SetCoverageWanted(g_moodSettings.enabled);
        renderer.ReassertColorSpace();
        g_suspended = false;
        // Mood/journey state lives in the config, so it survives via savedCfg;
        // purely time-based phases (hue wheel position, transition progress)
        // jump across the suspend — accepted, serializing conductor state
        // isn't worth it for a wallpaper that was invisible anyway.
        QueryPerformanceCounter(&prev);   // don't integrate the suspended gap
        SYSTEMTIME st;
        GetLocalTime(&st);
        printf("[%02d:%02d:%02d.%03d] renderer resumed\n",
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    };

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
            fsPausedSince = 0;   // don't bank suspend time across a shell restart
            static ULONGLONG lastTry = 0;
            ULONGLONG now2 = GetTickCount64();
            if (now2 - lastTry >= 1000) {
                lastTry = now2;
                HWND newHost = FindWallpaperHost();
                if (newHost) {
                    hwnd = CreateWallpaperWindow(newHost, width, height);
                    g_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                    // suspended: skip the swapchain-only reattach — the resume
                    // does a full Init() onto this new window instead
                    if (!g_suspended) renderer.Reattach(hwnd);
                    g_wallpaperLost = false;
                    QueryPerformanceCounter(&prev);
                }
            }
            if (g_wallpaperLost) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
                continue;
            }
        }

        // Second-monitor mirror lifecycle (off by default; settings checkbox)
        {
            static HWND mirrorWnd = nullptr;
            static ULONGLONG lastMirrorTry = 0;
            // suspended: the mirror chain went down with the device — keep
            // wanting it off here; the resume Init leaves MirrorActive()
            // false, so it re-enables on the next iteration
            bool want = !g_suspended &&
                        renderer.Config().mirrorSecond && g_monitor2 != nullptr;

            if (renderer.MirrorActive() && renderer.MirrorBroken()) {
                renderer.DisableMirror();
                g_destroyingMirror = true;
                if (mirrorWnd && IsWindow(mirrorWnd)) DestroyWindow(mirrorWnd);
                g_destroyingMirror = false;
                mirrorWnd = nullptr;
            }
            if (want && !renderer.MirrorActive()) {
                ULONGLONG now3 = GetTickCount64();
                if (now3 - lastMirrorTry >= 2000) {
                    lastMirrorTry = now3;
                    EnumDisplayMonitors(nullptr, nullptr, FindSecondMonitor, 0);
                    HWND host2 = FindWallpaperHost();
                    if (g_monitor2 && host2) {
                        int w2 = g_monitor2Rect.right - g_monitor2Rect.left;
                        int h2 = g_monitor2Rect.bottom - g_monitor2Rect.top;
                        mirrorWnd = CreateWallpaperWindowAt(host2, g_monitor2Rect.left,
                                                           g_monitor2Rect.top, w2, h2);
                        renderer.EnableMirror(mirrorWnd, w2, h2);
                        float max2 = 0.0f;
                        bool hdr2 = QueryHDR(g_monitor2, &max2);
                        float sdrW2 = GetSdrWhiteNits(g_monitor2);
                        float peak2 = hdr2 ? (g_hdrPeakNits < 0.0f ? max2 : g_hdrPeakNits) : 0.0f;
                        renderer.SetMirrorHdr(hdr2 ? sdrW2 / 80.0f : 1.0f, peak2);
                    }
                }
            } else if (!want && renderer.MirrorActive()) {
                renderer.DisableMirror();
                g_destroyingMirror = true;
                if (mirrorWnd && IsWindow(mirrorWnd)) DestroyWindow(mirrorWnd);
                g_destroyingMirror = false;
                mirrorWnd = nullptr;
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

        // Suspend/resume the renderer around long fullscreen pauses. Only
        // g_fsPaused drives this — manual pause alone never suspends.
        if (!g_suspended) {
            if (g_fsPaused && !g_wallpaperLost) {
                if (fsPausedSince == 0) fsPausedSince = tick;
                if (tick - fsPausedSince >= kSuspendAfterMs) {
                    suspendRenderer("fullscreen app, freeing GPU/RAM");
                    fsSuspended = true;
                }
            } else {
                fsPausedSince = 0;
            }
        } else if (fsSuspended && !g_fsPaused) {
            // fullscreen app exited: rebuild the renderer like startup did
            fsSuspended = false;
            fsPausedSince = 0;
            resumeRenderer();
        }

        // hidden test hook: force one suspend ~3 s after start, resume ~8 s
        if (g_testSuspend) {
            const double el = (double)(tick - bootTick) / 1000.0;
            if (!g_suspended && el >= 3.0 && el < 8.0) {
                printf("[test-suspend] forcing suspend at %.1f s\n", el);
                suspendRenderer("test hook, freeing GPU/RAM");
            } else if (g_suspended && !fsSuspended && el >= 8.0) {
                printf("[test-suspend] forcing resume at %.1f s\n", el);
                resumeRenderer();
            }
        }

        if (paused || g_suspended) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
            QueryPerformanceCounter(&prev);   // don't integrate the paused gap
            continue;
        }

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);

        // Mood conductor: dwell/shift/emit/return transitions between
        // moods\*.ini recipes (replaces the old instant-swap interludes)
        UpdateMoods(renderer, dt);

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
