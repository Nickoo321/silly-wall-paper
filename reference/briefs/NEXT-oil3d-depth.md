# NEXT — Oil3D v2: the live oil as a TOP-DOWN TANK with physically rendered depth

Status: DRAFT v2, 2026-10-14 late. Awaiting the "Custom WE wallpaper port" chat's approval/edits
(the user gave that chat final authority over Oil3D). Then ONE local Opus executor (worktree
C:\Users\abg77\fw-oil3d-depth, branch `oil3d-depth`), one stage per hand-back.
Supersedes v1 (projected 3D field) and the standalone/lab raymarch (rejected: "still ass").

## The user's canon (2026-10-14, verbatim gist, in order)
- "Imitate the 2D version exactly at first, then add dimensions." "If everything spawns in 1
  layer it looks the same." "The current setup is 2.5D, I just want to flesh it out."
- "Make more of the effects PHYSICAL. The oil is still mostly flat, but actually RENDER the
  depth, and get the GRITTINESS a lot realer."
- On their green panel photo: "do you see how the green photo lacks a certain haziness?"
  (reference/shots/photos/haze-ref-1..3: crisp equal-contrast droplets vs. underwater light
  shafts with specks at many depths, and misty rays with the grain living in the light.)
- "It should look like a LAVA LAMP, being very ORTHOGONAL in nature" / "Yes exactly, orthogonal,
  or just very small fov from far." "Motes, directional lighting. The water can be say 1 meter
  deep, but the oil should still usually be on the top. Start with the TOP DOWN idea (ignore
  gravity, it still goes up relative to the screen; more precisely it's like a lid, angled).
  It needs REAL motes, and a lot of post processing, to read as real."

- On a close-up of a ring + droplets (shadow-ref-1): "this looks good, but the SHADOW IS BEING
  FAKED." On a wide panel photo (shadow-ref-2): "there's WHITING in the bottom right, but it's
  FAKE." => the shadows and the brightening must come FROM THE LIGHT, falling on a real receiver
  at a real depth, not from offset copies and painted gradients.

## THE KEY IDEA: separate today's one plane into layers along z
Today the oil and the lit film/ink are effectively ONE plane, so every shadow and every
brightening has to be painted. Pull them apart: the oil stays at the surface (z = 0) and the lit
ink/film becomes the RECEIVER on the floor at z = `tank_depth`. With tank_depth = 0 everything is
in one layer again and the frame equals today (the user's "spawns in 1 layer = looks the same").
As tank_depth grows, every shadow becomes real: displaced along the light by depth * cot(elev),
and SOFTENED by a penumbra that grows with depth (width = depth * `light_size`, PCSS-style), so
a big mass casts a clear soft shadow while a tiny droplet's shadow blurs away, exactly like a real
tank. The ring's inner crescent becomes the ring's own shadow on the floor, seen through its hole.
The "whiting" becomes the light's FOOTPRINT on the floor and its in-scatter in the water (a spot
light or a directional light with a falloff), interrupted by the oil's shadow shafts, instead of a
gradient. When the physical light is on, the existing fake terms it replaces (the offset-shadow
taps and the lamp falloff gradient) fade out; at default nothing changes.

## The scene (everything new defaults OFF = today, byte-identical)
Orthographic, top-down. Screen up = the oil's rise direction, exactly as today (a tilted lid;
do not model gravity). World: the water SURFACE is z = 0, the water column goes down to
z = `tank_depth` (m, the user's "1 meter"). `tank_frame_m` = how many metres the frame height
spans (so depth vs. frame size is tunable; this ratio sets how far shadows travel).
- OIL: today's whole oil (masses, holes, droplets, film, rims...) floats at z = 0, unchanged.
- INK: today's fluid-sim dye, at the surface as today (a depth for it is a later key).
- DIRECTIONAL LIGHT from above at an angle: `light_azimuth` (deg, screen space), `light_elev`
  (deg above the surface). Its xy offset per metre of depth is s = cot(elev) * dir_xy.
- MEDIUM: the water scatters and absorbs: `water_sigma` (absorption per m), `water_scatter`
  (in-scatter strength), `water_color` (default: derived from the scheme's ink/film so the THREE
  ROLES hold: one pitch black, one main colour, one accent).
- FLOOR (optional, `floor_albedo`, default 0 = no floor): at z = tank_depth, catches the oil's
  shadow displaced by tank_depth * s, and later caustics.

## Physics that renders the depth (all orthographic, all cheap)
1. SHADOW SHAFTS (the "haze" + "light rays" refs). For a pixel p over open water, in-scattered
   light = integral over z in [0, D] of scatter * T(z) * lit(p, z), where T(z) = exp(-sigma z)
   on both the light and the view path and lit(p, z) = 1 - oilCoverage(p + z * s). The oil
   masses therefore cast VISIBLE shafts down through the water that, seen from above, stream
   away from every mass in the light direction, and the open water gets a lit haze that is dark
   in the shafts. 12-24 taps of a DOWNSAMPLED oil-coverage texture (half or quarter res), jittered
   + temporally stable; it is low frequency. The existing shadow taps in the display pass are the
   starting point (Fable: "caustic lobe riding the existing shadow taps").
2. MOTES (the user's "real motes"; Fable's top grit item). A new particle class living IN the
   column: z in [0, D], slow Brownian + current drift (generated off screen, moving up the screen
   like everything else; OLED rule: nothing sits still), size 1-3 px at focus. Each mote is:
   lit only if not in the oil's shadow (one coverage lookup at p + z * s), attenuated by T(z),
   DEFOCUSED by its depth through the EXISTING CoC machinery (camera_focus at the surface,
   dof_max_px): deep motes become soft discs, near ones sharp specks, a few catch a glint.
   Seen THROUGH the oil where the film is transparent (oil_absorb), hidden under opaque masses.
   Keys: `motes` (count, 0 = off), `mote_px`, `mote_depth_bias`, `mote_glint`. Bin them like the
   droplets (DropCells grid); hundreds to a few thousand.
3. HAZE BY DEPTH: anything below the surface lifts toward the lit medium and softens with z
   (aerial perspective). Today everything is at the surface, so this mainly drives motes and the
   floor; it is the "lacks a certain haziness" fix.
4. LATER STAGE (Fable's memo, reference/briefs/COACH-oil3d-fable.md): ONE height-field oil film
   (h from the sdf, sessile-cap profile) feeding refraction / caustics / Fresnel / CoC, replacing
   the six separate fake surfaces; caustics from the oil lenses onto the motes and the floor.

## Post ("a lot of post processing to read as real")
The live post stack already applies (it is FluidWallpaper's own). This round only: grain that
lives IN THE LIGHT (the shafts and lit haze carry it, the darks stay clean: the BD density work),
halation on the mote glints and the lit shafts, the existing camera softness/aberration. Grain
history and what was rejected: per the old chat's answer (it knows; "high-ISO artifacting" was
rejected).

## Stages (each = one build + a few renders + its proof; hand back after each)
- S1: light + motes. Proof: identity (fluid parity md5 10E36EBF1A74EDFE609065D757300054 and the
  acid identity set byte-identical with the new keys at default); then a 4-panel blind sheet of the
  live Magenta Cyan stage (base monotone-post-0924.ini + Layout - Few giants.ini + Scheme -
  Magenta Cyan.ini): off / motes only / motes + light at two elevations, plus a 300% crop of the
  same cluster; MAD numbers; one sentence each.
- S2: shadow shafts + haze (+ the optional floor). Same sheet format; HDR on AND off.
- S3: the height-field film + caustics (Fable). S4: post tuning for realism with the user.
Budget: S1+S2 under +1.0 ms at 1440p; every key gets its useful slider range.

## Rules
reference/briefs/EXECUTOR-CARD.md (build2 only, gpu.lock, the proof block, identity). Every exe
run carries --shot. Never touch the live FluidWallpaper.exe or Wallpaper Engine. GPU: the user
said full GPU is fine right now (yield 0); drop to yield 30 if told the user is gaming.

## C1 RULING (port chat, 2026-10-15 ~03:20, after the UE5 test) — BINDING, supersedes the floor-receiver text above
1. DEPTH IN FRAME HEIGHTS, not metres. "1 m" was an intuition; under a ~22 cm frame it puts every shadow
   off-screen. `tank_depth` 0..1 in frame heights (0 = today; useful 0.15-0.6; the morning sheet shows
   0.2 / 0.35 / 0.6). `light_elev` default 60 deg, `light_azim` slow drift. The light is a LAMP, not the sun:
   `light_size` (angular, deg) default ~8; penumbra = D * tan(light_size/2) (PCSS). Shadow offset = D * cot(elev).
   "Infinite" = the haze-only limit of the same keys.
2. THE FILM STAYS AT THE SURFACE, IN FOCUS, FLAT. The floor is NOT a colour source. Scene: the film at z=0
   (today's transmitting/lit layer, plateaus untouched), the oil on it, below it the water column (the medium)
   and a DARK floor (or none). The directional light passes through the translucent film (tinted by it), is
   blocked by the oil, and lights the column: shafts in the film's colour + motes, seen THROUGH the film via
   today's translucency path (oil_transparency / the ink under-layer), defocused by depth. Shadows live in that
   under-layer: beside each mass the column/floor is darker, offset D*cot(elev), soft by light_size, read through
   the film as a darker zone that does not break the plateau. Plus a weak direct shadow ON the film:
   `shadow_on_film` 0..1, default 0; the sheet shows 0 and 0.3. Shafts need a lit medium against a darker
   background, which the dark floor gives; the floor-lit case is NOT built.
3. UE ground truth carried into the analytic terms: offset D*cot(elev); penumbra ~ D*tan(light_size/2);
   in-scatter vs floor split by optical depth; shafts vanish over a lit floor. The footprint (the "whiting") =
   the lamp's cone on the film + the column's in-scatter, replacing lamp_grey / rise_bottom_light as the 0-state.
4. UE = ground-truth reference only. ONE rerun on the real dump with THIS config (depth 0.35 frame heights,
   film at the surface, dark floor, lamp 8 deg), then stop UE work. Morning report sentence: "1 m would put
   every shadow off-screen"; the user picks the depth by popup.

## B RE-RULING (port chat, 2026-10-15 ~05:40, after stageB-crop-massedge) — BINDING; do after C1 (first thing after the 07:00 reset)
1. CROSS-FADE, DON'T STACK. The transmission IS the lit edge band; the 2D thin-edge/penumbra band terms are its
   0-state fake. When oil3d_absorb > 0, scale those fake band terms down by the absorb share (a smooth 0..1 from
   the key) so the TOTAL band level at each sheet level matches today's 0.0859 within 3%; the WIDTH may change
   (lambda-driven physics). The 3% rule applies to the total band, never to transmission alone. The rim
   (rim_dark/rim_width) stays: it is the Fresnel grazing-angle darkening.
2. NO OUTLINE. Multiply T by (1 - rim_dark * rimProfile) so the transmission cannot peak under the rim; let it decay
   inward over lambda (oil_height_px). Acceptance on the 300% crop: the luma profile across the mass boundary =
   rim minimum -> ONE soft hump of width >= 6 px -> monotone decay into the core; no secondary peak narrower than
   3 px. A surviving line is a masking cut (wax-coverage mask or rim inset), not a key value: fix it.
3. CORES CLEAN AT EVERY LEVEL: T reaches 0 deep inside a mass (h -> 1; gate with massDeep if lambda leaves h short);
   at the sheet's highest level the dark patches move <= 0.5 code, no film texture in the cores. Drop any level
   that fails from the sheet rather than showing it.
4. DEFAULT STAYS 0 (byte for byte); the overlay preset carries the level; the morning popup asks the user
   LOW/MID/HIGH only. Re-render the B sheet with the cross-fade (same 4 columns, both configs, HDR on + off, band
   numbers per column).
