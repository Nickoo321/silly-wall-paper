
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
    // rg1.xy used to be the tilt, which this pass never read (the display
    // pass gets it through its acid slot). They are now two packs of four
    // 6-bit fields (BDU6, most significant first; unused fields are 0),
    // fixed for briefs BO and BN so neither repacks the other:
    //   x  artefact_lum_gate : 6 | corner_warp : 6 | corner_warp_r : 6 | bloom_warmth : 6
    //   y  glass_streaks : 6 | halation_threshold : 6 | spare : 6 | spare : 6
    // corner_warp_r is 0.4 + 0.5 * field; it and glass_streaks are written 0
    // unless the warp / the lid is on (fluid.cpp). rg1.zw are brief BM's lid
    // scratches, packed like rg4.z. Both are exactly 0 when the scratches
    // are off, which is their only branch.
    //   z  amount : 8 | density : 8 | len : 8
    //   w  corner : 8 | soft : 8 | tint : 8
    float4 rg1;   // x BO/BN pack  y BN pack  z scratch A  w scratch B
    // rg2.w is brief BD's four knobs, PACKED the way the lid packs its own
    // (b0's 32 constants are full and the root signature is at the 64-DWORD
    // limit, so a new number has to fit in a slot that already exists):
    //   grainChroma : 6 | grainDensity : 6 | fogMassGate : 6 | aberrCoc : 6
    // Four 6-bit fields make an exact integer at most 2^24-1, which float32
    // carries losslessly. Six bits is 1/63 of a slider that is a taste knob
    // read once per pixel -- finer than the panel can show. The DEFAULTS are
    // what matters: 1 / 0 / 0 / 0 quantise to 63 / 0 / 0 / 0 and unpack to
    // exactly 1.0 / 0.0 / 0.0 / 0.0, so no preset moves by a bit because
    // this field exists.
    float4 rg2;   // x aberration y aberrPx(this res) z aberrField  w BD pack
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
// brief BD's four 6-bit fields, most significant first:
// grainChroma | grainDensity | fogMassGate | aberrCoc.
float4 BDU6(float v) {
    float a = floor(v * (1.0 / 262144.0));
    float r = v - a * 262144.0;
    float b = floor(r * (1.0 / 4096.0));
    r -= b * 4096.0;
    float c = floor(r * (1.0 / 64.0));
    return float4(a, b, c, r - c * 64.0) * (1.0 / 63.0);
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
//
// brief BD, measured: at the shipped keys this grain spanned 64-79 PQ10 codes
// inside a dark mass -- a 4-6x brightness ratio from pixel to pixel -- against
// 22 codes and 1.2x on the open film. Two reasons, and `chroma` and `density`
// are the two fixes. Both default to today's numbers and at those values the
// arithmetic below is character-for-character what it always was, so every
// preset that does not name them is byte-identical.
//   chroma 1  the grain is ADDED, the same encoded number on all three
//             channels. That is not luminance-only: an equal step on unequal
//             channels moves the pixel's saturation, and near black the
//             max(...,0) clips the negative half, which lifts the mean.
//   chroma 0  the grain SCALES the encoded pixel instead. The channel ratios
//             survive exactly, so it is luminance and nothing else; it cannot
//             lift a black pixel and it cannot clip. It is also what film
//             does -- grain is a fluctuation in DENSITY, i.e. in
//             transmittance, which is multiplicative.
//   density 0 today's weight: full from ~0.06 encoded upward, with a 0.15
//             FLOOR on absolute black.
//   density 1 the stock's own D-log-E hump: exactly zero in the dense shadow,
//             peak in the mid-tones, gone again on a clean highlight.
float3 Emulsion(float3 d, float3 nz, float amt, float sdr,
                float chroma, float density) {
    const float3 EW = float3(0.2126, 0.7152, 0.0722);
    float3 base = d / sdr;
    float3 e    = ToSRGB(base);
    float  lum  = dot(e, EW);
    float  w    = (1.0 - smoothstep(0.55, 1.00, lum))
                * lerp(0.15, 1.0, smoothstep(0.0, 0.06, lum));
    [branch] if (density > 0.0005) {
        float wd = smoothstep(0.10, 0.32, lum) * (1.0 - smoothstep(0.55, 1.00, lum));
        w = lerp(w, wd, saturate(density));
    }
    float3 e2;
    [branch] if (chroma > 0.9995) {
        e2 = max(e + (nz - 0.5) * (amt * 0.5 * w), 0.0);
    } else {
        // 0.6667 = 0.5 / 0.75: calibrated so a pixel at encoded 0.75 -- the
        // open film in the rising presets, the level the user has already
        // approved -- gets EXACTLY the swing the additive path gave it, and
        // everything darker gets proportionally less. That is the whole
        // point: the film keeps its grain, the shadow stops fizzing.
        float3 mul = max(e * (1.0 + (nz.x - 0.5) * (amt * 0.6667 * w)), 0.0);
        float3 add = max(e + (nz - 0.5) * (amt * 0.5 * w), 0.0);
        e2 = lerp(mul, add, saturate(chroma));
    }
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
// brief BD: a 2x2 box of bilinear taps, half a texel out on each diagonal --
// a ~1.5 px average for the price of four samples. The lateral aberration
// reads the source through this, because the source still carries the display
// pass's own grain and a first difference of per-pixel noise taken on R and B
// alone is coloured speckle, not a lens.
float3 AvgTap(float2 uv, float2 t) {
    return (Src.SampleLevel(linearClamp, uv + float2( t.x,  t.y), 0).rgb
          + Src.SampleLevel(linearClamp, uv + float2(-t.x,  t.y), 0).rgb
          + Src.SampleLevel(linearClamp, uv + float2( t.x, -t.y), 0).rgb
          + Src.SampleLevel(linearClamp, uv + float2(-t.x, -t.y), 0).rgb) * 0.25;
}


// ---- LID SCRATCHES (brief BM) ---------------------------------------------
// The WEAR on the cover: a scratched acrylic sheet under a lamp (refs
// lid-scratch-ref-1..4). A scratch is a thin groove, and a groove sends light
// to the eye only where it runs roughly PERPENDICULAR to the line from it to
// the light. That is why a scratched panel is invisible until the light
// catches it, why swirl marks form arcs round the lamp's reflection, and why
// the pattern breathes as the lamp drifts and re-aims. That one test is the
// whole effect; the rest is where the grooves are.
//
// Everything here is in LID space, px at 1440p: the caller has carried the
// pixel and the lamp through the lid's own drift and turn, so the grooves
// ride the lid and never the fluid.
//
// Two scales. MICRO: three layers of short, faintly curved grooves on long
// thin cells, one layer per 60 degrees; a slow noise field picks each
// region's dominant direction and a layer thins out as it turns away from it
// (never to nothing -- worn plastic has every direction in it). LONG: up to
// eight straight hairline gouges a few hundred px long, placed anywhere or
// pulled into the corners. Cost: 12 cells (one hash, one sincos, a few MADs
// each) + 8 slots, no marching.
float4 LidH42(float2 p) {
    float4 p4 = frac(p.xyxy * float4(0.1031, 0.1030, 0.0973, 0.1099));
    p4 += dot(p4, p4.wzxy + 33.33);
    return frac((p4.xxyz + p4.yzzw) * p4.zywx);
}
// how much of the lamp a groove with tangent T catches: 1 when it runs
// perpendicular to the direction to the light, falling off `lobe` (in cos)
// away from that to a small floor -- a groove's walls are rough, so every
// groove scatters a little whichever way it runs. Without the floor the lit
// set is all one direction in any one region, and it reads as rain.
float LidScrLit(float2 T, float2 toL, float lobe) {
    float c = dot(T, toL);
    return 0.12 + 0.88 * exp(-(c * c) / (lobe * lobe));
}
// P, Lp: the pixel and the lamp in lid space; F: the frame (px at 1440p);
// pxU: one OUTPUT pixel in those units (1 at 1440p, 2 at 720p).
float LidScratch(float2 P, float2 Lp, float2 F, float dens, float len,
                 float corner, float soft, float pxU) {
    float2 toL  = normalize(Lp - P + float2(1e-3, 0.0));
    float  wPx0 = 0.75 + 1.6 * soft;       // tent half-width: ~1 px at 1440p
    // Below 1440p a 1 px groove is thinner than a pixel and a point-sampled
    // tent would come out dotted; hold it at one output pixel and dim it by
    // the same ratio, so it keeps its energy and reads the same, only softer.
    float  wPx  = max(wPx0, 0.75 * pxU);
    float  wAmp = wPx0 / wPx;
    float  lobe = 0.30 + 0.30 * soft;
    // Where the wear is. The corners (lid_scratch_corner) of the lid's own
    // frame, the edge of that zone broken up by the same slow patchiness that
    // thins and thickens the wear everywhere.
    float  patch = PVNoise(P * (1.0 / 380.0) + 17.3);
    float2 e     = abs(P / F - 0.5) * 2.0;
    float  cm    = smoothstep(0.22, 0.80, e.x * e.y + (patch - 0.5) * 0.35);
    float  wgt   = lerp(1.0, cm, corner);
    float  pres  = lerp(0.10, 0.95, dens) * wgt * (0.55 + 0.9 * patch);
    float  acc   = 0.0;
    [branch] if (pres > 0.004) {
        float baseA = PVNoise(P * (1.0 / 640.0) + 3.1) * 9.42;
        // 64 px along x 28 across: with centres in the middle half of a cell
        // and every groove reaching at most half a cell each way, the 2x2
        // nearest cells are an exact lookup
        const float2 CS = float2(64.0, 28.0);
        [unroll] for (int ly = 0; ly < 3; ly++) {
            float  la = (float)ly * 1.0471976 + 0.35;
            float  cl = cos(la), sl = sin(la);
            float  lw = lerp(0.2, 1.0, saturate(0.5 + cos(2.0 * (baseA - la))));
            float2 Q  = float2(P.x * cl + P.y * sl, -P.x * sl + P.y * cl) / CS;
            float2 ci = floor(Q);
            float2 o  = step(0.5, Q - ci) - 1.0;
            [unroll] for (int k = 0; k < 4; k++) {
                float2 cc = ci + o + float2((float)(k & 1), (float)(k >> 1));
                float4 h  = LidH42(cc + float2(37.0 * (float)ly + 11.0, 5.0));
                if (h.x > pres * lw) continue;
                float  u  = frac(h.x * 97.31 + h.w * 13.7);
                float  v  = frac(h.y * 53.17 + h.z * 29.3);
                float  br = frac(h.z * 71.93 + h.x * 7.31);
                float2 c  = (cc + 0.25 + 0.5 * h.yz) * CS;
                float  a  = (h.w - 0.5) * 0.60;               // +-17 deg off the layer
                float2 T  = float2(cos(a), sin(a));
                float2 N  = float2(-T.y, T.x);
                float2 r  = Q * CS - c;
                float  t  = dot(r, T);
                float  hl = 6.0 + 20.0 * u * u;                // half length, px
                float  kc = (v - 0.5) * 0.020;                 // curvature, 1/px
                float  dv = kc * t;
                float  ds = abs(dot(r, N) - 0.5 * kc * t * t) * rsqrt(1.0 + dv * dv);
                float  ln = saturate(1.0 - ds / wPx)
                          * (1.0 - smoothstep(0.55, 1.0, abs(t) / hl));
                // the groove's own tangent here, turned back into lid space
                float2 Tt = normalize(T + N * dv);
                Tt = float2(Tt.x * cl - Tt.y * sl, Tt.x * sl + Tt.y * cl);
                acc += ln * LidScrLit(Tt, toL, lobe) * (0.30 + 0.70 * br * br);
            }
        }
    }
    // The long gouges: `len` is the share of the eight slots in use. A
    // straight groove lights only round the foot of the perpendicular from
    // the lamp, so its lobe is wider or it would never show at all.
    [branch] if (len > 0.004) {
        [unroll] for (int k = 0; k < 8; k++) {
            float4 h = LidH42(float2((float)k * 7.13 + 3.1, 91.7));
            if (h.x > len) continue;
            float4 g  = LidH42(float2((float)k * 3.71 + 11.9, 23.3));
            float2 cc = float2(g.w < 0.5 ? 0.04 + h.y * 0.30 : 0.96 - h.y * 0.30,
                               frac(g.w * 2.0) < 0.5 ? 0.04 + h.z * 0.30
                                                     : 0.96 - h.z * 0.30);
            float2 c  = lerp(h.yz, cc, corner) * F;
            float  a  = h.w * 6.2831853;
            float2 T  = float2(cos(a), sin(a));
            float2 r  = P - c;
            float  t  = dot(r, T);
            float  hl = 160.0 + 380.0 * g.x;
            if (abs(t) > hl) continue;
            float  ds = abs(r.x * T.y - r.y * T.x);
            float  ln = saturate(1.0 - ds / (wPx * (1.0 + 0.5 * g.y)))
                      * (1.0 - smoothstep(0.6, 1.0, abs(t) / hl));
            acc += ln * LidScrLit(T, toL, lobe * 1.6) * (0.55 + 0.45 * g.z);
        }
    }
    return acc * wAmp * rsqrt(wPx0 / 0.75); // a wider groove is a dimmer one
}

#ifdef BN_OPTICS
// ---- INSTRUMENT OPTICS (brief BN) -----------------------------------------
// The user, on the LAPD microscope sheet: "the corners need to almost look
// oddly distorted, like a real microscope ... but underneath it should still
// look pretty clear, specifically in the centre."
//
// CORNER WARP. A microscope's field is flat and sharp in the middle and goes
// strange toward the edge of the field stop: off-axis the image is stretched
// ALONG the arcs round the axis (the tangential image of an astigmatic field)
// while the radius stays put. Here the frame is treated as a square with its
// corners at (+-1, +-1); outside `r0` (1 = a corner) each pixel reads from a
// point turned toward the nearest diagonal, by an angle that grows as
// ((r - r0) / (1 - r0))^2. That is a tangential MAGNIFICATION on each
// diagonal -- the corner content smears out along its arc -- paid for by a
// slight squeeze near the middle of each edge, and it is continuous all the
// way round because sin(4 theta) is. The radius is never changed, so nothing
// is ever read from outside the frame.
//
// It is tied to the FRAME, not to the drifting lens axis: the field stop is
// the eyepiece's, and the whole point is a centre that is untouched by
// construction. At r <= r0 the pixel's uv is returned as it came in -- the
// middle 40% of the frame lies inside r = 0.4, the lowest r0 the slider
// allows, so it is bit-identical at every setting. Nothing here adds light;
// the fluid moving through the warp is what keeps it off a fixed pixel.
float2 CornerWarp(float2 uv, float k, float r0) {
    float2 s  = uv * 2.0 - 1.0;
    float  rn = length(s) * 0.70710678;
    [branch] if (rn <= r0) return uv;
    float  t  = saturate((rn - r0) / max(1.0 - r0, 1e-3));
    // 0.20 rad at k = 1 on the diagonal's neighbourhood: a tangential
    // magnification of 1 / (1 - 0.8 k) right at the corner (5x at k = 1)
    float  dA = (0.20 * k * t * t) * sin(4.0 * atan2(s.y, s.x));
    float  c  = cos(dA), sn = sin(dA);
    return float2(s.x * c - s.y * sn, s.x * sn + s.y * c) * 0.5 + 0.5;
}
// HALATION with a movable knee (brief BN halation_threshold): the halation
// block's own two-ring gather, taps and weights unchanged, knee lo..hi on
// the peak channel. Only called when the key is on; returns the normalised sum.
float3 HalGatherT(float2 ctr, float2 rad, float jt, float sdrH, float lo, float hi) {
    float3 hot = float3(0.0, 0.0, 0.0);
    float  hw  = 0.0;
    [unroll] for (int k = 0; k < 8; k++) {
        float a2 = (float)k * 0.7853982 + jt;
        float3 sp = Src.SampleLevel(linearClamp, ctr + float2(cos(a2), sin(a2)) * (0.55 * rad), 0).rgb;
        hot += sp * smoothstep(lo, hi, max(sp.r, max(sp.g, sp.b)) / sdrH); hw += 1.0;
    }
    [unroll] for (int m = 0; m < 12; m++) {
        float a2 = (float)m * 0.5235988 + jt + 0.2617994;
        float3 sp = Src.SampleLevel(linearClamp, ctr + float2(cos(a2), sin(a2)) * rad, 0).rgb;
        hot += sp * smoothstep(lo, hi, max(sp.r, max(sp.g, sp.b)) / sdrH) * 0.6; hw += 0.6;
    }
    return hot / max(hw, 1e-5);
}
// GLASS STREAKS. The LAPD sheet's thin bright wavy lines across the glass:
// light caught along a fold or a smear line in the cover, not in the picture.
// Two lines, near-horizontal, each a ~1.7 px (FWHM, 1440p) core with a very
// faint 6 px sheen, brighter along wisps and brightest where they pass the
// lamp's reflection in the cover. They live in LID space exactly like the
// scratches -- carried by a third of the lid's wander and turned by a tenth
// of its rotation about its optical centre -- so they drift with the ghosts
// and the sheen, swing when the rig readjusts, and their waves travel slowly
// along them on their own clock: nothing sits on a pixel.
// uv: the pixel (OLED orbit only), ctrL/lidO/lidA/lampR: the lid block's own
// numbers, pxU: one OUTPUT pixel in 1440p px. Returns the line strength.
float GlassStreaks(float2 uv, float2 ctrL, float2 lidO, float lidA,
                   float2 lampR, float aspL, float t, float pxU) {
    float2 F  = float2(1440.0 * aspL, 1440.0);
    float2 P  = uv * F - ctrL * F - lidO * (0.35 * F);
    float  th = 0.10 * lidA;
    float  c  = cos(th), s = sin(th);
    float2 Q  = float2(P.x * c + P.y * s, -P.x * s + P.y * c);  // along, across
    // below 1440p hold the core at one output pixel and dim it by the same
    // ratio (the scratches' rule), so it is never dotted and keeps its energy
    float  w0 = 1.0;
    float  w  = max(w0, 0.75 * pxU);
    float  wA = w0 / w;
    float  acc = 0.0;
    [unroll] for (int k = 0; k < 2; k++) {
        float  fk = (float)k;
        float  y0 = (k == 0) ? -0.085 * F.y : 0.060 * F.y;
        float  f1 = (k == 0) ? 0.0041 : 0.0033;
        float  f2 = (k == 0) ? 0.0107 : 0.0089;
        float  a1 = (k == 0) ? 26.0 : 34.0;
        float  a2 = (k == 0) ? 7.0 : 9.0;
        float  p1 = Q.x * f1 + fk * 2.1 + t * 0.011;
        float  p2 = Q.x * f2 + fk * 4.7 - t * 0.017;
        float  cv = a1 * sin(p1) + a2 * sin(p2);
        float  dv = a1 * f1 * cos(p1) + a2 * f2 * cos(p2);
        float  ds = abs(Q.y - y0 - cv) * rsqrt(1.0 + dv * dv);
        float  ln = exp(-(ds * ds) / (w * w)) * wA
                  + 0.06 * exp(-(ds * ds) / 36.0);
        // wisps: the line comes and goes along its length, slowly
        float  ws = PVNoise(float2(Q.x * (1.0 / 260.0) + fk * 13.1 + t * 0.006, fk * 7.3));
        float  al = 0.25 + 0.75 * smoothstep(0.30, 0.70, ws);
        acc += ln * al * ((k == 0) ? 1.0 : 0.75);
    }
    float  dl = length((uv - lampR) * float2(aspL, 1.0));
    return acc * (0.30 + 0.70 / (1.0 + dl * dl * 6.0));
}
#endif // BN_OPTICS

float4 PSMain(VSOut i) : SV_Target {
    const float3 W = float3(0.2126, 0.7152, 0.0722);
    // brief BD's four knobs, unpacked once: x grain chroma (1 = today),
    // y grain density curve (0 = today), z fog mass gate (0 = today),
    // w aberration fades with defocus (0 = today's uniform split).
    const float4 bdK = BDU6(rg2.w);
    float2 uv = i.uv;
#ifdef BN_OPTICS
    // brief BN corner_warp (rg1.x field 2) and corner_warp_r (field 3,
    // 0.4..0.9): FIRST, so every tap below reads the warped frame; the grain,
    // the dither and the film overlay are in SV_Position and stay unwarped.
    // All BN code is compiled only into the second post PSO (BN_OPTICS, see
    // FluidRenderer::RunPostPass), so with every BN key at 0 the post pass
    // runs today's exact bytecode: merely adding untaken branches to one
    // shader moved 1-2 pixels by one code on the identity presets (the
    // driver's own ISA compile, not the HLSL, is what changed).
    [branch] if (rg1.x >= 4096.0) {
        const float4 bnW = BDU6(rg1.x);
        [branch] if (bnW.y > 0.0005) uv = CornerWarp(uv, bnW.y, 0.4 + 0.5 * bnW.z);
    }
#endif
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
    // brief BD: ONE "am I deep inside a dye mass?" test, shared by the haze
    // below and by the lid's sheen and glint halo further down. Four taps
    // 40 px out: if the WIDE surroundings are dark too, this pixel is inside
    // something and a lift has no business here; if they take in bright film,
    // it is the water beside a mass or the LIGHTER BAND along its edge, and
    // everything stays exactly as it was. The test is spatial, not a test of
    // the pixel's own level, which is the whole reason it cannot darken that
    // band (the user, on the panel: "that low-key should stay, it has a cool
    // effect"). A shape narrower than 40 px never reads as a mass at all, so
    // droplets keep their reflections too.
    //
    // brief BO moved it up here, above the aberration, because the lens split
    // is now one of the things it gates. It only reads Src (the unaberrated
    // frame) at the final uv, so the value the haze and the lid see is the
    // one they always saw.
    //
    // artefact_lum_gate (brief BO, rg1.x's first 6-bit field). The user, on
    // the 1440p dye frames: the grain, the lens split and the cover's sheen
    // read as fake on a PURE BLACK mass and as natural on a coloured one. So
    // inside a mass those artefacts follow the mass's own brightness and
    // colour: g = max(massDeep, satLift), where satLift judges the SAME ring
    // by its peak channel when it is saturated (a deep blue dye weighs next
    // to nothing in luminance and is still plainly a colour). Every artefact
    // is scaled by lerp(1, g, gate); the sheen and the glint halo already
    // carried massDeep, and for them g REPLACES it (lerp(massDeep, g, gate))
    // rather than stacking, so a dyed mass gets its sheen back and a black
    // one stays clean. The glint's core keeps its own rules. At 0 lumG is
    // exactly 1 and lidG exactly massDeep: today's frame, bit for bit.
    const float4 bnK = BDU6(rg1.x);
    float massDeep = 1.0;
    float lumG = 1.0;      // grain, aberration, iridescence
    float lidG = 1.0;      // sheen and glint halo (was massDeep)
    [branch] if (bdK.z > 0.0005 || rg4.z > 0.5 || rg4.w > 0.5 || bnK.x > 0.0005) {
        float  sdrM = max(pp2.z, 1e-3);
        float2 mr   = (40.0 * pp4.w) * pp0.xy;
        float3 sw   = (Src.SampleLevel(linearClamp, uv + float2(mr.x, 0.0), 0).rgb
                     + Src.SampleLevel(linearClamp, uv - float2(mr.x, 0.0), 0).rgb
                     + Src.SampleLevel(linearClamp, uv + float2(0.0, mr.y), 0).rgb
                     + Src.SampleLevel(linearClamp, uv - float2(0.0, mr.y), 0).rgb) * 0.25;
        massDeep = smoothstep(0.02, 0.18, dot(ToSRGB(max(sw, 0.0) / sdrM), W));
        lidG = massDeep;
        [branch] if (bnK.x > 0.0005) {
            float3 eS  = ToSRGB(max(sw, 0.0) / sdrM);
            float  mxS = max(eS.r, max(eS.g, eS.b));
            float  mnS = min(eS.r, min(eS.g, eS.b));
            float  stS = (mxS - mnS) / max(mxS, 1e-4);
            float  g   = max(massDeep, smoothstep(0.02, 0.18, mxS)
                                     * smoothstep(0.15, 0.50, stS));
            lumG = lerp(1.0, g, saturate(bnK.x));
            lidG = lerp(massDeep, g, saturate(bnK.x));
        }
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
        // are not in Src.
        //
        // brief BD, two corrections to what this block used to claim and do:
        //
        // (1) The comment here said the fringe "correctly fades out with the
        //     blur, which is what a real lens does". It did not. Both taps
        //     came from Src, which is the SHARP display output, while `d` is
        //     the blurred picture -- so a bokeh disc kept a razor-sharp
        //     colour fringe, exactly backwards. The split is now scaled by
        //     this pixel's own defocus radius against the diffraction floor,
        //     so on the sharp slice it is what the user approved on
        //     2026-09-19 and on an out-of-focus element it goes away.
        //
        // (2) Both taps are now a ~1.5 px average (AvgTap) rather than a
        //     single texel. Src still carries the display pass's own grain,
        //     and a first difference of per-pixel noise taken on R and B and
        //     never on G is not a lens -- it is coloured speckle, and it
        //     measured as 72% of all the chroma noise inside a dark mass.
        //     Averaging first leaves the SHAPES, which are what a lateral
        //     split is supposed to act on, and costs eight extra samples.
        // The reference is the diffraction floor, or one texel where there is
        // none, so "in focus" means ab = 1 and nothing about the approved
        // sharp-slice look changes. aberration_coc 0 is the uniform split the
        // user approved on 2026-09-19 and is the default, so this costs no
        // preset anything until one asks for it.
        float  ab = 1.0;
        [branch] if (bdK.w > 0.0005) {
            float abR = max(pp6.z, 1.0);
            ab = lerp(1.0, abR / max(rPx, abR), saturate(bdK.w));
        }
        // brief BO: artefact_lum_gate (lumG = 1 when the gate is 0)
        ab *= lumG;
        float2 ts = pp0.xy * 0.75;
        float3 s0 = AvgTap(uv, ts);
        d.r += (AvgTap(uv + duv, ts).r - s0.r) * ab;
        d.b += (AvgTap(uv - duv, ts).b - s0.b) * ab;
        d = max(d, 0.0);
    }

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
        // brief BD: this jitter used to be the FULL circle per pixel. Twenty
        // taps of a spatially varying quantity, angled at random per pixel and
        // weighted into the dark, is a noisy estimator -- it measured as part
        // of the 27% of the dark masses' luminance noise that the post lifts
        // own, and it arrives in this block's warm tint, which is where the
        // grey-brown speckle came from. The taps already cover the circle at
        // 45 degrees; a jitter of HALF a tap spacing fills the gaps between
        // them and leaves nothing else to vary.
        float  jt   = 0.3926991 * (PHash21(i.pos.xy * 0.0271 + 7.13) - 0.5);
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
            float a2 = (float)m * 0.5235988 + jt + 0.2617994;
            float3 sp = Src.SampleLevel(linearClamp, ctr + float2(cos(a2), sin(a2)) * rad, 0).rgb;
            float  ex = smoothstep(0.55, 1.05, max(sp.r, max(sp.g, sp.b)) / sdrH);
            hot += sp * ex * 0.6; hw += 0.6;
        }
        hot /= max(hw, 1e-5);
        // brief BN halation_threshold (rg1.y field 2): 0 = today's knee, and
        // the gather above is left exactly as it was (a runtime-selected knee
        // measurably moved default frames). Above 0 the same gather is redone
        // with the knee lowered toward 0.05..0.25 of SDR white, so the
        // mid-tones scatter too and the halation becomes the diffusion haze of
        // a pro-mist filter -- the sharp picture is never blurred, only veiled
        // by a wide, weak, brightness-weighted copy of itself, still leaning
        // toward the lamp.
#ifdef BN_OPTICS
        const float hThr = BDU6(rg1.y).y;
        [branch] if (hThr > 0.0005)
            hot = HalGatherT(ctr, rad, jt, sdrH, 0.55 - 0.50 * hThr, 1.05 - 0.80 * hThr);
#endif
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
    // (massDeep, the "am I deep inside a dye mass?" test the haze gate reads,
    // is computed above the aberration block since brief BO.)
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
            // brief BD, fog_mass_gate. "Dark" is not the same question as
            // "water". A dark pixel beside the bright film IS the water and
            // should glow; a dark pixel deep inside a dye mass is looking at
            // the mass, which floats in FRONT of the water, and the haze has
            // no business shining through it. massDeep above is that test. At
            // 0 the gate does not exist and the haze is what it always was.
            float mg = lerp(1.0, massDeep, saturate(bdK.z));
            d += float3(1.00, 0.93, 0.86)
               * (saturate(pp5.y) * 0.16 * hz * hz * wd * mg * sdrL);
        }
        // (b) BLOOM. Rings of taps at a radius of 100+ px at 1440p: too few
        // taps for a clean blur, which does not matter at a few percent, and
        // a half-tap-spacing angular jitter breaks up what banding there
        // would be without turning the gather itself into noise (brief BD).
        // The radius breathes, the gather centre creeps, and the wash is
        // stronger on the lamp's side, so it drifts with the light instead of
        // sitting still.
        [branch] if (pp5.z > 0.0005) {
            float  rad = pp6.y * (1.0 + 0.12 * sin(tq * 0.0197 + 0.9));
            float2 ctr = uv + float2(sin(tq * 0.0131), cos(tq * 0.0173))
                            * (rad * 0.10 * pp0.xy);
            // The user photographed faint concentric RINGS around every bright
            // droplet sitting in the black: two rings of taps at fixed radii
            // ARE two circles, and a point source lights each of them up. So
            // the taps now cover the whole disc -- four radii with Gaussian
            // weights, jittered in ANGLE by half a tap spacing -- and a point
            // source blooms into a smooth wash, never a halo with edges.
            // brief BD: both of these used to vary over their whole range
            // per pixel. Twenty-four taps spread over 140 px with a per-pixel
            // RADIUS as well as a per-pixel angle is a very noisy estimate of
            // a very wide average, and it is weighted into the dark, so what
            // it printed on a mass was speckle in the film's own colour. The
            // angle keeps half a tap spacing of jitter, which is all that is
            // needed to fill the gaps between the six taps; the radius is
            // fixed. The wash still breathes and still creeps -- those are
            // `rad` and `ctr` above, and they are per FRAME, not per pixel.
            float  jit = 0.5235988 * (PHash21(i.pos.xy * 0.37) - 0.5);
            float  rj  = 1.0;
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
#ifdef BN_OPTICS
            // brief BN bloom_warmth (rg1.x field 4): the LAPD tile's veil is
            // the LAMP's colour, not the film's -- hot yellow-white on the
            // lamp side, falling to red away from it. The wash keeps its own
            // luminance exactly (the tint is normalised to Y = 1), so this is
            // a hue move and nothing else: mean_lum and ABL do not change.
            const float bWm = BDU6(rg1.x).w;
            [branch] if (bWm > 0.0005) {
                float  nl  = exp(-dl / max(pp6.x * 1.5, 1e-4));
                float3 wc  = lerp(float3(1.00, 0.34, 0.20), float3(1.00, 0.82, 0.55), nl);
                wc /= dot(wc, W);
                bl = lerp(bl, dot(bl, W) * wc, saturate(bWm));
            }
#endif
            d += bl * (saturate(pp5.z) * 0.20 * wd * lw);
        }
    }


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
#ifdef BN_OPTICS
    [branch] if (rg4.z > 0.5 || rg4.w > 0.5 || rg1.z > 0.5 || rg1.y >= 262144.0) {
#else
    [branch] if (rg4.z > 0.5 || rg4.w > 0.5 || rg1.z > 0.5) {
#endif
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
            // brief BD: `massDeep` here is new. The sheen was the one lid
            // term with no fade to black at all -- a flat warm floor across
            // the whole frame, masses included, measured at about a third of
            // a nit of warm lift on pixels that are meant to be black (a
            // coloured dye is brief AG, a separate feature). It keeps every
            // bit of itself on the film and along a mass edge and gives up
            // only the deep interior, where there is nothing for a reflection
            // to sit on.
            d += float3(1.00, 0.88, 0.70)
               * (M * lidC.z * 0.075 * sheenE * sdrL * lidG);   // BO: lidG = massDeep at gate 0
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
            d += (ir - 0.333) * (M * lidD.y * 0.055 * sheenE * sdrL * lumG);   // BO
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
            // brief BD: the CORE keeps its own rules -- it is the lamp seen
            // in the cover, a small bright thing, and the user asked for more
            // of the lid's reflections, not less (brief AF). It is the wide
            // amber HALO that was the lift: ~2 nits of warm haze over a third
            // of the frame with nothing to stop it inside a mass, so that one
            // gives up the deep interior and nothing else.
            d += (float3(1.00, 0.90, 0.72) * (core * 0.55)
                + float3(1.00, 0.56, 0.20) * (halo * 0.13 * lidG))   // BO: lidG, see above
                 * (M * lidD.x * sdrL);
        }

        // ---- SCRATCHES: the wear on the cover (brief BM) ----------------
        // Lit by the lamp as the cover sees it (lampR, the glint's position)
        // and carried by the lid: shifted by a third of its wander and turned
        // by a sliver of its rotation about its optical centre, so they drift
        // with the ghosts and are shoved with them at a readjust. The pixel
        // comes in through the OLED orbit only, NOT the shimmer warp above --
        // that one is moved by the fluid, and the lid never is. Not scaled by
        // the lid master: they are the sheet's wear, not a reflection in it.
        [branch] if (rg1.z > 0.5) {
            float3 sA  = LidU8(rg1.z);                     // amount density len
            float3 sB  = LidU8(rg1.w);                     // corner soft tint
            float2 F   = float2(1440.0 * aspL, 1440.0);
            float2 C   = ctrL * F;
            float2 off = lidO * (0.35 * F);
            float  th  = 0.06 * lidA;
            float  cth = cos(th), sth = sin(th);
            float2 Ps  = (i.uv + float2(rg3.z, rg3.w)) * F - C - off;
            float2 Ls  = lampR * F - C - off;
            float2 Pl  = float2(Ps.x * cth + Ps.y * sth, -Ps.x * sth + Ps.y * cth) + C;
            float2 Ll  = float2(Ls.x * cth + Ls.y * sth, -Ls.x * sth + Ls.y * cth) + C;
            float  sc  = saturate(LidScratch(Pl, Ll, F, sA.y, sA.z, sB.x, sB.y,
                                              1.0 / max(pp4.w, 1e-4)));
            // brightest near the lamp, never gone far from it
            float  dl  = length((i.uv - lampR) * float2(aspL, 1.0));
            float  fal = 0.15 + 0.85 / (1.0 + dl * dl * 8.0);
            // clear over the dark, a whisper over the bright film
            float  ovr = lerp(0.10, 1.0, 1.0 - smoothstep(0.12, 0.80, ownL));
            // tint 0: neutral white. 1: the colour of the film under the
            // groove (light scattered out of the picture it lies over);
            // over black there is no colour to take, so it stays white.
            float  mx  = max(d.r, max(d.g, d.b));
            float3 fc  = max(d, 0.0) / max(mx, 1e-4);
            float3 col = lerp(float3(1.0, 1.0, 1.0), fc,
                              sB.z * smoothstep(0.02, 0.10, mx / sdrL));
            d += col * (sA.x * 0.60 * sc * fal * ovr * sdrL);
        }
#ifdef BN_OPTICS
        // ---- GLASS STREAKS (brief BN, rg1.y field 1) --------------------
        // Deliberately NOT gated by massDeep / lidG: they are on the glass,
        // in front of everything, and a gate would leave them inert over the
        // masses that fill most of the frame. Not ADDED either: the first
        // cut was an add leaned off the bright pixels, and on a frame that is
        // 90% saturated film it was inert too. A line of light on glass
        // WHITENS what is behind it, so the pixel is pulled toward a pale
        // warm target at 1.2x SDR white, per channel and never downward
        // (max): over black it is a bright line, over the magenta film a
        // paler one, and over a core already brighter than the target
        // nothing -- the frame's peak cannot rise. Written 0 by the CPU
        // unless the lid is on, so lidO / lidA are always real here.
        [branch] if (rg1.y >= 262144.0) {
            float  gsA = BDU6(rg1.y).x;
            float  gs  = GlassStreaks(i.uv + float2(rg3.z, rg3.w), ctrL, lidO, lidA,
                                      lampR, aspL, pp2.y, 1.0 / max(pp4.w, 1e-4));
            float3 tg  = float3(1.00, 0.90, 0.86) * (1.2 * sdrL);
            d = max(d, lerp(d, tg, saturate(gsA * gs)));
        }
#endif
    }

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
        // The whole overlay leans into the dark: near-invisible on the film.
        // brief BD: the lean used to have a FLOOR of 0.10, so a speck still
        // landed at a tenth strength on a pixel that was meant to be black.
        // The floor is now zero -- on the bright film there is nothing to see,
        // which is what this block's own help text always claimed.
        float sdrA = max(pp2.z, 1e-3);
        float lumA = dot(ToSRGB(d / sdrA), W);
        d += add * (sdrA * (1.0 - smoothstep(0.20, 0.80, lumA)));
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
        d = Emulsion(d, nz, pp1.y * lumG, max(pp2.z, 1e-3), bdK.x, bdK.y);   // BO: lumG
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
        d = Emulsion(d, float3(n0, n0, n0), pp4.y * lumG, max(pp2.z, 1e-3), bdK.x, bdK.y);   // BO: lumG
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
