// Oil3DLab — look lab for the Oil3D prototype (local, GPU-verified).
//
// ONE fullscreen-triangle pixel shader raymarches ~50 Wyvill metaballs (plus a
// few negative "hole" tunnels) inside a shallow slab behind the screen; a
// second tiny pass upsamples (half-res) and applies sdrScale. The blob sim is
// CPU-only (fixed 60 Hz, seeded), so a --shot renders only the frames it
// captures (plus the --timings frames): the GPU is touched for a few frames.
//
// HEADLESS ONLY: no window, no swap chain, no WorkerW. InitOffscreen is the
// only entry point. Device/readback/timestamp code is copied (not shared) from
// src_ink3d/ink3d.cpp.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

enum { LAB_GLOSS = 0, LAB_FLAT = 1 };

// Defaults = reference/presets/Layout - Few giants.ini over
// reference/configs/monotone-post-0924.ini, loosely (sizes are the 2D
// meanings: support radius = base radius * support_scale, so an isolated blob
// shows at 0.55 x its base radius with threshold 0.79).
struct LabConfig {
    int   blobs        = 50;      // incl. holes; <= 64
    int   holes        = 4;       // negative tunnels, attached to the giants
    float discFrac     = 0.22f;   // giants
    float webFrac      = 0.34f;   // mid blobs, random-walk veins
    float discMin = 0.16f, discMax = 0.38f;
    float webMin  = 0.07f, webMax  = 0.20f;
    float bubbleMin = 0.015f, bubbleMax = 0.075f;
    float holeMin = 0.20f, holeMax = 0.30f;     // x the parent's base radius
    float size         = 0.75f;   // 3D scale on every 2D radius (2D sizes merge into one mass)
    float holeWeight   = 2.5f;
    float holeTunnel   = 2.5f;    // z stretch: bores through the parent (3 bit neighbours in front/behind)
    float bigBias      = 1.15f;
    float sizeBias     = 2.9f;
    float threshold    = 0.79f;
    float supportScale = 2.0f;
    float riseStretch  = 0.45f;
    float riseSpeed    = 0.0195f; // screen heights / s
    float riseWobble   = 0.5f;
    float flowGain     = 1.15f;
    float repulsion    = 0.9f;
    float depth        = 0.6f;    // slab depth, screen heights
    float fovDeg       = 38.0f;
    float cameraDrift  = 0.03f;
    int   steps        = 64;
    bool  halfRes      = false;
    int   shade        = LAB_GLOSS;
    float level        = 0.85f;   // palette level
    float chroma       = 1.30f;   // linear chroma gain on the palette ([hdr] saturation x post_chroma)
    float inkLevel     = 0.035f;  // backdrop marbling (the scheme's ink is black)
    float peakNits     = 1000.0f;
};

struct LabBlob {
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;   // world units, +y UP
    float baseR = 0.1f, r = 0.1f;
    float phase = 0, breath = 0.2f, s1 = 0, s2 = 0, s3 = 0;
    float zc = 0.3f, zAmp = 0.1f, zRate = 0.05f;
    float wgt = 1.0f;
    int   kind = 0;          // 0 disc, 1 web, 2 bubble, 3 hole
    int   col = 0;           // palette shade 0..3
    int   parent = -1;       // holes only
    float hfrac = 0.25f, hang = 0.0f, hdist = 0.0f;
    bool  accent = false;
    float id = 0.0f;
};

class LabRenderer {
public:
    void InitOffscreen(int width, int height, const LabConfig& cfg, unsigned seed);
    void Step(float dt);                         // CPU sim, fixed step
    void Render(float sdrScale, float hiGain, float maxOut);   // one GPU frame
    bool CaptureOffscreen(std::vector<float>& out);
    void Shutdown();
    double SimTime() const { return m_time; }
    void LogBlobs() const;

    int  PassCount() const { return (int)m_passNames.size(); }
    const char* PassName(int i) const { return m_passNames[i].c_str(); }
    double PassMsMean(int i) const { return m_passSamples ? m_passSum[i] / m_passSamples : 0.0; }
    double PassMsMin(int i) const { return m_passSamples ? m_passMin[i] : 0.0; }
    int  GpuSamples() const { return m_passSamples; }
    void ResetGpuStats();
    int  RayW() const { return m_rayW; }
    int  RayH() const { return m_rayH; }

private:
    static const UINT kFrames = 3;
    static const UINT kMaxTs  = 8;
    static const int  kMaxBlobs = 64;

    void CreateDevice();
    void CreatePipelines();
    void CreateTargets();
    void Seed();
    void Respawn(LabBlob& b, bool atBottom);
    float Rand();
    float Rand(float lo, float hi) { return lo + (hi - lo) * Rand(); }
    float VisR(float baseR) const;
    float HalfH(float z) const;
    void BeginFrame();
    void EndFrame();
    void Mark(const char* name);
    void CollectTimings(UINT slot);
    void WaitForGpuIdle();
    void Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES& cur, D3D12_RESOURCE_STATES to);

    LabConfig m_cfg;
    unsigned  m_rng = 1u;
    unsigned  m_seed = 1234u;
    double    m_time = 0.0;
    float     m_camDist = 1.45f;
    std::vector<LabBlob> m_blobs;
    float m_noisePh[6] = {};

    int m_width = 0, m_height = 0, m_rayW = 0, m_rayH = 0;

    ComPtr<IDXGIFactory6>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_queue;
    ComPtr<ID3D12CommandAllocator>    m_allocators[kFrames];
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    ComPtr<ID3D12Fence>               m_fence;
    HANDLE                            m_fenceEvent = nullptr;
    UINT64                            m_fenceValues[kFrames] = {};
    UINT64                            m_nextFence = 1;
    UINT                              m_frameIndex = 0;

    ComPtr<ID3D12RootSignature>  m_rs;
    ComPtr<ID3D12PipelineState>  m_psoRay, m_psoDisplay;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_srvHeap;
    UINT m_rtvStride = 0;

    ComPtr<ID3D12Resource> m_rayTex, m_shotTex, m_shotReadback, m_blobBuf;
    D3D12_RESOURCE_STATES  m_rayState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES  m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT   m_shotPitch = 0;
    uint8_t* m_blobMapped = nullptr;

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
