// Ink3DWallpaper — HLSL sources (compiled at startup with D3DCompile).
//
//   kSimSrc       every compute kernel, one root signature (cs_5_1)
//   kRaymarchSrc  fullscreen-triangle PS, Beer-Lambert front-to-back
//   kDisplaySrc   fullscreen-triangle PS, hue + HDR knee/peak + sdrScale
//
// CONVENTIONS (see config.h):
//   * +y is DOWN. Grid row 0 is the top face of the box; gravity/buoyancy
//     pushes ink toward +y, which is where --shot-drop's VY=80 sends it.
//   * velocity is in velocity-grid voxels/second; the density grid is
//     velScale times finer, so a density-grid displacement is v*velScale*dt.
//   * the pressure solve follows the GPU-Gems convention: divergence is the
//     raw central difference and the gradient subtract uses the same 0.5
//     factor, so dt and the cell size cancel. h2 is 1 on the finest level and
//     4^level on the multigrid coarse levels.
//   * the render box is normalized: (1.0, 0.5625, 0.5).
#pragma once

static const char* kSimSrc = R"hlsl(
cbuffer CB : register(b0) {
    int3   dims;        float dt;              // DESTINATION grid dims
    float3 invDims;     float dissipation;
    float  vorticity;   float buoyancy;  float noiseAmt;  float sharpen;
    int3   dropMin;     float dropRadius;
    float3 dropCenter;  float dropAmount;      // in DESTINATION voxel units
    float3 dropVel;     uint  seed;
    int    passIdx;     float velScale;  float dissipationFast; float decayThreshold;
    int3   velDims;     float sinkBottom;
    float3 invVelDims;  float h2;
    float  omega;       float timeSec;   float lightStepX; float lightStepY;
    float  lightSigma;  float densityCap; float velDissipation; float sinkRate;
    float4 pad0;
};

Texture3D<float4>   gA4  : register(t0);   // velocity / advection tmp
Texture3D<float>    gB1  : register(t1);   // density / pressure / fine residual
Texture3D<float4>   gC4  : register(t2);   // curl, or src for MacCormack
Texture3D<float>    gD1  : register(t3);   // rhs, or src density for MacCormack
RWTexture3D<float4> gOut4  : register(u0);
RWTexture3D<float>  gOut1  : register(u1);
RWTexture3D<float>  gOut1b : register(u2);
SamplerState gSamp : register(s0);

bool OutOfRange(uint3 id) { return any(id >= (uint3)dims); }
float3 CellUvw(uint3 id)  { return ((float3)id + 0.5) * invDims; }
int3   ClampC(int3 c)     { return clamp(c, int3(0,0,0), dims - 1); }

// one seeded hash for every random value in the app (--shot determinism)
float Hash13(float3 p, uint s) {
    p = frac(p * 0.3183099 + float3(0.1, 0.2, 0.3) + (float)(s & 0xffffu) * 0.0001);
    p += dot(p, p.yzx + 19.19);
    return frac((p.x + p.y) * p.z);
}
float VNoise(float3 p, uint s) {
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = Hash13(i + float3(0,0,0), s), n100 = Hash13(i + float3(1,0,0), s);
    float n010 = Hash13(i + float3(0,1,0), s), n110 = Hash13(i + float3(1,1,0), s);
    float n001 = Hash13(i + float3(0,0,1), s), n101 = Hash13(i + float3(1,0,1), s);
    float n011 = Hash13(i + float3(0,1,1), s), n111 = Hash13(i + float3(1,1,1), s);
    return lerp(lerp(lerp(n000,n100,f.x), lerp(n010,n110,f.x), f.y),
                lerp(lerp(n001,n101,f.x), lerp(n011,n111,f.x), f.y), f.z);
}

// ---------------------------------------------------------------------------
// clears / static scene
// ---------------------------------------------------------------------------
[numthreads(8,8,4)]
void CSClear4(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    gOut4[id] = float4(0,0,0,0);
}
[numthreads(8,8,4)]
void CSClear1(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    gOut1[id] = 0.0;
}

// M0: an analytic soft sphere plus a thin tilted sheet written straight into
// the density grid. No solver runs, so this isolates the raymarcher.
[numthreads(8,8,4)]
void CSInitScene(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 w = CellUvw(id) * float3(1.0, 0.5625, 0.5);   // round, not ellipsoid

    float3 sc = float3(0.42, 0.27, 0.25);
    float  sr = 0.115;
    float  rho = smoothstep(sr, sr * 0.90, length(w - sc));

    // Thin tilted SHEET (not a second sphere — it is meant to read as an
    // elongated slab seen at a glancing angle). Its normal lies mostly in the
    // screen plane, so the camera cuts it obliquely.
    //
    // Thickness 2.2 density voxels, not 1.2: at 1.2 the analytic plane is
    // thinner than the sampling lattice, so which voxel centres land on it
    // depends on the row — the classic Nyquist beat, and it painted a stripe
    // pattern that survived 4x the ray steps unchanged (steptest-96 vs
    // steptest-384 are pixel-identical in that region). That was grid
    // aliasing of the TEST OBJECT, never the raymarcher.
    float3 n  = normalize(float3(0.88, 0.44, 0.18));
    float3 c2 = float3(0.70, 0.30, 0.25);
    float3 r  = w - c2;
    float  th = 2.2 * (0.5 / max((float)dims.z, 1.0));
    float  sheet = saturate(1.0 - abs(dot(r, n)) / th);
    sheet *= saturate(1.0 - length(r - dot(r, n) * n) / 0.17);
    rho = max(rho, sheet * 0.85);

    gOut1[id] = min(rho, densityCap);
}

// ---------------------------------------------------------------------------
// injection. Full-grid ping-pong: drops are rare (one per 8-20 s), so
// correctness beats the bounding-box dispatch the plan sketched.
// noiseAmt carries drop_edge_noise for these two kernels only.
// ---------------------------------------------------------------------------
[numthreads(8,8,4)]
void CSInjectVel(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 d = ((float3)id + 0.5) - dropCenter;
    float  t = saturate(1.0 - dot(d,d) / max(dropRadius * dropRadius, 1e-6));
    float  fall = t * t;
    float3 v = gA4.Load(int4((int3)id, 0)).xyz;
    gOut4[id] = float4(lerp(v, dropVel, fall), 0);   // SET inside the sphere
}

[numthreads(8,8,4)]
void CSInjectDensity(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 d = ((float3)id + 0.5) - dropCenter;
    // smooth radial noise on the edge: a perfectly symmetric sphere makes a
    // perfectly symmetric ring that never goes unstable (plan §2), so this
    // asymmetry is what seeds the RT lobes and KH curls.
    float  n  = VNoise(normalize(d + 1e-4) * 3.7 + dropCenter * 0.013, seed);
    float  rr = dropRadius * (1.0 + noiseAmt * (n * 2.0 - 1.0));
    float  t  = saturate(1.0 - dot(d,d) / max(rr * rr, 1e-6));
    gOut1[id] = min(densityCap, gB1.Load(int4((int3)id, 0)) + dropAmount * t * t);
}

// ---------------------------------------------------------------------------
// curl
// ---------------------------------------------------------------------------
[numthreads(8,8,4)]
void CSCurl(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float3 vR = gA4.Load(int4(ClampC(c + int3(1,0,0)), 0)).xyz;
    float3 vL = gA4.Load(int4(ClampC(c - int3(1,0,0)), 0)).xyz;
    float3 vU = gA4.Load(int4(ClampC(c + int3(0,1,0)), 0)).xyz;
    float3 vD = gA4.Load(int4(ClampC(c - int3(0,1,0)), 0)).xyz;
    float3 vF = gA4.Load(int4(ClampC(c + int3(0,0,1)), 0)).xyz;
    float3 vB = gA4.Load(int4(ClampC(c - int3(0,0,1)), 0)).xyz;
    float3 om = 0.5 * float3((vU.z - vD.z) - (vF.y - vB.y),
                             (vF.x - vB.x) - (vR.z - vL.z),
                             (vR.y - vL.y) - (vU.x - vD.x));
    gOut4[id] = float4(om, length(om));
}

// ---------------------------------------------------------------------------
// forces: vorticity confinement + buoyancy + ambient curl noise
// ---------------------------------------------------------------------------
// divergence-free ambient drift: the analytic curl of a smooth sinusoidal
// vector potential (finite-differenced). Seeded, time-varying, deterministic.
float3 Psi(float3 p, float t, float3 ph) {
    return float3(sin(p.y + ph.x + t * 0.11) * cos(p.z * 0.83 + ph.y),
                  sin(p.z + ph.y + t * 0.13) * cos(p.x * 0.91 + ph.z),
                  sin(p.x + ph.z + t * 0.09) * cos(p.y * 0.79 + ph.x));
}
float3 CurlNoise(float3 p, float t, uint s) {
    float3 ph = float3(Hash13(float3(1,2,3), s), Hash13(float3(4,5,6), s),
                       Hash13(float3(7,8,9), s)) * 6.2831853;
    const float e = 0.35;
    float3 dx = (Psi(p + float3(e,0,0), t, ph) - Psi(p - float3(e,0,0), t, ph)) / (2*e);
    float3 dy = (Psi(p + float3(0,e,0), t, ph) - Psi(p - float3(0,e,0), t, ph)) / (2*e);
    float3 dz = (Psi(p + float3(0,0,e), t, ph) - Psi(p - float3(0,0,e), t, ph)) / (2*e);
    return float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x);
}

[numthreads(8,8,4)]
void CSForces(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float4 om = gC4.Load(int4(c, 0));              // t2 = curl

    float3 eta = 0.5 * float3(
        gC4.Load(int4(ClampC(c + int3(1,0,0)), 0)).w - gC4.Load(int4(ClampC(c - int3(1,0,0)), 0)).w,
        gC4.Load(int4(ClampC(c + int3(0,1,0)), 0)).w - gC4.Load(int4(ClampC(c - int3(0,1,0)), 0)).w,
        gC4.Load(int4(ClampC(c + int3(0,0,1)), 0)).w - gC4.Load(int4(ClampC(c - int3(0,0,1)), 0)).w);
    float3 N = eta / (length(eta) + 1e-5);
    float3 f = vorticity * cross(N, om.xyz);

    // ink is heavier than water: it accelerates toward +y (down). The density
    // grid is finer; normalized coords bridge the two resolutions.
    f.y += buoyancy * gB1.SampleLevel(gSamp, CellUvw(id), 0);

    if (noiseAmt > 1e-6)
        f += noiseAmt * CurlNoise((float3)id * 0.05, timeSec, seed);

    gOut4[id] = float4(gA4.Load(int4(c, 0)).xyz + f * dt, 0);
}

)hlsl" R"hlsl(
// ---------------------------------------------------------------------------
// advection — MacCormack (Selle 2008) with the min/max clamp that stops the
// ringing. Never skip the clamp.  (MSVC caps one string literal at 16380
// bytes, so kSimSrc is split into adjacent raw literals here and below.)
// ---------------------------------------------------------------------------
[numthreads(8,8,4)]
void CSAdvectVelFwd(uint3 id : SV_DispatchThreadID) {          // phi_hat = SL(-dt)
    if (OutOfRange(id)) return;
    float3 pos = (float3)id + 0.5;
    float3 v   = gA4.Load(int4((int3)id, 0)).xyz;
    gOut4[id] = float4(gA4.SampleLevel(gSamp, (pos - v * dt) * invDims, 0).xyz, 0);
}

// t0 = phi_hat (tmp), t2 = src (original velocity)
[numthreads(8,8,4)]
void CSAdvectVelBack(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 pos  = (float3)id + 0.5;
    float3 src  = gC4.Load(int4((int3)id, 0)).xyz;
    float3 v    = src;
    float3 back = pos - v * dt;

    float3 hatHere = gA4.Load(int4((int3)id, 0)).xyz;
    float3 bar     = gA4.SampleLevel(gSamp, (pos + v * dt) * invDims, 0).xyz;
    float3 res     = hatHere + 0.5 * (src - bar);

    float3 lo = float3( 1e20,  1e20,  1e20);
    float3 hi = float3(-1e20, -1e20, -1e20);
    int3 b0 = (int3)floor(back - 0.5);
    [unroll] for (int k = 0; k < 8; k++) {
        float3 s = gC4.Load(int4(ClampC(b0 + int3(k & 1, (k >> 1) & 1, (k >> 2) & 1)), 0)).xyz;
        lo = min(lo, s); hi = max(hi, s);
    }
    gOut4[id] = float4(clamp(res, lo, hi) * velDissipation, 0);
}

[numthreads(8,8,4)]
void CSAdvectVelSL(uint3 id : SV_DispatchThreadID) {           // advect=sl
    if (OutOfRange(id)) return;
    float3 pos = (float3)id + 0.5;
    float3 v   = gA4.Load(int4((int3)id, 0)).xyz;
    float3 r   = gA4.SampleLevel(gSamp, (pos - v * dt) * invDims, 0).xyz;
    gOut4[id] = float4(r * velDissipation, 0);
}

// --- density ---------------------------------------------------------------
float3 SampleVel(float3 densPos) {
    return gA4.SampleLevel(gSamp, densPos * invDims, 0).xyz * velScale;
}

[numthreads(8,8,4)]
void CSAdvectDenFwd(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 pos = (float3)id + 0.5;
    gOut1[id] = gD1.SampleLevel(gSamp, (pos - SampleVel(pos) * dt) * invDims, 0);
}

// t0 = velocity, t1 = phi_hat (tmp), t3 = src density
[numthreads(8,8,4)]
void CSAdvectDenBack(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 pos  = (float3)id + 0.5;
    float3 v    = SampleVel(pos);
    float3 back = pos - v * dt;

    float hatHere = gB1.Load(int4((int3)id, 0));
    float bar     = gB1.SampleLevel(gSamp, (pos + v * dt) * invDims, 0);
    float src     = gD1.Load(int4((int3)id, 0));
    float res     = hatHere + 0.5 * (src - bar);

    float lo = 1e20, hi = -1e20;
    int3 b0 = (int3)floor(back - 0.5);
    [unroll] for (int k = 0; k < 8; k++) {
        float s = gD1.Load(int4(ClampC(b0 + int3(k & 1, (k >> 1) & 1, (k >> 2) & 1)), 0));
        lo = min(lo, s); hi = max(hi, s);
    }
    res = clamp(res, lo, hi);

    // dual-rate decay: faint ink dies faster, so the water clears between
    // drops instead of filling with grey (CSAdvectDye, src/shaders.h:166-200)
    res *= (res < decayThreshold) ? dissipationFast : dissipation;
    // bottom sink so ink reaching the floor drains instead of pooling
    if (((float)id.y + 0.5) * invDims.y > 1.0 - sinkBottom) res *= sinkRate;
    gOut1[id] = clamp(res, 0.0, densityCap);
}

[numthreads(8,8,4)]
void CSAdvectDenSL(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    float3 pos = (float3)id + 0.5;
    float  res = gD1.SampleLevel(gSamp, (pos - SampleVel(pos) * dt) * invDims, 0);
    res *= (res < decayThreshold) ? dissipationFast : dissipation;
    if (((float)id.y + 0.5) * invDims.y > 1.0 - sinkBottom) res *= sinkRate;
    gOut1[id] = clamp(res, 0.0, densityCap);
}

// rho += k*(rho - box3x3x3(rho)) — counteracts trilinear smearing so veils
// stay sheets instead of fog.
[numthreads(8,8,4)]
void CSSharpen(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float s = 0.0;
    [unroll] for (int z = -1; z <= 1; z++)
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++)
        s += gB1.Load(int4(ClampC(c + int3(x,y,z)), 0));
    float v = gB1.Load(int4(c, 0));
    gOut1[id] = clamp(v + sharpen * (v - s / 27.0), 0.0, densityCap);
}

)hlsl" R"hlsl(
// ---------------------------------------------------------------------------
// projection
// ---------------------------------------------------------------------------
// DIVERGENCE — forward difference, i.e. a MAC/staggered reading of the
// collocated velocity texture: v[i].x is the flux through the face between
// cell i-1 and cell i, and the divergence of cell i is (face i+1) - (face i).
//
// This is NOT cosmetic. With the plan's central differences on both sides,
// div(grad p) is the WIDE Laplacian (p[i+2] - 2p[i] + p[i-2])/4 while the
// solver inverts the COMPACT 7-point one, so the odd/even checkerboard modes
// are never projected out: measured, Jacobi-24 left 88% of the divergence
// standing even though its own residual was already down to 0.4%. Forward
// divergence paired with the backward gradient below makes div(grad p)
// exactly the compact stencil the solver inverts, so the projection is exact
// to solver convergence.
//
// Closed box, free slip: the outermost faces (index 0 on each axis, and the
// far face which is not stored at all) carry zero flux.
[numthreads(8,8,4)]
void CSDivergence(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float3 vC = gA4.Load(int4(c, 0)).xyz;
    float fxR = (c.x == dims.x - 1) ? 0.0 : gA4.Load(int4(c + int3(1,0,0), 0)).x;
    float fyR = (c.y == dims.y - 1) ? 0.0 : gA4.Load(int4(c + int3(0,1,0), 0)).y;
    float fzR = (c.z == dims.z - 1) ? 0.0 : gA4.Load(int4(c + int3(0,0,1), 0)).z;
    float fxL = (c.x == 0) ? 0.0 : vC.x;
    float fyL = (c.y == 0) ? 0.0 : vC.y;
    float fzL = (c.z == 0) ? 0.0 : vC.z;
    gOut1[id] = (fxR - fxL) + (fyR - fyL) + (fzR - fzL);
}

// Neumann (dp/dn = 0): the out-of-box neighbour is the cell itself.
float PN(int3 c, int3 o) {
    int3 n = c + o;
    if (any(n < 0) || any(n >= dims)) n = c;
    return gB1.Load(int4(n, 0));
}

[numthreads(8,8,4)]
void CSPressureJacobi(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float sum = PN(c, int3( 1,0,0)) + PN(c, int3(-1,0,0))
              + PN(c, int3(0, 1,0)) + PN(c, int3(0,-1,0))
              + PN(c, int3(0,0, 1)) + PN(c, int3(0,0,-1));
    float pNew = (sum - h2 * gD1.Load(int4(c, 0))) / 6.0;
    gOut1[id] = lerp(gB1.Load(int4(c, 0)), pNew, omega);
}

// red-black Gauss-Seidel, IN PLACE on u1 (typed UAV load of R16_FLOAT; the
// cap is checked at init and the Jacobi kernel above is the fallback).
float PNU(int3 c, int3 o) {
    int3 n = c + o;
    if (any(n < 0) || any(n >= dims)) n = c;
    return gOut1[(uint3)n];
}
[numthreads(8,8,4)]
void CSPressureRB(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    if ((uint)((c.x + c.y + c.z) & 1) != (uint)passIdx) return;
    float sum = PNU(c, int3( 1,0,0)) + PNU(c, int3(-1,0,0))
              + PNU(c, int3(0, 1,0)) + PNU(c, int3(0,-1,0))
              + PNU(c, int3(0,0, 1)) + PNU(c, int3(0,0,-1));
    gOut1[id] = (sum - h2 * gD1.Load(int4(c, 0))) / 6.0;
}

// GRADIENT SUBTRACT — backward difference, the exact partner of the forward
// divergence above: together they give div(grad p) = p[i+1] - 2p[i] + p[i-1],
// the same compact stencil the smoothers invert, so the projection removes
// the divergence exactly instead of leaving the checkerboard behind.
// Face 0 on each axis is the box wall: pinned to zero flux.
[numthreads(8,8,4)]
void CSGradSub(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float pC = gB1.Load(int4(c, 0));
    float3 v = gA4.Load(int4(c, 0)).xyz;
    v.x = (c.x == 0) ? 0.0 : v.x - (pC - PN(c, int3(-1,0,0)));
    v.y = (c.y == 0) ? 0.0 : v.y - (pC - PN(c, int3(0,-1,0)));
    v.z = (c.z == 0) ? 0.0 : v.z - (pC - PN(c, int3(0,0,-1)));
    gOut4[id] = float4(v, 0);
}

// --- multigrid -------------------------------------------------------------
[numthreads(8,8,4)]
void CSResidual(uint3 id : SV_DispatchThreadID) {           // r = b - A p
    if (OutOfRange(id)) return;
    int3 c = (int3)id;
    float p = gB1.Load(int4(c, 0));
    float sum = PN(c, int3( 1,0,0)) + PN(c, int3(-1,0,0))
              + PN(c, int3(0, 1,0)) + PN(c, int3(0,-1,0))
              + PN(c, int3(0,0, 1)) + PN(c, int3(0,0,-1));
    gOut1[id] = gD1.Load(int4(c, 0)) - (sum - 6.0 * p) / h2;
}

// restriction onto the 2x-coarse grid (dims = COARSE dims, t1 = fine residual)
[numthreads(8,8,4)]
void CSRestrict(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 f  = (int3)id * 2;
    int3 hi = dims * 2 - 1;
    float s = 0.0;
    [unroll] for (int k = 0; k < 8; k++)
        s += gB1.Load(int4(min(f + int3(k & 1, (k >> 1) & 1, (k >> 2) & 1), hi), 0));
    gOut1[id] = s * 0.125;
}

// trilinear prolongation added to the fine solution (dims = FINE dims,
// t1 = coarse correction, t3 = fine p)
[numthreads(8,8,4)]
void CSProlong(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    gOut1[id] = gD1.Load(int4((int3)id, 0)) + gB1.SampleLevel(gSamp, CellUvw(id), 0);
}

// ---------------------------------------------------------------------------
// occupancy: max of an 8^3 density block, for empty-space skipping
// ---------------------------------------------------------------------------
[numthreads(4,4,4)]
void CSOccupancy(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    int3 base = (int3)id * 8;
    float m = 0.0;
    [loop] for (int z = 0; z < 8; z++)
    [loop] for (int y = 0; y < 8; y++)
    [loop] for (int x = 0; x < 8; x++)
        m = max(m, gB1.Load(int4(base + int3(x,y,z), 0)));
    gOut1[id] = m;
}

// ---------------------------------------------------------------------------
// light transmittance sweep: one thread per (x,y) column, z back-to-front.
// A different thread-group shape from the rest — that is fine, entry points
// in one source file each carry their own [numthreads].
// ---------------------------------------------------------------------------
[numthreads(8,8,1)]
void CSLight(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)dims.x || id.y >= (uint)dims.y) return;
    float T  = 1.0;
    float ds = 0.5 / (float)dims.z;          // box depth / slices, world units
    for (int z = dims.z - 1; z >= 0; z--) {
        float k = (float)(dims.z - 1 - z);
        float3 uvw = float3(((float)id.x + 0.5) * invDims.x + lightStepX * k,
                            ((float)id.y + 0.5) * invDims.y + lightStepY * k,
                            ((float)z + 0.5) * invDims.z);
        gOut1[uint3(id.xy, (uint)z)] = T;    // transmittance ARRIVING at the slab
        T *= exp(-lightSigma * gB1.SampleLevel(gSamp, saturate(uvw), 0) * ds);
    }
}

// --stats only: |x|, so a plain readback can average it
[numthreads(8,8,4)]
void CSAbs1(uint3 id : SV_DispatchThreadID) {
    if (OutOfRange(id)) return;
    gOut1[id] = abs(gB1.Load(int4((int3)id, 0)));
}
)hlsl";

// ---------------------------------------------------------------------------
// raymarch: fullscreen triangle, front-to-back Beer-Lambert through the
// density volume. The mode composite happens here so the display pass stays
// trivial (and so a half-res march upsamples finished colour).
// ---------------------------------------------------------------------------
static const char* kRaymarchSrc = R"hlsl(
cbuffer RCB : register(b0) {
    float2 invScreen;  float2 boxXY;
    float3 camPos;     float  boxZ;
    float3 sigma;      float  stepsF;
    float3 albedo;     float  scatter;
    float3 paperRgb;   float  paperLevel;
    float3 inkRgb;     float  inkLevel;
    float  mode;       float  scatterMix;  float gradientLight; float jitterPhase;
};

Texture3D<float> gDensity : register(t0);
Texture3D<float> gOcc     : register(t1);
Texture3D<float> gLight   : register(t2);
SamplerState gSamp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// Per-pixel start-offset jitter. Interleaved-gradient noise was the first
// try and it is STRUCTURED by construction — on a soft volume edge its
// diagonal lattice reads as step moire. This is a decorrelated integer hash
// of the pixel coordinate instead: still a pure function of the pixel (so
// --shot stays byte-identical), but white rather than patterned.
// jitterPhase is an integer 0..3, and 0 unless jitter_anim is on.
float PixJitter(float2 p) {
    uint2 q = (uint2)p;
    uint h = q.x * 1973u + q.y * 9277u + (uint)jitterPhase * 26699u + 1u;
    h = (h ^ 61u) ^ (h >> 16);
    h *= 9u;   h ^= h >> 4;
    h *= 0x27d4eb2du;
    h ^= h >> 15;
    return (float)(h & 0x00ffffffu) * (1.0 / 16777216.0);
}

float4 PSMain(float4 svp : SV_Position) : SV_Target {
    float2 ndc = svp.xy * invScreen * 2.0 - 1.0;     // y: -1 top, +1 bottom
    float3 target = float3(boxXY.x * 0.5 * (1.0 + ndc.x),
                           boxXY.y * 0.5 * (1.0 + ndc.y), 0.0);
    float3 ro = camPos;
    float3 rd = normalize(target - ro);

    float3 bmin = float3(0,0,0), bmax = float3(boxXY.x, boxXY.y, boxZ);
    float3 inv = 1.0 / rd;
    float3 ta = (bmin - ro) * inv, tb = (bmax - ro) * inv;
    float3 tsm = min(ta, tb), tbg = max(ta, tb);
    float tn = max(max(max(tsm.x, tsm.y), tsm.z), 0.0);
    float tf = min(min(tbg.x, tbg.y), tbg.z);

    float3 T = float3(1,1,1);
    float3 S = float3(0,0,0);
    if (tf > tn) {
        int   steps = (int)stepsF;
        float ds = (tf - tn) / stepsF;
        float t  = tn + PixJitter(svp.xy) * ds;
        float3 invBox = 1.0 / float3(boxXY.x, boxXY.y, boxZ);
        [loop] for (int i = 0; i < steps; i++) {
            if (t > tf || max(T.r, max(T.g, T.b)) < 0.004) break;
            float3 uvw = saturate((ro + rd * t - bmin) * invBox);
            // empty-space skip: one occupancy texel per 8^3 density voxels,
            // sampled LINEAR so it is effectively dilated by half a block —
            // a 4-step jump (5.3 voxels) can never cross an occupied block.
            if (gOcc.SampleLevel(gSamp, uvw, 0) < 0.002) { t += ds * 4.0; continue; }
            float rho = gDensity.SampleLevel(gSamp, uvw, 0);
            if (rho > 1e-4) {
                float3 dT = exp(-sigma * rho * ds);
                float  L  = gLight.SampleLevel(gSamp, uvw, 0);
                float3 sc = albedo * scatter * L;
                if (gradientLight > 0.5 && rho > 0.05) {
                    float3 e = invBox * 1.5;
                    float3 g = float3(
                        gDensity.SampleLevel(gSamp, uvw + float3(e.x,0,0), 0) -
                        gDensity.SampleLevel(gSamp, uvw - float3(e.x,0,0), 0),
                        gDensity.SampleLevel(gSamp, uvw + float3(0,e.y,0), 0) -
                        gDensity.SampleLevel(gSamp, uvw - float3(0,e.y,0), 0),
                        gDensity.SampleLevel(gSamp, uvw + float3(0,0,e.z), 0) -
                        gDensity.SampleLevel(gSamp, uvw - float3(0,0,e.z), 0));
                    float gl = length(g);
                    if (gl > 1e-5)
                        sc *= 1.0 + 1.6 * pow(saturate(1.0 - abs(g.z / gl)), 3.0);
                }
                S += T * (1.0 - dT) * sc;
                T *= dT;
            }
            t += ds;
        }
    }

    float opacity = saturate(1.0 - dot(T, float3(0.2126, 0.7152, 0.0722)));
    float3 col;
    if (mode < 0.5)                       // negative (default on the OLED)
        col = opacity * inkRgb * inkLevel + S * scatterMix;
    else if (mode < 1.5)                  // paper (the photographs)
        col = T * paperRgb * paperLevel + S;
    else                                  // backlit (light behind the ink)
        col = S;
    return float4(col, opacity);
}
)hlsl";

// ---------------------------------------------------------------------------
// display: hue rotate + HDR knee/peak gain + sdrScale, into the back buffer
// (or the offscreen FP16 target in shot mode).
// ---------------------------------------------------------------------------
static const char* kDisplaySrc = R"hlsl(
cbuffer DCB : register(b0) {
    float2 invScreen; float sdrScale; float peakGain;
    float  knee;      float hueDeg;   float gamut;  float pad;
};
Texture2D<float4> gSrc : register(t0);
SamplerState gSamp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// W3C hue-rotate matrix (the CssHueRotate primitive, src/shaders.h:541)
float3 CssHueRotate(float3 c, float deg) {
    float r = radians(deg), cs = cos(r), sn = sin(r);
    float3x3 m = float3x3(
        0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928,
        0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.140, 0.072 - cs * 0.072 - sn * 0.283,
        0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072);
    return mul(m, c);
}

float4 PSMain(float4 svp : SV_Position) : SV_Target {
    float4 s = gSrc.SampleLevel(gSamp, svp.xy * invScreen, 0);   // = half-res upsample
    float3 c = s.rgb;
    if (abs(hueDeg) > 0.05) c = max(CssHueRotate(c, hueDeg), 0.0);

    // HDR: lift only DENSE cores toward peak_nits (knee 0.70 on the ink
    // opacity), never the veils. Identity when peak_nits <= SDR white.
    float gain = 1.0;
    if (peakGain > 1.001) {
        float t = smoothstep(knee, 1.0, s.a);
        gain = lerp(1.0, peakGain, t * t);
    }
    return float4(c * sdrScale * gain, 1.0);
}
)hlsl";
