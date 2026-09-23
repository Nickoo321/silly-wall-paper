// HLSL sources, runtime-compiled with D3DCompile.
// Compute passes are direct ports of reference/script.js's GLSL fragment
// passes (including the custom dual-rate decay, saturation restore, and the
// proportional brightness cap in the splat).
#pragma once

// Shared by every compute pass via 32-bit root constants (b0).
// Keep in sync with struct SimCB in fluid.cpp.
static const char* kComputeSrc = R"hlsl(
cbuffer CB : register(b0) {
    float2 texelSize;      // 1 / sim resolution
    float  dt;
    float  dissipation;
    float  dissipationFast;
    float  decayThreshold;
    float  satRestore;     // already converted to per-step amount on CPU
    float  curlStrength;
    float  value;          // clear-pressure multiplier
    float  aspect;         // width / height
    float2 point_;         // splat center, uv
    float3 color;          // splat color (or velocity delta)
    float  radius;         // splat radius (uv^2 scale)
    float  cap;            // proportional brightness cap
    int2   dims;           // target texture dimensions
    float  baroclinic;     // dye-front torque strength (0 = off)
    float  gravity;        // dye-weighted gravity, sim texels/s^2 (+y = down, 0 = off)
    float  gravityPow;     // rho^p, so thin veils hang and dense cores fall
    float  gravityBlur;    // sim texels: blur radius of the density gravity reads
    float  pad0_;
};

SamplerState linearClamp : register(s0);

Texture2D<float4> SrcA : register(t0);
Texture2D<float4> SrcB : register(t1);
Texture2D<float4> SrcC : register(t2);   // dye (baroclinic pass only)
RWTexture2D<float2> DstV : register(u0);   // velocity-typed targets
RWTexture2D<float4> Dst4 : register(u1);   // dye / generic float4 targets
RWTexture2D<float>  Dst1 : register(u2);   // single-channel targets

int2 ClampCoord(int2 c) { return clamp(c, int2(0, 0), dims - 1); }

// --- init clears -----------------------------------------------------------
[numthreads(8, 8, 1)]
void CSClearV(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    DstV[id.xy] = float2(0, 0);
}
[numthreads(8, 8, 1)]
void CSClear4(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    Dst4[id.xy] = float4(0, 0, 0, 1);
}
[numthreads(8, 8, 1)]
void CSClear1(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    Dst1[id.xy] = 0;
}

// --- curl ------------------------------------------------------------------
// SrcA = velocity (rg). Dst1 = curl.
[numthreads(8, 8, 1)]
void CSCurl(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 p = int2(id.xy);
    float L = SrcA.Load(int3(ClampCoord(p + int2(-1, 0)), 0)).y;
    float R = SrcA.Load(int3(ClampCoord(p + int2( 1, 0)), 0)).y;
    float T = SrcA.Load(int3(ClampCoord(p + int2( 0,-1)), 0)).x;
    float B = SrcA.Load(int3(ClampCoord(p + int2( 0, 1)), 0)).x;
    // Screen y is down; reference GL had y up. R-L-T+B with T/B swapped keeps
    // the same physical handedness as the reference.
    float vorticity = R - L - (B - T);
    Dst1[id.xy] = 0.5 * vorticity;
}

// --- vorticity confinement ---------------------------------------------------
// SrcA = velocity, SrcB = curl. DstV = new velocity.
[numthreads(8, 8, 1)]
void CSVorticity(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 p = int2(id.xy);
    float L = SrcB.Load(int3(ClampCoord(p + int2(-1, 0)), 0)).x;
    float R = SrcB.Load(int3(ClampCoord(p + int2( 1, 0)), 0)).x;
    float T = SrcB.Load(int3(ClampCoord(p + int2( 0,-1)), 0)).x;
    float B = SrcB.Load(int3(ClampCoord(p + int2( 0, 1)), 0)).x;
    float C = SrcB.Load(int3(p, 0)).x;

    // Verbatim port of the reference (its vT is +v = our B at y+1, and it
    // flips force.y). Getting this sign wrong makes confinement DAMP swirls
    // instead of amplifying them — the sim turns into honey.
    float2 force = 0.5 * float2(abs(B) - abs(T), abs(L) - abs(R));
    force /= length(force) + 0.0001;
    force *= curlStrength * C;

    // Baroclinic torque ("forms resist forms"): bend the flow along dye
    // fronts so wakes curl around existing masses instead of cutting
    // straight through. Perp(density gradient), scaled by local density so
    // empty regions are untouched. Dye is high-res; sample by uv.
    if (baroclinic > 0.0f) {
        float2 uv = (float2(p) + 0.5) * texelSize;
        float2 ex = float2(texelSize.x, 0), ey = float2(0, texelSize.y);
        float3 lw = float3(0.299, 0.587, 0.114);
        float dl = dot(SrcC.SampleLevel(linearClamp, uv - ex, 0).rgb, lw);
        float dr = dot(SrcC.SampleLevel(linearClamp, uv + ex, 0).rgb, lw);
        float dt_ = dot(SrcC.SampleLevel(linearClamp, uv - ey, 0).rgb, lw);
        float db = dot(SrcC.SampleLevel(linearClamp, uv + ey, 0).rgb, lw);
        float rho = dot(SrcC.SampleLevel(linearClamp, uv, 0).rgb, lw);
        float2 g = 0.5 * float2(dr - dl, db - dt_);
        force += baroclinic * rho * float2(g.y, -g.x);
    }

    // Dye-weighted gravity: ink is denser than water, so a dye-laden parcel
    // sinks (positive = down, negative = a buoyant/smoke rise). rho^p with
    // p > 1 means the dense head keeps falling while the thin veils, whose
    // density is a fraction of it, barely move — which is the shape of the
    // ref-1 plume. Style-agnostic; exactly zero work when gravity == 0.
    if (gravity != 0.0) {
        float2 guv = (float2(p) + 0.5) * texelSize;
        float3 glw = float3(0.299, 0.587, 0.114);
        // BLURRED density, not the per-texel one. A buoyancy force driven by a
        // noisy density field seeds Rayleigh-Taylor fingers at the GRID scale,
        // and vorticity confinement then amplifies them: the plume came out as
        // a fuzzy cauliflower instead of the references' smooth sheets. A box
        // blur over +-gravityBlur sim texels pushes the fastest-growing
        // wavelength up to that scale, so gravity sets the sink rate of whole
        // lobes and stops carving fringe.
        float2 gb = texelSize * max(gravityBlur, 0.0);
        float  grho = dot(SrcC.SampleLevel(linearClamp, guv, 0).rgb, glw) * 2.0;
        grho += dot(SrcC.SampleLevel(linearClamp, guv + float2( gb.x,  gb.y), 0).rgb, glw);
        grho += dot(SrcC.SampleLevel(linearClamp, guv + float2(-gb.x,  gb.y), 0).rgb, glw);
        grho += dot(SrcC.SampleLevel(linearClamp, guv + float2( gb.x, -gb.y), 0).rgb, glw);
        grho += dot(SrcC.SampleLevel(linearClamp, guv + float2(-gb.x, -gb.y), 0).rgb, glw);
        grho /= 6.0;
        force.y += gravity * pow(max(grho, 0.0), gravityPow);
    }

    float2 vel = SrcA.Load(int3(p, 0)).xy;
    DstV[id.xy] = vel + force * dt;
}

// --- divergence --------------------------------------------------------------
// SrcA = velocity. Dst1 = divergence.
[numthreads(8, 8, 1)]
void CSDivergence(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 p = int2(id.xy);
    float2 C = SrcA.Load(int3(p, 0)).xy;
    float L = (p.x == 0)          ? -C.x : SrcA.Load(int3(p + int2(-1, 0), 0)).x;
    float R = (p.x == dims.x - 1) ? -C.x : SrcA.Load(int3(p + int2( 1, 0), 0)).x;
    float T = (p.y == 0)          ? -C.y : SrcA.Load(int3(p + int2( 0,-1), 0)).y;
    float B = (p.y == dims.y - 1) ? -C.y : SrcA.Load(int3(p + int2( 0, 1), 0)).y;
    // y-down: T is the -y neighbor. div = d(vx)/dx + d(vy)/dy.
    float div = 0.5 * ((R - L) + (B - T));
    Dst1[id.xy] = div;
}

// --- pressure ----------------------------------------------------------------
// Clear: SrcA = pressure. Dst1 = pressure * value.
[numthreads(8, 8, 1)]
void CSClearPressure(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    Dst1[id.xy] = SrcA.Load(int3(int2(id.xy), 0)).x * value;
}

// Jacobi iteration: SrcA = pressure, SrcB = divergence. Dst1 = new pressure.
[numthreads(8, 8, 1)]
void CSPressure(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 p = int2(id.xy);
    float L = SrcA.Load(int3(ClampCoord(p + int2(-1, 0)), 0)).x;
    float R = SrcA.Load(int3(ClampCoord(p + int2( 1, 0)), 0)).x;
    float T = SrcA.Load(int3(ClampCoord(p + int2( 0,-1)), 0)).x;
    float B = SrcA.Load(int3(ClampCoord(p + int2( 0, 1)), 0)).x;
    float divergence = SrcB.Load(int3(p, 0)).x;
    Dst1[id.xy] = (L + R + T + B - divergence) * 0.25;
}

// --- gradient subtract ---------------------------------------------------------
// SrcA = pressure, SrcB = velocity. DstV = new velocity.
[numthreads(8, 8, 1)]
void CSGradientSubtract(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 p = int2(id.xy);
    float L = SrcA.Load(int3(ClampCoord(p + int2(-1, 0)), 0)).x;
    float R = SrcA.Load(int3(ClampCoord(p + int2( 1, 0)), 0)).x;
    float T = SrcA.Load(int3(ClampCoord(p + int2( 0,-1)), 0)).x;
    float B = SrcA.Load(int3(ClampCoord(p + int2( 0, 1)), 0)).x;
    float2 velocity = SrcB.Load(int3(p, 0)).xy;
    velocity -= float2(R - L, B - T);   // y-down
    DstV[id.xy] = velocity;
}

// --- advection ------------------------------------------------------------------
// Velocity: SrcA = velocity (also the advected source). DstV = new velocity.
[numthreads(8, 8, 1)]
void CSAdvectVelocity(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float2 vel = SrcA.SampleLevel(linearClamp, uv, 0).xy;
    float2 coord = uv - dt * vel * texelSize;
    float2 v = SrcA.SampleLevel(linearClamp, coord, 0).xy;
    DstV[id.xy] = v * dissipation;
}

// Dye: SrcA = velocity (sim res), SrcB = dye. Dst4 = new dye.
// Custom features from the reference: dual-rate decay (faint dye decays at
// dissipationFast) and per-step saturation restore.
[numthreads(8, 8, 1)]
void CSAdvectDye(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float2 vel = SrcA.SampleLevel(linearClamp, uv, 0).xy;
    float2 coord = uv - dt * vel * texelSize;   // texelSize = sim texel, like reference
    float4 c = SrcB.SampleLevel(linearClamp, coord, 0);

    float lum = max(c.r, max(c.g, c.b));
    float k = lerp(dissipationFast, dissipation, smoothstep(0.0, max(decayThreshold, 0.0001), lum));
    c *= k;

    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    if (satRestore > 0.0 && mx > 0.001) {
        float3 satC = (c.rgb - mn) * (mx / max(mx - mn, 0.0001));
        c.rgb = lerp(c.rgb, satC, satRestore * smoothstep(0.0, 0.05, mx));
    }
    c.a = 1.0;
    Dst4[id.xy] = c;
}

// --- splat ------------------------------------------------------------------------
// Velocity splat: SrcA = velocity. DstV = velocity + gaussian * color.xy (uncapped).
[numthreads(8, 8, 1)]
void CSSplatVelocity(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float2 p = uv - point_;
    p.x *= aspect;
    float2 splat = exp(-dot(p, p) / radius) * color.xy;
    float2 base = SrcA.Load(int3(int2(id.xy), 0)).xy;
    DstV[id.xy] = base + splat;
}

// Dye diffusion: 3x3 tent blur blended by 'value' — the D∇²c term real
// dye/smoke has. Softens splat stamps and lets fronts fade into clear fluid
// instead of stopping at a hard edge. SrcA = dye, Dst4 = new dye.
[numthreads(8, 8, 1)]
void CSDiffuseDye(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 p = int2(id.xy);
    float4 C = SrcA.Load(int3(p, 0));
    float4 sum = C * 4.0;
    sum += SrcA.Load(int3(ClampCoord(p + int2(-1, 0)), 0)) * 2.0;
    sum += SrcA.Load(int3(ClampCoord(p + int2( 1, 0)), 0)) * 2.0;
    sum += SrcA.Load(int3(ClampCoord(p + int2( 0,-1)), 0)) * 2.0;
    sum += SrcA.Load(int3(ClampCoord(p + int2( 0, 1)), 0)) * 2.0;
    sum += SrcA.Load(int3(ClampCoord(p + int2(-1,-1)), 0));
    sum += SrcA.Load(int3(ClampCoord(p + int2( 1,-1)), 0));
    sum += SrcA.Load(int3(ClampCoord(p + int2(-1, 1)), 0));
    sum += SrcA.Load(int3(ClampCoord(p + int2( 1, 1)), 0));
    sum /= 16.0;
    Dst4[id.xy] = lerp(C, sum, value);
}

// Coverage governor: bilinear-downsample the dye field into a 48x27 grid the
// CPU reads back once per second (SrcB = dye at t1 not used; SrcA = dye).
[numthreads(8, 8, 1)]
void CSDownsample(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    Dst4[id.xy] = SrcA.SampleLevel(linearClamp, uv, 0);
}

// Dye splat with the reference's proportional cap: brightness limited, hue kept.
// DROP_COMPACT: the same splat with FINITE SUPPORT. A Gaussian has infinite
// tails, and for an ink drop that is a real artefact, not a nicety: the outer
// halo of the dye stamp lies outside the velocity impulse that drives the
// drop, never acquires momentum, and is left parked at the injection point for
// the rest of the drop's life — soft mist on paper, an opaque white orb in
// inverted + HDR. Multiplying by a smooth cutoff at ~1.5-1.8 sigma removes
// exactly that halo and nothing that is visibly part of the cap.
// The cutoff is in units of dot(p,p)/radius, i.e. (d/sigma)^2 / 2: 1.8 is
// 1.90 sigma (weight 0.165) and 3.2 is 2.53 sigma (weight 0.041).
// Compiled as a SECOND PSO from this same source; the PSO the fluid look uses
// is compiled without the macro and is byte-identical to before.
[numthreads(8, 8, 1)]
void CSSplatDye(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float2 p = uv - point_;
    p.x *= aspect;
    float3 splat = exp(-dot(p, p) / radius) * color;
#ifdef DROP_COMPACT
    splat *= 1.0 - smoothstep(1.8, 3.2, dot(p, p) / max(radius, 1e-9));
#endif
    float3 base = SrcA.Load(int3(int2(id.xy), 0)).rgb;
    float3 c = base + splat;
    float m = max(c.r, max(c.g, c.b));
    c *= cap / max(m, cap);
    Dst4[id.xy] = float4(c, 1.0);
}

)hlsl"
// (MSVC caps one string literal at 16380 bytes, so the compute source is
// split here and concatenated by the preprocessor. Nothing above changes.)
R"hlsl(
// ===========================================================================
// OIL DRAG ([liquid_acid] oil_drag / oil_dye_block). Three passes, dispatched
// only while one of those keys is non-zero -- the fluid look and every acid
// ini that leaves them at 0 never runs a line of this.
//
// The user: "make it impossible for the fluid sim underneath, the mono ink, to
// get under the oil, or rather an intense friction that makes it hard for it
// to get under."
//
// CSOilMask evaluates the SAME Wyvill metaball field the display shader
// thresholds, at sim resolution (~256x144, so ~37k texels x <=128 blobs = a
// rounding error next to the Jacobi solve), and soft-thresholds it. Because it
// is the same field, a negative "hole" blob is negative here too: a hole is
// NOT oil, so ink inside it is neither dragged nor faded, which is exactly the
// behaviour the holes need (the ink you see through one is the ink layer).
//
// Packing (the pass builds its own SimCB, so these are free):
//   value  = threshold        radius = support scale
//   point_ = (blob count, soft-edge half width in field units)
//   color  = (drag k, edge push, viscous diffusion k)   [drag pass]
//   cap    = dye block k                                [dye pass]
// ===========================================================================
struct OilBlobGPU { float4 a; float4 b; float4 c; };
StructuredBuffer<OilBlobGPU> OilBlobs : register(t3);

[numthreads(8, 8, 1)]
void CSOilMask(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float2 pp = float2(uv.x * aspect, uv.y);
    float  field = 0.0;
    int nb = (int)point_.x;
    [loop]
    for (int bi = 0; bi < nb; bi++) {
        OilBlobGPU B = OilBlobs[bi];
        float2 q = pp - float2(B.a.x * aspect, B.a.y);
        float  sup = B.a.z * radius;
        float  s2 = sup * sup;
        float  d2 = dot(q, q);
        if (d2 >= s2) continue;
        float u = 1.0 - d2 / s2;
        field += u * u * u * B.a.w;
    }
    // Soft edge a few sim texels wide, expressed in FIELD units: the Wyvill
    // field crosses the threshold over roughly its own gradient, and point_.y
    // is tuned on the CPU from the mean blob size so the band is the same
    // handful of texels whatever the resolution.
    float m = smoothstep(value - point_.y, value + point_.y, field);
    Dst1[id.xy] = m;
}

// Velocity under the mask. Two terms:
//   1. friction -- an exponential decay toward rest, fps-normalised on the
//      CPU, so ink already under the oil comes to a stop and nothing new can
//      stream in and keep its momentum;
//   2. the mask's own GRADIENT as a gentle outward push at the rim. grad(m)
//      points INTO the oil, so -grad deflects the flow AROUND an island and
//      the ink piles up along its edge instead of leaking underneath.
// Plus, when oil_viscosity is on, a Laplacian blend toward the neighbourhood
// mean under the mask: a thick fluid's own velocity diffusion.
[numthreads(8, 8, 1)]
void CSOilDrag(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    int2 c = int2(id.xy);
    float2 v = SrcA.Load(int3(c, 0)).xy;
    float  m = SrcB.Load(int3(c, 0)).x;
    float  mL = SrcB.Load(int3(ClampCoord(c - int2(1, 0)), 0)).x;
    float  mR = SrcB.Load(int3(ClampCoord(c + int2(1, 0)), 0)).x;
    float  mD = SrcB.Load(int3(ClampCoord(c - int2(0, 1)), 0)).x;
    float  mU = SrcB.Load(int3(ClampCoord(c + int2(0, 1)), 0)).x;
    if (color.z > 0.0) {
        float2 vL = SrcA.Load(int3(ClampCoord(c - int2(1, 0)), 0)).xy;
        float2 vR = SrcA.Load(int3(ClampCoord(c + int2(1, 0)), 0)).xy;
        float2 vD = SrcA.Load(int3(ClampCoord(c - int2(0, 1)), 0)).xy;
        float2 vU = SrcA.Load(int3(ClampCoord(c + int2(0, 1)), 0)).xy;
        v = lerp(v, 0.25 * (vL + vR + vD + vU), saturate(color.z * m));
    }
    v *= 1.0 - saturate(color.x * m);
    // Sim texels per unit mask: the gradient is in texel space already, which
    // is the same space the velocity lives in.
    float2 g = float2(mR - mL, mU - mD) * 0.5;
    v -= g * color.y;
    DstV[id.xy] = v;
}

// Dye that lands under the oil fades out, so the oil reads as sitting ON the
// water instead of as a colour filter over trapped ink. Ink outside the mask
// is multiplied by exactly 1.0.
[numthreads(8, 8, 1)]
void CSOilDyeBlock(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float  m = SrcB.SampleLevel(linearClamp, uv, 0).x;
    float4 d = SrcA.Load(int3(int2(id.xy), 0));
    d.rgb *= 1.0 - saturate(cap * m);
    Dst4[id.xy] = d;
}
)hlsl";

// Display: fullscreen triangle sampling the dye texture, with the reference's
// pseudo-normal shading. Output is linear scRGB (1.0 = 80 nits).
static const char* kDisplaySrc = R"hlsl(
cbuffer CB : register(b0) {
    float2 texelSize;   // 1 / screen resolution (reference uses screen texels)
    float  shading;
    float  sdrScale;    // scRGB multiplier for SDR-reference white (1.0 when HDR off)
    float  gamut;       // 0 = sRGB, 1 = Display-P3 (WE parity), 2 = BT.2020 (QD-OLED)
    float  peakGain;    // HDR highlight expansion: peakNits / sdrWhiteNits (1 = off)
    float  knee;        // dye brightness where highlight expansion starts
    float  capBright;   // dye brightness cap (expansion reaches peakGain here)
    // CSS-filter chain parameters, evaluated primitive by primitive in gamma
    // space with a clamp to [0,1] after each one — exactly what Chromium/Skia
    // does with the reference's canvas filter (each filter function is its
    // own colour-matrix stage, and Skia clamps after every stage).
    //   fm0 = (hdrSaturate, hdrBrightness, hdrContrast, hdrEnabled)
    //   fm1 = (hueBurstDeg, postSaturate, postBrightness, postContrast)
    //   fm2 = (postHueDeg, 0, 0, 0)
    float4 fm0;
    float4 fm1;
    float4 fm2;
    float4 fmOff;
    // Response curve (Lightroom-style hump): x=enabled, y=center, z=width,
    // w=height. Remaps brightness through a Gaussian bump so mids near
    // 'center' glow to 'height' while brighter cores drop back down —
    // producing bright outlines of each splat.
    float4 curve;
    // Shadow floor ("bottom knee"): x = gray floor level, y = knee width.
    // Near-black output lifts smoothly toward the floor so dark regions keep
    // visible marbling instead of crushing to pure black. x = 0 disables.
    float4 shadow;
};

SamplerState linearClamp : register(s0);
Texture2D<float4> Dye : register(t0);

// ---------------------------------------------------------------------------
// Screen mirroring / kaleidoscope ([mirror]). A uv transform applied FIRST in
// PSMain, so EVERY look inherits it: the dye sample, the acid metaball field
// (pp derives from uv) and InkWater all see the folded coordinate, and blobs,
// rims, swarms and speckle mirror with the dye instead of floating over it.
//
// This lives in the SHARED part of the source — no macro, no extra PSO — so
// mode 0 must be an exact no-op. It is: MirrorFold() returns uv untouched
// before doing any arithmetic, which is why style=fluid stays bit-identical.
//
// Structured for the SWEEP transition sketched in PROGRESS.md: (1) MirrorFold
// returns BOTH the source uv and the distance to the nearest fold line, and
// (2) the line definition (centre, angle, mode) is this one cbuffer block. A
// later "side weight" can call it for the distance alone.
// ---------------------------------------------------------------------------
cbuffer MirrorCB : register(b3) {
    float4 mrP0;   // x mode     y segments  z aspect         w time
    float4 mrP1;   // xy centre              z rotate_period  w drift
    float4 mrP2;   // x soft     y source    z -              w -
    // ---- [post]: the final composite trim, shared by every look ----------
    // Same root-constants slot because it is bound on exactly the same draws
    // and needs exactly the same two numbers (time, aspect) that mrP0 already
    // carries. Both amounts are 0 by default and the code is branched out.
    float4 poP0;   // x film_grain y grain_size z grain_speed w grain_color
    float4 poP1;   // x vignCx(uv) y vignCy(uv) z grainFps    w vignette
};

// |d| with the corner rounded off over a band of half-width s: equals abs(d)
// for |d| >> s, and has zero derivative at 0. That is the whole of `soft` —
// a mirror is already continuous in VALUE at the seam, what gives it away is
// the reversed gradient, the hard V. Rounding the fold itself removes it for
// one sqrt, where "sample both sides and lerp" would cost a second run of the
// entire look pipeline. The band is very slightly compressed in exchange.
float MirSoftAbs(float d, float s) {
    return (s > 1e-5) ? (sqrt(d * d + s * s) - s) : abs(d);
}
// Quasi-random wander in ~[-1,1]: three incommensurate sines, i.e. an fbm of
// time with no texture and no hash. The slowest term sets the pace (~2 min).
float MirWander(float t) {
    return 0.55 * sin(t) + 0.30 * sin(t * 1.913 + 1.7) + 0.15 * sin(t * 3.271 + 4.1);
}
// One folded axis. The fold line maps to the MIDDLE of the sim (0.5) and the
// screen edge to the outer edge of the chosen half, so the shown half is
// stretched to fill its mirrored tile: the full sim resolution ends up on
// screen and nothing is wasted. sgn picks which half of the sim is shown.
// sd = distance from the pixel to the fold line, uv units.
float MirFoldAxis(float u, float c, float sgn, float soft, out float sd) {
    float d = u - c;
    sd = abs(d);
    float M = max(max(c, 1.0 - c), 1e-4);
    return 0.5 + sgn * saturate(MirSoftAbs(d, soft) / M) * 0.5;
}

float2 MirrorFold(float2 uv, out float seam) {
    seam = 1e9;
    int mode = (int)(mrP0.x + 0.5);
    if (mode <= 0) return uv;          // the no-op path: nothing below runs

    float2 c = mrP1.xy;
    if (mrP1.w > 0.0005) {
        // drift: unglue the seam from the screen centre. Clamped well inside
        // the frame so one tile never collapses to nothing.
        float t = mrP0.w * 0.05;
        c += 0.18 * mrP1.w * float2(MirWander(t), MirWander(t * 0.83 + 11.0));
        c = clamp(c, 0.2, 0.8);
    }
    const float soft = max(mrP2.x, 0.0);

    if (mode >= 4) {
        // Kaleidoscope: N wedges about c, every other one REFLECTED, so each
        // wedge boundary is a mirror seam and never a jump. Angles are taken
        // in aspect-corrected space or the wedges come out sheared.
        const float aspect = max(mrP0.z, 1e-3);
        const float rot = (mrP1.z > 0.01) ? (6.2831853 * mrP0.w / mrP1.z) : 0.0;
        float2 p = (uv - c) * float2(aspect, 1.0);
        // Scale the radius so the wedge reads the sim's central DISC rather
        // than running off the texture: the wedge points in one direction for
        // the whole screen, so without this the far corners all land outside
        // [0,1]^2 and the clamp sampler smears one row of texels into radial
        // streaks. 0.5 is the largest disc that fits the frame in this space.
        float  r = length(p) * (0.5 / max(length(float2(0.5 * aspect, 0.5)), 1e-3));
        float  seg = 6.2831853 / max(mrP0.y, 2.0);
        float  a = atan2(p.y, p.x) - rot;
        float  m2 = a - seg * 2.0 * floor(a / (seg * 2.0));   // wrapped to [0, 2seg)
        float  as = soft / max(r, 1e-3);                      // uv band -> radians
        float  af = MirSoftAbs(m2 - seg, as);                 // soft at the wedge axis
        af = seg - MirSoftAbs(seg - af, as);                  // and at the wedge edge
        seam = r * min(af, seg - af);
        float  ang = af + rot;
        return c + float2(cos(ang), sin(ang)) * r * float2(1.0 / aspect, 1.0);
    }

    // Axis-aligned folds. Rotation is deliberately NOT applied here: a rotated
    // quad fold no longer tiles the rectangle exactly, and the reference look
    // is the fixed cross. The kaleidoscope is the mode that turns.
    int   src  = (int)(mrP2.y + 0.5);
    float sgnX = (src & 1) ? 1.0 : -1.0;
    float sgnY = (src & 2) ? 1.0 : -1.0;
    float2 s = uv;
    float sdx = 1e9, sdy = 1e9;
    if (mode == 1 || mode == 3) s.x = MirFoldAxis(uv.x, c.x, sgnX, soft, sdx);
    if (mode == 2 || mode == 3) s.y = MirFoldAxis(uv.y, c.y, sgnY, soft, sdy);
    seam = min(sdx, sdy);
    return s;
}

// Cheap hash for the [post] film grain (no texture, no table). Same shape as
// the acid look's own AcidHash21, duplicated here because that one lives
// inside the LIQUID_ACID block and the grain belongs to every look.
float PostHash21(float2 p) {
    p = frac(p * float2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return frac(p.x * p.y);
}

#ifdef LIQUID_ACID
// --------------------------------------------------------------------------
// "Liquid Acid" look. Compiled as a SECOND PSO from this same source with
// LIQUID_ACID defined; the fluid PSO is compiled without it and therefore
// contains none of this code (bit-identical to the pre-feature shader).
// Everything here is parameterised from LiquidAcidConfig (see fluid.h).
// --------------------------------------------------------------------------
cbuffer AcidCB : register(b1) {
    float4 laOil[4];     // oil palette, rgb
    float4 laInk[4];     // ink ramp stops (dark -> bright), rgb
    float4 laP0;         // x blobCount  y cutoff      z clampMax   w aaScale
    float4 laP1;         // x rimWidth   y rimInset    z rimDark    w refraction
    float4 laP2;         // x meniscus   y meniscusW   z translucency w oilTexture
    float4 laP3;         // x inkLevels  y inkSoft     z inkMix     w inkHueVary
    float4 laP4;         // x inkGain    y inkBias     z seamStr    w seamScale
    float4 laP5;         // x seamLo     y seamHi      z grainAmt   w grainScale
    float4 laP6;         // x speckle    y speckScale  z time       w aspect
    float4 laP7;         // x oilHdr     y rimHdr      z meniscusOff w inkShading
    float4 laP8;         // x swarmHoles y swarmDrops  z density    w swarmRimDark
    float4 laP9;         // x scaleA     y scaleB      z rMin       w rMax (cell units)
    float4 laP10;        // x swarmClump y swarmDark   z inkMode (1=water) w toeTint
    float4 laP11;        // x lockOn     y lockSpan    z targetHue  w sweepDeg
    float4 laP12;        // x rimVary    y rimInkFollow z rimOrder  w grainShadowW
    float4 laP13;        // x oilThinEdge y oilEdgeFrac z oilSpecular w oilIrid
    float4 laP14;        // x swarmLens  y menFromInk  z oilGlow    w refrWidth
    float4 laP15;        // x oilTransp  y oilAbsorb   z filmBump   w refrBody
    float4 laP16;        // x oilInkBlur y dyeDepthW z - w -
    float4 laP17;        // x riseBottomLight y postChroma z postLift w -
    float4 laP18;        // x dropsOn    y gridW      z gridH      w edgeMode
    float4 laP19;        // x dropSupport y dropPunch z dropOilW   w -
    float4 laP20;        // x ringWidth  y ringLift   z edgeCurve  w diffScale(uv)
    float4 laP21;        // x halo       y haloW(uv)  z softness(uv) w bandMin
    float4 laP22;        // x penumbra   y penW(uv)   z penHueDeg  w penDark
    float4 laP23;        // x cellInk    y cellOil    z cellScale(uv) w cellDrift(uv/s)
    float4 laMen;        // meniscus halo colour, rgb
    // --- perspective camera + depth of field + tilt (items N + R) ---------
    float4 laP24;        // x axisX(uv) y axisY(uv) z focusDepth  w dofMaxPx(1440p)
    float4 laP25;        // x fieldCurve y tilt     z cos(tiltAng) w sin(tiltAng)
    float4 laP26;        // x band(uv)  y 1/cocSpan z fovK        w diffraction
    // --- droplet lens shading (item X) ------------------------------------
    float4 laP27;        // x lens  y centre  z bandW(uv)  w spec
    float4 laP28;        // x lampX(uv) y lampY(uv)  z dyeDepth w dyeTilt
    float4 laP29;        // x massRim    y rimW(uv)   z -           w -
    // --- multicolour oil (brief AE) ---------------------------------------
    float4 laP30;        // x hue2Amt  y hue2Deg   z hue3Amt   w hue3Deg
    float4 laP31;        // x crustHueMix  y -  z -  w -
    // --- CAST SHADOWS (brief BC) ------------------------------------------
    float4 laP32;        // x shadowAmt y shadowLen(p-units) z shadowSoft w lightZ
    // The 20x12 mix field, four cells per float4. Small on purpose: the
    // patches the reference shows are a quarter to a half of the frame, so
    // this carries them with room to spare and costs one cbuffer fetch and a
    // bilinear blend per pixel -- no texture, no descriptor.
    float4 laMix[60];
};
// xy = centre uv, z = radius, w = field weight (+1 oil, negative = hole)
// rgb of .b = flat fill colour, .w = rise_stretch anisotropy (0 = round)
// .c = mouse_oil_mode 2 (comb): x = stretch amount along the DRAG direction
// (0 = off, and then the whole comb branch is skipped and the round/rise
// path below is the original code), yz = that direction as a unit vector in
// p-space. Zero for every blob unless the comb is actually running.
struct AcidBlobGPU { float4 a; float4 b; float4 c; };
StructuredBuffer<AcidBlobGPU> AcidBlobs : register(t1);

// ---------------------------------------------------------------------------
// DROPLET PARTICLE SIM. One float4 per particle: xy = centre uv, z = visible
// radius SIGNED (negative = water trapped in the oil, i.e. a hole; positive =
// an oil droplet on open ink), w = the CPU-side contribution gate (0 while a
// droplet is on the wrong side of the interface or below a pixel across --
// invisible either way, and without the gate its steep little gradient would
// still collapse sdf and print a hollow rim ring; see StepAcidDroplets). The CPU bins them into a uniform
// grid and uploads them sorted by cell; DropCells[c] = (first, count), so a
// pixel only walks the 3x3 cells around it. The support radius is clamped on
// the CPU to one cell, which is what makes 3x3 exact rather than approximate.
// Both buffers are all-zero (count 0 everywhere) when the sim is off.
// ---------------------------------------------------------------------------
StructuredBuffer<float4> AcidDrops : register(t2);
StructuredBuffer<uint2>  DropCells : register(t4);
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
float AcidHash21(float2 p) {
    p = frac(p * float2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return frac(p.x * p.y);
}
float AcidVNoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(AcidHash21(i),               AcidHash21(i + float2(1, 0)), u.x),
                lerp(AcidHash21(i + float2(0, 1)), AcidHash21(i + float2(1, 1)), u.x), u.y);
}
float AcidFbm(float2 p) {
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 3; i++) { v += amp * AcidVNoise(p); p = p * 2.13 + 19.19; amp *= 0.5; }
    return v;
}
// n flat bands with a soft shoulder at each step: the ink's "flat colour
// bands" without the hard 8-bit staircase.
float AcidBand(float x, float n, float soft) {
    float f = floor(x * n), fr = frac(x * n);
    return (f + smoothstep(0.5 - soft, 0.5 + soft, fr)) / n;
}
// Hue rotation that PRESERVES saturation and value, so a colour is exactly as
// vivid at its new hue as it was at its old one. CssHueRotate (the W3C matrix)
// holds luma instead, so a bright orange rotated toward yellow lands on olive
// and a saturated red-orange lands on pastel lavender - which is what the
// first sweep build did. Every reference frame is vivid at every hue, so the
// sweep and the complement lock both rotate here, not in the matrix.
float3 AcidRgb2Hsv(float3 c) {
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float d  = mx - mn;
    float h  = 0.0;
    if (d > 1e-7) {
        if (mx == c.r)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
        else                h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return float3(h, (mx > 1e-7) ? d / mx : 0.0, mx);
}
float3 AcidHsv2Rgb(float3 c) {
    float3 q = abs(frac(c.x + float3(1.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return c.z * lerp(1.0, saturate(q - 1.0), c.y);
}
float3 AcidHueShift(float3 c, float deg) {
    float3 hsv = AcidRgb2Hsv(c);
    hsv.x = frac(hsv.x + deg * 0.0027777778);
    return AcidHsv2Rgb(hsv);
}

// ---- DIFFRACTION (item W) ------------------------------------------------
// The user, on an in-focus ring: "a thin hair on a film will never cause pure
// darkness because the light bends around it; it's not out of focus per se."
// A feature narrower than the point spread has light filled in from all round
// it, so it cannot reach full darkness however well it is focused -- that is
// diffraction, not defocus, and it is why a hair on film is grey. `size` and
// `kp` are both in p-space: one point spread across gives 63% of full
// darkness, two gives 98%, and anything several spreads wide is untouched.
// Only ever REMOVES darkness from small things, so the black under a big mass
// is exactly the black it was.
float AcidDiffract(float size, float kp, float amt) {
    [branch] if (amt <= 0.0 || kp <= 0.0) return 1.0;
    float x = size / kp;
    return lerp(1.0, 1.0 - exp(-x * x), saturate(amt));
}

// Procedural bubble swarm. A jittered cellular layer of round droplets with a
// wide (squared-hash) size range: hundreds of bubbles of every size for ~20
// hashes per pixel, where the same thing in metaballs would cost hundreds of
// loop iterations. Returns the signed distance to the nearest droplet in
// p-space units (negative inside).
// `nrm` comes back as the unit vector from the winning droplet's centre
// toward the pixel and `rad` as that droplet's radius (both in p-space), so
// the caller can shade a droplet as a lens (soft edge, offset highlight)
// instead of a flat disc. Free: the loop already has both.
float AcidSwarm(float2 p, float scale, float density, float clumpAmt,
                float rmin, float rmax, out float2 nrm, out float rad) {
    float2 pc = p * scale;
    float2 base = floor(pc);
    float d = 1e9;
    float2 bestV = float2(1.0, 0.0);
    float  bestR = 1e-3;
    [unroll] for (int oy = -1; oy <= 1; oy++) {
        [unroll] for (int ox = -1; ox <= 1; ox++) {
            float2 c = base + float2(ox, oy);
            // Clustering is decided PER CELL, from a low-frequency value noise
            // of the cell index. It must not depend on the shading pixel: an
            // earlier version modulated `density` by a per-pixel fbm, so the
            // presence test could flip part-way across one droplet and slice a
            // straight-edged wedge out of it (the dark chips in the oil).
            float  dens = density * lerp(1.0,
                          smoothstep(0.34, 0.62, AcidVNoise(c * 0.17)) * 1.8, clumpAmt);
            float  h  = AcidHash21(c * 1.37 + 0.11);
            // cube on the radius hash: many tiny droplets, a few large ones
            float  hr = AcidHash21(c * 2.71 + 5.3);
            // empty cells get a negative radius, so their distance never wins
            float  r  = (h < dens) ? lerp(rmin, rmax, hr * hr * hr) : -2.0;
            float2 j  = float2(AcidHash21(c + 3.71), AcidHash21(c + 9.13));
            float2 v  = pc - (c + 0.18 + 0.64 * j);
            float  dd = length(v) - r;
            if (dd < d) { d = dd; bestV = v; bestR = max(r, 1e-3); }
        }
    }
    nrm = bestV / max(length(bestV), 1e-5);
    rad = bestR / max(scale, 1e-3);
    return d / max(scale, 1e-3);
}
#endif

)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
#if defined(INK) || defined(LIQUID_ACID)
// ==========================================================================
// SHARED "ink in water" block — compiled into the INK PSO (style=ink) and
// into the LIQUID_ACID PSO (used when [liquid_acid] ink_mode=water, so the
// oil floats on ink-in-water instead of on posterised bands). One
// implementation, two looks; the fluid PSO has neither macro and therefore
// none of this code.
//
// The dye field stops being a colour and becomes an optical DEPTH. Refs:
// reference/shots/photos/ink-in-water-ref-*.jpg.
// ==========================================================================
cbuffer InkCB : register(b2) {
    float4 ikP0;        // x density k, y chroma, z edgeStrength, w edgeScale
    float4 ikP1;        // x edgeLo,   y edgeHi,  z inverted,     w vignette
    float4 ikP2;        // x parallax, y parallaxScale, z parallaxDrift, w time
    float4 ikP3;        // x coreKnee, y hdrCore, z motionLo, w motionHi
    float4 ikP4;        // x motionOpacity, y veilFloor, z tintMidDip, w tonemap
    float4 ikPaper;     // paper / background colour
    float4 ikTintThin;  // inverted: light through a thin veil
    float4 ikTintThick; // inverted: light out of an opaque core
    float4 ikP5;        // x whiteScRGB, y blackScRGB, z toneKnee, w toneChroma
    float4 ikP6;        // x tintHueBlend, y peakScRGB, z -, w -
};

// HSV for the duotone pair blend. Named apart from the LIQUID_ACID pair so
// both can live in the acid PSO.
float3 InkRgb2Hsv(float3 c) {
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float d = mx - mn;
    float h = 0.0;
    if (d > 1e-6) {
        if (mx == c.r)      h = frac(((c.g - c.b) / d) / 6.0);
        else if (mx == c.g) h = (((c.b - c.r) / d) + 2.0) / 6.0;
        else                h = (((c.r - c.g) / d) + 4.0) / 6.0;
    }
    return float3(h, (mx > 1e-6) ? d / mx : 0.0, mx);
}
float3 InkHsv2Rgb(float3 c) {
    float3 p = abs(frac(c.x + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * lerp(float3(1.0, 1.0, 1.0), saturate(p - 1.0), c.y);
}

// Ink "thickness" carried by one dye texel. Max channel, not luma: a coloured
// ink must absorb by its strongest component or a pure blue drop reads thinner
// than a grey one of the same concentration.
// Low-res (64x36) copy of the velocity field, the same texture the Liquid Acid
// blob advection reads back. Used only to tell MOVING ink from ink that is
// just sitting there.
Texture2D<float4> VelLow : register(t3);

float InkDensity(float3 c) { return max(c.r, max(c.g, c.b)); }

// Motion gate. A drop's dye stamp always leaves some dye at the injection
// point that never got any momentum; in inverted + HDR that stationary patch
// is the brightest thing on screen, which is not what a drop entering water
// looks like. Gate the HDR lift on |velocity| so only ink that is actually
// being carried gets to go above SDR white. motionHi <= motionLo disables it.
float InkMotion(float2 uv) {
    if (ikP3.w <= ikP3.z) return 1.0;
    float2 v = VelLow.SampleLevel(linearClamp, uv, 0).xy;
    return smoothstep(ikP3.z, ikP3.w, length(v));
}

// Edge darkening. A sheet of ink seen EDGE-ON is a longer optical path, which
// is why every fold in the references has a dark outline; in 2D the stand-in
// is the density gradient. 4 taps at edge_scale screen texels.
float InkEdge(float2 uv, float2 texel, float scale) {
    float2 ex = float2(texel.x * scale, 0.0), ey = float2(0.0, texel.y * scale);
    float L = InkDensity(Dye.SampleLevel(linearClamp, uv - ex, 0).rgb);
    float R = InkDensity(Dye.SampleLevel(linearClamp, uv + ex, 0).rgb);
    float T = InkDensity(Dye.SampleLevel(linearClamp, uv - ey, 0).rgb);
    float B = InkDensity(Dye.SampleLevel(linearClamp, uv + ey, 0).rgb);
    return smoothstep(ikP1.x, max(ikP1.y, ikP1.x + 1e-4),
                      0.5 * length(float2(R - L, B - T)));
}

// C   = RAW dye (pre-emboss, pre-CSS chain).
// pos = SV_Position.xy, for the paper vignette.
// Returns the display colour in the SAME sRGB-encoded space the fluid look's
// C is in before SRGBToLinear, plus the level that drives the HDR gain.
float3 InkWater(float3 C, float2 uv, float2 texel, float2 pos, out float hdrM) {
    float  d = InkDensity(C);
    // Unit-max chroma of this parcel. Dye-free pixels are exactly (0,0,0), so
    // the divide must be guarded or the NaN propagates through every lerp
    // below and blacks the pixel out (the same trap the acid atan2 fell into).
    float3 chroma = (d > 1e-4) ? C / d : float3(1.0, 1.0, 1.0);
    float  thick = d * (1.0 + ikP0.z * InkEdge(uv, texel, ikP0.w));
    // Same motion gate, on the optical path itself: ink that is not being
    // carried anywhere reads thinner. This is what demotes the stationary
    // entry patch from an opaque white disc to a faint mist, without touching
    // the plume, whose interior is still swirling.
    if (ikP4.x > 0.001) thick *= lerp(1.0 - saturate(ikP4.x), 1.0, InkMotion(uv));
    if (ikP2.x > 0.001) {
        // Cheap second layer: the SAME dye sampled at a slightly different
        // scale and drift, blurred, added to the path length. Reads as ink
        // hanging at another depth. A real second field would double the
        // dominant dye-res passes; this is 4 taps.
        float2 dr = float2(ikP2.z * ikP2.w, ikP2.z * ikP2.w * 0.6);
        float2 duv = (uv - 0.5) * ikP2.y + 0.5 + dr;
        float2 e = texel * 2.0;
        float d2 = 0.25 * (InkDensity(Dye.SampleLevel(linearClamp, duv + float2( e.x,  e.y), 0).rgb)
                         + InkDensity(Dye.SampleLevel(linearClamp, duv + float2(-e.x,  e.y), 0).rgb)
                         + InkDensity(Dye.SampleLevel(linearClamp, duv + float2( e.x, -e.y), 0).rgb)
                         + InkDensity(Dye.SampleLevel(linearClamp, duv + float2(-e.x, -e.y), 0).rgb));
        thick += ikP2.x * d2;
    }
    // Coloured absorption: the channel the ink is made of absorbs least, so
    // the light that gets through is the ink's own colour. chroma = 0 gives
    // a = 1 on every channel = a neutral (black) ink.
    float3 a = 1.0 + ikP0.y * (1.0 - chroma);
    float3 T = exp(-ikP0.x * thick * a);
    if (ikP1.z < 0.5) {
        // ---- PAPER: backlit white ground, ink subtracts from it ----------
        float2 sp = pos * texel - 0.5;
        float  v = 1.0 - ikP1.w * smoothstep(0.35, 1.0, length(sp) * 1.6);
        hdrM = 0.0;                       // a white field never gets HDR gain
        return ikPaper.rgb * T * v;
    }
    // ---- INVERTED: pale ink on black (the OLED-friendly one) -------------
    float  op = 1.0 - exp(-ikP0.x * thick);
    // veil floor: faint dye (old, diffused wash) goes to the background
    // instead of a semi-transparent haze
    if (ikP4.y > 0.001) op = saturate((op - ikP4.y) / max(1.0 - ikP4.y, 1e-3));
    float  tk = smoothstep(min(ikP3.x, 0.99), 1.0, op);
    float3 tint = lerp(ikTintThin.rgb, ikTintThick.rgb, tk);
    // tint_hue_blend: THE reason a vermillion ink reads as pale peach. The RGB
    // lerp above walks a COMPLEMENTARY pair straight through grey — teal
    // (0.04,0.63,0.65) to vermillion (0.90,0.27,0.15) crosses (0.47,0.45,0.40),
    // a dead beige — and `tk` spends most of a plume in that middle, so the
    // beige is most of what the look actually shows. Restore the blend's
    // SATURATION and VALUE to the anchors' own (lerped), keeping the RGB-lerp
    // HUE: the duotone stays a duotone (no third colour is invented, which is
    // what walking the hue wheel does — teal->vermillion the short way is a
    // trip through green and yellow) but nothing in it is ever washed out.
    if (ikP6.x > 0.001) {
        float3 hsv = InkRgb2Hsv(tint);
        float3 a = InkRgb2Hsv(ikTintThin.rgb), b = InkRgb2Hsv(ikTintThick.rgb);
        float w = saturate(ikP6.x);
        hsv.y = lerp(hsv.y, lerp(a.y, b.y, tk), w);
        hsv.z = lerp(hsv.z, lerp(a.z, b.z, tk), w);
        tint = InkHsv2Rgb(hsv);
    }
    // Complementary tints blend to grey-brown at the midpoint (the flat "mud"
    // on smooth mid-density plumes). Real ink goes DARK where it is neither
    // thin nor saturated, so dip the blend toward black around the midpoint.
    // Done on the opacity, so the dip shows the BACKGROUND colour (paper_color),
    // which need not be black.
    op *= 1.0 - ikP4.z * pow(4.0 * tk * (1.0 - tk), 2.0);
    hdrM = d * ikP3.y * InkMotion(uv);    // hot cores expand, veils stay SDR
    return lerp(ikPaper.rgb, tint * lerp(float3(1.0, 1.0, 1.0), chroma,
                                         saturate(ikP0.y)), op);
}
#endif
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

// The reference wrote dye values into an sRGB canvas: the display decoded them
// through the sRGB curve. Our swap chain is LINEAR scRGB, so we must apply
// that decode ourselves or the tone curve (and color mixing) is completely
// different. sdrScale then maps 1.0 to the user's SDR white level in HDR mode.
float3 SRGBToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(lo, hi, step(0.04045, c));
}

// CSS/SVG colour-matrix primitives (W3C Filter Effects), Rec.709 luma.
float3 CssSaturate(float3 c, float s) {
    return float3(
        dot(float3(0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s), c),
        dot(float3(0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s), c),
        dot(float3(0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s), c));
}
float3 CssHueRotate(float3 c, float deg) {
    float a = radians(deg);
    float co = cos(a), si = sin(a);
    return float3(
        dot(float3(0.213 + 0.787 * co - 0.213 * si, 0.715 - 0.715 * co - 0.715 * si, 0.072 - 0.072 * co + 0.787 * si), c),
        dot(float3(0.213 - 0.213 * co + 0.143 * si, 0.715 + 0.285 * co + 0.140 * si, 0.072 - 0.072 * co - 0.283 * si), c),
        dot(float3(0.213 - 0.213 * co - 0.787 * si, 0.715 - 0.715 * co + 0.715 * si, 0.072 + 0.928 * co + 0.072 * si), c));
}
float3 CssContrast(float3 c, float k) { return c * k + 0.5 * (1.0 - k); }

float4 PSMain(VSOut i) : SV_Target {
    // Mirroring first: everything downstream reads the folded coordinate.
    // NOT mirrored, by design: i.pos.xy (the paper vignette and the film
    // grain stay screen-space effects, as they would be on real film), and
    // the one-pixel fwidth() spike exactly on a seam.
    float mirrorSeam;
    float2 uv = MirrorFold(i.uv, mirrorSeam);
    // The [post] camera pass reads this pixel's CIRCLE OF CONFUSION out of the
    // render target's ALPHA (px at 1440p). Only the acid look can produce one
    // -- it is the only look with an isoline and a per-droplet depth -- so
    // every other path leaves the 1.0 the target has always carried, and the
    // post pass is told (dof_max_px = 0) not to read it.
    float outCoc = 1.0;
#ifdef LIQUID_ACID
    // ---- oil metaball field (evaluated first: it refracts the ink sample) ----
    const float aspect = laP6.w;
    float2 pp = float2(uv.x * aspect, uv.y);
    float  field = 0.0;
    float2 grad  = float2(0.0, 0.0);
    float  colW = 0.0;
    float3 colSum = float3(0.0, 0.0, 0.0);
    // How deep inside a hollow droplet's interior this pixel is; 0 unless
    // droplet_ring_frac is on.
    float  ringIn = 0.0;
    int nb = (int)laP0.x;
    const float thresh = laP0.y;
    [loop]
    for (int bi = 0; bi < nb; bi++) {
        AcidBlobGPU B = AcidBlobs[bi];
        float2 q  = pp - float2(B.a.x * aspect, B.a.y);
        // rise_stretch: an ANISOTROPIC kernel is the cheapest teardrop there
        // is -- squash the distance along the blob's travel (here y, the rise
        // axis) and the same round kernel draws an ellipse. e = 1 is exact
        // arithmetic, so a blob with no stretch is bit-identical to before.
        // Written as dot() on the SCALED vector, not as an expanded
        // q.x*q.x + qy*qy: with no stretch e is exactly 1, q.y / 1 is exact,
        // and dot() then emits the instruction the untouched loop emitted, so
        // an existing acid ini renders bit-for-bit as it did. (Expanding it
        // by hand moved the rounding and changed every acid frame.)
        float  e  = 1.0 + B.b.w;
        // mouse_oil_mode = 2 (comb) stretches along the DRAG direction rather
        // than the rise axis, so the same anisotropy is built in the blob's
        // own frame. B.c.x is 0 for every blob unless the comb is running and
        // the condition is uniform across the wave (all lanes read the same
        // blob), so [branch] genuinely skips it and the else arm below is the
        // untouched original line.
        float2 qs;
        [branch] if (B.c.x > 0.0) {
            float2 ax = B.c.yz, pv = float2(-B.c.z, B.c.y);
            qs = float2(dot(q, ax) / (1.0 + B.c.x), dot(q, pv));
        } else {
            qs = float2(q.x, q.y / e);
        }
        float  d2 = dot(qs, qs);
        float  sup = B.a.z * laP0.z;              // support radius
        float  s2 = sup * sup;
        if (d2 >= s2) continue;
        // Wyvill/Blinn compact kernel (1 - t^2)^3, t = d / support. Bounded,
        // C2, no singularity and no clamping — unlike the inverse-square
        // metaball, whose broad field ~= 1 plateau made hole boundaries
        // ragged and forced the gradient-clamp guard.
        float  u  = 1.0 - d2 / s2;
        float  u2 = u * u;
        float  w  = u2 * u;
        field += w * B.a.w;
        // d(d2)/dp for the scaled distance is 2*(q.x, q.y/e^2), so the
        // gradient stays consistent with the ellipse and the sdf, the rim and
        // the lens radius all follow the stretched shape instead of the
        // circle it was drawn from. (e = 1 leaves this the original line.)
        float2 gq;
        [branch] if (B.c.x > 0.0) {
            float2 ax = B.c.yz, pv = float2(-B.c.z, B.c.y);
            float  ec = 1.0 + B.c.x;
            gq = (dot(q, ax) / (ec * ec)) * ax + dot(q, pv) * pv;
        } else {
            gq = float2(q.x, q.y / (e * e));
        }
        grad  += (-6.0 * u2 / s2) * gq * B.a.w;
        // Flat fill from a SOFT-max over the blob weights. A hard argmax drew
        // a crisp circle wherever the dominant blob handed over inside a
        // merged mass; a plain influence-weighted mean is what turned the oil
        // PoC milky. w^4 is the middle road: tails contribute nothing, the
        // handover is a soft gradient a few pixels wide.
        if (B.a.w > 0.0) {
            float w2 = w * w, w4 = w2 * w2;
            colSum += B.b.rgb * w4;
            colW   += w4;
        }
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- droplets: the SAME surface, not an overlay ----------------------
    // Every droplet's Wyvill kernel is added into `field`/`grad` BEFORE the
    // threshold, so a trapped water droplet is a literal hole in the oil and
    // an oil droplet on the ink is a literal bump of it: both get the soft
    // thickness edge, the thin-oil colour fringe, the emergent halo, the lens
    // highlight and the metaball NECK as two of them approach -- which is
    // what makes a coalescence read as surface tension instead of a cut.
    //
    // The negative (hole) weight is scaled by the LOCAL oil field, not by a
    // constant: deep inside a merged mass the field is several units high and
    // a fixed -1.2 would not reach the isoline, so small holes would simply
    // vanish wherever the oil is thick. Scaling by max(field, 1) punches the
    // same relative depth everywhere. Its own spatial derivative is ignored
    // in `grad` on purpose: the blob field varies over ~0.1 p-units and a
    // droplet over ~0.01, so the droplet term dominates the edge anyway.
    float2 gradB = grad;      // blobs only -- the surface the LENS belongs to
    // ...and the blobs-only FIELD with it. A crust droplet (brief AB) sits in
    // a dye mass but PUNCHES the field above the threshold at its own pixels,
    // so its core reads as oil and sdf alone cannot tell it from the open
    // film. The blob field before any droplet is added can: below the
    // threshold here means "this is inside a mass", droplet or not, which is
    // what crust_hue_mix has to key off.
    float fieldB = field;
    // ---- per-pixel DEPTH, for the circle of confusion (items N + R) ------
    // A weighted mean of the depths of whatever covers this pixel, primed
    // with the MASSES' own plane (0.5) at a low weight: open film reads as the
    // base depth, a pixel well inside a droplet reads as that droplet's, and
    // the ground between them is a smooth blend rather than a stencil -- which
    // matters, because a hard depth edge would print a hard blur edge in the
    // post pass and look like a cut-out instead of a lens.
    // Where the optical axis meets the dish, in p-space. Everything about the
    // perspective view -- foreshortening, field curvature, the tilt gradient
    // -- is measured from here.
    const float2 axP = float2(laP24.x * aspect, laP24.y);
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- THE DYE'S OWN DEPTH (item AA) -----------------------------------
    // This prior used to be the constant 0.5 -- which is exactly camera_focus,
    // so every pixel with no droplet in it, i.e. every big dye mass, sat on
    // the plane of focus BY DEFINITION and could only blur through the shape
    // of the focus surface. The user saw it straight away: "the bottom right
    // blob isn't getting more out of focus even as it approaches the edge."
    //
    // The dye is a layer at its own height instead. Its slope runs along the
    // direction from the lens centre to the RIG's lamp, so the slab is not
    // parallel to the focus surface: the two cross on a LINE rather than
    // agreeing over a region, and that line travels as the lamp drifts. With
    // dye_depth 0.5 and dye_depth_tilt 0 this is the old constant, to the bit.
    float2 lampP = float2(laP28.x * aspect, laP28.y) - axP;
    float2 lampD = lampP / max(length(lampP), 1e-5);
    float  dyeW  = laP16.y;
    float  dyeZ  = laP28.z + laP28.w * dot(pp - axP, lampD);
    float depSum = dyeZ * dyeW, depW = dyeW;
    if (laP18.x > 0.5) {
        const int gw = (int)laP18.y, gh = (int)laP18.z;
        const int cx = clamp((int)floor(uv.x * gw), 0, gw - 1);
        const int cy = clamp((int)floor(uv.y * gh), 0, gh - 1);
        float  dNeg = 0.0, dPos = 0.0;
        float2 gNeg = float2(0.0, 0.0), gPos = float2(0.0, 0.0);
        [loop]
        for (int oy = -1; oy <= 1; oy++) {
            int yy = cy + oy;
            if (yy < 0 || yy >= gh) continue;
            [loop]
            for (int ox = -1; ox <= 1; ox++) {
                int xx = cx + ox;
                if (xx < 0 || xx >= gw) continue;
                uint2 cell = DropCells[yy * gw + xx];
                [loop]
                for (uint di = 0; di < cell.y; di++) {
                    float4 D = AcidDrops[cell.x + di];
                    // .w packs the gate with the ring FLAG (see
                    // UploadAcidConstants): w = gate + 2*ring, gate in [0,1].
                    // A flag, not a size: the ring's width is a global key, so
                    // nothing per-droplet is lost and the particle stays one
                    // float4.
                    // ...and, above both, an 8-bit DEPTH with 0 as the "no
                    // depth" sentinel: w = 4*dq + 2*ring + gate. Three extra
                    // ALU in the hottest loop of the frame, against a second
                    // buffer fetch per droplet per pixel if depth had its own
                    // record -- and with droplet_depth 0 the arithmetic here
                    // is exactly the two lines it replaces.
                    float  dwf  = D.w;
                    float  dqz  = floor(dwf * 0.25);
                    float  dwr  = dwf - 4.0 * dqz;
                    float  rng  = (dwr > 1.5) ? 1.0 : 0.0;
                    float  gate = dwr - 2.0 * rng;
                    float  ddep = (dqz > 0.5) ? (dqz - 1.0) * (1.0 / 254.0) : 0.5;
                    float2 dq = pp - float2(D.x * aspect, D.y);
                    float  R  = abs(D.z);
                    float  ds = R * laP19.x;
                    float  ds2 = ds * ds;
                    float  dd2 = dot(dq, dq);
                    if (dd2 >= ds2) continue;
                    [branch] if (rng > 0.5) {
                        // HOLLOW LENS: the punch is a BAND centred on the
                        // droplet's own visible radius, so the interior never
                        // crosses the isoline and keeps its oil -- the "empty
                        // double" the user photographed, drawn deliberately.
                        // Its gradient points radially away from the band's
                        // centre line, so the dark rim, the meniscus and the
                        // thickness ramp all wrap the annulus exactly as they
                        // wrap a disc: no special-case shading anywhere.
                        // ---- PERSPECTIVE: seen at an angle, not from above -
                        // The user's sketch: a LENS at the tip of a view cone
                        // over a flat dish. Only the point where the optical
                        // axis meets the dish is seen face on; every other
                        // element is seen at an angle that grows with its
                        // distance from that point, so a ring there is a
                        // circle in PERSPECTIVE -- foreshortened along the
                        // radius toward the axis, its far wall thicker than
                        // its near one. That is the real answer to "these are
                        // pixel perfect circles": not a wobble bolted on, but
                        // a camera that cannot draw a symmetric one off-axis.
                        // camera_fov 0 leaves invc exactly 1 and every line
                        // below is the arithmetic that was here before.
                        float2 av   = float2(1.0, 0.0);
                        float  tanT = 0.0, invc = 1.0;
                        float2 dqv  = dq;
                        [branch] if (laP26.z > 0.0) {
                            float2 toAx = axP - float2(D.x * aspect, D.y);
                            float  la   = length(toAx);
                            [branch] if (la > 1e-5) {
                                av   = toAx / la;
                                tanT = laP26.z * la;
                                // the apparent extent along that radius is
                                // multiplied by cos(theta), so testing the
                                // pixel against a circle means stretching its
                                // offset along it by 1/cos(theta).
                                invc = sqrt(1.0 + tanT * tanT);
                                dqv  = dq + av * (dot(dq, av) * (invc - 1.0));
                            }
                        }
                        float  dd = length(dqv);
                        float2 un = dqv / max(dd, 1e-6);
                        // ---- never a perfect circle -----------------------
                        // The user, on the first rings: "these are pixel
                        // perfect circles." The band's radius is a Fourier
                        // series in the polar angle (see UploadAcidConstants),
                        // and cos/sin of 2*phi and 3*phi come straight out of
                        // the radial unit vector -- no atan2, no trig at all.
                        float4 S  = AcidDrops[(uint)laP19.w + cell.x + di];
                        float  c2 = un.x * un.x - un.y * un.y;
                        float  s2 = 2.0 * un.x * un.y;
                        float  c3 = un.x * (4.0 * un.x * un.x - 3.0);
                        float  s3 = un.y * (3.0 - 4.0 * un.y * un.y);
                        float  wv = S.x * c2 + S.y * s2 + S.z * c3 + S.w * s3;
                        float  wd = 2.0 * (S.y * c2 - S.x * s2)
                                  + 3.0 * (S.w * c3 - S.z * s3);   // d/d(phi)
                        float  Rp = R * (1.0 + wv);
                        float  bw = max(saturate(laP20.x) * ds, 1e-6);
                        // a stretched film THINS: the wall is narrower where
                        // the ring bulges out and thicker where it pinches in,
                        // so the wall is not a uniform stroke either.
                        bw *= clamp(1.0 - 1.6 * wv, 0.55, 1.7);
                        // ...and the OBLIQUE view thickens the wall on the far
                        // side and thins it on the near one: the line of sight
                        // crosses more of the bubble's skin where it enters at
                        // a glancing angle. dot(un, av) is +1 on the side
                        // facing the optical axis, -1 on the side away from
                        // it, so this is one mad and it is exactly the
                        // asymmetry a stamped "O" can never have.
                        float  asym = clamp(0.90 * tanT, 0.0, 0.45);
                        bw *= 1.0 - asym * dot(un, av);
                        // ...and a ring diffracts by its WALL WIDTH, not by
                        // its radius: a big hoop drawn with a two-pixel wall
                        // is exactly the hair in the user's photograph, and it
                        // has no business being pure black either.
                        float  gW = gate * AcidDiffract(bw, laP20.w, laP26.w);
                        float  sgn = dd - Rp;
                        float  su = 1.0 - (sgn * sgn) / (bw * bw);
                        if (su > 0.0) {
                            float  su2 = su * su;
                            float  dw  = su2 * su * gW;
                            // d/d(dd) of the profile, carried onto grad(sgn).
                            // grad(phi) = (-un.y, un.x)/dd, so an out-of-round
                            // band's normal leans off the radius exactly as
                            // much as its radius is changing with the angle --
                            // which is what keeps the rim, the meniscus and
                            // the halo wrapped square on the wall.
                            float2 gs = un - (R * wd / max(dd, 1e-6))
                                             * float2(-un.y, un.x);
                            // gs lives in the foreshortened frame; the map
                            // back to p-space is the same symmetric stretch,
                            // so the rim, the meniscus and the halo stay
                            // square on the wall of an oblique ring instead of
                            // sliding round it. Identity at camera_fov 0.
                            gs += av * (dot(gs, av) * (invc - 1.0));
                            float2 dg  = ((-6.0 * su2 / (bw * bw)) * sgn * gW) * gs;
                            dNeg += dw; gNeg += dg;
                            depSum += dw * ddep; depW += dw;
                        }
                        // droplet_ring_lift: the interior is a little lens, so
                        // it reads slightly brighter than the film around it.
                        // max(), not a sum, so a raft of touching rings does
                        // not stack into a glowing patch.
                        float li = saturate((Rp - 0.5 * bw - dd) / max(0.5 * Rp, 1e-5));
                        // The oblique view also swings the highlight: a lens
                        // tipped away from you shows its bright face on the
                        // side turned toward the axis. So the interior lift is
                        // not centred either -- the whole ring is asymmetric,
                        // wall and light together, which is what stops it
                        // reading as a glyph.
                        li *= 1.0 + clamp(1.10 * tanT, 0.0, 0.55) * dot(un, av);
                        ringIn = max(ringIn, saturate(li) * gW);
                        depSum += li * gW * ddep * 0.5; depW += li * gW * 0.5;
                        continue;
                    }
                    // DIFFRACTION by the droplet's own size: a 3-px dot
                    // cannot punch the film fully black, so it comes out a
                    // soft grey spot instead of a hard hole. Carried on the
                    // gate, which means the field AND its gradient stay
                    // consistent and the edge softens with the fill -- no
                    // separate shading path, and nothing to fight the halo.
                    float  gS = gate * AcidDiffract(R, laP20.w, laP26.w);
                    float  du = 1.0 - dd2 / ds2;
                    float  du2 = du * du;
                    float  dw = du2 * du * gS;
                    float2 dg = (-6.0 * du2 / ds2) * dq * gS;
                    if (D.z < 0.0) { dNeg += dw; gNeg += dg; }
                    else           { dPos += dw; gPos += dg; }
                    depSum += dw * ddep; depW += dw;
                }
            }
        }
        if (dNeg > 0.0) {
            float pun = max(field, 1.0) * laP19.y;
            field -= dNeg * pun;
            grad  -= gNeg * pun;
        }
        if (dPos > 0.0) {
            // Symmetric with the hole punch above, and for the same reason: a
            // FIXED positive weight only clears the isoline where the ink is
            // already close to it, so an oil droplet that drifted into a deep
            // hole raised the field a little, crossed nothing, and left just
            // its rim -- a pink-ringed dot inside a black tail, which is one
            // of the three the user photographed. Carry whatever deficit is
            // actually here, plus the authored weight on top.
            float pup = laP19.z + max(thresh - field, 0.0);
            field += dPos * pup;
            grad  += gPos * pup;
            // same soft-max fill blend the blobs use, so an oil droplet takes
            // the palette's own bright shade and merges colour with a blob it
            // touches instead of stamping a flat disc over it.
            float dq2 = saturate(dPos), dq4 = dq2 * dq2 * dq2 * dq2;
            colSum += laOil[3].rgb * dq4;
            colW   += dq4;
        }
        // ---- droplet_ring_lift: the interior of a hollow droplet ---------
        // A bubble's middle is a lens, not a hole: it is the same film seen
        // through a curved surface, so it sits a hair thicker and a hair
        // brighter than the sheet around it. Both go in BEFORE the threshold
        // and through the same soft-max fill the blobs use, so the interior
        // keeps the local palette colour and the band around it is untouched.
        [branch] if (ringIn > 0.0 && laP20.y > 0.0005) {
            float k  = saturate(laP20.y);
            field += ringIn * k * 0.25 * thresh;
            float wl = ringIn * ringIn; wl = wl * wl * k;
            colSum += laOil[3].rgb * wl;
            colW   += wl;
        }
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- CIRCLE OF CONFUSION -> the render target's ALPHA (items N + R) ---
    // The [post] pass used to defocus the whole frame by one radius, which is
    // what a flatbed scanner does, not a lens. A lens has ONE surface in focus
    // and everything else blurs in proportion to how far off it is -- the
    // Requiem frame the user picked out is one cell sharp with the rings
    // behind it dissolved into ghosts. So the depth of field is decided HERE,
    // where the depths are known, and handed to the post pass per pixel:
    //   * the surface in focus is not a plane parallel to the dish. It is
    //     CURVED (camera_field_curve: a real lens cannot hold the centre and
    //     the corners at once) and TILTED (focus_tilt: the freelensing sweet
    //     spot, a slanted strip of sharpness), both measured from where the
    //     optical axis meets the dish;
    //   * focus_band_px is a width in the PICTURE, so it is turned into a
    //     depth tolerance by the local gradient of that surface -- the sharp
    //     strip is that many pixels across however hard the lens is tilted,
    //     which is the number a photographer would actually reach for;
    //   * outside the band the radius ramps up over a fixed depth interval
    //     and is clamped at dof_max_px, so a far element goes soft and stays
    //     soft instead of dissolving.
    [branch] if (laP24.w > 0.0) {
        float2 qo  = pp - axP;
        float  rr  = length(qo);
        float  fz  = laP24.z + laP25.x * rr * rr
                             + laP25.y * dot(qo, float2(laP25.z, laP25.w));
        float  gm  = abs(laP25.y) + 2.0 * abs(laP25.x) * rr;
        float  tol = 0.5 * laP26.x * gm;
        float  dz  = abs(depSum / max(depW, 1e-6) - fz);
        // ...and it falls away FAST. The user's focus reference is a macro
        // shot where the one plane in focus resolves fine texture and
        // everything off it is already a wash: a shallow depth of field with a
        // STEEP shoulder, not a gentle ramp. So the radius leaves zero the
        // moment the element clears the sharp band and is most of the way to
        // the clamp within a tenth of the depth range -- x*(2-x) rather than a
        // straight line, over a deliberately short span.
        float  x   = saturate((dz - tol) * laP26.y);
        outCoc = laP24.w * x * (2.0 - x);
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    float3 oilBase = (colW > 1e-9) ? colSum / colW : laOil[0].rgb;
    float  gl   = length(grad) + 1e-6;
    float  sdf  = clamp((field - thresh) / gl, -0.25, 0.25);  // >0 inside the oil
    // ---- [post] softness: nothing in a macro shot is razor-sharp ---------
    // The user, on the live panel: "these edges are way too accurate and
    // focused." A lens defocuses the SURFACE, so this widens the isoline's own
    // transition rather than blurring the frame: the coverage AA band, the
    // film-edge band and the rim/meniscus widths all grow by the same blur
    // radius. The grain, which is added after everything, stays sharp -- which
    // is exactly right: the grain is in the camera, the blur is in the lens.
    // laP21.z is the radius in uv-y (authored in px at 1440p), and the field
    // units it takes here are that distance times the local |grad|.
    float  soft = laP21.z;
    // ---- MINIMUM BAND WIDTHS (band_min) ----------------------------------
    // The user, photographing a big mass beside a medium droplet: "what
    // happened that the bubbles got a solid outline -- whatever shading is in
    // this photo needs to be everywhere." Every band below (film edge, dark
    // rim, meniscus, halo, penumbra) is a fraction of the LENS radius, which
    // is right for a big mass and wrong for a small droplet: the same shading
    // squeezed into a sub-pixel line is a hard outline, not a soft glow. Each
    // one now has a floor authored in px at 1440p and carried as a fraction
    // of the frame, so a 3-px droplet is shaded like a 300-px mass and the
    // preview and the panel agree. band_min = 0 restores the old widths.
    const float PX1440 = 1.0 / 1440.0;
    float  bmin = saturate(laP21.w);
    float  aaF  = fwidth(field) * laP0.w + 1e-4;
    if (soft > 1e-6) aaF += soft * gl;
    float  cov  = smoothstep(thresh - aaF, thresh + aaF, field);
    // ---- local lens radius and film thickness (oil_thin_edge) -----------
    // The Wyvill kernel gives the blob radius for free: at the isoline
    // |grad| ~= 1.72/S while the visible radius is 0.454*S, so R ~= 0.78/|grad|.
    // That is what makes the soft edge a fixed FRACTION of each disc instead
    // of a fixed number of pixels. Deep inside a merged mass |grad| -> 0, so
    // the band is clamped; there the sdf clamp (0.25) saturates thk to 1.
    // Clamped: on a broad merged mass |grad| collapses and an unclamped R
    // blew the halo up into frame-sized pale lobes. The largest authored
    // blob radius is ~0.4 p-units, so that is the ceiling.
    // ---- "is this pixel really ON the isoline?" (isoOk) ------------------
    // sdf = (field - thresh) / |grad| is a true signed distance only where the
    // field behaves like a plane: there it changes by exactly one pixel per
    // pixel, so |grad(sdf)| == 1. Around a blob or a droplet whose field dips
    // TOWARD the threshold without crossing it, sdf collapses to nearly 0 and
    // springs back within a couple of pixels, and its own gradient is many
    // times too steep. That is exactly where the dark rim band painted a
    // hollow RING with oil still inside it, and where the halo painted a soft
    // plume with no surface under it (the user photographed both). One number
    // off two derivatives the quad already has, and it is ~1 on every real
    // edge, so a genuine rim keeps its authored width.
    float sdfWarp = length(float2(ddx(sdf), ddy(sdf))) / max(texelSize.y, 1e-7);
    // A true signed distance has |grad(sdf)| == 1 EXACTLY, so the band can sit
    // tight above 1; at 2.5..7.0 it only clipped the worst of each ring and
    // left the rest as a dotted circle.
    // A true signed distance has |grad(sdf)| == 1 EXACTLY, so the band starts
    // just above 1. It ENDS far out (6) on purpose: this is a blend weight,
    // and a narrow band put a hard bright seam around the plume it was meant
    // to clean up. Wide = gradual = invisible.
    float isoOk   = 1.0 - smoothstep(1.35, 6.00, sdfWarp);
    // ---- the lens radius that sets the soft band's width -----------------
    // A droplet's kernel is tiny and therefore very steep, so a droplet merely
    // PASSING THROUGH the wide band around a big hole spikes |grad| there and
    // makes the band width jump frame to frame -- the user's "individual dots"
    // flickering, "usually the blur around the dark spots".
    //
    // The blob-only gradient cannot simply be substituted: sdf still divides by
    // the FULL |grad|, and deep inside a merged mass the blob gradient goes to
    // zero, so lensR pinned to its ceiling, edgeW to 0.060, and the oil went
    // black (tried, reverted). Use it only where it is a credible surface
    // gradient -- i.e. near a blob isoline, which is exactly where the band
    // lives and exactly where droplets were modulating it. Deep inside, where
    // gradB is meaningless, fall back to the full gradient, which is what this
    // line always did. With the droplet sim off gradB == grad and both branches
    // are the same number.
    float  glB = length(gradB) + 1e-6;
    float  glLens = lerp(gl, glB, smoothstep(0.35, 0.90, glB));
    float  lensR = clamp(0.78 / max(glLens, 1e-3), 0.02, 0.35);
    // ---- oil_edge_mode ---------------------------------------------------
    // 0 (default, every existing ini): the film thins over a band proportional
    // to the LENS radius, so a big disc fades out over a wide translucent
    // gradient and a droplet over a couple of pixels.
    // 1 CRISP: every surface gets the droplets' treatment -- one narrow,
    // SIZE-INDEPENDENT band a few rim-widths across, so a big hole ends on the
    // same hard isoline a droplet does and carries only its meniscus. The film
    // BODY (oil_transparency, absorption, bump, refraction) is untouched in
    // both: this is the edge and nothing else.
    // The user, on stills of the two: "hard to say in stills, apply all" --
    // hence a key and both shipped, to be judged live from the tray.
    float  edgeW = clamp(max(laP13.y, 0.02) * lensR, laP1.x * 2.0, 0.060);
    if (laP18.w > 0.5) edgeW = max(laP1.x * 2.5, 1e-5);
    // ...and no film edge may be tighter than the lens's own blur circle.
    if (soft > 1e-6) edgeW = max(edgeW, soft * 2.0);
    // ...nor than the floor: a droplet's film has to thin over the same few
    // px a mass's does, or its edge is a cut.
    edgeW = max(edgeW, bmin * 3.0 * PX1440);
    float  thk   = 1.0;                       // 1 = full-thickness oil
    // oil_transparency needs the same thickness proxy even when the soft
    // thin edge itself is off: a transparent film MUST be clearest where it
    // is thinnest, or it reads as a sheet of tinted glass cut with scissors.
    if (laP13.x > 0.0005 || laP15.x > 0.0005) {
        thk = smoothstep(0.0, edgeW, sdf);
        // ---- oil_edge_curve --------------------------------------------
        // The user, on the panel: "it looks like it goes from green to black,
        // then stops; it should be more S-curved: distance vs mix of colours".
        // smoothstep is an S in the middle but its ENDS are only C1 -- the
        // ramp arrives at full thickness (and at black) with a visible knee.
        // smootherstep (6t^5-15t^4+10t^3) is flat to second order at both
        // ends, so both knees vanish. Same band, same width: a profile
        // change only, and the branch is the original line at 0.
        [branch] if (laP20.z > 0.0005) {
            float t = saturate(sdf / max(edgeW, 1e-7));
            thk = lerp(thk, t * t * t * (t * (t * 6.0 - 15.0) + 10.0),
                       saturate(laP20.z));
        }
        // ...but only as far as sdf can be believed. Where it cannot (isoOk
        // < 1: the field dips toward the threshold without crossing it, so
        // sdf collapses although the oil is thick) the thin-edge model would
        // otherwise conclude the film had thinned to nothing -- which drew a
        // hollow dark RING around every bubble that failed to punch through,
        // with oil still inside it. Fall back to the plain coverage, which
        // cannot lie: 1 in the oil, 0 on the ink.
        thk = lerp(cov, thk, isoOk);
    }
    // Refraction: the ink is seen through thinning oil near the rim, so shift
    // the ink lookup along the field gradient inside an edge band. Its width
    // is rim_width * refraction_width (default the shipped 7), widened toward
    // the soft-edge band so the ink is actually seen bending under the oil
    // instead of only inside a hairline.
    float  refrW = max(laP1.x * ((laP14.w > 0.0005) ? laP14.w : 7.0), 1e-4);
    if (laP13.x > 0.0005) refrW = lerp(refrW, max(refrW, edgeW), saturate(laP13.x));
    float  eb = sdf / refrW;
    float  edgeB = exp(-eb * eb);
    float2 rOff = (grad / gl) * (laP1.w * edgeB);
    // ---- body refraction (oil_refract_body) ------------------------------
    // The edge band above bends the ink only in a hairline around each disc.
    // A real film of oil has thickness EVERYWHERE, so what you see through
    // its middle is displaced too — and unevenly, because the surface is not
    // flat. Offset along the gradient of (field + a slow fbm bump): the field
    // part leans the whole disc's contents one way, the fbm part is what makes
    // the marbling wobble as it passes under. Capped to a unit vector, so the
    // key is the offset in uv units and can never run away.
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    if (laP15.w > 0.0005) {
        float2 bq = pp * 3.1 + float2(laP6.z * 0.006, -laP6.z * 0.0045);
        const float bh = 0.05;
        float  b0 = AcidFbm(bq);
        float2 bg = float2(AcidFbm(bq + float2(bh, 0.0)) - b0,
                           AcidFbm(bq + float2(0.0, bh)) - b0) / bh;
        float2 nd = (grad / gl) * 0.35 + bg * 0.85;
        nd /= max(1.0, length(nd));
        rOff += nd * (laP15.w * cov);
    }
    uv = saturate(uv + rOff * float2(1.0 / aspect, 1.0));
#endif
    float3 C = Dye.SampleLevel(linearClamp, uv, 0).rgb;
#ifdef LIQUID_ACID
    // ---- oil_ink_blur ----------------------------------------------------
    // The ink under a film of oil is slightly out of focus (refs 3/4/7): the
    // marbling reads through, but softened, and more so where the oil is
    // thick. Four diagonal dye taps, weighted by thickness * coverage, taken
    // BEFORE the ink pipeline so the bands, seams and water absorption all
    // inherit the defocus instead of being blurred after the fact.
    if (laP16.x > 0.0005) {
        float bw = saturate(laP16.x) * thk * cov;
        if (bw > 0.002) {
            float2 bo = texelSize * (2.0 + 11.0 * saturate(laP16.x));
            float3 b4 = 0.25 * (Dye.SampleLevel(linearClamp, uv + bo, 0).rgb
                              + Dye.SampleLevel(linearClamp, uv - bo, 0).rgb
                              + Dye.SampleLevel(linearClamp, uv + float2(bo.x, -bo.y), 0).rgb
                              + Dye.SampleLevel(linearClamp, uv - float2(bo.x, -bo.y), 0).rgb);
            C = lerp(C, b4, bw);
        }
    }
#endif
    // Raw dye intensity, before shading/filters/clamps: drives the HDR
    // highlight expansion below so it can see how hot the dye really is
    // (the clamped, filtered colour can't exceed 1.0 any more).
    float m = max(C.r, max(C.g, C.b));
#if defined(INK) || defined(LIQUID_ACID)
    const float3 C0 = C;    // raw dye: what the ink-in-water block absorbs
#endif
#ifdef INK
    // The ink look REPLACES the dye->colour step (and the emboss with it: an
    // absorbing medium has no pseudo-3D bevel, its depth cue is InkEdge).
    C = InkWater(C0, uv, texelSize, i.pos.xy, m);
#else
    if (shading > 0.5) {
        float3 L = Dye.SampleLevel(linearClamp, uv - float2(texelSize.x, 0), 0).rgb;
        float3 R = Dye.SampleLevel(linearClamp, uv + float2(texelSize.x, 0), 0).rgb;
        float3 T = Dye.SampleLevel(linearClamp, uv - float2(0, texelSize.y), 0).rgb;
        float3 B = Dye.SampleLevel(linearClamp, uv + float2(0, texelSize.y), 0).rgb;
        float dx = length(R) - length(L);
        float dy = length(B) - length(T);
        float3 n = normalize(float3(dx, dy, length(texelSize)));
        float diffuse = clamp(dot(n, float3(0, 0, 1)) + 0.7, 0.7, 1.0);
#ifdef LIQUID_ACID
        // The reference ink bands are FLAT. The fluid look's pseudo-3D emboss
        // puts a glossy bevel on every dye front, which reads as melted
        // plastic once the bands are posterised — attenuate it here instead of
        // making the user turn `shading` off (it belongs to the fluid look).
        C *= lerp(1.0, diffuse, saturate(laP7.w));
#else
        C *= diffuse;
#endif
    }
#endif
    // The reference composited the dye into an 8-bit canvas before any CSS
    // filter touched it, so dye above 1.0 (cap 1.35, bursts 1.5) was flattened
    // to 1.0 first. Then: canvas filter (HDR compensation + hue-rotate burst),
    // then Wallpaper Engine's right-panel adjust, each primitive clamped.
    C = saturate(C);
    if (fm0.w > 0.5) {
        C = saturate(CssSaturate(C, fm0.x));
        C = saturate(C * fm0.y);
        C = saturate(CssContrast(C, fm0.z));
    }
    if (abs(fm1.x) > 0.05) C = saturate(CssHueRotate(C, fm1.x));
    C = saturate(CssSaturate(C, fm1.y));
    C = saturate(C * fm1.z);
    C = saturate(CssContrast(C, fm1.w));
    if (abs(fm2.x) > 0.05) C = saturate(CssHueRotate(C, fm2.x));

    // Response curve: remap brightness through a Gaussian hump. Hue preserved
    // (all channels scaled together); pixels below ~0.2% forced to black so the
    // background stays truly dark instead of exploding through the divide.
    if (curve.x > 0.5) {
        float x = max(C.r, max(C.g, C.b));
        float d = (x - curve.y) / max(curve.z, 0.001);
        float hump = exp(-0.5 * d * d) * curve.w;
        float scale = (x > 0.002) ? (hump / x) : 0.0;
        C *= scale;
    }

    // Shadow floor: chroma-preserving lift, strongest at black, gone above
    // the knee. Scales the pixel's own colour up toward the floor (hue and
    // saturation intact) instead of adding neutral grey, so dark marbling
    // stays visible without washing the blacklight look. Pixels that are
    // exactly zero stay zero — there is no marbling to reveal in empty fluid.
    if (shadow.x > 0.0001) {
        float lum = max(C.r, max(C.g, C.b));
        float lift = shadow.x * (1.0 - smoothstep(0.0, max(shadow.y, 0.001), lum));
        C *= (lum + lift) / max(lum, 1e-4);
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
#ifdef LIQUID_ACID
    // =====================================================================
    // Liquid Acid: restyle the parity colour C as INK, then composite OIL.
    // =====================================================================
    float3 inkC;
    if (laP10.z > 0.5) {
        // ---- ink_mode=water: the SHARED ink-in-water block ---------------
        // Reads the RAW dye (C0), so it bypasses the parity CSS chain exactly
        // the way bands mode does at ink_mix ~= 1. The oil, rims, meniscus and
        // swarms below are unchanged and simply composite over this ink.
        float inkM;
        inkC = InkWater(C0, uv, texelSize, i.pos.xy, inkM);
        m = inkM;
    } else {
    // ---- INK: flat colour bands, dark seams, duotone ramp ----------------
    float inkLum = max(C.r, max(C.g, C.b));
    float bandIn = saturate(inkLum * laP4.x + laP4.y);
    float band   = AcidBand(bandIn, max(laP3.x, 1.0), clamp(laP3.y, 0.001, 0.5));
    // Keep the BOTTOM of the range continuous: posterising the fluid's low-dye
    // regions down to level 0 turns every faint wisp into a hard-edged black
    // cut-out. Below the first band edge, fade back to the smooth value.
    band = lerp(bandIn, band, smoothstep(0.0, 1.5 / max(laP3.x, 1.0), bandIn));
    // chroma-preserving quantise of the sim's own colour
    inkC = C * (band / max(inkLum, 1e-4));
    // 4-stop ramp indexed by the banded luminance
    float  rs = saturate(band) * 3.0;
    int    ri = (int)floor(rs);
    float3 rampC = lerp(laInk[min(ri, 3)].rgb, laInk[min(ri + 1, 3)].rgb, rs - ri);
    // Regional hue variation: rotate the ramp by the DYE's own hue so the ink
    // still drifts (purple <-> magenta, red <-> orange) instead of reading as
    // one flat duotone. 0 = pure duotone.
    if (laP3.w > 0.01) {
        float2 cc = float2(C.r - 0.5 * (C.g + C.b), 0.8660254 * (C.g - C.b));
        // atan2(0,0) is NaN, and dye-free pixels are exactly (0,0). The NaN
        // propagates through lerp() and blacks the pixel out even where the
        // OIL covers it - that is what painted hard, fluid-shaped black smears
        // across the oil discs. Guard on the chroma magnitude.
        if (dot(cc, cc) > 1e-10)
            rampC = AcidHueShift(rampC, atan2(cc.y, cc.x) * laP3.w * 0.31831);
    }
    // Complement lock: clamp the ramp's hue into a window centred on the oil's
    // opposite. Applied AFTER the regional variation, so the ink still drifts
    // — it just drifts inside the complementary window instead of wandering
    // toward the oil hue. Near-greys have no hue to clamp, so skip them.
    if (laP11.x > 0.5) {
        float3 rhsv = AcidRgb2Hsv(rampC);
        if (rhsv.y > 0.04) {                             // near-greys have no hue
            float d = laP11.z - rhsv.x * 360.0;
            d = d - 360.0 * floor(d / 360.0 + 0.5);      // wrap to [-180, 180]
            float half = max(laP11.y, 0.0) * 0.5;
            if (abs(d) > half) rampC = AcidHueShift(rampC, d - sign(d) * half);
        }
    }
    inkC = lerp(inkC, rampC, saturate(laP3.z));
    // dark seams where |grad dye| is steep (the marbled acrylic-pour edging)
    {
        float2 st = texelSize * laP4.w;
        float gx = length(Dye.SampleLevel(linearClamp, uv + float2(st.x, 0), 0).rgb)
                 - length(Dye.SampleLevel(linearClamp, uv - float2(st.x, 0), 0).rgb);
        float gy = length(Dye.SampleLevel(linearClamp, uv + float2(0, st.y), 0).rgb)
                 - length(Dye.SampleLevel(linearClamp, uv - float2(0, st.y), 0).rgb);
        float gm = sqrt(gx * gx + gy * gy);
        // Only the steepest fronts get a seam, AND only where the tap actually
        // crosses more than one colour band. Without that band-index test a
        // seam lands between two ADJACENT levels of the same hue, which draws a
        // dark ring around every band and makes each one read as a lit ridge
        // (light-dark-light) — a glossy relief instead of the references' flat
        // bands. gm * inkGain * inkLevels is the number of bands the tap spans.
        float gBands = gm * laP4.x * max(laP3.x, 1.0);
        inkC *= 1.0 - laP4.z * smoothstep(laP5.x, max(laP5.y, laP5.x + 1e-3), gm)
                             * smoothstep(1.1, 2.1, gBands);
    }
    }   // end ink_mode == bands
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- MULTICOLOUR OIL (brief AE) --------------------------------------
    // A second dye hue living IN the film, not over it. The reference the
    // user kept is a magenta film with soft cyan patches that read like a
    // light leak -- but a leak is additive and would lift the black corners,
    // and in that reference the cyan sits UNDER the masses. Only a dye in the
    // film does both, so this shifts the film's own colour and everything
    // downstream (the thin-film edge, the penumbra, the droplets' fill)
    // inherits it for free. The masses stay black over it because they are
    // drawn from the ink ramp, which this never touches.
    //
    // The hue rotation is AcidHueShift, which holds saturation and value, so
    // a patch is exactly as vivid as the palette it came from -- the W3C
    // matrix would have landed the cyan on a pastel.
    float3 oilC = oilBase;
    [branch] if (laP30.x > 0.0005 || laP30.z > 0.0005) {
        // bilinear fetch of the 20x12 field
        const float MW = 20.0, MH = 12.0;
        float2 mf = float2(uv.x * MW - 0.5, uv.y * MH - 0.5);
        float2 mi = floor(mf), mt = mf - mi;
        mt = mt * mt * (3.0 - 2.0 * mt);          // smooth, so cells never show
        float m00, m10, m01, m11;
        {
            int x0 = (int)clamp(mi.x, 0.0, MW - 1.0), x1 = (int)clamp(mi.x + 1.0, 0.0, MW - 1.0);
            int y0 = (int)clamp(mi.y, 0.0, MH - 1.0), y1 = (int)clamp(mi.y + 1.0, 0.0, MH - 1.0);
            int i00 = y0 * 20 + x0, i10 = y0 * 20 + x1;
            int i01 = y1 * 20 + x0, i11 = y1 * 20 + x1;
            m00 = laMix[i00 >> 2][i00 & 3];  m10 = laMix[i10 >> 2][i10 & 3];
            m01 = laMix[i01 >> 2][i01 & 3];  m11 = laMix[i11 >> 2][i11 & 3];
        }
        float mixV = lerp(lerp(m00, m10, mt.x), lerp(m01, m11, mt.x), mt.y);
        // A soft threshold, not the raw field: the reference's patches have
        // an edge to them, and a linear ramp over the whole field is a tint.
        // The band is deliberately narrow and sits ABOVE the field's own
        // mean: in the reference the second colour is a MINORITY -- patches
        // in a film, not half the frame -- and a wide band spends most of the
        // picture in the intermediate hues, which reads as a rainbow gradient
        // rather than as two dyes meeting. 0.50..0.74 puts roughly a third of
        // the frame in the patch and keeps the magenta-to-cyan run short.
        // The band has to be NARROW, and the wider the rotation the narrower
        // it has to be. k2 sweeps the hue continuously from 0 to film_hue2,
        // so with a 180 deg contrast the transition passes through EVERY
        // intermediate hue -- at 0.50..0.74 that put a full rainbow across a
        // quarter of the frame and the picture stopped reading as two dyes
        // meeting. 0.56..0.68 keeps the rainbow as a thin rim, which is what
        // the reference actually shows at a patch edge.
        float k2 = smoothstep(0.56, 0.68, mixV) * saturate(laP30.x);
        // The droplets INSIDE a mass take their own share of it (the ref's
        // cyan-lit specks in the black). 1 = the same as the film.
        if (fieldB < thresh) k2 *= saturate(laP31.x);
        // ROTATE the hue by the patch's share of it -- do NOT cross-fade to
        // the rotated colour. lerp(magenta, cyan, 0.5) in RGB is GREY, and
        // that is exactly what the first render produced: pale lavender fog
        // instead of the reference's cyan. A dye shifts a hue; it does not
        // blend a colour with its own opposite. Rotating also keeps
        // saturation and value flat across the whole patch, so the film is as
        // vivid in the second colour as it is in the first.
        oilC = AcidHueShift(oilC, laP30.y * k2);
        // ...and an optional THIRD hue off the other end of the SAME field,
        // so a second colour costs no second field and no second fetch.
        [branch] if (laP30.z > 0.0005) {
            float k3 = (1.0 - smoothstep(0.18, 0.46, mixV)) * saturate(laP30.z);
            if (fieldB < thresh) k3 *= saturate(laP31.x);
            oilC = AcidHueShift(oilC, laP30.w * k3);
        }
    }
    // rise_bottom_light: a lava lamp is lit and heated from BELOW, so the wax
    // near the base is hotter and brighter and cools on the way up. One
    // vertical ramp on the oil (not on the ink: the glass is not lit, the wax
    // is), applied here so it also feeds the film's own scattered light.
    float lampG = 1.0;
    if (laP17.x > 0.0005) {
        lampG = lerp(1.0, 0.80 + 0.50 * smoothstep(0.0, 1.0, uv.y), saturate(laP17.x));
        oilC *= lampG;
    }
    oilC *= lerp(1.0, 0.93 + 0.14 * AcidFbm(pp * 7.0 + float2(laP6.z * 0.010,
                                                              -laP6.z * 0.007)),
                 saturate(laP2.w));
    // Thin film: the ink underneath modulates the oil, but only BROADLY. The
    // raw per-pixel band printed the dye's texel structure onto the oil as
    // short horizontal dashes, so drive it from a wide, heavily smoothed
    // luminance tap instead of the pixel's own band.
    if (laP2.z > 0.002) {
        float2 bt = texelSize * 9.0;
        float lw = length(Dye.SampleLevel(linearClamp, uv + bt, 0).rgb)
                 + length(Dye.SampleLevel(linearClamp, uv - bt, 0).rgb)
                 + length(Dye.SampleLevel(linearClamp, uv + float2(bt.x, -bt.y), 0).rgb)
                 + length(Dye.SampleLevel(linearClamp, uv - float2(bt.x, -bt.y), 0).rgb);
        oilC *= lerp(1.0, 0.88 + 0.30 * saturate(lw * 0.45 * laP4.x), saturate(laP2.z));
    }
    // ---- thin film: the oil is a LENS, not a cut-out (oil_thin_edge) -----
    // refs 4/5/6: there is NO stroked boundary. The film thins to nothing at
    // the edge, so the light reaching the eye there has crossed the ink AND a
    // sliver of oil: the colour slides toward the PRODUCT of the two (orange
    // over red ink goes red; white over teal goes teal) and loses level, over
    // a band a few percent of the lens's own radius. That soft thickness
    // gradient is the whole reason a real oil disc is not a sticker.
    float3 oilThinC = oilC;
    if (laP13.x > 0.0005) {
        // saturate(): the factor is capped at 1, so a thinning film can only
        // go DARKER and toward the ink's hue, never brighter and paler. Left
        // uncapped it lifted every edge over bright ink into a milky lobe.
        float3 prod = oilC * saturate(0.30 + 1.10 * inkC);
        float  tt   = 1.0 - thk;
        oilThinC = prod;
        oilC = lerp(oilC, prod, saturate(tt * tt * laP13.x));
    }
    // ---- TRANSPARENT COLOURED FILM (oil_transparency) --------------------
    // Until now the oil was a fill: whatever the ink did under a disc, the
    // disc hid it. A real coloured oil is an ABSORBING film, so the ink's
    // marbling reads through it, tinted. Beer-Lambert per channel:
    //   T = exp(-absorb * thickness * (1 - oilHueNormalised))
    // An orange film has oilN ~= (1, 0.31, 0.18), so 1-oilN ~= (0, .69, .82):
    // red passes untouched, blue is eaten. A near-black oil normalises to ~0
    // and absorbs every channel -- which is right, it is a neutral-density
    // film, and that is why the approved black-oil family survives this key.
    //
    // The dish is BACKLIT, so the light reaching the eye has come THROUGH the
    // ink: over black ink nothing is transmitted and the disc would go black.
    // The scatter term oilC * (1 - T_avg) is the light scattered inside the
    // film itself -- it is what keeps thick oil reading as its own colour over
    // dead-black ink, and it vanishes at the rim where T -> 1.
    //
    // Thickness = the same proxy the edge work uses (thk: 0 at the rim, 1 deep
    // inside), times a slow fbm so the film has ISLANDS of thick and thin
    // instead of being one even pane of glass. thk -> 0 at the edge means the
    // thin edge is automatically the clearest part of the film, which is
    // exactly what oil_thin_edge was approximating by hand.
    float filmOp = 1.0;    // how much of this pixel is the OIL's own light
    if (laP15.x > 0.0005) {
        float k = saturate(laP15.x);
        float fb = saturate(AcidFbm(pp * 2.2 + float2(laP6.z * 0.004,
                                                      -laP6.z * 0.003)) * 1.143);
        float thick = thk * lerp(1.0, 0.35 + 1.30 * fb, saturate(laP15.z));
        float  mx   = max(oilC.r, max(oilC.g, oilC.b));
        float3 oilN = (mx > 1e-4) ? saturate(oilC / mx) : float3(0.0, 0.0, 0.0);
        float3 T    = exp(-max(laP15.y, 0.0) * max(thick, 0.0) * (1.0 - oilN));
        float  Tav  = dot(T, float3(0.3333333, 0.3333333, 0.3333333));
        // inkC is already the REFRACTED (and, with oil_ink_blur, defocused)
        // ink at this pixel: the marbling under the film, seen through it.
        float3 glassC = inkC * T + oilC * (1.0 - Tav);
        oilC   = lerp(oilC, glassC, k);
        filmOp = lerp(1.0, saturate(1.0 - Tav), k);
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- surface relief: specular (oil_specular) + thin-film iridescence --
    // Kept deliberately weak: the references show almost no specular, because
    // the rig is BACKLIT. What little there is comes from the lens curvature
    // near the edge plus a slow thickness ripple, never a pin-point.
    float specAmt = 0.0;
    if (laP13.z > 0.0005 || laP13.w > 0.0005) {
        float2 q  = pp * 4.3 + float2(laP6.z * 0.006, -laP6.z * 0.0045);
        float  f0 = AcidFbm(q);
        if (laP13.z > 0.0005) {
            const float he = 0.035;
            float2 bg = float2(AcidFbm(q + float2(he, 0.0)) - f0,
                               AcidFbm(q + float2(0.0, he)) - f0) / he;
            // dome normal: tilts OUTWARD (-grad) and hardest where the film is
            // thin, with the fbm adding a low relief across the flat middle.
            float2 nxy = (-grad / gl) * (1.0 - thk) * 0.85 - bg * 0.06;
            float3 nS  = normalize(float3(nxy, 1.0));
            float3 Lv  = float3(-0.406, -0.406, 0.819);   // top-left, ~55 deg up
            float3 Hv  = normalize(Lv + float3(0.0, 0.0, 1.0));
            float  sp  = pow(saturate(dot(nS, Hv)), 18.0);   // BROAD lobe
            float  fr  = pow(saturate(1.0 - nS.z), 3.0);     // Fresnel edge
            specAmt = (sp * 0.30 + fr * 0.12) * saturate(laP13.z);
            oilC += specAmt;
        }
        if (laP13.w > 0.0005) {
            float3 ir = 0.5 + 0.5 * cos(6.2831853 * (f0 * 3.0 + float3(0.0, 0.33, 0.67)));
            float  iw = saturate(laP13.w) * lerp(0.30, 1.0, 1.0 - thk) * 0.45;
            oilC *= lerp(1.0, ir * 1.5, iw);
        }
    }
    // ---- rim variation (rim_vary / rim_ink_follow) -----------------------
    // The reference rim is NOT a uniform stroke: it thickens and brightens
    // where the ink under it is bright, and thins to nothing along other
    // stretches. Two multipliers, both EXACTLY 1.0 when the keys are 0, so
    // every shipped ini renders unchanged.
    float rimMul = 1.0, haloMul = 1.0;
    if (laP12.x > 0.0005) {
        // low-frequency noise of POSITION (features ~1/3 of the frame) with a
        // slow drift, contrast-stretched so it really reaches 0 and 1
        float rn = AcidFbm(pp * 3.3 + float2(laP6.z * 0.011, -laP6.z * 0.008)) * 1.143;
        rn = saturate((rn - 0.5) * 1.9 + 0.5);
        float rv = saturate(laP12.x);
        rimMul  = lerp(1.0, 0.40 + 1.50 * rn, rv);                  // 0.4x..1.9x width
        haloMul = lerp(1.0, smoothstep(0.18, 0.72, rn) * 1.55, rv); // dies / thickens
    }
    if (laP12.y > 0.0005) {
        // The halo IS refracted ink, so it can be no brighter than the ink
        // just OUTSIDE the edge: one dye tap a few halo-widths along -grad
        // (the outward normal), so it doesn't collapse under the oil itself.
        float2 uvO = saturate(uv - (grad / gl) * (max(laP2.y, laP1.x) * 8.0)
                                  * float2(1.0 / aspect, 1.0));
        float3 dO  = Dye.SampleLevel(linearClamp, uvO, 0).rgb;
        float  il  = saturate(max(dO.r, max(dO.g, dO.b)) * max(laP4.x, 1.0));
        // Brightens where the ink outside is bright, and falls toward a floor
        // -- not to nothing -- over clear water, because the halo is also the
        // oil edge's own refraction (ink_mode=water is mostly clear water).
        haloMul *= lerp(1.0, 0.55 + 0.90 * smoothstep(0.03, 0.45, il), saturate(laP12.y));
    }
    // ---- paired-annulus ordering (rim_order) -----------------------------
    // laP12.z = 0: the shipped placement — dark band centred at rim_inset,
    // bright halo centred at -meniscus_offset, wherever the ini put them.
    // laP12.z = 1: force the physical pairing (Micromachines 13(7):1021) —
    // the dark band one half-width INSIDE the isoline and the bright caustic
    // one half-width OUTSIDE it, so they are adjacent and never overlap.
    // ---- EMERGENT boundary (meniscus_from_ink) ---------------------------
    // The reference boundary is not a stroke of some chosen colour: it is the
    // INK, concentrated by the meniscus. Where the ink just outside is bright
    // it makes a WIDE, soft, grainy halo of that ink's own hue (ref 7's cyan)
    // with a dark hairline against it; where the ink is dark (refs 4/5/6, and
    // every mono-ink ini) both simply vanish and the oil softens into the
    // dark. One gate does both, so a foreign cyan line can never appear on
    // black ink again. laMen (meniscus_color) survives only as the legacy
    // fallback at meniscus_from_ink = 0.
    float3 menC   = laMen.rgb;
    float  menHWx = 1.0;      // halo half-width multiplier
    float  haloInk = 1.0;     // ink-brightness gate on halo AND hairline
    if (laP14.y > 0.0005) {
        float  k  = saturate(laP14.y);
        // The gate reads the ink OUTSIDE the isoline, not the ink under the
        // oil: a bright dye patch beneath a disc must not summon a halo, and
        // a hole punched into clear water must not get one either.
        float2 uvO = saturate(uv - (grad / gl) * (0.06 * lensR + laP2.y * 2.0)
                                  * float2(1.0 / aspect, 1.0));
        float3 dO  = Dye.SampleLevel(linearClamp, uvO, 0).rgb;
        float  ilo = saturate(max(dO.r, max(dO.g, dO.b)) * max(laP4.x, 1.0));
        haloInk    = lerp(1.0, smoothstep(0.03, 0.40, ilo), k);
        float3 hs = AcidRgb2Hsv(inkC);     // lift: more saturated, brighter
        hs.y = saturate(hs.y * 1.35);
        hs.z = saturate(hs.z * 1.55 + 0.05);
        menC   = lerp(menC, AcidHsv2Rgb(hs), k);
        // a few percent of the LOCAL lens radius, never a 1-2 px line
        // ...but in CRISP mode the halo keeps its authored width: scaling it by
        // the local lens radius is what made a big hole's halo wide, and a
        // crisp edge wants the same thin meniscus at every size.
        menHWx = (laP18.w > 0.5) ? 1.0
               : lerp(1.0, max(1.0, (0.055 * lensR) / max(laP2.y, 1e-5)), k);
    }
    // ...both floored in px (band_min): the meniscus scaled by the lens
    // radius is exactly the "solid outline" the user photographed on a
    // medium droplet, and the dark rim is its twin.
    float rimHW = max(laP1.x * rimMul, bmin * 1.5 * PX1440);
    float menHW = clamp(laP2.y * rimMul * menHWx, bmin * 3.5 * PX1440, 0.024);
    // A defocused hairline is a wider, fainter hairline: widths add in
    // quadrature, the way a Gaussian convolves with a Gaussian.
    if (soft > 1e-6) {
        rimHW = sqrt(rimHW * rimHW + soft * soft);
        menHW = sqrt(menHW * menHW + soft * soft);
    }
    float rimCtr = laP12.z > 0.5 ?  rimHW : laP1.y;
    float menCtr = laP12.z > 0.5 ? -menHW : -laP7.z;
    if (laP14.y > 0.0005) menCtr = lerp(menCtr, -menHW * 0.55, saturate(laP14.y));
    // dark rim: a Gaussian band centred just INSIDE the boundary, forced to
    // zero deep inside a merged mass so no concentric rings appear there.
    float rimX = (sdf - rimCtr) / rimHW;
    float rimB = exp(-rimX * rimX)
               * (1.0 - smoothstep(thresh * 1.7, thresh * 3.4, field))
    // ...and the SAME |grad| gate the bright halo already carries. sdf =
    // (field - thresh) / |grad| is only a distance where the gradient is
    // strong; between two surfaces that are nearly merged (or nearly necked
    // apart) both the deficit and the gradient collapse, and the quotient
    // still lands inside the rim band -- painting a thin dark ARC along a
    // saddle with no isoline under it. That is the curved hairline the user
    // photographed below a big hole. The halo was gated for exactly this
    // reason and the dark twin never was.
               * smoothstep(0.5, 1.5, gl) * isoOk;
    // The hairline lives only where the halo does, and the soft thickness
    // edge has already taken over most of its job.
    float rimK = saturate(laP1.z) * haloInk * (1.0 - 0.65 * saturate(laP13.x));
    oilC *= 1.0 - rimK * rimB;

)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // Film alpha: with oil_thin_edge the disc fades out over the thickness
    // band instead of over 1-2 px of coverage AA. Squared, because a lens
    // thins fast near its edge.
    float alpha = cov;
    if (laP13.x > 0.0005) {
        float aS = smoothstep(-edgeW * 0.35, edgeW, sdf);
        // the same S as the thickness ramp, or the disc's opacity would
        // arrive at the ink with the knee the colour no longer has.
        [branch] if (laP20.z > 0.0005) {
            float t = saturate((sdf + edgeW * 0.35) / max(edgeW * 1.35, 1e-7));
            aS = lerp(aS, t * t * t * (t * (t * 6.0 - 15.0) + 10.0),
                      saturate(laP20.z));
        }
        alpha = lerp(cov, aS * aS, saturate(laP13.x));
    }
    float3 col = lerp(inkC, oilC, alpha);
    // ---- BACKLIGHT PENUMBRA (oil_penumbra) -------------------------------
    // The lamp is under the middle of the dish: the oil right next to a black
    // mass receives less of it than the open sheet does, and a pigment lit
    // less shifts hue as well as level. A smootherstep band on the OIL side
    // of the isoline only -- it can never touch the black, because it is
    // multiplied by the coverage it sits on.
    [branch] if (laP22.x > 0.0005) {
        float u = saturate(sdf / max(max(laP22.y, bmin * 4.0 * PX1440), 1e-5));
        float b = 1.0 - u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
        float k = saturate(laP22.x) * b * cov * isoOk;
        float3 lit = CssHueRotate(col, laP22.z) * (1.0 - saturate(laP22.w));
        col = lerp(col, lit, k);
    }
    // ---- BRIGHT-FIELD HALO ([post] halo / halo_px) -----------------------
    // The microscope look the user is after: every dark shape carries a soft
    // bright glow hugging its outside, with a faint darker echo further out --
    // the "double contour" of a phase-contrast image. "Very weak, but quite
    // wide", so this is a low-amplitude lift over a band many pixels across
    // with a smooth falloff, never a stroked line.
    //
    // Which side is the bright one is decided per pixel from the two colours
    // that meet here, so the halo always sits OUTSIDE the darker of the two:
    // over black ink it glows on the oil side of a hole, and it would flip by
    // itself under a dark oil on a bright ink.
    // Gated by the same |grad| and isoOk tests the meniscus carries, or a
    // shallow plateau that never crosses the threshold would grow a halo with
    // no surface under it.
    [branch] if (laP21.x > 0.0005) {
        float hw = max(laP21.y, 1e-5);
        // a bigger shape carries a slightly wider halo, as a lens does...
        hw *= lerp(1.0, clamp(lensR / 0.12, 0.6, 2.0), 0.5);
        // ...but never below the floor, and never wider than the frame can
        // read as a glow rather than a wash (band_min).
        hw = clamp(hw, bmin * 6.0 * PX1440, 24.0 * PX1440);
        float lo = dot(oilC, float3(0.2126, 0.7152, 0.0722));
        float li = dot(inkC, float3(0.2126, 0.7152, 0.0722));
        float sgnB = (lo >= li) ? 1.0 : -1.0;       // +1: the oil is the bright side
        float u  = (sdf * sgnB) / hw;               // distance into the BRIGHT side
        float br = exp(-u * u);                     // the glow, hugging the edge
        float ec = exp(-(u - 2.6) * (u - 2.6));     // the faint echo beyond it
        float k  = saturate(laP21.x) * smoothstep(0.5, 1.5, gl) * isoOk;
        float3 bright = (sgnB > 0.0) ? oilC : inkC;
        col += lerp(bright, float3(1.0, 1.0, 1.0), 0.35) * (k * br * 0.60);
        col *= 1.0 - k * ec * 0.35;
    }
    // ---- MASS RIM (mass_rim, brief AB) -----------------------------------
    // The user's lava-lamp photos: the wax mass is TRANSLUCENT and its edge is
    // a refracting boundary with a bright rim on the lit side (refs 5 and 7),
    // never a flat black cut-out. So a thin bright band just OUTSIDE the
    // isoline -- on the mass side, sdf < 0 -- and only where that edge faces
    // the rig's LAMP: the surface normal here is -grad(field), and its dot
    // with the lamp direction is how much of the lamp the boundary catches.
    // The far side of the same mass gets nothing, which is what stops this
    // reading as an outline and makes it read as a lit body.
    [branch] if (laP29.x > 0.0005) {
        float  rw  = max(laP29.y, bmin * 1.5 * PX1440);
        float  u2  = sdf / rw;                       // < 0 on the mass side
        float  band = exp(-(u2 + 1.15) * (u2 + 1.15) * 1.6);
        // which way this piece of edge faces, and how much lamp it catches
        float2 nrm = -grad / max(gl, 1e-5);
        float  lit = saturate(dot(nrm, lampD) * 0.5 + 0.5);
        lit = lit * lit;
        // Peak channel, never luminance: the film is a saturated magenta
        // whose luminance is a third of its red.
        float  src = max(oilC.r, max(oilC.g, oilC.b));
        float3 tint = lerp(oilC / max(src, 1e-4), float3(1.0, 1.0, 1.0), 0.45);
        col += tint * (saturate(laP29.x) * band * lit * 0.28
                       * smoothstep(0.5, 1.5, gl) * isoOk * src);
    }
    // ---- DROPLET LENS SHADING (droplet_lens, item X) ---------------------
    // The user, holding the oil-and-water reference beside our live frame: "I
    // was thinking of the oil boundary layer -- some mechanics of how it
    // should be gradual -- and it translated to it just being blurry. The
    // ratio of blurry to focused is off." In the reference every droplet down
    // to four pixels is CRISP at its edge and GRADUAL inside it. Ours were
    // soft-edged flat fills, which is the opposite trade.
    //
    // So: nothing here blurs anything. Four terms, all of them read off the
    // signed distance and the surface gradient the pass already has --
    //   (a) a radial interior gradient: a droplet is a plano-convex LENS over
    //       the backlight, so its middle is lighter than its shoulder;
    //   (b) a dark band just inside the boundary, on the oil side;
    //   (c) a thin BRIGHT refractive rim hugging the boundary -- the
    //       meniscus the reference shows on every droplet, crisp and the same
    //       few px wide at every size, as against the existing [post] halo,
    //       which is deliberately weak and 8-15 px wide;
    //   (d) a small specular, offset toward the RIG's lamp, so it swings when
    //       the lamp does instead of being painted on.
    //
    // All of it gated by smoothstep(gl) * isoOk, which is the cheap test for
    // "is there a CURVED surface here": a droplet's kernel stays steep right
    // through its middle, while deep inside a big merged mass the gradient
    // collapses. That is what keeps the film flat, keeps the OLED's black
    // black in the middle of a mass, and gives the lens treatment to the
    // things that are actually lenses.
    [branch] if (laP27.x > 0.0005) {
        float k0  = saturate(laP27.x);
        float lg  = smoothstep(0.35, 1.20, gl) * isoOk;
        float sIn = (sdf >= 0.0) ? 1.0 : -1.0;     // +1 in the oil, -1 in a hole
        float din = abs(sdf);                      // distance INTO whichever body
        // This lens's own radius, off the FULL gradient: lensR above is
        // deliberately blob-biased and floored at 0.02 p-units (~29 px), which
        // is right for the film's bands and useless for a 4-px droplet.
        float lensRD = clamp(0.78 / max(gl, 1e-3), 0.0012, 0.35);
        float u   = saturate(din / max(lensRD * 0.72, 1e-6));
        // (a) the interior: smootherstep so there is no knee at the rim and
        // none at the middle -- gradual is the whole point of the term.
        [branch] if (laP27.y > 0.0005) {
            float dome = u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
            // a hole is a lens too: its middle passes more of the film's own
            // light than its shoulder does, so it lifts toward the film
            // colour. An oil droplet lifts toward the backlight instead.
            float3 tgt = (sIn > 0.0) ? lerp(col, float3(1.0, 1.0, 1.0), 0.55) : oilC;
            col = lerp(col, tgt, k0 * saturate(laP27.y) * dome * lg * 0.55);
        }
        float bw = max(laP27.z, bmin * 1.5 * PX1440);
        // (b) the dark band, on the OIL side only: inside a hole it would be
        // black on black, and this way the same term shades an oil droplet's
        // shoulder and the film's own lip at the edge of a hole.
        float xb  = (sdf - bw * 1.15) / bw;
        float bnd = exp(-xb * xb) * step(0.0, sdf);
        col *= 1.0 - k0 * 0.30 * bnd * lg;
        // (c) the bright refractive rim, hugging the boundary just outside
        // the dark band. Authored in px and floored, never scaled by the
        // element's size, which is what makes a 4-px droplet carry the same
        // crisp rim a mass does instead of a sub-pixel smear.
        float xm  = (sdf + bw * 0.55) / (bw * 0.85);
        float men = exp(-xm * xm);
        col += lerp(oilC, float3(1.0, 1.0, 1.0), 0.45) * (k0 * 0.30 * men * lg);
        // (d) the specular. nOut is the outward direction of whichever body
        // this pixel is inside, so one expression lights a droplet and a hole
        // alike, and both turn to face the lamp when the rig moves it.
        [branch] if (laP27.w > 0.0005) {
            float2 Lv = float2(laP28.x * aspect, laP28.y) - pp;
            float2 Ld = Lv / max(length(Lv), 1e-6);
            float2 nO = (-sIn / gl) * grad;
            float  f  = saturate(dot(nO, Ld));
            float  f2 = f * f; f2 = f2 * f2;              // ^4: a small hotspot
            float  w  = (u - 0.58) / 0.30;
            float  sp = f2 * exp(-w * w);
            col += lerp(oilC, float3(1.0, 1.0, 1.0), 0.80)
                 * (k0 * saturate(laP27.w) * sp * lg * 0.50);
        }
    }
    // ---- outside glow (oil_glow): the lens spills a little of its own
    // colour into the ink around it -- diffuse, never a line (refs 4/5).
    if (laP14.z > 0.0005) {
        float dOut = max(-sdf, 0.0) / max(edgeW * 1.3, 1e-5);
        float go   = exp(-dOut * dOut) * (1.0 - alpha) * smoothstep(0.5, 1.5, gl);
        col += oilThinC * (go * 0.45 * saturate(laP14.z));
    }

)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- bubble swarms ---------------------------------------------------
    // Two procedural cellular layers, each masked to one side of the oil
    // surface: dark water droplets trapped INSIDE the oil (ref 2, ref 3) and
    // oil droplets floating on the open ink (ref 3). Both creep and warp so
    // they are not glued to the screen.
    {
        float st = laP6.z;
        float2 warp = float2(sin(pp.y * 4.1 + st * 0.11), cos(pp.x * 3.7 - st * 0.09));
        // DENSITY vs OPACITY: "denser here, sparser there" belongs in the
        // swarm's CELL DENSITY (and therefore inside AcidSwarm, decided per
        // cell), never in the composite alpha — fading the alpha tints every
        // droplet with the layer under it, which is why holes once came out
        // dark orange instead of ink-coloured. It must not be a per-PIXEL term
        // either, or the presence test flips part-way across a droplet and
        // slices a wedge out of it.
        // AA: fwidth() of a cellular min() jumps wherever the nearest cell
        // changes, which smeared droplets into short streaks along the cell
        // seams. p-space y spans 0..1 over the frame height, so one pixel is
        // texelSize.y — use that fixed width instead.
        float aaS = texelSize.y * 1.2;
        // swarm_lens: a trapped droplet is a HOLE IN THE FILM, not a punched
        // disc. The oil thins into it, so it gets the same soft edge and the
        // same thin-oil colour fringe as the outer boundary (ref 6), a
        // softened dark ring, and one small lens highlight offset toward the
        // light instead of a flat black interior.
        float lensK = saturate(laP14.x);
        if (laP8.x > 0.002) {
            float2 sp = pp + warp * 0.030 + st * float2(0.0040, -0.0030);
            // two octaves 2.7x apart so the sizes run from ~1 px to ~2% of the
            // frame height; the fine one stays sparse or the oil reads as dust
            float2 sn1, sn2; float sr1, sr2;
            float  sda = AcidSwarm(sp, laP9.x, laP8.z, laP10.x, laP9.z, laP9.w, sn1, sr1);
            float  sdb = AcidSwarm(sp + 7.31, laP9.x * 2.7, laP8.z * 0.12, laP10.x,
                                   laP9.z, laP9.w, sn2, sr2);
            bool   wa = (sda <= sdb);
            float  sd = wa ? sda : sdb;
            float2 sn = wa ? sn1 : sn2;
            float  sr = max(wa ? sr1 : sr2, 1e-4);
            float  sw   = lerp(aaS, max(sr * 0.26, aaS), lensK);
            float  scov = 1.0 - smoothstep(-sw, sw, sd);
            float  sx = sd / max(lerp(aaS * 2.4, sw * 1.15, lensK), 1e-6);
            float  srim = exp(-sx * sx);
            float  mask = smoothstep(0.004, 0.030, sdf) * laP8.x;   // inside the oil only
            if (lensK > 0.0005) {   // thin-oil fringe just OUTSIDE the droplet
                float fx = max(sd, 0.0) / max(sw * 1.8, 1e-5);
                col = lerp(col, oilThinC,
                           exp(-fx * fx) * (1.0 - scov) * mask * lensK * 0.75);
            }
            // a trapped water droplet shows the INK through the oil film
            col = lerp(col, inkC * (1.0 - laP10.y * (1.0 - 0.40 * lensK)), scov * mask);
            col *= 1.0 - laP8.w * (1.0 - 0.55 * lensK) * srim * mask;
            if (lensK > 0.0005) {
                float2 vc = sn * (sd + sr);
                float  hd = length(vc - float2(-0.707, -0.707) * (0.42 * sr))
                          / max(sr * 0.30, 1e-5);
                col += oilC * (exp(-hd * hd) * scov * mask * lensK * 0.30);
            }
        }
        if (laP8.y > 0.002) {
            float2 sp = pp * 1.31 - warp * 0.024 + st * float2(-0.0031, 0.0042) + 41.7;
            float2 sn1, sn2; float sr1, sr2;
            float  sda = AcidSwarm(sp, laP9.y, laP8.z, laP10.x, laP9.z, laP9.w, sn1, sr1);
            float  sdb = AcidSwarm(sp + 3.17, laP9.y * 2.7, laP8.z * 0.12, laP10.x,
                                   laP9.z, laP9.w, sn2, sr2);
            bool   wa = (sda <= sdb);
            float  sd = wa ? sda : sdb;
            float2 sn = wa ? sn1 : sn2;
            float  sr = max(wa ? sr1 : sr2, 1e-4);
            float  sw   = lerp(aaS, max(sr * 0.26, aaS), lensK);
            float  scov = 1.0 - smoothstep(-sw, sw, sd);
            float  sx = sd / max(lerp(aaS * 2.4, sw * 1.15, lensK), 1e-6);
            float  srim = exp(-sx * sx);
            float  mask = smoothstep(0.004, 0.030, -sdf) * laP8.y;  // open ink only
            col = lerp(col, laOil[3].rgb, scov * mask);
            col *= 1.0 - laP8.w * (1.0 - 0.55 * lensK) * srim * mask;
            if (lensK > 0.0005) {
                // an oil droplet on open ink is a lens too: a faint caustic
                // just OUTSIDE it and one small highlight inside.
                float fx = max(sd, 0.0) / max(sw * 1.6, 1e-5);
                col += laOil[3].rgb * (exp(-fx * fx) * (1.0 - scov) * mask * lensK * 0.22);
                float2 vc = sn * (sd + sr);
                float  hd = length(vc - float2(-0.707, -0.707) * (0.42 * sr))
                          / max(sr * 0.30, 1e-5);
                col += exp(-hd * hd) * scov * mask * lensK * 0.15;
            }
        }
    }

    // ---- meniscus: thin BRIGHT ink-coloured halo just outside the rim ----
    // In the references this is the brightest thing in the frame (cyan on
    // ref 1) and it is what separates the oil from the ink. Painted with the
    // ramp's bright stop so it reads even over black ink.
    float haloW = 0.0;
    if (laP2.x * haloMul * haloInk > 0.002) {
        float hx = (sdf - menCtr) / menHW;
        float halo = exp(-hx * hx);
        // sdf = (field - thresh) / |grad| is only a distance where |grad| is
        // strong. On a broad low-gradient plateau that never reaches the
        // threshold, a tiny field deficit divided by a tiny gradient still
        // lands inside the halo band, painting a fuzzy blob-shaped glow with
        // no oil under it. Real blob surfaces have |grad| >~ 2 (it scales as
        // 1/radius, and the largest discs here are ~0.4), so gate on it.
        halo *= smoothstep(0.5, 1.5, gl) * isoOk;
        haloW = saturate(halo * laP2.x * haloMul * haloInk);
        col = lerp(col, menC, haloW);
    }

    // ---- interface speckle: sparse cellular dots hugging the boundary ----
    if (laP6.x > 0.001) {
        float2 cell = floor(pp * laP6.y);
        float  rnd  = AcidHash21(cell);
        float2 jit  = float2(AcidHash21(cell + 7.13), AcidHash21(cell + 13.71));
        float  dd   = length(frac(pp * laP6.y) - (0.25 + 0.5 * jit));
        float  dot1 = step(dd, 0.08 + 0.26 * rnd) * step(0.88, rnd);
        float  sMask = saturate(smoothstep(-0.020, 0.0, sdf) * (1.0 - cov) * 1.2
                              + (1.0 - smoothstep(laP1.x * 2.0, laP1.x * 7.0, sdf)) * cov * 0.30);
        col = lerp(col, col * 0.18, dot1 * sMask * laP6.x);
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- MACRO "CELLULOSE" SURFACE TEXTURE (cellulose) -------------------
    // The user, on the Requiem microscope frame: the cell body is fibrous and
    // mottled, "macro-ish cellulose noise", and they want it "in the black oil
    // more than the oil". Two cheap fBms:
    //   * an ANISOTROPIC one -- the sample point is rotated into a slowly
    //     turning frame and squashed 5:1 along it, so the same noise reads as
    //     STRANDS instead of blobs -- run through 1-|2f-1| so the ridges
    //     become filaments rather than lumps;
    //   * a slow isotropic one three times coarser, for the mottling that
    //     makes the strands come in patches.
    // Both drift with the rise (laP23.w = rise_speed * cellulose_drift), so
    // the texture belongs to the masses and not to the screen, and this sits
    // in the DISPLAY pass -- under the film grain, because the fibres are the
    // surface and the grain is the camera.
    //
    // On the BLACK side it is a THICKNESS effect, never a fill: the strands
    // are strongest where the black layer is thin (just inside its edge) and
    // fall off to nothing deep inside a mass, so the OLED's true black is
    // still true black over most of the frame. On the film it is a faint
    // multiplicative mottle of the colour, which cannot lift anything.
    [branch] if (laP23.x > 0.0005 || laP23.y > 0.0005) {
        float  csc = max(laP23.z, 1e-4);
        float2 q0  = pp + float2(0.0, laP23.w * laP6.z);
        float  ang = 0.9 * AcidVNoise(q0 * 0.7) + laP6.z * 0.010;
        float2 ca  = float2(cos(ang), sin(ang));
        float2 qr  = float2(dot(q0, ca), dot(q0, float2(-ca.y, ca.x))) / csc;
        qr.x *= 0.20;
        float  f   = saturate(AcidFbm(qr) * 1.143);
        float  str = 1.0 - abs(2.0 * f - 1.0);
        float  mot = saturate(AcidFbm(q0 / (csc * 3.5)) * 1.143);
        float  n   = (str - 0.5) * 0.75 + (mot - 0.5) * 0.45;
        // Thickness falloff into the black, Beer-Lambert rather than
        // Gaussian: a long tail is what makes the strands fade OUT of a mass
        // instead of ending on a rim of their own. The amplitudes below are
        // in the display's sRGB-ENCODED domain, where a lift of 0.08 over
        // black is ~1.5 nits at SDR white 240 -- "a few nits at most", and
        // the negative half of the noise is simply clamped away by the black.
        float  dIn = max(-sdf, 0.0);
        float  bw  = max(csc * 3.0, 24.0 * PX1440);
        float  th  = exp(-dIn / bw);
        // isoOk: on a plateau that never crosses the threshold sdf collapses
        // to nearly zero over a wide area, and without this gate the falloff
        // would print a mass-shaped grey cloud with no edge under it.
        col += n * (0.28 * laP23.x * (1.0 - alpha) * th * isoOk);
        col *= 1.0 + n * (0.45 * laP23.y * alpha);
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- CAST SHADOWS (shadow_amt / shadow_len / shadow_soft / light_z) --
    // The user, brief BC: "idk if this is a feature, volumetric lighting, or
    // some implementation of it. The light should cast shadows essentially...
    // it can be from behind, or the bottom, or the top or side." Until now the
    // lamp drove the specular, the mass rim, the penumbra, the haze and the
    // bloom, and nothing in the frame blocked a single photon.
    //
    // WHAT CASTS. In this look the black "masses" are the NEGATIVE space of
    // the blob field -- the film is the blobs -- so a mass shadow cannot be
    // written per caster the way a rim can. It is SAMPLED instead: four taps
    // from this pixel TOWARD the lamp, each re-evaluating the blob field
    // alone (no gradient, no colour, no comb -- a shadow is low frequency and
    // none of that would survive the softening). A tap that lands on the dark
    // side of the isoline is a tap where the light never got through. The
    // droplets are the other caster and they are done ANALYTICALLY, in a walk
    // of the same 3x3 cells the field uses, because a droplet's shadow is
    // short: a capsule from its own centre is exact, and cheaper than four
    // more taps per droplet.
    //
    // LIGHT_Z is the lamp's stand-off from the plane of the dish:
    //   > 0  in front of / above it -- the shadow rakes away from the lamp,
    //        and the higher the lamp stands the shorter it is;
    //   = 0  in the plane -- the longest shadows this key can make;
    //   < 0  behind the dish: BACKLIT. There is no direction left to rake
    //        toward, so the shadow spills evenly round every caster onto the
    //        film in front of it, which is what a body lit from behind does
    //        (and the caster's own edge keeps the mass_rim / meniscus glow).
    //
    // WHERE. It MULTIPLIES the composed film, here -- after every shading
    // term, before the grain and before the post pass -- so halation, fog and
    // bloom all see a darker source and a shadow reads as LESS LIGHT rather
    // than as a grey overlay dropped on the picture. Over the black it is a
    // no-op by construction: a multiplier cannot darken a zero.
    float shadowMul = 1.0;
    [branch] if (laP32.x > 0.0005) {
        float2 lampS = float2(laP28.x * aspect, laP28.y);
        float2 toLv  = lampS - pp;
        float2 toLd  = toLv / max(length(toLv), 1e-5);
        float  lz    = clamp(laP32.w, -1.0, 1.0);
        // 1.0 at light_z 0.35 (a lamp a little in front of the dish), up to
        // 5x as the lamp sinks into the plane, half as it rises over it.
        float  elong = min(0.35 / max(lz, 0.07), 5.0);
        float  dirW  = saturate(lz * 4.0);            // 0 at and below zero
        float  sft   = saturate(laP32.z);
        float  L0    = max(laP32.y, 0.0);
        float  lenD  = L0 * elong * dirW;             // the raked length
        float  lenI  = L0 * (1.0 - dirW) * 0.55;      // the backlit spill
        // Four taps, near to far. The near one is tight and counts most, the
        // far ones are wide and weak: "darkest and tightest at the caster,
        // fading and softening with distance".
        const float4 fk = float4(0.30, 0.58, 0.82, 1.00);
        const float4 wk = float4(1.00, 0.76, 0.52, 0.32);
        float2 o0 = toLd * (lenD * fk.x) + float2( 0.7071,  0.7071) * (lenI * fk.x);
        float2 o1 = toLd * (lenD * fk.y) + float2(-0.7071,  0.7071) * (lenI * fk.y);
        float2 o2 = toLd * (lenD * fk.z) + float2(-0.7071, -0.7071) * (lenI * fk.z);
        float2 o3 = toLd * (lenD * fk.w) + float2( 0.7071, -0.7071) * (lenI * fk.w);
        // One bounding disc round all four taps, so a blob nowhere near this
        // pixel's light path costs a dot product and nothing else.
        float2 cB = pp + toLd * (lenD * 0.64);
        float  rB = lenD * 0.64 + lenI + 1e-4;
        float4 ft = float4(0.0, 0.0, 0.0, 0.0);
        [loop]
        for (int si = 0; si < nb; si++) {
            AcidBlobGPU S = AcidBlobs[si];
            float2 sc  = float2(S.a.x * aspect, S.a.y);
            float  e   = 1.0 + S.b.w;
            float  sup = S.a.z * laP0.z;
            float  reach = sup * e + rB;
            float2 qb  = cB - sc;
            if (dot(qb, qb) >= reach * reach) continue;
            float  s2 = sup * sup;
            float2 q; float d2, u;
            q = pp + o0 - sc; q.y /= e; d2 = dot(q, q);
            if (d2 < s2) { u = 1.0 - d2 / s2; ft.x += u * u * u * S.a.w; }
            q = pp + o1 - sc; q.y /= e; d2 = dot(q, q);
            if (d2 < s2) { u = 1.0 - d2 / s2; ft.y += u * u * u * S.a.w; }
            q = pp + o2 - sc; q.y /= e; d2 = dot(q, q);
            if (d2 < s2) { u = 1.0 - d2 / s2; ft.z += u * u * u * S.a.w; }
            q = pp + o3 - sc; q.y /= e; d2 = dot(q, q);
            if (d2 < s2) { u = 1.0 - d2 / s2; ft.w += u * u * u * S.a.w; }
        }
        // The far taps read the occluder through a WIDER threshold band --
        // that is the penumbra, and it costs one madd each. A weighted mean,
        // not a max: the max of four taps walks down four steps as the pixel
        // leaves a mass, and prints them as bands.
        float4 bk = thresh * (0.18 + sft * (0.35 + 1.9 * fk));
        float4 oc;
        oc.x = 1.0 - smoothstep(thresh - bk.x, thresh + bk.x, ft.x);
        oc.y = 1.0 - smoothstep(thresh - bk.y, thresh + bk.y, ft.y);
        oc.z = 1.0 - smoothstep(thresh - bk.z, thresh + bk.z, ft.z);
        oc.w = 1.0 - smoothstep(thresh - bk.w, thresh + bk.w, ft.w);
        float occ = dot(oc, wk) / dot(wk, float4(1.0, 1.0, 1.0, 1.0));
        // ---- the droplets, analytically ----------------------------------
        float dOcc = 0.0;
        [branch] if (laP18.x > 0.5) {
            const int gw2 = (int)laP18.y, gh2 = (int)laP18.z;
            float cellP = min(aspect / max((float)gw2, 1.0), 1.0 / max((float)gh2, 1.0));
            int cx2 = clamp((int)floor(uv.x * gw2), 0, gw2 - 1);
            int cy2 = clamp((int)floor(uv.y * gh2), 0, gh2 - 1);
            float2 sdir = -toLd;                       // away from the lamp
            [loop]
            for (int oy2 = -1; oy2 <= 1; oy2++) {
                int yy2 = cy2 + oy2;
                if (yy2 < 0 || yy2 >= gh2) continue;
                [loop]
                for (int ox2 = -1; ox2 <= 1; ox2++) {
                    int xx2 = cx2 + ox2;
                    if (xx2 < 0 || xx2 >= gw2) continue;
                    uint2 cl = DropCells[yy2 * gw2 + xx2];
                    [loop]
                    for (uint dj = 0; dj < cl.y; dj++) {
                        float4 D = AcidDrops[cl.x + dj];
                        float dqz = floor(D.w * 0.25);
                        float dwr = D.w - 4.0 * dqz;
                        float rng = (dwr > 1.5) ? 1.0 : 0.0;
                        float gate = dwr - 2.0 * rng;
                        if (gate <= 0.002) continue;
                        float R = abs(D.z);
                        // A droplet is a body, and its HEIGHT is its own
                        // radius: a 3-px speck throws a 3-px shadow and a big
                        // ring a long one. Capped at half a grid cell, which
                        // is as far as the 3x3 walk can see a caster at all.
                        float hN = saturate(R / 0.012);
                        float dl = min(lenD * hN, cellP * 0.55);
                        // The capsule starts just BEYOND the droplet's own
                        // rim, not at its centre. Centred, a droplet's shadow
                        // came out concentric -- a dark collar all round it,
                        // including on the side facing the lamp, which is the
                        // one side that cannot be in shadow. Pushed out by
                        // three quarters of the radius it leaves the lamp
                        // side clean and the lobe emerges on the far side,
                        // which is what makes it read as a shadow and not as
                        // a halo.
                        float2 dc = float2(D.x * aspect, D.y) + sdir * (R * 0.75);
                        float2 v = pp - dc;
                        float  t = clamp(dot(v, sdir), 0.0, dl);
                        float2 pe = v - sdir * t;
                        float  f  = (dl > 1e-6) ? (t / dl) : 0.0;
                        // A hollow droplet is a BUBBLE: its middle is film,
                        // so it shadows with its WALL and not with a disc.
                        float core  = rng * R;
                        float wallR = lerp(min(R, cellP * 0.5),
                                           max(R * saturate(laP20.x), 0.0008), rng);
                        float rd = length(pe) - core;
                        if (rng < 0.5) rd = max(rd, 0.0);
                        float rad = wallR * (0.90 + sft * (0.30 + 1.5 * f))
                                  + min(lenI * hN, cellP * 0.45);
                        // ...and a droplet is a LENS, not a plug: it bends
                        // the light aside rather than eating it, so it never
                        // throws the full umbra a mass does.
                        float s = 0.85 * exp(-2.0 * f) * gate
                                * exp(-(rd * rd) / max(rad * rad, 1e-12));
                        dOcc = max(dOcc, s);
                    }
                }
            }
        }
        shadowMul = 1.0 - saturate(laP32.x) * saturate(max(occ, dOcc));
        col *= shadowMul;
    }
    // ---- ink-tinted toe (toe_tint) ---------------------------------------
    // The genre's darkest ink is #180808 / #2c1506 — a lifted, HUE-TINTED toe,
    // never a crush to neutral black. Lift only the bottom of the range, and
    // only toward a low-value version of the ink ramp's own mid hue, so the
    // tint always belongs to this palette (and follows the palette sweep).
    // laP10.w = 0 leaves the shipped neutral toe untouched.
    if (laP10.w > 0.001) {
        float3 inkHue = laInk[1].rgb / max(max(laInk[1].r, max(laInk[1].g, laInk[1].b)), 1e-3);
        float  toeL   = dot(col, float3(0.2126, 0.7152, 0.0722));
        col += inkHue * (0.12 * saturate(laP10.w) * (1.0 - smoothstep(0.0, 0.22, toeL)));
    }
    // ---- final trim: post_chroma / post_lift -----------------------------
    // A transparent film costs perceptual chroma (10-20%) and a little
    // lightness against the same look opaque, measured in OKLab over the
    // non-dark pixels. Both of those are corrected here rather than in every
    // palette: chroma is scaled about the pixel's OWN luma, so hue and
    // luminance survive and only the distance from grey grows; lift is a
    // plain luma multiplier on top. 1 / 1 = untouched, and both are applied
    // to the whole composite, so ink, halo and film move together.
    if (abs(laP17.y - 1.0) > 0.001) {
        float pl = dot(col, float3(0.2126, 0.7152, 0.0722));
        col = max(pl.xxx + (col - pl.xxx) * laP17.y, 0.0);
    }
    if (abs(laP17.z - 1.0) > 0.001) col *= laP17.z;
    // ---- coarse ANIMATED film grain over everything ----------------------
    // grain_shadow_weight (laP12.w) biases the amplitude into the darks:
    // in every reference frame the ink is visibly noisy while the flat oil
    // discs are clean, which is what real high-ISO backlit macro looks like.
    // (1 - luma)^2 -- squared, so mid-tones already lose most of the grain.
    float grainAmp = laP5.z;
    if (laP12.w > 0.0005) {
        float gl2 = saturate(1.0 - dot(col, float3(0.2126, 0.7152, 0.0722)));
        grainAmp *= lerp(1.0, gl2 * gl2, saturate(laP12.w));
    }
    // The reference halo band is visibly GRAINY (film grain over a bright,
    // thin, refracted band); the shadow weighting above would scrub it clean.
    grainAmp *= 1.0 + haloW * 1.2 * saturate(laP14.y);
    col += (AcidHash21(floor(i.pos.xy / max(laP5.w, 1.0)) + frac(laP6.z) * 913.7) - 0.5)
         * grainAmp;
    C = saturate(col);
    // HDR: the oil is a flat fill, so give it its own highlight level rather
    // than inheriting the ink's. Keep the hot part small (ABL): the rim band.
    // With oil_transparency the oil pixel is mostly TRANSMITTED ink, so it
    // must keep the ink's own highlight level; only the scattered part of the
    // film is driven to oil_hdr. filmOp is 1 when transparency is off.
    // lampG carries rise_bottom_light into the HDR level too: the base of the
    // lamp should be the hot part of the frame, not merely the pale part.
    if (laP7.x > 0.001) m = lerp(m, laP7.x * lampG * shadowMul, alpha * filmOp);
    if (laP7.y > 0.001) m = max(m, laP7.y * rimB * alpha);
    // a specular on a real oil surface is a highlight, not a paler fill
    if (laP7.x > 0.001 && specAmt > 0.0005)
        m = max(m, min(laP7.x * (1.0 + 2.5 * specAmt), 1.6) * alpha);
#endif

    // ======================= [post]: the last two steps ====================
    // Shared by every look and applied AFTER everything else (post_chroma /
    // post_lift included), in SCREEN space -- i.pos.xy, never the folded uv:
    // a lens and a film emulsion sit in front of the picture, so they do not
    // mirror with it. Both amounts default to 0 and neither branch is entered
    // then, so style=fluid is untouched down to the bit unless asked.
    // (aberration used to live here as a first-order expansion of the
    // composite about the pixel. It could not work: the offset direction came
    // from the LUMINANCE gradient, which always points at the brighter side,
    // so dR was positive and dB negative on BOTH sides of every droplet -- a
    // symmetric warm outline, never a split, at any slider value. And a 3-px
    // displacement is not something one derivative can express anyway. It is
    // now a real resample of the finished frame in kPostSrc, where the pixels
    // exist to be resampled. See item Z.)
    // A slight fall-off toward the corners: the field stop of a macro lens,
    // never a circle with an edge. This one stays here -- it is a shading of
    // the picture, not a displacement of it, so it needs nothing resampled.
    [branch] if (poP1.w > 0.0005) {
        // ...and its centre WANDERS with the rig's lens (item V3), so the
        // darkest corner turns over minutes instead of sitting in one corner
        // of the panel for hours. vignette_wander 0 puts it back at the
        // middle, which is the constant this line used to subtract.
        float2 d = i.pos.xy * texelSize - float2(poP1.x, poP1.y);
        float  r2 = dot(d, d) * 2.0;
        C *= 1.0 - saturate(poP1.w) * 0.45 * r2 * r2;
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    [branch] if (poP0.x > 0.0005) {
        // FILM GRAIN, luminance-weighted: the mids and darks carry it (that is
        // where emulsion noise lives and where the ink already looks grainy),
        // the peaks stay clean so a bright core never fizzes, and a pixel that
        // is TRUE BLACK gets only a whisper -- an off OLED pixel is the best
        // thing on this panel and grain would light the whole frame's floor.
        float2 gc  = floor(i.pos.xy / max(poP0.y, 0.25));
        // film_grain_fps patterns per second at speed 1 (24 by default: the
        // grain changes once per FRAME of the stock, not once per refresh);
        // lower speeds hold a pattern longer, the slow chatter of a big-grain
        // stock.
        float  tq  = floor(mrP0.w * max(poP1.z, 1.0) * max(poP0.z, 0.0));
        float2 tj  = frac(tq * float2(0.1031, 0.0973)) * 733.0;
        float  n0  = PostHash21(gc + tj);
        float3 nz  = float3(n0, n0, n0);
        [branch] if (poP0.w > 0.0005) {
            nz = lerp(nz, float3(n0, PostHash21(gc + tj + 37.71),
                                     PostHash21(gc + tj + 91.37)), saturate(poP0.w));
        }
        float lum = dot(C, float3(0.2126, 0.7152, 0.0722));
        float w   = (1.0 - smoothstep(0.55, 1.00, lum))
                  * lerp(0.15, 1.0, smoothstep(0.0, 0.06, lum));
        C = max(C + (nz - 0.5) * (poP0.x * 0.5 * w), 0.0);
    }
    float3 lin = SRGBToLinear(saturate(C));
    // Interpret the dye in a wider gamut and convert to the swap chain's 709
    // primaries. Out-of-gamut saturation comes out as negative components —
    // FP16 scRGB carries those to the display (QD-OLED shows them).
    if (gamut > 1.5) {          // BT.2020 — full QD-OLED vividness
        lin = float3(
            dot(float3( 1.66049, -0.58764, -0.07285), lin),
            dot(float3(-0.12455,  1.13290, -0.00835), lin),
            dot(float3(-0.01815, -0.10058,  1.11873), lin));
    } else if (gamut > 0.5) {   // Display-P3 — parity with the WE original
        lin = float3(
            dot(float3( 1.22494, -0.22494,  0.0),     lin),
            dot(float3(-0.04206,  1.04206,  0.0),     lin),
            dot(float3(-0.01964, -0.07868,  1.09832), lin));
    }

    // True HDR ("parity-plus"): the SDR-parity colour above is complete and
    // already clamped, so this gain scales ALL channels together in linear
    // light — hue and saturation are untouched, hot dye just gets brighter
    // than SDR white (a saturated red at 600 nits, not a white core). Driven
    // by the raw dye intensity with a soft knee: below `knee` exact parity,
    // reaching peakGain at the dye cap. Identity when peak_nits = 0.
#ifdef INK
    // ---- [ink] tone_chroma / tonemap ------------------------------------
    // Chroma hold first, in linear light, about the pixel's own luma — the
    // same trim [liquid_acid] post_chroma makes. Raising the white point
    // below must never read as a wash.
    if (abs(ikP5.w - 1.0) > 0.001) {
        // NOT clamped at 0 the way [liquid_acid] post_chroma is: with
        // gamut > 0 the out-of-gamut components are NEGATIVE on purpose and
        // the QD-OLED shows them, so clamping here would throw away exactly
        // the saturation this key exists to protect.
        float il = dot(lin, float3(0.2126, 0.7152, 0.0722));
        lin = il.xxx + (lin - il.xxx) * ikP5.w;
    }
    // HDR RANGE MAPPING. The parity-plus gain below spends headroom only on
    // dye that is nearly at the brightness CAP; the ink composite is an
    // absorption result that never gets there, so with `tonemap` 0 the whole
    // frame is squeezed into SDR white however much headroom the panel has.
    //
    // Here the composite's OWN 0..1 range is mapped onto [black, white] nits
    // instead, hue-preserving (one scale on all three channels, exactly like
    // the gain). `tone_knee` blends in a smoothstep S: the mids gain slope
    // (1.5x at the midpoint) and therefore more distinguishable steps, at the
    // cost of the extreme toe and shoulder. The parity-plus gain is then
    // retargeted ABOVE the new white, so hot moving cores (hdr_core, motion
    // gated) still run up toward peak_nits instead of stopping at white.
    if (ikP4.w > 0.5) {
        float v = max(lin.r, max(lin.g, lin.b));
        if (v > 1e-5) {
            float x = saturate(v);
            float s = lerp(x, x * x * (3.0 - 2.0 * x), saturate(ikP5.z));
            float o = ikP5.y + (ikP5.x - ikP5.y) * s;   // scRGB (1.0 = 80 nits)
            if (peakGain > 1.001) {
                float t = smoothstep(knee, max(capBright, knee + 0.01), m);
                o *= lerp(1.0, max(ikP6.y / max(ikP5.x, 1e-4), 1.0), t * t);
            }
            lin *= min(o, ikP6.y) / v;
        }
        return float4(lin, outCoc);
    }
#endif
    float gain = 1.0;
    if (peakGain > 1.001) {
        float t = smoothstep(knee, max(capBright, knee + 0.01), m);
        gain = lerp(1.0, peakGain, t * t);
    }
    return float4(lin * sdrScale * gain, outCoc);
}
)hlsl";

// ===========================================================================
// [post] IMAGE-SPACE pass: the camera in front of the dish.
// The display pass above shades every edge analytically off a signed distance
// to its isoline, which is exact on a big mass and breaks on anything narrower
// than the band itself: a 2-px ring wall has no gradient at its centre, so the
// rim, meniscus and halo all gate themselves off within a pixel of it and the
// wall renders as a hard stroked line beside softly shaded droplets (the user:
// "these are pixel perfect circles", "whatever shading is in this photo needs
// to be everywhere"). A lens does not know what it is looking at. This pass
// takes the finished frame (linear scRGB, HDR gain and gamut already applied)
// and gives every feature, whatever its size, the same optics:
//   post_blur_px  a small disc defocus (the circle of confusion),
//   post_glow     a weak, wide veiling glare: the picture mixed with a wide
//                 blur of itself, so dark bleeds a little into bright and
//                 bright into dark around every edge -- the soft dark
//                 gradient the user liked on the medium droplet, everywhere,
//   film grain    re-applied here, AFTER the blur, so it stays crisp: the
//                 grain is in the emulsion, the blur is in the glass (the
//                 display pass zeroes its own grain when this pass runs).
// Runs only when blur or glow is non-zero, so style=fluid never enters it.
// Bound with the graphics root signature: b0 = 32 root constants, t0 = the
// display pass's target, s0 = the static linear clamp sampler.
// ===========================================================================
static const char* kPostSrc = R"hlsl(
cbuffer PostPassCB : register(b0) {
    float4 pp0;   // x 1/W        y 1/H        z blurPx (this res) w glow
    float4 pp1;   // x glowPx     y grain      z grainSize(this res) w grainSpeed
    float4 pp2;   // x grainColor y time       z sdrScale           w glowDark
    float4 pp3;   // x dust       y hairs      z scratches          w leak
    float4 pp4;   // x rate (s)   y noise      z noiseSize(this res) w H/1440
    float4 pp5;   // x stock      y fog        z bloom   w dofMaxPx(this res; 0 = alpha is not a CoC)
    float4 pp6;   // x fogReach(uv) y bloomPx(this res) z psfPx(this res) w grainFps
    float4 pp7;   // x dither  y halation  z halationPx(this res)  w warmth
};
// ---- THE RIG -------------------------------------------------------------
// One lamp and one lens on one body, stepped on the CPU (FluidRenderer::
// StepCameraRig) and handed over as a single block, so the haze, the bloom,
// the depth of field and -- later -- the lid ghosts, the flare and the
// vignette centre cannot disagree about where the rig is. rg2 is the lens's
// chromatic split, rg3 is reserved and rg4 is the lid; they are
// reserved for those. This is b3, which the display pass uses for its mirror
// fold and this pass has never bound.
cbuffer RigCB : register(b3) {
    float4 rg0;   // x lampX(uv, drifted) y lampY  z axisX(uv)  w axisY(uv)
    float4 rg1;   // x tiltAngle(rad) y tiltAmt  z focusDepth  w movePhase 0..1
    float4 rg2;   // x aberration y aberrPx(this res) z aberrField  w -
    float4 rg3;   // x shimmer y shimmerPx(this res) z shiftX(uv) w shiftY(uv)
    // rg4 is THE LID (task V2), all of it, PACKED -- rg2 is the lens's
    // chromatic split and rg3 is spoken for, so the lid gets one float4 and
    // twelve numbers have to fit in it. Each float carries an exact integer
    // below 2^24, so a float32 holds it without loss.
    //   x  lidX : 12 | lidY : 12          the cover's own wander, uv
    //   y  lidRot : 12 | refractPx : 12   orientation, reflection wobble
    //   z  ghost : 8 | rings : 8 | sheen : 8
    //   w  glint : 7 | iris : 7 | sheenPx : 6 | ghostSpread : 4
    // The master is folded into the amplitudes on the CPU; all four floats
    // are exactly 0 when the lid is off, which is the block's only branch.
    float4 rg4;
};
Texture2D Src : register(t0);
// The sim's own low-res velocity field, bound only when the thermal shimmer
// is on. It is what lets a post effect be moved BY THE FLUID instead of by
// its own clock -- the user's rule that a burst which shoves the oil should
// shove the heat above it too.
Texture2D<float4> VelLow : register(t3);
SamplerState linearClamp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

// Value noise off the same hash: two smoothed lattice octaves are enough for
// a heat wobble, and it costs no texture and no table. (item V3)
float PVNoise(float2 p);
// same hash as the display pass's PostHash21, so the grain pattern is the one
// the user already approved
float PHash21(float2 p) {
    p = frac(p * float2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return frac(p.x * p.y);
}
// ---- THE LID's packed constants (rg4) -----------------------------------
// Each field was quantised on the CPU and shifted into one exact integer.
// float32 carries 24 bits of mantissa, so every pack below is lossless and
// the unpack is a divide and a floor per field.
float2 LidU12(float v) {
    float a = floor(v * (1.0 / 4096.0));
    return float2(a, v - a * 4096.0) * (1.0 / 4095.0);
}
float3 LidU8(float v) {
    float a = floor(v * (1.0 / 65536.0));
    float r = v - a * 65536.0;
    float b = floor(r * (1.0 / 256.0));
    return float3(a, b, r - b * 256.0) * (1.0 / 255.0);
}
float4 LidU7764(float v) {
    float a = floor(v * (1.0 / 131072.0));
    float r = v - a * 131072.0;
    float b = floor(r * (1.0 / 1024.0));
    r -= b * 1024.0;
    float c = floor(r * (1.0 / 16.0));
    return float4(a * (1.0 / 127.0), b * (1.0 / 127.0),
                  c * (1.0 / 63.0), (r - c * 16.0) * (1.0 / 15.0));
}
float PVNoise(float2 p) {
    float2 i0 = floor(p), f = frac(p);
    float2 u  = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(PHash21(i0),                 PHash21(i0 + float2(1.0, 0.0)), u.x),
                lerp(PHash21(i0 + float2(0.0, 1.0)), PHash21(i0 + float2(1.0, 1.0)), u.x), u.y);
}
float3 ToSRGB(float3 c) {
    c = saturate(c);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 1e-6), 1.0 / 2.4) - 0.055;
    return lerp(lo, hi, step(0.0031308, c));
}
float3 ToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(lo, hi, step(0.04045, c));
}
// ONE noise layer, applied exactly the way the display pass applies its own
// grain: in an sRGB-ENCODED proxy of the pixel, luminance-weighted (the mids
// and darks carry it, the peaks stay clean, a true-black pixel gets only a
// whisper), converted back to linear and scaled by whatever HDR gain this
// pixel carries so it never fizzes on a hot core. Shared by the coarse film
// grain and the finer, faster film_noise.
float3 Emulsion(float3 d, float3 nz, float amt, float sdr) {
    const float3 EW = float3(0.2126, 0.7152, 0.0722);
    float3 base = d / sdr;
    float3 e    = ToSRGB(base);
    float  lum  = dot(e, EW);
    float  w    = (1.0 - smoothstep(0.55, 1.00, lum))
                * lerp(0.15, 1.0, smoothstep(0.0, 0.06, lum));
    float3 e2   = max(e + (nz - 0.5) * (amt * 0.5 * w), 0.0);
    float3 l0   = ToLinear(e), l2 = ToLinear(e2);
    float  gain = clamp(dot(base, EW) / max(dot(l0, EW), 1e-4), 1.0, 16.0);
    return max(d + (l2 - l0) * gain * sdr, min(d, 0.0));
}

// Uniform disc, 19 taps (centre + 6 at r/2 + 12 at r): the circle of confusion
// of a defocused lens is a flat disc, not a Gaussian, which is why a defocused
// hairline becomes a soft band of the same darkness spread wider rather than
// a faint smear. Bilinear taps between texels make it smoother than its count.
// Three EQUAL-AREA annuli (radii sqrt(1/6), sqrt(3/6), sqrt(5/6)) with equal
// total weight each, plus a light centre tap: that is a flat disc sampled
// uniformly by area, where a ring pattern chosen by eye is not. It matters now
// that the radius is per-pixel and reaches ten-odd px on the far layer -- the
// old 19 taps printed a visible 12-pointed star round every out-of-focus
// highlight at that size. `jit` turns each pixel's tap angles by its own hash,
// so what banding is left dissolves into noise the grain is about to cover.
float3 Disc(float2 uv, float2 r, float jit) {
    float3 s = Src.SampleLevel(linearClamp, uv, 0).rgb * 0.06;
    float  w = 0.06;
    [unroll] for (int k = 0; k < 6; k++) {
        float a = (float)k * 1.0471976 + jit;
        s += Src.SampleLevel(linearClamp, uv + float2(cos(a), sin(a)) * (0.41 * r), 0).rgb * 0.0556;
        w += 0.0556;
    }
    [unroll] for (int j = 0; j < 10; j++) {
        float a = (float)j * 0.6283185 + jit * 1.7 + 0.31;
        s += Src.SampleLevel(linearClamp, uv + float2(cos(a), sin(a)) * (0.71 * r), 0).rgb * 0.0333;
        w += 0.0333;
    }
    [unroll] for (int m = 0; m < 14; m++) {
        float a = (float)m * 0.4487990 + jit * 2.3 + 0.73;
        s += Src.SampleLevel(linearClamp, uv + float2(cos(a), sin(a)) * (0.91 * r), 0).rgb * 0.0238;
        w += 0.0238;
    }
    return s / w;
}
// Wide, soft-shouldered blur, 37 taps on three rings with falling weights:
// the veiling glare of a real lens (scatter in the glass and the film base),
// which is what makes every edge in a macro photograph carry a faint wide
// gradient on both sides.
float3 Wide(float2 uv, float2 r) {
    float3 s = Src.SampleLevel(linearClamp, uv, 0).rgb;
    float  w = 1.0;
    [unroll] for (int k = 0; k < 8; k++) {
        float a = (float)k * 0.7853982 + 0.2;
        s += Src.SampleLevel(linearClamp, uv + float2(cos(a), sin(a)) * (0.33 * r), 0).rgb * 0.9;
        w += 0.9;
    }
    [unroll] for (int j = 0; j < 12; j++) {
        float a = (float)j * 0.5235988 + 0.1;
        s += Src.SampleLevel(linearClamp, uv + float2(cos(a), sin(a)) * (0.66 * r), 0).rgb * 0.55;
        w += 0.55;
    }
    [unroll] for (int m = 0; m < 16; m++) {
        float a = (float)m * 0.3926991;
        s += Src.SampleLevel(linearClamp, uv + float2(cos(a), sin(a)) * r, 0).rgb * 0.25;
        w += 0.25;
    }
    return s / w;
}

)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
float4 PSMain(VSOut i) : SV_Target {
    const float3 W = float3(0.2126, 0.7152, 0.0722);
    float2 uv = i.uv;
    // ======================= V3: MOTION ====================================
    // The user's rule for this whole family: nothing may sit at a fixed screen
    // position on an OLED, ever. Their motion model is specific -- a SLIGHT,
    // SLOW drift all the time, plus occasional readjustments where everything
    // moves at once. Both of these shift the coordinate the finished frame is
    // READ from, so every effect downstream (defocus, aberration, halation,
    // haze, bloom) travels with the picture as one piece, while the grain and
    // the dither stay in screen space -- which is right: the film moves, the
    // sensor does not.
    [branch] if (abs(rg3.z) > 1e-9 || abs(rg3.w) > 1e-9) {
        // OLED PIXEL-SHIFT ORBIT: a couple of px on a many-minute closed path,
        // stepped by a few thousandths of a pixel per frame. Invisible, and
        // still moving, which is the whole trick.
        uv += float2(rg3.z, rg3.w);
    }
    [branch] if (rg3.x > 0.0005) {
        // THERMAL SHIMMER: the air above a lamp. A very fine refractive
        // wobble, a pixel or two at most, strongest near the lamp and fading
        // to nothing away from it -- and ADVECTED BY THE SIM, not by its own
        // clock: the low-res velocity field offsets the noise coordinates, so
        // a burst that shoves the oil shoves the heat above it as well.
        float  aspS = pp0.y / max(pp0.x, 1e-9);
        float2 qs   = float2((uv.x - rg0.x) * aspS, uv.y - rg0.y);
        float  near = exp(-length(qs) * 1.35);
        // CLAMPED on purpose: this texture holds the sim's velocity in the
        // sim's own units, which a burst can drive arbitrarily high, and an
        // unbounded offset into a noise field is not advection -- it is white
        // noise that flickers. Bounded, a burst leans the heat and a calm
        // frame leaves it alone, which is the behaviour that was wanted.
        float2 vel  = clamp(VelLow.SampleLevel(linearClamp, uv, 0).xy * 0.5, -1.5, 1.5);
        float  t    = pp2.y;
        // two octaves, drifting upward off the lamp (heat rises) and carried
        // sideways by the fluid
        float2 np   = float2(uv.x * aspS, uv.y) * 11.0
                    + vel * 1.5 + float2(0.0, -t * 0.09);
        float  n1   = PVNoise(np);
        float  n2   = PVNoise(np * 2.17 + 31.7 - float2(0.0, t * 0.05));
        float2 warp = float2(n1 - 0.5, n2 - 0.5) * (2.0 * rg3.y * rg3.x * near);
        uv += warp * pp0.xy;
    }
    float4 c4 = Src.SampleLevel(linearClamp, uv, 0);
    float3 c  = c4.rgb;
    float3 d  = c;
    // ---- DEPTH OF FIELD: one defocus radius PER PIXEL --------------------
    // post_blur_px is the lens's own softness, the floor under everything;
    // the display pass has written this pixel's circle of confusion into the
    // source's ALPHA (px at 1440p), so an element far from the plane of focus
    // gets a wider disc than the one sitting in it. Gather-style: the radius
    // comes from the CENTRE pixel, which is what makes it one texture read
    // and no second pass, at the price of a sharp element bleeding very
    // slightly into a blurred neighbour's disc -- invisible at these radii,
    // and cheaper than any scatter that would fix it. pp7.x = 0 means no look
    // wrote a CoC and alpha is the plain 1.0, so it is never read.
    // The user, on the first depth-of-field frame: "what's in focus is
    // EXTREMELY in focus." The dreaminess is supposed to come from the haze,
    // the bloom and the elements that are OFF the plane -- not from a global
    // softness sitting on the sharp slice too. So with a CoC in hand
    // post_blur_px stops being a floor under the whole frame and becomes the
    // lens's softness on pixels that have ALREADY left focus, faded in with
    // the CoC itself: on the plane the radius is exactly zero and the Disc is
    // never entered, which is as sharp as the renderer can be.
    float rPx = pp0.z;
    [branch] if (pp5.w > 0.0005) {
        float coc = min(c4.a * pp4.w, pp5.w);
        rPx = max(coc, pp0.z * saturate(coc));
    }
    // DIFFRACTION (item W). The floor, and it does not care about focus: the
    // sharpest a lens can draw a point is its own point spread, so at
    // psf_px >= 1 nothing in the frame is ever pixel-limited and an in-focus
    // isoline can never come out as the stair-stepped coverage AA the user
    // photographed.
    rPx = max(rPx, pp6.z);
    [branch] if (rPx > 0.01) {
        float jit = 6.2831853 * PHash21(i.pos.xy * 0.0173 + 0.31);
        d = Disc(uv, rPx * pp0.xy, jit);
    }
    [branch] if (pp0.w > 0.0005) {
        float3 g = Wide(uv, pp1.x * pp0.xy);
        // glowDark > 0 leans the glare toward the DARK side: dark features
        // bleed into the bright film more than the bright film bleeds into
        // the black (the backlit dish: a droplet's shadow is a real thing,
        // the film's light scattering into the ink is fainter).
        float wg = pp0.w;
        if (pp2.w > 0.0005) {
            float darker = saturate(dot(d, W) - dot(g, W));   // this pixel is brighter than its surround
            wg *= lerp(1.0, 1.0 + 1.5 * step(1e-5, darker), saturate(pp2.w));
        }
        d = lerp(d, g, saturate(wg));
    }
    // ---- LATERAL CHROMATIC ABERRATION (item Z) ---------------------------
    // A real lens focuses red and blue at slightly different MAGNIFICATIONS,
    // so the three channels land on the sensor at slightly different scales:
    // red pushed out from the optical axis, blue pulled in, green where it
    // belongs. On a frame that is a radial split which is nothing at the
    // centre and a couple of pixels at the corners -- exactly the LAPD sheet's
    // colour fringing.
    //
    // This used to be attempted in the display pass as a first-order expansion
    // about the pixel, and it could not work: the offset direction came from
    // the LUMINANCE gradient, which always points at the brighter side, so red
    // was added and blue subtracted on BOTH sides of every droplet. A
    // symmetric warm outline, never a split, at any slider value -- which is
    // why the user could not see it. A lateral split needs the sign to FLIP
    // across an edge, and no derivative taken about one pixel can do that.
    // Here the finished frame exists as a texture, so the channels are simply
    // resampled where they actually landed.
    //
    // The optical centre is the RIG's lens centre, not the middle of the
    // screen: it drifts and it re-aims with everything else, so the fringing
    // never has a fixed null point burnt into one spot on the panel.
    [branch] if (rg2.x > 0.0005) {
        float  aspZ = pp0.y / max(pp0.x, 1e-9);
        float2 qz   = float2((uv.x - rg0.z) * aspZ, uv.y - rg0.w);
        float  rz   = length(qz);
        float2 nz   = qz / max(rz, 1e-5);
        // 0.60 in this space is about a 16:9 corner, so rn is ~1 there. The
        // floor keeps a trace of it mid-frame -- a lens is never perfect in
        // the middle either -- and aberration_field bends how fast it grows.
        float  rn   = saturate(rz / 0.60);
        float  ramp = lerp(0.12, 1.0, pow(rn, 1.0 + saturate(rg2.z * 0.5) * 2.0));
        float  sp   = rg2.y * ramp * saturate(rg2.x);        // px at this res
        float2 duv  = nz * (sp * pp0.y) * float2(1.0 / aspZ, 1.0);
        // Applied as the DIFFERENCE the displacement makes, not as a raw
        // resample: `d` already carries the defocus and the glare, and those
        // are not in Src. On a sharp edge Src == d and this is exactly the
        // split; in a defocused region the two samples are nearly equal and
        // the fringe correctly fades out with the blur, which is what a real
        // lens does -- you cannot see colour fringing on a bokeh disc.
        float3 s0 = Src.SampleLevel(linearClamp, uv, 0).rgb;
        d.r += Src.SampleLevel(linearClamp, uv + duv, 0).r - s0.r;
        d.b += Src.SampleLevel(linearClamp, uv - duv, 0).b - s0.b;
        d = max(d, 0.0);
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- HALATION (item V1) ----------------------------------------------
    // CineStill 800T is ordinary cinema stock with the anti-halation backing
    // removed: light from a highlight goes through the emulsion, reflects off
    // the film base and comes back a few pixels out, reddened by the layers it
    // has now crossed twice. So it is NOT the wide weak wash `bloom` already
    // does. It is tight (a dozen px), it is taken only from what is genuinely
    // bright, and it lands in the DARK around a highlight rather than on the
    // highlight itself -- which is the difference between the film glowing and
    // the picture simply being over-exposed.
    //
    // Two jittered rings of taps around a centre pushed toward the rig's LAMP,
    // so the glow pools on the lamp side and the whole field's asymmetry turns
    // when the lamp wanders. Nothing here is ever at a fixed screen position.
    [branch] if (pp7.y > 0.0005) {
        float  sdrH = max(pp2.z, 1e-3);
        float  asp  = pp0.y / max(pp0.x, 1e-9);
        float2 toL  = float2((rg0.x - uv.x) * asp, rg0.y - uv.y);
        float2 lean = toL / max(length(toL), 1e-5) * float2(1.0 / asp, 1.0);
        float2 rad  = pp7.z * pp0.xy;
        float2 ctr  = uv + lean * rad * 0.30;
        float  jt   = 6.2831853 * PHash21(i.pos.xy * 0.0271 + 7.13);
        float3 hot  = float3(0.0, 0.0, 0.0);
        float  hw   = 0.0;
        [unroll] for (int k = 0; k < 8; k++) {
            float a2 = (float)k * 0.7853982 + jt;
            float3 sp = Src.SampleLevel(linearClamp, ctr + float2(cos(a2), sin(a2)) * (0.55 * rad), 0).rgb;
            // Only what is REALLY bright contributes, with a soft knee so a
            // drifting highlight does not switch the glow on in one frame.
            // The test is on the PEAK CHANNEL, not on luminance: this film is
            // a saturated magenta whose luminance is a third of its red
            // channel, so a luminance threshold called the brightest thing in
            // the frame dark and the whole effect evaluated to exactly zero.
            // Physically the peak channel is also the right question -- the
            // film base scatters whatever light reached it, and a saturated
            // red highlight halates hard however little it weighs in Y.
            float  ex = smoothstep(0.55, 1.05, max(sp.r, max(sp.g, sp.b)) / sdrH);
            hot += sp * ex; hw += 1.0;
        }
        [unroll] for (int m = 0; m < 12; m++) {
            float a2 = (float)m * 0.5235988 + jt * 1.7;
            float3 sp = Src.SampleLevel(linearClamp, ctr + float2(cos(a2), sin(a2)) * rad, 0).rgb;
            float  ex = smoothstep(0.55, 1.05, max(sp.r, max(sp.g, sp.b)) / sdrH);
            hot += sp * ex * 0.6; hw += 0.6;
        }
        hot /= max(hw, 1e-5);
        // reddened by the two passes through the emulsion
        float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(1.00, 0.34, 0.13), saturate(pp7.w));
        // ...and it only shows where this pixel is DARKER than what it is
        // gathering: on the bright film itself there is nothing to see.
        float  own  = max(d.r, max(d.g, d.b)) / sdrH;   // same metric as the gather
        float  into = 1.0 - smoothstep(0.30, 0.95, own);
        d += hot * tint * (saturate(pp7.y) * 0.55 * into);
    }
    // ---- film grain, after the glass ------------------------------------
    // The display pass's grain lives in the sRGB-encoded domain before the HDR
    // gain; the frame here is linear scRGB with the gain applied. The same
    // luminance weighting and amplitude are reproduced on an sRGB proxy of
    // the pixel and the resulting linear delta is scaled by whatever gain the
    // pixel carries, so the grain reads the same in the SDR mids and never
    // fizzes on a hot HDR core.
    // ---- LIGHT IN THE WATER: volumetric haze + a wide, weak bloom -------
    // The user's reference is a glass of cloudy water with a lamp under it:
    // the WATER glows, brightest near the lamp and fading with distance, so
    // the dark side is never pure black near the light but a soft milky murk,
    // and the bright film bleeds a very wide, very weak wash into the black.
    // Both hang off ONE off-view light position, and that position never sits
    // still: a sum of slow sines, seconds to a minute, so the frame has an
    // idle animation of its own instead of a fixed gradient.
    [branch] if (pp5.y > 0.0005 || pp5.z > 0.0005) {
        float  tq  = pp2.y;
        float  asp = pp0.y / max(pp0.x, 1e-9);           // W/H
        // the lamp, already drifted, straight off the rig -- same sines as
        // before, evaluated once on the CPU instead of once per pixel
        float2 L   = rg0.xy;
        float2 qL  = float2((uv.x - L.x) * asp, uv.y - L.y);
        float  dl  = length(qL);
        float  sdrL = max(pp2.z, 1e-3);
        float  lum0 = dot(ToSRGB(d / sdrL), W);
        // (a) HAZE. Only into the DARK, and it reaches EXACTLY zero a little
        // way out (the exponential has its own floor subtracted), so far from
        // the lamp an off OLED pixel is still off -- the whole frame is never
        // lifted, which is the one thing that would ruin this panel.
        [branch] if (pp5.y > 0.0005) {
            float x  = dl / max(pp6.x, 1e-4);
            float hz = max(exp(-x) - 0.050, 0.0) / 0.950;
            float wd = 1.0 - smoothstep(0.02, 0.50, lum0);
            d += float3(1.00, 0.93, 0.86)
               * (saturate(pp5.y) * 0.16 * hz * hz * wd * sdrL);
        }
        // (b) BLOOM. Two jittered rings of taps at a radius of 100+ px at
        // 1440p: too few taps for a clean blur, which does not matter at a
        // few percent, and the per-pixel angular jitter turns what banding
        // there would be into the grain that is coming anyway. The radius
        // breathes, the gather centre creeps, and the wash is stronger on the
        // lamp's side, so it drifts with the light instead of sitting still.
        [branch] if (pp5.z > 0.0005) {
            float  rad = pp6.y * (1.0 + 0.12 * sin(tq * 0.0197 + 0.9));
            float2 ctr = uv + float2(sin(tq * 0.0131), cos(tq * 0.0173))
                            * (rad * 0.10 * pp0.xy);
            // The user photographed faint concentric RINGS around every bright
            // droplet sitting in the black: two rings of taps at fixed radii
            // ARE two circles, and a point source lights each of them up. So
            // the taps now cover the whole disc -- four radii with Gaussian
            // weights, and the radius itself jittered per pixel -- and a point
            // source blooms into a smooth wash, never a halo with edges.
            float  jit = PHash21(i.pos.xy * 0.37) * 6.2831853;
            float  rj  = 0.80 + 0.40 * PHash21(i.pos.yx * 0.53 + 17.1);
            float3 bl  = float3(0.0, 0.0, 0.0);
            float  bw  = 0.0;
            [unroll] for (int m = 0; m < 6; m++) {
                float  an = (float)m * 1.0471976 + jit;
                float2 un = float2(cos(an), sin(an));
                [unroll] for (int k = 0; k < 4; k++) {
                    float  fr = (0.18 + 0.27 * (float)k) * rj;
                    float  w  = exp(-fr * fr * 1.6);
                    bl += max(Src.SampleLevel(linearClamp, ctr + un * (fr * rad * pp0.xy), 0).rgb, 0.0) * w;
                    bw += w;
                }
            }
            bl /= max(bw, 1e-4);
            float wd = 1.0 - smoothstep(0.10, 0.75, lum0);
            float lw = 0.75 + 0.50 * exp(-dl / max(pp6.x * 1.5, 1e-4));
            d += bl * (saturate(pp5.z) * 0.20 * wd * lw);
        }
    }

)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // =====================================================================
    // THE LID (task V2)  -- self-contained; ONE packed constant (rg4), own
    // branch, nothing above or below it touched. The user's constraints are
    // NEGATIVE ones: no visible instrument, no dish rim, no grid, no UI
    // marks, no dark corners -- the oil stays full-bleed. So there is no mask
    // and no border anywhere in here. Every term is ADDITIVE and full-frame.
    //
    // The excuse for all of it is a sheet of glass or plastic lying over the
    // dish: what you see is the bright film reflected INSIDE that sheet
    // (ghosts and ring ghosts), the lamp smeared across its surface (sheen
    // and glint), the interference colours of the thin oil film on it (iris),
    // and the wobble a cheap sheet gives its own reflections (refract).
    // Reference: the LAPD optics sheet, endgoal-ref-13.
    //
    // NOTHING HERE IS AT A FIXED SCREEN POSITION. Every position is built
    // from the rig: the lamp (rg0.xy, always drifting), the lens axis
    // (rg0.zw) and the lid's own wander (rg4.x, four periods between three
    // and twelve minutes). At each of the rig's occasional readjustments the
    // lid is shoved to a new resting offset on the same spring the focus
    // rides, so the ghosts, the sheen and the glint all arrive together --
    // never one effect moving alone.
    // =====================================================================
    [branch] if (rg4.z > 0.5 || rg4.w > 0.5) {
        float  sdrL = max(pp2.z, 1e-3);
        float  aspL = pp0.y / max(pp0.x, 1e-9);            // W/H
        // unpack: see the rg4 comment in the cbuffer above
        float2 lidO = LidU12(rg4.x) - 0.5;                 // uv, +-0.5
        float2 lidB = LidU12(rg4.y);
        float  lidA = lidB.x * 8.0 - 4.0;                  // rad
        float  lidW = lidB.y * 24.0;                       // refract, px here
        float3 lidC = LidU8(rg4.z);                        // ghost rings sheen
        float4 lidD = LidU7764(rg4.w);                     // glint iris shPx spread
        float  lidS = 8.0 + lidD.z * 1392.0;               // sheen px, this res
        float  lidP = lidD.w * 2.0;                        // ghost spread
        // The master is already folded into every amplitude on the CPU.
        const float M = 1.0;
        // Where the sheet's optical centre sits: the lens axis, carried by
        // the lid's own wander.
        float2 ctrL = float2(rg0.z, rg0.w) + lidO * 0.45;
        // ...and where the lamp's reflection lands in it: the lamp mirrored
        // through that centre and pulled back inside the frame, so it is
        // always somewhere on screen and always moving with the lamp.
        float2 lampR = ctrL - (float2(rg0.x, rg0.y) - ctrL) * 0.55 + lidO * 0.75;
        // The peak channel, never luminance: this film is a saturated
        // magenta whose luminance is a third of its red, and a luminance
        // test calls the brightest thing in the frame dark.
        float  ownL = max(d.r, max(d.g, d.b)) / sdrL;
        // Reflections in a cover show up in the DARK; on the bright film
        // itself there is nothing to see. Not a hard gate -- a lean.
        float  into = lerp(0.30, 1.0, 1.0 - smoothstep(0.25, 0.95, ownL));

        // ---- the sheet is not flat: its reflections wobble --------------
        // Only the REFLECTIONS wobble, not the transmitted picture: warping
        // the picture would mean resampling the frame and undoing the depth
        // of field the camera pass just computed. Physically it is also the
        // right half -- light bounced inside the sheet crosses its uneven
        // faces twice, transmitted light barely at all.
        float2 wob = float2(sin(uv.y * 7.3 + lidA * 3.1 + pp2.y * 0.047),
                            sin(uv.x * 6.1 - lidA * 2.3 + pp2.y * 0.031))
                   * (lidW * pp0.xy);

        // ---- GHOSTS: offset, dimmed, slightly magnified copies ----------
        // The classic lens-flare chain: each internal bounce puts a copy of
        // the bright field on the line from the lamp's reflection through the
        // optical centre, at its own scale. Tinted by the coating it bounced
        // off -- amber, green, magenta, which is what the LAPD sheet shows.
        [branch] if (lidC.x > 0.0005) {
            float3 gsum = float3(0.0, 0.0, 0.0);
            [unroll] for (int gi = 0; gi < 3; gi++) {
                float  fk = lerp(-0.42, 0.80, (float)gi * 0.5) * (0.4 + lidP);
                float2 guv = ctrL + (uv - ctrL) * (1.0 + fk * 0.55)
                           - (lampR - ctrL) * fk + wob;
                float3 sp  = Src.SampleLevel(linearClamp, guv, 0).rgb;
                // Same knee as halation, and on the PEAK CHANNEL for the same
                // reason: only what is genuinely bright bounces.
                float  ex  = smoothstep(0.55, 1.15,
                                        max(sp.r, max(sp.g, sp.b)) / sdrL);
                // fade where the copy runs off the plate, or the clamp would
                // paint a static-looking band along the edge
                float2 fe = smoothstep(0.0, 0.05, guv) * smoothstep(1.0, 0.95, guv);
                // Desaturated on purpose. A fully saturated green bounce over
                // a magenta film comes out OLIVE and the black masses turn to
                // mud; these are coatings, not filters.
                float3 tint = (gi == 0) ? float3(1.00, 0.70, 0.38)
                           : ((gi == 1) ? float3(0.60, 0.95, 0.80)
                                        : float3(0.95, 0.66, 1.00));
                // ...and the green bounce is the weakest of the three, for
                // the same reason.
                float  gw   = (gi == 0) ? 1.00 : ((gi == 1) ? 0.55 : 0.40);
                gsum += sp * (ex * fe.x * fe.y * gw) * tint;
            }
            d += gsum * (M * lidC.x * 0.085 * into);
        }

        // ---- RING GHOSTS: the concentric coloured arcs ------------------
        // A reflection off a curved element images the field as a ring, not
        // as a copy. Three annuli of different width and colour about a
        // centre that is the far end of the ghost chain, so they travel with
        // the ghosts. Weighted into the dark, never a drawn circle over the
        // oil.
        [branch] if (lidC.y > 0.0005) {
            float2 rc = ctrL - (lampR - ctrL) * (0.55 + 0.30 * lidP);
            float2 q  = (uv - rc) * float2(aspL, 1.0);
            float  rr = length(q) / max(0.34 + 0.22 * lidP, 1e-4);
            float  b1 = exp(-64.0 * (rr - 0.55) * (rr - 0.55));
            float  b2 = exp(-192.0 * (rr - 0.74) * (rr - 0.74));
            float  b3 = exp(-36.0 * (rr - 0.96) * (rr - 0.96));
            float3 rcol = float3(0.30, 1.00, 0.68) * b1
                        + float3(1.00, 0.80, 0.30) * b2
                        + float3(1.00, 0.26, 0.12) * b3;
            d += rcol * (M * lidC.y * 0.030 * into * sdrL);
        }

        // ---- SHEEN: the broad soft specular off the cover ---------------
        // A wide anisotropic smear about the lamp's reflection, its long axis
        // turning with the sheet. This is the "warm veiling flare across the
        // field" of the reference -- deliberately enormous and deliberately
        // weak, so it reads as glass in front of the picture rather than as a
        // light in it.
        float sheenE = 0.0;
        [branch] if (lidC.z > 0.0005 || lidD.y > 0.0005) {
            float  rs = max(lidS * pp0.y, 1e-4);          // px at this res -> uv
            float2 q  = (uv - lampR) * float2(aspL, 1.0);
            float  ca = cos(lidA), sa = sin(lidA);
            float2 e2 = float2(q.x * ca + q.y * sa, -q.x * sa + q.y * ca);
            e2.x /= 2.7;                                   // drawn out along the sheet
            sheenE = exp(-dot(e2, e2) / (rs * rs));
            d += float3(1.00, 0.88, 0.70)
               * (M * lidC.z * 0.075 * sheenE * sdrL);
        }

        // ---- IRIDESCENCE: the thin oil film on the cover ----------------
        // Interference colours: a slowly turning phase ramp read through a
        // cosine palette, living only where the sheet is catching light, so
        // it appears as a colour sweep across the sheen and nowhere else.
        [branch] if (lidD.y > 0.0005) {
            float2 q  = (uv - ctrL) * float2(aspL, 1.0);
            float  ph = 7.0 * (q.x * cos(lidA) + q.y * sin(lidA))
                      + 2.6 * sin(q.y * 3.3 - lidA * 1.7)
                      + pp2.y * 0.021;
            float3 ir = 0.5 + 0.5 * cos(ph + float3(0.0, 2.0944, 4.1888));
            d += (ir - 0.333) * (M * lidD.y * 0.055 * sheenE * sdrL);
        }

        // ---- GLINT: the lamp itself, seen in the cover ------------------
        // A soft core with a wide amber halo. The core is kept soft and
        // modest on purpose: it is the brightest thing this block adds and it
        // lives on an OLED, so it is a blob that wanders hundreds of pixels
        // over the minutes, never a star pinned to a pixel.
        [branch] if (lidD.x > 0.0005) {
            float  dd   = length((uv - lampR) * float2(aspL, 1.0));
            float  core = exp(-(dd * dd) / (0.030 * 0.030));
            float  halo = exp(-dd / 0.19);
            d += (float3(1.00, 0.90, 0.72) * (core * 0.55)
                + float3(1.00, 0.56, 0.20) * (halo * 0.13))
                 * (M * lidD.x * sdrL);
        }
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // ---- FILM OVERLAY ARTEFACTS -----------------------------------------
    // Hairs caught in the gate, dust, fine scratches and the odd light leak.
    // Everything is authored in px at 1440p (P below) and is therefore the
    // same size relative to the picture at any output resolution, and
    // everything is ADDITIVE and weighted toward the DARK pixels: a speck on
    // the black is a little star, the same speck on the film is nothing.
    //
    // Nothing sits still. Each artefact's seed is hashed from a QUANTISED
    // time -- floor(t / rate) for the hairs, slower multiples of it for the
    // scratches and the leak, the 24 fps "film frame" for the dust -- so the
    // dust flickers frame to frame, a hair sticks for a few seconds and is
    // gone, and a scratch persists for a stretch, drifts and disappears.
    [branch] if (pp3.x > 0.0005 || pp3.y > 0.0005 ||
                 pp3.z > 0.0005 || pp3.w > 0.0005) {
        float  sc  = max(pp4.w, 1e-4);
        float2 P   = i.pos.xy / sc;                       // px at 1440p
        float  Hy  = 1440.0;
        float  Wx  = 1440.0 * (pp0.y / max(pp0.x, 1e-9)); // frame width, same units
        float  t   = pp2.y;
        float  per = max(pp4.x, 0.25);
        float  ff  = floor(t * 24.0);                     // the film frame
        float3 add = float3(0.0, 0.0, 0.0);

        // DUST: sparse bright points 1-3 px across at 1440p, a new population
        // every film frame. A cell of a coarse grid may carry one speck,
        // placed well inside it so a single-cell lookup is exact.
        [branch] if (pp3.x > 0.0005) {
            const float CELL = 42.0;
            float2 cid = floor(P / CELL);
            float2 fp  = P - cid * CELL;
            float  h0  = PHash21(cid * 1.7 + frac(ff * 0.0371) * 511.0);
            [branch] if (h0 > 1.0 - 0.10 * saturate(pp3.x)) {
                float  h1 = PHash21(cid + 19.3 + h0 * 37.0);
                float  h2 = PHash21(cid + 71.9 + h0 * 11.0);
                float  h3 = PHash21(cid + 3.71 + h0 * 53.0);
                float2 ct = float2(0.2 + 0.6 * h1, 0.2 + 0.6 * h2) * CELL;
                float  r  = 0.6 + 1.4 * h3;
                float  dd = length(fp - ct) / r;
                add += (0.30 * saturate(pp3.x) * (0.35 + 0.65 * h3))
                     * exp(-dd * dd * 1.6);
            }
        }
        // HAIRS: three slots, each either empty or carrying one curly strand
        // for this reel. The strand is the set of points a fixed distance
        // from a wobbling line (two sines in the along-strand coordinate),
        // with the distance divided by sqrt(1 + slope^2) so the hair keeps
        // its thickness through every curl.
        [branch] if (pp3.y > 0.0005) {
            float tq  = floor(t / per);
            float ph  = frac(t / per);
            float env = smoothstep(0.0, 0.12, ph) * (1.0 - smoothstep(0.78, 1.0, ph));
            [unroll] for (int k = 0; k < 3; k++) {
                float fk = (float)k;
                float s0 = PHash21(float2(tq * 1.13 + 5.7, fk * 9.31 + 2.1));
                if (s0 > saturate(pp3.y)) continue;
                float2 C  = float2(PHash21(float2(tq + 3.3, fk * 7.7)) * Wx,
                                   PHash21(float2(tq + 7.1, fk * 5.9)) * Hy);
                float  an = PHash21(float2(tq + 11.3, fk * 2.7)) * 6.2831853;
                float  L  = 30.0 + 120.0 * PHash21(float2(tq + 17.9, fk * 4.1));
                float2 dr = float2(cos(an), sin(an));
                float2 nr = float2(-dr.y, dr.x);
                float2 rl = P - C;
                float  tt = dot(rl, dr);
                if (abs(tt) > L * 0.5 + 3.0) continue;
                float  w1 = 0.055 + 0.050 * s0;
                float  a1 = 3.0 + 6.0 * PHash21(float2(tq + 23.1, fk * 6.3));
                float  a2 = 5.0 + 9.0 * PHash21(float2(tq + 29.7, fk * 8.7));
                float  cp = PHash21(float2(tq + 37.3, fk * 1.9)) * 6.2831853;
                float  cv = a1 * sin(tt * w1 + cp) + a2 * sin(tt * 0.021 - cp * 0.7);
                float  dv = a1 * w1 * cos(tt * w1 + cp)
                          + a2 * 0.021 * cos(tt * 0.021 - cp * 0.7);
                float  ds = abs(dot(rl, nr) - cv) * rsqrt(1.0 + dv * dv);
                float  wd = 0.6 + 0.6 * PHash21(float2(tq + 41.7, fk * 3.1));
                // the gate flutters: the strand breathes a little every frame
                float  jt = (PHash21(float2(ff, fk * 13.1)) - 0.5) * 0.7;
                float  en = 1.0 - smoothstep(L * 0.40, L * 0.5, abs(tt));
                add += (1.0 - smoothstep(wd, wd + 1.5, ds + jt)) * en * env
                     * (0.09 * (0.5 + 0.5 * saturate(pp3.y)));
            }
        }
        // SCRATCHES: four slots on a slower clock, near-vertical, faintly
        // wavy, drifting sideways, each covering part of the height.
        [branch] if (pp3.z > 0.0005) {
            float tq  = floor(t / (per * 3.0));
            float ph  = frac(t / (per * 3.0));
            float env = smoothstep(0.0, 0.10, ph) * (1.0 - smoothstep(0.82, 1.0, ph));
            [unroll] for (int j = 0; j < 4; j++) {
                float fj = (float)j;
                float s0 = PHash21(float2(tq * 1.7 + 3.3, fj * 7.1 + 1.3));
                if (s0 > saturate(pp3.z)) continue;
                float xc = PHash21(float2(tq + 13.7, fj * 2.9)) * Wx
                         + (PHash21(float2(tq + 19.1, fj * 5.3)) - 0.5) * 6.0 * t
                         + 2.0 * sin(P.y * 0.004 + s0 * 6.2831853);
                float wd = 0.35 + 0.80 * PHash21(float2(tq + 23.3, fj * 3.7));
                float y0 = PHash21(float2(tq + 29.9, fj * 11.3)) * Hy * 0.6;
                float y1 = y0 + Hy * (0.35 + 0.65 * PHash21(float2(tq + 31.1, fj * 4.7)));
                float yv = smoothstep(y0 - 40.0, y0 + 40.0, P.y)
                         * (1.0 - smoothstep(y1 - 60.0, y1 + 60.0, P.y));
                // The user, on the first version: "no film has a line like
                // that" -- a clean 1-px rule down the frame. A real gate
                // scratch is faint, its edges are soft, and it is BROKEN along
                // its length: the emulsion is torn in stretches, not cut, so
                // it reads as a flickering dashed thread, never a ruled line.
                float seg = PHash21(float2(floor(P.y / 14.0) + tq * 3.1, fj * 9.7 + ff * 0.13));
                float brk = smoothstep(0.35, 0.75, seg);           // ~55% of segments lit
                add += (1.0 - smoothstep(wd, wd + 2.6, abs(P.x - xc))) * yv * env * brk
                     * (0.5 + 0.5 * PHash21(float2(ff, fj * 17.3)))
                     * (0.022 * (0.4 + 0.6 * saturate(pp3.z)));
            }
        }
        // LIGHT LEAK: a COLOURED wash entering from one edge -- a warm core
        // (red through orange to yellow) with a cool green/teal fringe just
        // outside it, in soft bands along the edge -- swelling and dying on
        // its own slow clock, and not returning every time.
        [branch] if (pp3.w > 0.0005) {
            float tq  = floor(t / (per * 5.0));
            float ph  = frac(t / (per * 5.0));
            float s0  = PHash21(float2(tq * 2.7 + 1.9, 4.21));
            float on  = step(s0, saturate(pp3.w * 0.8 + 0.2));
            float env = sin(saturate(ph) * 3.14159265);
            env = on * env * env;
            float sd  = PHash21(float2(tq + 5.1, 8.3));
            float2 q  = float2(P.x / Wx, P.y / Hy);
            float  u  = (sd < 0.25) ? q.x : ((sd < 0.5) ? 1.0 - q.x
                      : ((sd < 0.75) ? q.y : 1.0 - q.y));
            float  v  = (sd < 0.5) ? q.y : q.x;                  // along the edge
            float  bd = 0.75 + 0.25 * sin(v * 9.0 + t * 0.05 + s0 * 6.2831853)
                             * sin(v * 3.0 - t * 0.03);
            float  core = exp(-u * u * 34.0);                    // warm, on the edge
            float  frng = exp(-(u - 0.16) * (u - 0.16) * 60.0);  // cool, just outside
            float3 warm = lerp(float3(1.00, 0.22, 0.06), float3(1.00, 0.72, 0.18),
                               PHash21(float2(tq + 9.7, 2.3)));
            float3 cool = float3(0.10, 0.85, 0.65);
            add += (warm * core + cool * (frng * 0.45))
                 * (bd * env * 0.20 * saturate(pp3.w));
        }
        // the whole overlay leans into the dark: near-invisible on the film
        float sdrA = max(pp2.z, 1e-3);
        float lumA = dot(ToSRGB(d / sdrA), W);
        d += add * (sdrA * lerp(0.10, 1.0, 1.0 - smoothstep(0.20, 0.80, lumA)));
    }
    // ---- film grain, after the glass ------------------------------------
    [branch] if (pp1.y > 0.0005) {
        float2 gc  = floor(i.pos.xy / max(pp1.z, 0.25));
        // film_grain_fps patterns per second (24 by default: once per frame of
        // the stock, a whole number of panel refreshes each) times the speed.
        float  tq  = floor(pp2.y * max(pp6.w, 1.0) * max(pp1.w, 0.0));
        float2 tj  = frac(tq * float2(0.1031, 0.0973)) * 733.0;
        float  n0  = PHash21(gc + tj);
        float3 nz  = float3(n0, n0, n0);
        [branch] if (pp2.x > 0.0005) {
            nz = lerp(nz, float3(n0, PHash21(gc + tj + 37.71),
                                     PHash21(gc + tj + 91.37)), saturate(pp2.x));
        }
        d = Emulsion(d, nz, pp1.y, max(pp2.z, 1e-3));
    }
    // ---- film_noise: the finer, faster layer under the stock's grain -----
    // A new pattern every FRAME OF THE STOCK (film_grain_fps, ignoring the
    // grain speed multiplier), one px at 1440p by default: the emulsion's
    // fizz as against the stock's grain structure, on the same film cadence.
    [branch] if (pp4.y > 0.0005) {
        float2 nc = floor(i.pos.xy / max(pp4.z, 0.25));
        float  tq = floor(pp2.y * max(pp6.w, 1.0));
        float2 tj = frac(tq * float2(0.0891, 0.1237)) * 557.0;
        float  n0 = PHash21(nc + tj + 211.7);
        d = Emulsion(d, float3(n0, n0, n0), pp4.y, max(pp2.z, 1e-3));
    }
    // ---- film_stock: the stock's own colour ------------------------------
    // Lifted teal shadows, warm highlights, a slightly different curve per
    // channel -- the cross-process feel. Done on the sRGB proxy so it reads
    // as a grade and not as a gain, and scaled by the pixel's own HDR gain so
    // a hot core keeps its level. The shadow lift is deliberately tiny: at
    // the shipped strength it is a fraction of a nit on a black pixel.
    [branch] if (pp5.x > 0.0005) {
        float  k    = saturate(pp5.x);
        float  sdrS = max(pp2.z, 1e-3);
        float3 base = d / sdrS;
        float3 e    = ToSRGB(base);
        float  l    = dot(e, W);
        // ...but a pixel that is TRUE BLACK keeps its black: the lift ramps in
        // from just above zero, so an off OLED pixel stays off and only the
        // shadows that already carry light get the teal.
        float3 sh   = float3(-0.004, 0.008, 0.022) * (1.0 - smoothstep(0.0, 0.40, l))
                    * smoothstep(0.004, 0.060, l);
        float3 hi   = float3( 0.045, 0.012, -0.030) * smoothstep(0.30, 1.0, l);
        float3 e2   = max(e + (sh + hi * e) * k, 0.0);
        float3 l0   = ToLinear(e), l2 = ToLinear(e2);
        float  gain = clamp(dot(base, W) / max(dot(l0, W), 1e-4), 1.0, 16.0);
        d = max(d + (l2 - l0) * gain * sdrS, min(d, 0.0));
    }
    // ---- OUTPUT DITHER (item V0) -----------------------------------------
    // The frame leaves here as FP16, but the panel quantises it to 10 bits in
    // a perceptual domain, and on a big saturated flat -- which is most of
    // this look -- the user can see the steps. So the noise goes in where the
    // quantiser is: on the sRGB-encoded proxy, half an LSB of a 10-bit signal,
    // converted back to linear and scaled by whatever HDR gain this pixel
    // carries so a hot core is dithered by the same half step as a mid tone.
    //
    // INTERLEAVED-GRADIENT noise rather than a hash: its spectrum is close to
    // blue, so the pattern sits above the eye's peak sensitivity instead of
    // clumping the way white noise does, and it costs three instructions. A
    // new phase every frame, and a different one per channel, because the
    // channels band independently on a saturated colour.
    //
    // It FADES OUT into true black. A dither that lifted an off OLED pixel
    // would be a far worse bug than the band it fixed.
    [branch] if (pp7.x > 0.0005) {
        float  sdrD = max(pp2.z, 1e-3);
        float3 base = d / sdrD;
        float3 e    = ToSRGB(base);
        float2 jt   = frac(pp2.y * float2(37.0, 61.0)) * 61.0;
        float3 n;
        n.x = frac(52.9829189 * frac(dot(i.pos.xy + jt,          float2(0.06711056, 0.00583715))));
        n.y = frac(52.9829189 * frac(dot(i.pos.xy + jt + 23.71,  float2(0.06711056, 0.00583715))));
        n.z = frac(52.9829189 * frac(dot(i.pos.xy + jt + 51.37,  float2(0.06711056, 0.00583715))));
        float  lq = dot(e, W);
        // ...and the fade starts well ABOVE the first code, not at it. Banding
        // is a mid-tone and flat-highlight problem; the deep shadow has
        // nothing to dither and everything to lose, so below ~0.2 nits this
        // term is exactly zero and the OLED's off pixels stay off.
        float  amp = pp7.x * (1.0 / 1023.0) * smoothstep(0.010, 0.040, lq);
        float3 e2 = max(e + (n - 0.5) * amp, 0.0);
        float3 l0 = ToLinear(e), l2 = ToLinear(e2);
        float  g  = clamp(dot(base, W) / max(dot(l0, W), 1e-4), 1.0, 16.0);
        d = max(d + (l2 - l0) * g * sdrD, min(d, 0.0));
    }
    return float4(d, 1.0);
}
)hlsl";

// The M1 HDR diagnostic gradient, kept behind the --gradient flag.
static const char* kGradientSrc = R"hlsl(
cbuffer Consts : register(b0) {
    float2 res;
    float  time;
    float  hdrOn;
    float  page;    // 0 = M1 gradient; 1..3 = calibration quiz pages
};

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 HueToRGB(float h) {
    float r = abs(h * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(h * 6.0 - 2.0);
    float b = 2.0 - abs(h * 6.0 - 4.0);
    return saturate(float3(r, g, b));
}

// Quiz page 1 — black level. Ten vertical strips at near-black gray levels
// (scRGB linear; 1.0 = 80 nits). Bottom edge of each strip shows strip-number
// notches. User reports the lowest-numbered strip visible -> shadow_floor.
float4 PageBlackLevel(float2 uv) {
    float lv[10] = { 0.002, 0.004, 0.006, 0.008, 0.010,
                     0.015, 0.020, 0.030, 0.050, 0.080 };
    int i = min(9, (int)(uv.x * 10.0));
    float fx = frac(uv.x * 10.0);
    float v = (fx > 0.04 && fx < 0.96) ? lv[i] : 0.0;
    if (uv.y < 0.04) {   // notch row: strip index+1 white cells out of 10
        int cell = (int)(frac(uv.x * 10.0) * 10.0);
        v = (cell < i + 1) ? 0.5 : 0.0;
    }
    return float4(v, v, v, 1.0);
}

// Quiz page 2 — saturation. Rows = hues, columns = CSS-saturate factor
// applied in gamma space (like the app's post filter), then gamma-decoded.
// User reports the most pleasing column (1-6, left to right).
float4 PageSaturation(float2 uv) {
    float hues[4] = { 0.0, 0.08, 0.33, 0.60 };
    float sats[6] = { 0.8, 1.0, 1.2, 1.4, 1.6, 2.0 };
    int r = min(3, (int)(uv.y * 4.0));
    int c = min(5, (int)(uv.x * 6.0));
    float3 col = HueToRGB(hues[r]) * 0.6;
    float l = dot(col, float3(0.2126, 0.7152, 0.0722));
    col = max(l + (col - l) * sats[c], 0.0);
    if (frac(uv.x * 6.0) < 0.02 || frac(uv.y * 4.0) < 0.04) col = 0.0;
    return float4(pow(col, 2.2), 1.0);
}

// Quiz page 3 — blowout ladder. Horizontal bands of near-white at rising
// nits (scRGB: 1.0 = 80 nits). Band 1 is at the BOTTOM. User reports the
// "almost blown but not painful" band -> peak_nits / max_brightness.
float4 PageBlowout(float2 uv) {
    float nits[10] = { 80, 150, 240, 300, 400, 500, 600, 800, 1000, 1500 };
    int i = min(9, (int)(uv.y * 10.0));
    float v = nits[i] / 80.0;
    if (hdrOn < 0.5) v = min(v, 1.0);
    if (frac(uv.y * 10.0) < 0.03) v = 0.0;
    return float4(v, v * 0.98, v * 0.95, 1.0);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy / res;
    int p = (int)(page + 0.5);
    if (p == 1) return PageBlackLevel(uv);
    if (p == 2) return PageSaturation(uv);
    if (p == 3) return PageBlowout(uv);
    if (uv.y > 0.88) {
        float levels[5] = { 1.0, 2.0, 4.0, 8.0, 12.5 };
        int i = min(4, (int)(uv.x * 5.0));
        float fx = frac(uv.x * 5.0);
        float v = (fx > 0.02 && fx < 0.98) ? levels[i] : 0.0;
        if (hdrOn < 0.5) v /= 12.5;
        return float4(v, v, v, 1.0);
    }
    float y = uv.y / 0.88;
    float hue = frac(uv.x + time * 0.03);
    float3 c = HueToRGB(hue);
    float ramp = lerp(4.0, 0.02, pow(y, 1.5));
    float band = 0.5 + 0.5 * sin(6.28318 * (y * 2.0 - time * 0.25));
    float brightness = ramp * (0.6 + 0.8 * band * band);
    if (hdrOn < 0.5) brightness /= 5.6;
    return float4(c * brightness, 1.0);
}
)hlsl";
