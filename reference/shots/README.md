# WE reference captures — "Hyper-Vivid Fluid" (workshop 3764348580)

Captured 2026-09-15, 22:30:19 – 22:33:20 local (EDT). Capture only: Wallpaper Engine was
left running and untouched, no focus changes, no input, FluidWallpaper.exe never launched.

## Which monitor (the brief had this backwards)

The fluid wallpaper is on the **PRIMARY** monitor, not the secondary.

| GDI name | Position | Monitor | Role | Content |
|---|---|---|---|---|
| `\\.\DISPLAY2` | **X=0, Y=0, 2560x1440** | X27U W1 (OLED), target 264 | **primary** | **target — Hyper-Vivid Fluid** |
| `\\.\DISPLAY1` | X=2560, Y=0, 2560x1440 | BenQ GW2765, target 260 | secondary | user's working desktop (apps, browser) — obstructed |

The secondary was completely covered by application windows, so it was never a capture
candidate anyway. The primary showed the fluid full-bleed with no icons and no windows over it.

## Method

`System.Drawing.Graphics.CopyFromScreen(0, 0, 0, 0, 2560x1440)` from a hidden background
PowerShell process, saved as PNG. Plain screen capture worked on the first try — no
`PrintWindow`/`PW_RENDERFULLCONTENT` fallback was needed, and no WorkerW/webwallpaper64 window
had to be located. Every frame verified non-black by sampled mean brightness (series means
ranged 20–91 of 255; see `_capture-log.txt`).

## HDR state — this matters for color comparison

Queried via `QueryDisplayConfig` + `DisplayConfigGetDeviceInfo`
(`GET_ADVANCED_COLOR_INFO`, `GET_SDR_WHITE_LEVEL`):

| Display | HDR supported | **HDR enabled** | Wide gamut | Bits/ch | SDR white level |
|---|---|---|---|---|---|
| DISPLAY2 (primary, captured) | yes | **YES** | no flag | 10 | 2791 → **~223 nits** |
| DISPLAY1 (secondary) | yes | no | yes | 10 | 1000 → ~80 nits |

**So these PNGs are the SDR-composited projection of an HDR desktop.** Practical consequences
when diffing against the DX12 port:

- They do **not** look tone-mapped dark — the wallpaper is bright enough that the SDR view is
  vivid. But highlights **clip**: 8 of 23 series frames have >10% of pixels at ≥250 in some
  channel (peak 28% at t=16s). Real on-screen peak brightness is well above what the PNG shows.
- Hue and relative saturation survive the SDR projection and are the trustworthy part.
  Absolute luminance and any highlight rolloff are **not** — do not tune the port's exposure
  or bloom against these files.
- The SDR white level (~223 nits) is consistent with the machine notes (1000-nit OLED,
  SDR white ~240).

## Files

Time series — one full-res 2560x1440 PNG every 8 s for 176 s (23 frames), named by elapsed seconds:

```
we-000.png  we-008.png  we-016.png  we-024.png  we-032.png  we-040.png
we-048.png  we-056.png  we-064.png  we-072.png  we-080.png  we-088.png
we-096.png  we-104.png  we-112.png  we-120.png  we-128.png  we-136.png
we-144.png  we-152.png  we-160.png  we-168.png  we-176.png
we-contact.png     <- 5x5 contact sheet, 512px tiles, timestamped
```

Bursts — 12 frames each, captured back-to-back to show motion. Measured intervals were
**~123 ms (8.1 fps)**, very stable across all three bursts (per-frame elapsed_ms below,
full list in `_burst-log.txt`):

| Burst | Wall clock (ends) | Series t | Frame elapsed_ms | Span | Effective rate |
|---|---|---|---|---|---|
| burst1 | 22:31:20 | ~t+60 s | 46,158,283,406,534,657,782,909,1032,1154,1282,1402 | 1.36 s | 8.1 fps |
| burst2 | 22:32:18 | ~t+118 s | 36,156,285,405,531,656,780,906,1029,1159,1280,1405 | 1.37 s | 8.0 fps |
| burst3 | 22:33:16 | ~t+176 s | 30,160,283,407,535,659,782,906,1032,1159,1282,1405 | 1.38 s | 8.0 fps |

```
burst1-00.png .. burst1-11.png   burst1-contact.png   burst1.gif  (1280px, 150 ms/frame)
burst2-00.png .. burst2-11.png   burst2-contact.png   burst2.gif
burst3-00.png .. burst3-11.png   burst3-contact.png   burst3.gif
```

Author's intended look — 8 evenly spaced frames from the workshop item's `preview.gif`
(source is only **192x192**, 50 frames, so treat it as a hue/mood reference, not a detail reference):

```
gif-00.png .. gif-07.png   gif-contact.png
```

Logs: `_capture-log.txt` (series, per-frame mean/max), `_burst-log.txt` (burst frame timings).

## What the frames actually show

**Dominant hues / the global hue walk.** The whole screen re-tints in discrete steps roughly
every 10–20 s, and the steps are large — consecutive 8 s samples jump from green (t=0) to
cyan/teal (t=8) to magenta (t=16) to blue-violet (t=24) to orange (t=32). Over 176 s the
series visits essentially the full hue circle. Crucially the tint applies to *everything* at
once — splats, background haze and outlines all shift together, which reads as a global
post-process hue rotation rather than per-splat palette picks. The workshop `project.json`
confirms the source palette is pure primaries: splat colors are `(0,1,0)`, `(0,1,1)`,
`(0,0,1)`, `(1,0,0)`, `(0.94,1,0)` and `background_color` is `(0,0,0)`.

**Saturation is extreme.** Mean HSV saturation over non-dark pixels is **0.74–1.00**, and 11 of
23 frames sit at ≥0.95. Several frames have a channel driven to literal zero (t=168 mean R =
0.0; t=176 mean G = 2.4; t=88 mean R = 9.7). This is fully-saturated, near-clipped primary
color — if the port's output looks "tastefully desaturated" next to this, the port is wrong.

**The background is NOT black.** Only 0–4.7% of pixels are below 16/255 (median ~1%). The
"empty" areas are a dark *tinted* haze — deep maroon, bottle green, near-black teal — that
carries the current global hue, plus faint residual dye structure. A port that clears to pure
black between splats will read as emptier and harder-edged than the reference.

**Splat brightness and structure.** Bright cores clip to 255 in most frames (median ~9% of
pixels ≥250). Fresh splats are near-white-hot at the center with a saturated halo; older dye
decays into the tinted haze rather than fading to black. `burst2-contact.png` shows a green
plume advancing on a purple field over 1.4 s — the motion per 125 ms frame is small but clearly
visible, so the sim is slow and viscous, not flickery.

**Outlines / shading.** There is strong dark edging between adjacent dye masses — a near-black
outline that traces the fluid's filaments and gives the whole thing a marbled / acrylic-pour
look. This is one of the most distinctive features and is very visible in `we-048.png`,
`we-104.png` and `burst2-contact.png`. It is not a soft gradient; it is a hard dark rim.

**Banding / posterization.** Visible, and it looks like a deliberate part of the style rather
than a capture artifact: the dye masses break into flat-ish color plateaus with abrupt steps
between them (clearest in `we-016.png`, `we-136.png`, `we-176.png`). Fine turbulent detail is
preserved as high-frequency filigree on top of those plateaus. Some of the hard stepping is
certainly the 8-bit SDR capture of a 10-bit HDR signal, but the plateau *shapes* follow the
fluid, not the bit depth, so the effect is at least partly in the shader.

**One near-black frame.** `we-168.png` (mean 20.5, max 70) is not a failed capture — it is a
genuine near-fade, a very dark cyan/green field with faint structure and 0% clipped pixels,
caught between hue steps. Worth keeping: it shows how low the wallpaper's floor goes.

**Author's gif vs. the live wallpaper.** The `preview.gif` frames are noticeably tamer —
saturation 0.83–0.91, 0% pure-black pixels, a slow magenta → rose → red → orange drift with a
single green intruder at the end, over a warm near-black ground. The live wallpaper on this
machine is more saturated, higher contrast, and cycles hue far more aggressively. That gap is
the user's own property settings (and the HDR path), not a different wallpaper.

## photos/ — user's phone photos of the panel (added 2026-09-16)

Phone photos show the real P3 gamut (screenshots clip it). All are the WE original.
- `we-2026-09-16-salmon-red-purple-*.jpg`: what the user calls the trippy hue fade — one
  dye mass grading salmon core -> orange -> saturated red -> dark red -> purple haze at
  the rim, and walking down that ladder as it decays. Mechanism: per-channel clipping in
  the 8-bit canvas + WE contrast crush + global hue-rotate; not a painted gradient.
- `we-old-cyan-clouds-on-magenta.jpg`: older WE revision; mint/cyan clouds with navy rims on a
  fully tinted magenta-red haze (the background is never black here).
- `we-old-cream-red-full-pour.jpg`: older WE revision; full-screen acrylic-pour look, cream/
  salmon cores clipped toward yellow-white on deep red, dense fine curls (sim_res 256,
  vorticity 48), green pocket on the left.
Earlier photos from 2026-09-15 (cyan marble, green cloud, red/blue/green on black) were shown
in chat only.
