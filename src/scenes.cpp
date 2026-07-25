// Scenes window: RGB-suite style preset manager. Scene list on the left,
// apply/save/delete on the right. Dark themed.
// Everything applies live and persists immediately.
// (Auto-cycling is the mood conductor's job now — see the Settings window.)

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <cstdio>
#include "app_state.h"
#include "moods.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static const int IDC_LIST = 100, IDC_APPLY = 101, IDC_OVERWRITE = 102,
                 IDC_NEW = 103, IDC_DELETE = 104, IDC_FOLDER = 108,
                 IDC_OPENSET = 109;

static HWND s_wnd = nullptr;
static HWND s_list = nullptr;
static HFONT s_font = nullptr, s_bigFont = nullptr;
static HBRUSH s_bg = nullptr;
static std::vector<std::wstring> s_paths, s_names;

static void RefreshList() {
    SendMessageW(s_list, LB_RESETCONTENT, 0, 0);
    s_paths.clear();
    s_names.clear();
    wchar_t dir[MAX_PATH], pattern[MAX_PATH];
    MoodsGetDirectory(dir);   // one managed recipe folder: the moods dir
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
                    DeleteFileW(s_paths[sel].c_str());
                    RefreshList();
                }
            }
            return 0;
        case IDC_FOLDER: {
            wchar_t dir[MAX_PATH];
            MoodsGetDirectory(dir);
            ShellExecuteW(nullptr, L"open", dir, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case IDC_OPENSET:
            ShowSettingsWindow();
            return 0;
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

    const int W = 700, H = 300;
    RECT r = { 0, 0, W, H };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    s_wnd = CreateWindowExW(0, L"FluidWallpaperScenes", L"Fluid Wallpaper - Looks",
                            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(s_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    Mk(L"STATIC", L"Scenes", 0, 16, 12, 200, 24, 0, true);
    s_list = Mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                16, 44, 300, 190, IDC_LIST);
    Mk(L"STATIC", L"Double-click a scene to apply it", 0, 16, 240, 300, 18, 0);

    const int bx = 340, bw = 330;
    Mk(L"BUTTON", L"Apply scene", BS_PUSHBUTTON, bx, 44, bw, 32, IDC_APPLY);
    Mk(L"BUTTON", L"Overwrite scene with current look", BS_PUSHBUTTON, bx, 82, bw, 32, IDC_OVERWRITE);
    Mk(L"BUTTON", L"New scene from current look", BS_PUSHBUTTON, bx, 120, bw, 32, IDC_NEW);
    Mk(L"BUTTON", L"Delete scene", BS_PUSHBUTTON, bx, 158, bw, 32, IDC_DELETE);

    Mk(L"BUTTON", L"Open moods folder", BS_PUSHBUTTON, bx, 212, 160, 28, IDC_FOLDER);
    Mk(L"BUTTON", L"All settings…", BS_PUSHBUTTON, bx + 170, 212, 160, 28, IDC_OPENSET);

    RefreshList();
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
