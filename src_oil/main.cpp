// OilWallpaper — standalone lava-lamp oil-blob wallpaper (proof of concept).
// Trimmed app shell following src/main.cpp's patterns: WorkerW wallpaper
// host attach (primary monitor only), tray icon (Pause / Palette / Exit),
// Explorer-restart reattach. No moods/settings/scenes/ini — hardcoded config.
//
// GUI app. Flags:
//   --console            open a diagnostics console
//   --palette <0|1>      0 = Acid (default), 1 = Royal
//   --step <0..8>        Phase 0 look gate (see oil.h); default 8 = everything
//
// --shot is a fully HEADLESS path (see RunShotMode below): no window, no
// WorkerW attach, no tray, no single-instance mutex, no SPI_SETDESKWALLPAPER.
// It renders into an offscreen R8G8B8A8 target on a fixed timestep with no
// vsync, so "70 seconds of wallpaper" costs a few seconds of wall clock and a
// running wallpaper (FluidWallpaper / Wallpaper Engine) is never disturbed.
//   --shot <out.png>     offscreen capture, then exit
//   --shot-delay <sec>   simulated time before the first capture (default 25)
//   --shot-series N:IV   N captures IV simulated seconds apart
//   --shot-size WxH      capture size (default 2560x1440)
//   --shot-fps <hz>      fixed simulation timestep (default 60)
//   --shot-yield <ms>    sleep per rendered frame (0; only for long bench runs)
//   --shot-warm <n>      frames actually rendered before each capture (6);
//                        everything before that is CPU sim only, no GPU work
//   --seed <n>           deterministic blob seeding (default 1234)
//   --refract <k>        override OilConfig::refractK    (A/B without a rebuild)
//   --marble-scale <s>   override OilConfig::marbleScale
//   --rim-width <w>      override OilConfig::rimWidth
//
// fps + diagnostics are also appended to %TEMP%\OilWallpaper.log every 5 s
// (the --console re-attaches stdout, so shell redirection would go silent).

#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <share.h>
#include <string>
#include <vector>
#include "oil.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

void Fail(const char* what, HRESULT hr) {
    char buf[512];
    _snprintf_s(buf, _TRUNCATE, "%s failed (hr=0x%08lX)", what, (unsigned long)hr);
    fprintf(stderr, "FATAL: %s\n", buf);
    MessageBoxA(nullptr, buf, "Oil Wallpaper - fatal error", MB_ICONERROR);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// Logging: stdout (for --console) + %TEMP%\OilWallpaper.log
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;

static void LogInit() {
    wchar_t tmp[MAX_PATH];
    if (GetEnvironmentVariableW(L"TEMP", tmp, MAX_PATH)) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%s\\OilWallpaper.log", tmp);
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
static bool         g_running = true;
static bool         g_manualPause = false;
static bool         g_shuttingDown = false;
static bool         g_wallpaperLost = false;
static OilRenderer* g_renderer = nullptr;

// ===========================================================================
// --shot: headless offscreen capture
//
// Nothing in this path creates a window, finds Progman/WorkerW, adds a tray
// icon, takes the single-instance mutex or calls SPI_SETDESKWALLPAPER.
// ===========================================================================
static void ShotLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    wchar_t path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"OilWallpaper-shot.log");
    if (FILE* f = _wfsopen(path, L"a", _SH_DENYNO)) { fputs(buf, f); fclose(f); }
}

// WIC PNG writer (24bpp BGR).
static bool WritePng(const wchar_t* path, const std::vector<unsigned char>& bgr,
                     int w, int h) {
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fac)))) return false;
    bool ok = false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    do {
        if (FAILED(fac->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) break;
        if (FAILED(fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) break;
        if (FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(enc->CreateNewFrame(&frame, &props))) break;
        if (FAILED(frame->Initialize(props))) break;
        if (FAILED(frame->SetSize((UINT)w, (UINT)h))) break;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
        if (FAILED(frame->SetPixelFormat(&fmt))) break;
        const UINT stride = (UINT)w * 3;
        if (FAILED(frame->WritePixels((UINT)h, stride, stride * (UINT)h,
                                      const_cast<BYTE*>(bgr.data())))) break;
        if (FAILED(frame->Commit())) break;
        ok = SUCCEEDED(enc->Commit());
    } while (false);
    if (props) props->Release();
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    fac->Release();
    return ok;
}

static void EnsureParentDir(const wchar_t* path) {
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, path);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return;
    *slash = 0;
    wchar_t parent[MAX_PATH];
    wcscpy_s(parent, MAX_PATH, dir);
    if (wchar_t* s2 = wcsrchr(parent, L'\\')) { *s2 = 0; CreateDirectoryW(parent, nullptr); }
    CreateDirectoryW(dir, nullptr);
}

static float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

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

    std::wstring out;
    int   width = 2560, height = 1440;
    float delaySec = 25.0f, seriesInterval = 0.0f, shotFps = 60.0f;
    int   seriesCount = 1, yieldMs = 0, warmFrames = 6;
    OilConfig cfg;
    cfg.seed = 1234;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            auto next = [&]() -> const wchar_t* { return i + 1 < argc ? argv[++i] : nullptr; };
            if (wcscmp(argv[i], L"--shot") == 0) {
                if (const wchar_t* v = next()) out = v;
            } else if (wcscmp(argv[i], L"--shot-size") == 0) {
                if (const wchar_t* v = next()) {
                    int w = 0, h = 0;
                    if (swscanf_s(v, L"%dx%d", &w, &h) == 2 && w > 0 && h > 0) { width = w; height = h; }
                }
            } else if (wcscmp(argv[i], L"--shot-delay") == 0) {
                if (const wchar_t* v = next()) delaySec = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--shot-series") == 0) {
                if (const wchar_t* v = next()) {
                    int n = 0; float iv = 0;
                    if (swscanf_s(v, L"%d:%f", &n, &iv) == 2 && n > 0) { seriesCount = n; seriesInterval = iv; }
                }
            } else if (wcscmp(argv[i], L"--shot-fps") == 0) {
                if (const wchar_t* v = next()) shotFps = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--shot-yield") == 0) {
                if (const wchar_t* v = next()) yieldMs = _wtoi(v);
            } else if (wcscmp(argv[i], L"--shot-warm") == 0) {
                if (const wchar_t* v = next()) warmFrames = _wtoi(v);
            } else if (wcscmp(argv[i], L"--palette") == 0) {
                if (const wchar_t* v = next()) cfg.palette = _wtoi(v) & 1;
            } else if (wcscmp(argv[i], L"--step") == 0) {
                if (const wchar_t* v = next()) cfg.lookStep = _wtoi(v);
            } else if (wcscmp(argv[i], L"--seed") == 0) {
                if (const wchar_t* v = next()) cfg.seed = (unsigned)_wtoi(v);
            } else if (wcscmp(argv[i], L"--refract") == 0) {
                if (const wchar_t* v = next()) cfg.refractK = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--marble-scale") == 0) {
                if (const wchar_t* v = next()) cfg.marbleScale = (float)_wtof(v);
            } else if (wcscmp(argv[i], L"--rim-width") == 0) {
                if (const wchar_t* v = next()) cfg.rimWidth = (float)_wtof(v);
            }
        }
        LocalFree(argv);
    }
    if (out.empty()) {
        ShotLog("[shot] ERROR: --shot needs an output .png path\n");
        return 2;
    }
    if (shotFps < 10.0f) shotFps = 10.0f;

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        ShotLog("[shot] ERROR: CoInitializeEx failed\n");
        return 3;
    }

    ShotLog("[shot] OilWallpaper headless capture: %dx%d palette=%d step=%d seed=%u "
            "delay=%.1fs series=%d:%.1fs fps=%.0f\n",
            width, height, cfg.palette, cfg.lookStep, cfg.seed,
            delaySec, seriesCount, seriesInterval, shotFps);

    OilRenderer renderer;
    renderer.InitOffscreen(width, height, cfg);

    std::wstring stem = out;
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".png") == 0)
        stem.resize(stem.size() - 4);

    const float dt = 1.0f / shotFps;
    long long frames = 0, nextLog = 0;
    const DWORD wallStart = GetTickCount();
    std::vector<unsigned char> rgba;
    std::vector<unsigned char> bgr((size_t)width * height * 3);

    for (int s = 0; s < seriesCount; s++) {
        const double target = delaySec + (double)s * seriesInterval;
        const long long want = (long long)llround(target * shotFps);
        // The oil look holds no GPU-side state, so the sim can be fast
        // forwarded on the CPU and only the last `warmFrames` actually
        // rendered. That keeps a capture to a fraction of a second of GPU
        // work — the user's live wallpaper is on the same card.
        const long long renderFrom = want - (long long)warmFrames;
        renderer.ResetGpuStats();       // per-capture GPU cost
        while (frames < want) {
            if (frames >= renderFrom) {
                renderer.Frame(dt);
                // Leave the GPU air: an unthrottled full-screen render starves
                // the compositor (and once greyed the OLED when two ran at the
                // same time). Never run two of these concurrently.
                if (yieldMs > 0) Sleep((DWORD)yieldMs);
            } else {
                renderer.Advance(dt);       // CPU sim only, no GPU submission
            }
            frames++;
            if (frames >= nextLog) {
                ShotLog("[shot] simulated %.1f s (%lld frames, %.1f s wall)\n",
                        frames / shotFps, frames, (GetTickCount() - wallStart) / 1000.0);
                nextLog = frames + (long long)(shotFps * 30);
            }
        }
        if (!renderer.CaptureOffscreen(rgba)) {
            ShotLog("[shot] ERROR: readback failed\n");
            renderer.Shutdown();
            CoUninitialize();
            return 4;
        }

        // stats + BGR pack. Mean LINEAR luminance is the ABL-relevant number
        // (proportional to panel light output); mean code luma is what the
        // 8-bit frame actually carries.
        double linSum = 0.0, codeSum = 0.0;
        size_t hot = 0;
        const size_t n = (size_t)width * height;
        for (size_t p = 0; p < n; p++) {
            const float r = rgba[p * 4 + 0] / 255.0f;
            const float g = rgba[p * 4 + 1] / 255.0f;
            const float b = rgba[p * 4 + 2] / 255.0f;
            const float lin = 0.2126f * SrgbToLinear(r) + 0.7152f * SrgbToLinear(g)
                            + 0.0722f * SrgbToLinear(b);
            linSum += lin;
            codeSum += 0.2126f * r + 0.7152f * g + 0.0722f * b;
            if (lin > 0.5f) hot++;
            bgr[p * 3 + 0] = rgba[p * 4 + 2];
            bgr[p * 3 + 1] = rgba[p * 4 + 1];
            bgr[p * 3 + 2] = rgba[p * 4 + 0];
        }

        std::wstring shotPath = stem;
        if (seriesCount > 1) {
            wchar_t suffix[32];
            swprintf_s(suffix, L"-%03d", (int)llround(target));
            shotPath += suffix;
        }
        shotPath += L".png";
        EnsureParentDir(shotPath.c_str());
        const bool ok = WritePng(shotPath.c_str(), bgr, width, height);
        ShotLog("[shot] t=%.1fs %ls mean_lum_linear=%.4f mean_code_luma=%.4f "
                "above_half=%.2f%% gpu_ms min=%.3f mean=%.3f  %s\n",
                frames / shotFps, shotPath.c_str(), linSum / (double)n, codeSum / (double)n,
                100.0 * hot / (double)n, renderer.GpuMsMin(), renderer.GpuMsMean(),
                ok ? "ok" : "FAILED");
    }

    ShotLog("[shot] done: %lld frames in %.1f s wall, gpu %.3f ms min / %.3f ms mean "
            "(%dx%d, step %d)\n",
            frames, (GetTickCount() - wallStart) / 1000.0,
            renderer.GpuMsMin(), renderer.GpuMsMean(), width, height, cfg.lookStep);
    renderer.Shutdown();
    CoUninitialize();
    return 0;
}

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
    wc.lpszClassName = L"OilWallpaperWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);   // fails harmlessly after the first call

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Oil Wallpaper",
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
    CMD_PAL_ACID = 10, CMD_PAL_ROYAL = 11,
};

static NOTIFYICONDATAW g_nid = {};

static void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_manualPause ? MF_CHECKED : 0), CMD_PAUSE, L"Pause");

    HMENU pal = CreatePopupMenu();
    AppendMenuW(pal, MF_STRING, CMD_PAL_ACID, L"Acid (ink / coral / cyan)");
    AppendMenuW(pal, MF_STRING, CMD_PAL_ROYAL, L"Royal (violet / orange / pink)");
    int active = g_renderer ? g_renderer->Palette() : 0;
    CheckMenuRadioItem(pal, CMD_PAL_ACID, CMD_PAL_ROYAL,
                       active == 0 ? CMD_PAL_ACID : CMD_PAL_ROYAL, MF_BYCOMMAND);
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
        case CMD_PAL_ACID:
            if (g_renderer) g_renderer->SetPalette(0);
            Log("palette: Acid\n");
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
    wc.lpszClassName = L"OilWallpaperTray";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Oil Wallpaper Tray",
                                0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) Fail("CreateTrayWindow", HRESULT_FROM_WIN32(GetLastError()));

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Oil Wallpaper (PoC)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    return hwnd;
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Headless capture first: no window, no WorkerW, no tray, no mutex
    // handshake (a running wallpaper must not be disturbed).
    if (ShotModeRequested()) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        return RunShotMode();
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"OilWallpaper_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Oil Wallpaper is already running.",
                    L"Oil Wallpaper", MB_ICONINFORMATION);
        return 0;
    }

    static int g_palette = 0;
    static int g_step = 8;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"--console") == 0) {
                AllocConsole();
                FILE* f;
                freopen_s(&f, "CONOUT$", "w", stdout);
                freopen_s(&f, "CONOUT$", "w", stderr);
            } else if (wcscmp(argv[i], L"--palette") == 0 && i + 1 < argc) {
                g_palette = _wtoi(argv[++i]);
            } else if (wcscmp(argv[i], L"--step") == 0 && i + 1 < argc) {
                g_step = _wtoi(argv[++i]);
            }
        }
        LocalFree(argv);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LogInit();

    Log("OilWallpaper PoC - lava-lamp oil blobs behind desktop icons\n");

    HWND host = FindWallpaperHost();
    if (!host) {
        MessageBoxW(nullptr, L"Could not find the desktop wallpaper layer (is Explorer running?)",
                    L"Oil Wallpaper", MB_ICONERROR);
        return 1;
    }

    const int width  = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    Log("Primary monitor: %dx%d\n", width, height);

    HWND hwnd = CreateWallpaperWindow(host, width, height);

    OilConfig cfg;   // hardcoded defaults
    cfg.palette = g_palette & 1;
    cfg.lookStep = g_step;
    OilRenderer renderer;
    renderer.Init(hwnd, width, height, cfg);
    g_renderer = &renderer;

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

        fpsTime += dt;
        fpsFrames++;
        if (fpsTime >= 5.0) {
            Log("fps: %.1f (%d frames in %.1f s), gpu %.3f ms mean\n",
                fpsFrames / fpsTime, fpsFrames, fpsTime, renderer.GpuMsMean());
            fpsTime = 0.0;
            fpsFrames = 0;
            renderer.ResetGpuStats();
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
