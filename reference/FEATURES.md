# Fluid Wallpaper — feature manual

This one app renders three looks selected by `[look] style` in the ini: `fluid` (the native
GPU dye simulation), `liquid_acid` (oil floating on inked water — the tray's "Liquid Acid"
presets), and `ink` (Beer-Lambert ink-in-water). Every knob below lives in one ini, read once
at startup and re-read live where the code supports it.

Day to day, settings are edited from the tray's Settings window (`src/settings.cpp`), which
writes straight back to `settings.ini` next to the executable. Saved looks are full ini
snapshots kept as presets, checked into the repo under `reference/presets/` (one file per
tray preset, `Liquid Acid - ...`, `Ink - ...`, etc.) so a look can be restored exactly. The
preset actually running right now — the one this document's "Live" column is read from — is
`reference/configs/acid-rise-12.ini`, the "lava lamp" tile9 rise look.

This file is generated from the slider/checkbox tables in `src/settings.cpp`, cross-checked
against every `getF`/`getI`/`getB` read in `src/main.cpp` and the field initialisers in
`src/fluid.h` (and `src/moods.h` for mood settings), on **2026-09-22** at `main` `afdeed1`,
updated **2026-09-23** at `main` `9999eba` to fold in the day's remaining landings (AJ boundary
reflect, BC shadows, AG/AM dye, BD noise/aberration, meniscus_film_mix) and the two 2026-09-22
audits' inert-key findings.
A blank Live cell means the key is absent from `acid-rise-12.ini` (the code falls back to its
fluid.h default). Rows marked *(no slider)* have no control in the settings window at all —
they were found only by grepping `main.cpp`'s ini reads, and can only be set by hand-editing
the ini.

## 1. Fluid sim base

The underlying GPU fluid simulation and its native look, before either alternate render style is switched on: vorticity/curl, dye and velocity diffusion, gravity, the emitted colour band, wanderers/idle splats/hue-shift bursts that keep it moving on their own, the HDR grading applied to the base look, and the three-way style switch itself (`[look] style = fluid | liquid_acid | ink`).

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[sim] vorticity` | Vorticity (swirl strength) | 0..50, step 0.5 | 48.0 | 12 | Small-scale swirl. High = cauliflower billows, low = smooth streams |
| `[sim] baroclinic` | Form resistance (baroclinic) | 0..200, step 5 | 0.0 | 0.000 | Wakes bend around dye masses instead of cutting through. 0 = off |
| `[sim] flow_speed` | Flow speed (all currents) | 0.2..2, step 0.05 | 1.0 | 1.00 | Global current multiplier. Lower = slower evolution, same shapes |
| `[sim] splat_radius` | Splat radius | 0.01..1, step 0.005 | 0.64 | 0.64 | Size of emitted blobs |
| `[sim] density_diffusion` | Density diffusion (dye linger) | 0.95..1, step 0.0001 | 0.999 | 0.999 | How long dye lingers. Higher = longer trails |
| `[sim] velocity_diffusion` | Velocity diffusion | 0.95..1, step 0.0001 | 0.999 | 0.999 | How long currents persist. Higher = smoother flow |
| `[sim] pressure_diffusion` | Pressure diffusion | 0..1, step 0.005 | 0.85 | 0.85 | Flow smoothing. Lower = sharper blob edges |
| `[sim] pressure_iterations` | Pressure iterations | 10..60, step 1 | 20 | 20 | Solver quality. Rarely needs changing |
| `[sim] decay_fast` | Tail decay speed (lower=snappier) | 0.5..1, step 0.002 | 1.0 | 1.000 | How fast faint haze clears. 1.0 = never (deep layers) |
| `[sim] decay_threshold` | Decay threshold | 0..0.3, step 0.002 | 0.29 | 0.290 | Below this brightness the fast decay acts |
| `[sim] saturation_restore` | Saturation restore /s | 0..1, step 0.005 | 0.93 | 0 | Re-saturates aging dye so old layers stay colorful |
| `[sim] max_brightness` | Color intensity cap | 0.3..4, step 0.05 | 1.35 | 1.35 | Max dye brightness (hue-preserving clip) |
| `[sim] dye_diffusion` | Dye diffusion (smoke spread) | 0..0.5, step 0.005 | 0.0 | 0.000 | Blurs dye. 0 = sharp marbling, high = soft mush |
| `[sim] gravity` | Gravity (dye sinks / rises) | -200..200, step 1 | 0.0 | 0 | Dye-weighted gravity. Positive = ink sinks, negative = smoke rises. 0 = off |
| `[sim] gravity_pow` | Gravity power (rho^p) | 0.5..3, step 0.1 | 1.5 | 1.5 | Higher = only dense cores fall, thin veils hang |
| `[sim] gravity_blur` | Gravity smoothing (sim texels) | 0..12, step 0.5 | 3.0 | 3 | Blurs the density gravity reads. Low = grid-scale fingering, high = whole lobes sink |
| `[behavior] wanderer_count` | Count | 1..8, step 1 | 2 | 2 | Number of autonomous emitters |
| `[behavior] wanderer_speed` | Speed (px/s) | 50..1200, step 10 | 246.0 | 246 | Emitter speed = current strength |
| `[behavior] wanderer_brightness` | Brightness | 0.05..1, step 0.01 | 0.1 | 0.10 | Paint per emitter per step |
| `[behavior] wanderer_scale` | Path size (circle / figure-8) | 0.1..0.9, step 0.01 | 0.1 | 0.10 | Roam area for circle / figure-8 paths |
| `[behavior] wanderer_resume_delay` | Resume after idle (s) | 0..30, step 0.5 | 4.5 | 4.5 | Quiet time after your input before emitters resume |
| `[behavior] dark_floor` | Min dark area % before pause | 5..60, step 1 | 9.0 | 9 | Emitters pause when dark area falls below this |
| `[behavior] dark_level` | Dark pixel cutoff | 0.005..0.1, step 0.005 | 0.07 | 0.070 | Brightness that still counts as dark |
| `[behavior] surv_dark_floor` | Survivor wanderer dark floor % | 0..40, step 1 | 8.0 | 8 | One survivor emitter paints until this darkness |
| `[behavior] contrast_req` | Contrast required % (0 = off) | 0..100, step 1 | 30.0 | 30 | Require a bright focal region, else emitters pause |
| `[behavior] dart_interval` | Interval (s) | 1..30, step 1 | 7.0 | 7 | Seconds between piercing darts |
| `[behavior] dart_speed` | Speed (px/s) | 500..6000, step 50 | 967.0 | 967 | Dart travel speed |
| `[hdr] knee` | Knee (boost starts at) | 0.1..1.3, step 0.02 | 0.6 | 0.70 | Dye level where HDR highlight boost begins |
| `[hdr] saturation` | Saturation boost | 1..2, step 0.01 | 1.2 | 1.20 | Extra punch while Windows HDR is on |
| `[hdr] brightness` | Brightness boost | 0.8..1.5, step 0.01 | 1.08 | 1.08 | Extra punch while Windows HDR is on |
| `[hdr] contrast` | Contrast | 0.8..1.5, step 0.01 | 1.0 | 1.00 | Extra punch while Windows HDR is on |
| `[color] post_saturation` | Saturation | 0.5..2, step 0.01 | 1.0 | 1.14 | WE-style whole-frame filter |
| `[color] post_contrast` | Contrast | 0.5..2, step 0.01 | 1.0 | 1.34 | WE-style whole-frame filter |
| `[color] post_brightness` | Brightness | 0.5..1.5, step 0.01 | 1.0 | 1.06 | WE-style whole-frame filter |
| `[behavior] color_cycle_period` | Cycle time (s per lap) | 2..120, step 1 | 19.0 | 19 | Seconds for emitted hue to sweep its band |
| `[color] hue_center` | Hue band center (deg) | 0..360, step 1 | 0.0 | 0 | Where on the color wheel emission lives (0=red 120=green 240=blue) |
| `[color] hue_range` | Hue band range (180 = full wheel) | 5..180, step 1 | 180.0 | 180 | Half-width of the emission band. 180 = full wheel |
| `[color] hue_linger` | Hue linger (rest at band edges) | 0..0.45, step 0.05 | 0.0 | 0 | Fraction of each half-lap spent resting on one hue. 0 = off |
| `[behavior] hueshift_step` | Step (deg) | 10..180, step 1 | 83.0 | 83 | Palette rotation per burst step |
| `[behavior] hueshift_linger` | Linger (s) | 0..15, step 0.5 | 6.5 | 6.5 | Hold time between burst steps |
| `[behavior] hueshift_glide` | Glide (s) | 0.1..10, step 0.1 | 7.2 | 7.2 | Rotation speed of each step |
| `[behavior] hueshift_burst_steps` | Steps per burst | 1..12, step 1 | 2 | 2 | Steps before rotating home |
| `[behavior] hueshift_off_time` | Off time between bursts (s) | 0..120, step 1 | 10.0 | 10 | Quiet time between hue-shift bursts |
| `[color] curve_center` | Hump center (input brightness) | 0.05..1, step 0.01 | 0.30 |  | Response curve shape: glowing rims when enabled |
| `[color] curve_width` | Hump width | 0.02..0.5, step 0.01 | 0.10 |  | Response curve shape: glowing rims when enabled |
| `[color] curve_height` | Hump height (output brightness) | 0.1..2, step 0.05 | 1.3 |  | Response curve shape: glowing rims when enabled |
| `[color] shadow_floor` | Shadow floor (colour lift) | 0..0.25, step 0.005 | 0.0 | 0.100 | Lift near-black along its own hue so dark marbling stays visible |
| `[color] shadow_knee` | Shadow knee (lift range) | 0.02..0.6, step 0.01 | 0.15 | 0.15 | Brightness range the lift fades over |
| `[behavior] wanderers` | Auto wanderer splats | on/off | true | 0 | Autonomous roaming emitters |
| `[behavior] auto_pause` | Auto-pause when screen full | on/off | true | 1 | Stop painting when the field is full |
| `[behavior] dart_enabled` | Separating dart while paused | on/off | true | 0 | Periodic shot that splits merged blobs |
| `[behavior] hueshift_enabled` | Hue shift cycler | on/off | true | 0 | Palette rotation bursts (full-wheel moods only) |
| `[sim] shading` | Shading | on/off | true | 1 | Pseudo-3D emboss on dye edges |
| `[color] colorful` | Random color (hue wheel) | on/off | true | 1 | Emit from the hue band instead of the fixed palette |
| `[color] more_colors` | Use all 5 palette colors | on/off | true | 1 | Splats cycle all five palette colors instead of one |
| `[hdr] compensation` | HDR compensation (sat/brightness) | on/off | true | 1 | Apply HDR boosts when Windows HDR is on |
| `[color] curve_enabled` | Response curve (bright rims) | on/off | false | 0 | Enable the brightness hump curve |
| `[look] liquid_acid` | Liquid Acid look | on/off | false |  | Oil-on-inked-water render look. Switches live (first switch costs one shader compile) |
| `[look] ink` | Ink in water look | on/off | false |  | Beer-Lambert ink-in-water render look. Switches live (first switch costs one shader compile) |
| `[behavior] wanderer_mode` | *(no slider)* | — | 0 | 0 | No control in the settings window — read from the ini as an int (`main.cpp`), no help text in `settings.cpp`. |
| `[sim] dye_res` | *(no slider)* | — | 1024 | 4096 | No control in the settings window — read from the ini as an int (`main.cpp`), no help text in `settings.cpp`. |
| `[sim] sim_res` | *(no slider)* | — | 256 | 256 | No control in the settings window — read from the ini as an int (`main.cpp`), no help text in `settings.cpp`. |
| `[color] splat_color_1` | *(no slider — colour picker button, not a slider)* | — | see splatColors[15] in fluid.h |  | One of the base fluid look's 5 fixed palette colours (r g b, 0..1). Set from the settings window's colour pickers; not read via getF/I/B so it never showed up in the ini-read grep. |
| `[color] splat_color_2` | *(no slider — colour picker button, not a slider)* | — | see splatColors[15] in fluid.h |  | One of the base fluid look's 5 fixed palette colours (r g b, 0..1). Set from the settings window's colour pickers; not read via getF/I/B so it never showed up in the ini-read grep. |
| `[color] splat_color_3` | *(no slider — colour picker button, not a slider)* | — | see splatColors[15] in fluid.h |  | One of the base fluid look's 5 fixed palette colours (r g b, 0..1). Set from the settings window's colour pickers; not read via getF/I/B so it never showed up in the ini-read grep. |
| `[color] splat_color_4` | *(no slider — colour picker button, not a slider)* | — | see splatColors[15] in fluid.h |  | One of the base fluid look's 5 fixed palette colours (r g b, 0..1). Set from the settings window's colour pickers; not read via getF/I/B so it never showed up in the ini-read grep. |
| `[color] splat_color_5` | *(no slider — colour picker button, not a slider)* | — | see splatColors[15] in fluid.h |  | One of the base fluid look's 5 fixed palette colours (r g b, 0..1). Set from the settings window's colour pickers; not read via getF/I/B so it never showed up in the ini-read grep. |

## 2. Liquid acid sim

The population and physics of the "oil on inked water" look: how many blobs/discs/webs/bubbles/holes exist and how big, how they drift, rise, merge, coalesce, race, wrap and drag the ink beneath them, the droplet/ring/crust particle counts, conservation of mass, the weather cycle, and the mouse's push/comb effect on the oil.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[liquid_acid] blob_count` | Blob count | 16..128, step 1 | 96 | 96 | Oil blobs stepped and evaluated per pixel. Higher = busier and slower |
| `[liquid_acid] disc_frac` | Big discs (fraction) | 0..0.5, step 0.01 | 0.22 | 0.22 | Share of blobs that are big flat discs |
| `[liquid_acid] web_frac` | Web blobs (fraction) | 0..0.6, step 0.01 | 0.42 | 0.34 | Share seeded in chains so they merge into veined webs |
| `[liquid_acid] bubble_frac` | Bubbles (fraction) | 0..0.8, step 0.01 | 0.22 | 0.20 | Share that are small round bubbles. Remainder = holes |
| `[liquid_acid] hole_weight` | Hole bite (negative weight) | 0..3, step 0.05 | 0.75 | 0.608 | How hard negative blobs eat round holes out of the oil |
| `[liquid_acid] size_bias` | Bubble size bias (higher = tiny) | 0.5..4, step 0.1 | 2.0 | 2.0 | Skews bubble/hole radii toward the small end |
| `[liquid_acid] big_bias` | Disc size bias (lower = bigger) | 0.2..2, step 0.05 | 0.55 | 0.62 | Skews disc/web radii toward the large end. Below 1 = mostly huge |
| `[liquid_acid] threshold` | Surface level (higher = smaller) | 0.1..1.2, step 0.01 | 0.50 | 0.85 | Field level of the oil surface. Higher = tighter, more separate blobs |
| `[liquid_acid] support_scale` | Stickiness (support radius) | 1.2..4, step 0.05 | 2.20 | 2.00 | How far a blob's influence reaches. Higher = blobs bridge from further apart |
| `[liquid_acid] flow_gain` | Flow gain (fluid drags the oil) | 0..4, step 0.05 | 1.15 | 1.15 | How strongly the sim's velocity advects the blobs |
| `[liquid_acid] curl_drift` | Curl drift | 0..0.01, step 0.0002 | 0.0016 | 0.0016 | Analytic swirl on top, so oil still creeps in still water |
| `[liquid_acid] repulsion` | Repulsion (same-sign blobs) | 0..3, step 0.05 | 0.55 | 0.90 | Keeps blobs from collapsing into a single mass |
| `[liquid_acid] swarm_holes` | Hole swarm (bubbles in the oil) | 0..1, step 0.02 | 0.90 | 0 | Hundreds of water droplets trapped inside the oil -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_drops` | Droplet swarm (oil on the ink) | 0..1, step 0.02 | 0.55 | 0 | Small oil droplets floating on the open ink -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_clump` | Swarm clumping | 0..1, step 0.02 | 0.70 | 0.85 | 0 = even blanket of droplets, 1 = droplets only in patches -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_density` | Swarm density | 0.05..1, step 0.02 | 0.55 | 0.2674 | Fraction of swarm cells that carry a droplet -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_scale_holes` | Swarm hole size (higher=smaller) | 6..90, step 1 | 26.0 | 22 | Cells per unit for the hole swarm. Higher = smaller, denser holes -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_scale_drops` | Swarm droplet size (higher=small) | 6..90, step 1 | 34.0 | 30 | Cells per unit for the droplet swarm -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] rise_speed` | Rise speed (lava lamp, uv/s) | 0..0.08, step 0.001 | 0.0 | 0.0195 | Constant upward drift on every blob. 0.015 = one screen height in about 65 s. 0 = the shipped free drift |
| `[liquid_acid] rise_wobble` | Rise wobble | 0..1, step 0.05 | 0.0 | 0.5 | A lazy sideways sway on the way up, out of step from blob to blob |
| `[liquid_acid] rise_stretch` | Rise stretch (teardrops) | 0..1, step 0.05 | 0.0 | 0.45 | Moving blobs elongate along their travel and relax round as they slow. Small fast ones stretch most |
| `[liquid_acid] rise_bottom_light` | Lamp base light | 0..1, step 0.05 | 0.0 | 0.5 | Brightens the oil near the bottom of the frame and cools it toward the top, like a lamp heated from below |
| `[liquid_acid] rise_parallax` | Rise parallax (small = far) | 0..1, step 0.05 | 0.0 | 0.7 | Small blobs rise, sway and follow the water more slowly, as if further away. Scaled by radius against the largest disc |
| `[liquid_acid] rise_parallax_dim` | Parallax dimming | 0..1, step 0.05 | 0.0 | 0 | Far (small) blobs are slightly darker as a depth cue. Keep it subtle; past ~0.4 they read as a different palette |
| `[liquid_acid] oil_drag` | Oil drag on the ink | 0..1, step 0.05 | 0.0 | 0.9 | Friction under the oil: ink beneath an island is brought to rest and deflected around its rim, so the water cannot stream in underneath |
| `[liquid_acid] oil_dye_block` | Oil blocks the ink | 0..1, step 0.05 | 0.0 | 0.7 | Ink that ends up under the oil fades out over a second or two, so the oil reads as sitting ON the water. Droplet holes are not oil and keep their ink -- INERT on acid-rise-12 (why: gate not traced). |
| `[liquid_acid] oil_viscosity` | Oil viscosity | 0..1, step 0.05 | 0.0 | 0.6 | Thick liquid: blobs lag the water, accelerate and coast slowly, breathe and sway slowly, neck together over seconds, and the droplets stop jittering |
| `[liquid_acid] mouse_oil_mode` | Mouse oil mode (0 none 1 push 2 comb) | 0..2, step 1 | 0 | 0 | What the cursor does to the OIL (never to the ink). 0 = nothing at all, 1 = drags and parts it, 2 = a marbling comb that leaves streaks which round back up over about 3 s |
| `[liquid_acid] mouse_oil_radius` | Mouse oil radius (uv) | 0.02..0.5, step 0.01 | 0.12 |  | Reach of the push, or the length of the comb's band along the pointer path |
| `[liquid_acid] mouse_oil_gain` | Mouse oil gain | 0..3, step 0.05 | 1.0 |  | Strength multiplier for the push or the comb |
| `[liquid_acid] droplets` | Droplets (particles, 0 = off) | 0..4096, step 25 | 0 | 950 | Simulated droplets rendered INTO the oil surface: water trapped in the oil and oil droplets on the ink. Above 0 the procedural swarms are switched off |
| `[liquid_acid] droplet_spawn_rate` | Droplet nucleation (per s) | 0..300, step 5 | 40.0 | 40 | How fast new droplets come out of solution, biased to thin oil near an edge and to shearing flow |
| `[liquid_acid] droplet_r_max` | Droplet max radius | 0.002..0.03, step 0.001 | 0.0120 | 0.0132 | Largest droplet, as a fraction of frame height. Clamped to one grid cell |
| `[liquid_acid] droplet_bias` | Droplet size bias (higher = tiny) | 1..6, step 0.1 | 3.2 |  | Heavy tail toward small droplets |
| `[liquid_acid] droplet_weight` | Droplet hole depth | 0..3, step 0.05 | 1.25 |  | How hard a trapped water droplet punches through the oil, relative to the LOCAL field so it works in thick oil too |
| `[liquid_acid] droplet_ink_frac` | Droplets on the ink (fraction) | 0..1, step 0.02 | 0.22 |  | Share of droplets that are oil sitting on the open ink instead of water trapped in the oil |
| `[liquid_acid] droplet_life` | Droplet life (s) | 0..600, step 10 | 120.0 |  | Seconds before a droplet dissolves (shrinking away, never popping). 0 = never |
| `[liquid_acid] droplet_attract` | Droplet attraction | 0..2, step 0.05 | 0.40 | 0.50 | Short-range pull between droplets of the same kind, which is what makes them find each other and merge |
| `[liquid_acid] droplet_merge` | Droplet coalescence overlap | 0.05..0.9, step 0.02 | 0.30 |  | Overlap fraction at which two droplets become one, area-conserving |
| `[liquid_acid] droplet_ring_frac` | Hollow ring droplets (fraction) | 0..1, step 0.02 | 0.0 | 0.15 | Share of new trapped droplets drawn as a thin dark RING with the oil showing through the middle -- the empty doubles |
| `[liquid_acid] droplet_ring_width` | Ring width (x radius) | 0.02..0.6, step 0.01 | 0.08 | 0.13 | How thick a hollow droplet's dark band is, as a fraction of its radius |
| `[liquid_acid] droplet_ring_lift` | Ring interior lift | 0..1, step 0.02 | 0.10 | 0.08 | How much brighter (and slightly thicker) the middle of a hollow droplet reads -- a lens, not a hole |
| `[liquid_acid] droplet_ring_clump` | Ring clumping (bubble rafts) | 0..1, step 0.02 | 0.0 | 0.6 | Rings attract each other and pack into rafts with a shared dark wall instead of merging |
| `[liquid_acid] weather` | Bubble weather (seasons) | 0..1, step 0.05 | 0.0 | 0.5 | Slow seasons in the droplet population: spells of more rings, more solids, sparser, denser, so the frame never settles (needs conservation of mass for the density half) |
| `[liquid_acid] weather_period_s` |   weather period (s) | 30..1200, step 10.0 | 300.0 | 300 | Mean seconds per weather phase |
| `[liquid_acid] droplet_mass_bias` | Bubble crust on the masses | 0..1, step 0.05 | 0.0 | 0.6 | How much crust of small bubbles the dye masses carry, as a share of the film population. The film keeps everything it has -- this is added, not swapped |
| `[liquid_acid] droplet_crust_density` |   crust density (x film) | 1..4, step 0.1 | 1.0 | 2.5 | How dense the crust inside a mass is against the open film. 1 = no crust at all |
| `[liquid_acid] droplet_crust_r` |   crust bubble size | 0.05..1, step 0.05 | 1.0 | 0.6 | Crust bubbles are small: droplet_r_max times this. They clump but never coalesce |
| `[liquid_acid] droplet_racer_frac` | Racing micro-bubbles (share) | 0..0.5, step 0.01 | 0.0 | 0.25 | Share of the smallest trapped droplets that race upward, the way uber-small bubbles do in water (0 = off) |
| `[liquid_acid] droplet_racer_speed` |   racer speed (x) | 1..5, step 0.1 | 2.5 | 2.0 | How many times an ordinary droplet's climb a racer makes |
| `[liquid_acid] droplet_racer_wobble` |   racer wobble | 0..2, step 0.05 | 0.5 | 0.5 | Lateral zigzag / spiral on the way up, as a fraction of the climb |
| `[liquid_acid] droplet_racer_r_max` |   racer max radius | 0..0.01, step 0.0005 | 0.0 | 0.0050 | Only droplets at or below this radius race (0 = twice droplet_r_min) |
| `[liquid_acid] droplet_ring_r_mul` | Big hollow bubbles (size x) | 1..6, step 0.1 | 1.0 | 3.5 | How many times droplet_r_max a BIG hollow ring may be (1 = today: rings are limited to the solid droplets' size range) |
| `[liquid_acid] droplet_ring_big_frac` |   big-ring share | 0..0.3, step 0.005 | 0.0 | 0.10 | Share of new rings that are big -- keep it small, a couple visible at a time is the point |
| `[liquid_acid] droplet_coalesce` | Coalescence (touching drops merge) | 0..1, step 0.05 | 0.0 | 1 | Same-kind solid droplets that touch pour into one round droplet of the combined area instead of parking as a lumpy cluster of lobes (rings keep their foam walls) |
| `[liquid_acid] droplet_coalesce_s` |   neck close (s) | 0..4, step 0.1 | 0.0 | 1.0 | Seconds the coalescence takes (0 = the old 0.18 s snap) |
| `[liquid_acid] conserve_mass` | Conservation of mass | 0..1, step 1.00 | 0.0 | 1 | Nothing appears or vanishes at a visible size in the open: droplets enter and leave across the frame edges, blobs wrap only once their field is clear of the frame, and births swell from below the visible floor |
| `[liquid_acid] spawn_grow_s` |   spawn grow (s) | 0..20, step 0.5 | 0.0 | 4 | Seconds a new droplet takes to swell from nothing to full size (0 = the old 0.34 s pop) |
| `[liquid_acid] dissolve_s` |   dissolve (s) | 0..20, step 0.5 | 0.0 | 5 | Seconds a droplet dying of old age takes to shrink away (0 = the old 0.34 s) |
| `[liquid_acid] droplet_ring_wobble` | Ring out-of-round | 0..1, step 0.05 | 1.0 | 1 | Stops a ring being a perfect circle: a seeded ellipse whose axis turns over a minute or two, plus a breathing 3-lobe wobble, and a wall that thins where the ring bulges. 0 = perfect circles |
| `[liquid_acid] ink_water` | Ink under the oil (0 bands 1 water) | 0..1, step 1 | 0 |  | 1 = the shared ink-in-water render (translucent veils) instead of flat bands |
| `[liquid_acid] rise_respawn` | Rising blobs re-enter from below | on/off | false | 1 | With a rise speed set, a blob that climbs off the top comes back in under the bottom edge at a new place and size, instead of reappearing where it left |
| `[liquid_acid] aa_scale` | *(no slider)* | — | 1.3 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] breath` | *(no slider)* | — | 0.10 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] bubble_max` | *(no slider)* | — | 0.075 | 0.075 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] bubble_min` | *(no slider)* | — | 0.015 | 0.015 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] buoyancy` | *(no slider)* | — | 0.0020 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] damping` | *(no slider)* | — | 2.2 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] disc_max` | *(no slider)* | — | 0.460 | 0.380 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] disc_min` | *(no slider)* | — | 0.250 | 0.160 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] droplet_damping` | *(no slider)* | — | 4.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] droplet_jitter` | *(no slider)* | — | 0.0022 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] droplet_oil_weight` | *(no slider)* | — | 1.20 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] droplet_r_min` | *(no slider)* | — | 0.0009 | 0.0011 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] droplet_rise` | *(no slider)* | — | 0.30 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] droplet_support` | *(no slider)* | — | 2.20 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] hole_max` | *(no slider)* | — | 0.130 | 0.130 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] hole_min` | *(no slider)* | — | 0.030 | 0.030 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] swarm_dark` | *(no slider)* | — | 0.80 | 0.35 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_drift` | *(no slider)* | — | 1.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] swarm_r_max` | *(no slider)* | — | 0.430 | 0.420 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_r_min` | *(no slider)* | — | 0.045 | 0.120 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] swarm_rim_dark` | *(no slider)* | — | 0.55 | 0.55 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] web_max` | *(no slider)* | — | 0.260 | 0.200 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] web_min` | *(no slider)* | — | 0.110 | 0.070 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] wrap_margin` | *(no slider)* | — | 0.20 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |

## 3. Palette and hue

Where every colour in the oil, the ink and the dark masses comes from: the curated palette sweep and continuous hue rotation, the duotone ink ramp, the accent-colour rule, the second and third hue fields (patch size/drift/decay/wobble/seeding), and the dye's own hue/saturation/brightness and whether it tracks the film's hue.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[liquid_acid] ink_shading` | Ink emboss (0 = flat bands) | 0..1, step 0.02 | 0.00 | 0.00 | How much of the fluid look's pseudo-3D shading survives. The refs are flat |
| `[liquid_acid] ink_levels` | Ink bands (posterise) | 2..24, step 1 | 5.0 | 4 | Flat colour plateaus in the ink. Low = poster, high = smooth -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] ink_soft` | Ink band softness | 0.01..0.5, step 0.01 | 0.42 | 0.60 | 0 = hard steps between bands -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] ink_mix` | Ink palette strength | 0..1, step 0.02 | 0.88 | 0.95 | 0 = keep the normal fluid colours, 1 = full duotone ramp -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] ink_hue_vary` | Ink hue variation (deg) | 0..90, step 1 | 14.0 | 3 | Rotates the ramp by the dye's own hue so the ink still drifts -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] ink_complement_span` | Complement window (deg) | 5..180, step 1 | 40.0 |  | How far the ink hue may wander from the oil's opposite. Small = strictly two-hue |
| `[liquid_acid] hue_sweep_period` | Palette sweep (s per cycle, 0=off) | 0..1800, step 5 | 0.0 | 630 | Cross-fade through the curated vivid palette list. Long settled stretches, short fades. 0 = fixed palette |
| `[liquid_acid] sweep_count` | Palettes in the sweep list | 1..12, step 1 | kSweepPairs | 8 | How many entries of the sweep list are used (the shipped list is 5; the tile9 set is 8) |
| `[liquid_acid] hue_rotate_period` | Hue rotation (s per turn, 0=off) | 0..1800, step 10 | 0.0 | 3600 | Continuously rotates the whole oil palette's hue. The ink is untouched, so mono ink stays grey and the holes stay black. 0 = off |
| `[liquid_acid] oil_saturation` | Oil saturation | 0..2, step 0.05 | 1.0 | 1.0 | Vividness of the oil palette, whichever source it came from. 1 = the authored colours |
| `[liquid_acid] ink_gain` | Ink ramp gain | 0.3..3, step 0.05 | 2.30 | 1.30 | Maps dye brightness onto the ramp. Higher = more bright ink -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] toe_tint` | Ink-tinted black toe | 0..1, step 0.02 | 0.0 |  | Lifts the darkest pixels toward a dark version of the ink hue instead of neutral black |
| `[liquid_acid] accent_mode` | Accent: 0 any size / 1 small only | 0..1, step 1 | 0 |  | Who may take the palette's 4th shade (the complement in the 80-20 palettes). 0 = whoever drew it, big masses included. 1 = only elements under the accent size, so the frame is one colour with small scattered accents |
| `[liquid_acid] accent_max_r` | Accent max size (of disc max) | 0.05..1, step 0.01 | 0.35 |  | How small a blob has to be to carry the accent colour, as a fraction of the biggest disc. Anything larger uses the base shades |
| `[liquid_acid] accent_frac` | Accent share (of the small ones) | 0..1, step 0.02 | 0.60 |  | Fraction of the small blobs that actually take it, so it stays an accent and not a rule |
| `[liquid_acid] film_hue2` | Second film hue (deg) | -180..180, step 5.0 | 0.0 | 180 | How far off the palette's own hue the second dye is. Roughly complementary reads like the reference: cyan patches in a magenta film |
| `[liquid_acid] film_hue2_amt` |   second hue amount | 0..1, step 0.05 | 0.0 | 1.0 | How far the film goes toward that hue inside a patch. 0 = one colour, as before |
| `[liquid_acid] film_hue2_scale` |   patch size | 0.08..0.90, step 0.05 | 0.35 | 0.35 | Patch size as a fraction of the frame. A quarter to a half is the reference's scale |
| `[liquid_acid] film_hue2_drift` |   patch drift | 0..2, step 0.05 | 1.0 | 1.0 | How fast the patches wander on their own, on top of being carried by the sim's flow |
| `[liquid_acid] film_hue2_decay` |   patch decay | 0..2, step 0.05 | 0.35 | 0.35 | How fast the field falls back toward its base. Too low and the second hue spreads into a flat tint over minutes |
| `[liquid_acid] film_hue2_wobble` |   second hue wobble (deg) | 0..60, step 1.0 | 0.0 | 10 | The contrast hue is not nailed to one angle: it wanders this many degrees either side of it, so a 180 deg pair lives in 170-190 and the combo keeps changing. Two sines in the golden ratio, so it never repeats |
| `[liquid_acid] film_hue2_wobble_period` |   wobble period (s) | 10..1800, step 10.0 | 300.0 | 300 | Seconds of the slower of the two wobble sines. It also re-aims on the rig's readjustment, with the lamp and the focus |
| `[liquid_acid] film_hue2_seed_rows` |   seed rows below screen | 0..8, step 1.0 | 2.0 | 2 | New patch colour is generated only in this many hidden rows BELOW the bottom edge and rises into view. 0 = the old behaviour, which laid new patches anywhere in the frame |
| `[liquid_acid] film_hue2_rise` |   patch rise (screens/min) | 0..2, step 0.05 | 0.25 | 0.3 | Extra upward drift of the patches on top of the oil's own rise. Patches can never travel down the screen |
| `[liquid_acid] film_hue3` | Third film hue (deg) | -180..180, step 5.0 | 0.0 | 0 | An optional third hue, taken off the other end of the SAME patch field so it costs nothing extra per pixel |
| `[liquid_acid] film_hue3_amt` |   third hue amount | 0..1, step 0.05 | 0.0 | 0 | 0 = off, which is the default |
| `[liquid_acid] crust_hue_mix` |   crust hue mix | 0..1, step 0.05 | 1.0 | 1.0 | How much of the hue shift the droplets INSIDE a dark mass take. 1 = the same as the film, which is the reference's cyan-lit specks in the black |
| `[liquid_acid] dye_hue` | Dye hue (deg) | -180..180, step 5 | 0.0 | 285 | The colour the dark masses take. With Dye follows film on, this is an OFFSET from the film's own hue, so the two turn together; with it off it is an absolute hue. +-180 covers the circle either way |
| `[liquid_acid] dye_sat` | Dye saturation | 0..1, step 0.05 | 0.0 | 0.8 | How coloured the dark masses are. The full 0..1 is useful here because the dye sits at a very low value: even fully saturated it reads as a deep wax, not as a bright fill. 0 turns the dye OFF (today's neutral black), it is not a grey dye |
| `[liquid_acid] dye_lum` | Dye brightness | 0..0.50, step 0.02 | 0.0 | 0 | How much lamp the THIN edge of a mass passes; the thick core keeps about 40% of it, which is what reads as translucent wax. Range from the dye4 sheet: under ~0.10 the mass is still black, 0.28-0.34 is the deep wax, and past ~0.45 the mass stops reading as dark at all. 0 = today's black |
| `[liquid_acid] dye_hue_follow` | Dye follows film hue | 0..1, step 1 | 0 | 0 | 1 = Dye hue is an offset from the film's current hue, so the dye rotates with it under hue_rotate_period and the sweep and the pair stays designed. 0 = a fixed absolute hue |
| `[liquid_acid] dye_masses` | Dye on masses | 0..1, step 0.05 | 1.0 | 1 | (brief BK) How much dye the big masses take. A mass = a gap between oil blobs (the blob-only field `fieldB` below the threshold), decided per pixel in the display pass. 1 = today; 0 = masses pitch black while the droplets can keep a dye of their own |
| `[liquid_acid] dye_droplets` | Dye on droplets | 0..1, step 0.05 | 1.0 | 1 | (brief BK) How much dye the droplet holes take (a droplet = a dark pixel whose blob-only field is ABOVE the threshold, i.e. a hole punched into the sheet). 1 = today, every droplet dyed ("too bubbly"); 0 = droplets stay black. A droplet within ~3 px of a mass (plus the smoke's reach) shares the mass colour; crust bubbles INSIDE a mass (positive oil droplets) sit in mass territory and take the mass role. Multiplies dye_droplet_lum, so the two overlap (see the BK proof block) |
| `[liquid_acid] dye_droplet_hue` | Droplet dye hue (deg) | -5..360, step 5 | -1 | -1 | (brief BK) The droplets' own dye hue, so masses and droplets can be two colours (main + accent). Below 0 = inherit dye_hue. Follows the film like dye_hue when dye_hue_follow is on |
| `[liquid_acid] dye_droplet_sat` | Droplet dye saturation | -0.05..1, step 0.05 | -1 | -1 | (brief BK) Below 0 = inherit dye_sat; 0 = droplets black (same convention as dye_sat) |
| `[liquid_acid] dye_droplet_lum` | Droplet dye brightness | -0.02..0.50, step 0.02 | -1 | -1 | (brief BK) Same scale as dye_lum. Below 0 = inherit dye_lum; 0 = droplets black. The droplet colour is packed as hue8 | sat8 | lum8 in one cbuffer float (DYE_DROP_RGB, laP11.w) and rebuilt in the shader as InkHsv2Rgb(h, s, 1) x lum (brief BP; it was 8-bit RGB after the lum multiply -- measured on the roles tiles at lum .30 the old quantisation cost only C/L +-0.3% and MAD 0.03-0.08, larger only at very low lum) |
| `[liquid_acid] dye_smoke` | Dye smoke | 0..1, step 0.05 | 0.0 | 0 | (brief BK) 0 = today's wax: the dye stops hard at the mass edge and is brightest at the thin rim. Up = the dye thickens gradually inward (over 0.010 + 0.080*smoke of the frame height), leaks up to 0.030*smoke out under the thin film, loses the edge peak and is broken into slow fbm wisps that also warp its edge. Masses only (droplets are narrower than the ramp; smoking them only dimmed them, which is dye_droplets' job). Brief BP: the LEAK under the film (coverage > 0) takes the film's hue (measured C/L in the leak .301 -> .303 at smoke .6: the leak tint is real but tiny; the mid-value mud is the smoke's own fade + the fog veil, not hue mixing). Needs dye_lum > 0 |
| `[liquid_acid] film_level` | Film level | 0..1, step 0.02 | 1.0 | 1 | (brief BK) Level of the FLAT oil film (the background sheet), applied just before the film is composited. The edge light (mass_rim, oil_glow, halo, lens highlights) keeps the full palette and the mass/droplet dye is untouched, so ~0.05 = a pitch-black film with glowing edges (brief BL). 1 = today |
| `[liquid_acid] oil_fluor` | Oil fluorescence (lamp) | 0..1, step 0.05 | 0.0 | 0 | (brief BL) The oil EMITS its own hue (saturation x1.25, value 1) under the rig lamp: Gaussian falloff from LAMP_X/Y over oil_fluor_reach, profile 0.20 + 0.80 x the blob-surface edge term (the film's thin MASS edge; droplet holes get only the body term), weighted by film alpha, added after film_level and before the cast shadows; the edge band gets HDR headroom (m -> knee..capBright). PROOF (720p, dye_lum .30, film .3): 0.5 / 1 raise the frame mean 9.5 -> 11.1 / 13.0 nt, bottom third 11.2 -> 14.2 / 18.0, top third 7.6 -> 8.1 / 8.5, mass-edge ring p99 16 -> 17 / 20 nt, inside masses unchanged, 99.9th pct 27 -> 57 nt. Reads as a lamp-side lift of the film, NOT a neon edge: largely redundant with rise_bottom_light x film_level; the edge glow the refs ask for comes from meniscus_film_mix 1 (ring p99 16 -> 40 nt). Candidate for the KEY PASS unless the user keeps it on the panel |
| `[liquid_acid] oil_fluor_reach` | Fluorescence reach (screen h) | 0.3..2, step 0.05 | 0.8 | 0.8 | (brief BL) Gaussian radius of oil_fluor's lamp falloff in screen heights (the lamp sits at light_y 1.2, 0.2 below the frame). 0.45 = only the bottom band glows (frame mean 10.5 vs 13.0 at 0.8); 1.5 = the whole film lifts (16.7 nt, i.e. it just undoes film_level). Inert while oil_fluor is 0 |
| `[liquid_acid] dye_lamp_follow` | Dye follows the lamp | 0..1, step 0.05 | 1.0 | 1 | (brief BL) 1 = today: the mass/droplet dye rides the lamp ramp (lampG, rise_bottom_light) and the thin-edge profile lerp(0.42, 1, Tw). 0 = both drop out and the dye sits at its thick-core level everywhere: flat ambient colour. PROOF (dye_lum .30, film .3): inside-mass mean 1.85 -> 1.09 nt, mass-edge p99 9.3 -> 3.4 nt, MAD 3.65 -- the dyed masses go to a dim flat maroon with no lit rim, the most direct "dark surround" lever of BL. Inert while dye_lum is 0 |
| `[liquid_acid] dark_sat` | Dark saturation (punch) | 0..1, step 0.05 | 0.0 | 0 | (brief BP) Chroma gain k = 1 + dark_sat x (1 - level) keyed on the ELEMENT level (film_level for the film, dye level for masses, droplet level for droplets), applied in LINEAR light as Y + (l - Y) x k, then gain-corrected so the luminance AFTER post_chroma (which re-amplifies chroma about the encoded luma and clamps at 0) is held; clamped at the gamut edges (min >= 0, no new channel above 1). Inert on an element at level 1. PROOF (roles tiles, .jxr, OKLab): tile 1 film .05 dark_sat .5/1: film C/L .095 -> .129/.159, dye C/L .155 -> .175/.178, mean_lum +0.0/+0.5%; tile 2: dye C/L .277 -> .300/.302, mean_lum +1.2/+1.7%; film .3: film C/L .297 -> .378, mean_lum -2.1%; film .1: .160 -> .246, -0.1%. The dye is gamut-bound above ~0.5; the film gain keeps growing to 1. HDR on and off agree (1440p hero: nits mean +0.5% both, peak unchanged). First version (gain about encoded luma, no post_chroma hold) raised mean_lum 11-40%: do not reintroduce |
| `[liquid_acid] dye_lum_vary` | Dye brightness spread | 0..0.50, step 0.02 | 0.0 | 0 | (brief AG-b) Mass dye level x (1 + value x (2 id - 1)). IDENTITY id in 0..1: hole blobs carry a CPU hash in AcidBlobGPU.c.w (fixed for the blob's life, re-rolled only on the off-screen respawn), soft-maxed with w^4 in the blob loop, and it counts only where the hole CARVED the mass (oil-only field above the isoline; a hole drifting through a gap otherwise printed a disc); everywhere else a slow AcidFbm at ~0.4 screen that rises with rise_speed (laP34.w). MASSES only (BK's mK test, droplets keep their dye). PROOF (acid-rise-12 + dye_lum .36, 7 masses pooled from t 25/40/55 s, .jxr OKLab): across-mass L sd 8.8 -> 9.3 -> 9.9 -> 10.7% at 0/.15/.3/.5, mass level 5.23 -> 6.40 nt, MAD 0.58/1.17/1.96, frame mean +1.6% at .5. In this preset only 6-9% of mass pixels are hole-carved (idW > 0.5: 3-4%), so it reads as a slow brightness GRADIENT across the big gap masses rather than distinct per-mass shades. Inert while dye_lum is 0 |
| `[liquid_acid] dye_hue_vary` | Dye hue spread (deg) | 0..60, step 1 | 0.0 | 0 | (brief AG-b) Mass dye hue + value x (2 id - 1) degrees, same identity as dye_lum_vary. PROOF (same set): across-mass hue sd 1.1 -> 1.8 -> 4.5 -> 8.5 deg at 0/25/50/90, within-mass 8 -> 11 -> 17 -> 28 (gradients inside the big gaps), MAD 0.89/1.67/2.82. 25 = a hint, 50 = clearly two-tone, 90 = orange / teal mud (slider capped at 60). Series at the hero values (5 frames over 10 s): per-mass hue drift max 1.03 deg, no hole id re-rolled on screen. Inert while dye_lum is 0 |
| `[liquid_acid] dye_thick_hue` | Dye hue with thickness (deg) | -90..90, step 1 | 0.0 | 0 | (brief AG-b) Mass hue + value x (1 - Tw), Tw = exp(-depth/0.055) the wax transmission: thin edge = dye_hue, thick core = dye_hue + value. Not a duplicate of dye_hue_vary (depth, not identity). PROOF: core-minus-edge hue -20.9 / -0.5 / +6.9 / +18.4 deg at -45/0/20/45, MAD 1.72/0.84/1.78; negative = blue cores (the clearest of the three on screen, mass level -17% since blue is darker), positive = wine cores. Inert while dye_lum is 0 |
| `[liquid_acid] dye_core` | Dye core density | 0.42..1, step 0.02 | 0.42 | 0.42 | (brief BR) The mass core's brightness as a fraction of its thin rim: the dye profile is lerp(dye_core, 1, Tw), Tw = exp(-depth/0.055), at all three sites (plain, dark_sat and split/smoke paths). 0.42 = the old hard-wired floor (uploaded as the same float32; preset-identity MATCH on all presets). 1 = an even, opaque fill (pigment, not a lit wash). PROOF (LAPD candidate with dye_lamp_follow 1, 1280x720, t=40 s, one mass, PQ nits): core/rim 0.18 / 0.41 / 0.73 at 0.42 / 0.70 / 1.0 (rim ~58-63 nt, core 10 -> 25 -> 46 nt; the measured rim includes mass_rim and halation, hence below the nominal 0.42/0.7/1); frame mean 13.8 -> 17.5 -> 22.1 nt (+27% / +60%, ABL-irrelevant at these levels, still lower dye_lum to hold it); MAD vs 0.42: 5.3 / 10.5, max 133 / 205. WHAT ADDS: 0.42..0.6 the core only lifts a little (still a lit wash); 0.6..0.85 the wash fills in; 0.85..1 even fill, the only range that reads as dense pigment. Interactions: dye_smoke pulls the profile toward 0.75, so with smoke up this key does less (not gated); dye_lamp_follow < 1 multiplies Tw toward 0 i.e. toward this floor -- at dye_core 1 that no longer thins anything and dye_lamp_follow only sets the lamp ramp; dye_thick_hue is hue only. Droplets have Tw ~ 1, barely change. Inert while dye_lum is 0 |
| `[liquid_acid] lamp_grey` | Lamp grey (far corner) | 0..1, step 0.05 | 0.0 | 0 | (brief BU) Desaturates a soft disc on the corner diagonally opposite the rig lamp about the pixel's LINEAR luminance (1 = 60% at the peak; w = 1 - smoothstep(0.35 S, S, dist)), with a post_chroma luminance hold; display pass, acid PSO, after the film/dye composite and before every rim term. CPU picks the corner from sign(lamp - 0.5) with +-0.04 hysteresis and slides the centre along the frame edge (never through the middle) over 90 s, eased; laP36/laP37.w. PROOF (monotone-post-0924, 1280x720 t=40, far corner = BR): OKLab C ratio in 0.1-H rings from the corner 0.91 / 0.83 / 0.97 / 0.99 / 1.00 at lamp_grey 1 (0.96 / 0.94 / 0.99 / 1.00 at 0.5); centre disc (r < 0.1 H) MAD 0; mean_lum -0.04% (lamp_grey 1); OKLab L -1.2 / -2.7 / -0.6% in the same rings (Y is held, so the pink loses a little perceived lightness: reads as "less light"). Series across a readjust (light_x 0.45, 13 frames 80-200 s): exactly one lobe on every frame, at most one corner > 0.1, lobe centroid within 0.01-0.11 of the logged centre. WHAT ADDS: shows only where film/lit dye sits in that corner (on this preset at t=40 the BR corner was a black mass, so only its droplets greyed); 0.3-0.6 subtle, 1 a clear pale corner disc. mean_lum on a bright green/blue film: -1.2..+2.6% (HDR gain / bloom interplay, not the display pass) |
| `[liquid_acid] lamp_grey_size` | Lamp grey size (screen h) | 0.25..0.60, step 0.05 | 0.40 | 0.4 | (brief BU) Region radius S. At lamp_grey 1: 0.25 frame MAD 0.02, 0.4 0.05, 0.6 0.43 (8-bit); 0.6 reaches the middle band while sliding along the long edge (centre-crop MAD 0.008, disc r<0.1 still 0) and costs mean_lum -0.8% |
| `[liquid_acid] lamp_grey_cool` | Lamp grey cool | 0..1, step 0.05 | 0.20 | 0.2 | (brief BU) Share of a Y-normalised cool white balance (0.80, 0.94, 1.30) in the grey. 0 -> 0.5 moves the corner hue a further -1.2 deg (lamp_grey 1): nearly inert on a pink film; a hint, as specified |
| `[liquid_acid] shadow_tone` | Shadow tone (split tone) | 0..1, step 0.05 | 0.0 | 0 | (brief BU) Adds toneAdd x (1 - alpha) x (1 - smoothstep(0.02, 0.15, luma)) to the composite before the rims: black masses/droplets take the OKLab complement of the main film hue (CPU solves the HSV hue so the added colour at this frame's level sits at film OKLab hue + 180; the plain HSV complement missed by up to 23 deg). Gate = 50% duty on shadow_tone_period with smoothstep fades, starts ON. PROOF (jxr, scRGB): tint hue - film hue - 180 = +4.0 / +1.6 deg (lift .06 / .12, t=40) and within +-7.3 deg over 9 series frames spanning 120 deg of hue; body luma lift +0.025 / +0.047 (<= lift); lit rims (ring >= 0.15 luma) MAD 0.92 / 1.96 8-bit (<= 0.1 x lift = 1.53 / 3.06); film core (>= 8 px from any body) MAD 0.13 / 0.29, max 27-45 = DOF/halation spread of the tinted edges in the post pass, 0 in the display pass by construction; mean_lum +0.84% / +2.06%. Crossfade series: measured lift follows the CPU gate 1 / 0.84 / 0.50 / 0.16 / 0 in 10 s steps, no step. DUPLICATE: shadow_tone and shadow_tone_lift multiply (0.5 x 0.12 renders byte-identical to 1 x 0.06) |
| `[liquid_acid] shadow_tone_lift` | Shadow tone lift | 0..0.15, step 0.01 | 0.06 | 0.06 | (brief BU) sRGB-encoded level of the tint at full tone. 0.06 = a deep teal on the pink look, 0.12 = clearly coloured bodies. Same knob as shadow_tone (product) |
| `[liquid_acid] shadow_tone_sat` | Shadow tone saturation | 0..1, step 0.05 | 0.80 | 0.8 | (brief BU) 0 = a neutral grey lift (body dC 0.021 vs 0.048 at 0.8, hue undefined); 0.8 = the complement |
| `[liquid_acid] shadow_tone_period` | Shadow tone period (s) | 0..3600, step 25 | 2225 | 2225 | (brief BU) On+off cycle; 0 = always on. 2225 = 3600 / golden ratio: gear log over 3 h puts the edges at hue phases 93/217/316/79/178/302/41/164/263 deg, first repeat (same edge within 5 deg) after 21.3 h; the 3:5 option 2160 repeats exactly after 3.25 h. Default: fully on 0-872 s, fades out 872-992, off to 2105, back on by 2225 |
| `[liquid_acid] shadow_tone_fade` | Shadow tone fade (s) | 0..600, step 10 | 120 | 120 | (brief BU) Smoothstep length of each edge; verified on a 480 s / 40 s test clock (series above) |
| `[liquid_acid] shadow_tone_hue` | Shadow tone hue (deg) | -5..360, step 5 ("complement" below 0) | -1 | -1 | (brief BU/BV) -1 = the complement above; 0..360 = a FIXED HSV hue that does not rotate, CPU only (no slot). -1 renders byte-identical to the pre-key build (md5 E3721DD5...). 275 -> OKLab tint hue 298 (violet, body lift +0.016), 185 -> 201 (teal, +0.045) on monotone-post-0924 |
| `[liquid_acid] oil_hdr` | Oil HDR level (0 = follow ink) | 0..1.4, step 0.02 | 0.0 |  | Drives the HDR highlight gain for oil pixels. 0 = inherit the ink's |
| `[liquid_acid] ink_complement_lock` | Lock ink opposite the oil hue | on/off | true | 0 | Hold the ink's hue on the far side of the wheel from the oil -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] ink_bias` | *(no slider)* | — | 0.05 | 0.03 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] oil_color_1` | *(no slider — colour triple, hand-edit only)* | — | 0.902 0.278 0.157 | 0.95 0.40 0.10 | One of the oil look's up to 4 dominant blob colours (r g b). Fixed palette, used when the sweep/rotation are both off. |
| `[liquid_acid] oil_color_2` | *(no slider — colour triple, hand-edit only)* | — | 0.976 0.400 0.078 | 0.98 0.45 0.08 | One of the oil look's up to 4 dominant blob colours (r g b). Fixed palette, used when the sweep/rotation are both off. |
| `[liquid_acid] oil_color_3` | *(no slider — colour triple, hand-edit only)* | — | 0.859 0.204 0.098 | 0.92 0.35 0.12 | One of the oil look's up to 4 dominant blob colours (r g b). Fixed palette, used when the sweep/rotation are both off. |
| `[liquid_acid] oil_color_4` | *(no slider — colour triple, hand-edit only)* | — | 0.988 0.541 0.114 | 1.00 0.50 0.05 | One of the oil look's up to 4 dominant blob colours (r g b). Fixed palette, used when the sweep/rotation are both off. |
| `[liquid_acid] ink_stop_1` | *(no slider — colour triple, hand-edit only)* | — | 0.006 0.034 0.038 | 0.086 0.478 0.494 | One of the 4-stop duotone ink ramp's colour stops (dark to bright). |
| `[liquid_acid] ink_stop_2` | *(no slider — colour triple, hand-edit only)* | — | 0.043 0.353 0.376 | 0.016 0.040 0.046 | One of the 4-stop duotone ink ramp's colour stops (dark to bright). |
| `[liquid_acid] ink_stop_3` | *(no slider — colour triple, hand-edit only)* | — | 0.478 0.173 0.020 | 0.478 0.165 0.020 | One of the 4-stop duotone ink ramp's colour stops (dark to bright). |
| `[liquid_acid] ink_stop_4` | *(no slider — colour triple, hand-edit only)* | — | 0.969 0.510 0.055 | 0.980 0.545 0.078 | One of the 4-stop duotone ink ramp's colour stops (dark to bright). |
| `[liquid_acid] sweep_pair_N_oil / sweep_pair_N_ink` | *(no slider — palette data list, up to 24 ini keys, not a slider)* | — | 5 shipped pairs (vermillion/teal, red/cyan-green, magenta/green, gold/violet, lime/purple) | 8 pairs defined (sweep_oil_1..8 / sweep_ink_1..8, the tile9 set) | The curated palette sweep list `hue_sweep_period` cross-fades through, up to 12 entries (`sweep_count` picks how many). Short aliases `sweep_oil_N`/`sweep_ink_N` are also accepted. An oil entry may be 3 floats (one anchor the other 3 shades derive from) or the full 12 (all 4 shades verbatim, how the tile9 palettes are carried). |

## 4. Per-droplet optics

How an individual blob, droplet or mass is actually rendered: its rim and meniscus, refraction and translucency, the thin-film edge and specular/iridescence, the lens shading on trapped droplets, cellulose texture, seams, penumbra shading, and diffraction of the smallest features.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[liquid_acid] rim_width` | Rim width | 0.001..0.03, step 0.0005 | 0.0013 | 0.0016 | Half-width of the dark rim band at the oil edge |
| `[liquid_acid] rim_dark` | Rim darkness | 0..1, step 0.02 | 0.80 | 0.80 | How dark the thin rim just inside the oil edge goes |
| `[liquid_acid] rim_vary` | Rim variation | 0..1, step 0.05 | 0.0 | 0.4 | Break the rim up: low-frequency noise on its width and on the halo, so it thickens, thins and dies out along the edge -- INERT on acid-rise-12 (why: ink-brightness gate (haloInk=0): the ink outside the oil is black everywhere under this preset's ink_mode=water, so the rim+halo annulus this key shapes contributes zero regardless of its value). |
| `[liquid_acid] rim_ink_follow` | Rim follows ink | 0..1, step 0.05 | 0.0 | 0.5 | The bright halo is refracted ink: scale it by how bright the ink just outside the edge is -- INERT on acid-rise-12 (why: ink-brightness gate (haloInk=0): the ink outside the oil is black everywhere under this preset's ink_mode=water, so the rim+halo annulus this key shapes contributes zero regardless of its value). |
| `[liquid_acid] meniscus` | Meniscus (bright halo) | 0..1, step 0.02 | 0.85 | 0.85 | Thin bright ink-coloured halo just outside the oil's dark rim |
| `[liquid_acid] meniscus_width` | Meniscus width | 0.0005..0.01, step 0.0005 | 0.0020 | 0.0020 | Half-width of the bright halo |
| `[liquid_acid] refraction` | Refraction through the rim | 0..0.2, step 0.005 | 0.050 | 0.050 | Shifts the ink seen through the thinning oil near edges |
| `[liquid_acid] translucency` | Oil translucency | 0..1, step 0.02 | 0.16 | 0.10 | How much the ink underneath modulates the oil fill |
| `[liquid_acid] seam_strength` | Seam strength (dark edging) | 0..1, step 0.02 | 0.70 | 0.30 | Dark seams where the dye gradient is steep (acrylic-pour edging) -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] seam_lo` | Seam threshold | 0..0.5, step 0.01 | 0.06 | 0.06 | Gradient magnitude a seam starts at |
| `[liquid_acid] grain` | Film grain | 0..0.2, step 0.005 | 0.030 | 0 | Coarse animated grain over the whole frame. Dropped when the [post] pass runs with a `film_grain` of its own, so it is never a second stock on top of it (brief BD); kept where it is the only grain; paced to `film_grain_fps` -- INERT on acid-rise-12 (why: dropped because [post] film_grain is already running (0.10) in this preset, per this key's own stated rule). |
| `[liquid_acid] grain_scale` | Grain coarseness (px) | 1..6, step 0.5 | 3.0 | 3.0 | Pixels per grain cell. 1 = fine, 4 = chunky macro-film |
| `[liquid_acid] grain_shadow_weight` | Grain into shadows | 0..1, step 0.02 | 0.0 |  | Weights the grain by (1-luminance)^2 so flat bright oil stays clean |
| `[liquid_acid] oil_edge_mode` | Edge: 0 soft film / 1 crisp | 0..1, step 1 | 0 | 0 | 0 = the film thins out over a wide band that scales with the blob size. 1 = every surface ends on the same hard isoline the droplets do, with only a thin meniscus |
| `[liquid_acid] oil_penumbra` | Backlight penumbra | 0..1, step 0.02 | 0.0 | 0.3 | The lamp is under the middle of the dish: the oil right beside a black mass is lit less, so it darkens and shifts hue a little over a soft band |
| `[liquid_acid] oil_penumbra_px` | Penumbra width (px at 1440p) | 1..40, step 1 | 8.0 | 8 | How far that shading reaches into the oil |
| `[liquid_acid] oil_penumbra_hue` | Penumbra hue shift (deg) | -90..90, step 1 | 12.0 | 12 | Which way the less-lit pigment turns. Arbitrary but consistent for a look |
| `[liquid_acid] oil_penumbra_dark` | Penumbra darkening | 0..1, step 0.02 | 0.15 | 0.15 | How much level the band loses as well as hue |
| `[liquid_acid] cellulose` | Cellulose texture | 0..1, step 0.02 | 0.0 | 0.45 | A fibrous, mottled macro texture on the surface: strands of the kind a cell body shows under a microscope, drifting with the rise. 0 = off |
| `[liquid_acid] cellulose_ink` | Cellulose on the black | 0..2, step 0.05 | 1.0 | 1 | How much of it lands on the black side. It fades out deep inside a black mass, so the strands read as a thin layer near its edge and never as a grey wash |
| `[liquid_acid] cellulose_oil` | Cellulose on the film | 0..2, step 0.05 | 0.35 | 0.35 | How much of it lands on the coloured film, where it only mottles the colour |
| `[liquid_acid] cellulose_scale` | Cellulose scale (px at 1440p) | 8..140, step 2 | 40.0 | 40 | How long and how far apart the fibres are |
| `[liquid_acid] cellulose_drift` | Cellulose drift | 0..1, step 0.05 | 1.0 | 1 | 1 = the texture travels with the rise, so it belongs to the masses; 0 = it is pinned to the screen |
| `[liquid_acid] oil_edge_curve` | Edge S-curve | 0..1, step 0.05 | 0.0 | 1 | Reshapes the edge ramp from smoothstep to smootherstep, so the slide from oil colour to black arrives without a knee at either end. Same band width |
| `[liquid_acid] oil_thin_edge` | Soft thin edge (real oil) | 0..1, step 0.02 | 0.0 | 0.70 | The film thins at its edge: the colour slides toward the ink beneath and the disc fades over a band, instead of a hard cut-out with a stroked rim |
| `[liquid_acid] oil_edge_frac` | Thin-edge width | 0.02..0.40, step 0.01 | 0.14 | 0.14 | That band as a fraction of each blob's own radius |
| `[liquid_acid] oil_specular` | Oil specular | 0..1, step 0.02 | 0.0 | 0.35 | Broad soft highlight off the lens curvature and a slow thickness ripple (the rig is backlit, so keep it low) |
| `[liquid_acid] oil_iridescence` | Oil iridescence | 0..1, step 0.02 | 0.0 | 0.20 | Thin-film hue shimmer indexed by film thickness, strongest at the thin edges |
| `[liquid_acid] swarm_lens` | Droplets as lenses | 0..1, step 0.02 | 0.0 | 0.80 | Trapped droplets become soft-edged holes in the film with a thin-oil fringe and a small highlight, not flat black discs -- INERT on acid-rise-12 (why: swarm layers forced off: fluid.cpp zeroes LA_SWARM_HOLES/LA_SWARM_DROPS whenever droplets>0, and acid-rise-12 runs 950). |
| `[liquid_acid] meniscus_from_ink` | Emergent halo (from the ink) | 0..1, step 0.02 | 0.0 | 1.0 | The halo takes the colour and the brightness of the ink just outside the edge, and disappears (with the dark hairline) over dark ink |
| `[liquid_acid] meniscus_film_mix` |   halo colour from the film | 0..1, step 0.05 | 0.0 | 0 | The bright halo is the widest, strongest thing painted on a droplet's boundary, and its colour comes from the INK -- so no film-hue feature (a second hue, a seam reflect, the sweep) ever reaches it. Raised, the halo takes its colour from the FILM at that droplet instead, the same colour its lit rim already uses, so a hue2 seam colours the halos around it too, and the ink-brightness gate that hides the halo over black ink opens by the same amount. 0 = as shipped: `acid-rise-12` leaves this ink outside the oil black everywhere, so the gate is 0 and `meniscus`/`rim_dark` paint nothing regardless of this key (see the inert-keys section) |
| `[liquid_acid] oil_glow` | Oil glow outside | 0..1, step 0.02 | 0.0 | 0.45 | Diffuse spill of the oil's own colour into the ink around it |
| `[liquid_acid] refraction_width` | Refraction band width | 0..40.0, step 1.0 | 0.0 | 22 | Rim-width multiplier for the band where the ink is seen bending under the oil (0 = the shipped 7) |
| `[liquid_acid] oil_transparency` | Transparent film | 0..1, step 0.02 | 0.0 | 0.5 | The oil stops being a fill and becomes a coloured film: the ink's marbling reads through it, tinted by the oil's own hue |
| `[liquid_acid] oil_absorb` | Film absorption | 0..6.0, step 0.1 | 2.6 | 2.6 | How deeply the film absorbs. Low = a pale wash, high = a deep saturated glass |
| `[liquid_acid] oil_film_bump` | Film thickness variation | 0..1, step 0.02 | 0.35 | 0.35 | Slow noise on the film's thickness, so it has islands of thick and thin instead of one even pane |
| `[liquid_acid] oil_refract_body` | Refraction across the body | 0..0.04, step 0.002 | 0.0 | 0.006 | Displaces what is seen through the whole film, not only its edge, so the marbling wobbles as it passes under |
| `[liquid_acid] oil_ink_blur` | Ink out of focus under the oil | 0..1, step 0.02 | 0.0 | 0.5 | Softens the ink seen through the film, strongest where the film is thickest -- INERT on acid-rise-12 (why: gate not traced). |
| `[liquid_acid] boundary_reflect_r` |   seam reflect reach | 0..0.60, step 0.01 | 0.0 | 0.33 | How far a hue2 SEAM reaches into the droplets around it, as a fraction of the screen HEIGHT. 0 = as before: a droplet carries the seam's colour only while it is standing in the seam, about one droplet across. Raised, the lit rims, lens highlights and glow of droplets this far away rotate toward the seam's own hue, fading smoothly so the nearest stay strongest. The film itself never changes and nothing is blurred |
| `[liquid_acid] boundary_reflect_amt` |   seam reflect amount | 0..1, step 0.05 | 1.0 | 1.0 | How much of the seam's own hue a rim right beside it takes. 1 = the full seam colour. Only does anything where the reach above is above 0 |
| `[liquid_acid] mass_rim` |   mass rim (lamp side) | 0..1, step 0.05 | 0.0 | 0.35 | A thin bright refracted rim on the lamp side of a mass edge, so a mass reads as a translucent body instead of a flat black cut-out |
| `[liquid_acid] shadow_amt` | Cast shadows | 0..1, step 0.02 | 0.0 | 0.40 | The lamp finally blocks: every mass and every droplet darkens the film on the side away from it. 0 = the flat, shadowless frame this look had until now |
| `[liquid_acid] shadow_len` |   shadow length (frac of height) | 0..0.30, step 0.01 | 0.12 | 0.12 | How far the longest shadow reaches, as a fraction of the screen height. A caster throws less than this in proportion to its own height, and Lamp z stretches it further |
| `[liquid_acid] shadow_soft` |   shadow softness | 0..1, step 0.05 | 0.50 | 0.55 | How fast the penumbra opens up with distance. 0 keeps the shadow as sharp at its tip as at the caster; 1 has it dissolve |
| `[liquid_acid] droplet_depth` | Droplet depth spread | 0..1, step 0.05 | 0.0 | 0.35 | How far droplets and rings are spread in front of and behind the masses' own plane. 0 = everything at one depth, which is what the camera saw before. Needs [post] dof_max_px to be visible |
| `[liquid_acid] depth_rise` | Back layer rise bonus | 0..1, step 0.05 | 0.0 | 0.22 | The far layer climbs faster than the near one, so depth reads in the motion as well as in the blur. Symmetric, so the average rise speed is unchanged |
| `[liquid_acid] diffraction` | Diffraction (size vs the point spread) | 0..1, step 0.05 | 0.0 | 1 | Light bends round anything narrow, so a feature smaller than the lens's point spread cannot reach full darkness: tiny droplets come out as soft grey dots, medium ones dark with soft edges, only the big masses go truly black. Never lifts the black under a mass |
| `[liquid_acid] diffraction_px` | Diffraction width (px at 1440p) | 0.2..8, step 0.1 | 1.5 | 1.5 | How wide that point spread is. A droplet about this radius comes out at two thirds of its full darkness; anything several times it is unaffected |
| `[liquid_acid] droplet_lens` | Droplet lens shading | 0..1, step 0.05 | 0.0 | 0.55 | Every droplet is a lens over the backlight: a thin bright refractive rim hugging its edge, a darker band just inside, a lighter middle, and a small specular. Crisp at the edge, gradual inside -- it adds no blur at all. 0 = flat fills |
| `[liquid_acid] droplet_lens_centre` | Lens centre lift | 0..1, step 0.05 | 0.30 | 0.30 | How much lighter the middle of a droplet is than its shoulder. A hole lifts toward the film colour, an oil droplet toward the backlight. Dies out where the surface stops being curved, so a big mass stays flat and its black stays black. BQ TRIAGE (2026-09-24, LAPD candidate, bq-triage-sheet.png): on a DARK film this is the source of the "grey things / dark circles" -- every film pixel with a surface gradient gets the oil-side lift toward white (a grey fill, lift ~13 SDR codes), which falls away along the film's valleys (soft ~10 px loops, and a dark halo round every droplet). film_level 0 / oil_thin_edge 0 / oil_penumbra 0 leave the loops; droplet_lens 0 or droplet_lens_centre 0 remove loops and fill (ring contrast 28 -> 10 RGB-sum codes, frame -8%), droplets keep their dye. On a dark film use 0 |
| `[liquid_acid] droplet_lens_band` | Lens band width (px at 1440p) | 0.5..12, step 0.25 | 3.0 | 3.0 | Width of the dark inner band and of the bright rim just outside it. Authored in pixels and floored by band_min, never scaled by the element, so a 4-px droplet carries the same crisp rim a big mass does |
| `[liquid_acid] droplet_spec` | Droplet specular | 0..1, step 0.05 | 0.0 | 0.35 | A small highlight on the side of each droplet facing the lamp. It reads the same rig the haze and the bloom do, so it swings when the lamp moves instead of sitting still |
| `[liquid_acid] speckle` | Interface speckle | 0..1, step 0.02 | 0.12 | 0.2227 | Sparse dark dots hugging the oil/ink boundary |
| `[liquid_acid] rim_order` | Paired rim (dark in, bright out) | on/off | false |  | Forces the dark rim and the bright halo adjacent and ordered across the isoline, instead of wherever rim_inset/meniscus_offset put them |
| `[liquid_acid] meniscus_offset` | *(no slider)* | — | 0.0022 | 0.0030 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] oil_texture` | *(no slider)* | — | 0.07 | 0.0759 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] rim_hdr` | *(no slider)* | — | 0.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] rim_inset` | *(no slider)* | — | 0.0013 | 0.0022 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. -- INERT on acid-rise-12 (why: ink-brightness gate (haloInk=0): the ink outside the oil is black everywhere under this preset's ink_mode=water, so the rim+halo annulus this key shapes contributes zero regardless of its value). |
| `[liquid_acid] seam_hi` | *(no slider)* | — | 0.45 | 0.70 | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. -- INERT on acid-rise-12 (why: ink_mode=bands-only code path skipped: acid-rise-12 runs ink_mode=water (the shared InkWater() branch), so this key's block in kDisplaySrc never executes). |
| `[liquid_acid] seam_scale` | *(no slider)* | — | 2.6 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] speckle_scale` | *(no slider)* | — | 240.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[liquid_acid] meniscus_color` | *(no slider — colour triple, hand-edit only)* | — | 0.353 0.918 0.894 (bright cyan) | 0.02 0.02 0.02 | RGB of the bright meniscus halo, when meniscus_from_ink is off. |

## 5. Camera

The virtual lens the whole scene is shot through: focus depth, tilt and field curvature, depth of field, lateral colour aberration, the lamp/lens rig and its slow readjustment, pixel-shift orbit (OLED burn-in guard), and the frame vignette.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[liquid_acid] dye_depth` | Dye layer depth | 0..1, step 0.02 | 0.5 | 0.5 | Where the dye masses float. This used to be pinned to the focus distance, so a big mass sat on the plane of focus by definition and could never go soft on its own. Move it off Focus depth and the masses leave focus like everything else |
| `[liquid_acid] dye_depth_tilt` | Dye layer slope | 0..1, step 0.02 | 0.0 | 0.28 | Tips the dye layer along the direction of the lamp, so it is not parallel to the focus surface: the two cross on a line instead of agreeing over a whole region, and that line travels as the lamp drifts |
| `[liquid_acid] dye_depth_w` | Dye depth weight | 0.05..2, step 0.05 | 0.25 | 0.25 | How much the dye layer counts against the droplets where they overlap. Higher pulls a droplet's focus toward the film it sits in; 0.25 is the original blend |
| `[post] aberration` | Lateral aberration | 0..1, step 0.02 | 0.0 | 1 | A lens focuses red and blue at slightly different magnifications, so the channels land at different scales: red pushed out from the optical axis, blue pulled in. Nothing at the axis, a couple of pixels at the corners. Centred on the rig's lens, which drifts, so the clean spot never sits still |
| `[post] aberration_px` | Aberration width (px at 1440p) | 0..6, step 0.1 | 0.8 | 1.8 | How far red and blue are displaced radially at the corners, in pixels at 1440p. The fringe fades out on anything defocused, exactly as a real one does |
| `[post] aberration_field` | Aberration growth to field edge | 0..2, step 0.1 | 0.5 | 1.2 | How fast the split grows from the optical axis outward. 0 = nearly uniform across the frame, 2 = clean in the middle and all of it in the corners |
| `[post] aberration_coc` | Aberration fades with defocus | 0..1, step 0.05 | 0.0 | 1 | 0 = the same split everywhere, in focus or not. 1 = it fades with this pixel own blur, so the fringe lives on the sharp slice and disappears on an out-of-focus shape, which is what a real lens does |
| `[post] vignette` | Vignette | 0..1, step 0.02 | 0.0 | 0.12 | A gentle fall-off toward the corners -- a field stop, never a circle |
| `[post] softness` | Lens softness (px at 1440p) | 0..6, step 0.1 | 0.0 | 0.5 | Defocuses the isolines themselves -- coverage band, film edge, rim and meniscus -- so no edge in the frame is razor-sharp. The grain stays sharp |
| `[post] post_blur_px` | Camera defocus (px at 1440p) | 0..4, step 0.1 | 0.0 | 0 | Image-space disc blur of the finished frame: every feature, whatever its size, gets the same lens defocus. 0 = off |
| `[post] light_x` | Lamp x | -1..2, step 0.02 | 0.5 | 0.5 | Where the off-view lamp sits, in screen coordinates. 0.5 = the middle, outside 0..1 = off-frame |
| `[post] light_y` | Lamp y | -1..2, step 0.02 | 1.20 | 1.2 | 1.2 = just below the bottom edge, which is where the reference lamp is |
| `[post] light_drift` | Lamp idle drift | 0..1, step 0.05 | 1.0 | 1 | How far the lamp wanders on its own: a sum of slow sines over seconds to a minute, so the light is never static |
| `[post] light_z` | Lamp z (in front / behind) | -1..1, step 0.05 | 0.35 | 0.35 | How far the lamp stands off the plane of the dish. Above 0 it is in front of it and the cast shadows rake away from it, shortening as it rises; 0 is in the plane and throws the longest shadows; below 0 the lamp is behind the dish and every caster spills its shadow evenly onto the film in front of it |
| `[post] dof_max_px` | Depth of field (max CoC px at 1440p) | 0..16, step 0.5 | 0.0 | 9 | Turns the one global defocus into a real depth of field: each element is blurred by how far its own depth is from the plane of focus, up to this radius. 0 = off, and the whole per-pixel path is skipped. liquid_acid only |
| `[post] camera_focus` | Focus depth | 0..1, step 0.02 | 0.5 | 0.5 | Which depth is sharp. 0.5 is the plane the big masses sit on; lower favours the front droplets, higher the back ones |
| `[post] camera_field_curve` | Field curvature | 0..2, step 0.05 | 0.0 | 0.12 | Bends the surface of focus away from the dish with distance from the optical axis, so the centre and the corners of the frame cannot both be sharp -- what a real lens does |
| `[post] corner_warp` | Corner warp | 0..1, step 0.05 | 0.0 | 0 | Brief BN. The microscope field edge: outside corner_warp_r each pixel reads from a point turned toward the nearest frame diagonal (tangential stretch along the arcs, radius unchanged), so the corners look oddly distorted. Frame-fixed, applied to the post pass uv before every tap; grain/dither/overlay stay unwarped. The middle 40% of the frame is bit-identical at every value. Packed in rg1.x field 2. |
| `[post] corner_warp_r` |   corner warp start | 0.4..0.9, step 0.05 | 0.6 | 0.6 | Where the warp begins, 1 = a frame corner (square-normalised radius); 0.4 = just outside the centre 40% crop. Inert while corner_warp is 0 (written 0 then). rg1.x field 3. |
| `[post] focus_tilt` | Focus tilt | 0..2, step 0.05 | 0.0 | 0.26 | Slants the surface of focus (Lensbaby / freelensing): a strip of sharpness across the frame with the focus falling off smoothly to either side. 0 = level |
| `[post] focus_tilt_angle` | Focus tilt angle (deg) | -180..180, step 5 | 0.0 | 25 | Which way that strip runs. With a readjustment period set, this is the angle it is re-aimed around |
| `[post] focus_band_px` | Sharp band width (px at 1440p) | 40..1200, step 20 | 260.0 | 340 | How wide the sharp strip is on screen. Wide is a gentle depth of field, narrow is the freelensing sliver |
| `[post] focus_tilt_period` | Refocus period (s, 0 = never) | 0..300, step 5 | 0.0 | 75 | Mean seconds the focus plane holds still before somebody re-tilts the lens. Randomised around this, so it never feels scheduled. 0 = the lens is bolted down -- INERT on acid-rise-12 (why: time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect). |
| `[post] focus_tilt_move_s` | Refocus move (s) | 0.2..4, step 0.1 | 1.4 | 1.4 | How long one readjustment takes. A second or two, eased, with a slight overshoot and settle -- a hand letting go of a lens barrel |
| `[post] camera_fov` | Camera field of view (deg) | 0..90, step 2 | 0.0 | 28 | 0 = an orthographic scanner, every ring a symmetric circle. Above 0 the dish is seen from a lens at the tip of a view cone: off-axis rings foreshorten toward the axis, their far wall reads thicker and their highlight swings to the side facing the axis |
| `[post] camera_axis_x` | Optical axis x | 0..1, step 0.02 | 0.5 | 0.5 | Where the optical axis meets the dish: the one point seen face on, and the centre the field curvature and the tilt are measured from |
| `[post] camera_axis_y` | Optical axis y | 0..1, step 0.02 | 0.5 | 0.5 | The other half of that point |
| `[post] psf_px` | Point spread (px at 1440p) | 0..6, step 0.1 | 0.0 | 1.0 | The floor under the defocus radius, applied whatever the focus: no lens resolves a point to a point, so the sharpest thing in the frame is still this wide. At 1 px and up an in-focus edge can never come out as stair-stepped coverage AA |
| `[post] vignette_wander` | Vignette wander | 0..1, step 0.05 | 0.0 | 0.6 | Lets the vignette's centre follow the rig's lens, so the darkest corner turns over minutes instead of sitting in one corner of the panel for hours. 0 = pinned to the middle |
| `[post] pixel_shift_px` | Pixel-shift orbit (px at 1440p) | 0..8, step 0.5 | 0.0 | 3 | The OLED safety net under everything else: the whole finished frame walks a slow closed orbit of this radius, a few thousandths of a pixel per frame, so no feature ever holds one pixel. Invisible, and still moving |
| `[post] rig_readjust` | Rig readjust reach | 0..1, step 0.05 | 0.0 | 0.5 | How far the lamp and the lens centre re-aim when the focus readjusts. 0 = only focus and tilt move; above 0 the whole rig moves as one body on the same eased spring, which is the point of having a rig -- INERT on acid-rise-12 (why: time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect). |

## 6. Lid

The transparent cover glass over the dish: its ghost reflections, concentric ring ghosts, warm sheen, lamp glint, thin-film iridescence and its own refraction of those reflections. Master switch is `lid`; everything else is `lid_*` and inert while it is 0.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[post] lid` | Lid (cover glass) | 0..1, step 0.05 | 0.0 | 0.45 | Master for the transparent cover over the dish: its internal reflections, sheen, glint and iridescence. 0 = no cover at all |
| `[post] lid_ghost` |   lid ghosts | 0..1, step 0.05 | 0.0 | 0.24 | Dim, offset, slightly magnified copies of the bright film reflected inside the cover, tinted by the coating they bounced off |
| `[post] lid_ghost_spread` |   lid ghost spread | 0..2, step 0.05 | 1.0 | 1.0 | How far the ghost chain runs along the line from the lamp reflection through the optical centre |
| `[post] lid_rings` |   lid ring ghosts | 0..1, step 0.05 | 0.0 | 0.25 | Concentric coloured arcs -- the field reflected off a curved element (the LAPD optics look) |
| `[post] lid_sheen` |   lid sheen | 0..1, step 0.05 | 0.0 | 0.04 | A very wide, very weak warm smear where the lamp catches the cover |
| `[post] lid_sheen_px` |   lid sheen width (px) | 40..1400, step 20.0 | 420.0 | 400 | Width of that smear, in px at 1440p -- INERT on acid-rise-12 (why: gate not traced). |
| `[post] lid_glint` |   lid glint | 0..1, step 0.05 | 0.0 | 0.15 | The lamp's own reflection in the cover: a soft core with a wide amber halo |
| `[post] lid_iris` |   lid iridescence | 0..1, step 0.05 | 0.0 | 0.22 | Interference colours of the thin oil film on the cover, visible only across the sheen |
| `[post] lid_refract_px` |   lid refraction (px) | 0..12, step 0.5 | 0.0 | 2.5 | How much the uneven cover wobbles its own reflections (the transmitted picture is left alone) |
| `[post] lid_scratch` |   lid scratches | 0..1, step 0.05 | 0.0 | 0 | Wear on the cover (brief BM): hairline scratches fixed to the lid that catch the lamp only where they run across its light, so the pattern shifts as the lamp drifts. Clear over the dark, nearly gone on the bright film. 0 = off (needs Lid above 0). Not scaled by `lid`. |
| `[post] lid_scratch_density` |   lid scratch density | 0..1, step 0.05 | 0.5 | 0.5 | How many fine scratches there are where the wear is -- inert while lid_scratch is 0. |
| `[post] lid_scratch_len` |   lid scratch long gouges | 0..1, step 0.05 | 0.3 | 0.3 | 0 = only short micro-swirls. Higher adds up to eight long straight gouges across the cover -- inert while lid_scratch is 0. |
| `[post] lid_scratch_corner` |   lid scratch corners | 0..1, step 0.05 | 0.7 | 0.7 | Where the wear sits: 0 = all over the cover, 1 = only in the corners -- inert while lid_scratch is 0. |
| `[post] lid_scratch_soft` |   lid scratch softness | 0..1, step 0.05 | 0.2 | 0.2 | 0 = crisp 1 px hairlines that catch the light over a narrow angle; higher = wider, dimmer grooves that catch it over a wider one -- inert while lid_scratch is 0. |
| `[post] lid_scratch_tint` |   lid scratch tint | 0..1, step 0.05 | 0.0 | 0 | 0 = neutral white. 1 = the scratch takes the colour of the film under it (over black it stays white) -- inert while lid_scratch is 0. |
| `[post] glass_streaks` |   glass streaks | 0..1, step 0.05 | 0.0 | 0 | Brief BN. Two thin (~1.7 px FWHM at 1440p) bright wavy lines on the cover glass, brightest past the lamp reflection, in lid space (ride the lid wander/turn, waves travel slowly). Not gated by massDeep; kept off hot pixels so the peak cannot rise. Needs lid > 0; not scaled by it. rg1.y field 1. |

## 7. Film and post

The final composite trim applied to every look: film grain and its frame rate, halation and bloom and haze, camera glare, dust/hairs/scratches/light-leak artefacts, dither, thermal shimmer, plus the output-stage HDR settings (peak nits, gamut) and the frame rate cap.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[general] fps_limit` | FPS limit | 30..260, step 1 | 60.0 | 240 | Frame rate cap |
| `[hdr] peak_nits` | Peak brightness (nits, 0 = off) | 0..1500, step 5 | -1.0 | 1000 | HDR hot-spot target. 0 = match SDR |
| `[color] post_hue` | Hue rotate (deg) | 0..360, step 1 | 0.0 | 14.4 | Rotates all colors. Warning: rotates outside the hue band |
| `[liquid_acid] post_chroma` | Final chroma | 0.5..2, step 0.02 | 1.0 | 1.2 | Scales the finished frame's colourfulness about its own brightness. About 1.2 restores what a transparent film costs |
| `[liquid_acid] post_lift` | Final lift | 0.5..1.6, step 0.02 | 1.0 | 1.08 | Brightness multiplier on the finished frame |
| `[post] film_grain` | Film grain | 0..1, step 0.01 | 0.0 | 0.10 | Animated film grain over the finished frame, weighted into the mids and darks and kept off the peaks and off true black |
| `[post] film_grain_size` | Film grain size (px) | 0.5..6, step 0.25 | 1.5 | 2.5 | Pixels per grain cell. 1 = per-pixel noise, larger = coarser stock |
| `[post] film_grain_speed` | Film grain speed | 0..2, step 0.05 | 1.0 | 1 | Multiplier on the grain frame rate; 1 = the film_grain_fps rate, lower holds each pattern longer |
| `[post] film_grain_fps` | Film grain frame rate (fps) | 1..240, step 1 | 24.0 |  | How many grain patterns per second: 24 or 30 for a film cadence (both divide 240 exactly), 240 = every refresh. Also paces film_noise |
| `[post] film_grain_color` | Film grain colour (0 mono) | 0..1, step 0.05 | 0.0 | 0 | 0 = monochrome grain, 1 = independent RGB noise |
| `[post] film_grain_chroma` | Film grain chroma (1 = old) | 0..1, step 0.05 | 1.0 | 0 | 1 = the grain is an equal step on all three channels, which moves saturation and clips against black. 0 = it scales the pixel instead, so hue and saturation survive and black stays black |
| `[post] film_grain_density` | Film grain density curve | 0..1, step 0.05 | 0.0 | 1 | 0 = grain at full amplitude from just above black upward. 1 = the stock own density curve: nothing in the dense shadow, most of it in the mid-tones, nothing on a clean highlight |
| `[post] halo` | Bright-field halo | 0..1, step 0.01 | 0.0 | 0.12 | A soft bright glow hugging the outside of every dark shape, with a faint darker echo beyond it -- the microscope double contour (liquid_acid only) |
| `[post] halo_px` | Halo width (px at 1440p) | 1..40, step 1 | 12.0 | 12 | How wide that glow is. Wide and weak is the look; narrow and strong is a stroked line |
| `[post] band_min` | Minimum band widths | 0..1, step 0.05 | 1.0 | 1 | Floors every band around an edge (film edge, rim, meniscus, halo, penumbra) at a few px, so a small droplet is shaded like a big mass instead of getting a solid outline. 0 = bands proportional to each element's size |
| `[post] post_glow` | Camera glare | 0..1, step 0.02 | 0.0 | 0.22 | Weak wide veiling glare: the frame mixed with a wide blur of itself, so dark bleeds a little into bright and bright into dark around every edge, including a thin ring wall. 0 = off |
| `[post] post_glow_px` | Camera glare radius (px at 1440p) | 2..40, step 0.5 | 10.0 | 10 | How far the glare spreads |
| `[post] fog` | Light in the water: haze | 0..1, step 0.02 | 0.0 | 0.22 | The water itself glows near the off-view lamp and fades with distance, added only into the dark. It falls to exactly zero far from the lamp, so black stays black. 0 = off |
| `[post] fog_px` | Haze reach (px at 1440p) | 100..2000, step 25 | 700.0 | 700 | How far the glow of the water carries from the lamp |
| `[post] fog_mass_gate` | Haze: keep masses black | 0..1, step 0.05 | 0.0 | 0.7 | 0 = the haze lands wherever it is dark. 1 = it is kept out of the inside of a dark mass, which floats in front of the water, and still glows in the water beside it |
| `[post] artefact_lum_gate` | Masses: artefacts follow colour | 0..1, step 0.05 | 0.0 | 0 | Brief BO. Inside a mass, film grain (+ film_noise), the aberration split, the lid iridescence and the sheen / glint halo follow the mass's own brightness and colour (massDeep ring + saturation lift): a pure black mass stays clean, a dyed one keeps its texture, the edge band is untouched. 0 = today. Packed in rg1.x (first 6-bit field). |
| `[post] bloom` | Bloom (wide, weak) | 0..1, step 0.02 | 0.0 | 0.30 | The bright film bleeds a very wide, very weak wash into the black. Its radius breathes and the wash drifts with the lamp |
| `[post] bloom_px` | Bloom radius (px at 1440p) | 40..400, step 5 | 140.0 | 140 | How far that wash spreads |
| `[post] bloom_warmth` |   bloom warmth (lamp colour) | 0..1, step 0.05 | 0.0 | 0 | Brief BN. The keyed bloom takes the lamp colour instead of the film colour: hot yellow-white on the lamp side, red away from it (the LAPD tile veil). Luminance-normalised tint, so mean_lum/ABL do not move. rg1.x field 4. |
| `[post] film_dust` | Film dust | 0..1, step 0.02 | 0.0 | 0.10 | Specks of dust on the film: sparse bright points, a new scattering every film frame. Additive and weighted into the dark, so they are stars on the black and nothing on the bright film |
| `[post] film_hairs` | Film hairs | 0..1, step 0.02 | 0.0 | 0.12 | How often a curly hair is caught in the gate. Each one sticks for a few seconds, flutters, and is gone -- INERT on acid-rise-12 (why: gate not traced). |
| `[post] film_scratches` | Film scratches | 0..1, step 0.02 | 0.0 | 0 | Faint near-vertical scratches that persist for a stretch, drift sideways and disappear |
| `[post] film_leak` | Film light leak | 0..1, step 0.02 | 0.0 | 0.06 | A coloured leak at one edge -- warm core, cool fringe, soft bands -- that swells and dies, and does not come back every time -- INERT on acid-rise-12 (why: gate not traced). |
| `[post] film_artefact_rate` | Film artefact rate (s) | 1..30, step 0.5 | 5.0 | 5 | How long one population of hairs lasts. Scratches change three times slower, the leak five times slower, the dust every film frame |
| `[post] film_noise` | Film noise | 0..1, step 0.01 | 0.0 | 0 | A second, finer and faster noise layer under the coarse grain: the emulsion's own fizz as against the stock's grain structure |
| `[post] film_noise_size` | Film noise size (px at 1440p) | 0.5..4, step 0.25 | 1.0 | 1 | Cell size of that finer layer, in px at 1440p |
| `[post] film_stock` | Film stock colour | 0..1, step 0.02 | 0.0 | 0.15 | The stock's own grade, applied last: lifted teal shadows, warm highlights, a slightly different curve per channel |
| `[post] post_glow_dark` | Camera glare, dark bias | 0..1, step 0.05 | 0.5 | 0.6 | Leans the glare toward the dark side: shadows of dark features bleed into the bright film more than the film's light bleeds into the black |
| `[post] dither` | Output dither (LSB of 10 bit) | 0..4, step 0.25 | 0.0 | 1 | Half an LSB of ordered blue-ish noise on the finished frame, in the domain the panel quantises in. Breaks the 10-bit steps that show as bands on a big saturated flat. It fades out into true black, so an off pixel stays off. 1 = half an LSB |
| `[post] halation` | Halation | 0..1, step 0.02 | 0.0 | 0.25 | CineStill's missing anti-halation layer: a tight warm glow bleeding out of the bright film into the dark around it. Taken only from what is genuinely bright, and it never lands on the highlight itself. Much tighter than Bloom, which is the wide weak wash |
| `[post] halation_px` | Halation radius (px at 1440p) | 4..60, step 1 | 14.0 | 14 | How far the glow reaches. A dozen pixels is the film look; far more and it becomes a second bloom |
| `[post] halation_warmth` | Halation warmth | 0..1, step 0.05 | 0.75 | 0.75 | 0 = a colourless glow. 1 = the red-orange of light that has crossed the emulsion twice, which is the CineStill signature |
| `[post] halation_threshold` |   halation threshold (haze) | 0..0.6, step 0.05 | 0.0 | 0 | Brief BN. Lowers the halation knee from 0.55..1.05 (0) to 0.30..0.65 (0.5) of SDR white (the key reaches 0.05..0.25 at 1, but on acid-rise-12 0.5 and 1.0 measure the same, so the slider stops at 0.6), so mid-tones scatter too and halation becomes a diffusion haze: the base is never blurred, only veiled. Pair with halation_px. rg1.y field 2. |
| `[post] shimmer` | Thermal shimmer | 0..1, step 0.02 | 0.0 | 0.35 | The air above the lamp. A very fine refractive wobble, strongest near the light and fading to nothing away from it, and carried by the sim's own velocity -- a burst that shoves the oil shoves the heat above it too |
| `[post] shimmer_px` | Shimmer amount (px at 1440p) | 0..6, step 0.1 | 1.5 | 0.8 | How far the wobble displaces the picture at its strongest. A pixel or two is heat; more is water |
| `[hdr] gamut` | Output gamut (sRGB / Display-P3 / BT.2020) | 0..2 (radio buttons) | 2 | 1 | Output colour gamut for the HDR swapchain (radio buttons, not a slider row) |

## 8. Ink look

The Beer-Lambert "ink in water" render style ([ink] section, `[look] style = ink`): optical density, edge darkening at folds, the duotone tint pair and its rotation, HDR core lift gated by motion, and the parallax second layer.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[ink] density` | Ink density k | 0.5..8, step 0.1 | 3.0 |  | Beer-Lambert absorption. High = thin veils already opaque |
| `[ink] chroma` | Ink chroma (0 = neutral black) | 0..3, step 0.05 | 0.0 | 0 | How much the dye's own hue tints the transmitted light |
| `[ink] edge_strength` | Edge darkening (folds) | 0..1, step 0.02 | 0.35 |  | Extra optical path where the density gradient is steep = sheets seen edge-on |
| `[ink] edge_lo` | Edge threshold (lo) | 0..0.5, step 0.005 | 0.02 |  | Gradient magnitude the fold darkening starts at |
| `[ink] edge_hi` | Edge threshold (hi) | 0..0.5, step 0.005 | 0.25 |  | Gradient magnitude the fold darkening saturates at |
| `[ink] edge_scale` | Edge tap spacing (px) | 1..6, step 0.5 | 2.0 |  | Screen texels between the gradient taps |
| `[ink] vignette` | Paper vignette | 0..0.6, step 0.02 | 0.15 |  | Radial darkening of the backlit paper (paper mode only) |
| `[ink] core_knee` | Core knee (inverted) | 0..1, step 0.02 | 0.30 |  | Opacity where the tint crosses from the thin-veil colour to the core colour |
| `[ink] veil_floor` | Veil floor (inverted) | 0..0.6, step 0.02 | 0.0 |  | Faint dye below this opacity goes to the background instead of a haze |
| `[ink] tint_mid_dip` | Midpoint dip to dark | 0..1, step 0.05 | 0.0 |  | Darken the duotone blend around its midpoint so mid-density ink is dark, not grey-brown |
| `[ink] pair_sweep_period` | Duotone pair rotation (s, 0=off) | 0..900, step 10 | 0.0 |  | Cross-fade tint_thin/tint_thick through the curated complementary pairs. 0 = the fixed pair |
| `[ink] hdr_core` | HDR core (inverted) | 0..1.4, step 0.02 | 1.0 |  | How hard dense cores drive the HDR highlight gain. Veils stay SDR |
| `[ink] motion_lo` | HDR motion gate: still (texels/s) | 0..60, step 1 | 3.0 |  | Below this local speed, ink gets no HDR lift at all - keeps the entry patch from blowing out |
| `[ink] motion_hi` | HDR motion gate: moving | 0..200, step 5 | 40.0 |  | Above this local speed the HDR lift is full. Set at or below the still value to disable the gate |
| `[ink] motion_opacity` | Motion gate on opacity | 0..1, step 0.02 | 0.0 |  | How much the motion gate also thins stationary ink. 0 = HDR lift only |
| `[ink] parallax` | Parallax second layer | 0..1, step 0.02 | 0.0 |  | Adds the same dye at another scale as extra depth. 0 = off |
| `[ink] parallax_scale` | Parallax scale | 0.8..1, step 0.005 | 0.92 |  | How much bigger the second layer reads |
| `[ink] inverted` | Inverted (pale ink on black) | on/off | false | 1 | Off = dark ink on backlit paper. On = pale ink on black (OLED-friendly) |
| `[ink] black_nits` | *(no slider)* | — | 0.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] parallax_drift` | *(no slider)* | — | 0.004 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] tint_hue_blend` | *(no slider)* | — | 0.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] tone_chroma` | *(no slider)* | — | 1.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] tone_knee` | *(no slider)* | — | 0.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] tonemap` | *(no slider)* | — | 0 |  | No control in the settings window — read from the ini as an int (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] white_nits` | *(no slider)* | — | 0.0 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[ink] paper_color` | *(no slider — colour triple, hand-edit only)* | — | 0.90 0.91 0.93 | 0.00 0.00 0.00 | Backlit paper colour (paper mode only, i.e. inverted=0). |
| `[ink] tint_thin` | *(no slider — colour triple, hand-edit only)* | — | 0.70 0.85 1.00 | 0.85 0.85 0.85 | Duotone tint for the thinnest veils. |
| `[ink] tint_thick` | *(no slider — colour triple, hand-edit only)* | — | 1.00 1.00 1.00 | 1.00 1.00 1.00 | Duotone tint for the most opaque core. |

## 9. Display and behaviour

How the wallpaper behaves on the desktop rather than what the fluid looks like: pausing for fullscreen/maximized apps, mouse-driven splats, idle bursts, mood cycling between look recipes, the falling ink-drop emitter, screen mirroring/kaleidoscope, and autostart.

| Key | Label | Range (min..max, step) | Default | Live (acid-rise-12) | What it does |
|---|---|---|---|---|---|
| `[moods] dwell_minutes` | Dwell (min per mood) | 1..30, step 1 | 3.0 |  | Minutes in a mood before switching |
| `[moods] transition_seconds` | Transition length (s) | 2..60, step 1 | 4.0 |  | Seconds a mood change takes |
| `[moods] jitter` | Timing jitter (± fraction) | 0..0.5, step 0.05 | 0.3 |  | Random +/- on dwell time so switches feel organic |
| `[behavior] idle_interval` | Interval (s) | 0.5..30, step 0.5 | 9.6 | 9.6 | Seconds between random blob bursts |
| `[behavior] idle_amount` | Amount per burst | 1..30, step 1 | 8 | 8 | Blobs per burst |
| `[behavior] idle_brightness` | Burst brightness | 0.2..3, step 0.05 | 1.5 | 1.50 | Idle blob intensity (emitters paint at 0.15) |
| `[drops] interval` | Interval (s) | 2..90, step 1 | 14.0 |  | Seconds between ink drops (jittered +-35%) |
| `[drops] speed` | Entry speed (downward) | 0..3000, step 25 | 700.0 |  | Downward velocity impulse. This is what rolls the head into a cap |
| `[drops] radius` | Drop radius (%) | 0.02..2, step 0.01 | 0.35 |  | Splat radius of the drop head, same units as splat radius |
| `[drops] density` | Drop density | 0.1..4, step 0.05 | 1.35 |  | Dye intensity of the head. 1.35 = a fully opaque core |
| `[drops] tail_sec` | Entry trail (s) | 0..4, step 0.1 | 0.6 |  | How long dye keeps feeding in at the entry point after the impulse |
| `[drops] tail_density` | Entry trail density | 0..1, step 0.02 | 0.25 |  | Intensity of that trail |
| `[drops] spatter` | Splash droplets | 0..12, step 1 | 0 |  | Satellite droplets around the entry (ref 2). Each one costs a full dye pass |
| `[drops] spatter_radius` | Splash radius (%) | 0.01..0.4, step 0.005 | 0.04 |  | Size of each satellite droplet |
| `[drops] spatter_speed` | Splash speed | 0..2000, step 25 | 500.0 |  | Outward impulse of the satellites |
| `[drops] tail_radius_frac` | Entry stream width (x drop radius) | 0.05..1, step 0.01 | 0.22 |  | Keep this small: a wide entry stamp reads as a bright orb parked at the injection point |
| `[drops] tail_speed` | Entry stream pull (x drop speed) | 0..1, step 0.02 | 0.0 |  | Downward impulse on the entry stream so it feeds the stem instead of parking |
| `[drops] impulse_spread` | Impulse spread (x drop radius) | 0.5..4, step 0.05 | 2.2 |  | How much wider the velocity impulse is than the dye stamp. Below ~1.5 the dye's outer halo parks at the entry as a bright orb |
| `[drops] asymmetry` | Lobe asymmetry | 0..1, step 0.02 | 0.35 |  | 0 = a textbook symmetric vortex pair. Higher = unequal lobes, one side leading |
| `[drops] y_max` | Entry band bottom (uv y) | 0.02..0.9, step 0.01 | 0.22 |  | How far down the screen a drop may enter |
| `[mirror] mode` | Mode (0 off 1 horiz 2 vert 3 quad 4 kaleido) | 0..4, step 1 | 0 |  | Folds the picture about a centre. 3 = the 4-fold quad; 4 = wedges around the centre |
| `[mirror] segments` | Kaleidoscope segments | 2..16, step 1 | 6 |  | Number of wedges in mode 4. Every other one is reflected, so there is no jump at a wedge edge |
| `[mirror] source` | Shown quadrant (0-3) | 0..3, step 1 | 0 |  | Which quarter of the sim is the one you see, and is copied around: +1 = right half, +2 = bottom half |
| `[mirror] center_x` | Fold centre x | 0.1..0.9, step 0.01 | 0.5 |  | Where the vertical seam sits |
| `[mirror] center_y` | Fold centre y | 0.1..0.9, step 0.01 | 0.5 |  | Where the horizontal seam sits |
| `[mirror] rotate_period` | Rotation (s per turn, 0=fixed) | 0..600, step 5 | 0.0 |  | Slowly turns the kaleidoscope's fold axes about the centre |
| `[mirror] drift` | Centre drift | 0..1, step 0.02 | 0.0 |  | Slow wander of the fold point, so the seam is not glued to the middle of the screen |
| `[mirror] soft` | Seam softness (uv) | 0..0.05, step 0.002 | 0.0 |  | Rounds the fold off over this band so the mirror crease is not a hard line. 0 = a hard mirror |
| `[behavior] idle_splats` | Idle random splats | on/off | true | 0 | Random blobs when the field is calm |
| `[behavior] hold_to_splat` | Hold left mouse = pour dye | on/off | true | 0 | Hold the left button to pour a continuous dye stream |
| `[behavior] splat_on_click` | Splat on click (if not holding) | on/off | true | 0 | Each click splats a single dye blob |
| `[behavior] show_mouse` | Mouse movement stirs fluid | on/off | true | 0 | Moving the mouse pushes currents through the fluid |
| `[general] pause_on_fullscreen` | Pause on fullscreen app | on/off | true | 1 | Pause the wallpaper while a fullscreen app has focus |
| `[general] pause_on_maximized` | Pause on maximized app | on/off | true |  | Pause the wallpaper while a maximized window has focus |
| `[moods] enabled` | Mood cycling (auto-switch looks) | on/off | false | 0 | Auto-switch between moods/*.ini recipes |
| `[general] mirror_second` | Mirror on second monitor | on/off | false | 0 | Also render the wallpaper on the second monitor |
| `[drops] drops` | Ink drops | on/off | false |  | Periodic falling ink drops. Works with any render look |
| `[drops] obey_governor` | Drops obey the fullness governor | on/off | true |  | Skip a drop while the water is already full of ink |
| *(none — registry)* | Start with Windows | on/off | off (not registered) |  | Registers/unregisters FluidWallpaper.exe under HKCU Run. Not backed by any ini key, so it has no default/live ini value and survives a settings.ini delete. |
| `[drops] color_mode` | *(no slider)* | — | 0 |  | No control in the settings window — read from the ini as an int (`main.cpp`), no help text in `settings.cpp`. |
| `[drops] spatter_spread` | *(no slider)* | — | 0.05 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[drops] x_max` | *(no slider)* | — | 0.85 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[drops] x_min` | *(no slider)* | — | 0.15 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[drops] y_min` | *(no slider)* | — | 0.04 |  | No control in the settings window — read from the ini as a float (`main.cpp`), no help text in `settings.cpp`. |
| `[drops] color` | *(no slider — colour triple, hand-edit only)* | — | 1.0 1.0 1.0 (neutral, no tint) |  | Optional tint on the falling ink drop's dye (r g b); 1 1 1 = whatever the current palette would emit anyway. |
| `[moods] migrated` | *(no slider — internal migration flag)* | — | 0 | 1 | One-time flag: this ini has already been migrated from the old [cycle] format. Set once by the app, not meant for hand-editing. |
| `[moods] base_mood` | *(no slider — string, read in moods.cpp)* | — | "" (empty; falls back to "Neon" the first time) |  | Name of the mood the app boots into before mood cycling takes over. |
| `[moods] skip` | *(no slider — string, read in moods.cpp)* | — | "" (no moods skipped) |  | Comma-separated list of mood names excluded from the auto-cycle rotation. |
| `[moods] early_switch_darkpct` | *(no slider — int, read in moods.cpp)* | — | 92 |  | The field being this % dark counts as "decayed" — a discreet moment to leave a mood early instead of waiting out the full dwell. |
| `[moods] min_dwell_seconds` | *(no slider — int, read in moods.cpp)* | — | 60 |  | Never switch moods before this many seconds, however dark the field gets. |
| `[cycle] enabled` | *(no slider — legacy, superseded key)* | — | 0 | 0 | Legacy pre-mood-conductor cycling flag, read once during ini migration and then superseded by `[moods] enabled`. |

## 10. Unsorted

Every key above was placeable into one of the nine named groups by section or by the vocabulary in its own help text; none needed this bucket. Kept per instructions.

*(no keys)*

## Known inert or unverified keys

Keys that are present, wired up and carry a live value in `acid-rise-12.ini`, but that are known — or found by this pass — not to be doing anything visible right now:

| Key | Live | Why it's listed |
|---|---|---|
| `[liquid_acid] meniscus` | 0.85 | INERT in acid-rise-12: `haloInk = lerp(1, smoothstep(0.03,0.40,ilo), meniscus_from_ink)` reads the ink brightness just outside the oil, which is black everywhere in this preset, so the gate is 0 at every pixel and `meniscus = 0` renders byte-identical to `meniscus = 0.85`. Confirmed by measurement (audit 2026-09-22 (3), executor G's A/B); live only via `meniscus_film_mix`, shipped 0. |
| `[liquid_acid] rim_dark` | 0.80 | INERT in acid-rise-12 for the same reason as `meniscus` above: `rimK` is gated by the same `haloInk` ink-brightness read, which is 0 everywhere on this preset's black ink surround. |
| `[liquid_acid] swarm_lens` | 0.80 | INERT in acid-rise-12 by code, not by gate: the swarm-lens shading only runs inside the screen-space swarm blocks, which are forced to 0 (`swarm_holes`/`swarm_drops`) whenever the droplet particle sim is active — and this preset runs 950 droplets. |
| `[post] shimmer` | 0.35 | Invisible in headless stills at the shipped value — the heat-haze wobble only reads in motion. Verdict deferred to a live on-panel tour (`--tour`, brief item AI), not yet confirmed either way. |
| `[post] lid` | 0.45 | The cover-glass reflections are on at a value the user did not notice at the subtle preset (brief item AF: "weak lid reflections, raise ghost/rings/glint/iris"). Live, just under-strength; also gated indirectly by brief BD's unkeyed `massDeep` fade on the sheen/iridescence halo along mass interiors. |
| `[sim] baroclinic` | 0.000 | Default 0 = off per its own help text, and `acid-rise-12.ini` ships it at exactly that default — the dye-front torque is not acting in the live preset. |
| `[sim] gravity` | 0 | Default 0 = off per its own help text, and the live preset leaves it at 0 — dye is not weighted up or down in the live preset. |
| `[color] hue_linger` | 0 | Default 0 = off ("fraction of each half-lap spent resting on one hue"), and the live preset leaves it at 0 — the base fluid look's hue never lingers on a band edge. |
| `[liquid_acid] film_hue3_amt` | 0 | Its own help text says "0 = off, which is the default", and the live preset does not override it — the optional third hue field is defined (`film_hue3` = angle) but contributes nothing. |
| `[post] post_blur_px` | 0 | Default 0 = off ("the whole per-pixel path is skipped"), and the live preset leaves it there — no extra whole-frame camera defocus beyond what depth of field already applies. |

`[liquid_acid] dye_hue` (285 in acid-rise-12) is REMOVED from this list as of `c5e78d3`: the dye is
now applied directly to `inkC` in the display pass (a translucent wax reading against the lamp),
which does reach the frame — confirmed by a hue-vs-hue delta sheet (max|delta| 143/255, 43% of
pixels differ by more than 2), not merely re-asserted. It was listed here because the two earlier
attempts patched a part of the ink ramp that is dead code under this preset's `ink_mode = water`.

### Brief BJ's 30-key sweep

`reference/reports/key-effect-acid-rise-12-1d2786a.md` (brief BJ, `tools/key-effect.ps1`) rendered
`acid-rise-12` at its live value against each key's fluid.h/settings.cpp default and found 30 keys
byte-identical (MAD 0.000): every `ink_*`/`seam_strength`/`seam_hi` key (dead code — this preset's
`ink_mode = water` skips the whole `ink_mode == bands` branch in `kDisplaySrc`), every `swarm_*` key
plus `swarm_lens` (the procedural swarm layers are zeroed in `fluid.cpp` whenever `droplets > 0`,
and this preset runs 950), `rim_vary`/`rim_ink_follow`/`rim_inset` (the same `haloInk` ink-brightness
gate as `meniscus`/`rim_dark` above — see line 514 — zeroes the whole rim+halo annulus these three
only shape), `grain` (dropped per its own rule above because `[post] film_grain` is already running),
and `rig_readjust`/`focus_tilt_period` (time/motion-named keys — a 10 s still can't show a slow or
periodic effect). Each of these has its own inline "INERT on acid-rise-12 (why: ...)" note next to
its entry above. Five more from the same sweep — `oil_ink_blur`, `oil_dye_block`, `film_hairs`,
`film_leak`, `lid_sheen_px` — were also byte-identical but the report gives no reason and none was
traced for this pass; their inline notes say "gate not traced".

## Totals

**430 keys total** (ini-backed; excludes the registry-only "Start with Windows" row), **358 with sliders**, **72 read-only** (no control in the settings window, main.cpp-only), **39 inert or unverified** in the current live preset (the original 10 above plus 29 new from brief BJ's 30-key sweep, one of which — `swarm_lens` — was already counted).

