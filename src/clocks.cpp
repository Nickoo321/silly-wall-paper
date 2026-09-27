// Brief CLOCKS, phase 1 -- see clocks.h for the model and the contract.
// Binding sources: reference/briefs/CLOCKS.md, its AUDITOR PRE-FLIGHT
// (2026-09-27, items 1-30; phase 1 = item 30) and the FABLE DECISIONS.
//
// Phase 1 scope: multiplicative groups only -- grain, film (+ tail adds),
// lens (+ tail adds), lid, rig (strength keys), the split-tone and hue2
// AMOUNTS, dye (non-sentinel). Angular walks, the fluid colour group, the sim
// groups, per-stage shapes and the UI row are phase 2.

#include <windows.h>
#include "clocks.h"
#include "animators.h"
#include "app_state.h"
#include "cycle.h"
#include "journey.h"
#include "ui/ui_model.h"   // G_* / KF_* for keys.inc
#include "ui/ui_cycle.h"   // the [cycle] row pointers keys.inc names
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ---- groups -------------------------------------------------------------------
enum Group { GR_GRAIN = 0, GR_FILM, GR_LENS, GR_LID, GR_RIG, GR_SPLITTONE, GR_HUE2, GR_DYE, GR_COUNT };
const char* const kGroupName[GR_COUNT] = {
    "grain", "film", "lens", "lid", "rig", "splittone", "hue2", "dye"
};
// Which looks a group's TAILS (and adds) run on: bit 1 fluid, 2 liquid_acid,
// 4 ink. Brief §2: fluid and ink read grain + lens; oil reads them all. The
// body clocks run everywhere (a key the look does not read is simply inert).
const unsigned kLookF = 1, kLookA = 2, kLookI = 4;
const unsigned kGroupLooks[GR_COUNT] = {
    kLookF | kLookA | kLookI, kLookA, kLookF | kLookA | kLookI, kLookA, kLookA, kLookA, kLookA, kLookA
};

// ---- the phase-1 key table (keyed by section.key, pre-flight 17) ----------------
// Every row is a keys.inc FLOAT slider (floatPtr into FluidConfig, pre-flight 6);
// Build() refuses anything else. Flags:
enum : unsigned {
    K_FLIP  = 1,    // the effect grows as the value DROPS (pre-flight 19): F' = exp(-s z - s^2/2)
    K_ABL   = 2,    // brightness-capped: hi = min(1.25 x base, slider max) (brief §1 list)
    K_REEL1 = 4,    // slot-gated by a hash < amount per film REEL: commit only when the reel
    K_REEL3 = 8,    //   re-rolls (hairs: film_artefact_rate, scratches x3, leak x5), so an
    K_REEL5 = 16,   //   amount change never pops a hair / scratch / leak in mid-life
};
struct Spec { const char* sec; const char* key; int group; unsigned flags; };
const Spec kSpec[] = {
    // grain (10). Out: dither (pre-flight 17), film_grain_fps + film_grain_speed (phase-snapping, 12).
    { "liquid_acid", "grain",               GR_GRAIN, 0 },
    { "liquid_acid", "grain_scale",         GR_GRAIN, 0 },
    { "liquid_acid", "grain_shadow_weight", GR_GRAIN, 0 },
    { "post", "film_grain",                 GR_GRAIN, 0 },
    { "post", "film_grain_chroma",          GR_GRAIN, 0 },
    { "post", "film_grain_color",           GR_GRAIN, 0 },
    { "post", "film_grain_density",         GR_GRAIN, 0 },
    { "post", "film_grain_size",            GR_GRAIN, 0 },
    { "post", "film_noise",                 GR_GRAIN, 0 },
    { "post", "film_noise_size",            GR_GRAIN, 0 },
    // film (9). Out: film_artefact_rate (phase-snapping), lid_scratch_corner (a placement),
    // lid_scratch_density + lid_scratch_len (a STATIC hash < amount population on the lid:
    // every step pops a fixed scratch / a 300-1000 px gouge in or out, forever).
    { "post", "film_dust",                  GR_FILM, 0 },
    { "post", "film_hairs",                 GR_FILM, K_REEL1 },
    { "post", "film_scratches",             GR_FILM, K_REEL3 },
    { "post", "film_leak",                  GR_FILM, K_REEL5 },
    { "post", "film_stock",                 GR_FILM, 0 },
    { "post", "glass_streaks",              GR_FILM, 0 },
    { "post", "lid_scratch",                GR_FILM, 0 },
    { "post", "lid_scratch_soft",           GR_FILM, 0 },
    { "post", "lid_scratch_tint",           GR_FILM, 0 },
    // lens (30). Out: band_min + pixel_shift_px (pre-flight 17), bloom by name (17) and its
    // bloom_px / bloom_warmth (bloom is an AGENTS hard constraint), curve_center (a position
    // on the tone axis), ink.vignette (ink group, phase 2). camera_fov moved here from rig:
    // the brief's lens-distortion episode (tail_add_camera_fov) rides the LENS tail.
    { "color", "curve_width",               GR_LENS, 0 },
    { "color", "curve_height",              GR_LENS, K_ABL },
    { "color", "shadow_floor",              GR_LENS, K_ABL },
    { "color", "shadow_knee",               GR_LENS, 0 },
    { "post", "aberration",                 GR_LENS, 0 },
    { "post", "aberration_coc",             GR_LENS, 0 },
    { "post", "aberration_field",           GR_LENS, 0 },
    { "post", "aberration_px",              GR_LENS, 0 },
    { "post", "artefact_lum_gate",          GR_LENS, 0 },
    { "post", "corner_warp",                GR_LENS, 0 },
    { "post", "corner_warp_r",              GR_LENS, K_FLIP },
    { "post", "fog",                        GR_LENS, 0 },
    { "post", "fog_mass_gate",              GR_LENS, 0 },
    { "post", "fog_px",                     GR_LENS, 0 },
    { "post", "halation",                   GR_LENS, K_ABL },
    { "post", "halation_px",                GR_LENS, 0 },
    { "post", "halation_threshold",         GR_LENS, K_ABL },
    { "post", "halation_warmth",            GR_LENS, 0 },
    { "post", "halo",                       GR_LENS, 0 },
    { "post", "halo_px",                    GR_LENS, 0 },
    { "post", "post_blur_px",               GR_LENS, 0 },
    { "post", "post_glow",                  GR_LENS, 0 },
    { "post", "post_glow_dark",             GR_LENS, 0 },
    { "post", "post_glow_px",               GR_LENS, 0 },
    { "post", "shimmer",                    GR_LENS, 0 },
    { "post", "shimmer_px",                 GR_LENS, 0 },
    { "post", "softness",                   GR_LENS, 0 },
    { "post", "vignette",                   GR_LENS, 0 },
    { "post", "vignette_wander",            GR_LENS, 0 },
    { "post", "camera_fov",                 GR_LENS, 0 },
    // lid (8). Out: lid_ghost_spread (packed in 4 bits: every step jumps the ghost chain).
    { "post", "lid",                        GR_LID, 0 },
    { "post", "lid_ghost",                  GR_LID, 0 },
    { "post", "lid_glint",                  GR_LID, 0 },
    { "post", "lid_iris",                   GR_LID, 0 },
    { "post", "lid_refract_px",             GR_LID, 0 },
    { "post", "lid_rings",                  GR_LID, 0 },
    { "post", "lid_sheen",                  GR_LID, 0 },
    { "post", "lid_sheen_px",               GR_LID, 0 },
    // rig: STRENGTH keys only (15). Out: the positions light_x/y/z, camera_axis_x/y,
    // camera_focus, focus_tilt_angle (pre-flight 15); the times focus_tilt_move_s,
    // focus_tilt_period; the extents lamp_grey_size, oil_fluor_reach, oil_penumbra_px,
    // shadow_len, shadow_soft and the hue turn oil_penumbra_hue (not strengths).
    { "liquid_acid", "lamp_grey",           GR_RIG, 0 },
    { "liquid_acid", "lamp_grey_cool",      GR_RIG, 0 },
    { "liquid_acid", "oil_fluor",           GR_RIG, 0 },
    { "liquid_acid", "oil_hdr",             GR_RIG, K_ABL },
    { "liquid_acid", "oil_penumbra",        GR_RIG, 0 },
    { "liquid_acid", "oil_penumbra_dark",   GR_RIG, 0 },
    { "liquid_acid", "rise_bottom_light",   GR_RIG, 0 },
    { "liquid_acid", "shadow_amt",          GR_RIG, 0 },
    { "post", "camera_field_curve",         GR_RIG, 0 },
    { "post", "dof_max_px",                 GR_RIG, 0 },
    { "post", "focus_band_px",              GR_RIG, K_FLIP },
    { "post", "focus_tilt",                 GR_RIG, 0 },
    { "post", "light_drift",                GR_RIG, 0 },
    { "post", "psf_px",                     GR_RIG, 0 },
    { "post", "rig_readjust",               GR_RIG, 0 },
    // split tone: the AMOUNT keys (7). Out: shadow/highlight_tone_hue (angles + sentinels,
    // phase 2), shadow_tone_period / shadow_tone_fade (clock periods), tone_balance (a position).
    { "liquid_acid", "shadow_tone",         GR_SPLITTONE, 0 },
    { "liquid_acid", "highlight_tone_amt",  GR_SPLITTONE, 0 },
    { "liquid_acid", "highlight_tone_desat",GR_SPLITTONE, 0 },
    { "liquid_acid", "highlight_tone_sat",  GR_SPLITTONE, 0 },
    { "liquid_acid", "shadow_tone_sat",     GR_SPLITTONE, 0 },
    { "liquid_acid", "shadow_tone_lift",    GR_SPLITTONE, K_ABL },
    { "liquid_acid", "dark_sat",            GR_SPLITTONE, 0 },
    // hue2: the AMOUNT keys (6). Out: film_hue2 / film_hue3 (angles, phase 2), seed_rows
    // (lattice, 13), drift (12), wobble_period (period), rise (a rate), decay (a per-step
    // decay, 14), scale / seam / boundary_reflect_r (layout), film_equal_load(_patches)
    // (a panel-load compensation, not a look).
    { "liquid_acid", "film_hue2_amt",       GR_HUE2, 0 },
    { "liquid_acid", "film_hue3_amt",       GR_HUE2, 0 },
    { "liquid_acid", "film_hue2_cover",     GR_HUE2, 0 },
    { "liquid_acid", "film_hue3_share",     GR_HUE2, 0 },
    { "liquid_acid", "boundary_reflect_amt",GR_HUE2, 0 },
    { "liquid_acid", "film_hue2_wobble",    GR_HUE2, 0 },
    // dye, non-sentinel (21). Out: dye_droplet_hue/sat/lum (sentinels, 16), dye_masses /
    // dye_droplets (KF_SUPERSEDED, 17), dye_depth (a position, 15), dye_hue (an angle,
    // phase 2), droplet_mass_bias (a droplet POPULATION target: droplets group, phase 2).
    { "liquid_acid", "crust_hue_mix",       GR_DYE, 0 },
    { "liquid_acid", "dye_core",            GR_DYE, 0 },
    { "liquid_acid", "dye_depth_tilt",      GR_DYE, 0 },
    { "liquid_acid", "dye_depth_w",         GR_DYE, 0 },
    { "liquid_acid", "dye_hue_vary",        GR_DYE, 0 },
    { "liquid_acid", "dye_lamp_follow",     GR_DYE, 0 },
    { "liquid_acid", "dye_lum",             GR_DYE, K_ABL },
    { "liquid_acid", "dye_lum_vary",        GR_DYE, 0 },
    { "liquid_acid", "dye_sat",             GR_DYE, 0 },
    { "liquid_acid", "dye_smoke",           GR_DYE, 0 },
    { "liquid_acid", "dye_thick_hue",       GR_DYE, 0 },
    { "liquid_acid", "ink_gain",            GR_DYE, K_ABL },
    { "liquid_acid", "ink_levels",          GR_DYE, K_FLIP },
    { "liquid_acid", "ink_mix",             GR_DYE, 0 },
    { "liquid_acid", "ink_shading",         GR_DYE, 0 },
    { "liquid_acid", "ink_soft",            GR_DYE, 0 },
    { "liquid_acid", "mass_rim",            GR_DYE, 0 },
    { "liquid_acid", "seam_lo",             GR_DYE, K_FLIP },
    { "liquid_acid", "seam_strength",       GR_DYE, 0 },
    { "liquid_acid", "speckle",             GR_DYE, 0 },
    { "liquid_acid", "toe_tint",            GR_DYE, 0 },
};
const int kSpecCount = (int)(sizeof(kSpec) / sizeof(kSpec[0]));

// Episode ADDS (brief §3): during the OWNING group's tail the key rises to
// base + amount x E(t). Oil stages only (brief §3). tail_add_<key> overrides,
// 0 turns one off. film_noise is a grain key whose add rides the FILM tail.
struct AddSpec { const char* sec; const char* key; int group; float amount; };
const AddSpec kAdds[] = {
    { "post", "film_scratches", GR_FILM, 0.35f },
    { "post", "film_hairs",     GR_FILM, 0.25f },
    { "post", "film_dust",      GR_FILM, 0.30f },
    { "post", "film_leak",      GR_FILM, 0.20f },
    { "post", "film_noise",     GR_FILM, 0.15f },
    { "post", "focus_tilt",     GR_RIG,  0.50f },
    { "post", "dof_max_px",     GR_RIG,  6.0f  },
    { "post", "corner_warp",    GR_LENS, 0.25f },
    { "post", "camera_fov",     GR_LENS, 8.0f  },
};

// Keys the director's journeys write every DWELL frame (journey.cpp StartLeg):
// held at their base while a journey runs (pre-flight 9). None is in the
// phase-1 table (they are the colour group's, phase 2); the check stays so a
// phase-2 row cannot forget it.
const char* const kJourneyKeys[][2] = {
    { "color", "hue_center" }, { "color", "hue_range" }, { "behavior", "dark_floor" },
};

// ---- parameters ([clocks]) -------------------------------------------------------
struct Params {
    bool  enabled = false;
    float sigma = 0.30f, sigmaKey = 0.10f, tailAt = 1.8f, tailLenS = 120.0f;
    float gapMinS = 20.0f * 60.0f, gapMaxS = 75.0f * 60.0f;
    float drawMinS = 300.0f, drawMaxS = 540.0f, fadeS = 20.0f;
    float w[GR_COUNT], tailw[GR_COUNT];
    Params() { for (int g = 0; g < GR_COUNT; g++) { w[g] = 1.0f; tailw[g] = 1.0f; } }
};

void (*s_logger)(const char*) = nullptr;
void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (s_logger) s_logger(buf);
    else fputs(buf, stdout);
}

float Smooth(float x) {
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x * x * (3.0f - 2.0f * x);
}
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
uint32_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// splitmix64 (the clocks' own streams; pre-flight 24)
struct Rng {
    uint64_t s = 0;
    bool haveSpare = false;
    double spare = 0.0;
    uint64_t Next() {
        s += 0x9E3779B97F4A7C15ull;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double U() { return (double)(Next() >> 11) * (1.0 / 9007199254740992.0); }   // [0,1)
    double Upos() { double u; do { u = U(); } while (u <= 0.0); return u; }
    double N() {                                  // Box-Muller
        if (haveSpare) { haveSpare = false; return spare; }
        const double r = sqrt(-2.0 * log(Upos())), a = 6.283185307179586 * U();
        spare = r * sin(a);
        haveSpare = true;
        return r * cos(a);
    }
    // z ~ N(0,1) conditioned on z > c (c > 0): Marsaglia's tail method
    double TailN(double c) {
        if (c <= 0.0) { double z; do { z = N(); } while (z <= c); return z; }
        for (;;) {
            const double x = -log(Upos()) / c, y = -log(Upos());
            if (2.0 * y >= x * x) return c + x;
        }
    }
};

// ---- the scheduler (shared by the live tick and the dry run) --------------------
enum Phase { PH_BODY = 0, PH_TAIL_IN, PH_TAIL_HOLD, PH_TAIL_OUT };
const char* const kPhaseName[] = { "body", "tail_in", "tail_hold", "tail_out" };

struct GroupState {
    int    phase = PH_BODY;
    double t = 0.0, len = 300.0, bodyLen = 300.0;
    float  fFrom = 1.0f, fTo = 1.0f, gFrom = 1.0f, gTo = 1.0f;
    float  tailF = 1.0f, tailG = 1.0f;
    float  F = 1.0f, G = 1.0f, E = 0.0f;
    bool   active = true;                 // the look reads it: tails + adds allowed
    long   draws = 0, tails = 0;
};

struct Sched {
    Params p;
    Rng    rng;
    double T = 0.0;
    GroupState g[GR_COUNT];
    int    tailGroup = -1, lastGroup = -1;
    double lastEnd = 0.0, tailStart = 0.0;
    long   tails = 0, forced = 0, natural = 0, repeats = 0, refused = 0;
    double minGap = 1e30, sumGap = 0.0, maxGap = 0.0;
    double minGlide = 1e30;
    const char* minGlideKind = "";
    bool   quiet = false;                 // dry run: per-draw lines to stdout only
    bool   verbose = true;

    float Sig(int k) const { return p.sigma * p.w[k]; }
    float Fz(double z, float s) const { return (float)exp(s * z - 0.5 * s * s); }
    float Gz(double z, float s) const { return (float)exp(-s * z - 0.5 * s * s); }
    double Zc(float s) const { return (log((double)p.tailAt) + 0.5 * s * s) / s; }

    void Glide(double len, const char* kind) {
        if (len < minGlide) { minGlide = len; minGlideKind = kind; }
    }

    void Init(unsigned seed) {
        rng.s = (uint64_t)seed * 0x2545F4914F6CDD1Dull ^ 0xC10C5C10C5C10C5Bull;
        rng.haveSpare = false;
        T = 0.0;
        tailGroup = lastGroup = -1;
        lastEnd = 0.0;
        tails = forced = natural = repeats = refused = 0;
        minGap = 1e30; sumGap = 0.0; maxGap = 0.0;
        minGlide = 1e30; minGlideKind = "";
        for (int k = 0; k < GR_COUNT; k++) {
            GroupState& s = g[k];
            s = GroupState();
            // every group starts at F = 1 (the mean) and glides to its first draw over a
            // full segment of its own random length: staggered, no two share a period
            NewSegment(k, false);
        }
    }

    void DrawLine(int k, bool tail, const char* why) {
        if (!verbose) return;
        const GroupState& s = g[k];
        const float tf = tail ? s.tailF : s.fTo, tg = tail ? s.tailG : s.gTo;
        Log("[clocks] t=%.1f group=%s F=%.3f G=%.3f tail=%d len=%.0f%s\n", T, kGroupName[k], tf, tg,
            tail ? 1 : 0, tail ? (double)p.tailLenS : s.len, why);
    }

    bool Admit(int k) {
        if (tailGroup >= 0 || T - lastEnd < p.gapMinS || !g[k].active) return false;
        if (k == lastGroup) { refused++; return false; }
        float mx = 0.0f;
        for (int j = 0; j < GR_COUNT; j++) if (p.w[j] > 0.0f && p.tailw[j] > mx) mx = p.tailw[j];
        if (mx <= 0.0f || p.tailw[k] <= 0.0f) return false;
        return rng.U() < (double)(p.tailw[k] / mx);
    }

    void StartTail(int k, double z, bool isForced) {
        GroupState& s = g[k];
        const float sg = Sig(k);
        s.tailF = Fz(z, sg);
        s.tailG = Gz(z, sg);
        s.fFrom = s.F;
        s.gFrom = s.G;
        s.phase = PH_TAIL_IN;
        s.t = 0.0;
        s.len = p.fadeS;
        s.tails++;
        Glide(p.fadeS, "tail_in");
        const double gap = T - lastEnd;
        if (tails > 0 || lastEnd > 0.0) {      // the first tail's "gap" is from the start
            if (gap < minGap) minGap = gap;
            if (gap > maxGap) maxGap = gap;
            sumGap += gap;
        }
        if (k == lastGroup) repeats++;
        tailGroup = k;
        lastGroup = k;
        tailStart = T;
        tails++;
        if (isForced) forced++; else natural++;
        DrawLine(k, true, isForced ? " TAIL forced (gap_max)" : " TAIL natural");
    }

    // A new BODY segment for group k from its current value. tryTail: a draw above
    // tail_at may become a tail (else it is clamped to tail_at: the body stays the same
    // distribution, only the excursions are governed).
    void NewSegment(int k, bool tryTail) {
        GroupState& s = g[k];
        const float sg = Sig(k);
        s.fFrom = s.F;
        s.gFrom = s.G;
        s.phase = PH_BODY;
        s.t = 0.0;
        s.len = p.drawMinS + (p.drawMaxS - p.drawMinS) * rng.U();
        s.bodyLen = s.len;
        s.draws++;
        if (!(sg > 0.0f)) {                    // group_<name> = 0: frozen at F = 1
            s.fTo = s.gTo = 1.0f;
            return;
        }
        double z = rng.N();
        const double zc = Zc(sg);
        bool clamped = false;
        if (z > zc) {
            if (tryTail && Admit(k)) { StartTail(k, z, false); return; }
            z = zc;
            clamped = true;
        }
        s.fTo = Fz(z, sg);
        s.gTo = Gz(z, sg);
        Glide(s.len, "body");
        DrawLine(k, false, clamped ? " (tail refused by the governor: clamped at tail_at)" : "");
    }

    void ForceTail() {
        double tot = 0.0;
        for (int k = 0; k < GR_COUNT; k++)
            if (k != lastGroup && g[k].active && p.w[k] > 0.0f && p.tailw[k] > 0.0f) tot += p.tailw[k];
        if (tot <= 0.0) { lastEnd = T; return; }   // nothing eligible: wait another gap_max
        const double u = rng.U() * tot;
        double acc = 0.0;
        int pick = -1;
        for (int k = 0; k < GR_COUNT; k++) {
            if (k == lastGroup || !g[k].active || !(p.w[k] > 0.0f) || !(p.tailw[k] > 0.0f)) continue;
            acc += p.tailw[k];
            pick = k;
            if (u < acc) break;
        }
        if (pick < 0) { lastEnd = T; return; }
        StartTail(pick, rng.TailN(Zc(Sig(pick))), true);
    }

    void Step(double dt) {
        if (dt <= 0.0) return;
        T += dt;
        if (tailGroup < 0 && T - lastEnd >= p.gapMaxS) ForceTail();
        for (int k = 0; k < GR_COUNT; k++) {
            GroupState& s = g[k];
            s.t += dt;
            switch (s.phase) {
            case PH_BODY: {
                const float u = Smooth((float)(s.t / s.len));
                s.F = Lerp(s.fFrom, s.fTo, u);
                s.G = Lerp(s.gFrom, s.gTo, u);
                s.E = 0.0f;
                if (s.t >= s.len) { s.F = s.fTo; s.G = s.gTo; NewSegment(k, true); }
                break;
            }
            case PH_TAIL_IN: {
                const float u = Smooth((float)(s.t / p.fadeS));
                s.F = Lerp(s.fFrom, s.tailF, u);
                s.G = Lerp(s.gFrom, s.tailG, u);
                s.E = u;
                if (s.t >= p.fadeS) { s.phase = PH_TAIL_HOLD; s.t = 0.0; s.F = s.tailF; s.G = s.tailG; s.E = 1.0f; }
                break;
            }
            case PH_TAIL_HOLD:
                s.F = s.tailF; s.G = s.tailG; s.E = 1.0f;
                if (s.t >= p.tailLenS) {
                    // out: to a fresh BODY draw (clamped at tail_at), over fade_min_s
                    const float sg = Sig(k);
                    double z = rng.N();
                    const double zc = Zc(sg);
                    if (z > zc) z = zc;
                    s.fTo = Fz(z, sg);
                    s.gTo = Gz(z, sg);
                    s.phase = PH_TAIL_OUT;
                    s.t = 0.0;
                    s.len = p.fadeS;
                    Glide(p.fadeS, "tail_out");
                }
                break;
            case PH_TAIL_OUT: {
                const float u = Smooth((float)(s.t / p.fadeS));
                s.F = Lerp(s.tailF, s.fTo, u);
                s.G = Lerp(s.tailG, s.gTo, u);
                s.E = 1.0f - u;
                if (s.t >= p.fadeS) {
                    s.F = s.fTo; s.G = s.gTo; s.E = 0.0f;
                    tailGroup = -1;
                    lastEnd = T;
                    if (verbose) Log("[clocks] t=%.1f group=%s tail ended (%.0f s)\n", T, kGroupName[k], T - tailStart);
                    NewSegment(k, false);
                }
                break;
            }
            }
        }
    }
};

// ---- live state -----------------------------------------------------------------
struct KeyRowInfo { std::string sec, key; ptrdiff_t off = -1; bool isFloat = false; float mn = 0, mx = 1;
                    unsigned flags = 0; std::string special; };

struct Key {
    const Spec* spec = nullptr;
    ptrdiff_t off = -1;
    float lo = 0.0f, hi = 1.0f, sliderMax = 1.0f;
    int   addGroup = -1;
    float add = 0.0f;
    bool  journey = false;
    int   reelMul = 0;                   // 0 = continuous, else 1 / 3 / 5 x film_artefact_rate
    // jitter
    double jT = 0.0, jLen = 300.0;
    float  jFrom = 1.0f, jTo = 1.0f, j = 1.0f;
    // adopt-on-change
    bool   have = false, haveOut = false;
    float  base = 0.0f, lastOut = 0.0f;
    double reel = -1.0;
    // proof 3e: the largest per-frame change the clocks made (never an adoption frame), per
    // cause: 0 body glide, 1 tail (its own group's or its add group's), 2 hold / engage ramp,
    // 3 a reel-quantised commit
    float  maxD[4] = {}, maxR[4] = {};
    double maxAt[4] = {};
};
const char* const kCauseName[4] = { "body", "tail", "hold/engage glide", "reel step" };

Params      s_p;
std::wstring s_ini;
Sched       s_sched;
Rng         s_keyRng;
std::vector<Key> s_keys;
bool        s_built = false, s_buildOk = false;
unsigned    s_seed = 0;
bool        s_seeded = false;
bool        s_wasActive = false;
bool        s_hold = false, s_holdLogged = false, s_settledLogged = true;
float       s_x = 0.0f;                  // engagement ramp 0..1 (smoothstepped), fade_min_s each way
double      s_clock = 0.0;               // wall time the clocks have been active
double      s_holdAt = 0.0;              // s_clock when the current hold began
// --clocks-force
bool        s_forced = false;
bool        s_forceGroup[GR_COUNT] = {};
float       s_forceF[GR_COUNT], s_forceG[GR_COUNT], s_forceE[GR_COUNT];

float* At(FluidConfig& c, ptrdiff_t off) { return reinterpret_cast<float*>(reinterpret_cast<char*>(&c) + off); }

// every keys.inc row, with the float rows' offsets inside FluidConfig
std::vector<KeyRowInfo> CollectRows() {
    std::vector<KeyRowInfo> out;
    static FluidConfig c;                 // offsets only
    const char* lo = reinterpret_cast<const char*>(&c);
    const char* hi = reinterpret_cast<const char*>(&c + 1);
    auto add = [&](const float* fp, float mn, float mx, const char* sec, const char* key, unsigned flags,
                   const char* special) {
        KeyRowInfo r;
        r.sec = sec; r.key = key; r.mn = mn; r.mx = mx; r.flags = flags; r.special = special;
        const char* pc = reinterpret_cast<const char*>(fp);
        if (fp && pc >= lo && pc < hi) { r.off = pc - lo; r.isFloat = true; }
        out.push_back(r);
    };
    auto addOther = [&](const char* sec, const char* key) {
        KeyRowInfo r;
        r.sec = sec; r.key = key;
        out.push_back(r);
    };
#define KEY_SLIDER(lbl, mn, mx, st, dec, fp, ip, sec, key, re, tip, grp, looks, gate, flags, special, front) \
    add(fp, (float)(mn), (float)(mx), sec, key, (unsigned)(flags), special);
#define KEY_CHECK(lbl, bp, sec, key, tip, grp, looks, gate, flags, special, front) addOther(sec, key);
#include "ui/keys.inc"
#undef KEY_SLIDER
#undef KEY_CHECK
    return out;
}

float TailAdd(const std::wstring& ini, const char* key, float def) {
    if (ini.empty()) return def;
    wchar_t k[96], buf[64] = {};
    swprintf_s(k, L"tail_add_%hs", key);
    GetPrivateProfileStringW(L"clocks", k, L"", buf, 64, ini.c_str());
    return buf[0] ? (float)_wtof(buf) : def;
}

void Build() {
    s_built = true;
    s_keys.clear();
    const std::vector<KeyRowInfo> rows = CollectRows();
    int bad = 0;
    for (int i = 0; i < kSpecCount; i++) {
        const Spec& sp = kSpec[i];
        const KeyRowInfo* row = nullptr;
        int n = 0;
        for (const auto& r : rows)
            if (r.sec == sp.sec && r.key == sp.key) { row = &r; n++; }
        if (!row || n != 1 || !row->isFloat || row->off < 0 ||
            (row->flags & (KF_SUPERSEDED | KF_SHELL | KF_MACHINE)) || row->special.rfind("neg:", 0) == 0) {
            Log("[clocks] ERROR: %s.%s is not a single plain float keys.inc row (found %d) -- skipped\n",
                sp.sec, sp.key, n);
            bad++;
            continue;
        }
        Key k;
        k.spec = &sp;
        k.off = row->off;
        k.lo = row->mn;
        k.sliderMax = row->mx;
        // pre-flight 18: hi = slider max for max <= 1, else 1.5 x (an outlier may leave the
        // normal range); the ABL list is capped per base in Out()
        k.hi = row->mx <= 1.0f ? row->mx : 1.5f * row->mx;
        k.reelMul = (sp.flags & K_REEL1) ? 1 : (sp.flags & K_REEL3) ? 3 : (sp.flags & K_REEL5) ? 5 : 0;
        for (auto& jk : kJourneyKeys)
            if (!strcmp(jk[0], sp.sec) && !strcmp(jk[1], sp.key)) k.journey = true;
        s_keys.push_back(k);
    }
    for (const AddSpec& a : kAdds) {
        const float amt = TailAdd(s_ini, a.key, a.amount);
        bool found = false;
        for (Key& k : s_keys)
            if (!strcmp(k.spec->sec, a.sec) && !strcmp(k.spec->key, a.key)) {
                k.addGroup = a.group;
                k.add = amt;
                found = true;
            }
        if (!found) Log("[clocks] ERROR: tail add %s.%s names no table key\n", a.sec, a.key);
    }
    // any other tail_add_<key> in [clocks] for a table key (no owner declared: its own group)
    for (Key& k : s_keys) {
        if (k.addGroup >= 0) continue;
        const float amt = TailAdd(s_ini, k.spec->key, 0.0f);
        if (amt != 0.0f) { k.addGroup = k.spec->group; k.add = amt; }
    }
    int per[GR_COUNT] = {}, adds = 0, flips = 0, abl = 0, reels = 0;
    for (const Key& k : s_keys) {
        per[k.spec->group]++;
        if (k.addGroup >= 0 && k.add != 0.0f) adds++;
        if (k.spec->flags & K_FLIP) flips++;
        if (k.spec->flags & K_ABL) abl++;
        if (k.reelMul) reels++;
    }
    Log("[clocks] table: %d keys (grain %d, film %d, lens %d, lid %d, rig %d, splittone %d, hue2 %d, dye %d); "
        "%d flipped, %d ABL-capped, %d reel-quantised, %d tail adds%s\n",
        (int)s_keys.size(), per[GR_GRAIN], per[GR_FILM], per[GR_LENS], per[GR_LID], per[GR_RIG],
        per[GR_SPLITTONE], per[GR_HUE2], per[GR_DYE], flips, abl, reels, adds, bad ? " -- TABLE ERRORS above" : "");
    s_buildOk = bad == 0;
}

bool Active() { return s_forced || (s_p.enabled && g_cycleActive); }

void EnsureSeeded() {
    if (s_seeded) return;
    s_seeded = true;
    unsigned seed = s_seed;
    if (!seed) seed = (unsigned)(GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 16));
    s_sched.p = s_p;
    s_sched.verbose = true;
    s_sched.Init(seed);
    s_keyRng.s = (uint64_t)seed * 0x9E3779B97F4A7C15ull ^ 0x6A09E667F3BCC909ull;
    s_keyRng.haveSpare = false;
    for (Key& k : s_keys) {               // each key's jitter: its own phase in its group's segment
        const GroupState& g = s_sched.g[k.spec->group];
        k.jFrom = 1.0f;
        k.jT = 0.0;
        k.jLen = g.bodyLen * (0.3 + 0.7 * s_keyRng.U());
        const double z = s_keyRng.N();
        const float sk = s_p.sigmaKey;
        k.jTo = sk > 0.0f ? (float)exp(sk * z - 0.5 * sk * sk) : 1.0f;
        k.j = 1.0f;
    }
    Log("[clocks] seed=%u%s sigma=%.2f sigma_key=%.2f tail_at=%.2f tail_len=%.0f s gap=%.0f..%.0f min "
        "draw=%.0f..%.0f s fade_min=%.0f s\n", seed, s_seed ? "" : " (wall clock)", s_p.sigma, s_p.sigmaKey,
        s_p.tailAt, s_p.tailLenS, s_p.gapMinS / 60.0f, s_p.gapMaxS / 60.0f, s_p.drawMinS, s_p.drawMaxS, s_p.fadeS);
}

void StepJitter(double dt) {
    if (dt <= 0.0) return;
    const float sk = s_p.sigmaKey;
    for (Key& k : s_keys) {
        k.jT += dt;
        if (k.jT >= k.jLen) {
            k.jFrom = k.jTo;
            const double z = s_keyRng.N();
            k.jTo = sk > 0.0f ? (float)exp(sk * z - 0.5 * sk * sk) : 1.0f;
            k.jT = 0.0;
            k.jLen = s_sched.g[k.spec->group].bodyLen;   // the group's segment length
            s_sched.Glide(k.jLen, "key jitter");
        }
        k.j = Lerp(k.jFrom, k.jTo, Smooth((float)(k.jT / k.jLen)));
    }
}

float ReadF(const wchar_t* ini, const wchar_t* key, float def) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"clocks", key, L"", buf, 64, ini);
    return buf[0] ? (float)_wtof(buf) : def;
}
float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

} // namespace

// ===================================================================================

void ClocksSetLogger(void (*fn)(const char*)) { s_logger = fn; }

void ClocksLoad(const wchar_t* ini) {
    s_p = Params();
    s_ini = ini ? ini : L"";
    s_built = false;
    if (s_ini.empty()) return;
    const wchar_t* I = s_ini.c_str();
    s_p.enabled  = GetPrivateProfileIntW(L"clocks", L"enabled", 0, I) != 0;
    s_p.sigma    = Clampf(ReadF(I, L"sigma", 0.30f), 0.05f, 0.60f);
    s_p.sigmaKey = Clampf(ReadF(I, L"sigma_key", 0.10f), 0.0f, 0.30f);
    s_p.tailAt   = Clampf(ReadF(I, L"tail_at", 1.8f), 1.2f, 3.0f);
    s_p.tailLenS = Clampf(ReadF(I, L"tail_len_s", 120.0f), 30.0f, 300.0f);
    s_p.gapMinS  = 60.0f * Clampf(ReadF(I, L"tail_gap_min_min", 20.0f), 1.0f, 120.0f);
    s_p.gapMaxS  = 60.0f * Clampf(ReadF(I, L"tail_gap_max_min", 75.0f), 2.0f, 240.0f);
    if (s_p.gapMaxS < s_p.gapMinS) s_p.gapMaxS = s_p.gapMinS;
    s_p.drawMinS = Clampf(ReadF(I, L"draw_min_s", 300.0f), 60.0f, 1800.0f);
    s_p.drawMaxS = Clampf(ReadF(I, L"draw_max_s", 540.0f), 60.0f, 1800.0f);
    if (s_p.drawMaxS < s_p.drawMinS) s_p.drawMaxS = s_p.drawMinS;
    s_p.fadeS    = Clampf(ReadF(I, L"fade_min_s", 20.0f), 5.0f, 120.0f);
    for (int g = 0; g < GR_COUNT; g++) {
        wchar_t k[64];
        swprintf_s(k, L"group_%hs", kGroupName[g]);
        s_p.w[g] = Clampf(ReadF(I, k, 1.0f), 0.0f, 2.0f);
        swprintf_s(k, L"tailw_%hs", kGroupName[g]);
        s_p.tailw[g] = Clampf(ReadF(I, k, 1.0f), 0.0f, 10.0f);
    }
    // phase-2 keys: read nowhere yet, say so rather than ignore them silently
    const wchar_t* later[] = { L"angle_max_hue2", L"angle_max_hue", L"sigma_deg_hue2", L"sigma_deg_hue" };
    for (const wchar_t* k : later) {
        wchar_t buf[16] = {};
        GetPrivateProfileStringW(L"clocks", k, L"", buf, 16, I);
        if (buf[0]) Log("[clocks] %ls is a phase-2 key (angular walks): ignored\n", k);
    }
    wchar_t sec[8] = {};
    if (GetPrivateProfileSectionW(L"clocks", sec, 8, I) > 0 || s_p.enabled)
        Log("[clocks] [clocks] from %ls: enabled=%d\n", I, s_p.enabled ? 1 : 0);
}

void ClocksSeed(unsigned seed) { s_seed = seed; s_seeded = false; }

bool ClocksForce(const wchar_t* spec) {
    if (!spec || !spec[0]) return false;
    std::wstring s = spec;
    size_t p = 0;
    bool ok = true;
    while (p <= s.size()) {
        size_t c = s.find(L',', p);
        std::wstring item = s.substr(p, c == std::wstring::npos ? std::wstring::npos : c - p);
        p = c == std::wstring::npos ? s.size() + 1 : c + 1;
        if (item.empty()) continue;
        const size_t eq = item.find(L'=');
        if (eq == std::wstring::npos) { ok = false; continue; }
        const std::wstring name = item.substr(0, eq), val = item.substr(eq + 1);
        int g0 = -1, g1 = -1;
        if (!_wcsicmp(name.c_str(), L"all")) { g0 = 0; g1 = GR_COUNT - 1; }
        else for (int g = 0; g < GR_COUNT; g++) {
            wchar_t w[32];
            swprintf_s(w, L"%hs", kGroupName[g]);
            if (!_wcsicmp(name.c_str(), w)) { g0 = g1 = g; }
        }
        if (g0 < 0) { ok = false; continue; }
        for (int g = g0; g <= g1; g++) {
            // sigma per group as the ini sets it; the flipped factor from the same z
            const float sg = s_p.sigma * s_p.w[g];
            float F = 1.0f, G = 1.0f, E = 0.0f;
            if (!_wcsicmp(val.c_str(), L"mode")) {            // z = -sigma: F at its mode
                F = (float)exp(-1.5 * sg * sg);
                G = (float)exp(0.5 * sg * sg);
            } else if (!_wcsicmp(val.c_str(), L"tail")) {     // F = 2.2 + the adds at full
                F = 2.2f;
                G = (float)(exp(-(double)sg * sg) / 2.2);
                E = 1.0f;
            } else {
                F = (float)_wtof(val.c_str());
                if (!(F > 0.0f)) { ok = false; continue; }
                G = (F == 1.0f) ? 1.0f : (float)(exp(-(double)sg * sg) / F);   // F = 1: the base, verbatim
            }
            s_forceGroup[g] = true;
            s_forceF[g] = F; s_forceG[g] = G; s_forceE[g] = E;
            Log("[clocks] --clocks-force %s F=%.4f G=%.4f adds=%.0f%%\n", kGroupName[g], F, G, E * 100.0f);
        }
    }
    s_forced = true;
    return ok;
}

bool ClocksActive() { return Active(); }

void ClocksTick(FluidRenderer& r, float dt) {
    if (!Active()) {
        if (s_wasActive) {                  // defensive: GoOff restores synchronously already
            ClocksRestoreBase(r.Config(), "clocks inactive");
            s_wasActive = false;
        }
        return;                              // enabled=0: nothing touched (bit-identical)
    }
    if (!s_built) Build();
    EnsureSeeded();
    if (dt < 0.0f) dt = 0.0f;
    if (!s_wasActive) {
        s_wasActive = true;
        s_x = s_forced ? 1.0f : 0.0f;
        Log("[clocks] active%s: %s over %.0f s\n", s_forced ? " (forced)" : "",
            s_forced ? "pinned from the first frame" : "factors glide in from 1", s_p.fadeS);
        if (!s_forced) s_sched.Glide(s_p.fadeS, "engage");
    }
    FluidConfig& c = r.Config();
    const unsigned look = c.ink.enabled ? kLookI : (c.acid.enabled ? kLookA : kLookF);
    for (int g = 0; g < GR_COUNT; g++) s_sched.g[g].active = (kGroupLooks[g] & look) != 0;

    const bool frozen = IsFrozen(ANIM_CLOCKS);
    const double dS = (s_forced || frozen || s_hold) ? 0.0 : (double)dt;
    s_clock += dt;
    if (!s_forced) {
        s_sched.Step(dS);
        StepJitter(dS);
    }
    // engagement: the Settings hold glides every factor to 1 over fade_min_s and back
    const float x0 = s_x;
    if (s_forced) s_x = 1.0f;
    else if (s_hold) s_x = fmaxf(0.0f, s_x - dt / s_p.fadeS);
    else             s_x = fminf(1.0f, s_x + dt / s_p.fadeS);
    const bool ramping = s_x != x0;
    const float a = Smooth(s_x);

    const float per = fmaxf(c.post.filmArtefactRate, 0.25f);
    const double tNow = (double)r.AnimatorTime(-1) + dt;   // m_time as the next Frame() sees it
    const bool journey = JourneyActive();
    const bool oil = look == kLookA;
    bool settled = true;
    for (Key& k : s_keys) {
        float* p = At(c, k.off);
        bool adopted = false;
        if (!k.have || Bits(*p) != Bits(k.lastOut)) {   // someone else wrote it: the new base
            k.base = *p;
            k.have = true;
            adopted = true;
        }
        const int gi = k.spec->group;
        const GroupState& gs = s_sched.g[gi];
        float Fk, jk, Ek;
        if (s_forced) {
            if (s_forceGroup[gi]) Fk = (k.spec->flags & K_FLIP) ? s_forceG[gi] : s_forceF[gi];
            else Fk = 1.0f;
            jk = 1.0f;
            Ek = (k.addGroup >= 0 && s_forceGroup[k.addGroup]) ? s_forceE[k.addGroup] : 0.0f;
        } else {
            Fk = (k.spec->flags & K_FLIP) ? gs.G : gs.F;
            jk = k.j;
            Ek = k.addGroup >= 0 ? s_sched.g[k.addGroup].E : 0.0f;
        }
        float Feff = 1.0f + a * (Fk - 1.0f);
        float jeff = 1.0f + a * (jk - 1.0f);
        float add = (k.addGroup >= 0 && oil) ? a * k.add * Ek : 0.0f;
        if (k.journey && journey) { Feff = 1.0f; jeff = 1.0f; add = 0.0f; }
        float out;
        if (Feff == 1.0f && jeff == 1.0f && add == 0.0f) {
            out = k.base;                            // VERBATIM (pre-flight 5)
        } else {
            float hi = k.hi;
            if (k.spec->flags & K_ABL) hi = fminf(1.25f * fabsf(k.base), k.sliderMax);
            const float lo = fminf(k.lo, k.base);
            hi = fmaxf(hi, k.base);                  // never clamp the base itself
            out = Clampf(k.base * Feff * jeff + add, lo, hi);
        }
        if (k.reelMul) {                              // commit only when the reel re-rolls
            const double idx = floor(tNow / ((double)per * k.reelMul));
            if (!adopted && k.haveOut && idx == k.reel) out = k.lastOut;
            else k.reel = idx;
        }
        if (!adopted && k.haveOut) {                  // proof 3e: per-frame change, clocks only
            const float d = fabsf(out - k.lastOut);
            const int ag = k.addGroup;
            const int cause = k.reelMul ? 3 : ramping ? 2
                            : (gs.phase != PH_BODY || (ag >= 0 && s_sched.g[ag].phase != PH_BODY)) ? 1 : 0;
            if (d > k.maxD[cause]) {
                k.maxD[cause] = d;
                k.maxR[cause] = fabsf(k.base) > 1e-9f ? d / fabsf(k.base) : 0.0f;
                k.maxAt[cause] = s_clock;
            }
        }
        *p = out;
        k.lastOut = out;
        k.haveOut = true;
        if (Bits(out) != Bits(k.base)) settled = false;
    }
    if (s_hold && settled && !s_settledLogged) {
        s_settledLogged = true;
        Log("[clocks] settled: live == base on all %d keys, %.2f s after the hold began\n", (int)s_keys.size(),
            s_clock - s_holdAt);
    }
}

void ClocksBaseCopy(FluidConfig& inout) {
    if (!s_built) return;                     // never ran: nothing of ours in any config
    for (const Key& k : s_keys) {
        if (!k.have || !k.haveOut) continue;
        float* p = At(inout, k.off);
        if (Bits(*p) == Bits(k.lastOut)) *p = k.base;
    }
}

void ClocksRestoreBase(FluidConfig& live, const char* why) {
    if (!s_built) return;
    int n = 0;
    for (Key& k : s_keys) {
        if (!k.have || !k.haveOut) continue;
        float* p = At(live, k.off);
        if (Bits(*p) == Bits(k.lastOut)) {
            if (Bits(*p) != Bits(k.base)) n++;
            *p = k.base;
        }
        k.lastOut = *p;                      // what is there now is not ours to adopt back
    }
    s_x = 0.0f;                               // a later start glides in again
    s_wasActive = false;
    Log("[clocks] base restored on %d key(s) (%s)\n", n, why ? why : "");
}

void ClocksHold(bool on) {
    if (on == s_hold) return;
    s_hold = on;
    if (!Active()) return;
    if (on) {
        s_settledLogged = false;
        s_holdAt = s_clock;
        Log("[clocks] hold (Settings open): every factor glides to 1 over %.0f s, the scheduler stops\n", s_p.fadeS);
    } else {
        Log("[clocks] hold released (Settings closed): factors glide back over %.0f s, the scheduler resumes\n",
            s_p.fadeS);
    }
    s_sched.Glide(s_p.fadeS, on ? "hold glide" : "release glide");
}
bool ClocksHeld() { return s_hold; }

bool ClocksSettling() {
    if (!s_hold || !Active() || !s_built) return false;
    for (const Key& k : s_keys) if (k.haveOut && Bits(k.lastOut) != Bits(k.base)) return true;
    return false;
}

bool ClocksRowSettling(const char* sec, const char* key, float* base) {
    if (!s_hold || !Active() || !s_built || !sec || !key) return false;
    for (const Key& k : s_keys)
        if (!strcmp(k.spec->sec, sec) && !strcmp(k.spec->key, key)) {
            if (!k.haveOut || Bits(k.lastOut) == Bits(k.base)) return false;
            if (base) *base = k.base;
            return true;
        }
    return false;
}

void ClocksReport() {
    if (!s_built || s_keys.empty()) return;
    char line[512];
    int len = 0;
    for (int g = 0; g < GR_COUNT; g++) {
        const GroupState& s = s_sched.g[g];
        len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, "%s%s F=%.3f G=%.3f E=%.2f %s",
                           g ? "  " : "", kGroupName[g], s.F, s.G, s.E, kPhaseName[s.phase]);
    }
    Log("[clocks] now: %s  engage=%.3f hold=%d\n", line, Smooth(s_x), s_hold ? 1 : 0);
    Log("[clocks] tails=%ld (natural %ld, forced %ld) draws: %ld; shortest glide %.1f s (%s), fade_min_s %.0f\n",
        s_sched.tails, s_sched.natural, s_sched.forced,
        [&] { long n = 0; for (auto& s : s_sched.g) n += s.draws; return n; }(),
        s_sched.minGlide < 1e29 ? s_sched.minGlide : 0.0, s_sched.minGlideKind, s_p.fadeS);
    // proof 3e: the largest per-frame change the clocks wrote, per key and cause (never an
    // adoption frame); rel = delta / |base|
    std::vector<const Key*> v;
    for (const Key& k : s_keys) v.push_back(&k);
    std::sort(v.begin(), v.end(), [](const Key* a, const Key* b) { return a->maxR[0] > b->maxR[0]; });
    for (int c = 0; c < 4; c++) {
        const Key* w = nullptr;
        for (const Key* k : v) if (k->maxD[c] > 0.0f && (!w || k->maxR[c] > w->maxR[c])) w = k;
        if (w) Log("[clocks] maxdelta-summary %-17s largest rel/frame %.6f (= %.5f/s at 144 Hz) on %s.%s "
                   "(abs %.6g, base %.4g) at %.1f s\n", kCauseName[c], w->maxR[c], w->maxR[c] * 144.0f,
                   w->spec->sec, w->spec->key, w->maxD[c], w->base, w->maxAt[c]);
        else Log("[clocks] maxdelta-summary %-17s none\n", kCauseName[c]);
    }
    for (const Key* k : v) {
        if (k->maxD[0] <= 0.0f && k->maxD[1] <= 0.0f && k->maxD[2] <= 0.0f && k->maxD[3] <= 0.0f) continue;
        Log("[clocks] maxdelta %-9s %s.%s base %.4g | body %.3g (rel %.6f) | tail %.3g (rel %.6f) | "
            "glide %.3g (rel %.6f) | reel %.3g\n", kGroupName[k->spec->group], k->spec->sec, k->spec->key,
            k->base, k->maxD[0], k->maxR[0], k->maxD[1], k->maxR[1], k->maxD[2], k->maxR[2], k->maxD[3]);
    }
}

ClocksStatus ClocksState() {
    ClocksStatus s;
    s.enabled = s_p.enabled;
    s.active = Active();
    s.held = s_hold;
    s.frozen = IsFrozen(ANIM_CLOCKS);
    s.forced = s_forced;
    s.keys = (int)s_keys.size();
    s.groups = GR_COUNT;
    s.tailGroup = s_sched.tailGroup;
    s.nextTailMinSec = (float)fmax(0.0, s_sched.p.gapMinS - (s_sched.T - s_sched.lastEnd));
    s.engage = Smooth(s_x);
    return s;
}

const char* ClocksGroupName(int g) { return (g >= 0 && g < GR_COUNT) ? kGroupName[g] : "?"; }

// ---- --clocks-dryrun HOURS --------------------------------------------------------
void ClocksDryRun(double hours) {
    if (!(hours > 0.0)) hours = 24.0;
    unsigned seed = s_seed;
    if (!seed) seed = (unsigned)(GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 16));
    Sched S;
    S.p = s_p;
    S.verbose = true;
    void (*saved)(const char*) = s_logger;
    s_logger = nullptr;                      // per-draw lines to this process's stdout only
    S.Init(seed);
    printf("[clocks-dryrun] %.0f h at 10 Hz, seed=%u, sigma=%.2f tail_at=%.2f tail_len=%.0f s gap=%.0f..%.0f min "
           "draw=%.0f..%.0f s fade=%.0f s; groups active: all %d (as on an oil stage)\n",
           hours, seed, S.p.sigma, S.p.tailAt, S.p.tailLenS, S.p.gapMinS / 60.0, S.p.gapMaxS / 60.0,
           S.p.drawMinS, S.p.drawMaxS, S.p.fadeS, (int)GR_COUNT);
    const double dt = 0.1;
    const long long steps = (long long)llround(hours * 3600.0 / dt);
    const int NB = 100;                       // histogram of F, bins of 0.05 over 0..5
    std::vector<long long> hist(GR_COUNT * NB, 0);
    double sumF[GR_COUNT] = {}, sumG[GR_COUNT] = {}, maxBody[GR_COUNT] = {}, maxTail[GR_COUNT] = {};
    long long above[GR_COUNT] = {};
    float prevF[GR_COUNT], prevG[GR_COUNT];
    int prevPh[GR_COUNT];
    for (int g = 0; g < GR_COUNT; g++) { prevF[g] = S.g[g].F; prevG[g] = S.g[g].G; prevPh[g] = S.g[g].phase; }
    long long tailSteps = 0;
    for (long long i = 0; i < steps; i++) {
        S.Step(dt);
        if (S.tailGroup >= 0) tailSteps++;
        for (int g = 0; g < GR_COUNT; g++) {
            const GroupState& s = S.g[g];
            sumF[g] += s.F;
            sumG[g] += s.G;
            if (s.F > S.p.tailAt + 1e-4f) above[g]++;
            int b = (int)(s.F / 0.05f);
            if (b >= NB) b = NB - 1;
            hist[g * NB + b]++;
            // slope: a step that stayed in the BODY both ends counts for the body bound
            const double sl = fmax(fabs(s.F - prevF[g]), fabs(s.G - prevG[g])) / dt;
            const bool body = s.phase == PH_BODY && prevPh[g] == PH_BODY;
            if (body) { if (sl > maxBody[g]) maxBody[g] = sl; }
            else if (sl > maxTail[g]) maxTail[g] = sl;
            prevF[g] = s.F; prevG[g] = s.G; prevPh[g] = s.phase;
        }
    }
    const double H = hours;
    printf("[clocks-dryrun] ---- summary ----\n");
    bool pass = true;
    double worstBody = 0.0;
    for (int g = 0; g < GR_COUNT; g++) {
        const double mF = sumF[g] / (double)steps, mG = sumG[g] / (double)steps;
        int mb = 0;
        for (int b = 1; b < NB; b++) if (hist[g * NB + b] > hist[g * NB + mb]) mb = b;
        const double mode = (mb + 0.5) * 0.05;
        const bool okMean = fabs(mF - 1.0) <= 0.03 && fabs(mG - 1.0) <= 0.03;
        const bool okMode = mode < 1.0;
        if (!okMean || !okMode) pass = false;
        if (maxBody[g] > worstBody) worstBody = maxBody[g];
        printf("[clocks-dryrun] %-9s mean F %.4f  mean F' %.4f %s  mode bin %.3f %s  time above tail_at %.2f%%  "
               "draws %ld  tails %ld  max|dF/dt| body %.5f/s  tail %.4f/s\n",
               kGroupName[g], mF, mG, okMean ? "PASS" : "FAIL", mode, okMode ? "PASS" : "FAIL",
               100.0 * above[g] / (double)steps, S.g[g].draws, S.g[g].tails, maxBody[g], maxTail[g]);
    }
    const double rate = S.tails / H;
    const double gapMin = S.minGap < 1e29 ? S.minGap / 60.0 : 0.0;
    const double gapMean = S.tails > 1 ? S.sumGap / 60.0 / (double)(S.tails - 1) : 0.0;
    const bool okRate = rate >= 1.0 && rate <= 1.6;
    const bool okGap = gapMin >= S.p.gapMinS / 60.0 - 1e-6;
    const bool okRep = S.repeats == 0;
    const bool okSlope = worstBody <= 0.02;
    printf("[clocks-dryrun] tails %ld in %.0f h = %.3f/h %s (natural %ld, forced %ld; a draw above tail_at refused "
           "for being the last tail's group %ld times)\n", S.tails, H, rate, okRate ? "PASS (1.0..1.6)" : "OUTSIDE 1.0..1.6",
           S.natural, S.forced, S.refused);
    printf("[clocks-dryrun] gap end->start: min %.1f min %s  mean %.1f  max %.1f min; same group twice in a row: %ld %s; "
           "a tail running %.2f%% of the time\n", gapMin, okGap ? "PASS" : "FAIL", gapMean, S.maxGap / 60.0,
           S.repeats, okRep ? "PASS" : "FAIL", 100.0 * tailSteps / (double)steps);
    printf("[clocks-dryrun] body slope max %.5f/s %s (bound 0.02/s; tail/hold glides are bounded by fade_min_s "
           "instead); shortest glide %.1f s (%s)\n", worstBody, okSlope ? "PASS" : "FAIL",
           S.minGlide < 1e29 ? S.minGlide : 0.0, S.minGlideKind);
    printf("[clocks-dryrun] RESULT %s\n", (pass && okRate && okGap && okRep && okSlope) ? "PASS" : "SEE ABOVE");
    s_logger = saved;
}
