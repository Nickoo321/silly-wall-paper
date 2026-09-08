// AcidWallpaper — "liquid acid" macro oil-and-ink renderer (proof of concept).
// D3D12 device + swapchain + ONE fullscreen-triangle graphics pipeline.
// The whole look lives in the pixel shader: a 64-blob metaball field rendered
// posterized — flat banded interiors, a thin bright rim at the field == 1
// boundary (a Gaussian on the signed-distance estimate (field-1)/|grad|), a
// dark meniscus band just inside, cellular speckle near interfaces, and film
// grain. NO bloom/glow anywhere: rim brightness is direct color.
// The CPU sim (StepBlobs) drifts blobs through a pseudo-curl field with
// radius-proportional buoyancy; palette 0 seeds uniform droplets, palette 1
// seeds a dense diagonal band so merges form a vein web with holes.

#include "acid.h"
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
        MessageBoxA(nullptr, what, "Acid Wallpaper - fatal error", MB_ICONERROR);
        ExitProcess(1);
    }
}

// ---------------------------------------------------------------------------
// Per-frame constant buffer — keep in sync with cbuffer CB in kShaderSrc.
// ---------------------------------------------------------------------------
struct AcidCBReal {
    float blobs[64][4];      // xy = pos uv, z = radius (y units), w = colorIndex
    float bg[4];
    float bgAccent[4];
    float blobCol[3][4];
    float rimCol[4];
    float speckCol[4];
    float time, aspect, rimWidth, rimStrength;
    float speckScale, grainAmt, marbleScale, pad;
};
static_assert(sizeof(AcidCBReal) == 64 * 16 + 5 * 16 + 3 * 16 + 16, "CB layout");

// ---------------------------------------------------------------------------
// Shader: runtime-compiled, same pattern as the oil PoC.
// ---------------------------------------------------------------------------
static const char* kShaderSrc = R"hlsl(
cbuffer CB : register(b0) {
    float4 blobs[64];      // xy pos uv (y down), z radius, w colorIndex
    float4 bg;
    float4 bgAccent;
    float4 blobCol[3];
    float4 rimCol;
    float4 speckCol;
    float  time;
    float  aspect;
    float  rimWidth;       // bright rim half-width, uv units
    float  rimStrength;
    float  speckScale;     // speckle cells per uv unit
    float  grainAmt;
    float  marbleScale;    // fbm frequency inside blobs
    float  pad;
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

// softened posterize: n flat bands, soft transitions only at band tops
float qstep(float x, float n) {
    float f = floor(x * n);
    float fr = frac(x * n);
    return (f + smoothstep(0.65, 1.0, fr)) / n;
}

float4 PSMain(VSOut inp) : SV_Target {
    float2 p = float2(inp.uv.x * aspect, inp.uv.y);

    // Metaball field + analytic gradient + influence-weighted color sum.
    float  field  = 0.0;
    float2 grad   = float2(0.0, 0.0);
    float3 colSum = float3(0.0, 0.0, 0.0);
    [loop]
    for (int b = 0; b < 64; b++) {
        float4 bl = blobs[b];
        float2 q  = p - float2(bl.x * aspect, bl.y);
        float  d2 = dot(q, q) + 1e-6;
        float  r2 = bl.z * bl.z;
        float  wr = r2 / d2 - 0.14;             // compact tail cutoff
        float  w  = clamp(wr, 0.0, 3.5);
        field  += w;
        // only accumulate the gradient where the weight is NOT clamped —
        // the clamp flattens the field but the analytic derivative keeps
        // exploding toward the center, which would drag sdf back to ~0
        // there and paint the rim color into blob centers
        if (wr > 0.0 && wr < 3.5)
            grad += (-2.0 * r2 / (d2 * d2)) * q;
        colSum += blobCol[(uint)(bl.w + 0.5)].rgb * w;
    }

    // signed-distance estimate: sdf > 0 inside the blob, 0 at the boundary.
    // NOTE: diverges where |grad| -> 0 (blob centers) — use it only for
    // narrow edge bands; coverage must come from the field itself.
    float gl  = length(grad) + 1e-5;
    float sdf = clamp((field - 1.0) / gl, -0.1, 0.1);
    float aaF = fwidth(field) * 1.5 + 1e-4;
    float cov = smoothstep(1.0 - aaF, 1.0 + aaF, field);

    // Medium: mottled two-tone backdrop, posterized into 3 soft bands.
    float m1 = fbm(p * 1.4 + float2(time * 0.006, -time * 0.004));
    float m2 = fbm(p * 5.3 - float2(time * 0.004, time * 0.006));
    float med = qstep(saturate((m1 * 0.72 + m2 * 0.38) * 1.15 - 0.10), 3.0);
    float3 bgc = lerp(bg.rgb, bgAccent.rgb, med);

    // Blob interior: influence-weighted base color, posterized brightness
    // (2 flat bands) so interiors read FLAT, not gradient-shaded.
    float3 bcol = colSum / max(field, 1e-4);
    float bm = fbm(p * marbleScale + float2(time * 0.015, -time * 0.010));
    bcol *= 0.93 + 0.14 * qstep(bm, 2.0);

    // Meniscus: darker band just inside the edge.
    float men = smoothstep(rimWidth * 0.8, rimWidth * 2.5, sdf)
              * (1.0 - smoothstep(rimWidth * 2.5, rimWidth * 7.0, sdf));
    bcol *= 1.0 - 0.16 * men;

    float3 col = lerp(bgc, bcol, cov);

    // Bright rim exactly at the boundary: Gaussian on sdf, straddles the
    // edge. Direct color only — no bloom.
    float rim = exp(-pow(sdf / rimWidth, 2.0));
    col = lerp(col, rimCol.rgb, saturate(rim * rimStrength));

    // Speckle: sparse cellular dots, weighted to a band near interfaces
    // (both sides of the boundary), denser just outside.
    float sMaskOut = smoothstep(-0.022, 0.0, sdf) * (1.0 - cov);   // fades outward
    float sMaskIn  = (1.0 - smoothstep(rimWidth * 2.0, rimWidth * 6.0, sdf)) * cov;
    float sMask = saturate(sMaskOut * 1.2 + sMaskIn * 0.30);
    float2 cell = floor(p * speckScale);
    float2 cf   = frac(p * speckScale);
    float  rnd  = hash21(cell);
    float2 jit  = float2(hash21(cell + 7.13), hash21(cell + 13.71));
    float  dd   = length(cf - (0.25 + 0.5 * jit));
    float  dot1 = step(dd, 0.08 + 0.26 * rnd) * step(0.88, rnd);
    col = lerp(col, speckCol.rgb, dot1 * sMask * 0.85);

    // film grain overall
    col += (hash21(inp.pos.xy + frac(time) * 913.7) - 0.5) * grainAmt;

    return float4(saturate(col), 1.0);
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
struct AcidPalette {
    float bg[3], bgAccent[3], col[3][3], rim[3], speck[3];
    float rimWidth, rimStrength, speckScale, grain, marbleScale;
};
static const AcidPalette kPalettes[2] = {
    // Coral: deep teal <-> dark amber mottled medium; flat bright coral
    // droplets; thin bright cyan rim; near-black speckle.
    { { 0.030f, 0.300f, 0.330f }, { 0.420f, 0.160f, 0.010f },
      { { 0.930f, 0.290f, 0.160f },   // coral
        { 0.920f, 0.240f, 0.110f },   // orange-red (close to coral: flat family)
        { 0.210f, 0.095f, 0.020f } }, // dark amber droplet
      { 0.150f, 0.920f, 0.900f },     // rim: bright cyan
      { 0.050f, 0.020f, 0.008f },     // speckle: near-black brown
      0.006f, 0.95f, 300.0f, 0.030f, 9.0f },
    // Royal: deep purple medium; red-orange vein web with holes; hot orange
    // rim reads as glowing veins (direct color, no bloom); dark purple dots.
    { { 0.060f, 0.008f, 0.150f }, { 0.200f, 0.025f, 0.360f },
      { { 0.950f, 0.120f, 0.030f },   // red-orange
        { 0.620f, 0.045f, 0.012f },   // deep red
        { 0.070f, 0.008f, 0.150f } }, // dark hole-blob
      { 1.000f, 0.460f, 0.070f },     // rim: hot orange
      { 0.040f, 0.004f, 0.090f },     // speckle: dark purple
      0.009f, 1.00f, 340.0f, 0.026f, 7.0f },
};

// ---------------------------------------------------------------------------
// Device / swapchain / pipeline
// ---------------------------------------------------------------------------
void AcidRenderer::Init(HWND hwnd, int width, int height, const AcidConfig& cfg) {
    m_cfg = cfg;
    m_width = width;
    m_height = height;
    CreateDevice();
    CreateSwapChainResources(hwnd, width, height);
    CreatePipeline();
    SeedBlobs();
    printf("Acid renderer ready: %dx%d, %d blobs, palette %d\n",
           width, height, kBlobCount, m_cfg.palette);
}

void AcidRenderer::CreateDevice() {
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
        rd.Width = (sizeof(AcidCBReal) + 255) & ~255u;
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

void AcidRenderer::CreateSwapChainResources(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // SDR sRGB
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

void AcidRenderer::CreatePipeline() {
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

void AcidRenderer::WaitForGpu() {
    if (!m_queue || !m_fence) return;
    HR(m_queue->Signal(m_fence.Get(), m_nextFence), "Signal");
    if (m_fence->GetCompletedValue() < m_nextFence) {
        HR(m_fence->SetEventOnCompletion(m_nextFence, m_fenceEvent), "SetEvent");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_nextFence++;
}

void AcidRenderer::Reattach(HWND hwnd) {
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

void AcidRenderer::Shutdown() {
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
// CPU blob sim — slow viscous drift. Parents are seeded first; satellites are
// tiny droplets sprinkled around a random parent (they then drift on their
// own, which fakes split/merge well enough for a PoC).
// Palette 0: uniform spread, distinct droplets.
// Palette 1: dense diagonal band so merges form a vein web with holes.
// ---------------------------------------------------------------------------
void AcidRenderer::SeedBlobs() {
    std::mt19937 rng((unsigned)GetTickCount() ^ 0xAC1D);
    auto U = [&rng](float lo, float hi) {
        return lo + (hi - lo) * (float)rng() / (float)rng.max();
    };
    const float TWO_PI = 6.2831853f;
    const bool veins = (m_cfg.palette & 1) == 1;

    auto place = [&](BlobCPU& b, float x, float y) {
        b.x = x; b.y = y;
        b.vx = b.vy = 0.0f;
        b.phase = U(0.0f, TWO_PI);
        b.breathRate = U(0.10f, 0.35f);
        b.s1 = U(0.0f, TWO_PI);
        b.s2 = U(0.0f, TWO_PI);
        b.sinker = (U(0.0f, 1.0f) < 0.08f);
    };
    // palette 1 cluster band: a loose sine ribbon across the screen
    auto bandY = [&](float x) {
        return 0.5f + 0.16f * sinf(x * 5.0f + 0.8f) + U(-0.10f, 0.10f);
    };

    int parents;
    if (!veins) {
        // few big flat coral droplets + a handful of small dark droplets in
        // the medium; satellites match their parent's color so merges are
        // invisible (no "donut eyes" inside big blobs)
        parents = 34;
        for (int i = 0; i < parents; i++) {
            BlobCPU& b = m_blobs[i];
            if (i < 8)       { b.colorIdx = 0; b.baseR = U(0.090f, 0.150f); }
            else if (i < 16) { b.colorIdx = (i % 3 == 0) ? 1.0f : 0.0f; b.baseR = U(0.045f, 0.090f); }
            else if (i < 22) { b.colorIdx = 0; b.baseR = U(0.018f, 0.045f); }
            else if (i < 28) { b.colorIdx = 2; b.baseR = U(0.030f, 0.055f); }
            else             { b.colorIdx = 1; b.baseR = U(0.015f, 0.030f); }
            place(b, U(0.03f, 0.97f), U(0.05f, 0.97f));
        }
    } else {
        // vein web: smaller web blobs (thinner connecting strands) plus
        // several BIG dark hole-blobs inside the band that read as round
        // holes eaten out of the red phase
        parents = 40;
        for (int i = 0; i < parents; i++) {
            BlobCPU& b = m_blobs[i];
            float x = U(0.02f, 0.98f);
            if (i < 12)      { b.colorIdx = (i % 3 == 0) ? 1.0f : 0.0f; b.baseR = U(0.060f, 0.100f); }
            else if (i < 28) { b.colorIdx = 0; b.baseR = U(0.035f, 0.065f); }
            else if (i < 32) { b.colorIdx = 2; b.baseR = U(0.015f, 0.035f); }
            else             { b.colorIdx = 2; b.baseR = U(0.050f, 0.090f); }  // holes
            place(b, x, bandY(x));
        }
    }
    // satellites: tiny droplets hugging a random parent. Coral palette:
    // satellites take the parent's color and hug only the coral parents
    // (indices < 24) so they read as mini replicas, not dirt inside blobs.
    const int satParentMax = veins ? parents : 22;
    for (int i = parents; i < kBlobCount; i++) {
        BlobCPU& b = m_blobs[i];
        const BlobCPU& par = m_blobs[rng() % satParentMax];
        float ang = U(0.0f, TWO_PI);
        float dist = par.baseR * U(1.10f, veins ? 2.1f : 1.6f);
        b.colorIdx = veins ? 2.0f : par.colorIdx;
        b.baseR = U(0.005f, veins ? 0.028f : 0.020f);
        place(b, par.x + cosf(ang) * dist, par.y + sinf(ang) * dist);
    }
}

void AcidRenderer::StepBlobs(float dt) {
    const float t = m_time;
    for (int i = 0; i < kBlobCount; i++) {
        BlobCPU& b = m_blobs[i];

        // Pseudo-curl drift (divergence-free swirl, same trick as the oil PoC)
        float a1 = 3.1f * b.x + 0.23f * t + b.s1;
        float a2 = 2.7f * b.y + 0.19f * t;
        float a3 = 5.3f * b.x - 0.11f * t;
        float a4 = 4.1f * b.y + 0.29f * t + b.s2;
        float dpsidx = 3.1f * cosf(a1) * cosf(a2) + 0.7f * 5.3f * cosf(a3) * cosf(a4);
        float dpsidy = -2.7f * sinf(a1) * sinf(a2) - 0.7f * 4.1f * sinf(a3) * sinf(a4);
        const float driftK = 0.0022f;
        float tx = dpsidy * driftK;
        float ty = -dpsidx * driftK;

        // buoyancy: big blobs rise slowly (uv y down, so up = negative)
        const float buoyK = 0.007f;
        ty += (b.sinker ? 1.0f : -1.0f) * buoyK * (b.baseR / 0.12f);

        float k = 1.0f - expf(-2.5f * dt);
        b.vx += (tx - b.vx) * k;
        b.vy += (ty - b.vy) * k;
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        const float m = 0.16f;
        if (b.x < -m)       b.x += 1.0f + 2.0f * m;
        if (b.x > 1.0f + m) b.x -= 1.0f + 2.0f * m;
        if (b.y < -m)       b.y += 1.0f + 2.0f * m;
        if (b.y > 1.0f + m) b.y -= 1.0f + 2.0f * m;

        b.phase += b.breathRate * dt;
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void AcidRenderer::Frame(float dt) {
    if (m_presentBroken || !m_device) return;
    m_time += dt;
    StepBlobs(dt);

    // fill + upload the per-frame constants
    AcidCBReal cb = {};
    const AcidPalette& pal = kPalettes[m_cfg.palette & 1];
    for (int i = 0; i < kBlobCount; i++) {
        const BlobCPU& b = m_blobs[i];
        float r = b.baseR * (1.0f + 0.12f * sinf(b.phase));   // breathing ±12%
        cb.blobs[i][0] = b.x;
        cb.blobs[i][1] = b.y;
        cb.blobs[i][2] = r;
        cb.blobs[i][3] = b.colorIdx;
    }
    for (int c = 0; c < 3; c++) {
        cb.bg[c] = pal.bg[c];
        cb.bgAccent[c] = pal.bgAccent[c];
        cb.rimCol[c] = pal.rim[c];
        cb.speckCol[c] = pal.speck[c];
        for (int j = 0; j < 3; j++) cb.blobCol[c][j] = pal.col[c][j];
    }
    cb.time = m_time;
    cb.aspect = (float)m_width / (float)m_height;
    cb.rimWidth = pal.rimWidth;
    cb.rimStrength = pal.rimStrength;
    cb.speckScale = pal.speckScale;
    cb.grainAmt = pal.grain;
    cb.marbleScale = pal.marbleScale;
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
void AcidRenderer::WriteSnapshotBmp() {
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
