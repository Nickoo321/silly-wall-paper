#include "oil3d.h"
#include "shaders.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern void Oil3DFail(const char* what, HRESULT hr);      // main.cpp
extern void Oil3DLog(const char* fmt, ...);               // main.cpp
#define HR(e) do { HRESULT _hr = (e); if (FAILED(_hr)) Oil3DFail(#e, _hr); } while (0)

static const float kPi  = 3.14159265358979f;
static const float kTau = 6.2831853f;

// ===========================================================================
// colour helpers — copied, not shared, from src/fluid.cpp @8a4f185
// (HSVtoRGB :95, RgbToHsv :3470, HsvHueShiftCpu :3485, HsvLerp3 :3497,
//  BuildAcidPalette :3513, AcidAccentHash :3399, ShadowToneGate :3436,
//  BuOkHueEnc :3456, BuildAnchorCdf :5922)
// ===========================================================================
namespace {

struct RGB { float r, g, b; };

RGB HSVtoRGB(float h, float s, float v) {
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

void RgbToHsv(const float c[3], float& h, float& sv, float& v) {
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

void HsvHueShiftCpu(float c[3], float deg) {
    float h, sat, val;
    RgbToHsv(c, h, sat, val);
    h = fmodf(h + deg / 360.0f, 1.0f);
    if (h < 0.0f) h += 1.0f;
    RGB out = HSVtoRGB(h, sat, val);
    c[0] = out.r; c[1] = out.g; c[2] = out.b;
}

void HsvLerp3(const float a3[3], const float b3[3], float t, float out[3]) {
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

void BuildAcidPalette(const float oilA[3], const float inkA[3], float outOil[12],
                      float outInk[12]) {
    float ho, so, vo, hi, si, vi;
    RgbToHsv(oilA, ho, so, vo);
    RgbToHsv(inkA, hi, si, vi);
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
    put(0, hi, si,          vi * 0.260f);
    put(1, hi, si * 0.850f, vi * 0.090f);
    put(2, hi, si,          vi * 0.720f);
    put(3, hi, si * 0.800f, vi * 1.450f);
}

float AcidAccentHash(uint32_t i, uint32_t salt) {
    uint32_t h = (i * 2654435761u) ^ salt;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    h *= 3266489917u; h ^= h >> 16;
    return (h >> 8) * (1.0f / 16777216.0f);
}

float ShadowToneGate(double t, float period, float fade) {
    if (period <= 0.5f) return 1.0f;
    const double P = period;
    const double half = 0.5 * P;
    const double f = fmin(fmax((double)fade, 0.0), 0.5 * half);
    double v = fmod(t + f, P);
    if (v < 0.0) v += P;
    auto ss = [](double e0, double e1, double x) {
        if (e1 - e0 < 1e-6) return x >= e1 ? 1.0 : 0.0;
        double u = (x - e0) / (e1 - e0);
        u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
        return u * u * (3.0 - 2.0 * u);
    };
    return (float)(ss(0.0, f, v) * (1.0 - ss(half - f, half, v)));
}

float BuOkHueEnc(float r, float g, float b) {
    auto lin = [](float c) {
        c = fmaxf(c, 0.0f);
        return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    };
    const float lr = lin(r), lg = lin(g), lb = lin(b);
    const float l = cbrtf(0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb);
    const float m = cbrtf(0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb);
    const float s = cbrtf(0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb);
    const float A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    const float B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
    return atan2f(B, A);
}

void BuildAnchorCdf(float w, float cdf[257]) {
    static const float kAnc[4][2] = { {325.0f, 1.0f}, {215.0f, 0.6f},
                                      {355.0f, 0.5f}, {275.0f, 0.4f} };
    const float sig = 20.0f;
    double dens[257];
    for (int j = 0; j <= 256; j++) {
        const float H = 360.0f * (float)j / 256.0f;
        double d = 1.0;
        for (int k = 0; k < 4; k++) {
            float dh = fmodf(fabsf(H - kAnc[k][0]), 360.0f);
            if (dh > 180.0f) dh = 360.0f - dh;
            d += 4.0 * w * kAnc[k][1] * exp(-0.5 * (dh / sig) * (dh / sig));
        }
        dens[j] = d;
    }
    double cum[257]; cum[0] = 0.0;
    double acc = 0.0;
    for (int j = 1; j <= 256; j++) {
        acc += 0.5 * (dens[j - 1] + dens[j]);
        cum[j] = acc;
    }
    for (int j = 0; j <= 256; j++) cdf[j] = (float)(cum[j] / acc);
    cdf[0] = 0.0f;
    cdf[256] = 1.0f;
}

uint32_t XorShift(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}
float U01(uint32_t& s) { return (XorShift(s) >> 8) * (1.0f / 16777216.0f); }

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

} // namespace

// ===========================================================================
// Oil3DSim
// ===========================================================================
float Oil3DSim::Rf() { return U01(m_respawnRng); }

float Oil3DSim::DrawR(int kind) {
    const OilKeys& a = m_cfg.oil;
    float lo = a.bubbleMin, hi = a.bubbleMax, bias = a.sizeBias;
    if      (kind == 0) { lo = a.discMin; hi = a.discMax; bias = a.bigBias; }
    else if (kind == 1) { lo = a.webMin;  hi = a.webMax;  bias = a.bigBias; }
    else if (kind == 3) { lo = a.holeMin; hi = a.holeMax; bias = a.sizeBias; }
    return lo + (hi - lo) * powf(Rf(), fmaxf(bias, 0.05f));
}

void Oil3DSim::Init(const Oil3DConfig& cfg, unsigned seed, float aspect) {
    m_cfg = cfg;
    m_aspect = aspect > 0.1f ? aspect : 16.0f / 9.0f;
    m_seed = seed;
    m_time = 0.0;
    int want = cfg.blobs > 0 ? cfg.blobs : cfg.oil.blobCount;
    // headroom for blobs still dissolving: the turnover only retires while the
    // vector has a free slot (2D: 96 of 128; a full 64 of 64 would block it)
    const int kWantMax = kOil3DMaxBlobs - 8;
    if (want > kWantMax) {
        Oil3DLog("[oil3d] blob_count %d capped at %d (3D prototype limit; [oil3d] blobs "
                 "overrides)\n", want, kWantMax);
        want = kWantMax;
    }
    if (want < 1) want = 1;
    m_want = want;
    m_rng = (seed * 2654435761u) ^ 0xAC1Du;
    if (!m_rng) m_rng = 0x9E3779B9u;
    SeedBlobs();
    m_dropRng = ((seed * 0x27D4EB2Fu) ^ 0xD20B5u) | 1u;
    SeedDrops();
    ApplyPaletteStart();
}

// Seed the oil population: the 2D SeedAcidBlobs (src/fluid.cpp:3642) with a
// depth. Discs, webs in chains, bubbles (some clustered), holes inside a
// disc/web parent. Every kind is spread over the full height, which is the
// stagger the rise mode needs (the column is populated from the first frame).
void Oil3DSim::SeedBlobs() {
    const OilKeys& a = m_cfg.oil;
    const float A = m_aspect, D = m_cfg.depth;
    const float zLo = 0.10f * D, zHi = 0.90f * D;
    const int n = m_want;
    uint32_t& s = m_rng;
    auto f = [&]() { return U01(s); };
    auto fr = [&](float lo, float hi) { return lo + (hi - lo) * U01(s); };

    int nDisc   = (int)(n * a.discFrac   + 0.5f);
    int nWeb    = (int)(n * a.webFrac    + 0.5f);
    int nBubble = (int)(n * a.bubbleFrac + 0.5f);
    if (nDisc > n) nDisc = n;
    if (nDisc + nWeb > n) nWeb = n - nDisc;
    if (nDisc + nWeb + nBubble > n - 1) nBubble = n - 1 - nDisc - nWeb;
    if (nBubble < 0) nBubble = 0;
    int nHole = n - nDisc - nWeb - nBubble;
    if (nHole < 0) nHole = 0;

    m_blobs.assign((size_t)n, Blob3{});
    auto drawR = [&](float lo, float hi, float bias) {
        return lo + (hi - lo) * powf(f(), fmaxf(bias, 0.05f));
    };
    auto unit3 = [&](float& ux, float& uy, float& uz) {
        const float z = fr(-1.0f, 1.0f), ph = fr(0.0f, kTau);
        const float r = sqrtf(fmaxf(1.0f - z * z, 0.0f));
        ux = r * cosf(ph); uy = r * sinf(ph); uz = z;
    };
    auto place = [&](Blob3& b, float x, float y, float z) {
        b.x = x; b.y = y; b.z = fminf(fmaxf(z, zLo), zHi);
        b.vx = b.vy = b.vz = 0.0f;
        b.phase = fr(0.0f, kTau);
        b.breathRate = fr(0.08f, 0.32f);
        b.s1 = fr(0.0f, kTau); b.s2 = fr(0.0f, kTau); b.s3 = fr(0.0f, kTau);
    };

    // Y of a free blob. With rise_respawn the 2D seeds Y on screen only, but
    // one trip round the column is 1 + 2 x the support reach (~2.4 screen
    // heights for a giant): the seeded batch then leaves the top together and
    // the frame sits nearly empty until the re-entries arrive (measured in the
    // 3D prototype, t ~ 30-45 s). Seeding over the WHOLE loop, including the
    // parked rows under the bottom edge, starts the column in steady state.
    const bool respawnMode = (a.riseSpeed > 1e-6f && a.riseRespawn);
    auto seedY = [&](float r) {
        if (!respawnMode) return fr(0.03f, 0.97f);
        const float sup = r * a.supportScale * (1.0f + a.breathAmt) * 0.86f * m_cfg.radiusScale;
        return fr(-sup, 1.0f + sup);
    };

    int idx = 0;
    for (int k = 0; k < nDisc && idx < n; k++, idx++) {
        Blob3& b = m_blobs[idx];
        b.kind = 0; b.wgt = 1.0f;
        b.baseR = drawR(a.discMin, a.discMax, a.bigBias);
        b.colIdx = (k % 3 == 0) ? 2 : 0;
        const float x = fr(0.03f, 0.97f) * A;
        place(b, x, seedY(b.baseR), fr(zLo, zHi));
    }
    // webs: a random walk of touching blobs makes connected veins
    float cx = fr(0.15f, 0.85f) * A, cy = fr(0.15f, 0.85f), cz = fr(zLo, zHi), prevR = 0.06f;
    for (int k = 0; k < nWeb && idx < n; k++, idx++) {
        Blob3& b = m_blobs[idx];
        b.kind = 1; b.wgt = 1.0f;
        b.baseR = drawR(a.webMin, a.webMax, a.bigBias);
        b.colIdx = (k % 4 == 0) ? 0 : 1;
        if (k % 9 == 0) { cx = fr(0.10f, 0.90f) * A; cy = seedY(a.webMax); cz = fr(zLo, zHi); }
        else {
            float ux, uy, uz;
            unit3(ux, uy, uz);
            const float step = (prevR + b.baseR) * fr(0.75f, 1.25f);
            cx += ux * step; cy += uy * step; cz += uz * step * 0.5f;
            cx = fminf(fmaxf(cx, -0.05f * A), 1.05f * A);
            if (!respawnMode) cy = fminf(fmaxf(cy, -0.05f), 1.05f);
        }
        prevR = b.baseR;
        place(b, cx, cy, cz);
    }
    for (int k = 0; k < nBubble && idx < n; k++, idx++) {
        Blob3& b = m_blobs[idx];
        b.kind = 2; b.wgt = 1.0f;
        b.baseR = drawR(a.bubbleMin, a.bubbleMax, a.sizeBias);
        b.colIdx = (k % 3 == 0) ? 1 : 3;
        if (k > 0 && f() < 0.45f) {          // cluster on an earlier bubble
            int parIdx = idx - 1 - (int)(f() * 3.0f) % 3;
            if (parIdx < 0) parIdx = 0;
            const Blob3& par = m_blobs[parIdx];
            float ux, uy, uz;
            unit3(ux, uy, uz);
            const float d = (par.baseR + b.baseR) * fr(1.15f, 4.0f);
            place(b, par.x + ux * d, par.y + uy * d, par.z + uz * d);
        } else {
            const float x = fr(0.02f, 0.98f) * A;
            place(b, x, seedY(b.baseR), fr(zLo, zHi));
        }
    }
    const int positives = nDisc + nWeb;
    for (int k = 0; k < nHole && idx < n; k++, idx++) {
        Blob3& b = m_blobs[idx];
        b.kind = 3; b.wgt = -fmaxf(a.holeWeight, 0.05f);
        b.baseR = drawR(a.holeMin, a.holeMax, a.sizeBias);
        b.colIdx = 0;
        if (positives > 0) {
            const Blob3& par = m_blobs[(int)(f() * positives) % positives];
            // sub-scaled to the parent, or the hole swallows the blob it sits in
            b.baseR = fminf(b.baseR, par.baseR * fr(0.20f, 0.55f));
            float ux, uy, uz;
            unit3(ux, uy, uz);
            const float d = (par.baseR - b.baseR) * fr(0.0f, 0.80f);
            place(b, par.x + ux * d, par.y + uy * d, par.z + uz * d * 0.5f);
        } else {
            place(b, fr(0.05f, 0.95f) * A, fr(0.05f, 0.95f), fr(zLo, zHi));
        }
    }
    for (Blob3& b : m_blobs) { b.rTarget = b.baseR; b.born = 0.0f; }
    m_respawnRng = XorShift(s) | 1u;
    m_turnRng = ((m_seed ? m_seed * 0x85EBCA6Bu : 1u) ^ 0x7B1DB00Cu) | 1u;
    m_turnAcc = 0.0f;
    Oil3DLog("[oil3d] seeded %d blobs: %d disc, %d web, %d bubble, %d hole\n",
             n, nDisc, nWeb, nBubble, nHole);
}

// The 2D droplets are hundreds of particles summed into the field; the 3D
// prototype carries up to droplet_cap (<= 64) small analytic beads instead.
void Oil3DSim::SeedDrops() {
    const OilKeys& a = m_cfg.oil;
    m_drops.clear();
    if (a.droplets <= 0 || m_cfg.dropletCap <= 0) return;
    int n = a.droplets / 12;
    if (n < 8) n = 8;
    if (n > m_cfg.dropletCap) n = m_cfg.dropletCap;
    if (n > kOil3DMaxDrops) n = kOil3DMaxDrops;
    m_drops.resize((size_t)n);
    for (Drop3& d : m_drops) RespawnDrop(d, false);
    Oil3DLog("[oil3d] droplets: 2D asks for %d, 3D carries %d beads (droplet_cap %d)\n",
             a.droplets, n, m_cfg.dropletCap);
}

void Oil3DSim::RespawnDrop(Drop3& d, bool below) {
    const OilKeys& a = m_cfg.oil;
    const float D = m_cfg.depth;
    auto f = [&]() { return U01(m_dropRng); };
    const float rmin = fmaxf(a.dropletRMin, 0.0002f);
    const float rmax = fmaxf(a.dropletRMax, rmin);
    const float raw = rmin + (rmax - rmin) * powf(f(), fmaxf(a.dropletBias, 0.05f));
    d.r = raw * fmaxf(m_cfg.dropletScale, 0.01f);
    const float racerR = a.dropletRacerRMax > 0.0f ? a.dropletRacerRMax : 2.0f * rmin;
    const float pr = f();
    d.racer = (a.dropletRacerFrac > 0.0f && raw <= racerR && pr < a.dropletRacerFrac) ? 1 : 0;
    const float base = a.riseSpeed > 1e-6f ? a.riseSpeed : 0.015f;
    d.climb = base * (0.8f + 0.6f * f()) * (d.racer ? fmaxf(a.dropletRacerSpeed, 0.1f) : 1.0f);
    d.wob = d.racer ? a.dropletRacerWobble : 0.15f;
    d.phase = f() * kTau;
    d.x = f() * m_aspect;
    d.z = (0.06f + 0.88f * f()) * D;
    d.y = below ? 1.0f + 2.0f * d.r + 0.15f * f() : f() * 1.1f;
}

float Oil3DSim::Stretch(const Blob3& b) const {
    const OilKeys& a = m_cfg.oil;
    if (!(a.riseStretch > 0.0005f && a.riseSpeed > 1e-6f)) return 0.0f;
    const float vxu = b.vx / m_aspect;            // X/s -> uv/s
    const float sp = sqrtf(vxu * vxu + b.vy * b.vy);
    const float sf = fminf(sp / (a.riseSpeed * 1.6f), 1.0f);
    const float rf2 = fminf(fmaxf(0.075f / fmaxf(b.baseR, 0.012f), 0.25f), 1.6f);
    return fminf(a.riseStretch * sf * rf2, 0.80f);
}

void Oil3DSim::BlobColor(int i, const FramePalette& p, float rgb[3]) const {
    const Blob3& b = m_blobs[(size_t)i];
    int ci = (b.colIdx < 0 || b.colIdx > 3) ? 0 : b.colIdx;
    if (m_cfg.oil.accentMode == 1) {
        if (ci == 3) ci = (int)(AcidAccentHash((uint32_t)i, 0x9E3779B1u) * 3.0f) % 3;
        const float mx = Clamp01(b.accentMix);
        for (int ch = 0; ch < 3; ch++)
            rgb[ch] = p.oil[ci * 3 + ch] + (p.oil[9 + ch] - p.oil[ci * 3 + ch]) * mx;
    } else {
        for (int ch = 0; ch < 3; ch++) rgb[ch] = p.oil[ci * 3 + ch];
    }
}

void Oil3DSim::Step(float dt) {
    const OilKeys& a = m_cfg.oil;
    const bool cons = a.conserveMass > 0.5f;
    if (cons) {
        StepTurnover(dt);
        AdjustCount();
    }
    StepBlobs(dt);
    StepDrops(dt);
    m_time += dt;
}

// The 2D StepAcidBlobs (src/fluid.cpp:4243) minus the fluid velocity and the
// mouse modes, plus a depth axis:
//   * the 2D fluid's velocity -> an analytic divergence-free flow (two stream
//     functions: one in the screen plane, one in the x-z plane that vanishes
//     at the slab's front and back, so it never pushes a blob out)
//   * every screen-plane speed is divided by the blob's perspective scale
//     s(z): a far blob climbs the screen slower (real depth parallax), and
//     only parallax3d of the 2D size-based parallax is kept on top
void Oil3DSim::StepBlobs(float dt) {
    const OilKeys& a = m_cfg.oil;
    const int n = (int)m_blobs.size();
    if (n == 0) return;
    const float A = m_aspect, D = m_cfg.depth;
    const float zLo = 0.08f * D, zHi = 0.92f * D;
    const float t = (float)m_time;
    const float camDist = 0.5f / tanf(m_cfg.fovDeg * 0.5f * kPi / 180.0f);

    const float vis     = Clamp01(a.oilViscosity);
    const float visFlow = 1.0f - 0.55f * vis;
    const float breathS = 1.0f - 0.60f * vis;
    const float dampRate = fmaxf(a.damping, 0.05f) / (1.0f + 2.0f * vis);
    const float tw = (vis > 1e-4f) ? t * (1.0f - 0.5f * vis) : t;
    const float fg = a.flowGain * visFlow;
    const float cdrift = a.curlDrift * visFlow;
    const float par  = Clamp01(a.riseParallax) * Clamp01(m_cfg.parallax3d);
    const float rRef = fmaxf(a.discMax, 1e-4f);
    const bool  cons0 = (a.conserveMass > 0.5f);
    // global flow phases from the seed (a FIELD, shared by every blob)
    const float g1 = (float)(m_seed % 997u) * 0.0131f, g2 = (float)(m_seed % 991u) * 0.0173f;

    for (int i = 0; i < n; i++) {
        Blob3& b = m_blobs[i];
        float pscale = 1.0f;
        if (par > 1e-4f) {
            float rr = b.baseR / rRef;
            rr = rr < 0.25f ? 0.25f : (rr > 1.0f ? 1.0f : rr);
            pscale = 1.0f + (rr - 1.0f) * par;
        }
        const float invS = camDist / (camDist + b.z);
        const float u = b.x / A, v = b.y, zn = b.z / D;

        // target velocity: tx in uv/s (converted to X/s below), ty uv/s, tz world/s
        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        // (1) the fluid stand-in: psi1(u,v) in the screen plane
        {
            const float p1 = 2.1f * u + 0.050f * t + g1, p2 = 1.7f * v - 0.037f * t + g2;
            const float amp = m_cfg.flowAmp * fg * pscale;
            tx += amp * (-1.7f * sinf(p1) * sinf(p2));
            ty += amp * (-2.1f * cosf(p1) * cosf(p2));
        }
        // (2) the 2D curl drift, verbatim (per-blob phases s1/s2)
        {
            const float a1 = 3.1f * u + 0.23f * t + b.s1;
            const float a2 = 2.7f * v + 0.19f * t;
            const float a3 = 5.3f * u - 0.11f * t;
            const float a4 = 4.1f * v + 0.29f * t + b.s2;
            const float dpsidx = 3.1f * cosf(a1) * cosf(a2) + 0.7f * 5.3f * cosf(a3) * cosf(a4);
            const float dpsidy = -2.7f * sinf(a1) * sinf(a2) - 0.7f * 4.1f * sinf(a3) * sinf(a4);
            tx += dpsidy * cdrift;
            ty += -dpsidx * cdrift;
        }
        // (3) depth: psi2(u, zn) = sin(2.3u + .031t + g2) sin(pi zn), so blobs
        // cross in front of each other and vz is 0 at the front and back
        {
            const float ph = 2.3f * u + 0.031f * t + g2 + 0.6f * b.s3;
            const float amp = m_cfg.zDrift;
            tx += amp * 0.25f * kPi * sinf(ph) * cosf(kPi * zn);
            // vz chosen so d(vx)/dx + d(vz)/dz = 0 in (X, z): divergence-free
            tz += -amp * 0.25f * 2.3f * cosf(ph) * sinf(kPi * zn) * D;
        }
        // buoyancy (uv y is down, so "up" is negative); holes sink
        ty += (b.wgt > 0.0f ? -1.0f : 1.0f) * a.buoyancy * (b.baseR / 0.12f);
        // LAVA LAMP rise; holes are water and climb at 0.7x
        if (a.riseSpeed > 1e-6f) {
            const float rs = a.riseSpeed * (b.wgt > 0.0f ? 1.0f : 0.7f) * pscale;
            ty -= rs;
            if (a.riseWobble > 1e-4f)
                tx += a.riseWobble * rs *
                      (0.85f * sinf(0.17f * tw + b.s1) + 0.45f * sinf(0.29f * tw + b.s2));
        }
        // screen-plane speeds are world speeds seen through the perspective
        tx *= invS; ty *= invS;
        float tX = tx * A;   // uv/s -> X/s

        // soft repulsion between SAME-SIGN blobs (+ the viscosity neck), in 3D
        if (a.repulsion > 0.0001f || vis > 1e-4f) {
            const float reachF = 1.0f + 0.9f * vis;
            const float soft   = 1.0f - 0.35f * vis;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                const Blob3& o = m_blobs[j];
                if ((o.wgt > 0.0f) != (b.wgt > 0.0f)) continue;
                const float dx = b.x - o.x, dy = b.y - o.y, dz = b.z - o.z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                const float rr = (b.baseR + o.baseR) * 0.95f;
                const float reach = rr * reachF;
                if (d2 > reach * reach || d2 < 1e-8f) continue;
                const float d = sqrtf(d2);
                float push;
                if (d < rr)           push = a.repulsion * (1.0f - d / rr) * 0.02f * soft;
                else if (vis > 1e-4f) push = -vis * (1.0f - (d - rr) / (reach - rr)) * 0.006f;
                else                  continue;
                tX += (dx / d) * push;
                ty += (dy / d) * push;
                tz += (dz / d) * push;
            }
        }

        const float k = 1.0f - expf(-dampRate * dt);   // fps-normalised
        b.vx += (tX - b.vx) * k;
        b.vy += (ty - b.vy) * k;
        b.vz += (tz - b.vz) * k;
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        b.z += b.vz * dt;
        if (b.z < zLo) { b.z = zLo; if (b.vz < 0.0f) b.vz = 0.0f; }
        if (b.z > zHi) { b.z = zHi; if (b.vz > 0.0f) b.vz = 0.0f; }

        // wrap / re-entry: the 2D rules (conserve_mass: the margin is the
        // field's own reach, so nothing pops at an edge)
        const float supOut = b.baseR * a.supportScale * (1.0f + a.breathAmt) * 0.86f
                           * m_cfg.radiusScale;
        const bool  cons   = (a.conserveMass > 0.5f);
        const float m  = fmaxf(a.wrapMargin, 0.02f);
        const float mx = (cons ? fmaxf(m, supOut / fmaxf(A, 1e-4f) + 0.02f) : m) * A;
        const float my = cons ? fmaxf(m, supOut + 0.02f) : m;
        if (b.x < -mx)     b.x += A + 2.0f * mx;
        if (b.x > A + mx)  b.x -= A + 2.0f * mx;
        const bool respawnMode = (a.riseSpeed > 1e-6f && a.riseRespawn);
        if (respawnMode && b.y < -supOut) {
            // re-enter BELOW the bottom edge (generated off screen, only ever
            // moves up on screen: the OLED rule) at a fresh x / depth
            const float newR = DrawR(b.kind);
            if (!cons) b.baseR = newR;
            if (b.rTarget > 0.0f) b.rTarget = b.baseR;
            b.x = Rf() * A;
            b.y = 1.0f + b.baseR * a.supportScale * (1.0f + a.breathAmt) * 0.86f * m_cfg.radiusScale;
            b.z = zLo + (zHi - zLo) * Rf();
            b.vx = b.vy = b.vz = 0.0f;
            b.s1 = Rf() * kTau;
            b.s2 = Rf() * kTau;
        } else if (!respawnMode) {
            if (b.y < -my)       b.y += 1.0f + 2.0f * my;
            if (b.y > 1.0f + my) b.y -= 1.0f + 2.0f * my;
        }
        if (respawnMode) {
            const float supFloor = (a.blobTurnoverS > 0.0f && cons)
                ? fmaxf(b.baseR, b.rTarget) * a.supportScale * (1.0f + a.breathAmt) * 0.86f
                  * m_cfg.radiusScale
                : supOut;
            const float floorY = 1.0f + supFloor + 0.06f;
            if (b.y > floorY) { b.y = floorY; if (b.vy > 0.0f) b.vy = 0.0f; }
        }

        b.phase += b.breathRate * breathS * dt;

        if (a.accentMode == 1) {
            const float amr = fmaxf(a.accentMaxR, 0.0f) * fmaxf(a.discMax, 1e-4f);
            if (b.accentSmall < 0)          b.accentSmall = (b.baseR < amr) ? 1 : 0;
            else if (b.baseR < amr * 0.96f) b.accentSmall = 1;
            else if (b.baseR > amr * 1.04f) b.accentSmall = 0;
            const float frac = Clamp01(a.accentFrac);
            const float tgt = (b.accentSmall > 0 &&
                               AcidAccentHash((uint32_t)i, 0x51ED2701u) < frac) ? 1.0f : 0.0f;
            if (b.accentMix < -0.5f) b.accentMix = tgt;
            else {
                const float step = dt * 0.5f;
                b.accentMix = (b.accentMix < tgt) ? fminf(b.accentMix + step, tgt)
                                                  : fmaxf(b.accentMix - step, tgt);
            }
        }
        if (b.rTarget != b.baseR) {
            const float tauB = (b.rTarget > b.baseR)
                             ? fmaxf(a.spawnGrowS, 1.0f) : fmaxf(a.dissolveS, 1.0f);
            b.baseR += (b.rTarget - b.baseR) * (1.0f - expf(-dt / tauB));
        }
    }
    if (cons0) {
        const float gone = 0.2f / (1440.0f * fmaxf(a.supportScale, 0.5f));   // 1440 px nominal
        for (size_t i = m_blobs.size(); i-- > 0; )
            if (m_blobs[i].rTarget <= 0.0f && m_blobs[i].baseR < gone)
                m_blobs.erase(m_blobs.begin() + (ptrdiff_t)i);
    }
}

// blob_turnover_s (src/fluid.cpp:3848): retire one blob every turnover /
// count seconds; AdjustCount grows one in from under the bottom edge.
void Oil3DSim::StepTurnover(float dt) {
    const OilKeys& a = m_cfg.oil;
    if (a.conserveMass <= 0.5f || a.blobTurnoverS <= 0.0f) return;
    const float period = a.blobTurnoverS / (float)(m_want > 0 ? m_want : 1);
    m_turnAcc += dt;
    if (m_turnAcc < period) return;
    if ((int)m_blobs.size() >= kOil3DMaxBlobs - 1) { m_turnAcc = period; return; }
    const uint32_t salt = XorShift(m_turnRng);
    auto hashIdx = [salt](uint32_t i) {
        uint32_t h = (i * 0x9E3779B9u) ^ salt;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        return h;
    };
    const float A = m_aspect;
    float oldest = 3.4e38f;
    for (const Blob3& b : m_blobs)
        if (b.rTarget > 0.0f && b.baseR >= 0.9f * b.rTarget && b.born < oldest) oldest = b.born;
    const float now = (float)m_time;
    const float dueBy = now - 0.5f * a.blobTurnoverS;
    int best = -1; int bestClear = 0; float bestBorn = 0.0f; uint32_t bestHash = 0;
    for (size_t i = 0; i < m_blobs.size(); i++) {
        const Blob3& b = m_blobs[i];
        if (b.rTarget <= 0.0f || b.baseR < 0.9f * b.rTarget) continue;
        if (!(b.born <= oldest || b.born <= dueBy)) continue;
        const float supOut = b.baseR * a.supportScale * (1.0f + a.breathAmt) * 0.86f
                           * m_cfg.radiusScale;
        const int clear = (b.y < -supOut || b.y > 1.0f + supOut ||
                           b.x < -supOut || b.x > A + supOut) ? 1 : 0;
        const uint32_t h = hashIdx((uint32_t)i);
        bool better = best < 0;
        if (!better) {
            if (clear != bestClear)      better = clear > bestClear;
            else if (b.born != bestBorn) better = b.born < bestBorn;
            else                         better = h < bestHash;
        }
        if (better) { best = (int)i; bestClear = clear; bestBorn = b.born; bestHash = h; }
    }
    if (best < 0) { m_turnAcc = period; return; }
    m_blobs[(size_t)best].rTarget = 0.0f;
    m_turnAcc -= period;
    if (m_turnAcc > period) m_turnAcc = period;
}

// blob_count, smoothly (src/fluid.cpp AdjustAcidBlobCount): a shortfall is
// grown in from under the bottom edge at 2% of its radius.
void Oil3DSim::AdjustCount() {
    const OilKeys& a = m_cfg.oil;
    const float A = m_aspect, D = m_cfg.depth;
    int live = 0;
    for (const Blob3& b : m_blobs) if (b.rTarget > 0.0f) live++;
    while (live > m_want) {
        int best = -1; float bestScore = -1e9f;
        for (size_t i = 0; i < m_blobs.size(); i++) {
            const Blob3& b = m_blobs[i];
            if (b.rTarget <= 0.0f) continue;
            const float off = fmaxf(fmaxf(-b.y, b.y - 1.0f), 0.0f);
            const float score = off * 10.0f - b.baseR;
            if (score > bestScore) { bestScore = score; best = (int)i; }
        }
        if (best < 0) break;
        m_blobs[(size_t)best].rTarget = 0.0f;
        live--;
    }
    while (live < m_want && (int)m_blobs.size() < kOil3DMaxBlobs) {
        Blob3 b{};
        const float u = Rf();
        float lo, hi, bias;
        if      (u < a.discFrac)              { b.kind = 0; b.wgt = 1.0f; lo = a.discMin;   hi = a.discMax;   bias = a.bigBias;  b.colIdx = 0; }
        else if (u < a.discFrac + a.webFrac)  { b.kind = 1; b.wgt = 1.0f; lo = a.webMin;    hi = a.webMax;    bias = a.bigBias;  b.colIdx = 1; }
        else if (u < a.discFrac + a.webFrac + a.bubbleFrac)
                                              { b.kind = 2; b.wgt = 1.0f; lo = a.bubbleMin; hi = a.bubbleMax; bias = a.sizeBias; b.colIdx = 3; }
        else                                  { b.kind = 3; b.wgt = -fmaxf(a.holeWeight, 0.05f);
                                                lo = a.holeMin; hi = a.holeMax; bias = a.sizeBias; b.colIdx = 0; }
        b.rTarget = lo + (hi - lo) * powf(Rf(), fmaxf(bias, 0.05f));
        b.baseR = b.rTarget * 0.02f;
        b.x = Rf() * A;
        b.y = (a.riseSpeed > 1e-6f && a.riseRespawn)
            ? 1.0f + b.rTarget * a.supportScale * (1.0f + a.breathAmt) * 0.86f * m_cfg.radiusScale
            : Rf();
        b.z = (0.10f + 0.80f * Rf()) * D;
        b.phase = Rf() * kTau;
        b.breathRate = 0.08f + Rf() * 0.24f;
        b.s1 = Rf() * kTau; b.s2 = Rf() * kTau; b.s3 = Rf() * kTau;
        b.born = (float)m_time;
        m_blobs.push_back(b);
        live++;
    }
}

void Oil3DSim::StepDrops(float dt) {
    if (m_drops.empty()) return;
    const float camDist = 0.5f / tanf(m_cfg.fovDeg * 0.5f * kPi / 180.0f);
    const float t = (float)m_time;
    for (Drop3& d : m_drops) {
        const float invS = camDist / (camDist + d.z);
        d.y -= d.climb * invS * dt;
        d.x += d.wob * d.climb * m_aspect * 0.5f * sinf(d.phase + 1.3f * t) * invS * dt;
        if (d.x < -0.05f) d.x += m_aspect + 0.1f;
        if (d.x > m_aspect + 0.05f) d.x -= m_aspect + 0.1f;
        if (d.y < -2.0f * d.r - 0.01f) RespawnDrop(d, true);
    }
}

// ---- palette clock --------------------------------------------------------
float Oil3DSim::AnchorWarpDeg(float u, float h0Deg, float w) const {
    if (fabsf(w - m_anchorCdfW) > 1e-6f) {
        BuildAnchorCdf(w, m_anchorCdf);
        m_anchorCdfW = w;
    }
    auto F = [&](float H) {
        const float x = H * (256.0f / 360.0f);
        int j = (int)floorf(x);
        if (j < 0) j = 0;
        if (j > 255) j = 255;
        const float tt = x - (float)j;
        return m_anchorCdf[j] + (m_anchorCdf[j + 1] - m_anchorCdf[j]) * tt;
    };
    auto Finv = [&](float y) {
        int lo = 0, hi = 256;
        while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (m_anchorCdf[mid] <= y) lo = mid; else hi = mid; }
        const float span = m_anchorCdf[lo + 1] - m_anchorCdf[lo];
        const float tt = span > 1e-9f ? (y - m_anchorCdf[lo]) / span : 0.0f;
        return 360.0f * ((float)lo + tt) / 256.0f;
    };
    float h0 = fmodf(h0Deg, 360.0f); if (h0 < 0.0f) h0 += 360.0f;
    float y = F(h0) + u;
    if (y >= 1.0f) y -= 1.0f;
    float d = Finv(y) - h0;
    if (d < 0.0f) d += 360.0f;
    if (u > 0.5f && d < 1e-3f) d = 360.0f;   // the wrap end, never a snap back
    return d;
}

// src/fluid.cpp EffectivePalette: the sweep cross-fade, then the global
// hue_rotate_period rotation (warped by hue_anchor_weight).
void Oil3DSim::EffectivePalette(float clk, float effOil[12], float effInk[12]) const {
    const OilKeys& a = m_cfg.oil;
    memcpy(effOil, a.oilColors, 12 * sizeof(float));
    memcpy(effInk, a.inkRamp, 12 * sizeof(float));
    if (a.hueSweepPeriod > 0.01f) {
        int np = a.sweepCount;
        if (np < 1) np = 1;
        if (np > OilKeys::kSweepMax) np = OilKeys::kSweepMax;
        const float u = fmodf(clk / a.hueSweepPeriod, 1.0f) * np;
        const int k0 = (int)u % np, k1 = (k0 + 1) % np;
        float f = u - floorf(u);
        f = f * f * (3.0f - 2.0f * f);
        float oilA[3], inkA[3];
        HsvLerp3(&a.sweepOil[k0 * 3], &a.sweepOil[k1 * 3], f, oilA);
        HsvLerp3(&a.sweepInk[k0 * 3], &a.sweepInk[k1 * 3], f, inkA);
        BuildAcidPalette(oilA, inkA, effOil, effInk);
        if (a.sweepOilFullSet[k0] || a.sweepOilFullSet[k1]) {
            float fam0[12], fam1[12], dummy[12];
            if (a.sweepOilFullSet[k0]) memcpy(fam0, &a.sweepOilFull[k0 * 12], sizeof(fam0));
            else BuildAcidPalette(&a.sweepOil[k0 * 3], &a.sweepInk[k0 * 3], fam0, dummy);
            if (a.sweepOilFullSet[k1]) memcpy(fam1, &a.sweepOilFull[k1 * 12], sizeof(fam1));
            else BuildAcidPalette(&a.sweepOil[k1 * 3], &a.sweepInk[k1 * 3], fam1, dummy);
            for (int ci = 0; ci < 4; ci++)
                HsvLerp3(&fam0[ci * 3], &fam1[ci * 3], f, &effOil[ci * 3]);
        }
    }
    const float rot = m_cfg.hueRotateOverride > 0.0f ? m_cfg.hueRotateOverride
                                                     : a.hueRotatePeriod;
    if (rot > 0.01f) {
        float deg = 360.0f * fmodf(clk / rot, 1.0f);
        if (a.hueAnchorWeight > 0.0005f) {
            float h0, s0, v0;
            RgbToHsv(&effOil[0], h0, s0, v0);
            deg = AnchorWarpDeg(fmodf(clk / rot, 1.0f), h0 * 360.0f, a.hueAnchorWeight);
        }
        for (int ci = 0; ci < 4; ci++) HsvHueShiftCpu(&effOil[ci * 3], deg);
    }
}

float Oil3DSim::PalettePhaseForHue(float hueDeg) const {
    const OilKeys& a = m_cfg.oil;
    float h0, s0, v0;
    RgbToHsv(&a.oilColors[0], h0, s0, v0);
    h0 *= 360.0f;
    float H = fmodf(hueDeg, 360.0f);
    if (H < 0.0f) H += 360.0f;
    if (!(a.hueAnchorWeight > 0.0005f)) {
        float d = H - h0;
        if (d < 0.0f) d += 360.0f;
        return d / 360.0f;
    }
    AnchorWarpDeg(0.0f, h0, a.hueAnchorWeight);   // (re)builds the table
    auto F = [&](float x0) {
        const float x = x0 * (256.0f / 360.0f);
        int j = (int)floorf(x);
        if (j < 0) j = 0;
        if (j > 255) j = 255;
        const float tt = x - (float)j;
        return m_anchorCdf[j] + (m_anchorCdf[j + 1] - m_anchorCdf[j]) * tt;
    };
    float u = F(H) - F(h0);
    if (u < 0.0f) u += 1.0f;
    if (u >= 1.0f) u -= 1.0f;
    return u;
}

// palette_start_hue (src/fluid.cpp ApplyPaletteStart): move the palette clock
// once, before anything is on screen, so the palette opens on that hue. -2 is
// the random start, drawn from the anchor density and seeded by --seed.
void Oil3DSim::ApplyPaletteStart() {
    const OilKeys& a = m_cfg.oil;
    m_palOffset = 0.0;
    const float want = a.paletteStartHue;
    const bool random = want <= -1.5f;
    if (!random && want < 0.0f) return;
    const bool sweep = a.hueSweepPeriod > 0.01f;
    const float rot = m_cfg.hueRotateOverride > 0.0f ? m_cfg.hueRotateOverride
                                                     : a.hueRotatePeriod;
    if (!(rot > 0.01f) && !sweep) return;
    float H = fmodf(want, 360.0f);
    if (random) {
        uint64_t x = (uint64_t)m_seed * 0x9E3779B97F4A7C15ull + 1ull * 0xD1B54A32D192ED03ull;
        x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27; x *= 0x94D049BB133111EBull;
        x ^= x >> 31;
        const float yv = (float)(unsigned)(x >> 40) * (1.0f / 16777216.0f);
        float cdf[257];
        BuildAnchorCdf(1.0f, cdf);
        int lo = 0, hi = 256;
        while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (cdf[mid] <= yv) lo = mid; else hi = mid; }
        const float span = cdf[lo + 1] - cdf[lo];
        const float tt = span > 1e-9f ? (yv - cdf[lo]) / span : 0.0f;
        H = 360.0f * ((float)lo + tt) / 256.0f;
    }
    if (H < 0.0f) H += 360.0f;
    if (H >= 360.0f) H -= 360.0f;
    auto angd = [](float x, float y) {
        float d = fabsf(fmodf(x - y, 360.0f));
        return d > 180.0f ? 360.0f - d : d;
    };
    auto hueAt = [&](float clk) {
        float eo[12], ei[12];
        EffectivePalette(clk, eo, ei);
        float h, s, v;
        RgbToHsv(&eo[0], h, s, v);
        return h * 360.0f;
    };
    float clk = 0.0f;
    if (!sweep) {
        clk = PalettePhaseForHue(H) * rot;
    } else {
        const float span = fminf(8.0f * fmaxf(rot, a.hueSweepPeriod), 30000.0f);
        const int NS = 8192;
        float bestD = 1e9f;
        for (int k = 0; k < NS; k++) {
            const float tk = span * (float)k / (float)NS;
            const float d = angd(hueAt(tk), H);
            if (d < bestD) { bestD = d; clk = tk; }
        }
        const float step = span / (float)NS, c0 = clk;
        for (int k = -64; k <= 64; k++) {
            const float tk = fmaxf(c0 + step * (float)k / 64.0f, 0.0f);
            const float d = angd(hueAt(tk), H);
            if (d < bestD) { bestD = d; clk = tk; }
        }
    }
    m_palOffset = -(double)clk;   // palette clock = time + clk
    Oil3DLog("[palette] start hue %s %.1f: palette clock %.1f s, hue now %.1f deg\n",
             random ? "RANDOM (seeded by --seed)" : "fixed", H, clk, hueAt(clk));
}

void Oil3DSim::Palette(FramePalette& p) const {
    const OilKeys& a = m_cfg.oil;
    EffectivePalette(PaletteClock(), p.oil, p.ink);
    if (fabsf(a.oilSaturation - 1.0f) > 0.001f) {
        for (int ci = 0; ci < 4; ci++) {
            float h, s, v;
            RgbToHsv(&p.oil[ci * 3], h, s, v);
            RGB c = HSVtoRGB(h, Clamp01(s * a.oilSaturation), v);
            p.oil[ci * 3 + 0] = c.r; p.oil[ci * 3 + 1] = c.g; p.oil[ci * 3 + 2] = c.b;
        }
    }
    float fh, fs, fv;
    RgbToHsv(&p.oil[0], fh, fs, fv);
    p.paletteHueDeg = fh * 360.0f;

    // film hues (+ the wobble: two incommensurate sines, src/fluid.cpp Hue2Deg)
    p.hue2Deg = a.filmHue2;
    if (fabsf(a.filmHue2Wobble) > 0.001f) {
        const float P = fmaxf(a.filmHue2WobbleP, 5.0f);
        const float th = (float)m_time;
        p.hue2Deg += a.filmHue2Wobble * (0.62f * sinf(kTau * th / P)
                                       + 0.38f * sinf(kTau * th / (P * 0.61803399f) + 1.7f));
    }
    p.hue2Amt = a.filmHue2Amt;
    p.hue3Deg = a.filmHue3;
    p.hue3Amt = a.filmHue3Amt;

    // dye of the masses (wall + cavities) and of the droplets (beads)
    auto dyeHueN = [&](float hue) {
        if (a.dyeHueFollow) hue += fh * 360.0f;
        float hn = fmodf(hue, 360.0f);
        if (hn < 0.0f) hn += 360.0f;
        return hn / 360.0f;
    };
    p.massAmt = 0.0f;
    if (a.dyeSat > 1e-4f && a.dyeLum > 1e-4f) {
        RGB c = HSVtoRGB(dyeHueN(a.dyeHue), Clamp01(a.dyeSat), 1.0f);
        p.massRgb[0] = c.r; p.massRgb[1] = c.g; p.massRgb[2] = c.b;
        p.massAmt = Clamp01(a.dyeLum) * Clamp01(a.dyeMasses);
    }
    p.dropAmt = 0.0f;
    {
        const float dSat = (a.dyeDropSat >= 0.0f) ? a.dyeDropSat : a.dyeSat;
        const float dLum = (a.dyeDropLum >= 0.0f) ? a.dyeDropLum : a.dyeLum;
        if (dSat > 1e-4f && dLum > 1e-4f) {
            const float hue = (a.dyeDropHue >= 0.0f) ? a.dyeDropHue : a.dyeHue;
            RGB c = HSVtoRGB(dyeHueN(hue), Clamp01(dSat), 1.0f);
            p.dropRgb[0] = c.r; p.dropRgb[1] = c.g; p.dropRgb[2] = c.b;
            p.dropAmt = Clamp01(dLum) * Clamp01(a.dyeDroplets);
        }
    }

    // split tone: the black role takes the OKLab complement of the main hue
    // (or shadow_tone_hue), lifted by at most shadow_tone_lift, gated on its
    // own clock (src/fluid.cpp:6610)
    p.toneAdd[0] = p.toneAdd[1] = p.toneAdd[2] = 0.0f;
    {
        const float amt  = Clamp01(a.shadowTone);
        const float lift = fminf(fmaxf(a.shadowToneLift, 0.0f), 0.25f);
        const float g = ShadowToneGate(m_time, a.shadowTonePeriod, a.shadowToneFade);
        if (amt > 1e-4f && lift > 1e-6f && g > 1e-5f) {
            const float k   = lift * g * amt;
            const float sat = Clamp01(a.shadowToneSat);
            const float tgt = BuOkHueEnc(p.oil[0], p.oil[1], p.oil[2]) + kPi;
            auto err = [&](float hh) {
                const RGB c = HSVtoRGB(hh - floorf(hh), sat, 1.0f);
                float d = BuOkHueEnc(c.r * k, c.g * k, c.b * k) - tgt;
                d = fmodf(d, kTau);
                if (d >  kPi) d -= kTau;
                if (d < -kPi) d += kTau;
                return fabsf(d);
            };
            float best = fh + 0.5f, be = 1e9f;
            if (a.shadowToneHue >= 0.0f) {
                best = fmodf(a.shadowToneHue, 360.0f) / 360.0f;
            } else if (sat > 1e-3f) {
                for (int i = 0; i < 96; i++) {
                    const float hh = i / 96.0f, e = err(hh);
                    if (e < be) { be = e; best = hh; }
                }
                const float c0 = best;
                for (int i = -24; i <= 24; i++) {
                    const float hh = c0 + i / 4608.0f, e = err(hh);
                    if (e < be) { be = e; best = hh; }
                }
            }
            const RGB c = HSVtoRGB(best - floorf(best), sat, 1.0f);
            p.toneAdd[0] = c.r * k; p.toneAdd[1] = c.g * k; p.toneAdd[2] = c.b * k;
        }
    }
}

void Oil3DSim::LogState(const char* tag) const {
    int live = 0, onScreen = 0, holes = 0;
    float minY = 1e9f, maxY = -1e9f, sumY = 0.0f, minZ = 1e9f, maxZ = -1e9f;
    for (const Blob3& b : m_blobs) {
        if (b.rTarget <= 0.0f) continue;
        live++;
        if (b.wgt < 0.0f) holes++;
        if (b.y > -0.05f && b.y < 1.05f && b.x > -0.05f && b.x < m_aspect + 0.05f) onScreen++;
        minY = fminf(minY, b.y); maxY = fmaxf(maxY, b.y); sumY += b.y;
        minZ = fminf(minZ, b.z); maxZ = fmaxf(maxZ, b.z);
    }
    Oil3DLog("[sim] %s t=%.1fs blobs live=%d (holes %d) on-screen=%d  y[min %.3f mean %.3f "
             "max %.3f]  z[%.3f..%.3f]  drops=%d\n", tag, m_time, live, holes, onScreen,
             minY, live ? sumY / live : 0.0f, maxY, minZ, maxZ, (int)m_drops.size());
    for (size_t i = 0; i < m_blobs.size() && i < 3; i++) {
        const Blob3& b = m_blobs[i];
        Oil3DLog("[sim]   blob %d kind=%d r=%.3f pos=(%.3f, %.3f, %.3f) v=(%.4f, %.4f, %.4f)\n",
                 (int)i, b.kind, b.baseR, b.x, b.y, b.z, b.vx, b.vy, b.vz);
    }
}

// ===========================================================================
// Oil3DRenderer
// ===========================================================================
namespace {

// Slots of the constant array C[64] (shaders.h CB_* defines mirror these).
enum CbSlot {
    S_SCREEN = 0, S_CAMPOS = 1, S_CAMR = 2, S_CAMD = 3, S_CAMF = 4, S_MARCH = 5,
    S_L0 = 6, S_L1 = 7, S_SHADE = 8, S_SHADE2 = 9, S_SHADE3 = 10, S_FLAT = 11,
    S_PAL0 = 12,
    S_HUE = 16, S_HUEB = 17, S_HUEC = 18, S_HUED = 19,
    S_INK0 = 20, S_INK1 = 21, S_INK2 = 22, S_INK3 = 23, S_RAMP0 = 24,
    S_TONE = 28, S_DROP = 29, S_MASS = 30, S_MISC = 31, S_MISC2 = 32,
    S_DISP = 33, S_DISP2 = 34,
    S_COUNT = 64
};

// Must match `struct Blob` / `struct Drop` in shaders.h.
struct BlobGPU { float a[4], b[4], c[4]; };
struct DropGPU { float a[4], b[4]; };
static_assert(sizeof(BlobGPU) == 48, "BlobGPU must match the HLSL struct (3 float4)");
static_assert(sizeof(DropGPU) == 32, "DropGPU must match the HLSL struct (2 float4)");

float HalfToFloat(uint16_t h) {   // copied from src_ink3d/ink3d.cpp
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

ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target,
                         const char* name) {
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3DCompile(src, strlen(src), name, nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
        Oil3DLog("shader '%s' %s (%s) compile error:\n%s\n", name, entry, target,
                 err ? (char*)err->GetBufferPointer() : "?");
        ExitProcess(1);
    }
    return blob;
}

void Norm3(float v[3]) {
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l < 1e-6f) { v[0] = 0; v[1] = 0; v[2] = 1; return; }
    v[0] /= l; v[1] /= l; v[2] /= l;
}
void Cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

} // namespace

void Oil3DRenderer::InitOffscreen(int width, int height, const Oil3DConfig& cfg, unsigned seed) {
    m_cfg = cfg;
    m_width = width; m_height = height;
    m_rayW = cfg.halfRes ? (width + 1) / 2 : width;
    m_rayH = cfg.halfRes ? (height + 1) / 2 : height;
    m_sim.Init(cfg, seed, (float)width / (float)(height > 0 ? height : 1));

    CreateDevice();
    CreatePipelines();
    CreateTargets();
    CreateUploads();
    Oil3DLog("[oil3d] target %dx%d  raymarch %dx%d  steps=%d refine=%d shade=%s fov=%.1f "
             "depth=%.2f\n", m_width, m_height, m_rayW, m_rayH, cfg.steps, cfg.refine,
             cfg.shade == OIL3D_FLAT ? "flat" : "gloss", cfg.fovDeg, cfg.depth);
}

void Oil3DRenderer::CreateDevice() {
    HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));
    ComPtr<IDXGIAdapter1> adapter;
    HR(m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad = {};
    adapter->GetDesc1(&ad);
    Oil3DLog("[oil3d] GPU: %ls (%llu MB)\n", ad.Description,
             (unsigned long long)(ad.DedicatedVideoMemory >> 20));
    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)));
    m_queue->GetTimestampFrequency(&m_tsFreq);

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 2;                 // 0 = shot target, 1 = raymarch target
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
    sh.NumDescriptors = kFrames * 4;       // per frame: CBV, blobs, drops, ray SRV
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)));

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

void Oil3DRenderer::CreatePipelines() {
    // ONE root signature for both passes: a single descriptor table per frame
    //   b0 CBV (the C[64] block), t0 blobs, t1 droplets, t2 raymarch result
    // + a static linear-clamp sampler s0. (Root CBV/SRV descriptors were the
    // first cut; one table is just as standard and also runs on vkd3d, which
    // is how the cloud executor smoke-tested this build.)
    {
        D3D12_DESCRIPTOR_RANGE r[2] = {
            { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, 0 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 1 },
        };
        D3D12_ROOT_PARAMETER p[1] = {};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[0].DescriptorTable = { 2, r };
        p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC s = {};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd = { 1, p, 1, &s, D3D12_ROOT_SIGNATURE_FLAG_NONE };
        ComPtr<ID3DBlob> sig, err;
        HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        if (FAILED(hr)) {
            Oil3DLog("root signature: %s\n", err ? (char*)err->GetBufferPointer() : "?");
            Oil3DFail("D3D12SerializeRootSignature", hr);
        }
        HR(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&m_rs)));
    }
    auto gfx = [&](const char* src, const char* name, ComPtr<ID3D12PipelineState>& pso) {
        ComPtr<ID3DBlob> vs = Compile(src, "VSMain", "vs_5_1", name);
        ComPtr<ID3DBlob> ps = Compile(src, "PSMain", "ps_5_1", name);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_rs.Get();
        d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        HR(m_device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)));
    };
    gfx(kRaymarchSrc, "oil3d_raymarch", m_psoRaymarch);
    gfx(kDisplaySrc, "oil3d_display", m_psoDisplay);
}

void Oil3DRenderer::CreateTargets() {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto makeRT = [&](int w, int h, ComPtr<ID3D12Resource>& res, int rtvSlot) {
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                             IID_PPV_ARGS(&res)));
        auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (SIZE_T)rtvSlot * m_rtvStride;
        m_device->CreateRenderTargetView(res.Get(), nullptr, rtv);
    };
    makeRT(m_width, m_height, m_shotTex, 0);
    makeRT(m_rayW, m_rayH, m_rayTex, 1);

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

void Oil3DRenderer::CreateUploads() {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    auto make = [&](UINT64 bytes, ComPtr<ID3D12Resource>& res, void*& ptr) {
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (bytes + 255) & ~255ull;
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HR(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&res)));
        D3D12_RANGE none = { 0, 0 };
        HR(res->Map(0, &none, &ptr));
        memset(ptr, 0, (size_t)rd.Width);
    };
    const UINT inc = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i = 0; i < kFrames; i++) {
        make(kCbBytes, m_cbUp[i], m_cbPtr[i]);
        make(sizeof(BlobGPU) * kOil3DMaxBlobs, m_blobUp[i], m_blobPtr[i]);
        make(sizeof(DropGPU) * kOil3DMaxDrops, m_dropUp[i], m_dropPtr[i]);

        // the frame's table: [CBV b0, SRV t0 blobs, SRV t1 drops, SRV t2 ray]
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)(i * 4) * inc;
        D3D12_CONSTANT_BUFFER_VIEW_DESC cv = {};
        cv.BufferLocation = m_cbUp[i]->GetGPUVirtualAddress();
        cv.SizeInBytes = kCbBytes;
        m_device->CreateConstantBufferView(&cv, h);
        h.ptr += inc;
        auto structured = [&](ID3D12Resource* res, UINT count, UINT stride) {
            D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = DXGI_FORMAT_UNKNOWN;
            sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sv.Buffer.FirstElement = 0;
            sv.Buffer.NumElements = count;
            sv.Buffer.StructureByteStride = stride;
            m_device->CreateShaderResourceView(res, &sv, h);
            h.ptr += inc;
        };
        structured(m_blobUp[i].Get(), kOil3DMaxBlobs, (UINT)sizeof(BlobGPU));
        structured(m_dropUp[i].Get(), kOil3DMaxDrops, (UINT)sizeof(DropGPU));
        D3D12_SHADER_RESOURCE_VIEW_DESC tv = {};
        tv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        tv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        tv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tv.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_rayTex.Get(), &tv, h);

        m_table[i] = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_table[i].ptr += (UINT64)(i * 4) * inc;
    }
}

// ---------------------------------------------------------------------------
// frame plumbing (src_ink3d/ink3d.cpp)
// ---------------------------------------------------------------------------
void Oil3DRenderer::Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES& cur,
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

void Oil3DRenderer::WaitForGpuIdle() {
    const UINT64 v = m_nextFence++;
    HR(m_queue->Signal(m_fence.Get(), v));
    if (m_fence->GetCompletedValue() < v) {
        HR(m_fence->SetEventOnCompletion(v, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void Oil3DRenderer::BeginFrame() {
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

void Oil3DRenderer::EndFrame() {
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

void Oil3DRenderer::Mark(const char* name) {
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

void Oil3DRenderer::CollectTimings(UINT slot) {
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

void Oil3DRenderer::ResetGpuStats() {
    for (UINT i = 0; i < kMaxTs; i++) { m_passSum[i] = 0.0; m_passMin[i] = 1e9; }
    m_passSamples = 0;
}

double Oil3DRenderer::FrameMsMean() const {
    double s = 0.0;
    for (size_t i = 0; i < m_passNames.size(); i++) s += PassMsMean((int)i);
    return s;
}

// ---------------------------------------------------------------------------
// per-frame upload: camera, palette, blobs, droplets
// ---------------------------------------------------------------------------
void Oil3DRenderer::UploadFrame(float sdrScale) {
    const UINT fi = m_frameIndex;
    const OilKeys& a = m_cfg.oil;
    const float A = (float)m_width / (float)m_height;
    const float D = m_cfg.depth;
    const float t = (float)m_sim.Time();
    const float tanH = tanf(m_cfg.fovDeg * 0.5f * kPi / 180.0f);
    const float camDist = 0.5f / tanH;   // the front plane fills the frame exactly

    FramePalette pal;
    m_sim.Palette(pal);

    float C[S_COUNT][4] = {};
    auto set = [&](int s, float x, float y, float z, float w) {
        C[s][0] = x; C[s][1] = y; C[s][2] = z; C[s][3] = w;
    };

    // ---- camera: a very slow orbit about the slab centre (OLED: nothing
    // sits perfectly still), always looking at the middle of the slab
    float cam[3] = { A * 0.5f, 0.5f, -camDist };
    {
        const float P = fmaxf(m_cfg.cameraDriftPeriod, 5.0f);
        const float dr = m_cfg.cameraDrift;
        cam[0] += dr * sinf(kTau * t / P);
        cam[1] += 0.6f * dr * sinf(kTau * t / (P * 1.37f) + 1.0f);
    }
    float tgt[3] = { A * 0.5f, 0.5f, D * 0.5f };
    float fwd[3] = { tgt[0] - cam[0], tgt[1] - cam[1], tgt[2] - cam[2] };
    Norm3(fwd);
    const float worldDown[3] = { 0.0f, 1.0f, 0.0f };
    float right[3], down[3];
    Cross3(worldDown, fwd, right);       // y x z = x
    Norm3(right);
    Cross3(fwd, right, down);            // z x x = y
    Norm3(down);

    set(S_SCREEN, 1.0f / m_rayW, 1.0f / m_rayH, A, t);
    set(S_CAMPOS, cam[0], cam[1], cam[2], tanH);
    set(S_CAMR, right[0], right[1], right[2], D);
    set(S_CAMD, down[0], down[1], down[2], a.threshold);

    // ---- blobs: sim (X, Y, Z) -> world through the NOMINAL frustum, so the
    // sim's screen position is the projected one at every depth
    BlobGPU* bg = (BlobGPU*)m_blobPtr[fi];
    const std::vector<Blob3>& blobs = m_sim.Blobs();
    int nb = 0;
    for (size_t i = 0; i < blobs.size() && nb < kOil3DMaxBlobs; i++) {
        const Blob3& b = blobs[i];
        const float r = b.baseR * (1.0f + a.breathAmt * sinf(b.phase)) * m_cfg.radiusScale;
        const float s = r * a.supportScale;
        if (s < 1e-5f) continue;
        const float sz = (camDist + b.z) / camDist;
        BlobGPU& g = bg[nb++];
        g.a[0] = A * 0.5f + (b.x - A * 0.5f) * sz;
        g.a[1] = 0.5f + (b.y - 0.5f) * sz;
        g.a[2] = b.z;
        g.a[3] = s;
        float rgb[3];
        m_sim.BlobColor((int)i, pal, rgb);
        g.b[0] = rgb[0]; g.b[1] = rgb[1]; g.b[2] = rgb[2];
        g.b[3] = b.wgt;
        const float e  = 1.0f + (b.wgt > 0.0f ? m_sim.Stretch(b) : 0.0f);
        const float ez = b.wgt > 0.0f ? 1.0f : fmaxf(m_cfg.holeDepthStretch, 1.0f);
        g.c[0] = 1.0f / e;
        g.c[1] = 1.0f / ez;
        g.c[2] = 1.0f / (s * s);
        g.c[3] = 1.717f * fabsf(b.wgt) / s;
    }
    set(S_CAMF, fwd[0], fwd[1], fwd[2], (float)nb);

    DropGPU* dg = (DropGPU*)m_dropPtr[fi];
    const std::vector<Drop3>& drops = m_sim.Drops();
    int nd = 0;
    for (size_t i = 0; i < drops.size() && nd < kOil3DMaxDrops; i++) {
        const Drop3& d = drops[i];
        const float sz = (camDist + d.z) / camDist;
        DropGPU& g = dg[nd++];
        g.a[0] = A * 0.5f + (d.x - A * 0.5f) * sz;
        g.a[1] = 0.5f + (d.y - 0.5f) * sz;
        g.a[2] = d.z;
        g.a[3] = d.r;
        g.b[0] = (float)d.racer; g.b[1] = d.phase; g.b[2] = 0.0f; g.b[3] = 0.0f;
    }
    set(S_MARCH, (float)m_cfg.steps, (float)m_cfg.refine, (float)nd, m_cfg.edgeSoft);

    // ---- lights / shading
    float l0[3] = { m_cfg.light0Dir[0], m_cfg.light0Dir[1], m_cfg.light0Dir[2] };
    float l1[3] = { m_cfg.light1Dir[0], m_cfg.light1Dir[1], m_cfg.light1Dir[2] };
    Norm3(l0); Norm3(l1);
    set(S_L0, l0[0], l0[1], l0[2], m_cfg.light0);
    set(S_L1, l1[0], l1[1], l1[2], m_cfg.light1);
    // mass_rim (the 2D lamp-side refracted rim on a mass edge) scales the rim
    const float rimAmt = m_cfg.rim * (a.massRim > 1e-4f ? 0.65f + a.massRim : 0.65f);
    set(S_SHADE, (float)m_cfg.shade, m_cfg.spec, m_cfg.specPower, rimAmt);
    set(S_SHADE2, m_cfg.rimPower, m_cfg.sss, m_cfg.sssSigma, m_cfg.ao);
    set(S_SHADE3, m_cfg.fog, m_cfg.wrap, m_cfg.ambient, m_cfg.exposure);
    set(S_FLAT, (float)m_cfg.flatBands, fmaxf(m_cfg.flatSoft, 0.02f), m_cfg.flatRimWidth,
        m_cfg.flatRimDark);
    set(S_PAL0, pal.oil[0], pal.oil[1], pal.oil[2], 0.0f);

    // ---- scheme hues: the patch field rises with the wax (+ film_hue2_rise)
    {
        const float seam = (fabsf(a.filmHue2Seam - 1.0f) < 1e-6f) ? 1.0f
                         : fminf(fmaxf(a.filmHue2Seam, 0.1f), 1.0f);
        const float freq = 0.6f / fmaxf(a.filmHue2Scale, 0.05f);
        const float rise = t * (a.riseSpeed + a.filmHue2Rise / 60.0f);
        const float drift = t * 0.004f * a.filmHue2Drift;
        set(S_HUE, pal.hue2Deg, a.filmHue2Amt, pal.hue3Deg, a.filmHue3Amt);
        set(S_HUEB, seam, Clamp01(a.filmHue3Share), a.filmHue2Cover, freq);
        set(S_HUEC, rise, a.filmEqualLoad, a.filmEqualLoadP, Clamp01(a.filmLevel));
        // the rim takes the SEAM colour (half the hue2 rotation): the accent
        const float rimHue = a.filmHue2Amt > 0.0005f ? 0.5f * pal.hue2Deg * Clamp01(a.filmHue2Amt)
                                                     : 0.0f;
        set(S_HUED, m_cfg.mixContrast, rimHue, drift * 0.8f, drift * 0.6f);
    }

    // ---- ink (the far wall)
    {
        float marble = m_cfg.inkMarble;
        if (marble < 0.0f) {
            // auto: in water mode the 2D water only carries ink when splats or
            // drops put some there; the live stages have both off -> black
            if (a.inkMode == 1) marble = (m_cfg.idleSplats == 0 && m_cfg.dropsOn == 0) ? 0.0f : 0.35f;
            else                marble = 0.6f;
        }
        set(S_INK0, m_cfg.ink.paper[0], m_cfg.ink.paper[1], m_cfg.ink.paper[2], marble);
        set(S_INK1, m_cfg.ink.tintThin[0], m_cfg.ink.tintThin[1], m_cfg.ink.tintThin[2],
            m_cfg.inkMarbleScale);
        set(S_INK2, m_cfg.ink.tintThick[0], m_cfg.ink.tintThick[1], m_cfg.ink.tintThick[2],
            t * m_cfg.inkFlow);
        set(S_INK3, (float)a.inkMode, a.inkLevels, fmaxf(a.inkSoft, 0.02f), 0.0f);
        for (int k = 0; k < 4; k++)
            set(S_RAMP0 + k, pal.ink[k * 3 + 0], pal.ink[k * 3 + 1], pal.ink[k * 3 + 2], 0.0f);
    }
    set(S_TONE, pal.toneAdd[0], pal.toneAdd[1], pal.toneAdd[2], 0.0f);
    set(S_DROP, pal.dropRgb[0], pal.dropRgb[1], pal.dropRgb[2], pal.dropAmt);
    set(S_MASS, pal.massRgb[0], pal.massRgb[1], pal.massRgb[2], pal.massAmt);
    set(S_MISC, a.riseBottomLight, m_cfg.cavityDark, fminf(fmaxf(a.dyeCore, 0.0f), 1.0f),
        a.postChroma);
    set(S_MISC2, a.postLift, (float)m_cfg.debugView, 0.0f, 0.0f);

    // ---- display: peak gain lifts only the hot speculars; sdrScale is 1.0
    // when Windows HDR is off, and then the gain is 1 too
    const float peakGain = (m_cfg.peakNits > 0.0f && sdrScale > 1.0f)
                         ? fmaxf(m_cfg.peakNits / (sdrScale * 80.0f), 1.0f) : 1.0f;
    set(S_DISP, sdrScale, peakGain, m_cfg.knee, 0.0f);
    set(S_DISP2, 1.0f / m_width, 1.0f / m_height, 0.0f, 0.0f);

    memcpy(m_cbPtr[fi], C, sizeof(C));
}

void Oil3DRenderer::Frame(float dt, float sdrScale) {
    m_sim.Step(dt);
    BeginFrame();
    UploadFrame(sdrScale);
    Mark("raymarch");
    RenderRaymarch();
    Mark("display");
    RenderDisplay();
    Mark(nullptr);
    EndFrame();
}

void Oil3DRenderer::RenderRaymarch() {
    const UINT fi = m_frameIndex;
    Barrier(m_rayTex.Get(), m_rayState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_rtvStride;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_rayW, (float)m_rayH, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_rayW, m_rayH };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);
    m_cmd->SetGraphicsRootSignature(m_rs.Get());
    m_cmd->SetPipelineState(m_psoRaymarch.Get());
    // t2 (the display pass's input) is in the table too; this shader never
    // declares it, so the ray target being a render target here is fine
    m_cmd->SetGraphicsRootDescriptorTable(0, m_table[fi]);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);
}

void Oil3DRenderer::RenderDisplay() {
    const UINT fi = m_frameIndex;
    Barrier(m_rayTex.Get(), m_rayState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Barrier(m_shotTex.Get(), m_shotState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    D3D12_RECT sc = { 0, 0, m_width, m_height };
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);
    m_cmd->SetGraphicsRootSignature(m_rs.Get());
    m_cmd->SetPipelineState(m_psoDisplay.Get());
    m_cmd->SetGraphicsRootDescriptorTable(0, m_table[fi]);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// readback (src_ink3d/ink3d.cpp CaptureOffscreen)
// ---------------------------------------------------------------------------
bool Oil3DRenderer::CaptureOffscreen(std::vector<float>& out) {
    WaitForGpuIdle();
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
    // the capture used allocator 0 outside the ring: restart the ring
    for (UINT i = 0; i < kFrames; i++) m_fenceValues[i] = 0;
    m_frameIndex = 0;
    return true;
}

void Oil3DRenderer::Shutdown() {
    if (m_device) WaitForGpuIdle();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}
