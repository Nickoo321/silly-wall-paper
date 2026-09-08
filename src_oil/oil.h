// OilWallpaper — lava-lamp oil-blob renderer (proof of concept).
// One fullscreen-triangle pixel shader evaluates a 64-blob metaball field
// per pixel; a tiny CPU sim drifts the blobs. No compute passes, no HDR
// (SDR sRGB only — HDR output is a follow-up).
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

struct OilConfig {
    int   palette     = 0;      // 0 = Acid, 1 = Royal
    float marbleScale = 70.0f;  // fbm frequency inside blobs (aspect-corrected uv)
    float rimWidth    = 0.022f; // dark rim outline width, uv units
    float fpsLimit    = 60.0f;
};

class OilRenderer {
public:
    void Init(HWND hwnd, int width, int height, const OilConfig& cfg);
    void Shutdown();
    void Reattach(HWND hwnd);       // swapchain-only rebuild (Explorer restart)
    void Frame(float dt);
    bool PresentBroken() const { return m_presentBroken; }
    void SetPalette(int p)     { m_cfg.palette = p & 1; }
    int  Palette() const       { return m_cfg.palette; }
    OilConfig& Config()        { return m_cfg; }
    // --shot support: dump one rendered frame (post-present GPU readback) to
    // a BMP after `delay` seconds; ShotDone() flips true once written.
    void RequestSnapshot(const char* pathBmp, float delay) {
        m_shotPath = pathBmp;
        m_shotDelay = delay;
        m_shotPending = true;
    }
    bool ShotDone() const { return m_shotDone; }

private:
    void CreateDevice();
    void CreateSwapChainResources(HWND hwnd, int width, int height);
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
        bool  sinker;
    };

    OilConfig m_cfg;
    int       m_width = 0, m_height = 0;
    float     m_time = 0.0f;
    bool      m_seeded = false;
    bool      m_presentBroken = false;
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

    // --shot state
    bool                              m_shotPending = false;
    bool                              m_shotDone = false;
    float                             m_shotDelay = 0.0f;
    std::string                       m_shotPath;
    ComPtr<ID3D12Resource>            m_readback;
    UINT                              m_readbackPitch = 0;
    void WriteSnapshotBmp();
};
