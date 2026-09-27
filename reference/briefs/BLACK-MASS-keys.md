# Black-mass layouts: key study (read-only, no build/render run)

Scope: `[liquid_acid]` in `src/fluid.cpp`/`fluid.h`, tooltips in
`src/ui/keys.inc`, display PS in `src/shaders.h`. Line refs = this checkout
(main).

## 1. Population model

**Seeding — `SeedAcidBlobs()` (fluid.cpp:3159-3265)**, runs on app start or
when `blob_count` changes with `conserve_mass=0`. `n=blob_count` splits into
`nDisc=n*disc_frac`, `nWeb=n*web_frac`, `nBubble=n*bubble_frac`
(fluid.cpp:3166-3168); **`nHole = n - nDisc - nWeb - nBubble`** — holes are
the remainder, there is no `hole_frac` key (3171).
`drawR(lo,hi,bias)=lo+(hi-lo)*pow(rng(),max(bias,0.05))` (3179-3181): `bias<1`
skews to the **large** end, `>1` to the **small** end. Discs/webs use
`big_bias` against `disc_min/max`/`web_min/max` (3195,3205,3312-3313);
bubbles/holes use `size_bias` against `bubble_min/max`/`hole_min/max`
(3222,3237,3314,3317). Webs are seeded as a **random walk of touching
blobs** (3199-3218) — that chaining is what produces connected veins/lobes,
not independent placement. Holes are **negative weight**
(`b.wgt=-max(hole_weight,0.05)`), placed inside a disc/web parent, sub-scaled
to `parent.baseR*rng(0.20,0.55)` (3234-3250). **`b.wgt` is fixed at creation
and never re-read from `hole_weight` afterward** — changing `hole_weight`
live only affects newly-created holes.

**`blob_count` live — `AdjustAcidBlobCount()` (3276-3335)**, gated every
frame at 1956-1977: `conserve_mass<=0.5` → any `blob_count` change calls
`SeedAcidBlobs()` outright, **the whole field replaces itself in one frame**
(1975-1977) — a hard cut, **not safe for a continuous clock**.
`conserve_mass>0.5` → `AdjustAcidBlobCount(want)` walks gradually: retires
the least-visible first (off-frame then smallest, 3290-3304) via
`rTarget=0` decaying `baseR` over `dissolve_s` then erasing (4009-4024); a
shortfall grows a new blob in from under the bottom edge at
`baseR=rTarget*0.02` swelling over `spawn_grow_s` (3306-3334), **drawing its
kind fresh from the current `disc_frac/web_frac/bubble_frac/hole_weight/
big_bias`** (3310-3318) — the *only* per-frame path by which those fractions
have any effect, and only for newly-grown blobs. **Existing blobs never
change kind/size-range/wgt-sign**, including across `rise_respawn`
re-entries (3936-3939 keys the redraw range off `b.kind`, never reassigned).

**Consequence**: `disc_frac`/`web_frac`/`bubble_frac`/`hole_weight`/
`big_bias` (and the min/max radius keys) are read every frame but only
**restructure the standing population via a reseed or slow conserve_mass
turnover** — gliding them continuously will not visibly reshape an
already-seeded, long-lived field (discs/webs rarely die unless `blob_count`
itself drops). Treat as **stage-entry-only** keys.

**Per-frame-safe motion (`StepAcidBlobs`, 3673-4025)** — change existing
blobs' behaviour without touching identity:
- `rise_speed`: constant upward drift on target velocity, damped in
  (3782-3794); holes climb at 0.7x.
- `rise_wobble`: two incommensurate sines on the blob's own phase, scaled by
  `rise_speed` (3788-3793).
- `rise_parallax`: `pscale=1+(clamp(baseR/disc_max,0.25,1)-1)*par`
  (3699-3703,3749-3755) scales flow response, curl drift and rise speed
  together; **`disc_max` is the parallax reference radius**, so raising it
  reclassifies what counts as "near."
- `rise_respawn`: field-cleared-the-top test uses the **support** radius, not
  `baseR` (`b.y<-supOut`, 3929); re-enters below the bottom at fresh x/phase,
  and (unless `conserve_mass`) a freshly redrawn radius from its own kind's
  range (3913-3964).
- `depth_rise`: droplet-side only (back layer climbs faster, symmetric,
  4481-4502) — doesn't touch blobs.
- `oil_edge_frac`, `film_level`: pure shader constants (see below).
- `rise_stretch`, `rise_bottom_light`: **not traced into shaders.h in this
  pass** — by tooltip/name they're shading terms on existing blobs (GUESS:
  per-frame safe, unverified).

**`film_level`** (shaders.h:2420-2424): `oilC *= LA_FILM_LEVEL` dims only the
oil's **flat interior fill**; `mass_rim`/`oil_glow`/halo/lens-highlight terms
are computed from un-dimmed `oilR`/`oilThinR` copies (2390-2393) and stay
full-palette. This is exactly "flat body black, thin rim survives" —
**per-frame safe**, a shader constant with no population effect.

**Droplets (`StepAcidDroplets`, from 4027)** — two kinds sharing the
metaball field:
- **kind 0 ("trapped")**: nucleates only where `sdf>0.004` — inside an
  existing blob's field (4294-4298); rides the mass it's born in.
- **kind 1 ("free")**: nucleates only where `sdf<-0.004` — in open ink,
  biased exponentially toward the oil edge (`p=0.10+0.70*exp(sdf/0.070)`,
  4299-4302). **Independent of any big mass** — the candidate mechanism for
  `starfield`.
- `droplet_spawn_rate/r_min/r_max/bias/attract/merge/coalesce[_s]/
  ring_frac/width/lift/clump/wobble/racer_*/mass_bias/crust_*` are all read
  fresh at every nucleation/step off `m_cfg.acid` (4195-4234, 4783-4956) —
  **all per-frame safe** (droplets already turn over every `droplet_life`,
  ~120s default).
- **`droplet_ring_clump`**: pulls nearby same-kind ring droplets into a
  shared-wall foam capped at `kRingMaxTouch=6` (4786-4788,4893-4911) — local
  clustering **wherever ring droplets happen to be near each other**. Grep
  for `orbit`/`parentBlob`/`hostBlob`/`nearestBlob`/`attractBlob` in
  fluid.cpp returns nothing relevant: **no code pulls droplets into a ring
  around a chosen small core blob.** See Rafts below.

## 2. Coverage readback — what "black %" is

Two unrelated systems; only one measures oil/black share.

**`m_darkPct`/`CoverageDarkPct()` — the black-oil-share metric.**
`UpdateCoverage()` (6451) downsamples to a 48x27 buffer periodically;
`ProcessCoverage()` (6512-6575): per texel `m=max(r,g,b)`,
**`if (m<=dark_level) dark++`** (6528; `dark_level` default 0.07 scRGB ≈5.6
nits, `[behavior] dark_level`, keys.inc:64). `darkPct=100*dark/(48*27)` →
`m_darkPct`/`CoverageDarkPct()` (6547-6548, fluid.h:1786). This is the
correct "black oil share" measure, **given the look renders oil black and
the ink/dye background does not**. Also computes a brightness-weighted hue
mean and a 6x3-tile contrast check, both unrelated to black%, feeding only
the auto-pause hysteresis (`dark_floor`/`surv_dark_floor`, 6565-6568) and the
cycle director's `early_switch_darkpct` (cycle.cpp:1197).
**Gap**: `CoverageDarkPct()` is **never logged** anywhere — its only two
call sites are that hysteresis and the cycle early-switch check. No
`--console`/`--stats`/`--shot` output prints it.

**`[cover]`/`MixCoverage()` (3624-3671, logged main.cpp:2952-2966) is NOT
black %.** It's the FINAL-CYCLE C hue2/hue3 mix-field coverage logger — a
CPU Perlin-ish field biasing secondary dye-colour patch placement, gated on
`film_hue2_amt`/`film_hue3_amt`. Its fields (`hi62/full68/band/lo32/28/24/20/
mean/patches/big`) are thresholds against that 0..1 field at
0.62/0.68/0.56-0.68/0.32/0.28/0.24/0.20 (3636-3645) plus a connected-
component patch count (3647-3669) — purely about second-colour patch
placement, unrelated to oil geometry. `tools/bv-schemes.py`'s
`--cover-sweep`/`--cover-seeds` usage is this system, not black%.

**`mean_lum`/nit histogram (`--shot`, main.cpp:2677-2694)**: per frame,
full-res mean luma plus a 10-bucket nit histogram, `[<1] [1-5] [5-15] ...`.
The **`[<1]`** bucket (pixels <1 nit) is the only *already-printed* black-ish
proxy, but at full render resolution and a **different threshold** than
`dark_level` (1 nit vs. 0.07 scRGB≈5.6 nits) — not a drop-in substitute.

## 3. Per-layout candidates

All keep `sim_res=256`/`dye_res=4096` and existing bloom rules untouched.
"Restructure" = needs reseed / stage entry (kind-mix keys); "safe" = rides
the continuous per-frame clocks.

**Few giants (~28%, 2-3 huge lobed masses, sparse droplets).**
`blob_count` low-moderate (~16-24, restructure); `disc_frac` high (~0.5-0.6),
`web_frac` moderate (~0.2-0.3, chain-merges into/extends disc lobes),
`bubble_frac` low (~0.05) — remainder→holes small, keep `hole_weight`
modest (~0.3-0.5) so giants aren't visibly perforated (all restructure);
`big_bias` low (~0.3-0.5, restructure); `disc_max`/`web_max` raised toward
frame scale (e.g. disc_max ~0.45-0.55 vs. acid-rise-12's 0.38) so "huge"
reads huge — also shifts the `rise_parallax` reference radius (restructure).
`film_level` low (~0.05-0.15, safe) for pure-black body with rim intact.
Small `rise_speed` (~0.003-0.006) + `rise_wobble` so giants still evolve
(safe) per the brief's "must move" rule. Low `droplet_spawn_rate`/
`droplet_ring_clump`/`droplet_mass_bias` to stay flat-banded, not foamy
(safe). **Risk**: at this `disc_max`, `wrap_margin`'s clearance (keyed off
`supportScale`, 3906-3910) may need raising too or a giant could pop at the
edge instead of clearing smoothly — not verified, no render run.

**Lava rise (~25%, tall rising blobs, bubbles pinch off tops).** Already a
shipped mode — see §4; reuse `acid-rise-12.ini`'s rise block near-verbatim
(all safe). To hit ~25% specifically, raise `disc_frac`/`web_frac`/
`disc_max` a bit (restructure) — a live glide of these won't reshape an
already-seeded rise field. The pinch-off look is likely just kind-0 droplets
nucleating inside a tall mass plus normal droplet rise-lag as the mass
climbs past them, not a distinct mechanic — **inference, unverified**; the
overflow "satellite" pinch in droplet-merge code (4867-4884) is a merge-
overflow safety valve, not tied to blob tops.

**Today (~9%, one medium mass + droplets).** Close to/likely already near
the shipped default per the brief's own framing. Lowest risk: ease the other
four layouts' extremes back toward center on `blob_count`/`disc_frac`/
`bubble_frac`/`film_level` — safe if the live population is already in
range, restructure only if coming from an extreme layout.

**Starfield (~12%, dense small droplets, no big mass).** Keep `blob_count`
very low or bubble-kind only (restructure); get density from **kind-1
open-ink droplet nucleation** (4299-4302) via high `droplet_spawn_rate` and
small-skewed `droplet_r_min/max`/`droplet_bias` (safe once base population
set). **Risk**: kind-1 nucleation probability is weighted toward proximity
to an *existing* oil edge — with almost no blobs on screen there's little
"near oil" area to seed from, so achieved density may undershoot
`droplet_spawn_rate`'s nominal rate — **not verified, needs an empirical
check**.

**Rafts (~8%, droplets in rings round a small core). Likely NOT reachable
with existing keys — flag to the user, per the brief's own "no new features
quietly" rule.** `droplet_ring_frac` makes individual hollow-ring droplets;
`droplet_ring_clump` clusters same-kind rings that are *already near each
other* into shared-wall foam (4786-4911) — local clustering wherever it
happens, not an orbit around a chosen core. No code path biases co-location
with a specific small blob (confirmed absent: orbit/parentBlob/hostBlob/
nearestBlob/attractBlob). A small `blob_count` plus high `droplet_ring_frac/
_clump` gives foam patches *somewhere*, not reliably "round a small core."
**Cost note**: a literal ring-round-a-core needs a new attraction term
(droplet accel biased toward the nearest small positive blob's rim at a
target radius) — a physics change, not a value change.

## 4. Existing rise/lava code — already a mode?

**Yes.** `reference/configs/acid-rise-12.ini` ships the full block today:
`rise_speed=0.0195 rise_wobble=0.5 rise_respawn=1 rise_stretch=0.45
rise_bottom_light=0.8 rise_parallax=0.7 rise_parallax_dim=0
conserve_mass=1 spawn_grow_s=4 dissolve_s=5 film_level=1 bloom=0.30`
(values read directly from the ini). Also `blob_count=96 disc_frac=0.22
web_frac=0.34 bubble_frac=0.20 big_bias=0.62 hole_weight=0.608` — holes are
~24% of population today (remainder). `bloom=0.30` matches AGENTS.md's note
that acid-rise-12 ships the keyed post bloom at 0.30. Good starting ini for
"lava rise"; hitting ~25% black specifically needs measuring its current
darkPct (not measured — no render run) then adjusting
`disc_frac/web_frac/disc_max/hole_weight` (all restructure-only, §1).

## 5. Existing sweep tooling for black % over 60s per ini

**No existing tool measures `CoverageDarkPct()` directly.**
- `tools/bv-schemes.py`'s `--cover-sweep`/`--cover-seeds` (its own comments,
  lines 15-25) target the hue2/hue3 mix-field coverage (§2's `[cover]`
  line), not oil black% — not applicable without modification.
- `reference/configs/ab.py` (also at `build/shots/ab.py`) only builds a
  contact-sheet PNG from existing `--shot-series` frames
  (`prefix-<seconds>.png`); it measures nothing. Usage per its docstring:
  `python ab.py out.png "Label A=build2/shots/s0-live" "Label B=build2/shots/s1-live"`.
- The app's real flags for this are `--shot`, `--shot-preset`,
  `--shot-delay`, `--shot-series N:S`, `--shot-yield`, `--shot-png-only`
  (main.cpp:2760-2865). A 60s/ini measurement run would look like (after
  taking `build2/shots/gpu.lock` per the GPU-lock rule):
  `FluidWallpaper.exe --shot build2\shots\layout1.png --shot-preset "reference\configs\<layout>.ini" --shot-delay 0 --shot-series 12:5 --shot-yield 8 --shot-png-only --console`
  — 12 frames 5s apart (~60s), each logging a `[shot]` nit histogram. The
  **`[<1]`** bucket, averaged across the 12 frames, is the only
  already-printed proxy, but at the wrong threshold (1 nit vs.
  `dark_level`'s 0.07 scRGB≈5.6 nits — §2). A faithful measurement against
  the brief's own ±5-point acceptance criterion needs either (a) a temporary
  code change to log `CoverageDarkPct()` on a timer in `--shot` mode, or
  (b) an offline script recomputing `max(r,g,b)<=0.07` over the `--shot-
  series` PNG/JXR pixel data (buildable without touching the renderer).
  **Neither exists today — flagged as new tooling to write, not something
  already in `tools/`.**

## Summary: per-frame-safe vs. restructure-only

| Key(s) | Safe live? | Why |
|---|---|---|
| rise_speed/_wobble/_respawn/_parallax/_parallax_dim | yes | 3699-3794, 3913-3968 |
| rise_stretch, rise_bottom_light | yes (unverified math) | not traced into shaders.h |
| depth_rise | yes | droplet-only, 4481-4502 |
| oil_edge_frac, film_level | yes | shader constants only |
| conserve_mass, spawn_grow_s, dissolve_s | yes | timing, not kind |
| droplet_* (spawn/size/life/attract/merge/coalesce/ring/racer/mass_bias/crust) | yes | read fresh per nucleation |
| blob_count | only w/ conserve_mass=1 | else forces SeedAcidBlobs() reseed |
| disc_frac/web_frac/bubble_frac/hole_weight/big_bias | no | only affect newly-created blobs |
| disc/web/bubble/hole _min/_max, size_bias | no | same; disc_max also = rise_parallax reference radius |
