// Ink3DWallpaper — 3D "ink in water" renderer (INK3D-PLAN.md branch 4).
//
// Milestones M0-M2 only: grid + raymarch, advection + pressure, plume look.
// HEADLESS ONLY — there is no swap chain, no window and no WorkerW attach in
// this build (M4). InitOffscreen is the only entry point.
//
// Device/offscreen-target/frame-ring/capture code is copied, not shared, from
// src/fluid.cpp @873a6dc (AGENTS.md: standalone targets share no sources);
// the timestamp GPU timer follows src_oil/oil.cpp:415-455, 838-891.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "config.h"

using Microsoft::WRL::ComPtr;

struct Tex3 {
    ComPtr<ID3D12Resource>      res;
    D3D12_GPU_DESCRIPTOR_HANDLE srv{}, uav{};
    D3D12_RESOURCE_STATES       state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    int w = 0, h = 0, d = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
};

// One multigrid level: ping-pong solution, its own right-hand side, its own
// h^2 (1 on the finest level, 4^level below it).
struct MgLevel {
    Tex3  p[2];
    Tex3  rhsStore;          // levels >= 1 own their rhs
    Tex3* rhs = nullptr;     // level 0 points at the divergence texture, so
                             // resource-state tracking is never duplicated
    int   cur = 0;
    int   w = 0, h = 0, d = 0;
    float h2 = 1.0f;
    Tex3& P()       { return p[cur]; }
    Tex3& Scratch() { return p[1 - cur]; }
    void  Flip()    { cur = 1 - cur; }
};

struct Ink3DDrop {
    float nx = 0.5f, ny = 0.08f, nz = 0.5f;   // box-normalized
    float radius = 4.0f;                      // velocity voxels
    float vx = 0.0f, vy = 80.0f, vz = 0.0f;   // voxel/s, +y = down
    float amount = 1.0f;
};

struct Ink3DStats {
    double divBefore = 0.0;    // mean |div| straight after the divergence pass
    double divAfter  = 0.0;    // mean |div| of the projected velocity
    double residual  = 0.0;    // mean |b - A p| after the pressure solve
    double rhsNorm   = 0.0;    // mean |b|, so residual can be read relatively
    double densityMax = 0.0;
    double densityMean = 0.0;
    bool   valid = false;
};

class Ink3DRenderer {
public:
    void InitOffscreen(int width, int height, const Ink3DConfig& cfg);
    void Shutdown();

    // One presented frame. simEnabled=false re-renders the last density
    // (M0's static scene, and the throttle path later).
    void Frame(float dt, float sdrScale, bool simEnabled);

    void QueueDrop(const Ink3DDrop& d) { m_queued.push_back(d); }
    void FillStaticScene();               // M0: analytic sphere + tilted sheet

    bool CaptureOffscreen(std::vector<float>& outRgba);   // linear scRGB floats
    Ink3DStats MeasureStats();            // extra GPU work — --stats only

    Ink3DConfig& Config() { return m_cfg; }
    void SetSeed(unsigned s) { m_seed = s; }
    double SimTime() const { return m_time; }

    // per-pass GPU timings (ms), accumulated since the last reset
    void ResetGpuStats();
    int  PassCount() const { return (int)m_passNames.size(); }
    const char* PassName(int i) const { return m_passNames[i].c_str(); }
    double PassMsMean(int i) const {
        return m_passSamples ? m_passSum[i] / m_passSamples : 0.0;
    }
    double PassMsMin(int i) const { return m_passSamples ? m_passMin[i] : 0.0; }
    double FrameMsMean() const;
    int  GpuSamples() const { return m_passSamples; }
    bool UsingRedBlack() const { return m_redBlack; }

    int VelW() const { return m_velW; } int VelH() const { return m_velH; } int VelD() const { return m_velD; }
    int DenW() const { return m_denW; } int DenH() const { return m_denH; } int DenD() const { return m_denD; }
    int RayW() const { return m_rayW; } int RayH() const { return m_rayH; }
    int MgLevels() const { return (int)m_mg.size(); }

private:
    static const UINT kFrames = 3;
    static const UINT kMaxTs  = 24;

    void CreateDevice();
    void CreateOffscreenTarget();
    void CreateRaymarchTarget();
    void CreatePipelines();
    void CreateSimResources();
    Tex3 CreateTex3(int w, int h, int d, DXGI_FORMAT fmt, int heapSlot);

    void BeginFrame();
    void EndFrame();
    void Transition(Tex3& t, D3D12_RESOURCE_STATES to);
    void UavBarrier(ID3D12Resource* res);
    void WaitForGpuIdle();
    void Mark(const char* name);
    void CollectTimings(UINT slot);

    // pass helpers
    void SetCB(int dw, int dh, int dd);
    void Dispatch(ID3D12PipelineState* pso, Tex3* s0, Tex3* s1, Tex3* s2, Tex3* s3,
                  Tex3* dst, int gx = 8, int gy = 8, int gz = 4);
    void DispatchInPlace(ID3D12PipelineState* pso, Tex3* s3, Tex3* inout);

    void Inject();
    void SolvePressure();
    void Smooth(MgLevel& lv, int sweeps);
    void VCycle();
    void RenderRaymarch();
    void RenderDisplay(float sdrScale);
    bool ReadbackTex3(Tex3& t, std::vector<float>& out);

    Ink3DConfig m_cfg;
    int      m_width = 0, m_height = 0;
    int      m_rayW = 0, m_rayH = 0;
    int      m_velW = 0, m_velH = 0, m_velD = 0;
    int      m_denW = 0, m_denH = 0, m_denD = 0;
    int      m_occW = 0, m_occH = 0, m_occD = 0;
    int      m_ltW = 0, m_ltH = 0, m_ltD = 0;
    double   m_time = 0.0;
    unsigned m_seed = 1234;
    bool     m_redBlack = false;
    std::vector<Ink3DDrop> m_queued;

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
    UINT                              m_rtvStride = 0, m_srvStride = 0;

    ComPtr<ID3D12RootSignature> m_computeRS, m_graphicsRS;
    ComPtr<ID3D12PipelineState> m_psoClear4, m_psoClear1, m_psoInitScene,
        m_psoInjectVel, m_psoInjectDen, m_psoCurl, m_psoForces,
        m_psoAdvVelFwd, m_psoAdvVelBack, m_psoAdvVelSL,
        m_psoAdvDenFwd, m_psoAdvDenBack, m_psoAdvDenSL, m_psoSharpen,
        m_psoDivergence, m_psoJacobi, m_psoRB, m_psoGradSub,
        m_psoResidual, m_psoRestrict, m_psoProlong, m_psoOccupancy, m_psoLight;
    ComPtr<ID3D12PipelineState> m_psoRaymarch, m_psoDisplay;

    // fields
    Tex3 m_vel[2], m_velTmp, m_curl, m_div;
    Tex3 m_den[2], m_denTmp, m_occ, m_light;
    int  m_velCur = 0, m_denCur = 0;
    std::vector<MgLevel> m_mg;            // [0] is the finest (192x108x96)

    // targets
    ComPtr<ID3D12Resource> m_shotTex, m_shotReadback, m_rayTex;
    D3D12_GPU_DESCRIPTOR_HANDLE m_raySrv{};
    UINT m_shotPitch = 0;
    D3D12_RESOURCE_STATES m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_rayState  = D3D12_RESOURCE_STATE_RENDER_TARGET;

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

    // scratch for readbacks (--stats)
    ComPtr<ID3D12Resource> m_rbBuf;
    UINT64                 m_rbSize = 0;
};
