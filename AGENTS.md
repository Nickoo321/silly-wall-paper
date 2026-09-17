# AGENTS.md — FluidWallpaper

C++/D3D12 desktop fluid-sim wallpaper (renders behind the icons via WorkerW).
Single exe, no deps beyond Windows SDK libs.

## Build & run

- Agents build ONLY into `build2/` (`build2.cmd FluidWallpaper`, run from PowerShell; see the
  GPU-lock section). `build.bat` -> `build/` is the USER's live exe: never build there and
  **never stop the running FluidWallpaper.exe** — it is the user's wallpaper. If a build into
  `build/` is ever needed, the user relaunches it themselves (or via tools/panel-check.ps1 with
  their go-ahead).
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
- **`style=ink`** ("ink in water") — third display PSO, `#ifdef INK` in
  `kDisplaySrc`. Beer-Lambert absorption on the dye (`T = exp(-k*thickness)`),
  edge darkening for folds, two modes: paper (dark ink on backlit white) and
  INVERTED (pale ink on black — the OLED one). The block is SHARED: the same
  `InkWater()` is used by `[liquid_acid] ink_mode=water` (oil on ink-in-water).
  Sim-side and style-agnostic: `[sim] gravity/gravity_pow/gravity_blur`
  (dye-weighted gravity) and the `[drops]` emitter (`InjectDrop`,
  `--shot-drop X,Y,T[,VY]`). Inis: reference/configs/ink-{paper,inverted}.ini,
  liquid-acid-water.ini. Gotchas: `saturation_restore` MUST be 0 (the restore
  erases neutral dye); vorticity 48 turns a drop into a smoke puff — the ink
  inis use 12; `idle_splats=0` also suppresses the startup burst so the water
  starts clear.
- **`[mirror]`** — screen mirroring / kaleidoscope. ONE uv transform at the top of
  `PSMain` in the SHARED part of `kDisplaySrc` (`MirrorFold`), so all three
  looks fold together; `mode=0` returns early and the fluid PSO is
  bit-identical. Keys: mode (0 off / 1 horiz / 2 vert / 3 quad / 4 kaleido),
  segments, source, center_x/center_y, rotate_period, drift, soft. Root
  CONSTANTS at b3 — bind it on EVERY display draw. Display-only and
  look-agnostic, so it ships as PARTIAL overlay presets
  (`reference/presets/Mirror - *.ini`) that fold whatever look is running.
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

## GPU lock (multi-agent rule, 2026-09-16)
Only ONE GPU render (any `--shot` run, any build target, any worktree) may run at a time —
two at once starved DWM and greyed the user's OLED. Before any render: wait until
`build2/shots/gpu.lock` (absolute: C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\build2\shots\gpu.lock)
does not exist (poll every 5 s; give up after 30 min and report), then create it containing
your agent name + time, render, and delete it in a `finally`/trap. Builds need no lock. Never
stop the user's FluidWallpaper.exe or Wallpaper Engine.

## Pausing the live sim

The only sanctioned way to touch the user's running FluidWallpaper.exe is a PAUSE via its tray window (WM_COMMAND CMD_PAUSE=1, a toggle): `toolsway-pause.ps1` does this when the user is away and resumes on input. Never stop, kill or relaunch the exe; never SC_MONITORPOWER.
