// Settings window: every wallpaper parameter, organized into category pages
// (left nav column + paged content area), dark themed. Resizable; the mood
// bar on top and the bottom utility rows stay visible at every size. Every
// change applies live and persists to settings.ini immediately.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vector>
#include <unordered_set>
#include <string>
#include <cstdio>
#include <cstddef>
#include <cmath>
#include "app_state.h"
#include "moods.h"
#include "journey.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ---------------------------------------------------------------------------

struct SliderDef {
    const wchar_t* label;
    float mn, mx, step;
    int decimals;
    float* fval;             // one of fval/ival is set
    int* ival;
    const wchar_t* section;  // ini section
    const wchar_t* key;
    bool reinitWanderers;
    const wchar_t* header;   // non-null: start a new category page here
    int col;                 // legacy 4-column layout field — unused
    const wchar_t* tip;      // hover tooltip
};
struct CheckDef {
    const wchar_t* label;
    bool* val;               // null => autostart (registry-backed)
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* header;
    int col;
    const wchar_t* tip;      // hover tooltip (null => none)
};

static HWND s_wnd = nullptr;
static HFONT s_font = nullptr, s_headFont = nullptr;
static HBRUSH s_darkBrush = nullptr;
static std::vector<SliderDef> s_sliders;
static std::vector<HWND> s_sliderCtls, s_sliderLabels;
static std::vector<CheckDef> s_checks;
static std::vector<HWND> s_checkCtls;
static std::vector<std::wstring> s_lastSliderText, s_lastCheckText;  // flicker guard
static std::unordered_set<HWND> s_headers;    // accent-colored statics
static HWND s_comboMode = nullptr, s_comboSim = nullptr, s_comboDye = nullptr;
static HWND s_fpsLabel = nullptr, s_pauseBtn = nullptr, s_moodLabel = nullptr;
static HWND s_moodName = nullptr, s_inCycle = nullptr;               // mood bar
static std::wstring s_lastMoodName;   // flicker guard for the name static
static HWND s_moodSave = nullptr, s_moodNew = nullptr, s_moodDel = nullptr;

// page model: the defs' header strings become the category list, in order
static std::vector<std::wstring> s_pages;
static std::vector<int> s_sliderPage, s_checkPage;
static std::vector<HWND> s_navBtns, s_pageHeads;
static HWND s_navPanel = nullptr, s_pagePanel = nullptr;
static int s_page = 0;   // survives window recreation (mood-change auto-rebuild)

struct ScrollState { int pos = 0, content = 0, view = 0; };
static ScrollState s_navScr, s_pageScr;

// bottom utility rows: fixed x per control, row-based y anchored to the
// bottom edge; stretch controls grow with the window width
struct BottomCtl { HWND hwnd; int row, x, yOff, w, h; bool stretch; };
static std::vector<BottomCtl> s_bottom;

static const int IDC_CHECK_BASE = 300;
static const int IDC_GAMUT_BASE = 400;
static const int IDC_WMODE      = 450;
static const int IDC_SIMRES     = 460;
static const int IDC_DYERES     = 461;
static const int IDC_COLOR_BASE = 500;
static const int IDC_NAV_BASE   = 700;
static const int IDC_OPEN_ANALYZER = 260;
static const int IDC_PAUSE_BTN  = 261;
static const int IDC_EXIT_BTN   = 262;
static const int IDC_SCENES_BTN = 263;
static const int IDC_SAVE_SCENE = 264;
static const int IDC_NEXT_MOOD  = 265;
static const int IDC_MOOD_INCYCLE = 266;
static const int IDC_MOOD_SAVE    = 267;
static const int IDC_MOOD_NEW     = 268;
static const int IDC_MOOD_DELETE  = 269;

static const int kMoodBarH = 36;   // top strip: mood name + save/new/delete
static const int kNavW = 168;
static const int kNavBtnH = 21, kNavBtnStep = 23;
static const int kMargin = 12;
static const int kRowH = 48, kHeadH = 30, kCheckH = 25;
static const int kRowSpace = 32, kBottomRows = 5, kLegendH = 22;
static const int kBottomH = kBottomRows * kRowSpace + kLegendH;
static const int kMinClientW = 640, kMinClientH = 480;
static const DWORD kWndStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                               WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;

static const int kSimResOptions[] = { 32, 64, 128, 256, 512 };
static const int kDyeResOptions[] = { 256, 512, 1024, 2048, 4096 };
static const COLORREF kAccent = RGB(122, 184, 255);

// ---------------------------------------------------------------------------

static void WriteIniFloat(const wchar_t* section, const wchar_t* key, float v, int decimals) {
    if (!g_iniPath[0]) return;
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f", decimals, v);
    WritePrivateProfileStringW(section, key, buf, g_iniPath);
}
static void WriteIniInt(const wchar_t* section, const wchar_t* key, int v) {
    if (!g_iniPath[0]) return;
    wchar_t buf[32];
    swprintf_s(buf, L"%d", v);
    WritePrivateProfileStringW(section, key, buf, g_iniPath);
}

static bool GetAutostart() {
    wchar_t path[MAX_PATH];
    DWORD sz = sizeof(path);
    return RegGetValueW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"FluidWallpaper", RRF_RT_REG_SZ, nullptr, path, &sz) == ERROR_SUCCESS;
}
static void SetAutostart(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 4];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        swprintf_s(cmd, L"\"%s\"", exe);
        RegSetValueExW(key, L"FluidWallpaper", 0, REG_SZ,
                       (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"FluidWallpaper");
    }
    RegCloseKey(key);
    printf("autostart: %s\n", on ? "enabled" : "disabled");
}

// ---------------------------------------------------------------------------

static void BuildDefs() {
    FluidConfig& c = g_renderer->Config();
    s_sliders = {
        { L"Vorticity (swirl strength)",        0,     50,   0.5f,  1, &c.curl,               nullptr, L"sim", L"vorticity", false, L"Simulation", 0, L"Small-scale swirl. High = cauliflower billows, low = smooth streams" },
        { L"Form resistance (baroclinic)",      0,     200,  5,     0, &c.baroclinic,         nullptr, L"sim", L"baroclinic", false, nullptr, 0, L"Wakes bend around dye masses instead of cutting through. 0 = off" },
        { L"Flow speed (all currents)",         0.2f,  2,    0.05f, 2, &c.flowSpeed,          nullptr, L"sim", L"flow_speed", false, nullptr, 0, L"Global current multiplier. Lower = slower evolution, same shapes" },
        { L"Splat radius",                      0.01f, 1,    0.005f,3, &c.splatRadius,        nullptr, L"sim", L"splat_radius", false, nullptr, 0, L"Size of emitted blobs" },
        { L"Density diffusion (dye linger)",    0.95f, 1,    0.0001f,4,&c.densityDissipation, nullptr, L"sim", L"density_diffusion", false, nullptr, 0, L"How long dye lingers. Higher = longer trails" },
        { L"Velocity diffusion",                0.95f, 1,    0.0001f,4,&c.velocityDissipation,nullptr, L"sim", L"velocity_diffusion", false, nullptr, 0, L"How long currents persist. Higher = smoother flow" },
        { L"Pressure diffusion",                0,     1,    0.005f,3, &c.pressureDissipation,nullptr, L"sim", L"pressure_diffusion", false, nullptr, 0, L"Flow smoothing. Lower = sharper blob edges" },
        { L"Pressure iterations",               10,    60,   1,     0, nullptr, &c.pressureIterations, L"sim", L"pressure_iterations", false, nullptr, 0, L"Solver quality. Rarely needs changing" },
        { L"Tail decay speed (lower=snappier)", 0.5f,  1,    0.002f,3, &c.decayFast,          nullptr, L"sim", L"decay_fast", false, nullptr, 0, L"How fast faint haze clears. 1.0 = never (deep layers)" },
        { L"Decay threshold",                   0,     0.3f, 0.002f,3, &c.decayThreshold,     nullptr, L"sim", L"decay_threshold", false, nullptr, 0, L"Below this brightness the fast decay acts" },
        { L"Saturation restore /s",             0,     1,    0.005f,3, &c.satRestore,         nullptr, L"sim", L"saturation_restore", false, nullptr, 0, L"Re-saturates aging dye so old layers stay colorful" },
        { L"Color intensity cap",               0.3f,  4,    0.05f, 2, &c.maxBrightness,      nullptr, L"sim", L"max_brightness", false, nullptr, 0, L"Max dye brightness (hue-preserving clip)" },
        { L"Dye diffusion (smoke spread)",      0,     0.5f, 0.005f,3, &c.dyeDiffusion,       nullptr, L"sim", L"dye_diffusion", false, nullptr, 0, L"Blurs dye. 0 = sharp marbling, high = soft mush" },
        { L"Gravity (dye sinks / rises)",       -200,  200,  1,     1, &c.gravity,            nullptr, L"sim", L"gravity", false, nullptr, 0, L"Dye-weighted gravity. Positive = ink sinks, negative = smoke rises. 0 = off" },
        { L"Gravity power (rho^p)",             0.5f,  3,    0.1f,  1, &c.gravityPow,         nullptr, L"sim", L"gravity_pow", false, nullptr, 0, L"Higher = only dense cores fall, thin veils hang" },
        { L"Gravity smoothing (sim texels)",    0,     12,   0.5f,  1, &c.gravityBlur,        nullptr, L"sim", L"gravity_blur", false, nullptr, 0, L"Blurs the density gravity reads. Low = grid-scale fingering, high = whole lobes sink" },
        { L"FPS limit",                         30,    260,  1,     0, &c.fpsLimit,           nullptr, L"general", L"fps_limit", false, L"Performance", 0, L"Frame rate cap" },
        { L"Count",                             1,     8,    1,     0, nullptr, &c.wandererCount,      L"behavior", L"wanderer_count", true, L"Wanderers", 1, L"Number of autonomous emitters" },
        { L"Speed (px/s)",                      50,    1200, 10,    0, &c.wandererSpeed,      nullptr, L"behavior", L"wanderer_speed", false, nullptr, 1, L"Emitter speed = current strength" },
        { L"Brightness",                        0.05f, 1,    0.01f, 2, &c.wandererBrightness, nullptr, L"behavior", L"wanderer_brightness", false, nullptr, 1, L"Paint per emitter per step" },
        { L"Path size (circle / figure-8)",     0.1f,  0.9f, 0.01f, 2, &c.wandererScale,      nullptr, L"behavior", L"wanderer_scale", true, nullptr, 1, L"Roam area for circle / figure-8 paths" },
        { L"Resume after idle (s)",             0,     30,   0.5f,  1, &c.wandererResumeDelay,nullptr, L"behavior", L"wanderer_resume_delay", false, nullptr, 1, L"Quiet time after your input before emitters resume" },
        { L"Min dark area % before pause",      5,     60,   1,     0, &c.darkFloor,          nullptr, L"behavior", L"dark_floor", false, L"Screen-fullness governor", 1, L"Emitters pause when dark area falls below this" },
        { L"Dark pixel cutoff",                 0.005f,0.1f, 0.005f,3, &c.darkLevel,          nullptr, L"behavior", L"dark_level", false, nullptr, 1, L"Brightness that still counts as dark" },
        { L"Survivor wanderer dark floor %",    0,     40,   1,     0, &c.survDarkFloor,      nullptr, L"behavior", L"surv_dark_floor", false, nullptr, 1, L"One survivor emitter paints until this darkness" },
        { L"Contrast required % (0 = off)",     0,     100,  1,     0, &c.contrastReq,        nullptr, L"behavior", L"contrast_req", false, nullptr, 1, L"Require a bright focal region, else emitters pause" },
        { L"Interval (s)",                      1,     30,   1,     0, &c.dartInterval,       nullptr, L"behavior", L"dart_interval", false, L"Separating dart", 1, L"Seconds between piercing darts" },
        { L"Speed (px/s)",                      500,   6000, 50,    0, &c.dartSpeed,          nullptr, L"behavior", L"dart_speed", false, nullptr, 1, L"Dart travel speed" },
        { L"Peak brightness (nits, 0 = off)",   0,     1500, 5,     0, &g_hdrPeakNits,        nullptr, L"hdr", L"peak_nits", false, L"HDR output", 2, L"HDR hot-spot target. 0 = match SDR" },
        { L"Knee (boost starts at)",            0.1f,  1.3f, 0.02f, 2, &c.hdrKnee,            nullptr, L"hdr", L"knee", false, nullptr, 2, L"Dye level where HDR highlight boost begins" },
        { L"Saturation boost",                  1,     2,    0.01f, 2, &c.hdrSaturation,      nullptr, L"hdr", L"saturation", false, nullptr, 2, L"Extra punch while Windows HDR is on" },
        { L"Brightness boost",                  0.8f,  1.5f, 0.01f, 2, &c.hdrBrightness,      nullptr, L"hdr", L"brightness", false, nullptr, 2, L"Extra punch while Windows HDR is on" },
        { L"Contrast",                          0.8f,  1.5f, 0.01f, 2, &c.hdrContrast,        nullptr, L"hdr", L"contrast", false, nullptr, 2, L"Extra punch while Windows HDR is on" },
        { L"Saturation",                        0.5f,  2,    0.01f, 2, &c.postSaturation,     nullptr, L"color", L"post_saturation", false, L"Color grading (WE panel)", 2, L"WE-style whole-frame filter" },
        { L"Contrast",                          0.5f,  2,    0.01f, 2, &c.postContrast,       nullptr, L"color", L"post_contrast", false, nullptr, 2, L"WE-style whole-frame filter" },
        { L"Brightness",                        0.5f,  1.5f, 0.01f, 2, &c.postBrightness,     nullptr, L"color", L"post_brightness", false, nullptr, 2, L"WE-style whole-frame filter" },
        { L"Hue rotate (deg)",                  0,     360,  1,     0, &c.postHue,            nullptr, L"color", L"post_hue", false, nullptr, 2, L"Rotates all colors. Warning: rotates outside the hue band" },
        { L"Dwell (min per mood)",              1,     30,   1,     0, &g_moodSettings.dwellMinutes,   nullptr, L"moods", L"dwell_minutes", false, L"Mood cycling", 2, L"Minutes in a mood before switching" },
        { L"Transition length (s)",             2,     60,   1,     0, &g_moodSettings.transitionSec,  nullptr, L"moods", L"transition_seconds", false, nullptr, 2, L"Seconds a mood change takes" },
        { L"Timing jitter (± fraction)",        0,     0.5f, 0.05f,2, &g_moodSettings.jitter,          nullptr, L"moods", L"jitter", false, nullptr, 2, L"Random +/- on dwell time so switches feel organic" },
        { L"Cycle time (s per lap)",            2,     120,  1,     0, &c.colorCyclePeriod,   nullptr, L"behavior", L"color_cycle_period", false, L"Color wheel", 3, L"Seconds for emitted hue to sweep its band" },
        { L"Hue band center (deg)",             0,     360,  1,     0, &c.hueCenter,          nullptr, L"color", L"hue_center", false, nullptr, 3, L"Where on the color wheel emission lives (0=red 120=green 240=blue)" },
        { L"Hue band range (180 = full wheel)", 5,     180,  1,     0, &c.hueRange,           nullptr, L"color", L"hue_range", false, nullptr, 3, L"Half-width of the emission band. 180 = full wheel" },
        { L"Hue linger (rest at band edges)",   0,     0.45f,0.05f, 2, &c.hueLinger,         nullptr, L"color", L"hue_linger", false, nullptr, 3, L"Fraction of each half-lap spent resting on one hue. 0 = off" },
        { L"Step (deg)",                        10,    180,  1,     0, &c.hsStep,             nullptr, L"behavior", L"hueshift_step", false, L"Hue shift bursts", 3, L"Palette rotation per burst step" },
        { L"Linger (s)",                        0,     15,   0.5f,  1, &c.hsLinger,           nullptr, L"behavior", L"hueshift_linger", false, nullptr, 3, L"Hold time between burst steps" },
        { L"Glide (s)",                         0.1f,  10,   0.1f,  1, &c.hsGlide,            nullptr, L"behavior", L"hueshift_glide", false, nullptr, 3, L"Rotation speed of each step" },
        { L"Steps per burst",                   1,     12,   1,     0, nullptr, &c.hsBurstSteps,       L"behavior", L"hueshift_burst_steps", false, nullptr, 3, L"Steps before rotating home" },
        { L"Off time between bursts (s)",       0,     120,  1,     0, &c.hsOffTime,          nullptr, L"behavior", L"hueshift_off_time", false, nullptr, 3, L"Quiet time between hue-shift bursts" },
        { L"Interval (s)",                      0.5f,  30,   0.5f,  1, &c.idleInterval,       nullptr, L"behavior", L"idle_interval", false, L"Idle splats", 3, L"Seconds between random blob bursts" },
        { L"Amount per burst",                  1,     30,   1,     0, nullptr, &c.idleAmount,         L"behavior", L"idle_amount", false, nullptr, 3, L"Blobs per burst" },
        { L"Burst brightness",                  0.2f,  3,    0.05f, 2, &c.idleBrightness,     nullptr, L"behavior", L"idle_brightness", false, nullptr, 3, L"Idle blob intensity (emitters paint at 0.15)" },
        { L"Hump center (input brightness)",    0.05f, 1,    0.01f, 2, &c.curveCenter,        nullptr, L"color", L"curve_center", false, L"Response curve (bright rims)", 3, L"Response curve shape: glowing rims when enabled" },
        { L"Hump width",                        0.02f, 0.5f, 0.01f, 2, &c.curveWidth,         nullptr, L"color", L"curve_width", false, nullptr, 3, L"Response curve shape: glowing rims when enabled" },
        { L"Hump height (output brightness)",   0.1f,  2,    0.05f, 2, &c.curveHeight,       nullptr, L"color", L"curve_height", false, nullptr, 3, L"Response curve shape: glowing rims when enabled" },
        { L"Shadow floor (colour lift)",          0,     0.25f,0.005f,3, &c.shadowFloor,        nullptr, L"color", L"shadow_floor", false, L"Shadow floor (dark marbling)", 3, L"Lift near-black along its own hue so dark marbling stays visible" },
        { L"Shadow knee (lift range)",          0.02f, 0.6f, 0.01f, 2, &c.shadowKnee,         nullptr, L"color", L"shadow_knee", false, nullptr, 3, L"Brightness range the lift fades over" },
        // --- "Liquid Acid" look (oil on inked water); inert unless enabled ---
        { L"Blob count",                        16,    128,  1,     0, nullptr, &c.acid.blobCount,   L"liquid_acid", L"blob_count", false, L"Liquid Acid", 3, L"Oil blobs stepped and evaluated per pixel. Higher = busier and slower" },
        { L"Big discs (fraction)",              0,     0.5f, 0.01f, 2, &c.acid.discFrac,      nullptr, L"liquid_acid", L"disc_frac", false, nullptr, 3, L"Share of blobs that are big flat discs" },
        { L"Web blobs (fraction)",              0,     0.6f, 0.01f, 2, &c.acid.webFrac,       nullptr, L"liquid_acid", L"web_frac", false, nullptr, 3, L"Share seeded in chains so they merge into veined webs" },
        { L"Bubbles (fraction)",                0,     0.8f, 0.01f, 2, &c.acid.bubbleFrac,    nullptr, L"liquid_acid", L"bubble_frac", false, nullptr, 3, L"Share that are small round bubbles. Remainder = holes" },
        { L"Hole bite (negative weight)",       0,     3,    0.05f, 2, &c.acid.holeWeight,    nullptr, L"liquid_acid", L"hole_weight", false, nullptr, 3, L"How hard negative blobs eat round holes out of the oil" },
        { L"Bubble size bias (higher = tiny)",  0.5f,  4,    0.1f,  1, &c.acid.sizeBias,      nullptr, L"liquid_acid", L"size_bias", false, nullptr, 3, L"Skews bubble/hole radii toward the small end" },
        { L"Disc size bias (lower = bigger)",   0.2f,  2,    0.05f, 2, &c.acid.bigBias,       nullptr, L"liquid_acid", L"big_bias", false, nullptr, 3, L"Skews disc/web radii toward the large end. Below 1 = mostly huge" },
        { L"Surface level (higher = smaller)",  0.1f,  1.2f, 0.01f, 2, &c.acid.threshold,     nullptr, L"liquid_acid", L"threshold", false, nullptr, 3, L"Field level of the oil surface. Higher = tighter, more separate blobs" },
        { L"Stickiness (support radius)",       1.2f,  4,    0.05f, 2, &c.acid.supportScale,  nullptr, L"liquid_acid", L"support_scale", false, nullptr, 3, L"How far a blob's influence reaches. Higher = blobs bridge from further apart" },
        { L"Flow gain (fluid drags the oil)",   0,     4,    0.05f, 2, &c.acid.flowGain,      nullptr, L"liquid_acid", L"flow_gain", false, nullptr, 3, L"How strongly the sim's velocity advects the blobs" },
        { L"Curl drift",                        0,     0.01f,0.0002f,4,&c.acid.curlDrift,     nullptr, L"liquid_acid", L"curl_drift", false, nullptr, 3, L"Analytic swirl on top, so oil still creeps in still water" },
        { L"Repulsion (same-sign blobs)",       0,     3,    0.05f, 2, &c.acid.repulsion,     nullptr, L"liquid_acid", L"repulsion", false, nullptr, 3, L"Keeps blobs from collapsing into a single mass" },
        { L"Rim width",                         0.001f,0.03f,0.0005f,4,&c.acid.rimWidth,      nullptr, L"liquid_acid", L"rim_width", false, nullptr, 3, L"Half-width of the dark rim band at the oil edge" },
        { L"Rim darkness",                      0,     1,    0.02f, 2, &c.acid.rimDark,       nullptr, L"liquid_acid", L"rim_dark", false, nullptr, 3, L"How dark the thin rim just inside the oil edge goes" },
        { L"Rim variation",                     0,     1,    0.05f, 2, &c.acid.rimVary,       nullptr, L"liquid_acid", L"rim_vary", false, nullptr, 3, L"Break the rim up: low-frequency noise on its width and on the halo, so it thickens, thins and dies out along the edge" },
        { L"Rim follows ink",                   0,     1,    0.05f, 2, &c.acid.rimInkFollow,  nullptr, L"liquid_acid", L"rim_ink_follow", false, nullptr, 3, L"The bright halo is refracted ink: scale it by how bright the ink just outside the edge is" },
        { L"Meniscus (bright halo)",            0,     1,    0.02f, 2, &c.acid.meniscus,      nullptr, L"liquid_acid", L"meniscus", false, nullptr, 3, L"Thin bright ink-coloured halo just outside the oil's dark rim" },
        { L"Meniscus width",                    0.0005f,0.01f,0.0005f,4,&c.acid.meniscusW,    nullptr, L"liquid_acid", L"meniscus_width", false, nullptr, 3, L"Half-width of the bright halo" },
        { L"Hole swarm (bubbles in the oil)",   0,     1,    0.02f, 2, &c.acid.swarmHoles,    nullptr, L"liquid_acid", L"swarm_holes", false, nullptr, 3, L"Hundreds of water droplets trapped inside the oil" },
        { L"Droplet swarm (oil on the ink)",    0,     1,    0.02f, 2, &c.acid.swarmDrops,    nullptr, L"liquid_acid", L"swarm_drops", false, nullptr, 3, L"Small oil droplets floating on the open ink" },
        { L"Swarm clumping",                    0,     1,    0.02f, 2, &c.acid.swarmClump,    nullptr, L"liquid_acid", L"swarm_clump", false, nullptr, 3, L"0 = even blanket of droplets, 1 = droplets only in patches" },
        { L"Swarm density",                     0.05f, 1,    0.02f, 2, &c.acid.swarmDensity,  nullptr, L"liquid_acid", L"swarm_density", false, nullptr, 3, L"Fraction of swarm cells that carry a droplet" },
        { L"Swarm hole size (higher=smaller)",  6,     90,   1,     0, &c.acid.swarmScaleA,   nullptr, L"liquid_acid", L"swarm_scale_holes", false, nullptr, 3, L"Cells per unit for the hole swarm. Higher = smaller, denser holes" },
        { L"Swarm droplet size (higher=small)", 6,     90,   1,     0, &c.acid.swarmScaleB,   nullptr, L"liquid_acid", L"swarm_scale_drops", false, nullptr, 3, L"Cells per unit for the droplet swarm" },
        { L"Refraction through the rim",        0,     0.2f, 0.005f,3, &c.acid.refraction,    nullptr, L"liquid_acid", L"refraction", false, nullptr, 3, L"Shifts the ink seen through the thinning oil near edges" },
        { L"Oil translucency",                  0,     1,    0.02f, 2, &c.acid.translucency,  nullptr, L"liquid_acid", L"translucency", false, nullptr, 3, L"How much the ink underneath modulates the oil fill" },
        { L"Ink emboss (0 = flat bands)",        0,     1,    0.02f, 2, &c.acid.inkShading,    nullptr, L"liquid_acid", L"ink_shading", false, nullptr, 3, L"How much of the fluid look's pseudo-3D shading survives. The refs are flat" },
        { L"Ink bands (posterise)",             2,     24,   1,     0, &c.acid.inkLevels,     nullptr, L"liquid_acid", L"ink_levels", false, nullptr, 3, L"Flat colour plateaus in the ink. Low = poster, high = smooth" },
        { L"Ink band softness",                 0.01f, 1.0f, 0.01f, 2, &c.acid.inkSoft,       nullptr, L"liquid_acid", L"ink_soft", false, nullptr, 3, L"0 = hard steps between bands" },
        { L"Ink palette strength",              0,     1,    0.02f, 2, &c.acid.inkMix,        nullptr, L"liquid_acid", L"ink_mix", false, nullptr, 3, L"0 = keep the normal fluid colours, 1 = full duotone ramp" },
        { L"Ink hue variation (deg)",           0,     90,   1,     0, &c.acid.inkHueVary,    nullptr, L"liquid_acid", L"ink_hue_vary", false, nullptr, 3, L"Rotates the ramp by the dye's own hue so the ink still drifts" },
        { L"Complement window (deg)",           5,     180,  1,     0, &c.acid.inkComplementSpan, nullptr, L"liquid_acid", L"ink_complement_span", false, nullptr, 3, L"How far the ink hue may wander from the oil's opposite. Small = strictly two-hue" },
        { L"Palette sweep (s per cycle, 0=off)",   0,     1800, 5,     0, &c.acid.hueSweepPeriod, nullptr, L"liquid_acid", L"hue_sweep_period", false, nullptr, 3, L"Cross-fade through the curated vivid palette list. Long settled stretches, short fades. 0 = fixed palette" },
        { L"Palettes in the sweep list",        1,     12,   1,     0, nullptr, &c.acid.sweepCount, L"liquid_acid", L"sweep_count", false, nullptr, 3, L"How many entries of the sweep list are used (the shipped list is 5; the tile9 set is 8)" },
        { L"Hue rotation (s per turn, 0=off)",  0,     3600, 10,    0, &c.acid.hueRotatePeriod, nullptr, L"liquid_acid", L"hue_rotate_period", false, nullptr, 3, L"Continuously rotates the whole oil palette's hue. The ink is untouched, so mono ink stays grey and the holes stay black. 0 = off" },
        { L"Oil saturation",                    0,     2,    0.05f, 2, &c.acid.oilSaturation, nullptr, L"liquid_acid", L"oil_saturation", false, nullptr, 3, L"Vividness of the oil palette, whichever source it came from. 1 = the authored colours" },
        { L"Film level",                        0,     1,    0.02f, 2, &c.acid.filmLevel,     nullptr, L"liquid_acid", L"film_level", false, nullptr, 3, L"Brightness of the flat oil film (the background sheet). 1 = today; ~0.05 = the film pitch black so the dyed masses/droplets carry the colour. The edge light (mass rim, oil glow, halo, lens highlights) keeps the full palette and the mass/droplet dye is not touched" },
        { L"Rise speed (lava lamp, uv/s)",      0,     0.08f,0.001f,3, &c.acid.riseSpeed,     nullptr, L"liquid_acid", L"rise_speed", false, nullptr, 3, L"Constant upward drift on every blob. 0.015 = one screen height in about 65 s. 0 = the shipped free drift" },
        { L"Rise wobble",                       0,     1,    0.05f, 2, &c.acid.riseWobble,    nullptr, L"liquid_acid", L"rise_wobble", false, nullptr, 3, L"A lazy sideways sway on the way up, out of step from blob to blob" },
        { L"Rise stretch (teardrops)",          0,     1,    0.05f, 2, &c.acid.riseStretch,   nullptr, L"liquid_acid", L"rise_stretch", false, nullptr, 3, L"Moving blobs elongate along their travel and relax round as they slow. Small fast ones stretch most" },
        { L"Lamp base light",                   0,     1,    0.05f, 2, &c.acid.riseBottomLight, nullptr, L"liquid_acid", L"rise_bottom_light", false, nullptr, 3, L"Brightens the oil near the bottom of the frame and cools it toward the top, like a lamp heated from below" },
        { L"Rise parallax (small = far)",       0,     1,    0.05f, 2, &c.acid.riseParallax,  nullptr, L"liquid_acid", L"rise_parallax", false, nullptr, 3, L"Small blobs rise, sway and follow the water more slowly, as if further away. Scaled by radius against the largest disc" },
        { L"Parallax dimming",                  0,     1,    0.05f, 2, &c.acid.riseParallaxDim, nullptr, L"liquid_acid", L"rise_parallax_dim", false, nullptr, 3, L"Far (small) blobs are slightly darker as a depth cue. Keep it subtle; past ~0.4 they read as a different palette" },
        { L"Oil drag on the ink",               0,     1,    0.05f, 2, &c.acid.oilDrag,       nullptr, L"liquid_acid", L"oil_drag", false, nullptr, 3, L"Friction under the oil: ink beneath an island is brought to rest and deflected around its rim, so the water cannot stream in underneath" },
        { L"Oil blocks the ink",                0,     1,    0.05f, 2, &c.acid.oilDyeBlock,   nullptr, L"liquid_acid", L"oil_dye_block", false, nullptr, 3, L"Ink that ends up under the oil fades out over a second or two, so the oil reads as sitting ON the water. Droplet holes are not oil and keep their ink" },
        { L"Oil viscosity",                     0,     1,    0.05f, 2, &c.acid.oilViscosity,  nullptr, L"liquid_acid", L"oil_viscosity", false, nullptr, 3, L"Thick liquid: blobs lag the water, accelerate and coast slowly, breathe and sway slowly, neck together over seconds, and the droplets stop jittering" },
        { L"Mouse oil mode (0 none 1 push 2 comb)", 0, 2,   1,     0, nullptr, &c.acid.mouseOilMode, L"liquid_acid", L"mouse_oil_mode", false, nullptr, 3, L"What the cursor does to the OIL (never to the ink). 0 = nothing at all, 1 = drags and parts it, 2 = a marbling comb that leaves streaks which round back up over about 3 s" },
        { L"Mouse oil radius (uv)",             0.02f, 0.5f, 0.01f, 2, &c.acid.mouseOilRadius, nullptr, L"liquid_acid", L"mouse_oil_radius", false, nullptr, 3, L"Reach of the push, or the length of the comb's band along the pointer path" },
        { L"Mouse oil gain",                    0,     3,    0.05f, 2, &c.acid.mouseOilGain,  nullptr, L"liquid_acid", L"mouse_oil_gain", false, nullptr, 3, L"Strength multiplier for the push or the comb" },
        { L"Final chroma",                      0.5f,  2,    0.02f, 2, &c.acid.postChroma,    nullptr, L"liquid_acid", L"post_chroma", false, nullptr, 3, L"Scales the finished frame's colourfulness about its own brightness. About 1.2 restores what a transparent film costs" },
        { L"Final lift",                        0.5f,  1.6f, 0.02f, 2, &c.acid.postLift,      nullptr, L"liquid_acid", L"post_lift", false, nullptr, 3, L"Brightness multiplier on the finished frame" },
        { L"Ink ramp gain",                     0.3f,  3,    0.05f, 2, &c.acid.inkGain,       nullptr, L"liquid_acid", L"ink_gain", false, nullptr, 3, L"Maps dye brightness onto the ramp. Higher = more bright ink" },
        { L"Seam strength (dark edging)",       0,     1,    0.02f, 2, &c.acid.seamStrength,  nullptr, L"liquid_acid", L"seam_strength", false, nullptr, 3, L"Dark seams where the dye gradient is steep (acrylic-pour edging)" },
        { L"Seam threshold",                    0,     0.5f, 0.01f, 2, &c.acid.seamLo,        nullptr, L"liquid_acid", L"seam_lo", false, nullptr, 3, L"Gradient magnitude a seam starts at" },
        { L"Film grain",                        0,     0.2f, 0.005f,3, &c.acid.grainAmt,      nullptr, L"liquid_acid", L"grain", false, nullptr, 3, L"Coarse animated grain over the whole frame" },
        { L"Grain coarseness (px)",             1,     6,    0.5f,  1, &c.acid.grainScale,    nullptr, L"liquid_acid", L"grain_scale", false, nullptr, 3, L"Pixels per grain cell. 1 = fine, 4 = chunky macro-film" },
        { L"Grain into shadows",                0,     1,    0.02f, 2, &c.acid.grainShadowW,  nullptr, L"liquid_acid", L"grain_shadow_weight", false, nullptr, 3, L"Weights the grain by (1-luminance)^2 so flat bright oil stays clean" },
        { L"Ink-tinted black toe",              0,     1,    0.02f, 2, &c.acid.toeTint,       nullptr, L"liquid_acid", L"toe_tint", false, nullptr, 3, L"Lifts the darkest pixels toward a dark version of the ink hue instead of neutral black" },
        { L"Edge: 0 soft film / 1 crisp",       0,     1,    1,     0, nullptr, &c.acid.oilEdgeMode, L"liquid_acid", L"oil_edge_mode", false, nullptr, 3, L"0 = the film thins out over a wide band that scales with the blob size. 1 = every surface ends on the same hard isoline the droplets do, with only a thin meniscus" },
        { L"Accent: 0 any size / 1 small only",  0,    1,    1,     0, nullptr, &c.acid.accentMode, L"liquid_acid", L"accent_mode", false, nullptr, 3, L"Who may take the palette's 4th shade (the complement in the 80-20 palettes). 0 = whoever drew it, big masses included. 1 = only elements under the accent size, so the frame is one colour with small scattered accents" },
        { L"Accent max size (of disc max)",     0.05f, 1,   0.01f, 2, &c.acid.accentMaxR,    nullptr, L"liquid_acid", L"accent_max_r", false, nullptr, 3, L"How small a blob has to be to carry the accent colour, as a fraction of the biggest disc. Anything larger uses the base shades" },
        { L"Accent share (of the small ones)",  0,     1,   0.02f, 2, &c.acid.accentFrac,    nullptr, L"liquid_acid", L"accent_frac", false, nullptr, 3, L"Fraction of the small blobs that actually take it, so it stays an accent and not a rule" },
        { L"Backlight penumbra",                0,     1,   0.02f, 2, &c.acid.oilPenumbra,   nullptr, L"liquid_acid", L"oil_penumbra", false, nullptr, 3, L"The lamp is under the middle of the dish: the oil right beside a black mass is lit less, so it darkens and shifts hue a little over a soft band" },
        { L"Penumbra width (px at 1440p)",      1,    40,   1,     0, &c.acid.oilPenumbraPx, nullptr, L"liquid_acid", L"oil_penumbra_px", false, nullptr, 3, L"How far that shading reaches into the oil" },
        { L"Penumbra hue shift (deg)",        -90,    90,   1,     0, &c.acid.oilPenumbraHue,nullptr, L"liquid_acid", L"oil_penumbra_hue", false, nullptr, 3, L"Which way the less-lit pigment turns. Arbitrary but consistent for a look" },
        { L"Penumbra darkening",                0,     1,   0.02f, 2, &c.acid.oilPenumbraDark,nullptr, L"liquid_acid", L"oil_penumbra_dark", false, nullptr, 3, L"How much level the band loses as well as hue" },
        { L"Cellulose texture",                 0,     1,   0.02f, 2, &c.acid.cellulose,     nullptr, L"liquid_acid", L"cellulose", false, nullptr, 3, L"A fibrous, mottled macro texture on the surface: strands of the kind a cell body shows under a microscope, drifting with the rise. 0 = off" },
        { L"Cellulose on the black",            0,     2,   0.05f, 2, &c.acid.celluloseInk,  nullptr, L"liquid_acid", L"cellulose_ink", false, nullptr, 3, L"How much of it lands on the black side. It fades out deep inside a black mass, so the strands read as a thin layer near its edge and never as a grey wash" },
        { L"Cellulose on the film",             0,     2,   0.05f, 2, &c.acid.celluloseOil,  nullptr, L"liquid_acid", L"cellulose_oil", false, nullptr, 3, L"How much of it lands on the coloured film, where it only mottles the colour" },
        { L"Cellulose scale (px at 1440p)",     8,   140,   2,     0, &c.acid.celluloseScale,nullptr, L"liquid_acid", L"cellulose_scale", false, nullptr, 3, L"How long and how far apart the fibres are" },
        { L"Cellulose drift",                   0,     1,   0.05f, 2, &c.acid.celluloseDrift,nullptr, L"liquid_acid", L"cellulose_drift", false, nullptr, 3, L"1 = the texture travels with the rise, so it belongs to the masses; 0 = it is pinned to the screen" },
        { L"Edge S-curve",                      0,     1,    0.05f, 2, &c.acid.oilEdgeCurve, nullptr, L"liquid_acid", L"oil_edge_curve", false, nullptr, 3, L"Reshapes the edge ramp from smoothstep to smootherstep, so the slide from oil colour to black arrives without a knee at either end. Same band width" },
        { L"Soft thin edge (real oil)",         0,     1,    0.02f, 2, &c.acid.oilThinEdge,   nullptr, L"liquid_acid", L"oil_thin_edge", false, nullptr, 3, L"The film thins at its edge: the colour slides toward the ink beneath and the disc fades over a band, instead of a hard cut-out with a stroked rim" },
        { L"Thin-edge width",                   0.02f, 0.40f, 0.01f, 2, &c.acid.oilEdgeFrac,  nullptr, L"liquid_acid", L"oil_edge_frac", false, nullptr, 3, L"That band as a fraction of each blob's own radius" },
        { L"Oil specular",                      0,     1,    0.02f, 2, &c.acid.oilSpecular,   nullptr, L"liquid_acid", L"oil_specular", false, nullptr, 3, L"Broad soft highlight off the lens curvature and a slow thickness ripple (the rig is backlit, so keep it low)" },
        { L"Oil iridescence",                   0,     1,    0.02f, 2, &c.acid.oilIrid,       nullptr, L"liquid_acid", L"oil_iridescence", false, nullptr, 3, L"Thin-film hue shimmer indexed by film thickness, strongest at the thin edges" },
        { L"Droplets as lenses",                0,     1,    0.02f, 2, &c.acid.swarmLens,     nullptr, L"liquid_acid", L"swarm_lens", false, nullptr, 3, L"Trapped droplets become soft-edged holes in the film with a thin-oil fringe and a small highlight, not flat black discs" },
        { L"Emergent halo (from the ink)",      0,     1,    0.02f, 2, &c.acid.meniscusFromInk, nullptr, L"liquid_acid", L"meniscus_from_ink", false, nullptr, 3, L"The halo takes the colour and the brightness of the ink just outside the edge, and disappears (with the dark hairline) over dark ink" },
        { L"  halo colour from the film",        0,     1,    0.05f, 2, &c.acid.meniscusFilmMix, nullptr, L"liquid_acid", L"meniscus_film_mix", false, nullptr, 3, L"The bright halo is the widest, strongest thing painted on a droplet's boundary, and its colour comes from the INK -- so no film-hue feature (a second hue, a seam reflect, the sweep) ever reaches it. Raised, the halo takes its colour from the FILM at that droplet instead, the same colour its lit rim already uses, so a hue2 seam colours the halos around it too, and the ink-brightness gate that hides the halo over black ink opens by the same amount -- that gate is there to stop a foreign INK colour landing on black ink, and over black ink it is what makes the halo invisible today. The band keeps its shape, its width and its position; the dark hairline is not touched" },
        { L"Oil glow outside",                  0,     1,    0.02f, 2, &c.acid.oilGlow,       nullptr, L"liquid_acid", L"oil_glow", false, nullptr, 3, L"Diffuse spill of the oil's own colour into the ink around it" },
        { L"Refraction band width",             0,    40.0f, 1.0f,  1, &c.acid.refractionWidth, nullptr, L"liquid_acid", L"refraction_width", false, nullptr, 3, L"Rim-width multiplier for the band where the ink is seen bending under the oil (0 = the shipped 7)" },
        { L"Transparent film",                  0,     1,    0.02f, 2, &c.acid.oilTransparency, nullptr, L"liquid_acid", L"oil_transparency", false, nullptr, 3, L"The oil stops being a fill and becomes a coloured film: the ink's marbling reads through it, tinted by the oil's own hue" },
        { L"Film absorption",                   0,     6.0f, 0.1f,  2, &c.acid.oilAbsorb,     nullptr, L"liquid_acid", L"oil_absorb", false, nullptr, 3, L"How deeply the film absorbs. Low = a pale wash, high = a deep saturated glass" },
        { L"Film thickness variation",          0,     1,    0.02f, 2, &c.acid.oilFilmBump,   nullptr, L"liquid_acid", L"oil_film_bump", false, nullptr, 3, L"Slow noise on the film's thickness, so it has islands of thick and thin instead of one even pane" },
        { L"Refraction across the body",        0,     0.04f, 0.002f, 4, &c.acid.oilRefractBody, nullptr, L"liquid_acid", L"oil_refract_body", false, nullptr, 3, L"Displaces what is seen through the whole film, not only its edge, so the marbling wobbles as it passes under" },
        { L"Ink out of focus under the oil",    0,     1,    0.02f, 2, &c.acid.oilInkBlur,    nullptr, L"liquid_acid", L"oil_ink_blur", false, nullptr, 3, L"Softens the ink seen through the film, strongest where the film is thickest" },
        // --- droplet particle sim (replaces the procedural swarms) ---------
        { L"Droplets (particles, 0 = off)",     0,     4096, 25,    0, nullptr, &c.acid.droplets, L"liquid_acid", L"droplets", false, nullptr, 3, L"Simulated droplets rendered INTO the oil surface: water trapped in the oil and oil droplets on the ink. Above 0 the procedural swarms are switched off" },
        { L"Droplet nucleation (per s)",        0,     300,  5,     0, &c.acid.dropletSpawn,  nullptr, L"liquid_acid", L"droplet_spawn_rate", false, nullptr, 3, L"How fast new droplets come out of solution, biased to thin oil near an edge and to shearing flow" },
        { L"Droplet max radius",                0.002f,0.03f,0.001f,4, &c.acid.dropletRMax,  nullptr, L"liquid_acid", L"droplet_r_max", false, nullptr, 3, L"Largest droplet, as a fraction of frame height. Clamped to one grid cell" },
        { L"Droplet size bias (higher = tiny)", 1,     6,    0.1f,  1, &c.acid.dropletBias,   nullptr, L"liquid_acid", L"droplet_bias", false, nullptr, 3, L"Heavy tail toward small droplets" },
        { L"Droplet hole depth",                0,     3,    0.05f, 2, &c.acid.dropletWeight, nullptr, L"liquid_acid", L"droplet_weight", false, nullptr, 3, L"How hard a trapped water droplet punches through the oil, relative to the LOCAL field so it works in thick oil too" },
        { L"Droplets on the ink (fraction)",    0,     1,    0.02f, 2, &c.acid.dropletInkFrac,nullptr, L"liquid_acid", L"droplet_ink_frac", false, nullptr, 3, L"Share of droplets that are oil sitting on the open ink instead of water trapped in the oil" },
        { L"Droplet life (s)",                  0,     600,  10,    0, &c.acid.dropletLife,   nullptr, L"liquid_acid", L"droplet_life", false, nullptr, 3, L"Seconds before a droplet dissolves (shrinking away, never popping). 0 = never" },
        { L"Droplet attraction",                0,     2,    0.05f, 2, &c.acid.dropletAttract,nullptr, L"liquid_acid", L"droplet_attract", false, nullptr, 3, L"Short-range pull between droplets of the same kind, which is what makes them find each other and merge" },
        { L"Droplet coalescence overlap",       0.05f, 0.9f, 0.02f, 2, &c.acid.dropletMerge,  nullptr, L"liquid_acid", L"droplet_merge", false, nullptr, 3, L"Overlap fraction at which two droplets become one, area-conserving" },
        { L"Hollow ring droplets (fraction)",   0,     1,    0.02f, 2, &c.acid.dropletRingFrac, nullptr, L"liquid_acid", L"droplet_ring_frac", false, nullptr, 3, L"Share of new trapped droplets drawn as a thin dark RING with the oil showing through the middle -- the empty doubles" },
        { L"Ring width (x radius)",             0.02f, 0.6f, 0.01f, 2, &c.acid.dropletRingWidth, nullptr, L"liquid_acid", L"droplet_ring_width", false, nullptr, 3, L"How thick a hollow droplet's dark band is, as a fraction of its radius" },
        { L"Ring interior lift",                0,     1,    0.02f, 2, &c.acid.dropletRingLift, nullptr, L"liquid_acid", L"droplet_ring_lift", false, nullptr, 3, L"How much brighter (and slightly thicker) the middle of a hollow droplet reads -- a lens, not a hole" },
        { L"Ring clumping (bubble rafts)",      0,     1,    0.02f, 2, &c.acid.dropletRingClump, nullptr, L"liquid_acid", L"droplet_ring_clump", false, nullptr, 3, L"Rings attract each other and pack into rafts with a shared dark wall instead of merging" },
        { L"Bubble weather (seasons)",          0,     1,    0.05f, 2, &c.acid.weather, nullptr, L"liquid_acid", L"weather", false, nullptr, 3, L"Slow seasons in the droplet population: spells of more rings, more solids, sparser, denser, so the frame never settles (needs conservation of mass for the density half)" },
        { L"  weather period (s)",             30,  1200,   10.0f, 0, &c.acid.weatherPeriodS, nullptr, L"liquid_acid", L"weather_period_s", false, nullptr, 3, L"Mean seconds per weather phase" },
        { L"Second film hue (deg)",          -180,   180,   5.0f,  0, &c.acid.filmHue2, nullptr, L"liquid_acid", L"film_hue2", false, nullptr, 3, L"How far off the palette's own hue the second dye is. Roughly complementary reads like the reference: cyan patches in a magenta film" },
        { L"  second hue amount",               0,     1,   0.05f, 2, &c.acid.filmHue2Amt, nullptr, L"liquid_acid", L"film_hue2_amt", false, nullptr, 3, L"How far the film goes toward that hue inside a patch. 0 = one colour, as before" },
        { L"  patch size",                   0.08f, 0.90f, 0.05f, 2, &c.acid.filmHue2Scale, nullptr, L"liquid_acid", L"film_hue2_scale", false, nullptr, 3, L"Patch size as a fraction of the frame. A quarter to a half is the reference's scale" },
        { L"  patch drift",                     0,     2,   0.05f, 2, &c.acid.filmHue2Drift, nullptr, L"liquid_acid", L"film_hue2_drift", false, nullptr, 3, L"How fast the patches wander on their own, on top of being carried by the sim's flow" },
        { L"  patch decay",                     0,     2,   0.05f, 2, &c.acid.filmHue2Decay, nullptr, L"liquid_acid", L"film_hue2_decay", false, nullptr, 3, L"How fast the field falls back toward its base. Too low and the second hue spreads into a flat tint over minutes" },
        { L"  second hue wobble (deg)",         0,    60,   1.0f,  0, &c.acid.filmHue2Wobble, nullptr, L"liquid_acid", L"film_hue2_wobble", false, nullptr, 3, L"The contrast hue is not nailed to one angle: it wanders this many degrees either side of it, so a 180 deg pair lives in 170-190 and the combo keeps changing. Two sines in the golden ratio, so it never repeats" },
        { L"  wobble period (s)",              10,  1800,  10.0f,  0, &c.acid.filmHue2WobbleP, nullptr, L"liquid_acid", L"film_hue2_wobble_period", false, nullptr, 3, L"Seconds of the slower of the two wobble sines. It also re-aims on the rig's readjustment, with the lamp and the focus" },
        { L"  seed rows below screen",          0,     8,   1.0f,  0, &c.acid.filmHue2SeedRows, nullptr, L"liquid_acid", L"film_hue2_seed_rows", false, nullptr, 3, L"New patch colour is generated only in this many hidden rows BELOW the bottom edge and rises into view. 0 = the old behaviour, which laid new patches anywhere in the frame" },
        { L"  patch rise (screens/min)",        0,     2,   0.05f, 2, &c.acid.filmHue2Rise, nullptr, L"liquid_acid", L"film_hue2_rise", false, nullptr, 3, L"Extra upward drift of the patches on top of the oil's own rise. Patches can never travel down the screen" },
        { L"Third film hue (deg)",           -180,   180,   5.0f,  0, &c.acid.filmHue3, nullptr, L"liquid_acid", L"film_hue3", false, nullptr, 3, L"An optional third hue, taken off the other end of the SAME patch field so it costs nothing extra per pixel" },
        { L"  third hue amount",                0,     1,   0.05f, 2, &c.acid.filmHue3Amt, nullptr, L"liquid_acid", L"film_hue3_amt", false, nullptr, 3, L"0 = off, which is the default" },
        { L"  seam reflect reach",              0, 0.60f,  0.01f, 2, &c.acid.boundaryReflectR, nullptr, L"liquid_acid", L"boundary_reflect_r", false, nullptr, 3, L"How far a hue2 SEAM reaches into the droplets around it, as a fraction of the screen HEIGHT. 0 = as before: a droplet carries the seam's colour only while it is standing in the seam, about one droplet across. Raised, the lit rims, lens highlights and glow of droplets this far away rotate toward the seam's own hue, fading smoothly so the nearest stay strongest. The film itself never changes and nothing is blurred" },
        { L"  seam reflect amount",             0,     1,   0.05f, 2, &c.acid.boundaryReflectAmt, nullptr, L"liquid_acid", L"boundary_reflect_amt", false, nullptr, 3, L"How much of the seam's own hue a rim right beside it takes. 1 = the full seam colour. Only does anything where the reach above is above 0" },
        { L"  crust hue mix",                   0,     1,   0.05f, 2, &c.acid.crustHueMix, nullptr, L"liquid_acid", L"crust_hue_mix", false, nullptr, 3, L"How much of the hue shift the droplets INSIDE a dark mass take. 1 = the same as the film, which is the reference's cyan-lit specks in the black" },
        { L"Bubble crust on the masses",        0,     1,   0.05f, 2, &c.acid.dropletMassBias, nullptr, L"liquid_acid", L"droplet_mass_bias", false, nullptr, 3, L"How much crust of small bubbles the dye masses carry, as a share of the film population. The film keeps everything it has -- this is added, not swapped" },
        { L"  crust density (x film)",          1,     4,   0.1f,  2, &c.acid.dropletCrustDens, nullptr, L"liquid_acid", L"droplet_crust_density", false, nullptr, 3, L"How dense the crust inside a mass is against the open film. 1 = no crust at all" },
        { L"  crust bubble size",            0.05f,    1,   0.05f, 2, &c.acid.dropletCrustR, nullptr, L"liquid_acid", L"droplet_crust_r", false, nullptr, 3, L"Crust bubbles are small: droplet_r_max times this. They clump but never coalesce" },
        { L"  mass rim (lamp side)",            0,     1,   0.05f, 2, &c.acid.massRim, nullptr, L"liquid_acid", L"mass_rim", false, nullptr, 3, L"A thin bright refracted rim on the lamp side of a mass edge, so a mass reads as a translucent body instead of a flat black cut-out" },
        { L"Cast shadows",                      0,     1,   0.02f, 2, &c.acid.shadowAmt, nullptr, L"liquid_acid", L"shadow_amt", false, nullptr, 3, L"The lamp finally blocks: every mass and every droplet darkens the film on the side away from it. 0 = the flat, shadowless frame this look had until now" },
        { L"  shadow length (frac of height)",   0, 0.30f,  0.01f, 2, &c.acid.shadowLen, nullptr, L"liquid_acid", L"shadow_len", false, nullptr, 3, L"How far the longest shadow reaches, as a fraction of the screen height. A caster throws less than this in proportion to its own height, and Lamp z stretches it further" },
        { L"  shadow softness",                 0,     1,   0.05f, 2, &c.acid.shadowSoft, nullptr, L"liquid_acid", L"shadow_soft", false, nullptr, 3, L"How fast the penumbra opens up with distance. 0 keeps the shadow as sharp at its tip as at the caster; 1 has it dissolve" },
        { L"Racing micro-bubbles (share)",      0,   0.5f,  0.01f, 2, &c.acid.dropletRacerFrac, nullptr, L"liquid_acid", L"droplet_racer_frac", false, nullptr, 3, L"Share of the smallest trapped droplets that race upward, the way uber-small bubbles do in water (0 = off)" },
        { L"  racer speed (x)",                 1,     5,    0.1f,  2, &c.acid.dropletRacerSpeed, nullptr, L"liquid_acid", L"droplet_racer_speed", false, nullptr, 3, L"How many times an ordinary droplet's climb a racer makes" },
        { L"  racer wobble",                    0,     2,    0.05f, 2, &c.acid.dropletRacerWobble, nullptr, L"liquid_acid", L"droplet_racer_wobble", false, nullptr, 3, L"Lateral zigzag / spiral on the way up, as a fraction of the climb" },
        { L"  racer max radius",                0, 0.01f, 0.0005f,4, &c.acid.dropletRacerRMax, nullptr, L"liquid_acid", L"droplet_racer_r_max", false, nullptr, 3, L"Only droplets at or below this radius race (0 = twice droplet_r_min)" },
        { L"Big hollow bubbles (size x)",       1,     6,    0.1f,  2, &c.acid.dropletRingRMul, nullptr, L"liquid_acid", L"droplet_ring_r_mul", false, nullptr, 3, L"How many times droplet_r_max a BIG hollow ring may be (1 = today: rings are limited to the solid droplets' size range)" },
        { L"  big-ring share",                  0,   0.3f,  0.005f,3, &c.acid.dropletRingBigFrac, nullptr, L"liquid_acid", L"droplet_ring_big_frac", false, nullptr, 3, L"Share of new rings that are big -- keep it small, a couple visible at a time is the point" },
        { L"Coalescence (touching drops merge)", 0,  1,    0.05f, 2, &c.acid.dropletCoalesce, nullptr, L"liquid_acid", L"droplet_coalesce", false, nullptr, 3, L"Same-kind solid droplets that touch pour into one round droplet of the combined area instead of parking as a lumpy cluster of lobes (rings keep their foam walls)" },
        { L"  neck close (s)",                  0,     4,    0.1f,  2, &c.acid.dropletCoalesceS, nullptr, L"liquid_acid", L"droplet_coalesce_s", false, nullptr, 3, L"Seconds the coalescence takes (0 = the old 0.18 s snap)" },
        { L"Conservation of mass",              0,     1,    1.00f, 0, &c.acid.conserveMass, nullptr, L"liquid_acid", L"conserve_mass", false, nullptr, 3, L"Nothing appears or vanishes at a visible size in the open: droplets enter and leave across the frame edges, blobs wrap only once their field is clear of the frame, and births swell from below the visible floor" },
        { L"  spawn grow (s)",                  0,    20,    0.5f,  1, &c.acid.spawnGrowS, nullptr, L"liquid_acid", L"spawn_grow_s", false, nullptr, 3, L"Seconds a new droplet takes to swell from nothing to full size (0 = the old 0.34 s pop)" },
        { L"  dissolve (s)",                    0,    20,    0.5f,  1, &c.acid.dissolveS, nullptr, L"liquid_acid", L"dissolve_s", false, nullptr, 3, L"Seconds a droplet dying of old age takes to shrink away (0 = the old 0.34 s)" },
        { L"Ring out-of-round",                 0,     1,    0.05f, 2, &c.acid.dropletRingWobble, nullptr, L"liquid_acid", L"droplet_ring_wobble", false, nullptr, 3, L"Stops a ring being a perfect circle: a seeded ellipse whose axis turns over a minute or two, plus a breathing 3-lobe wobble, and a wall that thins where the ring bulges. 0 = perfect circles" },
        { L"Droplet depth spread",              0,     1,    0.05f, 2, &c.acid.dropletDepth,  nullptr, L"liquid_acid", L"droplet_depth", false, nullptr, 3, L"How far droplets and rings are spread in front of and behind the masses' own plane. 0 = everything at one depth, which is what the camera saw before. Needs [post] dof_max_px to be visible" },
        { L"Back layer rise bonus",             0,     1,    0.05f, 2, &c.acid.depthRise,     nullptr, L"liquid_acid", L"depth_rise", false, nullptr, 3, L"The far layer climbs faster than the near one, so depth reads in the motion as well as in the blur. Symmetric, so the average rise speed is unchanged" },
        { L"Diffraction (size vs the point spread)", 0, 1, 0.05f, 2, &c.acid.diffraction,   nullptr, L"liquid_acid", L"diffraction", false, nullptr, 3, L"Light bends round anything narrow, so a feature smaller than the lens's point spread cannot reach full darkness: tiny droplets come out as soft grey dots, medium ones dark with soft edges, only the big masses go truly black. Never lifts the black under a mass" },
        { L"Diffraction width (px at 1440p)",   0.2f,  8,   0.1f,  2, &c.acid.diffractionPx, nullptr, L"liquid_acid", L"diffraction_px", false, nullptr, 3, L"How wide that point spread is. A droplet about this radius comes out at two thirds of its full darkness; anything several times it is unaffected" },
        { L"Droplet lens shading",              0,     1,   0.05f, 2, &c.acid.dropletLens,      nullptr, L"liquid_acid", L"droplet_lens", false, nullptr, 3, L"Every droplet is a lens over the backlight: a thin bright refractive rim hugging its edge, a darker band just inside, a lighter middle, and a small specular. Crisp at the edge, gradual inside -- it adds no blur at all. 0 = flat fills" },
        { L"Lens centre lift",                  0,     1,   0.05f, 2, &c.acid.dropletLensCentre,nullptr, L"liquid_acid", L"droplet_lens_centre", false, nullptr, 3, L"How much lighter the middle of a droplet is than its shoulder. A hole lifts toward the film colour, an oil droplet toward the backlight. Dies out where the surface stops being curved, so a big mass stays flat and its black stays black" },
        { L"Lens band width (px at 1440p)",   0.5f,   12,   0.25f, 2, &c.acid.dropletLensBand,  nullptr, L"liquid_acid", L"droplet_lens_band", false, nullptr, 3, L"Width of the dark inner band and of the bright rim just outside it. Authored in pixels and floored by band_min, never scaled by the element, so a 4-px droplet carries the same crisp rim a big mass does" },
        { L"Droplet specular",                  0,     1,   0.05f, 2, &c.acid.dropletSpec,      nullptr, L"liquid_acid", L"droplet_spec", false, nullptr, 3, L"A small highlight on the side of each droplet facing the lamp. It reads the same rig the haze and the bloom do, so it swings when the lamp moves instead of sitting still" },
        { L"Dye layer depth",                   0,     1,   0.02f, 2, &c.acid.dyeDepth,     nullptr, L"liquid_acid", L"dye_depth", false, nullptr, 3, L"Where the dye masses float. This used to be pinned to the focus distance, so a big mass sat on the plane of focus by definition and could never go soft on its own. Move it off Focus depth and the masses leave focus like everything else" },
        { L"Dye layer slope",                   0,     1,   0.02f, 2, &c.acid.dyeDepthTilt, nullptr, L"liquid_acid", L"dye_depth_tilt", false, nullptr, 3, L"Tips the dye layer along the direction of the lamp, so it is not parallel to the focus surface: the two cross on a line instead of agreeing over a whole region, and that line travels as the lamp drifts" },
        { L"Dye depth weight",               0.05f,    2,   0.05f, 2, &c.acid.dyeDepthW,    nullptr, L"liquid_acid", L"dye_depth_w", false, nullptr, 3, L"How much the dye layer counts against the droplets where they overlap. Higher pulls a droplet's focus toward the film it sits in; 0.25 is the original blend" },
        { L"Dye hue (deg)",                      0,   360,   5,     0, &c.acid.dyeHue,       nullptr, L"liquid_acid", L"dye_hue", false, nullptr, 3, L"The colour the dark masses take. With Dye follows film on, this is an OFFSET from the film's own hue, so the two turn together; with it off it is an absolute hue. 0..360 covers the circle either way" },
        { L"Dye saturation",                    0,     1,   0.05f, 2, &c.acid.dyeSat,       nullptr, L"liquid_acid", L"dye_sat", false, nullptr, 3, L"How coloured the dark masses are. The full 0..1 is useful here because the dye sits at a very low value: even fully saturated it reads as a deep wax, not as a bright fill. 0 turns the dye OFF (today's neutral black), it is not a grey dye" },
        { L"Dye brightness",                    0,  0.50f, 0.02f, 2, &c.acid.dyeLum,       nullptr, L"liquid_acid", L"dye_lum", false, nullptr, 3, L"How much lamp the THIN edge of a mass passes; the thick core keeps about 40% of it, which is what reads as translucent wax. Range from the dye4 sheet: under ~0.10 the mass is still black, 0.28-0.34 is the deep wax, and past ~0.45 the mass stops reading as dark at all. 0 = today's black" },
        { L"Dye follows film hue",              0,     1,   1,     0, nullptr, &c.acid.dyeHueFollow, L"liquid_acid", L"dye_hue_follow", false, nullptr, 3, L"1 = Dye hue is an offset from the film's current hue, so the dye rotates with it under hue_rotate_period and the sweep and the pair stays designed. 0 = a fixed absolute hue" },
        { L"Dye on masses",                     0,     1,   0.05f, 2, &c.acid.dyeMasses,    nullptr, L"liquid_acid", L"dye_masses", false, nullptr, 3, L"How much dye the big masses (the gaps between oil blobs) take. 1 = today; 0 = masses pitch black while the droplets can still carry a dye of their own" },
        { L"Dye on droplets",                   0,     1,   0.05f, 2, &c.acid.dyeDroplets,  nullptr, L"liquid_acid", L"dye_droplets", false, nullptr, 3, L"How much dye the small droplet holes take. 1 = today (every droplet dyed like a mass, which reads 'bubbly'); 0 = droplets stay black and only the big masses carry colour. A droplet within a few px of a mass shares the mass colour (more with Dye smoke)" },
        { L"Droplet dye hue (deg)",            -5,   360,   5,     0, &c.acid.dyeDropHue,   nullptr, L"liquid_acid", L"dye_droplet_hue", false, nullptr, 3, L"The droplets' own dye hue, so masses and droplets can be two colours (main + accent). Below 0 = same as Dye hue. Follows the film like Dye hue when Dye follows film is on" },
        { L"Droplet dye saturation",        -0.05f,    1,   0.05f, 2, &c.acid.dyeDropSat,   nullptr, L"liquid_acid", L"dye_droplet_sat", false, nullptr, 3, L"The droplets' own dye saturation. Below 0 = same as Dye saturation; 0 = droplets black" },
        { L"Droplet dye brightness",        -0.02f, 0.50f,  0.02f, 2, &c.acid.dyeDropLum,   nullptr, L"liquid_acid", L"dye_droplet_lum", false, nullptr, 3, L"The droplets' own dye level, same scale as Dye brightness. Below 0 = same as Dye brightness; 0 = droplets black" },
        { L"Dye smoke",                         0,     1,   0.05f, 2, &c.acid.dyeSmoke,     nullptr, L"liquid_acid", L"dye_smoke", false, nullptr, 3, L"0 = today's wax: the dye stops hard at the mass edge and is brightest at the thin rim. Up = the dye thickens gradually into the mass, leaks a little out under the thin film and breaks into slow wisps, so it reads as smoke in the body rather than a tinted disc. Masses only; droplets keep their own dye. Needs Dye brightness > 0" },
        { L"Interface speckle",                 0,     1,    0.02f, 2, &c.acid.speckle,       nullptr, L"liquid_acid", L"speckle", false, nullptr, 3, L"Sparse dark dots hugging the oil/ink boundary" },
        { L"Oil HDR level (0 = follow ink)",    0,     1.4f, 0.02f, 2, &c.acid.oilHdr,        nullptr, L"liquid_acid", L"oil_hdr", false, nullptr, 3, L"Drives the HDR highlight gain for oil pixels. 0 = inherit the ink's" },
        { L"Ink under the oil (0 bands 1 water)",0,    1,    1,     0, nullptr, &c.acid.inkMode,     L"liquid_acid", L"ink_water", false, nullptr, 3, L"1 = the shared ink-in-water render (translucent veils) instead of flat bands" },
        // --- "Ink in water" render look; inert unless [look] style=ink -------
        { L"Ink density k",                     0.5f,  8,    0.1f,  1, &c.ink.density,        nullptr, L"ink", L"density", false, L"Ink in water", 3, L"Beer-Lambert absorption. High = thin veils already opaque" },
        { L"Ink chroma (0 = neutral black)",    0,     3,    0.05f, 2, &c.ink.chroma,         nullptr, L"ink", L"chroma", false, nullptr, 3, L"How much the dye's own hue tints the transmitted light" },
        { L"Edge darkening (folds)",            0,     1,    0.02f, 2, &c.ink.edgeStrength,   nullptr, L"ink", L"edge_strength", false, nullptr, 3, L"Extra optical path where the density gradient is steep = sheets seen edge-on" },
        { L"Edge threshold (lo)",               0,     0.5f, 0.005f,3, &c.ink.edgeLo,         nullptr, L"ink", L"edge_lo", false, nullptr, 3, L"Gradient magnitude the fold darkening starts at" },
        { L"Edge threshold (hi)",               0,     0.5f, 0.005f,3, &c.ink.edgeHi,         nullptr, L"ink", L"edge_hi", false, nullptr, 3, L"Gradient magnitude the fold darkening saturates at" },
        { L"Edge tap spacing (px)",             1,     6,    0.5f,  1, &c.ink.edgeScale,      nullptr, L"ink", L"edge_scale", false, nullptr, 3, L"Screen texels between the gradient taps" },
        { L"Paper vignette",                    0,     0.6f, 0.02f, 2, &c.ink.vignette,       nullptr, L"ink", L"vignette", false, nullptr, 3, L"Radial darkening of the backlit paper (paper mode only)" },
        { L"Core knee (inverted)",              0,     1,    0.02f, 2, &c.ink.coreKnee,       nullptr, L"ink", L"core_knee", false, nullptr, 3, L"Opacity where the tint crosses from the thin-veil colour to the core colour" },
        { L"Veil floor (inverted)",             0,     0.6f, 0.02f, 2, &c.ink.veilFloor,       nullptr, L"ink", L"veil_floor", false, nullptr, 3, L"Faint dye below this opacity goes to the background instead of a haze" },
        { L"Midpoint dip to dark",              0,     1,    0.05f, 2, &c.ink.tintMidDip,       nullptr, L"ink", L"tint_mid_dip", false, nullptr, 3, L"Darken the duotone blend around its midpoint so mid-density ink is dark, not grey-brown" },
        { L"Duotone pair rotation (s, 0=off)", 0,   900,  10,    0, &c.ink.pairSweepPeriod, nullptr, L"ink", L"pair_sweep_period", false, nullptr, 3, L"Cross-fade tint_thin/tint_thick through the curated complementary pairs. 0 = the fixed pair" },
        { L"HDR core (inverted)",               0,     1.4f, 0.02f, 2, &c.ink.hdrCore,        nullptr, L"ink", L"hdr_core", false, nullptr, 3, L"How hard dense cores drive the HDR highlight gain. Veils stay SDR" },
        { L"HDR motion gate: still (texels/s)",0,     60,   1,     0, &c.ink.motionLo,       nullptr, L"ink", L"motion_lo", false, nullptr, 3, L"Below this local speed, ink gets no HDR lift at all - keeps the entry patch from blowing out" },
        { L"HDR motion gate: moving",          0,     200,  5,     0, &c.ink.motionHi,       nullptr, L"ink", L"motion_hi", false, nullptr, 3, L"Above this local speed the HDR lift is full. Set at or below the still value to disable the gate" },
        { L"Motion gate on opacity",            0,     1,    0.02f, 2, &c.ink.motionOpacity,  nullptr, L"ink", L"motion_opacity", false, nullptr, 3, L"How much the motion gate also thins stationary ink. 0 = HDR lift only" },
        { L"Parallax second layer",             0,     1,    0.02f, 2, &c.ink.parallax,       nullptr, L"ink", L"parallax", false, nullptr, 3, L"Adds the same dye at another scale as extra depth. 0 = off" },
        { L"Parallax scale",                    0.8f,  1,    0.005f,3, &c.ink.parallaxScale,  nullptr, L"ink", L"parallax_scale", false, nullptr, 3, L"How much bigger the second layer reads" },
        // --- ink drops (any look) -------------------------------------------
        { L"Interval (s)",                      2,     90,   1,     0, &c.drops.interval,     nullptr, L"drops", L"interval", false, L"Drops", 3, L"Seconds between ink drops (jittered +-35%)" },
        { L"Entry speed (downward)",            0,     3000, 25,    0, &c.drops.speed,        nullptr, L"drops", L"speed", false, nullptr, 3, L"Downward velocity impulse. This is what rolls the head into a cap" },
        { L"Drop radius (%)",                   0.02f, 2,    0.01f, 2, &c.drops.radius,       nullptr, L"drops", L"radius", false, nullptr, 3, L"Splat radius of the drop head, same units as splat radius" },
        { L"Drop density",                      0.1f,  4,    0.05f, 2, &c.drops.density,      nullptr, L"drops", L"density", false, nullptr, 3, L"Dye intensity of the head. 1.35 = a fully opaque core" },
        { L"Entry trail (s)",                   0,     4,    0.1f,  1, &c.drops.tailSec,      nullptr, L"drops", L"tail_sec", false, nullptr, 3, L"How long dye keeps feeding in at the entry point after the impulse" },
        { L"Entry trail density",               0,     1,    0.02f, 2, &c.drops.tailDensity,  nullptr, L"drops", L"tail_density", false, nullptr, 3, L"Intensity of that trail" },
        { L"Splash droplets",                   0,     12,   1,     0, nullptr, &c.drops.spatter,    L"drops", L"spatter", false, nullptr, 3, L"Satellite droplets around the entry (ref 2). Each one costs a full dye pass" },
        { L"Splash radius (%)",                 0.01f, 0.4f, 0.005f,3, &c.drops.spatterRadius,nullptr, L"drops", L"spatter_radius", false, nullptr, 3, L"Size of each satellite droplet" },
        { L"Splash speed",                      0,     2000, 25,    0, &c.drops.spatterSpeed, nullptr, L"drops", L"spatter_speed", false, nullptr, 3, L"Outward impulse of the satellites" },
        { L"Entry stream width (x drop radius)",0.05f, 1,    0.01f, 2, &c.drops.tailRadiusFrac,nullptr, L"drops", L"tail_radius_frac", false, nullptr, 3, L"Keep this small: a wide entry stamp reads as a bright orb parked at the injection point" },
        { L"Entry stream pull (x drop speed)",  0,     1,    0.02f, 2, &c.drops.tailSpeed,    nullptr, L"drops", L"tail_speed", false, nullptr, 3, L"Downward impulse on the entry stream so it feeds the stem instead of parking" },
        { L"Impulse spread (x drop radius)",    0.5f,  4,    0.05f, 2, &c.drops.impulseSpread,nullptr, L"drops", L"impulse_spread", false, nullptr, 3, L"How much wider the velocity impulse is than the dye stamp. Below ~1.5 the dye's outer halo parks at the entry as a bright orb" },
        { L"Lobe asymmetry",                    0,     1,    0.02f, 2, &c.drops.asymmetry,    nullptr, L"drops", L"asymmetry", false, nullptr, 3, L"0 = a textbook symmetric vortex pair. Higher = unequal lobes, one side leading" },
        { L"Entry band bottom (uv y)",          0.02f, 0.9f, 0.01f, 2, &c.drops.yMax,         nullptr, L"drops", L"y_max", false, nullptr, 3, L"How far down the screen a drop may enter" },
        // --- screen mirroring / kaleidoscope (display only, any look) -------
        { L"Mode (0 off 1 horiz 2 vert 3 quad 4 kaleido)", 0, 4, 1, 0, nullptr, &c.mirror.mode, L"mirror", L"mode", false, L"Mirror", 3, L"Folds the picture about a centre. 3 = the 4-fold quad; 4 = wedges around the centre" },
        { L"Kaleidoscope segments",             2,    16,   1,     0, nullptr, &c.mirror.segments, L"mirror", L"segments", false, nullptr, 3, L"Number of wedges in mode 4. Every other one is reflected, so there is no jump at a wedge edge" },
        { L"Shown quadrant (0-3)",              0,     3,   1,     0, nullptr, &c.mirror.source,   L"mirror", L"source", false, nullptr, 3, L"Which quarter of the sim is the one you see, and is copied around: +1 = right half, +2 = bottom half" },
        { L"Fold centre x",                     0.1f, 0.9f, 0.01f, 2, &c.mirror.centerX,     nullptr, L"mirror", L"center_x", false, nullptr, 3, L"Where the vertical seam sits" },
        { L"Fold centre y",                     0.1f, 0.9f, 0.01f, 2, &c.mirror.centerY,     nullptr, L"mirror", L"center_y", false, nullptr, 3, L"Where the horizontal seam sits" },
        { L"Rotation (s per turn, 0=fixed)",    0,   600,   5,     0, &c.mirror.rotatePeriod, nullptr, L"mirror", L"rotate_period", false, nullptr, 3, L"Slowly turns the kaleidoscope's fold axes about the centre" },
        { L"Centre drift",                      0,     1,   0.02f, 2, &c.mirror.drift,       nullptr, L"mirror", L"drift", false, nullptr, 3, L"Slow wander of the fold point, so the seam is not glued to the middle of the screen" },
        { L"Seam softness (uv)",                0,  0.05f, 0.002f, 3, &c.mirror.soft,        nullptr, L"mirror", L"soft", false, nullptr, 3, L"Rounds the fold off over this band so the mirror crease is not a hard line. 0 = a hard mirror" },
        // --- [post]: the final composite trim, for EVERY look ---------------
        { L"Film grain",                        0,     1,   0.01f, 3, &c.post.filmGrain,     nullptr, L"post", L"film_grain", false, L"Post", 3, L"Animated film grain over the finished frame, weighted into the mids and darks and kept off the peaks and off true black" },
        { L"Film grain size (px)",              0.5f,  6,   0.25f, 2, &c.post.filmGrainSize, nullptr, L"post", L"film_grain_size", false, nullptr, 3, L"Pixels per grain cell. 1 = per-pixel noise, larger = coarser stock" },
        { L"Film grain speed",                  0,     2,   0.05f, 2, &c.post.filmGrainSpeed,nullptr, L"post", L"film_grain_speed", false, nullptr, 3, L"Multiplier on the grain frame rate; 1 = the film_grain_fps rate, lower holds each pattern longer" },
        { L"Film grain frame rate (fps)",       1,   240,   1,     0, &c.post.filmGrainFps,  nullptr, L"post", L"film_grain_fps", false, nullptr, 3, L"How many grain patterns per second: 24 or 30 for a film cadence (both divide 240 exactly), 240 = every refresh. Also paces film_noise" },
        { L"Film grain colour (0 mono)",        0,     1,   0.05f, 2, &c.post.filmGrainColor,nullptr, L"post", L"film_grain_color", false, nullptr, 3, L"0 = monochrome grain, 1 = independent RGB noise" },
        { L"Film grain chroma (1 = old)",        0,     1,   0.05f, 2, &c.post.filmGrainChroma,nullptr, L"post", L"film_grain_chroma", false, nullptr, 3, L"1 = the grain is an equal step on all three channels, which moves saturation and clips against black. 0 = it scales the pixel instead, so hue and saturation survive and black stays black" },
        { L"Film grain density curve",           0,     1,   0.05f, 2, &c.post.filmGrainDensity,nullptr, L"post", L"film_grain_density", false, nullptr, 3, L"0 = grain at full amplitude from just above black upward. 1 = the stock own density curve: nothing in the dense shadow, most of it in the mid-tones, nothing on a clean highlight" },
        { L"Lateral aberration",                0,     1,   0.02f, 2, &c.post.aberration,    nullptr, L"post", L"aberration", false, nullptr, 3, L"A lens focuses red and blue at slightly different magnifications, so the channels land at different scales: red pushed out from the optical axis, blue pulled in. Nothing at the axis, a couple of pixels at the corners. Centred on the rig's lens, which drifts, so the clean spot never sits still" },
        { L"Aberration width (px at 1440p)",    0,     6,   0.1f,  2, &c.post.aberrationPx,  nullptr, L"post", L"aberration_px", false, nullptr, 3, L"How far red and blue are displaced radially at the corners, in pixels at 1440p. Whether the fringe fades on defocused shapes is `aberration_coc`" },
        { L"Aberration growth to field edge",   0,     2,   0.1f,  2, &c.post.aberrationField,nullptr, L"post", L"aberration_field", false, nullptr, 3, L"How fast the split grows from the optical axis outward. 0 = nearly uniform across the frame, 2 = clean in the middle and all of it in the corners" },
        { L"Aberration fades with defocus",     0,     1,   0.05f, 2, &c.post.aberrationCoc, nullptr, L"post", L"aberration_coc", false, nullptr, 3, L"0 = the same split everywhere, in focus or not. 1 = it fades with this pixel own blur, so the fringe lives on the sharp slice and disappears on an out-of-focus shape, which is what a real lens does" },
        { L"Vignette",                          0,     1,   0.02f, 2, &c.post.vignette,      nullptr, L"post", L"vignette", false, nullptr, 3, L"A gentle fall-off toward the corners -- a field stop, never a circle" },
        { L"Lens softness (px at 1440p)",       0,     6,   0.1f,  2, &c.post.softness,      nullptr, L"post", L"softness", false, nullptr, 3, L"Defocuses the isolines themselves -- coverage band, film edge, rim and meniscus -- so no edge in the frame is razor-sharp. The grain stays sharp" },
        { L"Bright-field halo",                 0,     1,   0.01f, 3, &c.post.halo,          nullptr, L"post", L"halo", false, nullptr, 3, L"A soft bright glow hugging the outside of every dark shape, with a faint darker echo beyond it -- the microscope double contour (liquid_acid only)" },
        { L"Halo width (px at 1440p)",          1,    40,   1,     0, &c.post.haloPx,        nullptr, L"post", L"halo_px", false, nullptr, 3, L"How wide that glow is. Wide and weak is the look; narrow and strong is a stroked line" },
        { L"Minimum band widths",               0,     1,   0.05f, 2, &c.post.bandMin,       nullptr, L"post", L"band_min", false, nullptr, 3, L"Floors every band around an edge (film edge, rim, meniscus, halo, penumbra) at a few px, so a small droplet is shaded like a big mass instead of getting a solid outline. 0 = bands proportional to each element's size" },
        { L"Camera defocus (px at 1440p)",      0,     4,   0.1f,  2, &c.post.postBlurPx,    nullptr, L"post", L"post_blur_px", false, nullptr, 3, L"Image-space disc blur of the finished frame: every feature, whatever its size, gets the same lens defocus. 0 = off" },
        { L"Camera glare",                     0,     1,   0.02f, 2, &c.post.postGlow,      nullptr, L"post", L"post_glow", false, nullptr, 3, L"Weak wide veiling glare: the frame mixed with a wide blur of itself, so dark bleeds a little into bright and bright into dark around every edge, including a thin ring wall. 0 = off" },
        { L"Camera glare radius (px at 1440p)", 2,    40,   0.5f,  1, &c.post.postGlowPx,    nullptr, L"post", L"post_glow_px", false, nullptr, 3, L"How far the glare spreads" },
        { L"Light in the water: haze",          0,     1,   0.02f, 2, &c.post.fog,           nullptr, L"post", L"fog", false, nullptr, 3, L"The water itself glows near the off-view lamp and fades with distance, added only into the dark. It falls to exactly zero far from the lamp, so black stays black. 0 = off" },
        { L"Haze reach (px at 1440p)",        100,  2000,  25,     0, &c.post.fogPx,         nullptr, L"post", L"fog_px", false, nullptr, 3, L"How far the glow of the water carries from the lamp" },
        { L"Haze: keep masses black",          0,     1,   0.05f, 2, &c.post.fogMassGate,   nullptr, L"post", L"fog_mass_gate", false, nullptr, 3, L"0 = the haze lands wherever it is dark. 1 = it is kept out of the inside of a dark mass, which floats in front of the water, and still glows in the water beside it" },
        { L"Masses: artefacts follow colour",   0,     1,   0.05f, 2, &c.post.artefactLumGate, nullptr, L"post", L"artefact_lum_gate", false, nullptr, 3, L"0 = grain, lens fringe and the cover's sheen land on every mass alike. 1 = inside a mass they follow its own brightness and colour: a pure black mass stays clean, a dyed one keeps its texture, the edge band is untouched" },
        { L"Bloom (wide, weak)",                0,     1,   0.02f, 2, &c.post.bloom,         nullptr, L"post", L"bloom", false, nullptr, 3, L"The bright film bleeds a very wide, very weak wash into the black. Its radius breathes and the wash drifts with the lamp" },
        { L"Bloom radius (px at 1440p)",       40,   400,   5,     0, &c.post.bloomPx,       nullptr, L"post", L"bloom_px", false, nullptr, 3, L"How far that wash spreads" },
        { L"  bloom warmth (lamp colour)",      0,     1,   0.05f, 2, &c.post.bloomWarmth,   nullptr, L"post", L"bloom_warmth", false, nullptr, 3, L"Brief BN. 0 = the wash is the colour of the film it came from. 1 = it takes the lamp's colour: hot yellow-white on the lamp's side, red away from it (the LAPD microscope veil). Luminance unchanged, so it does not brighten the frame" },
        { L"Lamp x",                           -1,     2,   0.02f, 2, &c.post.lightX,        nullptr, L"post", L"light_x", false, nullptr, 3, L"Where the off-view lamp sits, in screen coordinates. 0.5 = the middle, outside 0..1 = off-frame" },
        { L"Lamp y",                           -1,     2,   0.02f, 2, &c.post.lightY,        nullptr, L"post", L"light_y", false, nullptr, 3, L"1.2 = just below the bottom edge, which is where the reference lamp is" },
        { L"Lamp idle drift",                   0,     1,   0.05f, 2, &c.post.lightDrift,    nullptr, L"post", L"light_drift", false, nullptr, 3, L"How far the lamp wanders on its own: a sum of slow sines over seconds to a minute, so the light is never static" },
        { L"Lamp z (in front / behind)",       -1,     1,   0.05f, 2, &c.post.lightZ,        nullptr, L"post", L"light_z", false, nullptr, 3, L"How far the lamp stands off the plane of the dish. Above 0 it is in front of it and the cast shadows rake away from it, shortening as it rises; 0 is in the plane and throws the longest shadows; below 0 the lamp is behind the dish and every caster spills its shadow evenly onto the film in front of it" },
        { L"Film dust",                         0,     1,   0.02f, 2, &c.post.filmDust,      nullptr, L"post", L"film_dust", false, nullptr, 3, L"Specks of dust on the film: sparse bright points, a new scattering every film frame. Additive and weighted into the dark, so they are stars on the black and nothing on the bright film" },
        { L"Film hairs",                        0,     1,   0.02f, 2, &c.post.filmHairs,     nullptr, L"post", L"film_hairs", false, nullptr, 3, L"How often a curly hair is caught in the gate. Each one sticks for a few seconds, flutters, and is gone" },
        { L"Film scratches",                    0,     1,   0.02f, 2, &c.post.filmScratches, nullptr, L"post", L"film_scratches", false, nullptr, 3, L"Faint near-vertical scratches that persist for a stretch, drift sideways and disappear" },
        { L"Film light leak",                   0,     1,   0.02f, 2, &c.post.filmLeak,      nullptr, L"post", L"film_leak", false, nullptr, 3, L"A coloured leak at one edge -- warm core, cool fringe, soft bands -- that swells and dies, and does not come back every time" },
        { L"Film artefact rate (s)",            1,    30,   0.5f,  1, &c.post.filmArtefactRate, nullptr, L"post", L"film_artefact_rate", false, nullptr, 3, L"How long one population of hairs lasts. Scratches change three times slower, the leak five times slower, the dust every film frame" },
        { L"Film noise",                        0,     1,   0.01f, 3, &c.post.filmNoise,     nullptr, L"post", L"film_noise", false, nullptr, 3, L"A second, finer and faster noise layer under the coarse grain: the emulsion's own fizz as against the stock's grain structure" },
        { L"Film noise size (px at 1440p)",     0.5f,  4,   0.25f, 2, &c.post.filmNoiseSize, nullptr, L"post", L"film_noise_size", false, nullptr, 3, L"Cell size of that finer layer, in px at 1440p" },
        { L"Film stock colour",                 0,     1,   0.02f, 2, &c.post.filmStock,     nullptr, L"post", L"film_stock", false, nullptr, 3, L"The stock's own grade, applied last: lifted teal shadows, warm highlights, a slightly different curve per channel" },
        { L"Camera glare, dark bias",          0,     1,   0.05f, 2, &c.post.postGlowDark,  nullptr, L"post", L"post_glow_dark", false, nullptr, 3, L"Leans the glare toward the dark side: shadows of dark features bleed into the bright film more than the film's light bleeds into the black" },
        { L"Depth of field (max CoC px at 1440p)", 0,  16,   0.5f,  1, &c.post.dofMaxPx,      nullptr, L"post", L"dof_max_px", false, nullptr, 3, L"Turns the one global defocus into a real depth of field: each element is blurred by how far its own depth is from the plane of focus, up to this radius. 0 = off, and the whole per-pixel path is skipped. liquid_acid only" },
        { L"Focus depth",                       0,     1,   0.02f, 2, &c.post.cameraFocus,   nullptr, L"post", L"camera_focus", false, nullptr, 3, L"Which depth is sharp. 0.5 is the plane the big masses sit on; lower favours the front droplets, higher the back ones" },
        { L"Field curvature",                   0,     2,   0.05f, 2, &c.post.cameraFieldCurve, nullptr, L"post", L"camera_field_curve", false, nullptr, 3, L"Bends the surface of focus away from the dish with distance from the optical axis, so the centre and the corners of the frame cannot both be sharp -- what a real lens does" },
        { L"Corner warp",                       0,     1,   0.05f, 2, &c.post.cornerWarp,    nullptr, L"post", L"corner_warp", false, nullptr, 3, L"Brief BN. The microscope's field edge: outside the start radius the corners are stretched along the arcs round the centre, so they look oddly distorted while the middle stays exactly as it was. 0 = off" },
        { L"  corner warp start",             0.4f, 0.9f, 0.05f, 2, &c.post.cornerWarpR,   nullptr, L"post", L"corner_warp_r", false, nullptr, 3, L"Where the warp begins: 0.4 = just outside the middle 40% of the frame, 0.9 = only the very corners (1 = a corner)" },
        { L"Focus tilt",                        0,     2,   0.05f, 2, &c.post.focusTilt,     nullptr, L"post", L"focus_tilt", false, nullptr, 3, L"Slants the surface of focus (Lensbaby / freelensing): a strip of sharpness across the frame with the focus falling off smoothly to either side. 0 = level" },
        { L"Focus tilt angle (deg)",         -180,   180,   5,     0, &c.post.focusTiltAngle,nullptr, L"post", L"focus_tilt_angle", false, nullptr, 3, L"Which way that strip runs. With a readjustment period set, this is the angle it is re-aimed around" },
        { L"Sharp band width (px at 1440p)",   40,  1200,  20,     0, &c.post.focusBandPx,   nullptr, L"post", L"focus_band_px", false, nullptr, 3, L"How wide the sharp strip is on screen. Wide is a gentle depth of field, narrow is the freelensing sliver" },
        { L"Refocus period (s, 0 = never)",     0,   300,   5,     0, &c.post.focusTiltPeriod,nullptr, L"post", L"focus_tilt_period", false, nullptr, 3, L"Mean seconds the focus plane holds still before somebody re-tilts the lens. Randomised around this, so it never feels scheduled. 0 = the lens is bolted down" },
        { L"Refocus move (s)",               0.2f,     4,   0.1f,  2, &c.post.focusTiltMoveS,nullptr, L"post", L"focus_tilt_move_s", false, nullptr, 3, L"How long one readjustment takes. A second or two, eased, with a slight overshoot and settle -- a hand letting go of a lens barrel" },
        { L"Camera field of view (deg)",        0,    90,   2,     0, &c.post.cameraFov,     nullptr, L"post", L"camera_fov", false, nullptr, 3, L"0 = an orthographic scanner, every ring a symmetric circle. Above 0 the dish is seen from a lens at the tip of a view cone: off-axis rings foreshorten toward the axis, their far wall reads thicker and their highlight swings to the side facing the axis" },
        { L"Optical axis x",                    0,     1,   0.02f, 2, &c.post.cameraAxisX,   nullptr, L"post", L"camera_axis_x", false, nullptr, 3, L"Where the optical axis meets the dish: the one point seen face on, and the centre the field curvature and the tilt are measured from" },
        { L"Optical axis y",                    0,     1,   0.02f, 2, &c.post.cameraAxisY,   nullptr, L"post", L"camera_axis_y", false, nullptr, 3, L"The other half of that point" },
        { L"Point spread (px at 1440p)",         0,     6,   0.1f,  2, &c.post.psfPx,        nullptr, L"post", L"psf_px", false, nullptr, 3, L"The floor under the defocus radius, applied whatever the focus: no lens resolves a point to a point, so the sharpest thing in the frame is still this wide. At 1 px and up an in-focus edge can never come out as stair-stepped coverage AA" },
        { L"Output dither (LSB of 10 bit)",      0,     4,   0.25f, 2, &c.post.dither,       nullptr, L"post", L"dither", false, nullptr, 3, L"Half an LSB of ordered blue-ish noise on the finished frame, in the domain the panel quantises in. Breaks the 10-bit steps that show as bands on a big saturated flat. It fades out into true black, so an off pixel stays off. 1 = half an LSB" },
        { L"Halation",                          0,     1,   0.02f, 2, &c.post.halation,     nullptr, L"post", L"halation", false, nullptr, 3, L"CineStill's missing anti-halation layer: a tight warm glow bleeding out of the bright film into the dark around it. Taken only from what is genuinely bright, and it never lands on the highlight itself. Much tighter than Bloom, which is the wide weak wash" },
        { L"Halation radius (px at 1440p)",     4,    60,   1,     0, &c.post.halationPx,   nullptr, L"post", L"halation_px", false, nullptr, 3, L"How far the glow reaches. A dozen pixels is the film look; far more and it becomes a second bloom" },
        { L"Lid (cover glass)",                 0,     1,   0.05f, 2, &c.post.lid, nullptr, L"post", L"lid", false, nullptr, 3, L"Master for the transparent cover over the dish: its internal reflections, sheen, glint and iridescence. 0 = no cover at all" },
        { L"  lid ghosts",                      0,     1,   0.05f, 2, &c.post.lidGhost, nullptr, L"post", L"lid_ghost", false, nullptr, 3, L"Dim, offset, slightly magnified copies of the bright film reflected inside the cover, tinted by the coating they bounced off" },
        { L"  lid ghost spread",                0,     2,   0.05f, 2, &c.post.lidGhostSpread, nullptr, L"post", L"lid_ghost_spread", false, nullptr, 3, L"How far the ghost chain runs along the line from the lamp reflection through the optical centre" },
        { L"  lid ring ghosts",                 0,     1,   0.05f, 2, &c.post.lidRings, nullptr, L"post", L"lid_rings", false, nullptr, 3, L"Concentric coloured arcs -- the field reflected off a curved element (the LAPD optics look)" },
        { L"  lid sheen",                       0,     1,   0.05f, 2, &c.post.lidSheen, nullptr, L"post", L"lid_sheen", false, nullptr, 3, L"A very wide, very weak warm smear where the lamp catches the cover" },
        { L"  lid sheen width (px)",           40,  1400,  20.0f, 0, &c.post.lidSheenPx, nullptr, L"post", L"lid_sheen_px", false, nullptr, 3, L"Width of that smear, in px at 1440p" },
        { L"  lid glint",                       0,     1,   0.05f, 2, &c.post.lidGlint, nullptr, L"post", L"lid_glint", false, nullptr, 3, L"The lamp's own reflection in the cover: a soft core with a wide amber halo" },
        { L"  lid iridescence",                 0,     1,   0.05f, 2, &c.post.lidIris, nullptr, L"post", L"lid_iris", false, nullptr, 3, L"Interference colours of the thin oil film on the cover, visible only across the sheen" },
        { L"  lid refraction (px)",             0,    12,   0.5f,  1, &c.post.lidRefractPx, nullptr, L"post", L"lid_refract_px", false, nullptr, 3, L"How much the uneven cover wobbles its own reflections (the transmitted picture is left alone)" },
        { L"  lid scratches",                   0,     1,   0.05f, 2, &c.post.lidScratch, nullptr, L"post", L"lid_scratch", false, nullptr, 3, L"Wear on the cover: hairline scratches fixed to the lid that catch the lamp only where they run across its light, so the pattern shifts as the lamp drifts. Clear over the dark, nearly gone on the bright film. 0 = off (needs Lid above 0)" },
        { L"  lid scratch density",             0,     1,   0.05f, 2, &c.post.lidScratchDensity, nullptr, L"post", L"lid_scratch_density", false, nullptr, 3, L"How many fine scratches there are where the wear is" },
        { L"  lid scratch long gouges",         0,     1,   0.05f, 2, &c.post.lidScratchLen, nullptr, L"post", L"lid_scratch_len", false, nullptr, 3, L"0 = only short micro-swirls. Higher adds up to eight long straight gouges across the cover" },
        { L"  lid scratch corners",             0,     1,   0.05f, 2, &c.post.lidScratchCorner, nullptr, L"post", L"lid_scratch_corner", false, nullptr, 3, L"Where the wear sits: 0 = all over the cover, 1 = only in the corners" },
        { L"  lid scratch softness",            0,     1,   0.05f, 2, &c.post.lidScratchSoft, nullptr, L"post", L"lid_scratch_soft", false, nullptr, 3, L"0 = crisp 1 px hairlines that catch the light over a narrow angle; higher = wider, dimmer grooves that catch it over a wider one" },
        { L"  lid scratch tint",                0,     1,   0.05f, 2, &c.post.lidScratchTint, nullptr, L"post", L"lid_scratch_tint", false, nullptr, 3, L"0 = neutral white. 1 = the scratch takes the colour of the film under it (over black it stays white)" },
        { L"  glass streaks",                   0,     1,   0.05f, 2, &c.post.glassStreaks,  nullptr, L"post", L"glass_streaks", false, nullptr, 3, L"Brief BN. Two thin bright wavy lines on the cover glass (the LAPD sheet), brightest past the lamp's reflection. They ride the lid's drift and turn, so they never sit still. Needs Lid above 0. Not scaled by it" },
        { L"Halation warmth",                   0,     1,   0.05f, 2, &c.post.halationWarmth,nullptr, L"post", L"halation_warmth", false, nullptr, 3, L"0 = a colourless glow. 1 = the red-orange of light that has crossed the emulsion twice, which is the CineStill signature" },
        { L"  halation threshold (haze)",       0,     0.6f, 0.05f, 2, &c.post.halationThreshold, nullptr, L"post", L"halation_threshold", false, nullptr, 3, L"Brief BN. 0 = only the genuinely bright film halates. Higher lowers the knee so the mid-tones scatter too and halation becomes a diffusion haze (pro-mist): the picture stays sharp and is veiled, never blurred. Pair with a wider halation radius" },
        { L"Thermal shimmer",                    0,     1,   0.02f, 2, &c.post.shimmer,       nullptr, L"post", L"shimmer", false, nullptr, 3, L"The air above the lamp. A very fine refractive wobble, strongest near the light and fading to nothing away from it, and carried by the sim's own velocity -- a burst that shoves the oil shoves the heat above it too" },
        { L"Shimmer amount (px at 1440p)",       0,     6,   0.1f,  2, &c.post.shimmerPx,     nullptr, L"post", L"shimmer_px", false, nullptr, 3, L"How far the wobble displaces the picture at its strongest. A pixel or two is heat; more is water" },
        { L"Vignette wander",                    0,     1,   0.05f, 2, &c.post.vignetteWander,nullptr, L"post", L"vignette_wander", false, nullptr, 3, L"Lets the vignette's centre follow the rig's lens, so the darkest corner turns over minutes instead of sitting in one corner of the panel for hours. 0 = pinned to the middle" },
        { L"Pixel-shift orbit (px at 1440p)",    0,     8,   0.5f,  1, &c.post.pixelShiftPx,  nullptr, L"post", L"pixel_shift_px", false, nullptr, 3, L"The OLED safety net under everything else: the whole finished frame walks a slow closed orbit of this radius, a few thousandths of a pixel per frame, so no feature ever holds one pixel. Invisible, and still moving" },
        { L"Rig readjust reach",                 0,     1,   0.05f, 2, &c.post.rigReadjust,   nullptr, L"post", L"rig_readjust", false, nullptr, 3, L"How far the lamp and the lens centre re-aim when the focus readjusts. 0 = only focus and tilt move; above 0 the whole rig moves as one body on the same eased spring, which is the point of having a rig" },
    };
    s_checks = {
        { L"Auto wanderer splats",              &c.wanderers,        L"behavior", L"wanderers", L"Behaviors", 0, L"Autonomous roaming emitters" },
        { L"Auto-pause when screen full",       &c.autoPause,        L"behavior", L"auto_pause", nullptr, 0, L"Stop painting when the field is full" },
        { L"Separating dart while paused",      &c.dartEnabled,      L"behavior", L"dart_enabled", nullptr, 0, L"Periodic shot that splits merged blobs" },
        { L"Hue shift cycler",                  &c.hsEnabled,        L"behavior", L"hueshift_enabled", nullptr, 0, L"Palette rotation bursts (full-wheel moods only)" },
        { L"Idle random splats",                &c.idleSplats,       L"behavior", L"idle_splats", nullptr, 0, L"Random blobs when the field is calm" },
        { L"Shading",                           &c.shading,          L"sim",      L"shading", nullptr, 0, L"Pseudo-3D emboss on dye edges" },
        { L"Hold left mouse = pour dye",        &c.holdToSplat,      L"behavior", L"hold_to_splat", L"Mouse", 1, L"Hold the left button to pour a continuous dye stream" },
        { L"Splat on click (if not holding)",   &c.splatOnClick,     L"behavior", L"splat_on_click", nullptr, 1, L"Each click splats a single dye blob" },
        { L"Mouse movement stirs fluid",        &c.showMouse,        L"behavior", L"show_mouse", nullptr, 1, L"Moving the mouse pushes currents through the fluid" },
        { L"Random color (hue wheel)",          &c.colorful,         L"color",    L"colorful", L"Color source", 2, L"Emit from the hue band instead of the fixed palette" },
        { L"Use all 5 palette colors",          &c.moreColors,       L"color",    L"more_colors", nullptr, 2, L"Splats cycle all five palette colors instead of one" },
        { L"HDR compensation (sat/brightness)", &c.hdrCompensation,  L"hdr",      L"compensation", nullptr, 2, L"Apply HDR boosts when Windows HDR is on" },
        { L"Response curve (bright rims)",      &c.curveEnabled,     L"color",    L"curve_enabled", nullptr, 2, L"Enable the brightness hump curve" },
        { L"Pause on fullscreen app",           &g_pauseOnFullscreen,L"general",  L"pause_on_fullscreen", L"System", 3, L"Pause the wallpaper while a fullscreen app has focus" },
        { L"Pause on maximized app",            &g_pauseOnMaximized, L"general",  L"pause_on_maximized", nullptr, 3, L"Pause the wallpaper while a maximized window has focus" },
        { L"Mood cycling (auto-switch looks)",  &g_moodSettings.enabled, L"moods", L"enabled", nullptr, 3, L"Auto-switch between moods/*.ini recipes" },
        { L"Mirror on second monitor",          &c.mirrorSecond,     L"general",  L"mirror_second", nullptr, 3, L"Also render the wallpaper on the second monitor" },
        // Look switch. Writes [look] liquid_acid=0|1; the ini also accepts
        // [look] style=fluid|liquid_acid. LIVE since the display PSO variants
        // are compiled on demand (FluidRenderer::EnsureLookResources), called
        // from the checkbox handler below.
        { L"Lock ink opposite the oil hue",     &c.acid.inkComplementLock, L"liquid_acid", L"ink_complement_lock", nullptr, 3, L"Hold the ink's hue on the far side of the wheel from the oil" },
        { L"Rising blobs re-enter from below",  &c.acid.riseRespawn, L"liquid_acid", L"rise_respawn", nullptr, 3, L"With a rise speed set, a blob that climbs off the top comes back in under the bottom edge at a new place and size, instead of reappearing where it left" },
        { L"Paired rim (dark in, bright out)",  &c.acid.rimOrder,    L"liquid_acid", L"rim_order", nullptr, 3, L"Forces the dark rim and the bright halo adjacent and ordered across the isoline, instead of wherever rim_inset/meniscus_offset put them" },
        { L"Liquid Acid look",                  &c.acid.enabled,     L"look",     L"liquid_acid", nullptr, 3, L"Oil-on-inked-water render look. Switches live (first switch costs one shader compile)" },
        // Ink look. Same rule: the INK display PSO variant is compiled on
        // demand. "Inverted" is live (read per frame into InkCB).
        { L"Ink in water look",                 &c.ink.enabled,      L"look",     L"ink", nullptr, 3, L"Beer-Lambert ink-in-water render look. Switches live (first switch costs one shader compile)" },
        { L"Inverted (pale ink on black)",      &c.ink.inverted,     L"ink",      L"inverted", nullptr, 3, L"Off = dark ink on backlit paper. On = pale ink on black (OLED-friendly)" },
        { L"Ink drops",                         &c.drops.enabled,    L"drops",    L"drops", nullptr, 3, L"Periodic falling ink drops. Works with any render look" },
        { L"Drops obey the fullness governor",  &c.drops.obeyGovernor, L"drops",  L"obey_governor", nullptr, 3, L"Skip a drop while the water is already full of ink" },
        { L"Start with Windows",                nullptr,             nullptr,     nullptr, nullptr, 3, nullptr },
    };

    // page model: every header starts a new category page, in order
    s_pages.clear();
    s_sliderPage.clear();
    s_checkPage.clear();
    int cur = -1;
    for (size_t i = 0; i < s_sliders.size(); i++) {
        if (s_sliders[i].header) {
            s_pages.push_back(s_sliders[i].header);
            cur = (int)s_pages.size() - 1;
        }
        s_sliderPage.push_back(cur);
    }
    for (size_t i = 0; i < s_checks.size(); i++) {
        if (s_checks[i].header) {
            s_pages.push_back(s_checks[i].header);
            cur = (int)s_pages.size() - 1;
        }
        s_checkPage.push_back(cur);
    }
}

static float SliderValue(const SliderDef& d, int pos) {
    return d.mn + pos * d.step;
}
static int SliderPos(const SliderDef& d) {
    float v = d.fval ? *d.fval : (float)*d.ival;
    if (v < d.mn) v = d.mn;
    if (v > d.mx) v = d.mx;
    return (int)((v - d.mn) / d.step + 0.5f);
}

// Maps an ini (section,key) to the matching FluidConfig field inside the
// cached current-mood config, so the UI can mark values that differ from
// what the mood file saved. Unmapped keys (peak_nits, moods/*, pause_*)
// return ok=false — no " *" marker for those.
enum class FType { F, I, B };
struct FieldMap { const wchar_t* sec; const wchar_t* key; FType t; size_t off; };
#define FM(s, k, t, field) { s, k, t, offsetof(FluidConfig, field) }
static const FieldMap kFieldMap[] = {
    FM(L"sim", L"vorticity", FType::F, curl),
    FM(L"sim", L"baroclinic", FType::F, baroclinic),
    FM(L"sim", L"splat_radius", FType::F, splatRadius),
    FM(L"sim", L"density_diffusion", FType::F, densityDissipation),
    FM(L"sim", L"velocity_diffusion", FType::F, velocityDissipation),
    FM(L"sim", L"pressure_diffusion", FType::F, pressureDissipation),
    FM(L"sim", L"pressure_iterations", FType::I, pressureIterations),
    FM(L"sim", L"decay_fast", FType::F, decayFast),
    FM(L"sim", L"decay_threshold", FType::F, decayThreshold),
    FM(L"sim", L"saturation_restore", FType::F, satRestore),
    FM(L"sim", L"max_brightness", FType::F, maxBrightness),
    FM(L"sim", L"dye_diffusion", FType::F, dyeDiffusion),
    FM(L"sim", L"shading", FType::B, shading),
    FM(L"general", L"fps_limit", FType::F, fpsLimit),
    FM(L"general", L"mirror_second", FType::B, mirrorSecond),
    FM(L"behavior", L"wanderers", FType::B, wanderers),
    FM(L"behavior", L"wanderer_count", FType::I, wandererCount),
    FM(L"behavior", L"wanderer_mode", FType::I, wandererMode),
    FM(L"behavior", L"wanderer_speed", FType::F, wandererSpeed),
    FM(L"behavior", L"wanderer_brightness", FType::F, wandererBrightness),
    FM(L"behavior", L"wanderer_scale", FType::F, wandererScale),
    FM(L"behavior", L"wanderer_resume_delay", FType::F, wandererResumeDelay),
    FM(L"behavior", L"auto_pause", FType::B, autoPause),
    FM(L"behavior", L"dark_floor", FType::F, darkFloor),
    FM(L"behavior", L"dark_level", FType::F, darkLevel),
    FM(L"behavior", L"surv_dark_floor", FType::F, survDarkFloor),
    FM(L"behavior", L"contrast_req", FType::F, contrastReq),
    FM(L"behavior", L"dart_enabled", FType::B, dartEnabled),
    FM(L"behavior", L"dart_interval", FType::F, dartInterval),
    FM(L"behavior", L"dart_speed", FType::F, dartSpeed),
    FM(L"behavior", L"hueshift_enabled", FType::B, hsEnabled),
    FM(L"behavior", L"hueshift_step", FType::F, hsStep),
    FM(L"behavior", L"hueshift_linger", FType::F, hsLinger),
    FM(L"behavior", L"hueshift_glide", FType::F, hsGlide),
    FM(L"behavior", L"hueshift_burst_steps", FType::I, hsBurstSteps),
    FM(L"behavior", L"hueshift_off_time", FType::F, hsOffTime),
    FM(L"behavior", L"idle_splats", FType::B, idleSplats),
    FM(L"behavior", L"idle_interval", FType::F, idleInterval),
    FM(L"behavior", L"idle_amount", FType::I, idleAmount),
    FM(L"behavior", L"idle_brightness", FType::F, idleBrightness),
    FM(L"behavior", L"hold_to_splat", FType::B, holdToSplat),
    FM(L"behavior", L"splat_on_click", FType::B, splatOnClick),
    FM(L"behavior", L"show_mouse", FType::B, showMouse),
    FM(L"behavior", L"color_cycle_period", FType::F, colorCyclePeriod),
    FM(L"hdr", L"knee", FType::F, hdrKnee),
    FM(L"hdr", L"saturation", FType::F, hdrSaturation),
    FM(L"hdr", L"brightness", FType::F, hdrBrightness),
    FM(L"hdr", L"contrast", FType::F, hdrContrast),
    FM(L"hdr", L"compensation", FType::B, hdrCompensation),
    FM(L"color", L"colorful", FType::B, colorful),
    FM(L"color", L"more_colors", FType::B, moreColors),
    FM(L"color", L"post_saturation", FType::F, postSaturation),
    FM(L"color", L"post_contrast", FType::F, postContrast),
    FM(L"color", L"post_brightness", FType::F, postBrightness),
    FM(L"color", L"post_hue", FType::F, postHue),
    FM(L"color", L"hue_center", FType::F, hueCenter),
    FM(L"color", L"hue_range", FType::F, hueRange),
    FM(L"color", L"hue_linger", FType::F, hueLinger),
    FM(L"color", L"curve_enabled", FType::B, curveEnabled),
    FM(L"color", L"curve_center", FType::F, curveCenter),
    FM(L"color", L"curve_width", FType::F, curveWidth),
    FM(L"color", L"curve_height", FType::F, curveHeight),
    FM(L"color", L"shadow_floor", FType::F, shadowFloor),
    FM(L"color", L"shadow_knee", FType::F, shadowKnee),
};
static float MoodFileValue(const wchar_t* sec, const wchar_t* key, bool& ok) {
    ok = false;
    const FluidConfig* m = MoodsCachedConfig();
    if (!m || !sec || !key) return 0.0f;
    const char* base = reinterpret_cast<const char*>(m);
    for (const FieldMap& f : kFieldMap) {
        if (wcscmp(f.sec, sec) != 0 || wcscmp(f.key, key) != 0) continue;
        ok = true;
        switch (f.t) {
        case FType::F: return *reinterpret_cast<const float*>(base + f.off);
        case FType::I: return (float)*reinterpret_cast<const int*>(base + f.off);
        case FType::B: return *reinterpret_cast<const bool*>(base + f.off) ? 1.0f : 0.0f;
        }
    }
    return 0.0f;
}
static float MoodFileValue(const SliderDef& d, bool& ok) {
    return MoodFileValue(d.section, d.key, ok);
}

// true when the live value differs from the current mood file's saved value
static bool DiffersFromMood(const SliderDef& d) {
    bool ok = false;
    float mv = MoodFileValue(d, ok);
    if (!ok) return false;
    if (d.ival) return (int)(mv + 0.5f) != *d.ival;
    return fabsf(mv - *d.fval) > d.step * 0.5f;
}

static void UpdateSliderLabel(size_t i) {
    const SliderDef& d = s_sliders[i];
    float v = d.fval ? *d.fval : (float)*d.ival;
    wchar_t val[64];
    if (d.fval == &g_hdrPeakNits && v <= 0.0f)
        swprintf_s(val, L"off");
    else
        swprintf_s(val, L"%.*f", d.decimals, v);
    wchar_t buf[192];
    swprintf_s(buf, L"%s%s:  %s%s",
               MoodsLocksKey(d.section, d.key) ? L"● " : L"○ ",
               d.label, val, DiffersFromMood(d) ? L" *" : L"");
    if (s_lastSliderText[i] != buf) {
        s_lastSliderText[i] = buf;
        SetWindowTextW(s_sliderLabels[i], buf);
    }
}

static void UpdateCheckLabel(size_t i) {
    const CheckDef& d = s_checks[i];
    std::wstring text;
    if (!d.section) {
        text = d.label;   // autostart: not an ini-backed setting, no markers
    } else {
        text = MoodsLocksKey(d.section, d.key) ? L"● " : L"○ ";
        text += d.label;
        bool ok = false;
        float mv = MoodFileValue(d.section, d.key, ok);
        if (ok && d.val && (*d.val != (mv >= 0.5f))) text += L" *";
    }
    if (s_lastCheckText[i] != text) {
        s_lastCheckText[i] = text;
        SetWindowTextW(s_checkCtls[i], text.c_str());
    }
}

// mood bar: current mood name, in-cycle checkbox, save/new/delete buttons
static void RefreshMoodBar() {
    if (!s_moodName) return;
    int cur = MoodsCurrentIndex();
    bool curOk = cur >= 0 && cur < (int)MoodsNames().size();
    const std::wstring& nm = MoodsCurrentName();
    const wchar_t* shown = curOk ? nm.c_str() : L"-";
    if (s_lastMoodName != shown) {
        s_lastMoodName = shown;
        SetWindowTextW(s_moodName, shown);
    }
    EnableWindow(s_inCycle, curOk);
    EnableWindow(s_moodSave, curOk);
    EnableWindow(s_moodNew, curOk);
    EnableWindow(s_moodDel, curOk && MoodsNames().size() > 1);
    SendMessageW(s_inCycle, BM_SETCHECK,
                 (curOk && !MoodsIsSkipped(cur)) ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void PickPaletteColor(HWND owner, int idx) {
    FluidConfig& c = g_renderer->Config();
    static COLORREF customColors[16] = {};
    float* col = c.splatColors + idx * 3;
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.lpCustColors = customColors;
    cc.rgbResult = RGB((BYTE)(col[0] * 255), (BYTE)(col[1] * 255), (BYTE)(col[2] * 255));
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    col[0] = GetRValue(cc.rgbResult) / 255.0f;
    col[1] = GetGValue(cc.rgbResult) / 255.0f;
    col[2] = GetBValue(cc.rgbResult) / 255.0f;
    wchar_t key[32], val[64];
    swprintf_s(key, L"splat_color_%d", idx + 1);
    swprintf_s(val, L"%.4f %.4f %.4f", col[0], col[1], col[2]);
    WritePrivateProfileStringW(L"color", key, val, g_iniPath);
}

// ---------------------------------------------------------------------------
// layout: nav column + scrolling page panel + bottom rows anchored to the
// window edges. All controls exist from creation; layout only moves them.

static void ApplyScroll(HWND panel, ScrollState& s) {
    int maxPos = s.content > s.view ? s.content - s.view : 0;
    if (s.pos > maxPos) s.pos = maxPos;
    if (s.pos < 0) s.pos = 0;
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0;
    si.nMax = s.content;
    si.nPage = (UINT)s.view;
    si.nPos = s.pos;
    SetScrollInfo(panel, SB_VERT, &si, TRUE);
    ShowScrollBar(panel, SB_VERT, s.content > s.view);
}

// registers a bottom-row control: fixed x, row-based y anchored to the bottom
static void RegBottom(HWND h, int row, int x, int yOff, int w, int hgt, bool stretch = false) {
    s_bottom.push_back({ h, row, x, yOff, w, hgt, stretch });
}

static void RelayoutNav() {
    if (!s_navPanel) return;
    RECT rc;
    GetClientRect(s_navPanel, &rc);
    s_navScr.view = rc.bottom;
    s_navScr.content = 4 + (int)s_navBtns.size() * kNavBtnStep;
    ApplyScroll(s_navPanel, s_navScr);
    HDWP hdwp = BeginDeferWindowPos((int)s_navBtns.size());
    for (size_t i = 0; i < s_navBtns.size(); i++) {
        if (hdwp) {
            HDWP next = DeferWindowPos(hdwp, s_navBtns[i], nullptr, 4,
                                       4 + (int)i * kNavBtnStep - s_navScr.pos,
                                       rc.right - 8, kNavBtnH, SWP_NOZORDER);
            if (next) { hdwp = next; continue; }
            EndDeferWindowPos(hdwp);
            hdwp = nullptr;
        }
        SetWindowPos(s_navBtns[i], nullptr, 4, 4 + (int)i * kNavBtnStep - s_navScr.pos,
                     rc.right - 8, kNavBtnH, SWP_NOZORDER);
    }
    if (hdwp) EndDeferWindowPos(hdwp);
}

// stacks the selected page's header + sliders + checkboxes inside the page
// panel (label over trackbar, same metrics as before) and hides the rest
static void RelayoutPage() {
    if (!s_pagePanel || s_pageHeads.empty()) return;
    RECT rc;
    GetClientRect(s_pagePanel, &rc);
    const int w = rc.right - 8;
    s_pageScr.view = rc.bottom;
    int rows = 0, checks = 0;
    for (size_t i = 0; i < s_sliders.size(); i++) if (s_sliderPage[i] == s_page) rows++;
    for (size_t i = 0; i < s_checks.size(); i++) if (s_checkPage[i] == s_page) checks++;
    s_pageScr.content = 4 + kHeadH + rows * kRowH + checks * kCheckH + 4;
    ApplyScroll(s_pagePanel, s_pageScr);
    for (size_t p = 0; p < s_pageHeads.size(); p++)
        ShowWindow(s_pageHeads[p], (int)p == s_page ? SW_SHOW : SW_HIDE);
    for (size_t i = 0; i < s_sliders.size(); i++) {
        int show = s_sliderPage[i] == s_page ? SW_SHOW : SW_HIDE;
        ShowWindow(s_sliderLabels[i], show);
        ShowWindow(s_sliderCtls[i], show);
    }
    for (size_t i = 0; i < s_checks.size(); i++)
        ShowWindow(s_checkCtls[i], s_checkPage[i] == s_page ? SW_SHOW : SW_HIDE);
    int y = 4 - s_pageScr.pos;
    HDWP hdwp = BeginDeferWindowPos(1 + 2 * rows + checks);
    auto move = [&](HWND h, int x, int yy, int ww, int hh) {
        if (hdwp) {
            HDWP next = DeferWindowPos(hdwp, h, nullptr, x, yy, ww, hh, SWP_NOZORDER);
            if (next) { hdwp = next; return; }
            EndDeferWindowPos(hdwp);
            hdwp = nullptr;
        }
        SetWindowPos(h, nullptr, x, yy, ww, hh, SWP_NOZORDER);
    };
    move(s_pageHeads[s_page], 4, y + 4, w, 20);
    y += kHeadH;
    for (size_t i = 0; i < s_sliders.size(); i++) {
        if (s_sliderPage[i] != s_page) continue;
        move(s_sliderLabels[i], 4, y, w, 15);
        move(s_sliderCtls[i], 4, y + 16, w, 24);
        y += kRowH;
    }
    for (size_t i = 0; i < s_checks.size(); i++) {
        if (s_checkPage[i] != s_page) continue;
        move(s_checkCtls[i], 4, y, w, 22);
        y += kCheckH;
    }
    if (hdwp) EndDeferWindowPos(hdwp);
}

static void LayoutAll() {
    if (!s_wnd || !s_navPanel || !s_pagePanel) return;
    RECT rc;
    GetClientRect(s_wnd, &rc);
    const int cw = rc.right, ch = rc.bottom;
    const int contentTop = kMoodBarH + 6;
    int contentH = ch - contentTop - kBottomH - 6;
    if (contentH < 60) contentH = 60;
    const int contentX = kMargin + kNavW + 8;
    int contentW = cw - contentX - kMargin;
    if (contentW < 120) contentW = 120;
    // batch every move into one screen update: ~30 individual SetWindowPos
    // calls during a drag-resize leave unerased ghost rows behind
    HDWP hdwp = BeginDeferWindowPos(2 + (int)s_bottom.size());
    const int by = ch - kBottomH;
    auto move = [&](HWND h, int x, int y, int w, int hgt) {
        if (hdwp) {
            HDWP next = DeferWindowPos(hdwp, h, nullptr, x, y, w, hgt, SWP_NOZORDER);
            if (next) { hdwp = next; return; }
            EndDeferWindowPos(hdwp);   // commit the partial batch, then go direct
            hdwp = nullptr;
        }
        SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER);
    };
    move(s_navPanel, kMargin, contentTop, kNavW, contentH);
    move(s_pagePanel, contentX, contentTop, contentW, contentH);
    for (const BottomCtl& b : s_bottom) {
        int w = b.stretch ? cw - b.x - kMargin : b.w;
        if (w < 40) w = 40;
        move(b.hwnd, b.x, by + b.row * kRowSpace + b.yOff, w, b.h);
    }
    if (hdwp) EndDeferWindowPos(hdwp);
    RelayoutNav();
    RelayoutPage();
}

// panel children forward their notifications to the main settings window;
// the panel itself handles its own scrollbar
static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return SendMessageW(GetParent(hwnd), msg, wp, lp);
    case WM_VSCROLL: {
        ScrollState& s = (hwnd == s_navPanel) ? s_navScr : s_pageScr;
        const int old = s.pos;
        const int maxPos = s.content > s.view ? s.content - s.view : 0;
        switch (LOWORD(wp)) {
        case SB_LINEUP:   s.pos -= 24;      break;
        case SB_LINEDOWN: s.pos += 24;      break;
        case SB_PAGEUP:   s.pos -= s.view;  break;
        case SB_PAGEDOWN: s.pos += s.view;  break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
            GetScrollInfo(hwnd, SB_VERT, &si);
            s.pos = si.nTrackPos;
            break;
        }
        default: return 0;
        }
        if (s.pos < 0) s.pos = 0;
        if (s.pos > maxPos) s.pos = maxPos;
        if (s.pos != old) {
            if (hwnd == s_navPanel) RelayoutNav();
            else                    RelayoutPage();
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HSCROLL: {
        HWND ctl = (HWND)lp;
        for (size_t i = 0; i < s_sliderCtls.size(); i++) {
            if (s_sliderCtls[i] != ctl) continue;
            SliderDef& d = s_sliders[i];
            int pos = (int)SendMessageW(ctl, TBM_GETPOS, 0, 0);
            float v = SliderValue(d, pos);
            if (d.fval) *d.fval = v; else *d.ival = (int)(v + 0.5f);
            if (d.reinitWanderers) g_renderer->ReinitWanderers();
            if (d.fval) WriteIniFloat(d.section, d.key, v, d.decimals);
            else        WriteIniInt(d.section, d.key, *d.ival);
            UpdateSliderLabel(i);
            break;
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_NAV_BASE && id < IDC_NAV_BASE + (int)s_navBtns.size()) {
            s_page = id - IDC_NAV_BASE;   // persists across window recreation
            s_pageScr.pos = 0;
            for (size_t j = 0; j < s_navBtns.size(); j++)
                SendMessageW(s_navBtns[j], BM_SETCHECK,
                             (int)j == s_page ? BST_CHECKED : BST_UNCHECKED, 0);
            RelayoutPage();
            return 0;
        }
        if (id >= IDC_CHECK_BASE && id < IDC_CHECK_BASE + (int)s_checks.size()) {
            CheckDef& d = s_checks[id - IDC_CHECK_BASE];
            bool on = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (d.val == &g_moodSettings.enabled) {
                MoodsSetEnabled(on);   // also toggles the coverage override
            } else if (d.val) {
                *d.val = on;
                WriteIniInt(d.section, d.key, on ? 1 : 0);
                // Look switches are live. The two looks are mutually exclusive
                // (DisplayPso() would otherwise just prefer acid), and turning
                // one ON may need its display PSO compiled first; turning one
                // OFF needs nothing at all — the fluid path reads none of that
                // state. Re-sync the boxes so the UI matches the exclusion.
                if (g_renderer && (d.val == &g_renderer->Config().acid.enabled ||
                                   d.val == &g_renderer->Config().ink.enabled)) {
                    if (on) {
                        if (d.val == &g_renderer->Config().acid.enabled) {
                            g_renderer->Config().ink.enabled = false;
                            WriteIniInt(L"look", L"ink", 0);
                        } else {
                            g_renderer->Config().acid.enabled = false;
                            WriteIniInt(L"look", L"liquid_acid", 0);
                        }
                    }
                    g_renderer->EnsureLookResources();
                    for (size_t j = 0; j < s_checks.size() && j < s_checkCtls.size(); j++)
                        if (s_checks[j].val)
                            SendMessageW(s_checkCtls[j], BM_SETCHECK,
                                         *s_checks[j].val ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            } else {
                SetAutostart(on);
            }
            return 0;
        }
        if (id >= IDC_GAMUT_BASE && id <= IDC_GAMUT_BASE + 2) {
            g_gamutMode = id - IDC_GAMUT_BASE;
            WriteIniInt(L"hdr", L"gamut", g_gamutMode);
            return 0;
        }
        if (id >= IDC_COLOR_BASE && id < IDC_COLOR_BASE + 5) {
            PickPaletteColor(hwnd, id - IDC_COLOR_BASE);
            return 0;
        }
        if (id == IDC_OPEN_ANALYZER) { ShowAnalyzerWindow(); return 0; }
        if (id == IDC_SCENES_BTN)    { ShowScenesWindow();   return 0; }
        if (id == IDC_NEXT_MOOD)     { MoodsNext(*g_renderer); return 0; }
        if (id == IDC_SAVE_SCENE)    { SaveCurrentAsPresetFile(); return 0; }
        if (id == IDC_MOOD_SAVE) {
            if (g_renderer) MoodsSaveCurrent(*g_renderer);
            return 0;
        }
        if (id == IDC_MOOD_NEW) {
            if (g_renderer && MoodsCreateFromLive(*g_renderer) >= 0) {
                DestroyWindow(hwnd);     // WM_DESTROY nulls s_wnd
                ShowSettingsWindow();    // recreate so markers/caches are fresh
            }
            return 0;
        }
        if (id == IDC_MOOD_DELETE) {
            wchar_t q[512];
            swprintf_s(q, L"Delete mood '%s'? This removes the file.",
                       MoodsCurrentName().c_str());
            if (MessageBoxW(hwnd, q, L"Delete mood", MB_YESNO | MB_ICONWARNING) == IDYES &&
                MoodsDeleteCurrent()) {
                if (g_renderer) MoodsRefreshUiCache(*g_renderer);
                DestroyWindow(hwnd);
                ShowSettingsWindow();
            }
            return 0;
        }
        if (id == IDC_MOOD_INCYCLE) {
            bool on = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
            MoodsSetSkipped(MoodsCurrentIndex(), !on);
            return 0;
        }
        if (id == IDC_PAUSE_BTN) {
            TogglePause();
            SetWindowTextW(s_pauseBtn, IsManualPaused() ? L"Resume wallpaper" : L"Pause wallpaper");
            return 0;
        }
        if (id == IDC_EXIT_BTN) { RequestExit(); return 0; }
        if (HIWORD(wp) == CBN_SELCHANGE) {
            if (id == IDC_WMODE) {
                int sel = (int)SendMessageW(s_comboMode, CB_GETCURSEL, 0, 0);
                g_renderer->Config().wandererMode = sel;
                g_renderer->ReinitWanderers();
                WriteIniInt(L"behavior", L"wanderer_mode", sel);
            } else if (id == IDC_SIMRES || id == IDC_DYERES) {
                int simSel = (int)SendMessageW(s_comboSim, CB_GETCURSEL, 0, 0);
                int dyeSel = (int)SendMessageW(s_comboDye, CB_GETCURSEL, 0, 0);
                int simRes = kSimResOptions[simSel < 0 ? 3 : simSel];
                int dyeRes = kDyeResOptions[dyeSel < 0 ? 2 : dyeSel];
                g_renderer->SetResolutions(simRes, dyeRes);
                WriteIniInt(L"sim", L"sim_res", simRes);
                WriteIniInt(L"sim", L"dye_res", dyeRes);
            }
            return 0;
        }
        return 0;
    }
    case WM_SIZE:
        LayoutAll();
        return 0;
    case WM_GETMINMAXINFO: {
        RECT r = { 0, 0, kMinClientW, kMinClientH };
        AdjustWindowRect(&r, kWndStyle, FALSE);
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_TIMER:
        if (s_fpsLabel) {
            wchar_t buf[96];
            swprintf_s(buf, L"Rendering at %.0f fps", g_currentFps);
            SetWindowTextW(s_fpsLabel, buf);
            const auto& names = MoodsNames();
            int cur = MoodsCurrentIndex();
            int nxt = MoodsNextIndex();
            bool curOk = cur >= 0 && cur < (int)names.size();
            bool nxtOk = nxt >= 0 && nxt < (int)names.size();
            if (curOk && nxtOk)
                swprintf_s(buf, L"Mood: %s -> %s", names[cur].c_str(), names[nxt].c_str());
            else if (JourneyActive()) {
                int legI = 0, legN = 0;
                JourneyLegInfo(&legI, &legN);
                swprintf_s(buf, L"Mood: %s [journey leg %d/%d%s]",
                           curOk ? names[cur].c_str() : L"-", legI + 1, legN,
                           JourneyInBlendHold() ? L" blend" : L"");
            } else
                swprintf_s(buf, L"Mood: %s", curOk ? names[cur].c_str() : L"-");
            SetWindowTextW(s_moodLabel, buf);
            RefreshMoodBar();

            // the current mood file vanished on disk (external delete):
            // rebuild the UI caches once so every marker falls back to "○"
            static bool s_moodFileMissing = false;
            wchar_t mp[MAX_PATH];
            MoodsCurrentPath(mp);
            bool missing = !mp[0] || GetFileAttributesW(mp) == INVALID_FILE_ATTRIBUTES;
            if (missing != s_moodFileMissing && g_renderer) {
                s_moodFileMissing = missing;
                MoodsRefreshUiCache(*g_renderer);
            }

            // markers: ● = key saved in this mood, ○ = not in the mood,
            // trailing * = live value differs from the mood file. The labels
            // only get SetWindowTextW when the composed string changed.
            for (size_t i = 0; i < s_sliders.size(); i++) UpdateSliderLabel(i);
            for (size_t i = 0; i < s_checks.size(); i++) UpdateCheckLabel(i);

            // a mood switch changes Config() behind the sliders' backs; rebuild
            // so they show the new mood's values. Skipped while the user is
            // mid-drag (capture held) — retried on the next tick instead. The
            // selected page survives via the file-static s_page.
            static int s_shownMood = -1;
            if (s_shownMood < 0) {
                s_shownMood = cur;
            } else if (cur != s_shownMood && GetCapture() == nullptr) {
                s_shownMood = cur;
                if (g_renderer) MoodsRefreshUiCache(*g_renderer);   // markers track the new mood
                DestroyWindow(hwnd);     // WM_DESTROY nulls s_wnd
                ShowSettingsWindow();    // recreates fresh from Config()
            }
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, s_headers.count((HWND)lp) ? kAccent : RGB(215, 215, 222));
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)s_darkBrush;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)s_darkBrush;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 2);
        s_fpsLabel = nullptr;
        s_moodLabel = nullptr;
        s_moodName = nullptr;
        s_lastMoodName.clear();   // next window must set the name fresh
        s_inCycle = nullptr;
        s_moodSave = nullptr;
        s_moodNew = nullptr;
        s_moodDel = nullptr;
        s_navPanel = nullptr;
        s_pagePanel = nullptr;
        s_navBtns.clear();
        s_pageHeads.clear();
        s_bottom.clear();
        s_headers.clear();
        s_wnd = nullptr;    // wallpaper keeps running
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND MakeCtl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                    int x, int y, int w, int h, HMENU id, bool header = false) {
    // single-line statics must clip, never wrap: a wrapped second line would
    // bleed glyph tops into the row below (reads as "text bunching")
    if (wcscmp(cls, L"STATIC") == 0) style |= SS_LEFTNOWORDWRAP;
    HWND ctl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               x, y, w, h, parent, id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctl, WM_SETFONT, (WPARAM)(header ? s_headFont : s_font), TRUE);
    if (wcscmp(cls, L"BUTTON") == 0)
        SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
    else if (wcscmp(cls, L"COMBOBOX") == 0)
        SetWindowTheme(ctl, L"DarkMode_CFD", nullptr);
    if (header) s_headers.insert(ctl);
    return ctl;
}

static void AddTip(HWND tip, HWND ctl, const wchar_t* text) {
    if (!text) return;
    TOOLINFOW ti = { sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = s_wnd;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void CloseSettingsWindow() {
    if (s_wnd) DestroyWindow(s_wnd);
}

void ShowSettingsWindow() {
    if (s_wnd) {
        ShowWindow(s_wnd, SW_SHOW);
        SetForegroundWindow(s_wnd);
        return;
    }
    if (!g_renderer) return;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    if (!s_font) s_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!s_headFont)
        s_headFont = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    static bool registered = false;
    if (!registered) {
        s_darkBrush = CreateSolidBrush(RGB(30, 30, 36));
        WNDCLASSW wc = {};
        // full-client invalidation on resize: without CS_*REDRAW the bottom
        // rows' old pixels are never erased after a drag = "bunching" ghosts
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FluidWallpaperSettings";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = s_darkBrush;
        RegisterClassW(&wc);
        wc.lpfnWndProc = PanelWndProc;
        wc.lpszClassName = L"FluidWallpaperSettingsPanel";
        RegisterClassW(&wc);
        registered = true;
    }

    BuildDefs();   // also builds the page list from the defs' header fields
    s_sliderCtls.clear();
    s_sliderLabels.clear();
    s_checkCtls.clear();
    s_navBtns.clear();
    s_pageHeads.clear();
    s_bottom.clear();
    s_lastSliderText.assign(s_sliders.size(), std::wstring());
    s_lastCheckText.assign(s_checks.size(), std::wstring());
    if (s_page >= (int)s_pages.size()) s_page = 0;
    s_navScr = ScrollState{};
    s_pageScr = ScrollState{};
    MoodsRefreshUiCache(*g_renderer);   // markers compare against the current mood

    RECT r = { 0, 0, 760, 640 };   // compact default: nav + one page column
    AdjustWindowRect(&r, kWndStyle, FALSE);
    s_wnd = CreateWindowExW(0, L"FluidWallpaperSettings",
                            L"Fluid Wallpaper — Settings",
                            kWndStyle,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(s_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetTimer(s_wnd, 2, 500, nullptr);

    // one tooltip control serves every slider/checkbox hover hint
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               s_wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(tip, L"DarkMode_Explorer", nullptr);
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 300);

    // scrolling panels: nav on the left, page content on the right. Children
    // are clipped to the panel rect; notifications are forwarded to s_wnd.
    s_navPanel = CreateWindowExW(0, L"FluidWallpaperSettingsPanel", nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                 0, 0, 10, 10, s_wnd, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    s_pagePanel = CreateWindowExW(0, L"FluidWallpaperSettingsPanel", nullptr,
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                  0, 0, 10, 10, s_wnd, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);

    // nav: one push-like toggle button per category page
    for (size_t p = 0; p < s_pages.size(); p++)
        s_navBtns.push_back(MakeCtl(s_navPanel, L"BUTTON", s_pages[p].c_str(),
                                    BS_PUSHLIKE | BS_AUTOCHECKBOX | BS_LEFT,
                                    0, 0, 10, 10, (HMENU)(UINT_PTR)(IDC_NAV_BASE + p)));
    if (!s_navBtns.empty())
        SendMessageW(s_navBtns[s_page], BM_SETCHECK, BST_CHECKED, 0);

    // one accent header per page, inside the scrolling content panel
    for (size_t p = 0; p < s_pages.size(); p++)
        s_pageHeads.push_back(MakeCtl(s_pagePanel, L"STATIC", s_pages[p].c_str(), 0,
                                      0, 0, 10, 10, nullptr, true));

    // sliders for every page, created up front; only the selected page shows
    for (size_t i = 0; i < s_sliders.size(); i++) {
        const SliderDef& d = s_sliders[i];
        HWND label = MakeCtl(s_pagePanel, L"STATIC", L"", 0, 0, 0, 10, 10, nullptr);
        HWND track = MakeCtl(s_pagePanel, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS,
                             0, 0, 10, 10, nullptr);
        int ticks = (int)((d.mx - d.mn) / d.step + 0.5f);
        SendMessageW(track, TBM_SETRANGE, FALSE, MAKELPARAM(0, ticks));
        int page = ticks / 25;
        if (page < 1) page = 1;
        SendMessageW(track, TBM_SETPAGESIZE, 0, page);
        SendMessageW(track, TBM_SETLINESIZE, 0, 1);
        if (d.fval == &g_hdrPeakNits && *d.fval < 0.0f)
            SendMessageW(track, TBM_SETPOS, TRUE, (LPARAM)(int)((g_maxNits - d.mn) / d.step + 0.5f));
        else
            SendMessageW(track, TBM_SETPOS, TRUE, SliderPos(d));
        s_sliderCtls.push_back(track);
        s_sliderLabels.push_back(label);
        UpdateSliderLabel(i);
        AddTip(tip, track, d.tip);
    }

    // checkboxes for every page
    for (size_t i = 0; i < s_checks.size(); i++) {
        const CheckDef& d = s_checks[i];
        HWND box = MakeCtl(s_pagePanel, L"BUTTON", d.label, BS_AUTOCHECKBOX,
                           0, 0, 10, 10, (HMENU)(UINT_PTR)(IDC_CHECK_BASE + i));
        bool on = d.val ? *d.val : GetAutostart();
        SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        s_checkCtls.push_back(box);
        UpdateCheckLabel(i);   // checkbox text carries the ●/○/* markers
        AddTip(tip, box, d.tip);
    }

    FluidConfig& c = g_renderer->Config();

    // mood bar (top strip, always visible): name + cycle + save/new/delete.
    // Label and name share the head font so their baselines line up — the old
    // 44px label clipped its colon and jammed the two texts together.
    MakeCtl(s_wnd, L"STATIC", L"Mood:", 0, kMargin, 7, 60, 22, nullptr, true);
    s_moodName = MakeCtl(s_wnd, L"STATIC", L"-", 0, kMargin + 62, 7, 196, 22, nullptr);
    SendMessageW(s_moodName, WM_SETFONT, (WPARAM)s_headFont, TRUE);
    s_inCycle = MakeCtl(s_wnd, L"BUTTON", L"In cycle", BS_AUTOCHECKBOX,
                        kMargin + 262, 8, 86, 22, (HMENU)(UINT_PTR)IDC_MOOD_INCYCLE);
    s_moodSave = MakeCtl(s_wnd, L"BUTTON", L"Save", BS_PUSHBUTTON,
                         kMargin + 352, 6, 80, 26, (HMENU)(UINT_PTR)IDC_MOOD_SAVE);
    s_moodNew = MakeCtl(s_wnd, L"BUTTON", L"New", BS_PUSHBUTTON,
                        kMargin + 436, 6, 80, 26, (HMENU)(UINT_PTR)IDC_MOOD_NEW);
    s_moodDel = MakeCtl(s_wnd, L"BUTTON", L"Delete", BS_PUSHBUTTON,
                        kMargin + 520, 6, 80, 26, (HMENU)(UINT_PTR)IDC_MOOD_DELETE);
    AddTip(tip, s_inCycle, L"Include this mood in the auto-cycle rotation");
    AddTip(tip, s_moodSave, L"Overwrite the current mood file with your live settings");
    AddTip(tip, s_moodNew, L"Create a new mood from your live settings");
    AddTip(tip, s_moodDel, L"Delete the current mood file");

    // bottom utility rows, anchored to the bottom edge by LayoutAll()

    // row 0: gamut + wanderer path
    RegBottom(MakeCtl(s_wnd, L"STATIC", L"Gamut:", 0, 0, 0, 10, 10, nullptr),
              0, kMargin, 5, 44, 16);
    const wchar_t* gamutLabels[3] = { L"sRGB", L"Display-P3", L"BT.2020 (QD-OLED)" };
    const int gamutX[3] = { 58, 118, 215 }, gamutW[3] = { 58, 95, 145 };
    for (int gIdx = 0; gIdx < 3; gIdx++) {
        HWND radio = MakeCtl(s_wnd, L"BUTTON", gamutLabels[gIdx],
                             BS_AUTORADIOBUTTON | (gIdx == 0 ? WS_GROUP : 0),
                             0, 0, 10, 10, (HMENU)(UINT_PTR)(IDC_GAMUT_BASE + gIdx));
        SendMessageW(radio, BM_SETCHECK, g_gamutMode == gIdx ? BST_CHECKED : BST_UNCHECKED, 0);
        RegBottom(radio, 0, gamutX[gIdx], 2, gamutW[gIdx], 22);
    }
    RegBottom(MakeCtl(s_wnd, L"STATIC", L"Wanderer path:", 0, 0, 0, 10, 10, nullptr),
              0, 366, 5, 92, 16);
    s_comboMode = MakeCtl(s_wnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                          0, 0, 10, 10, (HMENU)(UINT_PTR)IDC_WMODE);
    RegBottom(s_comboMode, 0, 460, 0, 140, 200);
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Random wander");
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Circle");
    SendMessageW(s_comboMode, CB_ADDSTRING, 0, (LPARAM)L"Figure 8");
    SendMessageW(s_comboMode, CB_SETCURSEL, c.wandererMode, 0);

    // row 1: resolutions
    RegBottom(MakeCtl(s_wnd, L"STATIC", L"Sim res:", 0, 0, 0, 10, 10, nullptr),
              1, kMargin, 5, 50, 16);
    s_comboSim = MakeCtl(s_wnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         0, 0, 10, 10, (HMENU)(UINT_PTR)IDC_SIMRES);
    RegBottom(s_comboSim, 1, 64, 0, 78, 200);
    RegBottom(MakeCtl(s_wnd, L"STATIC", L"Dye res:", 0, 0, 0, 10, 10, nullptr),
              1, 148, 5, 50, 16);
    s_comboDye = MakeCtl(s_wnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                         0, 0, 10, 10, (HMENU)(UINT_PTR)IDC_DYERES);
    RegBottom(s_comboDye, 1, 200, 0, 78, 200);
    for (int v : kSimResOptions) {
        wchar_t b[16]; swprintf_s(b, L"%d", v);
        SendMessageW(s_comboSim, CB_ADDSTRING, 0, (LPARAM)b);
    }
    for (int v : kDyeResOptions) {
        wchar_t b[16]; swprintf_s(b, L"%d", v);
        SendMessageW(s_comboDye, CB_ADDSTRING, 0, (LPARAM)b);
    }
    for (int i2 = 0; i2 < 5; i2++) {
        if (kSimResOptions[i2] == c.simRes) SendMessageW(s_comboSim, CB_SETCURSEL, i2, 0);
        if (kDyeResOptions[i2] == c.dyeRes) SendMessageW(s_comboDye, CB_SETCURSEL, i2, 0);
    }
    RegBottom(MakeCtl(s_wnd, L"STATIC", L"(changing res restarts the fluid)", 0,
                      0, 0, 10, 10, nullptr), 1, 286, 5, 240, 16);

    // row 2: palette pickers
    RegBottom(MakeCtl(s_wnd, L"STATIC", L"Palette (hue wheel off):", 0, 0, 0, 10, 10, nullptr),
              2, kMargin, 5, 146, 16);
    for (int ci = 0; ci < 5; ci++) {
        wchar_t lbl[16];
        swprintf_s(lbl, L"Color %d…", ci + 1);
        RegBottom(MakeCtl(s_wnd, L"BUTTON", lbl, BS_PUSHBUTTON, 0, 0, 10, 10,
                          (HMENU)(UINT_PTR)(IDC_COLOR_BASE + ci)), 2, 166 + ci * 90, 0, 84, 26);
    }

    // row 3: windows + mood step + scene save
    RegBottom(MakeCtl(s_wnd, L"BUTTON", L"Open HDR analyzer", BS_PUSHBUTTON, 0, 0, 10, 10,
                      (HMENU)(UINT_PTR)IDC_OPEN_ANALYZER), 3, kMargin, 0, 150, 26);
    RegBottom(MakeCtl(s_wnd, L"BUTTON", L"Scenes…", BS_PUSHBUTTON, 0, 0, 10, 10,
                      (HMENU)(UINT_PTR)IDC_SCENES_BTN), 3, 172, 0, 90, 26);
    RegBottom(MakeCtl(s_wnd, L"BUTTON", L"Next mood", BS_PUSHBUTTON, 0, 0, 10, 10,
                      (HMENU)(UINT_PTR)IDC_NEXT_MOOD), 3, 272, 0, 100, 26);
    RegBottom(MakeCtl(s_wnd, L"BUTTON", L"Save look as scene", BS_PUSHBUTTON, 0, 0, 10, 10,
                      (HMENU)(UINT_PTR)IDC_SAVE_SCENE), 3, 382, 0, 170, 26);

    // row 4: playback buttons + mood status
    s_pauseBtn = MakeCtl(s_wnd, L"BUTTON", IsManualPaused() ? L"Resume wallpaper" : L"Pause wallpaper",
                         BS_PUSHBUTTON, 0, 0, 10, 10, (HMENU)(UINT_PTR)IDC_PAUSE_BTN);
    RegBottom(s_pauseBtn, 4, kMargin, 0, 140, 26);
    RegBottom(MakeCtl(s_wnd, L"BUTTON", L"Exit wallpaper", BS_PUSHBUTTON, 0, 0, 10, 10,
                      (HMENU)(UINT_PTR)IDC_EXIT_BTN), 4, 162, 0, 140, 26);
    // mood status takes row 4's stretch slot (wide enough for journey-leg
    // text even at min width); fps moves to the legend row's tail. Small font
    // for the status — the big font clipped mid-word next to the legend row.
    s_moodLabel = MakeCtl(s_wnd, L"STATIC", L"Mood: …", 0, 0, 0, 10, 10, nullptr);
    s_headers.insert(s_moodLabel);   // keep the accent color at the small font
    RegBottom(s_moodLabel, 4, 312, 5, 280, 18, true);

    // legend (slim line at the very bottom) + fps at the row's right tail
    RegBottom(MakeCtl(s_wnd, L"STATIC",
                      L"● saved in this mood   ○ not in this mood   * differs from saved mood",
                      0, 0, 0, 10, 10, nullptr), 5, kMargin, 3, 450, 16);
    s_fpsLabel = MakeCtl(s_wnd, L"STATIC", L"Rendering at … fps", 0, 0, 0, 10, 10, nullptr);
    RegBottom(s_fpsLabel, 5, 470, 3, 140, 16, true);

    LayoutAll();
    RefreshMoodBar();
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
