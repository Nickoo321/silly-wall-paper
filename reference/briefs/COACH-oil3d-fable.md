# COACH memo: Oil3D depth, round 1 (Fable, 2026-10-14)

Read: NEXT-oil3d-depth.md, LiquidAcidConfig + PostConfig (fluid.h), the LIQUID_ACID field loop,
CoC block, refraction, specular, shadow and grain blocks of kDisplaySrc, the lab shader header.
Looked at: stage-02, stage-06, proven-1, proven-3, lab-09-gloss-1440, dir-1-lamp-wax.

## 0. The diagnosis before the plan

Two things jump out of the images.

1. The lab renders (lab-09, dir-1) failed for a reason that is not "3D": opaque Blinn-Phong
   spheres on black, no backlight, no ink underneath, no absorption, refraction, defocus or
   grain. "Clean CG bubblegum" is exactly right. Depth in a macro oil photo never comes from
   shading a sphere; it comes from OPTICS between layers.
2. The live look is ALREADY 2.5D done physically, in six places, each with its own fake surface:
   `refraction` (edge band along grad), `oil_refract_body` (grad + fbm at pp*3.1),
   `oil_film_bump` (fbm at pp*2.2), `oil_specular`'s dome normal `nS` (fbm at pp*4.3),
   `oil_iridescence` (hue from that fbm, not from thickness), the cast shadow (`light_z`),
   Beer-Lambert `oil_absorb` on `thk`. None share a surface. That is why it reads as
   flat-plus-effects: in a real drop the bent ink, the shadow, the caustic, the Fresnel edge and
   the thickness tint all come from ONE height field, and the eye checks that they agree.

So "flesh out the 2.5D" is not "add a z to blobs". It is "give the film one real surface and
derive every optical effect from it", then add the few things that truly sit at other depths.
The z-per-blob plan does the second half and skips the first.

## 1. What physically rendered depth means here (ranked by payoff per ms, then by how cleanly it degrades to "1 layer = today")

1. **Oil-lens refraction of the ink** (displaced, slightly magnified, locally inverted under a
   thick drop). Highest payoff, ~0 ms: the dye taps are already paid for. Replace the fbm
   direction with Snell from the height-field normal (eta ~1.45, offset = h * tan of the
   refracted angle). h = 0 gives today's taps.
2. **Shadow + caustic on the ink plane.** The shadow exists (4 taps). Missing is the caustic:
   the light the drop bends lands inside/past the shadow footprint as a bright lobe offset by h
   toward the lamp. Dark ring + bright core is THE signature of a backlit drop (proven-3 has it
   faintly). Reuse the shadow taps, one lobe scaled by (1 - transmission), tied to `inkC` like
   the halo is, never a free additive, or ABL and HDR-off bite.
3. **Defocus of out-of-plane elements.** Already real (per-pixel CoC, `camera_focus 0.30`,
   tilt, `dof_max_px 5`); anything with a z gets it free. Feed `z_film + h` into `depSum` and
   the big drops stop sitting on the focal plane by definition.
4. **Parallax between film and ink.** `rise_parallax` is a speed fake. The real one is a uv
   offset of the INK layer vs the FILM by (z - focus) along the rig's axis wander
   (`camera_axis`, `rig_readjust`, `pixel_shift_px` already move). Small in a still, large live,
   which is where the user watches.
5. **Meniscus with Fresnel** (weakly there as `fr`): free once the normal is shared.
6. **Thin-film from thickness** (hue = f(2 n h cos theta) instead of fbm): free once h exists,
   keep weak, the palette is near-monochrome by design and rainbow kitsch is one slider away.
7. **Ink as a volume**: InkWater already has it; the live ink is a flat lit field. Skip.

## 2. Grittiness: real grit vs a noise overlay

Grain reads as real when it has a PLACE in the optical chain: image plane, screen px, after the
lens blur, density hump (you have all three: `film_grain_density 1`, "after the glass",
`film_grain_size 3`). Three things still make it read as overlay:

- A per-cell hash at 3 px is a pixel mosaic, not silver. Real grain is 1-3 px CLUMPS with
  spatial correlation, softened by the lens/scanner MTF: two-octave value noise (or a blue-noise
  tile) plus a ~1 px blur of the GRAIN alone. One `[post]` key.
- In HDR the grain must live in log exposure, not in nits: a 400-nit film carrying the same
  encoded step as a 20-nit mass is what "high-ISO" looks like. `film_grain_chroma 0.15` is half
  of this; finish it.
- ONE camera story. The stack is film (CineStill halation, `film_stock`, gate dust). Shot noise
  ~ sqrt(signal) is a sensor story and would be a second overlay. Drop it.

What should be PHYSICAL, not post: the specks in a real dish are IN the dish. Gate dust
(`film_dust`) is correctly sharp and image-plane, but the grit the user means is particulate:
motes in the ink, specks on the oil, each with its own z, hence its own CoC, shadow and parallax.
You already have a 950-particle droplet system with hashed depth and a 3x3 cell walk. A third
particle class ("motes": sub-droplet, high count, wider depth spread than `droplet_depth`, dark or
lamp-catching) is the single highest-payoff "gritty AND deep" item: a far mote going soft beside
a sharp near one sells depth harder than any blob z. Surface micro-relief stays an h-derived
term. Grain tuning itself waits for the old chat's answer on the user's grain history (round 2);
the motes do not depend on it.

## 3. The projected field: argue against it

`P = max_z F3` keeps the 2D shading alive, which is its only virtue. Against it:

- **It kills the liquid.** What makes the 2D read as oil is metaball necking. Two blobs apart
  in z stop merging under max(). The user will see "less liquid", not "more depth".
- **Holes stop biting.** A negative blob lowers F3 only near its own z; under max() it cannot
  punch a disc at another z. In Few giants the black masses ARE negative space and kind-3 holes,
  and the brief never says holes inherit their parent's z. Stage 2 silently loses the masses.
- **The sdf creases.** The envelope theorem gives grad P at z*, but where the arg-max hands over
  between blobs grad jumps, so `gl`, `sdf` and every rim/halo/meniscus width jump along the
  handover line: hairline artefacts exactly where the depth is supposed to show.
- **It buys none of the physics.** No occlusion of shading, no shadow between layers, no
  refraction of a rear blob through a front one, no parallax. A silhouette union with a colour pick.
- **What has a z anyway?** In Few giants the film is one connected sheet with 2-3 gaps; only the
  few unmerged blobs could sit elsewhere. The win is small by construction.

Pick: **height-field film + explicit layers.** h(x,y) from the field you already have (sessile
cap: h ~ cap * sqrt(max(sdf,0)/lensR), meniscus toe from the same sdf), n = normalize(-grad h, 1),
evaluated once beside `thk` (~line 2322) and consumed by refraction, Fresnel, spec, caustic,
thin-film, CoC. Layers only for what is really at another depth: ink (z_ink), film (z_film + h),
droplets and motes (hashed z, existing path), and ONE optional rear film layer (per-blob flag,
second field evaluation, composited behind the front film through its refraction and
absorption): the "blobs pass in front and behind" the user first asked for, with real occlusion,
for a second loop over the flagged subset. Identity is structural: layer count 1 and
`oil_height 0` touch no instruction.

Cut: max-along-ray; `oil3d_persp` (`camera_fov 28` exists); true raymarch inside liquid_acid
(the lab proved it, and proved it is not the look); tunnels through masses (needs a positive
mass, i.e. only a lamp-wax palette with `dye_lum > 0`); spend on the standalone exe until Stage 2
is on the panel.

## 4. Stages (each: one build, 2-3 renders, same config/seed/t, proof)

Config everywhere: monotone-post-0924 + Few giants + Scheme Magenta Cyan, 1440p, HDR on, one seed,
one `--shot-time0`, plus a 300 % crop of the same droplet cluster in every sheet.

1. **Surface exists.** `[liquid_acid] oil_height 0|1`: compute h and n, route them nowhere but
   a debug view (`--shot-debug height`: h grey, n rgb). Proof: md5 identical at 0, plus the
   debug render so the user sees the relief he is about to get lit. Put droplet/dye depth and
   `z_film + h` on one `depSum` convention now.
2. **Refraction + caustic.** `oil_refract_phys` (Snell from n, replaces the fbm direction when
   > 0), `caustic` + `caustic_px` riding the shadow taps, `depSum` fed by h. Sheet: today /
   refraction / +caustic, crop. The "it's physical now" moment; if it does not land, stop and
   rethink before motes.
3. **Motes.** `motes`, `mote_depth`, `mote_px`, `mote_dark`, in the droplet cell walk, with CoC
   and shadow. Sheet: motes at depth 0 (flat specks) vs spread (near sharp, far soft). Grain
   clump/log keys follow in 3b after the round-2 answer.
4. **Rear layer + parallax.** `oil_layers 1|2`, blob layer flag, `layer_parallax`. Sheet: rear
   layer on, and a two-frame pair 10 s apart under the rig wander to show the parallax.
5. **Lighting from n.** Fresnel rim, glancing spec, thickness-driven iridescence, all lamp-driven
   and weak. One A/B. Then the panel, HDR on and off.

Cost budget: stages 1-3 should be under +0.5 ms; stage 4 is the only one that adds a loop.

## 5. Process

- One sheet per stage, four panels, blind-keyed per the standing rule, always with a crop: 3-px
  physics is invisible at full frame and the user judges full frames in two seconds.
- Do not show identity proofs; report the md5. Do show the debug height view once.
- From Stage 2 on, ship a PARTIAL overlay (`reference/presets/Oil - Physical.ini`) he can load in
  the Settings window: the panel is the truth, and parallax and motes are motion effects.
- Check HDR off for the caustic and Fresnel terms specifically (additive, bright).
- Split: the local Opus executor owns shader edits, build, md5 gate, renders, one stage per
  hand-back. Cloud agents (text only) do what needs no GPU: a numpy reference of h/n/Snell/
  caustic with expected numbers for a known blob set; key docs, slider ranges, ini overlays; a
  diff review for the identity traps the code already warns about (dot() ordering, e = 1
  exactness). Never let a cloud agent design a look from renders it cannot see. The standalone
  exe stays parked: it is the lab-09 risk again, 3D without the camera stack.

Open for round 2: the user's grain history (old chat), and whether he still wants the rear layer
once refraction + caustic + motes are on the panel. My bet: he will not.
