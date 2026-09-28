# BLACK-MASS phase 1 -- resume state

Worktree C:\Users\abg77\fw-blackmass, branch `blackmass` from main 14b01aa (CLOCKS + PHOTO phase 1 merged).
Do not merge. Brief: reference\briefs\BLACK-MASS.md (+ AUDITOR PRE-FLIGHT 1-26, + FABLE DECISIONS: ROLE A --
on the monotone base the FILM is lit, the black masses are the GAPS + kind-0 droplets; every recipe in the brief
and the study is inverted). Targets: handoff\review\creative-brief-black-mass.md (main repo, untracked).
Phase 1 = pre-flight item 25. Rafts NOT built (user decision pending). Layout clock = phase 2.

Scratch: %TEMP%\claude\...\scratchpad\bm\ (job scripts, logs). Renders: C:\Users\abg77\fw-blackmass\build2\shots\blackmass\
(per run: <label>-<hdr>\s<seed>-NNN.png, s<seed>.log = the exe's own stdout, black-share.json, summary.txt).
Sheets -> MAIN repo build2\shots\live\blackmass\. Main's exe for identity = C:\Users\abg77\fw-wt (detached, moved
bc4c6ef -> 14b01aa this session, rebuilt under the lock).

## Milestones
- [x] M0 code + tool (no GPU):
  - src: `[liquid_acid] blob_turnover_s` (fluid.h LiquidAcidConfig, default 0), `float born` in AcidBlob (set to m_time
    in AdjustAcidBlobCount's grow-in only), m_acidTurnAcc/m_acidTurnRng/m_acidTurnBlockedLogged, StepAcidTurnover()
    called from Frame() inside the conserve_mass branch only when blob_turnover_s > 0 (and it re-checks
    conserve_mass > 0.5). Pick per item 4 (candidates rTarget > 0 && baseR >= 0.9 rTarget; clear-of-frame first,
    then oldest born, then an index hash on the private xorshift). Accumulator reset at all three m_acidSeeded=false
    sites + in SeedAcidBlobs (which also seeds the private stream from g_randSeed without drawing from `rng`).
    Hazard 5a: size >= 127 -> skip, accumulator held at one period, `[turnover] blocked` printed once.
    Hazard 5b: respawn floor uses max(baseR, rTarget) when turnover > 0 and conserve_mass. Each retire prints
    `[turnover] retire t=.. blob=.. kind=.. r=.. born=.. clear=.. y=..` to stdout (never the shared ShotLog).
  - main.cpp getF/putF beside dissolve_s; keys.inc row after dissolve_s (G_OIL, gate conserve_mass>0.5,
    KF_MOTION, zero:off), range 0..300 step 5 PROVISIONAL until the sheet.
  - tools\black-share.py (measure: max(R,G,B) < 40 on the 8-bit SDR png; `merge` = base + partial, overlay keys
    first, the preset-identity composition) + tools\black-share.ps1 (BelowNormal, lock per seed, 1280x720,
    series 31:10 from T0 30, yield 30, png-only, per-seed mean/min/max + mean of means, summary.txt).
  - dxbc-cmp vs main 14b01aa shaders.h: IDENTICAL x6 (shaders.h untouched); slot-check PASS; keymeta-check OK.
- [x] M1 19:49 build OK (lock), fw-wt moved to 14b01aa + rebuilt; smoke monotone 1280x720 seed 1234 t=10/15/20:
      black 4.0/9.2/6.5 %, 34 ms/frame at yield 30 (a 330 s series = ~27 min/seed). Black masses render ~14/255
      (counted); kind-0 droplet interiors ~35-50 (partly above 40, so droplets are undercounted by the metric).
- [x] M2 MEASURE FIRST (00:00-01:28): monotone-post-0924 + acid-rise-12 black % (300 s, 3 seeds, HDR on; HDR off once)
- [x] M3 determinism (01:33-02:14): monotone + blob_count 80 + blob_turnover_s 90, 1280x720, 150 s, seed 1234, yield 8:
      run a A994D8621C935E292407423E501DE30B, run b A994D8621C935E292407423E501DE30B (EQUAL). 133 retires in 150 s,
      none blocked (80 live + <= ~40 retirees < 127); first cohort picks clear=1 bubbles, then discs in view; by 147 s
      the retirees are grow-ins born 56-58 s (lifetime ~90 s as designed). WORKS: vs turnover 0 (A0E792BB...) MAD 48.6,
      max 254; black 11.9 % vs 22.2 % at that instant (pair: build2\shots\blackmass\det\pair.png).
- [ ] M3b range sheet for blob_turnover_s (the strip at 45/90/180)
- [~] M4 screening round 1 (seed 1234, 300 s, yield 8): FG1 (blob_count 80, turnover 90, 3 holes .15-.30 w 2.5,
      droplets 500, T0 90) 11.2; LR1 (threshold .90, support 1.8) 5.6; SF1 (threshold .70, droplets 1800, ink_frac .08,
      r_max .016, mass_bias .3) 5.4. Reading: at 126 blobs the film stays continuous whatever threshold/support do;
      black = only the edge gap masses. Droplet interiors read ~45-70 in the SDR png (bloom/halo lift) so they are
      mostly NOT counted by max<40 -- only the big droplet cores are. FG2/3, LR2/3, SF3 dropped (same family);
      round 2 (q5.ps1): FG4/5/6 = blob_count 40/50/60 (+3 holes, turnover 90), LR4 (thr 1.10 sup 1.5), LR5 (70 blobs,
      thr .95, sup 1.7, stretch .8), LR6 (56 blobs, stretch .8), SF2 (thr .68, 2400 drops, r_max .018), SF4 (thr .65,
      2400 drops, r_max .024, droplet_bias 2.0).
- [ ] M4 Layout partials (Few giants / Lava rise / Starfield) from a first sheet
- [ ] M5 proofs 20 (identity/parity/dxbc/slot/keymeta), 21 (strip + popdetect), 23 (contact sheet); manifest,
      FEATURES rows; report

## Measured (black % = share of max(R,G,B) < 40 on the 8-bit SDR png, 1280x720, 300 s window)
- TODAY = monotone-post-0924, HDR on, t 30..330 s: seed 1234 6.4 (0.0..17.9), 5678 11.5 (0.2..36.7), 9012 10.6 (0.9..28.1);
  mean of means 9.5 (spread 5.1). Swings hard inside a seed (one big gap mass rising through the frame).
- acid-rise-12, HDR on: 1234 7.7 (0.7..25.2), 5678 17.0 (0.9..43.6), 9012 9.5 (0.0..40.5); mean of means 11.4 (spread 9.3).
- monotone HDR OFF seed 1234: 6.4 (0.0..17.9) = HDR on seed 1234 6.4 (per-frame max |on-off| 0.00 over 31 frames: the 8-bit SDR png is the same view in both modes).
  (seeds at --shot-yield 30; from 21:45 the coordinator switched the user-away renders to yield 8)

## Decisions / deviations
- Turnover pick: the clear-of-frame preference applies only among DUE blobs (the oldest cohort, or born at least
  half a turnover period ago). Reason: a fresh grow-in parks under the bottom edge fully grown (spawn_grow_s 4 vs a
  ~25 s climb into view), is "clear of the frame" and would be retired before ever being seen, so the standing
  population would never turn over. Among the seeded cohort (all born 0) the pre-flight order holds exactly.

## Queue (scratch\bm\q3.ps1, running from 19:53)
Coordinator 21:00: sleep 15 s before every lock take (black-share.ps1 now does; q3's inline det loop does not,
so q3 is ended by build2\shots\blackmass\STOP after M2 and q4.ps1 = det + screening with the gap).
M2 (monotone on x3, acid-rise-12 on x3, monotone off x1) -> M3 det (t90 a/b + t0, blob_count 80, 150 s) ->
screening round 1 (seed 1234): FG1-3 (blob_count 80, turnover 90, 3-5 big holes, T0 90), LR1-3 (threshold/support),
SF1-3 (thin film threshold, droplets up, ink_frac down). Log scratch\bm\q3.log.
