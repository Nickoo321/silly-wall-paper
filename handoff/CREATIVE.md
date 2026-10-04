# FluidWallpaper — creative direction (working file)

Kept by the creative-director chat. The implementing chat logs every keep/cut verdict in
`review/DECISIONS.md`; this file holds the direction, the reasons, and the idea backlog.
Rough mocks live in `review/colour-mocks/` (python mocks, not app renders).

## 1. The plot
Black oil on lit colour, seen through the filmic lens.
- The oil and droplets are always black (no dye). The black contrast is why it works.
- Proven = one colour, or a primary/secondary pair (near-complements) at BIG size. See the user's
  panel photos in `review/proven/`.
- 3 or 4 colours = "weird but a fine mix-up": allowed, but rare.
- Colour comes from the light and the grade (Lightroom-style effects), never from painting the oil.

## 2. Weighting rule (applies to every setting that rotates)
What's proven shows up most; weird/complicated shows up rarely. Default ~7 : 2 : 1
(proven : moderate : wild), applied per tier.

## 2b. The WE/fluid side (proven-5..8-fluid, 2026-09-26)
- 1-2 colours per frame; a third only as a <=10% accent.
- ~25-50% dark in big voids (black or a very dark member hue, never grey haze).
- Two motion scales at once: big bodies with fine curl marbling inside.
- Dense, solid bodies with ragged edges. Colours meet through dark bands, not bright seams.
- Red home, agreed by both chats: in the CLOCKS brief the fluid colour group's hue_center
  distribution is centred on red (~350-10), with a skewed hue_range (mode 60-90, tail to 180). So
  red is the mean state and the wide blue/violet sweep is the tail. Stage overlays only; the WE
  parity preset is untouched. The user gets a panel A/B (red home vs any colour) after CLOCKS lands.
- Gamut: BT.2020 everywhere (user).

## 3. Colour system
- One base hue + an offset pattern. The pattern rides a slow anchor drift that lingers at
  magenta 325 > blue 215 > red 355 > violet 275 and glides fast through lime/mustard/olive.
- Slots: film hue / second colour (film_hue2) / third colour (film_hue3) / shadow tint (split tone).
- The offset sign picks the seam path; the seam hues are part of the look. Seams stay NARROW
  (a thin line, like proven-2).
- Films are equal-load (dimmed to magenta's brightness). Yellow/gold/lime members stay bright but
  small (third colour, <= ~15%).
- **Colours are RELATIVE, and they move** (user 2026-09-25): "literally just do colors by their
  relative values". The offset patterns are the content; motion comes mostly from rotating the hue
  wheel and/or rotating the base (primary) hue. The two can be combined. The user's example: shift
  the primary +90 while the wheel goes -90, "to achieve 180 of movement, and it's interesting". A
  named pair (Sodium/Blue, Moonlight, ...) is just a point the rotation passes through, never a
  separate entry to curate.
  - Open (implementing chat): how the keys compose. If film_hue2 is relative to the film hue,
    a primary +90 against a wheel -90 cancels for everything relative. The accessory then needs its
    own drive for the two to move apart.

## 4. Combos
| Tier | Weight | Combos (film, offsets) |
|---|---|---|
| Home | 7 | Return to Form x3 (325) · Lightroom x2 (325 + teal shadows) · Magenta/Mint x1.5 (+180) · Magenta/Cyan x1.5 (-140) · Blue/Coral x1.5 (215, +160) · Red/Sky x1 (355, -140) · Violet/Amber x1 (275, +125) |
| 3-colour | 2 | Neon Demon · Warm Arc · Euphoria · Bright Triad · Bright Split · Teal/Orange · Lightroom Triad (split toning: teal shadows + gold highlights + balance) |
| 4-colour | 1 | Synthwave (third +70) · Microscope · Square+ |

## 5. Cycle stages
- Kept: Return to Form / monotone (home) · WE fluid (home) · acid-rise-12 (the "older settings")
  · ink-inverted (wild) · mirror quad (wild).
- Cut: acid-rise-2hue, acid-rise-rotate, lapd dyed.
- Hue bursts on oil: a rare, tamed event (brightness must not swing).
- Every stage opens with content (WE splat burst, ink drop), never an empty screen.
- Pace (user, 2026-09-25): "the whole point is the constant evolution". The continuous animators
  (hue wheel, drift, effect clocks) keep every look moving all the time; SHARP cuts between looks
  only about every ~7 min.
- Second-colour coverage: 0.08 for now. The follow-up keeps patches photo-sized (never "bubbly")
  or caps the peak.

## 6. Effects (the Lightroom family)
- **MAJOR RULE (user 2026-09-25, emphatic): post-processing effects are CYCLED, and subtle.**
  User: "a lot of the post processing effects should also be cycled... If we know it works with
  chromatic aberration and without. Do duty cycles, do this for all the post processing effects".
  - Every post effect (film grain, chromatic aberration, vignette, split tone, lamp grey, grey heart,
    and the rest) gets its own duty cycle: it fades in, holds, fades out, stays off, and repeats.
  - Gate: an effect cycles on a look only if that look is verified good both WITH and WITHOUT it,
    with HDR on and off. An effect that only works one way stays fixed on that look.
  - Duty fraction follows the weighting rule: the proven state gets most of the time (e.g. grain
    on ~70%, chromatic aberration on ~30%).
  - Periods differ per effect and don't divide the 7-min cut (e.g. 3.7 / 5.3 / 8.9 / 11.3 min),
    so the combination keeps changing and never repeats in step. Fades >= ~20 s, never a pop.
  - The ON level is the subtle, proven level; a cycle never pushes past it.
  - The keyed bloom follows the hard constraint (default 0). It only joins if the user says so.
- **Every key rotates; one distribution mechanism** (user, via the implementing chat, 2026-09-25):
  - "Every point should be rotating, and continuously changing." Every proven key has its own slow
    clock; presets are the points the clocks pass through.
  - Each key's level is drawn from a SKEWED, long-tailed distribution (log-normal shape):
    - mean = the preset value
    - mode below it, so most of the time it's lighter than the preset
    - a long upper tail, so an effect pops out once in a while.
    Draws change slowly and fades never pop.
  - "Most of the effects and options/modes should be made by affecting that distribution." A PRESET
    carries the means; a MODE carries the shape (spread, skew, tail weight, floor/ceiling).
  - BASE = narrow spread with the mode below the mean. EPISODE = the tail: strong, short (~2 min),
    rare. Occasional combos happen when tails overlap by chance.
  - Tail budget: set it from a TOTAL target, not per key. With N keys each in its tail a fraction p
    of the time, "all normal" = (1-p)^N. For 14 families, p ~3.6% gives ~60% all-normal, ~31% one
    effect popping and ~9% combos.
- **NO NEW FEATURES for episodes (user ruling 19:35).** Episodes use only keys that exist today:
  - old film = dust + hairs + scratches + leak + film noise
  - tilt-shift / defocus = focus_tilt + dof
  - lens distortion = corner_warp + camera_fov
  - VHS-adjacent = lens softness + lateral aberration pushed together
  - "scan lines once in a while" = film_scratches: rarer and stronger, never resident.
  Halation is a BASE effect, not an episode, because of ABL at episode strength.
- Split tone (BU): tint in the shadows, ANY hue, warm included. The user: "warm has always been
  allowed". My earlier "cool only" line was wrong and is struck. Watch: a dimmed yellow reads as
  olive.
- Lamp grey (BU): Y-flat desaturation of the corner farthest from the lamp.
- Grey heart (BX), from the user's own radial filter (`review/colour-mocks/user-lr-radial-heart.webp`):
  - true colour everywhere; the centre keeps ~25% chroma (dusty rose, not grey)
  - the centre is lifted ~1.45x
  - one soft ramp from r 0.4 to r 1.3
  - the oval is scaled 1.3x (the user's pick)
  - nothing is tinted on top.
- Rule: effects work on the real colours. Never a colour tint laid over a greyscale image.

## 7. Rejected (don't bring back without the user)
Multi-colour dyed oil · glow bodies · the LAPD dyed look · tint-over-greyscale vignettes ·
several strong hues at equal weight · "bubbly" droplet fields · NEW shader features for episodes
(VHS, fisheye, gate weave, flicker, CRT scanlines; user 2026-09-25).

## 8. Idea backlog (NOT approved; bring up when relevant)
- **Named colour pairs: superseded** by relative colours + wheel rotation (section 3). Sodium/Blue,
  Moonlight, Parchment and Darkroom Red are only positions the rotation can reach. A low-saturation
  or dim "night" setting is still an idea (`review/colour-mocks/pairs-new.png`).
- **Oil layouts: APPROVED, all of them, continuously mixed** (user 2026-09-25: "all of them, approx
  those splits, do continuous mixing of all of them"). Few giants ~28% black · lava rise ~25% ·
  starfield ~12% · rafts ~8% · today ~9% (`review/colour-mocks/layouts.png`). The oil drifts
  between layouts all the time (no sharp layout cuts), like the colour.
- **Big black masses: APPROVED as the next priority after request 5** (user 2026-09-25). The
  proven photos carry 20-30% black; app frames run under 10%. A structure pass brings the black back.
- **Time of day:** brighter families by day, darkroom-red families at night.
- **Transitions:** today it fades through black. The sweep transition (a moving split line on the
  mirror machinery) stays a later idea.
- **Episode material:** existing keys only (section 6). Film specks = film_dust.

## 9. How it ends (a GUIDELINE, user 2026-09-25: "softer")
I warn when an alarm fires; I never block. New sliders are fine when they're worth it.
1. **Finish line:**
   - the cycle runs WE fluid + the oil families above
   - the UI rework has landed
   - each stage has a panel verdict with HDR on AND off
   - no shader features are pending.
2. **After that, new ideas arrive as VALUES** (a combo, a layout, a preset), not new keys. A new
   key has to show that no combination of existing keys can do the job; one in, one out.
3. **Clutter alarms** (time to tell the implementing chat to finalize):
   - a key is proposed for something values could already do
   - three features land without a panel verdict
   - a sheet turns into a hue sweep
   - the user can't say what a slider does.

## 10. Waiting on / queued
- Request 5, judged 2026-09-26 09:50:
  - Magenta/Mint KEEP. Seam 0.33 stays; on the panel, look at the green rim on the mint side.
  - Equal load: 0.5 cool / 0.25 warm, AGREED by both chats (0.75 greyed the mint). A rote change
    before the next swap; the panel decides whether mint still out-glows.
  - Mirror quad enters EMPTY: needs content.
  - Monotone lime start: brightest frame (0.84), so check ABL on the panel.
  - Burst stays OFF and becomes the colour group's tail.
- Grouping (user): tails are drawn per GROUP of related keys (noise-type keys, colour keys...),
  across most keys; ~10% of keys ("lowkey useless") stay out of the noise set.
- Next: 5b, the grey-heart sheets (BX).
- The implementing chat's queue: CLOCKS brief (distribution model, groups, film_hue2 bounded sweep)
  -> auditor -> executor; then the black-mass pass (`review/creative-brief-black-mass.md`); then
  the with/without rote run.
