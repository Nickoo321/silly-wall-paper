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
