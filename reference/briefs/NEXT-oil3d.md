# NEXT — Oil3D: the oil wallpaper as a TRUE 3D lava lamp (prototype)

Status: OPEN, 2026-10-14. Written by the local orchestrator for a CLOUD executor. The cloud
writes the code (it cannot build or run D3D12); the local session builds it into `build2/`,
renders headless shots, sends them to the user and feeds fixes back.

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
