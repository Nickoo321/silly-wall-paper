// Oil3DLab — headless look lab for the Oil3D prototype (shell).
//
// HEADLESS ONLY: no window, no WorkerW, no tray, no swap chain, never a
// MessageBox, never AllocConsole. Launching without --shot logs one line and
// exits. The process drops itself to BELOW_NORMAL priority at start.
// Log: %TEMP%\Oil3DLab-shot.log (share-read), mirrored to the parent console.
//
// Flags:
//   --shot <out.png>      render OFFSCREEN, write <out>.png + <out>-hdr.png
//   --shot-size WxH       capture size (default 2560x1440)
//   --shot-delay S        simulated seconds before the first capture (default 15)
//   --shot-series N:S     N captures, S seconds apart, named by deciseconds
//   --shot-yield MS       Sleep after every GPU frame (default 2)
//   --seed N              every random value routes through this (default 1234)
//   --hdr on|off  --sdr-white N  --panel-max N
//   --timings             render 120 timed frames before the capture, print GPU ms
//   --console             attach to the PARENT console only (never creates one)
// Look overrides:
//   --shade gloss|flat  --blobs N  --holes N  --steps N  --half-res 0|1
//   --camera-drift X  --level X  --ink-level X  --depth X  --fov DEG  --size X

#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "lab.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

static FILE* g_log = nullptr;

static void LogInit() {
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp)) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%sOil3DLab-shot.log", tmp);
        g_log = _wfsopen(path, L"a", _SH_DENYNO);
    }
}

void LabLog(const char* fmt, ...) {
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
}

void LabFail(const char* what, HRESULT hr) {
    LabLog("FATAL: %s failed (hr=0x%08lX)\n", what, (unsigned long)hr);
    if (g_log) fclose(g_log);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// PNG output — copied from src_ink3d/main.cpp
// ---------------------------------------------------------------------------
static float LinearToSrgb(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}
static uint8_t ToByte(float srgb) {
    int v = (int)(srgb * 255.0f + 0.5f);
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

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
    for (wchar_t* p = dir; *p; p++) if (*p == L'/') *p = L'\\';
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return;
    *slash = 0;
    wchar_t parent[MAX_PATH];
    wcscpy_s(parent, MAX_PATH, dir);
    if (wchar_t* s2 = wcsrchr(parent, L'\\')) { *s2 = 0; CreateDirectoryW(parent, nullptr); }
    CreateDirectoryW(dir, nullptr);
}

static void WriteShotPair(const std::wstring& stem, const std::vector<float>& rgba,
                          int w, int h, float sdrScale, float elapsed) {
    const size_t n = (size_t)w * h;
    std::vector<uint8_t> sdr(n * 3), hdr(n * 3);
    double lumSum = 0.0;
    size_t aboveWhite = 0, negative = 0, nans = 0, black = 0;
    float maxScrgb = 0.0f;

    for (size_t p = 0; p < n; p++) {
        float r = rgba[p * 4 + 0], g = rgba[p * 4 + 1], b = rgba[p * 4 + 2];
        if (r != r || g != g || b != b) nans++;
        if (r != r) r = 0; if (g != g) g = 0; if (b != b) b = 0;   // NaN guard
        if (r < 0.0f || g < 0.0f || b < 0.0f) negative++;
        maxScrgb = fmaxf(maxScrgb, fmaxf(r, fmaxf(g, b)));
        lumSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        if (fmaxf(r, fmaxf(g, b)) > sdrScale) aboveWhite++;

        float dr = fmaxf(0.0f, r / sdrScale), dg = fmaxf(0.0f, g / sdrScale),
              db = fmaxf(0.0f, b / sdrScale);
        sdr[p * 3 + 0] = ToByte(LinearToSrgb(fminf(db, 1.0f)));
        sdr[p * 3 + 1] = ToByte(LinearToSrgb(fminf(dg, 1.0f)));
        sdr[p * 3 + 2] = ToByte(LinearToSrgb(fminf(dr, 1.0f)));
        if (sdr[p * 3 + 0] < 40 && sdr[p * 3 + 1] < 40 && sdr[p * 3 + 2] < 40) black++;

        float m = fmaxf(dr, fmaxf(dg, db));
        float k = m > 1e-6f ? (m / (1.0f + m)) / m : 0.0f;
        hdr[p * 3 + 0] = ToByte(LinearToSrgb(db * k));
        hdr[p * 3 + 1] = ToByte(LinearToSrgb(dg * k));
        hdr[p * 3 + 2] = ToByte(LinearToSrgb(dr * k));
    }

    std::wstring sdrPath = stem + L".png", hdrPath = stem + L"-hdr.png";
    EnsureParentDir(sdrPath.c_str());
    bool okS = WritePng(sdrPath.c_str(), sdr, w, h);
    bool okH = WritePng(hdrPath.c_str(), hdr, w, h);
    const double meanLum = lumSum / (double)n;
    LabLog("[shot] t=%.2fs %ls %dx%d  mean_lum=%.4f scRGB (%.1f nits)  "
           "above_sdr_white=%.3f%%  max_scRGB=%.3f (%.0f nits)  black(<40)=%.1f%%  "
           "negative_px=%.2f%%  nan_px=%zu  sdr=%s hdr=%s\n",
           elapsed, sdrPath.c_str(), w, h, meanLum, meanLum * 80.0,
           100.0 * aboveWhite / (double)n, maxScrgb, maxScrgb * 80.0f,
           100.0 * black / (double)n, 100.0 * negative / (double)n, nans,
           okS ? "ok" : "FAILED", okH ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------

static bool HasFlag(const wchar_t* f) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; i < argc && !found; i++) if (wcscmp(argv[i], f) == 0) found = true;
    LocalFree(argv);
    return found;
}

static int ShotMain() {
    std::wstring out;
    int   width = 2560, height = 1440;
    float delaySec = 15.0f;
    int   seriesCount = 1;
    float seriesInterval = 0.0f;
    unsigned seed = 1234;
    bool  hdrOn = false;
    float sdrWhiteNits = 240.0f, panelMaxNits = 1000.0f;
    int   yieldMs = 2;
    bool  timings = false;
    LabConfig cfg;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto nextF = [&](int& i) -> float { return i + 1 < argc ? (float)_wtof(argv[++i]) : 0.0f; };
    auto nextI = [&](int& i) -> int   { return i + 1 < argc ? _wtoi(argv[++i]) : 0; };
    for (int i = 1; i < argc; i++) {
        const wchar_t* a = argv[i];
        if      (wcscmp(a, L"--shot") == 0 && i + 1 < argc) out = argv[++i];
        else if (wcscmp(a, L"--shot-size") == 0 && i + 1 < argc) {
            int w = 0, h = 0;
            if (swscanf_s(argv[++i], L"%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                width = w; height = h;
            }
        }
        else if (wcscmp(a, L"--shot-delay") == 0)  delaySec = nextF(i);
        else if (wcscmp(a, L"--shot-yield") == 0)  yieldMs = nextI(i);
        else if (wcscmp(a, L"--shot-series") == 0 && i + 1 < argc) {
            int n = 0; float iv = 0;
            if (swscanf_s(argv[++i], L"%d:%f", &n, &iv) == 2 && n > 0) {
                seriesCount = n; seriesInterval = iv;
            }
        }
        else if (wcscmp(a, L"--seed") == 0)       seed = (unsigned)nextI(i);
        else if (wcscmp(a, L"--hdr") == 0 && i + 1 < argc)
            hdrOn = (_wcsicmp(argv[++i], L"on") == 0);
        else if (wcscmp(a, L"--sdr-white") == 0)  sdrWhiteNits = nextF(i);
        else if (wcscmp(a, L"--panel-max") == 0)  panelMaxNits = nextF(i);
        else if (wcscmp(a, L"--timings") == 0)    timings = true;
        else if (wcscmp(a, L"--shade") == 0 && i + 1 < argc)
            cfg.shade = (_wcsicmp(argv[++i], L"flat") == 0) ? LAB_FLAT : LAB_GLOSS;
        else if (wcscmp(a, L"--blobs") == 0)        cfg.blobs = nextI(i);
        else if (wcscmp(a, L"--holes") == 0)        cfg.holes = nextI(i);
        else if (wcscmp(a, L"--steps") == 0)        cfg.steps = nextI(i);
        else if (wcscmp(a, L"--half-res") == 0)     cfg.halfRes = nextI(i) != 0;
        else if (wcscmp(a, L"--camera-drift") == 0) cfg.cameraDrift = nextF(i);
        else if (wcscmp(a, L"--level") == 0)        cfg.level = nextF(i);
        else if (wcscmp(a, L"--ink-level") == 0)    cfg.inkLevel = nextF(i);
        else if (wcscmp(a, L"--depth") == 0)        cfg.depth = nextF(i);
        else if (wcscmp(a, L"--fov") == 0)          cfg.fovDeg = nextF(i);
        else if (wcscmp(a, L"--size") == 0)         cfg.size = nextF(i);
        else if (wcscmp(a, L"--chroma") == 0)       cfg.chroma = nextF(i);
    }
    LocalFree(argv);

    if (out.empty()) { LabLog("[shot] ERROR: --shot needs an output .png path\n"); return 2; }
    if (sdrWhiteNits < 1.0f) sdrWhiteNits = 80.0f;
    if (cfg.steps < 8) cfg.steps = 8;
    if (cfg.steps > 256) cfg.steps = 256;

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        LabLog("[shot] ERROR: CoInitializeEx failed\n");
        return 3;
    }
    const float sdrScale = hdrOn ? (sdrWhiteNits / 80.0f) : 1.0f;
    // speculars only: HDR headroom up to the panel's small-window peak
    const float hiGain = hdrOn ? fmaxf(1.0f, fminf(panelMaxNits / sdrWhiteNits, 4.5f)) : 1.6f;
    // HDR: nothing above the panel's small-window peak (scRGB 1.0 = 80 nits)
    const float maxOut = hdrOn ? panelMaxNits / 80.0f : 1e4f;
    LabLog("[shot] Oil3DLab offscreen capture\n");
    LabLog("[shot] size=%dx%d seed=%u delay=%.2fs series=%d:%.2fs hdr=%s sdr_white=%.0f "
           "sdrScale=%.3f hiGain=%.2f yield=%dms\n", width, height, seed, delaySec,
           seriesCount, seriesInterval, hdrOn ? "on" : "off", sdrWhiteNits, sdrScale,
           hiGain, yieldMs);

    LabRenderer r;
    r.InitOffscreen(width, height, cfg, seed);

    std::wstring stem = out;
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".png") == 0)
        stem.resize(stem.size() - 4);

    const float dt = 1.0f / 60.0f;
    long long steps = 0;
    const DWORD wallStart = GetTickCount();
    std::vector<float> pixels;
    const int timedFrames = timings ? 120 : 0;
    for (int s = 0; s < seriesCount; s++) {
        const double target = delaySec + (double)s * seriesInterval;
        const long long want = (long long)llround(target * 60.0);
        const long long timedFrom = (s == 0) ? want - timedFrames : want;
        while (steps < want) {
            r.Step(dt);
            steps++;
            if (steps > timedFrom) {          // --timings: one GPU frame per sim step
                if (steps == timedFrom + 4) r.ResetGpuStats();   // drop first-use cost
                r.Render(sdrScale, hiGain, maxOut);
                if (yieldMs > 0) Sleep((DWORD)yieldMs);
            }
            if (steps % 600 == 0) r.LogBlobs();
        }
        // three frames at the capture point: the last two give a rough GPU
        // ms on every shot (the first carries the first-use cost)
        const bool quickTiming = !timings && s == 0;
        for (int k = 0; k < (quickTiming ? 3 : 1); k++) {
            if (quickTiming && k == 1) { r.CaptureOffscreen(pixels); r.ResetGpuStats(); }
            r.Render(sdrScale, hiGain, maxOut);
            if (yieldMs > 0) Sleep((DWORD)yieldMs);
        }
        if (!r.CaptureOffscreen(pixels)) {
            LabLog("[shot] ERROR: readback failed\n");
            r.Shutdown();
            CoUninitialize();
            return 4;
        }
        std::wstring shotStem = stem;
        if (seriesCount > 1) {
            wchar_t suffix[32];
            swprintf_s(suffix, L"-%03d", (int)llround(target * 10.0));
            shotStem += suffix;
        }
        WriteShotPair(shotStem, pixels, width, height, sdrScale, (float)r.SimTime());
        if (quickTiming)
            for (int i = 0; i < r.PassCount(); i++)
                LabLog("[gpu] %-10s %7.3f ms (mean of %d frames)\n", r.PassName(i),
                       r.PassMsMean(i), r.GpuSamples());
    }

    if (timings) {
        LabLog("[timing] %d GPU frames at %dx%d (raymarch %dx%d, steps=%d, shade=%s)\n",
               r.GpuSamples(), width, height, r.RayW(), r.RayH(), cfg.steps,
               cfg.shade == LAB_FLAT ? "flat" : "gloss");
        double total = 0.0;
        for (int i = 0; i < r.PassCount(); i++) {
            LabLog("[timing] %-10s %7.3f ms mean  %7.3f ms min\n",
                   r.PassName(i), r.PassMsMean(i), r.PassMsMin(i));
            total += r.PassMsMean(i);
        }
        LabLog("[timing] TOTAL %.3f ms mean\n", total);
    }
    LabLog("[shot] done: %lld sim steps (%.2f s) in %.1f s wall\n",
           steps, r.SimTime(), (GetTickCount() - wallStart) / 1000.0);
    r.Shutdown();
    CoUninitialize();
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // never create a console or a window; attach to the launcher's console only
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    LogInit();

    if (!HasFlag(L"--shot")) {
        LabLog("Oil3DLab: HEADLESS look lab. Run with --shot <out.png>.\n");
        if (g_log) fclose(g_log);
        return 0;
    }
    int rc = ShotMain();
    if (g_log) fclose(g_log);
    return rc;
}
