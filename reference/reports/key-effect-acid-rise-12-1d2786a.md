# Key-effect report -- acid-rise-12 @ 1d2786a

(Actually run at HEAD f2cfd87, the tip of main at run time; `git diff --stat 1d2786a f2cfd87 --
src/fluid.h src/settings.cpp reference/configs/acid-rise-12.ini` is empty, so the three files this
report is derived from -- the preset, the field defaults and the slider table -- are byte-identical
between the two commits and this reading holds at either hash.)

(Correction: the first pass of this run resolved `sweep_count`'s default as the literal text
`kSweepPairs` -- a named C++ constant in `src\fluid.h`, not a number -- and wrote that string into
the temp ini, which the ini reader would have parsed as 0, not the constant's real value 5. Fixed
in `tools\key-effect.ps1` (it now resolves a bare-identifier default via a file-wide
`static const`/`constexpr`/`#define` search) and this one key was re-rendered; the row below is the
corrected result (default 5, MAD 0.604).)

Exe: `C:\Users\abg77\fw-slots\build2\FluidWallpaper.exe`
Ini: `C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\reference\configs\acid-rise-12.ini`
Sections tested: liquid_acid, post
Size: 1280x720, delay: 10s, seed 1234, --hdr on, --shot-yield 8
Baseline: `C:\Users\abg77\AppData\Local\Temp\claude\C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code\8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0\scratchpad\key-effect\acid-rise-12-baseline.png` md5 `D7EC6E19F509DE46DE7718F67CFFA59C`

## Results (sorted by MAD ascending)

| Key | Section | Live | Default | INERT? | MAD | Note |
|---|---|---|---|---|---|---|
| ink_levels | liquid_acid | 4 | 5.0 | YES | 0.000 | INERT on this frame |
| swarm_dark | liquid_acid | 0.35 | 0.80 | YES | 0.000 | INERT on this frame |
| swarm_holes | liquid_acid | 0 | 0.90 | YES | 0.000 | INERT on this frame |
| swarm_density | liquid_acid | 0.2674 | 0.55 | YES | 0.000 | INERT on this frame |
| oil_ink_blur | liquid_acid | 0.5 | 0.0 | YES | 0.000 | INERT on this frame |
| rig_readjust | post | 0.5 | 0.0 | YES | 0.000 | INERT on this frame; time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| swarm_lens | liquid_acid | 0.80 | 0.0 | YES | 0.000 | INERT on this frame |
| film_hairs | post | 0.12 | 0.0 | YES | 0.000 | INERT on this frame |
| ink_soft | liquid_acid | 0.60 | 0.42 | YES | 0.000 | INERT on this frame |
| ink_complement_lock | liquid_acid | 0 | 1 | YES | 0.000 | INERT on this frame |
| ink_bias | liquid_acid | 0.03 | 0.05 | YES | 0.000 | INERT on this frame |
| ink_hue_vary | liquid_acid | 3 | 14.0 | YES | 0.000 | INERT on this frame |
| oil_dye_block | liquid_acid | 0.7 | 0.0 | YES | 0.000 | INERT on this frame |
| swarm_scale_drops | liquid_acid | 30 | 34.0 | YES | 0.000 | INERT on this frame |
| swarm_drops | liquid_acid | 0 | 0.55 | YES | 0.000 | INERT on this frame |
| grain | liquid_acid | 0 | 0.030 | YES | 0.000 | INERT on this frame |
| rim_ink_follow | liquid_acid | 0.5 | 0.0 | YES | 0.000 | INERT on this frame |
| swarm_clump | liquid_acid | 0.85 | 0.70 | YES | 0.000 | INERT on this frame |
| rim_vary | liquid_acid | 0.4 | 0.0 | YES | 0.000 | INERT on this frame |
| seam_strength | liquid_acid | 0.30 | 0.70 | YES | 0.000 | INERT on this frame |
| film_leak | post | 0.06 | 0.0 | YES | 0.000 | INERT on this frame |
| lid_sheen_px | post | 400 | 420.0 | YES | 0.000 | INERT on this frame |
| rim_inset | liquid_acid | 0.0022 | 0.0013 | YES | 0.000 | INERT on this frame |
| focus_tilt_period | post | 75 | 0.0 | YES | 0.000 | INERT on this frame; time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| swarm_r_max | liquid_acid | 0.420 | 0.430 | YES | 0.000 | INERT on this frame |
| ink_mix | liquid_acid | 0.95 | 0.88 | YES | 0.000 | INERT on this frame |
| swarm_scale_holes | liquid_acid | 22 | 26.0 | YES | 0.000 | INERT on this frame |
| seam_hi | liquid_acid | 0.70 | 0.45 | YES | 0.000 | INERT on this frame |
| swarm_r_min | liquid_acid | 0.120 | 0.045 | YES | 0.000 | INERT on this frame |
| ink_gain | liquid_acid | 1.30 | 2.30 | YES | 0.000 | INERT on this frame |
| film_dust | post | 0.10 | 0.0 | no | 0.000 | no visible effect on this frame |
| droplet_ring_lift | liquid_acid | 0.08 | 0.10 | no | 0.001 | no visible effect on this frame |
| fog_mass_gate | post | 0.7 | 0.0 | no | 0.003 | no visible effect on this frame |
| psf_px | post | 1.0 | 0.0 | no | 0.003 | no visible effect on this frame |
| oil_texture | liquid_acid | 0.0759 | 0.07 | no | 0.007 | no visible effect on this frame |
| lid_refract_px | post | 2.5 | 0.0 | no | 0.020 | no visible effect on this frame |
| rise_respawn | liquid_acid | 1 | 0 | no | 0.023 | no visible effect on this frame; time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| post_glow_dark | post | 0.6 | 0.5 | no | 0.023 | no visible effect on this frame |
| speckle | liquid_acid | 0.2227 | 0.12 | no | 0.027 | no visible effect on this frame |
| oil_penumbra | liquid_acid | 0.3 | 0.0 | no | 0.032 | no visible effect on this frame |
| shimmer_px | post | 0.8 | 1.5 | no | 0.049 | no visible effect on this frame |
| shimmer | post | 0.35 | 0.0 | no | 0.056 |  |
| dither | post | 1 | 0.0 | no | 0.056 |  |
| softness | post | 0.5 | 0.0 | no | 0.076 |  |
| vignette_wander | post | 0.6 | 0.0 | no | 0.081 |  |
| shadow_soft | liquid_acid | 0.550 | 0.50 | no | 0.084 |  |
| aberration_field | post | 1.2 | 0.5 | no | 0.089 |  |
| film_stock | post | 0.15 | 0.0 | no | 0.096 |  |
| film_grain_density | post | 1 | 0.0 | no | 0.099 |  |
| mass_rim | liquid_acid | 0.35 | 0.0 | no | 0.099 |  |
| oil_edge_curve | liquid_acid | 1 | 0.0 | no | 0.120 |  |
| focus_band_px | post | 340 | 260.0 | no | 0.129 |  |
| diffraction | liquid_acid | 1 | 0.0 | no | 0.134 |  |
| fog | post | 0.22 | 0.0 | no | 0.159 |  |
| lid_rings | post | 0.25 | 0.0 | no | 0.191 |  |
| camera_fov | post | 28 | 0.0 | no | 0.234 |  |
| lid_sheen | post | 0.04 | 0.0 | no | 0.257 |  |
| focus_tilt_angle | post | 25 | 0.0 | no | 0.312 |  |
| refraction_width | liquid_acid | 22 | 0.0 | no | 0.354 |  |
| halation | post | 0.25 | 0.0 | no | 0.356 |  |
| lid_glint | post | 0.15 | 0.0 | no | 0.377 |  |
| lid_iris | post | 0.22 | 0.0 | no | 0.409 |  |
| lid_ghost | post | 0.24 | 0.0 | no | 0.422 |  |
| camera_field_curve | post | 0.12 | 0.0 | no | 0.424 |  |
| post_glow | post | 0.22 | 0.0 | no | 0.532 |  |
| translucency | liquid_acid | 0.10 | 0.16 | no | 0.539 |  |
| dye_depth_tilt | liquid_acid | 0.28 | 0.0 | no | 0.541 |  |
| droplet_ring_width | liquid_acid | 0.13 | 0.08 | no | 0.541 |  |
| oil_refract_body | liquid_acid | 0.006 | 0.0 | no | 0.571 |  |
| sweep_count | liquid_acid | 8 | 5 | no | 0.604 |  |
| droplet_spec | liquid_acid | 0.35 | 0.0 | no | 0.616 |  |
| aberration_px | post | 1.8 | 0.8 | no | 0.625 |  |
| halo | post | 0.12 | 0.0 | no | 0.678 |  |
| boundary_reflect_r | liquid_acid | 0.33 | 0.0 | no | 0.693 |  |
| droplet_depth | liquid_acid | 0.35 | 0.0 | no | 0.695 |  |
| vignette | post | 0.12 | 0.0 | no | 0.750 |  |
| film_hue2_wobble | liquid_acid | 10 | 0.0 | no | 1.017 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| hue_rotate_period | liquid_acid | 3600 | 0.0 | no | 1.081 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| focus_tilt | post | 0.26 | 0.0 | no | 1.238 |  |
| pixel_shift_px | post | 3 | 0.0 | no | 1.314 |  |
| film_grain_chroma | post | 0 | 1.0 | no | 1.334 |  |
| oil_glow | liquid_acid | 0.45 | 0.0 | no | 1.368 |  |
| dof_max_px | post | 9 | 0.0 | no | 1.378 |  |
| film_grain | post | 0.10 | 0.0 | no | 1.383 |  |
| film_hue2_rise | liquid_acid | 0.3 | 0.25 | no | 1.411 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| lid | post | 0.45 | 0.0 | no | 1.516 |  |
| meniscus_from_ink | liquid_acid | 1.0 | 0.0 | no | 1.704 |  |
| oil_iridescence | liquid_acid | 0.20 | 0.0 | no | 1.738 |  |
| bloom | post | 0.30 | 0.0 | no | 1.811 |  |
| film_grain_size | post | 2.5 | 1.5 | no | 1.823 |  |
| aberration | post | 1 | 0.0 | no | 1.831 |  |
| cellulose | liquid_acid | 0.45 | 0.0 | no | 1.876 |  |
| ink_mode | liquid_acid | water | 0 | no | 1.901 |  |
| rise_stretch | liquid_acid | 0.45 | 0.0 | no | 1.917 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| shadow_amt | liquid_acid | 0.400 | 0.0 | no | 2.177 |  |
| droplet_crust_r | liquid_acid | 0.6 | 1.0 | no | 2.556 |  |
| oil_thin_edge | liquid_acid | 0.70 | 0.0 | no | 3.061 |  |
| dye_hue | liquid_acid | 285 | 0.0 | no | 4.310 |  |
| dye_sat | liquid_acid | 0.8 | 0.0 | no | 5.894 |  |
| dye_lum | liquid_acid | 0.44 | 0.0 | no | 5.894 |  |
| post_lift | liquid_acid | 1.08 | 1.0 | no | 7.381 |  |
| droplet_lens | liquid_acid | 0.55 | 0.0 | no | 7.382 |  |
| droplets | liquid_acid | 950 | 0 | no | 8.385 |  |
| oil_specular | liquid_acid | 0.35 | 0.0 | no | 9.034 |  |
| droplet_ring_r_mul | liquid_acid | 3.5 | 1.0 | no | 9.600 |  |
| droplet_ring_big_frac | liquid_acid | 0.10 | 0.0 | no | 9.600 |  |
| rise_bottom_light | liquid_acid | 0.8 | 0.0 | no | 9.606 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| droplet_ring_frac | liquid_acid | 0.15 | 0.0 | no | 10.339 |  |
| weather | liquid_acid | 0.5 | 0.0 | no | 10.583 |  |
| post_chroma | liquid_acid | 1.2 | 1.0 | no | 10.634 |  |
| droplet_racer_speed | liquid_acid | 2.0 | 2.5 | no | 10.781 |  |
| droplet_r_max | liquid_acid | 0.0132 | 0.0120 | no | 10.915 |  |
| droplet_coalesce_s | liquid_acid | 1.0 | 0.0 | no | 11.030 |  |
| oil_drag | liquid_acid | 0.9 | 0.0 | no | 11.095 |  |
| droplet_attract | liquid_acid | 0.50 | 0.40 | no | 11.364 |  |
| droplet_ring_clump | liquid_acid | 0.6 | 0.0 | no | 11.480 |  |
| dissolve_s | liquid_acid | 5 | 0.0 | no | 11.635 |  |
| rim_width | liquid_acid | 0.0016 | 0.0013 | no | 11.766 |  |
| droplet_r_min | liquid_acid | 0.0011 | 0.0009 | no | 11.774 |  |
| droplet_racer_r_max | liquid_acid | 0.0050 | 0.0 | no | 12.255 |  |
| droplet_mass_bias | liquid_acid | 0.6 | 0.0 | no | 12.353 |  |
| droplet_crust_density | liquid_acid | 2.5 | 1.0 | no | 12.353 |  |
| hole_weight | liquid_acid | 0.608 | 0.75 | no | 12.697 |  |
| bubble_frac | liquid_acid | 0.20 | 0.22 | no | 12.788 |  |
| droplet_racer_frac | liquid_acid | 0.25 | 0.0 | no | 12.950 |  |
| depth_rise | liquid_acid | 0.22 | 0.0 | no | 13.008 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| conserve_mass | liquid_acid | 1 | 0.0 | no | 13.639 |  |
| repulsion | liquid_acid | 0.90 | 0.55 | no | 13.938 |  |
| droplet_coalesce | liquid_acid | 1 | 0.0 | no | 13.991 |  |
| big_bias | liquid_acid | 0.62 | 0.55 | no | 14.183 |  |
| disc_min | liquid_acid | 0.160 | 0.250 | no | 14.286 |  |
| threshold | liquid_acid | 0.85 | 0.50 | no | 14.711 |  |
| support_scale | liquid_acid | 2.00 | 2.20 | no | 14.718 |  |
| disc_max | liquid_acid | 0.380 | 0.460 | no | 14.867 |  |
| rise_wobble | liquid_acid | 0.5 | 0.0 | no | 15.226 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| web_min | liquid_acid | 0.070 | 0.110 | no | 15.754 |  |
| web_frac | liquid_acid | 0.34 | 0.42 | no | 15.842 |  |
| web_max | liquid_acid | 0.200 | 0.260 | no | 15.850 |  |
| oil_viscosity | liquid_acid | 0.6 | 0.0 | no | 16.057 |  |
| rise_parallax | liquid_acid | 0.7 | 0.0 | no | 17.352 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| oil_transparency | liquid_acid | 0.5 | 0.0 | no | 17.859 |  |
| spawn_grow_s | liquid_acid | 4 | 0.0 | no | 17.859 |  |
| rise_speed | liquid_acid | 0.0195 | 0.0 | no | 25.452 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |
| film_hue2 | liquid_acid | 180 | 0.0 | no | 33.111 |  |
| film_hue2_amt | liquid_acid | 1.0 | 0.0 | no | 33.187 |  |
| hue_sweep_period | liquid_acid | 630 | 0.0 | no | 57.197 | time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect |

**Summary:** 146 keys tested, 30 INERT (byte-identical), 11 with MAD < 0.05 ('no visible effect on this frame'), 26 skipped (no default found), 76 already at default (not tested), 0 render timeouts.

## Manual addendum: meniscus, rim_dark (auditor's other two known-inert keys)

The automated sweep above cannot test these two: their LIVE ini value (0.85 / 0.80) is already
identical to the compiled struct default parsed from `src\fluid.h`, so "render at default" is not
an experiment (nothing changes). That is a limitation of this method, not evidence either way, so
they were verified manually against the auditor's actual claim instead -- forcing each to 0 (well
below its shipped value) and diffing against the same baseline:

| Key | Section | Live | Forced | INERT? | MAD |
|---|---|---|---|---|---|
| meniscus | liquid_acid | 0.85 | 0 | YES | 0.000 |
| rim_dark | liquid_acid | 0.80 | 0 | YES | 0.000 |

Both byte-identical to baseline (md5 `D7EC6E19F509DE46DE7718F67CFFA59C`), confirming AUDIT 2's
finding: with `ink_mode = water` the ink outside the oil is black in every pixel, so
`meniscus_from_ink`'s gate never opens and these two top-of-ring-stack terms paint nothing in this
preset today, at any value.

## Keys skipped -- no default found

- sweep_ink_4 (liquid_acid)
- oil_color_2 (liquid_acid)
- oil_color_1 (liquid_acid)
- sweep_ink_5 (liquid_acid)
- oil_color_3 (liquid_acid)
- meniscus_color (liquid_acid)
- ink_stop_4 (liquid_acid)
- sweep_oil_2 (liquid_acid)
- sweep_oil_3 (liquid_acid)
- sweep_ink_1 (liquid_acid)
- oil_color_4 (liquid_acid)
- sweep_oil_5 (liquid_acid)
- sweep_ink_7 (liquid_acid)
- sweep_ink_6 (liquid_acid)
- sweep_ink_3 (liquid_acid)
- meniscus_offset (liquid_acid)
- sweep_ink_8 (liquid_acid)
- sweep_oil_7 (liquid_acid)
- sweep_ink_2 (liquid_acid)
- ink_stop_1 (liquid_acid)
- sweep_oil_4 (liquid_acid)
- sweep_oil_1 (liquid_acid)
- ink_stop_3 (liquid_acid)
- sweep_oil_6 (liquid_acid)
- ink_stop_2 (liquid_acid)
- sweep_oil_8 (liquid_acid)
