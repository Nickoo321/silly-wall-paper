#include "config.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ini helpers. Partial overlay: a key that is not in the file leaves the
// field alone (src/main.cpp:121-190 convention, copied @873a6dc).
static const wchar_t* kSec = L"ink3d";

static bool GetStr(const wchar_t* sec, const wchar_t* key, const wchar_t* path,
                   wchar_t* out, DWORD n) {
    out[0] = 0;
    GetPrivateProfileStringW(sec, key, L"\x01", out, n, path);
    return out[0] != L'\x01';
}
static void GetF(const wchar_t* key, const wchar_t* path, float& v,
                 const wchar_t* sec = kSec) {
    wchar_t buf[128];
    if (GetStr(sec, key, path, buf, 128) && buf[0]) v = (float)_wtof(buf);
}
static void GetI(const wchar_t* key, const wchar_t* path, int& v,
                 const wchar_t* sec = kSec) {
    wchar_t buf[128];
    if (GetStr(sec, key, path, buf, 128) && buf[0]) v = _wtoi(buf);
}
static void GetB(const wchar_t* key, const wchar_t* path, bool& v,
                 const wchar_t* sec = kSec) {
    wchar_t buf[128];
    if (GetStr(sec, key, path, buf, 128) && buf[0]) v = _wtoi(buf) != 0;
}
static void GetRgb(const wchar_t* key, const wchar_t* path, float v[3]) {
    wchar_t buf[128];
    if (!GetStr(kSec, key, path, buf, 128) || !buf[0]) return;
    float a = v[0], b = v[1], c = v[2];
    if (swscanf_s(buf, L"%f,%f,%f", &a, &b, &c) == 3) { v[0] = a; v[1] = b; v[2] = c; }
}

void ApplyInk3DTier(Ink3DConfig& cfg, const char* tier) {
    if (!tier || !*tier) return;
    if (_stricmp(tier, "quality") == 0) {
        cfg.gridX = 256; cfg.gridY = 144; cfg.gridZ = 128;
        cfg.steps = 160; cfg.mgCycles = 3;
    } else if (_stricmp(tier, "fast") == 0) {
        // density depth is 88 voxels at this tier, so 96 steps keeps the
        // "step <= 1 density voxel" rule with a little margin
        cfg.gridX = 128; cfg.gridY = 72; cfg.gridZ = 64;
        cfg.steps = 96;  cfg.mgCycles = 2;
    } else if (_stricmp(tier, "gaming") == 0) {
        cfg.gridX = 96;  cfg.gridY = 56; cfg.gridZ = 48;
        cfg.steps = 64;  cfg.mgCycles = 1; cfg.halfRes = true;
    } else {   // balanced
        cfg.gridX = 192; cfg.gridY = 108; cfg.gridZ = 96;
        cfg.steps = 128; cfg.mgCycles = 2;
    }
}

bool LoadInk3DConfig(const wchar_t* iniPath, Ink3DConfig& cfg) {
    if (!iniPath || !*iniPath) return false;
    if (GetFileAttributesW(iniPath) == INVALID_FILE_ATTRIBUTES) return false;

    // tier first: an explicit grid_* below still overrides it
    {
        wchar_t buf[64];
        if (GetStr(kSec, L"tier", iniPath, buf, 64) && buf[0]) {
            char t[64];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, t, 64, nullptr, nullptr);
            ApplyInk3DTier(cfg, t);
        }
    }

    GetI(L"grid_x", iniPath, cfg.gridX);
    GetI(L"grid_y", iniPath, cfg.gridY);
    GetI(L"grid_z", iniPath, cfg.gridZ);
    GetF(L"density_scale", iniPath, cfg.densityScale);

    {
        wchar_t buf[64];
        if (GetStr(kSec, L"advect", iniPath, buf, 64) && buf[0])
            cfg.maccormack = (_wcsicmp(buf, L"sl") != 0);
        if (GetStr(kSec, L"pressure_solver", iniPath, buf, 64) && buf[0])
            cfg.pressureSolver = (_wcsicmp(buf, L"jacobi") == 0) ? INK3D_JACOBI : INK3D_MG;
        if (GetStr(kSec, L"render_mode", iniPath, buf, 64) && buf[0]) {
            if (_wcsicmp(buf, L"paper") == 0)        cfg.renderMode = INK3D_PAPER;
            else if (_wcsicmp(buf, L"backlit") == 0) cfg.renderMode = INK3D_BACKLIT;
            else                                     cfg.renderMode = INK3D_NEGATIVE;
        }
    }

    GetI(L"mg_cycles", iniPath, cfg.mgCycles);
    GetI(L"mg_pre_smooth", iniPath, cfg.mgPreSmooth);
    GetI(L"mg_post_smooth", iniPath, cfg.mgPostSmooth);
    GetI(L"mg_coarse_sweeps", iniPath, cfg.mgCoarseSweeps);
    GetI(L"pressure_iterations", iniPath, cfg.pressureIterations);
    GetF(L"vorticity", iniPath, cfg.vorticity);
    GetF(L"buoyancy", iniPath, cfg.buoyancy);
    GetF(L"ambient_noise", iniPath, cfg.ambientNoise);
    GetF(L"dissipation", iniPath, cfg.dissipation);
    GetF(L"dissipation_fast", iniPath, cfg.dissipationFast);
    GetF(L"decay_threshold", iniPath, cfg.decayThreshold);
    GetF(L"vel_dissipation", iniPath, cfg.velDissipation);
    GetF(L"sharpen", iniPath, cfg.sharpen);
    GetF(L"sink_bottom", iniPath, cfg.sinkBottom);
    GetF(L"sink_rate", iniPath, cfg.sinkRate);
    GetF(L"density_cap", iniPath, cfg.densityCap);

    GetF(L"drop_radius", iniPath, cfg.dropRadius);
    GetF(L"drop_speed", iniPath, cfg.dropSpeed);
    GetF(L"drop_speed_jitter", iniPath, cfg.dropSpeedJitter);
    GetF(L"drop_amount", iniPath, cfg.dropAmount);
    GetF(L"drop_edge_noise", iniPath, cfg.dropEdgeNoise);
    GetB(L"idle_drops", iniPath, cfg.idleDrops);
    GetF(L"drop_interval_min", iniPath, cfg.dropIntervalMin);
    GetF(L"drop_interval_max", iniPath, cfg.dropIntervalMax);
    GetF(L"splash_chance", iniPath, cfg.splashChance);

    GetI(L"steps", iniPath, cfg.steps);
    GetB(L"half_res", iniPath, cfg.halfRes);
    GetB(L"jitter_anim", iniPath, cfg.jitterAnim);
    GetF(L"fov", iniPath, cfg.fovDeg);
    GetF(L"ink_density", iniPath, cfg.inkDensity);
    GetRgb(L"absorb_rgb", iniPath, cfg.absorbRgb);
    GetRgb(L"albedo_rgb", iniPath, cfg.albedoRgb);
    GetF(L"scatter", iniPath, cfg.scatter);
    GetRgb(L"light_dir", iniPath, cfg.lightDir);
    GetB(L"gradient_light", iniPath, cfg.gradientLight);
    GetRgb(L"paper_rgb", iniPath, cfg.paperRgb);
    GetF(L"paper_level", iniPath, cfg.paperLevel);
    GetRgb(L"ink_rgb", iniPath, cfg.inkRgb);
    GetF(L"ink_level", iniPath, cfg.inkLevel);
    GetF(L"scatter_mix", iniPath, cfg.scatterMix);
    GetF(L"hue", iniPath, cfg.hue);

    GetI(L"fps_limit", iniPath, cfg.fpsLimit, L"general");
    GetB(L"pause_on_fullscreen", iniPath, cfg.pauseOnFullscreen, L"general");
    GetB(L"pause_on_maximized", iniPath, cfg.pauseOnMaximized, L"general");
    GetF(L"peak_nits", iniPath, cfg.peakNits, L"hdr");
    GetF(L"knee", iniPath, cfg.knee, L"hdr");
    GetI(L"gamut", iniPath, cfg.gamut, L"hdr");

    // OLED guard rails (INK3D-PLAN §3): a full-white desktop trips ABL.
    if (cfg.paperLevel > 0.8f) cfg.paperLevel = 0.8f;
    if (cfg.gridX < 16) cfg.gridX = 16;
    if (cfg.gridY < 16) cfg.gridY = 16;
    if (cfg.gridZ < 16) cfg.gridZ = 16;
    if (cfg.steps < 8)  cfg.steps = 8;
    return true;
}
