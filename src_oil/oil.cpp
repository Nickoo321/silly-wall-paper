// OilWallpaper — lava-lamp oil-blob renderer (proof of concept).
// D3D12 device + (swapchain | offscreen RT) + ONE fullscreen-triangle pipeline.
//
// The whole look lives in the pixel shader: a 64-blob metaball field with
// dominant-colour selection, backdrop refraction, a Beer-Lambert thickness
// tint, a thin hot rim, meniscus-confined specular and blob-advected marbling.
// The CPU sim (StepBlobs) drifts the blobs through a pseudo-curl field and
// recirculates them with a rise-cool-sink temperature rule plus soft
// repulsion / cohesion.
//
// Everything Phase 0 changed is gated on OilConfig::lookStep so one build can
// shoot every intermediate state (see oil.h).

#include "oil.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cmath>
#include <random>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Headless (--shot) runs must never pop a modal dialog: a message box would
// block the capture forever and steal focus from whatever the user is doing.
static bool g_noDialogs = false;

static void HR(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        fprintf(stderr, "FATAL: %s failed (hr=0x%08lX)\n", what, (unsigned long)hr);
        if (!g_noDialogs)
            MessageBoxA(nullptr, what, "Oil Wallpaper - fatal error", MB_ICONERROR);
        ExitProcess(1);
    }
}

// ---------------------------------------------------------------------------
// Per-frame constant buffer — keep in sync with cbuffer CB in kShaderSrc.
// HLSL packs consecutive scalars four to a float4 row, so the scalar tail is
// written in explicit rows of four.
// ---------------------------------------------------------------------------
struct OilCBReal {
    float blobs[64][4];      // xy = pos uv, z = radius (y units), w = colorIndex
    float aux[64][4];        // xy = texture travel (p units), z = temp, w = pad
    float bg[4];
    float bgAccent[4];
    float blobCol[3][4];
    float rimCol[4];
    float time, aspect, marbleScale, rimWidth;
    float domSharp, refractK, absorbK, scatterK;
    float thickPow, thickScale, thickMax, marbleAmt;
    float marbleBright, domJitter, pad1, pad2;
    float glossTilt, glossFalloff, glossDiff, specAmt;
    float specPower, rimGain, grainAmt, bgBlotchAmt;
    float cutoff, clampW, menInner, menOuter;
    float throughGain, bgGamma, lookStep, pad0;
};
static_assert(sizeof(OilCBReal) == 64 * 16 + 64 * 16 + 2 * 16 + 3 * 16 + 16 + 8 * 16,
              "CB layout");

// ---------------------------------------------------------------------------
// Shader: runtime-compiled, same pattern as src/shaders.h in the fluid app.
// ---------------------------------------------------------------------------
static const char* kShaderSrc = R"hlsl(
cbuffer CB : register(b0) {
    float4 blobs[64];      // xy pos uv (y down), z radius, w colorIndex
    float4 aux[64];        // xy texture travel, z temp
    float4 bg;
    float4 bgAccent;
    float4 blobCol[3];
    float4 rimCol;
    float  time;
    float  aspect;
    float  marbleScale;
    float  rimWidth;
    float  domSharp;
    float  refractK;
    float  absorbK;
    float  scatterK;
    float  thickPow;
    float  thickScale;
    float  thickMax;
    float  marbleAmt;
    float  marbleBright;
    float  domJitter;
    float  pad1;
    float  pad2;
    float  glossTilt;
    float  glossFalloff;
    float  glossDiff;
    float  specAmt;
    float  specPower;
    float  rimGain;
    float  grainAmt;
    float  bgBlotchAmt;
    float  cutoff;
    float  clampW;
    float  menInner;
    float  menOuter;
    float  throughGain;
    float  bgGamma;
    float  lookStep;
    float  pad0;
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

// Backdrop as a function of position so the blobs can refract it: sampling it
// at a displaced coordinate is what turns "painted plastic" into "lens".
float3 Backdrop(float2 uv, float2 p) {
    float blotch = fbm(p * 2.6 + float2(time * 0.008, -time * 0.005));
    float3 c = lerp(bg.rgb, bgAccent.rgb, pow(saturate(uv.y), bgGamma));
    c *= (1.0 - bgBlotchAmt) + 2.0 * bgBlotchAmt * blotch;
    return c;
}

float4 PSMain(VSOut inp) : SV_Target {
    float2 p = float2(inp.uv.x * aspect, inp.uv.y);
    float  st = lookStep;

    // -- metaball field, analytic gradient, per-colour weights, texture offset
    float  field  = 0.0;
    float2 grad   = float2(0.0, 0.0);
    float3 colSum = float3(0.0, 0.0, 0.0);   // legacy influence-weighted mean
    float3 cw     = float3(0.0, 0.0, 0.0);   // per-colour weight (dominant pick)
    float2 offSum = float2(0.0, 0.0);
    [loop]
    for (int b = 0; b < 64; b++) {
        float4 bl = blobs[b];
        float2 q  = p - float2(bl.x * aspect, bl.y);
        float  d2 = dot(q, q) + 1e-6;
        float  r2 = bl.z * bl.z;
        float  wr = r2 / d2 - cutoff;           // compact tail cutoff
        float  w  = clamp(wr, 0.0, clampW);     // per-blob influence clamp
        field  += w;

        // STEP 1 — gradient guard: only accumulate d(w)/dp where the weight is
        // UNclamped. The clamp flattens the field but -2r^2/d^4 keeps growing
        // toward blob centres, which drags sdf back to ~0 inside merged masses
        // and paints a crescent seam around every sub-blob.
        float2 gq = (-2.0 * r2 / (d2 * d2)) * q;
        if (st >= 1.0) { if (wr > 0.0 && wr < clampW) grad += gq; }
        else           { grad += gq; }

        uint ci = (uint)(bl.w + 0.5);
        colSum += blobCol[ci].rgb * w;
        if (ci == 0)      cw.x += w;
        else if (ci == 1) cw.y += w;
        else              cw.z += w;
        offSum += aux[b].xy * w;
    }

    float  gl  = length(grad) + 1e-5;
    float  aa  = fwidth(field) * 1.5 + 1e-4;
    float  cov = smoothstep(1.0 - aa, 1.0 + aa, field);
    // signed-distance estimate in uv units, > 0 inside. Diverges where
    // |grad| -> 0 (blob cores), so it is only used for narrow edge bands.
    float  sdf = (field - 1.0) / gl;

    float2 nxy  = -grad / gl;                                   // outward
    float  edgeT= exp(-glossFalloff * max(field - 1.0, 0.0));   // 1 at rim, 0 deep
    float3 bgc  = Backdrop(inp.uv, p);

    float3 col = bgc;
    if (field > 0.5) {
        // marbling — STEP 5 samples it in blob-local space (p minus the
        // influence-weighted travel of the blobs under this pixel) so the
        // texture moves WITH the liquid instead of the liquid sliding
        // through a screen-locked pattern.
        float2 mp    = (st >= 5.0) ? (p - offSum / max(field, 1e-4)) : p;
        float  mdrift= (st >= 5.0) ? 0.15 : 1.0;
        float  m     = fbm(mp * marbleScale +
                           float2(time * 0.020, -time * 0.013) * mdrift);

        // -- STEP 2: dominant colour instead of the influence-weighted mean.
        // exp() softmax on the per-colour weights, normalised by the winner,
        // so ties blend and everything else is rejected — no milky cross-bleed.
        // The weights are jittered by the marbling noise (step 5 onward) so
        // the dye boundaries wisp instead of drawing perfect circular arcs.
        float3 dye;
        if (st >= 2.0) {
            float3 cwj = (st >= 5.0) ? cw * (1.0 + (m - 0.5) * domJitter) : cw;
            float  mx  = max(cwj.x, max(cwj.y, cwj.z));
            float3 e   = exp((cwj - mx) * (domSharp / max(mx, 1e-4)));
            e /= (e.x + e.y + e.z);
            dye = e.x * blobCol[0].rgb + e.y * blobCol[1].rgb + e.z * blobCol[2].rgb;
        } else {
            dye = colSum / max(field, 1e-4);
        }

        // -- STEP 3: refraction. Displace the backdrop sample along the
        // outward surface normal, strongest at the rim where the surface is
        // steep — this is what turns painted plastic into a liquid lens.
        float3 bgR = bgc;
        if (st >= 3.0) {
            float2 disp = nxy * refractK * edgeT;
            bgR = Backdrop(inp.uv + float2(disp.x / aspect, disp.y), p + disp);
        }

        float3 bcol;
        float  men = 0.0;
        if (st >= 4.0) {
            // -- STEP 4: Beer-Lambert body.
            // thickness from the field; sigma chosen so one unit of thickness
            // transmits exactly the dye colour. Thin edges stay translucent
            // (you see the refracted backdrop), deep cores go saturated.
            // saturating thickness curve (not a hard clamp): every sub-blob's
            // field bump flattens out once the mass is deep, so a merged body
            // stops reading as a pile of separate lenses
            float thick = thickMax * (1.0 - exp(-thickScale * pow(max(field - 1.0, 0.0), thickPow)));
            if (st >= 5.0) thick *= 1.0 + (m - 0.5) * marbleAmt;
            float3 sigma   = -log(saturate(dye) + 0.012) * absorbK;
            float3 T       = exp(-sigma * thick);
            float3 through = bgR * T * throughGain;
            float3 scat    = dye * (1.0 - exp(-thick * scatterK));
            bcol = through + scat;
            // brightness veining on top of the density veining — the
            // saturating scatter term alone swallows most of the modulation
            if (st >= 5.0) bcol *= 1.0 + (m - 0.5) * marbleBright;
            else           bcol *= 1.0 + (m - 0.5) * marbleAmt;

            // flat gloss: the tilt dies fast with depth so interiors read flat
            float  tilt = glossTilt * edgeT;
            float3 n    = normalize(float3(nxy * tilt, 1.0));
            float3 l    = normalize(float3(-0.45, -0.55, 0.7071));
            float  diff = saturate(dot(n, l));
            float  spec = pow(saturate(dot(n, normalize(l + float3(0, 0, 1)))), specPower);
            // meniscus: a thin band just inside the edge, the only place a
            // real oil surface shows a highlight
            men  = smoothstep(0.0, menInner, sdf) * (1.0 - smoothstep(menInner, menOuter, sdf));
            bcol *= (1.0 - glossDiff) + glossDiff * diff;
            bcol += spec * specAmt * men;
        } else {
            // pre-Phase-0 shading: flat mean colour, constant-width dark
            // outline, broad plastic gloss over the whole blob
            bcol = dye * (1.0 + (m - 0.5) * 0.30);
            float rimOld = max(smoothstep(0.0, rimWidth, sdf), smoothstep(1.0, 2.4, field));
            bcol *= lerp(0.38, 1.0, rimOld);
            bcol *= 1.0 + 0.10 * smoothstep(2.0, 5.0, field);
            float  tilt = glossTilt * exp(-0.45 * max(field - 1.0, 0.0));
            float3 n    = normalize(float3(nxy * tilt, 1.0));
            float3 l    = normalize(float3(-0.45, -0.55, 0.7071));
            float  diff = saturate(dot(n, l));
            float  spec = pow(saturate(dot(n, normalize(l + float3(0, 0, 1)))), 64.0);
            bcol *= 0.82 + 0.34 * diff;
            bcol += spec * 0.18 * rimOld;
            bcol  = saturate(bcol);
        }

        col = lerp(bgc, bcol, cov);

        // STEP 4 — thin hot rim at the field == 1 isoline (Gaussian on sdf so
        // it straddles the edge). Small area on purpose: the panel is ABL'd.
        if (st >= 4.0) {
            float rimLine = exp(-(sdf * sdf) / max(rimWidth * rimWidth, 1e-9));
            col = lerp(col, rimCol.rgb, saturate(rimLine * rimGain));
        }
    }

    // STEP 7 — animated output dither over the whole frame (pre-Phase-0 the
    // grain was a frozen screen-locked pattern on the backdrop only).
    if (st >= 7.0) col += (hash21(inp.pos.xy + frac(time) * 913.7) - 0.5) * grainAmt;
    else           col += (hash21(inp.pos.xy) - 0.5) * 0.028 * (1.0 - cov);

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
// kPalettes = the pre-Phase-0 pair, kept so lookStep 0..7 shows only the
// shader/sim change under test. kPalettesTuned = the fluorescent retune that
// lands with lookStep 8: near-black tinted backdrops (ABL headroom) and
// saturated dyes that survive the Beer-Lambert absorption.
// ---------------------------------------------------------------------------
struct OilPalette { float bg[3], bgAccent[3], col[3][3], rim[3]; };

static const OilPalette kPalettes[2] = {
    // Acid: orange backdrop -> dark brown; coral / teal / dark droplet
    { { 0.910f, 0.471f, 0.000f }, { 0.227f, 0.094f, 0.031f },
      { { 0.949f, 0.235f, 0.157f },    // #F23C28 coral red
        { 0.098f, 0.784f, 0.784f },    // #19C8C8 teal cyan
        { 0.165f, 0.071f, 0.024f } },  // #2A1206 dark droplet
      { 1.000f, 0.880f, 0.520f } },
    // Royal: deep purple backdrop -> violet; red-orange / dark red / dark bubble
    { { 0.165f, 0.039f, 0.333f }, { 0.416f, 0.059f, 0.659f },
      { { 0.949f, 0.188f, 0.059f },    // #F2300F red-orange
        { 0.627f, 0.071f, 0.020f },    // #A01205 darker red
        { 0.102f, 0.020f, 0.188f } },  // #1A0530 dark bubble
      { 1.000f, 0.600f, 0.180f } },
};

static const OilPalette kPalettesTuned[2] = {
    // Acid: near-black ink, fluorescent coral / spring-green / magenta
    { { 0.018f, 0.052f, 0.050f }, { 0.055f, 0.016f, 0.006f },
      { { 1.000f, 0.175f, 0.080f },    // fluorescent coral red
        { 1.000f, 0.430f, 0.030f },    // hot orange (same family as col 0, so a
                                       //  merged mass varies in tone, not hue)
        { 0.060f, 0.950f, 0.660f } },  // fluorescent spring cyan droplet
      { 1.000f, 0.940f, 0.680f } },    // hot white-gold rim
    // Royal: near-black violet, fluorescent orange / pink / electric violet
    { { 0.028f, 0.006f, 0.058f }, { 0.080f, 0.012f, 0.140f },
      { { 1.000f, 0.250f, 0.030f },    // fluorescent orange
        { 0.880f, 0.055f, 0.220f },    // deep fluorescent red (near family)
        { 0.340f, 0.100f, 0.960f } },  // electric violet droplet
      { 1.000f, 0.660f, 0.240f } },    // hot orange rim
};

// ---------------------------------------------------------------------------
// Device / swapchain / pipeline
// ---------------------------------------------------------------------------
void OilRenderer::Init(HWND hwnd, int width, int height, const OilConfig& cfg) {
    m_headless = false;
    m_cfg = cfg;
    m_width = width;
    m_height = height;
    CreateDevice();
    CreateSwapChainResources(hwnd, width, height);
    CreatePipeline();
    if (!m_seeded) { SeedBlobs(); m_seeded = true; }
    printf("Oil renderer ready: %dx%d, %d blobs, palette %d, lookStep %d\n",
           width, height, kBlobCount, m_cfg.palette, m_cfg.lookStep);
}

// Headless: no HWND is created, touched or attached anywhere in this path.
void OilRenderer::InitOffscreen(int width, int height, const OilConfig& cfg) {
    g_noDialogs = true;
    m_headless = true;
    m_cfg = cfg;
    m_width = width;
    m_height = height;
    CreateDevice();
    CreateOffscreenTarget();
    CreatePipeline();
    if (!m_seeded) { SeedBlobs(); m_seeded = true; }
    printf("Oil renderer ready (headless): %dx%d offscreen R8G8B8A8_UNORM, "
           "%d blobs, palette %d, lookStep %d\n",
           width, height, kBlobCount, m_cfg.palette, m_cfg.lookStep);
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
    m_queue->GetTimestampFrequency(&m_tsFreq);

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

    // GPU timing: two timestamps per frame slot, resolved into a readback
    // buffer and read once that slot's fence has retired.
    D3D12_QUERY_HEAP_DESC qh = {};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = kFrames * 2;
    HR(m_device->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_tsHeap)), "CreateQueryHeap");
    {
        D3D12_HEAP_PROPERTIES rb = { D3D12_HEAP_TYPE_READBACK };
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = kFrames * 2 * sizeof(UINT64);
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&m_tsReadback)), "CreateTSReadback");
    }
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

// Offscreen render target for --shot: one UNORM texture (identical format to
// the swap chain, so the captured pixels are what the wallpaper would show)
// plus a readback buffer. RTV slot 0 stands in for the back buffers.
void OilRenderer::CreateOffscreenTarget() {
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)m_width;
    rd.Height = (UINT)m_height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, m_shotState,
                                         nullptr, IID_PPV_ARGS(&m_shotTex)), "CreateShotTex");

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_shotTex.Get(), nullptr, rtv);

    m_shotPitch = ((UINT)m_width * 4 + 255) & ~255u;
    D3D12_HEAP_PROPERTIES rb = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)m_shotPitch * m_height;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&m_shotReadback)), "CreateShotReadback");
    m_frameIndex = 0;
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
    if (!m_device || m_headless) return;
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
    m_shotTex.Reset();
    m_shotReadback.Reset();
    m_tsHeap.Reset();
    m_tsReadback.Reset();
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
    std::mt19937 rng(m_cfg.seed ? m_cfg.seed : ((unsigned)GetTickCount() ^ 0x5EEDu));
    auto U = [&rng](float lo, float hi) {
        return lo + (hi - lo) * (float)rng() / (float)rng.max();
    };
    const float TWO_PI = 6.2831853f;
    const bool  tuned  = (m_cfg.lookStep >= 8);
    const int   nBig   = tuned ? m_cfg.bigCount : 35;
    const int   nMid   = tuned ? m_cfg.bigCount + m_cfg.midCount : 54;
    for (int i = 0; i < kBlobCount; i++) {
        BlobCPU& b = m_blobs[i];
        if (i < nBig)      { b.colorIdx = 0; b.baseR = tuned ? U(m_cfg.bigRMin, m_cfg.bigRMax)     : U(0.045f, 0.100f); }
        else if (i < nMid) { b.colorIdx = 1; b.baseR = tuned ? U(m_cfg.midRMin, m_cfg.midRMax)     : U(0.030f, 0.065f); }
        else               { b.colorIdx = 2; b.baseR = tuned ? U(m_cfg.smallRMin, m_cfg.smallRMax) : U(0.018f, 0.032f); }
        b.x = U(0.03f, 0.97f);
        // Recirculating sims fill the column; the pre-Phase-0 sim seeded the
        // lower 2/3 and let buoyancy sort everything to the top.
        b.y = (m_cfg.lookStep >= 6) ? U(0.10f, 0.92f) : U(0.34f, 0.97f);
        b.vx = b.vy = 0.0f;
        b.phase = U(0.0f, TWO_PI);
        b.breathRate = U(m_cfg.breathMin, m_cfg.breathMax);
        b.s1 = U(0.0f, TWO_PI);
        b.s2 = U(0.0f, TWO_PI);
        b.tx = b.ty = 0.0f;
        b.buoyScale = 1.0f + U(-m_cfg.buoyJitter, m_cfg.buoyJitter);
        b.zoneOff   = U(-m_cfg.zoneJitter, m_cfg.zoneJitter);
        // BIMODAL temperature: every blob is either clearly rising or clearly
        // sinking. Seeding mid-temperatures would start half the population
        // neutrally buoyant, and with no ambient relaxation they would never
        // leave the middle of the frame.
        b.temp = (U(0.0f, 1.0f) < 0.5f) ? U(0.78f, 1.0f) : U(0.0f, 0.22f);
        b.sinker = (U(0.0f, 1.0f) < 0.09f);   // a few fall instead of rising
    }
}

void OilRenderer::StepBlobs(float dt) {
    const float t = m_time;
    const float A = m_height ? (float)m_width / (float)m_height : 1.777f;
    const bool  newSim = (m_cfg.lookStep >= 6);

    // Pair forces (64 blobs -> 2016 pairs, negligible on the CPU): soft
    // repulsion inside touching distance so blobs deform instead of merging
    // into one plate, plus mild cohesion in the band just beyond touch so they
    // still form necks and clusters (surface tension).
    float fx[kBlobCount] = {}, fy[kBlobCount] = {};
    if (newSim) {
        for (int i = 0; i < kBlobCount; i++) {
            for (int j = i + 1; j < kBlobCount; j++) {
                const float dx = (m_blobs[j].x - m_blobs[i].x) * A;
                const float dy = (m_blobs[j].y - m_blobs[i].y);
                const float d  = sqrtf(dx * dx + dy * dy) + 1e-5f;
                const float R  = m_blobs[i].baseR + m_blobs[j].baseR;
                if (d > R * m_cfg.cohesionRange) continue;
                const float nx = dx / d, ny = dy / d;
                float f;
                if (d < R * m_cfg.repelRange) {
                    f = -m_cfg.repelK * (R * m_cfg.repelRange - d) / R;   // push apart
                } else {
                    const float span = R * (m_cfg.cohesionRange - m_cfg.repelRange) + 1e-5f;
                    const float u = (d - R * m_cfg.repelRange) / span;    // 0..1
                    f = m_cfg.cohesionK * 4.0f * u * (1.0f - u);          // bump, pulls together
                }
                fx[i] += nx * f; fy[i] += ny * f;
                fx[j] -= nx * f; fy[j] -= ny * f;
            }
        }
    }

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
        float tx = dpsidy * m_cfg.driftK;
        float ty = -dpsidx * m_cfg.driftK;

        if (newSim) {
            tx += fx[i] / A;                 // pair forces live in p space
            ty += fy[i];

            // Buoyancy from temperature, not radius: hot rises (uv y down),
            // cold sinks. buoyRadiusAmt keeps a trace of size sorting.
            const float rb = 1.0f + m_cfg.buoyRadiusAmt * ((b.baseR / 0.055f) - 1.0f);
            ty += -m_cfg.buoyK * (b.temp * 2.0f - 1.0f) * rb * b.buoyScale;

            // Soft walls instead of wrapping in y: the frame stays composed
            // and nothing welds itself to the top edge.
            if (b.y < m_cfg.yMin) ty += m_cfg.wallK * (m_cfg.yMin - b.y) / 0.1f;
            if (b.y > m_cfg.yMax) ty -= m_cfg.wallK * (b.y - m_cfg.yMax) / 0.1f;
        } else {
            const float buoyK = 0.012f;
            ty += (b.sinker ? 1.0f : -1.0f) * buoyK * (b.baseR / 0.10f);
        }

        // velocity relaxation — fps-normalised per-step decay
        float k = 1.0f - expf(-m_cfg.damping * dt);
        b.vx += (tx - b.vx) * k;
        b.vy += (ty - b.vy) * k;
        b.x += b.vx * dt;
        b.y += b.vy * dt;

        // texture travel, so the marbling rides along with the blob
        b.tx += b.vx * dt * A;
        b.ty += b.vy * dt;
        if (b.tx > 100.0f || b.tx < -100.0f) b.tx = fmodf(b.tx, 100.0f);
        if (b.ty > 100.0f || b.ty < -100.0f) b.ty = fmodf(b.ty, 100.0f);

        if (newSim) {
            // rise -> cool at the top -> sink -> reheat at the bottom
            const float zy = b.y - b.zoneOff;   // per-blob zone offset
            const float heat = (zy - m_cfg.heatY0) / (m_cfg.heatY1 - m_cfg.heatY0);
            const float cool = 1.0f - (zy - m_cfg.coolY0) / (m_cfg.coolY1 - m_cfg.coolY0);
            const float h = heat < 0.0f ? 0.0f : (heat > 1.0f ? 1.0f : heat);
            const float c = cool < 0.0f ? 0.0f : (cool > 1.0f ? 1.0f : cool);
            b.temp += (h * m_cfg.heatRate - c * m_cfg.coolRate) * dt;
            b.temp += (0.5f - b.temp) * (1.0f - expf(-m_cfg.tempRelax * dt));
            if (b.temp < 0.0f) b.temp = 0.0f;
            if (b.temp > 1.0f) b.temp = 1.0f;

            // wrap horizontally only; clamp vertically as a last resort
            const float m = 0.16f;
            if (b.x < -m)       b.x += 1.0f + 2.0f * m;
            if (b.x > 1.0f + m) b.x -= 1.0f + 2.0f * m;
            if (b.y < -0.04f) { b.y = -0.04f; b.vy = 0.0f; }
            if (b.y >  1.04f) { b.y =  1.04f; b.vy = 0.0f; }
        } else {
            const float m = 0.16f;
            if (b.x < -m)       b.x += 1.0f + 2.0f * m;
            if (b.x > 1.0f + m) b.x -= 1.0f + 2.0f * m;
            if (b.y < -m)       b.y += 1.0f + 2.0f * m;
            if (b.y > 1.0f + m) b.y -= 1.0f + 2.0f * m;
        }

        b.phase += b.breathRate * dt;
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void OilRenderer::Frame(float dt) {
    if ((!m_headless && m_presentBroken) || !m_device) return;
    m_time += dt;
    StepBlobs(dt);

    // fill + upload the per-frame constants
    OilCBReal cb = {};
    const OilPalette& pal = (m_cfg.lookStep >= 8) ? kPalettesTuned[m_cfg.palette & 1]
                                                  : kPalettes[m_cfg.palette & 1];
    for (int i = 0; i < kBlobCount; i++) {
        const BlobCPU& b = m_blobs[i];
        float r = b.baseR * (1.0f + m_cfg.breathAmt * sinf(b.phase));
        cb.blobs[i][0] = b.x;
        cb.blobs[i][1] = b.y;
        cb.blobs[i][2] = r;
        cb.blobs[i][3] = b.colorIdx;
        cb.aux[i][0] = b.tx;
        cb.aux[i][1] = b.ty;
        cb.aux[i][2] = b.temp;
        cb.aux[i][3] = 0.0f;
    }
    for (int c = 0; c < 3; c++) {
        cb.bg[c] = pal.bg[c];
        cb.bgAccent[c] = pal.bgAccent[c];
        cb.rimCol[c] = pal.rim[c];
        for (int j = 0; j < 3; j++) cb.blobCol[c][j] = pal.col[c][j];
    }
    const int st = m_cfg.lookStep;
    cb.time = m_time;
    cb.aspect = (float)m_width / (float)m_height;
    // pre-Phase-0 values for the steps that have not switched over yet, so
    // each row of the contact sheet isolates one change
    cb.marbleScale = (st >= 5) ? m_cfg.marbleScale : 70.0f;
    cb.rimWidth    = (st >= 4) ? m_cfg.rimWidth    : 0.022f;
    cb.domSharp    = m_cfg.domSharp;
    cb.refractK    = m_cfg.refractK;
    cb.absorbK     = m_cfg.absorbK;
    cb.scatterK    = m_cfg.scatterK;
    cb.thickPow    = m_cfg.thickPow;
    cb.thickScale  = m_cfg.thickScale;
    cb.thickMax    = m_cfg.thickMax;
    cb.marbleAmt   = m_cfg.marbleAmt;
    cb.marbleBright= m_cfg.marbleBright;
    cb.domJitter   = m_cfg.domJitter;
    cb.glossTilt   = m_cfg.glossTilt;
    cb.glossFalloff= m_cfg.glossFalloff;
    cb.glossDiff   = m_cfg.glossDiff;
    cb.specAmt     = m_cfg.specAmt;
    cb.specPower   = m_cfg.specPower;
    cb.rimGain     = m_cfg.rimGain;
    cb.grainAmt    = m_cfg.grainAmt;
    cb.bgBlotchAmt = m_cfg.bgBlotchAmt;
    cb.cutoff      = m_cfg.cutoff;
    cb.clampW      = m_cfg.clampW;
    cb.menInner    = m_cfg.menInner;
    cb.menOuter    = m_cfg.menOuter;
    cb.throughGain = m_cfg.throughGain;
    cb.bgGamma     = m_cfg.bgGamma;
    cb.lookStep    = (float)st;
    memcpy(m_cbData[m_frameIndex], &cb, sizeof(cb));

    // record
    const UINT i = m_frameIndex;
    if (m_fence->GetCompletedValue() < m_fenceValues[i]) {
        HR(m_fence->SetEventOnCompletion(m_fenceValues[i], m_fenceEvent), "SetEvent");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    // that slot's timestamps have retired — pick them up
    if (m_tsValid[i] && m_tsFreq) {
        UINT64* ts = nullptr;
        D3D12_RANGE rr = { i * 2 * sizeof(UINT64), (i * 2 + 2) * sizeof(UINT64) };
        if (SUCCEEDED(m_tsReadback->Map(0, &rr, (void**)&ts))) {
            const UINT64 t0 = ts[i * 2], t1 = ts[i * 2 + 1];
            if (t1 > t0) {
                m_gpuMsLast = 1000.0 * (double)(t1 - t0) / (double)m_tsFreq;
                m_gpuMsSum += m_gpuMsLast;
                m_gpuSamples++;
                if (m_gpuMsLast < m_gpuMsMin) m_gpuMsMin = m_gpuMsLast;
            }
            D3D12_RANGE none = { 0, 0 };
            m_tsReadback->Unmap(0, &none);
        }
        m_tsValid[i] = false;
    }
    HR(m_allocators[i]->Reset(), "AllocReset");
    HR(m_cmd->Reset(m_allocators[i].Get(), nullptr), "CmdReset");

    ID3D12Resource* target = m_headless ? m_shotTex.Get() : m_backBuffers[i].Get();
    D3D12_RESOURCE_BARRIER bar = {};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = target;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (m_headless) {
        if (m_shotState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            bar.Transition.StateBefore = m_shotState;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            m_cmd->ResourceBarrier(1, &bar);
            m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
    } else {
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_cmd->ResourceBarrier(1, &bar);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    if (!m_headless) rtv.ptr += (SIZE_T)i * m_rtvStride;   // headless: slot 0
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);
    m_cmd->SetGraphicsRootSignature(m_rootSig.Get());
    m_cmd->SetPipelineState(m_pso.Get());
    m_cmd->SetGraphicsRootConstantBufferView(0, m_cbUpload[i]->GetGPUVirtualAddress());
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i * 2);
    m_cmd->DrawInstanced(3, 1, 0, 0);
    m_cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i * 2 + 1);
    m_cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i * 2, 2,
                            m_tsReadback.Get(), i * 2 * sizeof(UINT64));
    m_tsValid[i] = true;

    if (!m_headless) {
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_cmd->ResourceBarrier(1, &bar);
    }

    HR(m_cmd->Close(), "CmdClose");
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    if (!m_headless) {
        // Present fails (not fatally) when Explorer tears our window down
        // mid-frame — flag it so the shell can rebuild instead of dying.
        HRESULT phr = m_swapChain->Present(1, 0);
        if (FAILED(phr)) {
            if (!m_presentBroken)
                printf("Present failed (0x%08lX) - wallpaper window lost, awaiting reattach\n",
                       (unsigned long)phr);
            m_presentBroken = true;
        }
    }
    m_fenceValues[m_frameIndex] = m_nextFence;
    HR(m_queue->Signal(m_fence.Get(), m_nextFence++), "Signal");
    m_frameIndex = m_headless ? ((m_frameIndex + 1) % kFrames)
                              : m_swapChain->GetCurrentBackBufferIndex();
}

// Copy the offscreen target back to the CPU as tightly packed RGBA8. Runs its
// own one-shot command list after a full GPU flush.
bool OilRenderer::CaptureOffscreen(std::vector<unsigned char>& out) {
    if (!m_headless || !m_shotTex || !m_shotReadback) return false;
    WaitForGpu();

    HR(m_allocators[0]->Reset(), "CapAllocReset");
    HR(m_cmd->Reset(m_allocators[0].Get(), nullptr), "CapCmdReset");

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
    src.SubresourceIndex = 0;
    dst.pResource = m_shotReadback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = (UINT)m_width;
    dst.PlacedFootprint.Footprint.Height = (UINT)m_height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = m_shotPitch;
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    HR(m_cmd->Close(), "CapCmdClose");
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    unsigned char* data = nullptr;
    D3D12_RANGE range = { 0, (SIZE_T)m_shotPitch * m_height };
    if (FAILED(m_shotReadback->Map(0, &range, (void**)&data))) return false;
    out.resize((size_t)m_width * m_height * 4);
    for (int y = 0; y < m_height; y++)
        memcpy(out.data() + (size_t)y * m_width * 4,
               data + (SIZE_T)y * m_shotPitch, (size_t)m_width * 4);
    D3D12_RANGE none = { 0, 0 };
    m_shotReadback->Unmap(0, &none);

    // allocator 0 was reset out of band — resync the frame ring
    for (UINT k = 0; k < kFrames; k++) { m_fenceValues[k] = 0; m_tsValid[k] = false; }
    m_frameIndex = 0;
    return true;
}
