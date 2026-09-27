# Brief CLOCKS — every key on its own slow clock (the distribution model)

Status: DRAFT 2026-09-26 23:20, for auditor pre-flight, then ONE Opus executor. Sources: the user's
direction 2026-09-25 (memory creative-direction-session items 5, 6, 6b, 6c, 7, 8), DECISIONS.md rows
"outlier rate", "request 5 Q4 burst", "fluid proven refs" (7 rows), handoff\CREATIVE.md §2, §6, and
the key inventory scratchpad\clocks-key-inventory.md (392 keys, 14 perceptual groups, 34 excluded).
No new visual features. Existing keys only. The director gets one new module.

## 0. The user's model, restated (binding)

1. Every proven key rotates, continuously; presets are only the points the clocks pass through.
2. Each key's level is drawn from a SKEWED, long-tailed distribution: MEAN = the preset value,
   MODE below it (most of the time lighter than the preset), a long upper tail so an effect pops
   out once in a while. Draws change slowly; fades never pop.
3. A mode/stage is defined by how it SHAPES that distribution (spread, tail, floor/ceiling), not by
   fixed values. Presets carry the means; a mode carries the shape.
4. Outliers: about ONE every ~45 min ACROSS all keys, not per key; a whole GROUP of related keys goes
   wild together; then several normal segments; then a DIFFERENT group. ~10% of keys stay out.
5. Effects stay subtle inside the band; the outlier is the mix-up. No VHS, no new features:
   "episodes" are the tail of existing keys (film scratches/dust/hairs/leak/noise, focus tilt + dof,
   corner_warp + camera_fov, softness + aberration). "Scan lines once in a while" = film_scratches.
6. Burst is OUT as its own mechanism; the palette burst is the COLOUR group's tail.
7. Fluid stages: red ~350–10 is the COLOUR group's mean hue (hue_center), hue_range skewed so 1–2
   colours is the norm and the wide sweep the tail (DECISIONS 2026-09-26). WE parity preset untouched.

## 1. What it is (spec)

A new director module `src/clocks.cpp/h` ("the clocks") that, while the cycle runs, keeps a slow
per-group random walk and applies it to the live config every frame:

    live[key] = base[key] * f_group(t) * j_key(t)          (multiplicative keys)
    live[key] = wrap(base[key] + a_group(t) + a_key(t))    (angular keys, degrees)
    live[key] = clamp(live[key], lo_key, hi_key)

- `base` = the director's composed stage config (what `CycleStageBase()` returns today, including a
  running LerpLook/Journey value mid-transition). The clocks never read the ini themselves.
- `f_group` is a smooth signal with a log-normal marginal: at each DRAW the group gets a new target
  `F = exp(sigma*z - sigma^2/2)` (z ~ N(0,1)), so E[F] = 1 exactly (mean = preset) and the mode is
  `exp(-1.5*sigma^2)` < 1 (sigma 0.30 -> mode 0.87, median 0.96, P(F > 1.8) ~1.7%). The signal
  GLIDES from the old target to the new one over the whole segment with smoothstep (no plateau, no
  pop): the derivative is continuous and |d(live)/dt| stays below `base * 0.02 / s` at sigma 0.30.
- `j_key` is a small per-key jitter, log-normal with sigma_key = 0.10 and its own draw phase, so the
  members of a group move together but not in lockstep.
- Segment length per group is drawn uniformly from [draw_min_s, draw_max_s] (default 300–540 s) with
  a per-group prime-ish offset so no two groups share a period (CREATIVE.md §6: periods never divide
  the cut). Phases are seeded from `[cycle] seed` + group index, so `--seed` makes a run reproducible.
- TAIL (the outlier / episode): a draw whose F exceeds `tail_at` (default 1.8) is a tail. A GOVERNOR
  enforces the user's rate and shape across all groups:
  - at most one tail at a time; minimum gap `tail_gap_min_min` (default 20 min) since the last tail
    ended; a forced tail at `tail_gap_max_min` (default 75 min) if chance produced none;
  - never the same group twice in a row; candidate groups are drawn with weight `group_<name>`;
  - a tail lasts `tail_len_s` (default 120 s) at its target, with the same smoothstep in and out
    (>= `fade_min_s`, default 20 s), then the group returns to a normal draw;
  - a draw that would be a tail while the governor says no is re-drawn from the body (z clamped to
    the value that gives F = tail_at). So the body statistics are unchanged and only the rate of
    excursions is governed. Expected rate at defaults: ~1 tail per 45–55 min with 8–10 active groups.
- EPISODE ADDS: some episode keys are 0 in every preset (film_scratches, film_hairs, corner_warp...)
  and a multiplicative clock leaves 0 at 0 (by design: what a preset turns off stays off). For those,
  a stage/mode may declare an ADDITIVE tail amplitude `tail_add_<key> = value`; during that group's
  tail the key rises to `base + value * e(t)` where e(t) is the tail envelope (0 outside). Shipped
  tail adds are listed in §3; the user's rule applies (rarer and stronger, never resident).
- ANGULAR keys (hue_center, post_hue, film_hue2, film_hue3, shadow_tone_hue, highlight_tone_hue,
  dye_hue, ink hue keys, palette_start_hue is NOT one (one-shot)): the group signal is an angle
  `a_group` in degrees, a smooth random walk with normal steps of `sigma_deg` (default 12 deg per
  draw for fluid hue_center, 25 deg for film_hue2) BOUNDED to `[-angle_max, angle_max]` around the
  base (reflecting walk, default +-180 for film_hue2 = the "bounded orbit", +-40 for hue_center so
  the fluid's red home stays home). Anchor weighting for film_hue2: the step is scaled by 0.5 when
  moving away from the nearest proven anchor offset and 1.0 toward it, so the offset lingers near
  the proven pairs without ever being pinned. The tail for an angular group = a wide excursion
  (a step of 3*sigma_deg) — the COLOUR group's tail replaces the burst.
- Integer/count keys are never varied. Keys at exactly 0 in the base are never varied except by a
  declared tail_add. A key whose slider max is hit by the tail may exceed the slider max up to
  `hi_key = 1.5 * slider_max` (user: going outside the normal range is OK for an outlier), EXCEPT the
  brightness-capped list (post_brightness, oil_hdr, max_brightness, film_level, dye_lum,
  dye_droplet_lum, lamp_grey_heart_lift, halation, halation_threshold inverse, shadow_tone_lift,
  wanderer_brightness, idle_brightness): those clamp at `min(1.25 * base, slider_max)` (ABL).
- DIRECTION: the skew is on the effect's STRENGTH. For a key whose visible effect grows as the value
  DROPS, the executor flips the factor (uses 1/F). Candidates from the tooltips: film_level (lower =
  darker film), oil_edge_frac?, focus_band_px (narrower = stronger tilt-shift), halation_threshold
  (lower = more). The executor makes the list from keys.inc tooltips and reports it; default is +1.

## 2. Keys and files

New ini section `[clocks]`, valid in settings.ini (global default) AND in any stage/preset ini (a
stage's block overrides the global one key by key = "the mode carries the shape"):

| key | default | range | meaning |
|---|---|---|---|
| enabled | 0 | 0/1 | 0 = today's app bit for bit (the clocks pass is an identity copy) |
| sigma | 0.30 | 0.05..0.60 | body spread of the log-normal for every multiplicative group |
| sigma_key | 0.10 | 0..0.30 | per-key jitter spread |
| tail_at | 1.8 | 1.2..3.0 | factor above which a draw counts as a tail |
| tail_len_s | 120 | 30..300 | tail hold |
| tail_gap_min_min | 20 | 5..120 | governor: minimum minutes between tails |
| tail_gap_max_min | 75 | 10..240 | governor: force a tail after this many minutes without one |
| draw_min_s / draw_max_s | 300 / 540 | 60..1800 | segment length range |
| fade_min_s | 20 | 5..120 | shortest glide in or out of anything |
| group_<name> | 1 | 0..2 | per-group weight: multiplies sigma for that group AND its tail draw weight; 0 = group frozen |
| angle_max_hue2 / angle_max_hue | 180 / 40 | 0..180 | bounds of the angular walks |
| sigma_deg_hue2 / sigma_deg_hue | 25 / 12 | 0..90 | angular step per draw |
| tail_add_<key> | (none) | key's range | additive tail amplitude for an episode key |

Group names (from the inventory, 14): grain, film, colour, hue2, splittone, rig, lid, droplets,
dye, oilsim, lens, fluidsim, ink, mirror. Membership is a static table in clocks.cpp built from
the inventory §2 (the executor copies it; the auditor checks it against keys.inc). Per look only the
groups that the look reads are active (fluid: colour, fluidsim, lens, grain, mirror; oil: all but
fluidsim's emitters/ink; ink: colour, ink, fluidsim, lens, grain).

EXCLUDED for good (never in the table): the inventory's 34 low-value keys (sim_res, dye_res, gamut,
[hdr] calibration, [general]/[system], look switches, every mode enum, mirror mode/segments/source,
wanderer_mode/count, pressure_iterations, blob_count/droplets/conserve_mass, ink.inverted, [cycle]);
the hard constraints (vorticity, bloom, saturation_restore, sim/dye res); every key that another
clock divides time by (PHASE-SNAPPING: hue_rotate_period, hue_sweep_period, color_cycle_period,
film_hue2_wobble_period, shadow_tone_period, shadow_tone_fade, mirror rotate_period, film_grain_fps,
pair_sweep_period, film_artefact_rate, dissolve_s, droplet_coalesce_s, any *_period/*_s/*_fps);
seeds; palette_start_hue and the hueshift_* burst cycler keys (event-driven; the palette animator and
the fluid hue cycler are already clocks of their own and keep running underneath); [drops] interval
(event rate; keep). That is ~50 keys out of 392, within the user's "~10% lowkey useless".

## 3. Shipped shapes and adds

- settings.ini `[clocks] enabled=1` is written by the cycle-on install only (scratchpad
  monotone-cycle-on.ini gets it; the repo configs keep 0 so every --shot proof is unchanged).
- Three overlay presets, look=any: `reference/presets/Clocks - Base.ini` (defaults above),
  `Clocks - Calm.ini` (sigma 0.18, tail_gap_min 45, sigma_deg halves), `Clocks - Wild.ini`
  (sigma 0.42, tail_at 1.6, tail_gap_min 10, draw 180–360 s). The cycle uses Base; Calm/Wild are
  for the user to try from the tray.
- Episode adds shipped in `Clocks - Base.ini` (oil stages; the film group and the rig group):
  `tail_add_film_scratches 0.35, tail_add_film_hairs 0.25, tail_add_film_dust 0.30,
  tail_add_film_leak 0.20, tail_add_film_noise 0.15` (film group = "old print"),
  `tail_add_focus_tilt 0.5, tail_add_dof_max_px 6` (rig group = tilt-shift),
  `tail_add_corner_warp 0.25, tail_add_camera_fov 8` (lens group = lens distortion; softness and
  aberration_px are multiplicative on their base and ride the same lens tail). Values are starting
  points for the proof sheet; the user judges on the panel. Halation is a base effect (ABL): no add.
- Fluid stages (WE journeys/variations, NOT "WE parity (fluid).ini"): the stage inis get
  `hue_center = 0` (red home) where they do not set a hue yet, and the colour group runs hue_range
  multiplicatively (mode ~0.87 x preset -> narrower than the preset most of the time, the tail a wide
  sweep). The parity preset's md5 must not change (it is rendered with the cycle off).

## 4. Where it hooks in (from the inventory §4)

- `CycleTick` (cycle.cpp:1149) already produces the base each tick: `ApplyStage()`/`FinishLerp()`
  write `r.Config() = c` and `LerpLook()`/`JourneyUpdate()` write Config during transitions/DWELL.
  Refactor: those write into a director-owned `s_base` (the same struct `CycleStageBase()` exposes),
  and ONE new pass `ClocksApply(s_base, r.Config(), dt)` at the end of `CycleTick` copies base to live
  with the factors applied. With `enabled=0` the pass is a plain copy: bit-identical, prove it.
- Nothing in fluid.cpp changes (no parity risk); no shader change (dxbc IDENTICAL for all three
  PSOs). The palette/hue2/rig/hue-shift animators keep running underneath on their own slots; the
  clocks only scale the keys those animators READ.
- New animator slot `ANIM_CLOCKS` in animators.h: Freeze holds every group's phase (no wander,
  factors frozen, auto-release like the others); `ClocksState()` getter for the UI.
- Settings window OPEN: the clocks glide to F = 1 (all groups) over fade_min_s and hold there, so the
  user edits and sees the BASE; on close they glide back and resume. (UI-ANIMATORS-MODEL §2 lets
  colour clocks keep running; the noise clocks hold, otherwise every slider is "dirty" against a
  moving target.) UI phase 2 later: a "Clocks" row (enabled, sigma, next tail in) — not this brief.
- `--clocks-dryrun HOURS [--seed N] [--ini file]`: runs only the scheduler on the CPU at 10 Hz
  virtual time, no window/GPU, prints one line per draw (t, group, F or angle, tail flag) and a
  summary: per-group mean F (must be 1.00 +- 0.03 over 24 h), mode bin (< 1), tails per hour,
  min gap, consecutive-same-group count (must be 0), max |dF/dt|.
- `--clocks-force <group>=<F>|tail`: pins one group for a --shot render (proof sheets).
- `[shot]` log line per draw when `--console`/--shot: `[clocks] t=... group=... F=... tail=0/1`.

## 5. Proof (PROOF BLOCK per EXECUTOR-CARD §6, plus §7)

1. enabled=0: identity fast tier 15/15 unchanged; WE parity md5 835AECBD9EF1384A8CAAF1A611EE3A26
   MATCH; dxbc-cmp IDENTICAL for fluid/ink/acid; slot-check, keymeta-check PASS.
2. `--clocks-dryrun 24 --seed 1234` with Base: mean F per group 1.00 +- 0.03; mode bin below 1;
   tails/h between 1.0 and 1.6; min gap >= 20 min; no same-group repeat; max |dF/dt| <= 0.02/s.
   Same with Calm (tails/h <= 0.5) and Wild (tails/h >= 2). Paste the summaries in the report.
3. Live no-pop proof: a 15-min real-time run with the cycle on (monotone stage, --console log),
   parse the [clocks] lines: every glide >= fade_min_s, no frame-to-frame delta on any key above the
   §1 bound. Report the largest.
4. Sheets (60 s, seed 1234, HDR on and off, GPU lock, BelowNormal, --shot-yield 30): on the monotone
   base and on we-look-live: (a) F=1 all, (b) every group at its mode, (c) one column per group at
   tail (F = 2.2, or the tail_add for film/rig/lens), so the user can see what each group's outlier
   looks like -> `build2\shots\live\clocks\groups-{mono,we}-{hdr,sdr}.png`; (d) fluid colour group:
   hue_center 0 with hue_range at mode / mean / tail -> `fluid-colour.png`. Numbers: mean_lum per
   column (ABL check: no tail column above 1.25 x the F=1 column except the declared adds).
5. UI: Settings open -> factors glide to 1 within fade_min_s (log); close -> resume; Freeze/Unfreeze
   ANIM_CLOCKS holds and releases (log lines).
6. FEATURES.md rows for every new key; SETTINGS.md regen; manifest rows for the three Clocks presets.

§7 for the review chat (verbatim in the report): 1. does it WORK (which of 1–5 are proven, which
only measured); 2. the visible DIFFERENCE in one sentence; 3. what each group's tail ADDS and which
add values look wrong on the sheet; 4. anything that looked like a pop.

## 6. Branch, order, merge

- Branch `clocks` from main after BX merges (BX adds lamp_grey_heart keys; they join the rig group).
  Worktree C:\Users\abg77\fw-clocks. Commit per milestone; write CLOCKS-RESUME.md at each.
- Merge order: BX -> CLOCKS -> PHOTO STAGE (its stage type gets group_* = 0: a still does not wander).
- After merge: the swap A/B (cycle on with Clocks Base) and the creative chat's §7 in its block; then
  the with/without rote sheets run on the Base shape (grain, aberration, vignette, halation, split
  tone/lamp grey, then the episode keys).

## Open (for the auditor pre-flight)

- Whether the Settings-open hold (F -> 1) is right, or the UI should show base x F per key instead.
- The DIRECTION list (keys where lower = stronger) — executor proposes, auditor checks.
- Tail adds are guesses; the sheet in §5.4 is what decides them, then the user on the panel.
- The angular anchor weighting for film_hue2 uses the same proven anchor list as hue_anchor_weight
  (fluid.cpp ANIM_PALETTE); confirm the offsets are relative to the palette hue, not absolute.

## AUDITOR PRE-FLIGHT (2026-09-27, main 97c3852) — binding

Read-only; no src change between 97c3852 and e4bc274. Where these items and the brief disagree, these items win.
### A. Architecture (replaces the §4 refactor)
1. NO "director writes s_base + one copy pass". `s_base` already exists as the int overlay stage (cycle.cpp:71).
   Writers OUTSIDE CycleTick would be overwritten every frame: BeginSwitch via CycleNext/Prev/Jump/SetEnabled (tray,
   --cycle-next-at: cycle.cpp:925,936,938,941-942), CycleRevertStage 1673, ApplyPreset main.cpp:2195, and the UI
   sliders, which point into renderer.Config() (main.cpp:2969; ui_model.cpp:409-411,623-663,803). ApplyStage,
   FinishLerp, SchemeSwap, SoftPoint and FlipDiscrete read live right after writing it (cycle.cpp:493-498,787,908,994,
   381), and CycleTick returns early in OFF and WARMUP (1155, 1311). Writers in the tick: LerpLook 1257 (45 floats,
   312-356), FlipDiscrete 1259, SetAmts 1235-1236, SchemeSwap 1245, FinishLerp 1265, SoftPoint 1217, ApplyStage 1218,
   journey StartLeg (journey.cpp:187-190). fluid.cpp writes m_cfg only at init and SetResolutions (125, 2609).
2. BINDING: ADOPT-ON-CHANGE. clocks.cpp keeps {base, lastOut} per clocked key. `ClocksTick(r, dt)` runs right after
   CycleTick in both loops (main.cpp:3103, 3812), gated `enabled && (g_cycleActive || forced)`. Per key: if live !=
   lastOut, base = live; then live = lastOut = f(base). With enabled=0 it returns before touching anything, so it is
   bit-identical. The order CycleTick -> ClocksTick -> SimOnlyStep -> Frame means the warm-up sims factored, at black.
3. Every snapshot takes the BASE (`ClocksBaseCopy`), or the factors compound: s_from cycle.cpp:758 (else a (F-1)*base
   pop at lerp start), s_amtFrom 883, s_preOverlay 985/978, the UI TakeSnapshot, and SaveCurrentAsPreset/
   PersistFullConfigNow main.cpp:2219,2223,2238. Safe as is: ApplyPreset's merge over live, and suspend 3467->3486.
4. GoOff (cycle.cpp:733) restores base synchronously: ApplyPreset calls CycleManualOverride (main.cpp:2174) before it
   reads Config (2187).
5. At F==1, j==1 and offset==0, write base VERBATIM (no multiply, clamp or wrap), so the hold gives live==base exactly.
   UiDirty compares live to the composed base (ui_model.cpp:530-534); Save writes live (ui_presets.cpp:208,261).
6. Floats only: build the table from the keys.inc floatPtr rows (ReadAt, ui_model.cpp:317). Never touch the 20 intPtr
   rows or the bools.
### B. UI, hold, transitions
7. No director function knows the Settings window is open, so add `ClocksHold(bool)`: true at create
   (ui_window.cpp:1902) and on the input re-arm (1496), false ONLY in WM_DESTROY (1537). Not CyclePaused(): that
   auto-releases at 600 s (cycle.cpp:1768), and "Let it run" (1060,1346) releases it with the window open.
   Open Q1: hold.
8. During the hold glide, a `ClocksSettling()` test in UiRowAnimLocked (ui_model.cpp:912) keeps the clocked rows out
   of dirty and Save-as-partial (532; ui_presets.cpp:261).
9. Journeys own hue_center, hue_range and dark_floor (ui_model.cpp:931-934), so hold the colour group while
   JourneyActive(). The BeginLerp bridge aims at s_target.hueCenter (cycle.cpp:763): add the colour offset or log the
   miss.
10. Append ANIM_CLOCKS after ANIM_CYCLE (animators.h:41): slots 0-3 keep fluid.h's kAnimClocks=4 arrays (1723-1841)
    and the ApplyFreezeToRenderer range (cycle.cpp:1023). Add AnimatorName 1750, --shot-freeze (main.cpp:~2847) and
    the UA_ map (ui_cycle.cpp:248). Give it its own time accumulator; comment the kAnimClocks name clash.
### C. Key table: every key named here is EXCLUDED unless marked
11. Ints placed in groups: hueshift_burst_steps (keys.inc:112), sweep_count (198), idle_amount (118), drops.spatter
    (552). wanderer_scale is read only in ReinitWanderers (fluid.cpp:2626). The inventory's "hot-swap: Yes" column is
    unreliable.
12. PHASE-SNAPPING the brief missed (key x absolute time): film_grain_speed (shaders.h:4640), film_hue2_drift
    (fluid.cpp:3511), rise_speed (shaders.h:2071 via fluid.cpp:5926; 2920), cellulose_drift (shaders.h:2920).
    Durations: droplet_life (4235), idle_interval (1913), dart_interval (2685), wanderer_resume_delay (1900),
    drops.tail_sec. Snapping grows with uptime: no 60 s shot or dryrun sees it.
13. Lattice keys that re-lay out every frame: swarm_scale_drops/holes (shaders.h:2779-2810), film_hue2_seed_rows.
14. Per-step decays near 1: density/velocity_diffusion and decay_fast. x0.87 clears the dye in ~1 s; x1.2 never
    decays. Phase 2: vary the half-life.
15. Positions: light_x/y/z, camera_axis_x/y, camera_focus, dye_depth, focus_tilt_angle, mirror.center_x/y,
    tone_balance.
16. NOT excluded but conditional: the sentinels (keys.inc `neg:`, -1 = inherit/complement) dye_droplet_hue/sat/lum and
    shadow/highlight_tone_hue vary only while base >= 0.
17. Also excluded: the governor setpoints dark_floor, dark_level, surv_dark_floor, contrast_req (they feed the
    dark-screen trigger, cycle.cpp:1197-1203); pixel_shift_px (OLED net), dither, band_min; KF_SUPERSEDED dye_masses
    and dye_droplets (keys.inc:448,450); hue_anchor_weight (FireBurst inverts the warp, cycle.cpp:831-837); vorticity,
    saturation_restore and bloom by name. Key the table by section.key (there are two `density` keys).
18. hi = slider max for every key whose max <= 1, and for hue_range. Many are clamped at upload anyway
    (fluid.cpp:1053), so 1.5x would be inert. lo = slider min.
19. DIRECTION. Flip: focus_band_px (keys.inc:676), corner_warp_r (670), seam_lo (246), ink.edge_lo (512), ink_levels
    (186). Do NOT flip halation_threshold (730, "Higher lowers the knee" = more; the brief is wrong), film_level (ABL
    list only), oil_edge_frac or the size biases. Flip as F' = exp(-sigma*z - sigma^2/2), not 1/F
    (E[1/F] = 1.094 at sigma 0.30).
### D. Angles
20. All degrees. hue_center, post_hue and dye_hue run 0..360; film_hue2/3 run -180..180 (wrap in that convention);
    oil_penumbra_hue and dye_thick_hue are ±90 signed offsets (multiplicative is fine). There are no ink hue keys.
    The "exact 0 is never varied" rule is for multiplicative keys only (hue_center 0 = red).
21. WheelHue subtracts the commanded angle whatever hue_center is (fluid.cpp:2535). The AGENTS invariant holds if the
    clocks never write m_hueAngle, never call CommandHueShift and never touch hueshift_*.
22. hue_range >= 179 is the full wheel: hue_center is ignored (fluid.cpp:2536) and the shift cycler runs. Below 179 the
    rotation glides home a full turn (2766-2782). So never vary hue_range when base >= 179, cap it at 178.9, and never
    vary post_hue while hue_range < 179 (2768). we-look-live and WE parity have hue_range=180, so "hue_center 0 red
    home" is INERT on them. "1-2 colours is the norm" needs banded hue_range means: a content call for Fable/user.
23. film_hue2 is an OFFSET applied after the palette rotation (fluid.cpp:5858-5861). The hue_anchor_weight anchors
    {325,215,355,275} are ABSOLUTE hues (5323-5338, cycle.cpp:48), so they cannot anchor it. Use the Scheme presets'
    offsets {180,160,150,125,-40,-45,-105,-120,-140,-145,-160}. angle_max 180 = unbounded.
### E. Seeding, dryrun, logs, proof
24. Seed: shots pass --cycle-seed, else --seed (1234), to CycleOverride (main.cpp:2916); live passes 0 (3385), meaning
    the [cycle] seed, where 0 = wall clock (cycle.cpp:146-148). Expose it. Use your own splitmix stream: never
    NextRand, never rand().
25. --clocks-dryrun goes beside --cycle-draw-test (main.cpp:2918-2922): before CycleBoot (2923) and InitOffscreen
    (2935, the device), return 0. It needs `--shot x.png` (2879). ShotLog appends to a %TEMP% log shared by every
    concurrent shot (2335-2350): parse your own redirected stdout, and do not pass --console.
26. Proof fixes. (a) The fast tier has no cycle row (manifest 36-40 are Tier full), and with the cycle off CycleTick
    returns at 1155: add `-Presets` cycle-final/first/lerp-test plus one ini with `[clocks] enabled=0`. (b) Mean
    1.00 +- 0.03 over 24 h is ~1.4 SE (~90% chance some group fails): run a 240 h dryrun. (c) 0.02/s contradicts the
    20 s tail and hold glides (~0.09/s): bound the body only. (d) Calm <= 0.5 tails/h is impossible with tail_gap_max
    75 (forced >= 0.78/h): Calm gets >= 150; say which groups are active. (e) The "15-min real-time run" would mean a
    second live instance: use a headless `--shot --cycle --shot-delay 900` plus an in-process max |delta|/frame
    tracker logged at the [state] site (main.cpp:3154). (f) --clocks-force must tick with the cycle off.
27. Commands: `tools\preset-identity.ps1 -Exe <wt exe> -Baseline <main file> [-Presets ..]`; `python tools\dxbc-cmp.py
    <main>\src\shaders.h <wt>\src\shaders.h <wt>\src\acid_slots.h`; slot-check; `keymeta-check -Root <wt>`, which
    greps key strings only in main.cpp and cycle.cpp, so there are no keys.inc rows for [clocks] in phase 1.
### F. BX
28. Branch from today's main and merge BX later: safe. BX touches cycle.cpp only in WantsScheme (+4, ~865), plus
    fluid.h (+3 floats), main.cpp ini load/save (+3 each), keys.inc (+8), ui_window.cpp (+3 at 1242), the shaders,
    cycle-final.ini and the manifest. None of that overlaps the CLOCKS touch points listed above. Add the
    lamp_grey_heart keys to the rig table after the merge (compile dependency), and re-save the identity baseline.
### G. Brief errors and the cut
29. §1 "base = CycleStageBase()" is WRONG. It recomposes from DISK on every call, returns the TARGET mid-lerp, has no
    journey legs and composes an overlay alone (cycle.cpp:1652-1660): never call it per frame. The stage [clocks]
    blocks and the Clocks tray presets are ignored (Compose and ApplyPreset skip unknown sections, cycle.cpp:385-416,
    main.cpp:2187), and identity would render a [clocks]-only preset as the defaults: phase 2. `enabled` is global.
    j_key needs a segment length. group_<name> scales sigma AND the tail weight (counts twice). EXECUTOR-CARD's file
    map is stale (settings.cpp and moods.cpp are gone; the keys live in src\ui\keys.inc).
30. Too big for one executor. PHASE 1: items 1-19 and 24-27; multiplicative grain, film (+adds), lens (+adds), lid,
    rig (strength keys), the splittone and hue2 AMOUNTS, dye (non-sentinel); governor; hold and settling lock;
    ANIM_CLOCKS; [clocks] from settings.ini or --ini only; dryrun; proofs 1, 2, 3(e) and 5; one sheet
    (monotone-post-0924, HDR on AND off, F=1 / mode / tail per group). PHASE 2: angular walks and film_hue2
    anchors, the fluid colour group and the hue_range decision, the fluidsim/oilsim/droplets/ink/mirror groups,
    per-stage shapes, the Calm/Wild presets, the UI Clocks row, the position/half-life variants, and the
    we-look-live sheets.

## FABLE DECISIONS on the pre-flight (2026-09-27 00:15) — binding
- Phase split ACCEPTED as item 30: this executor delivers PHASE 1 only. Phase 2 (angular walks, fluid colour
  group, sim groups, per-stage shapes, Calm/Wild, UI row, we-look-live sheets) gets its own brief after the
  hue_range content decision (creative chat + user: banded hue_range means per fluid stage; item 22).
- Open Q1 (hold): ClocksHold(bool) as item 7, released only in WM_DESTROY. The hold glide is fade_min_s.
- group_<name> scales sigma only; tail candidate weight is a separate `tailw_<name>` (default 1). Fixes item 29.
- j_key segment = the group's segment length with its own phase offset (item 29).
- Proof per item 26: 240 h dryrun, body-only slope bound (tail/hold glides bounded by fade_min_s instead),
  Calm tail_gap_max 150 (Calm/Wild themselves are phase 2; the dryrun runs the shapes from an --ini).
- [clocks] read from settings.ini or the --ini file only in phase 1 (item 29); the three Clocks presets are phase 2.
