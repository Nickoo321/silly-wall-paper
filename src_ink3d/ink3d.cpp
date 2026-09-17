#include "ink3d.h"
#include "shaders.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern void Ink3DFail(const char* what, HRESULT hr);      // main.cpp
extern void Ink3DLog(const char* fmt, ...);               // main.cpp
#define HR(e) do { HRESULT _hr = (e); if (FAILED(_hr)) Ink3DFail(#e, _hr); } while (0)

// Must match `cbuffer CB` in shaders.h (48 DWORDs). Kept file-local, next to
// the HLSL it mirrors; there is one renderer per process.
struct SimCB {
    int   dimsX, dimsY, dimsZ;   float dt;
    float invX, invY, invZ;      float dissipation;
    float vorticity, buoyancy, noiseAmt, sharpen;
    int   dropMinX, dropMinY, dropMinZ; float dropRadius;
    float dropCX, dropCY, dropCZ;       float dropAmount;
    float dropVX, dropVY, dropVZ;       unsigned seed;
    int   passIdx;  float velScale, dissipationFast, decayThreshold;
    int   velDimX, velDimY, velDimZ;    float sinkBottom;
    float invVelX, invVelY, invVelZ;    float h2;
    float omega, timeSec, lightStepX, lightStepY;
    float lightSigma, densityCap, velDissipation, sinkRate;
    float pad0[4];
};
static_assert(sizeof(SimCB) == 48 * 4, "SimCB must match the HLSL cbuffer (48 DWORDs)");
static SimCB g_cb = {};

// render box, normalized: the front face IS the 16:9 screen, depth = half the
// width (enough parallax for veils to pass in front of each other).
static const float kBoxW = 1.0f, kBoxH = 0.5625f, kBoxZ = 0.5f;

static float HalfToFloat(uint16_t h) {   // copied from src/fluid.cpp @873a6dc
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F, man = h & 0x3FF, f;
    if (exp == 0) {
        if (!man) f = sign;
        else { exp = 112; while (!(man & 0x400)) { man <<= 1; exp--; } man &= 0x3FF;
               f = sign | (exp << 23) | (man << 13); }
    } else if (exp == 31) f = sign | 0x7F800000u | (man << 13);
    else f = sign | ((exp + 112) << 23) | (man << 13);
    float r; memcpy(&r, &f, 4); return r;
}

static ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(src, strlen(src), entry, nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
        Ink3DLog("shader '%s' compile error:\n%s\n", entry,
                 err ? (char*)err->GetBufferPointer() : "?");
        ExitProcess(1);
    }
    return blob;
}

static UINT Grp(int n, int g) { return (UINT)((n + g - 1) / g); }

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void Ink3DRenderer::InitOffscreen(int width, int height, const Ink3DConfig& cfg) {
    m_cfg = cfg;
    m_width = width; m_height = height;
    m_rayW = cfg.halfRes ? (width + 1) / 2 : width;
    m_rayH = cfg.halfRes ? (height + 1) / 2 : height;

    m_velW = cfg.gridX; m_velH = cfg.gridY; m_velD = cfg.gridZ;
    // density dims rounded to a multiple of 8 so the occupancy reduce divides
    auto d8 = [&](int v) { int r = (int)lroundf(v * cfg.densityScale / 8.0f) * 8;
                           return r < 8 ? 8 : r; };
    m_denW = d8(m_velW); m_denH = d8(m_velH); m_denD = d8(m_velD);
    m_occW = m_denW / 8; m_occH = m_denH / 8; m_occD = m_denD / 8;
    m_ltW = std::max(8, m_denW / 2); m_ltH = std::max(8, m_denH / 2);
    m_ltD = std::max(8, m_denD / 2);

    CreateDevice();
    CreatePipelines();
    CreateSimResources();
    CreateOffscreenTarget();
    CreateRaymarchTarget();

    Ink3DLog("[ink3d] vel %dx%dx%d (%.2f M)  density %dx%dx%d (%.2f M)  "
             "occ %dx%dx%d  light %dx%dx%d  raymarch %dx%d  mg levels %d  "
             "smoother %s\n",
             m_velW, m_velH, m_velD, m_velW * (double)m_velH * m_velD / 1e6,
             m_denW, m_denH, m_denD, m_denW * (double)m_denH * m_denD / 1e6,
             m_occW, m_occH, m_occD, m_ltW, m_ltH, m_ltD, m_rayW, m_rayH,
             (int)m_mg.size(), m_redBlack ? "red-black GS" : "damped Jacobi");

    // clear every field once
    BeginFrame();
    Dispatch(m_psoClear4.Get(), nullptr, nullptr, nullptr, nullptr, &m_vel[0]);
    Dispatch(m_psoClear4.Get(), nullptr, nullptr, nullptr, nullptr, &m_vel[1]);
    Dispatch(m_psoClear4.Get(), nullptr, nullptr, nullptr, nullptr, &m_velTmp);
    Dispatch(m_psoClear4.Get(), nullptr, nullptr, nullptr, nullptr, &m_curl);
    Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_div);
    Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_den[0]);
    Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_den[1]);
    Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_denTmp);
    Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_occ);
    Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_light);
    for (size_t i = 0; i < m_mg.size(); i++) {
        Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_mg[i].p[0]);
        Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_mg[i].p[1]);
        if (i) Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_mg[i].rhsStore);
    }
    EndFrame();
    WaitForGpuIdle();
}

void Ink3DRenderer::CreateDevice() {
    HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));
    ComPtr<IDXGIAdapter1> adapter;
    HR(m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad = {};
    adapter->GetDesc1(&ad);
    Ink3DLog("[ink3d] GPU: %ls (%llu MB)\n", ad.Description,
             (unsigned long long)(ad.DedicatedVideoMemory >> 20));

    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    // In-place red-black Gauss-Seidel needs typed UAV loads of R16_FLOAT.
    // RDNA3 has them; check anyway and fall back to damped Jacobi (plan §4).
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
        m_redBlack = false;
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o)))
            && o.TypedUAVLoadAdditionalFormats) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { DXGI_FORMAT_R16_FLOAT };
            if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))))
                m_redBlack = (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
        }
    }

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)));
    m_queue->GetTimestampFrequency(&m_tsFreq);

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 4;                 // 0 = shot target, 1 = raymarch target
    HR(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (UINT i = 0; i < kFrames; i++)
        HR(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&m_allocators[i])));
    HR(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmd)));
    HR(m_cmd->Close());
    HR(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 96;                // 2 per Tex3 (SRV, UAV), 48 slots
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)));
    m_srvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // timestamps: kMaxTs per frame slot
    D3D12_QUERY_HEAP_DESC qh = {};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = kFrames * kMaxTs;
    HR(m_device->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_tsHeap)));
    {
        D3D12_HEAP_PROPERTIES rb = {}; rb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = kFrames * kMaxTs * sizeof(UINT64);
        bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&m_tsReadback)));
    }
    for (UINT i = 0; i < kMaxTs; i++) m_passMin[i] = 1e9;
}

void Ink3DRenderer::CreatePipelines() {
    // compute RS: b0 (48 dwords) + t0..t3 + u0..u2 + static linear-clamp sampler
    {
        D3D12_DESCRIPTOR_RANGE r[7];
        for (int i = 0; i < 4; i++) r[i] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, (UINT)i, 0, 0 };
        for (int i = 0; i < 3; i++) r[4 + i] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, (UINT)i, 0, 0 };
        D3D12_ROOT_PARAMETER p[8] = {};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[0].Constants = { 0, 0, sizeof(SimCB) / 4 };
        for (int i = 0; i < 7; i++) {
            p[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[1 + i].DescriptorTable = { 1, &r[i] };
        }
        D3D12_STATIC_SAMPLER_DESC s = {};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd = { 8, p, 1, &s };
        ComPtr<ID3DBlob> sig, err;
        HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&m_computeRS)));
    }
    // graphics RS: b0 (32 dwords) + t0..t2 + static linear-clamp sampler
    {
        D3D12_DESCRIPTOR_RANGE r[3];
        for (int i = 0; i < 3; i++) r[i] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, (UINT)i, 0, 0 };
        D3D12_ROOT_PARAMETER p[4] = {};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[0].Constants = { 0, 0, 32 };
        p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        for (int i = 0; i < 3; i++) {
            p[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[1 + i].DescriptorTable = { 1, &r[i] };
            p[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_STATIC_SAMPLER_DESC s = {};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd = { 4, p, 1, &s };
        ComPtr<ID3DBlob> sig, err;
        HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&m_graphicsRS)));
    }

    auto cs = [&](const char* entry, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> b = Compile(kSimSrc, entry, "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_computeRS.Get();
        d.CS = { b->GetBufferPointer(), b->GetBufferSize() };
        HR(m_device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)));
    };
    cs("CSClear4", m_psoClear4);           cs("CSClear1", m_psoClear1);
    cs("CSInitScene", m_psoInitScene);     cs("CSInjectVel", m_psoInjectVel);
    cs("CSInjectDensity", m_psoInjectDen); cs("CSCurl", m_psoCurl);
    cs("CSForces", m_psoForces);
    cs("CSAdvectVelFwd", m_psoAdvVelFwd);  cs("CSAdvectVelBack", m_psoAdvVelBack);
    cs("CSAdvectVelSL", m_psoAdvVelSL);
    cs("CSAdvectDenFwd", m_psoAdvDenFwd);  cs("CSAdvectDenBack", m_psoAdvDenBack);
    cs("CSAdvectDenSL", m_psoAdvDenSL);    cs("CSSharpen", m_psoSharpen);
    cs("CSDivergence", m_psoDivergence);   cs("CSPressureJacobi", m_psoJacobi);
    if (m_redBlack) cs("CSPressureRB", m_psoRB);
    cs("CSGradSub", m_psoGradSub);         cs("CSResidual", m_psoResidual);
    cs("CSRestrict", m_psoRestrict);       cs("CSProlong", m_psoProlong);
    cs("CSOccupancy", m_psoOccupancy);     cs("CSLight", m_psoLight);

    auto gfx = [&](const char* src, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> vs = Compile(src, "VSMain", "vs_5_1");
        ComPtr<ID3DBlob> ps = Compile(src, "PSMain", "ps_5_1");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_graphicsRS.Get();
        d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        HR(m_device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)));
    };
    gfx(kRaymarchSrc, m_psoRaymarch);
    gfx(kDisplaySrc, m_psoDisplay);
}

Tex3 Ink3DRenderer::CreateTex3(int w, int h, int d, DXGI_FORMAT fmt, int slot) {
    Tex3 t; t.w = w; t.h = h; t.d = d; t.fmt = fmt;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    rd.Width = (UINT64)w; rd.Height = (UINT)h; rd.DepthOrArraySize = (UINT16)d;
    rd.MipLevels = 1; rd.Format = fmt; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;    // driver swizzle: required for
                                                 // 3D cache locality (plan §4)
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    t.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, t.state,
                                         nullptr, IID_PPV_ARGS(&t.res)));

    auto cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture3D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sc = cpu; sc.ptr += (SIZE_T)(slot * 2) * m_srvStride;
    m_device->CreateShaderResourceView(t.res.Get(), &sv, sc);
    t.srv = gpu; t.srv.ptr += (SIZE_T)(slot * 2) * m_srvStride;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = fmt;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uv.Texture3D.WSize = (UINT)-1;
    D3D12_CPU_DESCRIPTOR_HANDLE uc = cpu; uc.ptr += (SIZE_T)(slot * 2 + 1) * m_srvStride;
    m_device->CreateUnorderedAccessView(t.res.Get(), nullptr, &uv, uc);
    t.uav = gpu; t.uav.ptr += (SIZE_T)(slot * 2 + 1) * m_srvStride;
    return t;
}

void Ink3DRenderer::CreateSimResources() {
    const DXGI_FORMAT F4 = DXGI_FORMAT_R16G16B16A16_FLOAT, F1 = DXGI_FORMAT_R16_FLOAT;
    int s = 0;
    m_vel[0] = CreateTex3(m_velW, m_velH, m_velD, F4, s++);
    m_vel[1] = CreateTex3(m_velW, m_velH, m_velD, F4, s++);
    m_velTmp = CreateTex3(m_velW, m_velH, m_velD, F4, s++);
    m_curl   = CreateTex3(m_velW, m_velH, m_velD, F4, s++);
    m_div    = CreateTex3(m_velW, m_velH, m_velD, F1, s++);
    m_den[0] = CreateTex3(m_denW, m_denH, m_denD, F1, s++);
    m_den[1] = CreateTex3(m_denW, m_denH, m_denD, F1, s++);
    m_denTmp = CreateTex3(m_denW, m_denH, m_denD, F1, s++);
    m_occ    = CreateTex3(m_occW, m_occH, m_occD, F1, s++);
    m_light  = CreateTex3(m_ltW,  m_ltH,  m_ltD,  F1, s++);

    // Multigrid. Level 0 is the finest; its p ping-pong is the pressure pair
    // and its rhs is the divergence texture. Coarsen while every dim is even.
    m_mg.clear();
    m_mg.reserve(4);
    {
        MgLevel l0;
        l0.p[0] = CreateTex3(m_velW, m_velH, m_velD, F1, s++);
        l0.p[1] = CreateTex3(m_velW, m_velH, m_velD, F1, s++);
        l0.w = m_velW; l0.h = m_velH; l0.d = m_velD; l0.h2 = 1.0f;
        m_mg.push_back(l0);
    }
    for (int lvl = 1; lvl < 3; lvl++) {
        const MgLevel& prev = m_mg.back();
        if ((prev.w & 1) || (prev.h & 1) || (prev.d & 1)) break;
        if (prev.w < 16 || prev.h < 12 || prev.d < 12) break;
        MgLevel lv;
        lv.w = prev.w / 2; lv.h = prev.h / 2; lv.d = prev.d / 2;
        lv.h2 = prev.h2 * 4.0f;
        lv.p[0] = CreateTex3(lv.w, lv.h, lv.d, F1, s++);
        lv.p[1] = CreateTex3(lv.w, lv.h, lv.d, F1, s++);
        lv.rhsStore = CreateTex3(lv.w, lv.h, lv.d, F1, s++);
        m_mg.push_back(lv);
    }
    // fix up the rhs pointers only once every push_back is done
    m_mg[0].rhs = &m_div;
    for (size_t i = 1; i < m_mg.size(); i++) m_mg[i].rhs = &m_mg[i].rhsStore;
}

void Ink3DRenderer::CreateOffscreenTarget() {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)m_width; rd.Height = (UINT)m_height;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                         IID_PPV_ARGS(&m_shotTex)));
    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_shotTex.Get(), nullptr, rtv);

    m_shotPitch = ((UINT)m_width * 8 + 255) & ~255u;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)m_shotPitch * m_height;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES rb = {}; rb.Type = D3D12_HEAP_TYPE_READBACK;
    HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&m_shotReadback)));
}

void Ink3DRenderer::CreateRaymarchTarget() {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)m_rayW; rd.Height = (UINT)m_rayH;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                         IID_PPV_ARGS(&m_rayTex)));
    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_rtvStride;
    m_device->CreateRenderTargetView(m_rayTex.Get(), nullptr, rtv);

    // SRV for the display pass: heap slot 40 (well past the Tex3 slots)
    const int slot = 40;
    auto cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sc = cpu; sc.ptr += (SIZE_T)(slot * 2) * m_srvStride;
    m_device->CreateShaderResourceView(m_rayTex.Get(), &sv, sc);
    m_raySrv = gpu; m_raySrv.ptr += (SIZE_T)(slot * 2) * m_srvStride;
}

// ---------------------------------------------------------------------------
// frame plumbing
// ---------------------------------------------------------------------------
void Ink3DRenderer::Transition(Tex3& t, D3D12_RESOURCE_STATES to) {
    if (t.state == to) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res.Get();
    b.Transition.StateBefore = t.state;
    b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);
    t.state = to;
}

void Ink3DRenderer::UavBarrier(ID3D12Resource* res) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    m_cmd->ResourceBarrier(1, &b);
}

void Ink3DRenderer::WaitForGpuIdle() {
    const UINT64 v = m_nextFence++;
    HR(m_queue->Signal(m_fence.Get(), v));
    if (m_fence->GetCompletedValue() < v) {
        HR(m_fence->SetEventOnCompletion(v, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void Ink3DRenderer::BeginFrame() {
    const UINT i = m_frameIndex;
    if (m_fence->GetCompletedValue() < m_fenceValues[i]) {
        HR(m_fence->SetEventOnCompletion(m_fenceValues[i], m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    CollectTimings(i);
    HR(m_allocators[i]->Reset());
    HR(m_cmd->Reset(m_allocators[i].Get(), nullptr));
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
    m_tsCount = 0;
}

void Ink3DRenderer::EndFrame() {
    const UINT i = m_frameIndex;
    if (m_tsCount > 0) {
        m_cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                i * kMaxTs, m_tsCount, m_tsReadback.Get(),
                                (UINT64)i * kMaxTs * sizeof(UINT64));
        m_tsWritten[i] = m_tsCount;
        m_tsValid[i] = true;
    }
    HR(m_cmd->Close());
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    m_fenceValues[i] = m_nextFence;
    HR(m_queue->Signal(m_fence.Get(), m_nextFence++));
    m_frameIndex = (m_frameIndex + 1) % kFrames;
}

void Ink3DRenderer::Mark(const char* name) {
    if (m_tsCount >= kMaxTs) return;
    if (name) {
        if (m_tsCount < m_passNames.size()) m_passNames[m_tsCount] = name;
        else m_passNames.push_back(name);
    } else {
        m_passNames.resize(m_tsCount);        // the closing mark has no pass
    }
    m_cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    m_frameIndex * kMaxTs + m_tsCount);
    m_tsCount++;
}

void Ink3DRenderer::CollectTimings(UINT slot) {
    if (!m_tsValid[slot] || !m_tsFreq) return;
    m_tsValid[slot] = false;
    const UINT n = m_tsWritten[slot];
    if (n < 2) return;
    UINT64* ts = nullptr;
    D3D12_RANGE rr = { (SIZE_T)slot * kMaxTs * sizeof(UINT64),
                       (SIZE_T)(slot * kMaxTs + n) * sizeof(UINT64) };
    if (FAILED(m_tsReadback->Map(0, &rr, (void**)&ts))) return;
    const UINT64* base = ts + slot * kMaxTs;
    for (UINT k = 0; k + 1 < n && k < kMaxTs; k++) {
        if (base[k + 1] <= base[k]) continue;
        double ms = 1000.0 * (double)(base[k + 1] - base[k]) / (double)m_tsFreq;
        m_passSum[k] += ms;
        if (ms < m_passMin[k]) m_passMin[k] = ms;
    }
    D3D12_RANGE none = { 0, 0 };
    m_tsReadback->Unmap(0, &none);
    m_passSamples++;
}

void Ink3DRenderer::ResetGpuStats() {
    for (UINT i = 0; i < kMaxTs; i++) { m_passSum[i] = 0.0; m_passMin[i] = 1e9; }
    m_passSamples = 0;
}

double Ink3DRenderer::FrameMsMean() const {
    double s = 0.0;
    for (size_t i = 0; i < m_passNames.size(); i++) s += PassMsMean((int)i);
    return s;
}

// ---------------------------------------------------------------------------
// dispatch helpers
// ---------------------------------------------------------------------------
void Ink3DRenderer::SetCB(int dw, int dh, int dd) {
    g_cb.dimsX = dw; g_cb.dimsY = dh; g_cb.dimsZ = dd;
    g_cb.invX = 1.0f / dw; g_cb.invY = 1.0f / dh; g_cb.invZ = 1.0f / dd;
}

void Ink3DRenderer::Dispatch(ID3D12PipelineState* pso, Tex3* s0, Tex3* s1, Tex3* s2,
                             Tex3* s3, Tex3* dst, int gx, int gy, int gz) {
    if (s0 && s0 != dst) Transition(*s0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (s1 && s1 != dst) Transition(*s1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (s2 && s2 != dst) Transition(*s2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (s3 && s3 != dst) Transition(*s3, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(*dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_cmd->SetComputeRootSignature(m_computeRS.Get());
    m_cmd->SetPipelineState(pso);
    SetCB(dst->w, dst->h, dst->d);
    m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &g_cb, 0);
    // bind every table; the entry point uses the ones it declares (the
    // SimStep trick, src/fluid.cpp:581-598)
    m_cmd->SetComputeRootDescriptorTable(1, s0 ? s0->srv : dst->srv);
    m_cmd->SetComputeRootDescriptorTable(2, s1 ? s1->srv : dst->srv);
    m_cmd->SetComputeRootDescriptorTable(3, s2 ? s2->srv : dst->srv);
    m_cmd->SetComputeRootDescriptorTable(4, s3 ? s3->srv : dst->srv);
    m_cmd->SetComputeRootDescriptorTable(5, dst->uav);
    m_cmd->SetComputeRootDescriptorTable(6, dst->uav);
    m_cmd->SetComputeRootDescriptorTable(7, dst->uav);
    m_cmd->Dispatch(Grp(dst->w, gx), Grp(dst->h, gy),
                    gz > 0 ? Grp(dst->d, gz) : 1u);
    UavBarrier(dst->res.Get());
}

void Ink3DRenderer::DispatchInPlace(ID3D12PipelineState* pso, Tex3* s3, Tex3* inout) {
    Transition(*s3, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(*inout, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_cmd->SetComputeRootSignature(m_computeRS.Get());
    m_cmd->SetPipelineState(pso);
    SetCB(inout->w, inout->h, inout->d);
    m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &g_cb, 0);
    for (UINT i = 1; i <= 4; i++) m_cmd->SetComputeRootDescriptorTable(i, s3->srv);
    for (UINT i = 5; i <= 7; i++) m_cmd->SetComputeRootDescriptorTable(i, inout->uav);
    m_cmd->Dispatch(Grp(inout->w, 8), Grp(inout->h, 8), Grp(inout->d, 4));
    UavBarrier(inout->res.Get());
}

// ---------------------------------------------------------------------------
// passes
// ---------------------------------------------------------------------------
void Ink3DRenderer::FillStaticScene() {
    BeginFrame();
    g_cb.densityCap = m_cfg.densityCap;
    Dispatch(m_psoInitScene.Get(), nullptr, nullptr, nullptr, nullptr, &m_den[m_denCur]);
    EndFrame();
    WaitForGpuIdle();
}

void Ink3DRenderer::Inject() {
    const float velScale = (float)m_denW / (float)m_velW;
    for (const Ink3DDrop& d : m_queued) {
        // velocity grid
        g_cb.dropCX = d.nx * m_velW; g_cb.dropCY = d.ny * m_velH; g_cb.dropCZ = d.nz * m_velD;
        g_cb.dropRadius = d.radius;
        g_cb.dropVX = d.vx; g_cb.dropVY = d.vy; g_cb.dropVZ = d.vz;
        g_cb.dropAmount = d.amount;
        g_cb.noiseAmt = 0.0f;
        Dispatch(m_psoInjectVel.Get(), &m_vel[m_velCur], nullptr, nullptr, nullptr,
                 &m_vel[1 - m_velCur]);
        m_velCur = 1 - m_velCur;

        // density grid (finer: centre and radius scale with it)
        g_cb.dropCX = d.nx * m_denW; g_cb.dropCY = d.ny * m_denH; g_cb.dropCZ = d.nz * m_denD;
        g_cb.dropRadius = d.radius * velScale;
        g_cb.noiseAmt = m_cfg.dropEdgeNoise;     // edge noise, not ambient noise
        Dispatch(m_psoInjectDen.Get(), nullptr, &m_den[m_denCur], nullptr, nullptr,
                 &m_den[1 - m_denCur]);
        m_denCur = 1 - m_denCur;
    }
    m_queued.clear();
}

void Ink3DRenderer::Smooth(MgLevel& lv, int sweeps) {
    g_cb.h2 = lv.h2;
    if (m_redBlack && m_psoRB) {
        g_cb.omega = 1.0f;
        for (int i = 0; i < sweeps; i++) {
            g_cb.passIdx = 0; DispatchInPlace(m_psoRB.Get(), lv.rhs, &lv.P());
            g_cb.passIdx = 1; DispatchInPlace(m_psoRB.Get(), lv.rhs, &lv.P());
        }
    } else {
        g_cb.omega = m_cfg.smootherOmega;
        for (int i = 0; i < sweeps; i++) {
            Dispatch(m_psoJacobi.Get(), nullptr, &lv.P(), nullptr, lv.rhs, &lv.Scratch());
            lv.Flip();
        }
    }
}

void Ink3DRenderer::VCycle() {
    const int L = (int)m_mg.size();
    // down
    for (int i = 0; i < L - 1; i++) {
        Smooth(m_mg[i], m_cfg.mgPreSmooth);
        g_cb.h2 = m_mg[i].h2;
        Dispatch(m_psoResidual.Get(), nullptr, &m_mg[i].P(), nullptr, m_mg[i].rhs,
                 &m_mg[i].Scratch());
        Dispatch(m_psoRestrict.Get(), nullptr, &m_mg[i].Scratch(), nullptr, nullptr,
                 m_mg[i + 1].rhs);
        m_mg[i + 1].cur = 0;
        Dispatch(m_psoClear1.Get(), nullptr, nullptr, nullptr, nullptr, &m_mg[i + 1].p[0]);
    }
    // coarsest
    Smooth(m_mg[L - 1], m_cfg.mgCoarseSweeps);
    // up
    for (int i = L - 2; i >= 0; i--) {
        g_cb.h2 = m_mg[i].h2;
        Dispatch(m_psoProlong.Get(), nullptr, &m_mg[i + 1].P(), nullptr, &m_mg[i].P(),
                 &m_mg[i].Scratch());
        m_mg[i].Flip();
        Smooth(m_mg[i], m_cfg.mgPostSmooth);
    }
}

void Ink3DRenderer::SolvePressure() {
    // Warm start: the previous frame's pressure is already in level 0, which
    // is what makes a small iteration count viable (the 2D pressure_diffusion
    // trick, src/fluid.cpp:608-611).
    if (m_cfg.pressureSolver == INK3D_MG && m_mg.size() >= 2) {
        for (int c = 0; c < m_cfg.mgCycles; c++) VCycle();
    } else {
        g_cb.h2 = 1.0f;
        g_cb.omega = m_cfg.jacobiOmega;
        MgLevel& lv = m_mg[0];
        for (int i = 0; i < m_cfg.pressureIterations; i++) {
            Dispatch(m_psoJacobi.Get(), nullptr, &lv.P(), nullptr, lv.rhs, &lv.Scratch());
            lv.Flip();
        }
    }
}

void Ink3DRenderer::Frame(float dt, float sdrScale, bool simEnabled) {
    BeginFrame();

    // per-frame constants
    const float steps144 = dt * 144.0f;   // fps-normalise every per-step decay
    g_cb.dt = dt;
    g_cb.seed = m_seed;
    g_cb.timeSec = (float)m_time;
    g_cb.densityCap = m_cfg.densityCap;
    g_cb.velScale = (float)m_denW / (float)m_velW;
    g_cb.velDimX = m_velW; g_cb.velDimY = m_velH; g_cb.velDimZ = m_velD;
    g_cb.invVelX = 1.0f / m_velW; g_cb.invVelY = 1.0f / m_velH; g_cb.invVelZ = 1.0f / m_velD;
    g_cb.dissipation = powf(m_cfg.dissipation, steps144);
    g_cb.dissipationFast = powf(m_cfg.dissipationFast, steps144);
    g_cb.velDissipation = powf(m_cfg.velDissipation, steps144);
    g_cb.sinkRate = powf(m_cfg.sinkRate, steps144);
    g_cb.decayThreshold = m_cfg.decayThreshold;
    g_cb.sinkBottom = m_cfg.sinkBottom;
    g_cb.vorticity = m_cfg.vorticity;
    g_cb.buoyancy = m_cfg.buoyancy;
    g_cb.sharpen = m_cfg.sharpen;

    if (simEnabled) {
        // marked unconditionally so the pass list (and therefore the timing
        // slot indices) is identical on every frame of a run
        Mark("inject");
        if (!m_queued.empty()) Inject();

        Mark("curl");
        Dispatch(m_psoCurl.Get(), &m_vel[m_velCur], nullptr, nullptr, nullptr, &m_curl);

        Mark("forces");
        g_cb.noiseAmt = m_cfg.ambientNoise;
        Dispatch(m_psoForces.Get(), &m_vel[m_velCur], &m_den[m_denCur], &m_curl, nullptr,
                 &m_vel[1 - m_velCur]);
        m_velCur = 1 - m_velCur;

        Mark("advect_vel");
        if (m_cfg.maccormack) {
            Dispatch(m_psoAdvVelFwd.Get(), &m_vel[m_velCur], nullptr, nullptr, nullptr, &m_velTmp);
            Dispatch(m_psoAdvVelBack.Get(), &m_velTmp, nullptr, &m_vel[m_velCur], nullptr,
                     &m_vel[1 - m_velCur]);
        } else {
            Dispatch(m_psoAdvVelSL.Get(), &m_vel[m_velCur], nullptr, nullptr, nullptr,
                     &m_vel[1 - m_velCur]);
        }
        m_velCur = 1 - m_velCur;

        Mark("divergence");
        Dispatch(m_psoDivergence.Get(), &m_vel[m_velCur], nullptr, nullptr, nullptr, &m_div);

        Mark("pressure");
        SolvePressure();

        Mark("grad_sub");
        Dispatch(m_psoGradSub.Get(), &m_vel[m_velCur], &m_mg[0].P(), nullptr, nullptr,
                 &m_vel[1 - m_velCur]);
        m_velCur = 1 - m_velCur;

        Mark("advect_den");
        if (m_cfg.maccormack) {
            Dispatch(m_psoAdvDenFwd.Get(), &m_vel[m_velCur], nullptr, nullptr,
                     &m_den[m_denCur], &m_denTmp);
            Dispatch(m_psoAdvDenBack.Get(), &m_vel[m_velCur], &m_denTmp, nullptr,
                     &m_den[m_denCur], &m_den[1 - m_denCur]);
        } else {
            Dispatch(m_psoAdvDenSL.Get(), &m_vel[m_velCur], nullptr, nullptr,
                     &m_den[m_denCur], &m_den[1 - m_denCur]);
        }
        m_denCur = 1 - m_denCur;

        if (m_cfg.sharpen > 1e-4f) {
            Mark("sharpen");
            Dispatch(m_psoSharpen.Get(), nullptr, &m_den[m_denCur], nullptr, nullptr,
                     &m_den[1 - m_denCur]);
            m_denCur = 1 - m_denCur;
        }
        m_time += dt;
    }

    Mark("occupancy");
    Dispatch(m_psoOccupancy.Get(), nullptr, &m_den[m_denCur], nullptr, nullptr, &m_occ,
             4, 4, 4);

    Mark("light");
    {
        float lx = m_cfg.lightDir[0], ly = m_cfg.lightDir[1], lz = m_cfg.lightDir[2];
        float len = sqrtf(lx * lx + ly * ly + lz * lz);
        if (len < 1e-5f) { lx = 0; ly = 0; lz = -1; len = 1; }
        lx /= len; ly /= len; lz /= len;
        float az = fabsf(lz); if (az < 0.2f) az = 0.2f;     // the sweep is z-major
        const float dzWorld = kBoxZ / (float)m_ltD;
        // step BACK along the light ray, so the value stored at (x,y,z) is the
        // transmittance actually reaching (x,y,z)
        g_cb.lightStepX = -(lx / az) * dzWorld / kBoxW;
        g_cb.lightStepY = -(ly / az) * dzWorld / kBoxH;
        const float lum = 0.2126f * m_cfg.absorbRgb[0] + 0.7152f * m_cfg.absorbRgb[1]
                        + 0.0722f * m_cfg.absorbRgb[2];
        g_cb.lightSigma = m_cfg.inkDensity * lum / az;
        Dispatch(m_psoLight.Get(), nullptr, &m_den[m_denCur], nullptr, nullptr, &m_light,
                 8, 8, 0);
    }

    Mark("raymarch");
    RenderRaymarch();
    Mark("display");
    RenderDisplay(sdrScale);
    Mark(nullptr);

    EndFrame();
}

void Ink3DRenderer::RenderRaymarch() {
    Transition(m_den[m_denCur], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(m_occ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(m_light, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (m_rayState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_rayTex.Get();
        b.Transition.StateBefore = m_rayState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmd->ResourceBarrier(1, &b);
        m_rayState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_rayW, (float)m_rayH, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_rayW, m_rayH };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    float c[32] = {};
    const float camDist = (kBoxH * 0.5f) / tanf(m_cfg.fovDeg * 0.5f * 3.14159265f / 180.0f);
    c[0] = 1.0f / m_rayW;   c[1] = 1.0f / m_rayH;  c[2] = kBoxW; c[3] = kBoxH;
    c[4] = kBoxW * 0.5f;    c[5] = kBoxH * 0.5f;   c[6] = -camDist; c[7] = kBoxZ;
    c[8]  = m_cfg.inkDensity * m_cfg.absorbRgb[0];
    c[9]  = m_cfg.inkDensity * m_cfg.absorbRgb[1];
    c[10] = m_cfg.inkDensity * m_cfg.absorbRgb[2];
    c[11] = (float)m_cfg.steps;
    c[12] = m_cfg.albedoRgb[0]; c[13] = m_cfg.albedoRgb[1]; c[14] = m_cfg.albedoRgb[2];
    c[15] = m_cfg.scatter;
    c[16] = m_cfg.paperRgb[0]; c[17] = m_cfg.paperRgb[1]; c[18] = m_cfg.paperRgb[2];
    c[19] = m_cfg.paperLevel;
    c[20] = m_cfg.inkRgb[0]; c[21] = m_cfg.inkRgb[1]; c[22] = m_cfg.inkRgb[2];
    c[23] = m_cfg.inkLevel;
    c[24] = (float)m_cfg.renderMode;
    c[25] = m_cfg.scatterMix;
    c[26] = m_cfg.gradientLight ? 1.0f : 0.0f;
    c[27] = m_cfg.jitterAnim ? (float)((int)(m_time * 144.0) & 3) : 0.0f;   // integer 0..3

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(m_psoRaymarch.Get());
    m_cmd->SetGraphicsRoot32BitConstants(0, 28, c, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_den[m_denCur].srv);
    m_cmd->SetGraphicsRootDescriptorTable(2, m_occ.srv);
    m_cmd->SetGraphicsRootDescriptorTable(3, m_light.srv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);
}

void Ink3DRenderer::RenderDisplay(float sdrScale) {
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_rayTex.Get();
        b.Transition.StateBefore = m_rayState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmd->ResourceBarrier(1, &b);
        m_rayState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (m_shotState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_shotTex.Get();
        b.Transition.StateBefore = m_shotState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmd->ResourceBarrier(1, &b);
        m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    float c[32] = {};
    c[0] = 1.0f / m_width; c[1] = 1.0f / m_height;
    c[2] = sdrScale;
    // peak gain lifts only dense cores; sdrScale is 1.0 when Windows HDR is off
    c[3] = (m_cfg.peakNits > 0.0f && sdrScale > 1.0f)
             ? m_cfg.peakNits / (sdrScale * 80.0f) : 1.0f;
    c[4] = m_cfg.knee;
    c[5] = m_cfg.hue;
    c[6] = (float)m_cfg.gamut;

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(m_psoDisplay.Get());
    m_cmd->SetGraphicsRoot32BitConstants(0, 8, c, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_raySrv);
    m_cmd->SetGraphicsRootDescriptorTable(2, m_raySrv);
    m_cmd->SetGraphicsRootDescriptorTable(3, m_raySrv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// readback
// ---------------------------------------------------------------------------
bool Ink3DRenderer::CaptureOffscreen(std::vector<float>& out) {
    WaitForGpuIdle();
    HR(m_allocators[0]->Reset());
    HR(m_cmd->Reset(m_allocators[0].Get(), nullptr));

    if (m_shotState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_shotTex.Get();
        b.Transition.StateBefore = m_shotState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmd->ResourceBarrier(1, &b);
        m_shotState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    src.pResource = m_shotTex.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = m_shotReadback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dst.PlacedFootprint.Footprint.Width = (UINT)m_width;
    dst.PlacedFootprint.Footprint.Height = (UINT)m_height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = m_shotPitch;
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    HR(m_cmd->Close());
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGpuIdle();

    uint8_t* data = nullptr;
    D3D12_RANGE range = { 0, (SIZE_T)m_shotPitch * m_height };
    if (FAILED(m_shotReadback->Map(0, &range, (void**)&data))) return false;
    out.resize((size_t)m_width * m_height * 4);
    for (int y = 0; y < m_height; y++) {
        const uint16_t* row = (const uint16_t*)(data + (SIZE_T)y * m_shotPitch);
        float* d = out.data() + (size_t)y * m_width * 4;
        for (int i = 0; i < m_width * 4; i++) d[i] = HalfToFloat(row[i]);
    }
    D3D12_RANGE none = { 0, 0 };
    m_shotReadback->Unmap(0, &none);

    for (UINT i = 0; i < kFrames; i++) m_fenceValues[i] = 0;
    m_frameIndex = 0;
    return true;
}

bool Ink3DRenderer::ReadbackTex3(Tex3& t, std::vector<float>& out) {
    D3D12_RESOURCE_DESC rd = t.res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    m_device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
    if (!m_rbBuf || m_rbSize < total) {
        WaitForGpuIdle();
        m_rbBuf.Reset();
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&m_rbBuf)));
        m_rbSize = total;
    }

    WaitForGpuIdle();
    HR(m_allocators[0]->Reset());
    HR(m_cmd->Reset(m_allocators[0].Get(), nullptr));
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res.Get();
    b.Transition.StateBefore = t.state;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (t.state != D3D12_RESOURCE_STATE_COPY_SOURCE) m_cmd->ResourceBarrier(1, &b);
    t.state = D3D12_RESOURCE_STATE_COPY_SOURCE;

    D3D12_TEXTURE_COPY_LOCATION s = {}, d = {};
    s.pResource = t.res.Get(); s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d.pResource = m_rbBuf.Get(); d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    d.PlacedFootprint = fp;
    m_cmd->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    HR(m_cmd->Close());
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGpuIdle();

    uint8_t* data = nullptr;
    D3D12_RANGE range = { 0, (SIZE_T)total };
    if (FAILED(m_rbBuf->Map(0, &range, (void**)&data))) return false;
    out.resize((size_t)t.w * t.h * t.d);
    for (int z = 0; z < t.d; z++)
        for (int y = 0; y < t.h; y++) {
            const uint16_t* row = (const uint16_t*)
                (data + (SIZE_T)(z * (int)rows + y) * fp.Footprint.RowPitch);
            float* o = out.data() + ((size_t)z * t.h + y) * t.w;
            for (int x = 0; x < t.w; x++) o[x] = HalfToFloat(row[x]);
        }
    D3D12_RANGE none = { 0, 0 };
    m_rbBuf->Unmap(0, &none);
    for (UINT i = 0; i < kFrames; i++) m_fenceValues[i] = 0;
    m_frameIndex = 0;
    return true;
}

// Extra GPU work, --stats only: divergence before/after the projection and the
// multigrid residual of the pressure the last frame actually used.
Ink3DStats Ink3DRenderer::MeasureStats() {
    Ink3DStats st;
    std::vector<float> buf;

    // 1. m_div still holds the divergence of the PRE-projection velocity from
    //    the last Frame() — that is the "before" number and the solver's rhs.
    if (ReadbackTex3(m_div, buf)) {
        double s = 0.0;
        for (float v : buf) s += fabs((double)v);
        st.divBefore = s / (double)buf.size();
        st.rhsNorm = st.divBefore;
    }

    // 2. residual of the last pressure solve against that same rhs
    BeginFrame();
    g_cb.h2 = 1.0f;
    Dispatch(m_psoResidual.Get(), nullptr, &m_mg[0].P(), nullptr, m_mg[0].rhs,
             &m_mg[0].Scratch());
    EndFrame();
    WaitForGpuIdle();
    if (ReadbackTex3(m_mg[0].Scratch(), buf)) {
        double s = 0.0;
        for (float v : buf) s += fabs((double)v);
        st.residual = s / (double)buf.size();
    }

    // 3. divergence of the CURRENT (projected) velocity — overwrites the rhs,
    //    so it has to come last
    BeginFrame();
    Dispatch(m_psoDivergence.Get(), &m_vel[m_velCur], nullptr, nullptr, nullptr, &m_div);
    EndFrame();
    WaitForGpuIdle();
    if (ReadbackTex3(m_div, buf)) {
        double s = 0.0;
        for (float v : buf) s += fabs((double)v);
        st.divAfter = s / (double)buf.size();
    }

    // 4. density statistics
    if (ReadbackTex3(m_den[m_denCur], buf)) {
        double s = 0.0, mx = 0.0;
        for (float v : buf) { s += v; if (v > mx) mx = v; }
        st.densityMean = s / (double)buf.size();
        st.densityMax = mx;
    }
    st.valid = true;
    return st;
}

void Ink3DRenderer::Shutdown() {
    if (m_device) WaitForGpuIdle();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}
