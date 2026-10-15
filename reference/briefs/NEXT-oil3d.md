# NEXT — Oil3D: the oil wallpaper as a TRUE 3D lava lamp (prototype)

Status: CODE LANDED on branch `oil3d` (cloud executor, 2026-10-14/15) -- waiting for the first
LOCAL build + real-GPU shots + timings, then the user's look verdict. See "Cloud report" at the
end. Written by the local orchestrator for a CLOUD executor. The cloud writes the code; the local
session builds it into `build2/`, renders headless shots, sends them to the user and feeds fixes
back. (The cloud DID manage to compile with the real fxc and render through D3D12 on a CPU
Vulkan device -- see the report -- so this build is smoke-tested, not untested.)

## The ask (user, 2026-10-14, verbatim gist)

"Make a copy of the wallpaper oil, but in 3D. Make a working prototype first. Don't tab me out."
Picks from the A/B popup:
- 3D style: **True 3D lava lamp**: raymarched blobs with real depth that pass in front of/behind
  each other, glossy light, rim glow, soft depth. Same palettes and slow rise as the live oil.
- Backdrop / colours: **"whatever colors and modes exist in the current oil thing"**: the 3D
  version must carry the live oil's colour schemes and modes (see "Colours and modes" below),
  not a new palette.

## What "the oil" is today (read these, they are the source of truth)

The live oil is FluidWallpaper's `[look] style=liquid_acid` (NOT the old src_oil PoC):
- `src/fluid.h` — `struct LiquidAcidConfig`: every oil key with comments (population, sizes,
  Wyvill kernel, threshold, rise mode, holes, droplets, palettes, schemes, monotone, film...).
- `src/shaders.h` — the `#ifdef LIQUID_ACID` blocks of `kDisplaySrc` (~line 1143 on): the 2D
  metaball field, flat fills (w^4 soft-max colour), rim, holes, droplets, ink under the oil.
- `src/fluid.cpp` — the CPU blob sim (search `AcidBlob`, rise mode, wrap/re-entry, turnover).
- `src/main.cpp` — the ini loader for `[liquid_acid]` (search `L"liquid_acid"`), so you know
  every key name and its default.
- The LIVE cycle: `reference/configs/cycle-flow.ini`. Its oil stages are:
  `monotone-stage.ini`; `acid-rise-12-tone-half.ini`; and every `../presets/Scheme - *.ini`
  stage layered over the base `monotone-post-0924.ini`. Every oil stage also gets the layout
  `reference/presets/Layout - Few giants.ini` (50 blobs, big black holes, the user's pick
  "add the number of bigger black blobs"). Layering order: base, then layout, then the stage file
  (partial overlays: unspecified keys keep the previous value). `scheme_hue_period=900`.
- `reference/configs/acid-rise-12.ini` — the shipped lava-lamp rise preset (8 curated palettes,
  900 s per trip).
- `reference/briefs/DONE-lava-lamp-rise-mode.md`, `DONE-transparent-oil.md`: why the rise and
  the film look the way they do.

## Deliverable: a new standalone target `Oil3DWallpaper` (src_oil3d/)

Mirror the parked 3D ink project's shell, which is proven (its M0 renders): `src_ink3d/main.cpp`
(headless shot shell, logging, WIC PNG writer, HDR/SDR outputs, `--shot-*` flags, yield),
`src_ink3d/config.*` (ini section loading), `tools/ink3d-shot.ps1` (GPU-lock wrapper). Copy and
adapt; do NOT modify src_ink3d, src_oil, src_acid or src/ (read only).

Files:
- `src_oil3d/main.cpp` (shell), `src_oil3d/oil3d.cpp/.h` (renderer + CPU blob sim),
  `src_oil3d/shaders.h` (HLSL as C++ raw strings; MSVC caps ONE string literal at 16380 bytes,
  so split long shaders into several adjacent `R"hlsl( ... )hlsl"` literals like src/shaders.h),
  `src_oil3d/config.cpp/.h`.
- `CMakeLists.txt`: add the target exactly like `Ink3DWallpaper` (WIN32, UNICODE, /utf-8, links
  d3d12 dxgi d3dcompiler user32 shell32 ole32 windowscodecs). Nothing else in CMake changes.
- `tools/oil3d-shot.ps1`: copy of ink3d-shot.ps1 for this exe (same gpu.lock rules).
- `reference/configs/oil3d-default.ini`: the prototype's own defaults.

### Hard constraints
- **HEADLESS ONLY in this prototype.** No window, no WorkerW attach, no tray, no swap chain, no
  focus change, no MessageBox: launching without `--shot` logs one line and exits (exactly like
  src_ink3d). The user games; nothing may pop up or steal focus. The desktop attach is a later
  milestone after the user OKs the look.
- No new dependencies: Windows SDK only. Shaders compiled at runtime with `D3DCompile`
  (fxc, SM 5.1: `vs_5_1`/`ps_5_1`/`cs_5_1`). No SM6-only features (no wave ops, no 16-bit types).
- Deterministic: fixed 60 Hz sim step driven by simulated time; every random routes through
  `--seed` (default 1234). Same flags => same image.
- `--shot-yield MS` sleeps per simulated frame (default 2) so the user's GPU stays responsive.
- Never stop/launch FluidWallpaper.exe or Wallpaper Engine (the user's live wallpaper).
- Panel: QD-OLED, ~1000 nits on small areas, ~418 full frame, ABL. Keep the hot stuff
  (speculars, rim glints) SMALL; mostly dark frame; nothing sits perfectly still.
- Everything must look right with Windows HDR on AND off: write the SDR PNG and the HDR PNG
  like ink3d does (`--hdr on|off`, `--sdr-white N`, `--panel-max N`).

### Flags (reuse ink3d's names where they exist)
`--shot out.png`, `--shot-size WxH` (default 2560x1440), `--shot-delay S` (simulated seconds
before the capture; default 15), `--shot-series N:S`, `--shot-yield MS`, `--seed N`,
`--ini <path>` (REPEATABLE, applied in order as partial overlays: this is how base + layout +
scheme stack), `--hdr on|off`, `--sdr-white N`, `--panel-max N`, `--timings` (per-pass GPU ms
via timestamp queries, printed to the log), `--console`.
Plus prototype look overrides, each also an `[oil3d]` ini key: `--blobs N`, `--half-res 0|1`,
`--steps N`, `--camera-drift X`, `--shade flat|gloss`.

## The look (prototype scope)

### Geometry
- A shallow 3D slab behind the screen: x spans the screen aspect, y the screen height, depth z
  about 0.6 of the height. Fixed perspective camera (~35-45 deg vertical FOV) looking in, with a
  very slow drift/parallax (`camera_drift`, small: the OLED rule "nothing sits still").
- Blobs = 3D Wyvill/Blinn compact kernels `(1 - d^2/s^2)^3` summed into a scalar field, surface at
  `threshold` (use the 2D look's `threshold` and `support_scale` meaning so the inis transfer).
  Optional anisotropic stretch along the rise axis (`rise_stretch`) exactly as the 2D shader does.
- **Negative "hole" blobs** carve the field (negative weight, scaled by the local field like the
  2D droplet note explains) so big oil masses get round black cavities/tunnels where the dark
  backdrop shows through. The Few giants layout's BIG black holes are a user pick: keep them big.
- Droplets/bubbles: the 2D `bubble_frac` small blobs become small spheres; `droplets` (hundreds
  of tiny ones in 2D) may be capped/approximated in the prototype; say what you did.

### Raymarch (performance matters: the user runs 144 fps; budget <= ~4 ms at 2560x1440 on an
RX 7900 GRE, half-res + upsample allowed)
- Upload blobs (<= 64 for the prototype; the layout's 50 must fit) as a StructuredBuffer each frame.
- Per ray: intersect the support spheres of all blobs to get the [tmin,tmax] interval (and skip
  the ray entirely when it hits none), then march only inside it. A Wyvill field is not an SDF:
  use a conservative step (field-gradient bound or a fixed small step inside the interval) plus a
  few bisection refinements at the crossing. Cap the step count (`steps`, default ~64).
- Normal = normalized analytic field gradient (accumulate it in the same loop, like 2D `grad`).
- Optional per-tile blob culling is a later optimisation; note the measured ms in the report.

### Shading: two modes, both from the SAME scheme colours
- `shade=gloss` (default for the prototype): wax/oil look: wrap diffuse from 1-2 soft lights,
  a SMALL hot specular (Blinn-Phong or GGX), fresnel rim that carries the 2D look's rim colour,
  cheap thickness/subsurface glow (field value along the view ray or a short inner march) so the
  big masses feel lit from inside like lamp wax, soft field-based AO where masses neck together.
- `shade=flat`: the 2D look's identity in 3D: flat saturated fills (w^4 soft-max of blob colours,
  same as 2D), banded/posterized light, thin rim at the silhouette. This keeps the acid look.
- Depth: mild depth fog toward the backdrop colour and an optional cheap depth-of-field
  (blur far blobs a little; can be a later step, say if skipped).

### Colours and modes (the user's explicit ask: carry them ALL over)
Map every colour key of `[liquid_acid]` that the live oil stages use onto the 3D look:
- Blob colours: the palette/scheme keys (curated palettes, `palette_*`/`sweep_pair_*`, two-hue,
  80-20 complement, monotone, ink-complement lock, hue rotation and `scheme_hue_period` / tile9
  palette stepping). Each blob carries a palette index like 2D does.
- Ink/backdrop: whatever the stage's ink is (mono ink, the duotone ink ramp, black). Render it as
  the far wall of the slab with a slow procedural marbling (domain-warped noise, slowly flowing
  upward) in the scheme's ink colours, black where the scheme's ink is black. This stands in for
  the 2D fluid sim, which the prototype does not run.
- THREE ROLES rule (user): every composition has ONE element pitch black, ONE main colour, ONE
  accent (e.g. black holes/backdrop, the oil masses, the small droplets/rims). Never recolour
  everything into one hue.
- The exe must load the REAL preset files: e.g.
  `--ini reference/configs/monotone-post-0924.ini --ini "reference/presets/Layout - Few giants.ini" --ini "reference/presets/Scheme - Magenta Cyan.ini"`
  reads `[liquid_acid]` (and whatever colour sections those files use) and renders that scheme in
  3D. Keys that have no 3D meaning are ignored (log them once at --console verbosity).
  `[oil3d]` holds the 3D-only keys.
- Hue changes glide, never snap (the project rule: never zero a hue angle abruptly).

### Motion (lava lamp)
- Port the 2D rise mode: constant upward drift (`rise_speed`), lazy wobble, buoyancy by radius,
  repulsion between same-sign blobs, damping (fps-normalised), re-entry from BELOW the bottom of
  the slab (generated off screen, only ever moves up on screen: OLED rule), blob turnover
  (`blob_turnover_s`), radius breathing. Add slow z motion so blobs cross in front of each other.
- The 2D blobs are advected by the 2D fluid's velocity; here use an analytic 3D curl-noise
  (divergence-free) flow scaled by `flow_gain`/`curl_drift` instead.
- Merging and necking should read as surface tension (the kernel sum does this for free).

## Verification you CAN do in the cloud (do it if the tools install; report what ran)
- C++: `apt-get install -y mingw-w64` then
  `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -DUNICODE -D_UNICODE -DNOMINMAX src_oil3d/*.cpp`
  (MSVC-only pragmas may warn; fix real errors).
- HLSL: if the DirectXShaderCompiler Linux release can be downloaded, compile each shader string
  with `dxc -T ps_6_0 -HV 2018` as a syntax check (the runtime uses fxc SM5.1, so stay inside
  SM5.1 features even though dxc would accept more). Extract the strings with a tiny script.
- A CPU reference of the blob sim is optional; a unit-style sanity print of blob positions over
  60 s in the log (`--console`) is enough.

## Hand-back
1. Commit on branch `oil3d` in small logical commits; push `origin oil3d`. Never touch `main`,
   never force-push.
2. Update the "Status" line at the top of this brief and append a short "Cloud report" section:
   what is implemented, what is stubbed/skipped, compile checks that ran and their result, the
   biggest risks for the first local build, and the exact shot commands to run, e.g.
   `& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/proto-015.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/oil3d-default.ini')`
   plus one command per colour mode (monotone, a Scheme, acid-rise-12, two-hue).
3. Add one WORKLOG.md entry (dated 2026-10-14, a few lines).
4. End with your honest opinion: what will look wrong first, and the one thing you would change.

---

## Cloud report (cloud executor, 2026-10-14/15, branch `oil3d`)

### What landed
`src_oil3d/` (main.cpp, oil3d.cpp/.h, shaders.h, config.cpp/.h), the `Oil3DWallpaper` target in
CMakeLists.txt (exactly like Ink3DWallpaper), `tools/oil3d-shot.ps1`,
`reference/configs/oil3d-default.ini`, and `reference/shots/oil3d-cloud/` (reference frames from
the cloud smoke test, see below). Nothing under src/, src_oil/, src_acid/, src_ink3d/ changed.

* **Shell** = src_ink3d's, headless only: no window, no tray, no swap chain; without `--shot` it
  logs one line and exits. Deviation: `--console` NEVER `AllocConsole`s (a new console window
  pops up and takes focus); it only attaches to a parent console and raises verbosity (lists
  the ini keys with no 3D meaning). Every flag in the brief exists; also `--debug-view N`.
  Fixed 60 Hz sim step on simulated time; all randomness from `--seed`. Verified: the same
  flags give byte-identical PNGs.
* **Ini**: `--ini` is repeatable, applied in order; each file's `[meta] base` chain first, BUT a
  base an earlier `--ini` already applied is skipped -- so `acid-rise-12 + Layout +
  acid-rise-12-tone-half` keeps the layout, exactly like the 2D cycle (base, oil_layout,
  stage). `oil3d-default.ini` alone == `monotone-post-0924 + Layout - Few giants` (verified
  byte-identical). 106-107 [liquid_acid] keys of the live stages have no 3D meaning (swarm_*,
  cellulose_*, lid/film/post stack, rim_*/meniscus_*, droplet ring/crust/lens..., dark_sat,
  weather, boundary_reflect_*): ignored, listed with `--console`.
* **Sim** (CPU): the 2D rise mode ported key for key (seed kinds incl. web chains, clustered
  bubbles, holes inside a parent; rise + wobble + buoyancy by radius + same-sign repulsion +
  viscosity neck + fps-normalised damping; wrap/re-entry with the conserve_mass margins;
  re-entry from BELOW the bottom; floor clamp; breathing; blob_turnover_s + grow-in;
  accent_mode) plus a z axis. The 2D fluid's velocity is replaced by an analytic
  divergence-free flow (a screen-plane stream function + an x-z stream function that vanishes
  at the slab's front/back, so blobs cross in front of each other and never leave).
  Positions are "screen-projected" (sim X,Y = the 2D uv), mapped to world through the nominal
  frustum, so every 2D off-screen test still holds; a far blob climbs the screen slower
  (perspective) and only `parallax3d` (0.5) of the 2D size parallax is kept on top.
  Deviation: with rise_respawn the seed Y covers the WHOLE loop (incl. the parked rows under
  the bottom): seeding on screen only (2D) made the seeded batch leave together and the 3D
  frame sat nearly empty at t ~ 30-45 s.
* **Colour** (CPU, ported verbatim from src/fluid.cpp): EffectivePalette (sweep incl. the
  12-float tile9 sets, hue_rotate_period, hue_anchor_weight warp), palette_start_hue incl. the
  seeded random start, oil_saturation, accent_mode 80-20, dye_* (masses + droplets, follow),
  the split tone (gate clock + OKLab complement solve), film_hue2 wobble. Per blob: its palette
  slot, w^4 soft-max at merges (two-hue works per blob). On the GPU: film_hue2/hue3 patches with
  the same bands, seam narrowing, hue3_share and equal-load (base + patches) maths as the 2D
  shader, on a rising 3D fbm field; centre 0.45 / `mix_contrast` 1.2 calibrated by Monte Carlo
  of the exact shader noise to ~19% second hue / ~13% third hue of the wax at cover 0.04 (the
  2D targets). film_level, rise_bottom_light, post_chroma/post_lift carried. THREE ROLES:
  black = the far wall + cavities (+ split tone; or the masses' dye when dye_lum > 0), main =
  the wax (palette), accent = hue2/hue3 patches + rims and bead rims in the SEAM colour (half
  the hue2 rotation, the 2D boundary-reflect idea). Hue only ever moves on the clocks: glides.
* **Raymarch** (one fullscreen PS, fxc ps_5_1): per ray, every blob's support ellipsoid is
  intersected (stretched in y by rise_stretch, holes stretched in z by `hole_depth_stretch` so
  big holes read as tunnels) into two 32-bit masks walked with firstbitlow; rays that hit no
  oil support skip the march. March = sphere tracing on the COMPACT support: step = min(
  (threshold - f) / Lipschitz of the supports containing p, distance to the next support not
  yet entered), floored at interval/steps/2, capped at 2*steps; then `refine` bisections.
  f = P - N*max(P,1) (holes scaled by the local oil field, as the brief asks). Normal =
  analytic gradient. Cost views measured on the home stage: mean 20 march iterations (p99 64,
  max 92 of 128), mean 3.7 blobs per ray list (max 10).
* **Shading**: `shade=gloss` -- wrap diffuse from two lights (the lamp under the column + the
  2D rig's top-left lamp), small hot Blinn (power 240; the ONLY thing the HDR lift touches),
  fresnel rim in the accent colour, thickness glow from a 5-tap inner march, field AO, a
  hole-carved wall goes to the black role (keyed on how much of the surface SHAPE the holes
  make), depth fog toward the wall, silhouette AA band. `shade=flat` -- flat fill, banded
  light, dark rim. Droplets: min(droplets/12, droplet_cap=40) analytic beads (black body =
  the 2D black droplet holes, accent rim, pin-point glint), racers carried.
* **HDR**: `--hdr on` scales by sdr_white/80 and lifts only the specular mask toward
  peak_nits; HDR off = plain SDR. Writes `x.png` + `x-hdr.png` like ink3d.

### Stubbed / skipped (say-so list)
* No [post] stack (grain, halation, bloom, vignette, lid, defocus/DOF). DOF skipped.
* Droplets are 40 beads, not hundreds of field particles; no rings/crust/lens/coalescence.
* The ink wall is procedural; on every live stage it is the black paper (auto `ink_marble` 0
  because the 2D water there never gets ink). Bands-mode ramp works but is untested by eye;
  ink_complement_lock is ignored.
* Not ported: mouse modes, oil_drag/oil_dye_block (no fluid), rise_parallax_dim, weather,
  dark_sat, lamp grey / grey heart, highlight tone, cellulose, penumbra, cast shadows.
* No per-tile culling (per-ray only).

### Checks that ran in the cloud
1. `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -Wall -Wextra -DUNICODE -D_UNICODE
   -DNOMINMAX src_oil3d/*.cpp`: clean (also -O2 -Wshadow objects: clean).
2. dxc 1.8.2407 (`-HV 2018`, vs_6_0/ps_6_0) on both shader sources: clean.
3. **More than asked:** the mingw build LINKS into a PE exe, and it ran under wine 9 + vkd3d on
   lavapipe (Mesa's CPU Vulkan) with Microsoft's own `d3dcompiler_47.dll` as a native
   override (taken from the cef.redist.x64 NuGet package; wine's builtin compiler cannot do
   SM5.1). So the shaders compiled with the REAL fxc at vs_5_1/ps_5_1, and the whole D3D12
   path (root signature, PSOs, descriptor table, upload ring, timestamps, readback, WIC PNG)
   executed and rendered every mode. That smoke test found and fixed four real bugs before
   hand-back: 12-float sweep_oil_N sets read as 3 floats (black wax on every tile9 palette);
   the cavity term painting whole outer walls black; the light budget clipping 16% of pixels;
   the empty frame at t ~ 30-45 s. It also forced one change you should know about: vkd3d on
   lavapipe rejected root CBV/SRV descriptors (8-descriptor-set limit), so the root signature
   is ONE descriptor table (b0 CBV + t0..t2 SRVs) -- standard D3D12, fine on AMD.
   lavapipe timings are CPU timings (105 ms at 640x360) and say nothing about the 7900 GRE.
4. Sim sanity over 60 s (`[sim]` lines every 10 s in the log, plus a 6-frame series): 50 blobs
   live throughout, z stays inside the slab, masses leave the top and re-enter from below, and
   the turnover is visibly active (the hole count changes as blobs are retired and regrown).

Independent review pass (a second agent, port vs src/): palette/config/D3D12 clean; six fixes
landed (blob cap 56 so blob_turnover_s is not blocked; seam clamp 0.1; rim = the 2D seam delta;
x-z flow truly divergence-free; lamp ramp on screen y; pow NaN guard).

Reference frames (lavapipe, seed 1234, t=15 s, 960x540, AFTER the review fixes):
`reference/shots/oil3d-cloud/contact.png` (note: at this size `shade=flat` reads close to gloss --
its bands are faint; raise flat_bands contrast if the user wants the flat identity) --
the local session can diff its first real-GPU shots against them (expect float-level
differences, not a different picture).

### Biggest risks for the first local build
1. **Timing** -- my estimate from the measured iteration/list counts is ~2-5 ms for the
   raymarch at 2560x1440 full res on the 7900 GRE (budget ~4). If it is over: `--half-res 1`
   (~1/4 of that; silhouettes go softer), or `--steps 48`. Per-tile culling is the real fix.
2. MSVC-only warnings (mingw/-Wall is clean; nothing MSVC-specific is used beyond the
   ink3d shell's own calls).
3. The look itself: see the opinion below.

### Shot commands (PowerShell, repo root; `build2.cmd Oil3DWallpaper` first)
```powershell
# home stage (monotone + Few giants), 720p, with timings
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/proto-015.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/oil3d-default.ini','--timings')
# the budget question: full 1440p, then half-res
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/home-1440.png -ExtraArgs @('--shot-delay','15','--ini','reference/configs/oil3d-default.ini','--timings')
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/home-1440-half.png -ExtraArgs @('--shot-delay','15','--ini','reference/configs/oil3d-default.ini','--half-res','1','--timings')
# a Scheme (base + layout + scheme, the cycle's order)
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/scheme-magenta-cyan.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/monotone-post-0924.ini','--ini','reference/presets/Layout - Few giants.ini','--ini','reference/presets/Scheme - Magenta Cyan.ini')
# three hues
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/scheme-bright-triad.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/monotone-post-0924.ini','--ini','reference/presets/Layout - Few giants.ini','--ini','reference/presets/Scheme - Bright Triad.ini')
# acid-rise-12 (the cycle's tone-half stage)
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/acid-rise-12.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/acid-rise-12.ini','--ini','reference/presets/Layout - Few giants.ini','--ini','reference/configs/acid-rise-12-tone-half.ini')
# two-hue
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/two-hue.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/acid-rise-2hue.ini','--ini','reference/presets/Layout - Few giants.ini')
# flat (the 2D identity in 3D)
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/flat.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--shade','flat','--ini','reference/configs/monotone-post-0924.ini','--ini','reference/presets/Layout - Few giants.ini','--ini','reference/presets/Scheme - Magenta Cyan.ini')
# HDR on (verify both)
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/home-hdr.png -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--hdr','on','--sdr-white','240','--ini','reference/configs/oil3d-default.ini')
# motion: six frames 10 s apart
& tools/oil3d-shot.ps1 -Out build2/shots/oil3d/series.png -ExtraArgs @('--shot-delay','10','--shot-series','6:10','--shot-size','960x540','--ini','reference/configs/oil3d-default.ini')
```
Debug views if something looks off: `--debug-view 1..7` (normal, hole share, AO, thickness,
unlit fill, march iterations, ray-list size).

### Honest opinion
**What will look wrong first:** it reads as a clean CG lava-lamp render, not as the live oil.
The 2D look is mostly its CAMERA -- grain, halation, defocus, vignette, the lid, the droplet
crust and the thin rims -- and none of that is here; the gloss wax is smooth and plastic-y,
with long specular streaks on the big flat faces. Second: the hue2 patches are a 3D noise
that rises rigidly with the column, so on a big mass they read as paint ON the surface, not a
dye moving INSIDE the wax. Third: composition -- the masses tend to pool in the lower half
(the analytic flow slows the climb in places and the viscosity neck clumps them), which is
lamp-like but bottom-heavy, and between masses the frame is plain black with 40 beads where
the 2D has 500 droplets.
**The one thing I would change:** put a camera on it -- a small post pass over the 3D output
with the 2D's film grain, halation and a depth-driven defocus (the raymarch already knows the
depth of every pixel). That is cheap (one fullscreen pass) and it is what makes the 2D
frames look photographed; everything else (per-blob dye identity for the patches, more
beads, tile culling) is tuning after the user has said whether 3D wax is the right direction.
