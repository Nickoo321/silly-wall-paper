// Ink3DWallpaper — configuration (INK3D-PLAN.md §4, ini section [ink3d]).
//
// Every constant the sim and the raymarcher read is a named field here, so
// the later settings/tray layer only has to bind names to fields (the
// LiquidAcidConfig convention from src/fluid.h).
//
// UNITS (these matter — the plan left two of them implicit):
//   * velocity is in VELOCITY-GRID VOXELS per second. A drop at 80 voxel/s
//     with dt = 1/144 moves 0.56 voxels per step (CFL < 1, no substepping).
//   * the render box is NORMALIZED: x in [0,1], y in [0,0.5625], z in [0,0.5]
//     (16:9:8). `inkDensity` is therefore an absorption coefficient per unit
//     of that box, NOT per voxel — see the note on the field.
#pragma once

#include <string>

enum Ink3DRenderMode { INK3D_NEGATIVE = 0, INK3D_PAPER = 1, INK3D_BACKLIT = 2 };
enum Ink3DPressure   { INK3D_MG = 0, INK3D_JACOBI = 1 };

struct Ink3DConfig {
    // --- grid ------------------------------------------------------------
    int   gridX = 192, gridY = 108, gridZ = 96;   // velocity grid (exact 16:9:8)
    float densityScale = 1.3333f;                 // density grid = this x velocity

    // --- solver ----------------------------------------------------------
    bool  maccormack        = true;    // advect = maccormack | sl
    int   pressureSolver    = INK3D_MG;
    int   mgCycles          = 2;
    int   mgPreSmooth       = 2;       // V(2,2)
    int   mgPostSmooth      = 2;
    int   mgCoarseSweeps    = 16;
    int   pressureIterations = 24;     // jacobi only
    float jacobiOmega       = 1.0f;    // 1.0 = plain Jacobi on the finest level
    float smootherOmega     = 0.857f;  // 6/7: optimal damped-Jacobi MG smoother
                                       // (only used when RB Gauss-Seidel is
                                       //  unavailable — see ink3d.cpp)
    float vorticity         = 4.0f;    // confinement eps, grid units
    float buoyancy          = 12.0f;   // voxel/s^2 per unit density, +y = DOWN
    float ambientNoise      = 0.02f;   // curl-noise drift, voxel/s^2
    // DECAYS ARE PER FRAME AT 144 fps (raised to dt*144, so the ini value is
    // literally the per-144-fps-frame factor). The plan's 0.998 / 0.98 were
    // quoted as if they were per SECOND: 0.998^144 = 0.75 per second, a 2.4 s
    // half-life, and 0.98^144 = 0.05 — measured, half the ink was gone two
    // seconds after a drop with no flow at all, which is what reduced the M1
    // plume to a bare core. These give the plan's stated intent instead:
    // ink lasts ~60 s, faint ink clears in ~10 s.
    float dissipation       = 0.9999f;   // 0.9857/s -> half gone at ~48 s
    float dissipationFast   = 0.999f;    // 0.866/s  -> faint ink clears ~10 s
    float decayThreshold    = 0.02f;
    float velDissipation    = 0.99995f;  // 0.9928/s
    float sharpen           = 0.08f;     // 0 disables the pass
    float sinkBottom        = 0.06f;     // bottom fraction that drains
    float sinkRate          = 0.995f;    // 0.487/s in that band
    float densityCap        = 1.0f;

    // --- drops -----------------------------------------------------------
    float dropRadius      = 4.0f;      // velocity voxels
    float dropSpeed       = 80.0f;     // voxel/s downward
    float dropSpeedJitter = 0.25f;
    float dropAmount      = 1.0f;
    float dropEdgeNoise   = 0.05f;     // radial noise on the density edge
    bool  idleDrops       = true;      // M5; the shot path forces it off
    float dropIntervalMin = 8.0f, dropIntervalMax = 20.0f;
    float splashChance    = 0.25f;

    // --- render ----------------------------------------------------------
    int   renderMode = INK3D_NEGATIVE;
    // 128, not the plan's 96: the density grid is 128 voxels deep and the box
    // depth is 0.5, so 128 steps makes the march step exactly one density
    // voxel along an axial ray. 96 steps is 1.33 voxels — measurably cheaper
    // (0.38 vs 0.50 ms at 1440p) and it looked fine on these scenes, but "step
    // <= 1 voxel" is worth 0.12 ms on a 4 ms budget.
    int   steps      = 128;
    bool  halfRes    = false;
    bool  jitterAnim = false;          // shot mode always forces this off
    float fovDeg     = 25.0f;
    // Absorption per unit path through the NORMALIZED box (depth = 0.5). The
    // plan's 6.0 assumed an unstated length unit; at box scale a dense core
    // needs sigma*rho*path ~ 3, i.e. ~90. Documented deviation.
    float inkDensity = 90.0f;
    float absorbRgb[3] = { 1.00f, 0.88f, 0.62f };   // blue-black ink
    float albedoRgb[3] = { 0.05f, 0.08f, 0.16f };
    float scatter      = 1.0f;
    // Light TRAVEL direction in render space (+y is DOWN the screen, +z is
    // away from the camera). Default: from behind the box, toward the viewer,
    // tilted so it also travels toward the top of the frame.
    float lightDir[3]  = { 0.0f, -0.34f, -0.94f };
    bool  gradientLight = false;
    float paperRgb[3]  = { 1.0f, 1.0f, 1.0f };
    float paperLevel   = 0.65f;        // capped at 0.8 (ABL)
    float inkRgb[3]    = { 0.72f, 0.80f, 0.95f };
    float inkLevel     = 0.6f;
    float scatterMix   = 1.0f;         // "S*k" in negative mode
    float hue          = 0.0f;

    // --- shell / hdr ------------------------------------------------------
    int   fpsLimit   = 144;
    bool  pauseOnFullscreen = true;
    bool  pauseOnMaximized  = true;
    float peakNits   = 700.0f;
    float knee       = 0.70f;
    int   gamut      = 0;
};

// Partial-overlay ini read (the 2D app's convention: keys absent from the
// file keep the value already in `cfg`). Returns false if the file is missing.
bool LoadInk3DConfig(const wchar_t* iniPath, Ink3DConfig& cfg);

// tier = balanced | quality | fast | gaming. Applied BEFORE the ini so an
// explicit grid_x/y/z in the file still wins.
void ApplyInk3DTier(Ink3DConfig& cfg, const char* tier);
