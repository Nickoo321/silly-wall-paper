// Oil3DWallpaper — the live oil (FluidWallpaper's style=liquid_acid) as a TRUE
// 3D raymarched lava lamp. Brief: reference/briefs/NEXT-oil3d.md.
//
// HEADLESS ONLY in this prototype — no swap chain, no window, no WorkerW
// attach. InitOffscreen is the only entry point.
//
// Two halves:
//   * Oil3DSim   — the CPU blob sim: a port of the 2D rise mode
//                  (src/fluid.cpp SeedAcidBlobs / StepAcidBlobs /
//                  StepAcidTurnover / AdjustAcidBlobCount @8a4f185) with a z
//                  axis and an analytic curl flow instead of the 2D fluid's
//                  velocity, plus the palette clock (EffectivePalette, the
//                  anchor warp, palette_start_hue, the split-tone solve).
//   * Oil3DRenderer — D3D12: one raymarch pass (fullscreen triangle into an
//                  RGBA16F target, optionally half res) and one display pass
//                  (upsample, HDR highlight lift, sdrScale) into the shot
//                  target. Device/frame-ring/timestamp/readback code is
//                  copied, not shared, from src_ink3d/ink3d.cpp (the
//                  standalone-target rule).
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>
#include "config.h"

using Microsoft::WRL::ComPtr;

static const int kOil3DMaxBlobs = 64;   // two 32-bit masks per ray in the shader
static const int kOil3DMaxDrops = 64;

// ---------------------------------------------------------------------------
// CPU sim
// ---------------------------------------------------------------------------
struct Blob3 {
    float x = 0, y = 0, z = 0;       // sim coords (config.h UNITS)
    float vx = 0, vy = 0, vz = 0;    // per second
    float baseR = 0.1f, rTarget = 0.1f;
    float phase = 0, breathRate = 0.2f;
    float wgt = 1.0f;                // +1 oil, -hole_weight hole
    int   colIdx = 0;
    int   kind = 0;                  // 0 disc, 1 web, 2 bubble, 3 hole
    float s1 = 0, s2 = 0, s3 = 0;    // flow phases
    float accentMix = -1.0f;
    int   accentSmall = -1;
    float born = 0.0f;
};

struct Drop3 {
    float x = 0, y = 0, z = 0, r = 0.005f;
    float climb = 0.01f;             // screen heights / s
    float phase = 0, wob = 0;
    int   racer = 0;
};

// Everything colour the renderer needs for one frame, resolved on the CPU.
struct FramePalette {
    float oil[12];        // four oil shades (sRGB-encoded, the 2D convention)
    float ink[12];        // ink ramp (bands mode)
    float hue2Deg = 0, hue2Amt = 0, hue3Deg = 0, hue3Amt = 0;
    float massRgb[3] = { 0, 0, 0 };   // dye of the masses (sRGB at value 1)
    float massAmt = 0;
    float dropRgb[3] = { 0, 0, 0 };   // dye of the droplets
    float dropAmt = 0;
    float toneAdd[3] = { 0, 0, 0 };   // split tone, sRGB-encoded additive
    float paletteHueDeg = 0;          // HSV hue of oil[0], for the log
};

class Oil3DSim {
public:
    void Init(const Oil3DConfig& cfg, unsigned seed, float aspect);
    void Step(float dt);
    void Palette(FramePalette& out) const;

    const std::vector<Blob3>& Blobs() const { return m_blobs; }
    const std::vector<Drop3>& Drops() const { return m_drops; }
    double Time() const { return m_time; }
    int    WantBlobs() const { return m_want; }
    // anisotropy for rise_stretch, the 2D formula (UploadAcidConstants)
    float  Stretch(const Blob3& b) const;
    // accent_mode 1 colour of blob i (sRGB), from the frame palette
    void   BlobColor(int i, const FramePalette& p, float rgb[3]) const;
    void   LogState(const char* tag) const;

private:
    float Rf();                       // respawn stream
    float DrawR(int kind);            // a radius from the kind's range
    void  SeedBlobs();
    void  SeedDrops();
    void  StepBlobs(float dt);
    void  StepTurnover(float dt);
    void  AdjustCount();
    void  StepDrops(float dt);
    void  RespawnDrop(Drop3& d, bool below);
    float PaletteClock() const { return (float)(m_time - m_palOffset); }
    void  EffectivePalette(float clk, float effOil[12], float effInk[12]) const;
    float AnchorWarpDeg(float u, float h0Deg, float w) const;
    float PalettePhaseForHue(float hueDeg) const;
    void  ApplyPaletteStart();

    Oil3DConfig m_cfg;
    float    m_aspect = 16.0f / 9.0f;
    unsigned m_seed = 1234;
    double   m_time = 0.0;
    double   m_palOffset = 0.0;       // palette clock = time - offset
    int      m_want = 0;
    std::vector<Blob3> m_blobs;
    std::vector<Drop3> m_drops;
    uint32_t m_rng = 1, m_respawnRng = 1, m_turnRng = 1, m_dropRng = 1;
    float    m_turnAcc = 0.0f;
    mutable float m_anchorCdf[257] = {};
    mutable float m_anchorCdfW = -1.0f;
};

// ---------------------------------------------------------------------------
// renderer
// ---------------------------------------------------------------------------
class Oil3DRenderer {
public:
    void InitOffscreen(int width, int height, const Oil3DConfig& cfg, unsigned seed);
    void Shutdown();

    // one simulated + rendered frame
    void Frame(float dt, float sdrScale);
    bool CaptureOffscreen(std::vector<float>& outRgba);   // linear scRGB floats

    Oil3DSim& Sim() { return m_sim; }
    const Oil3DConfig& Config() const { return m_cfg; }

    void ResetGpuStats();
    int  PassCount() const { return (int)m_passNames.size(); }
    const char* PassName(int i) const { return m_passNames[i].c_str(); }
    double PassMsMean(int i) const { return m_passSamples ? m_passSum[i] / m_passSamples : 0.0; }
    double PassMsMin(int i) const { return m_passSamples ? m_passMin[i] : 0.0; }
    double FrameMsMean() const;
    int  GpuSamples() const { return m_passSamples; }
    int  RayW() const { return m_rayW; }
    int  RayH() const { return m_rayH; }

private:
    static const UINT kFrames = 3;
    static const UINT kMaxTs  = 8;
    static const UINT kCbBytes = 1024;   // 64 float4, 256-aligned

    void CreateDevice();
    void CreatePipelines();
    void CreateTargets();
    void CreateUploads();
    void BeginFrame();
    void EndFrame();
    void WaitForGpuIdle();
    void Mark(const char* name);
    void CollectTimings(UINT slot);
    void Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES& cur, D3D12_RESOURCE_STATES to);
    void UploadFrame(float sdrScale);
    void RenderRaymarch();
    void RenderDisplay();

    Oil3DConfig m_cfg;
    Oil3DSim    m_sim;
    int  m_width = 0, m_height = 0, m_rayW = 0, m_rayH = 0;

    ComPtr<IDXGIFactory6>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_queue;
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap, m_srvHeap;
    ComPtr<ID3D12CommandAllocator>    m_allocators[kFrames];
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    ComPtr<ID3D12Fence>               m_fence;
    HANDLE                            m_fenceEvent = nullptr;
    UINT64                            m_fenceValues[kFrames] = {};
    UINT64                            m_nextFence = 1;
    UINT                              m_frameIndex = 0;
    UINT                              m_rtvStride = 0;

    ComPtr<ID3D12RootSignature> m_rs;
    ComPtr<ID3D12PipelineState> m_psoRaymarch, m_psoDisplay;

    ComPtr<ID3D12Resource> m_shotTex, m_shotReadback, m_rayTex;
    D3D12_GPU_DESCRIPTOR_HANDLE m_table[kFrames] = {};   // per-frame b0/t0/t1/t2 table
    UINT m_shotPitch = 0;
    D3D12_RESOURCE_STATES m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_rayState  = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // per-frame upload ring: constants, blobs, droplets
    ComPtr<ID3D12Resource> m_cbUp[kFrames], m_blobUp[kFrames], m_dropUp[kFrames];
    void* m_cbPtr[kFrames] = {};
    void* m_blobPtr[kFrames] = {};
    void* m_dropPtr[kFrames] = {};

    // timing
    ComPtr<ID3D12QueryHeap> m_tsHeap;
    ComPtr<ID3D12Resource>  m_tsReadback;
    UINT64 m_tsFreq = 0;
    UINT   m_tsCount = 0;
    UINT   m_tsWritten[kFrames] = {};
    bool   m_tsValid[kFrames] = {};
    std::vector<std::string> m_passNames;
    double m_passSum[kMaxTs] = {};
    double m_passMin[kMaxTs] = {};
    int    m_passSamples = 0;
};
