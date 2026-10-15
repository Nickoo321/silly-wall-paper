// Oil3DWallpaper — configuration (reference/briefs/NEXT-oil3d.md).
//
// The prototype reads the REAL 2D oil presets: every key of [liquid_acid]
// that has a 3D meaning lands in OilKeys under the same name and with the
// same default as src/fluid.h LiquidAcidConfig (read-only reference, copied
// @8a4f185), so base + layout + scheme stack exactly as they do in
// FluidWallpaper. Keys with no 3D meaning are ignored and, at --console
// verbosity, listed once. [oil3d] holds the 3D-only keys.
//
// UNITS. The sim works in "screen-projected" coordinates, the 2D look's own:
//   X in [0, aspect] (2D uv.x * aspect), Y in [0, 1] (2D uv.y, +Y is DOWN),
//   Z in [0, depth] (0 = the front plane of the slab, depth = the far wall).
// A radius is in screen-height units, exactly the 2D disc/web/bubble/hole
// ranges, so a Layout or a Scheme file transfers without a conversion.
// The renderer maps a sim position to world space through the camera
// frustum at that depth (oil3d.cpp, SimToWorld), so a blob's screen position
// is its sim (X, Y) whatever its Z: re-entry from below and the off-screen
// tests are the 2D ones, and depth only changes its size and parallax.
#pragma once

#include <string>
#include <vector>

enum Oil3DShade { OIL3D_FLAT = 0, OIL3D_GLOSS = 1 };

// [liquid_acid] — the subset the 3D look reads. Names in the trailing
// comments are the ini keys (src/main.cpp:558-858).
struct OilKeys {
    // --- population / sizes (uv-y units) ---
    int   blobCount   = 96;                         // blob_count (3D caps at 64)
    float discFrac    = 0.22f, webFrac = 0.42f, bubbleFrac = 0.22f;
    float discMin     = 0.250f, discMax   = 0.460f;
    float webMin      = 0.110f, webMax    = 0.260f;
    float bubbleMin   = 0.015f, bubbleMax = 0.075f;
    float holeMin     = 0.030f, holeMax   = 0.130f;
    float sizeBias    = 2.0f, bigBias = 0.55f;
    float holeWeight  = 0.75f;                      // hole_weight
    // --- field ---
    float threshold   = 0.50f;                      // threshold
    float supportScale= 2.20f;                      // support_scale
    // --- motion ---
    float flowGain    = 1.15f, curlDrift = 0.0016f, repulsion = 0.55f;
    float buoyancy    = 0.0020f, damping = 2.2f, breathAmt = 0.10f;   // breath
    float wrapMargin  = 0.20f;
    float riseSpeed   = 0.0f, riseWobble = 0.0f;
    bool  riseRespawn = false;
    float riseStretch = 0.0f, riseBottomLight = 0.0f;
    float riseParallax = 0.0f, riseParallaxDim = 0.0f;
    float oilViscosity = 0.0f;
    float conserveMass = 0.0f, spawnGrowS = 0.0f, dissolveS = 0.0f;
    float blobTurnoverS = 0.0f;
    // --- palette ---
    float oilColors[12] = { 0.902f, 0.278f, 0.157f,  0.976f, 0.400f, 0.078f,
                            0.859f, 0.204f, 0.098f,  0.988f, 0.541f, 0.114f };
    float inkRamp[12]   = { 0.006f, 0.034f, 0.038f,  0.043f, 0.353f, 0.376f,
                            0.478f, 0.173f, 0.020f,  0.969f, 0.510f, 0.055f };
    float meniscusCol[3] = { 0.353f, 0.918f, 0.894f };
    float hueSweepPeriod = 0.0f;
    static const int kSweepPairs = 5, kSweepMax = 12;
    int   sweepCount = kSweepPairs;
    float hueRotatePeriod = 0.0f, hueAnchorWeight = 0.0f;
    float paletteStartHue = -1.0f;                  // -1 off, -2 random (from --seed)
    float oilSaturation = 1.0f;
    float sweepOil[kSweepMax * 3] = {
        0.898f, 0.271f, 0.145f,  0.930f, 0.120f, 0.160f,  0.880f, 0.120f, 0.620f,
        0.970f, 0.780f, 0.100f,  0.550f, 0.850f, 0.120f };
    float sweepInk[kSweepMax * 3] = {
        0.086f, 0.478f, 0.494f,  0.100f, 0.620f, 0.500f,  0.220f, 0.700f, 0.240f,
        0.340f, 0.160f, 0.720f,  0.480f, 0.120f, 0.660f };
    bool  sweepOilFullSet[kSweepMax] = {};
    float sweepOilFull[kSweepMax * 12] = {};
    int   accentMode = 0;  float accentMaxR = 0.35f, accentFrac = 0.60f;
    // --- film hues (brief BV schemes) ---
    float filmHue2 = 0.0f, filmHue2Amt = 0.0f, filmHue2Scale = 0.35f;
    float filmHue2Drift = 1.0f, filmHue2Rise = 0.25f;
    float filmHue2Wobble = 0.0f, filmHue2WobbleP = 300.0f;
    float filmHue2Cover = 0.0f, filmHue2Seam = 1.0f;
    float filmHue3 = 0.0f, filmHue3Amt = 0.0f, filmHue3Share = 1.0f;
    float filmEqualLoad = 0.0f, filmEqualLoadP = 0.0f;
    float crustHueMix = 1.0f;
    float filmLevel = 1.0f;
    float massRim = 0.0f;                           // 3D: scales the gloss rim
    // --- dye (masses = backdrop + cavities; droplets = beads) ---
    float dyeHue = 0.0f, dyeSat = 0.0f, dyeLum = 0.0f;
    int   dyeHueFollow = 0;
    float dyeDroplets = 1.0f, dyeMasses = 1.0f;
    float dyeDropHue = -1.0f, dyeDropSat = -1.0f, dyeDropLum = -1.0f;
    float dyeCore = 0.42f;
    // --- split tone (black role) ---
    float shadowTone = 0.0f, shadowToneLift = 0.06f, shadowToneSat = 0.80f;
    float shadowTonePeriod = 2225.0f, shadowToneFade = 120.0f, shadowToneHue = -1.0f;
    // --- ink ---
    int   inkMode = 0;                              // ink_mode bands|water
    float inkLevels = 5.0f, inkSoft = 0.42f;
    // --- droplets (approximated: see oil3d.cpp) ---
    int   droplets = 0;
    float dropletRMin = 0.0009f, dropletRMax = 0.0120f, dropletBias = 3.2f;
    float dropletRise = 0.30f;
    float dropletRacerFrac = 0.0f, dropletRacerSpeed = 2.5f, dropletRacerWobble = 0.5f;
    float dropletRacerRMax = 0.0f;
    // --- final trim ---
    float postChroma = 1.0f, postLift = 1.0f;
};

// [ink] — the shared ink-in-water block (ink_mode = water).
struct InkKeys {
    bool  inverted = false;
    float paper[3]     = { 0.90f, 0.91f, 0.93f };
    float tintThin[3]  = { 0.70f, 0.85f, 1.00f };
    float tintThick[3] = { 1.00f, 1.00f, 1.00f };
};

struct Oil3DConfig {
    OilKeys oil;
    InkKeys ink;
    // Is any ink ever put into the 2D water? ([behavior] idle_splats, [drops]
    // drops.) The live stages have both 0, so their ink is the black paper.
    int   idleSplats = 1;
    int   dropsOn    = 0;

    // ---- [oil3d] -----------------------------------------------------------
    int   blobs      = -1;      // -1 = [liquid_acid] blob_count (capped at 64)
    bool  halfRes    = false;
    int   steps      = 64;      // march steps inside the support interval
    int   refine     = 5;       // bisection steps at the crossing
    float cameraDrift = 0.025f; // camera orbit amplitude, screen heights
    float cameraDriftPeriod = 97.0f;
    int   shade      = OIL3D_GLOSS;
    float fovDeg     = 38.0f;   // vertical
    float depth      = 0.60f;   // slab depth, screen heights
    float radiusScale = 1.0f;   // 3D radius = 2D radius * this
    float holeDepthStretch = 1.8f;  // holes are ellipsoids this long in z (tunnels)
    float cavityDark = 0.90f;   // a hole-carved wall goes this far toward the black role
    float zDrift     = 0.006f;  // slow depth wander, screen heights / s
    float flowAmp    = 0.004f;  // analytic curl flow (stands in for the 2D fluid), uv/s
    float parallax3d = 0.5f;    // share of the 2D size parallax kept (depth gives the rest)
    // lights: direction TOWARD the light, world space (+y down, +z away)
    float light0Dir[3] = { 0.20f, 0.85f, -0.50f };   // the lamp under the column
    float light0 = 0.45f;
    float light1Dir[3] = { -0.60f, -0.60f, -0.55f }; // top-left fill (the 2D rig lamp)
    float light1 = 0.42f;
    float wrap      = 0.15f;
    float ambient   = 0.06f;
    float spec      = 0.55f;    // small hot Blinn lobe
    float specPower = 240.0f;
    float rim       = 0.90f;    // fresnel rim strength (x 0.65 + mass_rim)
    float rimPower  = 3.5f;
    float sss       = 0.30f;    // thickness glow ("lit from inside")
    float sssSigma  = 12.0f;    // per screen-height of wax
    float ao        = 0.55f;
    float fog       = 0.35f;    // depth fog toward the backdrop
    float exposure  = 1.0f;
    float edgeSoft  = 0.10f;    // silhouette AA band in n.v
    int   flatBands = 3;
    float flatSoft  = 0.25f;
    float flatRimWidth = 0.22f; // n.v band of the dark rim
    float flatRimDark  = 0.85f;
    float inkMarble = -1.0f;    // -1 auto: 0 when the 2D water never gets ink
    float inkMarbleScale = 2.2f;
    float inkFlow   = 0.010f;   // backdrop marbling rise, screen heights / s
    int   dropletCap = 40;      // 3D droplets (2D runs hundreds; see the report)
    float dropletScale = 1.6f;
    float mixContrast  = 1.2f;  // hue2/hue3 patch field contrast (cover calibration)
    float hueRotateOverride = -1.0f;  // >0 replaces hue_rotate_period ([cycle] scheme_hue_period)
    int   debugView = 0;    // 1 normal 2 hole share 3 AO 4 thickness 5 unlit fill

    // ---- [hdr] -------------------------------------------------------------
    float peakNits = 1000.0f;
    float knee     = 0.70f;
};

// Partial-overlay load (keys absent from the file keep their value), with the
// file's [meta] base chain applied first (root first, up to 4, relative to the
// file's folder) exactly like the 2D app. Returns false if the file is missing.
// `ignored` collects "[section] key" strings that have no 3D meaning.
bool LoadOil3DConfig(const wchar_t* iniPath, Oil3DConfig& cfg,
                     std::vector<std::wstring>* ignored);

// Clamp to what the renderer supports. Logs what it changed.
void FinalizeOil3DConfig(Oil3DConfig& cfg);
