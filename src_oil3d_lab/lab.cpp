#include "lab.h"
#include "shaders.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern void LabFail(const char* what, HRESULT hr);   // main.cpp
extern void LabLog(const char* fmt, ...);            // main.cpp
#define HR(e) do { HRESULT _hr = (e); if (FAILED(_hr)) LabFail(#e, _hr); } while (0)

static const float kPi = 3.14159265f;
static const float kAspect = 16.0f / 9.0f;   // sim spread; the render uses the shot's own aspect

// GPU blob, must match `struct Blob` in shaders.h (4 x float4)
struct BlobGPU { float a[4]; float b[4]; float c[4]; float d[4]; };
static_assert(sizeof(BlobGPU) == 64, "BlobGPU must be 64 bytes");

// the hardcoded live scheme: Scheme - Magenta Cyan.ini over monotone-post-0924
// (palette_start_hue 325 -> sweep_oil_2, the magenta set; film_hue2 -140 ->
// 185 cyan = sweep_oil_4; film_equal_load_patches 0.5 dims the cyan toward
// the magenta's luminance; the seam is violet-blue, the scheme's own note).
static const float kMain[4][3] = {
    {0.88f, 0.12f, 0.62f}, {0.85f, 0.10f, 0.55f}, {0.90f, 0.18f, 0.68f}, {0.80f, 0.08f, 0.50f}};
static const float kAccent[4][3] = {
    {0.10f, 0.80f, 0.95f}, {0.08f, 0.75f, 0.90f}, {0.12f, 0.85f, 1.00f}, {0.05f, 0.70f, 0.88f}};
static const float kSeam[3] = {0.36f, 0.16f, 1.00f};
static const float kInk[3]  = {0.45f, 0.20f, 1.00f};   // only ever at ink_level (~3%)

static float Luma(const float* c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
// chroma gain around luma, never below 0, then a level
static void ChromaK(const float* src, float chroma, float k, float* out) {
    const float y = Luma(src);
    for (int ch = 0; ch < 3; ch++) out[ch] = fmaxf(y + (src[ch] - y) * chroma, 0.0f) * k;
}

// Per-look quick post (approximate; the faithful 2D stack is ported separately):
// halation amount + tint, aberration, grain, vignette, DOF (amount, focus depth,
// range), blur tap spacing, and the far wall: backLit > 0 = the lit 2D film.
struct LookPost {
    float hal, tint[3], aberr, grain, vignette, dof, focus, range, spread, backLit;
};
static const LookPost kLookPost[9] = {
    // hal   tint                 aberr grain vign  dof  focus range spread backLit
    { 0.12f, {1.0f, 0.72f, 0.48f}, 1.0f, 0.10f, 0.50f, 0.0f, 0.3f, 0.2f, 1.5f, 0.00f },  // 0 round 1
    { 0.35f, {1.0f, 0.72f, 0.48f}, 0.8f, 0.10f, 0.55f, 0.0f, 0.3f, 0.2f, 1.8f, 0.00f },  // 1 lamp wax
    { 0.12f, {1.0f, 0.90f, 0.80f}, 1.2f, 0.08f, 0.50f, 1.0f, 0.30f, 0.22f, 2.2f, 0.30f }, // 2 macro oil
    { 0.18f, {1.0f, 0.90f, 0.80f}, 1.0f, 0.10f, 0.50f, 0.0f, 0.3f, 0.2f, 1.5f, 0.00f },  // 3 thin film
    { 0.22f, {1.0f, 0.90f, 0.80f}, 1.0f, 0.10f, 0.50f, 0.0f, 0.3f, 0.2f, 1.5f, 0.00f },  // 4 chrome
    { 1.00f, {1.0f, 1.00f, 1.00f}, 0.8f, 0.08f, 0.45f, 0.0f, 0.3f, 0.2f, 3.0f, 0.00f },  // 5 neon
    { 0.30f, {1.0f, 0.72f, 0.48f}, 0.8f, 0.10f, 0.50f, 0.0f, 0.3f, 0.2f, 2.0f, 0.00f },  // 6 gummy
    { 0.10f, {1.0f, 0.72f, 0.48f}, 1.0f, 0.12f, 0.55f, 0.0f, 0.3f, 0.2f, 1.5f, 0.50f },  // 7 inverted
    { 0.25f, {1.0f, 0.82f, 0.65f}, 1.1f, 0.19f, 0.60f, 0.0f, 0.3f, 0.2f, 1.5f, 0.00f },  // 8 flat acid+
};

static float HalfToFloat(uint16_t h) {   // copied from src_ink3d/ink3d.cpp
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
    const DWORD t0 = GetTickCount();
    if (FAILED(D3DCompile(src, strlen(src), entry, nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
        LabLog("shader '%s' (%s) compile error:\n%s\n", entry, target,
               err ? (char*)err->GetBufferPointer() : "?");
        ExitProcess(1);
    }
    if (err && err->GetBufferSize() > 1)
        LabLog("shader '%s' (%s) warnings:\n%s\n", entry, target, (char*)err->GetBufferPointer());
    LabLog("[lab] compiled %s %s in %lu ms\n", entry, target, GetTickCount() - t0);
    return blob;
}

// ---------------------------------------------------------------------------
// CPU sim helpers
// ---------------------------------------------------------------------------
float LabRenderer::Rand() {                 // xorshift32, every random routes here
    unsigned x = m_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    m_rng = x;
    return (float)(x & 0xffffff) / 16777216.0f;
}

float LabRenderer::VisR(float baseR) const {
    // isolated blob: (1 - d^2/s^2)^3 = thr  ->  d = s * sqrt(1 - thr^(1/3))
    return baseR * m_cfg.supportScale * sqrtf(fmaxf(1.0f - cbrtf(m_cfg.threshold), 0.0f));
}

float LabRenderer::HalfH(float z) const { return 0.5f * (m_camDist + z) / m_camDist; }

// divergence-free drift: curl of a smooth sinusoidal vector potential
static void Psi(float x, float y, float z, float t, const float* ph, float* o) {
    o[0] = sinf(y + ph[0] + t * 0.11f) * cosf(z * 0.83f + ph[1]);
    o[1] = sinf(z + ph[1] + t * 0.13f) * cosf(x * 0.91f + ph[2]);
    o[2] = sinf(x + ph[2] + t * 0.09f) * cosf(y * 0.79f + ph[0]);
}
static void Curl(float x, float y, float z, float t, const float* ph, float* v) {
    const float e = 0.25f;
    float a[3], b[3], dx[3], dy[3], dz[3];
    Psi(x + e, y, z, t, ph, a); Psi(x - e, y, z, t, ph, b);
    for (int i = 0; i < 3; i++) dx[i] = (a[i] - b[i]) / (2 * e);
    Psi(x, y + e, z, t, ph, a); Psi(x, y - e, z, t, ph, b);
    for (int i = 0; i < 3; i++) dy[i] = (a[i] - b[i]) / (2 * e);
    Psi(x, y, z + e, t, ph, a); Psi(x, y, z - e, t, ph, b);
    for (int i = 0; i < 3; i++) dz[i] = (a[i] - b[i]) / (2 * e);
    v[0] = dy[2] - dz[1];
    v[1] = dz[0] - dx[2];
    v[2] = dx[1] - dy[0];
}

void LabRenderer::Respawn(LabBlob& b, bool atBottom) {
    const LabConfig& c = m_cfg;
    auto drawR = [&](float lo, float hi, float bias) {
        return c.size * (lo + (hi - lo) * powf(Rand(), fmaxf(bias, 0.05f)));
    };
    if (b.kind == 0)      b.baseR = drawR(c.discMin, c.discMax, c.bigBias);
    else if (b.kind == 1) b.baseR = drawR(c.webMin, c.webMax, c.bigBias);
    else                  b.baseR = drawR(c.bubbleMin, c.bubbleMax, c.sizeBias);
    b.r = b.baseR;
    const float rv = VisR(b.baseR);
    const float zLo = 0.06f + rv * 0.5f, zHi = fmaxf(c.depth - 0.03f - rv, zLo + 0.01f);
    b.zc = Rand(zLo, zHi);
    b.zAmp = fminf(0.12f, 0.5f * (zHi - zLo));
    b.zRate = Rand(2.0f * kPi / 140.0f, 2.0f * kPi / 60.0f);
    b.s3 = Rand(0.0f, 2.0f * kPi);
    b.z = b.zc + b.zAmp * sinf(b.s3 + (float)m_time * b.zRate);
    const float hh = HalfH(b.z);
    b.x = Rand(-1.05f, 1.05f) * hh * kAspect;
    const float e = 1.0f + c.riseStretch;
    b.y = atBottom ? -hh - rv * e - 0.02f - Rand(0.0f, 0.15f)
                   : Rand(-hh - rv * e, hh + rv * e);
    b.vx = b.vz = 0.0f;
    b.vy = c.riseSpeed;
    b.phase = Rand(0.0f, 2.0f * kPi);
    b.breath = Rand(0.08f, 0.32f);
    b.s1 = Rand(0.0f, 2.0f * kPi);
    b.s2 = Rand(0.0f, 2.0f * kPi);
}

void LabRenderer::Seed() {
    const LabConfig& c = m_cfg;
    int n = std::min(std::max(c.blobs, 2), kMaxBlobs);
    int nHole = std::min(std::max(c.holes, 0), n / 4);
    int pos = n - nHole;
    int nDisc = std::max(1, (int)lroundf(n * c.discFrac));
    int nWeb  = std::max(0, (int)lroundf(n * c.webFrac));
    if (nDisc + nWeb > pos) nWeb = pos - nDisc;
    int nBubble = pos - nDisc - nWeb;

    m_blobs.assign((size_t)n, LabBlob{});
    int idx = 0;
    for (int k = 0; k < nDisc; k++, idx++) {
        LabBlob& b = m_blobs[idx];
        b.kind = 0; b.wgt = 1.0f; b.col = (k % 3 == 0) ? 2 : 0;
        Respawn(b, false);
    }
    // webs: a random walk of touching blobs -> connected veins
    float cx = 0, cy = 0, cz = 0.3f, prevR = 0.06f;
    for (int k = 0; k < nWeb; k++, idx++) {
        LabBlob& b = m_blobs[idx];
        b.kind = 1; b.wgt = 1.0f; b.col = (k % 4 == 0) ? 0 : 1;
        b.accent = (k % 7 == 3);
        Respawn(b, false);
        if (k % 6 != 0) {
            float ang = Rand(0.0f, 2.0f * kPi);
            float step = (VisR(prevR) + VisR(b.baseR)) * Rand(1.0f, 1.6f);
            b.x = cx + cosf(ang) * step;
            b.y = cy + sinf(ang) * step;
            b.zc = fminf(fmaxf(cz + Rand(-0.06f, 0.06f), 0.05f), c.depth - 0.05f);
            b.z = b.zc;
        }
        cx = b.x; cy = b.y; cz = b.zc; prevR = b.baseR;
    }
    for (int k = 0; k < nBubble; k++, idx++) {
        LabBlob& b = m_blobs[idx];
        b.kind = 2; b.wgt = 1.0f; b.col = (k % 3 == 0) ? 1 : 3;
        b.accent = (k % 5 == 2);
        Respawn(b, false);
        if (k > 0 && Rand() < 0.45f) {             // cluster on an earlier bubble
            const LabBlob& par = m_blobs[idx - 1];
            float ang = Rand(0.0f, 2.0f * kPi);
            float d = (VisR(par.baseR) + VisR(b.baseR)) * Rand(1.2f, 3.0f);
            b.x = par.x + cosf(ang) * d;
            b.y = par.y + sinf(ang) * d;
            b.zc = par.zc; b.z = par.z;
        }
    }
    // holes: the giants carry them (largest discs first), as z tunnels
    std::vector<int> discs;
    for (int i = 0; i < nDisc; i++) discs.push_back(i);
    std::sort(discs.begin(), discs.end(),
              [&](int a, int b) { return m_blobs[a].baseR > m_blobs[b].baseR; });
    for (int k = 0; k < nHole; k++, idx++) {
        LabBlob& b = m_blobs[idx];
        b.kind = 3; b.wgt = -fmaxf(c.holeWeight, 0.05f);
        b.parent = discs[k % discs.size()];
        b.hfrac = Rand(c.holeMin, c.holeMax);
        b.hang = Rand(0.0f, 2.0f * kPi);
        b.hdist = Rand(0.0f, 0.45f);
    }
    for (size_t i = 0; i < m_blobs.size(); i++) m_blobs[i].id = Rand();
}

void LabRenderer::Step(float dt) {
    const LabConfig& c = m_cfg;
    m_time += dt;
    const float t = (float)m_time;
    const float e = 1.0f + c.riseStretch;
    const float relax = 1.0f - expf(-dt / 1.2f);   // fps-normalised damping
    const int n = (int)m_blobs.size();

    for (int i = 0; i < n; i++) {
        LabBlob& b = m_blobs[i];
        if (b.kind == 3) continue;
        b.r = b.baseR * (1.0f + 0.05f * sinf(b.phase + t * b.breath));
        // buoyancy by radius: bigger wax rises a little faster
        const float rn = fminf(b.baseR / (c.discMax * c.size), 1.0f);
        const float vRise = c.riseSpeed * (0.70f + 0.60f * rn);
        const float wob = c.riseWobble * c.riseSpeed * 1.2f *
                          sinf(t * (0.12f + 0.10f * b.id) + b.s1);
        float cv[3];
        Curl(b.x * 1.6f + m_noisePh[3], b.y * 1.6f + m_noisePh[4], b.z * 1.6f + m_noisePh[5],
             t * 0.05f, m_noisePh, cv);
        const float flow = c.flowGain * 0.0045f;
        const float zt = b.zc + b.zAmp * sinf(t * b.zRate + b.s3);
        const float tx = wob + cv[0] * flow;
        const float ty = vRise + cv[1] * flow;
        const float tz = (zt - b.z) * 0.25f + cv[2] * flow * 0.6f;
        b.vx += (tx - b.vx) * relax;
        b.vy += (ty - b.vy) * relax;
        b.vz += (tz - b.vz) * relax;
    }
    // soft repulsion between oil blobs (cores never coincide; supports still
    // overlap, so masses neck and merge through the kernel sum)
    for (int i = 0; i < n; i++) {
        LabBlob& a = m_blobs[i];
        if (a.kind == 3) continue;
        const float ra = VisR(a.r), ma = a.r * a.r * a.r;
        for (int j = i + 1; j < n; j++) {
            LabBlob& b = m_blobs[j];
            if (b.kind == 3) continue;
            const float rb = VisR(b.r), mb = b.r * b.r * b.r;
            float dx = a.x - b.x, dy = (a.y - b.y) / e, dz = a.z - b.z;
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            // droplets (kind 2) keep clear of the masses instead of sitting on
            // them as blisters; oil-oil pairs may overlap and neck
            const bool drop = (a.kind == 2) != (b.kind == 2);
            float R = drop ? (ra + rb) * 1.6f + 0.02f : (ra + rb) * 0.85f;
            if (d >= R || d < 1e-5f) continue;
            float k = c.repulsion * (drop ? 0.08f : 0.03f) * (1.0f - d / R) * dt / d;
            float wa = mb / (ma + mb), wb = ma / (ma + mb);
            a.vx += dx * k * wa; a.vy += dy * k * wa; a.vz += dz * k * wa;
            b.vx -= dx * k * wb; b.vy -= dy * k * wb; b.vz -= dz * k * wb;
        }
    }
    for (int i = 0; i < n; i++) {
        LabBlob& b = m_blobs[i];
        if (b.kind == 3) continue;
        b.x += b.vx * dt; b.y += b.vy * dt; b.z += b.vz * dt;
        const float rv = VisR(b.baseR);
        b.z = fminf(fmaxf(b.z, 0.02f), c.depth - 0.02f);
        const float hh = HalfH(b.z), hw = hh * kAspect * 1.12f;
        if (b.x > hw)  b.vx -= 0.02f * dt;
        if (b.x < -hw) b.vx += 0.02f * dt;
        // off the top edge -> re-enter from BELOW the bottom (only ever rises
        // on screen: the OLED rule)
        if (b.y - rv * e > hh + 0.02f) Respawn(b, true);
    }
    // holes ride their parent giant
    for (int i = 0; i < n; i++) {
        LabBlob& h = m_blobs[i];
        if (h.kind != 3) continue;
        const LabBlob& p = m_blobs[h.parent];
        h.hang += dt * 0.03f;
        h.baseR = p.baseR * h.hfrac;
        h.r = p.r * h.hfrac;
        // the CAVITY a hole carves is wider than its own visible radius: where
        // holeWeight * k = 1 - thr, i.e. d = s * sqrt(1 - ((1-thr)/w)^(1/3)).
        // Keep the cavity inside the parent, or it bites the silhouette into
        // sharp horns/claws (seen at t=90 and t=300 s, seed 1234).
        const float kc  = fminf((1.0f - c.threshold) / fmaxf(c.holeWeight, 0.05f), 1.0f);
        const float cav = h.r * c.supportScale * sqrtf(fmaxf(1.0f - cbrtf(kc), 0.0f));
        const float off = fmaxf(VisR(p.r) - cav * 1.15f, 0.0f) * h.hdist;
        h.x = p.x + cosf(h.hang) * off;
        h.y = p.y + sinf(h.hang) * off * e;
        h.z = p.z;
    }
}

void LabRenderer::LogBlobs() const {
    int alive = 0; float maxY = -9, minY = 9;
    for (const LabBlob& b : m_blobs) {
        if (b.kind == 3) continue;
        alive++; maxY = fmaxf(maxY, b.y); minY = fminf(minY, b.y);
    }
    LabLog("[sim] t=%.1fs blobs=%d y in [%.2f, %.2f]  b0=(%.3f %.3f %.3f r%.3f) "
           "b1=(%.3f %.3f %.3f r%.3f)\n", m_time, alive, minY, maxY,
           m_blobs[0].x, m_blobs[0].y, m_blobs[0].z, m_blobs[0].r,
           m_blobs[1].x, m_blobs[1].y, m_blobs[1].z, m_blobs[1].r);
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void LabRenderer::InitOffscreen(int width, int height, const LabConfig& cfg, unsigned seed) {
    m_cfg = cfg;
    m_seed = seed;
    m_rng = (seed * 2654435761u) ^ 0x0113D5u;
    if (!m_rng) m_rng = 1u;
    m_width = width; m_height = height;
    m_rayW = cfg.halfRes ? (width + 1) / 2 : width;
    m_rayH = cfg.halfRes ? (height + 1) / 2 : height;
    m_camDist = 0.5f / tanf(cfg.fovDeg * 0.5f * kPi / 180.0f);
    for (int i = 0; i < 6; i++) m_noisePh[i] = Rand(0.0f, 2.0f * kPi);
    Seed();

    CreateDevice();
    CreatePipelines();
    CreateTargets();
    LabLog("[lab] %dx%d raymarch %dx%d blobs=%d holes=%d steps=%d shade=%s look=%d "
           "ink_level=%.3f camDist=%.3f\n",
           m_width, m_height, m_rayW, m_rayH, (int)m_blobs.size(), m_cfg.holes, m_cfg.steps,
           m_cfg.shade == LAB_FLAT ? "flat" : "gloss", m_cfg.look, m_cfg.inkLevel, m_camDist);
}

void LabRenderer::CreateDevice() {
    HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));
    ComPtr<IDXGIAdapter1> adapter;
    HR(m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad = {};
    adapter->GetDesc1(&ad);
    LabLog("[lab] GPU: %ls (%llu MB)\n", ad.Description,
           (unsigned long long)(ad.DedicatedVideoMemory >> 20));
    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)));
    m_queue->GetTimestampFrequency(&m_tsFreq);

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 4;                 // shot, raymarch, down, blur
    HR(m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 3;                 // SRVs t1..t3: raymarch, down, blur
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)));

    for (UINT i = 0; i < kFrames; i++)
        HR(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&m_allocators[i])));
    HR(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmd)));
    HR(m_cmd->Close());
    HR(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    D3D12_QUERY_HEAP_DESC qh = {};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = kFrames * kMaxTs;
    HR(m_device->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_tsHeap)));
    D3D12_HEAP_PROPERTIES rb = {}; rb.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = kFrames * kMaxTs * sizeof(UINT64);
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HR(m_device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&m_tsReadback)));
    for (UINT i = 0; i < kMaxTs; i++) m_passMin[i] = 1e9;

    // blob ring: kFrames slots of kMaxBlobs, persistently mapped upload heap
    D3D12_HEAP_PROPERTIES up = {}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
    bd.Width = (UINT64)kFrames * kMaxBlobs * sizeof(BlobGPU);
    HR(m_device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&m_blobBuf)));
    D3D12_RANGE none = { 0, 0 };
    HR(m_blobBuf->Map(0, &none, (void**)&m_blobMapped));
}

void LabRenderer::CreatePipelines() {
    // one RS for both passes: b0 = 40 root constants, t0 = root SRV (blobs),
    // t1 = table (raymarch target, display pass), s0 = linear clamp
    D3D12_DESCRIPTOR_RANGE r = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 0, 0 };
    D3D12_ROOT_PARAMETER p[3] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants = { 0, 0, 48 };
    p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[1].Descriptor = { 0, 0 };
    p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[2].DescriptorTable = { 1, &r };
    p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC s = {};
    s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = { 3, p, 1, &s };
    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
        LabLog("root signature: %s\n", err ? (char*)err->GetBufferPointer() : "?");
        LabFail("D3D12SerializeRootSignature", E_FAIL);
    }
    HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                     IID_PPV_ARGS(&m_rs)));

    auto gfx = [&](const char* src, const char* entry, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> vs = Compile(src, "VSMain", "vs_5_1");
        ComPtr<ID3DBlob> ps = Compile(src, entry, "ps_5_1");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_rs.Get();
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
    gfx(kRaySrc, "PSMain", m_psoRay);
    gfx(kPostSrc, "PSDown", m_psoDown);
    gfx(kPostSrc, "PSBlur", m_psoBlur);
    gfx(kPostSrc, "PSBlurB", m_psoBlurB);
    gfx(kPostSrc, "PSPost", m_psoPost);
}

void LabRenderer::CreateTargets() {
    auto tex = [&](int w, int h, ComPtr<ID3D12Resource>& out, int rtvSlot) {
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                             IID_PPV_ARGS(&out)));
        auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (SIZE_T)rtvSlot * m_rtvStride;
        m_device->CreateRenderTargetView(out.Get(), nullptr, rtv);
    };
    m_qW = std::max(1, (m_width + 3) / 4);
    m_qH = std::max(1, (m_height + 3) / 4);
    tex(m_width, m_height, m_shotTex, 0);
    tex(m_rayW, m_rayH, m_rayTex, 1);
    tex(m_qW, m_qH, m_downTex, 2);
    tex(m_qW, m_qH, m_blurTex, 3);

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    const UINT srvStride =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Resource* srvs[3] = { m_rayTex.Get(), m_downTex.Get(), m_blurTex.Get() };
    for (int i = 0; i < 3; i++) {
        auto h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)i * srvStride;
        m_device->CreateShaderResourceView(srvs[i], &sv, h);
    }

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

// ---------------------------------------------------------------------------
// frame plumbing (copied from src_ink3d/ink3d.cpp)
// ---------------------------------------------------------------------------
void LabRenderer::Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES& cur,
                          D3D12_RESOURCE_STATES to) {
    if (cur == to) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.StateBefore = cur;
    b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);
    cur = to;
}

void LabRenderer::WaitForGpuIdle() {
    const UINT64 v = m_nextFence++;
    HR(m_queue->Signal(m_fence.Get(), v));
    if (m_fence->GetCompletedValue() < v) {
        HR(m_fence->SetEventOnCompletion(v, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void LabRenderer::BeginFrame() {
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

void LabRenderer::EndFrame() {
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

void LabRenderer::Mark(const char* name) {
    if (m_tsCount >= kMaxTs) return;
    if (name) {
        if (m_tsCount < m_passNames.size()) m_passNames[m_tsCount] = name;
        else m_passNames.push_back(name);
    } else {
        m_passNames.resize(m_tsCount);
    }
    m_cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    m_frameIndex * kMaxTs + m_tsCount);
    m_tsCount++;
}

void LabRenderer::CollectTimings(UINT slot) {
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

void LabRenderer::ResetGpuStats() {
    for (UINT i = 0; i < kMaxTs; i++) { m_passSum[i] = 0.0; m_passMin[i] = 1e9; }
    m_passSamples = 0;
}

// ---------------------------------------------------------------------------
// one GPU frame
// ---------------------------------------------------------------------------
void LabRenderer::Render(float sdrScale, float hiGain, float maxOut) {
    BeginFrame();
    const UINT slot = m_frameIndex;
    const LabConfig& c = m_cfg;
    const float t = (float)m_time;
    const float e = 1.0f + c.riseStretch;

    // --- blobs into this slot's ring entry ---------------------------------
    float mainLuma = Luma(kMain[0]);
    BlobGPU* g = (BlobGPU*)(m_blobMapped + (size_t)slot * kMaxBlobs * sizeof(BlobGPU));
    const int n = (int)m_blobs.size();
    for (int i = 0; i < n; i++) {
        const LabBlob& b = m_blobs[i];
        BlobGPU& o = g[i];
        const float s = fmaxf(b.r * c.supportScale, 1e-4f);
        o.a[0] = b.x; o.a[1] = b.y; o.a[2] = b.z; o.a[3] = s;
        const float* src = b.accent ? kAccent[b.col & 3] : kMain[b.col & 3];
        float k = c.level;
        if (b.accent) {                         // film_equal_load_patches 0.5
            float la = Luma(src);
            k *= 1.0f + 0.5f * (mainLuma / la - 1.0f);
        }
        ChromaK(src, c.chroma, k, o.b);   // dark_sat: deeper = more saturated
        o.b[3] = b.wgt;
        o.c[0] = 1.0f; o.c[1] = 1.0f / e;
        o.c[2] = (b.kind == 3) ? 1.0f / fmaxf(c.holeTunnel, 1.0f) : 1.0f;
        o.c[3] = 1.0f / (s * s);
        o.d[0] = b.accent ? 1.0f : 0.0f; o.d[1] = b.id; o.d[2] = 0; o.d[3] = 0;
    }

    // --- camera: fixed perspective + a very slow drift (OLED: never still) --
    const float dr = c.cameraDrift;
    float cam[3] = { dr * sinf(t * 0.037f), dr * 0.6f * sinf(t * 0.029f + 1.0f), -m_camDist };
    float tgt[3] = { 0.0f, 0.0f, c.depth * 0.5f };
    float fwd[3] = { tgt[0] - cam[0], tgt[1] - cam[1], tgt[2] - cam[2] };
    float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    for (int i = 0; i < 3; i++) fwd[i] /= fl;
    // right = up x fwd (left-handed), up = fwd x right
    float right[3] = { fwd[2], 0.0f, -fwd[0] };
    float rl = sqrtf(right[0] * right[0] + right[2] * right[2]);
    right[0] /= rl; right[2] /= rl;
    float up[3] = { fwd[1] * right[2] - fwd[2] * right[1],
                    fwd[2] * right[0] - fwd[0] * right[2],
                    fwd[0] * right[1] - fwd[1] * right[0] };

    auto norm3 = [](float x, float y, float z, float* o) {
        float l = sqrtf(x * x + y * y + z * z); o[0] = x / l; o[1] = y / l; o[2] = z / l;
    };
    float key[3], lamp[3];
    norm3(-0.78f, 0.76f, -0.65f, key);     // [post] light_x/y/z, upper-left-front
    norm3(0.15f, -1.0f, 0.25f, lamp);      // the lamp under the slab

    const int look = (c.look >= 0 && c.look <= 8) ? c.look : 0;
    const LookPost& lp = kLookPost[look];
    float cb[48] = {};
    cb[0] = 1.0f / m_rayW; cb[1] = 1.0f / m_rayH; cb[2] = (float)m_width / m_height; cb[3] = t;
    cb[4] = cam[0]; cb[5] = cam[1]; cb[6] = cam[2];
    cb[7] = tanf(c.fovDeg * 0.5f * kPi / 180.0f);
    cb[8] = right[0]; cb[9] = right[1]; cb[10] = right[2]; cb[11] = c.threshold;
    cb[12] = up[0]; cb[13] = up[1]; cb[14] = up[2]; cb[15] = c.depth;
    cb[16] = fwd[0]; cb[17] = fwd[1]; cb[18] = fwd[2]; cb[19] = (float)n;
    ChromaK(kMain[0], c.chroma, c.level, &cb[20]);
    cb[23] = (float)c.steps;
    cb[24] = kInk[0]; cb[25] = kInk[1]; cb[26] = kInk[2]; cb[27] = c.inkLevel;
    {
        float ls = Luma(kSeam), k = c.level * mainLuma / ls;
        cb[28] = kSeam[0] * k; cb[29] = kSeam[1] * k; cb[30] = kSeam[2] * k;
    }
    cb[31] = (look == 8) ? (float)LAB_FLAT : (float)c.shade;
    cb[32] = key[0]; cb[33] = key[1]; cb[34] = key[2]; cb[35] = hiGain;
    cb[36] = lamp[0]; cb[37] = lamp[1]; cb[38] = lamp[2]; cb[39] = (float)look;
    {
        const float ka = c.level * (1.0f + 0.5f * (mainLuma / Luma(kAccent[0]) - 1.0f));
        ChromaK(kAccent[0], c.chroma, ka, &cb[40]);
    }
    cb[43] = lp.backLit;

    Mark("raymarch");
    Barrier(m_rayTex.Get(), m_rayState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_rayW, (float)m_rayH, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_rayW, m_rayH };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);
    m_cmd->SetGraphicsRootSignature(m_rs.Get());
    m_cmd->SetPipelineState(m_psoRay.Get());
    m_cmd->SetGraphicsRoot32BitConstants(0, 48, cb, 0);
    m_cmd->SetGraphicsRootShaderResourceView(1, m_blobBuf->GetGPUVirtualAddress() +
                                                (UINT64)slot * kMaxBlobs * sizeof(BlobGPU));
    m_cmd->SetGraphicsRootDescriptorTable(2, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);

    // --- quick post: quarter-res box down, 9x9 blur, then the full-res post --
    float pc[20] = {};
    pc[2] = sdrScale; pc[3] = maxOut;
    // odd pass count, each pass at spacing <= 1.3 texels, same total radius
    const int blurPasses = lp.spread <= 1.3f ? 1 : (lp.spread <= 2.25f ? 3 : 5);
    pc[4] = 1.0f / m_rayW; pc[5] = 1.0f / m_rayH;
    pc[6] = lp.spread / sqrtf((float)blurPasses); pc[7] = 1.0f;
    pc[8] = lp.hal; pc[9] = lp.tint[0]; pc[10] = lp.tint[1]; pc[11] = lp.tint[2];
    pc[12] = lp.aberr; pc[13] = lp.grain; pc[14] = lp.vignette;
    pc[16] = lp.dof; pc[17] = lp.focus; pc[18] = lp.range;
    auto pass = [&](ID3D12PipelineState* pso, int rtvSlot, int w, int h) {
        auto r = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        r.ptr += (SIZE_T)rtvSlot * m_rtvStride;
        m_cmd->OMSetRenderTargets(1, &r, FALSE, nullptr);
        D3D12_VIEWPORT v = { 0, 0, (float)w, (float)h, 0, 1 };
        D3D12_RECT rc = { 0, 0, w, h };
        m_cmd->RSSetViewports(1, &v);
        m_cmd->RSSetScissorRects(1, &rc);
        pc[0] = 1.0f / w; pc[1] = 1.0f / h;
        m_cmd->SetPipelineState(pso);
        m_cmd->SetGraphicsRoot32BitConstants(0, 20, pc, 0);
        m_cmd->DrawInstanced(3, 1, 0, 0);
    };
    Mark("down");
    Barrier(m_rayTex.Get(), m_rayState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Barrier(m_downTex.Get(), m_downState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pass(m_psoDown.Get(), 2, m_qW, m_qH);
    Mark("blur");
    for (int bp = 0; bp < blurPasses; bp++) {
        if ((bp & 1) == 0) {      // down -> blur
            Barrier(m_downTex.Get(), m_downState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Barrier(m_blurTex.Get(), m_blurState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            pass(m_psoBlur.Get(), 3, m_qW, m_qH);
        } else {                  // blur -> down
            Barrier(m_blurTex.Get(), m_blurState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Barrier(m_downTex.Get(), m_downState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            pass(m_psoBlurB.Get(), 2, m_qW, m_qH);
        }
    }
    Mark("post");
    Barrier(m_blurTex.Get(), m_blurState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Barrier(m_shotTex.Get(), m_shotState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pass(m_psoPost.Get(), 0, m_width, m_height);
    Mark(nullptr);
    EndFrame();
}

bool LabRenderer::CaptureOffscreen(std::vector<float>& out) {
    WaitForGpuIdle();
    for (UINT i = 0; i < kFrames; i++) CollectTimings(i);   // every frame is done
    HR(m_allocators[0]->Reset());
    HR(m_cmd->Reset(m_allocators[0].Get(), nullptr));
    Barrier(m_shotTex.Get(), m_shotState, D3D12_RESOURCE_STATE_COPY_SOURCE);
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

void LabRenderer::Shutdown() {
    if (m_device) WaitForGpuIdle();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}
