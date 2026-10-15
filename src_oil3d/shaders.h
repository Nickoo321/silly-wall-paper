// Oil3DWallpaper — HLSL, compiled at runtime with D3DCompile (fxc, SM 5.1).
//
//   kRaymarchSrc  fullscreen-triangle PS: per-ray blob culling, compact-
//                 support sphere tracing of the Wyvill field, bisection,
//                 gloss / flat shading, droplets, ink far wall
//   kDisplaySrc   fullscreen-triangle PS: (half-res) upsample, HDR highlight
//                 lift, sdrScale
//
// MSVC caps ONE string literal at 16380 bytes, so the raymarch source is split
// into adjacent R"hlsl( ... )hlsl" literals (the src/shaders.h convention).
//
// CONVENTIONS
//   * world space: x right, +y DOWN (the 2D uv convention), +z away from the
//     camera. The slab's front plane is z = 0, its far wall z = depth; at the
//     front plane the frame spans x in [0, aspect], y in [0, 1].
//   * colours from the palette are sRGB-ENCODED (the 2D look's convention:
//     oil_color_N etc. are display values); the 2D colour maths (hue patches,
//     equal load) runs on them as in src/shaders.h, then they are decoded
//     to linear for the lighting. The target holds linear scRGB (1 = SDR
//     white) in rgb and the HDR "hot" mask in alpha.
//   * the constant block is an ARRAY (C[64]) and every slot has a name below
//     that mirrors enum CbSlot in oil3d.cpp, so the C++ and HLSL layouts can
//     never drift apart by padding.
#pragma once

static const char* kRaymarchSrc = R"hlsl(
cbuffer CB : register(b0) { float4 C[64]; };

#define CB_SCREEN   C[0]    // x 1/rayW  y 1/rayH  z aspect  w time
#define CB_CAMPOS   C[1]    // xyz camera  w tan(fov/2)
#define CB_CAMR     C[2]    // xyz right   w slab depth
#define CB_CAMD     C[3]    // xyz down    w threshold
#define CB_CAMF     C[4]    // xyz forward w blob count
#define CB_MARCH    C[5]    // x steps  y refine  z drop count  w edge soft
#define CB_L0       C[6]    // xyz dir TO light 0  w intensity
#define CB_L1       C[7]    // xyz dir TO light 1  w intensity
#define CB_SHADE    C[8]    // x mode(0 flat,1 gloss) y spec z spec power w rim
#define CB_SHADE2   C[9]    // x rim power  y sss  z sss sigma  w ao
#define CB_SHADE3   C[10]   // x fog  y wrap  z ambient  w exposure
#define CB_FLAT     C[11]   // x bands  y soft  z rim width  w rim dark
#define CB_PAL0     C[12]   // xyz oil shade 1 (sRGB), the accent source with no wax hit
#define CB_HUE      C[16]   // x hue2 deg  y hue2 amt  z hue3 deg  w hue3 amt
#define CB_HUEB     C[17]   // x seam  y hue3 share  z cover  w patch freq
#define CB_HUEC     C[18]   // x patch rise offset  y equal load  z equal load P  w film level
#define CB_HUED     C[19]   // x mix contrast  y rim hue deg  z drift x  w drift z
#define CB_INK0     C[20]   // xyz paper (sRGB)        w marble amount
#define CB_INK1     C[21]   // xyz tint thin (sRGB)    w marble scale
#define CB_INK2     C[22]   // xyz tint thick (sRGB)   w marble rise offset
#define CB_INK3     C[23]   // x mode(0 bands,1 water) y levels z soft w unused
#define CB_RAMP0    C[24]   // ink ramp, four stops (sRGB)
#define CB_RAMP1    C[25]
#define CB_RAMP2    C[26]
#define CB_RAMP3    C[27]
#define CB_TONE     C[28]   // xyz split-tone add (sRGB-encoded)
#define CB_DROP     C[29]   // xyz droplet dye (sRGB, value 1)  w amount (0 = black)
#define CB_MASS     C[30]   // xyz mass dye (sRGB, value 1)     w amount (0 = black)
#define CB_MISC     C[31]   // x bottom light  y cavity dark  z dye core  w post chroma
#define CB_MISC2    C[32]   // x post lift  y debug view (0 off; 1 normal 2 hole share
                            //   3 AO occlusion 4 thickness 5 unlit fill
                            //   6 march iterations 7 ray list size; 6/7 are raw
                            //   values in [0,1], not tone mapped)
#define CB_DISP     C[33]   // x sdrScale  y peak gain  z knee   (display pass)
#define CB_DISP2    C[34]   // xy 1/shot size                    (display pass)

struct Blob {
    float4 a;   // xyz centre (world)  w support radius s
    float4 b;   // rgb fill colour (sRGB)  w weight (< 0 = hole)
    float4 c;   // x 1/stretch_y  y 1/stretch_z  z 1/s^2  w slope bound 1.717 |w| / s
};
struct Drop {
    float4 a;   // xyz centre (world)  w radius
    float4 b;   // x racer  y phase
};
StructuredBuffer<Blob> gBlobs : register(t0);
StructuredBuffer<Drop> gDrops : register(t1);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// ---- colour helpers (copied from src/shaders.h @8a4f185) -----------------
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
float3 DsToLin(float3 c) {
    c = max(c, 0.0);
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}
float3 DsToSrgb(float3 l) {
    l = max(l, 0.0);
    return lerp(l * 12.92, 1.055 * pow(l, 1.0 / 2.4) - 0.055, step(0.0031308, l));
}
float Luma(float3 lin) { return dot(lin, float3(0.2126, 0.7152, 0.0722)); }

// ---- noise: integer hash, deterministic, no texture ------------------------
float Hash3(int3 p) {
    uint h = ((uint)p.x * 73856093u) ^ ((uint)p.y * 19349663u) ^ ((uint)p.z * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xffffffu) * (1.0 / 16777216.0);
}
float VNoise3(float3 p) {
    float3 fl = floor(p);
    int3   i  = (int3)fl;
    float3 f  = p - fl;
    f = f * f * (3.0 - 2.0 * f);
    float n000 = Hash3(i),               n100 = Hash3(i + int3(1, 0, 0));
    float n010 = Hash3(i + int3(0, 1, 0)), n110 = Hash3(i + int3(1, 1, 0));
    float n001 = Hash3(i + int3(0, 0, 1)), n101 = Hash3(i + int3(1, 0, 1));
    float n011 = Hash3(i + int3(0, 1, 1)), n111 = Hash3(i + int3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
                lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}
// three octaves, normalised back to [0,1] (mean 0.5)
float Fbm3(float3 p) {
    float s = VNoise3(p) * 0.5714;
    s += VNoise3(p * 2.03 + float3(17.1, 3.7, 9.2)) * 0.2857;
    s += VNoise3(p * 4.11 + float3(5.3, 41.9, 23.4)) * 0.1429;
    return s;
}
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
// ---------------------------------------------------------------------------
// The field. A blob is a Wyvill/Blinn compact kernel (1 - d^2/s^2)^3 (the 2D
// look's, src/shaders.h:1191) over an anisotropic distance: q.y / stretch is
// the 2D rise_stretch teardrop, q.z / stretch_z the hole tunnels. Holes are
// NEGATIVE and their bite is scaled by the local oil field (max(P,1)), so a
// small hole still reaches the isoline deep inside a thick merged mass.
//
//   f = P - N * max(P, 1)          P = sum of oil kernels, N = sum of holes
//
// Only the blobs whose support the ray crosses are evaluated: the per-ray
// list is two 32-bit masks walked with firstbitlow (no indexable temps).
// ---------------------------------------------------------------------------
void Kernel(Blob B, float3 p, out float w, out float3 gw, out float dOut) {
    float3 q  = p - B.a.xyz;
    float3 iv = float3(1.0, B.c.x, B.c.y);
    float3 qs = q * iv;
    float  d2 = dot(qs, qs);
    float  u  = 1.0 - d2 * B.c.z;
    // scaled-space distance to the support surface; the scaling only ever
    // shrinks (iv <= 1), so this is a LOWER bound on the world distance
    dOut = sqrt(d2) - B.a.w;
    w = 0.0; gw = float3(0.0, 0.0, 0.0);
    if (u > 0.0) {
        float u2 = u * u;
        w  = u2 * u;
        gw = (-6.0 * u2 * B.c.z) * (q * iv * iv);
    }
}

// Field + a SAFE step for the march: the Lipschitz bound of the blobs whose
// support contains p, and the distance to the nearest oil support not yet
// entered (inside it the kernel is exactly 0, so stepping to its boundary can
// never skip a crossing).
float FieldStep(float3 p, uint m0, uint m1, out float lip, out float dEnter) {
    float P = 0.0, N = 0.0, LP = 0.0, LN = 0.0;
    dEnter = 1e9;
    [loop] for (int hb = 0; hb < 2; hb++) {
        uint m = hb == 0 ? m0 : m1;
        [loop] while (m != 0u) {
            uint bit = firstbitlow(m);
            m &= m - 1u;
            Blob B = gBlobs[bit + (uint)hb * 32u];
            float w, dOut; float3 gw;
            Kernel(B, p, w, gw, dOut);
            if (B.b.w > 0.0) {
                if (dOut < 0.0) { P += w * B.b.w; LP += B.c.w; }
                else dEnter = min(dEnter, dOut);
            } else if (dOut < 0.0) {
                N += w * (-B.b.w); LN += B.c.w;
            }
        }
    }
    float sP = max(P, 1.0);
    lip = LP * (1.0 + N) + LN * sP;
    return P - N * sP;
}

// Field, gradient, w^4 soft-max fill colour and the holes' share, at a hit.
float FieldFull(float3 p, uint m0, uint m1, out float3 grad, out float3 col,
                out float holeK) {
    float P = 0.0, N = 0.0, cw = 0.0;
    float3 gP = 0.0, gN = 0.0, cs = 0.0;
    [loop] for (int hb = 0; hb < 2; hb++) {
        uint m = hb == 0 ? m0 : m1;
        [loop] while (m != 0u) {
            uint bit = firstbitlow(m);
            m &= m - 1u;
            Blob B = gBlobs[bit + (uint)hb * 32u];
            float w, dOut; float3 gw;
            Kernel(B, p, w, gw, dOut);
            if (B.b.w > 0.0) {
                P  += w * B.b.w;
                gP += gw * B.b.w;
                // Flat fill from a SOFT-max over the blob weights (the 2D
                // look's w^4: tails contribute nothing, the hand-over between
                // two merged blobs is a soft gradient, never a crisp circle).
                float w2 = w * w, w4 = w2 * w2;
                cs += B.b.rgb * w4;
                cw += w4;
            } else {
                N  += w * (-B.b.w);
                gN += gw * (-B.b.w);
            }
        }
    }
    float sP = max(P, 1.0);
    float3 gOil  = (P > 1.0) ? gP * (1.0 - N) : gP;
    float3 gHole = -gN * sP;
    grad  = gOil + gHole;
    col   = cw > 1e-20 ? cs / cw : float3(0.5, 0.5, 0.5);
    // how much of the surface's SHAPE comes from the holes: ~1 on the wall of
    // a carved pocket, ~0 on an outer surface that merely has a hole nearby
    float lh = length(gHole), lo = length(gOil);
    holeK = lh / max(lh + lo, 1e-6);
    return P - N * sP;
}

// ---- the 2D scheme maths on the wax colour --------------------------------
// The hue2 / hue3 patch field. 2D: a 20x12 scalar advected by the fluid; 3D: a
// slow fbm that RISES with the wax (CB_HUEC.x) so the patches belong to the
// masses, contrast-stretched and biased by film_hue2_cover.
float MixAt(float3 pw) {
    float3 q = float3(pw.x + CB_HUED.z, pw.y + CB_HUEC.x, pw.z + CB_HUED.w) * CB_HUEB.w;
    // centre 0.45 / contrast 1.2 (mix_contrast): Monte Carlo of this exact
    // noise puts the second hue on ~19% and the third on ~13% of the wax at
    // the schemes' film_hue2_cover 0.04 -- the 2D targets (~18%, <= ~15%)
    return saturate(0.45 + (Fbm3(q) - 0.5) * CB_HUED.x + CB_HUEB.z);
}

// film_hue2 / film_hue3 rotation + film_equal_load (src/shaders.h:1883-2084,
// same bands, same seam narrowing, same equal-load target at 325 deg).
float3 SchemeColor(float3 oilC, float3 pw, out float k2, out float k3) {
    k2 = 0.0;
    k3 = 0.0;
    [branch] if (CB_HUE.y > 0.0005 || CB_HUE.w > 0.0005) {
        float mixV = MixAt(pw);
        const float MIXSEAM = 0.62;
        float seam = CB_HUEB.x;
        float k2b = (seam != 1.0)
            ? smoothstep(MIXSEAM - 0.06 * seam, MIXSEAM + 0.06 * seam, mixV)
            : smoothstep(0.56, 0.68, mixV);
        k2 = k2b * saturate(CB_HUE.y);
        oilC = AcidHueShift(oilC, CB_HUE.x * k2);
        [branch] if (CB_HUE.w > 0.0005) {
            float h3d = (1.0 - CB_HUEB.y) * 0.16;
            float h3b = (seam != 1.0)
                ? smoothstep(0.32 - h3d - 0.14 * seam, 0.32 - h3d + 0.14 * seam, mixV)
                : smoothstep(0.18 - h3d, 0.46 - h3d, mixV);
            k3 = (1.0 - h3b) * saturate(CB_HUE.w);
            oilC = AcidHueShift(oilC, CB_HUE.z * k3);
        }
    }
    [branch] if (CB_HUEC.y > 0.0005 || CB_HUEC.z > 0.0005) {
        float3 hsv = AcidRgb2Hsv(oilC);
        float  wb  = saturate(CB_HUEC.y) * saturate(1.0 - k2 - k3);
        [branch] if (CB_HUEC.z > 0.0005)
            wb = saturate(wb + saturate(CB_HUEC.z)
                             * saturate(smoothstep(0.8, 1.0, k2) + smoothstep(0.8, 1.0, k3)));
        float Y  = Luma(DsToLin(oilC));
        float Yt = Luma(DsToLin(AcidHsv2Rgb(float3(0.9027778, hsv.y, hsv.z))));
        float gl = lerp(1.0, min(1.0, Yt / max(Y, 1e-6)), wb);
        oilC = DsToSrgb(DsToLin(oilC) * gl);
    }
    return oilC;
}
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
// ---- the far wall: the ink the oil floats on -------------------------------
// Stands in for the 2D fluid, which the prototype does not run: a slow
// domain-warped fbm rising up the wall, in the stage's ink colours. In
// ink_mode=water with the live stages' black paper and no ink ever injected
// the marble amount is 0 and the wall is the paper: pitch black. The masses'
// dye (dye_lum > 0) and the split tone land here too: the wall and the
// cavities are the 2D "masses".
float3 InkWall(float2 xy, out float marbleOut) {
    marbleOut = 0.5;
    // the live stages: water mode, no ink ever injected, no mass dye -> the
    // wall is the paper and none of the noise below is needed
    [branch] if (CB_INK3.x > 0.5 && CB_INK0.w < 1e-4 && CB_MASS.w < 0.0005)
        return CB_INK0.rgb;
    float sc = CB_INK1.w;
    float3 q = float3(xy.x * sc, (xy.y + CB_INK2.w) * sc, CB_SCREEN.w * 0.015);
    float2 warp = float2(Fbm3(q + float3(3.1, 7.7, 0.0)), Fbm3(q + float3(11.3, 1.9, 0.0)));
    float m = Fbm3(q + float3(warp * 1.6, 0.0));
    marbleOut = m;
    float3 c;
    [branch] if (CB_INK3.x > 0.5) {
        // water: Beer-Lambert-ish veils of the [ink] tint on the paper
        float op = saturate(CB_INK0.w) * smoothstep(0.45, 0.80, m);
        float3 tint = lerp(CB_INK1.rgb, CB_INK2.rgb, smoothstep(0.55, 0.85, m));
        c = lerp(CB_INK0.rgb, tint, op);
    } else {
        // bands: the posterised ramp (dark -> bright), scaled by the amount
        float v = saturate((m - 0.30) * 2.2) * saturate(CB_INK0.w);
        float lv = max(CB_INK3.y, 1.0);
        float x = v * lv, fx = frac(x);
        float sv = (floor(x) + smoothstep(0.5 - 0.5 * CB_INK3.z, 0.5 + 0.5 * CB_INK3.z, fx)) / lv;
        float3 r01 = lerp(CB_RAMP1.rgb, CB_RAMP0.rgb, saturate(sv * 3.0));
        float3 r23 = lerp(CB_RAMP2.rgb, CB_RAMP3.rgb, saturate(sv * 3.0 - 2.0));
        c = lerp(r01, r23, saturate(sv * 3.0 - 1.0));
    }
    return c;
}

// The black role (wall + cavities) with the masses' dye and the split tone.
float3 MassColor(float3 inkC, float marble) {
    float3 c = inkC;
    [branch] if (CB_MASS.w > 0.0005)
        c += CB_MASS.rgb * CB_MASS.w * lerp(CB_MISC.z, 1.0, marble);
    // split tone: only on the darkest tones (the 2D mask, luma 0.02..0.15)
    float l = Luma(DsToLin(c));
    c += CB_TONE.rgb * (1.0 - smoothstep(0.02, 0.15, l));
    return c;
}

float3 SkyRay(float3 ro, float3 rd, float tWall, out float3 massSrgb) {
    float3 pw = ro + rd * tWall;
    float marble;
    float3 ink = InkWall(pw.xy, marble);
    massSrgb = MassColor(ink, marble);
    return DsToLin(massSrgb);
}

// ---- droplets: small analytic spheres in front of / behind the wax ---------
// (2D: hundreds of particles summed into the field. 3D prototype: up to 64
// beads, ray-sphere only, so they cost nothing per march step.)
float NearestDrop(float3 ro, float3 rd, float tMax, out int which) {
    which = -1;
    float best = tMax;
    int nd = (int)CB_MARCH.z;
    [loop] for (int i = 0; i < nd; i++) {
        Drop D = gDrops[i];
        float3 o = ro - D.a.xyz;
        float b = dot(o, rd), c = dot(o, o) - D.a.w * D.a.w;
        float disc = b * b - c;
        if (disc <= 0.0) continue;
        float t = -b - sqrt(disc);
        if (t > 0.0 && t < best) { best = t; which = i; }
    }
    return best;
}
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
float3 ShadeWax(float3 p, float3 n, float3 v, float3 rd, float3 baseSrgb, float3 rimSrgb,
                uint m0, uint m1, float3 grad, out float hot, out float2 dbg) {
    float thr = CB_CAMD.w;
    float3 base = DsToLin(baseSrgb);
    float3 L0 = CB_L0.xyz, L1 = CB_L1.xyz;
    float  wrap = CB_SHADE3.y;
    float  d0 = saturate((dot(n, L0) + wrap) / (1.0 + wrap));
    float  d1 = saturate((dot(n, L1) + wrap) / (1.0 + wrap));
    float  ndv = saturate(dot(n, v));
    hot = 0.0;

    // soft field AO: probe a quarter of the local field length outward; a
    // neighbour mass keeps the field up where an isolated blob's falls away
    float gl  = max(length(grad), 1e-4);
    float h   = min(0.25 * thr / gl, 0.08);
    float lipA, dEA;
    float fo  = FieldStep(p + n * h, m0, m1, lipA, dEA);
    float occ = saturate((fo - (thr - h * gl)) / (0.5 * thr));
    float aoF = 1.0 - CB_SHADE2.w * occ;
    dbg = float2(occ, 0.0);

    float3 col;
    [branch] if (CB_SHADE.x > 0.5) {
        // ---- gloss: wax / oil -------------------------------------------
        float3 lit = base * (CB_SHADE3.z + CB_L0.w * d0 + CB_L1.w * d1) * aoF;
        // thickness glow: a short march INTO the wax along the view ray;
        // thin wax passes the lamp, thick wax keeps a floor of inner glow
        float hs = clamp(0.6 * thr / gl, 0.004, 0.05);
        float tau = 0.0;
        [unroll] for (int k = 1; k <= 5; k++) {
            float lp, de;
            if (FieldStep(p + rd * (hs * k), m0, m1, lp, de) >= thr) tau += hs;
        }
        dbg.y = tau;
        float glow = CB_SHADE2.y * (0.25 + 0.75 * exp(-CB_SHADE2.z * tau));
        float3 emit = base * glow * (0.6 + 0.4 * d0);
        // a SMALL hot specular (OLED: keep the hot stuff small)
        float3 h0 = normalize(L0 + v), h1 = normalize(L1 + v);
        float s0 = pow(saturate(dot(n, h0)), CB_SHADE.z) * CB_L0.w;
        float s1 = pow(saturate(dot(n, h1)), CB_SHADE.z * 1.5) * CB_L1.w * 0.6;
        float sp = (s0 + s1) * CB_SHADE.y * aoF;
        float3 specC = lerp(float3(1.0, 1.0, 1.0), base / max(max(base.r, max(base.g, base.b)), 1e-4), 0.3) * sp;
        // fresnel rim in the rim (accent) colour, lamp side brighter
        float fr = pow(1.0 - ndv, CB_SHADE2.x) * CB_SHADE.w * (0.55 + 0.45 * d0);
        float3 rimC = DsToLin(rimSrgb) * fr;
        col = lit + emit + rimC + specC;
        hot = saturate(sp * 1.5);
    } else {
        // ---- flat: the 2D identity -- flat saturated fill, banded light,
        // thin dark rim at the silhouette ---------------------------------
        float l  = saturate(CB_SHADE3.z + CB_L0.w * d0 + CB_L1.w * d1 * 0.5) * aoF;
        float bands = max(CB_FLAT.x, 1.0);
        float x = l * bands, fx = frac(x);
        float band = (floor(x) + smoothstep(0.5 - 0.5 * CB_FLAT.y, 0.5 + 0.5 * CB_FLAT.y, fx)) / bands;
        col = base * lerp(0.55, 1.05, band);
        float rimM = 1.0 - smoothstep(0.0, max(CB_FLAT.z, 1e-3), ndv);
        col *= 1.0 - CB_FLAT.w * rimM;
    }
    return col;
}

float4 PSMain(float4 svp : SV_Position) : SV_Target {
    float2 uv  = svp.xy * CB_SCREEN.xy;            // 0..1, y down
    float2 ndc = uv * 2.0 - 1.0;
    float  A   = CB_SCREEN.z;
    float3 ro  = CB_CAMPOS.xyz;
    float3 rd  = normalize(CB_CAMF.xyz + (ndc.x * CB_CAMPOS.w * A) * CB_CAMR.xyz
                                       + (ndc.y * CB_CAMPOS.w) * CB_CAMD.xyz);
    float  D   = CB_CAMR.w;
    float  tWall = (D - ro.z) / max(rd.z, 1e-4);
    float  thr = CB_CAMD.w;

    // ---- per-ray culling: which supports does this ray cross? -----------
    uint m0 = 0u, m1 = 0u;
    float tmin = 1e9, tmax = -1e9;
    int nb = (int)CB_CAMF.w;
    [loop] for (int i = 0; i < nb; i++) {
        Blob B = gBlobs[i];
        float3 iv = float3(1.0, B.c.x, B.c.y);
        float3 o = (ro - B.a.xyz) * iv, d = rd * iv;
        float a = dot(d, d), b = dot(o, d), c = dot(o, o) - B.a.w * B.a.w;
        float disc = b * b - a * c;
        if (disc <= 0.0) continue;
        float sq = sqrt(disc);
        float t0 = (-b - sq) / a, t1 = (-b + sq) / a;
        if (t1 <= 0.0 || t0 >= tWall) continue;
        if (i < 32) m0 |= 1u << (uint)i; else m1 |= 1u << (uint)(i - 32);
        if (B.b.w > 0.0) { tmin = min(tmin, max(t0, 0.0)); tmax = max(tmax, min(t1, tWall)); }
    }

    // ---- march: compact-support sphere tracing + bisection ---------------
    float tHit = -1.0;
    int   iters = 0;
    [branch] if (tmax > tmin) {
        int   steps = (int)CB_MARCH.x;
        float floorStep = (tmax - tmin) / (float)steps * 0.5;
        float t = tmin, lip, dE;
        float f = FieldStep(ro + rd * t, m0, m1, lip, dE);
        if (f >= thr) tHit = t;
        [loop] for (int s = 0; s < steps * 2 && tHit < 0.0; s++) {
            float safe = (thr - f) / max(lip, 1e-3);
            float stepL = max(min(safe, dE + 1e-4), floorStep);
            float tn = min(t + stepL, tmax);
            float fn = FieldStep(ro + rd * tn, m0, m1, lip, dE);
            iters++;
            if (fn >= thr) {
                float a0 = t, a1 = tn;
                int refine = (int)CB_MARCH.y;
                [loop] for (int k = 0; k < refine; k++) {
                    float tm = 0.5 * (a0 + a1), l2, d2;
                    if (FieldStep(ro + rd * tm, m0, m1, l2, d2) >= thr) a1 = tm; else a0 = tm;
                }
                tHit = a1;
            }
            t = tn; f = fn;
            if (t >= tmax) break;
        }
    }
)hlsl"
// (split: MSVC caps a single string literal at 16380 bytes)
R"hlsl(
    // cost views (6: march iterations / 2*steps, 7: blobs in the ray's list / 16)
    [branch] if (CB_MISC2.y > 5.5) {
        float v6 = (float)iters / (2.0 * CB_MARCH.x);
        float v7 = (float)(countbits(m0) + countbits(m1)) / 16.0;
        float v = CB_MISC2.y < 6.5 ? v6 : v7;
        return float4(v, v, v, 0.0);
    }
    // ---- shade -----------------------------------------------------------
    float3 massSrgb;
    float3 sky = SkyRay(ro, rd, tWall, massSrgb);
    float3 col = sky;
    float  hot = 0.0;
    float3 rimSrgb = float3(0.0, 0.0, 0.0);
    float  tSurf = tWall;
    [branch] if (tHit > 0.0) {
        float3 p = ro + rd * tHit;
        float3 grad, fill; float holeK;
        FieldFull(p, m0, m1, grad, fill, holeK);
        float3 n = -normalize(grad + float3(0.0, 0.0, -1e-6));
        float3 v = -rd;
        // screen-space y of the hit for the 2D lamp-under-the-column ramp
        // screen-space y of the hit (world y is spread by the perspective)
        float camDist = 0.5 / max(CB_CAMPOS.w, 1e-4);
        float sy = 0.5 + (p.y - 0.5) * camDist / (camDist + max(p.z, 0.0));
        float lampG = lerp(1.0, 0.80 + 0.50 * smoothstep(0.0, 1.0, saturate(sy)), saturate(CB_MISC.x));
        float k2, k3;
        float3 oilC = SchemeColor(fill, p, k2, k3);
        // the edge light keeps the full palette; the film level dims the body.
        // The rim turns to the SEAM colour (half the hue2 rotation) from
        // wherever it stands: the 2D boundary-reflect delta (seam - here).
        rimSrgb = AcidHueShift(oilC, CB_HUED.y - CB_HUE.x * k2 - CB_HUE.z * k3);
        float3 body = oilC * CB_HUEC.w * lampG;
        float2 dbg;
        col = ShadeWax(p, n, v, rd, body, rimSrgb * lampG, m0, m1, grad, hot, dbg);
        [branch] if (CB_MISC2.y > 0.5) {
            int dv = (int)CB_MISC2.y;
            float3 o = n * 0.5 + 0.5;
            if (dv == 2) o = float3(holeK, smoothstep(0.50, 0.80, holeK), 0.0);
            if (dv == 3) o = dbg.xxx;
            if (dv == 4) o = saturate(dbg.yyy * 5.0);
            if (dv == 5) o = DsToLin(body);
            return float4(o, 0.0);
        }
        // a hole-carved wall is the inside of a cavity: it takes the black
        // role (or the masses' dye), so a hole reads as a dark pocket even
        // where the tunnel does not pierce the whole mass
        float cav = smoothstep(0.50, 0.80, holeK) * saturate(CB_MISC.y);
        col = lerp(col, DsToLin(massSrgb), cav);
        hot *= 1.0 - cav;
        // depth fog toward the backdrop
        col = lerp(col, sky, CB_SHADE3.x * saturate(p.z / max(D, 1e-3)));
        // silhouette AA: blend the grazing band toward what is behind it
        float ndv = saturate(dot(n, v));
        float ea = smoothstep(0.0, max(CB_MARCH.w, 1e-4), ndv);
        col = lerp(sky, col, ea);
        hot *= ea;
        tSurf = tHit;
    }

    // ---- droplets ---------------------------------------------------------
    int di;
    float tD = NearestDrop(ro, rd, tSurf, di);
    [branch] if (di >= 0) {
        Drop Dp = gDrops[di];
        float3 p = ro + rd * tD;
        float3 n = normalize(p - Dp.a.xyz);
        float3 v = -rd;
        float ndv = saturate(dot(n, v));
        float d0 = saturate((dot(n, CB_L0.xyz) + 0.3) / 1.3);
        // the bead's body: the droplet dye, or black (the 2D droplets are
        // holes in the film), slightly see-through
        float3 bodyC = DsToLin(CB_DROP.rgb * CB_DROP.w) * (CB_SHADE3.z + d0 * CB_L0.w);
        float3 beadC = lerp(col, bodyC, 0.85);
        // accent rim + a pin-point glint
        float3 accent = (tHit > 0.0) ? rimSrgb : AcidHueShift(CB_PAL0.rgb, CB_HUED.y);
        float fr = pow(1.0 - ndv, 2.5) * 0.9;
        float3 hv = normalize(CB_L0.xyz + v);
        float sp = pow(saturate(dot(n, hv)), 220.0) * CB_SHADE.y * 1.4;
        beadC += DsToLin(accent) * fr + sp;
        float ea = smoothstep(0.0, 0.25, ndv);
        col = lerp(col, beadC, ea);
        hot = max(hot, saturate(sp * 1.5) * ea);
    }

    // ---- final trim (2D post_chroma / post_lift) and exposure -------------
    float Y = Luma(col);
    col = max(Y + (col - Y) * CB_MISC.w, 0.0) * CB_MISC2.x;
    col *= CB_SHADE3.w;
    return float4(col, hot);
}
)hlsl";

static const char* kDisplaySrc = R"hlsl(
cbuffer CB : register(b0) { float4 C[64]; };
Texture2D<float4> gSrc : register(t2);
SamplerState gSamp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PSMain(float4 svp : SV_Position) : SV_Target {
    float4 s = gSrc.SampleLevel(gSamp, svp.xy * C[34].xy, 0);   // = half-res upsample
    float3 c = max(s.rgb, 0.0) * C[33].x;                       // sdrScale
    // HDR: only the small hot speculars climb toward peak_nits; identity
    // when Windows HDR is off (peak gain 1)
    float gain = 1.0;
    if (C[33].y > 1.001) {
        float t = smoothstep(C[33].z * 0.5, 1.0, saturate(s.a));
        gain = lerp(1.0, C[33].y, t);
    }
    return float4(c * gain, 1.0);
}
)hlsl";
