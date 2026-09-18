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
extern void WpLog(const char* fmt, ...);          // main.cpp: rolling log file
extern void ForegroundDesc(char* out, size_t cap);
#define HR(expr) do { HRESULT _hr = (expr); if (FAILED(_hr)) Fail(#expr, _hr); } while (0)

// Must match cbuffer CB in shaders.h (22 DWORDs).
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
    float baroclinic;
    float gravity;       // dye-weighted gravity (0 = off; the branch is skipped)
    float gravityPow;
    float gravityBlur;
    float pad0_;
};
static_assert(sizeof(SimCB) == 24 * 4, "SimCB must match the HLSL cbuffer layout (24 DWORDs)");

static float RandF() { return (float)rand() / (float)RAND_MAX; }
static float HalfToFloat(uint16_t h);   // defined below

struct RGB { float r, g, b; };
// (CSS filter matrices now live in the display shader; see kDisplaySrc.)

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

static ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target,
                                const D3D_SHADER_MACRO* defines = nullptr) {
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(src, strlen(src), entry, defines, nullptr, entry, target, 0, 0, &blob, &err))) {
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

// --shot determinism: when non-zero, every rand()-driven behavior (startup
// splat burst, wanderer placement, darts, idle bursts, mood jitter) replays
// from this seed instead of the wall clock.
static unsigned g_randSeed = 0;
void FluidRenderer::SetRandomSeed(unsigned seed) { g_randSeed = seed; }

void FluidRenderer::Init(HWND hwnd, int width, int height, const FluidConfig& cfg) {
    m_headless = false;
    InitCommon(hwnd, width, height, cfg);
}

// Headless: same setup, no window and no swap chain. CreateDevice branches on
// m_headless and builds an offscreen FP16 render target instead.
void FluidRenderer::InitOffscreen(int width, int height, const FluidConfig& cfg) {
    m_headless = true;
    InitCommon(nullptr, width, height, cfg);
}

void FluidRenderer::InitCommon(HWND hwnd, int width, int height, const FluidConfig& cfg) {
    m_cfg = cfg;
    m_width = width;
    m_height = height;
    if (g_randSeed) {
        srand(g_randSeed);
        m_globalHue = RandF();
    } else {
        m_globalHue = RandF();
        srand(GetTickCount());
    }

    CreateDevice(hwnd, width, height);
    if (FAILED(m_initHr)) return;   // swap chain refused; TryInit() cleans up
    if (!m_cfg.gradientMode) CreateSimResources();
    printf("Renderer ready (%s), sim %dx%d, dye %dx%d\n",
           m_cfg.gradientMode ? "gradient mode" : "fluid",
           m_simW, m_simH, m_dyeW, m_dyeH);
}

// The one call that is allowed to fail without taking the process with it.
// DXGI refuses a flip-model swap chain with E_ACCESSDENIED while another app
// holds the output exclusively -- a fullscreen game, or DWM composition still
// being torn down -- and the resume out of a fullscreen pause fires at exactly
// that moment. Every attempt is logged with its HRESULT and with whatever owns
// the foreground, because that is the diagnosis.
bool FluidRenderer::CreateSwapChainSoft(HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& sd,
                                        Microsoft::WRL::ComPtr<IDXGISwapChain1>& out) {
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd,
                                                   nullptr, nullptr, &out);
    if (SUCCEEDED(hr)) return true;
    char fg[256];
    ForegroundDesc(fg, sizeof(fg));
    WpLog("CreateSwapChainForHwnd hwnd=%p %dx%d FAILED hr=0x%08lX  %s",
          (void*)hwnd, (int)sd.Width, (int)sd.Height, (unsigned long)hr, fg);
    printf("CreateSwapChainForHwnd failed (hr=0x%08lX) - %s\n", (unsigned long)hr, fg);
    if (m_softInit) { m_initHr = hr; return false; }
    Fail("m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1)", hr);
    return false;
}

// Init(), but a refused swap chain unwinds to a clean torn-down renderer and
// returns false instead of putting a fatal dialog over a black desktop.
bool FluidRenderer::TryInit(HWND hwnd, int width, int height, const FluidConfig& cfg) {
    m_softInit = true;
    m_initHr = S_OK;
    InitCommon(hwnd, width, height, cfg);
    m_softInit = false;
    if (FAILED(m_initHr)) {
        Shutdown();       // releases whatever CreateDevice managed to build
        return false;
    }
    return true;
}

bool FluidRenderer::TryReattach(HWND hwnd) {
    m_softInit = true;
    m_initHr = S_OK;
    Reattach(hwnd);
    m_softInit = false;
    return SUCCEEDED(m_initHr);
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

    if (!m_headless) {
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
        if (!CreateSwapChainSoft(hwnd, sd, sc1)) return;
        HR(sc1.As(&m_swapChain));
        m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        // Colour space, NOT fatal. An output that is being handed back by a
        // fullscreen app can refuse these for a moment, and a wallpaper that
        // is merely in the wrong colour space is still a wallpaper -- dying
        // over it is not a trade anyone would make. ReassertColorSpace() runs
        // on every resume and every reattach anyway, so a refusal here is
        // corrected within a second.
        const DXGI_COLOR_SPACE_TYPE scRGB = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        UINT support = 0;
        HRESULT cshr = m_swapChain->CheckColorSpaceSupport(scRGB, &support);
        if (FAILED(cshr)) {
            WpLog("CheckColorSpaceSupport failed hr=0x%08lX (continuing in the default space)",
                  (unsigned long)cshr);
            printf("WARNING: CheckColorSpaceSupport failed (hr=0x%08lX)\n", (unsigned long)cshr);
        } else if (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
            HRESULT sshr = m_swapChain->SetColorSpace1(scRGB);
            if (FAILED(sshr))
                WpLog("SetColorSpace1 failed hr=0x%08lX", (unsigned long)sshr);
            else
                printf("Swap chain: R16G16B16A16_FLOAT, scRGB color space set (1.0 = 80 nits)\n");
        } else {
            printf("WARNING: scRGB color space not supported on this output!\n");
        }
    } else {
        printf("Headless: no swap chain, offscreen R16G16B16A16_FLOAT target "
               "%dx%d (linear scRGB, 1.0 = 80 nits)\n", width, height);
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kFrames * 2 + 1;   // backbuffers + analyzer + mirror buffers
    HR(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrames; i++) {
        if (!m_headless) {   // headless: no back buffers; RTV slot 0 = shot target
            HR(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));
            m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv);
        }
        rtv.ptr += m_rtvStride;
        HR(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&m_allocators[i])));
    }
    HR(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmd)));
    HR(m_cmd->Close());

    HR(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_frameIndex = m_headless ? 0 : m_swapChain->GetCurrentBackBufferIndex();

    // Shader-visible heap: 2 slots (SRV, UAV) per texture, 16 textures max.
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 32;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)));
    m_srvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // --- root signatures ---
    // Compute: b0 root constants, t0, t1, t2 (baroclinic dye), u0..u2.
    {
        D3D12_DESCRIPTOR_RANGE rSrv0 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rSrv1 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rSrv2 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rUav0 = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rUav1 = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rUav2 = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, 0 };

        D3D12_ROOT_PARAMETER params[8] = {};
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
        table(params[6], &rSrv2);
        // Param 7 is the Liquid Acid blob buffer (t3) as a ROOT descriptor,
        // read only by the oil-mask pass (oil_drag / oil_dye_block). Every
        // other compute shader compiled from kComputeSrc ignores it, so it
        // costs the fluid sim two DWORDs of root signature and nothing else.
        params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[7].Descriptor = { 3, 0 };            // t3

        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 8;
        rsd.pParameters = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &samp;

        ComPtr<ID3DBlob> sig, err;
        HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&m_computeRS)));
    }
    // Graphics: b0 constants, t0 table, s0 static sampler.
    // Params 2/3 are the Liquid Acid look's blob buffer (t1) and parameter
    // block (b1) as ROOT descriptors — no descriptor-heap slots needed. The
    // fluid display shader references neither, so they cost it nothing.
    // Param 4 is the shared ink-in-water parameter block (b2), read by the INK
    // PSO and by LIQUID_ACID when ink_mode=water. Same story: the fluid
    // display shader does not reference it.
    // Param 6 is the [mirror] fold block (b3) — 12 ROOT CONSTANTS, because
    // unlike the blocks above this one IS read by all three display PSOs
    // (the fold is in the shared part of the shader), so it must be bound on
    // every display draw and an upload-buffer ring would buy nothing.
    {
        D3D12_DESCRIPTOR_RANGE rSrv0 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        D3D12_DESCRIPTOR_RANGE rSrv3 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 0 };
        D3D12_ROOT_PARAMETER params[9] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants = { 0, 0, 32 };
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &rSrv0 };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[2].Descriptor = { 1, 0 };            // b1
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor = { 1, 0 };            // t1
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[4].Descriptor = { 2, 0 };            // b2 (InkCB)
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[5].DescriptorTable = { 1, &rSrv3 };  // t3 (low-res velocity)
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[6].Constants = { 3, 0, 12 };         // b3 (MirrorCB)
        params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // Params 7/8 are the droplet particle sim's two structured buffers —
        // the particles (t2) and the uniform-grid cell table (t4) — as ROOT
        // descriptors, same deal as the blob buffer above: no heap slots, and
        // nothing at all for the shaders that never reference them.
        params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[7].Descriptor = { 2, 0 };            // t2 (AcidDrops)
        params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[8].Descriptor = { 4, 0 };            // t4 (DropCells)
        params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 9;
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
    {   // finite-support variant for ink drops (see DROP_COMPACT in shaders.h)
        const D3D_SHADER_MACRO defs[] = { { "DROP_COMPACT", "1" }, { nullptr, nullptr } };
        ComPtr<ID3DBlob> cs = Compile(kComputeSrc, "CSSplatDye", "cs_5_0", defs);
        D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {};
        cd.pRootSignature = m_computeRS.Get();
        cd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        HR(m_device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&m_psoSplatDyeCompact)));
    }
    makeCS("CSDownsample", m_psoDownsample);
    makeCS("CSDiffuseDye", m_psoDiffuseDye);

    // --- graphics PSOs (display + gradient) ---
    auto makeGfx = [&](const char* src, ComPtr<ID3D12PipelineState>& pso,
                       const D3D_SHADER_MACRO* defines = nullptr) {
        MakeGraphicsPso(src, pso, defines);
    };
    makeGfx(kDisplaySrc, m_psoDisplay);
    makeGfx(kGradientSrc, m_psoGradient);
    // Liquid Acid: the SAME display source compiled with LIQUID_ACID defined.
    // Built only when the look is on, so the normal path pays no compile cost
    // and, more importantly, its own shader has none of this code in it.
    if (m_cfg.acid.enabled) {
        const D3D_SHADER_MACRO defs[] = { { "LIQUID_ACID", "1" }, { nullptr, nullptr } };
        makeGfx(kDisplaySrc, m_psoLiquidAcid, defs);
    }
    // "Ink in water": the same display source with INK defined. Same rule —
    // built only when the look is on, so style=fluid keeps its exact shader.
    if (m_cfg.ink.enabled) {
        const D3D_SHADER_MACRO defs[] = { { "INK", "1" }, { nullptr, nullptr } };
        makeGfx(kDisplaySrc, m_psoInk, defs);
    }
    CreateAcidBuffers();

    if (m_headless) CreateOffscreenTarget();
}

// One display/gradient graphics PSO. Identical to the lambda CreateDevice used
// to inline, so a PSO compiled later by EnsureLookResources() is built exactly
// the same way as one compiled at device creation.
void FluidRenderer::MakeGraphicsPso(const char* src,
                                    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso,
                                    const D3D_SHADER_MACRO* defines) {
    ComPtr<ID3DBlob> vs = Compile(src, "VSMain", "vs_5_0", defines);
    ComPtr<ID3DBlob> ps = Compile(src, "PSMain", "ps_5_0", defines);
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
}

// Runtime look switching. The upload rings (acid blobs, acid params, InkCB)
// and the 64x36 velocity readback are already created unconditionally by
// CreateAcidBuffers() / CreateSimResources(), and the acid blob population is
// (re)seeded lazily by Frame(); the ONLY thing missing when a look is turned
// on after startup is its display PSO. Compiling one takes ~0.3 s (D3DCompile
// of kDisplaySrc), so do it off the GPU timeline: idle-wait first, exactly as
// SetResolutions does, then compile. Both branches are no-ops once built, so
// this is safe to call on every preset apply and every checkbox click.
void FluidRenderer::EnsureLookResources() {
    if (!m_device) return;
    const bool needAcid = m_cfg.acid.enabled && !m_psoLiquidAcid;
    const bool needInk  = m_cfg.ink.enabled  && !m_psoInk;
    if (!needAcid && !needInk) return;
    WaitForGpuIdle();
    if (needAcid) {
        const D3D_SHADER_MACRO defs[] = { { "LIQUID_ACID", "1" }, { nullptr, nullptr } };
        MakeGraphicsPso(kDisplaySrc, m_psoLiquidAcid, defs);
        m_acidSeeded = false;          // Frame() reseeds under the current keys
        printf("look: liquid_acid PSO compiled on demand\n");
    }
    if (needInk) {
        const D3D_SHADER_MACRO defs[] = { { "INK", "1" }, { nullptr, nullptr } };
        MakeGraphicsPso(kDisplaySrc, m_psoInk, defs);
        printf("look: ink PSO compiled on demand\n");
    }
}

// Headless render target: one FP16 texture the size of the requested shot,
// plus a readback buffer. RTV slot 0 (the first back-buffer slot, unused when
// there is no swap chain) — the analyzer keeps slot kFrames, the mirror
// kFrames+1.., so nothing collides.
void FluidRenderer::CreateOffscreenTarget() {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)m_width;
    rd.Height = (UINT)m_height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, m_shotState,
                                         nullptr, IID_PPV_ARGS(&m_shotTex)));

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_shotTex.Get(), nullptr, rtv);

    m_shotPitch = ((UINT)m_width * 8 + 255) & ~255u;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)m_shotPitch * m_height;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES rb = {};
    rb.Type = D3D12_HEAP_TYPE_READBACK;
    HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&m_shotReadback)));
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
    // Liquid Acid: 64x36 velocity downsample for CPU blob advection. Same
    // async-readback pattern as the coverage governor; one frame of latency.
    m_velLow = CreateTex(kVelW, kVelH, DXGI_FORMAT_R16G16B16A16_FLOAT, slot++);
    // Liquid Acid oil_drag / oil_dye_block: sim-res oil coverage mask. 72 KB
    // and one heap slot; nothing ever writes or reads it unless one of those
    // two keys is non-zero (see StepOilDrag), so the fluid look is untouched.
    m_oilMask = CreateTex(m_simW, m_simH, DXGI_FORMAT_R16_FLOAT, slot++);
    m_velPitch = (kVelW * 8 + 255) & ~255u;
    m_velCpu.assign((size_t)kVelW * kVelH * 2, 0.0f);
    m_velPending = false;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)m_velPitch * kVelH;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&m_velReadback)));
    }

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
    if (m_headless) {
        // no swap chain: just fence the frame and rotate the allocator ring
        m_fenceValues[m_frameIndex] = m_nextFence;
        HR(m_queue->Signal(m_fence.Get(), m_nextFence++));
        m_frameIndex = (m_frameIndex + 1) % kFrames;
        return;
    }
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
    cb.baroclinic = m_cfg.baroclinic;
    cb.gravity = m_cfg.gravity;
    cb.gravityPow = m_cfg.gravityPow;
    cb.gravityBlur = m_cfg.gravityBlur;
    cb.aspect = (float)m_width / (float)m_height;

    // Frame-rate independence: the reference applied its multiplicative decays
    // once per step at ~120 steps/s (60 fps, two substeps). Raise each factor
    // to dt/(1/120) so a 144 fps loop doesn't damp harder than a 60 fps one.
    const float stepsRef = dt * 120.0f;

    auto bind = [&](ID3D12PipelineState* pso, Tex* s0, Tex* s1, Tex* dst, UINT dstParam,
                    Tex* s2 = nullptr) {
        if (s0) Transition(*s0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (s1) Transition(*s1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (s2) Transition(*s2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
        m_cmd->SetComputeRootDescriptorTable(6, s2 ? s2->srv : dst->srv);
        m_cmd->Dispatch(Groups(dst->w), Groups(dst->h), 1);
    };

    m_cmd->SetComputeRootSignature(m_computeRS.Get());

    // 1. curl
    bind(m_psoCurl.Get(), m_velocity.read, nullptr, &m_curl, 5);
    // 2. vorticity confinement (+ baroclinic dye-front torque)
    bind(m_psoVorticity.Get(), m_velocity.read, &m_curl, m_velocity.write, 3, m_dye.read);
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
    // 9. dye diffusion (optional): the D∇²c smoke-spread term
    if (m_cfg.dyeDiffusion > 0.0001f) {
        cb.value = fminf(m_cfg.dyeDiffusion * stepsRef, 0.9f);
        bind(m_psoDiffuseDye.Get(), m_dye.read, nullptr, m_dye.write, 4);
        m_dye.Swap();
    }
}

// Shared body of the three splat entry points. `which` selects the passes:
// 1 = velocity only, 2 = dye only, 3 = both (velocity first, exactly the order
// the original Splat() used - the fluid look's seeded sequence depends on it).
void FluidRenderer::SplatImpl(int which, float x, float y, float dx, float dy,
                              float r, float g, float b, float radiusPct, float cap) {
    SimCB cb = {};
    cb.aspect = (float)m_width / (float)m_height;
    cb.pointX = x / m_width;
    cb.pointY = y / m_height;
    cb.radius = (radiusPct > 0.0f ? radiusPct : m_cfg.splatRadius) / 100.0f;

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

    if (which & 1) {   // velocity: add impulse, never capped
        cb.colorR = dx * m_cfg.flowSpeed; cb.colorG = dy * m_cfg.flowSpeed; cb.colorB = 0.0f;
        cb.cap = 1000000.0f;
        run(m_psoSplatVel.Get(), m_velocity.read, m_velocity.write);
        m_velocity.Swap();
    }
    if (which & 2) {   // dye: proportional brightness cap
        cb.colorR = r; cb.colorG = g; cb.colorB = b;
        cb.cap = (cap > 0.0f ? cap : m_cfg.maxBrightness);
        ID3D12PipelineState* pso = ((which & 4) && m_psoSplatDyeCompact)
                                 ? m_psoSplatDyeCompact.Get() : m_psoSplatDye.Get();
        run(pso, m_dye.read, m_dye.write);
        m_dye.Swap();
    }
}

void FluidRenderer::Splat(float x, float y, float dx, float dy, float r, float g, float b,
                          float radiusPct, float cap) {
    SplatImpl(3, x, y, dx, dy, r, g, b, radiusPct, cap);
}
void FluidRenderer::SplatVelocity(float x, float y, float dx, float dy, float radiusPct) {
    SplatImpl(1, x, y, dx, dy, 0.0f, 0.0f, 0.0f, radiusPct, -1.0f);
}
void FluidRenderer::SplatDye(float x, float y, float r, float g, float b,
                             float radiusPct, float cap, bool compact) {
    SplatImpl(compact ? 6 : 2, x, y, 0.0f, 0.0f, r, g, b, radiusPct, cap);
}

void FluidRenderer::MultipleSplats(int amount) {
    for (int i = 0; i < amount; i++) {
        // generateColor(): hue near the global wheel (or palette); intensity
        // is cfg.idleBrightness (wanderers paint at 0.15 — bursts were 1.5)
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
        Splat(x, y, dx, dy, c.r * m_cfg.idleBrightness,
              c.g * m_cfg.idleBrightness, c.b * m_cfg.idleBrightness);
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
    m_cmd->SetPipelineState(DisplayPso());
    float consts[32];
    BuildDisplayConstants(consts);
    m_cmd->SetGraphicsRoot32BitConstants(0, 32, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    BindAcid();
    BindInk();
    BindMirrorFold();
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd->ResourceBarrier(1, &b);
}

// Same display pass as RenderDisplay(), aimed at the offscreen FP16 target.
// Identical constants, identical shader — only the render target differs, so
// the captured pixels are exactly what the swap chain would have received.
void FluidRenderer::RenderDisplayOffscreen() {
    Transition(*m_dye.read, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);

    m_cmd->SetGraphicsRootSignature(m_graphicsRS.Get());
    m_cmd->SetPipelineState(DisplayPso());
    float consts[32];
    BuildDisplayConstants(consts);
    m_cmd->SetGraphicsRoot32BitConstants(0, 32, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    BindAcid();
    BindInk();
    BindMirrorFold();
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);
}

// Copy the offscreen target back to the CPU as linear scRGB floats. Runs its
// own one-shot command list after a full GPU flush, so it can be called
// between Frame() calls without disturbing the frame ring.
bool FluidRenderer::CaptureOffscreen(std::vector<float>& out) {
    if (!m_headless || !m_shotTex || !m_shotReadback) return false;
    WaitForGpuIdle();

    HR(m_allocators[0]->Reset());
    HR(m_cmd->Reset(m_allocators[0].Get(), nullptr));

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_shotTex.Get();
    b.Transition.StateBefore = m_shotState;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (m_shotState != D3D12_RESOURCE_STATE_COPY_SOURCE) m_cmd->ResourceBarrier(1, &b);
    m_shotState = D3D12_RESOURCE_STATE_COPY_SOURCE;

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

    // the frame ring's allocator 0 was reset out of band — resync so the next
    // BeginFrame() doesn't try to reset an allocator whose work is in flight
    for (UINT i = 0; i < kFrames; i++) m_fenceValues[i] = 0;
    m_frameIndex = 0;
    return true;
}

void FluidRenderer::BuildDisplayConstants(float out[32]) {
    BuildDisplayConstantsEx(out, m_width, m_height, m_sdrScale, m_cfg.hdrPeakNits);
}

void FluidRenderer::BuildDisplayConstantsEx(float out[32], int w, int h,
                                            float sdrScale, float peakNits) {
    // CSS-filter chain parameters. The shader evaluates them primitive by
    // primitive with a clamp after each (Chromium/Skia behaviour); see
    // kDisplaySrc. Canvas filter: saturate -> brightness -> contrast ->
    // hue-rotate (the hue-shift burst). Then WE's right-panel adjust,
    // outermost: saturate -> brightness -> contrast -> hue-rotate.
    float hdrOn = (m_hdrActive && m_cfg.hdrCompensation) ? 1.0f : 0.0f;
    float hue = fmodf(m_hueAngle, 360.0f);
    if (hue > -0.05f && hue < 0.05f) hue = 0.0f;

    // HDR highlight expansion: gain that carries hot dye from SDR white up to
    // the peak-nits target (resolved by the shell; 0 = parity mode).
    float peakGain = 1.0f;
    if (m_hdrActive && peakNits > 0.0f) {
        float sdrWhiteNits = 80.0f * sdrScale;
        peakGain = fmaxf(1.0f, peakNits / fmaxf(sdrWhiteNits, 1.0f));
    }

    float consts[32] = { 1.0f / w, 1.0f / h,
                         m_cfg.shading ? 1.0f : 0.0f, sdrScale,
                         (float)m_cfg.gamutMode, peakGain, m_cfg.hdrKnee,
                         fmaxf(m_cfg.maxBrightness, m_cfg.hdrKnee + 0.05f),
                         m_cfg.hdrSaturation, m_cfg.hdrBrightness, m_cfg.hdrContrast, hdrOn,
                         hue, m_cfg.postSaturation, m_cfg.postBrightness, m_cfg.postContrast,
                         m_cfg.postHue, 0, 0, 0,
                         0, 0, 0, 0,
                         m_cfg.curveEnabled ? 1.0f : 0.0f,
                         m_cfg.curveCenter, m_cfg.curveWidth, m_cfg.curveHeight,
                         m_cfg.shadowFloor, m_cfg.shadowKnee, 0.0f, 0.0f };
    memcpy(out, consts, sizeof(consts));
}

// ===========================================================================
// [mirror] — screen mirroring / kaleidoscope.
//
// The fold itself is one uv transform at the top of the display pixel shader
// (see MirrorFold in kDisplaySrc); everything here is its plumbing: the 12
// root constants, and a CPU twin of the same fold so a pointer splat lands
// where the user SEES it rather than where the pointer literally is.
//
// Note on names: m_mirrorChain / m_mirrorW below are the unrelated
// SECOND-MONITOR mirror. Everything to do with the fold says "MirrorFold"
// or lives in m_cfg.mirror.
// ===========================================================================

void FluidRenderer::BuildMirrorConstants(float out[12], int w, int h) const {
    const MirrorConfig& mr = m_cfg.mirror;
    const float aspect = (float)(w > 0 ? w : 1) / (float)(h > 0 ? h : 1);
    const float c[12] = {
        (float)mr.mode, (float)mr.segments, aspect, m_time,
        mr.centerX, mr.centerY, fmaxf(mr.rotatePeriod, 0.0f),
        fminf(fmaxf(mr.drift, 0.0f), 1.0f),
        fmaxf(mr.soft, 0.0f), (float)(mr.source & 3), 0.0f, 0.0f,
    };
    memcpy(out, c, sizeof(c));
}

void FluidRenderer::BindMirrorFold(int w, int h) {
    float c[12];
    BuildMirrorConstants(c, w > 0 ? w : m_width, h > 0 ? h : m_height);
    m_cmd->SetGraphicsRoot32BitConstants(6, 12, c, 0);
}

// Must stay in step with MirrorFold()/MirFoldAxis() in kDisplaySrc. `soft` is
// deliberately ignored: it is a display nicety a few pixels wide, and a splat
// does not care. NOT compensated: the kaleidoscope's rotation makes the
// impulse direction ill-defined, so there the flips stay 1.
void FluidRenderer::MirrorMapPointer(float& px, float& py,
                                     float& flipX, float& flipY) const {
    flipX = flipY = 1.0f;
    const MirrorConfig& mr = m_cfg.mirror;
    if (mr.mode <= 0 || m_width <= 0 || m_height <= 0) return;

    float u = px / (float)m_width, v = py / (float)m_height;
    float cx = mr.centerX, cy = mr.centerY;
    if (mr.drift > 0.0005f) {
        auto wander = [](float t) {
            return 0.55f * sinf(t) + 0.30f * sinf(t * 1.913f + 1.7f)
                 + 0.15f * sinf(t * 3.271f + 4.1f);
        };
        const float t = m_time * 0.05f;
        const float d = 0.18f * fminf(mr.drift, 1.0f);
        cx = fminf(fmaxf(cx + d * wander(t), 0.2f), 0.8f);
        cy = fminf(fmaxf(cy + d * wander(t * 0.83f + 11.0f), 0.2f), 0.8f);
    }

    if (mr.mode >= 4) {
        const float aspect = (float)m_width / (float)m_height;
        const float rot = (mr.rotatePeriod > 0.01f)
                        ? (6.2831853f * m_time / mr.rotatePeriod) : 0.0f;
        const float x = (u - cx) * aspect, y = v - cy;
        // same central-disc scaling as MirrorFold()
        const float r = sqrtf(x * x + y * y)
                      * (0.5f / sqrtf(0.25f * aspect * aspect + 0.25f));
        const float seg = 6.2831853f / fmaxf((float)mr.segments, 2.0f);
        const float a = atan2f(y, x) - rot;
        const float m2 = a - seg * 2.0f * floorf(a / (seg * 2.0f));
        const float ang = fabsf(m2 - seg) + rot;
        u = cx + cosf(ang) * r / aspect;
        v = cy + sinf(ang) * r;
    } else {
        auto axis = [](float uu, float c, float sgn, float& flip) {
            const float d = uu - c;
            const float M = fmaxf(fmaxf(c, 1.0f - c), 1e-4f);
            flip = sgn * ((d >= 0.0f) ? 1.0f : -1.0f) * (0.5f / M);
            return 0.5f + sgn * fminf(fabsf(d) / M, 1.0f) * 0.5f;
        };
        const float sgnX = (mr.source & 1) ? 1.0f : -1.0f;
        const float sgnY = (mr.source & 2) ? 1.0f : -1.0f;
        if (mr.mode == 1 || mr.mode == 3) u = axis(u, cx, sgnX, flipX);
        if (mr.mode == 2 || mr.mode == 3) v = axis(v, cy, sgnY, flipY);
    }
    px = u * (float)m_width;
    py = v * (float)m_height;
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
    m_cmd->SetPipelineState(DisplayPso());
    float consts[32];
    BuildDisplayConstants(consts);
    m_cmd->SetGraphicsRoot32BitConstants(0, 32, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    BindAcid();
    BindInk();
    BindMirrorFold(kAnaW, kAnaH);
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
    m_cmd->SetPipelineState(DisplayPso());
    float consts[32];
    BuildDisplayConstantsEx(consts, m_mirrorW, m_mirrorH, m_mirrorSdrScale, m_mirrorPeakNits);
    m_cmd->SetGraphicsRoot32BitConstants(0, 32, consts, 0);
    m_cmd->SetGraphicsRootDescriptorTable(1, m_dye.read->srv);
    BindAcid();
    BindInk();
    BindMirrorFold(m_mirrorW, m_mirrorH);
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
                        m_hdrActive ? 1.0f : 0.0f, (float)m_cfg.calibratePage, 0, 0, 0 };
    m_cmd->SetGraphicsRoot32BitConstants(0, 8, consts, 0);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd->ResourceBarrier(1, &b);
}

// One black frame, presented and then left on screen. Manual pause used to
// stop calling Frame(), which leaves the LAST rendered frame sitting on the
// panel -- a static image burning on an OLED for as long as the user is away.
// Clearing the back buffer and presenting once costs a single frame and the
// panel then shows black until the pause is lifted. Nothing about the sim is
// touched: m_time, the dye and the blob population all resume untouched.
void FluidRenderer::PresentBlack() {
    if (m_headless || !m_swapChain || m_presentBroken) return;
    BeginFrame();
    const UINT i = m_frameIndex;
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_backBuffers[i].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)i * m_rtvStride;
    m_cmd->ClearRenderTargetView(rtv, black, 0, nullptr);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd->ResourceBarrier(1, &b);
    // the second monitor, if it is mirroring us, goes dark with the first
    if (m_mirrorChain) {
        const UINT mi = m_mirrorChain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER mb = b;
        mb.Transition.pResource = m_mirrorBuffers[mi].Get();
        mb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        mb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_cmd->ResourceBarrier(1, &mb);
        D3D12_CPU_DESCRIPTOR_HANDLE mrtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        mrtv.ptr += (SIZE_T)(kFrames + 1 + mi) * m_rtvStride;
        m_cmd->ClearRenderTargetView(mrtv, black, 0, nullptr);
        mb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        mb.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_cmd->ResourceBarrier(1, &mb);
    }
    EndFrameAndPresent();
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

    if (m_cfg.gradientMode || m_cfg.calibratePage > 0) {
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
        // The reference's startup burst. Gated on idle_splats: a look whose
        // whole premise is CLEAR water (style=ink) cannot open with a
        // screenful of dye that then takes a minute of decay to clear. Looks
        // that keep idle splats on (every parity/WE config) are unchanged,
        // RandF() consumption included.
        if (m_cfg.idleSplats) MultipleSplats((int)(RandF() * 20) + 3);
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

    // ink drops (style-agnostic emitter; inert unless [drops] drops=1 and no
    // tail is still being painted)
    UpdateDrops(dt);

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

    // Liquid Acid oil layer: advect the blobs with the fluid we just stepped
    // (one frame of readback latency), then hand them to the display pass.
    if (m_cfg.acid.enabled) {
        // Re-seed when the population size changes from the settings window;
        // the per-kind fractions and radii are read at seed time, so moving
        // the count slider is also how you apply those live.
        int want = m_cfg.acid.blobCount;
        want = want < 1 ? 1 : (want > kAcidMaxBlobs ? kAcidMaxBlobs : want);
        if (!m_acidSeeded || (int)m_acidBlobs.size() != want) SeedAcidBlobs();
        UpdateVelocityReadback();
        StepAcidBlobs(dt);
        StepAcidDroplets(dt);
        UploadAcidConstants();
        // Oil as an obstacle to the ink: needs the blob buffer this frame's
        // display draw will read, so it runs after the upload. Returns at once
        // (and compiles nothing) while oil_drag and oil_dye_block are both 0.
        StepOilDrag(dt);
    }
    // Shared ink-in-water constants: needed by style=ink and by the acid look
    // when ink_mode=water. 112 bytes; skipped entirely for style=fluid.
    if (m_cfg.ink.enabled || m_cfg.acid.enabled) {
        UploadInkConstants();
        // The ink's motion gate samples the low-res velocity texture. The acid
        // path already refreshes it for the blob advection; style=ink has to
        // ask for it (one 64x36 dispatch).
        if (!m_cfg.acid.enabled && m_cfg.ink.motionHi > m_cfg.ink.motionLo)
            UpdateVelocityReadback();
    }

    if (m_headless) RenderDisplayOffscreen();
    else            RenderDisplay();
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
    if (!m_device) return;   // never initialized, or already shut down
    WaitForGpuIdle();

    // Release EVERYTHING Init/CreateDevice/CreateSimResources (and the lazy
    // mirror/analyzer paths) created, so all RAM/VRAM is actually handed back
    // and a later Init() starts from a clean slate. Used both at process exit
    // and by the fullscreen-pause suspend in main.cpp.

    // second-monitor mirror (its RTVs live in m_rtvHeap)
    for (UINT i = 0; i < kFrames; i++) m_mirrorBuffers[i].Reset();
    m_mirrorChain.Reset();
    m_mirrorBroken = false;

    // HDR analyzer
    m_anaTex.Reset();
    m_anaReadback.Reset();
    m_anaPending = false;
    m_anaState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // headless capture target
    m_shotTex.Reset();
    m_shotReadback.Reset();
    m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // stats + coverage readbacks
    m_readback.Reset();
    m_readbackPending = false;
    m_covReadback.Reset();
    m_covPending = false;

    // Liquid Acid: blob/param upload rings + the velocity readback
    for (UINT i = 0; i < kFrames; i++) {
        if (m_acidBlobUpload[i])  { m_acidBlobUpload[i]->Unmap(0, nullptr);  m_acidBlobUpload[i].Reset(); }
        if (m_acidParamUpload[i]) { m_acidParamUpload[i]->Unmap(0, nullptr); m_acidParamUpload[i].Reset(); }
        if (m_inkParamUpload[i])  { m_inkParamUpload[i]->Unmap(0, nullptr);  m_inkParamUpload[i].Reset(); }
        if (m_dropletUpload[i])     { m_dropletUpload[i]->Unmap(0, nullptr);     m_dropletUpload[i].Reset(); }
        if (m_dropletCellUpload[i]) { m_dropletCellUpload[i]->Unmap(0, nullptr); m_dropletCellUpload[i].Reset(); }
        m_acidBlobData[i] = nullptr;
        m_acidParamData[i] = nullptr;
        m_inkParamData[i] = nullptr;
        m_dropletData[i] = nullptr;
        m_dropletCellData[i] = nullptr;
    }
    // droplet particle sim: the population is CPU state, a resume reseeds it
    m_acidDrops.clear();
    m_dropletOrder.clear();
    m_dropletCellStart.clear();
    m_dropletCellCount.clear();
    m_dropletSeededFor = -1;
    m_dropletSpawnAcc = 0.0f;
    m_velReadback.Reset();
    m_velPending = false;
    m_acidBlobs.clear();
    m_acidSeeded = false;

    // sim textures
    auto freeTex = [](Tex& t) {
        t.res.Reset();
        t.srv = {};
        t.uav = {};
        t.state = D3D12_RESOURCE_STATE_COMMON;
    };
    freeTex(m_velocity.a);  freeTex(m_velocity.b);
    freeTex(m_dye.a);       freeTex(m_dye.b);
    freeTex(m_pressure.a);  freeTex(m_pressure.b);
    freeTex(m_divergence);  freeTex(m_curl);
    freeTex(m_coverage);
    freeTex(m_velLow);
    freeTex(m_oilMask);

    // root signatures + PSOs (shader blobs are recompiled from embedded source
    // in CreateDevice — nothing static is cached, so re-init is idempotent)
    m_psoClearV.Reset(); m_psoClear4.Reset(); m_psoClear1.Reset();
    m_psoCurl.Reset(); m_psoVorticity.Reset(); m_psoDivergence.Reset();
    m_psoClearPressure.Reset(); m_psoPressure.Reset(); m_psoGradSub.Reset();
    m_psoAdvectVel.Reset(); m_psoAdvectDye.Reset();
    m_psoSplatVel.Reset(); m_psoSplatDye.Reset(); m_psoSplatDyeCompact.Reset();
    m_psoDownsample.Reset(); m_psoDiffuseDye.Reset();
    m_psoDisplay.Reset(); m_psoGradient.Reset();
    m_psoLiquidAcid.Reset();
    m_psoOilMask.Reset(); m_psoOilDrag.Reset(); m_psoOilDyeBlock.Reset();
    m_oilMaskMade = false;
    m_psoInk.Reset();
    m_dropTimer = 0.0f;
    m_dropPrimed = false;
    m_dropTailLeft = 0.0f;
    m_dropQueued = false;
    m_computeRS.Reset();
    m_graphicsRS.Reset();

    // descriptor heaps
    m_srvHeap.Reset();
    m_rtvHeap.Reset();

    // swapchain, back buffers, command infrastructure, fence
    for (UINT i = 0; i < kFrames; i++) {
        m_backBuffers[i].Reset();
        m_allocators[i].Reset();
        m_fenceValues[i] = 0;
    }
    m_swapChain.Reset();
    m_cmd.Reset();
    m_queue.Reset();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
    m_fence.Reset();
    m_device.Reset();
    m_factory.Reset();

    // a following Init() must behave exactly like the first one
    m_frameIndex = 0;
    m_nextFence = 1;
    m_firstFrame = true;      // re-clear sim textures + startup splat burst
    m_presentBroken = false;
}

// ---------------------------------------------------------------------------
// M3: autonomous behaviors, ported from reference/script.js
// ---------------------------------------------------------------------------

// Map the cycling wheel position into the configured hue band. Full range
// (180) = classic wheel; narrower = a triangle sweep across the themed slice,
// with offsets (wanderer/dart) scaled into the band too.
float FluidRenderer::WheelHue(float offset) {
    float rangeFrac = m_cfg.hueRange / 180.0f;
    // coordination: while a hue command (mood transition bridge) rotates the
    // displayed field, counter-rotate emission so fresh dye still lands inside
    // the intended visible band instead of clashing with the rotated field
    float cmd = m_hsCommanded ? m_hueAngle / 360.0f : 0.0f;
    if (rangeFrac >= 0.999f) return m_globalHue + offset - cmd;
    float tri = 1.0f - fabsf(2.0f * m_globalHue - 1.0f);   // 0..1..0 per lap
    // linger: dwell at the band edges so one palette can fill the screen
    // before drifting on (0 = off = constant-speed triangle)
    float L = m_cfg.hueLinger;
    if (L > 0.0f) {
        float s;
        if (tri < L) s = 0.0f;
        else if (tri > 1.0f - L) s = 1.0f;
        else {
            s = (tri - L) / (1.0f - 2.0f * L);
            s = s * s * (3.0f - 2.0f * s);   // smooth glide between rests
        }
        tri = s;
    }
    return m_cfg.hueCenter / 360.0f + (tri - 0.5f) * rangeFrac + offset * rangeFrac - cmd;
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
    if (!m_device) return;   // suspended: the resume does a full Init instead
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
    if (!CreateSwapChainSoft(hwnd, sd, sc1)) return;
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
    if (!m_device) return;   // suspended: takes effect at the resume Init
    WaitForGpuIdle();
    m_covPending = false;
    m_readbackPending = false;
    m_velPending = false;
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

void FluidRenderer::CommandHueShift(float targetDeg, float durationSec) {
    m_hsCommanded = true;
    m_hsPhase = 1; m_hsTimer = 0;
    m_hsFrom = m_hueAngle; m_hsTo = targetDeg;
    m_hsGlideOverride = fmaxf(0.05f, durationSec);
}

void FluidRenderer::ReleaseHueShift(bool returnHome) {
    if (returnHome) {
        // rotate forward to the next full turn, then release (completion of
        // phase 3 clears the commanded flag inside UpdateHueShift)
        m_hsPhase = 3; m_hsTimer = 0;
        m_hsFrom = m_hueAngle;
        m_hsTo = ceilf((m_hueAngle + 0.001f) / 360.0f) * 360.0f;
        m_hsGlideOverride = 0.0f;   // return at the configured glide speed
    } else {
        // drop the command, but never snap the field's colors: if the angle
        // isn't home yet, glide to the next full turn like returnHome does
        m_hsGlideOverride = 0.0f;
        m_hsStep = 0;
        float rem = fmodf(m_hueAngle, 360.0f);
        if (rem > 0.5f || rem < -0.5f) {
            m_hsCommanded = true;
            m_hsPhase = 3; m_hsTimer = 0;
            m_hsFrom = m_hueAngle;
            m_hsTo = ceilf((m_hueAngle + 0.001f) / 360.0f) * 360.0f;
        } else {
            m_hsCommanded = false;
            m_hsPhase = 0; m_hsTimer = 0; m_hueAngle = 0;
        }
    }
}

void FluidRenderer::UpdateHueShift(float dt) {
    if (m_hsCommanded) {
        // externally driven: phase 1 glides to the target and holds;
        // phase 3 returns home, then hands back to the scheduler
        if (m_hsPhase == 1 || m_hsPhase == 3) {
            m_hsTimer += dt;
            float dur = m_hsGlideOverride > 0.0f ? m_hsGlideOverride
                                                 : fmaxf(0.05f, m_cfg.hsGlide);
            float t = fminf(1.0f, m_hsTimer / dur);
            t = t * t * (3.0f - 2.0f * t);
            m_hueAngle = m_hsFrom + (m_hsTo - m_hsFrom) * t;
            if (t >= 1.0f && m_hsPhase == 3) {
                m_hsCommanded = false;
                m_hsPhase = 0; m_hsTimer = 0; m_hsStep = 0; m_hueAngle = 0;
            }
        }
        return;
    }
    if (!m_cfg.hsEnabled || m_cfg.hueRange < 179.0f) {
        // Narrow hue bands must never be post-rotated: any rotation drags the
        // whole field out of band (the Ember green-leak bug). Color motion in
        // a banded look comes from emission-side drift (color_cycle_period),
        // never from the shift wheel. And never snap the rotation off —
        // glide to the next full turn.
        float rem = fmodf(m_hueAngle, 360.0f);
        if (rem > 0.5f || rem < -0.5f) {
            m_hsCommanded = true;
            m_hsPhase = 3; m_hsTimer = 0;
            m_hsFrom = m_hueAngle;
            m_hsTo = ceilf((m_hueAngle + 0.001f) / 360.0f) * 360.0f;
            m_hsGlideOverride = 0.0f;
        } else {
            m_hueAngle = 0;
            m_hsPhase = 0; m_hsTimer = 0; m_hsStep = 0;
        }
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
    // The sim still runs full-size under a mirror; only the DISPLAY folds. So
    // map the pointer through the same fold: the pixel the user is pointing at
    // is showing some source pixel, and that is where the dye must go. Land on
    // a mirrored copy and it maps back to the source, which is the same thing.
    // With mirror.mode = 0 this is the identity and costs nothing.
    float mx = in.mouseX, my = in.mouseY, fx = 1.0f, fy = 1.0f;
    MirrorMapPointer(mx, my, fx, fy);
    // [liquid_acid] mouse_oil_mode reads the pointer from here in uv -- and
    // ONLY the pointer. No splat, no dye, no branch above is affected: the
    // oil modes must never touch the ink (the rise presets ship with
    // show_mouse / hold_to_splat / splat_on_click all 0 and stay that way).
    m_ptrMoved = in.mouseMoved;
    if (in.mouseMoved) {
        m_ptrX = mx / fmaxf((float)m_width, 1.0f);
        m_ptrY = my / fmaxf((float)m_height, 1.0f);
    }
    if (m_cfg.holdToSplat && in.mouseDown) {
        float jx = (RandF() - 0.5f) * 200.0f;
        float jy = (RandF() - 0.5f) * 200.0f;
        Splat(mx, my, in.mouseDx * 0.5f * fx + jx, in.mouseDy * 0.5f * fy + jy,
              c[0] * es, c[1] * es, c[2] * es);
    } else if (!m_cfg.holdToSplat && m_cfg.splatOnClick &&
               in.mouseDown && !m_prevMouseDown) {
        MultipleSplats(rand() % 20 + 5);   // reference click burst (one-shot, unscaled)
    }
    m_prevMouseDown = in.mouseDown;
    if (in.mouseMoved && m_cfg.showMouse)
        Splat(mx, my, in.mouseDx * fx, in.mouseDy * fy,
              c[0] * es, c[1] * es, c[2] * es);
}

// ===========================================================================
// "Liquid Acid" look — oil metaballs on inked water.
//
// The oil is a CPU blob population (positive blobs = oil, NEGATIVE blobs =
// round holes/water bubbles eaten out of it) advected by the fluid's own
// velocity field, uploaded once per frame as a structured buffer the display
// shader loops over. See LiquidAcidConfig in fluid.h and the LIQUID_ACID
// section of kDisplaySrc.
// ===========================================================================

// GPU mirrors — must match struct AcidBlobGPU / cbuffer AcidCB in shaders.h.
// .c = the comb anisotropy (mouse_oil_mode 2): x = stretch amount along the
// drag direction, yz = that direction. All zero unless the comb is running.
struct AcidBlobGPU { float a[4]; float b[4]; float c[4]; };
struct AcidParamsGPU {
    float oil[4][4];
    float ink[4][4];
    float p0[4], p1[4], p2[4], p3[4], p4[4], p5[4], p6[4], p7[4], p8[4], p9[4];
    float p10[4], p11[4], p12[4], p13[4], p14[4], p15[4], p16[4], p17[4];
    float p18[4], p19[4], men[4];
};
static_assert(sizeof(AcidParamsGPU) == 464, "AcidCB layout");

// One particle of the droplet sim. Must match StructuredBuffer<float4>
// AcidDrops in shaders.h: xy = centre uv, z = visible radius SIGNED (negative
// = a water droplet trapped in the oil, positive = an oil droplet on the open
// ink), w = reserved.
struct AcidDropGPU { float a[4]; };
// Must match StructuredBuffer<uint2> DropCells: (first index, count).
struct DropCellGPU { uint32_t first, count; };

// GPU mirror of cbuffer InkCB in shaders.h (the SHARED ink-in-water block).
struct InkParamsGPU {
    float p0[4], p1[4], p2[4], p3[4], p4[4];
    float paper[4], tintThin[4], tintThick[4];
};
static_assert(sizeof(InkParamsGPU) == 128, "InkCB layout");

// CPU mirror of AcidHueShift in shaders.h: rotate HUE ONLY, holding saturation
// and value, so a swept colour is exactly as vivid at its new hue as it was at
// its old one. (A W3C matrix hue-rotate holds luma instead and turns a bright
// orange into olive on the way to yellow.) The oil colours and the ink stops
// are rotated here rather than in the pixel shader because the shader picks a
// flat fill per pixel out of the blob buffer.
static void RgbToHsv(const float c[3], float& h, float& sv, float& v) {
    const float mx = fmaxf(c[0], fmaxf(c[1], c[2]));
    const float mn = fminf(c[0], fminf(c[1], c[2]));
    const float d = mx - mn;
    h = 0.0f;
    if (d > 1e-7f) {
        if (mx == c[0])      h = (c[1] - c[2]) / d + (c[1] < c[2] ? 6.0f : 0.0f);
        else if (mx == c[1]) h = (c[2] - c[0]) / d + 2.0f;
        else                 h = (c[0] - c[1]) / d + 4.0f;
        h /= 6.0f;
    }
    sv = (mx > 1e-7f) ? d / mx : 0.0f;
    v = mx;
}

static void HsvHueShiftCpu(float c[3], float deg) {
    float h, sat, val;
    RgbToHsv(c, h, sat, val);
    h = fmodf(h + deg / 360.0f, 1.0f);
    if (h < 0.0f) h += 1.0f;
    RGB out = HSVtoRGB(h, sat, val);
    c[0] = out.r; c[1] = out.g; c[2] = out.b;
}

// Cross-fade two colours through HSV along the SHORT way round the wheel, so
// a magenta -> green transition passes through red/orange rather than sliding
// through desaturated grey the way a linear RGB lerp does.
static void HsvLerp3(const float a3[3], const float b3[3], float t, float out[3]) {
    float ha, sa, va, hb, sb, vb;
    RgbToHsv(a3, ha, sa, va);
    RgbToHsv(b3, hb, sb, vb);
    float dh = hb - ha;
    if (dh > 0.5f) dh -= 1.0f;
    if (dh < -0.5f) dh += 1.0f;
    float h = fmodf(ha + dh * t + 1.0f, 1.0f);
    RGB c = HSVtoRGB(h, sa + (sb - sa) * t, va + (vb - va) * t);
    out[0] = c.r; out[1] = c.g; out[2] = c.b;
}

// Rebuild the whole palette around two vivid anchors, reusing the S/V ratios
// of the hand-tuned palette so a swept pair has the same internal structure:
// four close oil shades, an ink ramp of [ink mid, near-black, oil dark, oil
// bright], and a bright ink-hue meniscus.
static void BuildAcidPalette(const LiquidAcidConfig& a, const float oilA[3],
                             const float inkA[3], float outOil[12],
                             float outInk[12], float outMen[3]) {
    float ho, so, vo, hi, si, vi;
    RgbToHsv(oilA, ho, so, vo);
    RgbToHsv(inkA, hi, si, vi);
    // the authored oil family's spread around its first entry
    static const float sMul[4] = { 1.000f, 1.055f, 1.019f, 1.058f };
    static const float vMul[4] = { 1.000f, 1.057f, 0.952f, 1.087f };
    for (int i = 0; i < 4; i++) {
        RGB c = HSVtoRGB(ho, fminf(so * sMul[i], 1.0f), fminf(vo * vMul[i], 1.0f));
        outOil[i * 3 + 0] = c.r; outOil[i * 3 + 1] = c.g; outOil[i * 3 + 2] = c.b;
    }
    auto put = [&](int i, float h, float sat, float val) {
        RGB c = HSVtoRGB(h, fminf(sat, 1.0f), fminf(val, 1.0f));
        outInk[i * 3 + 0] = c.r; outInk[i * 3 + 1] = c.g; outInk[i * 3 + 2] = c.b;
    };
    // Dark base. In every reference the ink is mostly NEAR-BLACK, with the
    // colour living in bands and filaments (ref 2: deep purple-black, lilac
    // only at the fronts). Stops 0/1 therefore keep the ink hue but stay dark;
    // only stops 2/3 carry the vivid complement, so the open ink reads as a
    // tinted black instead of flooding with mid tone.
    put(0, hi, si,          vi * 0.260f);   // dark base (the open ink)
    put(1, hi, si * 0.850f, vi * 0.090f);   // near-black, ink-tinted
    put(2, hi, si,          vi * 0.720f);   // mid band
    put(3, hi, si * 0.800f, vi * 1.450f);   // vivid front / filament band
    RGB m = HSVtoRGB(hi, fminf(si * 0.745f, 1.0f), fminf(vi * 1.858f, 1.0f));
    outMen[0] = m.r; outMen[1] = m.g; outMen[2] = m.b;
    (void)a;
}

// Saturation-weighted mean HSV hue of the oil palette, in the same
// parameterisation the shader measures the ink ramp with, so the complement
// clamp is consistent on both sides.
static float OilMeanHueDeg(const float cols[12]) {
    float sx = 0.0f, sy = 0.0f;
    for (int i = 0; i < 4; i++) {
        const float c[3] = { cols[i * 3 + 0], cols[i * 3 + 1], cols[i * 3 + 2] };
        float h, sat, val;
        RgbToHsv(c, h, sat, val);
        const float a = h * 6.2831853f, w = sat * val;
        sx += cosf(a) * w;
        sy += sinf(a) * w;
    }
    if (sx * sx + sy * sy < 1e-12f) return 0.0f;
    float d = atan2f(sy, sx) * 180.0f / 3.14159265358979f;
    return d < 0.0f ? d + 360.0f : d;
}

// Private deterministic RNG: the blob population must replay exactly under a
// --shot seed, and must not perturb the fluid's own rand() sequence (which
// would break parity with the normal look).
namespace {
struct AcidRng {
    uint32_t s;
    explicit AcidRng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() >> 8) * (1.0f / 16777216.0f); }         // [0,1)
    float f(float lo, float hi) { return lo + (hi - lo) * f(); }
};
}

void FluidRenderer::CreateAcidBuffers() {
    // Tiny (4 KB + 256 B per frame): created unconditionally so the display
    // draw can always bind root params 2/3 whichever PSO is selected.
    for (UINT i = 0; i < kFrames; i++) {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        rd.Width = sizeof(AcidBlobGPU) * kAcidMaxBlobs;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&m_acidBlobUpload[i])));
        rd.Width = (sizeof(AcidParamsGPU) + 255) & ~255u;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&m_acidParamUpload[i])));
        // shared ink-in-water parameter block (b2) — 112 bytes, same ring
        rd.Width = (sizeof(InkParamsGPU) + 255) & ~255u;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&m_inkParamUpload[i])));
        // droplet particle sim: 64 KB of particles + 18 KB of cell table per
        // frame. Created unconditionally, like the blob ring, so the display
        // draw can always bind root params 7/8 whichever PSO is selected —
        // and zeroed, so with the sim off every cell reports a count of 0.
        rd.Width = sizeof(AcidDropGPU) * kAcidMaxDrops;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&m_dropletUpload[i])));
        rd.Width = sizeof(DropCellGPU) * kDropGridW * kDropGridH;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&m_dropletCellUpload[i])));
        D3D12_RANGE none = { 0, 0 };
        HR(m_acidBlobUpload[i]->Map(0, &none, &m_acidBlobData[i]));
        HR(m_acidParamUpload[i]->Map(0, &none, &m_acidParamData[i]));
        HR(m_inkParamUpload[i]->Map(0, &none, &m_inkParamData[i]));
        HR(m_dropletUpload[i]->Map(0, &none, &m_dropletData[i]));
        HR(m_dropletCellUpload[i]->Map(0, &none, &m_dropletCellData[i]));
        memset(m_acidBlobData[i], 0, sizeof(AcidBlobGPU) * kAcidMaxBlobs);
        memset(m_acidParamData[i], 0, sizeof(AcidParamsGPU));
        memset(m_inkParamData[i], 0, sizeof(InkParamsGPU));
        memset(m_dropletData[i], 0, sizeof(AcidDropGPU) * kAcidMaxDrops);
        memset(m_dropletCellData[i], 0, sizeof(DropCellGPU) * kDropGridW * kDropGridH);
    }
}

// Seed the oil population. Four kinds, matching the references:
//   disc   — big flat saturated discs      (liquid-acid-ref-1)
//   web    — medium blobs seeded in CHAINS so they merge into veined webs
//            with circular openings        (liquid-acid-ref-2)
//   bubble — small round blobs, power-law sizes, some clustered
//            (liquid-acid-ref-3)
//   hole   — NEGATIVE weight, seeded inside a disc/web parent so it eats a
//            round hole out of the oil. Harmless when it drifts into open
//            ink: a negative field just stays below the isoline.
void FluidRenderer::SeedAcidBlobs() {
    const LiquidAcidConfig& a = m_cfg.acid;
    const int n = m_cfg.acid.blobCount < 1 ? 1 :
                  (m_cfg.acid.blobCount > kAcidMaxBlobs ? kAcidMaxBlobs : m_cfg.acid.blobCount);
    AcidRng rng(g_randSeed ? (g_randSeed * 2654435761u) ^ 0xAC1Du : GetTickCount());
    const float TWO_PI = 6.2831853f;

    int nDisc   = (int)(n * a.discFrac   + 0.5f);
    int nWeb    = (int)(n * a.webFrac    + 0.5f);
    int nBubble = (int)(n * a.bubbleFrac + 0.5f);
    if (nDisc + nWeb + nBubble > n - 1) nBubble = n - 1 - nDisc - nWeb;
    if (nBubble < 0) nBubble = 0;
    int nHole = n - nDisc - nWeb - nBubble;

    m_acidBlobs.assign((size_t)n, AcidBlob{});
    // power-law-ish size draw: pow(U, bias) with bias > 1 crowds the small end,
    // which is what gives "bubbles of every size" instead of one modal radius
    // pow(U, bias): bias > 1 crowds the small end (bubbles/holes, "every
    // size"), bias < 1 crowds the LARGE end (discs and webs, which the
    // references show filling most of the frame).
    auto drawR = [&](float lo, float hi, float bias) {
        return lo + (hi - lo) * powf(rng.f(), fmaxf(bias, 0.05f));
    };
    auto setCol = [&](AcidBlob& b, int idx) { b.colIdx = idx & 3; };
    auto place = [&](AcidBlob& b, float x, float y) {
        b.x = x; b.y = y; b.vx = b.vy = 0.0f;
        b.phase = rng.f(0.0f, TWO_PI);
        b.breathRate = rng.f(0.08f, 0.32f);
        b.s1 = rng.f(0.0f, TWO_PI);
        b.s2 = rng.f(0.0f, TWO_PI);
    };

    int idx = 0;
    for (int k = 0; k < nDisc && idx < n; k++, idx++) {
        AcidBlob& b = m_acidBlobs[idx];
        b.kind = 0; b.wgt = 1.0f;
        b.baseR = drawR(a.discMin, a.discMax, a.bigBias);
        setCol(b, (k % 3 == 0) ? 2 : 0);
        place(b, rng.f(0.03f, 0.97f), rng.f(0.03f, 0.97f));
    }
    // webs: a random walk of touching blobs makes connected veins with
    // circular openings between them, not a field of separate dots
    float cx = rng.f(0.15f, 0.85f), cy = rng.f(0.15f, 0.85f), prevR = 0.06f;
    for (int k = 0; k < nWeb && idx < n; k++, idx++) {
        AcidBlob& b = m_acidBlobs[idx];
        b.kind = 1; b.wgt = 1.0f;
        b.baseR = drawR(a.webMin, a.webMax, a.bigBias);
        setCol(b, (k % 4 == 0) ? 0 : 1);
        if (k % 9 == 0) { cx = rng.f(0.10f, 0.90f); cy = rng.f(0.10f, 0.90f); }
        else {
            float ang = rng.f(0.0f, TWO_PI);
            float step = (prevR + b.baseR) * rng.f(0.75f, 1.25f);
            cx += cosf(ang) * step;
            cy += sinf(ang) * step;
            cx = fminf(fmaxf(cx, -0.05f), 1.05f);
            cy = fminf(fmaxf(cy, -0.05f), 1.05f);
        }
        prevR = b.baseR;
        place(b, cx, cy);
    }
    for (int k = 0; k < nBubble && idx < n; k++, idx++) {
        AcidBlob& b = m_acidBlobs[idx];
        b.kind = 2; b.wgt = 1.0f;
        b.baseR = drawR(a.bubbleMin, a.bubbleMax, a.sizeBias);
        setCol(b, (k % 3 == 0) ? 1 : 3);
        if (k > 0 && rng.f() < 0.45f) {          // cluster on an earlier bubble
            const AcidBlob& par = m_acidBlobs[idx - 1 - (int)(rng.f() * 3.0f) % 3];
            float ang = rng.f(0.0f, TWO_PI);
            float d = (par.baseR + b.baseR) * rng.f(1.15f, 4.0f);
            place(b, par.x + cosf(ang) * d, par.y + sinf(ang) * d);
        } else {
            place(b, rng.f(0.02f, 0.98f), rng.f(0.02f, 0.98f));
        }
    }
    const int positives = nDisc + nWeb;
    for (int k = 0; k < nHole && idx < n; k++, idx++) {
        AcidBlob& b = m_acidBlobs[idx];
        b.kind = 3; b.wgt = -fmaxf(a.holeWeight, 0.05f);
        b.baseR = drawR(a.holeMin, a.holeMax, a.sizeBias);
        setCol(b, 0);                            // unused: holes never fill
        if (positives > 0) {
            const AcidBlob& par = m_acidBlobs[(int)(rng.f() * positives) % positives];
            // Sub-scale to the parent, or the hole swallows the blob it sits
            // in and the boundary between them turns to gravel.
            b.baseR = fminf(b.baseR, par.baseR * rng.f(0.20f, 0.55f));
            float ang = rng.f(0.0f, TWO_PI);
            float d = (par.baseR - b.baseR) * rng.f(0.0f, 0.80f);
            place(b, par.x + cosf(ang) * d, par.y + sinf(ang) * d);
        } else {
            place(b, rng.f(0.05f, 0.95f), rng.f(0.05f, 0.95f));
        }
    }
    // The seeding above already spreads every kind over the FULL height, which
    // is exactly the stagger rise_respawn needs: the column is populated from
    // the first frame and never empties, instead of one batch marching up
    // together and leaving a bare screen behind it.
    m_acidRespawnRng = rng.next() | 1u;
    m_acidSeeded = true;
}

// 64x36 velocity downsample -> CPU, one frame late (same trick as
// UpdateCoverage). Headless blocks on the fence so a --shot replays exactly.
void FluidRenderer::UpdateVelocityReadback() {
    if (!m_velLow.res || !m_velReadback) return;
    // 30 Hz is plenty for advecting blobs and keeps the headless path (which
    // blocks on the fence for determinism) from serialising every frame.
    const float kVelPeriod = 1.0f / 30.0f;
    if (m_headless && m_velPending && m_time - m_lastVelTime >= kVelPeriod &&
        m_fence->GetCompletedValue() < m_velFence) {
        HR(m_fence->SetEventOnCompletion(m_velFence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    if (m_velPending && m_fence->GetCompletedValue() >= m_velFence &&
        (!m_headless || m_time - m_lastVelTime >= kVelPeriod)) {
        uint8_t* data = nullptr;
        D3D12_RANGE range = { 0, (SIZE_T)m_velPitch * kVelH };
        if (SUCCEEDED(m_velReadback->Map(0, &range, (void**)&data))) {
            for (int y = 0; y < kVelH; y++) {
                const uint16_t* row = (const uint16_t*)(data + (SIZE_T)y * m_velPitch);
                for (int x = 0; x < kVelW; x++) {
                    m_velCpu[((size_t)y * kVelW + x) * 2 + 0] = HalfToFloat(row[x * 4 + 0]);
                    m_velCpu[((size_t)y * kVelW + x) * 2 + 1] = HalfToFloat(row[x * 4 + 1]);
                }
            }
            D3D12_RANGE none = { 0, 0 };
            m_velReadback->Unmap(0, &none);
        }
        m_velPending = false;
    }
    if (m_velPending || m_time - m_lastVelTime < kVelPeriod) return;
    m_lastVelTime = m_time;

    SimCB cb = {};
    m_cmd->SetComputeRootSignature(m_computeRS.Get());
    Transition(*m_velocity.read, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(m_velLow, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_cmd->SetPipelineState(m_psoDownsample.Get());
    cb.dimsW = kVelW; cb.dimsH = kVelH;
    m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &cb, 0);
    m_cmd->SetComputeRootDescriptorTable(1, m_velocity.read->srv);
    m_cmd->SetComputeRootDescriptorTable(2, m_velocity.read->srv);
    m_cmd->SetComputeRootDescriptorTable(3, m_velLow.uav);
    m_cmd->SetComputeRootDescriptorTable(4, m_velLow.uav);
    m_cmd->SetComputeRootDescriptorTable(5, m_velLow.uav);
    m_cmd->SetComputeRootDescriptorTable(6, m_velocity.read->srv);
    m_cmd->Dispatch(Groups(kVelW), Groups(kVelH), 1);

    Transition(m_velLow, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    src.pResource = m_velLow.res.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = m_velReadback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dst.PlacedFootprint.Footprint.Width = kVelW;
    dst.PlacedFootprint.Footprint.Height = kVelH;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = m_velPitch;
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    m_velFence = m_nextFence;      // EndFrameAndPresent signals this value
    m_velPending = true;
}

void FluidRenderer::StepAcidBlobs(float dt) {
    const LiquidAcidConfig& a = m_cfg.acid;
    const int n = (int)m_acidBlobs.size();
    if (n == 0) return;
    const float aspect = (float)m_width / fmaxf((float)m_height, 1.0f);
    const float simTexX = 1.0f / fmaxf((float)m_simW, 1.0f);   // sim texels/s -> uv/s
    const float simTexY = 1.0f / fmaxf((float)m_simH, 1.0f);
    const float t = m_time;

    // ---- oil_viscosity: one key, several coupled effects ------------------
    // A thick liquid lags the water it floats on, accelerates slowly, coasts,
    // breathes and sways more slowly, and NECKS into a neighbour over seconds
    // instead of snapping to it. Every factor below is exactly 1 (or the
    // branch is skipped) at oil_viscosity 0, so an existing ini is unmoved.
    const float vis     = fminf(fmaxf(a.oilViscosity, 0.0f), 1.0f);
    const float visFlow = 1.0f - 0.55f * vis;    // flow + curl response
    const float breathS = 1.0f - 0.60f * vis;    // breathing rate
    // Relaxation RATE, not a drag coefficient: a lower rate is what makes the
    // blob lag its target velocity in both directions.
    const float dampRate = fmaxf(a.damping, 0.05f) / (1.0f + 2.0f * vis);
    // The rise wobble is a function of wallpaper time, so "slower" is a slower
    // clock, not a smaller amplitude.
    const float tw = (vis > 1e-4f) ? t * (1.0f - 0.5f * vis) : t;
    const float fg = a.flowGain * visFlow;
    const float cdrift = a.curlDrift * visFlow;

    // ---- rise_parallax ----------------------------------------------------
    // s = lerp(1, clamp(r/disc_max, 0.25, 1), rise_parallax). A small blob is
    // a FAR blob: it rises slowly, sways slowly and picks up less of the near
    // currents. Exactly 1 for every blob when the key is 0.
    const float par  = fminf(fmaxf(a.riseParallax, 0.0f), 1.0f);
    const float rRef = fmaxf(a.discMax, 1e-4f);

    // ---- mouse_oil_mode: the pointer's own velocity, uv/s -----------------
    // Recorded by HandleInput, differentiated here (HandleInput has no dt) and
    // smoothed so a single 144 Hz sample does not spike. Nothing in this block
    // touches the ink -- the oil modes are deliberately oil-only.
    const int mm = a.mouseOilMode;
    float ptrVx = 0.0f, ptrVy = 0.0f, ptrSp = 0.0f;
    if (mm > 0) {
        if (m_ptrMoved && m_ptrHave && dt > 1e-5f) {
            const float kk = 1.0f - expf(-12.0f * dt);
            m_ptrVx += ((m_ptrX - m_ptrPx) / dt - m_ptrVx) * kk;
            m_ptrVy += ((m_ptrY - m_ptrPy) / dt - m_ptrVy) * kk;
        } else {
            const float kk = 1.0f - expf(-6.0f * dt);
            m_ptrVx -= m_ptrVx * kk;
            m_ptrVy -= m_ptrVy * kk;
        }
        ptrVx = m_ptrVx; ptrVy = m_ptrVy;
        ptrSp = sqrtf(ptrVx * ptrVx + ptrVy * ptrVy);
    }
    m_ptrPx = m_ptrX; m_ptrPy = m_ptrY; m_ptrHave = true;
    const float mGain = fmaxf(a.mouseOilGain, 0.0f);
    const float mR    = fmaxf(a.mouseOilRadius, 0.01f);

    // bilinear sample of the low-res velocity grid, in uv/s
    auto sampleVel = [&](float x, float y, float& ox, float& oy) {
        float fx = x * kVelW - 0.5f, fy = y * kVelH - 0.5f;
        int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
        float tx = fx - x0, ty = fy - y0;
        auto at = [&](int xi, int yi, int c) {
            xi = xi < 0 ? 0 : (xi >= kVelW ? kVelW - 1 : xi);
            yi = yi < 0 ? 0 : (yi >= kVelH ? kVelH - 1 : yi);
            return m_velCpu[((size_t)yi * kVelW + xi) * 2 + c];
        };
        ox = (at(x0, y0, 0) * (1 - tx) + at(x0 + 1, y0, 0) * tx) * (1 - ty)
           + (at(x0, y0 + 1, 0) * (1 - tx) + at(x0 + 1, y0 + 1, 0) * tx) * ty;
        oy = (at(x0, y0, 1) * (1 - tx) + at(x0 + 1, y0, 1) * tx) * (1 - ty)
           + (at(x0, y0 + 1, 1) * (1 - tx) + at(x0 + 1, y0 + 1, 1) * tx) * ty;
        ox *= simTexX; oy *= simTexY;
    };

    for (int i = 0; i < n; i++) {
        AcidBlob& b = m_acidBlobs[i];
        // rise_parallax: this blob's whole motion budget, by size.
        float pscale = 1.0f;
        if (par > 1e-4f) {
            float rr = b.baseR / rRef;
            rr = rr < 0.25f ? 0.25f : (rr > 1.0f ? 1.0f : rr);
            pscale = 1.0f + (rr - 1.0f) * par;
        }
        float vu = 0, vv = 0;
        sampleVel(b.x, b.y, vu, vv);
        const float fgb = fg * pscale;
        float tx = vu * fgb;
        float ty = vv * fgb;

        // Mild analytic curl drift so the oil still creeps where the fluid is
        // quiet (divergence-free: blobs swirl instead of piling up).
        float a1 = 3.1f * b.x + 0.23f * t + b.s1;
        float a2 = 2.7f * b.y + 0.19f * t;
        float a3 = 5.3f * b.x - 0.11f * t;
        float a4 = 4.1f * b.y + 0.29f * t + b.s2;
        float dpsidx = 3.1f * cosf(a1) * cosf(a2) + 0.7f * 5.3f * cosf(a3) * cosf(a4);
        float dpsidy = -2.7f * sinf(a1) * sinf(a2) - 0.7f * 4.1f * sinf(a3) * sinf(a4);
        tx += dpsidy * cdrift;
        ty += -dpsidx * cdrift;

        // buoyancy (uv y is down, so "up" is negative); holes sink
        ty += (b.wgt > 0.0f ? -1.0f : 1.0f) * a.buoyancy * (b.baseR / 0.12f);

        // ---- LAVA LAMP rise (rise_speed / rise_wobble) -------------------
        // A constant upward drift on the TARGET velocity, so the damping
        // relaxation still smooths it and the fluid can still shove a blob
        // sideways. Holes are water, not oil: they climb at 0.7x, which is
        // what makes a bubble creep across the disc it sits in rather than
        // riding it like a painted dot.
        if (a.riseSpeed > 1e-6f) {
            // rise_parallax scales the climb itself: the small (far) blobs
            // take up to 4x as long to cross the frame as the big near ones,
            // which is the whole parallax cue.
            const float rs = a.riseSpeed * (b.wgt > 0.0f ? 1.0f : 0.7f) * pscale;
            ty -= rs;
            if (a.riseWobble > 1e-4f) {
                // two incommensurate sines on the blob's own curl phases:
                // long lazy sway, never in step with its neighbours.
                tx += a.riseWobble * rs *
                      (0.85f * sinf(0.17f * tw + b.s1) + 0.45f * sinf(0.29f * tw + b.s2));
            }
        }

        // ---- mouse_oil_mode = 1, PUSH ------------------------------------
        // Blobs inside the radius take the pointer's own velocity (kernel
        // weighted) plus a weak radial shove, so the cursor drags AND parts
        // the oil. Added to the TARGET velocity, so the damping relaxation
        // still smooths it and the oil keeps its own inertia; nothing here can
        // move, shrink or delete a blob.
        if (mm == 1 && ptrSp > 1e-5f) {
            const float dx = (b.x - m_ptrX) * aspect, dy = b.y - m_ptrY;
            const float d2 = dx * dx + dy * dy;
            if (d2 < mR * mR) {
                float kq = 1.0f - d2 / (mR * mR);
                kq = kq * kq;
                const float g = mGain * kq;
                tx += ptrVx * g;
                ty += ptrVy * g;
                const float d = sqrtf(d2);
                if (d > 1e-5f) {
                    const float pr = 0.05f * g * ptrSp / (ptrSp + 0.25f);
                    tx += (dx / d) * pr / aspect;
                    ty += (dy / d) * pr;
                }
            }
        }
        // ---- mouse_oil_mode = 2, COMB ------------------------------------
        // The pointer path is a marbling comb: blobs in a NARROW band along it
        // stretch along the drag direction (b.comb / b.cdx,b.cdy, uploaded in
        // AcidBlobGPU.c and turned into the shader's anisotropic kernel), so
        // the cursor leaves thin oil streaks that slowly round back up over
        // ~3 s. Small blobs are towed along the stroke as well.
        if (mm == 2) {
            if (b.comb > 0.0f) {
                b.comb -= b.comb * (1.0f - expf(-dt / 3.0f));
                if (b.comb < 1e-4f) { b.comb = 0.0f; b.cdx = 0.0f; b.cdy = 0.0f; }
            }
            if (ptrSp > 1e-4f) {
                const float ux = ptrVx * aspect, uy = ptrVy;
                const float ul = sqrtf(ux * ux + uy * uy) + 1e-9f;
                const float nx = ux / ul, ny = uy / ul;
                const float dx = (b.x - m_ptrX) * aspect, dy = b.y - m_ptrY;
                const float along = dx * nx + dy * ny;
                const float perp  = fabsf(-dx * ny + dy * nx);
                const float bw = mR * 0.35f;
                if (perp < bw && fabsf(along) < mR) {
                    float kq = (1.0f - perp / bw) * (1.0f - fabsf(along) / mR);
                    kq = kq * kq;
                    const float sp = fminf(ptrSp / 0.6f, 1.0f);
                    const float want = fminf(mGain * sp * kq * 0.9f, 0.80f);
                    if (want > b.comb) {
                        b.comb += (want - b.comb) * (1.0f - expf(-dt / 0.25f));
                        b.cdx = nx; b.cdy = ny;
                    }
                    // (not `small`: <rpcndr.h> #defines that to char)
                    const float tow = fminf(fmaxf(0.10f / fmaxf(b.baseR, 0.01f), 0.3f), 2.0f);
                    tx += ptrVx * kq * tow * 0.5f * mGain;
                    ty += ptrVy * kq * tow * 0.5f * mGain;
                }
            }
        } else if (b.comb > 0.0f) {
            b.comb = 0.0f; b.cdx = 0.0f; b.cdy = 0.0f;
        }

        // Soft repulsion between SAME-SIGN blobs: keeps discs and bubbles from
        // collapsing into one continent (the oil PoC's size-sorting failure),
        // while holes stay free to sit inside oil.
        // oil_viscosity widens this into a NECKING model: outside contact
        // there is now a weak attraction over a band ~2x the contact radius
        // that pulls two approaching blobs together over seconds, and the
        // contact repulsion itself is softened so the neck thickens instead of
        // snapping. At oil_viscosity 0 reachF and soft are exactly 1, the
        // attraction branch is unreachable and this is the original loop.
        if (a.repulsion > 0.0001f || vis > 1e-4f) {
            const float reachF = 1.0f + 0.9f * vis;
            const float soft   = 1.0f - 0.35f * vis;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                const AcidBlob& o = m_acidBlobs[j];
                if ((o.wgt > 0.0f) != (b.wgt > 0.0f)) continue;
                float dx = (b.x - o.x) * aspect, dy = b.y - o.y;
                float d2 = dx * dx + dy * dy;
                float rr = (b.baseR + o.baseR) * 0.95f;
                const float reach = rr * reachF;
                if (d2 > reach * reach || d2 < 1e-8f) continue;
                float d = sqrtf(d2);
                float push;
                if (d < rr)            push = a.repulsion * (1.0f - d / rr) * 0.02f * soft;
                else if (vis > 1e-4f)  push = -vis * (1.0f - (d - rr) / (reach - rr)) * 0.006f;
                else                   continue;
                tx += (dx / d) * push / aspect;
                ty += (dy / d) * push;
            }
        }

        float k = 1.0f - expf(-dampRate * dt);   // fps-normalised
        b.vx += (tx - b.vx) * k;
        b.vy += (ty - b.vy) * k;
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        // wrap (population stays constant; no respawn churn)
        const float m = fmaxf(a.wrapMargin, 0.02f);
        if (b.x < -m)        b.x += 1.0f + 2.0f * m;
        if (b.x > 1.0f + m)  b.x -= 1.0f + 2.0f * m;
        // rise_respawn: a blob that has climbed clear of the TOP re-enters
        // BELOW the bottom edge, at a fresh x and a fresh radius from its own
        // kind's range, instead of reappearing at the same x with the same
        // size (a visible loop once the whole column has cycled once). The
        // population is unchanged either way -- this is a teleport, not a
        // spawn -- so nothing about the field's density moves.
        // A blob's FIELD reaches baseR * support_scale, not baseR, so the old
        // test (centre above -margin-baseR) teleported a big disc while its
        // lower lobe was still a quarter of the way down the screen: the whole
        // top of the frame twitched every time one respawned. That was the
        // user's "there is some flickering sometimes". Use the support radius,
        // cut at the distance where the Wyvill kernel has decayed to ~2% --
        // past that the blob cannot move any isoline. For the small kinds this
        // is a SHORTER trip than the old margin+baseR, so the on-screen
        // population does not thin out.
        const float supOut = b.baseR * a.supportScale * (1.0f + a.breathAmt) * 0.86f;
        const bool respawnMode = (a.riseSpeed > 1e-6f && a.riseRespawn);
        if (respawnMode && b.y < -supOut) {
            auto rf = [&]() {
                m_acidRespawnRng ^= m_acidRespawnRng << 13;
                m_acidRespawnRng ^= m_acidRespawnRng >> 17;
                m_acidRespawnRng ^= m_acidRespawnRng << 5;
                return (m_acidRespawnRng >> 8) * (1.0f / 16777216.0f);
            };
            float lo = a.bubbleMin, hi = a.bubbleMax, bias = a.sizeBias;
            if      (b.kind == 0) { lo = a.discMin; hi = a.discMax; bias = a.bigBias; }
            else if (b.kind == 1) { lo = a.webMin;  hi = a.webMax;  bias = a.bigBias; }
            else if (b.kind == 3) { lo = a.holeMin; hi = a.holeMax; bias = a.sizeBias; }
            b.baseR = lo + (hi - lo) * powf(rf(), fmaxf(bias, 0.05f));
            b.x = rf();
            b.y = 1.0f + b.baseR * a.supportScale * (1.0f + a.breathAmt) * 0.86f;
            b.vx = 0.0f;
            b.vy = 0.0f;
            b.s1 = rf() * 6.2831853f;
            b.s2 = rf() * 6.2831853f;
        } else if (!respawnMode) {
            if (b.y < -m)        b.y += 1.0f + 2.0f * m;
            if (b.y > 1.0f + m)  b.y -= 1.0f + 2.0f * m;
        }
        // In respawn mode the y wrap is off entirely: a blob is parked past
        // 1+m on purpose and has to climb back in. A hard ceiling on how far
        // below the frame it may be pushed is the only guard needed -- without
        // it a strong downward eddy could carry one off and the rise would
        // take minutes to bring it back, thinning the population for free.
        if (respawnMode) {
            const float floorY = 1.0f + supOut + 0.06f;
            if (b.y > floorY) { b.y = floorY; if (b.vy > 0.0f) b.vy = 0.0f; }
        }

        b.phase += b.breathRate * breathS * dt;
    }
}

// ===========================================================================
// DROPLET PARTICLE SIM ([liquid_acid] droplets)
//
// The procedural bubble swarms were two cellular layers evaluated in SCREEN
// space. The user, seeing them on the panel: "it still looks like 2 things
// layered. The dots look png'd on. They need to be simulated and attached to
// the oil, actually simulated. Make a surface tension sim and have smaller
// dots organically form."
//
// So these are particles, and -- the important part -- they are rendered by
// being ADDED INTO THE SAME METABALL FIELD as the big blobs, before the
// threshold. A trapped water droplet is a literal hole in the oil surface and
// an oil droplet on the ink (or inside a big hole) is a literal bump of it,
// which is why they get the soft thickness edge, the thin-oil colour fringe,
// the emergent halo and the rim without one line of special-case shading --
// and why two of them approaching NECK together, so a coalescence reads as
// surface tension. Both swarm kinds are replaced: kind 0 = water in oil,
// kind 1 = oil on open ink.
//
// Determinism: a private xorshift seeded off the shot seed, exactly like the
// blob population, so a --shot replays and the fluid's own rand() is untouched.
// ===========================================================================

// CPU twin of the shader's metaball loop: the field, its gradient and the
// local OIL's own velocity (the same soft-max blend the fill colour uses) at
// one uv point. rise_stretch's anisotropy is deliberately not reproduced --
// it only matters for a fast-moving blob's silhouette, and this is used for
// confinement and for carrying a droplet, where a few percent of a radius is
// far below what the damping relaxation smooths out anyway.
void FluidRenderer::AcidFieldAt(float x, float y, float aspect, float& outField,
                                float& outGx, float& outGy,
                                float& outVx, float& outVy) const {
    const LiquidAcidConfig& a = m_cfg.acid;
    const float px = x * aspect, py = y;
    float f = 0.0f, gx = 0.0f, gy = 0.0f;
    float vw = 0.0f, vx = 0.0f, vy = 0.0f;
    for (size_t bi = 0; bi < m_acidBlobs.size(); bi++) {
        const AcidBlob& b = m_acidBlobs[bi];
        const float r = b.baseR * (1.0f + a.breathAmt * sinf(b.phase));
        const float sup = r * a.supportScale;
        const float s2 = sup * sup;
        const float qx = px - b.x * aspect, qy = py - b.y;
        const float d2 = qx * qx + qy * qy;
        if (d2 >= s2 || s2 < 1e-12f) continue;
        const float u = 1.0f - d2 / s2;
        const float u2 = u * u;
        const float w = u2 * u;
        f += w * b.wgt;
        const float k = (-6.0f * u2 / s2) * b.wgt;
        gx += k * qx;
        gy += k * qy;
        if (b.wgt > 0.0f) {
            const float w2 = w * w, w4 = w2 * w2;
            vw += w4; vx += b.vx * w4; vy += b.vy * w4;
        }
    }
    outField = f; outGx = gx; outGy = gy;
    outVx = (vw > 1e-9f) ? vx / vw : 0.0f;
    outVy = (vw > 1e-9f) ? vy / vw : 0.0f;
}

void FluidRenderer::StepAcidDroplets(float dt) {
    const LiquidAcidConfig& a = m_cfg.acid;
    int target = a.droplets;
    if (target > kAcidMaxDrops) target = kAcidMaxDrops;
    if (target <= 0) {
        if (!m_acidDrops.empty()) m_acidDrops.clear();
        m_dropletSeededFor = -1;
        return;
    }
    if (dt <= 0.0f) return;

    const float aspect = (float)m_width / fmaxf((float)m_height, 1.0f);
    const float simTexX = 1.0f / fmaxf((float)m_simW, 1.0f);
    const float simTexY = 1.0f / fmaxf((float)m_simH, 1.0f);
    // The shader walks the 3x3 cells around a pixel, so a droplet's SUPPORT
    // radius may not exceed one cell -- that is what makes 3x3 exact instead
    // of "usually enough", and it is also the per-pixel cost bound.
    const float cellP = fminf(aspect / (float)kDropGridW, 1.0f / (float)kDropGridH);
    const float rCap  = cellP / fmaxf(a.dropletSupport, 0.5f);
    const float rMax  = fminf(fmaxf(a.dropletRMax, 1e-4f), rCap);
    const float rMin  = fminf(fmaxf(a.dropletRMin, 1e-5f), rMax * 0.5f);
    const float thresh = a.threshold;
    // ONE relaxation time for birth, coalescence and dissolution: nothing in
    // this sim changes size or membership discontinuously.
    const float tau   = 0.34f;
    const float relax = 1.0f - expf(-dt / tau);
    const float mrelax = 1.0f - expf(-dt / 0.18f);   // merge centre pull
    const float life  = a.dropletLife;
    // oil_viscosity, droplet side: a slower velocity relaxation (so a droplet
    // is carried smoothly and never snaps to a new target) and a damped
    // Brownian term. Both are exactly the shipped values at viscosity 0.
    const float dvis  = fminf(fmaxf(a.oilViscosity, 0.0f), 1.0f);
    const float dvDamp = fmaxf(a.dropletDamping, 0.1f) / (1.0f + 2.0f * dvis);
    const float dvJit  = 1.0f - 0.70f * dvis;

    auto rf = [&]() {
        m_dropletRng ^= m_dropletRng << 13;
        m_dropletRng ^= m_dropletRng >> 17;
        m_dropletRng ^= m_dropletRng << 5;
        return (m_dropletRng >> 8) * (1.0f / 16777216.0f);
    };
    // pow(U, bias) with bias > 1 crowds the small end: a heavy tail of tiny
    // droplets with a few big ones, which is what the macro footage shows.
    auto drawR = [&]() {
        return rMin + (rMax - rMin) * powf(rf(), fmaxf(a.dropletBias, 0.05f));
    };
    // bilinear tap of the 64x36 velocity readback, uv/s (same as StepAcidBlobs)
    auto sampleVel = [&](float x, float y, float& ox, float& oy) {
        float fx2 = x * kVelW - 0.5f, fy2 = y * kVelH - 0.5f;
        int x0 = (int)floorf(fx2), y0 = (int)floorf(fy2);
        float tx = fx2 - x0, ty = fy2 - y0;
        auto at = [&](int xi, int yi, int c) {
            xi = xi < 0 ? 0 : (xi >= kVelW ? kVelW - 1 : xi);
            yi = yi < 0 ? 0 : (yi >= kVelH ? kVelH - 1 : yi);
            return m_velCpu[((size_t)yi * kVelW + xi) * 2 + c];
        };
        ox = (at(x0, y0, 0) * (1 - tx) + at(x0 + 1, y0, 0) * tx) * (1 - ty)
           + (at(x0, y0 + 1, 0) * (1 - tx) + at(x0 + 1, y0 + 1, 0) * tx) * ty;
        oy = (at(x0, y0, 1) * (1 - tx) + at(x0 + 1, y0, 1) * tx) * (1 - ty)
           + (at(x0, y0 + 1, 1) * (1 - tx) + at(x0 + 1, y0 + 1, 1) * tx) * ty;
        ox *= simTexX; oy *= simTexY;
    };
    // Nucleation is a SURFACE effect: a droplet of water comes out of solution
    // where the film is THIN (near an edge, near another droplet's rim) and
    // where the flow is shearing, not uniformly across a slab of oil.
    auto nucleate = [&](int kind, AcidDrop& out) -> bool {
        for (int tr = 0; tr < 20; tr++) {
            const float x = rf(), y = rf();
            float f, gx, gy, ovx, ovy;
            AcidFieldAt(x, y, aspect, f, gx, gy, ovx, ovy);
            const float gl = sqrtf(gx * gx + gy * gy) + 1e-6f;
            const float sdf = (f - thresh) / gl;
            float p;
            if (kind == 0) {
                if (sdf <= 0.004f) continue;               // inside the oil only
                float vu, vv; sampleVel(x, y, vu, vv);
                const float sp = sqrtf(vu * vu + vv * vv);
                p = 0.12f + 0.68f * expf(-sdf / 0.055f) + 0.35f * (1.0f - expf(-sp * 18.0f));
            } else {
                if (sdf >= -0.004f) continue;              // open ink only
                p = 0.10f + 0.70f * expf(sdf / 0.070f);
            }
            if (rf() > fminf(p, 1.0f)) continue;
            out = AcidDrop{};
            out.x = x; out.y = y;
            out.r = 0.0f;                                  // born at nothing
            out.rt = drawR();
            out.kind = kind;
            out.mergeTo = -1;
            return true;
        }
        return false;
    };

    // ---- bulk fill (startup / population change) -------------------------
    // Nucleating 1500 droplets at ~40/s would leave the first 40 s bare, so
    // the initial condition is drawn in one go, at full radius and with
    // staggered ages so they do not all reach droplet_life together.
    if (m_dropletSeededFor != target) {
        m_dropletRng = (g_randSeed ? (g_randSeed * 2246822519u) ^ 0xD407u : GetTickCount()) | 1u;
        m_acidDrops.clear();
        m_acidDrops.reserve(target);
        for (int i = 0; i < target; i++) {
            AcidDrop d;
            const int kind = (rf() < a.dropletInkFrac) ? 1 : 0;
            if (!nucleate(kind, d)) continue;
            d.r = d.rt;
            d.age = (life > 0.5f) ? rf() * life : 0.0f;
            m_acidDrops.push_back(d);
        }
        m_dropletSeededFor = target;
        m_dropletSpawnAcc = 0.0f;
    }

    const int n = (int)m_acidDrops.size();
    std::vector<float> ax((size_t)n, 0.0f), ay((size_t)n, 0.0f);

    // ---- 1. motion -------------------------------------------------------
    for (int i = 0; i < n; i++) {
        AcidDrop& d = m_acidDrops[i];
        d.age += dt;
        float f, gx, gy, ovx, ovy;
        AcidFieldAt(d.x, d.y, aspect, f, gx, gy, ovx, ovy);
        const float gl = sqrtf(gx * gx + gy * gy) + 1e-6f;
        const float sdf = (f - thresh) / gl;
        float vu, vv; sampleVel(d.x, d.y, vu, vv);

        float tx, ty;
        if (d.kind == 0) {
            // trapped INSIDE the oil: it rides the oil, not the water. ovx/ovy
            // is the local oil's own velocity, so a droplet travels with the
            // disc it sits in and shears when that disc does.
            tx = ovx; ty = ovy;
            // ...and lags it slightly (uv y is down, so +y is down): trapped
            // water is denser than the wax, which is what makes a bubble creep
            // ACROSS the disc instead of riding it like a painted dot.
            ty += a.riseSpeed * a.dropletRise;
        } else {
            tx = vu * a.flowGain;
            ty = vv * a.flowGain;
        }
        // slow Brownian jitter -- damped by oil_viscosity: a droplet suspended
        // in a thick liquid does not twitch.
        tx += (rf() * 2.0f - 1.0f) * a.dropletJitter * dvJit;
        ty += (rf() * 2.0f - 1.0f) * a.dropletJitter * dvJit;

        // ---- confinement: kind 0 stays inside the oil, kind 1 outside it --
        // Push back along the field gradient (which points INTO the oil).
        const float want = (d.kind == 0) ? 1.0f : -1.0f;
        const float margin = fmaxf(d.r * 0.6f, 1e-4f);
        if (sdf * want < margin) {
            const float over = (margin - sdf * want) / margin;
            const float push = 0.05f * fminf(over, 3.0f);
            tx += (gx / gl) * want * push / aspect;
            ty += (gy / gl) * want * push;
            d.out += dt;
        } else {
            d.out = fmaxf(d.out - dt * 2.0f, 0.0f);
        }
        // Stranded on the wrong side for good (the oil moved away): dissolve
        // it. Never delete it outright -- that is exactly the one-frame pop
        // the procedural swarm had.
        if (d.out > 1.2f) d.rt = 0.0f;

        // merge pull: the absorbed droplet's centre slides into the survivor
        if (d.mergeTo >= 0 && d.mergeTo < n) {
            const AcidDrop& s = m_acidDrops[d.mergeTo];
            d.x += (s.x - d.x) * mrelax;
            d.y += (s.y - d.y) * mrelax;
        }

        const float k = 1.0f - expf(-dvDamp * dt);
        d.vx += (tx - d.vx) * k;
        d.vy += (ty - d.vy) * k;
        d.x += d.vx * dt;
        d.y += d.vy * dt;

        // ---- contribution gate -------------------------------------------
        // A droplet on the WRONG side of the interface is invisible in itself
        // -- a positive one inside the oil only adds oil to oil, a negative
        // one on open ink only subtracts from ink that is already below the
        // isoline -- but its kernel is TINY and therefore very steep, so it
        // still dominates |grad| in its neighbourhood. sdf = (field - thresh)
        // / |grad| then collapses toward 0 there, which drops the pixel into
        // the dark rim band and flattens the film thickness proxy: a droplet
        // that is not there draws a hollow dark RING with an oil centre, and
        // a cluster of them smears a soft dark veil over the sheet. Fade the
        // whole contribution (field AND gradient) out instead, over half the
        // droplet's own radius of travel, so it leaves no trace at all.
        {
            float f2, gx2, gy2, ovx2, ovy2;
            AcidFieldAt(d.x, d.y, aspect, f2, gx2, gy2, ovx2, ovy2);
            const float gl2 = sqrtf(gx2 * gx2 + gy2 * gy2) + 1e-6f;
            const float side = ((d.kind == 0) ? 1.0f : -1.0f) * ((f2 - thresh) / gl2);
            float g = side / fmaxf(0.5f * d.r, 1e-5f);
            g = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
            g = g * g * (3.0f - 2.0f * g);
            // ---- "an actual hole, or invisible": no rim without a fill ----
            // Being on the right side is not enough. The droplet's own PEAK
            // kernel has to carry the local field across the isoline, or it
            // draws nothing but a rim around nothing -- the hollow rings with
            // an oil centre the user photographed. A positive droplet that
            // has drifted into a deep hole can never lift the field back over
            // the threshold; a negative one has to beat whatever oil it is
            // under (the shader scales its punch by the LOCAL field, which is
            // what normally makes this work at any thickness, but not where
            // the two fight). Fade it out over a small field margin rather
            // than switching it, so nothing pops.
            float over;
            if (d.kind == 0) over = thresh - (f2 - fmaxf(f2, 1.0f) * a.dropletWeight);
            // kind 1 carries the local deficit plus its authored weight (see
            // the shader), so it always crosses -- the test is here for
            // symmetry and to keep the two definitions in one place.
            else             over = a.dropletOilW;
            float pg = over / 0.18f;
            pg = pg < 0.0f ? 0.0f : (pg > 1.0f ? 1.0f : pg);
            g *= pg * pg * (3.0f - 2.0f * pg);
            // ...and fade out below about a pixel. A sub-pixel droplet cannot
            // be resolved -- no pixel lands near enough to its centre for the
            // kernel to reach full strength and punch through -- so it, too,
            // would contribute nothing but a gradient spike.
            // The floor is a pixel OR the dark rim's own width, whichever is
            // larger: a droplet whose hole would come out narrower than the
            // rim band painted around it is all rim and no fill, which is the
            // other half of the ring. rim_width is a half-width in the same
            // p-units as r.
            const float pxY = 1.0f / fmaxf((float)m_height, 1.0f);
            const float rFloor = fmaxf(1.2f * pxY, 1.6f * fmaxf(a.rimWidth, 1e-5f));
            float sp2 = (d.r - 0.35f * rFloor) / fmaxf(0.65f * rFloor, 1e-9f);
            sp2 = sp2 < 0.0f ? 0.0f : (sp2 > 1.0f ? 1.0f : sp2);
            d.gate = g * sp2 * sp2 * (3.0f - 2.0f * sp2);
        }

        if (life > 0.5f && d.age > life) d.rt = 0.0f;
        // off the frame: dissolve there, never inside the visible area
        if (d.x < -0.04f || d.x > 1.04f || d.y < -0.04f || d.y > 1.04f) d.rt = 0.0f;
        d.r += (d.rt - d.r) * relax;
        if (!(d.r > 0.0f)) d.r = 0.0f;              // NaN guard
        if (!(d.x > -10.0f && d.x < 10.0f)) { d.x = 0.5f; d.rt = 0.0f; d.r = 0.0f; }
        if (!(d.y > -10.0f && d.y < 10.0f)) { d.y = 0.5f; d.rt = 0.0f; d.r = 0.0f; }
    }

    // ---- 2. uniform grid (neighbour search AND the shader's cell table) ---
    const int NC = kDropGridW * kDropGridH;
    auto buildGrid = [&]() {
        const int m = (int)m_acidDrops.size();
        m_dropletCellCount.assign((size_t)NC, 0);
        m_dropletCellStart.assign((size_t)NC + 1, 0);
        std::vector<int> cellOf((size_t)m, 0);
        for (int i = 0; i < m; i++) {
            const AcidDrop& d = m_acidDrops[i];
            int cx = (int)floorf(d.x * kDropGridW);
            int cy = (int)floorf(d.y * kDropGridH);
            cx = cx < 0 ? 0 : (cx >= kDropGridW ? kDropGridW - 1 : cx);
            cy = cy < 0 ? 0 : (cy >= kDropGridH ? kDropGridH - 1 : cy);
            const int c = cy * kDropGridW + cx;
            cellOf[i] = c;
            if (m_dropletCellCount[c] < kDropCellCap) m_dropletCellCount[c]++;
            else cellOf[i] = -1;                    // over the per-cell cap
        }
        int acc = 0;
        for (int c = 0; c < NC; c++) { m_dropletCellStart[c] = acc; acc += m_dropletCellCount[c]; }
        m_dropletCellStart[NC] = acc;
        std::vector<int> fill((size_t)NC, 0);
        m_dropletOrder.assign((size_t)acc, 0);
        for (int i = 0; i < m; i++) {
            const int c = cellOf[i];
            if (c < 0) continue;
            m_dropletOrder[(size_t)m_dropletCellStart[c] + fill[c]] = i;
            fill[c]++;
        }
    };
    buildGrid();

    // ---- 3. surface tension: attraction, contact repulsion, coalescence ---
    const float mergeF = 1.0f - fminf(fmaxf(a.dropletMerge, 0.05f), 0.90f);
    std::vector<AcidDrop> satellites;
    for (int ci = 0; ci < NC; ci++) {
        const int cx0 = ci % kDropGridW, cy0 = ci / kDropGridW;
        const int e0 = m_dropletCellStart[ci] + m_dropletCellCount[ci];
        for (int si = m_dropletCellStart[ci]; si < e0; si++) {
            const int i = m_dropletOrder[si];
            if (m_acidDrops[i].r <= 0.0f) continue;
            for (int oy = -1; oy <= 1; oy++) {
                const int yy = cy0 + oy;
                if (yy < 0 || yy >= kDropGridH) continue;
                for (int ox = -1; ox <= 1; ox++) {
                    const int xx = cx0 + ox;
                    if (xx < 0 || xx >= kDropGridW) continue;
                    const int cj = yy * kDropGridW + xx;
                    const int e1 = m_dropletCellStart[cj] + m_dropletCellCount[cj];
                    for (int sj = m_dropletCellStart[cj]; sj < e1; sj++) {
                        const int j = m_dropletOrder[sj];
                        if (j <= i) continue;               // each pair once
                        AcidDrop& di = m_acidDrops[i];
                        AcidDrop& dj = m_acidDrops[j];
                        if (dj.kind != di.kind || dj.r <= 0.0f) continue;
                        const float dx = (di.x - dj.x) * aspect, dy = di.y - dj.y;
                        const float d2 = dx * dx + dy * dy;
                        const float sum = di.r + dj.r;
                        const float reach = 3.0f * sum;
                        if (d2 > reach * reach || d2 < 1e-12f) continue;
                        const float dd = sqrtf(d2), inv = 1.0f / dd;
                        if (dd < sum * mergeF && di.mergeTo < 0 && dj.mergeTo < 0
                            && di.rt > 0.0f && dj.rt > 0.0f) {
                            // COALESCE, area-conserving. The survivor is the
                            // larger one; the other pours into it (rt -> 0,
                            // centre pulled in) instead of being deleted, so
                            // the metaball union necks them together.
                            const bool iBig = (di.rt >= dj.rt);
                            const int bi = iBig ? i : j, sm = iBig ? j : i;
                            const float rb = m_acidDrops[bi].rt, rs = m_acidDrops[sm].rt;
                            const float area = rb * rb + rs * rs;
                            float rn = sqrtf(area);
                            if (rn > rMax) {
                                // A cap, or every droplet ends up one puddle.
                                // The surplus area leaves as a SATELLITE, the
                                // way a real over-fed drop pinches one off.
                                const float rs2 = area - rMax * rMax;
                                rn = rMax;
                                if (rs2 > rMin * rMin) {
                                    AcidDrop sat = AcidDrop{};
                                    const float ang = rf() * 6.2831853f;
                                    const float rr = sqrtf(rs2);
                                    sat.x = m_acidDrops[bi].x
                                          + cosf(ang) * (rMax + rr) * 1.25f / aspect;
                                    sat.y = m_acidDrops[bi].y + sinf(ang) * (rMax + rr) * 1.25f;
                                    sat.r = 0.0f; sat.rt = rr;
                                    sat.kind = m_acidDrops[bi].kind; sat.mergeTo = -1;
                                    satellites.push_back(sat);
                                }
                            }
                            m_acidDrops[bi].rt = rn;
                            m_acidDrops[sm].rt = 0.0f;
                            m_acidDrops[sm].mergeTo = bi;
                            continue;
                        }
                        float acc;
                        if (dd < sum) {
                            // contact repulsion: two droplets that are not
                            // merging stay round instead of interpenetrating
                            acc = (1.0f - dd / sum) * 0.35f;
                        } else {
                            // short-range attraction within ~3 radii
                            acc = -a.dropletAttract * (1.0f - (dd - sum) / (2.0f * sum)) * 0.05f;
                        }
                        const float ux = dx * inv, uy = dy * inv;
                        ax[i] += ux * acc; ay[i] += uy * acc;
                        ax[j] -= ux * acc; ay[j] -= uy * acc;
                    }
                }
            }
        }
    }
    for (int i = 0; i < n; i++) {
        m_acidDrops[i].vx += ax[i] * dt;
        m_acidDrops[i].vy += ay[i] * dt;
    }

    // ---- 4. retire the fully dissolved, keeping mergeTo consistent -------
    {
        std::vector<int> remap(m_acidDrops.size(), -1);
        std::vector<AcidDrop> keep;
        keep.reserve(m_acidDrops.size());
        for (size_t i = 0; i < m_acidDrops.size(); i++) {
            const AcidDrop& d = m_acidDrops[i];
            if (d.rt <= 0.0f && d.r < rMin * 0.08f) continue;   // gone, invisibly
            remap[i] = (int)keep.size();
            keep.push_back(d);
        }
        for (size_t i = 0; i < keep.size(); i++) {
            AcidDrop& d = keep[i];
            d.mergeTo = (d.mergeTo >= 0 && d.mergeTo < (int)remap.size())
                      ? remap[d.mergeTo] : -1;
        }
        m_acidDrops.swap(keep);
    }
    for (size_t i = 0; i < satellites.size(); i++)
        if ((int)m_acidDrops.size() < kAcidMaxDrops) m_acidDrops.push_back(satellites[i]);

    // ---- 5. nucleation ---------------------------------------------------
    m_dropletSpawnAcc += fmaxf(a.dropletSpawn, 0.0f) * dt;
    if (m_dropletSpawnAcc > 60.0f) m_dropletSpawnAcc = 60.0f;
    int live = 0;
    for (size_t i = 0; i < m_acidDrops.size(); i++) if (m_acidDrops[i].rt > 0.0f) live++;
    while (m_dropletSpawnAcc >= 1.0f) {
        m_dropletSpawnAcc -= 1.0f;
        if (live >= target || (int)m_acidDrops.size() >= kAcidMaxDrops) break;
        AcidDrop d;
        if (nucleate((rf() < a.dropletInkFrac) ? 1 : 0, d)) { m_acidDrops.push_back(d); live++; }
    }

    // ---- 6. final bin, the one the shader reads --------------------------
    buildGrid();
}

// ===========================================================================
// OIL DRAG ([liquid_acid] oil_drag / oil_dye_block)
//
// The user, watching acid-rise-12 live: "make it impossible for the fluid sim
// underneath, the mono ink, to get under the oil, or rather an intense
// friction that makes it hard for it to get under."
//
// Three compute passes, all of them skipped entirely while both keys are 0 --
// including the PSO compiles, which happen the first frame one of them is
// turned on. The mask is the SAME Wyvill field the display shader thresholds,
// which is what makes the holes behave: a negative blob is negative here too,
// so a hole is not oil, is not dragged, and the ink you see through it is the
// ink layer, exactly as it renders.
// ===========================================================================
void FluidRenderer::EnsureOilMask() {
    if (m_oilMaskMade) return;
    if (!m_device || !m_computeRS) return;
    auto makeCS = [&](const char* entry, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> cs = Compile(kComputeSrc, entry, "cs_5_0");
        D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {};
        cd.pRootSignature = m_computeRS.Get();
        cd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        HR(m_device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&pso)));
    };
    makeCS("CSOilMask", m_psoOilMask);
    makeCS("CSOilDrag", m_psoOilDrag);
    makeCS("CSOilDyeBlock", m_psoOilDyeBlock);
    m_oilMaskMade = true;
}

void FluidRenderer::StepOilDrag(float dt) {
    const LiquidAcidConfig& a = m_cfg.acid;
    const float drag  = fmaxf(a.oilDrag, 0.0f);
    const float block = fmaxf(a.oilDyeBlock, 0.0f);
    if (drag <= 1e-4f && block <= 1e-4f) return;
    if (!m_oilMask.res || !m_acidBlobUpload[m_frameIndex] || dt <= 0.0f) return;
    EnsureOilMask();
    if (!m_psoOilMask || !m_psoOilDrag || !m_psoOilDyeBlock) return;

    const int nb = (int)m_acidBlobs.size();
    if (nb <= 0) return;

    // Soft edge of the coverage threshold, in FIELD units. The Wyvill field
    // climbs from 0 to ~1 over a blob's support radius, so a band of a few sim
    // texels is a small fraction of the threshold; 0.10 of it puts the edge at
    // roughly 3-5 texels for the shipped radii, which is the couple of texels
    // the brief asks for without aliasing the mask into a staircase.
    const float edge = fmaxf(a.threshold, 0.05f) * 0.10f;

    SimCB cb = {};
    cb.texelW = 1.0f / m_simW;
    cb.texelH = 1.0f / m_simH;
    cb.dt = dt;
    cb.aspect = (float)m_width / fmaxf((float)m_height, 1.0f);
    cb.value  = a.threshold;
    cb.radius = a.supportScale;
    cb.pointX = (float)nb;
    cb.pointY = edge;

    m_cmd->SetComputeRootSignature(m_computeRS.Get());
    m_cmd->SetComputeRootShaderResourceView(
        7, m_acidBlobUpload[m_frameIndex]->GetGPUVirtualAddress());

    auto run = [&](ID3D12PipelineState* pso, Tex* s0, Tex* s1, Tex* dst) {
        if (s0) Transition(*s0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (s1) Transition(*s1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(*dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_cmd->SetPipelineState(pso);
        cb.dimsW = dst->w; cb.dimsH = dst->h;
        m_cmd->SetComputeRoot32BitConstants(0, sizeof(SimCB) / 4, &cb, 0);
        m_cmd->SetComputeRootDescriptorTable(1, s0 ? s0->srv : dst->srv);
        m_cmd->SetComputeRootDescriptorTable(2, s1 ? s1->srv : dst->srv);
        m_cmd->SetComputeRootDescriptorTable(3, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(4, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(5, dst->uav);
        m_cmd->SetComputeRootDescriptorTable(6, dst->srv);
        m_cmd->Dispatch(Groups(dst->w), Groups(dst->h), 1);
    };

    // 1. coverage
    run(m_psoOilMask.Get(), nullptr, nullptr, &m_oilMask);
    UavBarrier(m_oilMask.res.Get());

    // 2. friction + rim deflection (+ viscous diffusion under the oil).
    // Applied ONCE per frame with the frame's own dt rather than inside the
    // substep loop: it is a damping term, fps-normalised the same way every
    // other decay in this file is (k = 1 - exp(-rate*dt)), so the result is
    // the same at 60 and at 144 fps and the solver never sees a discontinuity.
    if (drag > 1e-4f) {
        // oil_drag 1 -> ~6/s, i.e. ink under the oil loses ~99% of its speed
        // in half a second. Anything faster is indistinguishable from a wall.
        cb.colorR = 1.0f - expf(-6.0f * drag * dt);
        // The rim push is the mask gradient scaled to sim texels/s. Deliberately
        // gentle: this is meant to make ink SLIDE along the edge of an island,
        // not to blow a hole around it.
        cb.colorB = fminf(fmaxf(a.oilViscosity, 0.0f), 1.0f) * 0.6f;
        // texels/s of push per unit of mask gradient, times dt -- an
        // acceleration integrated over the frame, so it is fps-normalised too.
        cb.colorG = 90.0f * drag * dt;
        run(m_psoOilDrag.Get(), m_velocity.read, &m_oilMask, m_velocity.write);
        m_velocity.Swap();
    }
    // 3. dye under the oil fades out over ~1-2 s at oil_dye_block 1.
    if (block > 1e-4f) {
        cb.cap = 1.0f - expf(-1.2f * block * dt);
        run(m_psoOilDyeBlock.Get(), m_dye.read, &m_oilMask, m_dye.write);
        m_dye.Swap();
    }
}

void FluidRenderer::UploadAcidConstants() {
    const LiquidAcidConfig& a = m_cfg.acid;
    const UINT fi = m_frameIndex;
    if (!m_acidBlobData[fi] || !m_acidParamData[fi]) return;

    // ---- effective palette for this frame -------------------------------
    // With the sweep off this is just the authored palette. With it on, the
    // oil anchor and the ink mid tone cross-fade between curated vivid pairs
    // and the rest of the palette is rebuilt around them using the authored
    // palette's own S/V ratios.
    float effOil[12], effInk[12], effMen[3];
    memcpy(effOil, a.oilColors, sizeof(effOil));
    memcpy(effInk, a.inkRamp, sizeof(effInk));
    memcpy(effMen, a.meniscusCol, sizeof(effMen));
    if (a.hueSweepPeriod > 0.01f) {
        int np = a.sweepCount;
        if (np < 1) np = 1;
        if (np > LiquidAcidConfig::kSweepMax) np = LiquidAcidConfig::kSweepMax;
        const float u = fmodf(m_time / a.hueSweepPeriod, 1.0f) * np;
        const int k0 = (int)u % np, k1 = (k0 + 1) % np;
        // smoothstep the cross-fade so each pair gets a long settled stretch
        float f = u - floorf(u);
        f = f * f * (3.0f - 2.0f * f);
        float oilA[3], inkA[3];
        HsvLerp3(&a.sweepOil[k0 * 3], &a.sweepOil[k1 * 3], f, oilA);
        HsvLerp3(&a.sweepInk[k0 * 3], &a.sweepInk[k1 * 3], f, inkA);
        BuildAcidPalette(a, oilA, inkA, effOil, effInk, effMen);
        // Hand-authored four-shade entries (sweep_oil_N given twelve floats)
        // override the derived oil family: cross-fade the four shades one for
        // one, so the user's tile9 palettes land on screen exactly as they
        // were rendered instead of being rebuilt from a single anchor. An
        // entry without a full set contributes the family just derived for
        // it, so the two forms mix freely inside one list. Nothing here runs
        // unless an ini actually carries a full set.
        if (a.sweepOilFullSet[k0] || a.sweepOilFullSet[k1]) {
            float fam0[12], fam1[12], dummyInk[12], dummyMen[3];
            if (a.sweepOilFullSet[k0]) memcpy(fam0, &a.sweepOilFull[k0 * 12], sizeof(fam0));
            else BuildAcidPalette(a, &a.sweepOil[k0 * 3], &a.sweepInk[k0 * 3],
                                  fam0, dummyInk, dummyMen);
            if (a.sweepOilFullSet[k1]) memcpy(fam1, &a.sweepOilFull[k1 * 12], sizeof(fam1));
            else BuildAcidPalette(a, &a.sweepOil[k1 * 3], &a.sweepInk[k1 * 3],
                                  fam1, dummyInk, dummyMen);
            for (int ci = 0; ci < 4; ci++)
                HsvLerp3(&fam0[ci * 3], &fam1[ci * 3], f, &effOil[ci * 3]);
        }
    }
    // ---- global hue rotation (hue_rotate_period) -------------------------
    // The user's ask was "the hue of the ENTIRE screen can shift", so this is
    // one angle applied to the WHOLE oil family, never per blob: neighbouring
    // discs keep their relative shades and the frame reads as one palette
    // turning, not as confetti. The ink is untouched -- on the mono-ink
    // tile9 family it stays grey and the holes stay black through every hue.
    if (a.hueRotatePeriod > 0.01f) {
        const float deg = 360.0f * fmodf(m_time / a.hueRotatePeriod, 1.0f);
        for (int ci = 0; ci < 4; ci++) HsvHueShiftCpu(&effOil[ci * 3], deg);
    }
    // ---- oil_saturation: one vividness knob, whatever fed the palette ----
    if (fabsf(a.oilSaturation - 1.0f) > 0.001f) {
        for (int ci = 0; ci < 4; ci++) {
            float h, s, v;
            RgbToHsv(&effOil[ci * 3], h, s, v);
            RGB c = HSVtoRGB(h, fminf(fmaxf(s * a.oilSaturation, 0.0f), 1.0f), v);
            effOil[ci * 3 + 0] = c.r; effOil[ci * 3 + 1] = c.g; effOil[ci * 3 + 2] = c.b;
        }
    }

    AcidBlobGPU* dst = (AcidBlobGPU*)m_acidBlobData[fi];
    const int n = (int)m_acidBlobs.size();
    for (int i = 0; i < n && i < kAcidMaxBlobs; i++) {
        const AcidBlob& b = m_acidBlobs[i];
        dst[i].a[0] = b.x;
        dst[i].a[1] = b.y;
        dst[i].a[2] = b.baseR * (1.0f + a.breathAmt * sinf(b.phase));
        dst[i].a[3] = b.wgt;
        const int ci = (b.colIdx < 0 || b.colIdx > 3) ? 0 : b.colIdx;
        dst[i].b[0] = effOil[ci * 3 + 0];
        dst[i].b[1] = effOil[ci * 3 + 1];
        dst[i].b[2] = effOil[ci * 3 + 2];
        // .w = per-blob anisotropy for rise_stretch (0 = round, the shipped
        // look). A rising drop in a lamp is a teardrop while it is moving and
        // relaxes round as it slows, and a small one is dragged out far more
        // than a heavy disc -- so the amount is speed relative to the rise
        // speed, scaled by how small the blob is. Capped: past ~0.8 the
        // metaball stops reading as a blob and starts reading as a smear.
        float stretch = 0.0f;
        if (a.riseStretch > 0.0005f && a.riseSpeed > 1e-6f) {
            const float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
            const float sf = fminf(sp / (a.riseSpeed * 1.6f), 1.0f);
            const float rf2 = fminf(fmaxf(0.075f / fmaxf(b.baseR, 0.012f), 0.25f), 1.6f);
            stretch = fminf(a.riseStretch * sf * rf2, 0.80f);
        }
        dst[i].b[3] = stretch;
        // rise_parallax_dim: a far (small) blob is slightly dimmer, its colour
        // pulled toward the background as a depth cue. Same s as the motion
        // scale in StepAcidBlobs, so the two cues agree.
        if (a.riseParallaxDim > 1e-4f && a.riseParallax > 1e-4f) {
            float rr = b.baseR / fmaxf(a.discMax, 1e-4f);
            rr = rr < 0.25f ? 0.25f : (rr > 1.0f ? 1.0f : rr);
            const float s = 1.0f + (rr - 1.0f) * fminf(a.riseParallax, 1.0f);
            const float dim = 1.0f - (1.0f - s) * fminf(a.riseParallaxDim, 1.0f);
            dst[i].b[0] *= dim; dst[i].b[1] *= dim; dst[i].b[2] *= dim;
        }
        // mouse_oil_mode = 2 (comb): stretch amount + direction. Zero unless
        // the comb is running, and the shader then skips the whole branch.
        dst[i].c[0] = b.comb;
        dst[i].c[1] = b.cdx;
        dst[i].c[2] = b.cdy;
        dst[i].c[3] = 0.0f;
    }

    AcidParamsGPU p = {};
    for (int i = 0; i < 4; i++) {
        p.oil[i][0] = effOil[i * 3 + 0];
        p.oil[i][1] = effOil[i * 3 + 1];
        p.oil[i][2] = effOil[i * 3 + 2];
        p.ink[i][0] = effInk[i * 3 + 0];
        p.ink[i][1] = effInk[i * 3 + 1];
        p.ink[i][2] = effInk[i * 3 + 2];
    }
    const float aspect = (float)m_width / fmaxf((float)m_height, 1.0f);
    float p0[4] = { (float)(n < kAcidMaxBlobs ? n : kAcidMaxBlobs),
                    a.threshold, a.supportScale, a.aaScale };
    float p1[4] = { a.rimWidth, a.rimInset, a.rimDark, a.refraction };
    float p2[4] = { a.meniscus, a.meniscusW, a.translucency, a.oilTexture };
    float p3[4] = { a.inkLevels, a.inkSoft, a.inkMix, a.inkHueVary };
    float p4[4] = { a.inkGain, a.inkBias, a.seamStrength, a.seamScale };
    float p5[4] = { a.seamLo, a.seamHi, a.grainAmt, a.grainScale };
    float p6[4] = { a.speckle, a.speckScale, m_time, aspect };
    float p7[4] = { a.oilHdr, a.rimHdr, a.meniscusOff, a.inkShading };
    // With the droplet particle sim on, the procedural swarms are forced OFF:
    // ONE system is the whole point (the user, on the two layered together:
    // "it still looks like 2 things layered. The dots look png'd on").
    const bool dropsOn = (a.droplets > 0);
    float p8[4] = { dropsOn ? 0.0f : a.swarmHoles, dropsOn ? 0.0f : a.swarmDrops,
                    a.swarmDensity, a.swarmRimDark };
    float p9[4] = { a.swarmScaleA, a.swarmScaleB, a.swarmRMin, a.swarmRMax };
    memcpy(p.p0, p0, 16); memcpy(p.p1, p1, 16); memcpy(p.p2, p2, 16); memcpy(p.p3, p3, 16);
    memcpy(p.p4, p4, 16); memcpy(p.p5, p5, 16); memcpy(p.p6, p6, 16); memcpy(p.p7, p7, 16);
    float p10[4] = { a.swarmClump, a.swarmDark, (a.inkMode == 1 ? 1.0f : 0.0f),
                     fmaxf(a.toeTint, 0.0f) };
    // Target ink hue = the oil family's mean hue, swept, plus 180 degrees.
    float targetHue = fmodf(OilMeanHueDeg(effOil) + 180.0f, 360.0f);
    float p11[4] = { a.inkComplementLock ? 1.0f : 0.0f, a.inkComplementSpan,
                     targetHue, 0.0f };   // .w unused: the sweep is applied above
    float p12[4] = { a.rimVary, a.rimInkFollow, a.rimOrder ? 1.0f : 0.0f,
                     fmaxf(a.grainShadowW, 0.0f) };
    float p13[4] = { fmaxf(a.oilThinEdge, 0.0f), fmaxf(a.oilEdgeFrac, 0.0f),
                     fmaxf(a.oilSpecular, 0.0f), fmaxf(a.oilIrid, 0.0f) };
    float p14[4] = { fmaxf(a.swarmLens, 0.0f), fmaxf(a.meniscusFromInk, 0.0f),
                     fmaxf(a.oilGlow, 0.0f), fmaxf(a.refractionWidth, 0.0f) };
    float men[4] = { effMen[0], effMen[1], effMen[2], 0.0f };
    memcpy(p.p8, p8, 16); memcpy(p.p9, p9, 16);
    memcpy(p.p10, p10, 16); memcpy(p.p11, p11, 16); memcpy(p.p12, p12, 16);
    float p15[4] = { fmaxf(a.oilTransparency, 0.0f), fmaxf(a.oilAbsorb, 0.0f),
                     fmaxf(a.oilFilmBump, 0.0f), fmaxf(a.oilRefractBody, 0.0f) };
    float p16[4] = { fmaxf(a.oilInkBlur, 0.0f), 0.0f, 0.0f, 0.0f };
    float p17[4] = { fmaxf(a.riseBottomLight, 0.0f), fmaxf(a.postChroma, 0.0f),
                     fmaxf(a.postLift, 0.0f), 0.0f };
    float p18[4] = { dropsOn ? 1.0f : 0.0f, (float)kDropGridW, (float)kDropGridH,
                     (a.oilEdgeMode == 1) ? 1.0f : 0.0f };
    float p19[4] = { fmaxf(a.dropletSupport, 0.5f), fmaxf(a.dropletWeight, 0.0f),
                     fmaxf(a.dropletOilW, 0.0f), 0.0f };
    memcpy(p.p13, p13, 16); memcpy(p.p14, p14, 16);
    memcpy(p.p15, p15, 16); memcpy(p.p16, p16, 16); memcpy(p.p17, p17, 16);
    memcpy(p.p18, p18, 16); memcpy(p.p19, p19, 16);
    memcpy(p.men, men, 16);
    memcpy(m_acidParamData[fi], &p, sizeof(p));

    // ---- droplet particle buffers ---------------------------------------
    // Sorted by grid cell, with a (first, count) table per cell, so the pixel
    // shader walks only the 3x3 cells around it. Both buffers stay all-zero
    // when the sim is off, which is a count of 0 in every cell.
    if (m_dropletData[fi] && m_dropletCellData[fi]) {
        AcidDropGPU* dd = (AcidDropGPU*)m_dropletData[fi];
        DropCellGPU* dc = (DropCellGPU*)m_dropletCellData[fi];
        const int NC = kDropGridW * kDropGridH;
        if (!dropsOn || m_dropletOrder.empty()) {
            memset(dc, 0, sizeof(DropCellGPU) * NC);
        } else {
            const int total = (int)m_dropletOrder.size();
            for (int k = 0; k < total && k < kAcidMaxDrops; k++) {
                const AcidDrop& d = m_acidDrops[m_dropletOrder[k]];
                dd[k].a[0] = d.x;
                dd[k].a[1] = d.y;
                // SIGN carries the kind: negative = a hole in the oil.
                dd[k].a[2] = (d.kind == 0) ? -d.r : d.r;
                dd[k].a[3] = d.gate;   // contribution scale (see StepAcidDroplets)
            }
            for (int c = 0; c < NC; c++) {
                int first = m_dropletCellStart[c], cnt = m_dropletCellCount[c];
                if (first >= kAcidMaxDrops) { first = 0; cnt = 0; }
                else if (first + cnt > kAcidMaxDrops) cnt = kAcidMaxDrops - first;
                dc[c].first = (uint32_t)first;
                dc[c].count = (uint32_t)(cnt < 0 ? 0 : cnt);
            }
        }
    }
}

void FluidRenderer::BindAcid() {
    const UINT fi = m_frameIndex;
    if (m_acidParamUpload[fi])
        m_cmd->SetGraphicsRootConstantBufferView(2, m_acidParamUpload[fi]->GetGPUVirtualAddress());
    if (m_acidBlobUpload[fi])
        m_cmd->SetGraphicsRootShaderResourceView(3, m_acidBlobUpload[fi]->GetGPUVirtualAddress());
    if (m_dropletUpload[fi])
        m_cmd->SetGraphicsRootShaderResourceView(7, m_dropletUpload[fi]->GetGPUVirtualAddress());
    if (m_dropletCellUpload[fi])
        m_cmd->SetGraphicsRootShaderResourceView(8, m_dropletCellUpload[fi]->GetGPUVirtualAddress());
}

// ===========================================================================
// "Ink in water" — the SHARED render block's constants, and the drop emitter.
// Both are used by style=ink and (the constants) by liquid_acid ink_mode=water.
// ===========================================================================

void FluidRenderer::UploadInkConstants() {
    const InkConfig& k = m_cfg.ink;
    const UINT fi = m_frameIndex;
    if (!m_inkParamData[fi]) return;
    InkParamsGPU p = {};
    const float p0[4] = { fmaxf(k.density, 0.0f), fmaxf(k.chroma, 0.0f),
                          k.edgeStrength, fmaxf(k.edgeScale, 0.25f) };
    const float p1[4] = { k.edgeLo, k.edgeHi, k.inverted ? 1.0f : 0.0f, k.vignette };
    const float p2[4] = { k.parallax, k.parallaxScale, k.parallaxDrift, m_time };
    const float p3[4] = { k.coreKnee, k.hdrCore, k.motionLo, k.motionHi };
    const float p4[4] = { k.motionOpacity, k.veilFloor, k.tintMidDip, 0.0f };
    memcpy(p.p0, p0, 16); memcpy(p.p1, p1, 16);
    memcpy(p.p2, p2, 16); memcpy(p.p3, p3, 16); memcpy(p.p4, p4, 16);

    // ---- duotone PAIR ROTATION ([ink] pair_sweep_period) -----------------
    // With the sweep off these are just the authored tints. With it on, the
    // pair cross-fades through the SAME curated complementary anchors the
    // liquid_acid palette sweep uses, with the same hue-preserving HSV lerp
    // and the same smoothstepped fade (so each pair gets a long settled
    // stretch). The ink anchor drives the THIN veil and the oil anchor the
    // THICK core: the ink anchors are the darker, cooler half of every pair,
    // which keeps veils darker than cores — the assignment the user's
    // hand-picked duotone inis already use (teal veil / vermillion core).
    float tThin[3], tThick[3];
    memcpy(tThin,  k.tintThin,  sizeof(tThin));
    memcpy(tThick, k.tintThick, sizeof(tThick));
    if (k.pairSweepPeriod > 0.01f) {
        const LiquidAcidConfig& a = m_cfg.acid;   // the curated pair list
        int np = a.sweepCount;                    // sweep_count, default 5
        if (np < 1) np = 1;
        if (np > LiquidAcidConfig::kSweepMax) np = LiquidAcidConfig::kSweepMax;
        const float u = fmodf(m_time / k.pairSweepPeriod, 1.0f) * np;
        const int k0 = (int)u % np, k1 = (k0 + 1) % np;
        float f = u - floorf(u);
        f = f * f * (3.0f - 2.0f * f);
        HsvLerp3(&a.sweepInk[k0 * 3], &a.sweepInk[k1 * 3], f, tThin);
        HsvLerp3(&a.sweepOil[k0 * 3], &a.sweepOil[k1 * 3], f, tThick);
    }
    for (int i = 0; i < 3; i++) {
        p.paper[i]     = k.paper[i];
        p.tintThin[i]  = tThin[i];
        p.tintThick[i] = tThick[i];
    }
    memcpy(m_inkParamData[fi], &p, sizeof(p));
}

void FluidRenderer::BindInk() {
    const UINT fi = m_frameIndex;
    if (m_inkParamUpload[fi])
        m_cmd->SetGraphicsRootConstantBufferView(4, m_inkParamUpload[fi]->GetGPUVirtualAddress());
    if (m_velLow.res) {
        Transition(m_velLow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cmd->SetGraphicsRootDescriptorTable(5, m_velLow.srv);
    }
}

void FluidRenderer::InjectDrop(float x, float y, float vx, float vy,
                               float radiusPct, float density,
                               const float* rgb, int spatter) {
    const DropConfig& d = m_cfg.drops;
    if (vy < 0.0f)        vy = d.speed;
    if (radiusPct < 0.0f) radiusPct = d.radius;
    if (density < 0.0f)   density = d.density;
    if (spatter < 0)      spatter = d.spatter;
    if (spatter > 12)     spatter = 12;

    float col[3];
    if (rgb) { col[0] = rgb[0]; col[1] = rgb[1]; col[2] = rgb[2]; }
    else if (d.colorMode == 1) PickSplatColor(0.0f, col);
    else { col[0] = d.color[0]; col[1] = d.color[1]; col[2] = d.color[2]; }

    // The head. One Gaussian velocity impulse pointing down: after the
    // pressure projection it is a vortex dipole, and that dipole is what rolls
    // the head into the mushroom cap in ref 1. The dye cap is raised to the
    // drop density so a 1.35 core is not clipped by a lower max_brightness.
    const float cap = fmaxf(density, m_cfg.maxBrightness);
    const float asym = d.asymmetry;
    if (asym > 0.01f) {
        // Two unequal, off-centre velocity impulses around ONE dye stamp. The
        // velocity grid is 256 wide, so this is nearly free; a second dye stamp
        // at 4096 would not be. The result is an unequal vortex pair with one
        // lobe leading, like the references, instead of the textbook
        // mirror-symmetric one a single Gaussian impulse produces.
        const float side = (RandF() < 0.5f) ? -1.0f : 1.0f;
        const float off  = 0.5f * asym * radiusPct * 0.01f * (float)m_width;
        const float lead = 1.0f + 0.5f * asym * side;
        const float sp   = fmaxf(d.impulseSpread, 0.25f);
        const float vr   = radiusPct * sp * sp;   // radius is a squared scale
        SplatVelocity(x - off, y, vx - 0.25f * asym * vy * side, vy * lead,
                      vr * (1.0f - 0.25f * asym));
        SplatVelocity(x + off, y, vx + 0.25f * asym * vy * side, vy * (2.0f - lead),
                      vr * (1.0f + 0.25f * asym));
        SplatDye(x, y, col[0] * density, col[1] * density, col[2] * density,
                 radiusPct, cap, true);
    } else {
        const float sp = fmaxf(d.impulseSpread, 0.25f);
        SplatVelocity(x, y, vx, vy, radiusPct * sp * sp);
        SplatDye(x, y, col[0] * density, col[1] * density, col[2] * density,
                 radiusPct, cap, true);
    }

    // Splash satellites (ref 2). Not 2D physics — a fake, and a cheap one:
    // each is a full dye-res pass, so the count is capped at 12.
    for (int i = 0; i < spatter; i++) {
        const float ang = RandF() * 6.2831853f;
        const float rr  = d.spatterSpread * (0.35f + 0.65f * RandF());
        const float sx  = x + cosf(ang) * rr * (float)m_width;
        const float sy  = y + sinf(ang) * rr * (float)m_height;
        Splat(sx, sy, cosf(ang) * d.spatterSpeed * 0.35f,
              sinf(ang) * d.spatterSpeed * 0.35f + d.spatterSpeed * 0.5f,
              col[0] * density, col[1] * density, col[2] * density,
              d.spatterRadius, cap);
    }

    // The thin trail hanging from the entry point: dye only, no velocity.
    m_dropTailLeft = d.tailSec;
    m_dropTailX = x; m_dropTailY = y;
    m_dropTailCol[0] = col[0]; m_dropTailCol[1] = col[1]; m_dropTailCol[2] = col[2];
}

void FluidRenderer::UpdateDrops(float dt) {
    const DropConfig& d = m_cfg.drops;
    if (m_dropQueued) {              // QueueDrop() from outside the render loop
        m_dropQueued = false;
        InjectDrop(m_dropQx, m_dropQy, 0.0f, m_dropQvy);
    }
    // The entry trail keeps painting even after the emitter is switched off,
    // so a half-finished drop is never truncated.
    if (m_dropTailLeft > 0.0f) {
        const float amt = d.tailDensity * m_emitScale;
        const float tr = fmaxf(d.radius * d.tailRadiusFrac, 0.01f);
        // fps-normalised: this runs every frame for tail_sec, so an un-scaled
        // impulse would add ~86 kicks at 144 fps and fire the plume into the floor
        if (d.tailSpeed > 0.001f)
            SplatVelocity(m_dropTailX, m_dropTailY, 0.0f,
                          d.speed * d.tailSpeed * m_emitScale * 0.1f, tr * 4.0f);
        SplatDye(m_dropTailX, m_dropTailY,
                 m_dropTailCol[0] * amt, m_dropTailCol[1] * amt, m_dropTailCol[2] * amt,
                 tr, fmaxf(d.density, m_cfg.maxBrightness), true);
        m_dropTailLeft -= dt;
    }
    if (!d.enabled) return;
    if (!m_dropPrimed) {
        // Seeded from RandF() AFTER InitWanderers, so --seed replay is stable
        // and existing seeded sequences are not shifted.
        m_dropTimer = d.interval * (0.3f + 0.7f * RandF());
        m_dropPrimed = true;
    }
    m_dropTimer -= dt;
    if (m_dropTimer > 0.0f) return;
    m_dropTimer = fmaxf(d.interval * (1.0f + (RandF() - 0.5f) * 0.7f), 0.5f);
    if (d.obeyGovernor && m_screenTooFull) return;   // water already full of ink
    const float x = (d.xMin + (d.xMax - d.xMin) * RandF()) * (float)m_width;
    const float y = (d.yMin + (d.yMax - d.yMin) * RandF()) * (float)m_height;
    InjectDrop(x, y, (RandF() - 0.5f) * 120.0f, d.speed);
}

void FluidRenderer::UpdateCoverage() {
    if ((!m_cfg.autoPause || !m_cfg.wanderers) && !m_coverageWanted) {
        m_screenTooFull = false;
        m_survivorTooFull = false;
        return;
    }
    // Headless capture: the harvest must land on a FIXED frame, not "whenever
    // the GPU happened to finish" — otherwise the governor flips a frame or
    // two earlier/later between runs and the images diverge. Pin it to the
    // same 1 Hz cadence as the issue, blocking if the copy isn't done yet.
    if (m_headless && m_covPending && m_time - m_lastCovTime >= 1.0f &&
        m_fence->GetCompletedValue() < m_covFence) {
        HR(m_fence->SetEventOnCompletion(m_covFence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    // harvest a finished readback (issued a frame or more ago; 1 Hz cadence
    // makes the staleness irrelevant and avoids any GPU sync)
    if (m_covPending && m_fence->GetCompletedValue() >= m_covFence &&
        (!m_headless || m_time - m_lastCovTime >= 1.0f)) {
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
    float hueX = 0, hueY = 0, hueW = 0;   // brightness-weighted circular mean

    for (int y = 0; y < kCovH; y++) {
        const uint16_t* row = (const uint16_t*)(data + (SIZE_T)y * pitch);
        for (int x = 0; x < kCovW; x++) {
            float r = HalfToFloat(row[x * 4 + 0]);
            float g = HalfToFloat(row[x * 4 + 1]);
            float b = HalfToFloat(row[x * 4 + 2]);
            float m = fmaxf(r, fmaxf(g, b));
            if (m <= m_cfg.darkLevel) dark++;
            else {
                float mn = fminf(r, fminf(g, b));
                float d = m - mn;
                if (d > 1e-5f) {
                    float h;
                    if (m == r) h = fmodf((g - b) / d, 6.0f);
                    else if (m == g) h = (b - r) / d + 2.0f;
                    else h = (r - g) / d + 4.0f;
                    float rad = h * 60.0f * (3.14159265f / 180.0f);
                    hueX += cosf(rad) * m;
                    hueY += sinf(rad) * m;
                    hueW += m;
                }
            }
            int ti = (int)fminf(TX - 1.0f, x / tw) + TX * (int)fminf(TY - 1.0f, y / th);
            tiles[ti] += m;
        }
    }
    float darkPct = 100.0f * dark / total;
    m_darkPct = darkPct;
    if (hueW > 0.001f) {
        float ang = atan2f(hueY, hueX) * 180.0f / 3.14159265f;
        m_avgHue = fmodf(ang + 720.0f, 360.0f);
    }

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
