# INK2D-PLAN — `style=ink` ("ink in water") for the 2D fluid app

Branch 3 plan. Executor: Opus. Refs: `reference/shots/photos/ink-in-water-ref-1-black-drop-plume.jpg`,
`ink-in-water-ref-2-veils-and-splash.jpg`. Base look: `reference/configs/we-look-live.ini`.
Pattern to copy: the Liquid Acid work (commit d6f6019): `#ifdef` section in `kDisplaySrc`, extra
PSO built only when the style is on, `[look] style=`, regression shot of `style=fluid` md5-identical.

Scope rule (user, 2026-09-16): the ink features are SHARED, not a silo.
- (a) sim-level toggles usable by ANY style: dye-weighted gravity/buoyancy, drop injection,
  decay/diffusion settings. Read regardless of `[look] style`.
- (b) render-level (Beer-Lambert translucency, edge darkening, paper/inverted): implemented ONCE as
  a common HLSL block, exposed as `style=ink` (third PSO) AND as `[liquid_acid] ink_mode=water`
  inside the Liquid Acid ink path (oil floating on ink-in-water = what the acid refs literally are).
- `style=fluid` stays byte-identical (shot md5), proven before and after.

## 1. Look analysis (priority order) and which knob makes it

Ref 1 (black drop, paper): backlit white softbox; one drop entering from the top; a dense OPAQUE
head that has rolled into a mushroom cap; a thin trail from the entry point; veils around the cap
are TRANSLUCENT grey (see-through), cores are solid black; every veil has a darker outline where it
is seen edge-on (folds); fine filaments hang off the cap; the drop keeps FALLING (gravity: ink is
denser than water). Ref 2 (blue-black veils + splash): the same, sideways, with hundreds of separate
splash droplets and a faint blue tint in the thin parts.

| # | Feature (most important first) | What produces it | Where |
|---|---|---|---|
| 1 | Translucent veils, opaque cores (thin = grey see-through, thick = black) | Beer-Lambert: `T = exp(-k * d * a_rgb)`, `d` = dye density (max channel, 0..1.35), `a_rgb = 1 + ink_chroma*(1 - chroma)` so the max channel is the ink's transmitted colour. Paper: `out = paper * T`. Inverted: `op = 1 - exp(-k d)`, `out = tint(op) * op`. | display PS, shared block |
| 2 | Falling plume / mushroom cap from ONE drop | A single Gaussian velocity impulse pointing down (+y) becomes a vortex dipole after the pressure projection = the cap. Then dye-weighted gravity keeps the dense head sinking while thin veils hang. Gravity goes where the baroclinic term already samples the dye (`CSVorticity`, `SrcC`): `force.y += gravity * pow(rho, gravity_pow)`. | sim: `CSVorticity`, `SimCB`; drop: `InjectDrop()` |
| 3 | Edge darkening (folds/sheets read as 3D) | Gradient of density at `edge_scale` screen texels: `e = smoothstep(lo, hi, |grad d|)`; treat it as extra optical path: `thick = d * (1 + edge_strength * e)`. Same taps the fluid emboss and acid seams already use. | display PS, shared block |
| 4 | Fine filaments vs glassy sheets | `sim_res`: 256 = WE curls/droplet spirals (the frozen live value); 512 = long glassy sheets (closer to ref 2). Shot-only experiment; the 256/4096 freeze in AGENTS.md is NOT changed by this plan — if the user prefers 512 for ink that is his call to change the rule. `dye_diffusion` 0.02–0.1 softens veil edges without touching cores. | `[sim]` |
| 5 | Drop lifetime / screen never fills | `density_diffusion` 0.999 (e-fold ~8 s at 120 steps/s, a 1.35 core stays visible ~30 s), 0.998 ~15 s. `decay_fast` MUST stay 1.0 (fast decay of faint dye kills the veils). `saturation_restore` MUST be 0 (see gotcha). | `[sim]` |
| 6 | Splash droplets (ref 2) | Not sim physics in 2D. Fake: `drop_spatter` N tiny high-density satellite splats with outward velocity at injection. | `InjectDrop()` |
| 7 | Backlit look | Paper colour slightly below full white + optional radial `paper_vignette`; on inverted, `ink_tint_thin` (pale) vs `ink_tint_thick` (white) so cores glow. | shared block |
| 8 | Depth / two layers (optional, phase 2) | Cheap parallax: sample the SAME dye at `uv*parallax_scale + drift`, 4-tap blur, add `parallax_amt * d2` to `thick`. Cost ~4 taps (~0.2 ms). A real second dye field would double the dominant dye-res passes — do not. | shared block |

Gotchas found in the code (must be respected):
- `CSAdvectDye` saturation restore: `satC = (c - mn) * ...` is 0 for grey dye, so a neutral (black/grey)
  drop is erased by `sat_restore > 0`. Ink inis set `saturation_restore=0`. Coloured ink (chroma > 0)
  survives but drifts toward full saturation — accept or keep 0.
- Velocity units are sim texels/s (`coord = uv - dt*vel*texelSize`); `MultipleSplats` uses ±500.
  Velocity dissipation 0.999 per 1/120 s = 8 s drag time constant, so gravity must be SMALL
  (tens, not thousands) or terminal speed crosses the grid in a second. Keep 0.999 (WE curls need it).
- `CssHueRotate`/`CssSaturate` rows sum to 1: white paper and black background are invariant, so the
  hue cycler and post filters act on ink chroma only. P3 matrix rows also sum to 1.
- Dye is additive intensity; "black ink" is stored as bright neutral dye and INVERTED at render.

## 2. Implementation plan

### 2.1 Files / functions
- `src/fluid.h`: `struct InkConfig` (render), new sim fields in `FluidConfig`, `struct DropConfig`,
  `FluidConfig::ink`, `FluidConfig::drops`, `LiquidAcidConfig::inkMode`. Renderer: `m_psoInk`,
  `m_inkParamUpload[kFrames]`/`m_inkParamData`, `DisplayPso()` 3-way, `void InjectDrop(...)` public,
  `UpdateDrops(float dt)`, `UploadInkConstants()`, `BindInk()`; `Splat()` gains `float radiusPct = -1`
  and `float cap = -1` overrides (default = old behaviour).
- `src/fluid.cpp`: `SimCB` += `gravity`, `gravityPow` (20 → 22 DWORDs; update the static_assert; root
  constant count is `sizeof/4`, automatic). `SimStep`: `cb.gravity = m_cfg.gravity` etc. `CreateDevice`:
  third PSO `makeGfx(kDisplaySrc, m_psoInk, {"INK","1"})` only when `m_cfg.ink.enabled`; graphics RS
  gets root param 4 = CBV `b2` (InkCB), bound by `BindInk()` next to every `BindAcid()` call (display,
  offscreen, analyzer, mirror). `Frame`: `UpdateDrops(dt)` after idle splats; `UploadInkConstants()`
  when ink or acid-water is on. `Shutdown`: reset the new PSO/buffers.
- `src/shaders.h`: gravity branch in `CSVorticity`; shared `INK_WATER` block; `#ifdef INK` in `PSMain`;
  `ink_mode` branch in the LIQUID_ACID ink section. New code in its own `R"hlsl(` chunk (MSVC 16 KB).
- `src/main.cpp`: `[look] style=ink`, `[look] ink=0|1`; `[ink]`, `[drops]`, `[sim] gravity*`,
  `[liquid_acid] ink_mode`; `--shot-drop X,Y,T[,VY]`. `printf` look name.
- `src/settings.cpp`: pages "Ink in water", "Drops"; sliders on "Simulation" (gravity); checkboxes.
- `reference/configs/ink-paper.ini`, `ink-inverted.ini`, `liquid-acid-water.ini` (full inis like
  liquid-acid-a.ini, base = we-look-live.ini).

### 2.2 Config: fields, defaults, ini keys
Sim-level (style-agnostic, `FluidConfig`, section `[sim]`; defaults keep fluid unchanged):
```
float gravity     = 0.0f;   // sim texels/s^2 per unit density, +down, -up (buoyant smoke). [sim] gravity
float gravityPow  = 1.5f;   // rho^p: thin veils hang, dense cores fall.            [sim] gravity_pow
```
Drops (`DropConfig FluidConfig::drops`, section `[drops]`, emitter usable with any style):
```
bool  enabled     = false;  // drops=1
float interval    = 14.0f;  // s between drops; jitter +-interval*0.35        interval
float xMin=0.15f, xMax=0.85f, yMin=0.04f, yMax=0.22f;   // entry band, uv    x_min x_max y_min y_max
float speed       = 700.0f; // downward impulse (dx jitter +-60)              speed
float radius      = 0.35f;  // splat radius, percent like splat_radius        radius
float density     = 1.35f;  // dye intensity (capped by max_brightness)       density
float tailSec     = 0.6f;   // keep painting dye (no velocity) at the entry   tail_sec
float tailDensity = 0.25f;  //                                                tail_density
int   spatter     = 0;      // satellite droplets (0 = off)                   spatter
float spatterRadius = 0.04f, spatterSpeed = 500.0f, spatterSpread = 0.05f;   // spatter_radius/_speed/_spread (uv)
int   colorMode   = 0;      // 0 = ink_color, 1 = wheel hue (PickSplatColor)   color_mode
float color[3]    = { 1, 1, 1 };  // dye-space colour (white = neutral ink)   color = "r g b"
bool  obeyGovernor= true;   // skip while m_screenTooFull                      obey_governor
```
Render (`InkConfig FluidConfig::ink`, section `[ink]`):
```
bool  enabled     = false;  // [look] style=ink  (or [look] ink=1)
bool  inverted    = false;  // 0 paper (dark ink on light), 1 ink on black    inverted
float density     = 3.0f;   // k: T(1.35)=e^-4=0.02 opaque, T(0.05)=0.86 veil density
float chroma      = 0.0f;   // 0 neutral ink, 1 = dye hue tints the ink (0..3)  chroma
float edgeStrength= 0.35f;  // fold darkening                                 edge_strength
float edgeLo=0.02f, edgeHi=0.25f;  // |grad d| window                        edge_lo edge_hi
float edgeScale   = 2.0f;   // tap spacing, screen texels                     edge_scale
float paper[3]    = { 0.90f, 0.91f, 0.93f };  // paper (or inverted background) paper_color
float vignette    = 0.15f;  // radial darkening of the paper                  vignette
float tintThin[3] = { 0.70f, 0.85f, 1.00f };  // inverted: thin veil light    tint_thin
float tintThick[3]= { 1.00f, 1.00f, 1.00f };  // inverted: opaque core light  tint_thick
float coreKnee    = 0.30f;  // inverted: op where tint blends thin->thick     core_knee
float hdrCore     = 1.0f;   // inverted: scale of raw-dye m driving HDR gain  hdr_core
float parallax    = 0.0f;   // phase 2, 0 = off                               parallax
float parallaxScale = 0.92f, parallaxDrift = 0.004f;  // parallax_scale parallax_drift
```
Liquid Acid: `int inkMode = 0; // 0 bands (current), 1 water` → `[liquid_acid] ink_mode=bands|water`
(string; the checkbox writes the int form `ink_water=0|1`, string wins).
`[look] style` parse: `fluid|liquid_acid|ink` sets exactly one of `acid.enabled`/`ink.enabled`;
the int keys `liquid_acid=`/`ink=` (checkboxes) apply only when `style` is absent; ink wins ties.
`DisplayPso()`: acid → `m_psoLiquidAcid`, else ink → `m_psoInk`, else `m_psoDisplay`.

### 2.3 Shader outline
`CSVorticity` (compute, all styles; identical velocity when gravity == 0):
```
if (gravity != 0.0f) {                       // rho sampled like the baroclinic block (reuse it
    float rho = dot(SrcC.SampleLevel(linearClamp, uv, 0).rgb, lw);   //  when both are on)
    force.y += gravity * pow(max(rho, 0.0), gravityPow);   // +y is down on screen
}
```
Shared block, once, inside `#if defined(INK) || defined(LIQUID_ACID)` (own `R"hlsl(` chunk placed
before the `VSOut` chunk):
```
cbuffer InkCB : register(b2) {
    float4 ikP0;   // x density k, y chroma, z edgeStrength, w edgeScale
    float4 ikP1;   // x edgeLo, y edgeHi, z inverted, w vignette
    float4 ikP2;   // x parallax, y parallaxScale, z parallaxDrift, w time
    float4 ikP3;   // x coreKnee, y hdrCore, z -, w -
    float4 ikPaper, ikTintThin, ikTintThick;
};
float  InkDensity(float3 C) { return max(C.r, max(C.g, C.b)); }
float  InkEdge(float2 uv, float2 texel, float scale) {   // 4 taps, |grad density|
    float2 ex = float2(texel.x * scale, 0), ey = float2(0, texel.y * scale);
    float L = InkDensity(Dye.SampleLevel(linearClamp, uv - ex, 0).rgb), R = ...+ex, T = ...-ey, B = ...+ey;
    return smoothstep(ikP1.x, ikP1.y, 0.5 * length(float2(R - L, B - T)));
}
// C = RAW dye (pre-emboss, pre-CSS). Returns display colour in sRGB-encoded space (the same
// space the fluid look's C is in before SRGBToLinear), and the m to drive HDR gain.
float3 InkWater(float3 C, float2 uv, float2 texel, float2 pos, out float hdrM) {
    float  d = InkDensity(C);
    float3 chroma = (d > 1e-4) ? C / d : 1.0;
    float  thick = d * (1.0 + ikP0.z * InkEdge(uv, texel, ikP0.w));
    if (ikP2.x > 0.0) { /* parallax: 4-tap blur of Dye at (uv-0.5)*ikP2.y+0.5+drift; thick += ikP2.x * d2 */ }
    float3 a = 1.0 + ikP0.y * (1.0 - chroma);            // coloured absorption
    float3 T = exp(-ikP0.x * thick * a);
    if (ikP1.z < 0.5) {                                    // PAPER
        float v = 1.0 - ikP1.w * smoothstep(0.35, 1.0, length(pos * texel - 0.5) * 1.6);
        hdrM = 0.0;                                        // paper never gets HDR gain
        return ikPaper.rgb * T * v;
    }
    float  op = 1.0 - exp(-ikP0.x * thick);                // INVERTED
    float3 tint = lerp(ikTintThin.rgb, ikTintThick.rgb, smoothstep(ikP3.x, 1.0, op));
    hdrM = d * ikP3.y;                                     // hot cores expand, veils stay SDR
    return lerp(ikPaper.rgb, tint * lerp(1.0, chroma, saturate(ikP0.y)), op);
}
```
`PSMain`:
```
float3 C = Dye.SampleLevel(...).rgb;  float3 C0 = C;  float m = max(...);
#ifdef INK
    C = InkWater(C0, uv, texelSize, i.pos.xy, m);          // replaces the dye->colour step
#else
    if (shading > 0.5) { ...existing emboss (LIQUID_ACID attenuation inside, unchanged)... }
#endif
    C = saturate(C); ...CSS chain, curve, shadow floor unchanged... (ink inis neutralise them)
#ifdef LIQUID_ACID
    float3 inkC;
    if (laP10.z > 0.5) { float mm; inkC = InkWater(C0, uv, texelSize, i.pos.xy, mm); }   // ink_mode=water
    else               { ...existing bands / ramp / complement lock...; }
    ...seams, oil composite, meniscus, swarms, grain unchanged (they read inkC)...
#endif
```
Fluid PSO: neither macro → none of this code; the `#ifndef INK` wrap of the emboss keeps the code
present. `laP10.z` (currently spare) carries `inkMode`. With ink_mode=water the water ink skips the
parity CSS chain (it reads C0) — same as bands mode's `ink_mix≈1` bypass, documented in WORKLOG.

### 2.4 Behaviours in `style=ink` (ini policy, no code changes to them)
- Idle splats (`MultipleSplats`: random direction, 1.5 intensity): OFF; `[drops]` replaces them.
- Wanderers: OFF by default (their 0.1 trails read as continuous wisps — keep as a user option).
- Dart: OFF. Mouse: hold-to-pour and stir stay ON (a pour = veils and sheets, good test).
- Hue cycler: harmless (white/black invariant); OFF in the paper ini, optional in inverted with chroma>0.
- Moods: `enabled=0` (recipes carry shadow_floor/post_* that break paper), same policy as acid.
- Coverage governor: reads the dye field, so dark% still means "empty water"; drops obey it.
- Colour source: `drops.colorMode=1` routes the drop through `PickSplatColor`-style wheel hue so
  `[color]` hue band/wheel + chroma>0 give coloured inks; mode 0 = fixed `[drops] color`.
- Post: `post_saturation/brightness/contrast=1`, `post_hue=0`, `shadow_floor=0`, `curve_enabled=0`,
  `shading=0`, `hdr compensation=0`, `saturation_restore=0`, `decay_fast=1`.

### 2.5 Settings window
Page "Ink in water" (slider format = existing `SliderDef`): Ink density k (0.5–8), Ink chroma (0–3),
Edge darkening (0–1), Edge lo/hi (0–0.5), Edge scale (1–6 px), Vignette (0–0.6), Core knee (0–1),
HDR core (0–1.4), Parallax (0–1), Parallax scale (0.8–1). Colour triplets ini-only (as acid does).
Checkboxes: "Ink look (restart)" `[look] ink`; "Inverted (ink on black)" `[ink] inverted` (live).
Page "Drops": Interval, Speed, Radius, Density, Tail s, Tail density, Spatter count/radius/speed,
Entry band y max; checkboxes "Ink drops" `[drops] drops`, "Drops obey governor".
Page "Simulation": Gravity (-200..200, 1 dp), Gravity power (0.5–3).
Page "Liquid Acid": checkbox "Ink-in-water under the oil" `[liquid_acid] ink_water`.
Restart-needed switch is only the `[look]` PSO choice; everything else is live (fields read per frame).

### 2.6 Drop injection and `--shot`
`InjectDrop(x_px, y_px, vx, vy, radiusPct, density, rgb, spatter)`: `Splat(x, y, vx, vy, rgb*density,
radiusPct)`; then `spatter` satellites: `Splat(x+r*cos, y+r*sin, 0.6*vy*dir, ..., rgb*density,
spatterRadius)` at `spatterSpread` uv, N ≤ 12 (each Splat is a full dye-res pass: keep N small, or
spread over 2–3 frames). `tailSec` keeps a dye-only splat (vx=vy=0, tailDensity*emitScale) at the
entry for that long. `UpdateDrops(dt)`: timer with jitter, seeded via `RandF()` (deterministic under
`--seed`), skip when `obeyGovernor && m_screenTooFull`.
Shot flags: `--shot-drop X,Y,T[,VY]` calls `InjectDrop` once at wallpaper time T (px, VY default
`drops.speed`) — the reproducible single-drop test. `--shot-pour X,Y,START,DUR` (existing, LMB held
with a 40 px circle) is the "sheets/veils from a pour" test. `--shot-series N:S` for time evolution.
Canonical drop shot (720p): `--shot build2/shots/ink-<step>.png --shot-size 1280x720 --ini <ini>
--seed 1234 --shot-drop 640,120,2 --shot-delay 4 --shot-series 4:3` → frames at t=4,7,10,13 s.
Paper judged on `.png` with `--hdr off`; inverted on `-hdr.png` with `--hdr on` and on the panel.

## 3. Iteration protocol (one variable per step, 1280x720 first, then 1920x1080)
GPU lock (AGENTS.md): before EVERY render wait until `build2/shots/gpu.lock` is absent (poll 5 s,
give up at 30 min), create it with agent name + time, render ONE shot process, delete it in a
`finally`/trap. Builds need no lock, build in `build2/` only. Never touch the user's
FluidWallpaper.exe or Wallpaper Engine. Sheets: `reference/configs/ab.py` (rows = times, cols =
variants), `refsheet.py` (ref crop next to a frame), `blind.py` for user picks (key in
`build2/shots/blindN-key.txt`).

| Step | Render (same seed 1234) | Pass by eye vs refs |
|---|---|---|
| 0 | Regression: `we-look-live.ini` 2560x1440 `--shot-delay 40 --hdr on` before the change (`regress-base`) and after every plumbing commit (`regress-after`); `md5` of `.png` AND `-hdr.png` | Identical md5. Fail = stop and fix before anything else. Repeat as the LAST step. |
| 1 | Render only, paper: `ink-paper.ini` = we-look-live + style=ink, gravity 0, drops off, wanderers ON (so there is dye), k = 1.5 / 3 / 6 | Thin trails grey and see-through, cores solid, paper white, no colour fringes (chroma 0), no blotchy banding. Pick k. |
| 2 | Drop alone (gravity 0, wanderers off): `--shot-drop`, speed 300 / 700 / 1400, series t=4,7,10,13 | A cap (dipole) forms and rolls, a trail hangs from the entry; not a flat disc (that was the pour bug at 512). |
| 3 | Gravity 0 / 10 / 30 / 100 at the chosen speed, `gravity_pow` 1.5 | Head keeps sinking (~1/3 screen in 5 s), thin veils stay up; no whole-field drift, no pile-up at the bottom within the drop's life. Then pow 1 / 1.5 / 2.5. |
| 4 | Edge darkening 0 / 0.35 / 0.7 (lo/hi fixed), then edge_scale 1 / 2 / 4 | Veils get a darker outline where they fold (ref 1 cap edges); no haloes on the paper, no doubled edges. |
| 5 | `dye_diffusion` 0 / 0.03 / 0.10 | Veil edges soften, cores and filaments stay crisp; no grey mush. |
| 6 | `sim_res` 256 vs 512 (SHOT-ONLY experiment, freeze rule untouched) | Report both to the user: 256 = curls/marbling, 512 = glassy sheets (ref 2). User decides; default stays 256. |
| 7 | Decay: `density_diffusion` 0.999 / 0.998 / 0.997, series 4:10 s | One drop visible 15–30 s, gone before the next few; no permanent floor deposit. |
| 8 | Drops scheduler + spatter: 120 s series every 20 s; spatter 0 / 6 / 12 | Screen never empty nor full; spatter reads as droplets (ref 2), not confetti. |
| 9 | Inverted: `ink-inverted.ini` (paper→black, tints), same drop, `--hdr on`, peak 700 knee 0.7, hdr_core 0.5 / 1.0 | `-hdr.png`: cores hot and small, veils pale and SDR; then the user judges on the panel (HDR on AND off). |
| 10 | Liquid Acid water: `liquid-acid-water.ini` = liquid-acid-a + ink_mode=water + drops on | Oil rims/meniscus still read; ink beneath is translucent water-ink instead of flat bands; no black smears under oil (NaN guard is in the shared code path — check chroma division). |
| 11 | 1920x1080 confirmation of the chosen set, t=60/90/120; blind A/B sheet of 2–4 finalists for the user | User picks; then panel check with HDR on and off. |
| 12 | Cost probe (section 5) | Under +1.5 ms/frame at 1440p. |

## 4. Risks and unknowns
- OLED/ABL with a light background: the paper variant is a full-frame white field. In HDR mode SDR
  white = ~240 nits (panel full-frame max ~418), in SDR mode paper = display white. It works, but it
  violates the user's "hot elements small" habit, costs panel brightness/burn-in, and ABL will make
  the paper dimmer than small highlights elsewhere on the desktop. Default paper 0.90 (~215 nits),
  vignette 0.15; expect the user to prefer INVERTED on the OLED. Judge on the panel, not PNGs.
- Grey ink vs `saturation_restore` (gotcha above): if the user wants sat_restore for WE feel AND
  neutral ink, it cannot be done in the dye field; choose per style ini.
- Gravity + reflective walls: dense dye reaching the bottom edge piles up (`ClampCoord`); decay must
  beat the fall time (step 7). If not enough, a follow-up is a bottom "drain" band (extra decay for
  uv.y > 0.95) in `CSAdvectDye` behind the same gravity!=0 guard.
- `sim_res` 512 may be what ref 2 needs; it is frozen at 256 by user directive. Report, don't switch.
- Hue cycler / moods on water ink in liquid_acid: water mode bypasses the CSS chain like bands mode
  (`ink_mix≈1`); the complement lock does not apply to water ink (it has no ramp). Decide with the user.
- `shading` (emboss) still runs in liquid_acid water mode via `ink_shading`; leave at 0.
- Determinism: `RandF()` for drop jitter keeps `--seed` reproducible; do not use `rand()` directly
  in a new order that shifts existing seeded sequences before the first frame (seed the drop timer
  after `InitWanderers`).
- NOT achievable in 2D (do not chase): true volumetric billowing — the cap rolling into a torus and
  shadowing itself, veils passing in FRONT of/behind each other, the 3D rim where a sheet folds over,
  depth-of-field, real refraction of the water, surface-tension splash physics. Edge darkening,
  parallax and spatter are the 2D stand-ins; if a step's "fail" is one of these, stop iterating.

## 5. Frame-cost budget (target < +1.5 ms/frame at 2560x1440)
| Piece | Estimate | Notes |
|---|---|---|
| INK display PS: 1 dye tap + 4 edge taps + exp | 0.2–0.4 ms | fluid path already does 5 taps with shading; INK skips the emboss |
| Gravity in `CSVorticity` | < 0.05 ms | 256² grid, 1 extra dye tap (5 if baroclinic off), branch-guarded |
| `dye_diffusion` at 4096 | 0.3–0.8 ms | EXISTING knob, the biggest item — one full dye-res 9-tap pass per frame; measure it separately |
| Drops | ~0 amortised | 2 full-res dispatches per drop every ~14 s; spatter N adds N×2, one-frame spike |
| Parallax (phase 2) | ~0.2 ms | 4 taps; off by default |
| InkCB upload/bind | ~0 | 112-byte root CBV ring like `m_acidParamUpload` |
Total expected +0.5–1.3 ms; if diffusion pushes it over, cap `dye_diffusion` in the ini, not the code.

Measurement = the Liquid Acid cost-probe method (WORKLOG 2026-09-16, `build2/shots/cost-*.png`):
`--shot build2/shots/cost-<name>.png --shot-size 2560x1440 --shot-delay 20 --hdr on --seed 1234
--ini <ini>` (default `--shot-yield 2`), read `[shot] done: 2880 frames simulated in N s wall` from
`%TEMP%\FluidWallpaper-shot.log`; `ms/frame = (N_variant - N_fluid) * 1000 / 2880`. Reference:
fluid 32.1 s, acid 35.1 s → +1.04 ms (upper bound: includes the 2 ms yield, CPU work and the headless
blocking readback). Run fluid / ink-paper / ink-inverted / ink-paper+diffusion / acid-water, each
twice, take the min, one process at a time under the GPU lock. Add the table to WORKLOG.

## 6. Deliverables checklist for the executor
1. Plumbing commit (config, ini, PSO, cbuffer, `DisplayPso`, shared block, `InjectDrop`, `--shot-drop`)
   + regression md5 (step 0). 2. Sim commit (gravity, drops scheduler) + regression. 3. Iteration
   shots a1..aN in `build2/shots/`, sheets, WORKLOG entry with per-step verdicts. 4. Inis in
   `reference/configs/` (`ink-paper.ini`, `ink-inverted.ini`, `liquid-acid-water.ini`), the cost table,
   AGENTS.md layout line for the ink style. 5. Blind sheet for the user + panel check request.
