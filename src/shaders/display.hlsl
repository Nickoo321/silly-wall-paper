
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
    float4 mrP2;   // x soft     y source    z grainChroma    w grainDensity
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
    // laMix, the 20x12 hue2 mix field, is four cells per float4. Small on
    // purpose: the patches the reference shows are a quarter to a half of the
    // frame, so this carries them with room to spare and costs one cbuffer
    // fetch and a bilinear blend per pixel -- no texture, no descriptor.
    // ---- BEGIN GENERATED by tools\slot-check.ps1 -Fix from src\acid_slots.h + AcidParamsGPU;
    // do not hand-edit. The acid code reads each packed scalar as LA_<NAME>, a
    // '#define LA_<NAME> laP<n>.<c>' that kAcidSlotMacros (fluid.cpp) passes to
    // D3DCompile. Units, ini keys and hardcoded values: src\acid_slots.h.
    float4 laOil[4];  // oil palette, rgb
    float4 laInk[4];  // ink ramp stops (dark -> bright), rgb
    float4 laP0;      // x BLOB_COUNT  y THRESHOLD  z SUPPORT_SCALE  w AA_SCALE
    float4 laP1;      // x RIM_WIDTH  y RIM_INSET  z RIM_DARK  w REFRACTION
    float4 laP2;      // x MENISCUS  y MENISCUS_W  z TRANSLUCENCY  w OIL_TEXTURE
    float4 laP3;      // x INK_LEVELS  y INK_SOFT  z INK_MIX  w INK_HUE_VARY
    float4 laP4;      // x INK_GAIN  y INK_BIAS  z SEAM_STR  w SEAM_SCALE
    float4 laP5;      // x SEAM_LO  y SEAM_HI  z GRAIN  w GRAIN_SCALE
    float4 laP6;      // x SPECKLE  y SPECKLE_SCALE  z TIME  w ASPECT
    float4 laP7;      // x OIL_HDR  y RIM_HDR  z MENISCUS_OFF  w INK_SHADING
    float4 laP8;      // x SWARM_HOLES  y SWARM_DROPS  z SWARM_DENSITY  w SWARM_RIM_DARK
    float4 laP9;      // x SWARM_SCALE_HOLES  y SWARM_SCALE_DROPS  z SWARM_R_MIN  w SWARM_R_MAX
    float4 laP10;     // x SWARM_CLUMP  y SWARM_DARK  z INK_WATER  w TOE_TINT
    float4 laP11;     // x INK_LOCK  y INK_LOCK_SPAN  z INK_TARGET_HUE  w DYE_DROP_RGB
    float4 laP12;     // x RIM_VARY  y RIM_INK_FOLLOW  z RIM_ORDER  w GRAIN_SHADOW_W
    float4 laP13;     // x OIL_THIN_EDGE  y OIL_EDGE_FRAC  z OIL_SPECULAR  w OIL_IRID
    float4 laP14;     // x SWARM_LENS  y MEN_FROM_INK  z OIL_GLOW  w REFR_WIDTH
    float4 laP15;     // x OIL_TRANSP  y OIL_ABSORB  z OIL_FILM_BUMP  w OIL_REFR_BODY
    float4 laP16;     // x OIL_INK_BLUR  y DYE_DEPTH_W  z MEN_FILM_MIX  w DYE_SMOKE
    float4 laP17;     // x RISE_BOTTOM_LIGHT  y POST_CHROMA  z POST_LIFT  w FILM_LEVEL
    float4 laP18;     // x DROPS_ON  y DROP_GRID_W  z DROP_GRID_H  w OIL_EDGE_MODE
    float4 laP19;     // x DROP_SUPPORT  y DROP_WEIGHT  z DROP_OIL_W  w DROP_RING_BASE
    float4 laP20;     // x DROP_RING_WIDTH  y DROP_RING_LIFT  z OIL_EDGE_CURVE  w DIFFR_SCALE
    float4 laP21;     // x HALO  y HALO_W  z SOFTNESS  w BAND_MIN
    float4 laP22;     // x PENUMBRA  y PENUMBRA_W  z PENUMBRA_HUE  w PENUMBRA_DARK
    float4 laP23;     // x CELL_INK  y CELL_OIL  z CELL_SCALE  w CELL_DRIFT
    float4 laMen;     // meniscus halo colour, rgb
    float4 laP24;     // x CAM_AXIS_X  y CAM_AXIS_Y  z FOCUS_DEPTH  w DOF_MAX_PX
    float4 laP25;     // x FIELD_CURVE  y TILT_AMT  z TILT_COS  w TILT_SIN
    float4 laP26;     // x FOCUS_BAND  y COC_SPAN_INV  z FOV_K  w DIFFRACTION
    float4 laP27;     // x LENS  y LENS_CENTRE  z LENS_BAND  w DROP_SPEC
    float4 laP28;     // x LAMP_X  y LAMP_Y  z DYE_DEPTH  w DYE_TILT
    float4 laP29;     // x MASS_RIM  y MASS_RIM_W  z DYE_AMT  w DYE_HUE
    float4 laP30;     // x HUE2_AMT  y HUE2_DEG  z HUE3_AMT  w HUE3_DEG
    float4 laP31;     // x CRUST_HUE_MIX  y REFLECT_R  z REFLECT_AMT  w DYE_SAT
    float4 laP32;     // x SHADOW_AMT  y SHADOW_LEN  z SHADOW_SOFT  w LIGHT_Z
    float4 laP33;     // x OIL_FLUOR  y OIL_FLUOR_REACH  z DYE_LAMP_FOLLOW  w DARK_SAT
    float4 laP34;     // x DYE_LUM_VARY  y DYE_HUE_VARY  z DYE_THICK_HUE  w DYE_ID_RISE
    float4 laP35;     // x DYE_CORE  y HUE3_SHARE  z EQUAL_LOAD  w -
    float4 laP36;     // x GREY_K  y GREY_SIZE  z GREY_COOL  w GREY_CX
    float4 laP37;     // x TONE_R  y TONE_G  z TONE_B  w GREY_CY
    float4 laP38;     // x -  y -  z -  w -
    float4 laP39;     // x HTONE_R  y HTONE_G  z HTONE_B  w TONE_BAL
    float4 laMix[60]; // hue2 mix field: 20x12 cells, four per float4 (brief AE)
    // ---- END GENERATED
};
// xy = centre uv, z = radius, w = field weight (+1 oil, negative = hole)
// rgb of .b = flat fill colour, .w = rise_stretch anisotropy (0 = round)
// .c = mouse_oil_mode 2 (comb): x = stretch amount along the DRAG direction
// (0 = off, and then the whole comb branch is skipped and the round/rise
// path below is the original code), yz = that direction as a unit vector in
// p-space. Zero for every blob unless the comb is actually running.
// .c.w = the blob's dye IDENTITY in (0,1), a CPU hash fixed for the blob's
// life (brief AG-b). Read for hole blobs only, and only with dye variation on.
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

// ---- dark_sat (brief BP): darker must mean MORE saturated ----------------
// The user: "it reduces colours and then reduces saturation as well, when
// those should be inversely proportional." A chroma gain that grows as the
// ELEMENT's level falls (film_level for the film, the dye level for a mass
// or a droplet -- never per-pixel luminance, which would re-grade every
// shadow), applied about the pixel's own LINEAR luminance: the colours here
// are sRGB-encoded, and a gain about the encoded luma (the first version)
// raised the displayed luminance by 10-40% through the decode's curvature
// (measured on the .jxr), so it decodes, scales about linear Y, re-encodes.
// Y is kept, so mean_lum, the HDR peak and the panel's ABL do not move. The
// only clamps are the gamut edges: no channel below 0, and no channel pushed
// past 1 that was not already there (the composite is saturate()d later, so a
// clipped channel would lose luminance), so HDR on and off agree. Callers
// skip it at dark_sat 0, so today's frame is untouched.
float3 DsToLin(float3 c) {
    c = max(c, 0.0);
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}
float3 DsToSrgb(float3 l) {
    l = max(l, 0.0);
    return lerp(l * 12.92, 1.055 * pow(l, 1.0 / 2.4) - 0.055, step(0.0031308, l));
}
float3 DarkSat(float3 c, float lvl) {
    float3 l = DsToLin(c);
    float Y = dot(l, float3(0.2126, 0.7152, 0.0722));
    float m = min(l.r, min(l.g, l.b));
    float M = max(l.r, max(l.g, l.b));
    float k = 1.0 + LA_DARK_SAT * (1.0 - saturate(lvl));
    k = min(k, Y / max(Y - m, 1e-6));
    k = min(k, max((1.0 - Y) / max(M - Y, 1e-6), 1.0));
    float3 o = DsToSrgb(Y + (l - Y) * k);
    // post_chroma (acid-rise-12: 1.2) later scales chroma about the ENCODED
    // luma and clamps at 0, which turns extra chroma back into luminance
    // (measured +3..8% mean_lum at dark_sat 1 without this). So hold the
    // luminance the pixel will have AFTER that trim: two fixed-point steps of
    // a linear-light gain, each within a fraction of a percent of the last.
    [branch] if (abs(LA_POST_CHROMA - 1.0) > 0.001) {
        const float3 W = float3(0.2126, 0.7152, 0.0722);
        float  pc = LA_POST_CHROMA;
        float  y0 = dot(c, W);
        float  t0 = dot(DsToLin(max(y0 + (c - y0) * pc, 0.0)), W);
        [unroll] for (int it = 0; it < 2; it++) {
            float y1 = dot(o, W);
            float t1 = dot(DsToLin(max(y1 + (o - y1) * pc, 0.0)), W);
            o = DsToSrgb(DsToLin(o) * (t0 / max(t1, 1e-8)));
        }
    }
    return o;
}

// ---- brief BU-b: the luminance the PANEL gets from a composite colour ----
// x = linear (709-coded) composite after post_chroma / post_lift / the clip.
// The gamut stretch below (gamut 2 = BT.2020, 1 = P3) turns a saturated
// film into out-of-gamut values whose NEGATIVE channels are clamped to 0 on
// the way out, and that clamp ADDS luminance (measured: the magenta film's
// G -> -0.075 clamp is +33% of its displayed Y). A shift that holds any
// fixed-weight Y still loses that bonus (-19% mean_lum at desat 0.5), so
// the highlight tone holds THIS instead: exact, and 1-homogeneous
// (BubLum(k x) = k BubLum(x), k >= 0), so a scale is one step.
float BubLum(float3 x) {
    float3 y = x;
    if (gamut > 1.5) {
        y = float3(dot(float3( 1.66049, -0.58764, -0.07285), x),
                   dot(float3(-0.12455,  1.13290, -0.00835), x),
                   dot(float3(-0.01815, -0.10058,  1.11873), x));
    } else if (gamut > 0.5) {
        y = float3(dot(float3( 1.22494, -0.22494,  0.0),     x),
                   dot(float3(-0.04206,  1.04206,  0.0),     x),
                   dot(float3(-0.01964, -0.07868,  1.09832), x));
    }
    return dot(max(y, 0.0), float3(0.2126, 0.7152, 0.0722));
}

// ---- the 20x12 hue2 MIX FIELD, one bilinear fetch (brief AE / AJ) --------
// Factored out of the display pass so the boundary-reflection reach (AJ) can
// probe the field a few times without repeating the index arithmetic. No
// texture and no descriptor: laMix is 240 floats already resident in the
// cbuffer, so a "tap" here is four indexed loads and three lerps.
// mt is smoothstepped, so the 20x12 cells never show as a quad grid.
float AcidMixAt(float2 uv) {
    const float MW = 20.0, MH = 12.0;
    float2 mf = float2(uv.x * MW - 0.5, uv.y * MH - 0.5);
    float2 mi = floor(mf), mt = mf - mi;
    mt = mt * mt * (3.0 - 2.0 * mt);
    int x0 = (int)clamp(mi.x, 0.0, MW - 1.0), x1 = (int)clamp(mi.x + 1.0, 0.0, MW - 1.0);
    int y0 = (int)clamp(mi.y, 0.0, MH - 1.0), y1 = (int)clamp(mi.y + 1.0, 0.0, MH - 1.0);
    int i00 = y0 * 20 + x0, i10 = y0 * 20 + x1;
    int i01 = y1 * 20 + x0, i11 = y1 * 20 + x1;
    float m00 = laMix[i00 >> 2][i00 & 3], m10 = laMix[i10 >> 2][i10 & 3];
    float m01 = laMix[i01 >> 2][i01 & 3], m11 = laMix[i11 >> 2][i11 & 3];
    return lerp(lerp(m00, m10, mt.x), lerp(m01, m11, mt.x), mt.y);
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
    const float aspect = LA_ASPECT;
    float2 pp = float2(uv.x * aspect, uv.y);
    float  field = 0.0;
    float2 grad  = float2(0.0, 0.0);
    float  colW = 0.0;
    float3 colSum = float3(0.0, 0.0, 0.0);
    // How deep inside a hollow droplet's interior this pixel is; 0 unless
    // droplet_ring_frac is on.
    float  ringIn = 0.0;
    // brief AG-b: the dye IDENTITY of the mass under this pixel, soft-maxed
    // (w^4, the oil colour's trick) over the HOLE blobs' hashes in c.w, so two
    // merging holes blend smoothly. Accumulated only while dye_lum_vary or
    // dye_hue_vary is on; otherwise the loop below is the old loop.
    float  idS = 0.0, idW = 0.0, idH = 0.0;   // idH = the holes' own (negative) field
    const bool dyeIdOn = LA_DYE_AMT > 0.0005 &&
                         (LA_DYE_LUM_VARY > 0.0005 || LA_DYE_HUE_VARY > 0.0005);
    int nb = (int)LA_BLOB_COUNT;
    const float thresh = LA_THRESHOLD;
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
        float  sup = B.a.z * LA_SUPPORT_SCALE;              // support radius
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
        } else if (dyeIdOn) {
            float w2 = w * w, w4 = w2 * w2;
            idS += B.c.w * w4;
            idW += w4;
            idH += w * B.a.w;
        }
    }

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
    const float2 axP = float2(LA_CAM_AXIS_X * aspect, LA_CAM_AXIS_Y);

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
    float2 lampP = float2(LA_LAMP_X * aspect, LA_LAMP_Y) - axP;
    float2 lampD = lampP / max(length(lampP), 1e-5);
    float  dyeW  = LA_DYE_DEPTH_W;
    float  dyeZ  = LA_DYE_DEPTH + LA_DYE_TILT * dot(pp - axP, lampD);
    float depSum = dyeZ * dyeW, depW = dyeW;
    if (LA_DROPS_ON > 0.5) {
        const int gw = (int)LA_DROP_GRID_W, gh = (int)LA_DROP_GRID_H;
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
                    float  ds = R * LA_DROP_SUPPORT;
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
                        [branch] if (LA_FOV_K > 0.0) {
                            float2 toAx = axP - float2(D.x * aspect, D.y);
                            float  la   = length(toAx);
                            [branch] if (la > 1e-5) {
                                av   = toAx / la;
                                tanT = LA_FOV_K * la;
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
                        float4 S  = AcidDrops[(uint)LA_DROP_RING_BASE + cell.x + di];
                        float  c2 = un.x * un.x - un.y * un.y;
                        float  s2 = 2.0 * un.x * un.y;
                        float  c3 = un.x * (4.0 * un.x * un.x - 3.0);
                        float  s3 = un.y * (3.0 - 4.0 * un.y * un.y);
                        float  wv = S.x * c2 + S.y * s2 + S.z * c3 + S.w * s3;
                        float  wd = 2.0 * (S.y * c2 - S.x * s2)
                                  + 3.0 * (S.w * c3 - S.z * s3);   // d/d(phi)
                        float  Rp = R * (1.0 + wv);
                        float  bw = max(saturate(LA_DROP_RING_WIDTH) * ds, 1e-6);
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
                        float  gW = gate * AcidDiffract(bw, LA_DIFFR_SCALE, LA_DIFFRACTION);
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
                    float  gS = gate * AcidDiffract(R, LA_DIFFR_SCALE, LA_DIFFRACTION);
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
            float pun = max(field, 1.0) * LA_DROP_WEIGHT;
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
            float pup = LA_DROP_OIL_W + max(thresh - field, 0.0);
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
        [branch] if (ringIn > 0.0 && LA_DROP_RING_LIFT > 0.0005) {
            float k  = saturate(LA_DROP_RING_LIFT);
            field += ringIn * k * 0.25 * thresh;
            float wl = ringIn * ringIn; wl = wl * wl * k;
            colSum += laOil[3].rgb * wl;
            colW   += wl;
        }
    }

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
    [branch] if (LA_DOF_MAX_PX > 0.0) {
        float2 qo  = pp - axP;
        float  rr  = length(qo);
        float  fz  = LA_FOCUS_DEPTH + LA_FIELD_CURVE * rr * rr
                             + LA_TILT_AMT * dot(qo, float2(LA_TILT_COS, LA_TILT_SIN));
        float  gm  = abs(LA_TILT_AMT) + 2.0 * abs(LA_FIELD_CURVE) * rr;
        float  tol = 0.5 * LA_FOCUS_BAND * gm;
        float  dz  = abs(depSum / max(depW, 1e-6) - fz);
        // ...and it falls away FAST. The user's focus reference is a macro
        // shot where the one plane in focus resolves fine texture and
        // everything off it is already a wash: a shallow depth of field with a
        // STEEP shoulder, not a gentle ramp. So the radius leaves zero the
        // moment the element clears the sharp band and is most of the way to
        // the clamp within a tenth of the depth range -- x*(2-x) rather than a
        // straight line, over a deliberately short span.
        float  x   = saturate((dz - tol) * LA_COC_SPAN_INV);
        outCoc = LA_DOF_MAX_PX * x * (2.0 - x);
    }

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
    // LA_SOFTNESS is the radius in uv-y (authored in px at 1440p), and the field
    // units it takes here are that distance times the local |grad|.
    float  soft = LA_SOFTNESS;
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
    float  bmin = saturate(LA_BAND_MIN);
    float  aaF  = fwidth(field) * LA_AA_SCALE + 1e-4;
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
    float  edgeW = clamp(max(LA_OIL_EDGE_FRAC, 0.02) * lensR, LA_RIM_WIDTH * 2.0, 0.060);
    if (LA_OIL_EDGE_MODE > 0.5) edgeW = max(LA_RIM_WIDTH * 2.5, 1e-5);
    // ...and no film edge may be tighter than the lens's own blur circle.
    if (soft > 1e-6) edgeW = max(edgeW, soft * 2.0);
    // ...nor than the floor: a droplet's film has to thin over the same few
    // px a mass's does, or its edge is a cut.
    edgeW = max(edgeW, bmin * 3.0 * PX1440);
    float  thk   = 1.0;                       // 1 = full-thickness oil
    // oil_transparency needs the same thickness proxy even when the soft
    // thin edge itself is off: a transparent film MUST be clearest where it
    // is thinnest, or it reads as a sheet of tinted glass cut with scissors.
    if (LA_OIL_THIN_EDGE > 0.0005 || LA_OIL_TRANSP > 0.0005) {
        thk = smoothstep(0.0, edgeW, sdf);
        // ---- oil_edge_curve --------------------------------------------
        // The user, on the panel: "it looks like it goes from green to black,
        // then stops; it should be more S-curved: distance vs mix of colours".
        // smoothstep is an S in the middle but its ENDS are only C1 -- the
        // ramp arrives at full thickness (and at black) with a visible knee.
        // smootherstep (6t^5-15t^4+10t^3) is flat to second order at both
        // ends, so both knees vanish. Same band, same width: a profile
        // change only, and the branch is the original line at 0.
        [branch] if (LA_OIL_EDGE_CURVE > 0.0005) {
            float t = saturate(sdf / max(edgeW, 1e-7));
            thk = lerp(thk, t * t * t * (t * (t * 6.0 - 15.0) + 10.0),
                       saturate(LA_OIL_EDGE_CURVE));
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
    float  refrW = max(LA_RIM_WIDTH * ((LA_REFR_WIDTH > 0.0005) ? LA_REFR_WIDTH : 7.0), 1e-4);
    if (LA_OIL_THIN_EDGE > 0.0005) refrW = lerp(refrW, max(refrW, edgeW), saturate(LA_OIL_THIN_EDGE));
    float  eb = sdf / refrW;
    float  edgeB = exp(-eb * eb);
    float2 rOff = (grad / gl) * (LA_REFRACTION * edgeB);
    // ---- body refraction (oil_refract_body) ------------------------------
    // The edge band above bends the ink only in a hairline around each disc.
    // A real film of oil has thickness EVERYWHERE, so what you see through
    // its middle is displaced too — and unevenly, because the surface is not
    // flat. Offset along the gradient of (field + a slow fbm bump): the field
    // part leans the whole disc's contents one way, the fbm part is what makes
    // the marbling wobble as it passes under. Capped to a unit vector, so the
    // key is the offset in uv units and can never run away.

    if (LA_OIL_REFR_BODY > 0.0005) {
        float2 bq = pp * 3.1 + float2(LA_TIME * 0.006, -LA_TIME * 0.0045);
        const float bh = 0.05;
        float  b0 = AcidFbm(bq);
        float2 bg = float2(AcidFbm(bq + float2(bh, 0.0)) - b0,
                           AcidFbm(bq + float2(0.0, bh)) - b0) / bh;
        float2 nd = (grad / gl) * 0.35 + bg * 0.85;
        nd /= max(1.0, length(nd));
        rOff += nd * (LA_OIL_REFR_BODY * cov);
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
    if (LA_OIL_INK_BLUR > 0.0005) {
        float bw = saturate(LA_OIL_INK_BLUR) * thk * cov;
        if (bw > 0.002) {
            float2 bo = texelSize * (2.0 + 11.0 * saturate(LA_OIL_INK_BLUR));
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
        C *= lerp(1.0, diffuse, saturate(LA_INK_SHADING));
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
    if (LA_INK_WATER > 0.5) {
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
    float bandIn = saturate(inkLum * LA_INK_GAIN + LA_INK_BIAS);
    float band   = AcidBand(bandIn, max(LA_INK_LEVELS, 1.0), clamp(LA_INK_SOFT, 0.001, 0.5));
    // Keep the BOTTOM of the range continuous: posterising the fluid's low-dye
    // regions down to level 0 turns every faint wisp into a hard-edged black
    // cut-out. Below the first band edge, fade back to the smooth value.
    band = lerp(bandIn, band, smoothstep(0.0, 1.5 / max(LA_INK_LEVELS, 1.0), bandIn));
    // chroma-preserving quantise of the sim's own colour
    inkC = C * (band / max(inkLum, 1e-4));
    // 4-stop ramp indexed by the banded luminance
    float  rs = saturate(band) * 3.0;
    int    ri = (int)floor(rs);
    float3 rampC = lerp(laInk[min(ri, 3)].rgb, laInk[min(ri + 1, 3)].rgb, rs - ri);
    // Regional hue variation: rotate the ramp by the DYE's own hue so the ink
    // still drifts (purple <-> magenta, red <-> orange) instead of reading as
    // one flat duotone. 0 = pure duotone.
    if (LA_INK_HUE_VARY > 0.01) {
        float2 cc = float2(C.r - 0.5 * (C.g + C.b), 0.8660254 * (C.g - C.b));
        // atan2(0,0) is NaN, and dye-free pixels are exactly (0,0). The NaN
        // propagates through lerp() and blacks the pixel out even where the
        // OIL covers it - that is what painted hard, fluid-shaped black smears
        // across the oil discs. Guard on the chroma magnitude.
        if (dot(cc, cc) > 1e-10)
            rampC = AcidHueShift(rampC, atan2(cc.y, cc.x) * LA_INK_HUE_VARY * 0.31831);
    }
    // Complement lock: clamp the ramp's hue into a window centred on the oil's
    // opposite. Applied AFTER the regional variation, so the ink still drifts
    // — it just drifts inside the complementary window instead of wandering
    // toward the oil hue. Near-greys have no hue to clamp, so skip them.
    if (LA_INK_LOCK > 0.5) {
        float3 rhsv = AcidRgb2Hsv(rampC);
        if (rhsv.y > 0.04) {                             // near-greys have no hue
            float d = LA_INK_TARGET_HUE - rhsv.x * 360.0;
            d = d - 360.0 * floor(d / 360.0 + 0.5);      // wrap to [-180, 180]
            float half = max(LA_INK_LOCK_SPAN, 0.0) * 0.5;
            if (abs(d) > half) rampC = AcidHueShift(rampC, d - sign(d) * half);
        }
    }
    inkC = lerp(inkC, rampC, saturate(LA_INK_MIX));
    // dark seams where |grad dye| is steep (the marbled acrylic-pour edging)
    {
        float2 st = texelSize * LA_SEAM_SCALE;
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
        float gBands = gm * LA_INK_GAIN * max(LA_INK_LEVELS, 1.0);
        inkC *= 1.0 - LA_SEAM_STR * smoothstep(LA_SEAM_LO, max(LA_SEAM_HI, LA_SEAM_LO + 1e-3), gm)
                             * smoothstep(1.1, 2.1, gBands);
    }
    }   // end ink_mode == bands

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
    // brief AJ: how far, in degrees, a droplet's RIM is rotated past the film
    // it stands in, so that a rim can carry a seam that is not under it. 0 =
    // today, and with boundary_reflect_r 0 it stays 0 for every pixel.
    float rimHueD = 0.0;
    // brief BV: the patch shares, kept for the equal-load / lift split below
    // (0 when both hues are off).
    float bvK2 = 0.0, bvK3 = 0.0;
    [branch] if (LA_HUE2_AMT > 0.0005 || LA_HUE3_AMT > 0.0005) {
        float mixV = AcidMixAt(uv);
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
        const float MIXSEAM = 0.62;   // the band's centre: THE seam (brief AJ)
        float k2 = smoothstep(0.56, 0.68, mixV) * saturate(LA_HUE2_AMT);
        float k3 = 0.0;
        // The droplets INSIDE a mass take their own share of it (the ref's
        // cyan-lit specks in the black). 1 = the same as the film.
        if (fieldB < thresh) k2 *= saturate(LA_CRUST_HUE_MIX);
        // ROTATE the hue by the patch's share of it -- do NOT cross-fade to
        // the rotated colour. lerp(magenta, cyan, 0.5) in RGB is GREY, and
        // that is exactly what the first render produced: pale lavender fog
        // instead of the reference's cyan. A dye shifts a hue; it does not
        // blend a colour with its own opposite. Rotating also keeps
        // saturation and value flat across the whole patch, so the film is as
        // vivid in the second colour as it is in the first.
        oilC = AcidHueShift(oilC, LA_HUE2_DEG * k2);
        // ...and an optional THIRD hue off the other end of the SAME field,
        // so a second colour costs no second field and no second fetch.
        [branch] if (LA_HUE3_AMT > 0.0005) {
            // film_hue3_share (brief BV): both thresholds move down together,
            // so the hue3 end of the field shrinks (1 = today's 0.18 / 0.46).
            float h3d = (1.0 - LA_HUE3_SHARE) * 0.16;
            k3 = (1.0 - smoothstep(0.18 - h3d, 0.46 - h3d, mixV)) * saturate(LA_HUE3_AMT);
            if (fieldB < thresh) k3 *= saturate(LA_CRUST_HUE_MIX);
            oilC = AcidHueShift(oilC, LA_HUE3_DEG * k3);
        }
        bvK2 = k2; bvK3 = k3;
        // ---- BOUNDARY REFLECTION REACH (boundary_reflect_r, brief AJ) ----
        // The user, on the hue2 seam live: "whatever algo is mixing the oil
        // boundary is insanely good", "and the way the bubbles reflect it,
        // accurately", "chef's kiss" -- then: "turn up the radius of effect
        // maybe, so further particles also reflect it on the boundary."
        //
        // Today every rim term (mass_rim, the droplet lens's meniscus and
        // specular, the bright-field halo, oil_glow, the swarm's caustics) is
        // tinted with oilC -- the film colour AT THAT PIXEL. So a droplet
        // carries the seam's colour only while it is standing IN the seam,
        // i.e. inside the 0.56..0.68 band of the mix field, which at
        // film_hue2_scale 0.35 is about one droplet across. That band's width
        // IS today's reach, and there is no radius to turn up: the term that
        // gives a rim the neighbouring film's colour is simply k2 at the
        // pixel, bounded by the width of the field's own transition.
        //
        // So give the RIMS a second, much longer reach: find how far away the
        // nearest seam is and rotate the rim's hue toward the seam's own hue
        // with a smooth falloff over that distance. The film is untouched,
        // nothing is blurred and no texture is sampled -- the probe walks the
        // 20x12 field, which is 240 floats already resident in the cbuffer, so
        // a "tap" is four indexed loads next to the dozens of TEXTURE taps the
        // depth of field alone already spends. Eight of them, no long tap
        // chains and no extra blur, so the rims stay exactly as sharp.
        //
        // Four of the taps are a central difference at a QUARTER of the reach
        // -- the field's large-scale slope, which points at the nearest seam.
        // (Not the analytic derivative of the bilinear: its x term vanishes on
        // every vertical cell line, which would print the 20x12 grid.) The
        // other four MARCH that direction in equal steps and take the first
        // segment whose ends straddle the seam. Marching, rather than testing
        // only the far end of one long ray, is what makes the reach monotonic:
        // a single ray that runs past a patch and out the other side comes
        // back with the SAME sign and reports no seam at all, so raising the
        // radius was losing droplets that a shorter radius had found.
        [branch] if (LA_REFLECT_R > 0.0005) {
            float  R  = max(LA_REFLECT_R, 1e-4);
            float  s0 = mixV - MIXSEAM;
            float  e  = R * 0.25;
            float2 ex = float2(e / aspect, 0.0), ey = float2(0.0, e);
            float2 g  = float2(AcidMixAt(uv + ex) - AcidMixAt(uv - ex),
                               AcidMixAt(uv + ey) - AcidMixAt(uv - ey));
            // toward the seam: down the slope from above it, up from below
            float2 dir = (g / max(length(g), 1e-6)) * -sign(s0);
            float dN = 1.0, sp = s0;
            [unroll] for (int mi = 1; mi <= 4; mi++) {
                float t  = (float)mi * 0.25;
                float2 o = dir * (R * t);
                float  sn = AcidMixAt(uv + float2(o.x / aspect, o.y)) - MIXSEAM;
                if (dN >= 1.0 && sp * sn < 0.0)
                    dN = t - 0.25 * (1.0 - saturate(-sp / (sn - sp)));
                sp = sn;
            }
            // near = full, far = 0, smootherstep so the nearest stay strongest
            // and the reach dies out with no edge of its own.
            float w = saturate(LA_REFLECT_AMT) * (1.0 - smoothstep(0.0, 1.0, dN));
            // The seam's own colour is the band's MIDPOINT -- half the
            // rotation, which off a blue film at film_hue2 180 is the magenta
            // the user photographed. Rotating by the DELTA (k at the seam
            // minus k here) means a rim already standing in the seam changes
            // by nothing, so the near droplets the user already loves keep
            // exactly the colour they have.
            float k2s = 0.5 * saturate(LA_HUE2_AMT);
            if (fieldB < thresh) k2s *= saturate(LA_CRUST_HUE_MIX);
            rimHueD = LA_HUE2_DEG * (k2s - k2);
            [branch] if (LA_HUE3_AMT > 0.0005) {
                float h3d = (1.0 - LA_HUE3_SHARE) * 0.16;
                float h3lo = 0.18 - h3d, h3hi = 0.46 - h3d;
                float k3s = (1.0 - smoothstep(h3lo, h3hi, MIXSEAM)) * saturate(LA_HUE3_AMT);
                if (fieldB < thresh) k3s *= saturate(LA_CRUST_HUE_MIX);
                rimHueD += LA_HUE3_DEG * (k3s - k3);
                // brief BV (AJ seam reach): the march above only looks for the
                // hue2 seam, so a rim beside a hue3 patch never took its seam.
                // March to the hue3 seam too (same slope, four more taps) and
                // blend the two targets by reach, normalised so they never add
                // past one full seam rotation.
                const float S3 = 0.5 * (h3lo + h3hi);
                float  t0 = mixV - S3;
                float2 dir3 = (g / max(length(g), 1e-6)) * -sign(t0);
                float dN3 = 1.0, tp = t0;
                [unroll] for (int mj = 1; mj <= 4; mj++) {
                    float t  = (float)mj * 0.25;
                    float2 o = dir3 * (R * t);
                    float  tn = AcidMixAt(uv + float2(o.x / aspect, o.y)) - S3;
                    if (dN3 >= 1.0 && tp * tn < 0.0)
                        dN3 = t - 0.25 * (1.0 - saturate(-tp / (tn - tp)));
                    tp = tn;
                }
                float w3 = saturate(LA_REFLECT_AMT) * (1.0 - smoothstep(0.0, 1.0, dN3));
                float k3m = 0.5 * saturate(LA_HUE3_AMT);
                float k2m = smoothstep(0.56, 0.68, S3) * saturate(LA_HUE2_AMT);
                if (fieldB < thresh) { k3m *= saturate(LA_CRUST_HUE_MIX); k2m *= saturate(LA_CRUST_HUE_MIX); }
                float d3 = LA_HUE2_DEG * (k2m - k2) + LA_HUE3_DEG * (k3m - k3);
                rimHueD = (rimHueD * w + d3 * w3) / max(w + w3, 1.0);
            } else {
                rimHueD *= w;
            }
        }
    }
    // rise_bottom_light: a lava lamp is lit and heated from BELOW, so the wax
    // near the base is hotter and brighter and cools on the way up. One
    // vertical ramp on the oil (not on the ink: the glass is not lit, the wax
    // is), applied here so it also feeds the film's own scattered light.
    float lampG = 1.0;
    if (LA_RISE_BOTTOM_LIGHT > 0.0005) {
        lampG = lerp(1.0, 0.80 + 0.50 * smoothstep(0.0, 1.0, uv.y), saturate(LA_RISE_BOTTOM_LIGHT));
        oilC *= lampG;
    }
    // ---- COLOUR SCHEMES: equal load (brief BV) -----------------------------
    // film_equal_load, per pixel, the BASE film (1 - k2 - k3) only -- patches
    // are never touched, so a yellow patch stays bright: dimmed in linear
    // light to the luminance the same S/V would have at magenta (325), never
    // brightened, so a yellow/green/cyan film loads the panel like magenta.
    // 0 = today: the branch is skipped. (The yellow-patch lifts were dropped:
    // patches sit at V 0.92..1 with nothing to lift, MAD 0.49 / 0.01.)
    [branch] if (LA_EQUAL_LOAD > 0.0005) {
        const float3 WY = float3(0.2126, 0.7152, 0.0722);
        float3 hsv = AcidRgb2Hsv(oilC);
        float  wb  = saturate(LA_EQUAL_LOAD) * saturate(1.0 - bvK2 - bvK3);
        float  Y   = dot(DsToLin(oilC), WY);
        float  Yt  = dot(DsToLin(AcidHsv2Rgb(float3(0.9027778, hsv.y, hsv.z))), WY);
        float  gl  = lerp(1.0, min(1.0, Yt / max(Y, 1e-6)), wb);
        oilC = DsToSrgb(DsToLin(oilC) * gl);
    }

    // ---- DYE THE DARK MASSES (brief AG / AM) -----------------------------
    // TRACE (2026-09-22, branch dye4) -- where a dark-mass pixel gets its
    // final colour in ink_mode=water, end to end:
    //   1. src\shaders.h, acid display literal, ~line 2096:
    //      `float3 col = lerp(inkC, oilC, alpha);`  -- inside a mass the oil
    //      field is BELOW the threshold, so alpha -> 0 and the pixel IS inkC.
    //   2. same file, ~line 1618:  `if (LA_INK_WATER > 0.5) inkC = InkWater(C0,..)`
    //      LA_INK_WATER = (ink_mode == water) (src\acid_slots.h).
    //      acid-rise-12 sets ink_mode=water, so the whole `else` bands branch
    //      under it is dead code for this preset.
    //   3. InkWater (~line 919) returns `lerp(ikPaper.rgb, tint*.., op)`. In a
    //      mass the sim dye density is ~0, so op ~= 0 and the pixel is
    //      ikPaper.rgb = [ink] paper_color = 0 0 0. THAT is the black.
    //   4. laInk[] (= effInk, the ramp the first two attempts dyed on the CPU)
    //      is read in exactly TWO places in this whole shader: the bands
    //      branch at ~line 1634 (dead here) and the toe_tint lift at ~line
    //      2589, gated on LA_TOE_TINT = toe_tint, which acid-rise-12 leaves at its
    //      0 default. So dyeing effInk could not change one bit of this preset
    //      -- which is what the byte-identical four-hue sheet was measuring,
    //      and why both earlier fixes read delta 0.
    // So the dye is applied HERE, on inkC, as a deep translucent wax:
    // thickness = depth into the mass (-sdf), light left = exp(-depth/w), so
    // the thin edge passes most of the lamp and the thick core keeps a floor
    // of the same hue instead of crushing to black. Gated by (1 - cov) so the
    // film is untouched, by the lamp ramp so the wax is lit from below like
    // the oil is, and rolled off where the ink film is actually BRIGHT -- this
    // dyes the black, nothing else. crust and mass_rim are added to `col`
    // further down and still read against it.
    // LA_DYE_AMT = 0 (dye_sat 0 or dye_lum 0) skips the branch: byte-identical.
    // brief BK: ...unless the droplets carry a dye of their own (DYE_DROP_RGB
    // >= 0, see below); it is -1 whenever the split is off, so this is the old
    // test for every existing preset.
    [branch] if (LA_DYE_AMT > 0.0005 || LA_DYE_DROP_RGB > -0.5) {
        // value 1: the amount and the thickness carry the level, so the hue is
        // as vivid deep in the mass as it is at the rim.
        float3 dyeC = InkHsv2Rgb(float3(LA_DYE_HUE, LA_DYE_SAT, 1.0));
        float dIn  = max(-sdf, 0.0);                 // p-units into the mass
        float Tw   = exp(-dIn / 0.055);              // light left after the wax

        // ---- brief AG-b: dye GRADIENTS, so the masses are not all one wax --
        // User: "there should probably be gradients or something, so they
        // aren't all the same." Every mass gets an IDENTITY id in 0..1:
        //   * a hole blob carries a hash in c.w (CPU, fixed for its life,
        //     re-rolled only when it respawns off-screen); the blob loop
        //     soft-maxed them with w^4 into idS / idW, so merging holes blend;
        //   * a gap between oil blobs has no hole near it (idW ~ 0) and falls
        //     back to a slow fbm at mass scale (~0.4 screen) that climbs with
        //     rise_speed, so a mass keeps its shade while it rises. NOT the
        //     hue2 field: that would couple the dye to the film's second hue.
        // dye_lum_vary scales the level by 1 +- value, dye_hue_vary turns the
        // hue by +- value degrees, and dye_thick_hue turns it with thickness
        // (thin edge = dye_hue, thick core = dye_hue + value; Tw saturates in
        // a small mass, so it stays edge-coloured and a big one gets a core).
        // MASSES only: mKv is BK's mass-territory test (the blob-only surface,
        // 3 px into the sheet), 0 on a droplet hole, so droplets keep dyeC.
        // All three 0 skips the block and dyeC is the old line.
        [branch] if (LA_DYE_LUM_VARY > 0.0005 || LA_DYE_HUE_VARY > 0.0005 ||
                     abs(LA_DYE_THICK_HUE) > 0.05) {
            float sBv = (fieldB - thresh) / max(length(gradB), 1e-6);
            float mKv = 1.0 - smoothstep(0.0, 3.0 * PX1440 + saturate(LA_DYE_SMOKE) * 0.030, sBv);
            float dh = LA_DYE_THICK_HUE * (1.0 - Tw);    // Tw = 1 at the edge
            float lk = 1.0;
            [branch] if (dyeIdOn) {
                float  fb  = AcidFbm(float2(pp.x, pp.y + LA_TIME * LA_DYE_ID_RISE) * 2.5
                                     + float2(7.31, 1.93));
                float  idF = saturate(0.5 + (fb * 1.143 - 0.5) * 2.2);
                // A hole's identity counts only where that hole CARVED the
                // mass: the oil-only field (fieldB minus the holes' own) is
                // above the isoline there. A hole that has drifted into a gap
                // carves nothing, and would otherwise print a disc of its own
                // colour inside that gap's mass (first sheet, hue_vary 90).
                float  kH  = smoothstep(0.0, 0.02, idW) *
                             smoothstep(thresh - 0.05, thresh + 0.20, fieldB - idH);
                float  id  = lerp(idF, idS / max(idW, 1e-20), kH);
                float  sg  = 2.0 * id - 1.0;
                dh += LA_DYE_HUE_VARY * sg;
                lk  = 1.0 + LA_DYE_LUM_VARY * sg;          // 0.5 .. 1.5
            }
            dyeC = lerp(dyeC, InkHsv2Rgb(float3(LA_DYE_HUE + dh * (1.0 / 360.0), LA_DYE_SAT, lk)), mKv);
        }

        // ---- brief BL: dye_lamp_follow (ambient dye) ---------------------
        // The lamp lights the OIL; the fluid under it should only see a dim
        // ambient. At 1 (today) the dye rides lampG (rise_bottom_light) and
        // the thin-edge profile lerp(dye_core, 1, Tw); toward 0 both fade out
        // and the dye sits at its thick-core level everywhere: a colour the
        // lamp never lifts. At 1 the branch is skipped and lampGd IS lampG.
        float lampGd = lampG;
        [branch] if (LA_DYE_LAMP_FOLLOW < 0.9995) {
            float fl = saturate(LA_DYE_LAMP_FOLLOW);
            lampGd = lerp(1.0, lampG, fl);
            Tw *= fl;
        }
        float inkL = max(inkC.r, max(inkC.g, inkC.b));
        float dkG  = 1.0 - smoothstep(0.0, 0.55, inkL);
        float wD   = (1.0 - cov) * dkG;
        float3 dyeAdd = dyeC * (LA_DYE_AMT * lerp(LA_DYE_CORE, 1.0, Tw) * lampGd * wD);
        [branch] if (LA_DARK_SAT > 0.0005)
            dyeAdd = DarkSat(dyeC * LA_DYE_AMT, LA_DYE_AMT) * (lerp(LA_DYE_CORE, 1.0, Tw) * lampGd * wD);
        // ---- brief BK: masses vs droplets, and smoke ---------------------
        // "Too bubbly": the dye on EVERY droplet is what reads as bubbles.
        // Mass vs droplet is decided by WHICH sim object made the dark, not
        // by size or depth (a mass's rim is as thin as a droplet): fieldB is
        // the blob field before any droplet is punched in, so fieldB below
        // the threshold is a mass (a gap between blobs) and a dark pixel with
        // fieldB above it is a droplet hole in the sheet -- the same test the
        // crust already keys off. sB is that blob-only surface as a distance;
        // mK = 1 in mass territory, easing to 0 over 3 px (plus the smoke's
        // reach, so a mass's own smoke is never cut by the droplet colour)
        // into the sheet. The two territories take two colours:
        //   masses   = dye_hue/sat/lum * dye_masses  (folded into DYE_AMT on
        //              the CPU while the split is on)
        //   droplets = DYE_DROP_RGB: dye_droplet_hue/sat/lum (each -1 =
        //              inherit the mass dye) * dye_droplets, built on the CPU
        //              and packed as three 8-bit channels r + 256 g + 65536 b
        //              in one float (exact: < 2^24); -1 = split off.
        // dye_smoke: the old gate is (1 - cov), a hard stop at the isoline,
        // and the thin-edge-brightest profile, i.e. a lit disc. Smoke
        // instead thickens GRADUALLY inward, leaks wOut out under the thin
        // film (the film's transparency shows it, tinted), carries no edge
        // peak and is broken into wisps by a slow fbm that both modulates the
        // density and warps the edge. Where sdf is not a real distance (isoOk
        // < 1) the gate falls back to plain coverage. Smoke is a MASS property
        // (weighted by mK): a droplet is narrower than the smoke's inward ramp,
        // so smoking it only dimmed it -- a second dye_droplets (first sheet).
        // Split off and smoke 0 skip all of this: dyeAdd above is the old line.
        [branch] if (LA_DYE_SMOKE > 0.0005 || LA_DYE_DROP_RGB > -0.5) {
            float s    = saturate(LA_DYE_SMOKE);
            float wOut = s * 0.030;
            float sB   = (fieldB - thresh) / max(length(gradB), 1e-6);
            float mK   = 1.0 - smoothstep(0.0, 3.0 * PX1440 + wOut, sB);
            float prof = lerp(LA_DYE_CORE, 1.0, Tw);
            float gate = 1.0 - cov;
            float leakK = 0.0;
            [branch] if (s > 0.0005) {
                float n  = saturate(AcidFbm(pp * 6.0 + float2(LA_TIME * 0.007,
                                                              -LA_TIME * 0.011)) * 1.143);
                float dS = -sdf + s * 0.030 * (n - 0.5);
                gate = lerp(gate, smoothstep(-wOut, 0.010 + 0.080 * s, dS), isoOk * mK);
                prof = lerp(prof, lerp(prof, 0.75, s) * lerp(1.0, 0.30 + 1.40 * n, s), mK);
                // brief BP: the LEAK (the smoke under the film, cov > 0) takes
                // the FILM's hue. A dye of another hue seen through the film
                // mixes additively toward grey -- the "middle values bad" of
                // the smoke sheet. Inside the mass (cov = 0) nothing changes.
                leakK = cov * mK;
            }
            float3 massC = dyeC * LA_DYE_AMT;
            [branch] if (leakK > 0.0005) {
                float3 fhs = AcidRgb2Hsv(oilC);
                massC = lerp(massC, InkHsv2Rgb(float3(fhs.x, LA_DYE_SAT, 1.0)) * LA_DYE_AMT, leakK);
            }
            float3 dropC = massC;
            float  dropL = LA_DYE_AMT;
            [branch] if (LA_DYE_DROP_RGB > -0.5) {
                // hue8 + 256 sat8 + 65536 lum8 (brief BP; was 8-bit RGB)
                float v  = LA_DYE_DROP_RGB;
                float cl = floor(v * (1.0 / 65536.0));
                float cs = floor((v - cl * 65536.0) * (1.0 / 256.0));
                float ch = v - cl * 65536.0 - cs * 256.0;
                dropL = cl * (1.0 / 255.0);
                dropC = InkHsv2Rgb(float3(ch * (1.0 / 256.0), cs * (1.0 / 255.0), 1.0)) * dropL;
            }
            [branch] if (LA_DARK_SAT > 0.0005) {
                massC = DarkSat(massC, LA_DYE_AMT);
                dropC = DarkSat(dropC, dropL);
            }
            dyeAdd = lerp(dropC, massC, mK) * (prof * lampGd * gate * dkG);
        }
        inkC += dyeAdd;
    }
    oilC *= lerp(1.0, 0.93 + 0.14 * AcidFbm(pp * 7.0 + float2(LA_TIME * 0.010,
                                                              -LA_TIME * 0.007)),
                 saturate(LA_OIL_TEXTURE));
    // Thin film: the ink underneath modulates the oil, but only BROADLY. The
    // raw per-pixel band printed the dye's texel structure onto the oil as
    // short horizontal dashes, so drive it from a wide, heavily smoothed
    // luminance tap instead of the pixel's own band.
    if (LA_TRANSLUCENCY > 0.002) {
        float2 bt = texelSize * 9.0;
        float lw = length(Dye.SampleLevel(linearClamp, uv + bt, 0).rgb)
                 + length(Dye.SampleLevel(linearClamp, uv - bt, 0).rgb)
                 + length(Dye.SampleLevel(linearClamp, uv + float2(bt.x, -bt.y), 0).rgb)
                 + length(Dye.SampleLevel(linearClamp, uv - float2(bt.x, -bt.y), 0).rgb);
        oilC *= lerp(1.0, 0.88 + 0.30 * saturate(lw * 0.45 * LA_INK_GAIN), saturate(LA_TRANSLUCENCY));
    }
    // ---- thin film: the oil is a LENS, not a cut-out (oil_thin_edge) -----
    // refs 4/5/6: there is NO stroked boundary. The film thins to nothing at
    // the edge, so the light reaching the eye there has crossed the ink AND a
    // sliver of oil: the colour slides toward the PRODUCT of the two (orange
    // over red ink goes red; white over teal goes teal) and loses level, over
    // a band a few percent of the lens's own radius. That soft thickness
    // gradient is the whole reason a real oil disc is not a sticker.
    float3 oilThinC = oilC;
    if (LA_OIL_THIN_EDGE > 0.0005) {
        // saturate(): the factor is capped at 1, so a thinning film can only
        // go DARKER and toward the ink's hue, never brighter and paler. Left
        // uncapped it lifted every edge over bright ink into a milky lobe.
        float3 prod = oilC * saturate(0.30 + 1.10 * inkC);
        float  tt   = 1.0 - thk;
        oilThinC = prod;
        oilC = lerp(oilC, prod, saturate(tt * tt * LA_OIL_THIN_EDGE));
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
    if (LA_OIL_TRANSP > 0.0005) {
        float k = saturate(LA_OIL_TRANSP);
        float fb = saturate(AcidFbm(pp * 2.2 + float2(LA_TIME * 0.004,
                                                      -LA_TIME * 0.003)) * 1.143);
        float thick = thk * lerp(1.0, 0.35 + 1.30 * fb, saturate(LA_OIL_FILM_BUMP));
        float  mx   = max(oilC.r, max(oilC.g, oilC.b));
        float3 oilN = (mx > 1e-4) ? saturate(oilC / mx) : float3(0.0, 0.0, 0.0);
        float3 T    = exp(-max(LA_OIL_ABSORB, 0.0) * max(thick, 0.0) * (1.0 - oilN));
        float  Tav  = dot(T, float3(0.3333333, 0.3333333, 0.3333333));
        // inkC is already the REFRACTED (and, with oil_ink_blur, defocused)
        // ink at this pixel: the marbling under the film, seen through it.
        float3 glassC = inkC * T + oilC * (1.0 - Tav);
        oilC   = lerp(oilC, glassC, k);
        filmOp = lerp(1.0, saturate(1.0 - Tav), k);
    }

    // ---- surface relief: specular (oil_specular) + thin-film iridescence --
    // Kept deliberately weak: the references show almost no specular, because
    // the rig is BACKLIT. What little there is comes from the lens curvature
    // near the edge plus a slow thickness ripple, never a pin-point.
    float specAmt = 0.0;
    if (LA_OIL_SPECULAR > 0.0005 || LA_OIL_IRID > 0.0005) {
        float2 q  = pp * 4.3 + float2(LA_TIME * 0.006, -LA_TIME * 0.0045);
        float  f0 = AcidFbm(q);
        if (LA_OIL_SPECULAR > 0.0005) {
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
            specAmt = (sp * 0.30 + fr * 0.12) * saturate(LA_OIL_SPECULAR);
            oilC += specAmt;
        }
        if (LA_OIL_IRID > 0.0005) {
            float3 ir = 0.5 + 0.5 * cos(6.2831853 * (f0 * 3.0 + float3(0.0, 0.33, 0.67)));
            float  iw = saturate(LA_OIL_IRID) * lerp(0.30, 1.0, 1.0 - thk) * 0.45;
            oilC *= lerp(1.0, ir * 1.5, iw);
        }
    }
    // ---- rim variation (rim_vary / rim_ink_follow) -----------------------
    // The reference rim is NOT a uniform stroke: it thickens and brightens
    // where the ink under it is bright, and thins to nothing along other
    // stretches. Two multipliers, both EXACTLY 1.0 when the keys are 0, so
    // every shipped ini renders unchanged.
    float rimMul = 1.0, haloMul = 1.0;
    if (LA_RIM_VARY > 0.0005) {
        // low-frequency noise of POSITION (features ~1/3 of the frame) with a
        // slow drift, contrast-stretched so it really reaches 0 and 1
        float rn = AcidFbm(pp * 3.3 + float2(LA_TIME * 0.011, -LA_TIME * 0.008)) * 1.143;
        rn = saturate((rn - 0.5) * 1.9 + 0.5);
        float rv = saturate(LA_RIM_VARY);
        rimMul  = lerp(1.0, 0.40 + 1.50 * rn, rv);                  // 0.4x..1.9x width
        haloMul = lerp(1.0, smoothstep(0.18, 0.72, rn) * 1.55, rv); // dies / thickens
    }
    if (LA_RIM_INK_FOLLOW > 0.0005) {
        // The halo IS refracted ink, so it can be no brighter than the ink
        // just OUTSIDE the edge: one dye tap a few halo-widths along -grad
        // (the outward normal), so it doesn't collapse under the oil itself.
        float2 uvO = saturate(uv - (grad / gl) * (max(LA_MENISCUS_W, LA_RIM_WIDTH) * 8.0)
                                  * float2(1.0 / aspect, 1.0));
        float3 dO  = Dye.SampleLevel(linearClamp, uvO, 0).rgb;
        float  il  = saturate(max(dO.r, max(dO.g, dO.b)) * max(LA_INK_GAIN, 1.0));
        // Brightens where the ink outside is bright, and falls toward a floor
        // -- not to nothing -- over clear water, because the halo is also the
        // oil edge's own refraction (ink_mode=water is mostly clear water).
        haloMul *= lerp(1.0, 0.55 + 0.90 * smoothstep(0.03, 0.45, il), saturate(LA_RIM_INK_FOLLOW));
    }
    // ---- paired-annulus ordering (rim_order) -----------------------------
    // LA_RIM_ORDER = 0: the shipped placement — dark band centred at rim_inset,
    // bright halo centred at -meniscus_offset, wherever the ini put them.
    // LA_RIM_ORDER = 1: force the physical pairing (Micromachines 13(7):1021) —
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
    if (LA_MEN_FROM_INK > 0.0005) {
        float  k  = saturate(LA_MEN_FROM_INK);
        // The gate reads the ink OUTSIDE the isoline, not the ink under the
        // oil: a bright dye patch beneath a disc must not summon a halo, and
        // a hole punched into clear water must not get one either.
        float2 uvO = saturate(uv - (grad / gl) * (0.06 * lensR + LA_MENISCUS_W * 2.0)
                                  * float2(1.0 / aspect, 1.0));
        float3 dO  = Dye.SampleLevel(linearClamp, uvO, 0).rgb;
        float  ilo = saturate(max(dO.r, max(dO.g, dO.b)) * max(LA_INK_GAIN, 1.0));
        haloInk    = lerp(1.0, smoothstep(0.03, 0.40, ilo), k);
        float3 hs = AcidRgb2Hsv(inkC);     // lift: more saturated, brighter
        hs.y = saturate(hs.y * 1.35);
        hs.z = saturate(hs.z * 1.55 + 0.05);
        menC   = lerp(menC, AcidHsv2Rgb(hs), k);
        // a few percent of the LOCAL lens radius, never a 1-2 px line
        // ...but in CRISP mode the halo keeps its authored width: scaling it by
        // the local lens radius is what made a big hole's halo wide, and a
        // crisp edge wants the same thin meniscus at every size.
        menHWx = (LA_OIL_EDGE_MODE > 0.5) ? 1.0
               : lerp(1.0, max(1.0, (0.055 * lensR) / max(LA_MENISCUS_W, 1e-5)), k);
    }
    // ...both floored in px (band_min): the meniscus scaled by the lens
    // radius is exactly the "solid outline" the user photographed on a
    // medium droplet, and the dark rim is its twin.
    float rimHW = max(LA_RIM_WIDTH * rimMul, bmin * 1.5 * PX1440);
    float menHW = clamp(LA_MENISCUS_W * rimMul * menHWx, bmin * 3.5 * PX1440, 0.024);
    // A defocused hairline is a wider, fainter hairline: widths add in
    // quadrature, the way a Gaussian convolves with a Gaussian.
    if (soft > 1e-6) {
        rimHW = sqrt(rimHW * rimHW + soft * soft);
        menHW = sqrt(menHW * menHW + soft * soft);
    }
    float rimCtr = LA_RIM_ORDER > 0.5 ?  rimHW : LA_RIM_INSET;
    float menCtr = LA_RIM_ORDER > 0.5 ? -menHW : -LA_MENISCUS_OFF;
    if (LA_MEN_FROM_INK > 0.0005) menCtr = lerp(menCtr, -menHW * 0.55, saturate(LA_MEN_FROM_INK));
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
    float rimK = saturate(LA_RIM_DARK) * haloInk * (1.0 - 0.65 * saturate(LA_OIL_THIN_EDGE));
    oilC *= 1.0 - rimK * rimB;
    // ---- the colour a RIM reflects (boundary_reflect_r, brief AJ) --------
    // Everything below that paints a droplet's boundary -- the bright-field
    // halo, mass_rim, the droplet lens's dome / meniscus / specular, oil_glow
    // and the swarm's caustics -- tints with oilR instead of oilC, and with
    // oilThinR instead of oilThinC. With boundary_reflect_r 0 rimHueD is 0 and
    // these are the same values, so every existing preset is byte-identical.
    // The film itself (the `col = lerp(inkC, oilC, alpha)` below) always keeps
    // its own colour: only the boundaries reach out.
    float3 oilR     = oilC;
    float3 oilThinR = oilThinC;
    [branch] if (abs(rimHueD) > 1e-4) {
        oilR     = AcidHueShift(oilC,     rimHueD);
        oilThinR = AcidHueShift(oilThinC, rimHueD);
    }


    // Film alpha: with oil_thin_edge the disc fades out over the thickness
    // band instead of over 1-2 px of coverage AA. Squared, because a lens
    // thins fast near its edge.
    float alpha = cov;
    if (LA_OIL_THIN_EDGE > 0.0005) {
        float aS = smoothstep(-edgeW * 0.35, edgeW, sdf);
        // the same S as the thickness ramp, or the disc's opacity would
        // arrive at the ink with the knee the colour no longer has.
        [branch] if (LA_OIL_EDGE_CURVE > 0.0005) {
            float t = saturate((sdf + edgeW * 0.35) / max(edgeW * 1.35, 1e-7));
            aS = lerp(aS, t * t * t * (t * (t * 6.0 - 15.0) + 10.0),
                      saturate(LA_OIL_EDGE_CURVE));
        }
        alpha = lerp(cov, aS * aS, saturate(LA_OIL_THIN_EDGE));
    }
    // ---- film_level (brief BK): the FLAT film's own light -----------------
    // The user: one element pitch black, one the main colour, one an accent.
    // This dims the sheet (and whatever of the ink its transparency shows)
    // but NOT the edge light: oilR / oilThinR were copied above, so mass_rim,
    // oil_glow, the bright-field halo and the lens highlights keep the full
    // palette -- brief BL's "brightness at the edges, the flat field dark".
    // The dye on the masses/droplets rides on inkC and is untouched.
    if (LA_FILM_LEVEL < 0.9995) {
        oilC *= LA_FILM_LEVEL;
        // brief BP: the dimmed film keeps its luma and gains chroma
        [branch] if (LA_DARK_SAT > 0.0005) oilC = DarkSat(oilC, LA_FILM_LEVEL);
    }
    float3 col = lerp(inkC, oilC, alpha);

    // ---- brief BU: SPLIT TONE + LAMP GREY ---------------------------------
    // Both act on the composite right here, after the film and the dye are
    // laid down and BEFORE every rim/halo/lens/glow term below, so droplet
    // and mass edges keep their colour and paint over both. Both branches are
    // skipped at their defaults (zero add / GREY_K 0): byte-identical.
    //
    // SPLIT TONE (shadow_tone): the dark tones of the MASSES take the
    // complement of the main film hue. The CPU folds hue, saturation, lift,
    // the on/off gate and the amount into one add; the mask keeps it off the
    // film (alpha) and off anything already lit (luma above ~0.15, e.g. dyed
    // bodies). It lifts true black by at most shadow_tone_lift: the user's
    // call over the auditor's objection.
    [branch] if (LA_TONE_R + LA_TONE_G + LA_TONE_B > 0.0) {
        float tY = dot(col, float3(0.2126, 0.7152, 0.0722));
        float tS = smoothstep(0.02, 0.15, tY);
        // tone_balance (BU-b) scales the shadow band with the split point
        // (0.5 - 0.25 b) / 0.5; a separate branch so balance 0 keeps BU's
        // constant-folded smoothstep bit for bit.
        [branch] if (LA_TONE_BAL != 0.0) {
            float tK = 1.0 - 0.5 * LA_TONE_BAL;
            tS = smoothstep(0.02 * tK, 0.15 * tK, tY);
        }
        float tM = (1.0 - alpha) * (1.0 - tS);
        col += float3(LA_TONE_R, LA_TONE_G, LA_TONE_B) * tM;
    }
    // HIGHLIGHT TONE (brief BU-b, Lightroom split toning): the lit FILM
    // shifts colour toward the highlight tint at CONSTANT displayed
    // luminance (the shadow half above lifts; this half never adds light --
    // the film is most of the frame, and ABL). Worked in DISPLAY light:
    // d0 = what post_chroma, post_lift and the saturate() below will make of
    // this pixel, hT = the luminance the panel shows for it (BubLum: the
    // gamut stretch and its clamp included). Weight = alpha x smoothstep over
    // the encoded hT around the split (0.5 - 0.25 x tone_balance), 0.25 below
    // to 0.05 above it: the lit film (encoded ~0.5) takes ~85% at balance
    // 0, all of it at +0.5, ~40% at -0.5 (the first band, 0.40..0.05 below,
    // left balance >= 0 inert on this look). Then a white-balance multiply toward the tint
    // (lamp grey's construction), the film desaturation, a CAP (no channel
    // above the pixel's own max: magenta -> gold drives red, which already
    // sits at the clip), and the luminance put back EXACTLY: a scale down if
    // brighter, else a bisection toward the flat grey at that max (which
    // keeps the cap). Last, the exact inverse of post_lift / post_chroma
    // back into the composite. The CPU folds hue, sat, amount, desat and the
    // gate into LA_HTONE (0 = off, branch skipped).
    [branch] if (LA_HTONE_R + LA_HTONE_G + LA_HTONE_B > 0.0) {
        const float3 GW = float3(0.2126, 0.7152, 0.0722);
        float  hPc = max(LA_POST_CHROMA, 1e-3), hPl = max(LA_POST_LIFT, 1e-3);
        float  hY0 = dot(col, GW);
        float3 d0 = DsToLin(saturate(max(hY0 + (col - hY0) * hPc, 0.0) * hPl));
        float  hT = BubLum(d0);
        float  hSp = 0.5 - 0.25 * LA_TONE_BAL;
        float  hW = alpha * smoothstep(hSp - 0.25, hSp + 0.05, DsToSrgb(hT.xxx).x);
        [branch] if (hW > 0.0) {
            float3 hU = float3(LA_HTONE_R, LA_HTONE_G, LA_HTONE_B);
            float  hMag = dot(hU, GW);
            float3 hc = d0 * (hU / hMag);
            float  hY = dot(hc, GW);
            hc = hY + (hc - hY) * (1.0 - saturate(hMag - 1.0));
            float hM0 = max(max(d0.r, max(d0.g, d0.b)), hT);
            float hM1 = max(hc.r, max(hc.g, hc.b));
            if (hM1 > hM0) hc *= hM0 / hM1;
            float3 hx = lerp(d0, hc, hW);
            float  hL = BubLum(hx);
            [branch] if (hL >= hT) {
                hx *= hT / max(hL, 1e-8);
            } else {
                float lo = 0.0, hi = 1.0;
                [unroll] for (int it = 0; it < 7; it++) {
                    float mid = 0.5 * (lo + hi);
                    if (BubLum(lerp(hx, hM0.xxx, mid)) < hT) lo = mid; else hi = mid;
                }
                hx = lerp(hx, hM0.xxx, 0.5 * (lo + hi));
            }
            float3 he = DsToSrgb(hx);
            float  hy = dot(he, GW) / hPl;
            col = max(hy + (he / hPl - hy) / hPc, 0.0);
        }
    }
    // LAMP GREY (lamp_grey): "as if the dye far from the lamp gets less
    // light". A soft disc on the far corner (the CPU picks it and slides it
    // along the frame edge): full inside 0.35 x size, gone at size (screen
    // heights), and the corner is > 1 screen height from the frame centre,
    // so the centre never changes. Desaturation about the pixel's LINEAR
    // luminance (the composite is sRGB-encoded, and a mix about encoded luma
    // would darken saturated film by 20-40%), then a cool white balance
    // renormalised to the same Y: luminance is kept, so ABL and mean_lum do
    // not move and black stays black (held through post_chroma too).
    // Applied after the tone, so a tinted body in the grey corner greys
    // with the rest.
    [branch] if (LA_GREY_K > 0.0) {
        float gS = max(LA_GREY_SIZE, 0.05);
        float gw = 1.0 - smoothstep(0.35 * gS, gS, length(pp - float2(LA_GREY_CX, LA_GREY_CY)));
        float gk = gw * LA_GREY_K;
        [branch] if (gk > 0.0) {
            const float3 GW = float3(0.2126, 0.7152, 0.0722);
            float3 gl = DsToLin(col);
            float  gY = dot(gl, GW);
            gl = gY + (gl - gY) * (1.0 - gk);
            float3 gc = gl * float3(0.80, 0.94, 1.30);
            gc *= gY / max(dot(gc, GW), 1e-7);
            gl = lerp(gl, gc, gk * saturate(LA_GREY_COOL));
            float3 go = DsToSrgb(gl);
            // post_chroma (below) scales chroma about the ENCODED luma and
            // clamps at 0, which gives a saturated pixel extra luminance that
            // a greyed one no longer gets (measured -1..-2% mean_lum on a
            // bright film). Hold the luminance the pixel will have AFTER that
            // trim, exactly as DarkSat does: two fixed-point gain steps.
            [branch] if (abs(LA_POST_CHROMA - 1.0) > 0.001) {
                float  pc = LA_POST_CHROMA;
                float  y0 = dot(col, GW);
                float  t0 = dot(DsToLin(max(y0 + (col - y0) * pc, 0.0)), GW);
                [unroll] for (int it = 0; it < 2; it++) {
                    float y1 = dot(go, GW);
                    float t1 = dot(DsToLin(max(y1 + (go - y1) * pc, 0.0)), GW);
                    go = DsToSrgb(DsToLin(go) * (t0 / max(t1, 1e-8)));
                }
            }
            col = go;
        }
    }

    // ---- FLUORESCENT OIL UNDER THE LAMP (oil_fluor, brief BL) ------------
    // The user: "What if the lamp lights up the oil, and it's like
    // fluorescent? And the ambient light brightens the fluid?" -- refs
    // bl-biolum-ref-1/-2: water that GLOWS in its own electric colour,
    // brightest in the churned edges, everything else near black.
    // So the oil EMITS: its own hue at full saturation (no hue key -- the
    // palette rotates, the glow rotates with it), driven by the rig's lamp
    // (the same LAMP_X/Y the rim, the shadows and the specular use) with a
    // Gaussian falloff over oil_fluor_reach screen heights. The profile is
    // edge-weighted the way a fluorescent acrylic sheet is: the light it
    // makes is trapped inside and leaks out where the sheet ends, so the thin
    // film at a MASS edge glows most and the flat body only a little. The
    // edge is measured on the blob-only surface (fieldB/gradB), so the 950
    // droplet holes do not each grow a glowing ring ("too bubbly", BK) --
    // they sit in the body term. Added AFTER film_level, so film_level 0.3 +
    // oil_fluor = dark sheet, glowing lamp-side edges; weighted by alpha, so
    // the masses themselves emit nothing (the shadow pass below still
    // multiplies it: an occluded film is not excited). The edge band alone
    // gets HDR headroom further down (fluorM), which keeps the hot area small
    // for the panel's ABL. oil_fluor 0 skips all of it: byte-identical.
    float fluorM = 0.0;
    [branch] if (LA_OIL_FLUOR > 0.0005) {
        float2 lampF = float2(LA_LAMP_X * aspect, LA_LAMP_Y);
        float  dL    = length(pp - lampF) / max(LA_OIL_FLUOR_REACH, 0.05);
        float  fall  = exp(-dL * dL);
        float  glBf  = length(gradB) + 1e-6;
        float  sBf   = (fieldB - thresh) / glBf;
        float  wE    = max(edgeW * 1.5, bmin * 4.0 * PX1440);
        float  edgeP = exp(-max(sBf, 0.0) / wE) * smoothstep(0.5, 1.5, glBf) * isoOk;
        float  prof  = 0.20 + 0.80 * edgeP;
        // the film's own hue, value 1, saturation pushed: it glows, it does
        // not whiten (peak channel, the mass_rim convention)
        float3 fh = AcidRgb2Hsv(oilR);
        fh.y = saturate(fh.y * 1.25);
        fh.z = 1.0;
        float  k  = saturate(LA_OIL_FLUOR) * fall * alpha;
        col += AcidHsv2Rgb(fh) * (0.80 * k * prof);
        fluorM = saturate(k * edgeP);
    }

    // ---- BACKLIGHT PENUMBRA (oil_penumbra) -------------------------------
    // The lamp is under the middle of the dish: the oil right next to a black
    // mass receives less of it than the open sheet does, and a pigment lit
    // less shifts hue as well as level. A smootherstep band on the OIL side
    // of the isoline only -- it can never touch the black, because it is
    // multiplied by the coverage it sits on.
    [branch] if (LA_PENUMBRA > 0.0005) {
        float u = saturate(sdf / max(max(LA_PENUMBRA_W, bmin * 4.0 * PX1440), 1e-5));
        float b = 1.0 - u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
        float k = saturate(LA_PENUMBRA) * b * cov * isoOk;
        float3 lit = CssHueRotate(col, LA_PENUMBRA_HUE) * (1.0 - saturate(LA_PENUMBRA_DARK));
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
    [branch] if (LA_HALO > 0.0005) {
        float hw = max(LA_HALO_W, 1e-5);
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
        float k  = saturate(LA_HALO) * smoothstep(0.5, 1.5, gl) * isoOk;
        float3 bright = (sgnB > 0.0) ? oilR : inkC;
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
    [branch] if (LA_MASS_RIM > 0.0005) {
        float  rw  = max(LA_MASS_RIM_W, bmin * 1.5 * PX1440);
        float  u2  = sdf / rw;                       // < 0 on the mass side
        float  band = exp(-(u2 + 1.15) * (u2 + 1.15) * 1.6);
        // which way this piece of edge faces, and how much lamp it catches
        float2 nrm = -grad / max(gl, 1e-5);
        float  lit = saturate(dot(nrm, lampD) * 0.5 + 0.5);
        lit = lit * lit;
        // Peak channel, never luminance: the film is a saturated magenta
        // whose luminance is a third of its red.
        float  src = max(oilR.r, max(oilR.g, oilR.b));
        float3 tint = lerp(oilR / max(src, 1e-4), float3(1.0, 1.0, 1.0), 0.45);
        col += tint * (saturate(LA_MASS_RIM) * band * lit * 0.28
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
    [branch] if (LA_LENS > 0.0005) {
        float k0  = saturate(LA_LENS);
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
        [branch] if (LA_LENS_CENTRE > 0.0005) {
            float dome = u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
            // a hole is a lens too: its middle passes more of the film's own
            // light than its shoulder does, so it lifts toward the film
            // colour. An oil droplet lifts toward the backlight instead.
            float3 tgt = (sIn > 0.0) ? lerp(col, float3(1.0, 1.0, 1.0), 0.55) : oilR;
            col = lerp(col, tgt, k0 * saturate(LA_LENS_CENTRE) * dome * lg * 0.55);
        }
        float bw = max(LA_LENS_BAND, bmin * 1.5 * PX1440);
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
        col += lerp(oilR, float3(1.0, 1.0, 1.0), 0.45) * (k0 * 0.30 * men * lg);
        // (d) the specular. nOut is the outward direction of whichever body
        // this pixel is inside, so one expression lights a droplet and a hole
        // alike, and both turn to face the lamp when the rig moves it.
        [branch] if (LA_DROP_SPEC > 0.0005) {
            float2 Lv = float2(LA_LAMP_X * aspect, LA_LAMP_Y) - pp;
            float2 Ld = Lv / max(length(Lv), 1e-6);
            float2 nO = (-sIn / gl) * grad;
            float  f  = saturate(dot(nO, Ld));
            float  f2 = f * f; f2 = f2 * f2;              // ^4: a small hotspot
            float  w  = (u - 0.58) / 0.30;
            float  sp = f2 * exp(-w * w);
            col += lerp(oilR, float3(1.0, 1.0, 1.0), 0.80)
                 * (k0 * saturate(LA_DROP_SPEC) * sp * lg * 0.50);
        }
    }
    // ---- outside glow (oil_glow): the lens spills a little of its own
    // colour into the ink around it -- diffuse, never a line (refs 4/5).
    if (LA_OIL_GLOW > 0.0005) {
        float dOut = max(-sdf, 0.0) / max(edgeW * 1.3, 1e-5);
        float go   = exp(-dOut * dOut) * (1.0 - alpha) * smoothstep(0.5, 1.5, gl);
        col += oilThinR * (go * 0.45 * saturate(LA_OIL_GLOW));
    }


    // ---- bubble swarms ---------------------------------------------------
    // Two procedural cellular layers, each masked to one side of the oil
    // surface: dark water droplets trapped INSIDE the oil (ref 2, ref 3) and
    // oil droplets floating on the open ink (ref 3). Both creep and warp so
    // they are not glued to the screen.
    {
        float st = LA_TIME;
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
        float lensK = saturate(LA_SWARM_LENS);
        if (LA_SWARM_HOLES > 0.002) {
            float2 sp = pp + warp * 0.030 + st * float2(0.0040, -0.0030);
            // two octaves 2.7x apart so the sizes run from ~1 px to ~2% of the
            // frame height; the fine one stays sparse or the oil reads as dust
            float2 sn1, sn2; float sr1, sr2;
            float  sda = AcidSwarm(sp, LA_SWARM_SCALE_HOLES, LA_SWARM_DENSITY, LA_SWARM_CLUMP, LA_SWARM_R_MIN, LA_SWARM_R_MAX, sn1, sr1);
            float  sdb = AcidSwarm(sp + 7.31, LA_SWARM_SCALE_HOLES * 2.7, LA_SWARM_DENSITY * 0.12, LA_SWARM_CLUMP,
                                   LA_SWARM_R_MIN, LA_SWARM_R_MAX, sn2, sr2);
            bool   wa = (sda <= sdb);
            float  sd = wa ? sda : sdb;
            float2 sn = wa ? sn1 : sn2;
            float  sr = max(wa ? sr1 : sr2, 1e-4);
            float  sw   = lerp(aaS, max(sr * 0.26, aaS), lensK);
            float  scov = 1.0 - smoothstep(-sw, sw, sd);
            float  sx = sd / max(lerp(aaS * 2.4, sw * 1.15, lensK), 1e-6);
            float  srim = exp(-sx * sx);
            float  mask = smoothstep(0.004, 0.030, sdf) * LA_SWARM_HOLES;   // inside the oil only
            if (lensK > 0.0005) {   // thin-oil fringe just OUTSIDE the droplet
                float fx = max(sd, 0.0) / max(sw * 1.8, 1e-5);
                col = lerp(col, oilThinR,
                           exp(-fx * fx) * (1.0 - scov) * mask * lensK * 0.75);
            }
            // a trapped water droplet shows the INK through the oil film
            col = lerp(col, inkC * (1.0 - LA_SWARM_DARK * (1.0 - 0.40 * lensK)), scov * mask);
            col *= 1.0 - LA_SWARM_RIM_DARK * (1.0 - 0.55 * lensK) * srim * mask;
            if (lensK > 0.0005) {
                float2 vc = sn * (sd + sr);
                float  hd = length(vc - float2(-0.707, -0.707) * (0.42 * sr))
                          / max(sr * 0.30, 1e-5);
                col += oilR * (exp(-hd * hd) * scov * mask * lensK * 0.30);
            }
        }
        if (LA_SWARM_DROPS > 0.002) {
            float2 sp = pp * 1.31 - warp * 0.024 + st * float2(-0.0031, 0.0042) + 41.7;
            float2 sn1, sn2; float sr1, sr2;
            float  sda = AcidSwarm(sp, LA_SWARM_SCALE_DROPS, LA_SWARM_DENSITY, LA_SWARM_CLUMP, LA_SWARM_R_MIN, LA_SWARM_R_MAX, sn1, sr1);
            float  sdb = AcidSwarm(sp + 3.17, LA_SWARM_SCALE_DROPS * 2.7, LA_SWARM_DENSITY * 0.12, LA_SWARM_CLUMP,
                                   LA_SWARM_R_MIN, LA_SWARM_R_MAX, sn2, sr2);
            bool   wa = (sda <= sdb);
            float  sd = wa ? sda : sdb;
            float2 sn = wa ? sn1 : sn2;
            float  sr = max(wa ? sr1 : sr2, 1e-4);
            float  sw   = lerp(aaS, max(sr * 0.26, aaS), lensK);
            float  scov = 1.0 - smoothstep(-sw, sw, sd);
            float  sx = sd / max(lerp(aaS * 2.4, sw * 1.15, lensK), 1e-6);
            float  srim = exp(-sx * sx);
            float  mask = smoothstep(0.004, 0.030, -sdf) * LA_SWARM_DROPS;  // open ink only
            col = lerp(col, laOil[3].rgb, scov * mask);
            col *= 1.0 - LA_SWARM_RIM_DARK * (1.0 - 0.55 * lensK) * srim * mask;
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
    // ---- meniscus_film_mix, part 1: the ink-brightness GATE --------------
    // haloInk is the meniscus_from_ink gate: "where the ink just outside is
    // dark, the halo vanishes", so a foreign colour can never be stroked onto
    // black ink. In acid-rise-12 the ink outside IS black everywhere, so
    // haloInk is 0 in every pixel and this whole term -- the 0.85 at the top
    // of the ring stack -- paints nothing at all (measured: meniscus 0 is
    // byte-identical to meniscus 0.85). The gate is an INK-SOURCE gate: once
    // the colour comes from the FILM it is guarding against a danger that no
    // longer exists, so the mix opens it by the same amount it moves the
    // colour. Only the meniscus sees this; the dark hairline below keeps the
    // original haloInk. At mix 0, menInk == haloInk and nothing changes.
    float menInk = haloInk;
    [branch] if (LA_MEN_FILM_MIX > 0.0005) menInk = lerp(haloInk, 1.0, saturate(LA_MEN_FILM_MIX));
    float haloW = 0.0;
    if (LA_MENISCUS * haloMul * menInk > 0.002) {
        float hx = (sdf - menCtr) / menHW;
        float halo = exp(-hx * hx);
        // sdf = (field - thresh) / |grad| is only a distance where |grad| is
        // strong. On a broad low-gradient plateau that never reaches the
        // threshold, a tiny field deficit divided by a tiny gradient still
        // lands inside the halo band, painting a fuzzy blob-shaped glow with
        // no oil under it. Real blob surfaces have |grad| >~ 2 (it scales as
        // 1/radius, and the largest discs here are ~0.4), so gate on it.
        halo *= smoothstep(0.5, 1.5, gl) * isoOk;
        haloW = saturate(halo * LA_MENISCUS * haloMul * menInk);
        // ---- meniscus_film_mix, part 2: the COLOUR from the FILM ---------
        // menC above is derived from the INK (meniscus_from_ink), and this
        // band replaces up to `meniscus` (0.85 live) of the pixel -- so it
        // sits on top of, and hides, every FILM-hue feature. oilR is the film
        // colour the rim terms beside it already use, hue-rotated by the seam
        // reflect where there is a seam, so mixing toward it is what lets a
        // hue2 seam reach the halo. Lifted by the SAME factors the ink path
        // uses, so the halo stays a bright caustic and only its colour moves:
        // haloW (shape, width, amplitude) is computed above and untouched.
        float3 menCm = menC;
        [branch] if (LA_MEN_FILM_MIX > 0.0005) {
            float3 fs = AcidRgb2Hsv(oilR);
            fs.y = saturate(fs.y * 1.35);
            fs.z = saturate(fs.z * 1.55 + 0.05);
            menCm = lerp(menC, AcidHsv2Rgb(fs), saturate(LA_MEN_FILM_MIX));
        }
        col = lerp(col, menCm, haloW);
    }

    // ---- interface speckle: sparse cellular dots hugging the boundary ----
    if (LA_SPECKLE > 0.001) {
        float2 cell = floor(pp * LA_SPECKLE_SCALE);
        float  rnd  = AcidHash21(cell);
        float2 jit  = float2(AcidHash21(cell + 7.13), AcidHash21(cell + 13.71));
        float  dd   = length(frac(pp * LA_SPECKLE_SCALE) - (0.25 + 0.5 * jit));
        float  dot1 = step(dd, 0.08 + 0.26 * rnd) * step(0.88, rnd);
        float  sMask = saturate(smoothstep(-0.020, 0.0, sdf) * (1.0 - cov) * 1.2
                              + (1.0 - smoothstep(LA_RIM_WIDTH * 2.0, LA_RIM_WIDTH * 7.0, sdf)) * cov * 0.30);
        col = lerp(col, col * 0.18, dot1 * sMask * LA_SPECKLE);
    }

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
    // Both drift with the rise (LA_CELL_DRIFT = rise_speed * cellulose_drift), so
    // the texture belongs to the masses and not to the screen, and this sits
    // in the DISPLAY pass -- under the film grain, because the fibres are the
    // surface and the grain is the camera.
    //
    // On the BLACK side it is a THICKNESS effect, never a fill: the strands
    // are strongest where the black layer is thin (just inside its edge) and
    // fall off to nothing deep inside a mass, so the OLED's true black is
    // still true black over most of the frame. On the film it is a faint
    // multiplicative mottle of the colour, which cannot lift anything.
    [branch] if (LA_CELL_INK > 0.0005 || LA_CELL_OIL > 0.0005) {
        float  csc = max(LA_CELL_SCALE, 1e-4);
        float2 q0  = pp + float2(0.0, LA_CELL_DRIFT * LA_TIME);
        float  ang = 0.9 * AcidVNoise(q0 * 0.7) + LA_TIME * 0.010;
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
        col += n * (0.28 * LA_CELL_INK * (1.0 - alpha) * th * isoOk);
        col *= 1.0 + n * (0.45 * LA_CELL_OIL * alpha);
    }

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
    [branch] if (LA_SHADOW_AMT > 0.0005) {
        float2 lampS = float2(LA_LAMP_X * aspect, LA_LAMP_Y);
        float2 toLv  = lampS - pp;
        float2 toLd  = toLv / max(length(toLv), 1e-5);
        float  lz    = clamp(LA_LIGHT_Z, -1.0, 1.0);
        // 1.0 at light_z 0.35 (a lamp a little in front of the dish), up to
        // 5x as the lamp sinks into the plane, half as it rises over it.
        float  elong = min(0.35 / max(lz, 0.07), 5.0);
        float  dirW  = saturate(lz * 4.0);            // 0 at and below zero
        float  sft   = saturate(LA_SHADOW_SOFT);
        float  L0    = max(LA_SHADOW_LEN, 0.0);
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
            float  sup = S.a.z * LA_SUPPORT_SCALE;
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
        [branch] if (LA_DROPS_ON > 0.5) {
            const int gw2 = (int)LA_DROP_GRID_W, gh2 = (int)LA_DROP_GRID_H;
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
                                           max(R * saturate(LA_DROP_RING_WIDTH), 0.0008), rng);
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
        shadowMul = 1.0 - saturate(LA_SHADOW_AMT) * saturate(max(occ, dOcc));
        col *= shadowMul;
    }
    // ---- ink-tinted toe (toe_tint) ---------------------------------------
    // The genre's darkest ink is #180808 / #2c1506 — a lifted, HUE-TINTED toe,
    // never a crush to neutral black. Lift only the bottom of the range, and
    // only toward a low-value version of the ink ramp's own mid hue, so the
    // tint always belongs to this palette (and follows the palette sweep).
    // LA_TOE_TINT = 0 leaves the shipped neutral toe untouched.
    if (LA_TOE_TINT > 0.001) {
        float3 inkHue = laInk[1].rgb / max(max(laInk[1].r, max(laInk[1].g, laInk[1].b)), 1e-3);
        float  toeL   = dot(col, float3(0.2126, 0.7152, 0.0722));
        col += inkHue * (0.12 * saturate(LA_TOE_TINT) * (1.0 - smoothstep(0.0, 0.22, toeL)));
    }
    // ---- final trim: post_chroma / post_lift -----------------------------
    // A transparent film costs perceptual chroma (10-20%) and a little
    // lightness against the same look opaque, measured in OKLab over the
    // non-dark pixels. Both of those are corrected here rather than in every
    // palette: chroma is scaled about the pixel's OWN luma, so hue and
    // luminance survive and only the distance from grey grows; lift is a
    // plain luma multiplier on top. 1 / 1 = untouched, and both are applied
    // to the whole composite, so ink, halo and film move together.
    if (abs(LA_POST_CHROMA - 1.0) > 0.001) {
        float pl = dot(col, float3(0.2126, 0.7152, 0.0722));
        col = max(pl.xxx + (col - pl.xxx) * LA_POST_CHROMA, 0.0);
    }
    if (abs(LA_POST_LIFT - 1.0) > 0.001) col *= LA_POST_LIFT;
    // ---- coarse ANIMATED film grain over everything ----------------------
    // grain_shadow_weight (LA_GRAIN_SHADOW_W) biases the amplitude into the darks:
    // in every reference frame the ink is visibly noisy while the flat oil
    // discs are clean, which is what real high-ISO backlit macro looks like.
    // (1 - luma)^2 -- squared, so mid-tones already lose most of the grain.
    //
    // brief BD: this is the look's OWN second stock, independent of
    // [post] film_grain, and two things about it were wrong. It re-rolled
    // once per REFRESH (frac(time)), i.e. 240 fizz per second rather than a
    // film cadence -- it now quantises to film_grain_fps like every other
    // noise in the build. And when the image-space pass runs WITH a grain of
    // its own ([post] film_grain > 0) it is zeroed on the CPU, because it was
    // living in the texture that pass resamples: the lateral aberration was taking a first
    // difference of it on R and B and printing it as colour, which measured
    // as 72% of all the chroma noise inside a dark mass. One stock at a time.
    float grainAmp = LA_GRAIN;
    if (LA_GRAIN_SHADOW_W > 0.0005) {
        float gl2 = saturate(1.0 - dot(col, float3(0.2126, 0.7152, 0.0722)));
        grainAmp *= lerp(1.0, gl2 * gl2, saturate(LA_GRAIN_SHADOW_W));
    }
    // The reference halo band is visibly GRAINY (film grain over a bright,
    // thin, refracted band); the shadow weighting above would scrub it clean.
    grainAmp *= 1.0 + haloW * 1.2 * saturate(LA_MEN_FROM_INK);
    // ...and the pattern holds for one FRAME OF THE STOCK, not one refresh.
    float acidGq = frac(floor(LA_TIME * max(poP1.z, 1.0)) * 0.0731) * 913.7;
    col += (AcidHash21(floor(i.pos.xy / max(LA_GRAIN_SCALE, 1.0)) + acidGq) - 0.5)
         * grainAmp;
    C = saturate(col);
    // HDR: the oil is a flat fill, so give it its own highlight level rather
    // than inheriting the ink's. Keep the hot part small (ABL): the rim band.
    // With oil_transparency the oil pixel is mostly TRANSMITTED ink, so it
    // must keep the ink's own highlight level; only the scattered part of the
    // film is driven to oil_hdr. filmOp is 1 when transparency is off.
    // lampG carries rise_bottom_light into the HDR level too: the base of the
    // lamp should be the hot part of the frame, not merely the pale part.
    if (LA_OIL_HDR > 0.001) m = lerp(m, LA_OIL_HDR * lampG * shadowMul, alpha * filmOp);
    if (LA_RIM_HDR > 0.001) m = max(m, LA_RIM_HDR * rimB * alpha);
    // brief BL: the fluorescent edge band runs into the HDR headroom (knee ->
    // capBright = up to peak_nits); shadowed film is not excited.
    [branch] if (fluorM > 0.0005) m = max(m, lerp(knee, capBright, fluorM * shadowMul));
    // a specular on a real oil surface is a highlight, not a paler fill
    if (LA_OIL_HDR > 0.001 && specAmt > 0.0005)
        m = max(m, min(LA_OIL_HDR * (1.0 + 2.5 * specAmt), 1.6) * alpha);
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
        // brief BD: the same two knobs Emulsion() takes in the post pass, and
        // for the same reasons -- this is the copy of that grain that runs
        // when no image-space pass is on. mrP2.z = film_grain_chroma (1 = the
        // additive step this always was), mrP2.w = film_grain_density (0 =
        // the weight above). At the defaults the two lines below are the one
        // line they replaced.
        [branch] if (mrP2.w > 0.0005) {
            float wd = smoothstep(0.10, 0.32, lum) * (1.0 - smoothstep(0.55, 1.00, lum));
            w = lerp(w, wd, saturate(mrP2.w));
        }
        [branch] if (mrP2.z > 0.9995) {
            C = max(C + (nz - 0.5) * (poP0.x * 0.5 * w), 0.0);
        } else {
            float3 mul = max(C * (1.0 + (nz.x - 0.5) * (poP0.x * 0.6667 * w)), 0.0);
            float3 add = max(C + (nz - 0.5) * (poP0.x * 0.5 * w), 0.0);
            C = lerp(mul, add, saturate(mrP2.z));
        }
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
