// Ink3DWallpaper — app shell, milestones M0-M2.
//
// HEADLESS ONLY. This build has no wallpaper window, no WorkerW attach, no
// tray and no swap chain: the desktop attach is M4 and the user gates it on
// M2's look and timings. Launching without --shot logs a line and exits, so
// the exe can never disturb a running FluidWallpaper.exe or Wallpaper Engine.
//
// WIN32 subsystem (like the other standalone targets) so no console window is
// ever created; it attaches to the launching console when there is one, and
// always mirrors to %TEMP%\Ink3DWallpaper-shot.log.
//
// Flags:
//   --shot <out.png>      render OFFSCREEN, write <out>.png + <out>-hdr.png
//   --shot-size WxH       capture size (default 2560x1440)
//   --shot-delay S        seconds of simulated time before the first capture
//   --shot-series N:S     N captures, S seconds apart, named by elapsed seconds
//   --shot-yield MS       Sleep per simulated frame (default 2) — leaves the
//                         GPU air so the user's desktop stays responsive
//   --shot-drop X,Y,Z,T,R,VX,VY,VZ   repeatable; box-normalized position,
//                         seconds, velocity voxels, voxel/s (+Y is DOWN)
//   --seed N              every random value routes through this (default 1234)
//   --ini <path>          config file, section [ink3d]
//   --hdr on|off  --sdr-white N  --panel-max N
//   --static-scene        M0: analytic sphere + tilted sheet, solver off
//   --stats               divergence before/after + multigrid residual
//   --timings             per-pass GPU timing table (timestamp queries)
//   --console             also open a console (never used by the shot scripts)
// Look overrides for the M2 sweeps (each also exists as an ini key):
//   --tier --render-mode --steps --half-res --advect --pressure --mg-cycles
//   --vorticity --buoyancy --sharpen --dissipation --ink-density --scatter
//   --ambient-noise --drop-radius

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

#include "ink3d.h"
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
        _snwprintf_s(path, _TRUNCATE, L"%sInk3DWallpaper-shot.log", tmp);
        g_log = _wfsopen(path, L"a", _SH_DENYNO);
    }
}

void Ink3DLog(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
}

void Ink3DFail(const char* what, HRESULT hr) {
    Ink3DLog("FATAL: %s failed (hr=0x%08lX)\n", what, (unsigned long)hr);
    if (g_log) fclose(g_log);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// PNG output — copied from src/main.cpp:1233-1320 @873a6dc
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
    size_t aboveWhite = 0, negative = 0;
    float maxScrgb = 0.0f;

    for (size_t p = 0; p < n; p++) {
        float r = rgba[p * 4 + 0], g = rgba[p * 4 + 1], b = rgba[p * 4 + 2];
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
    Ink3DLog("[shot] t=%.2fs %ls %dx%d  mean_lum=%.4f scRGB (%.1f nits)  "
             "above_sdr_white=%.3f%%  max_scRGB=%.3f (%.0f nits)  negative_px=%.2f%%  "
             "sdr=%s hdr=%s\n",
             elapsed, sdrPath.c_str(), w, h, meanLum, meanLum * 80.0,
             100.0 * aboveWhite / (double)n, maxScrgb, maxScrgb * 80.0f,
             100.0 * negative / (double)n, okS ? "ok" : "FAILED", okH ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------

struct ShotOpts {
    std::wstring out, ini;
    int   width = 2560, height = 1440;
    float delaySec = 6.0f;
    int   seriesCount = 1;
    float seriesInterval = 0.0f;
    unsigned seed = 1234;
    bool  hdrOn = false;
    float sdrWhiteNits = 240.0f;
    float panelMaxNits = 1000.0f;
    int   yieldMs = 2;
    bool  staticScene = false;
    bool  stats = false;
    bool  timings = false;
};

static bool HasFlag(const wchar_t* f) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; i < argc && !found; i++) if (wcscmp(argv[i], f) == 0) found = true;
    LocalFree(argv);
    return found;
}

int wmain_shot() {
    ShotOpts o;
    Ink3DConfig cfg;
    std::vector<Ink3DDrop> drops;
    std::vector<float>     dropTimes;

    // --- pass 1: --ini and --tier, so explicit flags below can override them
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"--ini") == 0 && i + 1 < argc) o.ini = argv[++i];
            else if (wcscmp(argv[i], L"--tier") == 0 && i + 1 < argc) {
                char t[64];
                WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, t, 64, nullptr, nullptr);
                ApplyInk3DTier(cfg, t);
            }
        }
        LocalFree(argv);
    }
    if (!o.ini.empty()) {
        if (!LoadInk3DConfig(o.ini.c_str(), cfg)) {
            Ink3DLog("[shot] ERROR: --ini file not found or unreadable: %ls\n", o.ini.c_str());
            return 2;
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
            if      (wcscmp(a, L"--shot") == 0 && i + 1 < argc) o.out = argv[++i];
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
            else if (wcscmp(a, L"--shot-drop") == 0 && i + 1 < argc) {
                Ink3DDrop d; float t = 0;
                if (swscanf_s(argv[++i], L"%f,%f,%f,%f,%f,%f,%f,%f",
                              &d.nx, &d.ny, &d.nz, &t, &d.radius,
                              &d.vx, &d.vy, &d.vz) == 8) {
                    d.amount = cfg.dropAmount;
                    drops.push_back(d);
                    dropTimes.push_back(t);
                } else {
                    Ink3DLog("[shot] ERROR: --shot-drop needs X,Y,Z,T,R,VX,VY,VZ\n");
                    return 2;
                }
            }
            else if (wcscmp(a, L"--seed") == 0)       o.seed = (unsigned)nextI(i);
            else if (wcscmp(a, L"--hdr") == 0 && i + 1 < argc)
                o.hdrOn = (_wcsicmp(argv[++i], L"on") == 0);
            else if (wcscmp(a, L"--sdr-white") == 0)  o.sdrWhiteNits = nextF(i);
            else if (wcscmp(a, L"--panel-max") == 0)  o.panelMaxNits = nextF(i);
            else if (wcscmp(a, L"--static-scene") == 0) o.staticScene = true;
            else if (wcscmp(a, L"--stats") == 0)      o.stats = true;
            else if (wcscmp(a, L"--timings") == 0)    o.timings = true;
            // look overrides
            else if (wcscmp(a, L"--render-mode") == 0 && i + 1 < argc) {
                const wchar_t* m = argv[++i];
                cfg.renderMode = (_wcsicmp(m, L"paper") == 0)   ? INK3D_PAPER
                               : (_wcsicmp(m, L"backlit") == 0) ? INK3D_BACKLIT
                                                                : INK3D_NEGATIVE;
            }
            else if (wcscmp(a, L"--steps") == 0)      cfg.steps = nextI(i);
            else if (wcscmp(a, L"--half-res") == 0)   cfg.halfRes = true;
            else if (wcscmp(a, L"--advect") == 0 && i + 1 < argc)
                cfg.maccormack = (_wcsicmp(argv[++i], L"sl") != 0);
            else if (wcscmp(a, L"--pressure") == 0 && i + 1 < argc)
                cfg.pressureSolver = (_wcsicmp(argv[++i], L"jacobi") == 0)
                                   ? INK3D_JACOBI : INK3D_MG;
            else if (wcscmp(a, L"--mg-cycles") == 0)     cfg.mgCycles = nextI(i);
            else if (wcscmp(a, L"--pressure-iters") == 0) cfg.pressureIterations = nextI(i);
            else if (wcscmp(a, L"--vorticity") == 0)     cfg.vorticity = nextF(i);
            else if (wcscmp(a, L"--buoyancy") == 0)      cfg.buoyancy = nextF(i);
            else if (wcscmp(a, L"--sharpen") == 0)       cfg.sharpen = nextF(i);
            else if (wcscmp(a, L"--dissipation") == 0)   cfg.dissipation = nextF(i);
            else if (wcscmp(a, L"--ink-density") == 0)   cfg.inkDensity = nextF(i);
            else if (wcscmp(a, L"--scatter") == 0)       cfg.scatter = nextF(i);
            else if (wcscmp(a, L"--ambient-noise") == 0) cfg.ambientNoise = nextF(i);
            else if (wcscmp(a, L"--drop-radius") == 0)   cfg.dropRadius = nextF(i);
            else if (wcscmp(a, L"--grid") == 0 && i + 1 < argc) {
                int gx = 0, gy = 0, gz = 0;
                if (swscanf_s(argv[++i], L"%dx%dx%d", &gx, &gy, &gz) == 3) {
                    cfg.gridX = gx; cfg.gridY = gy; cfg.gridZ = gz;
                }
            }
        }
        LocalFree(argv);
    }

    if (o.out.empty()) {
        Ink3DLog("[shot] ERROR: --shot needs an output .png path\n");
        return 2;
    }
    if (o.sdrWhiteNits < 1.0f) o.sdrWhiteNits = 80.0f;
    cfg.idleDrops = false;          // any --shot-drop turns the scheduler off,
                                    // and M5 is where the scheduler lands
    cfg.jitterAnim = false;         // shot mode must be byte-deterministic

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        Ink3DLog("[shot] ERROR: CoInitializeEx failed\n");
        return 3;
    }

    const bool  hdrActive = o.hdrOn;
    const float sdrScale = hdrActive ? (o.sdrWhiteNits / 80.0f) : 1.0f;
    Ink3DLog("[shot] Ink3DWallpaper offscreen capture (M0-M2 build)\n");
    Ink3DLog("[shot] size=%dx%d seed=%u delay=%.2fs series=%d:%.2fs hdr=%s "
             "sdr_white=%.0f sdrScale=%.4f peak=%.0f\n",
             o.width, o.height, o.seed, o.delaySec, o.seriesCount, o.seriesInterval,
             hdrActive ? "on" : "off", o.sdrWhiteNits, sdrScale, cfg.peakNits);
    Ink3DLog("[shot] mode=%s advect=%s pressure=%s(cycles=%d,iters=%d) vort=%.2f "
             "buoy=%.2f noise=%.3f sharpen=%.3f diss=%.4f ink_density=%.1f "
             "scatter=%.2f steps=%d half_res=%d drops=%d\n",
             cfg.renderMode == INK3D_PAPER ? "paper" :
             cfg.renderMode == INK3D_BACKLIT ? "backlit" : "negative",
             cfg.maccormack ? "maccormack" : "sl",
             cfg.pressureSolver == INK3D_MG ? "mg" : "jacobi",
             cfg.mgCycles, cfg.pressureIterations, cfg.vorticity, cfg.buoyancy,
             cfg.ambientNoise, cfg.sharpen, cfg.dissipation, cfg.inkDensity,
             cfg.scatter, cfg.steps, cfg.halfRes ? 1 : 0, (int)drops.size());

    Ink3DRenderer renderer;
    renderer.SetSeed(o.seed);
    renderer.InitOffscreen(o.width, o.height, cfg);
    if (o.staticScene) renderer.FillStaticScene();

    const float dt = 1.0f / 144.0f;
    long long frames = 0, nextLog = 0;
    size_t nextDrop = 0;
    const DWORD wallStart = GetTickCount();

    std::wstring stem = o.out;
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".png") == 0)
        stem.resize(stem.size() - 4);

    // warm-up so the timing table does not average in shader/PSO first-use cost
    renderer.ResetGpuStats();

    std::vector<float> pixels;
    for (int s = 0; s < o.seriesCount; s++) {
        const double target = o.delaySec + (double)s * o.seriesInterval;
        const long long want = (long long)llround(target * 144.0);
        while (frames < want) {
            const float t = (float)(frames / 144.0);
            while (nextDrop < drops.size() && dropTimes[nextDrop] <= t) {
                renderer.QueueDrop(drops[nextDrop]);
                Ink3DLog("[shot] drop %d at t=%.3f  pos=(%.2f,%.2f,%.2f) r=%.1f "
                         "v=(%.0f,%.0f,%.0f)\n", (int)nextDrop, t,
                         drops[nextDrop].nx, drops[nextDrop].ny, drops[nextDrop].nz,
                         drops[nextDrop].radius, drops[nextDrop].vx, drops[nextDrop].vy,
                         drops[nextDrop].vz);
                nextDrop++;
            }
            renderer.Frame(dt, sdrScale, !o.staticScene);
            frames++;
            // Leave the GPU air: an unthrottled sim starves the compositor and
            // Wallpaper Engine (the OLED went grey once when two of these ran
            // at once). Never run two shot processes together — use the lock.
            if (o.yieldMs > 0) Sleep((DWORD)o.yieldMs);
            if (frames >= nextLog) {
                Ink3DLog("[shot] simulated %.2f s (%lld frames, %.1f s wall)\n",
                         frames / 144.0, frames, (GetTickCount() - wallStart) / 1000.0);
                nextLog = frames + 144 * 2;
            }
        }
        if (o.staticScene && frames == 0) {
            renderer.Frame(dt, sdrScale, false);   // one render of the static scene
        }
        if (!renderer.CaptureOffscreen(pixels)) {
            Ink3DLog("[shot] ERROR: readback failed\n");
            renderer.Shutdown();
            CoUninitialize();
            return 4;
        }
        std::wstring shotStem = stem;
        if (o.seriesCount > 1) {
            // DECISECONDS, so sub-second intervals still give distinct names:
            // t = 0.6 s -> "-006", t = 4.0 s -> "-040", t = 9.0 s -> "-090".
            wchar_t suffix[32];
            swprintf_s(suffix, L"-%03d", (int)llround(target * 10.0));
            shotStem += suffix;
        }
        WriteShotPair(shotStem, pixels, o.width, o.height, sdrScale,
                      (float)(frames / 144.0));
    }

    if (o.stats) {
        Ink3DStats st = renderer.MeasureStats();
        Ink3DLog("[stats] mean|div| before=%.6g  after=%.6g  (%.2f%% of before)\n",
                 st.divBefore, st.divAfter,
                 st.divBefore > 0 ? 100.0 * st.divAfter / st.divBefore : 0.0);
        Ink3DLog("[stats] mean|residual| after the solve=%.6g  relative=%.4g  "
                 "(solver=%s, smoother=%s, levels=%d)\n",
                 st.residual, st.rhsNorm > 0 ? st.residual / st.rhsNorm : 0.0,
                 cfg.pressureSolver == INK3D_MG ? "mg" : "jacobi",
                 renderer.UsingRedBlack() ? "red-black GS" : "damped Jacobi",
                 renderer.MgLevels());
        Ink3DLog("[stats] density mean=%.6g max=%.4f\n", st.densityMean, st.densityMax);
    }

    if (o.timings) {
        Ink3DLog("[timing] %d GPU frames, vel %dx%dx%d density %dx%dx%d "
                 "raymarch %dx%d steps=%d\n",
                 renderer.GpuSamples(), renderer.VelW(), renderer.VelH(), renderer.VelD(),
                 renderer.DenW(), renderer.DenH(), renderer.DenD(),
                 renderer.RayW(), renderer.RayH(), cfg.steps);
        double simMs = 0.0, renderMs = 0.0;
        for (int i = 0; i < renderer.PassCount(); i++) {
            const char* n = renderer.PassName(i);
            double mean = renderer.PassMsMean(i);
            Ink3DLog("[timing] %-12s %7.3f ms mean  %7.3f ms min\n",
                     n, mean, renderer.PassMsMin(i));
            if (strcmp(n, "raymarch") == 0 || strcmp(n, "display") == 0 ||
                strcmp(n, "light") == 0 || strcmp(n, "occupancy") == 0)
                renderMs += mean;
            else simMs += mean;
        }
        Ink3DLog("[timing] TOTAL %.3f ms mean  (sim %.3f + render %.3f)\n",
                 renderer.FrameMsMean(), simMs, renderMs);
    }

    Ink3DLog("[shot] done: %lld frames (%.2f s simulated) in %.1f s wall\n",
             frames, frames / 144.0, (GetTickCount() - wallStart) / 1000.0);
    renderer.Shutdown();
    CoUninitialize();
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Attach to the launching console when there is one; never CREATE a window.
    if (HasFlag(L"--console")) {
        AllocConsole();
        FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LogInit();

    if (!HasFlag(L"--shot")) {
        Ink3DLog("Ink3DWallpaper: this build is HEADLESS (milestones M0-M2). "
                 "The desktop attach, tray and idle drops are M4/M5. "
                 "Run with --shot <out.png>.\n");
        if (g_log) fclose(g_log);
        return 0;
    }
    int rc = wmain_shot();
    if (g_log) fclose(g_log);
    return rc;
}
