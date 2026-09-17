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
    float4 laMen;        // meniscus halo colour, rgb
};
// xy = centre uv, z = radius, w = field weight (+1 oil, negative = hole)
// rgb of .b = flat fill colour
struct AcidBlobGPU { float4 a; float4 b; };
StructuredBuffer<AcidBlobGPU> AcidBlobs : register(t1);

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

// Procedural bubble swarm. A jittered cellular layer of round droplets with a
// wide (squared-hash) size range: hundreds of bubbles of every size for ~20
// hashes per pixel, where the same thing in metaballs would cost hundreds of
// loop iterations. Returns the signed distance to the nearest droplet in
// p-space units (negative inside).
float AcidSwarm(float2 p, float scale, float density, float clumpAmt,
                float rmin, float rmax) {
    float2 pc = p * scale;
    float2 base = floor(pc);
    float d = 1e9;
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
            d = min(d, length(pc - (c + 0.18 + 0.64 * j)) - r);
        }
    }
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
    float4 ikP4;        // x motionOpacity, y -, z -, w -
    float4 ikPaper;     // paper / background colour
    float4 ikTintThin;  // inverted: light through a thin veil
    float4 ikTintThick; // inverted: light out of an opaque core
};

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
    float3 tint = lerp(ikTintThin.rgb, ikTintThick.rgb,
                       smoothstep(min(ikP3.x, 0.99), 1.0, op));
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
    float2 uv = i.uv;
#ifdef LIQUID_ACID
    // ---- oil metaball field (evaluated first: it refracts the ink sample) ----
    const float aspect = laP6.w;
    float2 pp = float2(uv.x * aspect, uv.y);
    float  field = 0.0;
    float2 grad  = float2(0.0, 0.0);
    float  colW = 0.0;
    float3 colSum = float3(0.0, 0.0, 0.0);
    int nb = (int)laP0.x;
    const float thresh = laP0.y;
    [loop]
    for (int bi = 0; bi < nb; bi++) {
        AcidBlobGPU B = AcidBlobs[bi];
        float2 q  = pp - float2(B.a.x * aspect, B.a.y);
        float  d2 = dot(q, q);
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
        grad  += (-6.0 * u2 / s2) * q * B.a.w;
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
    float3 oilBase = (colW > 1e-9) ? colSum / colW : laOil[0].rgb;
    float  gl   = length(grad) + 1e-6;
    float  sdf  = clamp((field - thresh) / gl, -0.25, 0.25);  // >0 inside the oil
    float  aaF  = fwidth(field) * laP0.w + 1e-4;
    float  cov  = smoothstep(thresh - aaF, thresh + aaF, field);
    // Refraction: the ink is seen through thinning oil near the rim, so shift
    // the ink lookup along the field gradient inside a narrow edge band.
    float  eb = sdf / max(laP1.x * 7.0, 1e-4);
    float  edgeB = exp(-eb * eb);
    uv = saturate(uv + (grad / gl) * (laP1.w * edgeB) * float2(1.0 / aspect, 1.0));
#endif
    float3 C = Dye.SampleLevel(linearClamp, uv, 0).rgb;
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
    // ---- OIL: flat fill, thin dark rim just inside the isoline ----------
    float3 oilC = oilBase;
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
    float rimHW = max(laP1.x * rimMul, 1e-5);
    float menHW = max(laP2.y * rimMul, 1e-5);
    float rimCtr = laP12.z > 0.5 ?  rimHW : laP1.y;
    float menCtr = laP12.z > 0.5 ? -menHW : -laP7.z;
    // dark rim: a Gaussian band centred just INSIDE the boundary, forced to
    // zero deep inside a merged mass so no concentric rings appear there.
    float rimX = (sdf - rimCtr) / rimHW;
    float rimB = exp(-rimX * rimX)
               * (1.0 - smoothstep(thresh * 1.7, thresh * 3.4, field));
    oilC *= 1.0 - saturate(laP1.z) * rimB;

    float3 col = lerp(inkC, oilC, cov);

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
        if (laP8.x > 0.002) {
            float2 sp = pp + warp * 0.030 + st * float2(0.0040, -0.0030);
            // two octaves 2.7x apart so the sizes run from ~1 px to ~2% of the
            // frame height; the fine one stays sparse or the oil reads as dust
            float  sd = min(AcidSwarm(sp, laP9.x, laP8.z, laP10.x, laP9.z, laP9.w),
                            AcidSwarm(sp + 7.31, laP9.x * 2.7, laP8.z * 0.12, laP10.x,
                                      laP9.z, laP9.w));
            float  scov = 1.0 - smoothstep(-aaS, aaS, sd);
            float  sx = sd / (aaS * 2.4);
            float  srim = exp(-sx * sx);
            float  mask = smoothstep(0.004, 0.030, sdf) * laP8.x;   // inside the oil only
            // a trapped water droplet shows the INK through the oil film
            col = lerp(col, inkC * (1.0 - laP10.y), scov * mask);
            col *= 1.0 - laP8.w * srim * mask;
        }
        if (laP8.y > 0.002) {
            float2 sp = pp * 1.31 - warp * 0.024 + st * float2(-0.0031, 0.0042) + 41.7;
            float  sd = min(AcidSwarm(sp, laP9.y, laP8.z, laP10.x, laP9.z, laP9.w),
                            AcidSwarm(sp + 3.17, laP9.y * 2.7, laP8.z * 0.12, laP10.x,
                                      laP9.z, laP9.w));
            float  scov = 1.0 - smoothstep(-aaS, aaS, sd);
            float  sx = sd / (aaS * 2.4);
            float  srim = exp(-sx * sx);
            float  mask = smoothstep(0.004, 0.030, -sdf) * laP8.y;  // open ink only
            col = lerp(col, laOil[3].rgb, scov * mask);
            col *= 1.0 - laP8.w * srim * mask;
        }
    }

    // ---- meniscus: thin BRIGHT ink-coloured halo just outside the rim ----
    // In the references this is the brightest thing in the frame (cyan on
    // ref 1) and it is what separates the oil from the ink. Painted with the
    // ramp's bright stop so it reads even over black ink.
    if (laP2.x * haloMul > 0.002) {
        float hx = (sdf - menCtr) / menHW;
        float halo = exp(-hx * hx);
        // sdf = (field - thresh) / |grad| is only a distance where |grad| is
        // strong. On a broad low-gradient plateau that never reaches the
        // threshold, a tiny field deficit divided by a tiny gradient still
        // lands inside the halo band, painting a fuzzy blob-shaped glow with
        // no oil under it. Real blob surfaces have |grad| >~ 2 (it scales as
        // 1/radius, and the largest discs here are ~0.4), so gate on it.
        halo *= smoothstep(0.5, 1.5, gl);
        col = lerp(col, laMen.rgb, saturate(halo * laP2.x * haloMul));
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
    col += (AcidHash21(floor(i.pos.xy / max(laP5.w, 1.0)) + frac(laP6.z) * 913.7) - 0.5)
         * grainAmp;
    C = saturate(col);
    // HDR: the oil is a flat fill, so give it its own highlight level rather
    // than inheriting the ink's. Keep the hot part small (ABL): the rim band.
    if (laP7.x > 0.001) m = lerp(m, laP7.x, cov);
    if (laP7.y > 0.001) m = max(m, laP7.y * rimB * cov);
#endif

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
    float gain = 1.0;
    if (peakGain > 1.001) {
        float t = smoothstep(knee, max(capBright, knee + 0.01), m);
        gain = lerp(1.0, peakGain, t * t);
    }
    return float4(lin * sdrScale * gain, 1.0);
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
