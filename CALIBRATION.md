# Calibration benchmarks — user's panel & perception

Measured 2026-07-24 via `--calibrate N` quiz pages (rendered by the app itself,
behind desktop icons, judged on the panel). Panel: **X27U W1** (QD-OLED,
2560×1440, 240 Hz), Windows HDR on and off both tested.

Use this when building/tuning any mood or preset — these are the user's
perception anchors, not one-off answers.

## Black level (page 1: 10 strips, 0.16–6.4 nits linear scRGB)

- User can distinguish **all 10 strips** from black, HDR on AND off. The panel
  resolves near-black extremely well → shadow floor is a *taste* choice.
- Chosen presence: **strips 2–4 (0.32–0.64 nits)** → subtle-to-balanced lift.
- Baked value: `shadow_floor = 0.10` (gamma-space dye units), `shadow_knee = 0.15`.
  Conversion: gamma_floor ≈ linear^(1/2.2); 0.006 linear ≈ 0.48 nits → 0.10 gamma.

## Saturation (page 2: CSS-saturate swatches, gamma space)

- Neon target: **columns 5–6 → post_saturation 1.6–2.0**. Baked: **1.70**.
- User likes punchy/neon; pastel range (0.8–1.0) tested and rejected for Neon.

## Brightness / blowout (page 3: 80–1500 nits bands)

- **SDR**: user report — literally ALL 10 bands (80–1500 nits) looked the same
  brightness, including bands 1–2 which sit *below* SDR white (240 nits).
  Unexpected: not simple clipping at SDR white. Likely the Windows SDR
  brightness slider remapping the whole scRGB range. Treat as an open anomaly —
  do not read SDR quiz results as luminance-accurate; SDR judgments should be
  about relative look (hue/contrast), not absolute nits.
- **HDR**: band 5 (400 nits) visibly > band 4 (300); band 6 (500) NOT visibly
  brighter than 5 → large-area perception saturates ≈400–500 nits (ABL).
- **ABL is a moving slider** (user's words): full-screen brightness gets dimmed
  by the panel. Implication: design hot elements *small* (cores, filaments) so
  they ride the small-window headroom; don't chase full-screen brightness.
- Panel headroom (from NOTES.md): ~1000 nits small window, ~418 full frame,
  SDR white 240 nits.
- **Neon directive: "as bright as possible"** → `peak_nits = 1000`. Small hot
  cores hit it; the panel's ABL handles large areas on its own.

## HDR on/off rule (hard requirement)

Every mood/preset must look good with Windows HDR **on AND off**. Verify both
before calling anything done. HDR-off behavior: values >1.0 scRGB clip; the
shadow floor and post filter must not produce washed-out or crushed results
in SDR.
