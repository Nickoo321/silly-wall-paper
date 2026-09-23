# Executor orientation card

Read this INSTEAD of WORKLOG/PROGRESS. Terse, no history.

## 1. Hard rules

- GPU render/build hold `build2\shots\gpu.lock`: dot-source `tools\gpu-lock.ps1`, then
  `if (Wait-GpuLock -Owner "<name>" -TimeoutMinutes N) { try {...} finally { Release-GpuLock } }`
  in the SAME PowerShell process. Never two `--shot` renders at once (2026-09-15: OLED went grey).
- Headless `--shot` only — never the screen, never screenshots, never touch brightness/DDC.
- Never touch `build\`, `build2\live`, the running `FluidWallpaper.exe`, or Wallpaper Engine, or
  `tools\away-pause.ps1`. No swap — the parent (Fable) does that, and only after asking the user.
- Fluid parity is sacred: `reference\configs\we-look-live.ini`, 60 s, 2560x1440, seed 1234,
  `--hdr on` must give md5 `10E36EBF1A74EDFE609065D757300054` after EVERY change (parity peak
  700). New keys default to today's behaviour so existing presets stay byte-identical.
- MSVC string literal cap 16380 bytes in `src\shaders.h` — split with `)hlsl"` / `R"hlsl(`.
- Root signature is at the 64-DWORD limit — cbuffer slack only, don't add root params.
- New key's range/step comes from a real A/B sheet (3-5 values), not a guess (slider-range-policy:
  a blur useful over 0.1-3 cannot ship as a 0-50 slider — it hides the good zone).
- One feature per branch: one headless shot + a nearest-neighbour crop sheet, commit on the
  branch, merge to main, STOP and report (hash, md5, sheet path, values and why, rote list).
- Read the code path you touch END TO END before the first render (2026-09-22 dye lesson: two
  guessed edits from nearby code cost two wasted render cycles — the real fix was downstream in a
  different path than assumed).
- Work in your own worktree. Commit ends: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- If part of the task is rote (renders, md5, ini edits, doc text, sheets) and a cheaper model
  could do it without loss, say so in your report or hand back a task list — you judge, the
  parent dispatches (model-tiering-policy).
- Send every render/sheet you look at to the user via SendUserFile in the same turn — they follow
  from their phone and can't see tool output (show-what-you-see).

## 2. File map

- `src\main.cpp` — ini getF/putF, CLI flags, fullscreen pause.
- `src\fluid.h` — config structs + defaults; key names in trailing comments.
- `src\fluid.cpp` — sim, constants upload, post pass (`RunPostPass`).
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
Build with `cmd /c build-wt.cmd` in the worktree.

## 4. Current state

Live = `acid-rise-12` on main @ `fd81f49`. Looks = tray presets (`style=fluid|liquid_acid|ink`).
Oil look's stack, one line each: sim (racers, coalescence, big rings, conserve_mass, weather,
residue, crust) · per-droplet depth + tilted focus surface + DOF · real lateral CA · halation ·
fog/bloom · lid (ghosts/rings/sheen/iris/glint) · V3 motion (shimmer advected by t3, vignette
wander, pixel-shift orbit, rig readjust) · film grain at 24 fps · hue2 field (second hue, rotation
not blend, pair rotates, wobble, seeded off screen below the edge and rising) · dyed masses (the
negative space is a translucent purple wax, not black — dye_hue/sat/lum in the display pass, AG).

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

## 6. Report format (5 lines)

1. Hash + one-line summary. 2. Parity md5 (or "N/A — style=fluid untouched"). 3. Sheet path(s) in
`build2\shots\live\`. 4. Values shipped and why (the A/B result). 5. Rote list, or "none".
