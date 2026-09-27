# PHOTO-STAGE phase 1 -- resume state

Worktree C:\Users\abg77\fw-photo, branch `photo` from main bc4c6ef (CLOCKS phase 1 merged). Do not merge.
Brief: reference\briefs\PHOTO-STAGE.md (+ AUDITOR PRE-FLIGHT items 1-36, + FABLE DECISIONS).
Scratch: %TEMP%\claude\...\scratchpad\photo\ (gpujob.ps1 = one job under the main repo's gpu.lock;
job-*.ps1 = the jobs; gpujob.log = lock waits). Renders: C:\Users\abg77\fw-photo\build2\shots\photo\;
sheets -> MAIN repo build2\shots\live\photo\.

## Milestones
- [x] M0 code written (syntax-checked with cl /Zs; dxbc-cmp IDENTICAL x3; slot-check PASS;
      keymeta-check OK; kPhotoSrc compiles vs/ps_5_0):
      src\photo.cpp/.h (scan, bag, WIC worker), cycle.cpp/.h (kind, guards, load/gate/fallback),
      fluid.cpp/.h (kPhotoSrc PSO, upload record, DrawPhoto in the 3 display passes, photo mode),
      shaders.h (kPhotoSrc, own literal), main.cpp (photos folder, tray CMD_PHOTO_FOLDER=32,
      --photos, shot logger/sync, frame timing), app_state.h, CMakeLists.txt; test images +
      tools\photo-stage-images.py; reference\configs\photo-stage.ini, cycle-photo-test.ini,
      photo-test\*.ini; manifest rows; FEATURES.md rows.
- [x] M1 build OK 16:11 (e694e60); smoke cycle-photo-test 1280x720 t=19/33: bars-16x9 + bars-4x3 shown,
      max_scRGB 3.000, above_sdr_white 0.00%, pillarbox correct; decode 9-11 ms; first load window max frame
      16.6 ms = the one-time lazy kPhotoSrc compile (16 ms, at black), second window 0.87 ms vs 0.55 baseline.
- [x] main-side identity (fw-wt rebuilt on bc4c6ef): scratch\identity-main-bc4c6ef.txt, parity MATCH.
- [x] proof 6 draw test (no GPU): tier mode 20000 draws, WE-alternation breaks 0, overlay-after-photo 0.
- [ ] M2 identity: main exe (fw-wt rebuilt on bc4c6ef) vs photo exe, parity, dxbc, slot/keymeta
- [ ] M3 photo sheets HDR on/off + numbers.txt (item 33)
- [ ] M4 empty-folder skip + re-scan; notjpeg + 30 MB JPEG + max frame; alternation draw test
- [ ] M5 report

## Queue (background, BelowNormal, --shot-yield 30)
- job-main-identity.ps1: fw-wt checkout bc4c6ef + build + identity (parity + fast rows + cycle-final/
  first/lerp-test + clocks-off-test) -> scratch\identity-main-bc4c6ef.txt
- job-build-smoke.ps1: build fw-photo + smoke render

## Decisions / deviations so far
- Stage kind: [photo] section OR stage_K_photo=1; ResolveStage returns early for a photo (no base,
  look=3, overlay=false, burstOk=false); CycleSet persists stage_K_photo=1.
- Draw filter Drawable(j) = Valid && not the fallback-excluded stage && (photo -> folder has a usable
  file), applied before any NextUnit in DrawTiered / PickNext (fixed + random) / PickPrev /
  TakeQueued / CycleJump; skip line once per stage per director tick.
- OverlayBlocked: no overlay over a photo in PickTier, PickNext (both), PickPrev, TakeQueued,
  BeginSwitch (refuse + log) and CycleJump.
- Renderer: photo mode from PhotoEnter (black point) to PhotoLeave (next look's black point, or
  GoOff); FrameSim and SimOnlyStep skipped in photo mode; upload recorded only after PhotoEnter
  (m_photoAccept), so a photo -> photo switch never swaps the texture mid fade-out.
- Fullscreen-suspend: Shutdown joins the worker, drops photo GPU objects, keeps photo mode + file;
  PhotoPump re-requests after the re-init (black until back).
- 1:1 exact path: kPhotoSrc uses Load() when the rect equals the texture (the main output), Sample
  (linear clamp s0) otherwise (the second-monitor mirror).
