// OilWallpaper — lava-lamp oil-blob renderer (proof of concept).
// D3D12 device + swapchain + ONE fullscreen-triangle graphics pipeline.
// The whole look lives in the pixel shader: a 64-blob metaball field with
// influence-weighted palette colors, rim darkening, gloss from the analytic
// field gradient, and fbm marbling. The CPU sim (StepBlobs) drifts the
// blobs through a pseudo-curl field with radius-proportional buoyancy.

#include "oil.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cmath>
#include <random>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static void HR(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        fprintf(stderr, "FATAL: %s failed (hr=0x%08lX)\n", what, (unsigned long)hr);
        MessageBoxA(nullptr, what, "Oil Wallpaper - fatal error", MB_ICONERROR);
        ExitProcess(1);
    }
}

// ---------------------------------------------------------------------------
// Per-frame constant buffer — keep in sync with cbuffer CB in kShaderSrc.
// ---------------------------------------------------------------------------
struct OilCBReal {
    float blobs[64][4];      // xy = pos uv, z = radius (y units), w = colorIndex
    float bg[4];
    float bgAccent[4];
    float blobCol[3][4];
    float time, aspect, marbleScale, rimWidth;
};
static_assert(sizeof(OilCBReal) == 64 * 16 + 2 * 16 + 3 * 16 + 16, "CB layout");

// ---------------------------------------------------------------------------
// Shader: runtime-compiled, same pattern as src/shaders.h in the fluid app.
// ---------------------------------------------------------------------------
static const char* kShaderSrc = R"hlsl(
cbuffer CB : register(b0) {
    float4 blobs[64];      // xy pos uv (y down), z radius, w colorIndex
    float4 bg;
    float4 bgAccent;
    float4 blobCol[3];
    float  time;
    float  aspect;
    float  marbleScale;
    float  rimWidth;
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    float2 xy = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(xy * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(xy.x, 1.0 - xy.y);   // uv.y = 0 at top, 1 at bottom
    return o;
}

float hash21(float2 p) {
    p = frac(p * float2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return frac(p.x * p.y);
}

float vnoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1, 0));
    float c = hash21(i + float2(0, 1));
    float d = hash21(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p) {
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 3; i++) {
        v += amp * vnoise(p);
        p = p * 2.13 + 19.19;
        amp *= 0.5;
    }
    return v;
}

float4 PSMain(VSOut inp) : SV_Target {
    float2 p = float2(inp.uv.x * aspect, inp.uv.y);

    // Metaball field + analytic gradient + influence-weighted color sum.
    // Compact cutoff on the tails so blobs stay distinct until they nearly
    // touch (merging starts around center distance ~2.6r), then neck.
    float  field  = 0.0;
    float2 grad   = float2(0.0, 0.0);
    float3 colSum = float3(0.0, 0.0, 0.0);
    [loop]
    for (int b = 0; b < 64; b++) {
        float4 bl = blobs[b];
        float2 q  = p - float2(bl.x * aspect, bl.y);
        float  d2 = dot(q, q) + 1e-6;
        float  r2 = bl.z * bl.z;
        float  w  = max(r2 / d2 - 0.08, 0.0);   // compact tail cutoff
        w = min(w, 3.5);                        // per-blob influence clamp
        field  += w;
        grad   += (-2.0 * r2 / (d2 * d2)) * q;  // d(w)/dp, unclamped branch
        colSum += blobCol[(uint)(bl.w + 0.5)].rgb * w;
    }

    // anti-aliased coverage at the field == 1 surface
    float aa  = fwidth(field) * 1.5 + 1e-4;
    float cov = smoothstep(1.0 - aa, 1.0 + aa, field);

    // Backdrop: vertical gradient bg -> bgAccent, blotchy fbm texture, grain.
    float blotch = fbm(p * 2.6 + float2(time * 0.008, -time * 0.005));
    float3 bgc = lerp(bg.rgb, bgAccent.rgb, pow(inp.uv.y, 1.15));
    bgc *= 0.90 + 0.20 * blotch;
    bgc += (hash21(inp.pos.xy) - 0.5) * 0.028;

    float3 col = bgc;
    if (field > 1.0 - aa) {
        float3 bcol = colSum / max(field, 1e-4);

        // granular marbling: brightness modulated +/-15% by slow fbm
        float m = fbm(p * marbleScale + float2(time * 0.020, -time * 0.013));
        bcol *= 1.0 + (m - 0.5) * 0.30;

        // Rim darkening: distance-to-edge estimate sdf = (field-1)/|grad|
        // gives a constant-width dark outline even inside merged masses
        // (where the raw field stays low); the field-based term keeps
        // isolated cores saturated. rimWidth is in uv units.
        float sdf = (field - 1.0) / (length(grad) + 1e-5);
        float rim = max(smoothstep(0.0, rimWidth, sdf),
                        smoothstep(1.0, 2.4, field));
        bcol *= lerp(0.38, 1.0, rim);
        bcol *= 1.0 + 0.10 * smoothstep(2.0, 5.0, field);

        // gloss: fake surface normal from the field gradient (points inward;
        // -grad tilts the normal outward toward the rim). Tilt fades toward
        // the core so centers stay flat.
        float2 gd = normalize(-grad + float2(1e-6, 0.0));
        float  tilt = 0.75 * exp(-0.45 * max(field - 1.0, 0.0));
        float3 n = normalize(float3(gd * tilt, 1.0));
        float3 l = normalize(float3(-0.45, -0.55, 0.7071)); // from top-left
        float diff = saturate(dot(n, l));
        float spec = pow(saturate(dot(n, normalize(l + float3(0.0, 0.0, 1.0)))), 64.0);
        bcol *= 0.82 + 0.34 * diff;
        bcol += spec * 0.18 * rim;

        col = lerp(bgc, saturate(bcol), cov);
    }
    return float4(col, 1.0);
}
)hlsl";

static ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(src, strlen(src), entry, nullptr, nullptr, entry, target,
                          0, 0, &blob, &err))) {
        fprintf(stderr, "shader '%s' compile error:\n%s\n", entry,
                err ? (char*)err->GetBufferPointer() : "?");
        ExitProcess(1);
    }
    return blob;
}

// ---------------------------------------------------------------------------
// Palettes (sRGB values, written raw to the UNORM swapchain).
// ---------------------------------------------------------------------------
struct OilPalette { float bg[3], bgAccent[3], col[3][3]; };
static const OilPalette kPalettes[2] = {
    // Acid: orange backdrop -> dark brown; coral / teal / dark droplet
    { { 0.910f, 0.471f, 0.000f }, { 0.227f, 0.094f, 0.031f },
      { { 0.949f, 0.235f, 0.157f },   // #F23C28 coral red
        { 0.098f, 0.784f, 0.784f },   // #19C8C8 teal cyan
        { 0.165f, 0.071f, 0.024f } } },// #2A1206 dark droplet
    // Royal: deep purple backdrop -> violet; red-orange / dark red / dark bubble
    { { 0.165f, 0.039f, 0.333f }, { 0.416f, 0.059f, 0.659f },
      { { 0.949f, 0.188f, 0.059f },   // #F2300F red-orange
        { 0.627f, 0.071f, 0.020f },   // #A01205 darker red
        { 0.102f, 0.020f, 0.188f } } },// #1A0530 dark bubble
};

// ---------------------------------------------------------------------------
// Device / swapchain / pipeline
// ---------------------------------------------------------------------------
void OilRenderer::Init(HWND hwnd, int width, int height, const OilConfig& cfg) {
    m_cfg = cfg;
    m_width = width;
    m_height = height;
    CreateDevice();
    CreateSwapChainResources(hwnd, width, height);
    CreatePipeline();
    if (!m_seeded) { SeedBlobs(); m_seeded = true; }
    printf("Oil renderer ready: %dx%d, %d blobs, palette %d\n",
           width, height, kBlobCount, m_cfg.palette);
}

void OilRenderer::CreateDevice() {
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
    HR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");

    ComPtr<IDXGIAdapter1> adapter;
    HR(m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)), "EnumAdapter");
    DXGI_ADAPTER_DESC1 adesc = {};
    adapter->GetDesc1(&adesc);
    printf("GPU: %ls\n", adesc.Description);

    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                         IID_PPV_ARGS(&m_device)), "D3D12CreateDevice");

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)), "CreateCommandQueue");

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kFrames;
    HR(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)), "CreateRTVHeap");
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (UINT i = 0; i < kFrames; i++) {
        HR(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&m_allocators[i])), "CreateAllocator");
        // per-frame CB upload buffer, persistently mapped
        D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (sizeof(OilCBReal) + 255) & ~255u;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&m_cbUpload[i])), "CreateCB");
        D3D12_RANGE readRange = { 0, 0 };
        HR(m_cbUpload[i]->Map(0, &readRange, &m_cbData[i]), "MapCB");
    }
    HR(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_allocators[0].Get(), nullptr,
                                   IID_PPV_ARGS(&m_cmd)), "CreateCommandList");
    HR(m_cmd->Close(), "CloseCmd");

    HR(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

void OilRenderer::CreateSwapChainResources(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // SDR sRGB; HDR is a follow-up
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrames;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> sc1;
    HR(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1),
       "CreateSwapChainForHwnd");
    HR(sc1.As(&m_swapChain), "SwapChain3");
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrames; i++) {
        HR(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])), "GetBuffer");
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv);
        rtv.ptr += m_rtvStride;
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void OilRenderer::CreatePipeline() {
    // root signature: one CBV root descriptor (b0), pixel shader only
    D3D12_ROOT_PARAMETER rp = {};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp.Descriptor.ShaderRegister = 0;
    rp.Descriptor.RegisterSpace = 0;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 1;
    rsd.pParameters = &rp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, sigErr;
    HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &sigErr),
       "SerializeRootSignature");
    HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                     IID_PPV_ARGS(&m_rootSig)), "CreateRootSignature");

    ComPtr<ID3DBlob> vs = CompileShader(kShaderSrc, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = CompileShader(kShaderSrc, "PSMain", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = m_rootSig.Get();
    pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    HR(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_pso)), "CreatePSO");
}

void OilRenderer::WaitForGpu() {
    if (!m_queue || !m_fence) return;
    HR(m_queue->Signal(m_fence.Get(), m_nextFence), "Signal");
    if (m_fence->GetCompletedValue() < m_nextFence) {
        HR(m_fence->SetEventOnCompletion(m_nextFence, m_fenceEvent), "SetEvent");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_nextFence++;
}

void OilRenderer::Reattach(HWND hwnd) {
    if (!m_device) return;
    WaitForGpu();
    for (UINT i = 0; i < kFrames; i++) {
        m_backBuffers[i].Reset();
        m_fenceValues[i] = 0;
    }
    m_swapChain.Reset();
    CreateSwapChainResources(hwnd, m_width, m_height);
    m_presentBroken = false;
    printf("renderer reattached to new wallpaper window\n");
}

void OilRenderer::Shutdown() {
    if (m_device) WaitForGpu();
    for (UINT i = 0; i < kFrames; i++) {
        if (m_cbUpload[i]) m_cbUpload[i]->Unmap(0, nullptr);
        m_cbData[i] = nullptr;
        m_cbUpload[i].Reset();
        m_backBuffers[i].Reset();
        m_allocators[i].Reset();
    }
    m_pso.Reset();
    m_rootSig.Reset();
    m_cmd.Reset();
    m_swapChain.Reset();
    m_rtvHeap.Reset();
    m_fence.Reset();
    m_queue.Reset();
    m_device.Reset();
    m_factory.Reset();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}

// ---------------------------------------------------------------------------
// CPU blob sim — slow, viscous, lava-lamp.
// ---------------------------------------------------------------------------
void OilRenderer::SeedBlobs() {
    std::mt19937 rng((unsigned)GetTickCount() ^ 0x5EED);
    auto U = [&rng](float lo, float hi) {
        return lo + (hi - lo) * (float)rng() / (float)rng.max();
    };
    const float TWO_PI = 6.2831853f;
    for (int i = 0; i < kBlobCount; i++) {
        BlobCPU& b = m_blobs[i];
        // ~55% color 0, ~30% color 1, ~15% color 2 (small droplets)
        if (i < 35)      { b.colorIdx = 0; b.baseR = U(0.045f, 0.100f); }
        else if (i < 54) { b.colorIdx = 1; b.baseR = U(0.030f, 0.065f); }
        else             { b.colorIdx = 2; b.baseR = U(0.018f, 0.032f); }
        // seeded in the lower 2/3 of the screen (uv y down)
        b.x = U(0.03f, 0.97f);
        b.y = U(0.34f, 0.97f);
        b.vx = b.vy = 0.0f;
        b.phase = U(0.0f, TWO_PI);
        b.breathRate = U(0.15f, 0.45f);
        b.s1 = U(0.0f, TWO_PI);
        b.s2 = U(0.0f, TWO_PI);
        b.sinker = (U(0.0f, 1.0f) < 0.09f);   // a few fall instead of rising
    }
}

void OilRenderer::StepBlobs(float dt) {
    const float t = m_time;
    for (int i = 0; i < kBlobCount; i++) {
        BlobCPU& b = m_blobs[i];

        // Pseudo-curl drift: velocity = (dPSI/dy, -dPSI/dx) of an analytic
        // potential — divergence-free, so blobs swirl instead of clumping.
        float a1 = 3.1f * b.x + 0.23f * t + b.s1;
        float a2 = 2.7f * b.y + 0.19f * t;
        float a3 = 5.3f * b.x - 0.11f * t;
        float a4 = 4.1f * b.y + 0.29f * t + b.s2;
        float dpsidx = 3.1f * cosf(a1) * cosf(a2) + 0.7f * 5.3f * cosf(a3) * cosf(a4);
        float dpsidy = -2.7f * sinf(a1) * sinf(a2) - 0.7f * 4.1f * sinf(a3) * sinf(a4);
        const float driftK = 0.0030f;
        float tx = dpsidy * driftK;
        float ty = -dpsidx * driftK;

        // buoyancy: big blobs rise slowly (uv y down, so up = negative);
        // sinkers fall. Radius-proportional.
        const float buoyK = 0.012f;
        ty += (b.sinker ? 1.0f : -1.0f) * buoyK * (b.baseR / 0.10f);

        // high velocity damping: approach the target velocity quickly,
        // which keeps motion smooth and viscous
        float k = 1.0f - expf(-2.5f * dt);
        b.vx += (tx - b.vx) * k;
        b.vy += (ty - b.vy) * k;
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        // wrap with a small off-screen margin
        const float m = 0.16f;
        if (b.x < -m)      b.x += 1.0f + 2.0f * m;
        if (b.x > 1.0f + m) b.x -= 1.0f + 2.0f * m;
        if (b.y < -m)      b.y += 1.0f + 2.0f * m;
        if (b.y > 1.0f + m) b.y -= 1.0f + 2.0f * m;

        b.phase += b.breathRate * dt;
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void OilRenderer::Frame(float dt) {
    if (m_presentBroken || !m_device) return;
    m_time += dt;
    StepBlobs(dt);

    // fill + upload the per-frame constants
    OilCBReal cb = {};
    const OilPalette& pal = kPalettes[m_cfg.palette & 1];
    for (int i = 0; i < kBlobCount; i++) {
        const BlobCPU& b = m_blobs[i];
        float r = b.baseR * (1.0f + 0.15f * sinf(b.phase));   // breathing ±15%
        cb.blobs[i][0] = b.x;
        cb.blobs[i][1] = b.y;
        cb.blobs[i][2] = r;
        cb.blobs[i][3] = b.colorIdx;
    }
    for (int c = 0; c < 3; c++) {
        cb.bg[c] = pal.bg[c];
        cb.bgAccent[c] = pal.bgAccent[c];
        for (int j = 0; j < 3; j++) cb.blobCol[c][j] = pal.col[c][j];
    }
    cb.time = m_time;
    cb.aspect = (float)m_width / (float)m_height;
    cb.marbleScale = m_cfg.marbleScale;
    cb.rimWidth = m_cfg.rimWidth;
    memcpy(m_cbData[m_frameIndex], &cb, sizeof(cb));

    // record
    const UINT i = m_frameIndex;
    if (m_fence->GetCompletedValue() < m_fenceValues[i]) {
        HR(m_fence->SetEventOnCompletion(m_fenceValues[i], m_fenceEvent), "SetEvent");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    HR(m_allocators[i]->Reset(), "AllocReset");
    HR(m_cmd->Reset(m_allocators[i].Get(), nullptr), "CmdReset");

    D3D12_RESOURCE_BARRIER bar = {};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = m_backBuffers[i].Get();
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &bar);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)i * m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);
    m_cmd->SetGraphicsRootSignature(m_rootSig.Get());
    m_cmd->SetPipelineState(m_pso.Get());
    m_cmd->SetGraphicsRootConstantBufferView(0, m_cbUpload[i]->GetGPUVirtualAddress());
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    // --shot: this frame also gets copied to a readback buffer
    const bool doShot = m_shotPending && !m_shotDone && m_time >= m_shotDelay;
    if (doShot) {
        if (!m_readback) {
            m_readbackPitch = ((UINT)m_width * 4 + 255) & ~255u;
            D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_READBACK };
            D3D12_RESOURCE_DESC rd = {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = (UINT64)m_readbackPitch * m_height;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&m_readback)), "CreateReadback");
        }
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        m_cmd->ResourceBarrier(1, &bar);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = m_readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = m_width;
        dst.PlacedFootprint.Footprint.Height = m_height;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = m_readbackPitch;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_backBuffers[i].Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_cmd->ResourceBarrier(1, &bar);
    } else {
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_cmd->ResourceBarrier(1, &bar);
    }

    HR(m_cmd->Close(), "CmdClose");
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
    m_fenceValues[m_frameIndex] = m_nextFence;
    HR(m_queue->Signal(m_fence.Get(), m_nextFence++), "Signal");
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (doShot) {
        WaitForGpu();            // the copy is done once the queue is idle
        WriteSnapshotBmp();
        m_shotPending = false;
        m_shotDone = true;
        printf("snapshot written: %s\n", m_shotPath.c_str());
    }
}

// 32-bit top-down BMP. Backbuffer memory is R,G,B,A (R8G8B8A8_UNORM) but a
// BI_RGB BMP reads B,G,R,A — swap R/B while writing.
void OilRenderer::WriteSnapshotBmp() {
    void* data = nullptr;
    D3D12_RANGE readRange = { 0, (SIZE_T)m_readbackPitch * m_height };
    if (FAILED(m_readback->Map(0, &readRange, &data))) {
        printf("snapshot: readback map failed\n");
        return;
    }
    FILE* f = nullptr;
    fopen_s(&f, m_shotPath.c_str(), "wb");
    if (!f) {
        printf("snapshot: cannot open %s\n", m_shotPath.c_str());
        m_readback->Unmap(0, nullptr);
        return;
    }
    const UINT rowBytes = (UINT)m_width * 4;
    const UINT imgBytes = rowBytes * m_height;
    BITMAPFILEHEADER fh = {};
    fh.bfType = 0x4D42;
    fh.bfSize = sizeof(fh) + sizeof(BITMAPINFOHEADER) + imgBytes;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih);
    ih.biWidth = m_width;
    ih.biHeight = -(LONG)m_height;   // top-down
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    BYTE* row = new BYTE[rowBytes];
    const BYTE* src = (const BYTE*)data;
    for (int y = 0; y < m_height; y++) {
        const BYTE* s = src + (SIZE_T)y * m_readbackPitch;
        for (UINT x = 0; x < (UINT)m_width; x++) {
            row[x * 4 + 0] = s[x * 4 + 2];   // B
            row[x * 4 + 1] = s[x * 4 + 1];   // G
            row[x * 4 + 2] = s[x * 4 + 0];   // R
            row[x * 4 + 3] = s[x * 4 + 3];   // A
        }
        fwrite(row, rowBytes, 1, f);
    }
    delete[] row;
    fclose(f);
    m_readback->Unmap(0, nullptr);
}
