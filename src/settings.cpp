// Settings window: every wallpaper parameter (Wallpaper Engine parity + native
// extras), live-applied over FluidConfig + shell state, persisted to
// settings.ini per change. Opened from the tray menu or by re-running the exe.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include <vector>
#include <cstdio>
#include "app_state.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ---------------------------------------------------------------------------

struct SliderDef {
    const wchar_t* label;
    float mn, mx, step;
    int decimals;
    float* fval;            // one of fval/ival is set
    int* ival;
    const wchar_t* section;
    const wchar_t* key;
    bool reinitWanderers;
};
struct CheckDef {
    const wchar_t* label;
    bool* val;
    const wchar_t* section;  // nullptr => special (autostart)
    const wchar_t* key;
};

static HWND s_wnd = nullptr;
static HFONT s_font = nullptr;
static std::vector<SliderDef> s_sliders;
static std::vector<HWND> s_sliderCtls, s_sliderLabels;
static std::vector<CheckDef> s_checks;
static HWND s_comboMode = nullptr, s_comboSim = nullptr, s_comboDye = nullptr;

static const int IDC_CHECK_BASE = 300;
static const int IDC_GAMUT_BASE = 400;   // +0 sRGB, +1 P3, +2 2020
static const int IDC_WMODE      = 450;
static const int IDC_SIMRES     = 460;
static const int IDC_DYERES     = 461;
static const int IDC_COLOR_BASE = 500;   // 5 palette color buttons
static const int IDC_OPEN_ANALYZER = 260;
static const int IDC_PAUSE_BTN = 261;
static const int IDC_EXIT_BTN = 262;

static HWND s_fpsLabel = nullptr;
static HWND s_pauseBtn = nullptr;
static HBRUSH s_darkBrush = nullptr;

static const int kSimResOptions[] = { 32, 64, 128, 256, 512 };
static const int kDyeResOptions[] = { 256, 512, 1024, 2048, 4096 };

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
        // --- column 1: simulation ---
        { L"Vorticity (swirl strength)",        0,     50,   0.5f,  1, &c.curl,               nullptr, L"sim", L"vorticity", false },
        { L"Splat radius",                      0.01f, 1,    0.005f,3, &c.splatRadius,        nullptr, L"sim", L"splat_radius", false },
        { L"Density diffusion (dye linger)",    0.95f, 1,    0.0001f,4, &c.densityDissipation, nullptr, L"sim", L"density_diffusion", false },
        { L"Velocity diffusion",                0.95f, 1,    0.0001f,4, &c.velocityDissipation,nullptr, L"sim", L"velocity_diffusion", false },
        { L"Pressure diffusion",                0,     1,    0.005f,3, &c.pressureDissipation,nullptr, L"sim", L"pressure_diffusion", false },
        { L"Pressure iterations",               10,    60,   1,     0, nullptr, &c.pressureIterations, L"sim", L"pressure_iterations", false },
        { L"Tail decay speed (lower=snappier)", 0.5f,  1,    0.002f,3, &c.decayFast,          nullptr, L"sim", L"decay_fast", false },
        { L"Decay threshold",                   0,     0.3f, 0.002f,3, &c.decayThreshold,     nullptr, L"sim", L"decay_threshold", false },
        { L"Saturation restore /s",             0,     1,    0.005f,3, &c.satRestore,         nullptr, L"sim", L"saturation_restore", false },
        { L"Color intensity cap",               0.3f,  4,    0.05f, 2, &c.maxBrightness,      nullptr, L"sim", L"max_brightness", false },
        { L"Color cycle time (s)",              2,     120,  1,     0, &c.colorCyclePeriod,   nullptr, L"behavior", L"color_cycle_period", false },
        { L"FPS limit",                         30,    260,  1,     0, &c.fpsLimit,           nullptr, L"general", L"fps_limit", false },
        // --- column 2: wanderers / governor / dart ---
        { L"Wanderer count",                    1,     8,    1,     0, nullptr, &c.wandererCount,      L"behavior", L"wanderer_count", true },
        { L"Wanderer speed (px/s)",             50,    1200, 10,    0, &c.wandererSpeed,      nullptr, L"behavior", L"wanderer_speed", false },
        { L"Wanderer brightness",               0.05f, 1,    0.01f, 2, &c.wandererBrightness, nullptr, L"behavior", L"wanderer_brightness", false },
        { L"Wanderer path size (circle/fig-8)", 0.1f,  0.9f, 0.01f, 2, &c.wandererScale,      nullptr, L"behavior", L"wanderer_scale", true },
        { L"Resume after idle (s)",             0,     30,   0.5f,  1, &c.wandererResumeDelay,nullptr, L"behavior", L"wanderer_resume_delay", false },
        { L"Governor: min dark area %",         5,     60,   1,     0, &c.darkFloor,          nullptr, L"behavior", L"dark_floor", false },
        { L"Governor: dark pixel cutoff",       0.005f,0.1f, 0.005f,3, &c.darkLevel,          nullptr, L"behavior", L"dark_level", false },
        { L"Governor: survivor dark floor %",   0,     40,   1,     0, &c.survDarkFloor,      nullptr, L"behavior", L"surv_dark_floor", false },
        { L"Governor: contrast required %",     0,     100,  1,     0, &c.contrastReq,        nullptr, L"behavior", L"contrast_req", false },
        { L"Dart interval (s)",                 1,     30,   1,     0, &c.dartInterval,       nullptr, L"behavior", L"dart_interval", false },
        { L"Dart speed (px/s)",                 500,   6000, 50,    0, &c.dartSpeed,          nullptr, L"behavior", L"dart_speed", false },
        { L"Idle splat interval (s)",           0.5f,  30,   0.5f,  1, &c.idleInterval,       nullptr, L"behavior", L"idle_interval", false },
        // --- column 3: hue shift / HDR / idle ---
        { L"Idle splat amount",                 1,     30,   1,     0, nullptr, &c.idleAmount,         L"behavior", L"idle_amount", false },
        { L"Hue shift step (deg)",              10,    180,  1,     0, &c.hsStep,             nullptr, L"behavior", L"hueshift_step", false },
        { L"Hue shift linger (s)",              0,     15,   0.5f,  1, &c.hsLinger,           nullptr, L"behavior", L"hueshift_linger", false },
        { L"Hue shift glide (s)",               0.1f,  10,   0.1f,  1, &c.hsGlide,            nullptr, L"behavior", L"hueshift_glide", false },
        { L"Hue shift steps per burst",         1,     12,   1,     0, nullptr, &c.hsBurstSteps,       L"behavior", L"hueshift_burst_steps", false },
        { L"Hue shift off time (s)",            0,     120,  1,     0, &c.hsOffTime,          nullptr, L"behavior", L"hueshift_off_time", false },
        { L"HDR peak brightness (nits, 0=off)", 0,     1500, 5,     0, &g_hdrPeakNits,        nullptr, L"hdr", L"peak_nits", false },
        { L"HDR knee (boost starts at)",        0.1f,  1.3f, 0.02f, 2, &c.hdrKnee,            nullptr, L"hdr", L"knee", false },
        { L"HDR saturation boost",              1,     2,    0.01f, 2, &c.hdrSaturation,      nullptr, L"hdr", L"saturation", false },
        { L"HDR brightness boost",              0.8f,  1.5f, 0.01f, 2, &c.hdrBrightness,      nullptr, L"hdr", L"brightness", false },
        { L"HDR contrast",                      0.8f,  1.5f, 0.01f, 2, &c.hdrContrast,        nullptr, L"hdr", L"contrast", false },
        { L"Hue band center (deg)",             0,     360,  1,     0, &c.hueCenter,          nullptr, L"color", L"hue_center", false },
        { L"Hue band range (180=full wheel)",   5,     180,  1,     0, &c.hueRange,           nullptr, L"color", L"hue_range", false },
        { L"Cycle: base time (s)",              5,     120,  1,     0, &g_cycleBaseSec,       nullptr, L"cycle", L"base_seconds", false },
        { L"Cycle: interlude time (s)",         2,     60,   1,     0, &g_cycleInterludeSec,  nullptr, L"cycle", L"interlude_seconds", false },
        { L"Post saturation (WE panel)",        0.5f,  2,    0.01f, 2, &c.postSaturation,     nullptr, L"color", L"post_saturation", false },
        { L"Post contrast (WE panel)",          0.5f,  2,    0.01f, 2, &c.postContrast,       nullptr, L"color", L"post_contrast", false },
        { L"Post brightness (WE panel)",        0.5f,  1.5f, 0.01f, 2, &c.postBrightness,     nullptr, L"color", L"post_brightness", false },
        { L"Post hue rotate (deg)",             0,     360,  1,     0, &c.postHue,            nullptr, L"color", L"post_hue", false },
    };
    s_checks = {
        { L"Auto wanderer splats",              &c.wanderers,        L"behavior", L"wanderers" },
        { L"Auto-pause when screen full",       &c.autoPause,        L"behavior", L"auto_pause" },
        { L"Separating dart while paused",      &c.dartEnabled,      L"behavior", L"dart_enabled" },
        { L"Hue shift cycler",                  &c.hsEnabled,        L"behavior", L"hueshift_enabled" },
        { L"Idle random splats",                &c.idleSplats,       L"behavior", L"idle_splats" },
        { L"Shading",                           &c.shading,          L"sim",      L"shading" },
        { L"Random color (hue wheel)",          &c.colorful,         L"color",    L"colorful" },
        { L"Use all 5 palette colors",          &c.moreColors,       L"color",    L"more_colors" },
        { L"HDR compensation (sat/brightness)", &c.hdrCompensation,  L"hdr",      L"compensation" },
        { L"Hold left mouse = pour dye",        &c.holdToSplat,      L"behavior", L"hold_to_splat" },
        { L"Splat on click (if not holding)",   &c.splatOnClick,     L"behavior", L"splat_on_click" },
        { L"Mouse movement stirs fluid",        &c.showMouse,        L"behavior", L"show_mouse" },
        { L"Pause on fullscreen app",           &g_pauseOnFullscreen,L"general",  L"pause_on_fullscreen" },
        { L"Pause on maximized app",            &g_pauseOnMaximized, L"general",  L"pause_on_maximized" },
        { L"Preset interlude cycling",          &g_cycleEnabled,     L"cycle",    L"enabled" },
        { L"Start with Windows",                nullptr,             nullptr,     nullptr },  // registry-backed
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
        if (id == IDC_OPEN_ANALYZER) {
            ShowAnalyzerWindow();
            return 0;
        }
        if (id == IDC_PAUSE_BTN) {
            TogglePause();
            SetWindowTextW(s_pauseBtn, IsManualPaused() ? L"Resume wallpaper" : L"Pause wallpaper");
            return 0;
        }
        if (id == IDC_EXIT_BTN) {
            RequestExit();
            return 0;
        }
        if (id == 263) {   // Scenes…
            ShowScenesWindow();
            return 0;
        }
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
        SetTextColor(dc, RGB(225, 225, 230));
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
        s_wnd = nullptr;    // do NOT PostQuitMessage — the wallpaper keeps running
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND MakeCtl(const wchar_t* cls, const wchar_t* text, DWORD style,
                    int x, int y, int w, int h, HMENU id) {
    HWND ctl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, s_wnd, id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctl, WM_SETFONT, (WPARAM)s_font, TRUE);
    if (wcscmp(cls, L"BUTTON") == 0)
        SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);    // dark buttons/checkboxes
    else if (wcscmp(cls, L"COMBOBOX") == 0)
        SetWindowTheme(ctl, L"DarkMode_CFD", nullptr);         // dark dropdowns
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

    const int cols = 3, colW = 420, margin = 16, rowH = 50;
    const int rows = ((int)s_sliders.size() + cols - 1) / cols;   // 12
    const int checksY = margin + rows * rowH + 8;
    const int checkRows = ((int)s_checks.size() + cols - 1) / cols;
    const int bottomY = checksY + checkRows * 26 + 12;
    const int width = margin + cols * (colW + margin);
    const int height = bottomY + 4 * 32 + 16;

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

    // sliders, column-major
    for (size_t i = 0; i < s_sliders.size(); i++) {
        int col = (int)i / rows;
        int row = (int)i % rows;
        int x = margin + col * (colW + margin);
        int y = margin + row * rowH;
        HWND label = MakeCtl(L"STATIC", L"", 0, x, y, colW, 15, nullptr);
        HWND track = MakeCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS,
                             x, y + 16, colW, 24, nullptr);
        const SliderDef& d = s_sliders[i];
        int ticks = (int)((d.mx - d.mn) / d.step + 0.5f);
        SendMessageW(track, TBM_SETRANGE, FALSE, MAKELPARAM(0, ticks));
        // clicking the channel steps small; arrows always step exactly 1 tick
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
    }

    // checkboxes, column-major
    for (size_t i = 0; i < s_checks.size(); i++) {
        int col = (int)i / checkRows;
        int row = (int)i % checkRows;
        int x = margin + col * (colW + margin);
        int y = checksY + row * 26;
        HWND box = MakeCtl(L"BUTTON", s_checks[i].label, BS_AUTOCHECKBOX,
                           x, y, colW, 22, (HMENU)(UINT_PTR)(IDC_CHECK_BASE + i));
        bool on = s_checks[i].val ? *s_checks[i].val : GetAutostart();
        SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    FluidConfig& c = g_renderer->Config();

    // bottom row 1: gamut radios + wanderer path
    int by = bottomY;
    MakeCtl(L"STATIC", L"Color gamut:", 0, margin, by + 4, 85, 16, nullptr);
    const wchar_t* gamutLabels[3] = { L"sRGB", L"Display-P3", L"BT.2020 (QD-OLED)" };
    for (int gIdx = 0; gIdx < 3; gIdx++) {
        HWND radio = MakeCtl(L"BUTTON", gamutLabels[gIdx],
                             BS_AUTORADIOBUTTON | (gIdx == 0 ? WS_GROUP : 0),
                             margin + 90 + gIdx * 140, by + 2, 135, 22,
                             (HMENU)(UINT_PTR)(IDC_GAMUT_BASE + gIdx));
        SendMessageW(radio, BM_SETCHECK, g_gamutMode == gIdx ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    MakeCtl(L"STATIC", L"Wanderer path:", 0, margin + 540, by + 4, 95, 16, nullptr);
    s_comboMode = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                          margin + 640, by, 160, 200, (HMENU)(UINT_PTR)IDC_WMODE);
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Random wander");
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Circle");
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Figure 8");
    SendMessageW(s_comboMode, CB_SETCURSEL, c.wandererMode, 0);

    // bottom row 2: resolutions (changing restarts the fluid field)
    by += 32;
    MakeCtl(L"STATIC", L"Sim resolution:", 0, margin, by + 4, 95, 16, nullptr);
    s_comboSim = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         margin + 100, by, 100, 200, (HMENU)(UINT_PTR)IDC_SIMRES);
    for (int v : kSimResOptions) {
        wchar_t b[16]; swprintf_s(b, L"%d", v);
        SendMessageW(s_comboSim, CB_ADDSTRING, 0, (LPARAM)b);
    }
    for (int i2 = 0; i2 < 5; i2++)
        if (kSimResOptions[i2] == c.simRes) SendMessageW(s_comboSim, CB_SETCURSEL, i2, 0);
    MakeCtl(L"STATIC", L"Dye resolution:", 0, margin + 230, by + 4, 95, 16, nullptr);
    s_comboDye = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         margin + 330, by, 100, 200, (HMENU)(UINT_PTR)IDC_DYERES);
    for (int v : kDyeResOptions) {
        wchar_t b[16]; swprintf_s(b, L"%d", v);
        SendMessageW(s_comboDye, CB_ADDSTRING, 0, (LPARAM)b);
    }
    for (int i2 = 0; i2 < 5; i2++)
        if (kDyeResOptions[i2] == c.dyeRes) SendMessageW(s_comboDye, CB_SETCURSEL, i2, 0);
    MakeCtl(L"STATIC", L"(changing resolution restarts the fluid field)", 0,
            margin + 450, by + 4, 350, 16, nullptr);

    // bottom row 3: palette color pickers (used when "Random color" is off)
    by += 32;
    MakeCtl(L"STATIC", L"Palette (when hue wheel is off):", 0, margin, by + 4, 200, 16, nullptr);
    for (int ci = 0; ci < 5; ci++) {
        wchar_t lbl[16];
        swprintf_s(lbl, L"Color %d…", ci + 1);
        MakeCtl(L"BUTTON", lbl, BS_PUSHBUTTON,
                margin + 210 + ci * 95, by, 88, 24, (HMENU)(UINT_PTR)(IDC_COLOR_BASE + ci));
    }
    MakeCtl(L"BUTTON", L"Open HDR analyzer", BS_PUSHBUTTON,
            margin + 210 + 5 * 95 + 20, by, 160, 24, (HMENU)(UINT_PTR)IDC_OPEN_ANALYZER);

    // bottom row 4: playback controls + live fps
    by += 32;
    s_pauseBtn = MakeCtl(L"BUTTON", IsManualPaused() ? L"Resume wallpaper" : L"Pause wallpaper",
                         BS_PUSHBUTTON, margin, by, 150, 26, (HMENU)(UINT_PTR)IDC_PAUSE_BTN);
    MakeCtl(L"BUTTON", L"Exit wallpaper", BS_PUSHBUTTON,
            margin + 160, by, 150, 26, (HMENU)(UINT_PTR)IDC_EXIT_BTN);
    MakeCtl(L"BUTTON", L"Scenes…", BS_PUSHBUTTON,
            margin + 330, by, 120, 26, (HMENU)(UINT_PTR)263);
    s_fpsLabel = MakeCtl(L"STATIC", L"Rendering at … fps", 0,
                         margin + 470, by + 5, 250, 18, nullptr);

    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
