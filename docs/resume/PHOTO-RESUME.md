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
- [x] M2 identity (18:19): photo exe ad2b421-build vs main bc4c6ef exe: 19/19 UNCHANGED (parity + 14 fast rows +
      cycle-final/first/lerp-test + clocks-off-test), parity MATCH 835AECBD; cycle-photo-test NEW 5147ADA1FDD3DDA8B15E75485F4C477E;
      dxbc IDENTICAL x3, slot-check PASS, keymeta-check OK (M0).
- [x] M3 sheets (18:19-18:34): cycle-photo-test 2560x1440, HDR on AND off: 7/7 PASS each (max_scRGB 3.000 / 1.000,
      above_sdr_white 0.00 %, bar/ramp centres |d| <= 1, box bytes 0, fill seam row 911 (912 expected, Fant edge),
      grey16 2560 pq levels vs grey8 256). MAIN repo build2\shots\live\photo\sheet-{hdr,sdr}.png + numbers.txt.
- [x] M4 p4: empty folder -> skip line, ink -> WE with a normal fade; a file dropped mid-run is shown at the next
      visit (no restart). p5: notjpeg.jpg skipped (0x88982F50) -> fallback to WE at black; big-30mb.jpg 4500x3000 ->
      2160x1440 decode 229 ms upload 3.9 ms; max frame in its load window 1.80 ms vs 0.98 ms baseline.
      p6 draw test: WE-alternation breaks 0, overlay-after-photo 0, photo-after-photo 0 (20000 draws).
- [x] M5 report handed back.

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
