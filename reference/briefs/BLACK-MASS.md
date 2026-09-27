# Brief BZ — BIG BLACK MASSES + continuously mixed oil layouts

Status: DRAFT 2026-09-27 00:10, for auditor pre-flight, then ONE Opus executor AFTER CLOCKS and
PHOTO merge. Source: handoff\review\creative-brief-black-mass.md (creative chat, user-approved
2026-09-25 as the priority after request 5) and the code study scratchpad\black-mass-keys.md
(copied to reference\briefs\BLACK-MASS-keys.md). Existing keys first; ONE small mechanism key.

## 0. Targets (from the creative brief; presets = the means, the clocks glide between them)

| layout | black % | shape | tier / time share |
|---|---|---|---|
| Few giants | ~28 | 2–3 huge lobed masses, some cut by the frame edge, sparse droplets | proven 40% |
| Lava rise | ~25 | tall rising blobs, bubbles pinch off the tops | proven 30% |
| Today | ~9 | one medium mass + scattered droplets | moderate 12% |
| Starfield | ~12 | dense small droplets, no big mass | moderate 8% |
| Rafts | ~8 | droplets in rings round a small core | wild 10% — NOT REACHABLE, see §4 |

Rules that hold: oil and droplets stay pure black (no dye, no glow bodies; `film_level` low is the
mechanism: it dims only the flat interior, rim/glow stay — shaders.h:2420); droplet fields never
"bubbly"; the second-colour patches still read at photo size next to the black. Masses must move
and evolve; a parked blob is a fail. No sharp layout cuts.

## 1. The mechanism: continuous TURNOVER (the one code change)

Fact (study §1, fluid.cpp:3159–3335): the kind-mix keys (`disc_frac web_frac bubble_frac
hole_weight big_bias disc_min/max web_min/max bubble_*`) are read every frame but only shape NEWLY
CREATED blobs; an existing blob never changes kind or size range, so gliding those keys does
nothing until the population turns over. With `conserve_mass=1`, `AdjustAcidBlobCount()` already
walks gradually: it retires blobs and grows new ones in from under the bottom edge over
`spawn_grow_s`, drawing their kind and size from the CURRENT keys.

New key `[liquid_acid] blob_turnover_s` (default 0 = off = bit-identical; range 0..600, preset
90): every `blob_turnover_s / blob_count` seconds retire ONE blob (the oldest, dissolving over
`dissolve_s` exactly as a count decrease does today) and grow ONE new blob through the same
grow-in path. The population therefore re-draws itself completely every `blob_turnover_s` seconds
from whatever the kind keys say NOW. Nothing else changes: same retire/grow code, same wrap, same
merge. With the kind keys on the clocks (oilsim group), the layout drifts continuously; a layout
change is never a cut. `blob_count` itself may also breathe on the clocks (safe under
conserve_mass=1, study §1), which speeds the mix.

## 2. Layout points and the layout clock

Five overlay presets `reference/presets/Layout - <name>.ini` ([liquid_acid] partial, on the monotone
base, keys per the study §3 as STARTING values — the sheet decides):
- Few giants: blob_count ~20, disc_frac 0.55, web_frac 0.25, bubble_frac 0.05, hole_weight 0.4,
  big_bias 0.4, disc_max 0.50, web_max 0.45, film_level 0.10, rise_speed 0.004, rise_wobble 0.5,
  low droplet_spawn_rate / ring_clump / mass_bias.
- Lava rise: acid-rise-12's rise block (rise_speed 0.0195, wobble 0.5, respawn 1, stretch 0.45,
  bottom_light 0.8, parallax 0.7, conserve_mass 1) + disc_frac/web_frac/disc_max raised until
  ~25%; bloom stays the base's (monotone base = 0; the acid-rise-12 0.30 is that preset's own).
- Today: the monotone base as is (measured, ~9%).
- Starfield: blob_count ~6 bubble-kind, high droplet_spawn_rate, small droplet_r_min/max, droplet_bias
  toward small. Risk (study): kind-1 nucleation is weighted toward existing oil edges, so density
  may undershoot with few blobs — the sheet measures it; if it undershoots, raise blob_count of
  small bubbles rather than adding a mechanism.
- Rafts: see §4 (a best-effort "foam patches" preset ships under that name only if the user says so).

LAYOUT CLOCK (an extension of CLOCKS, in clocks.cpp): a stage may declare POINTS,
`[clocks] point_N=<preset file> point_N_share=<fraction>`; the director keeps a smooth random walk
of weights w_N(t) on the simplex (log-normal draws per point, normalised, glided with the same
segment/fade rules), with the long-run time share of the dominant point = point_N_share
(40/30/12/8 for the four reachable layouts, renormalised; the clocks' body draw is the same
skewed distribution, so a layout is "mostly its point, lighter most of the time, sometimes
exaggerated"). The effective key = sum_N w_N * key_N for every key any point sets; keys no point
sets stay on the ordinary per-key clock. The cut only happens when the ~7-min look cut lands.

## 3. Measurement: black % (tooling, no visual change)

`CoverageDarkPct()` / `m_darkPct` (fluid.cpp:6512–6575, threshold dark_level 0.07 scRGB ~5.6 nits)
is the right metric and is never logged. Add one `[dark] pct=NN.N` line per second to the `--shot`
/ `--console` log (stdout only, like the others) and a 60 s mean in the `[shot] done` line. Tool
`tools/black-share.ps1 -Ini <file> [-Seconds 60]`: runs the shot under the GPU lock and prints the
mean (BelowNormal, --shot-yield 30). Acceptance per the creative brief: each layout within +-5 points
of its target, measured as the 60 s mean at seed 1234, HDR on; HDR off must match within 2 points
(the threshold is in scRGB before tone mapping, so it should).

## 4. Rafts — to the user (no quiet build)

Study §3: nothing attracts droplets to a specific core blob; `droplet_ring_clump` clusters rings
that already sit near each other, wherever that is. "Rings round a small core" needs a new
attraction term (droplet acceleration toward the nearest small blob's rim at a target radius) — a
physics change. Cost: ~1 key + ~40 lines in the droplet step, one sheet. QUESTION for the user
(morning list): build the attractor, or ship the four reachable layouts and drop rafts? Until then
the layout clock ships with four points (shares 40/30/12/8 renormalised to 44/33/13/9).

## 5. Proof (PROOF BLOCK per EXECUTOR-CARD §6, plus §7)

1. blob_turnover_s = 0 and no Layout presets in the list: identity 15/15 unchanged, parity MATCH,
   dxbc IDENTICAL (the shader does not change; turnover is CPU sim).
2. Per layout: 1280x720 60 s render + `[dark]` mean (HDR on and off) -> one contact sheet
   `build2\shots\live\blackmass\layouts-{hdr,sdr}.png` with the four proven photos' row alongside
   (handoff\review\proven\, the oil ones) and each panel labelled target vs measured.
3. Turnover proof: monotone base + `blob_turnover_s 90`, a 6-frame strip at 0/30/60/90/120/150 s
   showing the population re-forming while the kind keys are switched at t=0 from Today to Few
   giants (via `--clocks-force` or a second ini): masses appear, none pops (max per-frame delta of
   the dark % < 1.5 points), black % glides from ~9 to ~28 within ~2 turnover periods.
4. Layout clock dry-run (`--clocks-dryrun 24` with the four points): dominant-point time shares
   within +-5 points of 44/33/13/9; no point's weight jumps faster than the fade rule.
5. A 3-min real-time mix clip (frames every 10 s, HDR on) on the live director between two layouts.
6. FEATURES.md rows (blob_turnover_s, point_N keys), SETTINGS.md regen, manifest rows for the
   Layout presets, cycle list update (the four layouts as oil stages with their tiers).

§7 for the review chat: 1. works (which of 1–5 proven); 2. the visible difference; 3. which layout
missed its target and by how much; 4. the rafts decision; 5. anything that popped.

## 6. Branch and merge

Branch `blackmass` from main after PHOTO merges; worktree C:\Users\abg77\fw-blackmass;
BLACKMASS-RESUME.md per milestone. The layout-clock part touches clocks.cpp (from CLOCKS), so the
executor reads CLOCKS.md and its pre-flight first.

## AUDITOR PRE-FLIGHT (2026-09-27, main c801947) — binding
Read-only, no build/render. Where these items and the brief (or the study) disagree, these items win.
### 0. Which element is black (read first: it inverts §0, §2 and the study §3)
1. On the monotone base the black is NOT the metaball film. `col = lerp(inkC, oilC, alpha)` (shaders.h:2425): alpha =
   the metaball union (field > threshold) = the LIT film (film_level 1, monotone-post-0924.ini:649). The black "masses"
   are the COMPLEMENT (field < threshold), inkC = [ink] paper_color 0 0 0 (trace shaders.h:2007-2019; keys.inc:448 "the
   gaps between oil blobs"), plus kind-0 droplets (water holes in the film). "Oil + droplets black" = dye_lum 0 +
   dye_droplet_lum -1 (Scheme headers). MORE black = LESS film: fewer/smaller positive blobs, higher threshold, lower
   support_scale, more/bigger holes. The brief's recipes (disc_frac/disc_max UP = "giants") raise film cover = LESS black.
2. film_level is no layout key: here (dye_lum 0) 0.10 blacks the film too -> ~90% black; low film + dye_lum > 0.1 = the
   LAPD dyed look (CREATIVE.md §7, rejected). Fable confirms "film lit, gaps black" (role A) before any Layout preset.
### 1. blob_turnover_s
3. AdjustAcidBlobCount (fluid.cpp:3276-3335) runs only under conserve_mass > 0.5 while live != want (1966-1974).
   DECREASE: rTarget 0 on the LEAST VISIBLE blob (off-frame*10 - baseR, 3297-3298), not the oldest; no age exists
   (fluid.h:2128-2165). The retiree shrinks over dissolve_s (4009-4013), stays retiring through respawn (3955) and stays
   IN the vector until baseR < 0.2/(H*support_scale) (4019-4023), ~6-8 tau = 30-40 s. INCREASE: kind, radius and wgt from
   the CURRENT fracs/_min/_max/bias/hole_weight (3310-3318), born at 2%, grown over spawn_grow_s; y under the bottom edge if
   rise+respawn, else a RANDOM mid-frame point (3324-3326). Grow path reusable verbatim; the retire pick is NOT.
4. New code (small, binding): (a) `float born` in AcidBlob (set to m_time on grow). (b) Pick: candidates rTarget > 0 AND
   baseR >= 0.9*rTarget (else the existing score retires the 2%-baseR blob just grown under the edge, every time); prefer
   a field clear of the frame (y < -supOut, y > 1+supOut, x beyond mx), else oldest `born`; seeded blobs are all born 0 in
   KIND order (3192-3250): tie-break by an index hash, never by index (index order retires every disc first).
   (c) m_acidTurnAcc += dt; per period turnover/want retire one; the unchanged `live != want` walk grows one. Reset the
   accumulator wherever m_acidSeeded is cleared (501, 2076, 2460).
5. Hazards. (a) kAcidMaxBlobs 128 (fluid.h:2126) counts retirees; grow stops at size 128 (3307). Monotone blob_count 126 at
   turnover 90: a swap per 0.7 s x 30-40 s linger = ~50 retirees -> grows block, live falls to ~75 (black rises by
   accident). Rule: while size >= 127 skip the retire (accumulator clamped to one period), log `[turnover] blocked` once.
   (b) The respawn floor (3975) uses the CURRENT baseR: a grown-in giant is pulled to y ~1.06 and swells in view at the
   bottom edge; with turnover > 0 use max(baseR, rTarget) there. (c) Grow-in drops the seed's structure: webs unchained
   (3199-3218), holes not in a parent nor sub-scaled (3239-3246), bubbles unclustered, colIdx fixed per kind (3312-3317).
   After one period the population is not the seed's: measure black % only after >= 1 period. (d) Monotone fracs sum 1.05:
   grow-in never makes a hole. (e) conserve_mass <= 0.5: retirees never erased (4018), size change reseeds (1975): guard.
   (f) accent_mode 1 hashes the INDEX (3997, 5569): every erase shifts classes; inert on monotone.
6. Guard `if (blobTurnoverS > 0 && conserveMass > 0.5)`: at 0 no RNG draw, `born` never read -> bit-identical.
   Determinism: shot dt 1/144 (main.cpp:2993); the pick uses a PRIVATE xorshift seeded from g_randSeed (not rand(), not
   m_acidRespawnRng; grow-ins draw m_acidRespawnRng as today). Two runs at turnover 90 must give one md5.
7. [liquid_acid] blob_turnover_s float: getF/putF beside conserve_mass (main.cpp:664/2003), keys.inc row by 410, range
   from the sheet (slider policy); excluded from every clock (*_s, CLOCKS item 12).
### 2. Layout key sets
8. keys.inc: blob_count INT 16..128 (132; clocks never touch ints, CLOCKS 6/11); disc_frac 0..0.5 (134: the brief's 0.55 is
   out); web_frac 0..0.6; bubble_frac 0..0.8; hole_weight 0..3; size_bias 0.5..4; big_bias 0.2..2; threshold 0.1..1.2;
   support_scale 1.2..4; droplets INT 0..4096 (312); droplet_ink_frac 0..1 (322). INI-ONLY (no row, so clocks.cpp Build()
   refuses them): all disc/web/bubble/hole _min/_max, breath, buoyancy, wrap_margin, damping, droplet_r_min
   (main.cpp:516-536, 596). rise_respawn is a BOOL (539; keys.inc:786); conserve_mass a 0/1 float branched at 0.5. No
   sentinel or KF_SUPERSEDED key is in the brief's sets.
9. Starfield "blob_count ~6" < slider min 16, and in role A leaves ~95% black. droplet_spawn_rate is no density lever: it
   only refills to `droplets` (INT) + crust (4146-4161, 4999-5012). No nucleation risk: kind 1 accepts p >= 0.10 anywhere in
   open ink (4301; 20 tries >= 88%); role A's black dots are kind 0, born anywhere in the film (4295). No key needed.
10. Role-A first sheet rows (guesses, 3-5 values each). Today = monotone as is, MEASURED FIRST (the ~9% is mock3.py's tile).
   Few giants = 2-3 big holes in a continuous film: bubble_frac down until (1-disc-web-bubble) x blob_count ~ 3, hole_min
   0.15 / hole_max 0.25-0.35 (ini-only), hole_weight 2-3. Lava rise: the base ALREADY has acid-rise-12's rise block
   (.0195/.5/1/.45/.7, conserve 1; bottom_light 1.0, not 0.8); threshold 0.79 -> 0.9-1.0, support_scale 2.0 -> 1.6-1.8
   until the gaps rise as columns. Starfield: continuous film (threshold 0.65-0.7), low droplet_ink_frac, droplets
   1500-2500, larger r_max, low droplet_mass_bias (crust = lit bubbles). Plausible: Today, Starfield; sheet: the other two.
11. supOut = baseR*support_scale*(1+breath)*0.86 (3906): at r .35 x 2.0 the respawn trip is ~2.2 frame heights, so a
   2-3-giant layout swings hard: measure 300 s x 3 seeds, not 60 s x 1. Buoyancy (0.002 at r .12, fluid.h:68; 3774) lifts a
   0.5 blob 0.008/s alone, swamping the brief's rise_speed .004. disc_max is also the parallax/accent radius (3704, 3991).
### 3. Black % measurement
12. CoverageDarkPct is the WRONG metric: UpdateCoverage (6451-6510) downsamples m_dye.read, the FLUID DYE texture, not the
   frame; the film exists only in the display PS. It runs only if (auto_pause && wanderers) || coverageWanted (6452):
   monotone has wanderers 0 and the cycle wants it on fluid stages only (cycle.cpp:478), so on oil m_darkPct sits at its
   init 100 (fluid.h:2111). Do not log or touch it (governor + early switch read it).
13. Metric (binding): black % = share of pixels of the 8-bit SDR .png (display-referred scRGB/sdrScale, main.cpp:2649-2654)
   with max(R,G,B) < 40 (popdetect.py's `hole` class, ~2% of SDR white linear). A fixed scRGB threshold cannot match HDR on
   and off: content is scaled by sdrScale (fluid.cpp:1060; sdr_white/80 vs 1, main.cpp:2929), so §3's reasoning is wrong.
   No src change: tools/black-share.py (PIL, as ab.py) + tools/black-share.ps1.
14. Run `<wt>\build2\FluidWallpaper.exe --shot <dir>\<n>.png --ini <FULL ini> --hdr on|off --seed <s> --shot-size 1280x720
   --shot-delay <T0> --shot-series 31:10 --shot-yield 30 --shot-png-only`, T0 >= blob_turnover_s when on; partials merged
   into a scratch full ini; BelowNormal; renders don't wait on the lock, builds do (EXECUTOR-CARD §1). Parse the PNGs, never
   the shared %TEMP% ShotLog (CLOCKS 25). Print mean/min/max per seed. No per-second in-process `[dark]` readback.
### 4. Layout clock = PHASE 2
15. Not buildable on today's CLOCKS: phase 1 (clocks f64e6e5) has no oilsim group, reads [clocks] from settings.ini/--ini
   only (CLOCKS 29) and takes keys.inc floats only. Per-stage points need CLOCKS merged + its phase-2 stage reading.
16. Adopt-on-change form: points are DELTAS from Today: live = base + sum_N w_N (P_N - P_today), clamped to keys.inc min/max;
   Today takes the remaining weight. ClocksHold glides weights to Today -> live == base verbatim (CLOCKS 5); a user edit
   moves base and the deltas ride it; snapshots take the base (CLOCKS 3).
17. Shares: the dominant point is drawn CATEGORICALLY per segment, P = share (44/33/13/9); its weight from the body draw, the
   rest log-normal; glide with the segment smoothstep. Shares hold by construction; 240 h dryrun (CLOCKS 26b).
18. Per-frame safe: threshold, support_scale (moves only wrap/parking bounds), repulsion, rise_wobble/stretch/parallax/
   bottom_light, droplet_ink_frac, droplet_r_max, droplet_bias, droplet_ring_frac/clump. Turnover-only: disc/web/bubble_frac,
   hole_weight, size_bias, big_bias. Need a keys.inc row first: *_min/_max, breath, buoyancy. NEVER interpolated:
   blob_count, droplets (ints), rise_respawn (bool, equal in all points), conserve_mass (1 in all), enums. Excluded:
   rise_speed (phase-snapping, CLOCKS 12: LA_TIME x LA_DYE_ID_RISE, fluid.cpp:5926, shaders.h:2071), spawn_grow_s/
   dissolve_s (also the droplet taus, 4228-4229), droplet_life, film_level. Every point keeps the stage's blob_count.
19. Turnover <= 1/3 of the shortest segment (300 s -> <= 100 s). Layouts are points OR cycle stages, not both: points.
### 5. Proof
20. Turnover 0: identity fast tier 15/15 MATCH vs the main baseline; WE parity 835AECBD... MATCH; dxbc-cmp IDENTICAL for
   every PSO (no shader); slot-check PASS (no slot); keymeta-check PASS. Turnover 90: two runs, one md5.
21. Strip: `--ini <monotone + blob_turnover_s 90> --shot-preset "reference\presets\Layout - <X>.ini" AT 5 --shot-delay 30
   --shot-series 6:30` (merges without reseed: main.cpp:3049-3054, 2185-2199). --clocks-force pins a group factor, not keys:
   unusable here. No-pop: popdetect.py --min 200 over three 2 s frame-step windows (1/144) at logged retires + black %/frame.
22. No 3-min clip on the LIVE director (second instance): headless `--shot --cycle --shot-delay 10 --shot-series 18:10`.
23. Sheet: ab.py wants prefix-NNN.png; proven row = handoff\review\proven\proven-1..4 (oil); panels labelled target vs
   measured (300 s, 3 seeds), HDR on AND off. Manifest rows for the Layout partials: Base = monotone, Tier full.
### 6. Rafts (to the user)
24. Confirmed: droplets feel the field (confine/carry 4446-4480) and each other (attract/merge/ring_clump 4783-4940); crust
   rides the negative blobs' velocity (4460-4471) anywhere in a mass; nothing pulls a droplet to a chosen blob. Attractor:
   2 keys (strength, ring radius x core radius), ~50 lines in the StepAcidDroplets motion (each kind-0 droplet springs to
   r_core*k round the nearest small hole, kind 3, baseR < cap); O(drops x blobs) ~120k ops/frame CPU, no shader, inert at 0.
   Risk: the ring coalesces unless it uses ring=1 droplets (4328). One sheet, ~half an executor session.
### 7. Scope
25. PHASE 1 (one executor, after PHOTO merges): Fable's role-A answer; black-share.py/.ps1; measure monotone + acid-rise-12;
   blob_turnover_s per 3-7; Layout partials from a sheet (role A); proofs 20, 21, 23; FEATURES/SETTINGS rows. PHASE 2
   (after CLOCKS merges + its phase 2): 15-19, proof 22, cycle list. Rafts only on the user's yes.
### 8. Brief errors
26. §0/§2 film_level as the black lever (1-2). §2 "Today measured ~9%" (mock3.py). §2 monotone bloom is 0.36 (ini:802),
   not 0. §2 disc_frac 0.55 / blob_count 6 out of range. §1 "oldest": no age; blob_count on clocks: excluded int. §3 m_darkPct
   + HDR reasoning (12-13). §5.3, §5.5, §2 clock (15). Study: §2 "correct black-oil share" and §3 undershoot are false.

## FABLE DECISIONS on the pre-flight (2026-09-27 01:10) — binding
- ROLE A CONFIRMED: on the monotone base the FILM is lit and the black masses are the GAPS (plus kind-0 droplets).
  Evidence: the BX first-look render (yellow film, black lobed masses and black droplets) is exactly the proven
  photos' structure. Every layout recipe in §0/§2 and the study §3 is therefore inverted: MORE black = fewer/smaller
  positive blobs, bigger holes, higher threshold, lower support_scale. film_level is not a layout key.
- All 26 items accepted. Phase 1 = item 25 (black-share.py/.ps1 metric on the 8-bit SDR PNG max(R,G,B) < 40, measure
  monotone + acid-rise-12 first, blob_turnover_s per items 3–7 with the `born` field and the private RNG, Layout
  partials from a role-A sheet: 300 s x 3 seeds). Phase 2 = the layout clock (items 15–19) after CLOCKS phase 2.
- Rafts stays a user question (item 24: 2 keys, ~50 CPU lines, half a session).
- Executor order stays: CLOCKS phase 1 -> BX merge -> PHOTO -> BLACK-MASS phase 1.
