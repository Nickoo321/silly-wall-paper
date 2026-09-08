// AcidWallpaper — standalone "liquid acid" wallpaper (proof of concept).
// Trimmed app shell following src_oil/main.cpp's patterns: WorkerW wallpaper
// host attach (primary monitor only), tray icon (Pause / Palette / Exit),
// Explorer-restart reattach. No moods/settings/scenes/ini — hardcoded config.
//
// GUI app. Flags:
//   --console            open a diagnostics console
//   --shot <path.bmp>    dump one rendered frame (GPU readback) to a BMP, exit
//   --shot-delay <sec>   delay before the --shot capture (default 5)
//   --palette <0|1>      0 = Coral (default), 1 = Royal
//
// fps + diagnostics are also appended to %TEMP%\AcidWallpaper.log every 5 s
// (the --console re-attaches stdout, so shell redirection would go silent).

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#include "acid.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

void Fail(const char* what, HRESULT hr) {
    char buf[512];
    _snprintf_s(buf, _TRUNCATE, "%s failed (hr=0x%08lX)", what, (unsigned long)hr);
    fprintf(stderr, "FATAL: %s\n", buf);
    MessageBoxA(nullptr, buf, "Acid Wallpaper - fatal error", MB_ICONERROR);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// Logging: stdout (for --console) + %TEMP%\AcidWallpaper.log
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;

static void LogInit() {
    wchar_t tmp[MAX_PATH];
    if (GetEnvironmentVariableW(L"TEMP", tmp, MAX_PATH)) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%s\\AcidWallpaper.log", tmp);
        g_log = _wfsopen(path, L"a", _SH_DENYNO);   // allow reading while running
    }
}

static void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
}

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------
static bool          g_running = true;
static bool          g_manualPause = false;
static bool          g_shuttingDown = false;
static bool          g_wallpaperLost = false;
static AcidRenderer* g_renderer = nullptr;

// ---------------------------------------------------------------------------
// WorkerW: the layer behind the desktop icons (same dance as the fluid app)
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
        Log("WorkerW layout: classic (sibling WorkerW)\n");
        return ctx.wallpaper;
    }

    if (FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
        HWND workerw = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
        if (workerw) {
            Log("WorkerW layout: Win11 24H2 (WorkerW child of Progman)\n");
            return workerw;
        }
        Log("WorkerW layout: 24H2 without WorkerW child, using Progman\n");
        return progman;
    }

    Log("WorkerW layout: unknown, falling back to Progman\n");
    return progman;
}

static LRESULT CALLBACK WallpaperWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (g_shuttingDown) {
            PostQuitMessage(0);
        } else {
            // Explorer restart tore down the WorkerW hierarchy (and us with
            // it). Don't quit — the main loop re-hooks into the new shell.
            Log("wallpaper window destroyed externally (Explorer restart?) - will reattach\n");
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
    wc.lpszClassName = L"AcidWallpaperWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);   // fails harmlessly after the first call

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Acid Wallpaper",
        WS_POPUP, 0, 0, width, height,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) Fail("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));

    SetParent(hwnd, host);
    POINT origin = { 0, 0 };   // primary monitor is always at screen (0,0)
    ScreenToClient(host, &origin);
    SetWindowPos(hwnd, HWND_TOP, origin.x, origin.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return hwnd;
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------
static const UINT WM_TRAYICON = WM_APP + 1;
enum TrayCmd : UINT {
    CMD_PAUSE = 1, CMD_EXIT = 2,
    CMD_PAL_CORAL = 10, CMD_PAL_ROYAL = 11,
};

static NOTIFYICONDATAW g_nid = {};

static void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_manualPause ? MF_CHECKED : 0), CMD_PAUSE, L"Pause");

    HMENU pal = CreatePopupMenu();
    AppendMenuW(pal, MF_STRING, CMD_PAL_CORAL, L"Coral (coral / teal / amber)");
    AppendMenuW(pal, MF_STRING, CMD_PAL_ROYAL, L"Royal (purple / red veins)");
    int active = g_renderer ? g_renderer->Palette() : 0;
    CheckMenuRadioItem(pal, CMD_PAL_CORAL, CMD_PAL_ROYAL,
                       active == 0 ? CMD_PAL_CORAL : CMD_PAL_ROYAL, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)pal, L"Palette");

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
            Log("manual pause: %s\n", g_manualPause ? "on" : "off");
            break;
        case CMD_PAL_CORAL:
            if (g_renderer) g_renderer->SetPalette(0);
            Log("palette: Coral\n");
            break;
        case CMD_PAL_ROYAL:
            if (g_renderer) g_renderer->SetPalette(1);
            Log("palette: Royal\n");
            break;
        case CMD_EXIT:
            Log("tray: exit clicked\n");
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
    wc.lpszClassName = L"AcidWallpaperTray";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Acid Wallpaper Tray",
                                0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) Fail("CreateTrayWindow", HRESULT_FROM_WIN32(GetLastError()));

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Acid Wallpaper (PoC)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    return hwnd;
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"AcidWallpaper_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Acid Wallpaper is already running.",
                    L"Acid Wallpaper", MB_ICONINFORMATION);
        return 0;
    }

    static char  g_shotPath[MAX_PATH] = {};
    static float g_shotDelay = 5.0f;
    static int   g_palette = 0;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"--console") == 0) {
                AllocConsole();
                FILE* f;
                freopen_s(&f, "CONOUT$", "w", stdout);
                freopen_s(&f, "CONOUT$", "w", stderr);
            } else if (wcscmp(argv[i], L"--shot") == 0 && i + 1 < argc) {
                // dump one rendered frame to this BMP, then exit (automation)
                WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, g_shotPath, MAX_PATH,
                                    nullptr, nullptr);
            } else if (wcscmp(argv[i], L"--shot-delay") == 0 && i + 1 < argc) {
                g_shotDelay = (float)_wtof(argv[++i]);
            } else if (wcscmp(argv[i], L"--palette") == 0 && i + 1 < argc) {
                g_palette = _wtoi(argv[++i]);
            }
        }
        LocalFree(argv);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LogInit();

    Log("AcidWallpaper PoC - liquid acid blobs behind desktop icons\n");

    HWND host = FindWallpaperHost();
    if (!host) {
        MessageBoxW(nullptr, L"Could not find the desktop wallpaper layer (is Explorer running?)",
                    L"Acid Wallpaper", MB_ICONERROR);
        return 1;
    }

    const int width  = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    Log("Primary monitor: %dx%d\n", width, height);

    HWND hwnd = CreateWallpaperWindow(host, width, height);

    AcidConfig cfg;   // hardcoded defaults
    cfg.palette = g_palette & 1;
    AcidRenderer renderer;
    renderer.Init(hwnd, width, height, cfg);
    g_renderer = &renderer;
    if (g_shotPath[0]) renderer.RequestSnapshot(g_shotPath, g_shotDelay);

    CreateTrayWindow();

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    float fps = 0.0f;
    double fpsTime = 0.0;
    int fpsFrames = 0;

    MSG msg = {};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        // Survive Explorer restarts: wait for the new shell, then re-hook.
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

        if (g_manualPause) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
            QueryPerformanceCounter(&prev);   // don't integrate the paused gap
            continue;
        }

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);

        // FPS cap (Present is also vsynced)
        float fpsLim = cfg.fpsLimit;
        if (fpsLim < 10.0f) fpsLim = 10.0f;
        if (dt < 1.0f / (fpsLim + 2.0f)) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
            continue;
        }
        prev = now;
        if (dt > 0.1f) dt = 0.1f;   // clamp hitches (window reattach etc.)
        if (dt > 0.0001f)
            fps = fps * 0.95f + (1.0f / dt) * 0.05f;

        renderer.Frame(dt);
        if (renderer.ShotDone()) {
            Log("snapshot captured, exiting\n");
            break;
        }

        fpsTime += dt;
        fpsFrames++;
        if (fpsTime >= 5.0) {
            Log("fps: %.1f (%d frames in %.1f s)\n",
                fpsFrames / fpsTime, fpsFrames, fpsTime);
            fpsTime = 0.0;
            fpsFrames = 0;
        }
    }

    Log("Shutting down...\n");
    g_shuttingDown = true;
    RemoveTrayIcon();
    renderer.Shutdown();
    if (!g_wallpaperLost) DestroyWindow(hwnd);
    ReleaseMutex(mutex);
    if (g_log) fclose(g_log);

    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_SENDCHANGE);
    return 0;
}
