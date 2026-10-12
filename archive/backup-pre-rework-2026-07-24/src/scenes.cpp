// Scenes window: RGB-suite style preset manager. Scene list on the left,
// apply/save/delete + interlude-cycling setup on the right. Dark themed.
// Everything applies live and persists immediately.

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <cstdio>
#include "app_state.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static const int IDC_LIST = 100, IDC_APPLY = 101, IDC_OVERWRITE = 102,
                 IDC_NEW = 103, IDC_DELETE = 104, IDC_SETINTER = 105,
                 IDC_CYCEN = 106, IDC_RANDOM = 107, IDC_FOLDER = 108,
                 IDC_OPENSET = 109;

static HWND s_wnd = nullptr;
static HWND s_list = nullptr, s_interLabel = nullptr, s_cycEn = nullptr,
            s_random = nullptr, s_baseTrack = nullptr, s_interTrack = nullptr,
            s_baseLabel = nullptr, s_interTimeLabel = nullptr;
static HFONT s_font = nullptr, s_bigFont = nullptr;
static HBRUSH s_bg = nullptr;
static std::vector<std::wstring> s_paths, s_names;

static void RefreshList() {
    SendMessageW(s_list, LB_RESETCONTENT, 0, 0);
    s_paths.clear();
    s_names.clear();
    wchar_t dir[MAX_PATH], pattern[MAX_PATH];
    GetPresetsDirectory(dir);
    swprintf_s(pattern, L"%s\\*.ini", dir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            size_t dot = name.rfind(L".ini");
            if (dot != std::wstring::npos) name.resize(dot);
            s_names.push_back(name);
            s_paths.push_back(std::wstring(dir) + L"\\" + fd.cFileName);
            SendMessageW(s_list, LB_ADDSTRING, 0, (LPARAM)name.c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
}

static int SelIndex() {
    int i = (int)SendMessageW(s_list, LB_GETCURSEL, 0, 0);
    return (i >= 0 && i < (int)s_paths.size()) ? i : -1;
}

static void UpdateCycleUi() {
    wchar_t buf[192];
    if (g_cyclePreset == L"*")
        swprintf_s(buf, L"Interlude scene:  random each time");
    else {
        std::wstring n = g_cyclePreset;
        size_t dot = n.rfind(L".ini");
        if (dot != std::wstring::npos) n.resize(dot);
        swprintf_s(buf, L"Interlude scene:  %s", n.c_str());
    }
    SetWindowTextW(s_interLabel, buf);
    SendMessageW(s_cycEn, BM_SETCHECK, g_cycleEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(s_random, BM_SETCHECK, g_cyclePreset == L"*" ? BST_CHECKED : BST_UNCHECKED, 0);
    swprintf_s(buf, L"Your look runs for:  %.0f s", g_cycleBaseSec);
    SetWindowTextW(s_baseLabel, buf);
    swprintf_s(buf, L"Interlude runs for:  %.0f s", g_cycleInterludeSec);
    SetWindowTextW(s_interTimeLabel, buf);
    SendMessageW(s_baseTrack, TBM_SETPOS, TRUE, (LPARAM)(int)g_cycleBaseSec);
    SendMessageW(s_interTrack, TBM_SETPOS, TRUE, (LPARAM)(int)g_cycleInterludeSec);
}

static LRESULT CALLBACK ScenesWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        int sel = SelIndex();
        switch (id) {
        case IDC_LIST:
            if (code == LBN_DBLCLK && sel >= 0) ApplyPresetPath(s_paths[sel]);
            return 0;
        case IDC_APPLY:
            if (sel >= 0) ApplyPresetPath(s_paths[sel]);
            return 0;
        case IDC_OVERWRITE:
            if (sel >= 0) {
                wchar_t q[256];
                swprintf_s(q, L"Overwrite scene \"%s\" with your current look?", s_names[sel].c_str());
                if (MessageBoxW(hwnd, q, L"Overwrite scene", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    PersistFullConfigNow();
                    CopyFileW(g_iniPath, s_paths[sel].c_str(), FALSE);
                }
            }
            return 0;
        case IDC_NEW:
            SaveCurrentAsPresetFile();
            RefreshList();
            return 0;
        case IDC_DELETE:
            if (sel >= 0) {
                wchar_t q[256];
                swprintf_s(q, L"Delete scene \"%s\"?\nThis removes the file permanently.", s_names[sel].c_str());
                if (MessageBoxW(hwnd, q, L"Delete scene", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    std::wstring fname = s_names[sel] + L".ini";
                    DeleteFileW(s_paths[sel].c_str());
                    if (g_cyclePreset == fname) {
                        g_cyclePreset = L"*";
                        PersistShellSettings();
                    }
                    RefreshList();
                    UpdateCycleUi();
                }
            }
            return 0;
        case IDC_SETINTER:
            if (sel >= 0) {
                g_cyclePreset = s_names[sel] + L".ini";
                PersistShellSettings();
                UpdateCycleUi();
            }
            return 0;
        case IDC_CYCEN:
            g_cycleEnabled = SendMessageW(s_cycEn, BM_GETCHECK, 0, 0) == BST_CHECKED;
            PersistShellSettings();
            return 0;
        case IDC_RANDOM:
            if (SendMessageW(s_random, BM_GETCHECK, 0, 0) == BST_CHECKED)
                g_cyclePreset = L"*";
            else if (sel >= 0)
                g_cyclePreset = s_names[sel] + L".ini";
            PersistShellSettings();
            UpdateCycleUi();
            return 0;
        case IDC_FOLDER: {
            wchar_t dir[MAX_PATH];
            GetPresetsDirectory(dir);
            ShellExecuteW(nullptr, L"open", dir, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case IDC_OPENSET:
            ShowSettingsWindow();
            return 0;
        }
        return 0;
    }
    case WM_HSCROLL: {
        HWND ctl = (HWND)lp;
        if (ctl == s_baseTrack) {
            g_cycleBaseSec = (float)SendMessageW(ctl, TBM_GETPOS, 0, 0);
            PersistShellSettings();
            UpdateCycleUi();
        } else if (ctl == s_interTrack) {
            g_cycleInterludeSec = (float)SendMessageW(ctl, TBM_GETPOS, 0, 0);
            PersistShellSettings();
            UpdateCycleUi();
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(225, 225, 232));
        SetBkMode(dc, TRANSPARENT);
        if (msg == WM_CTLCOLORLISTBOX) SetBkColor(dc, RGB(24, 24, 30));
        static HBRUSH listBrush = CreateSolidBrush(RGB(24, 24, 30));
        return (LRESULT)(msg == WM_CTLCOLORLISTBOX ? listBrush : s_bg);
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)s_bg;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        s_wnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style,
               int x, int y, int w, int h, int id, bool big = false) {
    HWND ctl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, s_wnd, (HMENU)(UINT_PTR)id,
                               GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctl, WM_SETFONT, (WPARAM)(big ? s_bigFont : s_font), TRUE);
    if (wcscmp(cls, L"BUTTON") == 0 || wcscmp(cls, L"LISTBOX") == 0)
        SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
    return ctl;
}

void ShowScenesWindow() {
    if (s_wnd) {
        RefreshList();
        UpdateCycleUi();
        ShowWindow(s_wnd, SW_SHOW);
        SetForegroundWindow(s_wnd);
        return;
    }
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    if (!s_font) s_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!s_bigFont)
        s_bigFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    static bool registered = false;
    if (!registered) {
        s_bg = CreateSolidBrush(RGB(30, 30, 36));
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ScenesWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FluidWallpaperScenes";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = s_bg;
        RegisterClassW(&wc);
        registered = true;
    }

    const int W = 700, H = 520;
    RECT r = { 0, 0, W, H };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    s_wnd = CreateWindowExW(0, L"FluidWallpaperScenes", L"Fluid Wallpaper — Scenes",
                            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(s_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    Mk(L"STATIC", L"Scenes", 0, 16, 12, 200, 24, 0, true);
    s_list = Mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                16, 44, 300, 400, IDC_LIST);
    Mk(L"STATIC", L"Double-click a scene to apply it", 0, 16, 450, 300, 18, 0);

    const int bx = 340, bw = 330;
    Mk(L"BUTTON", L"Apply scene", BS_PUSHBUTTON, bx, 44, bw, 32, IDC_APPLY);
    Mk(L"BUTTON", L"Overwrite scene with current look", BS_PUSHBUTTON, bx, 82, bw, 32, IDC_OVERWRITE);
    Mk(L"BUTTON", L"New scene from current look", BS_PUSHBUTTON, bx, 120, bw, 32, IDC_NEW);
    Mk(L"BUTTON", L"Delete scene", BS_PUSHBUTTON, bx, 158, bw, 32, IDC_DELETE);

    Mk(L"STATIC", L"Auto-cycling", 0, bx, 212, bw, 22, 0, true);
    s_cycEn = Mk(L"BUTTON", L"Cycle: run my look, then an interlude scene, repeat",
                 BS_AUTOCHECKBOX, bx, 240, bw, 22, IDC_CYCEN);
    s_interLabel = Mk(L"STATIC", L"", 0, bx, 268, bw, 18, 0);
    Mk(L"BUTTON", L"Use selected scene as interlude", BS_PUSHBUTTON, bx, 290, bw, 28, IDC_SETINTER);
    s_random = Mk(L"BUTTON", L"Random scene each interlude", BS_AUTOCHECKBOX, bx, 324, bw, 22, IDC_RANDOM);

    s_baseLabel = Mk(L"STATIC", L"", 0, bx, 354, bw, 16, 0);
    s_baseTrack = Mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, bx, 371, bw, 24, 0);
    SendMessageW(s_baseTrack, TBM_SETRANGE, FALSE, MAKELPARAM(5, 120));
    s_interTimeLabel = Mk(L"STATIC", L"", 0, bx, 399, bw, 16, 0);
    s_interTrack = Mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, bx, 416, bw, 24, 0);
    SendMessageW(s_interTrack, TBM_SETRANGE, FALSE, MAKELPARAM(2, 60));

    Mk(L"BUTTON", L"Open presets folder", BS_PUSHBUTTON, bx, 452, 160, 28, IDC_FOLDER);
    Mk(L"BUTTON", L"All settings…", BS_PUSHBUTTON, bx + 170, 452, 160, 28, IDC_OPENSET);

    RefreshList();
    UpdateCycleUi();
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
