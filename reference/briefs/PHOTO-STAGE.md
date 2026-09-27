# Brief BY — PHOTO STAGE (stills from a folder as their own cycle stage)

Status: DRAFT 2026-09-26 23:50, for auditor pre-flight, then ONE Opus executor AFTER CLOCKS merges.
User request 2026-09-26 12:45 (A/B "A photo stage in the cycle"; 12:50 "build it now with an empty
folder, I add photos later; it must skip itself gracefully when the folder is empty"). Creative chat
weighs in on selection/dwell later (its §7). No other new features.

## 1. What it is (spec)

A fourth stage KIND for the director: a stage that shows ONE still image full-screen for its dwell,
entered and left like any look change (fade through black, no sim warm-up), drawn from a folder.

- Folder: default `%APPDATA%\FluidWallpaper\photos\` (created empty on first run so the user can
  find it; the tray gets "Open photos folder" next to "Open presets folder"). A stage file may
  override it: `[photo] folder=<absolute or relative to the ini>`.
- Files: `*.jpg *.jpeg *.png *.bmp *.tif *.tiff`, plus `*.webp/*.heic/*.avif` only if WIC has a
  decoder for them on this machine (try, log once per extension, skip). Sub-folders are not scanned.
- Selection: at each visit the folder is RE-SCANNED (cheap; photos added later appear without a
  restart), one file is drawn at random (seeded from [cycle] seed + visit count), never the same
  file twice in a row while more than one exists; a shuffle bag so every photo is seen once before
  any repeats. A file that fails to decode is skipped with one log line and removed from the bag
  for this run; no crash, no black stage.
- EMPTY / MISSING folder, or every file failing: the stage is NOT drawn (its weight is 0 for that
  draw, logged once per visit attempt as `[cycle] photo stage skipped: no photos in <folder>`); the
  director draws the next stage as if the photo stage were absent. With `order=fixed` it is passed
  over. Nothing black, nothing paused.
- Look: scale-to-fit by default (letterbox/pillarbox pure black), `[photo] fit=fit|fill` (fill =
  centre crop); `[photo] drift=0` (0 = off; >0 = a slow Ken Burns: scale 1 -> 1+drift and a
  pan of up to drift/2 of the frame across the dwell, smoothstep, "maybe later" per the user, so it
  ships 0 and inert). No effects, no mirror fold, no clocks (a photo stage does not read Config;
  `[clocks]` groups are all 0 on it), no post.
- HDR: the image is SDR content. HDR ON (scRGB FP16 swap chain): sRGB-decode -> linear -> the
  app's sdrScale (SDR white, the same scalar the display pass folds the fade into), so a JPEG white
  lands exactly on SDR white and never on peak; gamut: the sRGB primaries are converted to the
  output space with the matrix the app already uses (BT.2020 mode = colours kept, no expansion).
  HDR OFF: pass-through sRGB 8-bit. Fade: multiply by m_fade like the looks (`SetFade`), black-out
  below 0.01 (`SetBlackOut`). Dwell default 120 s (`stage_K_dwell`), tier wild (`stage_K_tier`).
- Decode + upload: WIC decoder (`IWICBitmapDecoder` from file -> `IWICFormatConverter` to
  32bppBGRA; a 16-bit PNG/TIFF to 64bppRGBA -> R16G16B16A16_UNORM), downscaled with
  `IWICBitmapScaler` (Fant) so the longest side <= 4096 (the panel is 2560 wide). Decoding runs on a
  WORKER THREAD started when the director begins the FADE_OUT into the photo stage, so the texture
  is ready at the black point; if decode is slower than the fade the black holds until it is (cap
  5 s, then skip the file). Upload via one upload heap + CopyTextureRegion on the render queue at
  the black point; one live texture + one loading, the previous released after the fade-in.
- Sim underneath: keeps ticking (so the next look does not restart cold), not displayed. The photo
  stage counts as "the other look" for the alternation rule (WE alternates with oil/ink/overlay/
  photo), and overlays never apply on top of it.

## 2. Keys and files

- `[cycle] stage_K_photo=1` marks the stage kind (its `stage_K_file` is a small ini holding
  `[photo]`); `reference/presets/Photo stage.ini` ships with
  `[meta] look=photo`, `[photo] folder=` (empty = the default folder), `fit=fit`, `drift=0`.
- `[photo] folder, fit, drift` are the only new keys (keys.inc rows with `KF_...` look tag photo;
  folder is a text row = the first text key in keys.inc, or a "browse" button on the Modes page —
  the executor picks the smaller change and says which).
- FINAL-CYCLE stage list: append `Photo stage.ini` as a wild-tier stage (the shipped cycle-first.ini
  and the user's live [cycle] block via the next cycle-on install).
- Tray: "Open photos folder" (ShellExecute the folder). Header while showing: `Photo · <file name>`.
- Log lines (stdout/--console/--shot): `[photo] scan <folder>: N files`, `[photo] show <name>
  WxH -> WxH decode Nms upload Nms`, `[photo] skip <name>: <reason>`, the skipped-stage line above.

## 3. Where it hooks in

- cycle.h: `CycleStage.photo` (bool) + `CycleLook` gets `CYCLE_LOOK_PHOTO = 3`; CycleLoad parses
  `stage_%d_photo`; `Compose()` is not used for a photo stage (nothing to compose; the live Config
  is left as the previous look's so the sim keeps its state).
- cycle.cpp draw: the alternate_random / tier draw treats a photo stage whose scan found 0 files as
  weight 0 for THIS draw only (a later visit re-scans). ApplyStage for a photo stage: no
  ResetLookState, no PSO compile of a look, no warm-up; it asks the renderer to
  `ShowPhoto(texture)` / `HidePhoto()`.
- fluid.h/.cpp: a PHOTO display path = the fourth PSO (`kPhotoSrc`, a fullscreen triangle + one
  SRV + b0 constants: fit rect, sdrScale*fade, gamut matrix select, drift phase), selected in the
  display pass when a photo is shown; the three look PSOs are untouched (dxbc IDENTICAL), the
  shared `kDisplaySrc` is untouched. `Frame()` skips the look's display draw while a photo shows
  but still runs the sim step.
- main.cpp: the photos folder creation next to the presets folder creation; the tray item; the WIC
  decode helper next to the existing WIC PNG writer (main.cpp:2363); the worker thread.
- With no photo stage in the list nothing above runs: parity + identity unchanged.

## 4. Proof (PROOF BLOCK per EXECUTOR-CARD §6, plus §7)

1. No photo stage: identity fast tier 15/15 unchanged, WE parity md5 MATCH, dxbc-cmp IDENTICAL
   (fluid/ink/acid), slot-check + keymeta-check PASS.
2. Test images generated by a script into `reference/photos-stage/` (committed, small):
   `bars-16x9.png` (colour bars + a 16-step grey ramp, white = 255), `bars-4x3.png`,
   `bars-21x9.png`, `grey16.png` (16-bit PNG ramp), `big-30mb.jpg` (generated noise, ~30 MB, NOT
   committed: made at proof time), `notjpeg.jpg` (a text file).
3. A cycle ini whose list is the photo stage only, folder = that test folder, `--shot` 20 s, HDR on
   and off: (a) the white bar measures the SDR white (scRGB: sdrScale, e.g. 3.0 at 240 nits; SDR:
   1.0) and never more; (b) the grey ramp is monotone with the right sRGB decode (16 steps
   within 1/255 of the expected linear values); (c) letterbox/pillarbox pixels = 0 for 4:3 and
   21:9; (d) fill mode crops centred; (e) grey16 shows no banding vs an 8-bit decode (histogram);
   -> `build2\shots\live\photo\sheet-{hdr,sdr}.png` + numbers.
4. Empty folder: a cycle run (--console, 3 stages incl. the photo stage, 30 s dwell) logs the skip
   line and draws the next stage; no black frame beyond the normal fade; the same run after
   dropping one file into the folder shows the photo at the next visit (re-scan proven).
5. `notjpeg.jpg` and `big-30mb.jpg` in the folder: the bad one is skipped with its log line; the big
   one decodes on the worker (log decode/upload ms) and the longest render-thread frame during the
   load is reported (must stay under 2 frames at 144 Hz, else the upload must be split).
6. Alternation: with WE + oil + photo in the list, the log shows WE never follows WE and no overlay
   lands on the photo stage.
7. FEATURES.md rows; SETTINGS.md regen; manifest row for `Photo stage.ini` (tier fast, a --shot of
   the stage-only cycle with the test folder = its identity).

§7 for the review chat: 1. works (which of 1–6 proven); 2. the visible difference in one sentence;
3. proposed dwell and selection rule for the user's real photos (the creative chat's call);
4. anything that hitched or flashed.

## 5. Branch and merge

Branch `photo` from main AFTER CLOCKS merges (the clocks table needs the photo stage to carry
group weights 0; the director refactor in CLOCKS moves where ApplyStage writes Config, so this
branch builds on it). Worktree C:\Users\abg77\fw-photo. PHOTO-RESUME.md per milestone. Nothing
merged by the executor.
