# Fluid Wallpaper — Native DX12 HDR Rebuild

Rebuild of the heavily-modified WebGL fluid simulation wallpaper
([reference/script.js](reference/script.js), params in
[reference/project.json](reference/project.json)) as a native Windows C++ /
DirectX 12 app that renders **behind the desktop icons** with a **true HDR
scRGB (FP16) swap chain**, so bright dye exceeds SDR reference white on an HDR
OLED.

Reference is MIT-licensed (PavelDoGreat's WebGL-Fluid-Simulation + custom
features: soft-knee brightness cap, saturation restore, wanderers with
coverage governor + survivor tier, separating dart, duty-cycled hue rotation).

## Milestones

- [x] **M1 — Wallpaper window + HDR swap chain + test gradient** *(done 2026-07-13)*
  - [x] Project scaffold (CMake + build.bat, VS Build Tools install)
  - [x] Window parented into WorkerW behind desktop icons (incl. Win11 24H2 fallback)
  - [x] DX12 device + FP16 flip-model swap chain, scRGB color space (`RGB_FULL_G10_NONE_P709`)
  - [x] HDR diagnostic gradient: hue sweep + brightness ramp with reference patches at 1×/2×/4×/8×/12.5× SDR white (80–1000 nits), animated to prove it's live
  - [x] Verified running behind icons; console reports monitor HDR state + max luminance
- [~] **M2 — Fluid sim ported to compute shaders (SDR)** *(in progress 2026-07-13)*
  - [x] All passes as HLSL compute in [src/shaders.h](src/shaders.h): curl, vorticity, divergence, pressure clear + Jacobi ×20, gradient subtract, velocity/dye advection (incl. dual-rate decay + saturation restore), velocity/dye splats (incl. proportional cap)
  - [x] Renderer split into [src/fluid.cpp](src/fluid.cpp) (ping-pong textures, per-pass descriptor tables, fixed-substep integration, idle random splats, global hue wheel)
  - [x] Display pass with reference's pseudo-normal shading; `--gradient` keeps the M1 test pattern
  - [x] `--stats` dye-field readback (mean/max/lit%/NaN) for automated verification; `--simres/--dyeres/--force-render` test flags
  - [x] Verified numerically (`--stats`: no NaNs, cap respected, idle-splat cycle + advection spread visible) and visually via `PrintWindow(PW_RENDERFULLCONTENT)` capture of the covered window — works even with a fullscreen game on top.
  - [x] **Viscosity bug fixed**: user reported "honey"; cause was a negated vorticity-confinement force (y-down handedness slip). Rule learned: port orientation-sensitive shaders verbatim (the sim is orientation-agnostic; only I/O boundaries flip). After-fix capture shows the reference's turbulent billows.
  - [x] **Color/tone fixed**: reference wrote into an sRGB canvas; our swap chain is linear scRGB. Display pass now does the sRGB decode (piecewise, after shading) and multiplies by the monitor's **SDR white level** (`DISPLAYCONFIG_SDR_WHITE_LEVEL`, user's is 240 nits, polled live) when HDR is on. Without this: SDR colors wrong, HDR 3× too dim. Also re-assert swap chain color space on HDR toggles (user saw 1s color flashes during a transition once).
  - [x] **Wide gamut**: dye is now interpreted as Display-P3 (like the WE original with `wide_gamut: true`) and converted P3→709 in the display pass; out-of-gamut saturation rides negative scRGB components. User had flagged colors as "the normal version, not the P3 version".
  - [x] **Tail decay**: `decay_fast` default 1.0 → 0.90 (author-rec value; user found tails too slow). Sim knobs now hand-editable in `%APPDATA%\FluidWallpaper\settings.ini` `[sim]`: density_diffusion, decay_fast, decay_threshold, saturation_restore, max_brightness, vorticity, splat_radius, wide_gamut.
  - [ ] User eyeball check at full res (256 sim / 1024 dye now running)
  - [ ] Then bump dye toward 4096 (project.json ships that) + perf pass
- [~] **M3 — Full feature parity with reference script** *(core landed 2026-07-13)*
  - [x] Wanderers (random/circle/figure8, survivor tier = wanderer 0, hue offsets, resume delay after user input)
  - [x] Coverage governor: 48×27 compute downsample + **async** readback at 1 Hz (no GPU sync — data is a frame stale, irrelevant at this cadence); dark %, 6×3 tile contrast check, hysteresis — direct port
  - [x] Separating dart (edge-to-edge streak at complementary hue while group paused)
  - [x] Duty-cycled hue-shift cycler; applied as the CSS hue-rotate matrix in gamma space in the display pass (composed with HDR compensation below)
  - [x] HDR compensation from the WE original (saturate 1.2 / brightness 1.08 when Windows HDR on) — this was the remaining "vividness" gap the user reported
  - [x] Mouse painting: cursor movement splats (any focus, WE-style), hold-LMB-on-desktop continuous splat with jitter
  - [x] `velocity_diffusion` / `pressure_diffusion` added to settings.ini for the motion fine-tuning the user wants
  - [ ] Bloom (project.json ships it off — low priority)
  - [ ] Non-`colorful` mode (fixed splat-color palette) — reference supports it, project.json ships colorful=true
  - [ ] User verification of the full behavior suite
- [~] **M4 — HDR brightness mapping + gamut control** *(core landed 2026-07-14)*
  - [x] Soft-knee highlight expansion: dye above `hdr_knee` (0.6) gains toward a **peak-nits target**; mids stay at SDR parity, hot spots bloom to true HDR. Off when Windows HDR off.
  - [x] Gamut selection: sRGB / Display-P3 (WE parity) / **BT.2020 (full QD-OLED)** — default 2020 per user ("the whole point was to go from P3 to the full QD-OLED gamut"). Conversion in display pass, out-of-gamut carried as negative scRGB.
  - [x] **Tray-menu controls** (user asked for control without editing files): "HDR peak brightness" (Off / Panel max / 300 / 600 / 1000 nits) and "Color gamut" submenus, persisted to `[hdr]` in settings.ini, applied live (no restart).
  - [x] User verified on the OLED: greens much better, brightness "more or less correct". Panel does **1000-nit small window** (Windows only reports 418 full-frame) → saved peak set to 1000; menu gained an 800 option and a relabeled "Windows-reported max"; settings window has the fine slider.
  - [ ] Possible later: PQ/HDR10 output path if scRGB ever limits us (not expected)
- [~] **M5 — Tray icon + settings** *(shell early; settings window landed 2026-07-14)*
  - [x] GUI app (no console; `--console` flag for diagnostics), open by double-click, single-instance guard
  - [x] Tray icon with menu: Pause, "Pause when a fullscreen app is running", Exit (restores wallpaper)
  - [x] Fullscreen pause verified against a real game (Deadlock)
  - [x] Separate "Pause when an app is maximized" toggle (user request, default on, persisted) — maximized = `IsZoomed` foreground window on our monitor
  - [x] Settings persisted to `%APPDATA%\FluidWallpaper\settings.ini`
  - [x] **Settings window** ([src/settings.cpp](src/settings.cpp), tray → "Settings…"): 18 sliders (sim feel, HDR peak 0–1500 nits fine-grained, knee, wanderer/dart params), 10 checkboxes (behaviors, pauses, HDR compensation, **opt-in Start-with-Windows** via HKCU Run — only written when the user ticks it), gamut radios, wanderer-path combo. All changes apply live (config read per frame; wanderer count/mode reinit) and write to ini immediately.
  - [x] User verified the settings window works. Discoverability round: startup balloon, tray icon re-added on `TaskbarCreated` (Explorer restarts), **second launch of the exe opens Settings** (the reliable path — user couldn't find the tray icon), Settings window has minimize + taskbar button.
  - [x] Ops note: screenshots of the HDR desktop tone-map dark — a "black" capture is not proof the wallpaper died (got fooled once; decay default reverted to WE-original 1.0 anyway, snappiness is the user's slider now).
  - [x] **Explorer-restart resilience**: shell restart destroys the WorkerW hierarchy (killed the app + left a black desktop once, live). Now: external WM_DESTROY sets a lost flag instead of quitting; main loop polls 1 Hz for the new Progman/WorkerW, recreates the wallpaper window, and `FluidRenderer::Reattach()` rebuilds only the swapchain (fresh factory, same device — fluid state survives). Tray icon re-adds via `TaskbarCreated`.
  - [x] Post color filter (WE right-panel equivalent: saturation/contrast/brightness/hue) added as 4 sliders — candidate for the original's "psychedelic" look (user ran WE at sat 57 / contrast 67 / brightness 53 / hue 54 → try 1.14 / 1.34 / 1.06 / 14°). Awaiting user's reference photos for precise matching.
- [x] **Full settings parity** *(2026-07-14, user request "add all the settings, maybe more")*: settings window now has 35 sliders / 15 checkboxes / gamut radios / wanderer-path + **sim/dye resolution combos (live recreate via `SetResolutions`)** / 5 palette color pickers (`ChooseColor`). Added to engine: fixed-palette color mode (`colorful` off → 5 custom splat colors, `more_colors`), click-burst splats, FPS-limit slider, pressure iterations, all governor/hue-shift/idle knobs. **Bloom explicitly declined by user — do not add.** Exe now builds with **static CRT** (`/MT`, 359 KB, zero deps) so the user can share the single file. `explorer /select` used to reveal it.

- [x] **Presets** *(user request, 2026-07-14)*: tray → Presets submenu. Presets are full settings.ini snapshots in `%APPDATA%\FluidWallpaper\presets\*.ini` (filename = menu name). Applying copies over settings.ini + hot-reloads everything (`LoadFullConfig` refactored out of wWinMain; resolution changes trigger `SetResolutions`; settings window closes to avoid stale sliders). "Save current as new preset" auto-names Preset N; "Open presets folder" for renaming. Built-ins created on first run: **"WE Original"** (project.json values + WE panel filter, peak off/P3) and **"Deep Clouds"** (migrated from the user's checkpoint — user's favorite, never delete). Balloon confirms apply/save.

- [x] **Themed presets + interlude cycling** *(user idea, 2026-07-15)*: new **hue band** engine feature (`hue_center`/`hue_range`, 180=classic full wheel; wanderer/dart offsets scale into the band) pins a mood in place. Designed presets (partial inis — unspecified keys inherit current settings; ApplyPreset merges then `SaveFullConfig` persists the resolved state): **Sunny Embers** (orange/red band 25°±28°, slow dilute, deep-cloud caps — user's favorite mood), **Ember Depths** (palette mode: small red dots + blue accents, dark floor 30), **Verdant** (green band). **Interlude cycling**: base settings for ~N s → chosen/random preset paints into the same field for ~M s → back, ±30% jitter; look-only (never resolution/fps); tray toggle + interlude picker, durations as sliders. Manual preset apply resets an in-flight interlude.

- [x] **Bonus — HDR analyzer** *(user idea, 2026-07-14)*: tray → "HDR analyzer…". Parallel 640×360 render of the final scRGB output (post gamut/peak), 10 Hz async readback, false-color nits heat-map (log 0.05–1500), click-to-freeze, hover readout (luminance nits, max-channel nits, raw scRGB incl. negative wide-gamut values), H toggles heat-map/image. [src/analyzer.cpp](src/analyzer.cpp). Also: vortex icon embedded in exe ([src/app.rc](src/app.rc), assets/fluid.ico), desktop shortcut created, `/utf-8` compile flag (title-bar mojibake fix, applies next rebuild).

- [x] **M6 — Multi-monitor support (mirror mode)** *(2026-07-15)*
  - [x] "Mirror on second monitor" (settings checkbox, ON for this user): same dye field rendered to a second WorkerW window + swapchain on monitor 2, with per-monitor HDR mapping (own sdrScale/peak; SDR monitor gets parity mapping). Cheap: display pass only, sim shared. Mirror Present is interval-0 (main chain paces vsync) and non-fatal; lifecycle handles toggle/broken/Explorer-restart. Pause rules extended to fullscreen/maximized apps on either covered monitor. Verified via window tree + capture.
  - [ ] Possible later: independent sim per monitor (different fluid on each)
  - [x] Project under local **git** (2 commits so far) — `git log` in the project folder

## Current state (2026-07-13)

- **M1 complete and verified.** Build with [build.bat](build.bat), run
  `build\FluidWallpaper.exe`, Ctrl+C (or `taskkill /im FluidWallpaper.exe`) to quit.
- Verified via window-tree inspection: `Progman → WorkerW → FluidWallpaperWnd`,
  with `SHELLDLL_DefView` (icons) above us. Screenshot confirmed gradient renders.
- Console diagnostics confirmed: 24H2 WorkerW path, FP16 swap chain + scRGB
  color space accepted, Windows HDR **ON**.
- Now a **GUI tray app** (user request): double-click to start, tray menu to
  pause/exit. `--console` flag opens a diagnostics console. Logged output still
  reaches a redirected stdout pipe without the flag (handy for automated runs).
- **Fullscreen detection** (`FullscreenAppActive`): foreground window on our
  monitor, not maximized (`IsZoomed`), no `WS_CAPTION`, no `WS_THICKFRAME`
  (excludes maximized Electron/custom-titlebar apps), rect covers the monitor.
  Shell/own windows excluded by class. Checked every 500 ms; pause = no render,
  no GPU work. Validated live against Deadlock (SDL_app class).
- **Scenes window + Present-race fix (2026-07-15)**: second Explorer restart
  killed the app — `Present` failed mid-frame before WM_DESTROY arrived and hit
  the fatal handler. Now Present failure sets `m_presentBroken` (non-fatal);
  shell treats it as wallpaper-lost and reattaches. This was the user's "mouse
  stopped working" (whole app was dead). New [src/scenes.cpp](src/scenes.cpp):
  RGB-suite-style manager (tray → "Scenes…", Settings → Scenes button): scene
  list, apply/overwrite/new/delete, interlude picker + random toggle + duration
  sliders. Preset API exposed via app_state.h wrappers. DarkMode_Explorer/CFD
  themes on buttons/combos. NOTE: user had cycling enabled with random
  interludes — likely contributed to "settings feel different" reports.
- **UI polish (2026-07-15, user afk batch)**: settings + analyzer windows in
  dark mode (DWMWA_USE_IMMERSIVE_DARK_MODE + custom brushes); settings gained
  Pause/Resume + Exit buttons and a live "Rendering at N fps" readout (500 ms
  timer); analyzer status bar shows fps; FPS-limit slider max 260 (user has a
  240 Hz monitor). Verified 60 vs 144 fps stats statistically identical after
  the independence fix; user confirmed "looks the same now". Note: PrintWindow
  captures of the dark settings window are unreliable (blank regions) —
  verify controls via EnumChildWindows instead.
- **Frame-rate independence (bug found 2026-07-15)**: the FPS-limit slider made
  physics fps-dependent — per-step multiplicative decays (velocity/density
  dissipation, decay_fast, pressure decay) applied 144×/s at 144 fps vs the
  ~120 steps/s the reference was tuned at → "viscosity messed with, settings
  identical" (user report, correctly diagnosed as code not data). Fix: raise
  each factor to `dt*120` in SimStep, and scale per-frame emitters (wanderer/
  dart/mouse dye) by `dt*60` so trails don't paint 2.4× hotter at 144 fps.
  `--fps N` test flag; verified 60 vs 144 stats converge.
- **Live HDR detection**: polled every 2 s via a fresh DXGI factory (stale
  factories report stale color spaces), matched to our HMONITOR. On change:
  logged, tray tooltip updated, shader constant flipped — SDR mode compresses
  the test pattern into 0..1 instead of clipping. User can verify with the
  Win+Alt+B HDR toggle. (M4 replaces this compress with proper HDR mapping.)
- Next: **M2** — port sim passes to HLSL compute.

## Machine facts (dev box)

- Windows 11 Home, **24H2 desktop layout** (WorkerW is a child of Progman).
- GPU: AMD Radeon RX 7900 GRE. Two monitors, each 2560×1440 (virtual desktop 5120×1440).
- Primary display reports: HDR ON, MaxLuminance 418 nits, MaxFullFrame 254 nits
  (EDID values; use as defaults for the M4 peak-nits slider).
- **Wallpaper Engine runs on this box** (`WPEDesktopCEFWindow` primary /
  `WPEDesktopDX11Window` second monitor, same WorkerW). Our window creates on
  top of WE's inside the wallpaper layer; user should stop WE when running ours
  to avoid burning GPU on a hidden wallpaper.
- VS 2022 Build Tools at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`.
  The stray `'vswhere.exe' is not recognized` line during builds comes from
  inside Microsoft's vcvars scripts — harmless.
- stdout must stay unbuffered (`setvbuf` in main) or piped diagnostics never appear.

## Design decisions

- **Behind icons:** send `0x052C` to Progman to spawn WorkerW; parent our
  window to the WorkerW *after* the one holding `SHELLDLL_DefView`. On Win11
  24H2 `SHELLDLL_DefView` lives directly under Progman — then use the WorkerW
  child of Progman, else Progman itself. On exit, poke
  `SPI_SETDESKWALLPAPER(sendchange)` so the static wallpaper repaints.
- **HDR:** swap chain `R16G16B16A16_FLOAT` + `SetColorSpace1(G10_P709)`
  (scRGB, 1.0 = 80 nits). Works composited even when parented under WorkerW;
  values > 1.0 clip when Windows HDR is off (M4 adds proper SDR fallback).
- **Shaders:** runtime-compiled via `D3DCompile` (SM 5.x) for fast iteration;
  may switch to DXC/SM6 later if needed.
- **M1 scope:** primary monitor only. Multi-monitor later if wanted.

## Mapping reference → native (for M2/M3)

| Reference (WebGL)                  | Native plan (DX12)                          |
|------------------------------------|---------------------------------------------|
| fragment-shader passes over FBOs   | compute shaders over UAV textures (ping-pong)|
| `canvas.style.filter` hue-rotate / HDR compensation | final composite pass (hue matrix, sat/brightness/contrast) — real HDR replaces the "compensation" hack |
| `gl.readPixels` 48×27 coverage     | 48×27 downsample + readback buffer (or CS reduction), 1 Hz |
| Wallpaper Engine property listener | settings file + tray UI (M5)                |
| `wallpaperRegisterAudioListener`   | optional later (WASAPI loopback) — not in M1–M5 scope |
