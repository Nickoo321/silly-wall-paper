# OilWallpaper PoC — review and integration options

Investigation only: nothing was built, launched or edited. Sources read:
`src_oil/{main.cpp,oil.cpp,oil.h}`, `src_acid/{main.cpp,acid.cpp,acid.h}`,
`src/{main.cpp,fluid.cpp,fluid.h,shaders.h,settings.cpp,app_state.h}`,
`AGENTS.md`, `WORKLOG.md` 433-480, and the reference captures in `build/`.

---

## 1. Architecture

### 1.1 The look is entirely in one pixel shader

`src_oil/oil.cpp` compiles `kShaderSrc` at runtime (same pattern as
`src/shaders.h`), one fullscreen-triangle VS + PS, no compute, no textures,
one root CBV. Per pixel:

**Field.** `p = float2(uv.x*aspect, uv.y)`; a `[loop]` over all 64 blobs
accumulates a classic inverse-square metaball weight
`w = min(max(r²/d² - 0.08, 0), 3.5)`. The `-0.08` compact cutoff is what
keeps blobs from merging at long range (merge starts near centre distance
~2.6r); the `min(...,3.5)` stops single blobs from dominating the field.
Three things accumulate in the same loop: `field`, an analytic gradient
`grad += (-2r²/d⁴)·q`, and an influence-weighted colour sum
`colSum += blobCol[idx]·w`.

**Coverage.** `aa = fwidth(field)*1.5`, `cov = smoothstep(1-aa, 1+aa, field)`.
Screen-space-derivative AA of the field, which is the right call — the
surface is the `field == 1` isoline.

**Backdrop.** Vertical `lerp(bg, bgAccent, pow(uv.y, 1.15))`, multiplied by
a slow 3-octave fbm blotch (`±10 %`), plus a `hash21(pixel)` grain of
`±0.028`. The grain has **no time term** (contrast: `src_acid` uses
`hash21(pos + frac(time)*913.7)`), so oil's grain is a frozen fixed pattern
glued to the screen.

**Interior.** `bcol = colSum / field` (influence-weighted mean colour), then:
- marbling: 3-octave fbm at `marbleScale = 70` modulating brightness `±15 %`;
- rim: `sdf = (field-1)/|grad|`,
  `rim = max(smoothstep(0, rimWidth, sdf), smoothstep(1.0, 2.4, field))`,
  `bcol *= lerp(0.38, 1.0, rim)` — a constant-width dark outline that
  survives inside merged masses, with the field term keeping isolated cores
  saturated;
- a `+10 %` lift where `field` is high (`smoothstep(2,5,field)`);
- gloss: fake normal `n = normalize(float3(normalize(-grad)·tilt, 1))` with
  `tilt = 0.75·exp(-0.45·(field-1))`, a fixed top-left light, Lambert
  `0.82 + 0.34·diff` and a `pow(...,64)` specular added at `0.18·rim`.

Final `col = lerp(bgc, saturate(bcol), cov)`.

### 1.2 CPU sim (`StepBlobs`)

Per blob per frame:
- **pseudo-curl drift**: velocity taken as `(∂ψ/∂y, -∂ψ/∂x)` of an analytic
  two-term sine potential with per-blob phase offsets `s1/s2` and time terms
  — divergence-free, so blobs swirl rather than pile up. `driftK = 0.0030`
  gives peak speeds around `0.02 uv/s`.
- **buoyancy**: `±0.012·(baseR/0.10)` in y, sign flipped for the ~9 % of
  blobs flagged `sinker`. Radius-proportional, so it is also a size sorter.
- **damping**: `k = 1 - exp(-2.5·dt)` velocity relaxation — correctly
  fps-normalised (this is the per-step-decay lesson from the fluid app,
  applied properly here).
- **wrap** at `±0.16` uv margin in both axes.
- **breathing**: `phase += breathRate·dt`, radius `baseR·(1 + 0.15·sin(phase))`,
  rates 0.15–0.45 rad/s.

`SeedBlobs` places 35 big colour-0 blobs (r 0.045–0.100), 19 colour-1
(0.030–0.065), 10 small colour-2 droplets (0.018–0.032), all in the lower
2/3 of the screen.

### 1.3 Timing / fps / shell

`src_oil/main.cpp` is a trimmed clone of `src/main.cpp`: WorkerW attach with
the same three-layout probe (classic sibling / 24H2 child / Progman
fallback), tray with Pause + Palette radio + Exit, `TaskbarCreated`
re-add, Explorer-restart reattach loop, single-instance mutex,
`SPI_SETDESKWALLPAPER` repaint on exit. QPC timing, `dt` clamped to 0.1 s,
EMA fps, 5-second log lines to `%TEMP%\OilWallpaper.log` (share-read).

### 1.4 Hardcoded vs parameterizable

`OilConfig` exposes exactly four fields: `palette`, `marbleScale`,
`rimWidth`, `fpsLimit`. Only `palette` is reachable at runtime (tray /
`--palette`). Everything else is baked: the two palettes (`kPalettes[2]`,
raw sRGB floats), blob count 64, the whole size/colour distribution, the
0.08 cutoff and 3.5 clamp, drift/buoyancy constants, breathing rates and
amplitude, light direction, specular exponent, rim darkening 0.38, grain
amount, backdrop gradient exponent. There is no ini, no disk config, no
settings window.

### 1.5 Bugs and issues found

**a. Unconditional gradient accumulation (real bug).** Oil accumulates
`grad` for every blob including those whose weight was clamped to 0 (far
blobs contributing nothing to `field`) and those clamped at 3.5 (near
centres, where `2r²/d³` explodes). This is exactly the artifact `src_acid`
fixed and documented in AGENTS.md; oil only *masks* it with the
`smoothstep(1.0, 2.4, field)` branch of the rim term. The consequence is
visible in the captures: the gloss normal is wrong inside merged masses,
painting a crescent seam around every sub-blob (see §2).

**b. Colour-bleed haze (worst visual bug).** `colSum/field` averages the
tails of *every* blob, so a teal blob sitting behind a red mass desaturates
a wide region of it to a milky grey-pink. In `oil-final.png` this is the
dominant artifact across the whole top mass.

**c. Marbling is in screen space, not blob space.** The fbm is evaluated at
`p` with only a slow global scroll, so blobs slide *through* their own
texture. Nothing about the mottling travels with the liquid — a strong
tell that this is a painted field, not a fluid.

**d. Size-sorting empties the frame.** Buoyancy scales with radius, so big
blobs rise ~5× faster than the small dark droplets. Over a minute the
composition separates into "big mass at the top, beads in the middle,
nothing at the bottom" — exactly what `oil-final.png` (70 s) shows. Wrap
recirculates but does not mix.

**e. Present pacing on a 144 Hz panel (real quality issue for this look).**
`Present(1, 0)` blocks to a vblank, and the loop then gates on
`dt < 1/(fpsLimit+2)`. On 144 Hz that resolves to 2–3 vblanks per rendered
frame with an irregular cadence — which is what "~57 fps" in the worklog
means. Slow, smooth blob drift is the worst possible content for that kind
of judder. Either present every vblank with dt-correct motion, or use a
waitable swap-chain object, or `Present(0,0)` plus precise pacing.

**f. No pause-on-fullscreen.** The PoC renders and vsync-presents while a
game is in the foreground. Given the user games, this is the single most
user-visible missing feature, more than HDR.

**g. Two wallpaper apps can run at once.** Different mutex names
(`OilWallpaper_SingleInstance` vs the fluid one), both attach to the same
WorkerW, both `SPI_SETDESKWALLPAPER` on exit.

**h. `float m_time` accumulator.** Shared with `src/fluid.h:212`, not
oil-specific, but the oil look depends on `sin(0.23·t)` phases: after a
couple of days of uptime float32 epsilon at that magnitude exceeds a frame
delta and motion quantises.

**i. 8-bit output / banding — measured, currently OK.** Sampling the
`oil-royal.png` backdrop down a clean column: R runs 38→105 and B 78→165
over 1440 px, i.e. ~16 px per code value, which would contour badly on its
own. The `±0.028` hash grain (≈ ±3.6/255) dithers it; no contouring is
present in the captures. But the dither is what is saving it, and it is
undithered inside the blobs (the marble modulation happens to cover there).
Note also that the palettes are authored as sRGB values written **raw** to
a `R8G8B8A8_UNORM` chain — correct only because the chain is UNORM. The
moment the format becomes scRGB FP16 they must go through an sRGB decode
(`SRGBToLinear` in `src/shaders.h`) or every colour shifts.

**j. Perf.** 64 blobs × 3.69 M pixels = **236 M blob evaluations per frame**
(~14 G/s at 60 fps), plus 24 `hash21` calls for the two fbm evaluations.
The `[loop]` is never unrolled and has no early-out or culling. The
measured 57 fps is vsync-bound so the real cost is unknown — first action
is to measure it unlocked. If it needs cutting, the standard fix is a
coarse tile pass (32×32 tiles → 80×45) writing per-tile blob index lists to
a UAV; a typical tile touches 4–8 blobs, a 5–10× reduction. Dropping the
fbm to 2 octaves is another easy ~15 %.

---

## 2. Visual quality (from the captures)

### `oil-final.png` — Acid palette, 70 s
**Strengths.** The metaball topology is genuinely good: necks, splits,
pinch-offs and satellite droplets all read correctly, and the AA on the
isoline is clean at 1:1. The dark rim gives the blobs weight. Sizes are
well varied.

**Weaknesses.**
- The teal-into-red colour bleed (bug b) turns the entire top mass into a
  milky grey-pink fog. At 1:1 it reads as mould or freezer burn. This is
  the thing that most stops it looking like liquid.
- The marble fbm at scale 70 produces ~25 px blotches that read as
  *lichen/dirt*, not marbling.
- Composition has collapsed: one connected mass welded to the top edge,
  a big empty gradient through the middle, isolated beads.
- Surface reads as gummy/plastic — matte, opaque, no transparency and no
  interaction with the backdrop.
- Specular appears as thin white crescent slivers that look like creases
  in wax.

### `oil-royal.png` — Royal palette
Much stronger composition: one coherent red-orange mass on deep violet, a
clean complementary pairing. The same defects remain and one new one is
obvious at 1:1: every sub-blob inside the merged mass leaves a **crescent
seam** from the bad gradient (bug a), so a single fused mass reads as a bag
of overlapping balls. Dark colour-2 blobs embedded in the red read as
bruises/eyes rather than droplets. ABL concern: this frame is ~40–50 %
near-peak saturated red; on the X27U that raises full-frame APL and ABL will
pull the whole panel down — contrary to the AGENTS "keep hot elements small"
rule.

### Acid variant — `acid-p0b.png` / `acid-p1b.png`
A different art direction, not an improvement of the same one: posterized
flat interiors, no gloss, a thin bright rim (Gaussian on the sdf) that
reads as neon even though no bloom is used, a mottled two-tone medium,
interface speckle and animated film grain. `p1b` (hot orange rims, dark red
mass, deep purple medium) is the most striking of the four images. The acid
sim also seeds satellites around parents and, for palette 1, along a sine
ribbon.

Acid's own weakness: because interiors are posterized *per-blob-weight*,
every sub-blob's boundary shows as a hard perfect circle inside the mass —
dozens of visible ghost circles. It reads as a petri dish / cell biology,
which may be exactly the wanted vibe, but it is not oil. The speckle reads
as dirt. `p1b` also leaves most of the frame empty (band seeding).

### What would actually make it read as oil on water / lava lamp
In rough order of payoff per line of code:

1. **Refraction.** Sample the backdrop at `uv + k·grad_uv` instead of
   straight. One line; it is the single change that converts "painted
   plastic" into "transparent liquid lens", because the eye reads the
   displaced background through the blob.
2. **Thickness-based colour (Beer–Lambert).** Replace the flat interior
   colour with `exp(-absorb · thickness)` where thickness comes from the
   field magnitude. Real oil is dark and saturated where it is deep, pale
   and translucent at the edges. This also fixes the bleed problem for free
   if combined with (3).
3. **Dominant-colour selection instead of a weighted mean.** Track the
   argmax weight (or keep three separate per-colour fields and take the max
   with a soft blend near ties). Kills the milky haze.
4. **Fix the gradient accumulation** (port the `src_acid` guard). Kills the
   crescent seams and gives a correct normal.
5. **Specular only near the meniscus.** Real oil shows a thin bright line
   hugging the rim, not a broad diffuse shade over the whole blob. Fade the
   gloss tilt much faster with field so interiors are flat.
6. **Advect the marbling with the liquid.** Give each blob a texture-space
   offset that moves with it, or domain-warp the fbm by the local blob
   velocity. Currently the texture is nailed to the screen (bug c).
7. **Heavier, slower motion + surface tension.** Add soft repulsion so
   blobs deform instead of interpenetrating, and a temperature/recirculation
   rule (rise → cool at the top → sink) instead of wrap, so the frame stays
   composed rather than size-sorting (bug d).
8. **Animate the grain** or, better, move it to a proper output dither.

---

## 3. Parity with the main app's shell

Effort assumes working inside the existing codebase and includes HDR-on/off
verification as AGENTS requires.

| Item | Reusable from main app | Duplicate / new | Effort (standalone) | Effort (as a mode in FluidWallpaper) |
|---|---|---|---|---|
| **HDR scRGB FP16 output** | `src/fluid.cpp:158-181` (swapchain desc + `CheckColorSpaceSupport`/`SetColorSpace1`), `src/shaders.h` `SRGBToLinear` + both gamut matrices + `sdrScale`/`peakGain` tail, `BuildDisplayConstantsEx` (`fluid.cpp:651`), `GetSdrWhiteNits` + `QueryHDR` (`main.cpp:364`, `:400`) | format change in 3 places (`sd.Format`, `RTVFormats[0]`, readback footprint), FP16→8-bit conversion in `WriteSnapshotBmp`, 4 new CB fields, sRGB-decode the palettes | 3–5 h | ~1 h (append the existing tail to the oil PS; constants already built) |
| **ini config layer** | `main.cpp` `LoadSettings`/`SaveSettings`/`LoadConfigFromFile`/`WriteConfigToIni` patterns (`GetPrivateProfile*`), partial-overlay convention | `OilConfig` needs to grow from 4 fields to ~20–25 (palette colours, blob distribution, drift/buoy/breathe constants, marble, rim, gloss, HDR) | 2–3 h | ~2 h |
| **Settings window** | nothing directly: `src/settings.cpp` is 1099 lines hardwired to `FluidConfig`/`FluidRenderer` via `app_state.h`; but `BuildDefs()` is a flat declarative table of `{label, min, max, step, decimals, float*, int*, section, key, …, page}` rows | standalone: a whole new window | 1–2 days | **2–4 h** — add rows to `s_sliders` with a new page index; the table takes bare `float*`, so an oil config global plugs straight in |
| **pause-on-fullscreen** | `IsShellOrOwnWindow`, `FullscreenAppActive`, `MaximizedAppActive` (`main.cpp:305-355`) — ~60 lines, copy with the class names changed | — | 1 h | free |
| **suspend/resume** (free GPU+RAM after 20 s of fullscreen) | `main.cpp:1308-1325` + `FluidRenderer` teardown/Init split | oil teardown is trivial (no sim textures) | 2 h | free |
| **tray** | already present in the PoC | add fullscreen-pause toggle, settings entry, autostart reg key | 1 h | free |
| **second-monitor mirror** | `EnableMirror`/`DisableMirror`/`RenderMirror` (`fluid.cpp:820-902`), `CreateWallpaperWindowAt`, `FindSecondMonitor`, per-monitor `QueryHDR`/`GetSdrWhiteNits`, lifecycle block (`main.cpp:1221-1264`) | oil's mirror is *easier* than fluid's: the pass is stateless, so it is a second swapchain + a second draw with a different `aspect` | 3–4 h | ~1 h |
| **Explorer reattach / WorkerW / single instance** | already ported | — | done | free |

**Totals.** Standalone parity: **4–6 working days**, most of it re-writing a
settings window and re-verifying HDR on/off. As a mode inside
FluidWallpaper: **1.5–2 days**, because six of the eight rows collapse to
"free" or "one hour".

---

## 4. Integration options

### (a) Keep it a standalone exe
- **Pro:** zero regression risk to a finished, calibrated app. Clean
  separation; can be deleted without trace. Already builds.
- **Con:** every shell feature must be written twice and maintained twice
  (three times, counting `src_acid` — the shells are already byte-identical
  clones diverging only in names). Two wallpaper hosts can run
  simultaneously and fight over WorkerW. HDR, ABL and "looks good HDR on
  and off" have to be verified separately per exe. The user has to kill one
  app and launch another to switch looks, with no shared moods/journey.
  Realistically this path ends with three half-maintained wallpaper apps.

### (b) "Oil" as a render mode inside FluidWallpaper — **recommended**
How much actually transfers:
- **Shell: 100 %.** WorkerW attach, tray, ini, settings window, fullscreen
  pause, suspend, mirror, Explorer reattach, HDR detection, autostart — all
  of it, unmodified.
- **Swapchain / HDR: 100 %.** `FluidRenderer::RenderDisplay` is the only
  thing that writes the backbuffer. An oil PS becomes a sibling PSO; the
  final scRGB tail (sRGB decode → gamut matrix → `sdrScale` → `peakGain`)
  is literally the last ten lines of `kDisplaySrc` and can be pasted into
  the oil PS or factored into a shared HLSL snippet string.
- **What is new:** the blob upload CB. The display pass currently uses 32
  root constants; a 64×`float4` blob array is 1 KB, too large for root
  constants, so it needs an upload buffer + CBV like the PoC already has
  (~40 lines, the PoC code drops in).
- **Cheaper at runtime than fluid mode:** in oil mode the whole sim
  (velocity/dye/pressure/divergence/curl textures and every compute
  dispatch) is skipped. At 512/4096 that is a large VRAM and GPU saving.
- **Settings UI:** `BuildDefs()` is a flat table with a `page` index per
  row, so an "Oil" page is purely additive. The one catch is that rows hold
  raw `float*`/`int*` into `FluidConfig`; oil params either join
  `FluidConfig` (consistent with how `g_hdrPeakNits`, a shell global, is
  already referenced from that same table) or live in a file-scope
  `OilConfig` that the table points at — both work without touching the
  slider machinery.
- **Risks:** moods/journey/presets assume fluid parameters. Oil mode must
  either be excluded from mood overlays or get its own mood set; the ini
  overlay convention ("unspecified keys keep live values") makes that
  tractable but it needs a deliberate decision. A mode switch also needs to
  teardown/rebuild cleanly — the suspend/resume path already proves that
  works.
- **Effort:** 2–3 days for the mode plumbing + settings page + HDR-verified
  parity, on top of whatever look work is done.

### (c) Hybrid: fluid velocity/dye drives the oil
This is genuinely feasible with the existing compute pipeline, in three
increasingly interesting steps:

1. **Blobs advected by the velocity field.** Blobs need velocity at their
   positions on the CPU. The pattern already exists: `UpdateCoverage()`
   (`fluid.cpp:1501`) downsamples to a 48×27 RGBA16F texture, copies to a
   `HEAP_TYPE_READBACK` buffer and reads it a frame later at ~10 Hz. The
   same trick on `m_velocity.read` (a 64×36 R16G16F downsample) is ~100
   lines plus one compute PSO. Replacing the analytic curl potential with
   real fluid velocity is the highest-value change here: the blobs then
   swirl in the same currents as the dye, which is exactly the "alive"
   quality the analytic field cannot fake.
2. **Blob step in compute instead of on the CPU.** One dispatch of 64
   threads against a structured-buffer UAV, sampling the velocity SRV
   directly — no readback latency, negligible cost. The compute root
   signature already carries SRV and UAV descriptor tables; it needs one
   more UAV slot. ~150 lines.
3. **Two-way coupling.** Emit dye at blob positions (reuse the splat path)
   so the fluid carries colour plumes behind rising blobs — that is the
   actual convective signature of a lava lamp. And in the oil PS, offset
   the backdrop sample by the *velocity* field as well as by `grad`, so the
   liquid behind the blobs refracts and streams.

- **Would it look better?** Very likely yes, and it solves three of the
  listed defects at once: motion stops being analytic and periodic
  (fixes d), the backdrop stops being a static gradient, and marbling can
  be advected by the same field (fixes c). It is also the only option that
  makes the two looks one thing rather than two wallpapers in one exe.
- **Cost:** 2–3 days on top of (b). **Risk:** two art directions can fight
  — a busy fluid backdrop behind busy blobs is mud. It needs its own
  calibration quiz per the AGENTS protocol, and the "hot elements small"
  ABL rule gets harder when both layers are lit.

---

## 5. Recommendation

**Do (b): fold oil in as a render mode inside FluidWallpaper, and fix the
look before/while porting. Keep (c) as a flagged experiment afterwards.
Retire `src_oil`/`src_acid` as standalone targets once the mode lands.**

Reasoning:
- The parity cost is almost entirely *shell* cost, and the shell is already
  written, hardened (24H2 WorkerW quirks, Explorer restart, suspend) and
  calibrated. Duplicating it a second and third time buys nothing.
- The look is not finished. It is much cheaper to iterate on it when HDR
  output, live sliders, the calibration quiz pages and the analyzer nits
  heat-map already exist — those are exactly the tools needed to tune this
  on the X27U, and they only exist inside FluidWallpaper.
- The AGENTS constraints (no bloom, good with HDR on *and* off, ABL /
  small hot elements) have to be verified per-exe. One exe, one
  verification pass.
- Oil mode is *cheaper* at runtime than fluid mode (no sim textures, no
  compute), so it is a natural low-power alternative mode, not a burden.
- Standalone (a) is only right if the intent is to throw the oil look away
  soon; it is not worth 4–6 days of duplicated shell work otherwise.

On HDR specifically: spend the headroom on the **rim and specular only**,
keeping blob interiors at or below SDR white. That satisfies the ABL rule,
and a thin hot wet glint is also the most convincing "oil" use of HDR —
a large area of blob interior pushed above SDR white would both trip ABL
and look like plastic.

### Ordered task list

**Phase 0 — look fixes in the PoC (fast iteration, no shell work), ~1–2 days**
1. Port the `src_acid` gradient guard into `src_oil` (accumulate `grad`
   only where the per-blob weight is unclamped). Verify the crescent seams
   are gone with `--shot`.
2. Replace `colSum/field` with dominant-colour selection (argmax weight, or
   three per-colour fields with a soft blend near ties). Verify the milky
   haze is gone on the Acid palette.
3. Add backdrop refraction: sample the backdrop at `uv + k·grad_uv`.
4. Add Beer–Lambert thickness tint for the interior; flatten the gloss
   (much faster tilt falloff) and confine the specular to a thin band near
   the rim.
5. Advect the marbling with the blobs (per-blob texture offset or velocity
   domain warp); lower `marbleScale` until it stops reading as lichen.
6. Sim: add soft repulsion / surface tension, and a rise-cool-sink
   recirculation rule replacing pure wrap; decouple buoyancy from radius
   enough that the frame stops size-sorting.
7. Animate the grain (match `src_acid`) or move it to an output dither.
8. Measure real frame cost unlocked; if it is above ~2 ms at 2560×1440,
   add the coarse tile-list pass.
9. Shoot `--shot` captures of both palettes at 25/70/180 s and review
   composition over time.

**Phase 1 — mode plumbing in FluidWallpaper, ~2–3 days**
10. Add a `renderMode` field (fluid / oil) to `FluidConfig` + ini key +
    tray radio; make `Frame()` branch before the sim block and skip
    `CreateSimResources` in oil mode.
11. Move the oil PS into `src/shaders.h`, appending the existing
    `kDisplaySrc` scRGB tail (sRGB decode → gamut matrix → `sdrScale` →
    `peakGain`); sRGB-decode the palette constants. Add the blob upload CB
    (lift from `src_oil/oil.cpp`).
12. Move `SeedBlobs`/`StepBlobs` into `fluid.cpp` (or a new `src/oil.cpp`
    compiled into the same target).
13. Wire `BuildDisplayConstantsEx`'s HDR outputs into the oil pass; cap
    interior brightness at SDR white and route `peakGain` to rim/specular
    only.
14. Add an "Oil" page to `settings.cpp`'s `s_sliders` table (palette,
    blob count, size spread, drift, buoyancy, breathing, marble, rim,
    refraction, gloss).
15. Verify: HDR on and HDR off, both palettes, mirror on the second
    monitor, fullscreen pause + suspend/resume, Explorer restart.
16. Decide moods/journey policy for oil mode (exclude, or a separate mood
    set); document in AGENTS.md.
17. Remove the `OilWallpaper` and `AcidWallpaper` targets from
    `CMakeLists.txt` once the mode is verified; keep `src_acid`'s posterize
    look as a second oil *style* preset if it is wanted (it shares the
    whole field/sim, only the shading tail differs).

**Phase 2 — hybrid experiment, behind a flag, ~2–3 days**
18. Add a low-res velocity downsample + async readback modelled on
    `UpdateCoverage()`; feed it into `StepBlobs` as the drift source
    alongside (then instead of) the analytic curl potential.
19. If the readback latency shows, move the blob step to a 64-thread
    compute dispatch against a structured-buffer UAV sampling the velocity
    SRV directly.
20. Offset the oil PS's backdrop sample by the velocity field as well as by
    `grad`; try advecting the marble fbm by the same field.
21. Try two-way coupling: emit dye at blob positions via the existing splat
    path so the fluid carries plumes behind rising blobs.
22. Run a calibration quiz with the user on the result per CALIBRATION.md
    before it becomes a default.
