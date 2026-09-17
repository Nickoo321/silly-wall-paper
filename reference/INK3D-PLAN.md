# INK3D-PLAN — 3D "ink in water" wallpaper (branch 4)

Planner document, 2026-09-16. Target: new standalone exe `Ink3DWallpaper` in `src_ink3d/`,
nothing in `src/` changes. References: `reference/shots/photos/ink-in-water-ref-1-black-drop-plume.jpg`
(black ink, backlit white: a drop enters from the top with momentum, a vortex ring / mushroom cap
forms, the cap rolls into Rayleigh-Taylor lobes, thin see-through veils and filaments trail behind)
and `ink-in-water-ref-2-veils-and-splash.jpg` (blue-black ink as horizontal veils and sheets, Kelvin-
Helmholtz curls on the sheet edges, splash droplet spray). Both are backlit — thin ink is grey/blue-grey
and see-through, thick cores opaque. That translucency is a rendering property (Beer-Lambert); the
plume/veil structure is a solver property (low numerical diffusion + real 3D incompressibility).

Machine facts used below: Windows 11 24H2, RX 7900 GRE (80 CU, ~46 TFLOPS fp32, 576 GB/s, 16 GB),
primary 2560x1440 QD-OLED (X27U, ~1000 nits window / ~418 full-frame, ABL), Windows HDR on or off,
SDR white 240 nits, user runs 144 fps and games (Deadlock/Dota) — the wallpaper pauses on fullscreen.

## 1. Feasibility with numbers

Voxel counts (velocity grid; all dims multiples of 4 so `[numthreads(8,8,4)]` bounds-checks cleanly):

| grid (x*y*z)     | voxels | vs 128^3 | note                                    |
|------------------|--------|----------|-----------------------------------------|
| 96x56x48         | 0.26 M | 0.12x    | gaming / throttle tier                  |
| 128x72x64        | 0.59 M | 0.28x    | "fast" tier, 16:9                       |
| 160x92x80        | 1.18 M | 0.56x    | middle tier                             |
| 192x108x96       | 1.99 M | 0.95x    | RECOMMENDED velocity grid, exact 16:9   |
| 128^3            | 2.10 M | 1.00x    | cubic reference                         |
| 256x144x128      | 4.72 M | 2.25x    | RECOMMENDED density grid (4/3 x vel)    |
| 160^3            | 4.10 M | 1.95x    | too many pressure cells for the budget  |
| 192^3            | 7.08 M | 3.4x     | out of budget at 144 fps                |

Non-cubic is right for a wallpaper: the box front face IS the 16:9 screen, depth = half the width
(enough parallax for veils to pass in front of each other; more depth costs raymarch steps for nothing).

Memory per field (DXGI has no RGB16F; velocity is RGBA16F, w channel spare):

| field                                   | format      | at 192x108x96        | at 256x144x128    |
|-----------------------------------------|-------------|----------------------|-------------------|
| velocity A/B + 1 MacCormack tmp         | RGBA16F 8 B | 3 x 15.9 MB = 48 MB  | -                 |
| curl (xyz = omega, w = |omega|)         | RGBA16F     | 15.9 MB              | -                 |
| divergence                              | R16F 2 B    | 4.0 MB               | -                 |
| pressure A/B                            | R16F        | 2 x 4.0 MB           | -                 |
| multigrid levels 96x54x48 + 48x28x24 (p A/B + rhs) | R16F | ~2.0 MB       | -                 |
| density A/B + MacCormack tmp            | R16F        | -                    | 3 x 9.4 MB = 28 MB|
| occupancy (block max, 1/8 res)          | R16F        | 32x18x16 = 18 KB     | -                 |
| light transmittance volume              | R16F        | 128x72x64 = 1.2 MB   | -                 |
| raymarch target                         | RGBA16F     | 29.5 MB at 1440p (7.4 MB half res)       |
| swap chain 3 x FP16 1440p               | RGBA16F     | 88 MB (same as the 2D app)               |
| TOTAL                                   |             | ~230 MB VRAM (~140 MB sim + targets)     |

Estimated compute per pass on the 7900 GRE (bandwidth-bound estimate at ~350 GB/s effective for
3D-swizzled traffic, x1.5 fudge for latency; M0/M1 must verify these with the timestamp query
pattern from `src_oil/oil.cpp:415-455, 838-891`):

| pass                                          | grid       | traffic     | est. ms   |
|-----------------------------------------------|------------|-------------|-----------|
| curl                                          | 1.99 M vel | 40 MB       | 0.10      |
| forces (vorticity conf. + buoyancy + noise)   | vel        | 56 MB       | 0.13      |
| advect velocity, MacCormack (3 dispatches)    | vel        | 3 x 48 MB   | 0.35      |
| divergence                                    | vel        | 20 MB       | 0.06      |
| pressure Jacobi, ONE iteration                | vel        | 16 MB       | 0.06-0.08 |
| pressure multigrid, one V(2,2) cycle          | vel+coarse | ~5 fine-eq  | 0.35      |
| gradient subtract                             | vel        | 36 MB       | 0.08      |
| advect density, MacCormack (3 dispatches)     | 4.72 M     | 3 x 60 MB   | 0.45      |
| density sharpen (optional 3x3x3)              | density    | 30 MB       | 0.10      |
| occupancy max-reduce + light sweep            | small      | 12 MB       | 0.06      |
| raymarch 1440p, 96 steps, empty-space skip    | 3.69 M px  | see sec. 3  | 0.9-1.6   |
| display / upsample / HDR map                  | 1440p      | 60 MB       | 0.08      |

Frame budget at the recommended tier (vel 192x108x96, density 256x144x128, MG 2 cycles, full-res
raymarch): sim ~1.6 ms + light 0.06 + raymarch ~1.3 + display 0.08 = **~3.0 ms** (target < 4 ms
mean; leaves 40% of the 6.9 ms/144-fps frame to DWM, a browser, a windowed game). Fallbacks if M1
measures worse: half-res raymarch (-0.8 ms) or vel 128x72x64 + density 192x108x96 (sim ~0.6 ms).
Total VRAM traffic ~600 MB/frame = 86 GB/s at 144 fps, 15% of the bus.

Throttle ("gaming") mode: sim step at 48 Hz (one step every 3rd presented frame, dt = 1/48),
raymarch at half res with 64 steps, "fast" grid tier => ~0.7 ms GPU on sim frames, ~0.3 ms
averaged. Fullscreen games pause the exe outright (copied from the 2D shell, `FullscreenAppActive()`
main.cpp:463-491; the 20 s suspend frees the VRAM, main.cpp:1615-1660).

## 2. Solver design

Incompressible Navier-Stokes on a collocated grid (like the 2D app: velocity at cell centers, central
differences), operator split per step:

1. **Inject** — sphere splat of density + velocity (see drops below). Dispatch only the sphere's
   bounding box (radius + 2 voxels), never the whole grid.
2. **Curl** — omega = nabla x v (6 taps), store xyz + |omega| in w (RGBA16F).
3. **Forces** (one pass): vorticity confinement in 3D: eta = nabla|omega| (6 taps on the w channel),
   N = eta/(|eta|+1e-5), f = eps * (N x omega) * h. Recommended eps 2-6 in grid units (the 2D app's
   48 lives in its own scaling — retune). Buoyancy on density: ink is heavier than water, so
   f_y(down) = +g_ink * rho (trilinear sample of the density grid), minus an optional thermal term
   (-g_up * rho, default 0). Plus low-amplitude curl-noise ambient drift (0.02 voxel/s) so the water
   is never dead still. v += f * dt.
4. **Advect velocity** — MacCormack (Selle 2008): forward semi-Lagrangian, backward, correction
   0.5*(phi - phi_back), then clamp to the min/max of the 8 trilinear source samples (the clamp is
   what stops the ringing; never skip it). Key `advect=sl` falls back to plain semi-Lagrangian.
   Dissipation raised to the power dt*144 (the WORKLOG lesson: fps-normalize per-step decays).
5. **Divergence** — the reflect-at-wall trick of `CSDivergence` (shaders.h:114-128) extended to z.
6. **Pressure** — Recommendation: **geometric multigrid, V(2,2), 3 levels (192x108x96 -> 96x54x48
   -> 48x28x24, top level solved by 16 red-black sweeps), 2 V-cycles per frame**, red-black
   Gauss-Seidel smoother (one dispatch per colour, in place, no ping-pong), full-weighting restrict,
   trilinear prolong. Cost ~0.7 ms, residual drops ~100x per cycle. Jacobi needs O(N) ~ 200 sweeps
   to move a 192-cell-wide pressure mode; 20 sweeps leaves the bulk compressible, so drops "breathe"
   (swell after injection, then collapse). Fallback (M1 first, MG in M2): Jacobi 24 iterations
   (~1.7 ms), pressure warm-started from last frame x 0.8 (the 2D `pressure_diffusion` trick,
   fluid.cpp:608-611). Pressure BC: Neumann (dp/dn = 0) on all walls.
7. **Gradient subtract** — v -= nabla p (central differences, clamped at walls).
8. **Advect density** at 256x144x128 — MacCormack with min/max clamp, velocity sampled trilinear from
   the vel grid, per-step dissipation 0.998^(dt*144) (ink lasts ~60 s), NO explicit diffusion by
   default. Optional **sharpen**: rho += k*(rho - box3x3x3(rho)), k 0.05-0.15, clamped to
   [0, rho_max] — counteracts trilinear smearing and keeps veils as sheets instead of fog. Optional
   dual-rate decay (faint ink dies faster) copied from `CSAdvectDye` (shaders.h:166-200) so the
   water clears between drops instead of filling with grey.
9. **Occupancy** — max-reduce density into 32x18x16 (one cell = 8^3 density voxels) and the
   **light sweep** (section 3).

Boundaries: closed box, free-slip on all six faces (normal component reflected in divergence,
tangential kept), Neumann pressure. Top: same as the others — drops are injected just under the top
face with downward momentum, which is what the photo shows (the nozzle is above the frame). Density
sink: rho *= 0.97^(dt*144) in the bottom 6% of the box so ink that reaches the floor drains instead
of pooling. `boundary=periodic_xz` only as an ini option.

Drop injection: sphere of radius r (default 4 vel voxels = 5.3 density voxels); density added with a
falloff (1-(d/r)^2)^2, capped at 1.0; velocity SET (not added) inside the sphere to
(vx, vy, vz) = (+-10% jitter, 60-100 voxel/s down, +-10% jitter), blended by the same falloff. Add 3-5%
radial noise on the density edge (seeded hash of the voxel index) — a perfectly symmetric sphere
makes a perfectly symmetric ring that never goes unstable; the asymmetry seeds the RT lobes and KH
curls. Momentum matters more than buoyancy for the mushroom: the vortex ring forms in the first
~0.5 s from the entry velocity; buoyancy then feeds the RT billows on the cap for the next 2-3 s.
CFL: 100 voxel/s at dt = 1/144 is 0.7 voxel/step — no substepping; clamp injected speed to 3
voxels/step.

Why this gives plumes and veils rather than blobby smoke: (a) MacCormack + clamp on BOTH fields —
plain semi-Lagrangian smears a sheet into fog in ~40 steps; (b) density at 4/3 the velocity
resolution, so a 1-voxel velocity shear layer carries a sub-voxel ink sheet (ref 2's veils are
thinner than the eddies folding them); (c) no explicit diffusion, mild sharpening; (d) confinement
moderate — eps > 8 makes cauliflower smoke; (e) real 3D pressure convergence (MG) so a ring is a
ring, not a swelling blob; (f) thin ink rendered as translucent grey, so sheet structure is visible
at all — in emissive smoke rendering veils vanish.

## 3. Rendering design

Camera: fixed, looking down -z at the front face, mild perspective (fov ~25 deg) so veils near the
front loom slightly; the box front face maps exactly to the screen. Ray/box intersection is trivial.

Raymarch (fullscreen-triangle PS like `kDisplaySrc`, reading `Texture3D<float>` density through a
linear-clamp sampler): 96 steps default (step = depth/96 = 1.33 density voxels), per-pixel start
jitter from interleaved-gradient noise of the PIXEL coordinate (not the frame index — shot mode must
be deterministic; live mode may add a 4-frame cycle via `jitter_anim=1`), front-to-back Beer-Lambert:

    T *= exp(-sigma * rho * ds)     sigma = ink_density * absorb_rgb   (per channel)

`absorb_rgb` = (1.00, 0.88, 0.62) for blue-black ink (thin layers go blue-grey, thick go black),
(1,1,1) for neutral black. Early-out at max(T) < 0.004. Empty-space skipping: sample the 32x18x16
occupancy volume; if the block max < 0.002, jump to the block exit (8 density voxels). Ink covers
20-40% of the volume in steady state, so ~55% of steps are skipped — section 1 assumes that.
Adaptive step (double ds where rho < 0.01) is an optional second win. Half-res raymarch + bilateral
(density-edge-aware) upsample is the throttle path, not the default.

Cheap single scattering: precompute a light transmittance volume L(p) at 128x72x64 once per frame
by a sweep along the light direction (light from behind, +z toward the camera, tilted 20 deg up:
one thread per (x,y) column looping z back-to-front — 9216 threads x 64 iterations, ~0.03 ms). In
the march: `S += T * rho * ds * scatter * albedo_rgb * L(p)`, albedo (0.05, 0.08, 0.16) for
blue-black. Optional gradient lighting (`gradient_light=1`): n = normalize(6-tap density gradient)
only where rho > 0.05, adds a Fresnel-ish sheen `pow(1-|n.z|, 3)` — the "wet edge" on ref 1's cap.
Off by default (6 extra taps per lit sample).

Modes (`render_mode=`):
- `paper` (the photos): colour = T * paper_rgb * paper_level + S. paper_level default 0.65 of SDR
  white (= 156 nits at SDR white 240), NOT 1.0: full-frame white on the X27U triggers ABL (418 nits
  full frame) and it is the desktop icons' background. Tray shows a warning line in this mode.
- `negative` (default on the OLED): black background, colour = (1 - T_lum) * ink_rgb * ink_level +
  S*k. Thin veils read as faint pale blue, cores bright — the photo inverted. ink_rgb (0.72, 0.80,
  0.95), ink_level 0.6. HDR: the existing knee/peakGain pattern (fluid.cpp:823-858, shaders.h:759-767)
  applied to (1 - T_lum) with knee 0.7 lifts only dense cores toward peak_nits — small hot areas.
- `backlit` (dark room, light behind the ink): colour = S only; thin sheets glow where light passes
  through, thick cores go dark — smoke in a shaft of light. Most 3D-looking, most OLED-friendly
  (mostly black); needs scatter ~4x paper's.
Global hue tint via the CSS hue-rotate matrix from `kDisplaySrc`. No bloom (hard constraint).

HDR path: identical contract to the 2D app — swap chain R16G16B16A16_FLOAT, colour space
DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 (fluid.cpp:153-169, `ReassertColorSpace` 1094-1101), output
linear scRGB (1.0 = 80 nits) x sdrScale = sdrWhiteNits/80 when Windows HDR is on (main.cpp:1870-
1875), peak gain = peak_nits / sdrWhiteNits above the knee. `gamut` key kept for parity, default 0:
the ink is near-neutral, P3/2020 buys nothing. Verify HDR on AND off on the panel. `--shot` writes
the same `.png` + `-hdr.png` pair (main.cpp:1273-1320).

OLED dark-friendliness: default negative mode, ink_level 0.6, paper_level capped at 0.8, hot pixels
(above SDR white) only in cores (knee 0.7), a 30-min idle "fade to 40%" option, pause-on-fullscreen.
Mean frame luminance in negative mode ~5-10% APL.

## 4. Code plan (for the Opus executor)

Files in `src_ink3d/`, new CMake target, built in `build3/` so it never fights build/ or build2/:

- `main.cpp` — shell. COPY, don't share, from `src_oil/main.cpp` (already the trimmed standalone
  form of the 2D shell: WorkerW host search incl. the 24H2 layout, wallpaper window, Explorer-restart
  reattach, tray, single-instance mutex, SPI_SETDESKWALLPAPER on exit, headless `--shot`). Pull in
  from `src/main.cpp` what oil lacks: ini load/save with the partial-overlay convention (121-190
  pattern), `QueryHDR`/`GetSdrWhiteNits` + the 1 Hz HDR re-check (494-560, 1750-1765),
  `FullscreenAppActive`/`MaximizedAppActive` + the 20 s suspend (463-491, 1615-1660), fps-cap loop +
  cursor sampling (1826-1867), HDR peak submenu (627-640), the `ShotOpts` struct + WIC PNG pair
  writer (1178-1320). Mark each block `// copied from src/main.cpp @873a6dc`. Copy over share
  because AGENTS.md's standalone targets "share no sources", `src/` is frozen for the parity work,
  and the 2D shell is entangled with moods/scenes/settings globals. Names: exe `Ink3DWallpaper`,
  mutex `Ink3DWallpaper_SingleInstance`, ini `%APPDATA%\Ink3DWallpaper\settings.ini`, logs
  `%TEMP%\Ink3DWallpaper.log` and `Ink3DWallpaper-shot.log`. No MessageBox on non-fatal paths (the
  user games; log instead).
- `ink3d.h / ink3d.cpp` — `Ink3DRenderer`: device + FP16 scRGB swap chain + offscreen target +
  frame ring of 3 (copy `CreateDevice`, `CreateOffscreenTarget`, `BeginFrame`, `EndFrameAndPresent`,
  `Reattach`, `CaptureOffscreen`, `WaitForGpuIdle`, `Transition`, `UavBarrier` from fluid.cpp
  123-200, 349-382, 502-566, 770-822, 1278-1286, 1430-1466), the timestamp GPU timer from
  `src_oil/oil.cpp` (415-455, 838-891; `GpuMsMean/Min`), the 3D resources, the pass sequence, the
  drop scheduler and the mouse injector.
- `shaders.h` — `kSimSrc` (all compute kernels, one root signature), `kLightSrc`, `kRaymarchSrc`
  (PS), `kDisplaySrc` (PS: upsample if half-res + mode composite + hue + HDR map). Compiled at startup
  with `D3DCompile` (fluid.cpp:68), cs_5_1 / ps_5_1.
- `config.h / config.cpp` — `Ink3DConfig` with defaults, `LoadConfig(ini)` / `SaveConfig`, tier
  presets. Every constant a named field, like `LiquidAcidConfig` in fluid.h.
- `CMakeLists.txt` — append `add_executable(Ink3DWallpaper WIN32 src_ink3d/main.cpp
  src_ink3d/ink3d.cpp src_ink3d/config.cpp)` with the same defines, `/utf-8`, libs incl. windowscodecs.
- `tools/ink3d-shot.ps1` — wraps the GPU-lock protocol (below) around one `--shot` run.

D3D12 resource layout: one shader-visible CBV/SRV/UAV heap, 96 slots; each `Tex3` gets SRV (slot*2)
and UAV (slot*2+1) exactly like `CreateTex` (fluid.cpp:384-423) but `D3D12_RESOURCE_DIMENSION_
TEXTURE3D`, `DepthOrArraySize = depth`, `D3D12_SRV_DIMENSION_TEXTURE3D`, UAV `Texture3D.WSize = -1`,
layout `D3D12_TEXTURE_LAYOUT_UNKNOWN` (driver swizzle — required for 3D cache locality). Pressure and
MG levels are Tex3 too. Compute root signature: 32-bit constants (b0, 48 dwords) + SRV tables t0-t3
+ UAV tables u0-u2 (RGBA16F / R16F / R16F in-place) + static linear-clamp sampler; the "bind all
UAV params, the entry point uses one" trick from `SimStep` (fluid.cpp:581-598) carries over. The
in-place red-black smoother needs typed UAV loads of R16_FLOAT — check
`D3D12_FEATURE_D3D12_OPTIONS.TypedUAVLoadAdditionalFormats` at init, fall back to Jacobi ping-pong
if absent (RDNA3 has it). Thread groups `[numthreads(8,8,4)]` = 256 threads.

Compute pass order per frame (one sim substep; zero when paused or between throttle ticks, in
which case only 10-12 run and the image is re-rendered from the last density):
 1. inject (scheduler drops + mouse) — bounding-box dispatch on vel AND density
 2. curl            3. forces (vorticity + buoyancy + noise)      4. advect vel (MacCormack x3)
 5. divergence      6. pressure (MG V-cycle x2 | Jacobi xN)        7. gradient subtract
 8. advect density (MacCormack x3) + dissipation                   9. sharpen (optional)
10. occupancy max-reduce + light sweep
11. raymarch PS -> RGBA16F target (full or half res)
12. display PS -> back buffer (or the offscreen FP16 target in shot mode); Present(1, 0)

Constants: `SimCB { int3 dims; float dt; float3 invDims; float dissipation; float vorticity; float
buoyancy; float noise; float sharpen; int3 dropMin; float dropRadius; float3 dropCenter; float
dropAmount; float3 dropVel; uint seed; int pass; }` (48 dwords). `RenderCB { float2 invScreen;
float2 halfResScale; float3 boxMin; float3 boxMax; float3 camPos; float fov; int steps; float
jitter; float3 sigma; float3 albedo; float scatter; int mode; float3 paperRgb; float paperLevel;
float3 inkRgb; float inkLevel; float3 lightDir; float sdrScale; float peakGain; float knee; float
hue; float gamut; }`. Read from the ini at startup + tray "Reload ini" (no disk watcher, same as the
2D app).

ini keys, section `[ink3d]` (defaults in parentheses): `tier` (balanced | quality | fast | gaming),
`grid_x/y/z` (192/108/96), `density_scale` (1.333), `advect` (maccormack | sl), `pressure_solver`
(mg | jacobi), `mg_cycles` (2), `pressure_iterations` (24, jacobi only), `vorticity` (4.0),
`buoyancy` (12.0), `ambient_noise` (0.02), `dissipation` (0.998), `dissipation_fast` (0.98),
`decay_threshold` (0.02), `sharpen` (0.08), `sink_bottom` (0.06), `drop_radius` (4), `drop_speed`
(80), `drop_speed_jitter` (0.25), `idle_drops` (1), `drop_interval_min/max` (8/20 s), `splash_chance`
(0.25), `mouse_mode` (front | ray | off), `mouse_depth` (0.15), `render_mode` (negative | paper |
backlit), `steps` (96), `half_res` (0), `jitter_anim` (0), `ink_density` (6.0), `absorb_rgb`
(1.00,0.88,0.62), `albedo_rgb` (0.05,0.08,0.16), `scatter` (1.0), `light_dir` (0,-0.34,0.94),
`gradient_light` (0), `paper_rgb` (1,1,1), `paper_level` (0.65), `ink_rgb` (0.72,0.80,0.95),
`ink_level` (0.6), `hue` (0), `throttle` (auto | on | off), `throttle_ms` (4.5), `throttle_fps` (48).
Shared sections with the 2D app's meaning: `[general] fps_limit=144 pause_on_fullscreen=1
pause_on_maximized=1`, `[hdr] peak_nits=700 knee=0.70 gamut=0`.

Headless `--shot` (deterministic, scripted): `--shot out.png --shot-size WxH (2560x1440)
--shot-delay S (6) --shot-series N:S --seed N (1234) --ini path --hdr on|off --sdr-white N
--panel-max N --shot-yield ms (2) --shot-drop X,Y,Z,T,R,VX,VY,VZ` (repeatable; box-normalized
position, seconds, voxels, voxel/s; any `--shot-drop` turns the idle scheduler off) and
`--shot-mouse X,Y,START,DUR` (front-plane injector on a slow circle like `--shot-pour`). Fixed
dt = 1/144, no vsync, fixed jitter, seeded hash noise, seeded idle RNG => two runs with the same args
produce byte-identical PNGs (M5 check; trilinear filtering is deterministic on one GPU/driver).
Sheets: `reference/configs/ab.py`, `blind.py`, `refsheet.py` unchanged (they consume PNGs). Shots
land in `build3/shots/`.

GPU lock (AGENTS.md rule): before every `--shot`, wait until
`C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\build2\shots\gpu.lock` does not
exist (poll 5 s, give up after 30 min and report), create it with agent name + time, render ONE
shot process, delete it in a `finally`. Builds need no lock. Never stop the user's FluidWallpaper.exe
or Wallpaper Engine; never launch the live (non-shot) exe while the user is gaming — it attaches to
the desktop and competes with the game for the GPU. Live runs only when the user asks.

## 5. Milestones (pass/fail each; effort = Opus agent time)

M0 — grid + raymarch of a static sphere (0.5 day). Device, swap chain, `--shot`, 3D textures, an
init kernel writing an analytic soft sphere + a thin tilted sheet into density, occupancy + light
sweep, raymarch in paper and negative modes, timestamp timer. PASS: `--shot` produces .png/-hdr.png;
sphere edge translucent grey, core black (paper) / pale (negative); the sheet visible edge-on as a
hairline; raymarch GPU time at 2560x1440 with 96 steps < 1.6 ms (logged); no NaN / black frame.

M1 — advection + pressure with a single drop (1 day). Passes 1-8 with plain semi-Lagrangian and
Jacobi 24, `--shot-drop 0.5,0.08,0.5,0.0,4,0,80,0`, series at t = 0.3/0.7/1.5/3 s. PASS: the drop
travels down, a vortex ring / mushroom cap is visible by t ~ 1 s, no blow-up over 60 s, mean |div|
after the solve < 10% of before (debug reduce behind `--stats`), sim GPU time < 2.5 ms.

M2 — plume look: vorticity, buoyancy, MacCormack, density 4/3 res, sharpen, MG (2-3 days; the GPU
lock serialises sheets, so batch parameter sweeps in one detached process like
`build/shots/batch.cmd`). Compare against ref 1 with `refsheet.py`. PASS: blind sheet of four
parameter sets at t = 1/2/4 s, the user picks one as "looks like ink"; objective gates: the cap
rolls into >= 2 secondary lobes by t = 3 s, filaments < 3 px wide at 1440p exist at t = 4 s, MG
residual < 1e-3 after 2 cycles, sim + render < 4 ms mean.

M3 — render modes + HDR (1 day). paper / negative / backlit, scattering, optional gradient sheen,
knee/peak gain, hue, `--hdr on|off` pairs. PASS: both HDR states verified on the panel (not from
screenshots — luminance-inaccurate per AGENTS.md); paper mode never exceeds paper_level; negative
mode hot area (> SDR white) < 3% of pixels at t = 2 s.

M4 — desktop attach, tray, ini, throttle, pause (1 day). Tray: Pause, Mode (3), Tier (4), HDR peak
submenu, Reload ini, Open ini, Exit. PASS: runs behind the icons 30 min at 144 fps with mean GPU
< 4 ms (log every 5 s); Explorer restart reattaches; ini round-trips; a fullscreen window pauses it
and after 20 s VRAM is released (Task Manager); throttle halves GPU time. Test only when the user is
not gaming (the attach steals no focus, but the test needs the desktop).

M5 — idle drops + mouse (0.5 day). Seeded scheduler, splash bursts (3-6 tiny drops in 0.2 s,
`splash_chance`), front-plane injector (LMB hold = continuous injection following the cursor,
movement alone = gentle stir), ray mode. PASS: two identical `--shot` runs are byte-identical (md5);
idle drops every 8-20 s; LMB on the desktop injects at the cursor within 1 frame; the 2D rule
"cursor movement reaches the wallpaper regardless of focus, clicks only with the desktop focused"
holds (main.cpp:1845-1866).

Total ~6-7 agent days; M2 is where the time goes.

## 6. Risks and fallbacks

- **Raymarch cost cliff.** 96 steps at full 1440p with poor cache behaviour could hit 3-4 ms alone.
  Mitigations in order: occupancy skipping (planned), half-res + bilateral upsample (-60%), 64 steps,
  re-render only every 2nd frame when nothing was injected. Measure in M0 before any solver work.
- **Pressure convergence at large grids.** Jacobi 24 on 192 cells wide = compressible bulk (drops
  swell). MG is more code (restrict/prolong/RB kernels, ~250 lines HLSL) with its own bug class
  (wrong BC on coarse levels => checkerboards). Ship M1 on Jacobi, gate M2 on the MG residual check,
  keep `pressure_solver=jacobi` as the escape hatch; warm-starting hides most of the shortfall.
- **MacCormack artefacts.** Without the min/max clamp it rings at sharp edges (negative density,
  speckle) — keep the clamp (8 extra taps). Density can still staircase; `sharpen` defaults low.
- **Texture bandwidth / VRAM.** ~600 MB/frame traffic, 230 MB resident: fine on 16 GB, and the 20 s
  fullscreen suspend frees it for games. Never combine `quality` tier + full-res raymarch + MG 3
  cycles (> 5 ms).
- **AMD-specific.** RDNA3: typed UAV loads for R16_FLOAT supported (check the cap anyway);
  `[numthreads(8,8,4)]` maps to wave64 well; 3D swizzle is automatic with layout UNKNOWN — never
  ROW_MAJOR for 3D; `Texture3D.SampleLevel` trilinear is full rate for R16F; timestamp frequency
  via `GetTimestampFrequency` (oil.cpp:415). The 2D app already proved the FP16 scRGB path on this
  driver, and that HDR toggles can drop the colour space (re-assert every second). Two GPU-heavy
  processes at once greyed the OLED before — the lock rule exists for this.
- **OLED: ABL and burn-in.** Paper mode is a full-white desktop: ABL clamps to ~418 nits and a static
  white field ages the panel — default negative mode, cap paper_level, offer the idle fade. HDR peak
  on cores only (knee 0.7), never on veils.
- **Shot determinism.** Blue-noise jitter or `rand()` anywhere breaks byte-identical A/B; all
  randomness goes through one seeded hash (`seed` in SimCB / a CPU PCG seeded by `--seed`).
- **User is gaming.** No live-exe tests, no MessageBox pop-ups, no focus changes; `--shot` with
  `--shot-yield 25` while a game runs (the 2D app's convention).

2D fake if 3D proves too expensive (branch 3 in `reference/shots/README.md`, `style=ink` in the
2D app): the 2D sim at 256/4096 already makes vortex pairs — a 2D drop with downward momentum IS a
mushroom in projection (the `--shot-pour` curls prove the motion). Add: dye-weighted gravity in the
force pass, Beer-Lambert display (`T = exp(-sigma*dye)`, paper/negative modes, the same knee/peak
path), a "thickness" scatter term from the dye gradient, and 2-3 sim layers at different scales
composited with Beer-Lambert for parallax (each layer +0.5 ms; the 2D sim costs ~1 ms today). Gets
the translucent veils, cores, curls and drops at ~2 ms; misses true 3D veils crossing in front of
each other and the ring-to-RT-lobe transition. Decide after the M1/M2 timings.
