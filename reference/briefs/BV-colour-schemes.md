# BV COLOUR SCHEMES — base hue + wheel scheme (brief, 2026-09-25)

Source: the creative chat's shortlist (handoff\review\colour-mocks\combos-home.png, combos-mixups.png,
mock.py; numpy mocks, NOT app renders), built against the user's four proven photos
(handoff\review\proven\). User: proven = 1 colour or 2 (primary + secondary); 3 or 4 colours = a rare
mix-up. Weights 7 : 2 : 1. Read EXECUTOR-CARD.md + AGENTS.md first. Depends on BU (split tone) merged.

## Slot mapping (colours ride on the existing rotation; the whole palette rotates together)
- Colour 1 = film hue (base).
- Colour 2 = film_hue2 (signed offset from colour 1) + film_hue2_amt. Home pairs = big FLAT patches
  like the photos: amt ~0.8-1, scale ~0.3-0.5, narrow seam.
- Colour 3 = NEW film_hue3 (offset) + film_hue3_amt (+ scale/seed if the hue2 field needs a second
  independent field; say how: a second patch field or a split of the hue2 field by threshold).
  3- and 4-colour tiers only, smaller than colour 2 (≤ ~15% of the frame).
- Colour 4 = the BU split-tone shadow tint (shadow_tone hue), darks only; 4-colour tier, plus a
  subtle version in home "Lightroom".
- Droplet dye and mass dye OFF in every tier (dye_lum 0, dye_droplet_lum -1): oil and droplets are
  always black; rims pick up the patch hue through boundary reflections.

## Sign = seam path. Degrees on the standard wheel (0 red, 60 yellow, 120 green, 180 cyan, 240 blue,
300 magenta), + = increasing hue; the band between the two colours shows the hues the offset passes
through and is a big part of why the photos work. If the shader's sign is the other way, FLIP it: the
named seam colours are the spec.

## The schemes (a preset each: reference\presets\Scheme - <name>.ini, partial overlays on monotone-post-0924)
HOME, weight 7: 1 Return to Form: film 325 only (proven-1). 2 Lightroom: film 325 + teal shadow tint
185 subtle. 3 Magenta/Mint: 325, hue2 +180, warm seam red-orange-yellow (proven-2). 4 Magenta/Cyan:
325, hue2 -140, cool seam violet-blue (proven-3). 5 Blue/Coral: 215, hue2 +160, seam violet-magenta
(proven-4). 6 Red/Sky: 355, hue2 -140. 7 Violet/Amber: 275, hue2 +125. 8 Teal/Orange: 190, hue2 -160.
3-COLOUR, weight 2 (preference order): T1 Neon Demon 325, hue2 -140, hue3 +30 (no yellow/orange
anywhere; best). T5 Euphoria 275, +125, -45. T2 Warm Arc 10, +30, -40. T3 Triad 325, -120, +120.
T4 Split 215, +150, -150.
4-COLOUR, weight 1: Q2 Synthwave 325, hue2 -145, hue3 +55, shadow 275 (best). Q3 Microscope 230,
-105, +140, shadow 275. Q1 Square+ 325, +180, +90, shadow 235.

## Rules (each becomes a key or a documented CPU rule; say which)
1. Oil and droplets black in every tier.
2. EQUAL LOAD: dim yellow/green/cyan FILMS so full-frame brightness ≈ today's magenta (ABL flat, no
   blinding yellow) → a per-hue luminance weighting of film_level (CPU-side curve, key film_equal_load
   0..1, default 0 = bit-identical).
3. Gold/amber/yellow PATCHES need their own brightness lift or they go olive/mustard (mock T3/T4/Q1)
   → key film_hue2_lift / film_hue3_lift, default 0.
4. Weighted hue drift: the rotation lingers at proven anchors (magenta ~325 most, then blue 215,
   red 355, violet 275) and glides faster through lime/mustard/olive → the hue rotation phase becomes
   a warped clock (CPU-side, derived phase; key hue_anchor_weight 0..1, default 0 = linear = identical).
5. Proportions: home pair film ≥ ~50% of the frame, colour 2 up to ~40%; third ≤ ~15%; fourth darks
   only.
6. Scheme changes FADE the extra colours out, swap the offsets, fade back in; never snap (director
   transition; hue2/hue3 amt ramps).
7. Shadow tint stays cool/violet, never yellow/olive.

## Proof block (EXECUTOR-CARD §6): key-effect MAD for film_hue3/_amt/_lift, film_equal_load,
hue_anchor_weight (with --shot-series 12:1.7 for the time keys); ALL new keys default to bit-identical
(parity md5 + preset-identity fast MATCH + dxbc-cmp on the fluid PSO); one sheet per new key; one
contact sheet of ALL 16 schemes rendered from the app (1280x720, --shot-delay 40) side by side with
the mock → handoff\review\schemes\ for the creative chat; frame mean luminance per scheme (rule 2)
within ±15% of Return to Form; sign check: a crop of the Magenta/Mint seam showing red-orange-yellow.
Slots: laP38+ (laP35.w free; BU took laP36/37). Post pass rg1.y spares are AP's: do not take.

## AUDITOR PRE-FLIGHT (2026-09-25, main ae91f60) — binding
1. Sign OK, no flip: AcidHueShift does hsv.x = frac(hsv.x + deg/360) (shaders.h:687-689), standard
   wheel; the seam runs film + t·hue2 for t 0→1 (k2 = smoothstep(0.56,0.68,mixV)); 325+180 gives
   red-orange-yellow, -140 gives violet-blue, as specced. The ±10° wobble moves both ends slightly.
2. film_hue3 / film_hue3_amt ALREADY EXIST (laP30.w/.z); k3 = 1 - smoothstep(0.18,0.46,mixV) (:1805)
   takes the LOW end of the same field = the threshold split; keeps hue2 presets bit-identical, never
   overlaps hue2, free. As shipped hue3's area ≈ hue2's, not ≤15%: add film_hue3_share moving the
   0.18/0.46 thresholds down (default = today's thresholds). Also fix AJ seam reach: it marches only to
   the hue2 seam (MIXSEAM 0.62) and k3s is 0 there, so rims next to a hue3 patch rotate away from
   hue3 → march to both seams (~0.32 and 0.62) or drop the hue3 term (small, this executor).
3. Equal load and lift are per-pixel in the SHADER (a CPU scalar on film_level cannot correct patches,
   which are rotated per pixel with V held so Y changes with hue). Equal load dims only:
   oilC *= lerp(1, min(1, Yt/Y), equal_load·(1-k2-k3)), Yt = the magenta target computed on the CPU.
   Lift applies to the patch share only: k2·lift2, k3·lift3, weighted toward the 40-70° band, raising V
   and a little S. Split base vs patches or dimming and lifting fight over a yellow patch. All default 0
   behind a [branch] → bit-identical. Keys: film_equal_load, film_hue2_lift, film_hue3_lift.
4. Anchor drift: CPU, acid only. fluid.cpp:4855 deg = 360·u → deg = W(u), W = inverse cumulative of
   density 1 + k·Σ aᵢ·gauss(h - anchorᵢ) over the ABSOLUTE palette hue; 256-entry table rebuilt only
   when a key changes, binary-search inversion per frame; W monotone, W(0)=0, W(1)=360 → seamless
   wrap. At hue_anchor_weight 0 keep the old expression EXACTLY (identity by construction). The fluid
   look's m_hueAngle / color_cycle_period and hue_sweep_period are separate paths: untouched.
5. Slots: film_hue3_share, film_equal_load, film_hue2_lift, film_hue3_lift → laP35.yzw + laP38.x (or
   all laP38); hue_anchor_weight CPU-only. Never laP36/37 (BU). Fluid PSO byte-identical (acid code
   inside #ifdef LIQUID_ACID); prove with dxbc-cmp.
6. ABSOLUTE FILM HUES FIGHT THE ROTATION: monotone-post-0924 has hue_rotate_period 3600 and its
   oil_color_1 is orange (~22°); "film 325" is true at one phase only. DECISION: schemes are OFFSET
   PATTERNS riding the anchor-drift rotation (recommended; ~9 patterns: the offsets + which anchor
   they linger at), not fixed film hues. hue_sweep_period must be 0 for schemes.
7. Rule 7 conflicts with BU: BU's split tone = complement of the main hue (325 → 145 mint, 215 → 35
   orange). BU gets a shadow_tone_hue override (-1 = complement, else a fixed hue such as 275/235/185).
   Sent to the BU executor.
8. Start NOW from main; only "Lightroom" and the 4-colour tier need the split tone → render those
   after BU merges.
Opinion (relayed verbatim to the user): 7:2:1 is right; build the 16 as ~9 offset patterns + anchors.

## USER VERDICTS (via the creative chat, 2026-09-25 ~02:30; ticked on the phone) — FINAL for the tiers
3-COLOUR (weight 2), all kept: T1 Neon Demon 325/-140/+30; T2 Warm Arc 10/+30/-40; T5 Euphoria
275/+125/-45; T3a Bright Triad 325/-120 azure/+120 lime — the yellow-green member stays BRIGHT (not
equal-loaded); T4b Bright Split 215/+150 red/-150 yellow — the yellow member stays BRIGHT; T3b
Lightroom Triad = magenta film + teal + gold built with LIGHTROOM'S EFFECTS, not three patch colours
(user: "copy Lightroom's effects, not just the resultant colors"): Lightroom split toning over the
magenta film — Shadows hue ~175-185 teal (user's edit 171 / sat 100) + Highlights hue ~45 gold + a
Balance control; the colour follows the frame's own light (lit film warms to gold, darks around the
oil go teal), not flat zones. bu-lr-3/4 (graduated temp / desaturate) are the same idea: effects, not
recolours.
4-COLOUR (weight 1), all ticked: Q1 Square+, Q2 Synthwave, Q3 Microscope, offsets as listed.
RULE CHANGE: yellow/gold/lime MEMBERS of a combo stay bright (dimming to equal load turns them olive);
FILMS still follow equal load; keep the bright yellow member as the SMALLER colour (third, ≤ ~15%)
because a large bright yellow patch pushes ABL. → in the scheme presets: film_equal_load applies to
the film, film_hue2_lift/film_hue3_lift set > 0 on the yellow/gold/lime members.
BU FOLLOW-UP (BU-b, after BU merges, own pre-flight): highlights tint + balance for the split tone
(highlight_tone_hue, highlight_tone_amt, tone_balance; Lightroom semantics), so T3b and "Lightroom"
can be built as a grade. Mock next: handoff\review\colour-mocks\combos-question-fix.png.

## DECISIONS 3 (creative chat, 2026-09-25 ~08:40; agreed): keep all 14 patterns; Teal/Orange → 3-colour
tier; Synthwave third +55 → +70; Square+ stays; luminance ACCEPTED as is (no brighten follow-up);
home weights RtF ×3, Lightroom ×2, photo pairs ×1.5, Red/Sky + Violet/Amber ×1; BLOCKING GAP: second
colour coverage 10-15% → raise toward the photos' 30-45% for HOME pairs, prove Magenta/Mint vs proven-2
before ship (FINAL-CYCLE.md §C).
