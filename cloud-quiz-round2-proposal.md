# Clouds mood — quiz round 2 proposal (v2 values)

Target (user's words, WE reference): dense cream/beige/orange marbling, cloudy
organic texture, narrow orange-adjacent hue band, NOT neon, lower saturation,
layered oil-paint swirls. Must look good HDR on AND off (hard rule,
CALIBRATION.md). Panel: QD-OLED, ~1000 nits small window, ~418 full frame,
SDR white 240 nits; ABL dims large bright areas.

## a. Parameter table

| key | Neon (final) | Clouds v1 (guess) | Clouds v2 (proposed) | rationale |
|---|---|---|---|---|
| decay_threshold | 0.290 | 0.250 | **0.200** | More dye stays in slow-decay regime → denser, persistent cream masses |
| decay_fast | 1.000 | 0.970 | **0.980** | Slow tail-clearing: layers accumulate like oil paint, haze still dies eventually |
| saturation_restore | 0.930 | 0.950 | **0.850** | Stop forcing saturation back up; let mixing mute colors toward cream/beige |
| max_brightness | 1.35 | 1.60 | **1.40** | Layered contrast without driving full-screen cream into ABL dimming |
| dye_diffusion | 0.305 | 0.305 | **0.400** | Softer, cloudier blending (smooth marbling vs sharp filaments) |
| hdr saturation | 1.20 | 1.10 | **1.00** | HDR comp should not re-punch a deliberately muted mood |
| hdr brightness | 1.08 | 1.08 | **1.00** | No extra HDR lift on a bright full-screen scene (ABL headroom) |
| hdr contrast | 1.04 | 1.00 | **1.00** | Keep neutral; contrast shape comes from post_contrast |
| peak_nits | 1000 | (absent → inherits) | **400** | Explicit cap: large-area perception saturates ~400–500 anyway; full-screen cream must not fight ABL |
| gamut | 2 | (absent → inherits) | **2** (explicit) | Keep BT.2020, same as Neon; low-sat colors barely differ, avoid an uncontrolled variable |
| color_cycle_period | 10 | 25 | **30** | Slower in-band hue drift → coherent, layered strata instead of visible color turnover |
| post_saturation | 1.70 | 0.95 | **0.85** | Muted cream, not gray; quiz round 1 rejected pastel only *for Neon* |
| post_contrast | 1.34 | 1.15 | **1.10** | Soft marble gradations, not punchy separation |
| post_brightness | 1.06 | 1.15 | **1.10** | Cream lift without searing the full frame |
| post_hue | 14 | 10 | **10** | Fine nudge toward amber; hue_center does the real work |
| hue_center | 66 | 35 | **35** | Orange/amber center — matches WE reference |
| hue_range | 180 | 50 | **45** | ±45° around 35° → red-orange to yellow; tight enough to exclude green creep (WORKLOG Job 2 bug lesson) |
| shadow_floor | 0.100 | 0.100 | **0.100** | Calibrated user taste (strips 2–4); do not re-open |
| shadow_knee | 0.15 | 0.15 | **0.15** | Paired with floor; calibrated |
| hueshift_enabled | 1 | 0 | **0** | Must stay OFF in a narrow-band mood (see reasoning) |
| vorticity | 24.0 | 24.0 | **32.0** | More fine swirl structure = marbling density inside big blobs |
| splat_radius | 0.630 | 0.630 | **0.750** | Bigger emitted masses → the WE "big slow blobby" base shapes |

Untouched: density/velocity_diffusion 0.9990, pressure_diffusion 0.850,
pressure_iterations 20, knee 0.98, compensation 1, wanderer/dart/idle/governor
behavior keys (motion feel already approved at sim_res 512), curve_enabled 0.
Res/fps never touched (user directive).

## b. Reasoning — what makes cream-marble different from Neon

- **Hue band, not wheel.** Neon runs the full 180° wheel with fast emission
  rotation (period 10). Clouds pins emission to ±45° around 35° (orange/amber,
  40–60° total width per the target). All "color variety" must come from value
  and saturation layering inside that band — cream, beige, amber, rust — not
  from hue travel.
- **Desaturation strategy.** Neon's punch comes from post_saturation 1.70 +
  saturation_restore 0.93 (the sim actively re-saturates mixed dye). Cream is
  the opposite: lower post_saturation (0.85) AND lower saturation_restore
  (0.85) so dye that mixes stays muted; only fresh splats arrive vivid and then
  settle into the marble. Raising v1's sat_restore 0.95 → 0.85 is the single
  biggest "NOT neon" lever.
- **Persistence vs decay.** Neon clears fast (threshold 0.29, decay_fast 1.0 —
  dye below threshold freezes). Clouds wants accumulation: lower threshold
  (0.20) keeps more dye in the slow 0.999 regime, decay_fast 0.98 slowly clears
  only the dimmest haze. Result: strata of old and new swirls = oil-paint
  layering. v1's decay_fast 0.97 + threshold 0.25 was directionally right but
  timid.
- **Marbling density.** Fine internal swirls come from vorticity (24→32);
  cloudiness comes from dye_diffusion (0.305→0.40); blob scale from
  splat_radius (0.63→0.75). These three trade against each other — that's why
  the quiz isolates them.
- **hueshift stays OFF.** The hue-shift cycler is a post-process wheel rotation
  applied after the band constraint — any step drags cream through green/cyan
  and instantly breaks the mood's identity (and re-opens the WORKLOG Job 2
  out-of-band leak). Narrow-band moods must get all motion from emission-side
  hue drift inside the band (color_cycle_period), never from post rotation.
- **Brightness budget.** Neon directive was "as bright as possible" (peak 1000,
  small hot cores riding small-window headroom). Clouds is the inverse: a
  *large-area bright* scene, which is exactly what ABL punishes. Cap peak at
  400 nits, no HDR brightness/contrast comp, moderate post_brightness. Better
  slightly dimmer and stable than bright and visibly pumping.

## c. Quiz plan — round 2 (I edit Clouds.ini + restart, user eyeballs live)

Protocol per round: I apply the candidate, restart, user looks 15–30 s, picks
A/B(/C). One parameter group per round. Start each round from the v2 proposal.

1. **Hue band** — `hue_center`/`hue_range`: A=35/45 (v2), B=25/40 (redder,
   tighter), C=45/60 (yellower, wider).
   Ask: "Which one is the cream-orange of the WE photos? Any green or
   pure-red intruders?" Good: warm orange-amber marbling, zero green.
2. **Mutedness** — `post_saturation`: A=0.85 (v2), B=0.75, C=0.95.
   Ask: "Which is muted cream without going gray/dishwater?" Good: beige-cream
   body, still clearly orange family.
3. **Marbling density** — `vorticity`: A=32 (v2), B=24, C=40.
   Ask: "Which has fine swirls *inside* the big blobs without looking twitchy?"
   Good: layered internal detail at calm large-scale motion.
4. **Cloudiness** — `dye_diffusion`: A=0.40 (v2), B=0.305, C=0.50.
   Ask: "Which blends like oil paint — soft swirls, not sharp threads, not
   mush?" Good: soft-edged marbling with visible structure.
5. **Brightness / ABL** — `peak_nits` (+`post_brightness` paired):
   A=400/1.10 (v2), B=300/1.15, C=500/1.05. HDR on, full-screen cream.
   Ask: "Bright and creamy but stable? Any visible dimming/pumping as it
   fills?" Good: bright cream, no ABL breathing over 30 s.
6. **Layering persistence** — `decay_threshold`/`decay_fast`: A=0.20/0.98 (v2),
   B=0.25/0.97 (v1), C=0.15/0.99.
   Ask: "Do old swirls linger as visible layers, or wash away / build to mud?"
   Good: visible strata, field never saturates to flat cream.

Estimated total: ~10–15 min. Winner of each round becomes the new baseline
before the next round.

## d. HDR/SDR validation checklist (hard rule from CALIBRATION.md)

HDR ON:
- [ ] Full-screen cream scene stable 30 s: no visible ABL dimming/pumping.
- [ ] Hot filaments/highlight swirls peak ≈ peak_nits, small-area only
      (verify with HDR analyzer heat-map if ambiguous; screenshots tone-map
      dark — eyeball live, never trust captures).
- [ ] Dark regions: shadow floor 0.10 keeps marbling visible, no crush, no
      gray fog.
- [ ] No out-of-band color (green/cyan) at any point — watch ≥2 full
      color_cycle_periods (60 s).

HDR OFF (Win+Alt+B):
- [ ] Remember the SDR anomaly: all brightness bands read identical in SDR —
      judge **relative look only** (hue family, contrast shape, marble
      texture), never absolute nits.
- [ ] No flat clipped white field — marbling still readable in the bright body.
- [ ] Shadow floor does not wash the darks to gray in SDR; contrast not
      crushed.
- [ ] Hue stays orange-cream (SDR is where hue judgment is still valid).
- [ ] Toggle HDR on→off→on once: no color flash / stale mapping.

Sign-off rule: both columns pass on the user's panel before Clouds.ini is
marked calibrated and the "pending quiz" header is removed.
