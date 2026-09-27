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
