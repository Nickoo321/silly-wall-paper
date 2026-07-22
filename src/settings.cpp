// Settings window: every wallpaper parameter, organized into titled sections
// across four columns (RGB-suite style), dark themed. Every change applies
// live and persists to settings.ini immediately.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vector>
#include <unordered_set>
#include <cstdio>
#include "app_state.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ---------------------------------------------------------------------------

struct SliderDef {
    const wchar_t* label;
    float mn, mx, step;
    int decimals;
    float* fval;             // one of fval/ival is set
    int* ival;
    const wchar_t* section;  // ini section
    const wchar_t* key;
    bool reinitWanderers;
    const wchar_t* header;   // non-null: start a new titled group here
    int col;                 // column for that new group (valid with header)
};
struct CheckDef {
    const wchar_t* label;
    bool* val;               // null => autostart (registry-backed)
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* header;
    int col;
};

static HWND s_wnd = nullptr;
static HFONT s_font = nullptr, s_headFont = nullptr;
static HBRUSH s_darkBrush = nullptr;
static std::vector<SliderDef> s_sliders;
static std::vector<HWND> s_sliderCtls, s_sliderLabels;
static std::vector<CheckDef> s_checks;
static std::unordered_set<HWND> s_headers;    // accent-colored statics
static HWND s_comboMode = nullptr, s_comboSim = nullptr, s_comboDye = nullptr;
static HWND s_fpsLabel = nullptr, s_pauseBtn = nullptr;

static const int IDC_CHECK_BASE = 300;
static const int IDC_GAMUT_BASE = 400;
static const int IDC_WMODE      = 450;
static const int IDC_SIMRES     = 460;
static const int IDC_DYERES     = 461;
static const int IDC_COLOR_BASE = 500;
static const int IDC_OPEN_ANALYZER = 260;
static const int IDC_PAUSE_BTN  = 261;
static const int IDC_EXIT_BTN   = 262;
static const int IDC_SCENES_BTN = 263;
static const int IDC_SAVE_SCENE = 264;

static const int kSimResOptions[] = { 32, 64, 128, 256, 512 };
static const int kDyeResOptions[] = { 256, 512, 1024, 2048, 4096 };
static const COLORREF kAccent = RGB(122, 184, 255);

// ---------------------------------------------------------------------------

static void WriteIniFloat(const wchar_t* section, const wchar_t* key, float v, int decimals) {
    if (!g_iniPath[0]) return;
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f", decimals, v);
    WritePrivateProfileStringW(section, key, buf, g_iniPath);
}
static void WriteIniInt(const wchar_t* section, const wchar_t* key, int v) {
    if (!g_iniPath[0]) return;
    wchar_t buf[32];
    swprintf_s(buf, L"%d", v);
    WritePrivateProfileStringW(section, key, buf, g_iniPath);
}

static bool GetAutostart() {
    wchar_t path[MAX_PATH];
    DWORD sz = sizeof(path);
    return RegGetValueW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"FluidWallpaper", RRF_RT_REG_SZ, nullptr, path, &sz) == ERROR_SUCCESS;
}
static void SetAutostart(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 4];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        swprintf_s(cmd, L"\"%s\"", exe);
        RegSetValueExW(key, L"FluidWallpaper", 0, REG_SZ,
                       (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"FluidWallpaper");
    }
    RegCloseKey(key);
    printf("autostart: %s\n", on ? "enabled" : "disabled");
}

// ---------------------------------------------------------------------------

static void BuildDefs() {
    FluidConfig& c = g_renderer->Config();
    s_sliders = {
        // ---- column 0 ----
        { L"Vorticity (swirl strength)",        0,     50,   0.5f,  1, &c.curl,               nullptr, L"sim", L"vorticity", false, L"Simulation", 0 },
        { L"Splat radius",                      0.01f, 1,    0.005f,3, &c.splatRadius,        nullptr, L"sim", L"splat_radius", false, nullptr, 0 },
        { L"Density diffusion (dye linger)",    0.95f, 1,    0.0001f,4,&c.densityDissipation, nullptr, L"sim", L"density_diffusion", false, nullptr, 0 },
        { L"Velocity diffusion",                0.95f, 1,    0.0001f,4,&c.velocityDissipation,nullptr, L"sim", L"velocity_diffusion", false, nullptr, 0 },
        { L"Pressure diffusion",                0,     1,    0.005f,3, &c.pressureDissipation,nullptr, L"sim", L"pressure_diffusion", false, nullptr, 0 },
        { L"Pressure iterations",               10,    60,   1,     0, nullptr, &c.pressureIterations, L"sim", L"pressure_iterations", false, nullptr, 0 },
        { L"Tail decay speed (lower=snappier)", 0.5f,  1,    0.002f,3, &c.decayFast,          nullptr, L"sim", L"decay_fast", false, nullptr, 0 },
        { L"Decay threshold",                   0,     0.3f, 0.002f,3, &c.decayThreshold,     nullptr, L"sim", L"decay_threshold", false, nullptr, 0 },
        { L"Saturation restore /s",             0,     1,    0.005f,3, &c.satRestore,         nullptr, L"sim", L"saturation_restore", false, nullptr, 0 },
        { L"Color intensity cap",               0.3f,  4,    0.05f, 2, &c.maxBrightness,      nullptr, L"sim", L"max_brightness", false, nullptr, 0 },
        { L"Dye diffusion (smoke spread)",      0,     0.5f, 0.005f,3, &c.dyeDiffusion,       nullptr, L"sim", L"dye_diffusion", false, nullptr, 0 },
        { L"FPS limit",                         30,    260,  1,     0, &c.fpsLimit,           nullptr, L"general", L"fps_limit", false, L"Performance", 0 },
        // ---- column 1 ----
        { L"Count",                             1,     8,    1,     0, nullptr, &c.wandererCount,      L"behavior", L"wanderer_count", true, L"Wanderers", 1 },
        { L"Speed (px/s)",                      50,    1200, 10,    0, &c.wandererSpeed,      nullptr, L"behavior", L"wanderer_speed", false, nullptr, 1 },
        { L"Brightness",                        0.05f, 1,    0.01f, 2, &c.wandererBrightness, nullptr, L"behavior", L"wanderer_brightness", false, nullptr, 1 },
        { L"Path size (circle / figure-8)",     0.1f,  0.9f, 0.01f, 2, &c.wandererScale,      nullptr, L"behavior", L"wanderer_scale", true, nullptr, 1 },
        { L"Resume after idle (s)",             0,     30,   0.5f,  1, &c.wandererResumeDelay,nullptr, L"behavior", L"wanderer_resume_delay", false, nullptr, 1 },
        { L"Min dark area % before pause",      5,     60,   1,     0, &c.darkFloor,          nullptr, L"behavior", L"dark_floor", false, L"Screen-fullness governor", 1 },
        { L"Dark pixel cutoff",                 0.005f,0.1f, 0.005f,3, &c.darkLevel,          nullptr, L"behavior", L"dark_level", false, nullptr, 1 },
        { L"Survivor wanderer dark floor %",    0,     40,   1,     0, &c.survDarkFloor,      nullptr, L"behavior", L"surv_dark_floor", false, nullptr, 1 },
        { L"Contrast required % (0 = off)",     0,     100,  1,     0, &c.contrastReq,        nullptr, L"behavior", L"contrast_req", false, nullptr, 1 },
        { L"Interval (s)",                      1,     30,   1,     0, &c.dartInterval,       nullptr, L"behavior", L"dart_interval", false, L"Separating dart", 1 },
        { L"Speed (px/s)",                      500,   6000, 50,    0, &c.dartSpeed,          nullptr, L"behavior", L"dart_speed", false, nullptr, 1 },
        // ---- column 2 ----
        { L"Peak brightness (nits, 0 = off)",   0,     1500, 5,     0, &g_hdrPeakNits,        nullptr, L"hdr", L"peak_nits", false, L"HDR output", 2 },
        { L"Knee (boost starts at)",            0.1f,  1.3f, 0.02f, 2, &c.hdrKnee,            nullptr, L"hdr", L"knee", false, nullptr, 2 },
        { L"Saturation boost",                  1,     2,    0.01f, 2, &c.hdrSaturation,      nullptr, L"hdr", L"saturation", false, nullptr, 2 },
        { L"Brightness boost",                  0.8f,  1.5f, 0.01f, 2, &c.hdrBrightness,      nullptr, L"hdr", L"brightness", false, nullptr, 2 },
        { L"Contrast",                          0.8f,  1.5f, 0.01f, 2, &c.hdrContrast,        nullptr, L"hdr", L"contrast", false, nullptr, 2 },
        { L"Saturation",                        0.5f,  2,    0.01f, 2, &c.postSaturation,     nullptr, L"color", L"post_saturation", false, L"Color grading (WE panel)", 2 },
        { L"Contrast",                          0.5f,  2,    0.01f, 2, &c.postContrast,       nullptr, L"color", L"post_contrast", false, nullptr, 2 },
        { L"Brightness",                        0.5f,  1.5f, 0.01f, 2, &c.postBrightness,     nullptr, L"color", L"post_brightness", false, nullptr, 2 },
        { L"Hue rotate (deg)",                  0,     360,  1,     0, &c.postHue,            nullptr, L"color", L"post_hue", false, nullptr, 2 },
        { L"My look runs for (s)",              5,     120,  1,     0, &g_cycleBaseSec,       nullptr, L"cycle", L"base_seconds", false, L"Scene cycling", 2 },
        { L"Interlude runs for (s)",            2,     60,   1,     0, &g_cycleInterludeSec,  nullptr, L"cycle", L"interlude_seconds", false, nullptr, 2 },
        // ---- column 3 ----
        { L"Cycle time (s per lap)",            2,     120,  1,     0, &c.colorCyclePeriod,   nullptr, L"behavior", L"color_cycle_period", false, L"Color wheel", 3 },
        { L"Hue band center (deg)",             0,     360,  1,     0, &c.hueCenter,          nullptr, L"color", L"hue_center", false, nullptr, 3 },
        { L"Hue band range (180 = full wheel)", 5,     180,  1,     0, &c.hueRange,           nullptr, L"color", L"hue_range", false, nullptr, 3 },
        { L"Step (deg)",                        10,    180,  1,     0, &c.hsStep,             nullptr, L"behavior", L"hueshift_step", false, L"Hue shift bursts", 3 },
        { L"Linger (s)",                        0,     15,   0.5f,  1, &c.hsLinger,           nullptr, L"behavior", L"hueshift_linger", false, nullptr, 3 },
        { L"Glide (s)",                         0.1f,  10,   0.1f,  1, &c.hsGlide,            nullptr, L"behavior", L"hueshift_glide", false, nullptr, 3 },
        { L"Steps per burst",                   1,     12,   1,     0, nullptr, &c.hsBurstSteps,       L"behavior", L"hueshift_burst_steps", false, nullptr, 3 },
        { L"Off time between bursts (s)",       0,     120,  1,     0, &c.hsOffTime,          nullptr, L"behavior", L"hueshift_off_time", false, nullptr, 3 },
        { L"Interval (s)",                      0.5f,  30,   0.5f,  1, &c.idleInterval,       nullptr, L"behavior", L"idle_interval", false, L"Idle splats", 3 },
        { L"Amount per burst",                  1,     30,   1,     0, nullptr, &c.idleAmount,         L"behavior", L"idle_amount", false, nullptr, 3 },
        { L"Hump center (input brightness)",    0.05f, 1,    0.01f, 2, &c.curveCenter,        nullptr, L"color", L"curve_center", false, L"Response curve (bright rims)", 3 },
        { L"Hump width",                        0.02f, 0.5f, 0.01f, 2, &c.curveWidth,         nullptr, L"color", L"curve_width", false, nullptr, 3 },
        { L"Hump height (output brightness)",   0.1f,  2,    0.05f, 2, &c.curveHeight,        nullptr, L"color", L"curve_height", false, nullptr, 3 },
    };
    s_checks = {
        { L"Auto wanderer splats",              &c.wanderers,        L"behavior", L"wanderers", L"Behaviors", 0 },
        { L"Auto-pause when screen full",       &c.autoPause,        L"behavior", L"auto_pause", nullptr, 0 },
        { L"Separating dart while paused",      &c.dartEnabled,      L"behavior", L"dart_enabled", nullptr, 0 },
        { L"Hue shift cycler",                  &c.hsEnabled,        L"behavior", L"hueshift_enabled", nullptr, 0 },
        { L"Idle random splats",                &c.idleSplats,       L"behavior", L"idle_splats", nullptr, 0 },
        { L"Shading",                           &c.shading,          L"sim",      L"shading", nullptr, 0 },
        { L"Hold left mouse = pour dye",        &c.holdToSplat,      L"behavior", L"hold_to_splat", L"Mouse", 1 },
        { L"Splat on click (if not holding)",   &c.splatOnClick,     L"behavior", L"splat_on_click", nullptr, 1 },
        { L"Mouse movement stirs fluid",        &c.showMouse,        L"behavior", L"show_mouse", nullptr, 1 },
        { L"Random color (hue wheel)",          &c.colorful,         L"color",    L"colorful", L"Color source", 2 },
        { L"Use all 5 palette colors",          &c.moreColors,       L"color",    L"more_colors", nullptr, 2 },
        { L"HDR compensation (sat/brightness)", &c.hdrCompensation,  L"hdr",      L"compensation", nullptr, 2 },
        { L"Response curve (bright rims)",      &c.curveEnabled,     L"color",    L"curve_enabled", nullptr, 2 },
        { L"Pause on fullscreen app",           &g_pauseOnFullscreen,L"general",  L"pause_on_fullscreen", L"System", 3 },
        { L"Pause on maximized app",            &g_pauseOnMaximized, L"general",  L"pause_on_maximized", nullptr, 3 },
        { L"Scene interlude cycling",           &g_cycleEnabled,     L"cycle",    L"enabled", nullptr, 3 },
        { L"Mirror on second monitor",          &c.mirrorSecond,     L"general",  L"mirror_second", nullptr, 3 },
        { L"Start with Windows",                nullptr,             nullptr,     nullptr, nullptr, 3 },
    };
}

static float SliderValue(const SliderDef& d, int pos) {
    return d.mn + pos * d.step;
}
static int SliderPos(const SliderDef& d) {
    float v = d.fval ? *d.fval : (float)*d.ival;
    if (v < d.mn) v = d.mn;
    if (v > d.mx) v = d.mx;
    return (int)((v - d.mn) / d.step + 0.5f);
}
static void UpdateSliderLabel(size_t i) {
    const SliderDef& d = s_sliders[i];
    float v = d.fval ? *d.fval : (float)*d.ival;
    wchar_t buf[160];
    if (d.fval == &g_hdrPeakNits && v <= 0.0f)
        swprintf_s(buf, L"%s:  off", d.label);
    else
        swprintf_s(buf, L"%s:  %.*f", d.label, d.decimals, v);
    SetWindowTextW(s_sliderLabels[i], buf);
}

static void PickPaletteColor(HWND owner, int idx) {
    FluidConfig& c = g_renderer->Config();
    static COLORREF customColors[16] = {};
    float* col = c.splatColors + idx * 3;
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.lpCustColors = customColors;
    cc.rgbResult = RGB((BYTE)(col[0] * 255), (BYTE)(col[1] * 255), (BYTE)(col[2] * 255));
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    col[0] = GetRValue(cc.rgbResult) / 255.0f;
    col[1] = GetGValue(cc.rgbResult) / 255.0f;
    col[2] = GetBValue(cc.rgbResult) / 255.0f;
    wchar_t key[32], val[64];
    swprintf_s(key, L"splat_color_%d", idx + 1);
    swprintf_s(val, L"%.4f %.4f %.4f", col[0], col[1], col[2]);
    WritePrivateProfileStringW(L"color", key, val, g_iniPath);
}

// ---------------------------------------------------------------------------

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HSCROLL: {
        HWND ctl = (HWND)lp;
        for (size_t i = 0; i < s_sliderCtls.size(); i++) {
            if (s_sliderCtls[i] != ctl) continue;
            SliderDef& d = s_sliders[i];
            int pos = (int)SendMessageW(ctl, TBM_GETPOS, 0, 0);
            float v = SliderValue(d, pos);
            if (d.fval) *d.fval = v; else *d.ival = (int)(v + 0.5f);
            if (d.reinitWanderers) g_renderer->ReinitWanderers();
            if (d.fval) WriteIniFloat(d.section, d.key, v, d.decimals);
            else        WriteIniInt(d.section, d.key, *d.ival);
            UpdateSliderLabel(i);
            break;
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_CHECK_BASE && id < IDC_CHECK_BASE + (int)s_checks.size()) {
            CheckDef& d = s_checks[id - IDC_CHECK_BASE];
            bool on = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (d.val) {
                *d.val = on;
                WriteIniInt(d.section, d.key, on ? 1 : 0);
            } else {
                SetAutostart(on);
            }
            return 0;
        }
        if (id >= IDC_GAMUT_BASE && id <= IDC_GAMUT_BASE + 2) {
            g_gamutMode = id - IDC_GAMUT_BASE;
            WriteIniInt(L"hdr", L"gamut", g_gamutMode);
            return 0;
        }
        if (id >= IDC_COLOR_BASE && id < IDC_COLOR_BASE + 5) {
            PickPaletteColor(hwnd, id - IDC_COLOR_BASE);
            return 0;
        }
        if (id == IDC_OPEN_ANALYZER) { ShowAnalyzerWindow(); return 0; }
        if (id == IDC_SCENES_BTN)    { ShowScenesWindow();   return 0; }
        if (id == IDC_SAVE_SCENE)    { SaveCurrentAsPresetFile(); return 0; }
        if (id == IDC_PAUSE_BTN) {
            TogglePause();
            SetWindowTextW(s_pauseBtn, IsManualPaused() ? L"Resume wallpaper" : L"Pause wallpaper");
            return 0;
        }
        if (id == IDC_EXIT_BTN) { RequestExit(); return 0; }
        if (HIWORD(wp) == CBN_SELCHANGE) {
            if (id == IDC_WMODE) {
                int sel = (int)SendMessageW(s_comboMode, CB_GETCURSEL, 0, 0);
                g_renderer->Config().wandererMode = sel;
                g_renderer->ReinitWanderers();
                WriteIniInt(L"behavior", L"wanderer_mode", sel);
            } else if (id == IDC_SIMRES || id == IDC_DYERES) {
                int simSel = (int)SendMessageW(s_comboSim, CB_GETCURSEL, 0, 0);
                int dyeSel = (int)SendMessageW(s_comboDye, CB_GETCURSEL, 0, 0);
                int simRes = kSimResOptions[simSel < 0 ? 3 : simSel];
                int dyeRes = kDyeResOptions[dyeSel < 0 ? 2 : dyeSel];
                g_renderer->SetResolutions(simRes, dyeRes);
                WriteIniInt(L"sim", L"sim_res", simRes);
                WriteIniInt(L"sim", L"dye_res", dyeRes);
            }
            return 0;
        }
        return 0;
    }
    case WM_TIMER:
        if (s_fpsLabel) {
            wchar_t buf[64];
            swprintf_s(buf, L"Rendering at %.0f fps", g_currentFps);
            SetWindowTextW(s_fpsLabel, buf);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, s_headers.count((HWND)lp) ? kAccent : RGB(215, 215, 222));
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)s_darkBrush;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)s_darkBrush;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 2);
        s_fpsLabel = nullptr;
        s_headers.clear();
        s_wnd = nullptr;    // wallpaper keeps running
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND MakeCtl(const wchar_t* cls, const wchar_t* text, DWORD style,
                    int x, int y, int w, int h, HMENU id, bool header = false) {
    HWND ctl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, s_wnd, id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctl, WM_SETFONT, (WPARAM)(header ? s_headFont : s_font), TRUE);
    if (wcscmp(cls, L"BUTTON") == 0)
        SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
    else if (wcscmp(cls, L"COMBOBOX") == 0)
        SetWindowTheme(ctl, L"DarkMode_CFD", nullptr);
    if (header) s_headers.insert(ctl);
    return ctl;
}

void CloseSettingsWindow() {
    if (s_wnd) DestroyWindow(s_wnd);
}

void ShowSettingsWindow() {
    if (s_wnd) {
        ShowWindow(s_wnd, SW_SHOW);
        SetForegroundWindow(s_wnd);
        return;
    }
    if (!g_renderer) return;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    if (!s_font) s_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!s_headFont)
        s_headFont = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    static bool registered = false;
    if (!registered) {
        s_darkBrush = CreateSolidBrush(RGB(30, 30, 36));
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FluidWallpaperSettings";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = s_darkBrush;
        RegisterClassW(&wc);
        registered = true;
    }

    BuildDefs();
    s_sliderCtls.clear();
    s_sliderLabels.clear();

    const int cols = 4, colW = 396, margin = 14, rowH = 48, headH = 30;
    const int colX[4] = { margin, margin + (colW + margin),
                          margin + 2 * (colW + margin), margin + 3 * (colW + margin) };
    const int width = margin + cols * (colW + margin);

    // pre-compute total height: walk defs to find the tallest column
    int colY[4] = { 12, 12, 12, 12 };
    {
        int cur = 0;
        for (const SliderDef& d : s_sliders) {
            if (d.header) { cur = d.col; colY[cur] += headH; }
            colY[cur] += rowH;
        }
    }
    int slidersBottom = 12;
    for (int i = 0; i < 4; i++) slidersBottom = colY[i] > slidersBottom ? colY[i] : slidersBottom;

    int checkRows[4] = {};
    {
        int cur = 0;
        for (const CheckDef& d : s_checks) {
            if (d.header) cur = d.col;
            checkRows[cur]++;
        }
    }
    int maxCheck = 0;
    for (int i = 0; i < 4; i++) maxCheck = checkRows[i] > maxCheck ? checkRows[i] : maxCheck;
    const int checksTop = slidersBottom + 6;
    const int checksBottom = checksTop + headH + maxCheck * 25;
    const int bottomTop = checksBottom + 12;
    const int height = bottomTop + 3 * 34 + 12;

    RECT r = { 0, 0, width, height };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    s_wnd = CreateWindowExW(0, L"FluidWallpaperSettings",
                            L"Fluid Wallpaper — Settings",
                            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(s_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetTimer(s_wnd, 2, 500, nullptr);

    // sliders, grouped under accent headers
    {
        int y[4] = { 12, 12, 12, 12 };
        int cur = 0;
        for (size_t i = 0; i < s_sliders.size(); i++) {
            const SliderDef& d = s_sliders[i];
            if (d.header) {
                cur = d.col;
                MakeCtl(L"STATIC", d.header, 0, colX[cur], y[cur] + 6, colW, 20, nullptr, true);
                y[cur] += headH;
            }
            HWND label = MakeCtl(L"STATIC", L"", 0, colX[cur], y[cur], colW, 15, nullptr);
            HWND track = MakeCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS,
                                 colX[cur], y[cur] + 16, colW, 24, nullptr);
            int ticks = (int)((d.mx - d.mn) / d.step + 0.5f);
            SendMessageW(track, TBM_SETRANGE, FALSE, MAKELPARAM(0, ticks));
            int page = ticks / 25;
            if (page < 1) page = 1;
            SendMessageW(track, TBM_SETPAGESIZE, 0, page);
            SendMessageW(track, TBM_SETLINESIZE, 0, 1);
            if (d.fval == &g_hdrPeakNits && *d.fval < 0.0f)
                SendMessageW(track, TBM_SETPOS, TRUE, (LPARAM)(int)((g_maxNits - d.mn) / d.step + 0.5f));
            else
                SendMessageW(track, TBM_SETPOS, TRUE, SliderPos(d));
            s_sliderCtls.push_back(track);
            s_sliderLabels.push_back(label);
            UpdateSliderLabel(i);
            y[cur] += rowH;
        }
    }

    // checkboxes, grouped under accent headers
    {
        int y[4] = { checksTop, checksTop, checksTop, checksTop };
        int cur = 0;
        for (size_t i = 0; i < s_checks.size(); i++) {
            const CheckDef& d = s_checks[i];
            if (d.header) {
                cur = d.col;
                MakeCtl(L"STATIC", d.header, 0, colX[cur], y[cur] + 4, colW, 20, nullptr, true);
                y[cur] += headH;
            }
            HWND box = MakeCtl(L"BUTTON", d.label, BS_AUTOCHECKBOX,
                               colX[cur], y[cur], colW, 22,
                               (HMENU)(UINT_PTR)(IDC_CHECK_BASE + i));
            bool on = d.val ? *d.val : GetAutostart();
            SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            y[cur] += 25;
        }
    }

    FluidConfig& c = g_renderer->Config();

    // bottom row 1: gamut + wanderer path + resolutions
    int by = bottomTop;
    MakeCtl(L"STATIC", L"Gamut:", 0, colX[0], by + 5, 48, 16, nullptr);
    const wchar_t* gamutLabels[3] = { L"sRGB", L"Display-P3", L"BT.2020 (QD-OLED)" };
    for (int gIdx = 0; gIdx < 3; gIdx++) {
        HWND radio = MakeCtl(L"BUTTON", gamutLabels[gIdx],
                             BS_AUTORADIOBUTTON | (gIdx == 0 ? WS_GROUP : 0),
                             colX[0] + 52 + gIdx * 130, by + 2, 128, 22,
                             (HMENU)(UINT_PTR)(IDC_GAMUT_BASE + gIdx));
        SendMessageW(radio, BM_SETCHECK, g_gamutMode == gIdx ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    MakeCtl(L"STATIC", L"Wanderer path:", 0, colX[1] + 60, by + 5, 95, 16, nullptr);
    s_comboMode = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                          colX[1] + 160, by, 150, 200, (HMENU)(UINT_PTR)IDC_WMODE);
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Random wander");
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Circle");
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Figure 8");
    SendMessageW(s_comboMode, CB_SETCURSEL, c.wandererMode, 0);
    MakeCtl(L"STATIC", L"Sim res:", 0, colX[2], by + 5, 55, 16, nullptr);
    s_comboSim = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         colX[2] + 60, by, 90, 200, (HMENU)(UINT_PTR)IDC_SIMRES);
    MakeCtl(L"STATIC", L"Dye res:", 0, colX[2] + 170, by + 5, 55, 16, nullptr);
    s_comboDye = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         colX[2] + 230, by, 90, 200, (HMENU)(UINT_PTR)IDC_DYERES);
    for (int v : kSimResOptions) {
        wchar_t b[16]; swprintf_s(b, L"%d", v);
        SendMessageW(s_comboSim, CB_ADDSTRING, 0, (LPARAM)b);
    }
    for (int v : kDyeResOptions) {
        wchar_t b[16]; swprintf_s(b, L"%d", v);
        SendMessageW(s_comboDye, CB_ADDSTRING, 0, (LPARAM)b);
    }
    for (int i2 = 0; i2 < 5; i2++) {
        if (kSimResOptions[i2] == c.simRes) SendMessageW(s_comboSim, CB_SETCURSEL, i2, 0);
        if (kDyeResOptions[i2] == c.dyeRes) SendMessageW(s_comboDye, CB_SETCURSEL, i2, 0);
    }
    MakeCtl(L"STATIC", L"(changing res restarts the fluid)", 0, colX[3], by + 5, colW, 16, nullptr);

    // bottom row 2: palette pickers + windows
    by += 34;
    MakeCtl(L"STATIC", L"Palette (hue wheel off):", 0, colX[0], by + 5, 150, 16, nullptr);
    for (int ci = 0; ci < 5; ci++) {
        wchar_t lbl[16];
        swprintf_s(lbl, L"Color %d…", ci + 1);
        MakeCtl(L"BUTTON", lbl, BS_PUSHBUTTON,
                colX[0] + 155 + ci * 92, by, 86, 26, (HMENU)(UINT_PTR)(IDC_COLOR_BASE + ci));
    }
    MakeCtl(L"BUTTON", L"Open HDR analyzer", BS_PUSHBUTTON,
            colX[2], by, 160, 26, (HMENU)(UINT_PTR)IDC_OPEN_ANALYZER);
    MakeCtl(L"BUTTON", L"Scenes…", BS_PUSHBUTTON,
            colX[2] + 170, by, 120, 26, (HMENU)(UINT_PTR)IDC_SCENES_BTN);

    // bottom row 3: playback + save + fps
    by += 34;
    s_pauseBtn = MakeCtl(L"BUTTON", IsManualPaused() ? L"Resume wallpaper" : L"Pause wallpaper",
                         BS_PUSHBUTTON, colX[0], by, 150, 26, (HMENU)(UINT_PTR)IDC_PAUSE_BTN);
    MakeCtl(L"BUTTON", L"Exit wallpaper", BS_PUSHBUTTON,
            colX[0] + 160, by, 150, 26, (HMENU)(UINT_PTR)IDC_EXIT_BTN);
    MakeCtl(L"BUTTON", L"Save look as scene", BS_PUSHBUTTON,
            colX[1], by, 180, 26, (HMENU)(UINT_PTR)IDC_SAVE_SCENE);
    s_fpsLabel = MakeCtl(L"STATIC", L"Rendering at … fps", 0,
                         colX[2], by + 5, 250, 18, nullptr);

    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
