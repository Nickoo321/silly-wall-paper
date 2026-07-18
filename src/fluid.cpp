#include "fluid.h"
#include "shaders.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

extern void Fail(const char* what, HRESULT hr);   // main.cpp
#define HR(expr) do { HRESULT _hr = (expr); if (FAILED(_hr)) Fail(#expr, _hr); } while (0)

// Must match cbuffer CB in shaders.h (20 DWORDs).
struct SimCB {
    float texelW, texelH;
    float dt;
    float dissipation;
    float dissipationFast;
    float decayThreshold;
    float satRestore;
    float curlStrength;
    float value;
    float aspect;
    float pointX, pointY;
    float colorR, colorG, colorB;
    float radius;
    float cap;
    int   dimsW, dimsH;
};
static_assert(sizeof(SimCB) == 19 * 4, "SimCB must match the HLSL cbuffer layout (19 DWORDs)");

static float RandF() { return (float)rand() / (float)RAND_MAX; }
static float HalfToFloat(uint16_t h);   // defined below

struct RGB { float r, g, b; };
struct Mat3 { float m[9]; };   // row-major

static Mat3 Mat3Identity() { return { 1,0,0, 0,1,0, 0,0,1 }; }
static Mat3 Mat3Mul(const Mat3& a, const Mat3& b) {
    Mat3 r = {};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i * 3 + j] = a.m[i * 3 + 0] * b.m[0 * 3 + j] +
                             a.m[i * 3 + 1] * b.m[1 * 3 + j] +
                             a.m[i * 3 + 2] * b.m[2 * 3 + j];
    return r;
}
static Mat3 Mat3Scale(const Mat3& a, float s) {
    Mat3 r = a;
    for (int i = 0; i < 9; i++) r.m[i] *= s;
    return r;
}
// CSS filter matrices (SVG/W3C spec, applied in gamma space like the browser)
static Mat3 CssHueRotate(float deg) {
    float c = cosf(deg * 3.14159265f / 180.0f);
    float s = sinf(deg * 3.14159265f / 180.0f);
    return {
        0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.787f * s,
        0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s,
        0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s,
    };
}
static Mat3 CssSaturate(float s) {
    return {
        0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s,
        0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s,
        0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s,
    };
}

static RGB HSVtoRGB(float h, float s, float v) {
    int i = (int)floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (((i % 6) + 6) % 6) {
    case 0: return { v, t, p };
    case 1: return { q, v, p };
    case 2: return { p, v, t };
    case 3: return { p, q, v };
    case 4: return { t, p, v };
    default: return { v, p, q };
    }
}

// reference cycledColor(): slow wheel position + offset, dimmed to 0.15
static RGB CycledColorAt(float hue) {
    RGB c = HSVtoRGB(fmodf(hue + 1.0f, 1.0f), 1.0f, 1.0f);
    c.r *= 0.15f; c.g *= 0.15f; c.b *= 0.15f;
    return c;
}

static ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(src, strlen(src), entry, nullptr, nullptr, entry, target, 0, 0, &blob, &err))) {
        fprintf(stderr, "shader '%s' compile error:\n%s\n", entry,
                err ? (char*)err->GetBufferPointer() : "?");
        ExitProcess(1);
    }
    return blob;
}

// Aspect-corrected resolution, same as reference getResolution().
static void GetResolution(int base, int screenW, int screenH, int* outW, int* outH) {
    float aspect = (float)screenW / (float)screenH;
    if (aspect < 1.0f) aspect = 1.0f / aspect;
    int mx = (int)lroundf(base * aspect);
    int mn = base;
    if (screenW > screenH) { *outW = mx; *outH = mn; }
    else                   { *outW = mn; *outH = mx; }
}

void FluidRenderer::Init(HWND hwnd, int width, int height, const FluidConfig& cfg) {
    m_cfg = cfg;
    m_width = width;
    m_height = height;
    m_globalHue = RandF();
    srand(GetTickCount());

    CreateDevice(hwnd, width, height);
    if (!m_cfg.gradientMode) CreateSimResources();
    printf("Renderer ready (%s), sim %dx%d, dye %dx%d\n",
           m_cfg.gradientMode ? "gradient mode" : "fluid",
           m_simW, m_simH, m_dyeW, m_dyeH);
}

void FluidRenderer::CreateDevice(HWND hwnd, int width, int height) {
    UINT factoryFlags = 0;
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    HR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)));

    ComPtr<IDXGIAdapter1> adapter;
    HR(m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 adesc = {};
    adapter->GetDesc1(&adesc);
    printf("GPU: %ls\n", adesc.Description);

    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrames;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> sc1;
    HR(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1));
    HR(sc1.As(&m_swapChain));
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    const DXGI_COLOR_SPACE_TYPE scRGB = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    UINT support = 0;
    HR(m_swapChain->CheckColorSpaceSupport(scRGB, &support));
    if (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
        HR(m_swapChain->SetColorSpace1(scRGB));
        printf("Swap chain: R16G16B16A16_FLOAT, scRGB color space set (1.0 = 80 nits)\n");
    } else {
        printf("WARNING: scRGB color space not supported on this output!\n");
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kFrames * 2 + 1;   // backbuffers + analyzer + mirror buffers
    HR(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrames; i++) {
        HR(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv);
        rtv.ptr += m_rtvStride;
        HR(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&m_allocators[i])));
    }
    HR(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmd)));
    HR(m_cmd->Close());

    HR(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Shader-visible heap: 2 slots (SRV, UAV) per texture, 16 textures max.
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 32;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)));
    m_srvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // --- root signatures ---
    // Compute: b0 root constants, t0, t1, u0..u2 (each its own 1-descriptor table).
    {
        D3D12_DESCRIPTOR_RANGE rSrv0 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rSrv1 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rUav0 = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rUav1 = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rUav2 = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, 0 };

        D3D12_ROOT_PARAMETER params[6] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants = { 0, 0, sizeof(SimCB) / 4 };
        auto table = [](D3D12_ROOT_PARAMETER& p, D3D12_DESCRIPTOR_RANGE* r) {
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable = { 1, r };
        };
        table(params[1], &rSrv0);
        table(params[2], &rSrv1);
        table(params[3], &rUav0);
        table(params[4], &rUav1);
        table(params[5], &rUav2);

        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 6;
        rsd.pParameters = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &samp;

        ComPtr<ID3DBlob> sig, err;
        HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&m_computeRS)));
    }
    // Graphics: b0 constants, t0 table, s0 static sampler.
    {
        D3D12_DESCRIPTOR_RANGE rSrv0 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants = { 0, 0, 24 };
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &rSrv0 };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 2;
        rsd.pParameters = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &samp;

        ComPtr<ID3DBlob> sig, err;
        HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&m_graphicsRS)));
    }

    // --- compute PSOs ---
    auto makeCS = [&](const char* entry, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> cs = Compile(kComputeSrc, entry, "cs_5_0");
        D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {};
        cd.pRootSignature = m_computeRS.Get();
        cd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        HR(m_device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&pso)));
    };
    makeCS("CSClearV", m_psoClearV);
    makeCS("CSClear4", m_psoClear4);
    makeCS("CSClear1", m_psoClear1);
    makeCS("CSCurl", m_psoCurl);
    makeCS("CSVorticity", m_psoVorticity);
    makeCS("CSDivergence", m_psoDivergence);
    makeCS("CSClearPressure", m_psoClearPressure);
    makeCS("CSPressure", m_psoPressure);
    makeCS("CSGradientSubtract", m_psoGradSub);
    makeCS("CSAdvectVelocity", m_psoAdvectVel);
    makeCS("CSAdvectDye", m_psoAdvectDye);
    makeCS("CSSplatVelocity", m_psoSplatVel);
    makeCS("CSSplatDye", m_psoSplatDye);
    makeCS("CSDownsample", m_psoDownsample);

    // --- graphics PSOs (display + gradient) ---
    auto makeGfx = [&](const char* src, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> vs = Compile(src, "VSMain", "vs_5_0");
        ComPtr<ID3DBlob> ps = Compile(src, "PSMain", "ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = m_graphicsRS.Get();
        pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pd.SampleDesc.Count = 1;
        HR(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)));
    };
    makeGfx(kDisplaySrc, m_psoDisplay);
    makeGfx(kGradientSrc, m_psoGradient);
}

FluidRenderer::Tex FluidRenderer::CreateTex(int w, int h, DXGI_FORMAT fmt, int heapSlot) {
    Tex t;
    t.w = w; t.h = h;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    t.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, t.state,
                                         nullptr, IID_PPV_ARGS(&t.res)));

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    UINT srvSlot = heapSlot * 2, uavSlot = heapSlot * 2 + 1;

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sc = cpu; sc.ptr += (SIZE_T)srvSlot * m_srvStride;
    m_device->CreateShaderResourceView(t.res.Get(), &sv, sc);
    t.srv = gpu; t.srv.ptr += (SIZE_T)srvSlot * m_srvStride;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = fmt;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE uc = cpu; uc.ptr += (SIZE_T)uavSlot * m_srvStride;
    m_device->CreateUnorderedAccessView(t.res.Get(), nullptr, &uv, uc);
    t.uav = gpu; t.uav.ptr += (SIZE_T)uavSlot * m_srvStride;
    return t;
}

void FluidRenderer::CreateSimResources() {
    GetResolution(m_cfg.simRes, m_width, m_height, &m_simW, &m_simH);
    GetResolution(m_cfg.dyeRes, m_width, m_height, &m_dyeW, &m_dyeH);

    int slot = 0;
    m_velocity.a = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16G16_FLOAT, slot++);
    m_velocity.b = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16G16_FLOAT, slot++);
    m_velocity.read = &m_velocity.a; m_velocity.write = &m_velocity.b;
    m_dye.a = CreateTex(m_dyeW, m_dyeH, DXGI_FORMAT_R16G16B16A16_FLOAT, slot++);
    m_dye.b = CreateTex(m_dyeW, m_dyeH, DXGI_FORMAT_R16G16B16A16_FLOAT, slot++);
    m_dye.read = &m_dye.a; m_dye.write = &m_dye.b;
    m_pressure.a = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16_FLOAT, slot++);
    m_pressure.b = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16_FLOAT, slot++);
    m_pressure.read = &m_pressure.a; m_pressure.write = &m_pressure.b;
    m_divergence = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16_FLOAT, slot++);
    m_curl = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16_FLOAT, slot++);
    m_coverage = CreateTex(kCovW, kCovH, DXGI_FORMAT_R16G16B16A16_FLOAT, slot++);

    // coverage governor readback (48x27 RGBA16F, 256-aligned pitch)
    m_covPitch = (kCovW * 8 + 255) & ~255u;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)m_covPitch * kCovH;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&m_covReadback)));
    }
    InitWanderers();

    // stats readback buffer (dye-sized RGBA16F rows, 256-aligned pitch)
    if (m_cfg.stats) {
        m_readbackPitch = (m_dyeW * 8 + 255) & ~255u;
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)m_readbackPitch * m_dyeH;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&m_readback)));
    }
}

void FluidRenderer::Transition(Tex& t, D3D12_RESOURCE_STATES to) {
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

void FluidRenderer::UavBarrier(ID3D12Resource* res) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    m_cmd->ResourceBarrier(1, &b);
}

void FluidRenderer::BeginFrame() {
    const UINT i = m_frameIndex;
    if (m_fence->GetCompletedValue() < m_fenceValues[i]) {
        HR(m_fence->SetEventOnCompletion(m_fenceValues[i], m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    HR(m_allocators[i]->Reset());
    HR(m_cmd->Reset(m_allocators[i].Get(), nullptr));
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
}

void FluidRenderer::EndFrameAndPresent() {
    HR(m_cmd->Close());
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    // Present fails (not fatally) when Explorer tears our window down
    // mid-frame — flag it so the shell can rebuild instead of dying.
    HRESULT phr = m_swapChain->Present(1, 0);
    if (FAILED(phr)) {
        if (!m_presentBroken)
            printf("Present failed (0x%08lX) - wallpaper window lost, awaiting reattach\n",
                   (unsigned long)phr);
        m_presentBroken = true;
    }
    if (m_mirrorChain) {
        // interval 0: the main chain already provides vsync pacing
        HRESULT mhr = m_mirrorChain->Present(0, 0);
        if (FAILED(mhr)) m_mirrorBroken = true;
    }
    m_fenceValues[m_frameIndex] = m_nextFence;
    HR(m_queue->Signal(m_fence.Get(), m_nextFence++));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// Dispatch one compute pass. srv0/srv1 read, one of the u-slots written
// (chosen by target format type inside the shader entry point).
static UINT Groups(int n) { return (UINT)((n + 7) / 8); }

void FluidRenderer::SimStep(float dt) {
    SimCB cb = {};
    cb.texelW = 1.0f / m_simW;
    cb.texelH = 1.0f / m_simH;
    cb.dt = dt;
    cb.curlStrength = m_cfg.curl;
    cb.aspect = (float)m_width / (float)m_height;

    // Frame-rate independence: the reference applied its multiplicative decays
    // once per step at ~120 steps/s (60 fps, two substeps). Raise each factor
    // to dt/(1/120) so a 144 fps loop doesn't damp harder than a 60 fps one.
    const float stepsRef = dt * 120.0f;

    auto bind = [&](ID3D12PipelineState* pso, Tex* s0, Tex* s1, Tex* dst, UINT dstParam) {
        if (s0) Transition(*s0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (s1) Transition(*s1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(*dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_cmd->SetPipelineState(pso);
        cb.dimsW = dst->w; cb.dimsH = dst->h;
        m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &cb, 0);
        m_cmd->SetComputeRootDescriptorTable(1, s0 ? s0->srv : dst->srv);
        m_cmd->SetComputeRootDescriptorTable(2, s1 ? s1->srv : dst->srv);
        // set all three UAV params; only the one the entry point uses matters
        m_cmd->SetComputeRootDescriptorTable(3, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(4, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(5, dst->uav);
        m_cmd->Dispatch(Groups(dst->w), Groups(dst->h), 1);
    };

    m_cmd->SetComputeRootSignature(m_computeRS.Get());

    // 1. curl
    bind(m_psoCurl.Get(), m_velocity.read, nullptr, &m_curl, 5);
    // 2. vorticity confinement
    bind(m_psoVorticity.Get(), m_velocity.read, &m_curl, m_velocity.write, 3);
    m_velocity.Swap();
    // 3. divergence
    bind(m_psoDivergence.Get(), m_velocity.read, nullptr, &m_divergence, 5);
    // 4. pressure decay
    cb.value = powf(m_cfg.pressureDissipation, stepsRef);
    bind(m_psoClearPressure.Get(), m_pressure.read, nullptr, m_pressure.write, 5);
    m_pressure.Swap();
    // 5. Jacobi
    for (int i = 0; i < m_cfg.pressureIterations; i++) {
        bind(m_psoPressure.Get(), m_pressure.read, &m_divergence, m_pressure.write, 5);
        m_pressure.Swap();
    }
    // 6. gradient subtract
    bind(m_psoGradSub.Get(), m_pressure.read, m_velocity.read, m_velocity.write, 3);
    m_velocity.Swap();
    // 7. advect velocity
    cb.dissipation = powf(m_cfg.velocityDissipation, stepsRef);
    bind(m_psoAdvectVel.Get(), m_velocity.read, nullptr, m_velocity.write, 3);
    m_velocity.Swap();
    // 8. advect dye (velocity displacement still uses sim texels, like reference)
    cb.dissipation = powf(m_cfg.densityDissipation, stepsRef);
    cb.dissipationFast = powf(m_cfg.decayFast, stepsRef);
    cb.decayThreshold = m_cfg.decayThreshold;
    cb.satRestore = 1.0f - powf(1.0f - fminf(m_cfg.satRestore, 0.9999f), dt);
    bind(m_psoAdvectDye.Get(), m_velocity.read, m_dye.read, m_dye.write, 4);
    m_dye.Swap();
}

void FluidRenderer::Splat(float x, float y, float dx, float dy, float r, float g, float b) {
    SimCB cb = {};
    cb.aspect = (float)m_width / (float)m_height;
    cb.pointX = x / m_width;
    cb.pointY = y / m_height;
    cb.radius = m_cfg.splatRadius / 100.0f;

    m_cmd->SetComputeRootSignature(m_computeRS.Get());

    auto run = [&](ID3D12PipelineState* pso, Tex* src, Tex* dst) {
        Transition(*src, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(*dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_cmd->SetPipelineState(pso);
        cb.dimsW = dst->w; cb.dimsH = dst->h;
        m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &cb, 0);
        m_cmd->SetComputeRootDescriptorTable(1, src->srv);
        m_cmd->SetComputeRootDescriptorTable(2, src->srv);
        m_cmd->SetComputeRootDescriptorTable(3, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(4, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(5, dst->uav);
        m_cmd->Dispatch(Groups(dst->w), Groups(dst->h), 1);
    };

    // velocity: add impulse, never capped
    cb.colorR = dx; cb.colorG = dy; cb.colorB = 0.0f;
    cb.cap = 1000000.0f;
    run(m_psoSplatVel.Get(), m_velocity.read, m_velocity.write);
    m_velocity.Swap();

    // dye: proportional brightness cap
    cb.colorR = r; cb.colorG = g; cb.colorB = b;
    cb.cap = m_cfg.maxBrightness;
    run(m_psoSplatDye.Get(), m_dye.read, m_dye.write);
    m_dye.Swap();
}

void FluidRenderer::MultipleSplats(int amount) {
    for (int i = 0; i < amount; i++) {
        // generateColor(): hue near the global wheel (or palette), *0.15, *10 for bursts
        RGB c;
        if (m_cfg.colorful) {
            float h = fmodf(WheelHue((RandF() - 0.5f) * 0.12f) + 1.0f, 1.0f);
            c = HSVtoRGB(h, 1.0f, 1.0f);
        } else {
            int n = m_cfg.moreColors ? 5 : 1;
            const float* p = m_cfg.splatColors + 3 * (rand() % n);
            c = { p[0], p[1], p[2] };
        }
        float x = m_width * RandF();
        float y = m_height * RandF();
        float dx = 1000.0f * (RandF() - 0.5f);
        float dy = 1000.0f * (RandF() - 0.5f);
        Splat(x, y, dx, dy, c.r * 1.5f, c.g * 1.5f, c.b * 1.5f);
    }
}

void FluidRenderer::RenderDisplay() {
    const UINT i = m_frameIndex;
    Transition(*m_dye.read, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_backBuffers[i].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)i * m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(m_psoDisplay.Get());
    float consts[24];
    BuildDisplayConstants(consts);
    m_cmd->SetGraphicsRoot32BitConstants(0, 24, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd->ResourceBarrier(1, &b);
}

void FluidRenderer::BuildDisplayConstants(float out[24]) {
    BuildDisplayConstantsEx(out, m_width, m_height, m_sdrScale, m_cfg.hdrPeakNits);
}

void FluidRenderer::BuildDisplayConstantsEx(float out[24], int w, int h,
                                            float sdrScale, float peakNits) {
    // CSS-filter chain in reference order: saturate -> brightness -> contrast
    // -> hue-rotate. Composed into one matrix + offset. Hue-rotate preserves
    // white, so the contrast offset passes through unchanged.
    Mat3 M = Mat3Identity();
    float off = 0.0f;
    if (m_hdrActive && m_cfg.hdrCompensation) {
        M = CssSaturate(m_cfg.hdrSaturation);
        M = Mat3Scale(M, m_cfg.hdrBrightness * m_cfg.hdrContrast);
        off = 0.5f * (1.0f - m_cfg.hdrContrast);
    }
    float hue = fmodf(m_hueAngle, 360.0f);
    if (hue < -0.05f || hue > 0.05f)
        M = Mat3Mul(CssHueRotate(hue), M);

    // Post color filter (WE right-panel equivalent), applied outermost.
    // All these matrices map grey to grey, so the running offset stays scalar:
    // it just picks up the brightness*contrast gain plus the contrast shift.
    if (m_cfg.postSaturation != 1.0f || m_cfg.postContrast != 1.0f ||
        m_cfg.postBrightness != 1.0f || m_cfg.postHue != 0.0f) {
        Mat3 P = CssSaturate(m_cfg.postSaturation);
        P = Mat3Scale(P, m_cfg.postBrightness * m_cfg.postContrast);
        if (m_cfg.postHue != 0.0f)
            P = Mat3Mul(CssHueRotate(m_cfg.postHue), P);
        M = Mat3Mul(P, M);
        off = off * m_cfg.postBrightness * m_cfg.postContrast +
              0.5f * (1.0f - m_cfg.postContrast);
    }

    // HDR highlight expansion: gain that carries hot dye from SDR white up to
    // the peak-nits target (resolved by the shell; 0 = parity mode).
    float peakGain = 1.0f;
    if (m_hdrActive && peakNits > 0.0f) {
        float sdrWhiteNits = 80.0f * sdrScale;
        peakGain = fmaxf(1.0f, peakNits / fmaxf(sdrWhiteNits, 1.0f));
    }

    float consts[24] = { 1.0f / w, 1.0f / h,
                         m_cfg.shading ? 1.0f : 0.0f, sdrScale,
                         (float)m_cfg.gamutMode, peakGain, m_cfg.hdrKnee,
                         fmaxf(m_cfg.maxBrightness, m_cfg.hdrKnee + 0.05f),
                         M.m[0], M.m[1], M.m[2], 0,
                         M.m[3], M.m[4], M.m[5], 0,
                         M.m[6], M.m[7], M.m[8], 0,
                         off, off, off, 0 };
    memcpy(out, consts, sizeof(consts));
}

void FluidRenderer::CreateAnalyzerResources() {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = kAnaW;
    rd.Height = kAnaH;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    m_anaState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, m_anaState,
                                         nullptr, IID_PPV_ARGS(&m_anaTex)));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)kFrames * m_rtvStride;
    m_device->CreateRenderTargetView(m_anaTex.Get(), nullptr, rtv);

    m_anaPitch = (kAnaW * 8 + 255) & ~255u;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)m_anaPitch * kAnaH;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES rb = {};
    rb.Type = D3D12_HEAP_TYPE_READBACK;
    HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&m_anaReadback)));
}

// Re-render the display pass at analyzer resolution and queue a readback.
// Runs at ~10 Hz; the result is the exact final scRGB output (post gamut,
// post peak expansion) — what the display receives.
void FluidRenderer::MaybeRenderAnalyzer() {
    if (m_anaPending || m_time - m_lastAnaTime < 0.1f) return;
    if (!m_anaTex) CreateAnalyzerResources();
    m_lastAnaTime = m_time;

    if (m_anaState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_anaTex.Get();
        b.Transition.StateBefore = m_anaState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmd->ResourceBarrier(1, &b);
        m_anaState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)kFrames * m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)kAnaW, (float)kAnaH, 0, 1 };
    D3D12_RECT sc = { 0, 0, kAnaW, kAnaH };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(m_psoDisplay.Get());
    float consts[24];
    BuildDisplayConstants(consts);
    m_cmd->SetGraphicsRoot32BitConstants(0, 24, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_anaTex.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);
    m_anaState = D3D12_RESOURCE_STATE_COPY_SOURCE;

    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    src.pResource = m_anaTex.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = m_anaReadback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dst.PlacedFootprint.Footprint.Width = kAnaW;
    dst.PlacedFootprint.Footprint.Height = kAnaH;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = m_anaPitch;
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    m_anaFence = m_nextFence;   // signaled by EndFrameAndPresent
    m_anaPending = true;
}

bool FluidRenderer::ReadAnalyzerFrame(std::vector<float>& out) {
    if (!m_anaPending || !m_anaReadback) return false;
    if (m_fence->GetCompletedValue() < m_anaFence) return false;

    uint8_t* data = nullptr;
    D3D12_RANGE range = { 0, (SIZE_T)m_anaPitch * kAnaH };
    if (FAILED(m_anaReadback->Map(0, &range, (void**)&data))) return false;
    out.resize((size_t)kAnaW * kAnaH * 4);
    for (int y = 0; y < kAnaH; y++) {
        const uint16_t* row = (const uint16_t*)(data + (SIZE_T)y * m_anaPitch);
        float* dst = out.data() + (size_t)y * kAnaW * 4;
        for (int i = 0; i < kAnaW * 4; i++)
            dst[i] = HalfToFloat(row[i]);
    }
    D3D12_RANGE none = { 0, 0 };
    m_anaReadback->Unmap(0, &none);
    m_anaPending = false;
    return true;
}

// ---------------------------------------------------------------------------
// Second-monitor mirror: same dye field, own swapchain + HDR mapping
// ---------------------------------------------------------------------------

void FluidRenderer::EnableMirror(HWND hwnd, int width, int height) {
    DisableMirror();
    m_mirrorW = width;
    m_mirrorH = height;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrames;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd,
                                                 nullptr, nullptr, &sc1)) ||
        FAILED(sc1.As(&m_mirrorChain))) {
        printf("mirror: swapchain creation failed\n");
        m_mirrorChain.Reset();
        return;
    }
    const DXGI_COLOR_SPACE_TYPE scRGB = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    UINT support = 0;
    if (SUCCEEDED(m_mirrorChain->CheckColorSpaceSupport(scRGB, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        m_mirrorChain->SetColorSpace1(scRGB);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(kFrames + 1) * m_rtvStride;
    for (UINT i = 0; i < kFrames; i++) {
        HR(m_mirrorChain->GetBuffer(i, IID_PPV_ARGS(&m_mirrorBuffers[i])));
        m_device->CreateRenderTargetView(m_mirrorBuffers[i].Get(), nullptr, rtv);
        rtv.ptr += m_rtvStride;
    }
    m_mirrorBroken = false;
    printf("mirror enabled: %dx%d\n", width, height);
}

void FluidRenderer::DisableMirror() {
    if (!m_mirrorChain) return;
    WaitForGpuIdle();
    for (UINT i = 0; i < kFrames; i++) m_mirrorBuffers[i].Reset();
    m_mirrorChain.Reset();
    m_mirrorBroken = false;
    printf("mirror disabled\n");
}

void FluidRenderer::RenderMirror() {
    UINT bi = m_mirrorChain->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_mirrorBuffers[bi].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(kFrames + 1 + bi) * m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_mirrorW, (float)m_mirrorH, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_mirrorW, m_mirrorH };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(m_psoDisplay.Get());
    float consts[24];
    BuildDisplayConstantsEx(consts, m_mirrorW, m_mirrorH, m_mirrorSdrScale, m_mirrorPeakNits);
    m_cmd->SetGraphicsRoot32BitConstants(0, 24, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd->ResourceBarrier(1, &b);
}

void FluidRenderer::RenderGradient(float timeSec) {
    const UINT i = m_frameIndex;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_backBuffers[i].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)i * m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(m_psoGradient.Get());
    float consts[8] = { (float)m_width, (float)m_height, timeSec,
                        m_hdrActive ? 1.0f : 0.0f, 0, 0, 0, 0 };
    m_cmd->SetGraphicsRoot32BitConstants(0, 8, consts, 0);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd->ResourceBarrier(1, &b);
}

void FluidRenderer::ReassertColorSpace() {
    if (!m_swapChain) return;
    const DXGI_COLOR_SPACE_TYPE scRGB = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    UINT support = 0;
    if (SUCCEEDED(m_swapChain->CheckColorSpaceSupport(scRGB, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        m_swapChain->SetColorSpace1(scRGB);
}

void FluidRenderer::Frame(float dt, float sdrScale, bool hdrActive, const FrameInput& input) {
    m_hdrActive = hdrActive;
    m_sdrScale = sdrScale;
    m_time += dt;

    BeginFrame();

    if (m_cfg.gradientMode) {
        RenderGradient(m_time);
        EndFrameAndPresent();
        return;
    }

    if (m_firstFrame) {
        // zero all sim textures, then the reference's startup burst
        SimCB cb = {};
        m_cmd->SetComputeRootSignature(m_computeRS.Get());
        auto clear = [&](ID3D12PipelineState* pso, Tex& t) {
            Transition(t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_cmd->SetPipelineState(pso);
            cb.dimsW = t.w; cb.dimsH = t.h;
            m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &cb, 0);
            m_cmd->SetComputeRootDescriptorTable(1, t.srv);
            m_cmd->SetComputeRootDescriptorTable(2, t.srv);
            m_cmd->SetComputeRootDescriptorTable(3, t.uav);
            m_cmd->SetComputeRootDescriptorTable(4, t.uav);
            m_cmd->SetComputeRootDescriptorTable(5, t.uav);
            m_cmd->Dispatch(Groups(t.w), Groups(t.h), 1);
            UavBarrier(t.res.Get());
        };
        clear(m_psoClearV.Get(), m_velocity.a);
        clear(m_psoClearV.Get(), m_velocity.b);
        clear(m_psoClear4.Get(), m_dye.a);
        clear(m_psoClear4.Get(), m_dye.b);
        clear(m_psoClear1.Get(), m_pressure.a);
        clear(m_psoClear1.Get(), m_pressure.b);
        clear(m_psoClear1.Get(), m_divergence);
        clear(m_psoClear1.Get(), m_curl);
        MultipleSplats((int)(RandF() * 20) + 3);
        m_firstFrame = false;
    }

    // global color wheel
    m_globalHue = fmodf(m_globalHue + dt / fmaxf(1.0f, m_cfg.colorCyclePeriod), 1.0f);

    // continuous emitters (wanderers/dart/mouse) fire once per FRAME; scale
    // their dye by dt so a 144 fps loop doesn't paint 2.4x hotter than 60 fps
    m_emitScale = fminf(fmaxf(dt * 60.0f, 0.25f), 2.0f);

    // reference update() order: coverage, wanderers, dart, hue shift, input
    UpdateCoverage();
    if (m_cfg.wanderers && (m_time - m_lastInteraction > m_cfg.wandererResumeDelay))
        UpdateWanderers(dt);
    UpdateDart(dt);
    UpdateHueShift(dt);
    HandleInput(input);

    // idle random splats (project.json: every 9.6 s, 8 splats)
    if (m_cfg.idleSplats) {
        m_idleTimer += dt;
        if (m_idleTimer >= m_cfg.idleInterval) {
            m_idleTimer = 0;
            MultipleSplats(m_cfg.idleAmount);
        }
    }

    // fixed-substep integration like the reference update loop
    float remaining = fminf(dt, 0.05f);
    const float stepSize = 0.016f;
    while (remaining > stepSize) {
        SimStep(stepSize);
        remaining -= stepSize;
    }
    if (remaining > 0.0001f) SimStep(remaining);

    if (m_cfg.stats) {
        m_statsTimer += dt;
        if (m_statsTimer >= 5.0f && !m_readbackPending) {
            m_statsTimer = 0;
            Transition(*m_dye.read, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = m_dye.read->res.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = m_readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            dst.PlacedFootprint.Footprint.Width = m_dyeW;
            dst.PlacedFootprint.Footprint.Height = m_dyeH;
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = m_readbackPitch;
            m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            m_readbackPending = true;
        }
    }

    RenderDisplay();
    if (m_mirrorChain && !m_mirrorBroken) RenderMirror();
    if (m_anaEnabled) MaybeRenderAnalyzer();
    EndFrameAndPresent();

    if (m_readbackPending) {
        WaitForGpuIdle();
        ReportStats();
        m_readbackPending = false;
    }
}

static float HalfToFloat(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t man = h & 0x3FF;
    uint32_t f;
    if (exp == 0) {
        if (!man) f = sign;
        else {
            exp = 112;
            while (!(man & 0x400)) { man <<= 1; exp--; }
            man &= 0x3FF;
            f = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) {
        f = sign | 0x7F800000u | (man << 13);
    } else {
        f = sign | ((exp + 112) << 23) | (man << 13);
    }
    float r;
    memcpy(&r, &f, 4);
    return r;
}

void FluidRenderer::ReportStats() {
    uint8_t* data = nullptr;
    D3D12_RANGE range = { 0, (SIZE_T)m_readbackPitch * m_dyeH };
    if (FAILED(m_readback->Map(0, &range, (void**)&data))) return;

    double sum = 0;
    float mx = 0;
    int nonBlack = 0, samples = 0, nans = 0;
    for (int y = 0; y < m_dyeH; y += 8) {
        const uint16_t* row = (const uint16_t*)(data + (SIZE_T)y * m_readbackPitch);
        for (int x = 0; x < m_dyeW; x += 8) {
            float r = HalfToFloat(row[x * 4 + 0]);
            float g = HalfToFloat(row[x * 4 + 1]);
            float b = HalfToFloat(row[x * 4 + 2]);
            if (r != r || g != g || b != b) { nans++; continue; }
            float m = fmaxf(r, fmaxf(g, b));
            sum += m;
            if (m > mx) mx = m;
            if (m > 0.02f) nonBlack++;
            samples++;
        }
    }
    D3D12_RANGE none = { 0, 0 };
    m_readback->Unmap(0, &none);
    printf("[stats] dye mean=%.4f max=%.3f lit=%.1f%% NaN=%d\n",
           samples ? sum / samples : 0.0, mx,
           samples ? 100.0 * nonBlack / samples : 0.0, nans);
}

void FluidRenderer::WaitForGpuIdle() {
    const UINT64 v = m_nextFence++;
    HR(m_queue->Signal(m_fence.Get(), v));
    if (m_fence->GetCompletedValue() < v) {
        HR(m_fence->SetEventOnCompletion(v, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void FluidRenderer::Shutdown() {
    WaitForGpuIdle();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
}

// ---------------------------------------------------------------------------
// M3: autonomous behaviors, ported from reference/script.js
// ---------------------------------------------------------------------------

// Map the cycling wheel position into the configured hue band. Full range
// (180) = classic wheel; narrower = a triangle sweep across the themed slice,
// with offsets (wanderer/dart) scaled into the band too.
float FluidRenderer::WheelHue(float offset) {
    float rangeFrac = m_cfg.hueRange / 180.0f;
    if (rangeFrac >= 0.999f) return m_globalHue + offset;
    float tri = 1.0f - fabsf(2.0f * m_globalHue - 1.0f);   // 0..1..0 per lap
    return m_cfg.hueCenter / 360.0f + (tri - 0.5f) * rangeFrac + offset * rangeFrac;
}

// Reference color sources: cycledColor() when "colorful" (hue wheel), else a
// random pick from the fixed splat palette. Both dimmed to 0.15.
void FluidRenderer::PickSplatColor(float hueOffset, float out[3]) {
    if (m_cfg.colorful) {
        RGB c = CycledColorAt(WheelHue(hueOffset));
        out[0] = c.r; out[1] = c.g; out[2] = c.b;
    } else {
        int n = m_cfg.moreColors ? 5 : 1;
        const float* p = m_cfg.splatColors + 3 * (rand() % n);
        out[0] = p[0] * 0.15f; out[1] = p[1] * 0.15f; out[2] = p[2] * 0.15f;
    }
}

// Explorer restarted: the old WorkerW (and our window in it) died. The caller
// made a fresh wallpaper window; rebuild just the swapchain onto it — device,
// sim textures, and the fluid state all survive.
void FluidRenderer::Reattach(HWND hwnd) {
    DisableMirror();   // its window died with the old WorkerW; shell re-enables
    WaitForGpuIdle();
    for (UINT i = 0; i < kFrames; i++) {
        m_backBuffers[i].Reset();
        m_fenceValues[i] = 0;
    }
    m_swapChain.Reset();
    m_factory.Reset();
    HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrames;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> sc1;
    HR(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1));
    HR(sc1.As(&m_swapChain));
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    ReassertColorSpace();

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrames; i++) {
        HR(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv);
        rtv.ptr += m_rtvStride;
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_presentBroken = false;
    printf("reattached to new wallpaper window\n");
}

void FluidRenderer::SetResolutions(int simRes, int dyeRes) {
    m_cfg.simRes = simRes;
    m_cfg.dyeRes = dyeRes;
    WaitForGpuIdle();
    m_covPending = false;
    m_readbackPending = false;
    CreateSimResources();     // recreates textures/readbacks, reinits wanderers
    m_firstFrame = true;      // clear + startup splat burst next frame
    printf("resolutions changed: sim %dx%d, dye %dx%d\n", m_simW, m_simH, m_dyeW, m_dyeH);
}

void FluidRenderer::InitWanderers() {
    m_wanderers.clear();
    int n = m_cfg.wandererCount < 1 ? 1 : m_cfg.wandererCount;
    for (int i = 0; i < n; i++) {
        Wanderer w = {};
        w.R = fminf((float)m_width, (float)m_height) * m_cfg.wandererScale *
              (0.6f + RandF() * 0.8f) / 2.0f;
        w.x = m_width * (0.2f + RandF() * 0.6f);
        w.y = m_height * (0.2f + RandF() * 0.6f);
        w.heading = RandF() * 6.2831853f;
        w.turn = 0.0f;
        w.cx = m_width * (0.25f + RandF() * 0.5f);
        w.cy = m_height * (0.3f + RandF() * 0.4f);
        w.phase = RandF() * 6.2831853f;
        w.dir = RandF() < 0.5f ? 1 : -1;
        w.hueOffset = i * 0.13f;
        m_wanderers.push_back(w);
    }
}

void FluidRenderer::UpdateWanderers(float dt) {
    const float margin = 40.0f;
    for (size_t i = 0; i < m_wanderers.size(); i++) {
        // wanderer 0 is the survivor: it obeys its own floor, the rest the group floor
        if (i == 0 ? m_survivorTooFull : m_screenTooFull) continue;
        Wanderer& w = m_wanderers[i];
        float nx, ny;
        if (m_cfg.wandererMode == 1) {          // circle
            w.phase += w.dir * (m_cfg.wandererSpeed * dt) / w.R;
            nx = w.cx + cosf(w.phase) * w.R;
            ny = w.cy + sinf(w.phase) * w.R;
        } else if (m_cfg.wandererMode == 2) {   // figure8
            w.phase += w.dir * (m_cfg.wandererSpeed * dt) / w.R;
            nx = w.cx + sinf(w.phase) * w.R * 1.6f;
            ny = w.cy + sinf(w.phase * 2.0f) * w.R * 0.8f;
        } else {                                // random smooth wander
            w.turn += (RandF() - 0.5f) * 12.0f * dt;
            w.turn = fmaxf(-2.5f, fminf(2.5f, w.turn)) * 0.98f;
            w.heading += w.turn * dt * 4.0f;
            nx = w.x + cosf(w.heading) * m_cfg.wandererSpeed * dt;
            ny = w.y + sinf(w.heading) * m_cfg.wandererSpeed * dt;
            if (nx < margin || nx > m_width - margin) {
                w.heading = 3.14159265f - w.heading;
                nx = fmaxf(margin, fminf((float)m_width - margin, nx));
            }
            if (ny < margin || ny > m_height - margin) {
                w.heading = -w.heading;
                ny = fmaxf(margin, fminf((float)m_height - margin, ny));
            }
        }
        float dx = (nx - w.x) * 5.0f;
        float dy = (ny - w.y) * 5.0f;
        w.x = nx;
        w.y = ny;
        float c[3];
        PickSplatColor(w.hueOffset, c);
        float wb = m_cfg.wandererBrightness * m_emitScale;
        Splat(w.x, w.y, dx, dy, c[0] * wb, c[1] * wb, c[2] * wb);
    }
}

void FluidRenderer::UpdateDart(float dt) {
    if (!m_dart.active) {
        if (!m_cfg.dartEnabled || !m_screenTooFull || !m_cfg.wanderers) return;
        if (m_time - m_lastDartTime < m_cfg.dartInterval) return;
        m_lastDartTime = m_time;
        // start on a random edge, aim at a random point on the opposite edge
        float m = 10.0f, W = (float)m_width, H = (float)m_height;
        int side = (int)(RandF() * 4.0f) & 3;
        float sx, sy, ex, ey;
        if (side == 0)      { sx = m;     sy = RandF() * H; ex = W - m;       ey = RandF() * H; }
        else if (side == 1) { sx = W - m; sy = RandF() * H; ex = m;           ey = RandF() * H; }
        else if (side == 2) { sx = RandF() * W; sy = m;     ex = RandF() * W; ey = H - m; }
        else                { sx = RandF() * W; sy = H - m; ex = RandF() * W; ey = m; }
        float ddx = ex - sx, ddy = ey - sy;
        float len = sqrtf(ddx * ddx + ddy * ddy);
        m_dart = { sx, sy, ddx / len, ddy / len, len, true };
        return;
    }
    float step = fminf(m_cfg.dartSpeed * dt, m_dart.left);
    float nx = m_dart.x + m_dart.ux * step;
    float ny = m_dart.y + m_dart.uy * step;
    // complementary hue to the current wheel position, at wanderer brightness
    float c[3];
    PickSplatColor(0.5f, c);
    float wb = m_cfg.wandererBrightness * m_emitScale;
    Splat(nx, ny, (nx - m_dart.x) * 5.0f, (ny - m_dart.y) * 5.0f,
          c[0] * wb, c[1] * wb, c[2] * wb);
    m_dart.x = nx;
    m_dart.y = ny;
    m_dart.left -= step;
    if (m_dart.left <= 0.5f) m_dart.active = false;
}

void FluidRenderer::UpdateHueShift(float dt) {
    if (!m_cfg.hsEnabled) {
        m_hueAngle = 0;
        m_hsPhase = 0; m_hsTimer = 0; m_hsStep = 0;
        return;
    }
    m_hsTimer += dt;
    if (m_hsPhase == 0) {           // off
        if (m_hsTimer >= m_cfg.hsOffTime) {
            m_hsPhase = 1; m_hsTimer = 0; m_hsStep = 1;
            m_hsFrom = 0; m_hsTo = m_cfg.hsStep;
        }
    } else if (m_hsPhase == 2) {    // linger
        if (m_hsTimer >= m_cfg.hsLinger) {
            m_hsTimer = 0;
            if (m_hsStep >= m_cfg.hsBurstSteps) {
                // rotate forward to the next full turn so the palette lands home
                m_hsPhase = 3; m_hsFrom = m_hueAngle;
                m_hsTo = ceilf((m_hueAngle + 0.001f) / 360.0f) * 360.0f;
            } else {
                m_hsStep++; m_hsPhase = 1;
                m_hsFrom = m_hueAngle; m_hsTo = m_hsStep * m_cfg.hsStep;
            }
        }
    } else {                        // glide (1) or return (3)
        float t = fminf(1.0f, m_hsTimer / fmaxf(0.05f, m_cfg.hsGlide));
        t = t * t * (3.0f - 2.0f * t);
        m_hueAngle = m_hsFrom + (m_hsTo - m_hsFrom) * t;
        if (t >= 1.0f) {
            m_hsTimer = 0;
            if (m_hsPhase == 1) m_hsPhase = 2;
            else { m_hsPhase = 0; m_hueAngle = 0; m_hsStep = 0; }
        }
    }
}

void FluidRenderer::HandleInput(const FrameInput& in) {
    if (in.userInteracted) m_lastInteraction = m_time;
    float c[3];
    PickSplatColor(0.0f, c);
    float es = m_emitScale;
    if (m_cfg.holdToSplat && in.mouseDown) {
        float jx = (RandF() - 0.5f) * 200.0f;
        float jy = (RandF() - 0.5f) * 200.0f;
        Splat(in.mouseX, in.mouseY, in.mouseDx * 0.5f + jx, in.mouseDy * 0.5f + jy,
              c[0] * es, c[1] * es, c[2] * es);
    } else if (!m_cfg.holdToSplat && m_cfg.splatOnClick &&
               in.mouseDown && !m_prevMouseDown) {
        MultipleSplats(rand() % 20 + 5);   // reference click burst (one-shot, unscaled)
    }
    m_prevMouseDown = in.mouseDown;
    if (in.mouseMoved && m_cfg.showMouse)
        Splat(in.mouseX, in.mouseY, in.mouseDx, in.mouseDy,
              c[0] * es, c[1] * es, c[2] * es);
}

void FluidRenderer::UpdateCoverage() {
    if (!m_cfg.autoPause || !m_cfg.wanderers) {
        m_screenTooFull = false;
        m_survivorTooFull = false;
        return;
    }
    // harvest a finished readback (issued a frame or more ago; 1 Hz cadence
    // makes the staleness irrelevant and avoids any GPU sync)
    if (m_covPending && m_fence->GetCompletedValue() >= m_covFence) {
        uint8_t* data = nullptr;
        D3D12_RANGE range = { 0, (SIZE_T)m_covPitch * kCovH };
        if (SUCCEEDED(m_covReadback->Map(0, &range, (void**)&data))) {
            ProcessCoverage(data, m_covPitch);
            D3D12_RANGE none = { 0, 0 };
            m_covReadback->Unmap(0, &none);
        }
        m_covPending = false;
    }
    if (!m_covPending && m_time - m_lastCovTime >= 1.0f) {
        m_lastCovTime = m_time;
        SimCB cb = {};
        m_cmd->SetComputeRootSignature(m_computeRS.Get());
        Transition(*m_dye.read, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(m_coverage, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_cmd->SetPipelineState(m_psoDownsample.Get());
        cb.dimsW = kCovW; cb.dimsH = kCovH;
        m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &cb, 0);
        m_cmd->SetComputeRootDescriptorTable(1, m_dye.read->srv);
        m_cmd->SetComputeRootDescriptorTable(2, m_dye.read->srv);
        m_cmd->SetComputeRootDescriptorTable(3, m_coverage.uav);
        m_cmd->SetComputeRootDescriptorTable(4, m_coverage.uav);
        m_cmd->SetComputeRootDescriptorTable(5, m_coverage.uav);
        m_cmd->Dispatch(Groups(kCovW), Groups(kCovH), 1);

        Transition(m_coverage, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
        src.pResource = m_coverage.res.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = m_covReadback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        dst.PlacedFootprint.Footprint.Width = kCovW;
        dst.PlacedFootprint.Footprint.Height = kCovH;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = m_covPitch;
        m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        m_covFence = m_nextFence;   // EndFrameAndPresent signals this value
        m_covPending = true;
    }
}

// Port of measureCoverage(): dark-area %, 6x3 tile contrast, hysteresis.
void FluidRenderer::ProcessCoverage(const uint8_t* data, UINT pitch) {
    int dark = 0;
    const int total = kCovW * kCovH;
    float tiles[18] = {};   // 6x3
    const int TX = 6, TY = 3;
    const float tw = kCovW / (float)TX, th = kCovH / (float)TY;

    for (int y = 0; y < kCovH; y++) {
        const uint16_t* row = (const uint16_t*)(data + (SIZE_T)y * pitch);
        for (int x = 0; x < kCovW; x++) {
            float r = HalfToFloat(row[x * 4 + 0]);
            float g = HalfToFloat(row[x * 4 + 1]);
            float b = HalfToFloat(row[x * 4 + 2]);
            float m = fmaxf(r, fmaxf(g, b));
            if (m <= m_cfg.darkLevel) dark++;
            int ti = (int)fminf(TX - 1.0f, x / tw) + TX * (int)fminf(TY - 1.0f, y / th);
            tiles[ti] += m;
        }
    }
    float darkPct = 100.0f * dark / total;

    float maxTile = 0, sumTiles = 0;
    for (int t = 0; t < TX * TY; t++) {
        tiles[t] /= tw * th;
        sumTiles += tiles[t];
        if (tiles[t] > maxTile) maxTile = tiles[t];
    }
    float avgRest = (sumTiles - maxTile) / (TX * TY - 1);
    bool contrastOK = m_cfg.contrastReq <= 0 ||
                      maxTile >= avgRest * (1.0f + m_cfg.contrastReq / 100.0f);

    // hysteresis: pause below the floor, resume only once 10 points above it
    if (!m_screenTooFull && darkPct < m_cfg.darkFloor) m_screenTooFull = true;
    else if (m_screenTooFull && darkPct > fminf(m_cfg.darkFloor + 10.0f, 95.0f)) m_screenTooFull = false;
    if (!m_survivorTooFull && darkPct < m_cfg.survDarkFloor) m_survivorTooFull = true;
    else if (m_survivorTooFull && darkPct > fminf(m_cfg.survDarkFloor + 10.0f, 95.0f)) m_survivorTooFull = false;

    // flat dim mush (no standout region): cancel pauses, keep painting
    if (!contrastOK) {
        m_screenTooFull = false;
        m_survivorTooFull = false;
    }
}
