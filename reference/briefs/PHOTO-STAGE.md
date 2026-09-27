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

## AUDITOR PRE-FLIGHT (2026-09-27, main 5d9c39b) — binding

Read-only. Where these items and the brief disagree, these items win. The EXECUTOR-CARD file map is stale: keys live in src\ui\keys.inc; settings.cpp and moods.cpp are gone.
### A. The stage kind (cycle.h/.cpp)
1. Derive the kind from the FILE. In ResolveStage (cycle.cpp:214), a stage file with a `[photo]` section is a photo stage: `st.photo=true`, `st.look=CYCLE_LOOK_PHOTO (3)`, `overlay=false`, `burstOk=false`. Also accept `stage_K_photo=1` and persist it (CycleSet 1461-1483). If only the flag carried the kind, one Modes-page edit would drop it through the section rewrite at 1439, and the stage would reload as a default fluid look.
2. look=3 indexes past `kWarmupDefault[3]` (34, used at 298). WarmupOf returns 0 for a photo stage. LookName (123) and CycleStageLabel (1405) get "photo". ui_cycle.cpp:49 maps it to LOOK_F: acceptable in phase 1.
3. IsFluid (247) stays false and IsOtherLook (249) true, with no edits. Through those two, the photo stage gets the WE alternation (PickTier 635-645), the midpoint draw (1183), and no dark trigger (1194), no coverage (478, 1328), no journey (444) and no WE entry burst (1301). CycleBurst (1530) must refuse on a photo stage: FireBurst reads the HIDDEN look's acid config.
4. No overlay over a photo. Non-tier PickNext admits overlays after any other look (680). PickTier with no WE stage passes allowOverlays=true (636-637). Both must exclude overlays when IsPhoto(s_cur).
5. ApplyStage (485) on a photo: no Compose, no Config/peak/gamut write, no EnsureLookResources, no ResetLookState, no ApplyPaletteStart. Keep s_base=-1, PushHistory, s_cur, PersistCurrent, the log, and `s_entryDrop=false`. Guard every Compose caller against a photo stage: CycleStageBase 1652, CycleRevertStage 1667, WantsScheme 870 (already gated by look), BeginLerp/FinishLerp (gated by IsFluid).
6. The sim is NOT kept ticking. Every exit from a photo is BlackPoint -> ApplyStage -> ResetLookState + warm-up (1011-1019, 495); lerp, scheme and soft paths never apply (747, 863, 957). Ticking would buy nothing and cost a hidden 256/4096 sim at 144 fps. Frame() skips FrameSim while a photo is displayed (fluid.cpp:1825). The black WARMUP of a photo stage returns extraSteps=0 (cycle.cpp:1289) and does not wait for the hue glide (1292-1293).
7. Never boot on a photo stage: there is no renderer at CycleBoot (main.cpp:2923 vs 2935). Extend the overlay skip loop (cycle.cpp:1123) to photo, for `current=` resume (1120) and `--cycle-stage` (1119) too. A list with no look stage never enables; log it.
8. CycleStageFile (1662) returns "" on a photo stage. The UI header takes it as the preset path (ui_model.cpp:823-826), and Save writes live Config into that file (ui_presets.cpp:208,261), which would overwrite the photo ini. CycleSetStageIncluded (1701) marks any [look]-less file as an overlay: test [photo] first.
### B. Display path (fluid.cpp, shaders.h)
9. Use a FOURTH PSO in its own literal `kPhotoSrc`, following the pattern of kPostSrc/m_psoPost (shaders.h:3342, fluid.cpp:1257-1266). kDisplaySrc stays untouched, so dxbc-cmp is IDENTICAL by construction. Compile it lazily at the first photo black point via MakeGraphicsPso (467), after WaitForGpuIdle, the same way as EnsureLookResources (487-512).
10. NO root signature change (64 DWORDs are used: fluid.cpp:351-390). The photo binds b0 = param 0 (32 root constants) and its SRV through param 1's t0 table (fluid.cpp:355). It samples with the static linear-clamp s0 (385-388), and leaves params 2-8 unbound.
11. Descriptors: two ping-pong SRVs at heap indices 22 and 24 (texture slots 11 and 12). The sim uses indices 0-21 (CreateTex slot*2, fluid.cpp:574; slots 0-10 at 599-617) and post uses index 30 (1000). Write the new SRV only into the slot not being drawn.
12. Branch INSIDE RenderDisplay (906), RenderDisplayOffscreen (1274) and RenderMirror (1693) at the PSO bind, so the barriers and targets stay the same. For the photo, set `post=false` (skip RunPostPass), use no BindAcid/BindInk/BindMirrorFold, and skip MaybeRenderAnalyzer (1833). Build the photo constants per target size, with m_mirrorW/H and m_mirrorSdrScale for the mirror. The black-out path (1827) already covers the fade floor.
13. Constants: rect origin in integer px, 1/rect size, `sdrScale*m_fade` (the same product as fluid.cpp:1395), and an alpha flag. Output = SRGBToLinear(texel) * a * scale (the piecewise formula, shaders.h:1025). Outside the rect return exactly 0.
14. There is NO gamut matrix. scRGB is linear BT.709 (the SetColorSpace1 G10_P709 call, fluid.cpp:233-246), which uses the same primaries as sRGB. The display pass matrices EXPAND colour on purpose ("interpret the dye in a wider gamut", shaders.h:3249-3263). Applied to a photo, they would push sRGB red to (1.66, -0.12, -0.02). Drop the "gamut select" constant.
15. HDR off is the SAME shader. The swap chain is always FP16 scRGB (fluid.cpp:220). HDR off only means sdrScale=1 (main.cpp:3807 live, 2929 shot), and DWM maps 1.0 to SDR white. There is no tone map and no PQ in the app (PQ exists only in the shot writer, main.cpp:2630-2637). No peakGain: a photo never goes above sdrScale.
### C. Decode and upload
16. Put the loader in a new src/photo.cpp/.h and add it to CMakeLists.txt:16-17 (windowscodecs is already linked, :21). It covers the scan, the bag, the worker and WIC. It does not go in main.cpp: cycle.cpp cannot call main.cpp statics, and main.cpp is 3830 lines.
17. Worker: one persistent thread, one load at a time. It calls CoInitializeEx(COINIT_MULTITHREADED), creates its OWN IWICImagingFactory, and releases every WIC object on that thread. The shell threads are STA (main.cpp:2897, 3253), so no interface crosses threads. Shutdown() joins it before the device is released (fluid.cpp:2402).
18. WIC chain: CreateDecoderFromFilename -> frame 0 -> EXIF orientation via "System.Photo.Orientation" + IWICBitmapFlipRotator (the brief misses it: phone portraits would show sideways) -> IWICFormatConverter -> IWICBitmapScaler (Fant). The converter target is 32bppBGRA -> B8G8R8A8_UNORM, or 64bppRGBA -> R16G16B16A16_UNORM when the source is over 8 bits per channel (IWICPixelFormatInfo bits/channels). Scale to the fit rect in OUTPUT pixels, not to the brief's longest side <= 4096: 1:1 sampling is what makes the 1/255 proof hold. Fill = scale to cover, then IWICBitmapClipper centred.
19. Skip cloud placeholders (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS|OFFLINE) with a log line. Treat embedded ICC profiles as sRGB and log them. ICC transforms, HDR stills and gain maps are phase 2.
20. Upload: the device is free-threaded, so the WORKER creates the default texture (COPY_DEST) and one upload buffer. It uses GetCopyableFootprints (rows aligned to 256 bytes), Maps the buffer, CopyPixels straight into it, and Unmaps. The render thread only records CopyTextureRegion, the barrier to PIXEL_SHADER_RESOURCE and the SRV write, in the black frame on m_cmd/m_queue. It then stores `m_nextFence` as the analyzer does (fluid.cpp:1617). Release the upload buffer once that fence completes. Record the old texture's last-draw fence at hide time and release it once that completes.
### D. Folder, bag, skip
21. The default folder is dirname(g_iniPath)+"\photos", the same derivation as GetPresetsDir (main.cpp:1466; g_iniPath from InitSettingsPath 184-195). Expose `GetPhotosDirectory` beside GetPresetsDirectory (main.cpp:2236, app_state.h:23). Create the folder in EnsureBuiltinPresets (1553). That function does nothing when read-only, so shots never create it, and neither does the Claude app's MSIX shadow %APPDATA%. Tray: CMD_PHOTO_FOLDER=32 (free: main.cpp:1052-1070; 33/34 are retired) beside 1182, with its handler beside 1377.
22. Shot mode sets g_iniPath to the REAL %APPDATA% (2888). So under g_configReadOnly an empty `folder=` means "no folder", unless a new `--photos <dir>` flag is given. Otherwise identity would depend on the user's photos.
23. Scan at DRAW time with FindFirstFileEx (Basic, LARGE_FETCH), non-recursive, extension filter, names sorted case-insensitively. It takes under 1 ms locally. Cache one scan per director tick.
24. An empty stage is removed from the candidate list BEFORE any NextUnit(), so the RNG stream is identical to the list without that stage. That keeps cycle-final and cycle-first identity MATCH once the stage is appended (with the item 22 gate). Apply this in DrawTiered 527, PickNext 677/684, the fixed loop 658-662 (pass-over), PickPrev 718-729, TakeQueued 614 (re-check), CycleJump 1356 (refuse and log) and CycleDrawTest. Log `[cycle] photo stage skipped: no photos in <folder>` once per draw.
25. The bag uses its OWN splitmix stream, seeded from (seedOverride ? : cfg.seed) ^ a constant, where 0 = wall clock (SeedRng 145-148). Never call NextRand: it would shift the director's walk. Bag = unshown names. New files join and deleted files leave. On refill, exclude the last-shown file. A file counts as shown only when its fade-in starts. Failed files stay out for the whole run.
26. Start the load wherever a photo stage becomes the target: BeginSwitch into FADE_OUT (963), re-target in FADE_OUT (932-934), re-target in WARMUP (924-929), and CycleJump. At the black point, the photo WARMUP gate is "texture ready", not sim seconds.
27. Live: black holds up to 5 s. On timeout or failure, mark the file failed and FALL BACK: PickNext with this stage excluded, then ApplyStage of that stage in place (the 924-929 pattern) and a normal warm-up. The brief never says what shows after a skip at black. Shot mode JOINS the worker at the black point, so the timeline stays frame-deterministic.
### E. Keys
28. No keys.inc rows. keys.inc has only KEY_SLIDER/KEY_CHECK (keys.inc:5-7) with no text kind. Looks must be a subset of FAI (keymeta-check.ps1). Pointers go into the live FluidConfig, which a photo never reads.
29. cycle.cpp reads `[photo] folder` (resolved like Resolve 173, relative to the stage ini) and `fit=fit|fill` from the stage file at scan time, the same way as CLOCKS phase 1 (CLOCKS item 27). `drift` is CUT: it would be an unread key and would need its own A/B sheet (slider-range-policy). keymeta-check and SETTINGS.md are unaffected.
30. The stage file ships as reference/configs/photo-stage.ini (with `[photo]` and no `[meta] look=`). With `[meta] look=photo` in the presets folder, the tray and the UI library would classify it as an overlay (main.cpp:1119-1123; ui_presets.cpp:136-141).
### F. Proof
31. Test images at the NATIVE fit size for 2560x1440, so there is no scaler in the numbers: bars-16x9 2560x1440, bars-4x3 1920x1440 (pillarbox 320), bars-21x9 2560x1098 (letterbox), grey16 (16-bit horizontal ramp 0..65535) plus grey8 (the same ramp in 8 bits), and one off-size image (1600x1200) for the scaler. Make them with PIL (12.3) and numpy; cv2 is available for 16-bit reads. Each case goes in its own sub-folder under reference/photos-stage/.
32. A one-stage list never revisits: PickNext returns s_cur, and the director dwells again (1206-1209). With no look stage it cannot boot (item 7). So test with `reference/configs/cycle-photo-test.ini`: order=fixed, stage 1 = we-look-live at 5 s, then one tiny stage ini per sub-folder (reference/configs/photo-test/*.ini) at 10 s, jitter 0, `--shot-series` mid-dwell. Add manifest rows: cycle-photo-test Look=cycle Tier=fast, and the photo-stage/photo-test inis Look=test Tier=skip. A [photo]-only ini under --ini renders the default fluid look, so it is no identity. cycle-final (Tier full) must MATCH after the stage is appended.
33. Measurements. (a) The [shot] line's `max_scRGB` must be 3.000 with --hdr on (sdr-white 240) or 1.000 with --hdr off, with `above_sdr_white=0.00%` (main.cpp:2679). Cross-check with `tools\jxr-check.ps1 <jxr> -ExpectNits 240`. (b, c, d) The 8-bit <stem>.png is /sdrScale (2652-2657): bar and ramp centres must equal the source bytes ±1 in BOTH modes, and pillarbox/letterbox bytes must be 0. (e) On the -pq.png (16-bit, cv2 IMREAD_UNCHANGED), grey16 must show more distinct levels than grey8.
34. Frame time: add QPC timing around CycleTick..Frame in the shot loop (main.cpp:~3103-3110), excluding Sleep (3114). Log `[photo] max frame N ms`, both over the load window and for the preceding 5 s baseline, at the [state] site (3157). Share that site with CLOCKS item 26(e). No live-instance run.
### G. Scope and brief errors
35. PHASE 1 is items 1-34: the kind, the director guards, kPhotoSrc, the worker with 8/16-bit + orientation + fit/fill, the folder/bag/skip/fallback, the tray item, the logs, and proofs 1-6 and 7 (manifest + FEATURES rows). The .webp/.heic/.avif extensions are tried (WIC fails) and logged once per extension. PHASE 2 is Sonnet-rote UI work: the header `Photo · <name>` (ui_model.cpp:873-876), the Modes-page folder row/"Open photos folder" button and a stage-kind label. After that: drift, ICC, HDR stills, the presets-folder classification, and appending the stage to cycle-final.ini (content: creative chat's §7 call).
36. Brief errors (fixed above):
    - "gamut matrix" (14) and "HDR off 8-bit pass-through" (15).
    - "sim keeps ticking" (6) and "keys.inc rows" (28).
    - `[meta] look=photo` (30); the SHIPPED cycle is cycle-final.ini, not cycle-first.ini (tools\cycle-install.ps1:1-3,14).
    - look=3 overflow (2), the stage-only proof list (32), and the missing skip-at-black fallback (27).
    - The 4096 cap (18), missing EXIF orientation (18) and the persisted flag (1).
    - The photo does not need CLOCKS group weights: ClocksTick varies only the hidden Config. The branch still waits for CLOCKS because both touch cycle.cpp and the frame loops.

## FABLE DECISIONS on the pre-flight (2026-09-27 00:45) — binding
- All 36 items accepted; where the brief and the pre-flight disagree the pre-flight wins (gamut: none; HDR off = same
  shader with sdrScale 1; sim not ticking under a photo; kind from the file's [photo] section AND persisted flag;
  ships as reference/configs/photo-stage.ini; drift cut; EXIF orientation in; scale to the fit rect).
- Phase 1 = items 1–34. Phase 2 UI (header, Modes row) = Sonnet rote after merge. Appending the stage to
  cycle-final.ini is the creative chat's §7 call, then the user sees it at the swap after.
- Branch `photo` waits for CLOCKS phase 1 to merge (shared cycle.cpp / frame-loop touch points).
