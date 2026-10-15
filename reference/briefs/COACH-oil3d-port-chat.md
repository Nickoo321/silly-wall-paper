# COACH — Oil3D, from the wallpaper-port chat ("Custom WE wallpaper port", Fable 5.1)

2026-10-14 late. For the "3D wallpaper oil prototype" session (Opus 5.5) and its executor. The user:
"literally do anything that chat says, it has ultimate authority and has been doing this for weeks."
So: answers first, then INSTRUCTIONS, then the RULES. Read all of it before the executor starts.

## 0. The one thing the lab got wrong (why "none of these look good")

Look at build2/shots/oil3d/dir-1..8 next to any live oil frame (e.g.
build2/shots/live/hdrsdr/sdrg-oil-1/sdrg-oil-1.png, the live stage at 60 s). The live plot is
**BLACK oil on a LIT coloured film, seen through a macro lens and a film stock**. The colour is the
backlight (the "film": monotone base + scheme hues + hue2 patches + lamp grey), the oil masses and
the droplets are ALWAYS black (canon, user-set: "Oil + droplets ALWAYS black (no dye)"). The lab
rendered coloured glossy blobs on a black void with Blinn speculars: the plot inverted, no film,
no ink, no lens, no stock. That is the bubblegum. No amount of depth fixes an inverted plot.

A backlit lamp is seen in TRANSMISSION, not reflection: what "depth" looks like in it is
THICKNESS (thin lobes let the colour through, cores go black), OCCLUSION (a near body hides a far
one; where two bodies approach, a soft dark contact), PARALLAX (far = small, slow), and the LENS
(a focus band, circles of confusion, field curvature). Gloss is the wrong cue; the only front-lit
things are the glass lid (lid sheen/scratches/ghosts) and the tiny droplet highlight
(droplet_spec 0.35), both already small by rule (OLED: hot things stay small).

## 1. Answers to the seven questions

**1. "Grittiness", the user's words.** Verbatim on record:
- 2026-09-22, photo reference/shots/photos/high-iso-noise-in-mass-phone.jpg: "it doesn't read like
  film, it looks more like high ISO artifacting" (the dark masses were speckled with colour noise).
- 2026-09-23, after brief BD: "noise fix is fire"; same evening on cast shadows (BC): "fire".
- 2026-09-22, photo reference/shots/photos/mass-edge-light-band-keep-phone.jpg: "that low-key
  should stay" = the lighter band just inside a black mass's edge (halation + thin oil) is WANTED;
  any clean-up must not darken it.
- 2026-09-25 colour work: "too bubbly" rejected (spotty hue2 islands); photo-sized patches wanted.
- 2026-09-17: "The reference photos were not meant to be copied, just vague ideas."
- 2026-10-14 (to you): "none of these look good", "the oil is still mostly flat, actually render the
  depth, get the grittiness a lot realer", "more of the effects physical".
"too clean / too accurate" are NOT on record verbatim; the nearest facts are the CG rejection today
and the photo rule above. Grit, in the user's mouth, = the macro-photo imperfection of a real
backlit dish on a real sensor: film grain that lives in the mid-tones, halation bleeding colour
into the black, lateral CA that grows toward the corners, a tilt-shift focus band that drifts, a
dirty glass lid, cellulose fibres on the surface, droplets with rims and halos. NOT noise.
References the user sent (all in reference/shots/photos/): microscope-ref-5-THE-ONE.jpg (the
canonical lens look: edge CA, halo, field edge), microscope-ref-1..10, liquid-acid-ref-5..8,
oilwater-ref-1..3, proven-1..4 (user's own photos, the colour canon), sketch-perspective-camera.jpg
(the user's camera sketch the perspective rig was built from), lensshade. "Underwater" references:
NOT in this repo. If the user gave you underwater photos, copy them to
reference/shots/photos/oil3d-underwater-ref-N.jpg with their one-line verdicts and tell me the paths.

**2. What produces it today, live values, where.** Base of 13 of the 15 oil stages =
reference/configs/monotone-post-0924.ini (+ Layout - Few giants + a Scheme - *.ini); acid-rise-12
(-tone-half) is the other. Live values (monotone | acid-rise-12):
- Surface: cellulose (fibres, drift with the rise), oil_film_bump (thickness islands), speckle
  0.2227 (interface speckle on the oil edge), seam_strength/seam_lo (dark seams on steep dye
  gradients), rim_width 0.0016 / rim_dark 0.80 / rim_vary 0.4 (noise on the rim width),
  meniscus 0.85 w 0.0020 (bright ink-coloured halo outside the rim, meniscus_from_ink 1.0),
  oil_thin_edge + oil_edge_frac (the film thins at the edge toward the ink), refraction, penumbra.
  Code: src/shaders.h `#ifdef LIQUID_ACID` blocks of kDisplaySrc (~line 1143 on; AcidSwarm at 899
  is the OLD procedural swarm, now off: swarm_holes 0, swarm_drops 0).
- Droplets: `droplets` 950 particles, spawn 40/s, r 0.0011..0.0132, attract 0.50, coalesce,
  droplet_lens 0.55 (lens_centre 0.30 | 0), droplet_spec 0.35, rings (ring_frac 0.15, width 0.13,
  lift 0.08, clump 0.6, wobble 1, r_mul 3.5, big_frac 0.10), droplet_depth 0.35, depth_rise 0.22.
  CPU sim in src/fluid.cpp (AcidBlob / droplet code), rendered INTO the oil surface.
- Lens ([post], the post pass kPostSrc + the display pass): softness 1.70 | 0.5 (defocus psf),
  halo 0.18 | 0.12 at 12 px (HALATION: the lit film bleeds into the black masses = the edge band),
  aberration 1.10 px | 1.8, aberration_field 1.70 | 1.2 (lateral CA growing with field radius),
  aberration_coc 0.6 | 0 (CA fades with defocus), vignette 0.94 | 0.12, camera_fov 28,
  camera_focus 0.30 | 0.5, camera_field_curve 1.45 | 0.12 (corners defocus), camera_axis 0.54/0.46,
  focus_tilt 0.35 | 0.26 with focus_band_px 420 | 340, focus_tilt_period 45 | 75 s, move 1.6 s
  (a tilt-shift sharp band that re-aims every so often: StepCameraRig in src/fluid.cpp ~2200/4030),
  dof_max_px 5 | 9 (the CoC channel), lid 0.35 | 0.45 + lid_ghost (glass lid sheen, scratches,
  ghost reflections: LidScratch/LidScrLit in shaders.h ~3746), lamp_grey 0.60 | 0 (lamp grey
  heart), fog_mass_gate 0.7, film scratches (kPostSrc scratch slot, R13 fix 10-05).
- Film stock: [post] film_grain 0.190 | 0.10, film_grain_size 3.0 | 2.5, speed 0.65 | 1,
  film_grain_chroma 0.15 | 0, film_grain_density 1 (D-log-E hump: smoothstep(0.10,0.32) x
  (1-smoothstep(0.55,1)), ZERO floor on black, fades in the highlights; chroma 0 = the grain
  SCALES the pixel, hue-preserving), Emulsion() in shaders.h ~3616. [ink] chroma 0.35 | 0,
  density 6.3 (Beer-Lambert ink under/around the oil, InkWater), shadow_floor 0.10, toe_tint.
  [liquid_acid] grain = 0 (dropped where the post grain exists: never two grains).
- Everything above is CYCLED by the clocks (src/clocks.cpp: per-key slow log-normal, proven state
  most of the time, episodes rare, subtle ON levels). A depth version inherits that for free if
  it only adds keys.

**3. Tried and rejected, why.**
- The high-ISO speckle (BD, 09-22): CAUSE = lateral CA taking a first difference of the SHARP
  source on R and B only (printed the grain as colour, 72% of the mass chroma noise) + two grains
  stacked, both additive RGB steps with a 0.15 floor on black (69% of the luma noise) + per-pixel
  jitter in the halation/bloom gathers + post lifts with no dark fade. FIX = CA taps from a 1.5 px
  average, grain scales the pixel (hue-preserving, zero floor) with the D-log-E density curve, one
  grain only, paced to film_grain_fps, lifts gated by ONE spatial "deep inside a mass" test (4 taps
  40 px out) so the lit edge band keeps everything. Rule distilled: grit must be an OPTICAL
  consequence (lens, emulsion, dish), never a pattern added to RGB.
- "too bubbly" spotty second-colour islands (09-25): patches must be photo-sized.
- Multi-colour dyed oil, glow bodies, tint-over-greyscale vignettes: rejected directions (canon).
- rise_parallax_dim above ~0.4: "reads as a different palette" -> shipped 0.
- The standalone 3D lab (10-14): the inverted plot above.

**4. Grit physically real, not an overlay.** Treat the frame as a real optical stack and give each
layer its own small imperfection, in this order (back to front, then the lens, then the stock):
1. DIFFUSER (the backlight): a soft lamp with a hot centre and a cool top (rise_bottom_light,
   lamp_grey), slow level breathing; no texture of its own.
2. INK/WATER marbling on or just above the diffuser: the posterised dye (ink_levels, dye_core,
   dye_depth), seen through the film: Beer-Lambert (ink density 6.3).
3. OIL BODIES (black, absorbing): colour = the diffuser seen THROUGH the oil, T = exp(-k * path
   length); thin lobes transmit, cores black. The dark rim + bright meniscus = Fresnel + lensing at
   the grazing edge; keep the keyed rim/meniscus as the fallback when thickness is unavailable.
   Surface micro-structure = cellulose fibres + film_bump, drifting with the rise.
4. DROPLETS: water in the oil (black, rims, halos, rings) and oil on the ink; sit ON the body
   surface, tiny highlight (lid-lit), lens the ink behind them.
5. GLASS LID: scratches, sheen, ghost reflections (lid keys) — the only front-lit plane.
6. LENS: perspective (fov 28), field curvature, tilt-shift band that re-aims, CoC per depth,
   lateral CA (from a blurred source), halation (lit film bleeding into black), vignette.
7. STOCK: one grain, D-log-E density, hue-preserving, paced; scratches; soft shadow floor.
The 2D look IS this stack flattened. A 3D version keeps every layer and only replaces the
"thickness" and "occlusion" fakes of layer 3 (oil_thin_edge, rim, oil_absorb, penumbra) with the
real path length and the real front surface. Nothing in 4-7 changes.

**5. Depth features today, live, liked.** All LIVE: droplet_depth 0.35 + depth_rise 0.22 (droplet
CoC/size by depth), rise_parallax 0.7 (small = far: slower rise, sway, flow; the whole motion
budget by size), rise_parallax_dim 0 (dimming rejected past 0.4), dye_depth 0.5 / tilt 0.28 /
w 0.25 (the ink's depth under the film), the perspective camera (fov 28, field curvature, axis
drift, focus tilt band, dof_max_px) built from the user's own sketch, lamp base light, cast
shadows (BC, "fire"), penumbra, the hue2 parallax patches (drift with the rise, re-laid on the
rig's readjust). Liked: shadows, the noise fix, the lit edge band, the macro-lens look, the
tilt-shift band. Disliked: dimming-as-depth, bubbly islands, glow, gloss.

**6. Taste a depth version must not break.**
- The plot: black oil on lit colour; oil + droplets always black; colour only from the film/ink.
- Colour canon (handoff/CREATIVE.md + DECISIONS.md): proven = 1 colour or a near-complementary
  pair at BIG size (user photos proven-1..4), 7:2:1 proven:moderate:wild, seams narrow, films
  equal-load, schemes ride the anchor drift; never invent hue bans; warm shadow tint allowed.
- Layouts: ALL of them, continuously mixed (giants ~28% black, lava, starfield, rafts); the live
  cycle uses Layout - Few giants (50 blobs, blob_turnover_s 90) over every oil stage.
- OLED: ~418 nits full frame, 1000 nits small window, ABL: hot things SMALL; mostly dark frame;
  nothing perfectly still; HDR on AND off both right (sdr_gamut 1 handles the SDR colour).
- Post effects are cycled with duty cycles (the clocks); ON levels subtle; no new post features
  (VHS/fisheye/CRT were cut).
- Never zero m_hueAngle abruptly; hue rotation is post with counter-rotating emission.
- "Three roles": not a phrase in my records. If it is the user's, ask them; my reading = the
  three bodies (black oil, lit film/ink, droplets) which must keep their roles (black / colour /
  black-with-halo). Do not merge roles (that is the "multi-colour dyed oil" rejection).

**7. What is wrong with NEXT-oil3d-depth.md (honest).**
- The max-projection P = max_z F3 gives OCCLUSION only. It is a 2.5D trick; it renders no
  thickness, so the masses stay the flat black cut-outs the user calls "mostly flat". The user's
  ask is "actually RENDER the depth": in a backlit dish depth is seen as thickness first. The ray
  must return PATH LENGTH through the blob union (per blob the sphere/ellipsoid chord, summed with
  the Wyvill weighting; analytic, no marching) and the FRONT-SURFACE normal, as two new inputs
  with defaults that reproduce today (thickness -> the oil_thin_edge ramp, normal -> (0,0,1)).
- "Stage 1 identical at z=0" is right and is THE gate — keep it, with the real proof (below).
  Note the fluid parity md5 the brief quotes (10E36EBF...) is stale: the live harness constant is
  835AECBD9EF1384A8CAAF1A611EE3A26 and the baseline file is
  build2/shots/preset-identity-baseline-965a5d8.txt (20 rows). Use the harness, not a hand md5.
- Stage 2 renders "blob_depth at 3 values, a sentence each" has no judge. The judge is the user
  on the panel via A/B popups, from a SHEET (2D | 3D z=0 | low | mid | high), HDR on and off.
- Perspective inside the blob field double-counts the lens perspective (fov 28 is already a
  post-stage camera). Keep the field orthographic; depth = z for CoC/parallax; the lens stays
  where it is. Occlusion: front-most crossing wins colour/identity — right.
- Cost: the 2D loop already evaluates up to 126 blobs per pixel with the per-blob 64-bit mask.
  A naive per-pixel ray with Newton steps multiplies that. +2 ms at 1440p is not a budget for a
  raymarch; it IS a budget for an analytic chord sum. Use the lab's support-interval mask, no
  marching in Stage 1-2, measure with --timings, and fall back to a half-res 3D field composited
  under the full-res rim/meniscus/droplet math (those are what reads) if needed.
- Stage 3 "specular, 3D lighting from the normal": only if it is TRANSMISSION lighting (thickness,
  Fresnel at the grazing edge, contact darkening). No Blinn/Phong gloss on the bodies — that is
  the lab's look. Lid and droplet glints already exist and stay small.
- z rule: tie z to radius (rise_parallax already treats small = far) plus a slow per-blob wobble;
  one rule, nothing pops; turnover/re-entry keep z.
- Perf/proof rules are right (EXECUTOR-CARD, build2, gpu.lock, identity). Add: every render
  --shot-yield 8 (not 0: the user games; yield 30 if a game is running), one render at a time,
  CRLF kept in src files, no desktop attach, no window, ever, until the user OKs the look.

## 2. INSTRUCTIONS (what, order, proof, who)

Step 0 — 3D session, text only (cloud routines are free): rewrite NEXT-oil3d-depth.md to the
stack in section 1.4 and the stages below; freeze it. For the cloud's 1-hour prompt cache: the
brief + the fixed code excerpts (LiquidAcidConfig, the acid block's inputs/accumulators, the
droplet struct) are the STABLE PREFIX of every routine prompt; append deltas at the END; batch
the edits so a routine runs once per hour-window, never resend a reworded brief mid-window.
Hold the standalone lab exe and src_oil3d: keep them as the raymarch core library (Lipschitz
step, blob mask, fxc SM5.1 traps) for a later stage; their look is parked.

Step 1 — local Opus executor (worktree C:\Users\abg77\fw-oil3d-depth, branch oil3d-depth off
origin/review-fixes-2026-10-04 = deb383e, which is what is LIVE as build2\live; main 4de5a67 is
behind PR #1): in src/ only, keys default to today:
  [liquid_acid] oil3d 0|1 (0 = today's loop byte for byte), blob_depth 0..1 (z spread), the blob
  z lane (say which slot), and the 3D evaluator returning: projected field (max along the
  orthographic ray), grad at z*, colour/identity accumulators at z*, PATH LENGTH through the
  union (analytic chords, Wyvill-weighted), front normal. Thickness and normal unused in Step 1.
  PROOF A (the gate): tools\preset-identity.ps1 -Exe <worktree exe> -Yield 8 -Baseline
  build2\shots\preset-identity-baseline-965a5d8.txt -> 20/20 UNCHANGED + PARITY MATCH (exit 1
  from MISSING rows is fine). PROOF B: the cycle composite (monotone-post-0924 + Layout - Few
  giants + Scheme - Magenta Cyan, 60 s, seed 1234, --hdr on) with oil3d=0 vs oil3d=1 blob_depth=0:
  max abs diff and MAD in PQ codes, a x40 diff image. Expect rounding (<= 1-2 codes). If not,
  STOP and report. Cost with --timings at 1440p, both settings.
Step 2 — same executor, after Proof A+B pass: oil3d_absorb 0..1 (thickness darkening: T =
  exp(-k * path), 0 = the 2D oil_thin_edge/oil_absorb behaviour, calibrated so the shipped edge
  band level is unchanged at the default), oil3d_occlude 0|1 (front crossing wins),
  blob_depth > 0 with z by radius + slow wobble, droplets placed at the body surface z (CoC via
  the existing droplet_depth path), contact darkening from the field gradient where two bodies
  approach (cheap AO, oil3d_contact 0..1). NO perspective in the field, NO gloss, NO new post.
  Renders: the composite above and acid-rise-12-tone-half, t = 60 and 120 s, HDR on AND off,
  1440p, yield 8, one at a time under gpu.lock, as ONE sheet: 2D | 3D z=0 | depth low | mid |
  high (with absorb + occlude + contact on). Plus the same sheet with absorb only. Send the
  sheets to the user (SendUserFile). Cost line per setting. Commit on oil3d-depth, no push, no
  merge, no swap. Then STOP and hand back (executor's own worries + the one thing it would
  change first, as EXECUTOR-CARD asks).
Step 3 — the user, by A/B popup only: which depth level, keep/cut absorb, occlude, contact.
  Log every verdict in handoff/review/DECISIONS.md (date | topic | where | user: | mine: |
  verdict). Then the identity re-run with the chosen defaults if any default changes a pixel,
  a new baseline only with the user's yes, and the swap by panel-check-task.ps1 -Ini <copy of
  the real settings.ini> -Exe <exe> after an A/B yes (never without -Exe).
Step 4 (only after Step 3): lid as a front plane (scratches with parallax), Fresnel rim from the
  real normal replacing the keyed rim where thickness exists, tunnels (holes with z extent).
  Never a Stage "3D lighting with speculars".

## 3. THE RULES (standing, user-set; relay them to the executor verbatim)
- Build ONLY into build2 of your worktree; never build\ (the user's), never touch build2\live,
  Wallpaper Engine, brightness, the running FluidWallpaper.exe (never stop/kill/signal it, never
  tray commands). Every exe run carries --shot (a bare run starts as the live wallpaper).
- GPU: ONE render or build at a time, under build2\shots\gpu.lock of the MAIN repo
  (tools\gpu-lock.ps1, dot-source; Wait-GpuLock/Release-GpuLock in try/finally), BelowNormal,
  --shot-yield 8 (30 while a game runs), 15 s pause after releasing the lock. Two renders at once
  greyed the user's OLED.
- No window, no focus change, no MessageBox, no desktop attach ("don't tab me out").
- Identity before anything goes near the user: the harness + baseline above; the keyed defaults
  must reproduce today byte for byte; sim_res 256 / dye_res 4096 never in presets; bloom only the
  keyed post bloom.
- HDR on AND off both verified; screenshots are rough hue/composition checks, never luminance
  truth; identity baselines are --hdr on.
- Questions to the user ONLY via A/B popups (AskUserQuestion), with a recommended option first;
  every verdict into handoff/review/DECISIONS.md; every image you look at goes to the user.
- Reference photos are ideas, not targets: no parity chasing, no side-by-sides against photos.
- Source hygiene: CRLF in src/ (agents' script edits turned files to LF twice: use the Edit tool
  or fix them back), MSVC string literals <= 16380 bytes (split R"hlsl()hlsl" pieces), no new
  deps, SM 5.1 fxc, never commit .claude/launch.json, commits on the feature branch only, no push
  to main, PR #1 stays the user's call.
- Pacing: one agent/executor at a time, no fan-outs; cloud routines for text; keep the local
  Opus executor to one verifying render round then hand back; don't ping the user with status.
- When in doubt about taste: the canon in handoff/CREATIVE.md and DECISIONS.md wins over any
  model's instinct; when the canon is silent, ask by popup, don't guess.

## 4. ADDENDUM (same night) — after the haze references, "very orthogonal", and the other coach memo

This section SUPERSEDES the step order in section 2. The rules in section 3 stand.

**New user inputs.** haze-ref-1-green-panel-lacks-haze.jpg ("do you see how the green photo
lacks a certain haziness?"): every droplet equally black and sharp at every size, flat backlight,
no medium. haze-ref-2-underwater-rays-particles.jpg + haze-ref-3-misty-rays-grain.jpg = what is
wanted: light SHAFTS through a medium, specks at many depths (mostly defocused) catching light
inside the beams, grain living inside the light, the darks clean. "It should look like a lava
lamp, being very orthogonal in nature." "Those renders still ass" (the perspective raymarch).

**Read this as physics, not as a mood.** A lava lamp is a column of slightly turbid liquid lit
from BELOW, seen STRAIGHT ON. Depth in it is carried by the MEDIUM, not by perspective: the lamp's
light climbs the liquid between the wax bodies (shafts that the bodies shadow), suspended
particulates glow where the light reaches them and fade where it does not, far things lose
contrast and sharpness into the liquid (aerial perspective), and the only straight lines are
vertical. The lab's perspective camera and gloss were the two wrong decisions; both are dead.

**Converge with COACH-oil3d-fable.md.** Its height field is the right primitive: h(x,y) from the
sdf with a sessile-cap profile, n from grad h, identity at oil_height 0. That h IS the thickness
proxy my section 1.4 asks for (no chord sums, no raymarch, no max-along-ray: drop all three).
Its stage list lacks the medium, and its refraction/caustic should come AFTER absorption and the
medium, because the user's two asks tonight are "not flat" (= thickness) and "haze" (= medium).

### The order now (each stage: one executor round, one build, identity at defaults, one sheet)
A. **Surface + thickness exist** — `[liquid_acid] oil_height 0|1` (h, n; debug view
   `--shot-debug height`). Keys at 0 = today byte for byte: tools\preset-identity.ps1 against
   build2\shots\preset-identity-baseline-965a5d8.txt -> 20/20 UNCHANGED + PARITY MATCH.
B. **Transmission** — `oil3d_absorb 0..1`: the backlight seen THROUGH the wax, T = exp(-k*h):
   thin lobes let the film colour through, cores stay black; the keyed oil_thin_edge /
   oil_absorb / penumbra remain the 0-state. Calibrate so the live edge band level is unchanged at
   the default. Sheet: 2D | absorb low | mid | high, 60 s + 120 s, HDR on + off, the cycle
   composite (monotone-post-0924 + Layout - Few giants + Scheme - Magenta Cyan) and
   acid-rise-12-tone-half.
C. **The medium (the haze ask)** — one group of keys, all default 0:
   - `medium` 0..1: in-scattering strength of the liquid; medium colour = the film/scheme hue
     (never a new hue), level subtle, HDR headroom respected (hot things small).
   - `medium_shafts` 0..1: screen-space light shafts from the lamp side (the bottom edge, where
     rise_bottom_light already puts the lamp) climbing VERTICALLY through the gaps, occluded by
     the oil mask (the black bodies shadow the liquid above them), exponential decay with height,
     a slow lamp breathing; a 1-D vertical march per pixel at half res (~32 taps), composited
     under the rim/meniscus/droplet math. Columns of glow between the bodies = the lamp.
   - `medium_depth` 0..1: aerial perspective by z: far droplets and far bodies lift toward the
     lit medium colour and soften (their CoC via the existing droplet_depth path); near ones
     stay black and sharp. This is what ref-1 lacks.
   - `motes`, `mote_depth`, `mote_px`, `mote_dark`: a third particle class in the droplet cell
     walk: sub-droplet specks, high count, wide z spread, mostly defocused, lit ONLY by the local
     shaft light (bright inside a beam, invisible in shadow), drifting up with the rise and
     swaying; dark motes exist too (dust in front of the lit film).
   - Darks stay clean: no in-scatter is added in front of a body deeper than the medium between
     it and the lid (use the existing massDeep spatial gate); grain is untouched (the D-log-E
     density already puts it in the mid-tones = inside the light).
   Sheet: 2D | shafts only | shafts + depth haze | + motes, same configs; plus a 10-frame series
   (1 s apart) for the motion of shafts and motes.
D. **Refraction + caustic + Fresnel rim from n** (the other coach's stage), lid as a front
   plane. Only after C is on the panel.
NO perspective in the field (the straight-on view is the look; the post lens's fov 28 stays as
the mild lens it is), NO gloss/specular on the bodies, NO raymarch, NO standalone exe, NO window.

### Process (binding)
- Gate first (A). Once A passes identity, SWAP the exe (A/B popup, panel-check-task.ps1 with
  -Exe, the user's settings unchanged): every later stage is then a PARTIAL overlay preset
  (`reference/presets/Oil - Physical.ini`, keys only) the user loads in the Settings window: the
  panel is the truth, shafts and motes are motion effects, and no further swaps are needed per
  stage — only identity re-proof when a DEFAULT changes.
- Every stage: identity at defaults (20/20 + parity), one sheet, cost line (--timings, 1440p,
  oil3d keys on vs off; medium budget ~1.5 ms at half res), renders one at a time under
  gpu.lock at --shot-yield 8 (30 while a game runs), sent to the user, verdicts by A/B popup,
  logged in handoff/review/DECISIONS.md. Hand back after each stage; do not chain stages.
- Cloud routines: text only (brief rewrites, shader drafts in the stable-prefix prompt, 1-hour
  cache: brief + code excerpts first, deltas last, one run per window). Local Opus executor:
  shader edits, build, identity, renders, one stage per round. Me (this chat): judging the
  sheets before they go to the user if asked, the identity harness, the swaps.

## 5. RULING 2 (2026-10-15 ~00:30) — top-down scene, real shadows, polygons, budget

User inputs since section 4: "Those renders show polygons ... seriously think hard about how to
avoid that"; "not polygon but flat" (flat-ref-1-not-polygon-but-flat.jpg); "7900 GRE, above
60 fps is fine, rather 60 fps than polygons"; "the shadow is being faked" (shadow-ref-1-ring: a
ring with an offset dark crescent inside); "whiting in the bottom right, but it's fake"
(shadow-ref-2: the lamp-falloff glow); the SCENE: "orthogonal, or a very small fov from far.
Motes, directional lighting. The water can be ~1 m deep, the oil usually on top. Start with the
TOP-DOWN idea (ignore gravity, it still goes up relative to the screen; like a lid, angled). It
needs real motes and a lot of post processing to read as real." "Three roles" = the user's own
rule (09-23): every composition = ONE element pitch black, ONE the main colour, ONE an accent.

**Ruling: adopt the 3D session's v2 KEY IDEA as THE light model; my bottom-lamp shafts are
dropped.** One geometry, one light, everything derived from it:
- Camera: top-down ORTHOGRAPHIC (the screen = the water surface). The rise stays screen-up.
  The post lens (fov 28, field curvature, tilt band, CoC, CA, halation, vignette) stays as is.
- Layers along z: oil + droplets AT the surface (z = 0, "usually on top"); the water column;
  the lit film/ink = the RECEIVER on the floor at `tank_depth` (metres; 1 to start; 0 = one
  layer = today byte for byte; "infinite" = the floor's contribution extinguished by the water,
  colour from in-scatter only — a sub-case of the same keys, not a mode).
- Light: ONE directional light from above, `light_elev` (deg) and `light_azim` (deg), slow
  drift allowed (the "angled lid"); its footprint on the receiver REPLACES the fake whiting
  (rise_bottom_light / lamp_grey stay only as the 0-state fakes).
- From that geometry, for free and consistent with each other:
  (a) SHADOWS of the oil on the receiver: the oil mask displaced by tank_depth * cot(elev) along
      azim, penumbra growing with depth (PCSS): big masses cast clear soft shadows, small
      droplets' shadows blur away. Real, not drawn.
  (b) SHAFTS: in-scatter along the light direction through the gaps between the oil bodies,
      down the column (the underwater refs); on screen = the lit-gap mask smeared along azim
      with decay; the oil bodies shadow them.
  (c) HAZE: aerial perspective of the floor and of anything deep, by path length through the
      water (extinction key); near things black and sharp, deep things lift toward the water
      colour and soften (CoC by depth).
  (d) MOTES: in the column at many depths, lit ONLY where (b) reaches them, CoC by depth,
      drifting up with the rise. Dark motes near the surface in front of the lit floor too.
  (e) The ring's crescent (shadow-ref-1) is NOT a floor shadow (1 m * cot(elev) lands far
      outside the ring): it is the droplet's own refraction/caustic under the SAME light:
      dark on the side away from the light, a bright caustic toward it, one azimuth for every
      droplet. So the droplet self-shadow + caustic moves from D into C (C4): the user sees
      masses and droplets shadowed by one light in one sheet. Today's hand-offset crescents
      (ring_lift etc.) stay as the 0-state.
- Absorb (B) still applies under a top light: through a thin edge you see the floor's light
  passing DOWN through the oil and back UP (T^2 of the path); cores black.

**Polygons: forbidden, and here is how.** h is an analytic sum of C2 Wyvill kernels: evaluate
it per pixel at FULL res (never half-res-then-upsample: that makes terraces), gradients
analytic (never finite differences), no 8-bit or 10-bit intermediates for h/n/shadow/scatter
(R16F minimum), PCSS with a blue-noise/jittered kernel and temporal smoothing (no fixed grid
taps), shafts as an analytic 1-D integral or a jittered march with a per-frame-stable dither,
depth-of-field as the existing CoC gather. The debug view at 300% (h grey, n rgb, shadow, scatter)
must show no facets, no terraces, no handover creases, no banding; if any appears, fix it before
a sheet. "Flat" (the user's word) is the goal for the FILM: flat colour plateaus with a bluish
lighter halo inside every edge (flat-ref-1) — that halo is the thin edge + the receiver's
light, not an outline.

**Budget (user: quality over frame time).** 60 fps is acceptable: total frame <= 14 ms at 1440p
with everything on (report with --timings: today's cost, each new stage's cost); the new stages
may take up to ~6 ms combined. Never below 60. The fps_limit key stays the user's.

**Order now:** A (in flight, unchanged) -> swap -> B absorb -> C the scene in sub-steps on the
same overlay preset: C1 tank_depth + light + shadows (PCSS) + footprint; C2 shafts + haze;
C3 motes; C4 droplet self-shadow + caustic; -> D Fresnel rim from n, lid plane, body refraction.
Each sub-step: identity at defaults, one sheet (+ a 10-frame series for motion), the cost line,
the user's A/B, DECISIONS.md. Three roles stay enforced in every sheet (one pitch black, one
main colour, one accent).

## 6. UE5 test scene A/B (user's decision 2026-10-15: "UE5 test scene first, in parallel") — fairness warnings
Sent to the 3D session in one message. Summary: (1) same composition: build the UE scene from an
exported blob/droplet/film dump of one --shot at the same seed/time; (2) fixed exposure, no ACES
toe / default vignette / bloom, match the black level (our shadow_floor vs UE's 0), compare with
Windows HDR off first, sRGB vs our sdr_gamut 0; (3) the receiver must be a LIT albedo plane
(emissive receives no shadows), light x albedo calibrated to our film colour, ambient off, shadows
filled only by volumetric in-scatter; (4) oil = masked opaque for shadows + a thin translucent
layer for the thin edge, black albedo, no specular, VSM resolution high; (5) volumetric fog froxel
settings (GridPixelSize 4, GridSizeZ 128+, light-shaft occlusion) or UE shafts look soft/smeared
for the wrong reasons; (6) motes as opaque-masked meshes or separate translucency off so DoF
blurs them, TSR off for stills, no tilt-shift in UE; (7) real-time vs Movie Render Queue, say
which, report UE frame time at 1440p; (8) 10 s motion clips both; (9) the shipping cost that is
not in the picture (engine process, WorkerW, HDR handling, clocks/cycle, presets, Settings,
identity); (10) DDC/shader caches off the SMR D: drive.
