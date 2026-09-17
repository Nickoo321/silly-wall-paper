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

Every step kept `style=fluid` byte-identical (md5 checked before/after each change).

## The user's picks so far
- Liquid Acid palette A (orange oil / teal ink), sweep variant B approved for "more opposite hues".
- Rim: the softer variation.
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
- Needs from the queued work first: mirror overlays (brief-mirror), lava rise mode, the
  transparent film, and a preset directory the director can enumerate (reference/presets/).
- Open questions for later: minimum dwell time, blacklist of combos that clash (e.g. mirror on
  the single-drop ink), whether the user wants a 'next' hotkey / tray item.

## How work is paced now
Max 5x plan; the 5-hour session window is the limit that bites. One Opus executor at a time,
resumed rather than respawned; code is read before renders are spent; the coordinator does
config-only experiments itself. Every GPU render holds `build2/shots/gpu.lock`.
