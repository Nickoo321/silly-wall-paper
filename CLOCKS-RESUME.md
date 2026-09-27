# CLOCKS phase 1 -- resume state

Worktree C:\Users\abg77\fw-clocks, branch `clocks` from main 5d9c39b. Do not merge.
Brief: reference\briefs\CLOCKS.md (+ AUDITOR PRE-FLIGHT items 1-30, + FABLE DECISIONS).

## Milestones
- [x] M0 code written: src\clocks.cpp/.h, hooks in cycle.cpp (s_from / s_amtFrom / s_preOverlay
      take the base, GoOff restores it, CycleSeed(), AnimatorName), main.cpp (ClocksTick after
      CycleTick in both loops, [clocks] load + seed, --clocks-dryrun, --clocks-force, --shot-freeze
      clocks, --clocks-hold-at, save paths take the base, [clocks] report at [state]), ui
      (ClocksHold at create / input / WM_DESTROY, row settling lock, undo snapshot base, UA_CLOCKS),
      animators.h ANIM_CLOCKS, CMakeLists, FEATURES.md rows, test inis reference\configs\clocks-*.ini.
- [x] M1 build OK 04:30 (lock won only after BX's queue ended). Dry runs (pre mean-fix, f64e6e5): Base 240 h
      all PASS (1.083 tails/h, min gap 20.2, 0 repeats, body 0.0093/s); Calm-like 0.775/h; Wild-like mean F
      0.94-0.96 FAIL -> fd0da15 solves mu per group so the CLAMPED body has E[F]=1. Re-run dry runs on the rebuilt exe.
- [~] M2 identity: main side DONE (build2\shots\clocks\identity-main-5d9c39b.txt, parity MATCH, clocks-off-test == cycle-final); clocks side queued after 3e: fast tier + cycle-final/first/lerp-test + clocks-off-test.ini, main exe
      (C:\Users\abg77\fw-wt\build2, == main src) vs clocks exe; parity; dxbc-cmp; slot/keymeta-check
- [~] M3 proof 3e (running from 07:43, rebuilt exe): 900 s headless on clocks-mono-cycle.ini (+ hold / freeze windows)
- [x] M4 sheet: main repo build2\shots\live\clocks\groups-mono-{hdr,sdr}.png (+ groups-mono-meanlum.txt).
      Rendered on the f64e6e5 exe (force columns; mean fix only moves mode F 0.874->0.877). F=1 column md5 ==
      main's monotone md5 A060338C (verbatim proven). No tail column > 1.25x; hue2 tail inert on monotone
      (amounts 0, bit-identical); grain tail = blocky macro grain (size keys x2.2) -> recommend dropping
      grain_scale / film_grain_size / film_noise_size from the tail.
- [x] Dry runs on the final exe (fd0da15): Base PASS 1.075/h; Calm-like 0.817/h; Wild-like 2.80/h, means PASS.
- [ ] M5 report

## Scratch
- scripts + logs: %TEMP%\claude\...\scratchpad\clocks\ (gpujob.ps1 = one command under the lock)
- renders: C:\Users\abg77\fw-clocks\build2\shots\clocks\ ; sheets -> main repo build2\shots\live\clocks\

## Notes / findings so far
- BX queue (fw-bx after-queue.ps1) re-grabs the lock immediately after each render; my 5 s poll
  races it. Its identity phase holds the lock ~80 min twice.
- Excluded beyond the pre-flight (reasons in clocks.cpp kSpec comments): lid_scratch_density,
  lid_scratch_len (static hash<amount populations: pops), lid_ghost_spread (4-bit pack),
  lid_scratch_corner / curve_center (placements), bloom_px / bloom_warmth (bloom), droplet_mass_bias
  (population), film_equal_load(_patches), hue2 layout/rate keys, rig extents/times.
- Reel-quantised: film_hairs / film_scratches / film_leak commit only at their reel re-roll
  (shader gates each slot by hash < amount per reel -> a ramp would pop a hair in mid-life).
