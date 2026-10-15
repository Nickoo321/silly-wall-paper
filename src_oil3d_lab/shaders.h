// Oil3DLab shaders (fxc, SM 5.1). Fullscreen-triangle pixel shaders:
//   kRaySrc      the raymarch + shading (gloss | flat) + far-wall marbling
//   kPostSrc     quick post: quarter-res down + blur, then DOF / halation /
//                aberration / vignette / grain + sdrScale into the FP16 target
//
// WORLD: units are screen heights, +y UP, +z INTO the screen (left-handed).
// The slab spans z in [0, depth]; the far wall is the plane z = depth.
// Colours are linear, 1.0 = SDR white; the display pass multiplies sdrScale.
//
// fxc notes (kept here so the cloud code can avoid the same traps). This file
// compiles under fxc ps_5_1 /O3 with zero warnings in ~270 ms (RX 7900 GRE box):
//   * MSVC caps ONE string literal at 16380 bytes: split into adjacent R"hlsl()hlsl".
//   * no ddx/ddy/fwidth inside the march loops (X3511/X4014 with a varying
//     trip count); nothing here needs them (AA is analytic, see PSMain).
//   * bit-mask iteration: `while (m) { i = firstbitlow(m); m &= m - 1u; }` is
//     fine in ps_5_1 and keeps the per-ray blob list in two registers instead
//     of an indexable temp array (which fxc spills to x# scratch).
//   * the blob buffer is a ROOT SRV (D3D12_ROOT_PARAMETER_TYPE_SRV): legal for
//     StructuredBuffer, not for typed Buffer<>. No descriptor heap slot needed.
//   * `1u << (uint)i` with i an int loop counter: cast explicitly.
// Measured: 1.58 ms (gloss, full res, steps 64, 50 blobs) at 2560x1440.
// Look traps found on the GPU (not fxc):
//   * Lipschitz step must be DIRECTIONAL (|rd * inv_stretch|): the isotropic
//     bound made z-stretched hole tunnels march at the floor step -> serrated
//     tunnel exits. Also shrink the floor step near the iso-surface.
//   * the 2D w^4 colour soft-max is too soft in 3D (wide kernels): w^8.
//   * a hole's CAVITY is ~0.75 x its support, much wider than its visible
//     radius: offset it inside its parent by that, or it bites horns/claws.
//   * the 2D sizes (Few giants) merge into one screen-filling mass in 3D: x0.75.
#pragma once

static const char* kRaySrc = R"hlsl(
cbuffer RCB : register(b0) {
    float2 invScreen; float aspect;    float timeSec;
    float3 camPos;    float tanHalf;
    float3 camRight;  float thresh;
    float3 camUp;     float slabDepth;
    float3 camFwd;    float nBlobsF;
    float3 mainCol;   float stepsF;
    float3 inkCol;    float inkLevel;
    float3 seamCol;   float shadeMode;
    float3 keyDir;    float hiGain;
    float3 lampDir;   float lookF;     // --look N (0 = round-1 gloss/flat by shadeMode)
    float3 accentCol; float backLit;   // backLit > 0: the far wall is the LIT film (looks 2, 7)
    float4 lookP;                      // spare per-look params
};

// a: centre xyz, support radius s      b: linear rgb, weight (+1 oil, -k hole)
// c: inverse stretch xyz, 1/s^2        d: accent 0/1, id, 0, 0
struct Blob { float4 a; float4 b; float4 c; float4 d; };
StructuredBuffer<Blob> gBlobs : register(t0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// noise for the far-wall marbling
// ---------------------------------------------------------------------------
float Hash2i(int2 c) {
    uint h = ((uint)c.x * 1597334677u) ^ ((uint)c.y * 3812015801u);
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (float)(h & 0x00ffffffu) * (1.0 / 16777216.0);
}
float VNoise2(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    int2 c = (int2)i;
    float a = Hash2i(c), b = Hash2i(c + int2(1, 0));
    float e = Hash2i(c + int2(0, 1)), g = Hash2i(c + int2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(e, g, f.x), f.y);
}
float Fbm2(float2 p) {
    float s = 0.0, amp = 0.5;
    [unroll] for (int i = 0; i < 4; i++) {
        s += amp * VNoise2(p);
        p = p * 2.03 + float2(17.1, 9.3);
        amp *= 0.5;
    }
    return s;
}

// The far wall stands in for the 2D ink: a domain-warped marbling flowing
// slowly UP. Dark looks: the scheme's ink colour at ink_level, black wherever
// the veins are not. backLit > 0 (looks 2, 7): the LIT film of the 2D
// monotone look, main colour with a thin accent seam and black gaps.
float Marble(float2 w, out float2 r) {
    float t = timeSec;
    float2 q = w * 2.4 - float2(0.0, t * 0.020);
    r = float2(Fbm2(q + float2(0.0, -t * 0.006)),
               Fbm2(q + float2(5.2, 1.3) + t * 0.004));
    return Fbm2(q + 2.2 * r);
}
float3 Backdrop(float2 w) {
    float2 r;
    float m = Marble(w, r);
    // one result variable: early returns in if-blocks give fxc X4000 false positives
    float3 res = inkCol * inkLevel * smoothstep(0.50, 0.80, m) * (0.5 + r.x);
    if (backLit > 0.0) {
        float film = smoothstep(0.28, 0.52, m);
        float seam = saturate(1.0 - abs(m - 0.60) * 22.0);
        res = (mainCol * film * (0.55 + 0.6 * r.y) + accentCol * seam * 0.9) * backLit;
    }
    return res;
}

// ---------------------------------------------------------------------------
// the metaball field: Wyvill/Blinn (1 - d^2/s^2)^3, anisotropic via c.xyz
// ---------------------------------------------------------------------------
float BlobTerm(Blob B, float3 p) {
    float3 q = (p - B.a.xyz) * B.c.xyz;
    float u = saturate(1.0 - dot(q, q) * B.c.w);
    return B.b.w * u * u * u;
}

// only the blobs whose support the ray crosses (two 32-bit masks)
float FieldMask(float3 p, uint2 mask) {
    float f = 0.0;
    uint m = mask.x;
    [loop] while (m != 0u) {
        uint i = firstbitlow(m);
        m &= m - 1u;
        f += BlobTerm(gBlobs[i], p);
    }
    m = mask.y;
    [loop] while (m != 0u) {
        uint i = firstbitlow(m);
        m &= m - 1u;
        f += BlobTerm(gBlobs[i + 32u], p);
    }
    return f;
}

// every blob: for the off-ray shading samples (AO, inner glow)
float FieldAll(float3 p) {
    float f = 0.0;
    int nb = (int)nBlobsF;
    [loop] for (int i = 0; i < nb; i++) f += BlobTerm(gBlobs[i], p);
    return f;
}

// field + analytic gradient + w^4 soft-max colour (the 2D look's fill rule)
void FieldFull(float3 p, uint2 mask, out float f, out float3 g, out float3 col, out float acc) {
    f = 0.0; g = float3(0, 0, 0);
    float3 cs = float3(0, 0, 0);
    float cw = 0.0, ac = 0.0;
    [loop] for (uint k = 0u; k < 2u; k++) {
        uint m = (k == 0u) ? mask.x : mask.y;
        [loop] while (m != 0u) {
            uint i = firstbitlow(m) + k * 32u;
            m &= m - 1u;
            Blob B = gBlobs[i];
            float3 q = (p - B.a.xyz) * B.c.xyz;
            float u = 1.0 - dot(q, q) * B.c.w;
            if (u > 0.0) {
                float u2 = u * u, w = u2 * u;
                f += B.b.w * w;
                g += (-6.0 * u2 * B.c.w * B.b.w) * (q * B.c.xyz);
                if (B.b.w > 0.0) {
                    // w^8, not the 2D w^4: 3D kernels are wide on screen, so
                    // w^4 smeared the main/accent handover over a third of a blob
                    float w8 = w * w; w8 *= w8; w8 *= w8;
                    cs += B.b.rgb * w8; cw += w8; ac += B.d.x * w8;
                }
            }
        }
    }
    col = cw > 1e-30 ? cs / cw : mainCol;
    acc = cw > 1e-30 ? ac / cw : 0.0;
}
)hlsl" R"hlsl(
// ---------------------------------------------------------------------------
// shading
// ---------------------------------------------------------------------------
// gloss = wax/oil: wrap diffuse (key upper-left-front) + the lamp from below,
// cheap inner glow from the field just inside the surface, fresnel rim, field
// AO where masses neck, ONE small hot specular (the only HDR headroom).
float3 ShadeGloss(float3 p, float3 n, float3 v, float3 base, float ndv, float gl) {
    float ndl   = dot(n, keyDir);
    float wrapK = saturate((ndl + 0.55) / 1.55);
    float lampK = saturate((dot(n, lampDir) + 0.30) / 1.30);

    float fo = FieldAll(p + n * 0.045);
    float expect = max(thresh - gl * 0.045, 0.0);
    float ao = 1.0 - 0.75 * saturate((fo - expect) / (0.45 * thresh));

    float fi   = FieldAll(p - n * 0.06);
    float core = saturate((fi - thresh) / 0.9);

    float3 body = base * (0.025 + 0.44 * wrapK * wrapK + 0.34 * lampK * lampK) * ao;
    float3 glow = base * (0.035 + 0.34 * core) * (0.55 + 0.45 * ao);
    float  fr   = pow(1.0 - ndv, 4.0);
    float3 rim  = lerp(base, float3(1, 1, 1), 0.20) * fr * 0.50 * ao;

    float3 h  = normalize(keyDir + v);
    // hiGain: 1.6 in SDR (a clipped white core), panel_max / sdr_white with HDR
    float  sp = pow(saturate(dot(n, h)), 700.0);
    float3 spec = lerp(float3(1, 1, 1), base, 0.15) * sp * hiGain;
    float3 h2 = normalize(lampDir + v);
    spec += base * pow(saturate(dot(n, h2)), 160.0) * 0.20;
    return body + glow + rim + spec;
}

// flat = the 2D acid identity in 3D: flat saturated fill, 3 posterized light
// bands, a dark meniscus band inside the silhouette and a thin bright rim.
float3 ShadeFlat(float3 n, float3 base, float ndv) {
    float b  = saturate(dot(n, keyDir) * 0.5 + 0.5) * 3.0;
    float fb = floor(b);
    float band = saturate((fb + smoothstep(0.90, 1.0, b - fb)) / 3.0);
    float3 c = base * (0.42 + 0.58 * band);
    float men = smoothstep(0.42, 0.30, ndv) * smoothstep(0.10, 0.18, ndv);
    c *= 1.0 - 0.85 * men;
    float rim = smoothstep(0.12, 0.04, ndv);
    return lerp(c, lerp(base, float3(1, 1, 1), 0.35) * 1.1, rim);
}

// view-ray thickness: a short march from the entry point to where the ray
// leaves the wax (linear interpolation at the exit), capped at 8 x 0.035
float Thickness(float3 p, float3 rd, uint2 mask) {
    const float st = 0.035;
    float fPrev = thresh;
    [loop] for (int k = 1; k <= 8; k++) {
        float f = FieldMask(p + rd * (st * (float)k), mask);
        if (f < thresh) {
            float a = (fPrev - thresh) / max(fPrev - f, 1e-5);
            return st * ((float)(k - 1) + saturate(a));
        }
        fPrev = f;
    }
    return st * 8.0;
}
)hlsl" R"hlsl(
// a deeper, more saturated shade of the same colour (same peak channel)
float3 Deep(float3 c) {
    float m = max(max(c.r, c.g), max(c.b, 1e-4));
    return pow(saturate(c / m), 1.8) * m;
}
// a simple studio environment in the scheme's colours (chrome, macro reflections)
float3 Env(float3 d) {
    float3 c = mainCol * 1.3 * smoothstep(0.45, 0.90, d.y);              // softbox overhead
    float hz = d.y + 0.08;
    c += accentCol * 0.8 * exp(-hz * hz * 90.0);                          // accent horizon line
    c += mainCol * 0.20 * smoothstep(-0.30, -0.90, d.y);                  // the lamp below
    float win = dot(d, normalize(float3(-0.75, 0.45, -0.5)));
    c += float3(1, 1, 1) * 1.6 * smoothstep(0.93, 0.975, win);            // one hot window
    return c;
}

// The eight exploration directions (--look 1..8), all on the same march core.
float3 ShadeLook(int look, float3 p, float3 n, float3 v, float3 rd, float3 base,
                 float ndv, float gl, float thick) {
    float ndl   = dot(n, keyDir);
    float wrapK = saturate((ndl + 0.55) / 1.55);
    float fo = FieldAll(p + n * 0.045);
    float ao = 1.0 - 0.75 * saturate((fo - max(thresh - gl * 0.045, 0.0)) / (0.45 * thresh));
    float fi = FieldAll(p - n * 0.06);
    float core = saturate((fi - thresh) / 0.9);
    float fr = pow(1.0 - ndv, 4.0);
    float3 hk = normalize(keyDir + v);
    float3 spec = lerp(float3(1, 1, 1), base, 0.15) * pow(saturate(dot(n, hk)), 700.0) * hiGain;
    float3 deep = Deep(base);
    float thin = exp(-thick * 9.0);
    float bottom = saturate(dot(n, lampDir) * 0.6 + 0.4);
    float m = max(max(base.r, base.g), max(base.b, 1e-4));

    float3 outC;
    [branch] if (look == 1) {            // LAMP WAX: lit from a lamp below, thin parts glow
        float3 warm  = float3(1.0, 0.80, 0.60);
        float3 lampC = lerp(deep, deep * warm * 1.25, 0.5);
        float  lampH = saturate(0.75 - p.y * 0.7);          // nearer the lamp = more light
        float3 c = base * (0.012 + 0.16 * wrapK * wrapK) * ao;
        c += lampC * (1.15 * thin * (0.25 + 0.75 * bottom) + 0.28 * bottom * bottom)
                   * (0.6 + 0.4 * ao) * lampH;
        c += lerp(base, float3(1, 1, 1), 0.2) * fr * 0.18 * ao;
        outC = c + spec * 0.8;
    } else if (look == 2) {            // MACRO OIL IN WATER: clear lens drops over the lit ink
        float3 rr = refract(rd, n, 0.72);
        if (dot(rr, rr) < 1e-4) rr = reflect(rd, n);
        float tw = (slabDepth - p.z) / max(rr.z, 0.08);
        float3 seen = Backdrop((p + rr * tw).xy);
        float3 tint = lerp(float3(1, 1, 1), base / m, 0.35);
        float F = 0.03 + 0.97 * pow(1.0 - ndv, 5.0);
        float3 c = seen * tint * (1.0 - F) * 1.15 + Env(reflect(rd, n)) * F * 0.35;
        c *= lerp(0.08, 1.0, smoothstep(0.08, 0.35, ndv));  // the dark refraction ring
        c += lerp(base, float3(1, 1, 1), 0.5) * pow(1.0 - ndv, 10.0) * 0.5;
        outC = c + spec;
    } else if (look == 3) {            // THIN FILM: oil-slick interference on black
        float film = 0.30 + 0.9 * Fbm2(p.xy * 2.2 + p.z * 1.7 + float2(0.0, -timeSec * 0.03));
        float cosT = sqrt(saturate(1.0 - (1.0 - ndv * ndv) / (1.33 * 1.33)));
        float x = film * cosT * 2.4;
        float3 iri = 0.5 + 0.5 * cos(6.2831853 * (x + float3(0.0, 0.33, 0.67)));
        iri = lerp(iri, iri * (0.35 + 0.65 * base / m), 0.5);   // scheme tint
        float3 c = iri * (0.05 + 0.55 * pow(1.0 - ndv, 1.5) + 0.30 * wrapK * wrapK) * ao;
        outC = c + spec;
    } else if (look == 4) {            // LIQUID CHROME: a mirror of the scheme-coloured studio
        float F = 0.65 + 0.35 * pow(1.0 - ndv, 5.0);
        float3 c = Env(reflect(rd, n)) * F * lerp(1.0, ao, 0.8);
        outC = c * lerp(float3(1, 1, 1), base / m, 0.15);
    } else if (look == 5) {            // NEON: emissive, strongest at thin edges and rims
        float rim = pow(1.0 - ndv, 2.5);
        float3 c = base * (0.035 + 0.10 * core) + base * 1.3 * rim + base * 0.55 * thin;
        c += lerp(base, float3(1, 1, 1), 0.6) * pow(1.0 - ndv, 8.0) * 0.5 * hiGain;
        outC = c;
    } else if (look == 6) {            // GUMMY: back-lit, strong soft subsurface
        float trans = exp(-thick * 3.5);
        float3 c = deep * 1.1 * (0.10 + 1.25 * trans) * (0.65 + 0.35 * ao);
        c += base * 0.18 * wrapK * wrapK;
        c += float3(1, 1, 1) * pow(saturate(dot(n, hk)), 40.0) * 0.12;   // soft sheen
        outC = c + spec * 0.7;
    } else if (look == 7) {            // INVERTED: dark masses, thin bright rim, lit film behind
        float3 c = base * 0.012 * ao;
        c += lerp(mainCol, float3(1, 1, 1), 0.35) * pow(1.0 - ndv, 6.0) * 0.9;
        outC = c + spec * 0.35;
    } else {
    // look 8, FLAT ACID+: posterized flat fill, heavier dark meniscus, thin rim
    float bb = saturate(ndl * 0.5 + 0.5) * 3.0;
    float fb = floor(bb);
    float band = saturate((fb + smoothstep(0.92, 1.0, bb - fb)) / 3.0);
    float3 c8 = base * (0.40 + 0.60 * band);
    float men = smoothstep(0.50, 0.34, ndv) * smoothstep(0.08, 0.16, ndv);
    c8 *= 1.0 - 0.95 * men;
    float rimF = smoothstep(0.10, 0.03, ndv);
    outC = lerp(c8, lerp(base, float3(1, 1, 1), 0.45) * 1.2, rimF);
    }
    return outC;
}

// ---------------------------------------------------------------------------
float4 PSMain(float4 svp : SV_Position) : SV_Target {
    float2 ndc = float2(svp.x * invScreen.x * 2.0 - 1.0, 1.0 - svp.y * invScreen.y * 2.0);
    float3 ro = camPos;
    float3 rd = normalize(camFwd + camRight * (ndc.x * tanHalf * aspect)
                                 + camUp * (ndc.y * tanHalf));
    float tWall = (slabDepth - ro.z) / max(rd.z, 1e-4);
    float3 back = Backdrop((ro + rd * tWall).xy);

    // --- per-ray cull: the blobs whose support ellipsoid the ray crosses ----
    uint2 mask = uint2(0u, 0u);
    float tmin = 1e9, tmax = -1e9, L = 0.0;
    int nb = (int)nBlobsF;
    [loop] for (int i = 0; i < nb; i++) {
        Blob B = gBlobs[i];
        float3 o = (ro - B.a.xyz) * B.c.xyz;
        float3 d = rd * B.c.xyz;
        float qa = dot(d, d), qb = dot(o, d), qc = dot(o, o) - B.a.w * B.a.w;
        float disc = qb * qb - qa * qc;
        if (disc > 0.0) {
            float sq = sqrt(disc);
            float t1 = (-qb + sq) / qa;
            if (t1 > 0.0) {
                if (i < 32) mask.x |= 1u << (uint)i;
                else        mask.y |= 1u << (uint)(i - 32);
                // Lipschitz bound of the blob's term ALONG THIS RAY: max over u
                // of 6u^2|q|/s^2 is 1.717/s, times |rd * inv_stretch| (= sqrt(qa)).
                // The isotropic 1.717|w|/s made every tunnel hole (w -2.5, z
                // stretch 3) force floor steps: a serrated tunnel exit.
                L += 1.717 * abs(B.b.w) / B.a.w * sqrt(qa);
                if (B.b.w > 0.0) {
                    tmin = min(tmin, (-qb - sq) / qa);
                    tmax = max(tmax, t1);
                }
            }
        }
    }

    // --- march: Lipschitz steps (never overshoot), a floor step so the trip
    //     is bounded, then bisection at the crossing ----------------------
    bool  hit = false;
    float tHit = 0.0, fmax = -1e9, tMax = 0.0;
    if (tmax > tmin) {
        float t    = max(tmin, 0.0);
        float tEnd = min(tmax, tWall);
        float minStep = max((tEnd - t) / stepsF, 1e-4);
        float invL = 1.0 / max(L, 1e-3);
        float tPrev = t;
        int   cap = 2 * (int)stepsF;
        [loop] for (int k = 0; k < cap; k++) {
            float f = FieldMask(ro + rd * t, mask);
            if (f > fmax) { fmax = f; tMax = t; }
            if (f >= thresh) { hit = true; break; }
            if (t >= tEnd) break;
            tPrev = t;
            // near the iso-surface the floor step shrinks, so a grazing wall
            // (tunnel exits, necks) is not caught or missed by step phase
            float floorStep = (f > 0.75 * thresh) ? minStep * 0.3 : minStep;
            t = min(t + max((thresh - f) * invL, floorStep), tEnd);
        }
        if (hit) {
            float a = tPrev, b = t;
            [loop] for (int j = 0; j < 7; j++) {
                float mid = 0.5 * (a + b);
                if (FieldMask(ro + rd * mid, mask) >= thresh) b = mid; else a = mid;
            }
            tHit = b;
        }
    }

    float3 col = back;
    float  cov = 0.0, dk = 1.0;
    float  tS = tHit;
    if (hit) {
        cov = 1.0;
    } else if (fmax > thresh * 0.5) {
        // grazing miss: analytic 1-pixel coverage from the field's distance
        // estimate (thr - f) / |grad| at the ray's closest approach = AA
        float f2, a2; float3 g2, b2;
        FieldFull(ro + rd * tMax, mask, f2, g2, b2, a2);
        float dist = (thresh - f2) / max(length(g2), 1e-4);
        float px = tMax * 2.0 * tanHalf * invScreen.y;
        cov = saturate(1.0 - dist / px);
        tS = tMax;
    }
    if (cov > 0.0) {
        float3 p = ro + rd * tS;
        float f, acc; float3 g, base;
        FieldFull(p, mask, f, g, base, acc);
        float gl = length(g);
        float3 n = gl > 1e-6 ? -g / gl : -rd;
        float3 v = -rd;
        float ndv = saturate(dot(n, v));
        // where a main-colour mass and an accent mass merge: a thin seam hue
        float seam = saturate(1.0 - abs(acc - 0.5) * 5.0);   // acc 0.3..0.7 only
        base = lerp(base, seamCol, seam * 0.85);
        int look = (int)lookF;
        float3 sc;
        [branch] if (look > 0) {
            float thick = Thickness(p, rd, mask);
            sc = ShadeLook(look, p, n, v, rd, base, ndv, gl, thick);
        } else {
            sc = (shadeMode < 0.5) ? ShadeGloss(p, n, v, base, ndv, gl)
                                   : ShadeFlat(n, base, ndv);
        }
        dk = saturate(p.z / slabDepth);
        float fogK = (look == 2) ? 0.30 : ((look == 7) ? 0.55 : 0.70);
        sc = lerp(sc, back, fogK * pow(dk, 1.5));      // depth fog toward the wall
        col = lerp(back, sc, cov);
    }
    if ((int)lookF == 1) {
        // a faint glass cylinder: darker toward the sides, two vertical
        // reflections, a warm lamp glow at the very bottom
        float ax = abs(ndc.x);
        col *= 1.0 - 0.55 * smoothstep(0.55, 1.0, ax);
        float e1 = (ndc.x + 0.72) * 14.0, e2 = (ndc.x - 0.80) * 20.0;
        col += float3(1.0, 0.95, 0.9) * (0.05 * exp(-e1 * e1) + 0.025 * exp(-e2 * e2));
        col += mainCol * float3(1.0, 0.8, 0.6) * 0.05 * smoothstep(-0.55, -1.0, ndc.y);
    }
    // alpha = depth in the slab (1 = the far wall) for the post pass's depth of field
    return float4(max(col, 0.0), lerp(1.0, dk, cov));
}
)hlsl";

// post: PSDown (full -> quarter res, 16-px box), PSBlur (quarter res 9x9
// gaussian, tap spacing = spread), PSPost (DOF by the depth in alpha, warm
// halation from the blurred image, chromatic aberration growing toward the
// corners, vignette, multiplicative grain that keeps black black, sdrScale).
// QUICK approximation; the faithful 2D stack is being ported separately.
// One cbuffer layout for all three entry points.
static const char* kPostSrc = R"hlsl(
cbuffer PCB : register(b0) {
    float2 invDst;   float sdrScale; float maxOut;     // maxOut = panel_max / 80 with HDR
    float2 srcTexel; float spread;   float timeSeed;
    float  halAmt;   float3 halTint;
    float  aberr;    float grain;    float vignette; float pad0;
    float  dofAmt;   float focus;    float dofRange; float pad1;
};
Texture2D<float4> gRay  : register(t1);
Texture2D<float4> gDown : register(t2);
Texture2D<float4> gBlur : register(t3);
SamplerState gSamp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 PSDown(float4 svp : SV_Position) : SV_Target {
    float2 uv = svp.xy * invDst;
    float2 o = srcTexel;
    return 0.25 * (gRay.SampleLevel(gSamp, uv + float2(-o.x, -o.y), 0) +
                   gRay.SampleLevel(gSamp, uv + float2( o.x, -o.y), 0) +
                   gRay.SampleLevel(gSamp, uv + float2(-o.x,  o.y), 0) +
                   gRay.SampleLevel(gSamp, uv + float2( o.x,  o.y), 0));
}

// tap spacing must stay <= ~1.3 texels: at 2.2-3.0 the 9x9 taps skipped
// texels and drew a screen-door grid / ripples into the halation and DOF. A
// wide blur is an odd number of ping-pong passes instead (PSBlur t2 -> blur,
// PSBlurB t3 -> down, ...), so the result always lands in the blur target.
float4 Blur9(Texture2D<float4> src, float2 uv) {
    float4 acc = float4(0, 0, 0, 0);
    float  ws = 0.0;
    [unroll] for (int y = -4; y <= 4; y++) {
        [unroll] for (int x = -4; x <= 4; x++) {
            float w = exp(-(float)(x * x + y * y) / (2.0 * 2.6 * 2.6));
            acc += src.SampleLevel(gSamp, uv + float2(x, y) * invDst * spread, 0) * w;
            ws += w;
        }
    }
    return acc / ws;
}
float4 PSBlur(float4 svp : SV_Position) : SV_Target  { return Blur9(gDown, svp.xy * invDst); }
float4 PSBlurB(float4 svp : SV_Position) : SV_Target { return Blur9(gBlur, svp.xy * invDst); }

float HashP(float2 p) {
    uint2 q = (uint2)p;
    uint h = q.x * 1973u + q.y * 9277u + (uint)timeSeed * 26699u + 1u;
    h = (h ^ 61u) ^ (h >> 16);
    h *= 9u;   h ^= h >> 4;
    h *= 0x27d4eb2du;
    h ^= h >> 15;
    return (float)(h & 0x00ffffffu) * (1.0 / 16777216.0);
}

float4 PSPost(float4 svp : SV_Position) : SV_Target {
    float2 uv = svp.xy * invDst;
    float2 dc = uv - 0.5;
    float r2 = dot(dc, dc) * 4.0;
    float2 off = dc * aberr * r2 * 0.0025;
    float4 s = gRay.SampleLevel(gSamp, uv, 0);
    float3 c = float3(gRay.SampleLevel(gSamp, uv + off, 0).r, s.g,
                      gRay.SampleLevel(gSamp, uv - off, 0).b);
    float3 bl = gBlur.SampleLevel(gSamp, uv, 0).rgb;
    if (dofAmt > 0.0) {
        float coc = smoothstep(0.0, 1.0, saturate(abs(s.a - focus) / dofRange));
        c = lerp(c, bl, coc * dofAmt);
    }
    c += bl * halTint * halAmt;
    float vr = length(dc * float2(1.0, 0.85)) * 1.6;
    c *= 1.0 - vignette * smoothstep(0.35, 1.15, vr);
    float g = HashP(svp.xy) + HashP(svp.xy + float2(17.0, 59.0)) - 1.0;   // triangular
    c = max(c * (1.0 + g * grain), 0.0);
    return float4(min(c * sdrScale, maxOut), 1.0);
}
)hlsl";
