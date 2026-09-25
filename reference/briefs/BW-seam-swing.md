# BW — seam width + coverage swing (follow-up brief, 2026-09-25; run AFTER the weekly reset)

From creative decision request 4 (agreed). Depends on main e1b1538+ (film_hue2_cover, coverage logger).
Read EXECUTOR-CARD.md (§7), AGENTS.md, BV-colour-schemes.md, FINAL-CYCLE.md §C + pre-flight item 4.
1. SEAM WIDTH key `film_hue2_seam` (or reuse an existing softness if one drives the k2 smoothstep
   0.56-0.68 width; the thresholds are hard-coded today): narrow the band between colour 1 and colour 2
   to ~1/3 of today's so it reads as ONE thin yellow-orange line (proven-2), not red + pure-green
   stripes. Default = today's width (bit-identical). Applies to k3's band the same way if cheap.
2. SWING: the hue2 field swings 0-62% over a stage on its own (advection). Goal: the film stays the
   majority on AVERAGE with the second colour as big flat photo-sized fields (proven-2/4), never a
   spotty field of small islands (the user rejected "too bubbly"). Try in this order and measure with
   the CPU coverage logger (--cover-sweep, no renders): (a) moderately smaller patches
   (film_hue2_scale) while shapes stay big; (b) if that reads spotty, CAP the peak instead: a soft
   saturation of the second colour's share around 55-60% of the frame (CPU-side, e.g. bias reduced
   as the measured share rises), default off = identical. Home pairs ship at film_hue2_cover 0.08
   (already set) unless the cap allows 0.12 safely.
3. Proof (§6/§7): key-effect MAD per new key; defaults bit-identical (parity md5, preset-identity
   -Tier fast vs the current baseline, dxbc-cmp); a 0-180 s coverage curve (CPU log) before/after for
   Magenta/Mint showing the average and peak; one sheet: Magenta/Mint at 60/90/150 s after, beside
   proven-2, with the seam crop; a Blue/Coral frame to confirm the seam has no red/violet stripes.
   Copies to handoff\review\schemes\ for the creative chat.
NOTE for a later STRUCTURE pass (not this brief): the proven photos have big black oil masses at
20-30% of the frame; the renders at 150 s have <10% in the corners. The black contrast is part of the
look (creative chat, request 4).
4. (from the director's report) EQUAL-LOAD THE PATCHES: film_equal_load today dims only the base film
   (k2/k3 excluded); the tamed burst and the anchor drift both swing mean_lum by 30-40% because the
   second-colour patches are never dimmed. Add a per-pixel option (key film_equal_load_patches 0..1,
   default 0 = identical) that applies the equal-load target to the patch share too (mint/cyan at the
   magenta target; yellow/gold/lime members keep the user's "stay bright" rule via a lower value on
   those presets). Then re-measure the burst (<15% swing) and set burst_weight back to 1 if it passes.
5. START PHASE: after an app start every Scheme shows an ORANGE film because the palette clock starts
   at oil_color_1's hue (~22°): start the ANIM_PALETTE accumulator at the phase whose absolute hue is
   the nearest anchor (325 for the shipped presets) — identical when hue_anchor_weight is 0 (say how).
6. Rote: sync tools\bv-schemes.py with the presets (home pairs 0.08, Synthwave +70), manifest rows for
   the 16 Scheme presets + acid-rise-12-tone-half.ini + cycle-final.ini.

## AUDITOR PRE-FLIGHT (2026-09-25, main 970f26f) — binding
1. SEAM: k2 = smoothstep(0.56,0.68,mixV) (shaders.h:1819), centre 0.62, half-width 0.06; k3 uses
   0.18-h3d / 0.46-h3d (:1837). Runtime 0.62-0.06·s is NOT bit-identical to the literal 0.56f → key
   film_hue2_seam (width factor, default 1) in laP35.w with `[branch] if (LA_SEAM_W != 1.0)` narrowed
   lo/hi, else today's literals; same for k3's band around its centre. Only the acid DXBC changes.
   The AJ reflection must use the same bands: k3s (:1909-1911) reads the narrowed h3lo/h3hi.
2. SWING: STATIC, no threshold feedback (few big patches ⇒ large random swing; a bias feedback lags
   1-3 min vs a 180 s dwell and pumps at high gain; a k2-threshold feedback visibly breathes). Pick
   film_hue2_scale (a notch smaller, shapes stay large) + bias from a multi-seed CPU distribution
   (≥10 seeds × 600 s with --cover-sweep), targeting mean ≈ 40%, P95 ≤ 60%. A slow bias feedback only
   if P95 still fails, documented as a minutes-scale trim.
3. EQUAL-LOAD PATCHES: today wb = EQ·saturate(1-k2-k3) (:1958-1961), target Y = same S/V at hue 325.
   Add wp = EQP·w(k2,k3) behind `[branch] if (EQP > 0)`, old expression otherwise. NEVER weight by raw
   k2 (dims the seam's yellow to olive): weight by the patch CORE, e.g. smoothstep(0.8,1,k2); bright
   yellow members get a low EQP per preset. Then re-measure the burst (<15%) before burst_weight 1.
4. START PHASE: NOT "nearest anchor" (nearest to 22° is red 355). Per-preset key palette_start_hue
   (CPU-only, no slot, default -1 = today): at start and at each oil stage entry set the ANIM_PALETTE
   accumulator (AnimatorTime(0) = m_time - m_animFrozenAccum[0]; AnimatorKick fluid.cpp:2157 already
   moves it) so h0 + W(u*) lands on that hue via the inverse warp table. Blue/Coral 215, Magenta/Mint
   325, etc. -1 skips ⇒ parity and unset presets unchanged; presets that set it change their identity
   rows (document) and their key-effect baselines. monotone-post-0924 stays orange unless its ini sets
   it → USER DECISION (A/B at swap time); do not set it there in this run.
5. PROOF: seam OKLab hue profile along a seam normal at s = 1 / 0.5 / 0.33 (red + green band pixels
   fall to ~1/3) + Blue/Coral no-stripes crop; swing multi-seed distribution (mean, P95, max)
   before/after + the 60/90/150 s sheet beside proven-2; equal-load patches: seam pixels' OKLab C holds
   and hue stays 40-70° (not olive), burst mean_lum swing < 15%; start phase: first-frame hue = start
   hue ±5° per preset; identity: parity md5, preset-identity fast (changed rows listed), dxbc fluid +
   ink IDENTICAL.
Opinion relayed verbatim to the user.
