// OilWallpaper — lava-lamp oil-blob renderer (proof of concept).
// One fullscreen-triangle pixel shader evaluates a 64-blob metaball field
// per pixel; a small CPU sim drifts, repels and recirculates the blobs.
// No compute passes, no HDR (SDR sRGB only — HDR output is a follow-up).
//
// Phase 0 look fixes (OIL-REVIEW.md §5) are gated by OilConfig::lookStep so
// every step can be A/B'd from one build:
//   0 = pre-Phase-0 look          5 = + marbling advected with the blobs
//   1 = + gradient guard          6 = + repulsion/cohesion + rise-cool-sink
//   2 = + dominant-colour pick    7 = + animated output dither
//   3 = + backdrop refraction     8 = + retuned palettes / distribution
//   4 = + Beer-Lambert, flat gloss, meniscus specular, thin hot rim
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Every tunable the pixel shader and the sim read lives here, so the later
// ini/settings layer only has to bind names to these fields.
struct OilConfig {
    int      palette   = 0;        // 0 = Acid, 1 = Royal
    float    fpsLimit  = 60.0f;
    int      lookStep  = 8;        // Phase 0 A/B gate (see header comment)
    unsigned seed      = 0;        // 0 = time-seeded, else deterministic

    // --- metaball field ---------------------------------------------------
    float cutoff     = 0.08f;      // compact tail cutoff on r^2/d^2
    float clampW     = 2.6f;       // per-blob influence clamp. Low values flatten
                                   // each blob's own bump so a merged mass reads
                                   // as one body, not a bag of balls.

    // --- colour -----------------------------------------------------------
    float domSharp   = 3.2f;       // dominant-colour selection sharpness
                                   // (higher = harder argmax, ~0 = old mean)
    float domJitter  = 0.90f;      // marble-noise jitter on the colour weights,
                                   // so dye boundaries wisp instead of drawing
                                   // perfect circular arcs inside a mass

    // --- refraction -------------------------------------------------------
    float refractK   = 0.055f;     // backdrop displacement at the rim, uv units

    // --- Beer-Lambert body ------------------------------------------------
    float absorbK    = 1.20f;      // absorption scale (sigma = -log(dye)*absorbK)
    float scatterK   = 1.00f;      // how fast the dye's own colour saturates
    float thickPow   = 0.70f;      // field -> thickness shaping exponent
    float thickScale = 1.60f;      // rate of the saturating thickness curve
    float thickMax   = 1.80f;      // thickness the interior saturates toward.
                                   // The curve is exponential-saturating, not a
                                   // hard clamp, so merged masses read as one
                                   // body instead of a pile of lens bumps.
    float throughGain= 1.70f;      // gain on the transmitted backdrop (the
                                   // lens concentrates it, so > 1 reads right)

    // --- marbling ---------------------------------------------------------
    float marbleScale= 18.0f;      // fbm frequency (cells per aspect-corrected uv)
    float marbleAmt  = 0.70f;      // thickness modulation depth
    float marbleBright= 0.22f;     // brightness modulation depth

    // --- rim / gloss ------------------------------------------------------
    float rimWidth   = 0.0026f;    // thin hot rim half-width, uv units (~4 px
                                   // at 1440p — wider reads as an airbrushed
                                   // bevel, and a big hot area trips ABL)
    float rimGain    = 0.90f;      // rim colour strength
    float glossTilt  = 0.70f;      // fake-normal tilt at the edge
    float glossFalloff = 2.80f;    // how fast the tilt dies toward the core
                                   // (was 0.45 pre-Phase-0 = broad plastic shade)
    float glossDiff  = 0.16f;      // diffuse shading depth (flat interiors)
    float specAmt    = 0.62f;      // specular strength inside the meniscus band
    float specPower  = 90.0f;
    float menInner   = 0.0045f;    // meniscus band, uv units from the edge
    float menOuter   = 0.0170f;

    // --- backdrop / output ------------------------------------------------
    float grainAmt   = 0.022f;     // output dither amplitude
    float bgBlotchAmt= 0.22f;      // fbm blotch depth on the backdrop (gives the
                                   // refraction something to bend)
    float bgGamma    = 1.15f;      // vertical gradient exponent

    // --- sim: drift + damping --------------------------------------------
    float driftK     = 0.0030f;    // pseudo-curl drift gain
    float damping    = 2.5f;       // velocity relaxation rate (1/s)

    // --- sim: buoyancy / recirculation -----------------------------------
    float buoyK        = 0.045f;   // uv/s at temp 0 or 1 (~20 s per screen)
    float buoyRadiusAmt= 0.15f;    // 0 = fully decoupled from radius
    float buoyJitter   = 0.35f;    // per-blob buoyancy spread, breaks up rafts
    float zoneJitter   = 0.12f;    // per-blob offset on the heat/cool zones, uv.
                                   // Without it the whole population flips at
                                   // the same heights and re-synchronises into
                                   // one horizontal convection band.
    float heatRate     = 1.40f;    // temp/s gained near the bottom
    float coolRate     = 1.60f;    // temp/s lost near the top
    float tempRelax    = 0.00f;    // relaxation toward ambient (1/s). Keep at 0:
                                   // ANY ambient pull drags every blob to the
                                   // neutral temperature, where buoyancy is zero,
                                   // and the whole population parks in a raft.
    float heatY0       = 0.78f;    // heating ramp (uv y down: 1 = bottom). Keep
                                   // the flip zones hard against the edges or
                                   // blobs turn around early and orbit in a
                                   // horizontal raft across the middle.
    float heatY1       = 1.00f;
    float coolY0       = 0.00f;    // cooling ramp near the top
    float coolY1       = 0.22f;

    // --- sim: surface tension / walls ------------------------------------
    float repelK       = 0.075f;   // soft repulsion inside touching distance
    float repelRange   = 1.02f;    // x (ri+rj). Below the metaball merge
                                   // distance (~2.6r) so blobs still neck.
    float cohesionK    = 0.000f;   // attraction beyond touch. Off by default:
                                   // any global pull collapses 64 blobs into
                                   // two lumps within a minute.
    float cohesionRange= 2.20f;    // x (ri+rj)
    float yMin         = 0.07f;    // soft ceiling / floor, uv
    float yMax         = 0.94f;
    float wallK        = 0.10f;    // wall push strength

    // --- sim: breathing / seeding ----------------------------------------
    float breathAmt    = 0.15f;    // radius breathing, fraction
    float breathMin    = 0.15f;    // rad/s
    float breathMax    = 0.45f;
    int   bigCount     = 26;       // colour 0 blobs (remainder = droplets)
    int   midCount     = 22;       // colour 1 blobs
    float bigRMin      = 0.040f, bigRMax   = 0.080f;
    float midRMin      = 0.026f, midRMax   = 0.052f;
    float smallRMin    = 0.011f, smallRMax = 0.024f;
};

class OilRenderer {
public:
    void Init(HWND hwnd, int width, int height, const OilConfig& cfg);
    // Headless: no window, no swap chain — renders into an offscreen
    // R8G8B8A8_UNORM target of the requested size (same pixels the swap chain
    // would have received). Used by --shot; never creates or touches a window.
    void InitOffscreen(int width, int height, const OilConfig& cfg);
    void Shutdown();
    void Reattach(HWND hwnd);       // swapchain-only rebuild (Explorer restart)
    void Frame(float dt);
    // CPU-only: advance the blob sim without submitting any GPU work. The oil
    // look has no GPU-side state, so a headless capture can fast-forward to
    // t = 70 s on the CPU and render exactly the frames it keeps.
    void Advance(float dt) { m_time += dt; StepBlobs(dt); }
    // Read the offscreen target back as tightly packed RGBA8 rows.
    bool CaptureOffscreen(std::vector<unsigned char>& outRgba);
    bool PresentBroken() const { return m_presentBroken; }
    void SetPalette(int p)     { m_cfg.palette = p & 1; }
    int  Palette() const       { return m_cfg.palette; }
    OilConfig& Config()        { return m_cfg; }
    // Rolling GPU cost of the fullscreen pass (timestamp query, ms).
    double GpuMsLast() const   { return m_gpuMsLast; }
    double GpuMsMin() const    { return m_gpuSamples ? m_gpuMsMin : 0.0; }
    int    GpuSamples() const  { return m_gpuSamples; }
    double GpuMsMean() const   { return m_gpuSamples ? m_gpuMsSum / m_gpuSamples : 0.0; }
    void   ResetGpuStats()     { m_gpuMsSum = 0.0; m_gpuSamples = 0; m_gpuMsMin = 1e9; }

private:
    void CreateDevice();
    void CreateSwapChainResources(HWND hwnd, int width, int height);
    void CreateOffscreenTarget();
    void CreatePipeline();
    void WaitForGpu();
    void SeedBlobs();
    void StepBlobs(float dt);

    static const int kBlobCount = 64;
    static const int kFrames    = 2;

    struct BlobCPU {
        float x, y;          // uv (y down), center
        float vx, vy;        // uv / s
        float baseR;         // uv (y units)
        float phase;         // breathing phase
        float breathRate;    // rad / s
        float colorIdx;      // 0, 1, 2
        float s1, s2;        // curl-field phase offsets
        float tx, ty;        // texture travel (aspect-corrected p units)
        float buoyScale;     // per-blob buoyancy multiplier
        float zoneOff;       // per-blob heat/cool zone offset (uv)
        float temp;          // 0 = cold (sinks), 1 = hot (rises)
        bool  sinker;        // pre-Phase-0 buoyancy sign
    };

    OilConfig m_cfg;
    int       m_width = 0, m_height = 0;
    float     m_time = 0.0f;
    bool      m_seeded = false;
    bool      m_presentBroken = false;
    bool      m_headless = false;
    BlobCPU   m_blobs[kBlobCount] = {};

    ComPtr<IDXGIFactory6>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_queue;
    ComPtr<IDXGISwapChain3>           m_swapChain;
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    ComPtr<ID3D12Resource>            m_backBuffers[kFrames];
    ComPtr<ID3D12CommandAllocator>    m_allocators[kFrames];
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    ComPtr<ID3D12RootSignature>       m_rootSig;
    ComPtr<ID3D12PipelineState>       m_pso;
    ComPtr<ID3D12Resource>            m_cbUpload[kFrames];
    void*                             m_cbData[kFrames] = {};
    ComPtr<ID3D12Fence>               m_fence;
    HANDLE                            m_fenceEvent = nullptr;
    UINT64                            m_fenceValues[kFrames] = {};
    UINT64                            m_nextFence = 1;
    UINT                              m_frameIndex = 0;
    UINT                              m_rtvStride = 0;

    // headless render target + readback
    ComPtr<ID3D12Resource>            m_shotTex;
    ComPtr<ID3D12Resource>            m_shotReadback;
    UINT                              m_shotPitch = 0;
    D3D12_RESOURCE_STATES             m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // GPU timing (2 timestamps per frame slot)
    ComPtr<ID3D12QueryHeap>           m_tsHeap;
    ComPtr<ID3D12Resource>            m_tsReadback;
    UINT64                            m_tsFreq = 0;
    bool                              m_tsValid[kFrames] = {};
    double                            m_gpuMsLast = 0.0;
    double                            m_gpuMsSum = 0.0;
    double                            m_gpuMsMin = 1e9;
    int                               m_gpuSamples = 0;
};
