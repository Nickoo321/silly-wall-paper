# AGENTS.md — FluidWallpaper

C++/D3D12 desktop fluid-sim wallpaper (renders behind the icons via WorkerW).
Single exe, no deps beyond Windows SDK libs.

## Build & run

- `cmd //c build.bat` (CMake + Ninja, output `build/FluidWallpaper.exe`).
- **Always stop the exe first**: `powershell Stop-Process -Name FluidWallpaper -Force`
  — build fails LNK1104 if it's running.
- Config: `%APPDATA%\FluidWallpaper\settings.ini` (+ `moods\*.ini`, `presets\*.ini`).
  **No disk hot-reload** — hand-edited ini needs an app restart. The Settings
  window writes config live.
- Useful flags: `--console` (diagnostics console; note it re-attaches stdout so
  shell redirection goes silent), `--calibrate N` (quiz pattern pages),
  `--gradient`, `--stats`, `--force-render`.

## Hard constraints (user directives)

- **Bloom: never.**
- **sim_res/dye_res live at 256/4096** (256 = the WE grid; verified 2026-09-16 that the WE motion — droplet curls, marbled cores — only appears at 256, vorticity 48). Moods/presets must never carry them.
- Every mood recipe is finalized only via a calibration quiz with the user
  (protocol: CALIBRATION.md + cloud-quiz-round2-proposal.md).
- Everything must look good with Windows HDR **on AND off** — verify both.
- Screenshots tone-map dark and are SDR-inaccurate (see CALIBRATION.md
  anomaly); treat them as rough hue/composition checks, never luminance truth.
- Panel: X27U W1 QD-OLED, ~1000 nits small window, ~418 full frame, ABL —
  keep hot elements small; don't chase full-screen brightness.

## Layout

- `src/fluid.cpp/h` — sim + render + hue-shift cycler + coverage readback.
- `src_oil/` — OilWallpaper PoC (separate CMake target/exe, `build/OilWallpaper.exe`):
  lava-lamp metaball oil blobs, one fullscreen-triangle PS + CPU blob sim.
  Hardcoded config, no ini. Flags: `--shot x.bmp` (GPU-readback dump, then
  exits), `--shot-delay N`, `--palette 0|1`, `--console`; log at
  %TEMP%\OilWallpaper.log (share-read). SDR only; HDR is a follow-up.
- `src_acid/` — AcidWallpaper PoC (separate CMake target/exe,
  `build/AcidWallpaper.exe`): "liquid acid" oil-and-ink look — posterized
  metaball blobs, flat banded interiors, thin bright rim at the field==1
  boundary (Gaussian on sdf=(field-1)/|grad|), meniscus band, interface
  speckle, film grain. Same shell/flags as src_oil; log at
  %TEMP%\AcidWallpaper.log. SDR only, no bloom. Gotcha: only accumulate the
  field gradient where the per-blob weight is unclamped, else sdf->0 at blob
  centers and the rim color floods the interior.
- `src/moods.cpp/h` — mood conductor (DWELL→SHIFT→EMIT→RETURN transitions).
- `src/main.cpp` — app shell: WorkerW, tray, HDR detection, ini load/save.
- `src/settings.cpp` — Settings window (primary UI; user's taskbar is hidden).
- `src/scenes.cpp` — Scenes manager (interlude UI is dead; Job 2 rework pending).
- Docs: WORKLOG.md (session logs + state dumps), NOTES.md (full history),
  CALIBRATION.md (quiz benchmarks), backup-pre-rework-2026-07-24/ (snapshot).

## Conventions

- ini overlays (moods/presets) are partial: unspecified keys keep live values.
- Hue rotation is post-process (`CssHueRotate(+m_hueAngle)`); emission
  counter-rotates while a hue command is active (WheelHue) — keep that
  invariant when touching color generation.
- Never zero `m_hueAngle` abruptly (whole-screen color snap) — always glide
  to the next full turn.
