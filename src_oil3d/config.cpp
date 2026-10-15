#include "config.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <set>

extern void Oil3DLog(const char* fmt, ...);   // main.cpp

// ---------------------------------------------------------------------------
// ini helpers. Partial overlay: a key that is not in the file leaves the
// field alone (the src/main.cpp convention). Every key a reader asks for is
// remembered per section, so whatever else the file carries can be listed as
// "no 3D meaning" afterwards.
// ---------------------------------------------------------------------------
namespace {

struct Reader {
    const wchar_t* path;
    std::set<std::wstring> known;    // "section|key"

    bool Str(const wchar_t* sec, const wchar_t* key, wchar_t* out, DWORD n) {
        known.insert(std::wstring(sec) + L"|" + key);
        out[0] = 0;
        GetPrivateProfileStringW(sec, key, L"\x01", out, n, path);
        return out[0] != L'\x01' && out[0] != 0;
    }
    void F(const wchar_t* sec, const wchar_t* key, float& v) {
        wchar_t b[128];
        if (Str(sec, key, b, 128)) v = (float)_wtof(b);
    }
    void I(const wchar_t* sec, const wchar_t* key, int& v) {
        wchar_t b[128];
        if (Str(sec, key, b, 128)) v = _wtoi(b);
    }
    void B(const wchar_t* sec, const wchar_t* key, bool& v) {
        wchar_t b[128];
        if (Str(sec, key, b, 128)) v = _wtoi(b) != 0;
    }
    // "r g b" (the 2D form) or "r,g,b" (the ink3d form); n floats
    int Vec(const wchar_t* sec, const wchar_t* key, float* v, int n) {
        wchar_t b[256];
        if (!Str(sec, key, b, 256)) return 0;
        for (wchar_t* c = b; *c; c++) if (*c == L',' || *c == L'\t') *c = L' ';
        float f[12] = {};
        int got = 0;
        const wchar_t* s = b;
        while (got < 12) {
            wchar_t* end = nullptr;
            const double d = wcstod(s, &end);
            if (end == s) break;
            f[got++] = (float)d;
            s = end;
        }
        if (got < n) return got;   // too short: leave the field alone
        for (int i = 0; i < n; i++) v[i] = f[i];
        return got;
    }
};

std::wstring FullPath(const wchar_t* p) {
    wchar_t full[MAX_PATH * 2] = {};
    // GetPrivateProfileString resolves a RELATIVE path against the Windows
    // directory, not the working directory -- always hand it an absolute one.
    if (GetFullPathNameW(p, MAX_PATH * 2, full, nullptr)) return full;
    return p;
}

// [meta] base= of a preset: relative to the preset's own folder, or absolute.
std::wstring MetaBase(const std::wstring& path) {
    wchar_t b[MAX_PATH] = {};
    GetPrivateProfileStringW(L"meta", L"base", L"", b, MAX_PATH, path.c_str());
    if (!b[0]) return L"";
    std::wstring base = b;
    while (!base.empty() && (base.back() == L' ' || base.back() == L'\t')) base.pop_back();
    size_t a = 0;
    while (a < base.size() && (base[a] == L' ' || base[a] == L'\t')) a++;
    base = base.substr(a);
    const bool absolute = base.size() > 2 &&
                          (base[1] == L':' || (base[0] == L'\\' && base[1] == L'\\'));
    if (!absolute) {
        const size_t sl = path.find_last_of(L"\\/");
        base = (sl == std::wstring::npos ? std::wstring(L".") : path.substr(0, sl)) + L"\\" + base;
    }
    base = FullPath(base.c_str());
    if (GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Oil3DLog("[ini] WARNING: [meta] base not found: %ls (from %ls)\n",
                 base.c_str(), path.c_str());
        return L"";
    }
    return base;
}

void ReadOne(const std::wstring& path, Oil3DConfig& cfg, std::vector<std::wstring>* ignored) {
    Reader r{ path.c_str(), {} };
    OilKeys& a = cfg.oil;
    const wchar_t* S = L"liquid_acid";

    r.I(S, L"blob_count", a.blobCount);
    r.F(S, L"disc_frac", a.discFrac);     r.F(S, L"web_frac", a.webFrac);
    r.F(S, L"bubble_frac", a.bubbleFrac);
    r.F(S, L"disc_min", a.discMin);       r.F(S, L"disc_max", a.discMax);
    r.F(S, L"web_min", a.webMin);         r.F(S, L"web_max", a.webMax);
    r.F(S, L"bubble_min", a.bubbleMin);   r.F(S, L"bubble_max", a.bubbleMax);
    r.F(S, L"hole_min", a.holeMin);       r.F(S, L"hole_max", a.holeMax);
    r.F(S, L"size_bias", a.sizeBias);     r.F(S, L"big_bias", a.bigBias);
    r.F(S, L"hole_weight", a.holeWeight);
    r.F(S, L"threshold", a.threshold);    r.F(S, L"support_scale", a.supportScale);
    r.F(S, L"flow_gain", a.flowGain);     r.F(S, L"curl_drift", a.curlDrift);
    r.F(S, L"repulsion", a.repulsion);    r.F(S, L"buoyancy", a.buoyancy);
    r.F(S, L"damping", a.damping);        r.F(S, L"breath", a.breathAmt);
    r.F(S, L"wrap_margin", a.wrapMargin);
    r.F(S, L"rise_speed", a.riseSpeed);   r.F(S, L"rise_wobble", a.riseWobble);
    r.B(S, L"rise_respawn", a.riseRespawn);
    r.F(S, L"rise_stretch", a.riseStretch);
    r.F(S, L"rise_bottom_light", a.riseBottomLight);
    r.F(S, L"rise_parallax", a.riseParallax);
    r.F(S, L"rise_parallax_dim", a.riseParallaxDim);
    r.F(S, L"oil_viscosity", a.oilViscosity);
    r.F(S, L"conserve_mass", a.conserveMass);
    r.F(S, L"spawn_grow_s", a.spawnGrowS);
    r.F(S, L"dissolve_s", a.dissolveS);
    r.F(S, L"blob_turnover_s", a.blobTurnoverS);

    r.F(S, L"hue_sweep_period", a.hueSweepPeriod);
    r.F(S, L"hue_rotate_period", a.hueRotatePeriod);
    r.F(S, L"hue_anchor_weight", a.hueAnchorWeight);
    r.F(S, L"palette_start_hue", a.paletteStartHue);
    r.F(S, L"oil_saturation", a.oilSaturation);
    r.I(S, L"sweep_count", a.sweepCount);
    if (a.sweepCount < 1) a.sweepCount = 1;
    if (a.sweepCount > OilKeys::kSweepMax) a.sweepCount = OilKeys::kSweepMax;
    for (int ci = 0; ci < OilKeys::kSweepMax; ci++) {
        // sweep_pair_N_oil / sweep_oil_N: 3 floats (anchor) or 12 (four shades)
        wchar_t k1[40], k2[40];
        swprintf_s(k1, L"sweep_pair_%d_oil", ci + 1);
        swprintf_s(k2, L"sweep_oil_%d", ci + 1);
        float f[12];
        int n = r.Vec(S, k1, f, 3);
        if (!n) n = r.Vec(S, k2, f, 3);
        if (n >= 3) {
            for (int k = 0; k < 3; k++) a.sweepOil[ci * 3 + k] = f[k];
            a.sweepOilFullSet[ci] = (n >= 12);
            if (n >= 12) for (int k = 0; k < 12; k++) a.sweepOilFull[ci * 12 + k] = f[k];
        }
        swprintf_s(k1, L"sweep_pair_%d_ink", ci + 1);
        swprintf_s(k2, L"sweep_ink_%d", ci + 1);
        if (!r.Vec(S, k1, &a.sweepInk[ci * 3], 3)) r.Vec(S, k2, &a.sweepInk[ci * 3], 3);
    }
    for (int ci = 0; ci < 4; ci++) {
        wchar_t key[32];
        swprintf_s(key, L"oil_color_%d", ci + 1);
        r.Vec(S, key, &a.oilColors[ci * 3], 3);
        swprintf_s(key, L"ink_stop_%d", ci + 1);
        r.Vec(S, key, &a.inkRamp[ci * 3], 3);
    }
    r.Vec(S, L"meniscus_color", a.meniscusCol, 3);
    r.I(S, L"accent_mode", a.accentMode);
    r.F(S, L"accent_max_r", a.accentMaxR);
    r.F(S, L"accent_frac", a.accentFrac);

    r.F(S, L"film_hue2", a.filmHue2);           r.F(S, L"film_hue2_amt", a.filmHue2Amt);
    r.F(S, L"film_hue2_scale", a.filmHue2Scale); r.F(S, L"film_hue2_drift", a.filmHue2Drift);
    r.F(S, L"film_hue2_rise", a.filmHue2Rise);
    r.F(S, L"film_hue2_wobble", a.filmHue2Wobble);
    r.F(S, L"film_hue2_wobble_period", a.filmHue2WobbleP);
    r.F(S, L"film_hue2_cover", a.filmHue2Cover);
    r.F(S, L"film_hue2_seam", a.filmHue2Seam);
    r.F(S, L"film_hue3", a.filmHue3);           r.F(S, L"film_hue3_amt", a.filmHue3Amt);
    r.F(S, L"film_hue3_share", a.filmHue3Share);
    r.F(S, L"film_equal_load", a.filmEqualLoad);
    r.F(S, L"film_equal_load_patches", a.filmEqualLoadP);
    r.F(S, L"crust_hue_mix", a.crustHueMix);
    r.F(S, L"film_level", a.filmLevel);
    r.F(S, L"mass_rim", a.massRim);

    r.F(S, L"dye_hue", a.dyeHue);   r.F(S, L"dye_sat", a.dyeSat);   r.F(S, L"dye_lum", a.dyeLum);
    r.I(S, L"dye_hue_follow", a.dyeHueFollow);
    r.F(S, L"dye_droplets", a.dyeDroplets);    r.F(S, L"dye_masses", a.dyeMasses);
    r.F(S, L"dye_droplet_hue", a.dyeDropHue);  r.F(S, L"dye_droplet_sat", a.dyeDropSat);
    r.F(S, L"dye_droplet_lum", a.dyeDropLum);
    r.F(S, L"dye_core", a.dyeCore);

    r.F(S, L"shadow_tone", a.shadowTone);
    r.F(S, L"shadow_tone_lift", a.shadowToneLift);
    r.F(S, L"shadow_tone_sat", a.shadowToneSat);
    r.F(S, L"shadow_tone_period", a.shadowTonePeriod);
    r.F(S, L"shadow_tone_fade", a.shadowToneFade);
    r.F(S, L"shadow_tone_hue", a.shadowToneHue);

    {   // ink_mode = bands | water (string wins); int form ink_water = 0|1
        wchar_t m[32];
        if (r.Str(S, L"ink_mode", m, 32)) a.inkMode = (_wcsicmp(m, L"water") == 0) ? 1 : 0;
        bool w = a.inkMode != 0;
        r.B(S, L"ink_water", w);
        a.inkMode = w ? 1 : 0;
    }
    r.F(S, L"ink_levels", a.inkLevels);
    r.F(S, L"ink_soft", a.inkSoft);

    r.I(S, L"droplets", a.droplets);
    r.F(S, L"droplet_r_min", a.dropletRMin);  r.F(S, L"droplet_r_max", a.dropletRMax);
    r.F(S, L"droplet_bias", a.dropletBias);   r.F(S, L"droplet_rise", a.dropletRise);
    r.F(S, L"droplet_racer_frac", a.dropletRacerFrac);
    r.F(S, L"droplet_racer_speed", a.dropletRacerSpeed);
    r.F(S, L"droplet_racer_wobble", a.dropletRacerWobble);
    r.F(S, L"droplet_racer_r_max", a.dropletRacerRMax);
    r.F(S, L"post_chroma", a.postChroma);
    r.F(S, L"post_lift", a.postLift);

    // [ink] (the shared ink-in-water block)
    {
        const wchar_t* K = L"ink";
        r.B(K, L"inverted", cfg.ink.inverted);
        r.Vec(K, L"paper_color", cfg.ink.paper, 3);
        r.Vec(K, L"tint_thin", cfg.ink.tintThin, 3);
        r.Vec(K, L"tint_thick", cfg.ink.tintThick, 3);
    }
    r.I(L"behavior", L"idle_splats", cfg.idleSplats);
    r.I(L"drops", L"drops", cfg.dropsOn);
    r.F(L"hdr", L"peak_nits", cfg.peakNits);
    r.F(L"hdr", L"knee", cfg.knee);

    // [oil3d]
    {
        const wchar_t* O = L"oil3d";
        r.I(O, L"blobs", cfg.blobs);
        r.B(O, L"half_res", cfg.halfRes);
        r.I(O, L"steps", cfg.steps);
        r.I(O, L"refine", cfg.refine);
        r.F(O, L"camera_drift", cfg.cameraDrift);
        r.F(O, L"camera_drift_period", cfg.cameraDriftPeriod);
        wchar_t sh[32];
        if (r.Str(O, L"shade", sh, 32))
            cfg.shade = (_wcsicmp(sh, L"flat") == 0) ? OIL3D_FLAT : OIL3D_GLOSS;
        r.F(O, L"fov", cfg.fovDeg);
        r.F(O, L"depth", cfg.depth);
        r.F(O, L"radius_scale", cfg.radiusScale);
        r.F(O, L"hole_depth_stretch", cfg.holeDepthStretch);
        r.F(O, L"cavity_dark", cfg.cavityDark);
        r.F(O, L"z_drift", cfg.zDrift);
        r.F(O, L"flow_amp", cfg.flowAmp);
        r.F(O, L"parallax3d", cfg.parallax3d);
        r.Vec(O, L"light0_dir", cfg.light0Dir, 3);
        r.F(O, L"light0", cfg.light0);
        r.Vec(O, L"light1_dir", cfg.light1Dir, 3);
        r.F(O, L"light1", cfg.light1);
        r.F(O, L"wrap", cfg.wrap);
        r.F(O, L"ambient", cfg.ambient);
        r.F(O, L"spec", cfg.spec);
        r.F(O, L"spec_power", cfg.specPower);
        r.F(O, L"rim", cfg.rim);
        r.F(O, L"rim_power", cfg.rimPower);
        r.F(O, L"sss", cfg.sss);
        r.F(O, L"sss_sigma", cfg.sssSigma);
        r.F(O, L"ao", cfg.ao);
        r.F(O, L"fog", cfg.fog);
        r.F(O, L"exposure", cfg.exposure);
        r.F(O, L"edge_soft", cfg.edgeSoft);
        r.I(O, L"flat_bands", cfg.flatBands);
        r.F(O, L"flat_soft", cfg.flatSoft);
        r.F(O, L"flat_rim_width", cfg.flatRimWidth);
        r.F(O, L"flat_rim_dark", cfg.flatRimDark);
        r.F(O, L"ink_marble", cfg.inkMarble);
        r.F(O, L"ink_marble_scale", cfg.inkMarbleScale);
        r.F(O, L"ink_flow", cfg.inkFlow);
        r.I(O, L"droplet_cap", cfg.dropletCap);
        r.F(O, L"droplet_scale", cfg.dropletScale);
        r.F(O, L"mix_contrast", cfg.mixContrast);
        r.F(O, L"hue_rotate_period", cfg.hueRotateOverride);
    }

    // What the file carries in [liquid_acid] / [oil3d] that was never asked
    // for: no 3D meaning (or a typo). Other sections are the 2D app's own
    // (post, sim, color...) and are not listed key by key.
    if (ignored) {
        const wchar_t* secs[] = { L"liquid_acid", L"oil3d" };
        for (const wchar_t* sec : secs) {
            std::vector<wchar_t> buf(32768);
            const DWORD n = GetPrivateProfileSectionW(sec, buf.data(), (DWORD)buf.size(),
                                                      path.c_str());
            for (DWORD i = 0; i < n && buf[i]; ) {
                std::wstring line = &buf[i];
                i += (DWORD)line.size() + 1;
                if (line.empty() || line[0] == L';') continue;
                std::wstring key = line.substr(0, line.find(L'='));
                while (!key.empty() && (key.back() == L' ' || key.back() == L'\t')) key.pop_back();
                if (key.empty()) continue;
                if (!r.known.count(std::wstring(sec) + L"|" + key))
                    ignored->push_back(L"[" + std::wstring(sec) + L"] " + key);
            }
        }
    }
}

} // namespace

bool LoadOil3DConfig(const wchar_t* iniPath, Oil3DConfig& cfg,
                     std::vector<std::wstring>* ignored) {
    if (!iniPath || !*iniPath) return false;
    const std::wstring path = FullPath(iniPath);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    // [meta] base chain, root first (the 2D PresetBaseChain walk, depth 4)
    std::vector<std::wstring> chain;
    std::wstring cur = path;
    for (int d = 0; d < 4; d++) {
        const std::wstring base = MetaBase(cur);
        if (base.empty()) break;
        chain.insert(chain.begin(), base);
        cur = base;
    }
    // A base that an earlier --ini already applied is NOT re-applied: the
    // command line stacks base, layout, stage exactly like the 2D cycle
    // ([cycle] oil_layout is loaded between a stage's base and its file), so
    // `--ini acid-rise-12.ini --ini "Layout - Few giants.ini"
    //  --ini acid-rise-12-tone-half.ini` keeps the layout even though the
    // stage's own [meta] base is acid-rise-12.ini.
    static std::set<std::wstring> s_applied;
    auto key = [](std::wstring p) {
        for (wchar_t& c : p) c = (wchar_t)towlower(c);
        return p;
    };
    for (const std::wstring& b : chain) {
        if (s_applied.count(key(b))) {
            Oil3DLog("[ini]   base %ls (already applied, skipped)\n", b.c_str());
            continue;
        }
        Oil3DLog("[ini]   base %ls\n", b.c_str());
        ReadOne(b, cfg, ignored);
        s_applied.insert(key(b));
    }
    ReadOne(path, cfg, ignored);
    s_applied.insert(key(path));
    return true;
}

void FinalizeOil3DConfig(Oil3DConfig& cfg) {
    if (cfg.steps < 8)   cfg.steps = 8;
    if (cfg.steps > 256) cfg.steps = 256;
    if (cfg.refine < 0)  cfg.refine = 0;
    if (cfg.refine > 10) cfg.refine = 10;
    if (cfg.fovDeg < 10.0f) cfg.fovDeg = 10.0f;
    if (cfg.fovDeg > 80.0f) cfg.fovDeg = 80.0f;
    if (cfg.depth < 0.05f) cfg.depth = 0.05f;
    if (cfg.dropletCap < 0)  cfg.dropletCap = 0;
    if (cfg.dropletCap > 64) cfg.dropletCap = 64;
    if (cfg.flatBands < 1) cfg.flatBands = 1;
    if (cfg.oil.supportScale < 0.5f) cfg.oil.supportScale = 0.5f;
    if (cfg.oil.threshold < 0.01f) cfg.oil.threshold = 0.01f;
    if (cfg.oil.threshold > 0.99f) cfg.oil.threshold = 0.99f;
}
