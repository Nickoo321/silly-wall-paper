# BX resume (grey heart) -- executor #2, started 2026-09-26 14:14

## Milestones
- [x] 14:20 spec-update diff reviewed + committed: 6110cbd (grey = K*sqrt(hm), lift = L*hm*alpha, preset 0.75/1.7/0.45).
      CPU fit at S1.3 K.75 L.45 vs user table: plain smoothstep chroma error up to .15, sqrt(hm) <= .04 on the ramp.
- [x] merge main 8881adb -> a6e7c0f (clean; no src change, so the 13:58 build2 exe == current source).
- [x] slot-check PASS, keymeta-check OK, dxbc-cmp vs main: fluid/ink PSO IDENTICAL, acid PSMain DIFFERS (expected).
- [x] ui-dump / ui-shot (WARP): build2\shots\live\bx\ui-shot-heart-{on,off}.png, ui-heart-rows-on-off.png,
      ui-dump-heart-{on,off}.json (off: size+lift disabled "needs Grey heart > 0"; on: all three live).
- [x] parity with the bx exe: bx\r\we-look-live.png = 835AECBD9EF1384A8CAAF1A611EE3A26 MATCH.
- [ ] render queue (dead executor's INLINE script, wrapper pid 33800, the 30 jobs of build2\shots\bx\jobs.txt,
      ~5.3 min each, started 14:12 with mono-off; outputs build2\shots\bx\r\). Exe 13:58:23 postdates every
      source edit (latest 13:57:33) -> every queue render is valid for the heart maths; nothing to re-render.
      NOTE: bc/mm/mc job inis carry film_equal_load_patches 0.75; main now ships 0.5 on those presets
      (orthogonal to the heart; off vs heart pairs share the same base).
- [ ] 14:21 after-queue.ps1 launched detached (pid 27348, BelowNormal, log bx\after.log): waits for pid 33800,
      renders missing jobs.txt rows + jobs-extra.txt (m325-off/k75/s13 = palette_start_hue 325 magenta, for
      heart-vs-user + the measured column), then identity with the main-code exe (fw-wt, src == main 8881adb)
      -Save identity-main-8881adb.txt, then the bx exe -Baseline (id-bx.log). If it died: relaunch
      (cmd /c start "" /belownormal powershell -File ...after-queue.ps1); it skips outputs that exist.
- [x] 14:23 early numbers: mono-off -> mono-k75 (preset): mean_lum 0.7373 -> 0.9733 (+32.0%, yellow film at seed 1234);
      centre flat-film OKLab C 0.157 -> 0.056 (kept 0.35); core mass crop max 0.129 -> 0.141.
- [!] PC powered off after 14:23 (queue died in job 2 bc-off). Rebooted 22:38; coordinator removed the stale lock.
      22:53 after-queue.ps1 patched (no pid-33800 wait) and relaunched detached (pid 26364, BelowNormal):
      renders the 28 remaining jobs.txt rows + jobs-extra.txt, then both identity runs. Log bx\after.log.
      Sheets/numbers script: bx\bx2.py (python bx2.py sheets numbers).
- [ ] sheets -> main repo build2\shots\live\bx\ ; numbers ; FEATURES.md rows ; final commit
- [x] 2026-09-27 04:30 after-queue ALL DONE; identity fast tier: 15 unchanged, 0 changed (id-bx.log; baseline identity-main-8881adb.txt).
- [!] 10:31 bc-k75 and mc-k75 had rendered NO output overnight (15 / 10 min, empty md5) -> redo.ps1 (log lines go to after.log) re-renders them.
- [x] sheets written to live\bx (bx2.py sheets); rerun after the redo. FEATURES.md rows added (bc 0.45 mean_lum pending).
- Findings: mean_lum at lift 0 is -3..-4.5% (target 1%: fails; 709 hold vs gamut-2 clamp bonus); magenta (hue 325) preset keeps 0.50 chroma / x1.22
  in the centre vs the edit's 0.25 / x1.43 (yellow monotone at size 1.3 matches the table within ~0.05).
- [x] 10:42 redo done (bc-k75 069226..., mc-k75 834676...); sheets rebuilt into live\bx (6 sheets); FEATURES rows final; committing.
- [ ] 2026-09-27 10:45 FIX ROUND (Fable): heart moved AFTER the gamut stretch as an OKLab a/b x (1-g) desaturation
      (HeartGrey, shaders.h) + hold on the scRGB 709 Y (what mean_lum sums) x lift. LampGreyY (corner) untouched.
      Cause found: Y-flat linear desat keeps a hue-dependent OKLab share (CPU: 0.20 yellow / 0.32 magenta / 0.35 blue at 0.75),
      then post_chroma 1.2 (shaders.h final trim) and the gamut-2 stretch + clamp add ~+0.07 more.
      slot-check PASS, dxbc fluid/ink IDENTICAL. v1 outputs of the 18 re-rendered jobs moved to bx\r_v1\.
      fix-queue.ps1 launched 10:48 (detached, log bx\fix.log): build under lock (waiting on CLOCKS' lock), 18 renders
      (jobs-fix.txt), then identity bx vs identity-main-8881adb.txt -> id-bx2.log. Corner proof: mono-off md5 before
      a060338c929af4ec409df324ad4cc49d (r_v1\mono-off.png) must equal the new r\mono-off.png.
- [!] 12:43 v2 (display-pass OKLab heart after the stretch) built + rendered: corner/heart-0 mono-off md5 A060338C... UNCHANGED,
      but plateau kept 0.342 (lift 0) / 0.389 (preset) on yellow; amount 1 residual 0.071; lift-0 mean_lum -1.3/-1.8%.
      Cause: the [post] pass (bloom warmth, halation warmth, glow, grain chroma, film stock) re-adds colour + luminance.
- [x] v3 (13:18 exe): desaturation moved to the END of kPostSrc (BX_HEART variant = BN_OPTICS + BX_HEART PSO, on demand,
      fields 3/4 of rg1.y = amount, (size-0.8)/1.4); display pass does only the film lift when [post] runs
      (HEART_K written negative), the full OKLab heart when it does not. dxbc: display fluid/ink IDENTICAL, post base and
      BN PSOs IDENTICAL to main, heart variant compiles. slot-check PASS. v2 outputs in bx\r_v2\.
      Incident: the v2 queue's identity step failed to create its scratch dir (my stop trick) and would have sat 20 min/row
      holding the lock; I built v3 during that idle wait and turned id-bx2 into a dir, so that identity run now renders
      the 14 rows with the v3 exe (parity row = TIMEOUT; parity re-rendered as job parity-v3 in fix-queue3).
      fix-queue3.ps1 (render-only, jobs-fix3.txt, log fix.log) waits for that identity to release the lock.
- [x] 14:39 identity with the v3 exe (id-bx2.log): 14 unchanged vs identity-main-8881adb.txt incl. monotone-post-0924
      (= mono-off, corner lamp_grey 0.6 on: A060338C929AF4EC409DF324AD4CC49D, same as before the fix) and WE parity (fluid)
      preset 835AECBD...; the we-look-live row TIMEOUT (scratch-dir incident) -> job parity-v3 re-renders it.
      fix3 queue waiting: lock taken by "PHOTO main-identity" at 14:39.
- [x] v3 renders so far (fix.log): parity-v3 835AECBD... MATCH; mono-off A060338C... UNCHANGED (corner proof);
      mono-k75 plateau kept 0.286 (pq) / 0.284 (png); bc-k75 0.279 / 0.287; mono amount 1 lift 0 C 0.0002;
      mono lift 0 mean_lum +0.00% (0.75 and 1). 16:43 PHOTO took the lock between my renders; queue resumes after.
- [x] 19:49 FIX ROUND DONE (v3 exe 13:18): acceptance met. Plateau kept at the preset 0.75/1.7/0.45 (OKLab C from -pq.png,
      flat film r<0.51): mono 0.286, Blue Coral 0.279, magenta 325 0.271 (png .284/.287/.273); amount 1 lift 0: C 0.0002 on all
      three; lift-0 mean_lum mono +0.00% (0.75 and 1), Blue Coral -0.03% (0.75 and 1), magenta +0.00% (1). parity-v3 835AECBD MATCH;
      mono-off A060338C unchanged; identity (v3) 14 unchanged. Preset mean_lum now +35.5 / +35.2 / +34.2%.
      Sheets regenerated: live\bx\heart-vs-user.png, heart-home-4.png, ab-lamp_grey_heart.png. STALE (v1 renders):
      ab-lamp_grey_heart_lift.png, ab-lamp_grey_heart_size.png, heart-sdr.png. FEATURES rows + fluid.h comment updated.
- [x] 20:00 merge main 14b01aa (CLOCKS + PHOTO phase 1) -> 34f09d7: auto-merged, NO conflicts (heart rows in keys.inc/main.cpp,
      stage 21, manifest row all kept; tray ids untouched). src/clocks.cpp: lamp_grey_heart (GR_RIG), _lift (GR_RIG, K_ABL),
      _size (GR_RIG, K_NOTAIL). v4 build 19:56 (under lock). slot-check PASS, keymeta OK, dxbc: fluid/ink IDENTICAL,
      post base + BN IDENTICAL, acid differs. --clocks-dryrun 24 --seed 1234 (needs --shot to enter shot mode):
      byte-identical to main's exe; 24 h has lid FAIL on main too; 240 h RESULT PASS (1.075 tails/h).
      INCIDENT: my first dryrun call lacked --shot, so the exe started as a normal wallpaper (WorkerW, the user's
      settings.ini, cycle on) for 10 min until my own timeout ended it (~19:58-20:08).
- [ ] 20:09 v4-queue.ps1 (detached): clocks table probe, 12 stale-sheet renders (jobs-v4.txt), identity -Presets 19 rows
      vs identity-main-14b01aa-composed.txt (PHOTO's bc4c6ef baseline + cycle-photo-test 5147ADA1). Log fix.log / id-v4.log.
- [ ] 21:05 v4-queue.ps1 (pid 32064) still waiting for the GPU lock: the BLACKMASS executor re-takes it between its series
      (19:56, 20:24, 20:51) faster than the queue's 5 s poll. My attempt to stop and relaunch the queue with a faster poll
      was DENIED by the permission classifier; the queue was left running untouched and will proceed when it wins the lock.
      Remaining after it finishes: bx2.py sheets (lift/size/sdr) -> live\bx, id-v4.log check, final commit, report.
- [x] 2026-09-28 00:52 V4 DONE (queue won the lock 21:15): clocks probe "[clocks] table: 109 keys (... rig 18 ...); 9 ABL-capped,
      9 body-only"; identity (id-v4.log) PARITY MATCH 835AECBD..., diff 20 unchanged / 0 changed / 0 missing / 0 new vs main 14b01aa
      (composed baseline). Sheets regenerated in live\bx: ab-lamp_grey_heart_lift.png, ab-lamp_grey_heart_size.png, heart-sdr.png
      (v4 renders); FEATURES lift row = v4 sweep. All six BX sheets are now post-fix.
