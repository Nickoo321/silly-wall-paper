# Executor orientation card

Read this INSTEAD of WORKLOG/PROGRESS. Terse, no history.

## 1. Hard rules

- GPU lock is `build2\shots\gpu.lock` in the MAIN repo (dot-source `tools\gpu-lock.ps1`; it
  resolves to the main repo's lock even from a worktree, never a private per-worktree file). The
  lock rule: **renders never wait** — render at full speed, no lock wait (user 2026-09-22: idle
  executors lose their cache; concurrent headless renders accepted). **Builds always take the
  lock**: `if (Wait-GpuLock -Owner "<name>" -TimeoutMinutes N) { try {...} finally
  { Release-GpuLock } }` in the SAME PowerShell process. Incident: 2026-09-15, two concurrent
  `--shot` renders once turned the OLED grey; the user accepts that risk now (render at full
  speed, no lock wait for renders, 2026-09-22); if the panel misbehaves, stop rendering and tell
  the parent.
- Headless `--shot` only — never the screen, never screenshots, never touch brightness/DDC.
- Never touch `build\`, `build2\live`, the running `FluidWallpaper.exe`, or Wallpaper Engine, or
  `tools\away-pause.ps1`. No swap — the parent (Fable) does that, and only after asking the user.
- Fluid parity is sacred: `reference\configs\we-look-live.ini`, 60 s, 2560x1440, seed 1234,
  `--hdr on` must give md5 `835AECBD9EF1384A8CAAF1A611EE3A26` after EVERY change (parity peak
  700). New keys default to today's behaviour so existing presets stay byte-identical.
  gamut=2 since 2026-09-26; the gamut=1 render still gives 10E36EBF1A74EDFE609065D757300054
  (re-verified 2026-09-26 on a scratch copy of we-look-live.ini with gamut forced back to 1 —
  confirms nothing else on the fluid path changed since the old reference).
- Acid-side equivalent of the above: `tools\preset-identity.ps1 -Exe <exe> [-Baseline <file>]
  [-Save <file>]` renders `we-look-live.ini` (checked first, must equal the md5 above) plus the
  `acid-rise-*` presets, prints `name md5` each; `-Save` writes a baseline, `-Baseline` prints
  MATCH/DIFFERS per preset and exits 1 on any diff. Before merging: `tools\preset-identity.ps1
  -Exe <your build> -Baseline <latest baseline>` must print MATCH for every preset whose keys you
  did not intentionally change.
  Fast tier = 15 rows from tools\preset-identity.manifest.psd1; run with -Tier fast; the 1553a7f
  baseline is retired (its list predates the manifest).
- MSVC string literal cap 16380 bytes in `src\shaders.h` — split with `)hlsl"` / `R"hlsl(`.
  `tools\slot-check.ps1` prints the three largest pieces (and fails over 16000): split before
  adding more than ~20 lines to one within ~2.3 KB of the cap.
- Root signature is at the 64-DWORD limit — cbuffer slack only, don't add root params.
- New key's range/step comes from a real A/B sheet (3-5 values), not a guess (slider-range-policy:
  a blur useful over 0.1-3 cannot ship as a 0-50 slider — it hides the good zone).
- One feature per branch: one headless shot + a nearest-neighbour crop sheet, commit on the
  branch, merge to main, STOP and report (hash, md5, sheet path, values and why, rote list).
- Read the code path you touch END TO END before the first render (2026-09-22 dye lesson: two
  guessed edits from nearby code cost two wasted render cycles — the real fix was downstream in a
  different path than assumed).
- Work in your own worktree. Use a scratchpad subfolder named after your worktree (e.g.
  `build2\shots\live\<worktree-name>\`) for your own intermediate files, so concurrent executors'
  scratch output never collides in the shared `build2\shots\live\` tree. Commit ends:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- If part of the task is rote (renders, md5, ini edits, doc text, sheets) and a cheaper model
  could do it without loss, say so in your report or hand back a task list — you judge, the
  parent dispatches (model-tiering-policy).
- Executors have no SendUserFile: copy your sheets into the MAIN repo's `build2\shots\live\` and
  give the parent the path — the parent shows the user (show-what-you-see); they follow from
  their phone and can't see tool output.

## 2. File map

- `src\main.cpp` — ini getF/putF, CLI flags, fullscreen pause.
- `src\fluid.h` — config structs + defaults; key names in trailing comments.
- `src\fluid.cpp` — sim, constants upload, post pass (`RunPostPass`).
- `src\acid_slots.h` — the packed acid cbuffer's slot table (names for every `laP<n>.<c>`).
- `src\shaders.h` — HLSL string literals: acid sim/display `kAcid*`, post `kPostSrc`, display pass.
- `src\settings.cpp` — slider table: label, min, max, step, decimals, pointer, section, key, help.
- Other `src\`: `analyzer.cpp`, `app.rc`, `app_state.h`, `journey.cpp/h`, `moods.cpp/h`, `scenes.cpp`.
- `reference\configs\acid-rise-12.ini` — the LIVE preset, one comment line per key.
- `reference\presets\Liquid Acid*.ini` — tray variants (Sonnet rolls new keys in later).
- `reference\shots\photos\` — reference photos: ideas, not targets (WE fluid parity is the exception).
- `reference\shots\panel\` — keeper captures with the user's verdicts.
- `build2\shots\live\` — your sheets go here (gitignored).
- `tools\gpu-lock.ps1`, `tools\shot.ps1` if present.

## 3. Shot command (verbatim)

```
& "<worktree>\build2\FluidWallpaper.exe" --shot "<out>.png" --ini "<ini>" --hdr on `
  --shot-delay <sec> --shot-size 2560x1440 --shot-yield 2 --seed 1234
```
`--shot-series N:S` for a series. Poll the PNG until its size is stable (OneDrive lag on write).
Build from the worktree root with `cmd /c "<repo>\tools\build-wt.cmd"` (it builds `%CD%`, i.e.
wherever you run it from, not the main repo — a worktree-local `build-wt.cmd` at the root is
`.git\info\exclude`d, so it never existed in a fresh worktree; the tracked copy fixes that).
Every `--shot --hdr on` writes FOUR files, not one: `<out>.png` (8-bit SDR, the md5 file for parity/
identity), `<out>-hdr.png`, `<out>.jxr` (lossless scRGB-half, real HDR in Windows Photos) and
`<out>-pq.png` (16-bit Rec.2020/ST 2084). ~35 MB extra per shot into `build2\shots` (which is
gitignored but NOT OneDrive-ignored in the main repo) — sweep old shots periodically, and use the
`.jxr` (not the `-pq.png`) when you need to keep just one HDR file.

## 3.5. Before merging a branch that touches the acid cbuffer

**Run `tools\slot-check.ps1` before merging** (any branch that touches `UploadAcidConstants`,
`cbuffer AcidCB` or any HLSL literal; no GPU, <1 s; exit 1 = do not merge). Every packed acid
scalar has a NAME in `src\acid_slots.h` (one row: name, float4, component). The upload writes
`slot(LA_<NAME>, v)`; the acid HLSL reads `LA_<NAME>` (a define `kAcidSlotMacros` hands to
D3DCompile) — never `laP13.x`. The checker fails on a slot written but unread, read but unwritten,
written twice, two names on one component, any raw `laP<n>.<c>`, C++/HLSL cbuffer order mismatch,
a stale comment table, or a literal piece over 16000 bytes; it also prints the free components and
the three largest literals. New key: add a row, `slot(...)` it, read it, then
`tools\slot-check.ps1 -Fix` regenerates the cbuffer comment table in `shaders.h`.

## 3.6. Per-key inert test / shader bytecode diff

`tools\key-effect.ps1 -Exe <exe> -Ini <preset.ini> [-Keys a,b,c] [-Sections liquid_acid,post]
[-Size 1280x720] [-Delay 10] [-Out <dir>]` (brief BJ) renders a preset's baseline, then per key
whose live value differs from its fluid.h/settings.cpp default, renders it back at default and
reports md5 (INERT) + whole-frame MAD; holds the GPU lock once for the whole run.
`python tools\dxbc-cmp.py <old_shaders.h> <new_shaders.h> <acid_slots.h>` compiles both copies'
kDisplaySrc with d3dcompiler_47.dll (no build) and prints IDENTICAL/DIFFERS per PSO/stage — no GPU.

## 4. Current state

Live = `acid-rise-12` on main @ `1553a7f`, `dye_lum` reverted to 0 ("black masses back for now" —
the user's call after BK's "too bubbly" feedback tonight; it ran 0.44 earlier in the day). Looks =
tray presets (`style=fluid|liquid_acid|ink`).
Oil look's stack, one line each: sim (racers, coalescence, big rings, conserve_mass, weather,
residue, crust) · per-droplet depth + tilted focus surface + DOF · real lateral CA · halation ·
fog/bloom · lid (ghosts/rings/sheen/iris/glint) · V3 motion (shimmer advected by t3, vignette
wander, pixel-shift orbit, rig readjust) · film grain at 24 fps · hue2 field (second hue, rotation
not blend, pair rotates, wobble, seeded off screen below the edge and rising) · dyed masses (the
negative space is a translucent purple wax, not black — dye_hue/sat/lum in the display pass, AG).

Render throttle (supersedes section 1's flat "renders never wait" framing): `--shot-yield 30`
while the user is at the PC, `8` once idle 25 min, `2` once asleep.

In flight, no code landed yet: `fw-scratch` (branch `scratch`) for BM's scratched-acrylic lid;
`fw-bk` (branch `bk`) for BK's darkness levers (candidate keys `dye_droplets`/`dye_smoke`, so
droplets stay dark while masses keep colour). Queued ahead of both: a dye colour-variation sheet
(hue/sat/lum grid + `dye_hue_follow` + a dark variant), which the brief wants done first.

## 5. Open items AC-BA (`reference\briefs\NEXT-rings-grain-aberration-refraction.md`)

AC presets don't switch style · AD menus confusing (deferred) · AE multicolour oil (waiting on
photos) · AF weak lid reflections, raise ghost/rings/glint/iris · AG dye not just black (DONE,
dye4; follow-up = dye_hue_follow A/B) · AH film leaks/artefacts driven by oil movement · AI on-panel feature tour
(--tour) · AE-b global hue rotate + ~170-190 deg contrast wobble (landed, hue2b) · AJ FUTURE widen
boundary-reflect radius (do not start) · AK lens flare, hue opposite dominant sim colour · AL
colours must enter off-screen only (branch hue2c) · AM dye the black ink (=AG) · AN palette combos
from colour-wheel ref + jitter · AO grading pass temp+tint · AP hairs/dust more frequent, smaller,
more transparent, corner-clustered · AQ a post artefact should be directional · AR light-source
shadowing needs to be stronger · AS focus should actively move, >=10s · AT grain too
distracting/macro, needs supervised A/B · AU standing slider-range policy (=slider-range-policy) ·
AV some colour slots should go grayscale · AW weak lid effect, add global motion on lid move · AX
weak heat haze · AY curve mix speed for large-distance colour merges · AZ +5% motion for big
globs · BA small bubbles: rise-only, constant stream of 2-5, stronger (plate ~45 deg or flatter).

## 6. Report format (5 lines + the proof block)

1. Hash + one-line summary. 2. Parity md5 (or "N/A — style=fluid untouched"). 3. Sheet path(s) in
`build2\shots\live\`. 4. Values shipped and why (the A/B result). 5. Rote list, or "none".

PROOF BLOCK (user rule, 2026-09-23 19:05: "don't just implement a feature: check that it works,
note the difference, what setting actually adds to the effect"). For EVERY new key, before you
report done:
- WORKS: tools\key-effect.ps1 style evidence that the key is not inert at its test value on the
  live preset: MAD / max-diff numbers vs the key at default. A key that reads INERT is not done.
- DIFFERENCE: a difference image or a 2x crop pair (default vs test value) and ONE sentence in
  plain words of what changed on screen (where, how much, what it looks like).
- WHAT ADDS: per key, which values move the look and which do nothing (e.g. "0..0.3 nothing
  visible, 0.3..0.7 the effect, above 0.7 only brightness"); if two keys do the same thing, say
  so and propose dropping one. Slider range = the useful range (policy).
No proof block, no merge.

## §7 The 3 review questions (answer at the end of every final report)
1. WORKS: what did you verify actually works, and how (numbers, files), and what could only the user confirm?
2. DIFFERENCE: what can the user see or do now that they could not before (one plain sentence per feature)?
3. WHAT ADDS: which keys/values actually move the result, which are duplicates or inert, and what rote follow-ups can a cheaper model take?
Then, optionally: what worries you about the project, and what would you change first.
