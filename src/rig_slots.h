#pragma once
// The post pass's RIG block b3 (cbuffer RigCB in src/shaders/post.hlsl,
// rg0..rg4 = 20 root constants), as ONE table (brief CLOUD-1 "shaders" (b)).
//
//   X(NAME, vec, comp, shift, bits, "doc")
//     vec, comp  the float: rg<vec>.<comp>
//     bits == 0  the whole float carries one value (shift must be 0)
//     bits  > 0  a packed field: an exact integer 0..2^bits-1 at 2^shift.
//                Every field of a float sits below 2^24, which float32 holds
//                losslessly; the shader unpacks with BDU6 / LidU8 / LidU12 /
//                LidU7764, whose field layouts must match these rows.
//
// Writers: RenderPostPass in fluid.cpp, ONLY through RigPut(RG_<NAME>, v)
// (bits 0: assigns v; packed: adds q * 2^shift) and the quantisers
// RigQ / RigQz, which take the field width from this table.
// Checker: tools/rig-check.ps1 (no GPU, no build) fails on any drift between
// this table, the C++ writes and the HLSL reads / unpack helpers.
// Unused bits of a packed float (rg1.y's two low fields) have no row and are
// written 0.
#define RIG_SLOTS(X) \
    /* rg0 -- the rig itself: lamp and optical axis, uv */ \
    X(LAMP_X,             0, x,  0,  0, "lamp x, uv, drifted") \
    X(LAMP_Y,             0, y,  0,  0, "lamp y, uv, drifted") \
    X(AXIS_X,             0, z,  0,  0, "optical axis x, uv") \
    X(AXIS_Y,             0, w,  0,  0, "optical axis y, uv") \
    /* rg1.x -- briefs BO/BN, four 6-bit fields */ \
    X(ARTEFACT_LUM_GATE,  1, x, 18,  6, "[post] artefact_lum_gate (BO)") \
    X(CORNER_WARP,        1, x, 12,  6, "[post] corner_warp (BN)") \
    X(CORNER_WARP_R,      1, x,  6,  6, "[post] corner_warp_r, (v-0.4)/0.5; 0 unless the warp is on (BN)") \
    X(BLOOM_WARMTH,       1, x,  0,  6, "[post] bloom_warmth (BN)") \
    /* rg1.y -- brief BN; fields 3 and 4 spare */ \
    X(GLASS_STREAKS,      1, y, 18,  6, "[post] glass_streaks; 0 unless the lid is on (BN)") \
    X(HALATION_THRESHOLD, 1, y, 12,  6, "[post] halation_threshold (BN)") \
    /* rg1.zw -- brief BM, the lid's scratches, 8-bit fields; 0 unless lid + amount */ \
    X(SCRATCH_AMOUNT,     1, z, 16,  8, "[post] lid_scratch") \
    X(SCRATCH_DENSITY,    1, z,  8,  8, "[post] lid_scratch_density") \
    X(SCRATCH_LEN,        1, z,  0,  8, "[post] lid_scratch_len") \
    X(SCRATCH_CORNER,     1, w, 16,  8, "[post] lid_scratch_corner") \
    X(SCRATCH_SOFT,       1, w,  8,  8, "[post] lid_scratch_soft") \
    X(SCRATCH_TINT,       1, w,  0,  8, "[post] lid_scratch_tint") \
    /* rg2 -- the lens's chromatic split (item Z) + brief BD's pack */ \
    X(ABERRATION,         2, x,  0,  0, "[post] aberration, 0..1") \
    X(ABERRATION_PX,      2, y,  0,  0, "[post] aberration_px, px at this res") \
    X(ABERRATION_FIELD,   2, z,  0,  0, "[post] aberration_field, 0..2") \
    X(GRAIN_CHROMA,       2, w, 18,  6, "[post] film_grain_chroma (BD)") \
    X(GRAIN_DENSITY,      2, w, 12,  6, "[post] film_grain_density (BD)") \
    X(FOG_MASS_GATE,      2, w,  6,  6, "[post] fog_mass_gate (BD)") \
    X(ABERRATION_COC,     2, w,  0,  6, "[post] aberration_coc (BD)") \
    /* rg3 -- motion (item V3) */ \
    X(SHIMMER,            3, x,  0,  0, "[post] shimmer, 0..1") \
    X(SHIMMER_PX,         3, y,  0,  0, "[post] shimmer_px, px at this res") \
    X(SHIFT_X,            3, z,  0,  0, "pixel-shift orbit x, uv") \
    X(SHIFT_Y,            3, w,  0,  0, "pixel-shift orbit y, uv") \
    /* rg4 -- THE LID (task V2); all four exactly 0 while the lid is off */ \
    X(LID_X,              4, x, 12, 12, "lid wander x, -0.5..0.5 uv") \
    X(LID_Y,              4, x,  0, 12, "lid wander y, -0.5..0.5 uv") \
    X(LID_ROT,            4, y, 12, 12, "lid rotation, -4..4") \
    X(LID_REFRACT_PX,     4, y,  0, 12, "[post] lid_refract_px at this res, 0..24") \
    X(LID_GHOST,          4, z, 16,  8, "[post] lid_ghost * lid") \
    X(LID_RINGS,          4, z,  8,  8, "[post] lid_rings * lid") \
    X(LID_SHEEN,          4, z,  0,  8, "[post] lid_sheen * lid") \
    X(LID_GLINT,          4, w, 17,  7, "[post] lid_glint * lid") \
    X(LID_IRIS,           4, w, 10,  7, "[post] lid_iris * lid") \
    X(LID_SHEEN_PX,       4, w,  4,  6, "[post] lid_sheen_px at this res, 8..1400") \
    X(LID_GHOST_SPREAD,   4, w,  0,  4, "[post] lid_ghost_spread, 0..2")

constexpr int kRigVecs = 5;                  // rg0..rg4
constexpr int kRigFloats = kRigVecs * 4;     // root constants at b3

#define RIG_SLOT_ENUM(name, vec, comp, shift, bits, doc) RG_##name,
enum RigSlot { RIG_SLOTS(RIG_SLOT_ENUM) kRigSlotCount };
#undef RIG_SLOT_ENUM

struct RigSlotInfo {
    int   index;   // float index into rig[kRigFloats]
    int   bits;    // 0 = whole float
    float scale;   // 2^shift
    float maxq;    // 2^bits - 1 (the quantiser's top code)
};
constexpr int RigCompIndex(const char* c) { return c[0] == 'x' ? 0 : c[0] == 'y' ? 1 : c[0] == 'z' ? 2 : 3; }
#define RIG_SLOT_INFO(name, vec, comp, shift, bits, doc) \
    { (vec) * 4 + RigCompIndex(#comp), (bits), (float)(1 << (shift)), (float)((1 << (bits)) - 1) },
constexpr RigSlotInfo kRigSlots[kRigSlotCount] = { RIG_SLOTS(RIG_SLOT_INFO) };
#undef RIG_SLOT_INFO
