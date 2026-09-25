
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
