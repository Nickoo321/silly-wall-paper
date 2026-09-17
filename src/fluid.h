#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// "Liquid Acid" render look (ini section [liquid_acid], enabled by
// [look] style=liquid_acid).
//
// Macro footage of oil floating on inked water, per reference/shots/photos/
// liquid-acid-ref-*.jpg. Two layers:
//   INK  — the fluid sim's dye, re-styled: luminance posterised into soft flat
//          bands, remapped through a 4-stop duotone ramp, dark seams painted
//          where the dye gradient is steep. The marbling/filaments are the
//          sim's, unchanged; only the colour mapping is restyled.
//   OIL  — a CPU metaball field (discs, webs, bubbles, plus NEGATIVE blobs that
//          eat round holes/bubbles out of the oil) advected by the fluid's own
//          velocity field, rendered as flat saturated fills with a thin dark
//          rim just inside the field==1 isoline.
// Every constant below is a named field so the look is tunable from the ini
// and the settings window. All of it is inert when `enabled` is false — the
// display shader is then compiled without the LIQUID_ACID macro at all, so the
// normal fluid look is bit-identical.
// ---------------------------------------------------------------------------
struct LiquidAcidConfig {
    bool  enabled = false;          // [look] style = fluid | liquid_acid

    // --- oil population (counts are fractions of blobCount) ---
    // The references are mostly OIL with ink showing through as channels, so
    // the discs and webs are huge (a third of the frame tall) and the small
    // stuff is handled by the procedural swarm layer below, not by blobs.
    int   blobCount   = 96;         // blobs stepped on the CPU and looped per pixel (max 128)
    float discFrac    = 0.22f;      // huge flat discs  (ref 1)
    float webFrac     = 0.42f;      // big blobs seeded in chains -> oil sheets/webs (ref 2)
    float bubbleFrac  = 0.22f;      // free-floating oil droplets
                                    // remainder = negative "hole" blobs
    float discMin     = 0.250f, discMax   = 0.460f;   // radius, uv-y units
    float webMin      = 0.110f, webMax    = 0.260f;
    float bubbleMin   = 0.015f, bubbleMax = 0.075f;
    float holeMin     = 0.030f, holeMax   = 0.130f;
    float sizeBias    = 2.0f;       // >1 skews bubble/hole radii toward the small end
    float bigBias     = 0.55f;      // <1 skews disc/web radii toward the LARGE end
    float holeWeight  = 0.75f;      // negative-blob field weight (how hard holes bite)

    // --- metaball field (compact Wyvill kernel (1-t^2)^3) ---
    float threshold   = 0.50f;      // field level of the oil surface (the isoline)
    float supportScale= 2.20f;      // blob support radius / visible radius. With
                                    // threshold 0.5 the visible radius is ~= baseR;
                                    // bigger = stickier, blobs bridge further apart
    float aaScale     = 1.3f;       // fwidth multiplier for the coverage smoothstep

    // --- oil motion ---
    float flowGain    = 1.15f;      // fluid velocity -> blob advection
    float curlDrift   = 0.0016f;    // analytic divergence-free drift on top
    float repulsion   = 0.55f;      // soft separation between same-sign blobs
    float buoyancy    = 0.0020f;    // radius-proportional rise (uv/s at r=0.12)
    float damping     = 2.2f;       // velocity relaxation rate (1/s), fps-normalised
    float breathAmt   = 0.10f;      // radius breathing amplitude
    float wrapMargin  = 0.20f;      // uv margin before a blob wraps to the far side

    // --- oil shading ---
    float rimWidth    = 0.0013f;    // dark rim half-width, sdf units (~2 px at 1080p)
    float rimInset    = 0.0013f;    // rim band centre, INSIDE the isoline
    float rimDark     = 0.80f;      // 0..1 darkening at the rim core
    // meniscus: the THIN BRIGHT ink-coloured halo just outside the dark rim
    // (cyan on ref 1, pale violet on ref 2) — the ink refracted by the edge of
    // the oil lens. Painted with the ink ramp's bright stop so it reads even
    // where the ink behind is black.
    float meniscus    = 0.85f;      // 0..1 strength
    float meniscusW   = 0.0020f;    // half-width, sdf units
    float meniscusOff = 0.0022f;    // band centre, OUTSIDE the isoline
    float meniscusCol[3] = { 0.353f, 0.918f, 0.894f };   // bright cyan (ref 1)
    float refraction  = 0.050f;     // ink uv offset along the field gradient near rims
    float translucency= 0.16f;      // how much the ink under the oil modulates it
    float oilTexture  = 0.07f;      // faint in-blob mottle (interiors stay flat)
    float inkShading  = 0.00f;      // how much of the fluid look's pseudo-3D emboss
                                    // survives on the ink (refs are flat; 1 = normal)
    float oilHdr      = 0.0f;       // >0: drive HDR highlight gain for oil pixels
    float rimHdr      = 0.0f;       // >0: extra HDR level on the rim band only
    // oil palette: up to 4 colours, dominant-blob pick (no colour-bleed averaging)
    float oilColors[12] = { 0.902f, 0.278f, 0.157f,     // vermillion disc
                            0.976f, 0.400f, 0.078f,     // hot orange web
                            0.859f, 0.204f, 0.098f,     // deep red-orange
                            0.988f, 0.541f, 0.114f };   // amber bubble

    // --- ink (the fluid, re-styled) ---
    float inkLevels   = 5.0f;       // posterise bands (few + soft = broad flat plateaus)
    float inkSoft     = 0.42f;      // band-edge softness (0 = hard steps)
    float inkMix      = 0.88f;      // 0 = keep the parity colour, 1 = full ramp
    float inkHueVary  = 14.0f;      // degrees of ramp hue-rotate driven by the dye's own hue
    // Complement lock: hold the ink's hue OPPOSITE the oil's on the wheel.
    // The regional variation above still moves the ink around, but only inside
    // a window centred on (mean oil hue + 180), so the ink can never drift
    // toward the oil hue and the pair stays complementary all the time.
    bool  inkComplementLock = true;
    float inkComplementSpan = 40.0f;   // width of that window, degrees
    // Sweep the palette through a curated list of VIVID complementary pairs
    // (orange/teal -> red/cyan -> magenta/green -> gold/violet -> lime/purple),
    // cross-fading oil family and ink ramp together. A continuous hue rotation
    // was tried first and rejected: no rotation keeps a palette vivid at every
    // hue, because the saturation and lightness a colour needs to read as
    // "vivid" depend on the hue (a mid-value teal is a good teal; the same
    // value at yellow is olive). Curated anchors sidestep that entirely, and
    // match the reference pack, where each loop is one vivid pair.
    // Seconds for a full trip through the list; 0 = off (use the fixed palette).
    float hueSweepPeriod = 0.0f;
    static const int kSweepPairs = 5;
    // Each pair is two vivid anchors: the oil colour and the ink's mid tone.
    // The rest of the palette (the other three oil shades, the ink's near-black
    // and its two oil-hue bands, the meniscus) is derived from them using the
    // saturation/value ratios of the authored palette above, so every swept
    // pair has the same internal structure as the hand-tuned one.
    float sweepOil[kSweepPairs * 3] = {
        0.898f, 0.271f, 0.145f,   // vermillion
        0.930f, 0.120f, 0.160f,   // red
        0.880f, 0.120f, 0.620f,   // magenta
        0.970f, 0.780f, 0.100f,   // gold
        0.550f, 0.850f, 0.120f,   // lime
    };
    float sweepInk[kSweepPairs * 3] = {
        0.086f, 0.478f, 0.494f,   // teal
        0.100f, 0.620f, 0.500f,   // cyan-green
        0.220f, 0.700f, 0.240f,   // green
        0.340f, 0.160f, 0.720f,   // violet
        0.480f, 0.120f, 0.660f,   // purple
    };
    float inkGain     = 2.30f;      // luminance -> ramp position
    float inkBias     = 0.05f;
    float seamStrength= 0.70f;      // dark seams along |grad dye|
    float seamLo      = 0.06f, seamHi = 0.45f;
    float seamScale   = 2.6f;       // gradient tap spacing, screen texels
    // 4-stop ink ramp, dark -> bright. Ref 1's ink is teal AND orange, so the
    // ramp crosses the complement: near-black teal -> teal -> burnt -> hot.
    float inkRamp[12] = { 0.006f, 0.034f, 0.038f,       // near-black teal
                          0.043f, 0.353f, 0.376f,       // teal
                          0.478f, 0.173f, 0.020f,       // burnt orange
                          0.969f, 0.510f, 0.055f };     // hot orange

    // --- bubble swarms (procedural, NOT metaballs) ---------------------
    // The references carry hundreds of round droplets with a wide size range:
    // dark water droplets trapped INSIDE the oil, and oil droplets sitting on
    // the open ink. A jittered cellular layer gives all of them for ~20 hashes
    // per pixel; a hundred more metaballs would cost far more.
    float swarmHoles  = 0.90f;      // strength of the hole swarm inside the oil
    float swarmDrops  = 0.55f;      // strength of the oil-droplet swarm on the ink
    float swarmDensity= 0.55f;      // fraction of cells that carry a droplet
    float swarmScaleA = 26.0f;      // cells per p-unit, hole swarm (bigger = smaller holes)
    float swarmScaleB = 34.0f;      // cells per p-unit, droplet swarm
    float swarmRMin   = 0.045f;     // droplet radius in CELL units (wide range = every size)
    float swarmRMax   = 0.430f;
    float swarmRimDark= 0.55f;      // thin dark edge on every droplet
    float swarmDrift  = 1.0f;       // how fast the swarm layers creep / warp
    float swarmClump  = 0.70f;      // 0 = even blanket, 1 = droplets only in patches
    float swarmDark   = 0.80f;      // how dark a trapped water droplet reads

    // --- grain / speckle ---
    float grainAmt    = 0.030f;     // coarse animated film grain
    float grainScale  = 3.0f;       // px per grain cell (>1 = coarse)
    float speckle     = 0.12f;      // cellular dots concentrated at interfaces
    float speckScale  = 240.0f;     // cells per uv unit
};

// Defaults mirror reference/project.json (the shipped Wallpaper Engine values),
// falling back to reference/script.js config for values project.json doesn't set.
struct FluidConfig {
    int   simRes = 256;
    int   dyeRes = 1024;            // project.json ships 4096; raise after perf pass
    float densityDissipation = 0.999f;
    float velocityDissipation = 0.999f;
    float pressureDissipation = 0.85f;
    int   pressureIterations = 20;
    float curl = 48.0f;
    float baroclinic = 0.0f;    // dye-front torque: wakes bend around dye masses (0 = off)
    float flowSpeed = 1.0f;     // global impulse multiplier — slows/strengthens all currents
    float splatRadius = 0.64f;      // percent, /100 like reference
    bool  shading = true;
    float dyeDiffusion = 0.0f;      // D∇²c strength: 0 = classic sharp look, >0 = smoke-like spread
    float decayFast = 1.0f;         // 1.0 = WE-original balance. 0.90 starved the field to black:
                                    // wanderer dye (0.1 * 0.15 intensity) died before accumulating.
                                    // Tail snappiness is a settings slider now — user taste, not a default.
    float decayThreshold = 0.29f;
    // M4 HDR output mapping (controlled live from the tray menu)
    int   gamutMode = 1;            // 0 sRGB, 1 Display-P3 (WE parity, default), 2 BT.2020 (over-saturates vs WE)
    float hdrPeakNits = 0.0f;       // resolved target nits for hot spots; 0 = off (match SDR)
    float hdrKnee = 0.6f;           // dye brightness where highlight expansion starts
    float maxBrightness = 1.35f;
    float satRestore = 0.93f;
    float colorCyclePeriod = 19.0f;
    bool  idleSplats = true;
    float idleInterval = 9.6f;
    int   idleAmount = 8;
    float idleBrightness = 1.5f;   // burst intensity (wanderers paint at 0.15)
    // post color filter — the equivalent of Wallpaper Engine's right-panel
    // color controls the user ran the original with (1/1/1/0 = neutral)
    float postSaturation = 1.0f;
    float postContrast = 1.0f;
    float postBrightness = 1.0f;
    float postHue = 0.0f;           // degrees
    // response curve (Lightroom-style brightness hump -> glowing splat rims)
    bool  curveEnabled = false;
    float curveCenter = 0.30f;      // input brightness the hump peaks at
    float curveWidth = 0.10f;       // hump half-width (smaller = tighter rims)
    float curveHeight = 1.3f;       // output brightness at the peak
    // shadow floor ("bottom knee"): lift near-black toward a neutral gray
    // floor so dark regions keep visible marbling. 0 = off (pure black).
    float shadowFloor = 0.0f;
    float shadowKnee = 0.15f;       // brightness range the lift fades over
    // hue band: constrain the color wheel to a slice around hueCenter.
    // range 180 = the classic full wheel; smaller = themed (e.g. only oranges)
    float hueCenter = 0.0f;         // degrees
    float hueRange = 180.0f;        // degrees half-width
    float hueLinger = 0.0f;         // banded moods: fraction of each half-lap
                                    // spent resting at a band edge (0 = off)
    // color source: random wheel (colorful) vs fixed palette
    bool  colorful = true;
    bool  moreColors = true;
    float splatColors[15] = { 0,1,0,  0,1,1,  0,0,1,  1,0,0,  0.9412f,1,0 };
    bool  splatOnClick = true;     // click burst when holdToSplat is off
    // perf
    float fpsLimit = 60.0f;
    // second monitor: mirror the fluid there (same field, own HDR mapping)
    bool  mirrorSecond = false;
    // wanderers (autonomous roaming splats)
    bool  wanderers = true;
    int   wandererCount = 2;
    int   wandererMode = 0;          // 0 random, 1 circle, 2 figure8
    float wandererSpeed = 246.0f;    // px/s
    float wandererScale = 0.1f;      // path size for circle/figure8
    float wandererResumeDelay = 4.5f;// s of no user input before wanderers resume
    float wandererBrightness = 0.1f;
    // screen-fullness governor
    bool  autoPause = true;
    float darkFloor = 9.0f;          // pause when dark area < this %
    float darkLevel = 0.07f;         // pixel counts as dark below this brightness
    float survDarkFloor = 8.0f;      // survivor wanderer's own floor
    float contrastReq = 30.0f;       // % brightest tile must beat the rest (0 = off)
    // separating dart
    bool  dartEnabled = true;
    float dartInterval = 7.0f;       // s between darts (while group paused)
    float dartSpeed = 967.0f;        // px/s
    // hue-shift cycler (post-process palette rotation)
    bool  hsEnabled = true;
    float hsStep = 83.0f;            // degrees per step
    float hsLinger = 6.5f;
    float hsGlide = 7.2f;
    int   hsBurstSteps = 2;
    float hsOffTime = 10.0f;
    // HDR compensation (the WE original's CSS filter, applied when HDR is on)
    bool  hdrCompensation = true;
    float hdrSaturation = 1.2f;
    float hdrBrightness = 1.08f;
    float hdrContrast = 1.0f;
    // mouse
    bool  showMouse = true;          // cursor movement splats
    bool  holdToSplat = true;        // hold LMB on desktop = continuous splat
    // debug / test switches
    bool  gradientMode = false;     // render the M1 HDR test gradient instead
    int   calibratePage = 0;        // >0: render quiz pattern page N (--calibrate N)
    bool  stats = false;            // periodic dye-field readback stats to stdout
    // "Liquid Acid" render look — additive; inert unless acid.enabled
    LiquidAcidConfig acid;
};

// Per-frame input from the app shell (global cursor, desktop focus).
struct FrameInput {
    float mouseX = 0, mouseY = 0;    // px on our monitor
    float mouseDx = 0, mouseDy = 0;  // px delta * 5, reference scaling
    bool  mouseMoved = false;
    bool  mouseDown = false;         // LMB held with desktop focused
    bool  userInteracted = false;    // resets the wanderer resume timer
};

class FluidRenderer {
public:
    void Init(HWND hwnd, int width, int height, const FluidConfig& cfg);
    // Headless capture mode (--shot): device WITHOUT a swap chain, no window.
    // The display pass renders into an FP16 (R16G16B16A16_FLOAT) offscreen RT
    // of the requested size; CaptureOffscreen() reads it back as linear scRGB.
    void InitOffscreen(int width, int height, const FluidConfig& cfg);
    bool IsHeadless() const { return m_headless; }
    // width*height*4 floats, row-major RGBA, linear scRGB (1.0 = 80 nits).
    bool CaptureOffscreen(std::vector<float>& outRgba);
    // Deterministic runs: seed every rand()-based behavior. 0 (default) keeps
    // the normal time-seeded startup. Must be set before Init/InitOffscreen.
    static void SetRandomSeed(unsigned seed);
    void Frame(float dtSec, float sdrScale, bool hdrActive, const FrameInput& input);
    void ReassertColorSpace();
    // Full GPU teardown (swapchain, all textures, heaps, PSOs, device): frees
    // all RAM/VRAM. Idempotent, and Init() may be called again afterwards.
    void Shutdown();
    // live tray-menu controls (peakNits already resolved: actual nits, 0 = off)
    void SetHdrOptions(float peakNits, int gamutMode) {
        m_cfg.hdrPeakNits = peakNits;
        m_cfg.gamutMode = gamutMode;
    }
    // live settings-window access; fields are read per frame, so edits apply
    // immediately (resolution fields excluded from the UI — they need recreate)
    FluidConfig& Config() { return m_cfg; }
    void ReinitWanderers() { InitWanderers(); }
    void SetResolutions(int simRes, int dyeRes);   // recreates sim textures live
    void Reattach(HWND hwnd);   // new swapchain after Explorer restart; sim state survives
    bool PresentBroken() const { return m_presentBroken; }   // window died mid-frame
    // second-monitor mirror
    void EnableMirror(HWND hwnd, int width, int height);
    void DisableMirror();
    bool MirrorActive() const { return m_mirrorChain != nullptr; }
    bool MirrorBroken() const { return m_mirrorBroken; }
    void SetMirrorHdr(float sdrScale, float peakNits) {
        m_mirrorSdrScale = sdrScale;
        m_mirrorPeakNits = peakNits;
    }

    // mood-conductor primitives
    // Directed hue-shift glide to targetDeg over durationSec; holds at the
    // target until ReleaseHueShift. Works even when the cycler is disabled.
    void CommandHueShift(float targetDeg, float durationSec);
    // returnHome=true rotates forward to the next full turn first, then hands
    // the angle back to the scheduled cycler (or zero when hsEnabled=false).
    void ReleaseHueShift(bool returnHome);
    float HueAngleDeg() const { return m_hueAngle; }
    // Run the 1 Hz coverage readback even when the auto-pause governor is off
    // (the mood conductor needs fill % + average hue for its triggers).
    void SetCoverageWanted(bool on) { m_coverageWanted = on; }
    float CoverageDarkPct() const { return m_darkPct; }
    bool ScreenTooFull() const { return m_screenTooFull; }
    float FieldAvgHueDeg() const { return m_avgHue; }   // circular mean hue of lit dye

    // HDR analyzer: parallel low-res render of the FINAL scRGB output
    // (post gamut/peak mapping), read back ~10x/s for the analyzer window.
    static const int kAnaW = 640, kAnaH = 360;
    void EnableAnalyzer(bool on) { m_anaEnabled = on; }
    bool ReadAnalyzerFrame(std::vector<float>& outRgba);  // true if a new frame landed
    IDXGISwapChain3* SwapChain() { return m_swapChain.Get(); }

private:
    struct Tex {
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
        D3D12_GPU_DESCRIPTOR_HANDLE uav = {};
        int w = 0, h = 0;
    };
    struct DoubleTex { Tex a, b; Tex* read = &a; Tex* write = &b; void Swap() { Tex* t = read; read = write; write = t; } };

    void InitCommon(HWND hwnd, int width, int height, const FluidConfig& cfg);
    void CreateDevice(HWND hwnd, int width, int height);
    void CreateOffscreenTarget();      // headless render target + readback
    void RenderDisplayOffscreen();     // display pass -> m_shotTex
    void CreateSimResources();
    Tex  CreateTex(int w, int h, DXGI_FORMAT fmt, int heapSlot);
    void Transition(Tex& t, D3D12_RESOURCE_STATES to);
    void UavBarrier(ID3D12Resource* res);
    void BeginFrame();
    void EndFrameAndPresent();
    void SimStep(float dt);
    void Splat(float x, float y, float dx, float dy, float r, float g, float b);
    void MultipleSplats(int amount);
    void RenderDisplay();
    void RenderMirror();
    void BuildDisplayConstants(float out[32]);
    void BuildDisplayConstantsEx(float out[32], int w, int h, float sdrScale, float peakNits);
    void MaybeRenderAnalyzer();
    void CreateAnalyzerResources();
    void RenderGradient(float timeSec);
    void ReportStats();
    void WaitForGpuIdle();
    // M3 behaviors (ports of the reference's custom features)
    void InitWanderers();
    float WheelHue(float offset);
    void PickSplatColor(float hueOffset, float out[3]);
    void UpdateWanderers(float dt);
    void UpdateDart(float dt);
    void UpdateHueShift(float dt);
    void UpdateCoverage();          // schedule/process the 48x27 governor readback
    void ProcessCoverage(const uint8_t* data, UINT pitch);
    void HandleInput(const FrameInput& in);
    // --- Liquid Acid look ---
    void CreateAcidBuffers();       // blob SRV + param CBV upload rings (always)
    void SeedAcidBlobs();           // deterministic under the shot seed
    void StepAcidBlobs(float dt);   // CPU sim: fluid advection + curl + repulsion
    void UpdateVelocityReadback();  // 64x36 velocity downsample -> CPU (1 frame late)
    void UploadAcidConstants();     // fills this frame's blob + param upload buffers
    void BindAcid();                // root SRV/CBV for the display draw
    // Which display PSO this frame uses. Identical to m_psoDisplay unless the
    // Liquid Acid look is on AND its variant compiled.
    ID3D12PipelineState* DisplayPso() const {
        return (m_cfg.acid.enabled && m_psoLiquidAcid) ? m_psoLiquidAcid.Get()
                                                       : m_psoDisplay.Get();
    }

    FluidConfig m_cfg;
    int m_width = 0, m_height = 0;
    int m_simW = 0, m_simH = 0, m_dyeW = 0, m_dyeH = 0;
    float m_time = 0.0f;
    float m_emitScale = 1.0f;   // dt / (1/60): keeps per-second dye emission fps-independent
    float m_sdrScale = 1.0f;
    float m_globalHue = 0.0f;
    float m_idleTimer = 0.0f;
    float m_statsTimer = 0.0f;
    bool  m_hdrActive = false;
    bool  m_firstFrame = true;
    bool  m_headless = false;   // --shot: no swap chain, no window, no Present

    static const UINT kFrames = 3;
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_backBuffers[kFrames];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocators[kFrames];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmd;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValues[kFrames] = {};
    UINT64 m_nextFence = 1;
    UINT m_rtvStride = 0;
    UINT m_frameIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;   // shader visible
    UINT m_srvStride = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_computeRS;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_graphicsRS;
    // compute PSOs
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoClearV, m_psoClear4, m_psoClear1,
        m_psoCurl, m_psoVorticity, m_psoDivergence, m_psoClearPressure, m_psoPressure,
        m_psoGradSub, m_psoAdvectVel, m_psoAdvectDye, m_psoSplatVel, m_psoSplatDye,
        m_psoDownsample, m_psoDiffuseDye;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoDisplay;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoGradient;

    DoubleTex m_velocity, m_dye, m_pressure;
    Tex m_divergence, m_curl;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_readback;  // stats
    UINT m_readbackPitch = 0;
    bool m_readbackPending = false;

    // headless capture target (--shot)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shotTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shotReadback;
    D3D12_RESOURCE_STATES m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT m_shotPitch = 0;

    // --- M3 state ---
    bool m_prevMouseDown = false;
    bool m_presentBroken = false;

    // second-monitor mirror
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_mirrorChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_mirrorBuffers[3];
    int   m_mirrorW = 0, m_mirrorH = 0;
    float m_mirrorSdrScale = 1.0f, m_mirrorPeakNits = 0.0f;
    bool  m_mirrorBroken = false;
    struct Wanderer {
        float x, y, heading, turn;   // random-wander state
        float cx, cy, R, phase;      // circle/figure8 state
        int   dir;
        float hueOffset;
    };
    std::vector<Wanderer> m_wanderers;
    float m_lastInteraction = -1000.0f;

    // coverage governor (48x27 downsample, async readback, 1 Hz)
    static const int kCovW = 48, kCovH = 27;
    Tex m_coverage;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_covReadback;
    UINT   m_covPitch = 0;
    UINT64 m_covFence = 0;
    bool   m_covPending = false;
    float  m_lastCovTime = -10.0f;
    bool   m_screenTooFull = false;
    bool   m_survivorTooFull = false;
    bool   m_coverageWanted = false;   // conductor override: readback w/o governor
    float  m_darkPct = 100.0f;         // last measured dark-area %
    float  m_avgHue = 0.0f;            // circular mean hue (deg) of lit dye

    struct { float x, y, ux, uy, left; bool active = false; } m_dart;
    float m_lastDartTime = -1000.0f;

    // hue-shift cycler: 0 off, 1 glide, 2 linger, 3 return
    int   m_hsPhase = 0;
    float m_hsTimer = 0, m_hsFrom = 0, m_hsTo = 0;
    int   m_hsStep = 0;
    float m_hueAngle = 0;
    bool  m_hsCommanded = false;       // external (conductor) owns the angle
    float m_hsGlideOverride = 0.0f;    // 0 = use cfg.hsGlide

    // --- Liquid Acid look (all inert unless m_cfg.acid.enabled) ---
    static const int kAcidMaxBlobs = 128;
    static const int kVelW = 64, kVelH = 36;
    struct AcidBlob {
        float x, y;          // centre, uv (y down)
        float vx, vy;        // uv / s
        float baseR;         // uv (y units)
        float phase;         // breathing phase
        float breathRate;    // rad / s
        float wgt;           // +1 (oil) or -holeWeight (hole / bubble of water)
        int   colIdx;        // oil palette slot; resolved at upload so a swept
                             // palette reaches the blobs too
        float s1, s2;        // curl-drift phase offsets
        int   kind;          // 0 disc, 1 web, 2 bubble, 3 hole
    };
    std::vector<AcidBlob> m_acidBlobs;
    bool   m_acidSeeded = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_acidBlobUpload[kFrames];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_acidParamUpload[kFrames];
    void*  m_acidBlobData[kFrames] = {};
    void*  m_acidParamData[kFrames] = {};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoLiquidAcid;
    // low-res velocity readback (same async pattern as the coverage governor)
    Tex    m_velLow;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_velReadback;
    UINT   m_velPitch = 0;
    UINT64 m_velFence = 0;
    float  m_lastVelTime = -10.0f;
    bool   m_velPending = false;
    std::vector<float> m_velCpu;    // kVelW*kVelH*2, sim texels / s

    // HDR analyzer
    bool   m_anaEnabled = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_anaTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_anaReadback;
    D3D12_RESOURCE_STATES m_anaState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT   m_anaPitch = 0;
    UINT64 m_anaFence = 0;
    bool   m_anaPending = false;
    float  m_lastAnaTime = -10.0f;
};
