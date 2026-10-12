# COLOR-AUDIT.md — why the DX12 port's colors diverge from the WE original

Investigation only. No code or config was changed by this audit.

Sources compared:
- `reference/script.js` (the WebGL sim, 1856 lines, with the author's custom mods)
- `reference/project.json` (workshop defaults)
- `WE-PRESETS.json` / `WE-LIVE-PROPERTIES.json` (ground truth: what WE actually ran)
- `src/shaders.h`, `src/fluid.cpp`, `src/fluid.h`, `src/main.cpp`, `src/moods.cpp`, `src/journey.cpp`
- `%APPDATA%\FluidWallpaper\settings.ini`, `presets\WE Original.ini`, `moods\*.ini`

---

## 0. Executive summary (read this first)

The port's *pipeline* is, with a handful of exceptions, a faithful translation.
**The port's colors are bad almost entirely because of configuration, not
translation** — and the dominant term is that **the mood conductor is running**
and its recipes are the opposite of the WE original in three ways at once:
neutral grey shadow lift, reduced post-saturation, and a narrow hue band.

Ranked by visual impact:

| # | Discrepancy | Impact |
|---|---|---|
| 1 | Mood conductor active; mood recipes desaturate + grey-lift + hue-lock | **Critical** |
| 2 | `shadow_floor = 0.10` is a *neutral grey* add — destroys the WE black-crush and desaturates everything dim | **Critical** |
| 3 | Hue band (`hue_center`/`hue_range` 15–35°) vs the reference's full 360° wheel | **Critical** |
| 4 | `gamut = 2` (BT.2020) vs the reference's Display-P3 | **High** |
| 5 | `post_contrast 1.27 / post_brightness 1.03` vs the correct WE-panel mapping 1.34 / 1.06 | **High** |
| 6 | `peak_nits = 955` HDR expansion — a post-clip gain the reference cannot have | **High** |
| 7 | `decay_fast = 0.614` vs WE's `1.0` — the persistent faint field is erased | **High** |
| 8 | `hdr saturation 1.44 / brightness 1.12 / contrast 0.98` vs WE's `1.20 / 1.08 / 1.00` | **Medium** |
| 9 | No clamp to [0,1] before the CSS-filter matrix (the WebGL canvas was 8-bit) | **Medium** |
| 10 | No clamp *between* CSS filter primitives (SVG/Filter-Effects spec requires it) | **Medium** |
| 11 | `wanderer_brightness` / `idle_brightness` ratio inverted in moods (0.30–0.72 / 0.5 vs 0.10 / 1.5) | **Medium** |
| 12 | Hue-band jitter scaling collapses `generateColor()`'s ±21.6° spread | **Low-Med** |
| 13 | `sim_res 512` (frozen) vs WE's 256 — structural, but changes dye geometry hence colour mixing | **Low-Med** |
| 14 | Coverage governor reads FP16 instead of 8-bit UNORM | **Low** |
| 15 | Non-issues confirmed correct (documented so nobody "fixes" them) | — |

---

## A. Ranked discrepancies with evidence and fixes

### A1 — CRITICAL: the mood conductor is on, and its recipes are anti-WE

**Evidence.**
`settings.ini` has `[cycle] enabled=1` and no `[moods]` section.
`src/moods.cpp:445-452` migrates exactly that case to `moods.enabled = true`:

```
GetPrivateProfileStringW(L"moods", L"enabled", L"", buf, 64, g_iniPath);
if (buf[0] == L'\0') {
    int oldCycle = GetPrivateProfileIntW(L"cycle", L"enabled", 0, g_iniPath);
    if (oldCycle) { g_moodSettings.enabled = true; ... }
}
```

`src/moods.cpp:463` picks `base_mood` = **Neon** by default; after
`dwell_minutes` (default 3, `moods.cpp:457`) the conductor lerps the *whole
look block* — including `postSaturation` (`moods.cpp:63`) and `shadowFloor`
(`moods.cpp:70`) — into the next mood. So within minutes of launch the running
colour config is a mood file, not `settings.ini` and certainly not WE parity.

What those mood files actually contain (`%APPDATA%\FluidWallpaper\moods\`):

| mood | post_sat | post_hue | hdr sat/bri | shadow_floor | hue_center ± range |
|---|---|---|---|---|---|
| Glacier | **0.50** | 0 | 1.00 / 1.00 | 0.140 | 195 ± 20 |
| Pluto | **0.55** | 0 | 1.00 / 1.00 | 0.120 | 22 ± 25 |
| Journey Stoner | **0.70** | 0 | 1.00 / 1.00 | 0.100 | 120 ± 30 |
| Clouds | **0.75** | 0 | 1.00 / 1.00 | 0.100 | 10 ± 18 |
| Verdant | **0.80** | 0 | 1.00 / 1.00 | 0.100 | 140 ± 35 |
| Abyss | **0.90** | 0 | 1.00 / 1.00 | 0.100 | 210 ± 30 |
| Neon | 1.70 | 14 | 1.20 / 1.08 | 0.100 | 66 ± 180 |
| **WE original** | **1.14** | **14** | **1.20 / 1.08** | **0 (absent)** | **full wheel** |

Nine of the fifteen moods actively **desaturate** (`post_saturation < 1.14`,
several below 1.0 → literally pulling toward greyscale), **drop the WE panel's
+14° hue rotation**, and **zero the reference's HDR compensation**
(`saturate(1.2) brightness(1.08)`, `script.js:289-299`).

**Symptom.** Muted, washed, slightly grey colours that slowly drift between
single-hue moods — exactly "the colours are bad".

**Fix.** Set `[moods] enabled=0` in `settings.ini` for any WE-parity comparison.
Longer term: `moods.cpp` should treat WE parity as the *base look* and express
moods as deltas that never go below `post_saturation 1.0`, or a "WE parity"
lock that pins `[color]` and `[hdr]` while moods drive only motion/behaviour.

---

### A2 — CRITICAL: `shadow_floor` is a neutral grey add; it annihilates the WE look

**Evidence.** `src/shaders.h:341-344`:

```hlsl
if (shadow.x > 0.0001) {
    float lum = max(C.r, max(C.g, C.b));
    C += shadow.x * (1.0 - smoothstep(0.0, max(shadow.y, 0.001), lum));
}
```

Nothing like this exists anywhere in `reference/script.js`. It was added
deliberately (WORKLOG.md:63-76, calibration answer `shadow_floor = 0.10`,
`CALIBRATION.md:15`), and every mood file bakes `shadow_floor=0.100`–`0.140`.

**Why it is so destructive here.** The WE original's entire look depends on a
hard black crush. Trace a fully-saturated single-channel dye value `v` through
the *reference* chain with HDR on (`script.js:289-296` canvas filter, then WE's
right panel `wec_sa 57 / wec_brs 53 / wec_con 67 / wec_hue 54`):

```
saturate(1.2)   max-channel gain 0.213 + 0.787*1.2      = 1.15740
brightness(1.08)                                        × 1.08   → 1.25000 v
contrast(1.0)                                           identity
WE saturate(1.14) 0.213 + 0.787*1.14 = 1.11018          → 1.38773 v
WE brightness(1.06)                                     → 1.47099 v
WE contrast(1.34)  y = 1.34x − 0.17                     → 1.97113 v − 0.17
```

→ **every dye value below v = 0.0863 renders as literal black.**

That is the mechanism: a wanderer stamp peaks at `0.15 × wanderer_brightness
0.10 = 0.015`, far under the floor, so individual strokes are invisible; dye
only "ignites" where repeated passes have piled it above ~0.086. Vast true
black, with colour appearing only where the fluid has actually accumulated.

`shadow_floor = 0.10` adds a flat `+0.10` to **all three channels** below the
knee. Consequences:
- The crush is gone: the sub-threshold field, which was invisible, now shows as
  grey marbling over the whole screen.
- Full-screen floor: gamma 0.10 → linear 0.01003 → × SDR-white 240 nits ≈
  **2.4 nits of neutral grey across 100 % of a QD-OLED** that was showing 0.
- It desaturates: a dim red at code (0.05, 0, 0) becomes (0.15, 0.10, 0.10) —
  saturation falls from 100 % to 33 %.

**Fix (two parts).**
1. For parity, `shadow_floor = 0`.
2. If the user still wants dark-region marbling (they chose it in a quiz, so
   they probably do), make the lift **chroma-preserving** instead of neutral.
   Replace the `C +=` at `shaders.h:341-344` with a proportional lift that
   scales the pixel's own colour up to the floor rather than adding grey:

```hlsl
if (shadow.x > 0.0001) {
    float lum = max(C.r, max(C.g, C.b));
    float lift = shadow.x * (1.0 - smoothstep(0.0, max(shadow.y, 0.001), lum));
    // scale toward the floor along this pixel's own hue; grey only where the
    // pixel is genuinely colourless
    C *= (lum + lift) / max(lum, 1e-4);
}
```
   That keeps R:G:B ratios (hue + saturation intact) and still raises dark
   marbling off the floor. Pixels that are exactly 0 stay 0 — which is
   *correct*, since there is no marbling to reveal in genuinely empty fluid.

---

### A3 — CRITICAL: hue band vs the reference's full wheel

**Evidence.** Reference: `globalHue` advances monotonically and
`cycledColor()` (`script.js:1200-1207`) takes the **full** wheel with a fixed
per-wanderer offset `i * 0.13` (`script.js:1236`). There is no band concept at
all in `script.js`.

Port: `FluidRenderer::WheelHue` (`src/fluid.cpp:1197-1219`) maps the wheel into
`hueCenter ± hueRange`:

```cpp
float rangeFrac = m_cfg.hueRange / 180.0f;
if (rangeFrac >= 0.999f) return m_globalHue + offset - cmd;       // WE parity
float tri = 1.0f - fabsf(2.0f * m_globalHue - 1.0f);
...
return m_cfg.hueCenter / 360.0f + (tri - 0.5f) * rangeFrac + offset * rangeFrac - cmd;
```

`hue_range = 180` is the only WE-parity value. The mood files use 15–35
(`Clouds` 18, `Ember Giant` 15, `Glacier` 20, `Pluto` 25). Even
`moods\WE Original.ini` — which is *named* for parity — sets `hue_center=25`,
`hue_range=28`.

**Symptom.** The whole screen sits in one hue family and oscillates inside a
±18° slice instead of cycling the spectrum every 19 s. This is the most
immediately visible difference from the original.

**Fix.** `hue_center=0`, `hue_range=180`, `hue_linger=0` for parity; fix
`moods\WE Original.ini` (it is misnamed — it is not a parity recipe; see also
its `max_brightness=0.90` vs 1.35 and `color_cycle_period=45` vs 19).

---

### A4 — HIGH: `gamut = 2` (BT.2020) vs the reference's Display-P3

**Evidence.** Reference sets the *drawing buffer* colour space to Display-P3
when `WIDE_GAMUT` is on (`script.js:372-379`):

```js
if ('drawingBufferColorSpace' in gl) {
    var want = config.WIDE_GAMUT ? 'display-p3' : 'srgb';
```
and `wide_gamut` is `true` in `project.json` and in the `fire1` / `banger 1`
presets. So the dye values are interpreted as **Display-P3**, i.e. exactly what
`shaders.h:358-362` calls "Display-P3 — parity with the WE original".

Port default is `gamutMode = 2` (`src/fluid.h:29`) and the live
`settings.ini` has `gamut=2`; `moods\Neon.ini` and `moods\Preset 4.ini` also
force `gamut=2`.

**Quantitatively.** A pure green dye through the two matrices
(`shaders.h:353-363`):

| | R | G | B |
|---|---|---|---|
| P3 → 709 | −0.2249 | 1.0421 | −0.0787 |
| BT.2020 → 709 | **−0.5876** | **1.1329** | −0.1006 |

BT.2020 pushes primaries 2.6× further outside sRGB. On the X27U (≈P3 coverage,
not BT.2020) those excursions are outside the panel and get clipped by the
display's own gamut mapping → **hue rotation in the most saturated colours**,
which reads as "wrong colours" rather than "more colours".

Worse, **with Windows HDR OFF** the FP16 scRGB swap chain
(`fluid.cpp:161`, `fluid.cpp:173-177`) is composited by the DWM into SDR, where
negative components are hard-clamped to 0. Clamping −0.5876 → 0 *raises*
luminance and *lowers* saturation asymmetrically per hue. This directly
violates the AGENTS.md "must look good with HDR on AND off" constraint: the two
modes will not match each other, let alone the reference.

**Fix.** `gamut=1` everywhere for parity. If BT.2020 is kept as a taste option,
it should be soft-clipped toward the P3 hull before output, not fed raw.

---

### A5 — HIGH: the WE right-panel mapping was derived correctly once, then drifted

**The mapping.** WE stores `wec_sa/con/brs/hue` on 0..100 with 50 neutral. The
only self-consistent mapping that makes 50 the identity and 0/100 the natural
endpoints is:

```
saturate   = wec_sa  / 50          57 → 1.14
brightness = wec_brs / 50          53 → 1.06
contrast   = wec_con / 50          67 → 1.34
hue-rotate = (wec_hue − 50) * 3.6  54 → +14.4°
```
(Brightness 0 → black and saturation 0 → greyscale both require the `/50`
form; hue must be `(v−50)·3.6` because only that makes 50 the identity, and it
gives ±180° at the endpoints.)

The port **already derived exactly this** — `presets\WE Original.ini` contains
`post_saturation=1.14`, `post_contrast=1.34`, `post_brightness=1.06`,
`post_hue=14`, and WORKLOG.md:45-46 records the change as "(WE author panel
values)". So the mapping is settled and correct.

**But the live `settings.ini` has drifted** to `post_contrast=1.27`,
`post_brightness=1.03` (`[color]` section). That is not a different mapping,
just drift.

**Why 1.27 vs 1.34 matters a lot.** The contrast offset is the black-crush
knob. Crush point in gamma space is `0.5(c−1)/c`:
- `c = 1.34` → 0.1269 (WE original)
- `c = 1.27` → 0.1063

Carried back through the rest of the chain, the **dye** crush threshold is
`0.0863` for WE parity vs `0.0604` for the live port (computed below). The port
therefore keeps ~40 % more of the faint field visible as dim colour — which is
precisely the "muddy instead of inky" complaint — and then the shadow floor
(A2) removes the crush entirely.

**Fix.** `post_saturation=1.14`, `post_brightness=1.06`, `post_contrast=1.34`,
`post_hue=14.4` (14 is close enough; 14.4 is exact).

---

### A6 — HIGH: `peak_nits` HDR expansion is a post-clip gain with no analogue in the reference

**Evidence.** `src/shaders.h:365-372`:

```hlsl
float m = max(C.r, max(C.g, C.b));          // line 347, BEFORE the clamp
float3 lin = SRGBToLinear(saturate(C));      // line 349, clamp happens here
...
if (peakGain > 1.001) {
    float t = smoothstep(knee, max(capBright, knee + 0.01), m);
    gain = lerp(1.0, peakGain, t * t);
}
return float4(lin * sdrScale * gain, 1.0);
```

with `peakGain = peakNits / (80 * sdrScale)` (`fluid.cpp:683-687`).

The WE original is an SDR web wallpaper: with HDR on, Windows maps its white to
the SDR white level (240 nits here) and that is the ceiling. Nothing can exceed
it. `peak_nits` is therefore a pure addition, not a port of anything.

**With the live ini** (`peak_nits=955`, `knee=1.22`, `max_brightness=1.30`,
SDR white 240): `peakGain = 955/240 = 3.98`, applied over
`smoothstep(1.22, 1.30, m)`.

Two problems:
1. `m` is measured **before** `saturate(C)`, and `knee = 1.22 > 1.0`. So the
   gain only ever fires on pixels that are **already clipped**. It cannot
   recover highlight detail; it just multiplies flat, clipped pixels by ~4×.
   Result: hard-edged, internally flat blobs at ~955 nits with no gradient —
   the opposite of the reference's soft compressed cores.
2. Channels clip independently in `saturate()`, so a core whose channels clip
   at different rates **shifts hue toward white** before the gain, and then the
   gain makes that white-shifted region 4× brighter. On a 418-nit-full-field
   ABL panel, large hot areas then trigger ABL and dim the *entire* image —
   which reads as "colours went dull".

**Fix.** `peak_nits=0` for parity (this is what `presets\WE Original.ini`
already does). If HDR headroom is wanted later, the correct shape is a *soft
shoulder applied before the clamp*, e.g. compute the gain from `m` and roll the
knee below 1.0 (`knee ≈ 0.7`, `cap = maxBrightness`) so the expansion replaces
the clip instead of following it — and clamp the result to the panel's real
small-window capability, not `desc.MaxLuminance`.

---

### A7 — HIGH: `decay_fast = 0.614` destroys the persistent faint field

**Evidence.** WE live value is `decay_fast = 1` (WE-LIVE-PROPERTIES.json, and
`fire1`/`banger 1`/`preset 1` all agree), with `decay_threshold = 0.29`.

In `script.js:806-812` / `shaders.h:187-189`:
```
k = mix(dissipationFast, dissipation, smoothstep(0.0, decayThreshold, lum));
```
With `dissipationFast = 1.0`, dye **below 0.29 does not decay at all**. The
two-tier decay is *inverted* from the shipped default (0.90): the author made
the faint tail immortal. That is what lets 0.015-peak wanderer strokes
accumulate over minutes until they cross the 0.086 black-crush and ignite.

The live `settings.ini` has `decay_fast=0.614`. At ~120 steps/s that is
`0.614^120 ≈ 10⁻²⁵` per second — the faint tail is erased instantly. The
accumulate-then-ignite mechanism is gone; only fresh bright splats show.
`fluid.h:24-26` even documents the trap ("0.90 starved the field to black").

**Fix.** `decay_fast=1.000`, `decay_threshold=0.290`,
`density_diffusion=0.999` (live ini has 0.9997, a compensation for the wrong
`decay_fast`), `saturation_restore=0.93`.

---

### A8 — MEDIUM: HDR compensation values drifted

Reference: `hdr_saturation 1.2`, `hdr_brightness 1.08`, `hdr_contrast 1.0`
(`script.js:65-67`, confirmed by `project.json` and the `fire1` preset, where
only `hdr_contrast=1` is explicitly stored and the other two sit at defaults).

Live `settings.ini` `[hdr]`: `saturation=1.44`, `brightness=1.12`,
`contrast=0.98`. Compounded with the WE panel filter this is a ~20 % extra
saturation push and a slight *negative* contrast (0.98 → offset +0.01, a small
black lift that works against A2's crush in the same direction as the shadow
floor).

Also note the gating is correct and worth preserving: `hdrCompensation` is
applied only when `m_hdrActive` (`fluid.cpp:666-671`), matching
`HDR_MODE='auto'` (`script.js:290`), while the WE panel filter is applied
unconditionally (`fluid.cpp:673-681`) — correct, since WE's panel applies in
both modes. **No double-application was found**; the two are distinct stages,
composed in the right nesting order (panel outermost).

**Fix.** `saturation=1.20`, `brightness=1.08`, `contrast=1.00`.

---

### A9 — MEDIUM: no clamp to [0,1] before the CSS-filter matrix

**Evidence.** `src/shaders.h:326` applies the filter matrix to raw dye:

```hlsl
C = float3(dot(fm0.xyz, C), dot(fm1.xyz, C), dot(fm2.xyz, C)) + fmOff.xyz;
```
and the only clamp is at line 349 (`saturate(C)`), *after* filter + curve +
shadow.

In the reference the filter's input is the composited canvas, which is an 8-bit
sRGB/P3 surface: `render()` blits with `gl.blendFunc(ONE, ONE_MINUS_SRC_ALPHA)`
over an opaque black clear (`script.js:1531-1546`), so the pixel that reaches
`canvas.style.filter` is **hard-clamped to [0,1]** (and quantised to 8 bits).

Dye routinely exceeds 1.0: the splat cap is `MAX_BRIGHTNESS = 1.35`
(`script.js:770`, `shaders.h:254-255`), and `multipleSplats` paints at 1.5
peak. So in the original, everything in [1.0, 1.35] was flattened to 1.0
*before* saturate/contrast; in the port those values survive into the matrix.

**Symptom.** Splat cores are more saturated and hotter in the port than in the
original, and (with `peak_nits` on) they are the pixels that get the 4× gain.

**Fix.** Insert `C = saturate(C);` immediately before line 326 of
`shaders.h`, i.e. between the shading block and the filter matrix.

---

### A10 — MEDIUM: no clamp *between* CSS filter primitives

**Evidence.** `fluid.cpp:651-681` collapses `saturate → brightness → contrast →
hue-rotate` into one 3×3 matrix plus a scalar offset. The composition itself is
algebraically correct (all four map grey→grey, so the contrast offset stays
scalar and survives the hue-rotate unchanged — verified). But it silently drops
the per-primitive clamp.

Per Filter Effects / SVG, each filter primitive's result is clamped to the
allowed range before the next consumes it. That matters enormously for
saturated colours, because `saturate(s>1)` drives the two minor channels
**negative**, and the reference clamps them to 0 before `contrast` and
`hue-rotate` can feed them back into the major channel.

**Worked example — a pure-red dye splat at v = 0.15, HDR on.**

Reference (clamping at each stage):
```
0.15 → saturate(1.2)   → 0.17361  (g,b = −0.0064 → 0)
     → brightness(1.08)→ 0.18750
     → saturate(1.14)  → 0.20816  (g,b → 0)
     → brightness(1.06)→ 0.22065
     → contrast(1.34)  → 0.12567  (g,b: −0.17 → 0)
     → hue-rotate(14.4°) row0 gain 0.92510 → 0.11591
```
Port, live ini (no intermediate clamps, `sat 1.44 / bri 1.12 / con 0.98`,
`post 1.14 / 1.03 / 1.27 / 14`):
```
after HDR stage : ( 0.231652, −0.005430, −0.005430)
after post sat  : ( 0.257776, −0.012500, −0.012532)
× 1.03*1.27     : ( 0.337197, −0.016351, −0.016390)
+ (0.5·(1−1.27)): ( 0.202197, −0.151351, −0.151390)
after hue 14°   : ( 0.180871, −0.136882, −0.216465)
saturate()      : ( 0.180871,  0,         0       )
```
**0.1159 vs 0.1809 in code value → ≈ 2.9× in linear light.**
(3.69 nits vs 10.95 nits at SDR-white 240 with each config's own gamut matrix.)

Note how the carried-through negatives *add* to red through the hue-rotate
matrix (`+0.0230` from g, `−0.0291` from b), i.e. the missing clamps are not a
rounding detail — they change the answer.

Affine form of each chain in `v` (pure single-channel dye):
- reference: `1.97113 v − 0.17`, then ×0.92510 → **black below v = 0.0863**
- port live: `2.01861 v − 0.121919` → **black below v = 0.0604**

**Fix.** Either (a) evaluate the four primitives sequentially in the pixel
shader with a `saturate()` between each — cheap, four extra clamps — or (b)
keep the single matrix but add one `saturate()` after the saturate/brightness
sub-chain and before the contrast offset, which captures the dominant term.
(a) is the faithful option. Caveat in section C.

---

### A11 — MEDIUM: wanderer / burst brightness ratio inverted in the moods

Reference balance (WE live values): `wanderer_brightness = 0.10` and
`multipleSplats` colours scaled ×10 (`script.js:1659-1662`), i.e.

- wanderer stamp peak dye = `0.15 × 0.10` = **0.015**
- idle-burst stamp peak dye = `0.15 × 10` = **1.5**
- ratio **1 : 100**

Port defaults get this right — `fluid.h:38 idleBrightness = 1.5f`,
`fluid.h:76 wandererBrightness = 0.1f`, and `MultipleSplats`
(`fluid.cpp:591-609`) uses `HSVtoRGB(h,1,1) × idleBrightness` = 1.5 peak. ✔

But the moods override both: `wanderer_brightness = 0.30` (Neon: **0.72**) and
`idle_brightness = 0.5`. Ratio becomes 1 : 1.7 (Neon: 1 : 0.7 — the "accent"
bursts are *dimmer* than the continuous painter). Live `settings.ini` has
`wanderer_brightness=0.17` and `idle_amount=5`.

**Symptom.** No sparkle/accent hierarchy; a uniformly-lit field where
everything is the same brightness, which reads as flat and muddy because
overlapping mid-brightness dye of different hues mixes toward white/brown
instead of staying as dark field + bright cores.

**Fix.** `wanderer_brightness=0.10`, `idle_brightness=1.50`, `idle_amount=8`,
`idle_interval=9.6`.

---

### A12 — LOW-MED: the hue band also shrinks `generateColor()`'s jitter

`script.js:1757-1758`: `h = (globalHue + (Math.random() − 0.5) * 0.12)` — a
fixed ±0.06 turn = **±21.6°** spread inside each burst, which is what keeps a
10-splat burst from being monochrome.

Port `fluid.cpp:597` routes that jitter through `WheelHue`, which multiplies
*offsets* by `rangeFrac` (`fluid.cpp:1218`: `offset * rangeFrac`). With
`hue_range = 18` the ±21.6° spread collapses to **±2.2°**, so every splat in a
burst is the same colour. Same applies to the wanderers' `i * 0.13` (46.8°)
separation → 4.7°.

**Fix.** With `hue_range=180` this is moot (`rangeFrac = 1`). If bands are kept,
consider not scaling `offset` by `rangeFrac`, or scaling it by
`sqrt(rangeFrac)` so bursts keep some internal variety.

---

### A13 — LOW-MED: `sim_res 512` vs WE's 256

`settings.ini` has `sim_res=512`, frozen per AGENTS.md; WE ran `256`
(`project.json`, all presets). The worklog already found the compensation
(vorticity 48 → ~24, WORKLOG.md:63-66). This is a motion/geometry divergence,
not a colour-pipeline one, but it changes how dye masses merge and therefore
how hues mix. Note the conflict: `presets\WE Original.ini` sets `sim_res=256`,
which contradicts the AGENTS.md freeze. Flagging, not resolving.

---

### A14 — LOW: coverage governor reads FP16 where the reference read 8-bit UNORM

Reference `measureCoverage()` (`script.js:1292-1300`) blits into an
`RGBA / UNSIGNED_BYTE` FBO, so values are clamped to [0,1] and quantised before
`DARK_LEVEL (0.07)` is tested. Port `ProcessCoverage` (`fluid.cpp`, the
`HalfToFloat` loop) tests raw FP16. For the dark test the difference is a
fraction of a code value; the auto-pause trip point shifts slightly, which
changes how full the screen gets and hence the average colour density. Low
priority.

---

### A15 — Confirmed NON-issues (do not "fix" these)

- **Shading `dy` sign.** `shaders.h:320` uses `dy = length(B) − length(T)` where
  the port's `B` is `+y` (screen-down) and the reference's `vT` is `+y`
  (GL, screen-up), so the port's `dy` is the negated reference `dy`. **It does
  not matter**: `l = (0,0,1)` so `dot(n,l) = n.z = texelLen / |(dx,dy,texelLen)|`
  depends only on the gradient *magnitude*. Both produce identical output.
  (`script.js:664-670` vs `shaders.h:319-323`.)
- **sRGB decode.** `SRGBToLinear` at `shaders.h:306-310` is correct and
  necessary: the WebGL canvas values were electrical (gamma-encoded) code
  values, and the port's swap chain is linear scRGB. Removing it would be wrong.
- **CSS filters in gamma space.** Correct — CSS shorthand filter functions run
  with `color-interpolation-filters: sRGB`, which the port's comment at
  `shaders.h:271-273` already notes.
- **Filter nesting order.** `fluid.cpp:664-681` puts the WE panel filter
  outermost over the canvas HDR filter over the hue-rotate burst. That matches
  reality (WE composites the page, then applies its panel adjustment).
  The scalar-offset handling through `CssHueRotate` is also correct because
  hue-rotate maps grey → grey.
- **Decay frame-rate normalisation.** `fluid.cpp:494 stepsRef = dt * 120.0f` is
  right. The reference's `update()` (`script.js:1410-1418`) runs
  `while (rem > STEP_SIZE_S) step(0.016)` then a final partial step, and applies
  `c *= k` **once per step regardless of dt**. At WE's 60 fps that is exactly
  2 steps/frame = **120 steps/s** (and coincidentally also 120 at 120 fps).
  Minor caveat: the port raises each tier *before* the lerp
  (`lerp(fast^n, slow^n, s)`) where the reference lerps then applies
  (`lerp(fast, slow, s)^n`). With `decay_fast = 1.0` the two agree to within a
  fraction of a percent; only with a small `decay_fast` (like the live 0.614)
  does it drift, and then only slightly.
- **`SAT_RESTORE` semantics.** Reference `script.js:1526` passes
  `1 − (1−S)^dt` per step; because the steps sum to real time, the composition
  is exactly `(1−S)^T` per second — genuinely per-second. `fluid.cpp:544` does
  the same. ✔
- **Emission frame-rate normalisation.** `fluid.cpp:991`
  `m_emitScale = clamp(dt*60, 0.25, 2.0)` correctly compensates for wanderers
  firing once per frame at 144 fps instead of 60. Without it the field would be
  2.4× hotter. Note `MultipleSplats` deliberately does *not* apply it
  (`fluid.cpp:605-608`) — correct, bursts are on a wall-clock timer.
- **Dye format.** `DXGI_FORMAT_R16G16B16A16_FLOAT` + linear sampler
  (`fluid.cpp:376-377`) matches the reference's `RGBA16F` +
  `OES_texture_float_linear`. ✔
- **Splat maths.** `exp(-dot(p,p)/radius) * color`, `radius = SPLAT_RADIUS/100`,
  proportional cap `c *= cap/max(m,cap)`: verbatim
  (`script.js:751-770` ↔ `shaders.h:246-257`, `fluid.cpp:557-560`).
- **`generateColor` / `cycledColor` 0.15 scaling** — present in both
  (`script.js:1200-1207` ↔ `fluid.cpp:92-96`).
- **Hue-shift cycler uses the CSS hue-rotate matrix.** `fluid.cpp:60-68` uses
  the W3C matrix, matching the reference's `canvas.style.filter` `hue-rotate()`.
  That matrix is *not* saturation-preserving, but since the reference had the
  same artefact at the same 83° steps, keeping it is parity. ✔
- **Emission counter-rotation** (`fluid.cpp:1202-1203`) is a port addition tied
  to the mood bridge, inactive when `m_hsCommanded` is false — no parity impact
  with moods off.
- **Response curve** (`shaders.h:331-337`) — a port-only Gaussian-hump remap
  with no reference analogue. Currently **off**: absent from `settings.ini`
  (default `false`, `fluid.h:46`) and `curve_enabled=0` in every mood. Not
  contributing today, but it is a live landmine — `main.cpp:828` writes a
  `curve_enabled=1` stub for one seeded mood. Keep it off for parity.

---

## B. Recommended "WE parity" ini

Derived from `WE-LIVE-PROPERTIES.json` (the `Fluid Custom` / Monitor1 block,
identical to the `fire1` preset) + `project.json` defaults for the keys WE did
not override + the `wec_* / 50` panel mapping. Values that `presets\WE
Original.ini` already has right are marked ✔; **bold** = differs from the
current live `settings.ini`.

```ini
[moods]
enabled=0                      ; ← the single most important line (A1)

[hdr]
peak_nits=0                    ; **  live 955 — A6
compensation=1                 ; ✔
saturation=1.20                ; **  live 1.44 — A8
brightness=1.08                ; **  live 1.12
contrast=1.00                  ; **  live 0.98
knee=0.60                      ; **  live 1.22 (inert once peak_nits=0)
gamut=1                        ; **  live 2 — A4 (Display-P3 = WE parity)

[sim]
sim_res=512                    ; AGENTS freeze; WE ran 256 — see A13
dye_res=4096                   ; ✔
density_diffusion=0.999        ; **  live 0.9997
velocity_diffusion=0.999       ; **  live 0.9980
pressure_diffusion=0.85        ; **  live 0.990
pressure_iterations=20         ; ✔
vorticity=24                   ; 48 at sim_res 256; halve for 512 (WORKLOG iter 3)
splat_radius=0.64              ; **  live 0.325 — halves every stamp
decay_fast=1.000               ; **  live 0.614 — A7, biggest sim-side error
decay_threshold=0.290          ; **  live 0.272
saturation_restore=0.93        ; **  live 0.915
max_brightness=1.35            ; **  live 1.30
shading=1
dye_diffusion=0.000
baroclinic=0.000
flow_speed=1.00

[behavior]
color_cycle_period=19          ; **  live 29
wanderers=1
wanderer_count=2
wanderer_mode=0                ; random
wanderer_speed=246             ; **  live 330
wanderer_brightness=0.10       ; **  live 0.17 — A11
wanderer_scale=0.10
wanderer_resume_delay=4.5
auto_pause=1
dark_floor=9
dark_level=0.070
surv_dark_floor=8
contrast_req=30                ; **  live 24
dart_enabled=1
dart_interval=7                ; **  live 6
dart_speed=967                 ; **  live 2200
hueshift_enabled=1
hueshift_step=83               ; **  live 48
hueshift_linger=6.5            ; **  live 4.5
hueshift_glide=7.2             ; **  live 6.0
hueshift_burst_steps=2         ; **  live 3
hueshift_off_time=10           ; **  live 7
idle_splats=1
idle_interval=9.6              ; **  live 9.5
idle_amount=8                  ; **  live 5
idle_brightness=1.50           ; (absent from live ini → already 1.5 by default)
hold_to_splat=1
splat_on_click=1
show_mouse=1

[color]
colorful=1                     ; ✔
more_colors=1
post_saturation=1.14           ; ✔  wec_sa 57 / 50
post_brightness=1.06           ; **  live 1.03 — should be wec_brs 53 / 50
post_contrast=1.34             ; **  live 1.27 — should be wec_con 67 / 50 — A5
post_hue=14.4                  ; **  live 14   — (wec_hue 54 − 50) × 3.6
hue_center=0                   ; **  A3
hue_range=180                  ; **  A3 — full wheel
hue_linger=0
curve_enabled=0
shadow_floor=0.000             ; **  A2 (moods force 0.10)
shadow_knee=0.15
; splat_color_* are irrelevant while colorful=1; if ever used, WE's were
; 0 1 0 / 0 1 1 / 0 0 1 / 1 0 0 / 0.9412 1 0 — the live ini has replaced them

[general]
fps_limit=60                   ; safest parity (see C3); 144 is OK given m_emitScale
mirror_second=0
```

Code changes needed for full parity (all in the display pass):
1. `shaders.h:326` — `C = saturate(C);` before the filter matrix (A9).
2. `shaders.h:341-344` — chroma-preserving shadow lift (A2), or leave the
   feature off.
3. Optional/faithful: clamp between the four filter primitives (A10).

`presets\WE Original.ini` is already ~90 % of the above; its gaps are the keys
it does not mention, which — per the AGENTS.md "ini overlays are partial" rule —
**keep the live drifted values**: `shadow_floor`, `curve_*`, `hue_center`,
`hue_range`, `hue_linger`, `dye_diffusion`, `baroclinic`, `flow_speed`,
`idle_brightness`, `[moods] enabled`. Loading that preset today therefore
*cannot* restore parity. Adding those keys to it is the cheapest single fix
in this whole report.

---

## C. Open questions I could not resolve from code alone

**C1 — WE's actual `wec_*` shader.** I inferred the `/50` and `(v−50)×3.6`
mapping from first principles (endpoint behaviour + 50 = identity) and it is
corroborated by the port's own earlier derivation. But I could not verify from
any artifact:
- whether WE's contrast pivots at 0.5 (the CSS/`y = cx + 0.5(1−c)` form) or at
  the image's mean, or uses an S-curve;
- the **order** WE applies them in (the port assumes
  saturate → brightness → contrast → hue, the CSS order). Order changes the
  result: contrast-then-brightness gives a different black point than
  brightness-then-contrast.
- whether WE's saturate uses Rec.709 luma (0.213/0.715/0.072, as CSS does and
  as `fluid.cpp:69-75` implements) or Rec.601 (0.299/0.587/0.114). This shifts
  hue slightly on saturated colours.
Resolving this needs either WE's shader or an empirical capture: put a known
test pattern in a WE web wallpaper, screenshot with `wec_e` off and on, and
solve for the transfer curve.

**C2 — Does Chromium actually clamp between filter primitives?** The SVG /
Filter Effects spec says each primitive's result is clamped, and Blink builds
`saturate`/`hue-rotate` as `FEColorMatrix` and `brightness`/`contrast` as
`FEComponentTransfer`. But Skia is known to *fuse* consecutive colour filters
into a single matrix as an optimisation, which would remove the intermediate
clamps and make the port's current single-matrix approach the accurate one.
A10's 2.9×-in-linear delta hinges on this. **A9's input clamp is not in doubt
and should be fixed regardless.** Empirical check: render a known >1-ish and a
saturated colour in a filtered canvas in the same Chromium build WE uses and
read back the composited pixel.

**C3 — What FPS was WE running at?** `FRAME_INTERVAL_MS` comes from WE's
general `fps` property (`script.js:216-218`), which is not stored in
`project.json` or in the extracted property blocks. The decay normalisation
(`stepsRef = dt*120`) is exactly right for 60 fps and also for 120 fps, but at
30 fps the reference ran **90** steps/s and at 144 fps **144** steps/s. If the
user's WE FPS was 30, every decay in the port is ~33 % too fast at equilibrium.
Ask the user, or read WE's global `fps` setting from its config.

**C4 — `comp_knee`, `hue_equalize`, `hue_hold`, `sat_target`, `white_purge`.**
These appear in `WE-LIVE-PROPERTIES.json` (values 0.7, 0.34, 1.6, 1, 0.37) but
**`reference/script.js` does not reference any of them** (verified by grep for
both the property name and every plausible `config.` spelling). They are
therefore inert with the current script — *but their presence means the user
had, at some point, a revision of the wallpaper with a `white_purge` /
`hue_equalize` / `sat_target` colour stage that `script.js` no longer has*.
If the "awesome" memory is of *that* build, matching `script.js` will not
reproduce it. Names like `white_purge` (removing desaturated/white dye) and
`hue_equalize` (flattening hue distribution) describe exactly the kind of
"colours stay pure, never wash to white" behaviour the workshop description
advertises. **Worth asking the user whether `reference/script.js` is the
version they loved, and whether an older copy of it exists** (WE keeps no
history, but Steam Workshop update history or a backup might).

**C5 — Whether the user wants parity or parity-plus.** Several divergences
(shadow floor, response curve, `peak_nits`, BT.2020) were *deliberate*, chosen
by the user in the calibration quizzes recorded in `CALIBRATION.md`. This
report treats them as divergences because the brief is "match the original",
but A2's shadow floor in particular was an explicit quiz answer. The
chroma-preserving lift in A2 is offered as the way to keep that feature without
paying its colour cost.

**C6 — Which mood was on screen when the user judged the colours "bad".** The
conductor was cycling, so the answer differs by mood (Glacier at
`post_saturation 0.50` is a very different picture from Neon at 1.70). If the
complaint is about a specific look, identifying the mood would sharpen the
priority order — though A1/A2/A3 apply to all of them.
