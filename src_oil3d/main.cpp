// Oil3DWallpaper — app shell (prototype). Brief: reference/briefs/NEXT-oil3d.md.
//
// HEADLESS ONLY. No wallpaper window, no WorkerW attach, no tray, no swap
// chain, no focus change, no MessageBox: launching without --shot logs one
// line and exits (the src_ink3d rule), so the exe can never disturb a running
// FluidWallpaper.exe or Wallpaper Engine. The desktop attach is a later
// milestone, after the user OKs the look.
//
// WIN32 subsystem (like the other standalone targets) so no console window is
// ever created; it attaches to the launching console when there is one, and
// always mirrors to %TEMP%\Oil3DWallpaper-shot.log.
//
// Flags:
//   --shot <out.png>      render OFFSCREEN, write <out>.png + <out>-hdr.png
//   --shot-size WxH       capture size (default 2560x1440)
//   --shot-delay S        simulated seconds before the first capture (default 15)
//   --shot-series N:S     N captures, S seconds apart, named by elapsed seconds
//   --shot-yield MS       Sleep per simulated frame (default 2)
//   --seed N              every random value routes through this (default 1234)
//   --ini <path>          REPEATABLE, applied in order as partial overlays
//                         (base + layout + scheme stack like the 2D cycle;
//                         each file's [meta] base chain first, unless an
//                         earlier --ini already applied that base)
//   --hdr on|off  --sdr-white N  --panel-max N
//   --timings             per-pass GPU timing table (timestamp queries)
//   --console             verbose: lists the ini keys that have no 3D meaning
//                         (attaches to the parent console only; never opens one)
// Look overrides (each also an [oil3d] ini key):
//   --blobs N  --half-res 0|1  --steps N  --camera-drift X  --shade flat|gloss
//   --debug-view N  (1 normal, 2 hole share, 3 AO, 4 thickness, 5 unlit fill)
//
// The sim runs at a FIXED 60 Hz step on simulated time, so the same flags
// always give the same image.

#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <wrl/client.h>

#include "oil3d.h"
#include "config.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

// ---------------------------------------------------------------------------
// logging (no MessageBox on any path — the user games; log instead)
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;

static void LogInit() {
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp)) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%sOil3DWallpaper-shot.log", tmp);
        g_log = _wfsopen(path, L"a", _SH_DENYNO);
    }
}

void Oil3DLog(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
}

void Oil3DFail(const char* what, HRESULT hr) {
    Oil3DLog("FATAL: %s failed (hr=0x%08lX)\n", what, (unsigned long)hr);
    if (g_log) fclose(g_log);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// PNG output — copied from src_ink3d/main.cpp (itself src/main.cpp @873a6dc)
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
    wchar_t full[MAX_PATH * 2];
    if (!GetFullPathNameW(path, MAX_PATH * 2, full, nullptr)) return;
    // create every missing directory along the way
    for (wchar_t* p = full; *p; p++) {
        if ((*p == L'\\' || *p == L'/') && p > full && *(p - 1) != L':') {
            const wchar_t c = *p;
            *p = 0;
            CreateDirectoryW(full, nullptr);
            *p = c;
        }
    }
}

static void WriteShotPair(const std::wstring& stem, const std::vector<float>& rgba,
                          int w, int h, float sdrScale, float elapsed) {
    const size_t n = (size_t)w * h;
    std::vector<uint8_t> sdr(n * 3), hdr(n * 3);
    double lumSum = 0.0;
    size_t aboveWhite = 0, negative = 0;
    float maxScrgb = 0.0f;

    for (size_t p = 0; p < n; p++) {
        float r = rgba[p * 4 + 0], g = rgba[p * 4 + 1], b = rgba[p * 4 + 2];
        if (r != r) r = 0;
        if (g != g) g = 0;
        if (b != b) b = 0;   // NaN guard
        if (r < 0.0f || g < 0.0f || b < 0.0f) negative++;
        maxScrgb = fmaxf(maxScrgb, fmaxf(r, fmaxf(g, b)));
        lumSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        if (fmaxf(r, fmaxf(g, b)) > sdrScale) aboveWhite++;

        float dr = fmaxf(0.0f, r / sdrScale), dg = fmaxf(0.0f, g / sdrScale),
              db = fmaxf(0.0f, b / sdrScale);
        sdr[p * 3 + 0] = ToByte(LinearToSrgb(fminf(db, 1.0f)));
        sdr[p * 3 + 1] = ToByte(LinearToSrgb(fminf(dg, 1.0f)));
        sdr[p * 3 + 2] = ToByte(LinearToSrgb(fminf(dr, 1.0f)));

        // -hdr.png: a Reinhard view of the whole range, so the highlights
        // above SDR white are visible on an SDR viewer (rough check only)
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
    Oil3DLog("[shot] t=%.2fs %ls %dx%d  mean_lum=%.4f scRGB (%.1f nits)  "
             "above_sdr_white=%.3f%%  max_scRGB=%.3f (%.0f nits)  negative_px=%.2f%%  "
             "sdr=%s hdr=%s\n",
             elapsed, sdrPath.c_str(), w, h, meanLum, meanLum * 80.0,
             100.0 * aboveWhite / (double)n, maxScrgb, maxScrgb * 80.0f,
             100.0 * negative / (double)n, okS ? "ok" : "FAILED", okH ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------

struct ShotOpts {
    std::wstring out;
    std::vector<std::wstring> inis;
    int   width = 2560, height = 1440;
    float delaySec = 15.0f;
    int   seriesCount = 1;
    float seriesInterval = 0.0f;
    unsigned seed = 1234;
    bool  hdrOn = false;
    float sdrWhiteNits = 240.0f;
    float panelMaxNits = -1.0f;
    int   yieldMs = 2;
    bool  timings = false;
    bool  console = false;
};

static bool HasFlag(const wchar_t* f) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; i < argc && !found; i++) if (wcscmp(argv[i], f) == 0) found = true;
    LocalFree(argv);
    return found;
}

static int wmain_shot() {
    ShotOpts o;
    Oil3DConfig cfg;
    o.console = HasFlag(L"--console");

    // --- pass 1: every --ini, IN ORDER, so the explicit flags below win
    std::vector<std::wstring> ignored;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++)
            if (wcscmp(argv[i], L"--ini") == 0 && i + 1 < argc) o.inis.push_back(argv[++i]);
        LocalFree(argv);
    }
    for (const std::wstring& ini : o.inis) {
        Oil3DLog("[ini] %ls\n", ini.c_str());
        if (!LoadOil3DConfig(ini.c_str(), cfg, &ignored)) {
            Oil3DLog("[shot] ERROR: --ini file not found or unreadable: %ls\n", ini.c_str());
            return 2;
        }
    }
    {
        // keys with no 3D meaning: counted always, listed once with --console
        std::set<std::wstring> uniq(ignored.begin(), ignored.end());
        Oil3DLog("[ini] %d [liquid_acid]/[oil3d] keys have no 3D meaning and are ignored%s\n",
                 (int)uniq.size(), o.console ? ":" : " (--console lists them)");
        if (o.console) {
            std::wstring line;
            for (const std::wstring& k : uniq) {
                if (line.size() + k.size() > 100) {
                    Oil3DLog("[ini]   %ls\n", line.c_str());
                    line.clear();
                }
                line += k + L"  ";
            }
            if (!line.empty()) Oil3DLog("[ini]   %ls\n", line.c_str());
        }
    }

    // --- pass 2: everything else
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        auto nextF = [&](int& i) -> float { return i + 1 < argc ? (float)_wtof(argv[++i]) : 0.0f; };
        auto nextI = [&](int& i) -> int   { return i + 1 < argc ? _wtoi(argv[++i]) : 0; };
        for (int i = 1; i < argc; i++) {
            const wchar_t* a = argv[i];
            if      (wcscmp(a, L"--ini") == 0) { i++; }   // pass 1
            else if (wcscmp(a, L"--shot") == 0 && i + 1 < argc) o.out = argv[++i];
            else if (wcscmp(a, L"--shot-size") == 0 && i + 1 < argc) {
                int w = 0, h = 0;
                if (swscanf_s(argv[++i], L"%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                    o.width = w; o.height = h;
                }
            }
            else if (wcscmp(a, L"--shot-delay") == 0)  o.delaySec = nextF(i);
            else if (wcscmp(a, L"--shot-yield") == 0)  o.yieldMs = nextI(i);
            else if (wcscmp(a, L"--shot-series") == 0 && i + 1 < argc) {
                int n = 0; float iv = 0;
                if (swscanf_s(argv[++i], L"%d:%f", &n, &iv) == 2 && n > 0) {
                    o.seriesCount = n; o.seriesInterval = iv;
                }
            }
            else if (wcscmp(a, L"--seed") == 0)       o.seed = (unsigned)nextI(i);
            else if (wcscmp(a, L"--hdr") == 0 && i + 1 < argc)
                o.hdrOn = (_wcsicmp(argv[++i], L"on") == 0);
            else if (wcscmp(a, L"--sdr-white") == 0)  o.sdrWhiteNits = nextF(i);
            else if (wcscmp(a, L"--panel-max") == 0)  o.panelMaxNits = nextF(i);
            else if (wcscmp(a, L"--timings") == 0)    o.timings = true;
            else if (wcscmp(a, L"--console") == 0)    { }
            // look overrides
            else if (wcscmp(a, L"--blobs") == 0)        cfg.blobs = nextI(i);
            else if (wcscmp(a, L"--steps") == 0)        cfg.steps = nextI(i);
            else if (wcscmp(a, L"--camera-drift") == 0) cfg.cameraDrift = nextF(i);
            else if (wcscmp(a, L"--half-res") == 0) {
                // "--half-res 0|1"; a bare --half-res (the ink3d spelling) means 1
                if (i + 1 < argc && (wcscmp(argv[i + 1], L"0") == 0 || wcscmp(argv[i + 1], L"1") == 0))
                    cfg.halfRes = (_wtoi(argv[++i]) != 0);
                else
                    cfg.halfRes = true;
            }
            else if (wcscmp(a, L"--debug-view") == 0)   cfg.debugView = nextI(i);
            else if (wcscmp(a, L"--shade") == 0 && i + 1 < argc)
                cfg.shade = (_wcsicmp(argv[++i], L"flat") == 0) ? OIL3D_FLAT : OIL3D_GLOSS;
            else
                Oil3DLog("[shot] WARNING: unknown argument ignored: %ls\n", a);
        }
        LocalFree(argv);
    }

    if (o.out.empty()) {
        Oil3DLog("[shot] ERROR: --shot needs an output .png path\n");
        return 2;
    }
    if (o.sdrWhiteNits < 1.0f) o.sdrWhiteNits = 80.0f;
    if (o.panelMaxNits > 0.0f) cfg.peakNits = o.panelMaxNits;
    FinalizeOil3DConfig(cfg);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        Oil3DLog("[shot] ERROR: CoInitializeEx failed\n");
        return 3;
    }

    const bool  hdrActive = o.hdrOn;
    const float sdrScale = hdrActive ? (o.sdrWhiteNits / 80.0f) : 1.0f;
    const OilKeys& a = cfg.oil;
    Oil3DLog("[shot] Oil3DWallpaper offscreen capture (prototype)\n");
    Oil3DLog("[shot] size=%dx%d seed=%u delay=%.2fs series=%d:%.2fs hdr=%s "
             "sdr_white=%.0f sdrScale=%.4f peak=%.0f\n",
             o.width, o.height, o.seed, o.delaySec, o.seriesCount, o.seriesInterval,
             hdrActive ? "on" : "off", o.sdrWhiteNits, sdrScale, cfg.peakNits);
    Oil3DLog("[shot] oil: blob_count=%d threshold=%.2f support=%.2f hole_weight=%.2f "
             "rise=%.4f respawn=%d turnover=%.0fs droplets=%d ink_mode=%s\n",
             a.blobCount, a.threshold, a.supportScale, a.holeWeight, a.riseSpeed,
             a.riseRespawn ? 1 : 0, a.blobTurnoverS, a.droplets,
             a.inkMode ? "water" : "bands");
    Oil3DLog("[shot] colour: sweep=%.0fs rotate=%.0fs anchor_w=%.2f start_hue=%.0f "
             "hue2=%.0f@%.2f hue3=%.0f@%.2f equal_load=%.2f film_level=%.2f dye_lum=%.2f "
             "tone=%.2f accent_mode=%d\n",
             a.hueSweepPeriod,
             cfg.hueRotateOverride > 0.0f ? cfg.hueRotateOverride : a.hueRotatePeriod,
             a.hueAnchorWeight, a.paletteStartHue, a.filmHue2, a.filmHue2Amt, a.filmHue3,
             a.filmHue3Amt, a.filmEqualLoad, a.filmLevel, a.dyeLum, a.shadowTone,
             a.accentMode);

    Oil3DRenderer renderer;
    renderer.InitOffscreen(o.width, o.height, cfg, o.seed);
    renderer.Sim().LogState("start");

    const double kHz = 60.0;              // fixed sim step on simulated time
    const float dt = (float)(1.0 / kHz);
    long long frames = 0, nextLog = 0, nextSim = (long long)(10 * kHz);
    const DWORD wallStart = GetTickCount();

    std::wstring stem = o.out;
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".png") == 0)
        stem.resize(stem.size() - 4);

    renderer.ResetGpuStats();
    std::vector<float> pixels;
    for (int s = 0; s < o.seriesCount; s++) {
        const double target = o.delaySec + (double)s * o.seriesInterval;
        const long long want = (long long)llround(target * kHz);
        while (frames < want || frames == 0) {
            renderer.Frame(dt, sdrScale);
            frames++;
            // Leave the GPU air: an unthrottled render starves the compositor
            // and Wallpaper Engine. Never run two shot processes together.
            if (o.yieldMs > 0) Sleep((DWORD)o.yieldMs);
            if (frames >= nextLog) {
                Oil3DLog("[shot] simulated %.2f s (%lld frames, %.1f s wall)\n",
                         frames / kHz, frames, (GetTickCount() - wallStart) / 1000.0);
                nextLog = frames + (long long)(kHz * 5);
            }
            if (frames >= nextSim) {
                renderer.Sim().LogState("step");
                nextSim = frames + (long long)(kHz * 10);
            }
        }
        if (!renderer.CaptureOffscreen(pixels)) {
            Oil3DLog("[shot] ERROR: readback failed\n");
            renderer.Shutdown();
            CoUninitialize();
            return 4;
        }
        std::wstring shotStem = stem;
        if (o.seriesCount > 1) {
            // DECISECONDS, so sub-second intervals still give distinct names
            wchar_t suffix[32];
            swprintf_s(suffix, L"-%03d", (int)llround(target * 10.0));
            shotStem += suffix;
        }
        WriteShotPair(shotStem, pixels, o.width, o.height, sdrScale, (float)(frames / kHz));
    }
    renderer.Sim().LogState("end");

    if (o.timings) {
        Oil3DLog("[timing] %d GPU frames, raymarch %dx%d steps=%d refine=%d half_res=%d\n",
                 renderer.GpuSamples(), renderer.RayW(), renderer.RayH(), cfg.steps,
                 cfg.refine, cfg.halfRes ? 1 : 0);
        for (int i = 0; i < renderer.PassCount(); i++)
            Oil3DLog("[timing] %-12s %7.3f ms mean  %7.3f ms min\n",
                     renderer.PassName(i), renderer.PassMsMean(i), renderer.PassMsMin(i));
        Oil3DLog("[timing] TOTAL %.3f ms mean\n", renderer.FrameMsMean());
    }

    Oil3DLog("[shot] done: %lld frames (%.2f s simulated) in %.1f s wall\n",
             frames, frames / kHz, (GetTickCount() - wallStart) / 1000.0);
    renderer.Shutdown();
    CoUninitialize();
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Attach to the launching console when there is one; never CREATE a
    // window. Unlike src_ink3d, --console does NOT AllocConsole: a new console
    // window pops up and takes focus, and the user games (brief hard
    // constraint). Here it only raises the log verbosity.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LogInit();

    if (!HasFlag(L"--shot")) {
        Oil3DLog("Oil3DWallpaper: this build is HEADLESS (prototype). The desktop attach "
                 "comes after the look is OKed. Run with --shot <out.png>.\n");
        if (g_log) fclose(g_log);
        return 0;
    }
    int rc = wmain_shot();
    if (g_log) fclose(g_log);
    return rc;
}
