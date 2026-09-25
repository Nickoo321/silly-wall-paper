# SETTINGS — FluidWallpaper ini keys

Every key exposed as a slider or checkbox in the Settings window (src/settings.cpp `s_sliders`/`s_checks`).
Format: `[ini section] key` — range — what it does. Sections and order match the Settings window's category pages.

Sliders: 341. Checkboxes: 26 (includes one registry-backed item with no ini key).

## Sliders


### Simulation

- `[sim] vorticity` — 0 to 50 — Small-scale swirl. High = cauliflower billows, low = smooth streams
- `[sim] baroclinic` — 0 to 200 — Wakes bend around dye masses instead of cutting through. 0 = off
- `[sim] flow_speed` — 0.2 to 2 — Global current multiplier. Lower = slower evolution, same shapes
- `[sim] splat_radius` — 0.01 to 1 — Size of emitted blobs
- `[sim] density_diffusion` — 0.95 to 1 — How long dye lingers. Higher = longer trails
- `[sim] velocity_diffusion` — 0.95 to 1 — How long currents persist. Higher = smoother flow
- `[sim] pressure_diffusion` — 0 to 1 — Flow smoothing. Lower = sharper blob edges
- `[sim] pressure_iterations` — 10 to 60 — Solver quality. Rarely needs changing
- `[sim] decay_fast` — 0.5 to 1 — How fast faint haze clears. 1.0 = never (deep layers)
- `[sim] decay_threshold` — 0 to 0.3 — Below this brightness the fast decay acts
- `[sim] saturation_restore` — 0 to 1 — Re-saturates aging dye so old layers stay colorful
- `[sim] max_brightness` — 0.3 to 4 — Max dye brightness (hue-preserving clip)
- `[sim] dye_diffusion` — 0 to 0.5 — Blurs dye. 0 = sharp marbling, high = soft mush
- `[sim] gravity` — -200 to 200 — Dye-weighted gravity. Positive = ink sinks, negative = smoke rises. 0 = off
- `[sim] gravity_pow` — 0.5 to 3 — Higher = only dense cores fall, thin veils hang
- `[sim] gravity_blur` — 0 to 12 — Blurs the density gravity reads. Low = grid-scale fingering, high = whole lobes sink

### Performance

- `[general] fps_limit` — 30 to 260 — Frame rate cap

### Wanderers

- `[behavior] wanderer_count` — 1 to 8 — Number of autonomous emitters
- `[behavior] wanderer_speed` — 50 to 1200 — Emitter speed = current strength
- `[behavior] wanderer_brightness` — 0.05 to 1 — Paint per emitter per step
- `[behavior] wanderer_scale` — 0.1 to 0.9 — Roam area for circle / figure-8 paths
- `[behavior] wanderer_resume_delay` — 0 to 30 — Quiet time after your input before emitters resume

### Screen-fullness governor

- `[behavior] dark_floor` — 5 to 60 — Emitters pause when dark area falls below this
- `[behavior] dark_level` — 0.005 to 0.1 — Brightness that still counts as dark
- `[behavior] surv_dark_floor` — 0 to 40 — One survivor emitter paints until this darkness
- `[behavior] contrast_req` — 0 to 100 — Require a bright focal region, else emitters pause

### Separating dart

- `[behavior] dart_interval` — 1 to 30 — Seconds between piercing darts
- `[behavior] dart_speed` — 500 to 6000 — Dart travel speed

### HDR output

- `[hdr] peak_nits` — 0 to 1500 — HDR hot-spot target. 0 = match SDR
- `[hdr] knee` — 0.1 to 1.3 — Dye level where HDR highlight boost begins
- `[hdr] saturation` — 1 to 2 — Extra punch while Windows HDR is on
- `[hdr] brightness` — 0.8 to 1.5 — Extra punch while Windows HDR is on
- `[hdr] contrast` — 0.8 to 1.5 — Extra punch while Windows HDR is on

### Color grading (WE panel)

- `[color] post_saturation` — 0.5 to 2 — WE-style whole-frame filter
- `[color] post_contrast` — 0.5 to 2 — WE-style whole-frame filter
- `[color] post_brightness` — 0.5 to 1.5 — WE-style whole-frame filter
- `[color] post_hue` — 0 to 360 — Rotates all colors. Warning: rotates outside the hue band

### Mood cycling

- `[moods] dwell_minutes` — 1 to 30 — Minutes in a mood before switching
- `[moods] transition_seconds` — 2 to 60 — Seconds a mood change takes
- `[moods] jitter` — 0 to 0.5 — Random +/- on dwell time so switches feel organic

### Color wheel

- `[behavior] color_cycle_period` — 2 to 120 — Seconds for emitted hue to sweep its band
- `[color] hue_center` — 0 to 360 — Where on the color wheel emission lives (0=red 120=green 240=blue)
- `[color] hue_range` — 5 to 180 — Half-width of the emission band. 180 = full wheel
- `[color] hue_linger` — 0 to 0.45 — Fraction of each half-lap spent resting on one hue. 0 = off

### Hue shift bursts

- `[behavior] hueshift_step` — 10 to 180 — Palette rotation per burst step
- `[behavior] hueshift_linger` — 0 to 15 — Hold time between burst steps
- `[behavior] hueshift_glide` — 0.1 to 10 — Rotation speed of each step
- `[behavior] hueshift_burst_steps` — 1 to 12 — Steps before rotating home
- `[behavior] hueshift_off_time` — 0 to 120 — Quiet time between hue-shift bursts

### Idle splats

- `[behavior] idle_interval` — 0.5 to 30 — Seconds between random blob bursts
- `[behavior] idle_amount` — 1 to 30 — Blobs per burst
- `[behavior] idle_brightness` — 0.2 to 3 — Idle blob intensity (emitters paint at 0.15)

### Response curve (bright rims)

- `[color] curve_center` — 0.05 to 1 — Response curve shape: glowing rims when enabled
- `[color] curve_width` — 0.02 to 0.5 — Response curve shape: glowing rims when enabled
- `[color] curve_height` — 0.1 to 2 — Response curve shape: glowing rims when enabled

### Shadow floor (dark marbling)

- `[color] shadow_floor` — 0 to 0.25 — Lift near-black along its own hue so dark marbling stays visible
- `[color] shadow_knee` — 0.02 to 0.6 — Brightness range the lift fades over

### Liquid Acid

- `[liquid_acid] blob_count` — 16 to 128 — Oil blobs stepped and evaluated per pixel. Higher = busier and slower
- `[liquid_acid] disc_frac` — 0 to 0.5 — Share of blobs that are big flat discs
- `[liquid_acid] web_frac` — 0 to 0.6 — Share seeded in chains so they merge into veined webs
- `[liquid_acid] bubble_frac` — 0 to 0.8 — Share that are small round bubbles. Remainder = holes
- `[liquid_acid] hole_weight` — 0 to 3 — How hard negative blobs eat round holes out of the oil
- `[liquid_acid] size_bias` — 0.5 to 4 — Skews bubble/hole radii toward the small end
- `[liquid_acid] big_bias` — 0.2 to 2 — Skews disc/web radii toward the large end. Below 1 = mostly huge
- `[liquid_acid] threshold` — 0.1 to 1.2 — Field level of the oil surface. Higher = tighter, more separate blobs
- `[liquid_acid] support_scale` — 1.2 to 4 — How far a blob's influence reaches. Higher = blobs bridge from further apart
- `[liquid_acid] flow_gain` — 0 to 4 — How strongly the sim's velocity advects the blobs
- `[liquid_acid] curl_drift` — 0 to 0.01 — Analytic swirl on top, so oil still creeps in still water
- `[liquid_acid] repulsion` — 0 to 3 — Keeps blobs from collapsing into a single mass
- `[liquid_acid] rim_width` — 0.001 to 0.03 — Half-width of the dark rim band at the oil edge
- `[liquid_acid] rim_dark` — 0 to 1 — How dark the thin rim just inside the oil edge goes (no visible effect on the shipped preset)
- `[liquid_acid] rim_vary` — 0 to 1 — Break the rim up: low-frequency noise on its width and on the halo, so it thickens, thins and dies out along the edge (no visible effect on the shipped preset)
- `[liquid_acid] rim_ink_follow` — 0 to 1 — The bright halo is refracted ink: scale it by how bright the ink just outside the edge is (no visible effect on the shipped preset)
- `[liquid_acid] meniscus` — 0 to 1 — Thin bright ink-coloured halo just outside the oil's dark rim (no visible effect on the shipped preset)
- `[liquid_acid] meniscus_width` — 0.0005 to 0.01 — Half-width of the bright halo
- `[liquid_acid] swarm_holes` — 0 to 1 — Hundreds of water droplets trapped inside the oil (no visible effect on the shipped preset)
- `[liquid_acid] swarm_drops` — 0 to 1 — Small oil droplets floating on the open ink (no visible effect on the shipped preset)
- `[liquid_acid] swarm_clump` — 0 to 1 — 0 = even blanket of droplets, 1 = droplets only in patches (no visible effect on the shipped preset)
- `[liquid_acid] swarm_density` — 0.05 to 1 — Fraction of swarm cells that carry a droplet (no visible effect on the shipped preset)
- `[liquid_acid] swarm_scale_holes` — 6 to 90 — Cells per unit for the hole swarm. Higher = smaller, denser holes (no visible effect on the shipped preset)
- `[liquid_acid] swarm_scale_drops` — 6 to 90 — Cells per unit for the droplet swarm (no visible effect on the shipped preset)
- `[liquid_acid] refraction` — 0 to 0.2 — Shifts the ink seen through the thinning oil near edges
- `[liquid_acid] translucency` — 0 to 1 — How much the ink underneath modulates the oil fill
- `[liquid_acid] ink_shading` — 0 to 1 — How much of the fluid look's pseudo-3D shading survives. The refs are flat
- `[liquid_acid] ink_levels` — 2 to 24 — Flat colour plateaus in the ink. Low = poster, high = smooth (no visible effect on the shipped preset)
- `[liquid_acid] ink_soft` — 0.01 to 1 — 0 = hard steps between bands (no visible effect on the shipped preset)
- `[liquid_acid] ink_mix` — 0 to 1 — 0 = keep the normal fluid colours, 1 = full duotone ramp (no visible effect on the shipped preset)
- `[liquid_acid] ink_hue_vary` — 0 to 90 — Rotates the ramp by the dye's own hue so the ink still drifts (no visible effect on the shipped preset)
- `[liquid_acid] ink_complement_span` — 5 to 180 — How far the ink hue may wander from the oil's opposite. Small = strictly two-hue
- `[liquid_acid] hue_sweep_period` — 0 to 1800 — Cross-fade through the curated vivid palette list. Long settled stretches, short fades. 0 = fixed palette
- `[liquid_acid] sweep_count` — 1 to 12 — How many entries of the sweep list are used (the shipped list is 5; the tile9 set is 8)
- `[liquid_acid] hue_rotate_period` — 0 to 3600 — Continuously rotates the whole oil palette's hue. The ink is untouched, so mono ink stays grey and the holes stay black. 0 = off
- `[liquid_acid] oil_saturation` — 0 to 2 — Vividness of the oil palette, whichever source it came from. 1 = the authored colours
- `[liquid_acid] film_level` — 0 to 1 — Brightness of the flat oil film (the background sheet). 1 = today; ~0.05 = the film pitch black so the dyed masses/droplets carry the colour. The edge light (mass rim, oil glow, halo, lens highlights) keeps the full palette and the mass/droplet dye is not touched
- `[liquid_acid] oil_fluor` — 0 to 1 — The lamp makes the oil glow in its own saturated colour, strongest at the film's thin edge and near the lamp, with HDR headroom on that edge band. Added after Film level, so a dimmed film + fluorescence = dark sheet, glowing edges. 0 = today (brief BL)
- `[liquid_acid] oil_fluor_reach` — 0.3 to 2 — How far from the lamp the oil still fluoresces, in screen heights (Gaussian falloff). Low = only the lamp side glows, high = the whole frame. Inert while Oil fluorescence is 0
- `[liquid_acid] rise_speed` — 0 to 0.08 — Constant upward drift on every blob. 0.015 = one screen height in about 65 s. 0 = the shipped free drift
- `[liquid_acid] rise_wobble` — 0 to 1 — A lazy sideways sway on the way up, out of step from blob to blob
- `[liquid_acid] rise_stretch` — 0 to 1 — Moving blobs elongate along their travel and relax round as they slow. Small fast ones stretch most
- `[liquid_acid] rise_bottom_light` — 0 to 1 — Brightens the oil near the bottom of the frame and cools it toward the top, like a lamp heated from below
- `[liquid_acid] rise_parallax` — 0 to 1 — Small blobs rise, sway and follow the water more slowly, as if further away. Scaled by radius against the largest disc
- `[liquid_acid] rise_parallax_dim` — 0 to 1 — Far (small) blobs are slightly darker as a depth cue. Keep it subtle; past ~0.4 they read as a different palette
- `[liquid_acid] oil_drag` — 0 to 1 — Friction under the oil: ink beneath an island is brought to rest and deflected around its rim, so the water cannot stream in underneath
- `[liquid_acid] oil_dye_block` — 0 to 1 — Ink that ends up under the oil fades out over a second or two, so the oil reads as sitting ON the water. Droplet holes are not oil and keep their ink (no visible effect on the shipped preset)
- `[liquid_acid] oil_viscosity` — 0 to 1 — Thick liquid: blobs lag the water, accelerate and coast slowly, breathe and sway slowly, neck together over seconds, and the droplets stop jittering
- `[liquid_acid] mouse_oil_mode` — 0 to 2 — What the cursor does to the OIL (never to the ink). 0 = nothing at all, 1 = drags and parts it, 2 = a marbling comb that leaves streaks which round back up over about 3 s
- `[liquid_acid] mouse_oil_radius` — 0.02 to 0.5 — Reach of the push, or the length of the comb's band along the pointer path
- `[liquid_acid] mouse_oil_gain` — 0 to 3 — Strength multiplier for the push or the comb
- `[liquid_acid] post_chroma` — 0.5 to 2 — Scales the finished frame's colourfulness about its own brightness. About 1.2 restores what a transparent film costs
- `[liquid_acid] post_lift` — 0.5 to 1.6 — Brightness multiplier on the finished frame
- `[liquid_acid] ink_gain` — 0.3 to 3 — Maps dye brightness onto the ramp. Higher = more bright ink (no visible effect on the shipped preset)
- `[liquid_acid] seam_strength` — 0 to 1 — Dark seams where the dye gradient is steep (acrylic-pour edging) (no visible effect on the shipped preset)
- `[liquid_acid] seam_lo` — 0 to 0.5 — Gradient magnitude a seam starts at
- `[liquid_acid] grain` — 0 to 0.2 — Coarse animated grain over the whole frame (no visible effect on the shipped preset)
- `[liquid_acid] grain_scale` — 1 to 6 — Pixels per grain cell. 1 = fine, 4 = chunky macro-film
- `[liquid_acid] grain_shadow_weight` — 0 to 1 — Weights the grain by (1-luminance)^2 so flat bright oil stays clean
- `[liquid_acid] toe_tint` — 0 to 1 — Lifts the darkest pixels toward a dark version of the ink hue instead of neutral black
- `[liquid_acid] oil_edge_mode` — 0 to 1 — 0 = the film thins out over a wide band that scales with the blob size. 1 = every surface ends on the same hard isoline the droplets do, with only a thin meniscus
- `[liquid_acid] accent_mode` — 0 to 1 — Who may take the palette's 4th shade (the complement in the 80-20 palettes). 0 = whoever drew it, big masses included. 1 = only elements under the accent size, so the frame is one colour with small scattered accents
- `[liquid_acid] accent_max_r` — 0.05 to 1 — How small a blob has to be to carry the accent colour, as a fraction of the biggest disc. Anything larger uses the base shades
- `[liquid_acid] accent_frac` — 0 to 1 — Fraction of the small blobs that actually take it, so it stays an accent and not a rule
- `[liquid_acid] oil_penumbra` — 0 to 1 — The lamp is under the middle of the dish: the oil right beside a black mass is lit less, so it darkens and shifts hue a little over a soft band
- `[liquid_acid] oil_penumbra_px` — 1 to 40 — How far that shading reaches into the oil
- `[liquid_acid] oil_penumbra_hue` — -90 to 90 — Which way the less-lit pigment turns. Arbitrary but consistent for a look
- `[liquid_acid] oil_penumbra_dark` — 0 to 1 — How much level the band loses as well as hue
- `[liquid_acid] cellulose` — 0 to 1 — A fibrous, mottled macro texture on the surface: strands of the kind a cell body shows under a microscope, drifting with the rise. 0 = off
- `[liquid_acid] cellulose_ink` — 0 to 2 — How much of it lands on the black side. It fades out deep inside a black mass, so the strands read as a thin layer near its edge and never as a grey wash
- `[liquid_acid] cellulose_oil` — 0 to 2 — How much of it lands on the coloured film, where it only mottles the colour
- `[liquid_acid] cellulose_scale` — 8 to 140 — How long and how far apart the fibres are
- `[liquid_acid] cellulose_drift` — 0 to 1 — 1 = the texture travels with the rise, so it belongs to the masses; 0 = it is pinned to the screen
- `[liquid_acid] oil_edge_curve` — 0 to 1 — Reshapes the edge ramp from smoothstep to smootherstep, so the slide from oil colour to black arrives without a knee at either end. Same band width
- `[liquid_acid] oil_thin_edge` — 0 to 1 — The film thins at its edge: the colour slides toward the ink beneath and the disc fades over a band, instead of a hard cut-out with a stroked rim
- `[liquid_acid] oil_edge_frac` — 0.02 to 0.4 — That band as a fraction of each blob's own radius
- `[liquid_acid] oil_specular` — 0 to 1 — Broad soft highlight off the lens curvature and a slow thickness ripple (the rig is backlit, so keep it low)
- `[liquid_acid] oil_iridescence` — 0 to 1 — Thin-film hue shimmer indexed by film thickness, strongest at the thin edges
- `[liquid_acid] swarm_lens` — 0 to 1 — Trapped droplets become soft-edged holes in the film with a thin-oil fringe and a small highlight, not flat black discs (no visible effect on the shipped preset)
- `[liquid_acid] meniscus_from_ink` — 0 to 1 — The halo takes the colour and the brightness of the ink just outside the edge, and disappears (with the dark hairline) over dark ink
- `[liquid_acid] meniscus_film_mix` — 0 to 1 — The bright halo is the widest, strongest thing painted on a droplet's boundary, and its colour comes from the INK -- so no film-hue feature (a second hue, a seam reflect, the sweep) ever reaches it. Raised, the halo takes its colour from the FILM at that droplet instead, the same colour its lit rim already uses, so a hue2 seam colours the halos around it too, and the ink-brightness gate that hides the halo over black ink opens by the same amount -- that gate is there to stop a foreign INK colour landing on black ink, and over black ink it is what makes the halo invisible today. The band keeps its shape, its width and its position; the dark hairline is not touched
- `[liquid_acid] oil_glow` — 0 to 1 — Diffuse spill of the oil's own colour into the ink around it
- `[liquid_acid] refraction_width` — 0 to 40 — Rim-width multiplier for the band where the ink is seen bending under the oil (0 = the shipped 7)
- `[liquid_acid] oil_transparency` — 0 to 1 — The oil stops being a fill and becomes a coloured film: the ink's marbling reads through it, tinted by the oil's own hue
- `[liquid_acid] oil_absorb` — 0 to 6 — How deeply the film absorbs. Low = a pale wash, high = a deep saturated glass
- `[liquid_acid] oil_film_bump` — 0 to 1 — Slow noise on the film's thickness, so it has islands of thick and thin instead of one even pane
- `[liquid_acid] oil_refract_body` — 0 to 0.04 — Displaces what is seen through the whole film, not only its edge, so the marbling wobbles as it passes under
- `[liquid_acid] oil_ink_blur` — 0 to 1 — Softens the ink seen through the film, strongest where the film is thickest (no visible effect on the shipped preset)
- `[liquid_acid] droplets` — 0 to 4096 — Simulated droplets rendered INTO the oil surface: water trapped in the oil and oil droplets on the ink. Above 0 the procedural swarms are switched off
- `[liquid_acid] droplet_spawn_rate` — 0 to 300 — How fast new droplets come out of solution, biased to thin oil near an edge and to shearing flow
- `[liquid_acid] droplet_r_max` — 0.002 to 0.03 — Largest droplet, as a fraction of frame height. Clamped to one grid cell
- `[liquid_acid] droplet_bias` — 1 to 6 — Heavy tail toward small droplets
- `[liquid_acid] droplet_weight` — 0 to 3 — How hard a trapped water droplet punches through the oil, relative to the LOCAL field so it works in thick oil too
- `[liquid_acid] droplet_ink_frac` — 0 to 1 — Share of droplets that are oil sitting on the open ink instead of water trapped in the oil
- `[liquid_acid] droplet_life` — 0 to 600 — Seconds before a droplet dissolves (shrinking away, never popping). 0 = never
- `[liquid_acid] droplet_attract` — 0 to 2 — Short-range pull between droplets of the same kind, which is what makes them find each other and merge
- `[liquid_acid] droplet_merge` — 0.05 to 0.9 — Overlap fraction at which two droplets become one, area-conserving
- `[liquid_acid] droplet_ring_frac` — 0 to 1 — Share of new trapped droplets drawn as a thin dark RING with the oil showing through the middle -- the empty doubles
- `[liquid_acid] droplet_ring_width` — 0.02 to 0.6 — How thick a hollow droplet's dark band is, as a fraction of its radius
- `[liquid_acid] droplet_ring_lift` — 0 to 1 — How much brighter (and slightly thicker) the middle of a hollow droplet reads -- a lens, not a hole
- `[liquid_acid] droplet_ring_clump` — 0 to 1 — Rings attract each other and pack into rafts with a shared dark wall instead of merging
- `[liquid_acid] weather` — 0 to 1 — Slow seasons in the droplet population: spells of more rings, more solids, sparser, denser, so the frame never settles (needs conservation of mass for the density half)
- `[liquid_acid] weather_period_s` — 30 to 1200 — Mean seconds per weather phase
- `[liquid_acid] film_hue2` — -180 to 180 — How far off the palette's own hue the second dye is. Roughly complementary reads like the reference: cyan patches in a magenta film
- `[liquid_acid] film_hue2_amt` — 0 to 1 — How far the film goes toward that hue inside a patch. 0 = one colour, as before
- `[liquid_acid] film_hue2_scale` — 0.08 to 0.9 — Patch size as a fraction of the frame. A quarter to a half is the reference's scale
- `[liquid_acid] film_hue2_drift` — 0 to 2 — How fast the patches wander on their own, on top of being carried by the sim's flow
- `[liquid_acid] film_hue2_decay` — 0 to 2 — How fast the field falls back toward its base. Too low and the second hue spreads into a flat tint over minutes
- `[liquid_acid] film_hue2_wobble` — 0 to 60 — The contrast hue is not nailed to one angle: it wanders this many degrees either side of it, so a 180 deg pair lives in 170-190 and the combo keeps changing. Two sines in the golden ratio, so it never repeats
- `[liquid_acid] film_hue2_wobble_period` — 10 to 1800 — Seconds of the slower of the two wobble sines. It also re-aims on the rig's readjustment, with the lamp and the focus
- `[liquid_acid] film_hue2_seed_rows` — 0 to 8 — New patch colour is generated only in this many hidden rows BELOW the bottom edge and rises into view. 0 = the old behaviour, which laid new patches anywhere in the frame
- `[liquid_acid] film_hue2_rise` — 0 to 2 — Extra upward drift of the patches on top of the oil's own rise. Patches can never travel down the screen
- `[liquid_acid] film_hue3` — -180 to 180 — An optional third hue, taken off the other end of the SAME patch field so it costs nothing extra per pixel
- `[liquid_acid] film_hue3_amt` — 0 to 1 — 0 = off, which is the default
- `[liquid_acid] boundary_reflect_r` — 0 to 0.6 — How far a hue2 SEAM reaches into the droplets around it, as a fraction of the screen HEIGHT. 0 = as before: a droplet carries the seam's colour only while it is standing in the seam, about one droplet across. Raised, the lit rims, lens highlights and glow of droplets this far away rotate toward the seam's own hue, fading smoothly so the nearest stay strongest. The film itself never changes and nothing is blurred
- `[liquid_acid] boundary_reflect_amt` — 0 to 1 — How much of the seam's own hue a rim right beside it takes. 1 = the full seam colour. Only does anything where the reach above is above 0
- `[liquid_acid] crust_hue_mix` — 0 to 1 — How much of the hue shift the droplets INSIDE a dark mass take. 1 = the same as the film, which is the reference's cyan-lit specks in the black
- `[liquid_acid] droplet_mass_bias` — 0 to 1 — How much crust of small bubbles the dye masses carry, as a share of the film population. The film keeps everything it has -- this is added, not swapped
- `[liquid_acid] droplet_crust_density` — 1 to 4 — How dense the crust inside a mass is against the open film. 1 = no crust at all
- `[liquid_acid] droplet_crust_r` — 0.05 to 1 — Crust bubbles are small: droplet_r_max times this. They clump but never coalesce
- `[liquid_acid] mass_rim` — 0 to 1 — A thin bright refracted rim on the lamp side of a mass edge, so a mass reads as a translucent body instead of a flat black cut-out
- `[liquid_acid] shadow_amt` — 0 to 1 — The lamp finally blocks: every mass and every droplet darkens the film on the side away from it. 0 = the flat, shadowless frame this look had until now
- `[liquid_acid] shadow_len` — 0 to 0.3 — How far the longest shadow reaches, as a fraction of the screen height. A caster throws less than this in proportion to its own height, and Lamp z stretches it further
- `[liquid_acid] shadow_soft` — 0 to 1 — How fast the penumbra opens up with distance. 0 keeps the shadow as sharp at its tip as at the caster; 1 has it dissolve
- `[liquid_acid] droplet_racer_frac` — 0 to 0.5 — Share of the smallest trapped droplets that race upward, the way uber-small bubbles do in water (0 = off)
- `[liquid_acid] droplet_racer_speed` — 1 to 5 — How many times an ordinary droplet's climb a racer makes
- `[liquid_acid] droplet_racer_wobble` — 0 to 2 — Lateral zigzag / spiral on the way up, as a fraction of the climb
- `[liquid_acid] droplet_racer_r_max` — 0 to 0.01 — Only droplets at or below this radius race (0 = twice droplet_r_min)
- `[liquid_acid] droplet_ring_r_mul` — 1 to 6 — How many times droplet_r_max a BIG hollow ring may be (1 = today: rings are limited to the solid droplets' size range)
- `[liquid_acid] droplet_ring_big_frac` — 0 to 0.3 — Share of new rings that are big -- keep it small, a couple visible at a time is the point
- `[liquid_acid] droplet_coalesce` — 0 to 1 — Same-kind solid droplets that touch pour into one round droplet of the combined area instead of parking as a lumpy cluster of lobes (rings keep their foam walls)
- `[liquid_acid] droplet_coalesce_s` — 0 to 4 — Seconds the coalescence takes (0 = the old 0.18 s snap)
- `[liquid_acid] conserve_mass` — 0 to 1 — Nothing appears or vanishes at a visible size in the open: droplets enter and leave across the frame edges, blobs wrap only once their field is clear of the frame, and births swell from below the visible floor
- `[liquid_acid] spawn_grow_s` — 0 to 20 — Seconds a new droplet takes to swell from nothing to full size (0 = the old 0.34 s pop)
- `[liquid_acid] dissolve_s` — 0 to 20 — Seconds a droplet dying of old age takes to shrink away (0 = the old 0.34 s)
- `[liquid_acid] droplet_ring_wobble` — 0 to 1 — Stops a ring being a perfect circle: a seeded ellipse whose axis turns over a minute or two, plus a breathing 3-lobe wobble, and a wall that thins where the ring bulges. 0 = perfect circles
- `[liquid_acid] droplet_depth` — 0 to 1 — How far droplets and rings are spread in front of and behind the masses' own plane. 0 = everything at one depth, which is what the camera saw before. Needs [post] dof_max_px to be visible
- `[liquid_acid] depth_rise` — 0 to 1 — The far layer climbs faster than the near one, so depth reads in the motion as well as in the blur. Symmetric, so the average rise speed is unchanged
- `[liquid_acid] diffraction` — 0 to 1 — Light bends round anything narrow, so a feature smaller than the lens's point spread cannot reach full darkness: tiny droplets come out as soft grey dots, medium ones dark with soft edges, only the big masses go truly black. Never lifts the black under a mass
- `[liquid_acid] diffraction_px` — 0.2 to 8 — How wide that point spread is. A droplet about this radius comes out at two thirds of its full darkness; anything several times it is unaffected
- `[liquid_acid] droplet_lens` — 0 to 1 — Every droplet is a lens over the backlight: a thin bright refractive rim hugging its edge, a darker band just inside, a lighter middle, and a small specular. Crisp at the edge, gradual inside -- it adds no blur at all. 0 = flat fills
- `[liquid_acid] droplet_lens_centre` — 0 to 1 — How much lighter the middle of a droplet is than its shoulder. A hole lifts toward the film colour, an oil droplet toward the backlight. Dies out where the surface stops being curved, so a big mass stays flat and its black stays black
- `[liquid_acid] droplet_lens_band` — 0.5 to 12 — Width of the dark inner band and of the bright rim just outside it. Authored in pixels and floored by band_min, never scaled by the element, so a 4-px droplet carries the same crisp rim a big mass does
- `[liquid_acid] droplet_spec` — 0 to 1 — A small highlight on the side of each droplet facing the lamp. It reads the same rig the haze and the bloom do, so it swings when the lamp moves instead of sitting still
- `[liquid_acid] dye_depth` — 0 to 1 — Where the dye masses float. This used to be pinned to the focus distance, so a big mass sat on the plane of focus by definition and could never go soft on its own. Move it off Focus depth and the masses leave focus like everything else
- `[liquid_acid] dye_depth_tilt` — 0 to 1 — Tips the dye layer along the direction of the lamp, so it is not parallel to the focus surface: the two cross on a line instead of agreeing over a whole region, and that line travels as the lamp drifts
- `[liquid_acid] dye_depth_w` — 0.05 to 2 — How much the dye layer counts against the droplets where they overlap. Higher pulls a droplet's focus toward the film it sits in; 0.25 is the original blend
- `[liquid_acid] dye_hue` — 0 to 360 — The colour the dark masses take. With Dye follows film on, this is an OFFSET from the film's own hue, so the two turn together; with it off it is an absolute hue. 0..360 covers the circle either way
- `[liquid_acid] dye_sat` — 0 to 1 — How coloured the dark masses are. The full 0..1 is useful here because the dye sits at a very low value: even fully saturated it reads as a deep wax, not as a bright fill. 0 turns the dye OFF (today's neutral black), it is not a grey dye
- `[liquid_acid] dye_lum` — 0 to 0.5 — How much lamp the THIN edge of a mass passes; the thick core keeps about 40% of it, which is what reads as translucent wax. Range from the dye4 sheet: under ~0.10 the mass is still black, 0.28-0.34 is the deep wax, and past ~0.45 the mass stops reading as dark at all. 0 = today's black
- `[liquid_acid] dye_hue_follow` — 0 to 1 — 1 = Dye hue is an offset from the film's current hue, so the dye rotates with it under hue_rotate_period and the sweep and the pair stays designed. 0 = a fixed absolute hue
- `[liquid_acid] dye_masses` — 0 to 1 — How much dye the big masses (the gaps between oil blobs) take. 1 = today; 0 = masses pitch black while the droplets can still carry a dye of their own
- `[liquid_acid] dye_droplets` — 0 to 1 — How much dye the small droplet holes take. 1 = today (every droplet dyed like a mass, which reads 'bubbly'); 0 = droplets stay black and only the big masses carry colour. A droplet within a few px of a mass shares the mass colour (more with Dye smoke)
- `[liquid_acid] dye_droplet_hue` — -5 to 360 — The droplets' own dye hue, so masses and droplets can be two colours (main + accent). Below 0 = same as Dye hue. Follows the film like Dye hue when Dye follows film is on
- `[liquid_acid] dye_droplet_sat` — -0.05 to 1 — The droplets' own dye saturation. Below 0 = same as Dye saturation; 0 = droplets black
- `[liquid_acid] dye_droplet_lum` — -0.02 to 0.5 — The droplets' own dye level, same scale as Dye brightness. Below 0 = same as Dye brightness; 0 = droplets black
- `[liquid_acid] dye_smoke` — 0 to 1 — 0 = today's wax: the dye stops hard at the mass edge and is brightest at the thin rim. Up = the dye thickens gradually into the mass, leaks a little out under the thin film and breaks into slow wisps, so it reads as smoke in the body rather than a tinted disc. Masses only; droplets keep their own dye. Needs Dye brightness > 0
- `[liquid_acid] dye_lamp_follow` — 0 to 1 — 1 = today: the mass dye is lit by the lamp ramp and brightest at its thin edge. 0 = the dye ignores the lamp and the edge profile and sits at its core level everywhere -- a flat, dim ambient colour the lamp never lifts (brief BL). Inert while Dye brightness is 0
- `[liquid_acid] dark_sat` — 0 to 1 — Darker must mean MORE saturated: raises the chroma of the film, the masses and the droplets as their own level falls (k = 1 + value x (1 - level)), keeping their luminance exactly, clipped only at the gamut edge. 0 = today; inert on an element at level 1 (brief BP) (no visible effect on the shipped preset)
- `[liquid_acid] dye_lum_vary` — 0 to 0.5 — So the masses are not all one wax: the dye level becomes Dye brightness x (1 +- this). A mass a hole carved keeps one shade for its life; a big gap between oil blobs gets a slow gradient (about a quarter screen across) that rises with it. Subtle: 0.5 moves the mass level ~+-20%. 0 = today. Masses only; inert while Dye brightness is 0 (brief AG-b)
- `[liquid_acid] dye_hue_vary` — 0 to 60 — Hue spread: the dye sits at Dye hue +- up to this many degrees, per carved mass or as a slow gradient across a big gap (rises with it, no flicker). 25 = a hint, 50 = clearly two-tone; past ~60 it turns orange/teal mud (sheet). 0 = today. Masses only; inert while Dye brightness is 0 (brief AG-b)
- `[liquid_acid] dye_thick_hue` — -90 to 90 — Hue turns with depth into a mass, like real dye density: the thin edge keeps Dye hue and the thick core reaches Dye hue + this. Small masses stay edge-coloured, big ones get a core. 0 = today; inert while Dye brightness is 0 (brief AG-b)
- `[liquid_acid] dye_core` — 0.42 to 1 — How bright the thick core of a mass is next to its thin rim. 0.42 = today (the rim is the brightest part, the core ~42% of it: a lit wash); 1 = an even, opaque fill that reads as dense pigment. Pulls the mean level up with it, so lower Dye brightness to hold it. Less effect with Dye smoke up; with Dye follows the lamp at 1 the lamp ramp stays. Inert while Dye brightness is 0 (brief BR)
- `[liquid_acid] speckle` — 0 to 1 — Sparse dark dots hugging the oil/ink boundary
- `[liquid_acid] oil_hdr` — 0 to 1.4 — Drives the HDR highlight gain for oil pixels. 0 = inherit the ink's
- `[liquid_acid] ink_water` — 0 to 1 — 1 = the shared ink-in-water render (translucent veils) instead of flat bands

### Ink in water

- `[ink] density` — 0.5 to 8 — Beer-Lambert absorption. High = thin veils already opaque
- `[ink] chroma` — 0 to 3 — How much the dye's own hue tints the transmitted light
- `[ink] edge_strength` — 0 to 1 — Extra optical path where the density gradient is steep = sheets seen edge-on
- `[ink] edge_lo` — 0 to 0.5 — Gradient magnitude the fold darkening starts at
- `[ink] edge_hi` — 0 to 0.5 — Gradient magnitude the fold darkening saturates at
- `[ink] edge_scale` — 1 to 6 — Screen texels between the gradient taps
- `[ink] vignette` — 0 to 0.6 — Radial darkening of the backlit paper (paper mode only)
- `[ink] core_knee` — 0 to 1 — Opacity where the tint crosses from the thin-veil colour to the core colour
- `[ink] veil_floor` — 0 to 0.6 — Faint dye below this opacity goes to the background instead of a haze
- `[ink] tint_mid_dip` — 0 to 1 — Darken the duotone blend around its midpoint so mid-density ink is dark, not grey-brown
- `[ink] pair_sweep_period` — 0 to 900 — Cross-fade tint_thin/tint_thick through the curated complementary pairs. 0 = the fixed pair
- `[ink] hdr_core` — 0 to 1.4 — How hard dense cores drive the HDR highlight gain. Veils stay SDR
- `[ink] motion_lo` — 0 to 60 — Below this local speed, ink gets no HDR lift at all - keeps the entry patch from blowing out
- `[ink] motion_hi` — 0 to 200 — Above this local speed the HDR lift is full. Set at or below the still value to disable the gate
- `[ink] motion_opacity` — 0 to 1 — How much the motion gate also thins stationary ink. 0 = HDR lift only
- `[ink] parallax` — 0 to 1 — Adds the same dye at another scale as extra depth. 0 = off
- `[ink] parallax_scale` — 0.8 to 1 — How much bigger the second layer reads

### Drops

- `[drops] interval` — 2 to 90 — Seconds between ink drops (jittered +-35%)
- `[drops] speed` — 0 to 3000 — Downward velocity impulse. This is what rolls the head into a cap
- `[drops] radius` — 0.02 to 2 — Splat radius of the drop head, same units as splat radius
- `[drops] density` — 0.1 to 4 — Dye intensity of the head. 1.35 = a fully opaque core
- `[drops] tail_sec` — 0 to 4 — How long dye keeps feeding in at the entry point after the impulse
- `[drops] tail_density` — 0 to 1 — Intensity of that trail
- `[drops] spatter` — 0 to 12 — Satellite droplets around the entry (ref 2). Each one costs a full dye pass
- `[drops] spatter_radius` — 0.01 to 0.4 — Size of each satellite droplet
- `[drops] spatter_speed` — 0 to 2000 — Outward impulse of the satellites
- `[drops] tail_radius_frac` — 0.05 to 1 — Keep this small: a wide entry stamp reads as a bright orb parked at the injection point
- `[drops] tail_speed` — 0 to 1 — Downward impulse on the entry stream so it feeds the stem instead of parking
- `[drops] impulse_spread` — 0.5 to 4 — How much wider the velocity impulse is than the dye stamp. Below ~1.5 the dye's outer halo parks at the entry as a bright orb
- `[drops] asymmetry` — 0 to 1 — 0 = a textbook symmetric vortex pair. Higher = unequal lobes, one side leading
- `[drops] y_max` — 0.02 to 0.9 — How far down the screen a drop may enter

### Mirror

- `[mirror] mode` — 0 to 4 — Folds the picture about a centre. 3 = the 4-fold quad; 4 = wedges around the centre
- `[mirror] segments` — 2 to 16 — Number of wedges in mode 4. Every other one is reflected, so there is no jump at a wedge edge
- `[mirror] source` — 0 to 3 — Which quarter of the sim is the one you see, and is copied around: +1 = right half, +2 = bottom half
- `[mirror] center_x` — 0.1 to 0.9 — Where the vertical seam sits
- `[mirror] center_y` — 0.1 to 0.9 — Where the horizontal seam sits
- `[mirror] rotate_period` — 0 to 600 — Slowly turns the kaleidoscope's fold axes about the centre
- `[mirror] drift` — 0 to 1 — Slow wander of the fold point, so the seam is not glued to the middle of the screen
- `[mirror] soft` — 0 to 0.05 — Rounds the fold off over this band so the mirror crease is not a hard line. 0 = a hard mirror

### Post

- `[post] film_grain` — 0 to 1 — Animated film grain over the finished frame, weighted into the mids and darks and kept off the peaks and off true black
- `[post] film_grain_size` — 0.5 to 6 — Pixels per grain cell. 1 = per-pixel noise, larger = coarser stock
- `[post] film_grain_speed` — 0 to 2 — Multiplier on the grain frame rate; 1 = the film_grain_fps rate, lower holds each pattern longer
- `[post] film_grain_fps` — 1 to 240 — How many grain patterns per second: 24 or 30 for a film cadence (both divide 240 exactly), 240 = every refresh. Also paces film_noise
- `[post] film_grain_color` — 0 to 1 — 0 = monochrome grain, 1 = independent RGB noise
- `[post] film_grain_chroma` — 0 to 1 — 1 = the grain is an equal step on all three channels, which moves saturation and clips against black. 0 = it scales the pixel instead, so hue and saturation survive and black stays black
- `[post] film_grain_density` — 0 to 1 — 0 = grain at full amplitude from just above black upward. 1 = the stock own density curve: nothing in the dense shadow, most of it in the mid-tones, nothing on a clean highlight
- `[post] aberration` — 0 to 1 — A lens focuses red and blue at slightly different magnifications, so the channels land at different scales: red pushed out from the optical axis, blue pulled in. Nothing at the axis, a couple of pixels at the corners. Centred on the rig's lens, which drifts, so the clean spot never sits still
- `[post] aberration_px` — 0 to 6 — How far red and blue are displaced radially at the corners, in pixels at 1440p. Whether the fringe fades on defocused shapes is `aberration_coc`
- `[post] aberration_field` — 0 to 2 — How fast the split grows from the optical axis outward. 0 = nearly uniform across the frame, 2 = clean in the middle and all of it in the corners
- `[post] aberration_coc` — 0 to 1 — 0 = the same split everywhere, in focus or not. 1 = it fades with this pixel own blur, so the fringe lives on the sharp slice and disappears on an out-of-focus shape, which is what a real lens does
- `[post] vignette` — 0 to 1 — A gentle fall-off toward the corners -- a field stop, never a circle
- `[post] softness` — 0 to 6 — Defocuses the isolines themselves -- coverage band, film edge, rim and meniscus -- so no edge in the frame is razor-sharp. The grain stays sharp
- `[post] halo` — 0 to 1 — A soft bright glow hugging the outside of every dark shape, with a faint darker echo beyond it -- the microscope double contour (liquid_acid only)
- `[post] halo_px` — 1 to 40 — How wide that glow is. Wide and weak is the look; narrow and strong is a stroked line
- `[post] band_min` — 0 to 1 — Floors every band around an edge (film edge, rim, meniscus, halo, penumbra) at a few px, so a small droplet is shaded like a big mass instead of getting a solid outline. 0 = bands proportional to each element's size
- `[post] post_blur_px` — 0 to 4 — Image-space disc blur of the finished frame: every feature, whatever its size, gets the same lens defocus. 0 = off
- `[post] post_glow` — 0 to 1 — Weak wide veiling glare: the frame mixed with a wide blur of itself, so dark bleeds a little into bright and bright into dark around every edge, including a thin ring wall. 0 = off
- `[post] post_glow_px` — 2 to 40 — How far the glare spreads
- `[post] fog` — 0 to 1 — The water itself glows near the off-view lamp and fades with distance, added only into the dark. It falls to exactly zero far from the lamp, so black stays black. 0 = off
- `[post] fog_px` — 100 to 2000 — How far the glow of the water carries from the lamp
- `[post] fog_mass_gate` — 0 to 1 — 0 = the haze lands wherever it is dark. 1 = it is kept out of the inside of a dark mass, which floats in front of the water, and still glows in the water beside it
- `[post] artefact_lum_gate` — 0 to 1 — 0 = grain, lens fringe and the cover's sheen land on every mass alike. 1 = inside a mass they follow its own brightness and colour: a pure black mass stays clean, a dyed one keeps its texture, the edge band is untouched
- `[post] bloom` — 0 to 1 — The bright film bleeds a very wide, very weak wash into the black. Its radius breathes and the wash drifts with the lamp
- `[post] bloom_px` — 40 to 400 — How far that wash spreads
- `[post] bloom_warmth` — 0 to 1 — Brief BN. 0 = the wash is the colour of the film it came from. 1 = it takes the lamp's colour: hot yellow-white on the lamp's side, red away from it (the LAPD microscope veil). Luminance unchanged, so it does not brighten the frame
- `[post] light_x` — -1 to 2 — Where the off-view lamp sits, in screen coordinates. 0.5 = the middle, outside 0..1 = off-frame
- `[post] light_y` — -1 to 2 — 1.2 = just below the bottom edge, which is where the reference lamp is
- `[post] light_drift` — 0 to 1 — How far the lamp wanders on its own: a sum of slow sines over seconds to a minute, so the light is never static
- `[post] light_z` — -1 to 1 — How far the lamp stands off the plane of the dish. Above 0 it is in front of it and the cast shadows rake away from it, shortening as it rises; 0 is in the plane and throws the longest shadows; below 0 the lamp is behind the dish and every caster spills its shadow evenly onto the film in front of it
- `[post] film_dust` — 0 to 1 — Specks of dust on the film: sparse bright points, a new scattering every film frame. Additive and weighted into the dark, so they are stars on the black and nothing on the bright film
- `[post] film_hairs` — 0 to 1 — How often a curly hair is caught in the gate. Each one sticks for a few seconds, flutters, and is gone (no visible effect on the shipped preset)
- `[post] film_scratches` — 0 to 1 — Faint near-vertical scratches that persist for a stretch, drift sideways and disappear
- `[post] film_leak` — 0 to 1 — A coloured leak at one edge -- warm core, cool fringe, soft bands -- that swells and dies, and does not come back every time (no visible effect on the shipped preset)
- `[post] film_artefact_rate` — 1 to 30 — How long one population of hairs lasts. Scratches change three times slower, the leak five times slower, the dust every film frame
- `[post] film_noise` — 0 to 1 — A second, finer and faster noise layer under the coarse grain: the emulsion's own fizz as against the stock's grain structure
- `[post] film_noise_size` — 0.5 to 4 — Cell size of that finer layer, in px at 1440p
- `[post] film_stock` — 0 to 1 — The stock's own grade, applied last: lifted teal shadows, warm highlights, a slightly different curve per channel
- `[post] post_glow_dark` — 0 to 1 — Leans the glare toward the dark side: shadows of dark features bleed into the bright film more than the film's light bleeds into the black
- `[post] dof_max_px` — 0 to 16 — Turns the one global defocus into a real depth of field: each element is blurred by how far its own depth is from the plane of focus, up to this radius. 0 = off, and the whole per-pixel path is skipped. liquid_acid only
- `[post] camera_focus` — 0 to 1 — Which depth is sharp. 0.5 is the plane the big masses sit on; lower favours the front droplets, higher the back ones
- `[post] camera_field_curve` — 0 to 2 — Bends the surface of focus away from the dish with distance from the optical axis, so the centre and the corners of the frame cannot both be sharp -- what a real lens does
- `[post] corner_warp` — 0 to 1 — Brief BN. The microscope's field edge: outside the start radius the corners are stretched along the arcs round the centre, so they look oddly distorted while the middle stays exactly as it was. 0 = off
- `[post] corner_warp_r` — 0.4 to 0.9 — Where the warp begins: 0.4 = just outside the middle 40% of the frame, 0.9 = only the very corners (1 = a corner)
- `[post] focus_tilt` — 0 to 2 — Slants the surface of focus (Lensbaby / freelensing): a strip of sharpness across the frame with the focus falling off smoothly to either side. 0 = level
- `[post] focus_tilt_angle` — -180 to 180 — Which way that strip runs. With a readjustment period set, this is the angle it is re-aimed around
- `[post] focus_band_px` — 40 to 1200 — How wide the sharp strip is on screen. Wide is a gentle depth of field, narrow is the freelensing sliver
- `[post] focus_tilt_period` — 0 to 300 — Mean seconds the focus plane holds still before somebody re-tilts the lens. Randomised around this, so it never feels scheduled. 0 = the lens is bolted down (no visible effect on the shipped preset)
- `[post] focus_tilt_move_s` — 0.2 to 4 — How long one readjustment takes. A second or two, eased, with a slight overshoot and settle -- a hand letting go of a lens barrel
- `[post] camera_fov` — 0 to 90 — 0 = an orthographic scanner, every ring a symmetric circle. Above 0 the dish is seen from a lens at the tip of a view cone: off-axis rings foreshorten toward the axis, their far wall reads thicker and their highlight swings to the side facing the axis
- `[post] camera_axis_x` — 0 to 1 — Where the optical axis meets the dish: the one point seen face on, and the centre the field curvature and the tilt are measured from
- `[post] camera_axis_y` — 0 to 1 — The other half of that point
- `[post] psf_px` — 0 to 6 — The floor under the defocus radius, applied whatever the focus: no lens resolves a point to a point, so the sharpest thing in the frame is still this wide. At 1 px and up an in-focus edge can never come out as stair-stepped coverage AA
- `[post] dither` — 0 to 4 — Half an LSB of ordered blue-ish noise on the finished frame, in the domain the panel quantises in. Breaks the 10-bit steps that show as bands on a big saturated flat. It fades out into true black, so an off pixel stays off. 1 = half an LSB
- `[post] halation` — 0 to 1 — CineStill's missing anti-halation layer: a tight warm glow bleeding out of the bright film into the dark around it. Taken only from what is genuinely bright, and it never lands on the highlight itself. Much tighter than Bloom, which is the wide weak wash
- `[post] halation_px` — 4 to 60 — How far the glow reaches. A dozen pixels is the film look; far more and it becomes a second bloom
- `[post] lid` — 0 to 1 — Master for the transparent cover over the dish: its internal reflections, sheen, glint and iridescence. 0 = no cover at all
- `[post] lid_ghost` — 0 to 1 — Dim, offset, slightly magnified copies of the bright film reflected inside the cover, tinted by the coating they bounced off
- `[post] lid_ghost_spread` — 0 to 2 — How far the ghost chain runs along the line from the lamp reflection through the optical centre
- `[post] lid_rings` — 0 to 1 — Concentric coloured arcs -- the field reflected off a curved element (the LAPD optics look)
- `[post] lid_sheen` — 0 to 1 — A very wide, very weak warm smear where the lamp catches the cover
- `[post] lid_sheen_px` — 40 to 1400 — Width of that smear, in px at 1440p (no visible effect on the shipped preset)
- `[post] lid_glint` — 0 to 1 — The lamp's own reflection in the cover: a soft core with a wide amber halo
- `[post] lid_iris` — 0 to 1 — Interference colours of the thin oil film on the cover, visible only across the sheen
- `[post] lid_refract_px` — 0 to 12 — How much the uneven cover wobbles its own reflections (the transmitted picture is left alone)
- `[post] lid_scratch` — 0 to 1 — Wear on the cover: hairline scratches fixed to the lid that catch the lamp only where they run across its light, so the pattern shifts as the lamp drifts. Clear over the dark, nearly gone on the bright film. 0 = off (needs Lid above 0)
- `[post] lid_scratch_density` — 0 to 1 — How many fine scratches there are where the wear is
- `[post] lid_scratch_len` — 0 to 1 — 0 = only short micro-swirls. Higher adds up to eight long straight gouges across the cover
- `[post] lid_scratch_corner` — 0 to 1 — Where the wear sits: 0 = all over the cover, 1 = only in the corners
- `[post] lid_scratch_soft` — 0 to 1 — 0 = crisp 1 px hairlines that catch the light over a narrow angle; higher = wider, dimmer grooves that catch it over a wider one
- `[post] lid_scratch_tint` — 0 to 1 — 0 = neutral white. 1 = the scratch takes the colour of the film under it (over black it stays white)
- `[post] glass_streaks` — 0 to 1 — Brief BN. Two thin bright wavy lines on the cover glass (the LAPD sheet), brightest past the lamp's reflection. They ride the lid's drift and turn, so they never sit still. Needs Lid above 0. Not scaled by it
- `[post] halation_warmth` — 0 to 1 — 0 = a colourless glow. 1 = the red-orange of light that has crossed the emulsion twice, which is the CineStill signature
- `[post] halation_threshold` — 0 to 0.6 — Brief BN. 0 = only the genuinely bright film halates. Higher lowers the knee so the mid-tones scatter too and halation becomes a diffusion haze (pro-mist): the picture stays sharp and is veiled, never blurred. Pair with a wider halation radius
- `[post] shimmer` — 0 to 1 — The air above the lamp. A very fine refractive wobble, strongest near the light and fading to nothing away from it, and carried by the sim's own velocity -- a burst that shoves the oil shoves the heat above it too
- `[post] shimmer_px` — 0 to 6 — How far the wobble displaces the picture at its strongest. A pixel or two is heat; more is water
- `[post] vignette_wander` — 0 to 1 — Lets the vignette's centre follow the rig's lens, so the darkest corner turns over minutes instead of sitting in one corner of the panel for hours. 0 = pinned to the middle
- `[post] pixel_shift_px` — 0 to 8 — The OLED safety net under everything else: the whole finished frame walks a slow closed orbit of this radius, a few thousandths of a pixel per frame, so no feature ever holds one pixel. Invisible, and still moving
- `[post] rig_readjust` — 0 to 1 — How far the lamp and the lens centre re-aim when the focus readjusts. 0 = only focus and tilt move; above 0 the whole rig moves as one body on the same eased spring, which is the point of having a rig (no visible effect on the shipped preset)

## Checkboxes


### Behaviors

- `[behavior] wanderers` — on/off — Autonomous roaming emitters
- `[behavior] auto_pause` — on/off — Stop painting when the field is full
- `[behavior] dart_enabled` — on/off — Periodic shot that splits merged blobs
- `[behavior] hueshift_enabled` — on/off — Palette rotation bursts (full-wheel moods only)
- `[behavior] idle_splats` — on/off — Random blobs when the field is calm
- `[sim] shading` — on/off — Pseudo-3D emboss on dye edges

### Mouse

- `[behavior] hold_to_splat` — on/off — Hold the left button to pour a continuous dye stream
- `[behavior] splat_on_click` — on/off — Each click splats a single dye blob
- `[behavior] show_mouse` — on/off — Moving the mouse pushes currents through the fluid

### Color source

- `[color] colorful` — on/off — Emit from the hue band instead of the fixed palette
- `[color] more_colors` — on/off — Splats cycle all five palette colors instead of one
- `[hdr] compensation` — on/off — Apply HDR boosts when Windows HDR is on
- `[color] curve_enabled` — on/off — Enable the brightness hump curve

### System

- `[general] pause_on_fullscreen` — on/off — Pause the wallpaper while a fullscreen app has focus
- `[general] pause_on_maximized` — on/off — Pause the wallpaper while a maximized window has focus
- `[moods] enabled` — on/off — Auto-switch between moods/*.ini recipes
- `[general] mirror_second` — on/off — Also render the wallpaper on the second monitor
- `[liquid_acid] ink_complement_lock` — on/off — Hold the ink's hue on the far side of the wheel from the oil (no visible effect on the shipped preset)
- `[liquid_acid] rise_respawn` — on/off — With a rise speed set, a blob that climbs off the top comes back in under the bottom edge at a new place and size, instead of reappearing where it left
- `[liquid_acid] rim_order` — on/off — Forces the dark rim and the bright halo adjacent and ordered across the isoline, instead of wherever rim_inset/meniscus_offset put them
- `[look] liquid_acid` — on/off — Oil-on-inked-water render look. Switches live (first switch costs one shader compile)
- `[look] ink` — on/off — Beer-Lambert ink-in-water render look. Switches live (first switch costs one shader compile)
- `[ink] inverted` — on/off — Off = dark ink on backlit paper. On = pale ink on black (OLED-friendly)
- `[drops] drops` — on/off — Periodic falling ink drops. Works with any render look
- `[drops] obey_governor` — on/off — Skip a drop while the water is already full of ink
- `Start with Windows` — on/off — registry autostart entry (HKCU Run key); not an ini key.
