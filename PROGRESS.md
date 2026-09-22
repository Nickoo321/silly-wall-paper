# FluidWallpaper progress (as of 2026-09-17, midday)

## The goal
One wallpaper app with several selectable looks, each judged on the real OLED panel in HDR:

1. **WE parity** — the neon fluid the user modded in Wallpaper Engine. DONE and approved.
2. **Liquid Acid** — macro oil floating on inked water (user's references). Landed; polishing.
3. **Ink in water** — translucent ink drops on black or paper. Landed as a style; the user's
   preferred variants are the automated duotone ones.
4. **3D ink sim** — a separate volumetric app. Parked as work in progress until next week.

Everything below is committed on `main`; nothing is running in the background.

## Done, by commit
| what | commit |
|---|---|
| WE parity: config + clamped colour chain + 256 sim grid (the WE motion) | 10d6e71 |
| Liquid Acid look: metaball oil, ink restyle, bubble swarms, rims, grain | d6f6019 |
| Complementary-pair hue sweep for Liquid Acid | d236b06 |
| Plans for 2D ink and 3D ink (Fable planners) | 668db07, 8b634ec |
| `style=ink` (paper / inverted), drops emitter, dye gravity, `ink_mode=water` for acid | 820d102 |
| 3D ink sim M0 passing, M1 mid-bisect (parked) | c594127 |
| Rim variation (`rim_vary`, `rim_ink_follow`), water-ini coverage re-tune | cf554e8 |
| Rim adopted in all palettes at the softer setting (user: "not the harder one") | 5669297 |
| Liquid Acid research doc (creator, genre technique, palettes, ranked ideas) | 84e3970 |
| Automation-on-ink combos (hue-cycled drops, WE bursts, paint pour) | e2ca248, 4e2b32c |
| Duotone ink configs (user's three picks) with the WE hue-shift disabled | 3b59ebf |
| Looks switchable at runtime via tray presets (`EnsureLookResources`), `reference/presets/` (10 looks), `--shot-preset`; `[ink] pair_sweep_period`; acid `rim_order` / `grain_shadow_weight` / `toe_tint` | d9258f0 |
| Tools: `tools/panel-check.ps1`, `tools/oled-brightness.ps1`, `reference/configs/refsheet.py` | b0e4af4 etc. |
| `tools/explore.py` random look-exploration tool + knob spaces (layering1 Sonnet batch, tile9 Haiku batch); tiles sent to the user one at a time; Pillow from user site-packages | 87e4c39, 5f18e0f |
| Liquid Acid oil reads as a FILM, not a cut-out: `oil_thin_edge`/`oil_edge_frac` (soft thickness edge, thin oil goes dark), `meniscus_from_ink` (emergent halo: ink colour, weight = ink brightness, vanishes on black ink), `swarm_lens` (soft holes), `oil_glow`, `oil_specular`, `oil_iridescence`, `refraction_width`; all default 0. Configs `liquid-acid-a-real.ini`, `layering1-09-real.ini`; A/B build2/shots/oil/ | 54780a0 |
| Transparent coloured oil FILM: `oil_transparency` (Beer-Lambert per channel over the refracted ink + backlit scatter), `oil_absorb`, `oil_film_bump`, `oil_refract_body`, `oil_ink_blur`; holes stay pure ink; all default 0. Configs `liquid-acid-a-glass.ini`, `liquid-acid-water-glass.ini`; A/B build2/shots/oil-trans/ | 08a89aa |
| Screen mirroring for EVERY look: `[mirror]` mode off/horizontal/vertical/quad/kaleidoscope, `segments`, `source` quarter, `center`, `rotate_period`, `drift`, `soft`; one uv fold at the top of the display shader (no new PSO), pointer mapped; tray overlay presets "Mirror - quad / kaleidoscope 6 / off (overlay)" stack on any running look. Sheet build2/shots/mirror/ | db43eb9 |
| Lava lamp: `rise_speed/wobble/respawn/stretch/bottom_light` (blobs enter below the bottom edge, rise, leave the top); global colour change via `hue_rotate_period` (continuous) or the sweep list extended to 12 (`sweep_count`, `sweep_oil_N` = a tile9 palette verbatim); `oil_saturation`; `post_chroma`/`post_lift` (film 0.5 + chroma 1.2 = user pick, now default in the glass presets); app: black frame on manual pause, `CMD_PAUSE_ON=8/OFF=9`. Presets "Liquid Acid - rising colours" / "- rising hue rotation". Sheets build2/shots/rise/ | f5f3e30 |
| Screen mirroring / kaleidoscope `[mirror]` (display-pass uv fold, all three looks, overlay presets in reference/presets/) | db43eb9 |
| Lava-lamp rise (`rise_speed/wobble/respawn/stretch/bottom_light`), a 12-slot palette sweep that can carry the tile9 four-shade palettes verbatim (`sweep_count`, `sweep_oil_N`), global `hue_rotate_period` + `oil_saturation`, `post_chroma`/`post_lift`, black frame on manual pause + `CMD_PAUSE_ON/OFF` | (this commit) |
| Droplet particle sim: swarms replaced by simulated negative/positive metaball droplets (surface tension, no popping), rise_respawn support-radius fix, gradient-magnitude gate on the dark rim; sheets build2/shots/droplets/ | 210ff10 + 23d23d7 |
| Droplet hollow-ring / ghost-smear fix: CPU-side contribution gate (droplet `.w` fades field + gradient to zero for an off-side or sub-pixel droplet), isoline-trust gate `isoOk` (narrows the dark rim, bright halo and film-thickness proxy wherever sdf collapses without a true threshold crossing); `--shot-series` sub-second names in tenths so 60/60.5/61 stop colliding. Reduced, not gone; see WORKLOG for the measured follow-up | ee4c045 |
| Swap-chain hardening after two live crashes (game holding the output exclusively during resume/startup): `CreateSwapChainSoft()` shared by `CreateDevice`/`Reattach`, `TryInit`/`TryReattach` unwind instead of `Fail()`, `resumeRenderer()` retries 250 ms to 30 s and rebuilds the wallpaper window after 8 refusals, startup can come up SUSPENDED instead of a fatal dialog, colour-space calls non-fatal, rolling 256 KB `%APPDATA%\FluidWallpaper\FluidWallpaper.log`, `Fail()` logs before the dialog, `Shutdown()` releases the droplet upload rings | 23a8bb7, 221ea95, 6ead6c7 |
| `oil_drag`/`oil_dye_block` (drag + dye-block the field toward the oil's own motion), `rise_parallax`/`rise_parallax_dim` (small blobs rise slower, dim more), `oil_viscosity` (damps blob motion), `mouse_oil_mode` push/comb with `mouse_oil_radius`/`mouse_oil_gain`; all default off. Rising presets updated, two new mouse-variant presets. Sheets build2/shots/oildrag/ | 5abf21f |
| Droplet ring fix: `droplet_oil_weight` now carries the local field deficit instead of a fixed 1.2, and the size floor is `max(1 px, 1.6x rim half-width)`, so an oil droplet is a real disc wherever it sits instead of a hollow rim; flicker diagnostic `tools/popdetect.py` + millisecond `--shot-series` names (flicker fix itself parked, see WORKLOG) | 7f0188d |
| `tools/popdetect.py` hysteresis (loose-mask overlap drops classifier-drift false positives, 334 -> 0 pops); `oil_edge_mode` 0 soft / 1 crisp film edge with the band-jitter cause fixed (`lensR` takes the blob-only gradient where credible); rising presets ship mode 1, "(soft film edge)" variant carries mode 0 | 6bc9a11, 8b82ad5 |
| Camera/DOF/tilt-shift (briefs N, R, T): perspective camera + curved focal plane fixes the DOF ring artefact, tilt-shift/freelensing focus plane with occasional readjustment (not drift), Blade Runner 2049 lab-instrument razor-sharp slice, shared camera rig block, dof_max_px 9->5 | 0f9b098, d4a96b3, 86ef4f1, 7e62f37, a9744b4, dd324a1, 8ebd567, 540d8e2, f7acb9a, 168bc42, d488b35, 8bfd600, 7f7cebf, a64c54b |
| Diffraction (brief W): point-spread blur, darkness scales with droplet size, PSF fixed to size-dependent contrast | bfde4a7, cb6ff0a |
| Droplet lens shading (brief X): crisp at the edge, gradual toward the interior; DOF ratio tightened by ini | 7371e38, f781695, af53f84 |
| Bloom: disc kernel (4 radii, Gaussian weights, per-pixel radius jitter) replacing the two-ring bloom that photographed as concentric rings | 74f1094 |
| Conservation of mass (briefs A+S) in the acid sim -- no oil spawning/vanishing in view -- shipped to every rising preset; brief V shared rig state (lamp, lid ghosts, focus, motion) + motion model (slow drift, occasional all-at-once readjustments); droplet population/clustering tuned twice (1500->1350->950, fewer small droplets, more large masses) | 5cd7b53, 6328bdb, ea25d37, dee52c7, 5b51e06, c0bb68d, fe23cec |
| Coalescence + big hollow bubbles: touching solids become one instead of a lumpy group; big hollow bubbles sized so rings may outgrow the droplet grid cell | 2810fbb, b11521f, ef26209 |
| Film scratches (faint, soft-edged, flickering segments) implemented, shipped OFF -- user: "no film has a line like that" | 85b3f74 |
| Cellulose/film-overlay/light-in-the-water (briefs O, P, Q): macro cellulose texture in the black masses, film overlay artefacts (hairs/dust/scratches/light-leak/grain), wide weak bloom + volumetric fog as "light in the water" | 7049d02, 6009b1d, b62e447 |
| V0: output dither, half an LSB against 10-bit banding | 19d5f4a |
| Liquid Acid accent-by-size (brief J): `accent_mode`/`accent_max_r`/`accent_frac` keeps the complement shade off big masses (hash of blob index, hysteresis + 2s crossfade, no rand() draw); shipped mode 1 / max_r 0.12 / frac 0.6 in acid-rise-8020 and both 80-20 presets only | f384659, c6efa53 |
| V1 halation: tight warm glow from film's missing anti-halation backing (`halation`/`halation_px`/`halation_warmth`); brightness gate must read the PEAK channel, not luminance -- a luminance test evaluated to zero on saturated magenta; shipped 0.25/14px/0.75 in every acid-rise-*/rising preset, off in the NO-lens twin | e925d2a, 04c49ea |
| Fullscreen pause fix: detect games by client rect covering the monitor instead of window style -- windowed-fullscreen titles kept WS_CAPTION and were never paused | 876c72a |
| U9 bubble weather: `[liquid_acid] weather`/`weather_period_s` give the droplet population slow seasons (rings + big hollow bubbles vs. solids, sparse vs. crowded) instead of the one steady state it used to settle into; density channel scales the droplet target by +/-0.45*weather gated on `conserve_mass`, ring channel drives `droplet_ring_frac`/`droplet_ring_big_frac`; shipped weather 0.5 / weather_period_s 300 in all 24 rising configs+presets; verified 19% swing in visible droplets over 6 min | 0a87ca2 |
| Brief S residue: `blob_count` now walks via `AcidBlob.rTarget` instead of `SeedAcidBlobs()` reseeding the whole population every frame; fixed a bug where a rise-respawn restored `rTarget` on a blob already marked to retire, which kept the population churning instead of settling | 7fb7058 |
| Merge of U9 bubble weather + brief S residue onto `main`, keeping both the accent block and the rTarget block in `fluid.cpp`/`fluid.h` | 99f933c |
| Brief AA: dye masses sit at the focus depth by construction (the depth accumulator's prior is 0.5 = `camera_focus`); a curvature 0.9 / cap 14 slider test softened corner droplets but barely touched the mass edge -- fix is a rig-driven `dye_depth` key, assigned for later | b243780 |
| Z: lateral chromatic aberration is now a real radial resample in the post pass (`kPostSrc`), red pushed out / blue pulled in from the rig's drifting lens centre, applied as the difference the displacement makes so it fades on defocused elements; old symmetric-rim derivative removed from the display pass; keys unchanged, shipped 1.0/1.8/1.2 in all acid-rise-*/rising presets; rig relabelled rg0 lamp+lens centre / rg1 tilt+focus+phase / rg2 chromatic split, rg3/rg4 free | c61ff10 |
| Merge of `main`'s bubble weather + brief S residue (`99f933c`) into `camera-dof`, alongside brief Z and brief AA's diagnosis, ahead of item AA's fix | b4173dd |
| Item AA: dye masses get their own depth layer (`dye_depth`/`dye_depth_tilt`/`dye_depth_w`) instead of sitting at the focus-prior constant; `dof_max_px` 5->9 in the rising configs; corner mass edge peak gradient 16.3->13.5 | 5c8deee |
| Merge of `camera-dof` (item AA) onto `main` | 56c98c5 |
| Film grain frame rate: `film_grain_fps` key (default 24) paces grain/film_noise instead of a hardcoded 144 Hz, so the pattern holds a whole number of refreshes per change at 240 Hz | dbc55e0 |
| V3 motion: `shimmer`/`shimmer_px` (velocity-advected refractive wobble above the lamp, `t3` bound to the post pass), `vignette_wander` (vignette centre follows the lens via `poP1.xy`), `pixel_shift_px` (whole-frame orbit on ~7/~11 min periods applied to the read coordinate so grain/dither stay in screen space), `rig_readjust` (lamp + lens centre re-aim on the focus spring); `--shot` prints a `[rig]` line, verified by a rig table showing two readjustments; shipped shimmer 0.35 / shimmer_px 1.2 / vignette_wander 0.6 / pixel_shift_px 3 / rig_readjust 0.5 in every rising config+preset, off in the NO-lens twin | 53217e9, d364e9e |
| V2: the transparent lid -- `lid`/`lid_ghost`/`lid_ghost_spread`/`lid_rings`/`lid_sheen`/`lid_sheen_px`/`lid_glint`/`lid_iris`/`lid_refract_px`, all `[post]`, default 0, packed losslessly into rig slot `rg4`; ghosts/rings/sheen/iris/glint positioned from the drifting lamp + a lid wander (3-12 min periods) and shoved on the focus spring at readjustments; sheen lifts a big dye mass's mean 22 -> 34 of 255 at the shipped subtle values (0.45/0.24/1.0/0.25/0.16/400/0.15/0.22/2.5 in the four acid-rise inis + base rising preset, plus "(lid medium)"/"(lid strong)" presets, 0 in the NO-lens twin); `lid_sheen` is the single knob for the black lift; merge needed a `kPostSrc` literal split for MSVC's 16380-byte cap; parity md5 PASS | 8afca1d, 01fa483 |
| acid-rise-12 de-fuzz (user: too fuzzy/fine, hazy): `film_noise` 0, `film_grain_size` 2.5 px, `shimmer_px` 0.8 px, `fog` 0.22, `lid_sheen` 0.04 | b5c79bb |
| Brief AB: bubble crust on the masses -- `[liquid_acid]` `droplet_mass_bias`/`droplet_crust_density`/`droplet_crust_r` add a second droplet population (its own spawn target, can't starve the film) that nucleates deep inside a mass with probability rising with thickness and rides the mass's own velocity via a soft-max blend in `AcidFieldAt`; separation only, no self-attraction, rests just outside contact (1.10x radius sum) after two failed passes (dashes, then rigid rafts); `mass_rim` lights only the lamp-facing edge off the surface normal x lamp-direction dot. Ship 0.6/2.5/0.6/rim 0.35 in the four acid-rise-*.ini + base rising preset; in-mass share of visible droplets 8.5% -> 34.1% at seed 1234 t=60. A/B `build2/shots/cons/crust/SHEET-AB-crust-1440p.png` | 7004632, 7ee65e2 |
| Brief AE: multicolour oil -- a second dye hue in the film. `[liquid_acid]` gains `film_hue2`/`film_hue2_amt`/`film_hue2_scale`/`film_hue2_drift`/`film_hue2_decay`/`film_hue3`/`film_hue3_amt`/`crust_hue_mix` (sliders, help text, all default 0 amount); the field is one scalar/cell on a 20x12 grid, semi-Lagrangian-advected off the sim's own 64x36 velocity readback, riding the acid cbuffer's slack (4 cells/float4, no new texture/descriptor). Fixed a real bug on the way here: the hue must ROTATE (`AcidHueShift`), not cross-fade -- `lerp(magenta, cyan, 0.5)` in RGB is grey -- so the shipped amount is 1.0, not the 0.5 the brief assumed for a blend; `crust_hue_mix` keys off the blobs-only field, not sdf. Adding the block pushed a `kPostSrc` literal past MSVC's 16380-byte cap, split at the nearest top-level banner (largest now 14081). Shipped -140/1.0/0.35/1.0/0.35/0/0/1.0 in `acid-rise-12.ini` ONLY (that preset sweeps eight hues). style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054` held. Live on the OLED 13:43, user verdict "its beautiful"; keeper `reference/shots/panel/2026-09-19-hue2-cyan-in-magenta-beautiful.png` | 6c71d32, 90f97b9, 10f8bc4 |
| Brief AE-b: base hue2 keys synced at defaults to the other 33 acid configs/presets; confirmed `film_hue2` already relative so the primary/contrast pair rotates together under `hue_rotate_period`; `film_hue2_wobble`/`film_hue2_wobble_period` (sliders, help text, default 0/300) wobble the contrast hue a few degrees either side on two golden-ratio sines. acid-rise-12: `film_hue2` 180 / wobble 10 / period 300 / `hue_rotate_period` 3600 (not the suggested 10-20 min -- `hue_sweep_period` 630 already moves the hue, 3600 s adds ~17% so the eight sweep families never repeat a combo; sweep kept ON because the base ink stops aren't mono and would colour the masses if it were disabled instead). Patch band narrowed `smoothstep(0.56, 0.68)` so the 180 deg transition reads as a thin rim, not a rainbow across a quarter frame; a `hole_max` 0.130->0.150 trial reverted (one 60 s frame can't resolve a change that small; user said "it's fine"). style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054` held. Strip `build2/shots/live/hue2b-strip.png` | f840685, fed09f9, 09fc257 |
| Preset parity (rote): `film_hue2_wobble`/`film_hue2_wobble_period` were tuned (10/300) in `acid-rise-12.ini` only; added at their DEFAULTS (0/300, confirmed against `src/fluid.h`) right after `film_hue2_decay` in the same 33 files that got the base hue2 keys, exactly two lines each, zero deletions, each file's own CRLF/LF and no-BOM preserved | (this session) |
| Brief AE-c: hue2 patches are generated off screen and only rise (user's standing rule: colours must not appear out of thin air). The mix grid is now taller than the screen -- `film_hue2_seed_rows` hidden rows below the bottom edge are the only place new material is made; visible rows are pure advection, so nothing can form, brighten or re-lay inside the frame. Vertical velocity clamped at or below zero so a patch can never travel down. The readjustment no longer re-lays the field -- it changes the phase/scale of what is seeded below the edge instead. One deliberate deviation from the brief: decay now applies only in the seed rows (at the shipped 0.35 it is a time constant under three seconds, so without this a patch entering at the bottom would die before climbing a tenth of the screen). New keys `film_hue2_seed_rows` (default 2) / `film_hue2_rise` (screens/min, default 0.25), sliders + help text; both only matter when `film_hue2_amt` > 0. `acid-rise-12.ini`: seed_rows 2, rise 0.3. Provenance check (2 s apart, local window): 44/1680 violations before, 0/1680 after. style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054` held. Strip `build2/shots/live/hue2c-strip.png` | 6336c36, b09e0bc |
| Preset parity (rote): `film_hue2_seed_rows`/`film_hue2_rise` were shipped (2/0.3) in `acid-rise-12.ini` only; added at their DEFAULTS (2/0.25, confirmed against `src/fluid.h`) right after `film_hue2_wobble_period` in the same 33 files that got the base hue2 keys, exactly two lines each, zero deletions, each file's own CRLF/LF and no-BOM preserved; `film_hue2_amt` is still 0 in all 33, so the new keys are inert there too | (this session) |

Every step kept `style=fluid` byte-identical (md5 checked before/after each change); today's re-check (2026-09-18, `we-look-live.ini`) PASS, `10e36ebf1a74edfe609065d757300054`; re-confirmed again by this session's own Sonnet rote md5 check against the worktree exe built from `876c72a`, same hash, PASS; re-confirmed a third time this session against the worktree exe built from `99f933c`, same hash, PASS; re-confirmed a fourth time this session against the worktree exe built from `b4173dd`, same hash, PASS; re-confirmed a fifth time this session against the worktree exe built from `dc2febd` (two brief-only commits past `dbc55e0`, no `src` changes), same hash, PASS; re-confirmed a sixth time (2026-09-19, Sonnet rote) against `C:\Users\abg77\fw-wt\build2\FluidWallpaper.exe` confirmed built from `01fa483` (V3 motion + V2 lid both in), same hash, PASS; re-confirmed a seventh time (2026-09-19, Sonnet rote) against the worktree exe built from `7ee65e2` (crust merged), same hash, PASS.

## The user's picks so far
- Liquid Acid palette A (orange oil / teal ink), sweep variant B approved for "more opposite hues".
- Rim: the softer variation.
- Oil film: transparency 0.5 with chroma held to the opaque level ("the 50% chroma held is the best"; ~x1.2 post chroma). LANDED as `post_chroma` 1.2 / `post_lift` 1.08 and made the shipped default of the whole glass family + the rise inis.
- ON THE PANEL (2026-09-17 18:50, acid-rise-12 live, HDR, brightness 100): the colours are "amazing" — the vivid tile9 palettes at oil coverage ~85% on black are the keeper; do not tone them down. Verdict pending only on the droplets (must be simulated holes, not the stamped swarm). Panel photos of the cyan phase: reference/shots/panel/2026-09-17-acid-rise-cyan-panel-*.jpg ("stationary, this is amazing" — the remaining gap is motion: the dots must live and move with the oil).
- Ink: `ink-auto-bursts` (WE bursts driving ink, water clears) and the crowded paint pour
  (`ink-pour`), then the duotone versions of both: `ink-duo-pour-teal-vermillion`,
  `ink-duo-pour-yellow-magenta`, `ink-duo-bursts-yellow-magenta`.

## Open items, ranked
1. **Panel check (needs the user at the desk).** PNG previews are SDR projections and cannot show
   the 240-nit SDR white or the HDR-lifted cores; hue and composition are trustworthy, brightness
   relations are not. `tools\panel-check.ps1 -Ini <ini>` swaps the live look and relaunches the
   port from build2; `-Restore` puts the WE look back. Brightness is at 0 (`oled-brightness.ps1 100`).
2. **Install the presets.** The tray Presets menu enumerates `%APPDATA%\FluidWallpaper\moods\`;
   copy `reference/presets/*.ini` there (needs the user's ok) so the looks appear in the menu.
   "WE parity (fluid).ini" is the way back to the original look.
3. **Liquid Acid polish**: rim_order / grain_shadow_weight / toe_tint are implemented (default off)
   and await the user's verdict on the panel; still open from the research: oil slower than ink,
   one coverage scalar instead of three blob fractions, `rim_ink_follow` toward 1.0, a WARM toe
   (toe_tint follows the ink hue, which is teal for palette A).
4. **Liquid Acid water-ink coverage** sits at ~42% vs the ~50% target (one knob nudge).
5. **Moods / hue-cycler policy** for the acid and ink looks (they bypass the WE colour chain;
   the hue-shift automation must stay OFF for duotone ink — it rotates the whole frame).
6. **Ink look extras not done**: splash droplets (`spatter`), parallax layer, blind A/B sheet.
7. **3D ink sim**: bound the per-frame sharpen pass, re-run M1, then M2 + timings. Next week.
8. **Preview fidelity (optional)**: a panel-emulating PNG variant (SDR white mapped to PNG white,
   soft knee on the lifted cores) so previews stop under-reading brightness.
9. **Brief Z, assigned executor A**: real lateral chromatic aberration needs a resample in the POST
   pass, not the current display-pass derivative trick (which only gives a symmetric warm rim --
   no cool side splits off because a luminance-gradient derivative can't flip sign across an edge).
   Diagnosis in `reference/briefs/NEXT-rings-grain-aberration-refraction.md` (`efbec41`); no code yet.
10. **Brief AA, assigned for later**: dye masses currently sit at the focus depth only because the
    depth accumulator's prior is 0.5 = `camera_focus`; a rig-driven `dye_depth` key is the real
    fix (a slider test with curvature 0.9 / cap 14 barely moved the mass edge). No code yet (`b243780`
    is diagnosis only).

## End state (user, 2026-09-17): automated cycling through everything

"Ideally in the end the program should just cycle through whatever, basically never running out
of combos: WE + variations, + ink sim, + the oil sim + the mirroring." Considered only, not built;
"the last last step". Sketch, so it is not lost:
- A *director* on top of the existing `[cycle]`/`[moods]` machinery: every N minutes pick
  look (fluid / liquid_acid / ink) x palette or variation (curated lists: WE presets, acid
  sweep hues, ink duotone pairs, tile9 family) x mirror overlay (off / quad / kaleido) x duration.
- Transitions: all three looks share ONE sim, so switching the display look keeps the fluid
  continuous. Organic hand-over = ramp the look-specific parameters instead of cross-fading
  two renders: oil coverage (threshold) rising from empty, ink opacity/veil from 0, mirror
  `soft`/centre drift, palette HSV lerp. No double rendering, no second PSO chain.
- Preferred transition (user, 2026-09-17): a SWEEP built on the mirror machinery — the fold line
  becomes a split line, each side paints the same dye with a different look, and the line travels
  across the screen (any angle, the mirror's soft band on it; optionally an organic front offset by
  dye density / slow noise so the new look leaks in along the plumes). Per-pixel blend of the two
  recipes stays as the fallback. Both need one PSO with all looks enabled plus a blend/side weight,
  used only while a transition is in flight (fluid md5 untouched).
- Needs from the queued work first: mirror overlays (brief-mirror), lava rise mode, the
  transparent film, and a preset directory the director can enumerate (reference/presets/).
- Open questions for later: minimum dwell time, blacklist of combos that clash (e.g. mirror on
  the single-drop ink), whether the user wants a 'next' hotkey / tray item.

## How work is paced now
Max 5x plan; the 5-hour session window is the limit that bites. One Opus executor at a time,
resumed rather than respawned; code is read before renders are spent; the coordinator does
config-only experiments itself. Every GPU render holds `build2/shots/gpu.lock`.
