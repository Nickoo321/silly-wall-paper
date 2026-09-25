// acid_slots.h -- THE slot table of the packed Liquid Acid cbuffer.
//
// One line per scalar the acid display pass reads: its NAME, the float4 it
// lives in (laP<vec> in HLSL, AcidParamsGPU::p<vec> in C++) and the component.
// Everything else is derived from this list, so the writer and the readers
// cannot drift apart again (brief BE; the AG dye failure was exactly that):
//
//   C++    enum AcidSlot below: LA_<NAME> = vec * 4 + component. The only way
//          UploadAcidConstants writes a scalar is slot(LA_<NAME>, value).
//   HLSL   kAcidSlotMacros (fluid.cpp) passes "#define LA_<NAME> laP<vec>.<c>"
//          for every row to D3DCompile when the LIQUID_ACID PSO is built, so
//          the acid literals in shaders.h read LA_<NAME> and never laP<n>.x.
//   Docs   the cbuffer comment table in shaders.h (between the GENERATED
//          markers) is written from this list by tools\slot-check.ps1 -Fix.
//
// tools\slot-check.ps1 (no GPU, <1 s) cross-checks this table against the
// upload and every acid literal: a slot written but never read, read but never
// written, two names on one component, a stale comment table, a C++/HLSL
// cbuffer member-order mismatch or a literal over 16000 bytes all fail it.
// Run it before merging anything that touches the acid cbuffer.
//
// To ADD a key: take a free component (the checker lists them), add a row
// here, write it with slot(LA_<NAME>, ...) in UploadAcidConstants, read
// LA_<NAME> in the shader, then run tools\slot-check.ps1 -Fix. A new float4
// goes in BOTH AcidParamsGPU (fluid.cpp) and cbuffer AcidCB (shaders.h), in
// the same position, before mix/laMix; the checker compares the two orders.
//
// FULL as of brief BK (2026-09-23): laP11.w (packed droplet dye) and laP16.w /
// laP17.w (dye_smoke / film_level) took the last three free components. The
// NEXT key needs a new float4 laP33 (bump kAcidSlotVecs, add p33 to
// AcidParamsGPU and laP33 to cbuffer AcidCB in the same position), or packs
// into an existing scalar the way DYE_DROP_RGB does.
// Brief BL added laP33 = {oil_fluor, oil_fluor_reach, dye_lamp_follow,
// dark_sat (brief BP)}: full again; the next key needs laP34.
// Brief AG-b added laP34 = {dye_lum_vary, dye_hue_vary, dye_thick_hue,
// rise_speed for the identity fallback's drift}: full; the next key needs laP35.
// Brief BR added laP35 = {dye_core, -, -, -}; .y / .z were reserved for BB / BH
// (lamp temperature, breathing), .w for BQ's film_schlieren (not landed yet).
// Brief BU added laP36 = {grey_k, lamp_grey_size, lamp_grey_cool, grey_cx} and
// laP37 = {split-tone add rgb, grey_cy}: full; the next key needs laP38.
// Brief BV took laP35.yz (film_hue3_share, film_equal_load); laP35.w and laP38
// are free (laP38 is declared empty so BU-b's planned laP39 can follow).
//
// The fourth column is documentation only: the ini key ([liquid_acid] unless
// a section is named) or where a computed value comes from. Hardcoded values
// are marked as such -- there is no key for them.
#pragma once

#define ACID_SLOTS(X) \
    X(BLOB_COUNT,           0, x, "min(blob count, kAcidMaxBlobs)") \
    X(THRESHOLD,            0, y, "threshold (the isoline cutoff)") \
    X(SUPPORT_SCALE,        0, z, "support_scale") \
    X(AA_SCALE,             0, w, "aa_scale") \
    X(RIM_WIDTH,            1, x, "rim_width") \
    X(RIM_INSET,            1, y, "rim_inset") \
    X(RIM_DARK,             1, z, "rim_dark") \
    X(REFRACTION,           1, w, "refraction") \
    X(MENISCUS,             2, x, "meniscus") \
    X(MENISCUS_W,           2, y, "meniscus_width") \
    X(TRANSLUCENCY,         2, z, "translucency") \
    X(OIL_TEXTURE,          2, w, "oil_texture") \
    X(INK_LEVELS,           3, x, "ink_levels") \
    X(INK_SOFT,             3, y, "ink_soft") \
    X(INK_MIX,              3, z, "ink_mix") \
    X(INK_HUE_VARY,         3, w, "ink_hue_vary") \
    X(INK_GAIN,             4, x, "ink_gain") \
    X(INK_BIAS,             4, y, "ink_bias") \
    X(SEAM_STR,             4, z, "seam_strength") \
    X(SEAM_SCALE,           4, w, "seam_scale") \
    X(SEAM_LO,              5, x, "seam_lo") \
    X(SEAM_HI,              5, y, "seam_hi") \
    X(GRAIN,                5, z, "grain (0 when [post] film_grain runs)") \
    X(GRAIN_SCALE,          5, w, "grain_scale") \
    X(SPECKLE,              6, x, "speckle") \
    X(SPECKLE_SCALE,        6, y, "speckle_scale") \
    X(TIME,                 6, z, "m_time, seconds") \
    X(ASPECT,               6, w, "width / height") \
    X(OIL_HDR,              7, x, "oil_hdr") \
    X(RIM_HDR,              7, y, "rim_hdr") \
    X(MENISCUS_OFF,         7, z, "meniscus_offset") \
    X(INK_SHADING,          7, w, "ink_shading") \
    X(SWARM_HOLES,          8, x, "swarm_holes (0 while droplets > 0)") \
    X(SWARM_DROPS,          8, y, "swarm_drops (0 while droplets > 0)") \
    X(SWARM_DENSITY,        8, z, "swarm_density") \
    X(SWARM_RIM_DARK,       8, w, "swarm_rim_dark") \
    X(SWARM_SCALE_HOLES,    9, x, "swarm_scale_holes, cells per p-unit") \
    X(SWARM_SCALE_DROPS,    9, y, "swarm_scale_drops, cells per p-unit") \
    X(SWARM_R_MIN,          9, z, "swarm_r_min, cell units") \
    X(SWARM_R_MAX,          9, w, "swarm_r_max, cell units") \
    X(SWARM_CLUMP,         10, x, "swarm_clump") \
    X(SWARM_DARK,          10, y, "swarm_dark") \
    X(INK_WATER,           10, z, "ink_water: 1 = ink_mode water") \
    X(TOE_TINT,            10, w, "toe_tint") \
    X(INK_LOCK,            11, x, "ink_complement_lock, 0/1") \
    X(INK_LOCK_SPAN,       11, y, "ink_complement_span, degrees") \
    X(INK_TARGET_HUE,      11, z, "oil mean hue + 180, degrees (computed)") \
    X(DYE_DROP_RGB,        11, w, "droplet dye hue8+256sat8+65536lum8, -1 = split off; dye_droplet_* x dye_droplets (brief BK, packed HSV since BP)") \
    X(RIM_VARY,            12, x, "rim_vary") \
    X(RIM_INK_FOLLOW,      12, y, "rim_ink_follow") \
    X(RIM_ORDER,           12, z, "rim_order, 0/1") \
    X(GRAIN_SHADOW_W,      12, w, "grain_shadow_weight") \
    X(OIL_THIN_EDGE,       13, x, "oil_thin_edge") \
    X(OIL_EDGE_FRAC,       13, y, "oil_edge_frac") \
    X(OIL_SPECULAR,        13, z, "oil_specular") \
    X(OIL_IRID,            13, w, "oil_iridescence") \
    X(SWARM_LENS,          14, x, "swarm_lens") \
    X(MEN_FROM_INK,        14, y, "meniscus_from_ink") \
    X(OIL_GLOW,            14, z, "oil_glow") \
    X(REFR_WIDTH,          14, w, "refraction_width") \
    X(OIL_TRANSP,          15, x, "oil_transparency") \
    X(OIL_ABSORB,          15, y, "oil_absorb") \
    X(OIL_FILM_BUMP,       15, z, "oil_film_bump") \
    X(OIL_REFR_BODY,       15, w, "oil_refract_body") \
    X(OIL_INK_BLUR,        16, x, "oil_ink_blur") \
    X(DYE_DEPTH_W,         16, y, "dye_depth_w, clamped 0.01..4") \
    X(MEN_FILM_MIX,        16, z, "meniscus_film_mix") \
    X(DYE_SMOKE,           16, w, "dye_smoke, 0..1 (brief BK)") \
    X(RISE_BOTTOM_LIGHT,   17, x, "rise_bottom_light") \
    X(POST_CHROMA,         17, y, "post_chroma") \
    X(POST_LIFT,           17, z, "post_lift") \
    X(FILM_LEVEL,          17, w, "film_level, 0..1 (brief BK)") \
    X(DROPS_ON,            18, x, "1 while droplets > 0") \
    X(DROP_GRID_W,         18, y, "kDropGridW") \
    X(DROP_GRID_H,         18, z, "kDropGridH") \
    X(OIL_EDGE_MODE,       18, w, "oil_edge_mode == 1") \
    X(DROP_SUPPORT,        19, x, "droplet_support") \
    X(DROP_WEIGHT,         19, y, "droplet_weight (the hole punch)") \
    X(DROP_OIL_W,          19, z, "droplet_oil_weight") \
    X(DROP_RING_BASE,      19, w, "kAcidMaxDrops: first RING SHAPE record") \
    X(DROP_RING_WIDTH,     20, x, "droplet_ring_width, fraction of support") \
    X(DROP_RING_LIFT,      20, y, "droplet_ring_lift") \
    X(OIL_EDGE_CURVE,      20, z, "oil_edge_curve") \
    X(DIFFR_SCALE,         20, w, "2.5 * diffraction_px / 1440 (uv)") \
    X(HALO,                21, x, "[post] halo") \
    X(HALO_W,              21, y, "[post] halo_px / 1440 (uv)") \
    X(SOFTNESS,            21, z, "[post] softness / 1440 (uv)") \
    X(BAND_MIN,            21, w, "[post] band_min") \
    X(PENUMBRA,            22, x, "oil_penumbra") \
    X(PENUMBRA_W,          22, y, "oil_penumbra_px / 1440 (uv)") \
    X(PENUMBRA_HUE,        22, z, "oil_penumbra_hue, degrees") \
    X(PENUMBRA_DARK,       22, w, "oil_penumbra_dark") \
    X(CELL_INK,            23, x, "cellulose * cellulose_ink") \
    X(CELL_OIL,            23, y, "cellulose * cellulose_oil") \
    X(CELL_SCALE,          23, z, "cellulose_scale / 1440 (uv)") \
    X(CELL_DRIFT,          23, w, "rise_speed * cellulose_drift (uv/s)") \
    X(CAM_AXIS_X,          24, x, "rig optical axis x (uv)") \
    X(CAM_AXIS_Y,          24, y, "rig optical axis y (uv)") \
    X(FOCUS_DEPTH,         24, z, "rig focus depth (0.5 with dof off)") \
    X(DOF_MAX_PX,          24, w, "[post] dof_max_px (0 = dof off)") \
    X(FIELD_CURVE,         25, x, "[post] camera_field_curve") \
    X(TILT_AMT,            25, y, "rig tilt amount") \
    X(TILT_COS,            25, z, "cos(rig tilt angle)") \
    X(TILT_SIN,            25, w, "sin(rig tilt angle)") \
    X(FOCUS_BAND,          26, x, "[post] focus_band_px / 1440 (uv)") \
    X(COC_SPAN_INV,        26, y, "1 / 0.12, HARDCODED, no key") \
    X(FOV_K,               26, z, "tan(camera_fov / 2) / 0.5") \
    X(DIFFRACTION,         26, w, "diffraction") \
    X(LENS,                27, x, "droplet_lens") \
    X(LENS_CENTRE,         27, y, "droplet_lens_centre") \
    X(LENS_BAND,           27, z, "droplet_lens_band / 1440 (uv)") \
    X(DROP_SPEC,           27, w, "droplet_spec") \
    X(LAMP_X,              28, x, "rig lamp x (uv)") \
    X(LAMP_Y,              28, y, "rig lamp y (uv)") \
    X(DYE_DEPTH,           28, z, "dye_depth") \
    X(DYE_TILT,            28, w, "dye_depth_tilt") \
    X(MASS_RIM,            29, x, "mass_rim") \
    X(MASS_RIM_W,          29, y, "3 / 1440 (uv), HARDCODED, no key") \
    X(DYE_AMT,             29, z, "dye_lum (0 when dye_sat or dye_lum is 0)") \
    X(DYE_HUE,             29, w, "dye_hue (+ film hue if dye_hue_follow), 0..1") \
    X(HUE2_AMT,            30, x, "film_hue2_amt") \
    X(HUE2_DEG,            30, y, "film_hue2 + wobble, degrees") \
    X(HUE3_AMT,            30, z, "film_hue3_amt") \
    X(HUE3_DEG,            30, w, "film_hue3, degrees") \
    X(CRUST_HUE_MIX,       31, x, "crust_hue_mix") \
    X(REFLECT_R,           31, y, "boundary_reflect_r, screen heights") \
    X(REFLECT_AMT,         31, z, "boundary_reflect_amt") \
    X(DYE_SAT,             31, w, "dye_sat") \
    X(SHADOW_AMT,          32, x, "shadow_amt") \
    X(SHADOW_LEN,          32, y, "shadow_len, screen heights") \
    X(SHADOW_SOFT,         32, z, "shadow_soft") \
    X(LIGHT_Z,             32, w, "[post] light_z") \
    X(OIL_FLUOR,           33, x, "oil_fluor, 0..1 (brief BL)") \
    X(OIL_FLUOR_REACH,     33, y, "oil_fluor_reach, screen heights, >= 0.05 (brief BL)") \
    X(DYE_LAMP_FOLLOW,     33, z, "dye_lamp_follow, 0..1 (brief BL)") \
    X(DARK_SAT,            33, w, "dark_sat, 0..1 (brief BP)") \
    X(DYE_LUM_VARY,        34, x, "dye_lum_vary, relative +-, 0..0.5 (brief AG-b)") \
    X(DYE_HUE_VARY,        34, y, "dye_hue_vary, degrees, 0..90 (brief AG-b)") \
    X(DYE_THICK_HUE,       34, z, "dye_thick_hue, degrees, -90..90 (brief AG-b)") \
    X(DYE_ID_RISE,         34, w, "rise_speed, uv/s: drift of the dye identity fallback (brief AG-b)") \
    X(DYE_CORE,            35, x, "dye_core, core/rim brightness ratio 0.42..1, 0.42 = today (brief BR)") \
    X(GREY_K,              36, x, "0.6 * lamp_grey, 0 = off (brief BU)") \
    X(GREY_SIZE,           36, y, "lamp_grey_size, screen heights 0.25..0.6 (brief BU)") \
    X(GREY_COOL,           36, z, "lamp_grey_cool, 0..1 (brief BU)") \
    X(GREY_CX,             36, w, "grey region centre x, p-units (computed: far corner + 90 s slide, brief BU)") \
    X(TONE_R,              37, x, "split tone add r = HSV(h, shadow_tone_sat, 1) * lift * gate * shadow_tone, h solved to the film OKLab hue + 180 (computed, brief BU)") \
    X(TONE_G,              37, y, "split tone add g (computed, brief BU)") \
    X(TONE_B,              37, z, "split tone add b (computed, brief BU)") \
    X(GREY_CY,             37, w, "grey region centre y, p-units (computed, brief BU)") \
    X(HUE3_SHARE,          35, y, "film_hue3_share, 0..1, 1 = today's hue3 thresholds (brief BV)") \
    X(EQUAL_LOAD,          35, z, "film_equal_load, 0..1, base film dimmed to the magenta luminance (brief BV)")

// Component letter -> index, for the enum below.
#define ACID_COMP_x 0
#define ACID_COMP_y 1
#define ACID_COMP_z 2
#define ACID_COMP_w 3

// C++ side: LA_<NAME> = vec * 4 + component (vec = slot >> 2, comp = slot & 3).
enum AcidSlot : int {
#define ACID_SLOT_ENUM(name, vec, comp, doc) LA_##name = (vec) * 4 + ACID_COMP_##comp,
    ACID_SLOTS(ACID_SLOT_ENUM)
#undef ACID_SLOT_ENUM
};

// Number of laP<n> float4s (laP0 .. laP38).
static const int kAcidSlotVecs = 39;
