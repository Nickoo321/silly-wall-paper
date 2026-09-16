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
[numthreads(8, 8, 1)]
void CSSplatDye(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)dims)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(dims);
    float2 p = uv - point_;
    p.x *= aspect;
    float3 splat = exp(-dot(p, p) / radius) * color;
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
    float3 C = Dye.SampleLevel(linearClamp, i.uv, 0).rgb;
    // Raw dye intensity, before shading/filters/clamps: drives the HDR
    // highlight expansion below so it can see how hot the dye really is
    // (the clamped, filtered colour can't exceed 1.0 any more).
    float m = max(C.r, max(C.g, C.b));
    if (shading > 0.5) {
        float3 L = Dye.SampleLevel(linearClamp, i.uv - float2(texelSize.x, 0), 0).rgb;
        float3 R = Dye.SampleLevel(linearClamp, i.uv + float2(texelSize.x, 0), 0).rgb;
        float3 T = Dye.SampleLevel(linearClamp, i.uv - float2(0, texelSize.y), 0).rgb;
        float3 B = Dye.SampleLevel(linearClamp, i.uv + float2(0, texelSize.y), 0).rgb;
        float dx = length(R) - length(L);
        float dy = length(B) - length(T);
        float3 n = normalize(float3(dx, dy, length(texelSize)));
        float diffuse = clamp(dot(n, float3(0, 0, 1)) + 0.7, 0.7, 1.0);
        C *= diffuse;
    }
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
