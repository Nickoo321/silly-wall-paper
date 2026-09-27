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
