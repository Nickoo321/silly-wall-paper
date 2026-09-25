# FluidWallpaper — Handoff

## What it is

FluidWallpaper is a single C++/D3D12 Windows executable that renders a real-time
fluid simulation as a desktop wallpaper, drawn behind the desktop icons via a
WorkerW window. It supports Windows HDR (and runs correctly with HDR off). One
exe provides several selectable render looks, switched live from a tray menu
and Settings window: a Wallpaper-Engine-parity fluid look, a "Liquid Acid"
black-oil-on-inked-water look, and an "Ink in water" look (paper and inverted
variants). A screen-mirror/kaleidoscope overlay can be applied on top of any
of the three looks.

## How to run

- Build (agents only): `build2.cmd FluidWallpaper` from PowerShell, output in
  `build2\`. The user's live wallpaper runs from `build\FluidWallpaper.exe`
  and is built separately by the user.
- Config file: `%APPDATA%\FluidWallpaper\settings.ini`, plus per-mood overlays
  in `moods\*.ini` and saved presets in `presets\*.ini`. Ini files are read on
  startup only — there is no disk hot-reload; a hand-edited ini needs an app
  restart. The Settings window writes the live config to disk immediately.
- CLI flags (from `src/main.cpp` argv parsing):
  - `--console` — opens a diagnostics console (re-attaches stdout, so shell
    redirection of the process's own output stops working).
  - `--gradient` — pattern/test-page render path instead of the sim.
  - `--calibrate N` — shows calibration quiz page N.
  - `--stats` — prints frame stats.
  - `--force-render` — forces rendering even if the window would normally skip it.
  - `--test-suspend` — test hook for the suspend/resume path.
  - `--simres N` / `--dyeres N` — override the simulation/dye grid resolution.
  - `--fps N` — override the frame-rate target.
  - `--shot <file>` — offscreen single-frame (or series) capture mode; exits
    after writing the file(s). Related shot-mode flags: `--shot-size WxH`,
    `--shot-delay N` (seconds before capture), `--shot-series N:interval`,
    `--shot-yield N` (ms yielded to the OS between shot-mode frames),
    `--shot-pour`, `--shot-drop`, `--shot-mouse`, `--shot-preset`,
    `--seed N`, `--ini <file>`, `--hdr on|off`, `--sdr-white N`,
    `--panel-max N`, `--mouse-none`.

## The looks

- **WE-parity fluid** — the default fluid sim tuned to match the reference
  Wallpaper Engine fluid look. Image: `look-fluid-we-parity.png`.
- **Liquid Acid (black oil)** — posterized metaball oil blobs on inked water,
  flat banded interiors, a thin bright rim at the oil edge. Two renders are
  included to show its range: `look-liquid-acid-black-oil.png` (dark, oil-
  dominant framing) and `look-liquid-acid-vivid.png` (bright pink/green
  framing).
- **Monotone post (current live look)** — the Liquid Acid look with a single-
  hue monochrome post grade. Image: `look-monotone-post-0924.png`.
- **LAPD dyed candidate** — a Liquid Acid variant with a red/blue dye pairing.
  Image: `look-lapd-dyed-r8.png`.
- **Ink in water — paper** — dark ink on a backlit white background (Beer-
  Lambert dye absorption). Image: `look-ink-paper.png`.
- **Ink in water — inverted** — pale ink on a black background (the OLED
  variant of the same look). Image: `look-ink-inverted.png`.
- **Mirror (quad)** — the screen-mirror/kaleidoscope overlay in quad mode,
  applied here over the fluid look. Image: `look-mirror-quad.png`. The mirror
  overlay (`[mirror] mode`) can run on top of any of the three looks above.

## Images

| File | Look / source | Notes |
|---|---|---|
| `look-fluid-we-parity.png` (+ `.jxr`) | WE-parity fluid | `build2/shots/live/parity-941cd4d.png` |
| `look-liquid-acid-black-oil.png` (+ `.jxr`) | Liquid Acid | `build2/shots/live/bp-hero.png` |
| `look-liquid-acid-vivid.png` (+ `.jxr`) | Liquid Acid | `build2/shots/live/bn-hero.png` |
| `look-monotone-post-0924.png` (+ `.jxr`) | Monotone post (current live look) | `build2/shots/live/hq-mono-0924.png` |
| `look-lapd-dyed-r8.png` (+ `.jxr`) | LAPD dyed candidate | `build2/shots/live/lapd-look-r8.png` |
| `look-ink-paper.png` | Ink in water, paper mode | `build2/shots/ink/cost-inkpaper.png` |
| `look-ink-inverted.png` | Ink in water, inverted mode | `build2/shots/ink/cost-inkinv.png` |
| `look-mirror-quad.png` | Mirror overlay, quad mode, on the fluid look | `build2/shots/mirror/we-quad-060.png` |

All images are full-frame renders produced by FluidWallpaper's own `--shot`
capture mode (offscreen GPU render), not photos of a monitor. The `.jxr`
files are the HDR twin of the same render where one was available; the `.png`
is the SDR/tone-mapped version.

## Settings

The complete list of ini keys exposed in the Settings window as sliders and
checkboxes — 341 sliders and 25 checkboxes, one line per key with its range
and what it does — is in `SETTINGS.md` in this folder.

## Lightroom photos

Three of the user's Lightroom edits of the monotone-post-0924 look
(`look-monotone-post-0924.png` above), copied into `lightroom\`:

| File |
|---|
| `bu-lr-1-splittone-shadows-teal.webp` |
| `bu-lr-3-graduated-temp-dehaze.webp` |
| `bu-lr-4-graduated-desaturate.webp` |
