# FINAL CYCLE — cycle phase 2 + scheme coverage (brief, 2026-09-25, from creative decisions 2 + 3)

Read EXECUTOR-CARD.md (incl. §7), AGENTS.md, CYCLE-DIRECTOR.md (what exists), BV-colour-schemes.md,
UI-ANIMATORS-MODEL.md. Main bdcb41b has: director phase 1, BU/BU-b split tone, BV schemes (16 presets),
Settings 1a+1b (Cycle playlist pane reads [cycle]). Creative verdicts (implementation agreed, logged in
handoff\review\DECISIONS.md) are BINDING below.

## A. Stage list (cycle-first.ini → becomes the shipped cycle)
OUT: acid-rise-2hue (= Violet/Amber pattern), acid-rise-rotate (= stage 1 + anchor drift),
lapd-look-candidate (dyed masses break the black-oil rule; user rejected on the panel).
KEEP: monotone-post-0924 (home), we-look-live (home), acid-rise-12 (moderate: the crisper pre-filmic
lens, blue film), ink-inverted (wild), Mirror - quad overlay (wild).
ADD: every Scheme preset as an oil stage (14 patterns): HOME tier = Return to Form, Lightroom,
Magenta/Mint, Magenta/Cyan, Blue/Coral, Red/Sky, Violet/Amber; 3-COLOUR tier = Teal/Orange (moved
from home), Neon Demon, Euphoria, Warm Arc, Bright Triad, Bright Split; 4-COLOUR tier = Synthwave
(third +55 → +70), Microscope, Square+.
Weights are PER TIER: 7 : 2 : 1 (proven : moderate : wild) split among the tier's members; within HOME
the split is Return to Form ×3, Lightroom ×2, Magenta/Mint ×1.5, Magenta/Cyan ×1.5, Blue/Coral ×1.5,
Red/Sky ×1, Violet/Amber ×1. WE stages alternate with every oil/ink/overlay stage as before.
Dwell: default 180 s, max 240 (WE 240, oil 180, ink 150, overlay 120), jitter 0.3.

## B. Director rules to add
1. Entry content: a WE stage fires one splat burst at fade-in; an ink stage fires one drop at entry.
   A stage never opens on an empty screen.
2. Hue bursts under oil = a RARE EVENT, not a standing setting: hueshift_enabled is forced OFF for oil
   stages by the director; instead a wild-weight "burst moment" (weight 1 alongside the wild stages)
   triggers ONE tamed burst on the current oil stage: equal load holds during it (film_equal_load
   applies to the shifted hue), and the burst's landing hue is snapped to the nearest anchor (never
   lime/mustard/olive), then the anchor drift resumes. Keyed: [cycle] burst_weight (default 1) so it
   can be zeroed.
3. Per-tier weights as in A (replaces per-stage weights; stage_N_weight stays as a within-tier
   multiplier, default 1; stage_N_tier=proven|moderate|wild).
4. [meta] base= understood by the tray and ApplyPreset (main.cpp) as the window and the director
   already do (UI 1b deferred item).
5. Scheme change within oil = fade the extra colours out, swap offsets, fade in (BV rule 6) — the
   director's oil→oil transition when both stages are liquid_acid: ramp film_hue2_amt/film_hue3_amt to
   0 over 2 s, swap, ramp back over 2 s; no black.
6. Write the shipped cycle into reference/configs/cycle-final.ini with paths relative to the ini
   folder; and a one-line tool (tools/cycle-install.ps1) that copies its [cycle] block into
   %APPDATA%\FluidWallpaper\settings.ini with absolute paths (run by the maintainer at swap time, not
   by you).

## C. Scheme coverage (the creative chat's "one gap", BLOCKING for ship)
In the developed 150 s frames the second colour covers ~10-15% of the frame; the user's proven photos
show ~30-45% as big flat fields. For HOME pairs raise the second colour's coverage toward the photos
using the EXISTING hue2 field keys (film_hue2_scale / seed rows / decay / whatever drives patch size and
persistence — find them in shaders.h/fluid.cpp, do not add keys unless nothing existing can do it; if a
key is needed, one key, own sheet). Target: Magenta/Mint at 150 s with the mint field ≈ 30-40% of the
frame as a flat region with the warm seam, beside proven-2 (handoff\review\proven\
proven-2-magenta-green-panel.jpg) → handoff\review\schemes\coverage-magenta-mint-vs-proven2.png. Apply
the same values to all HOME pair presets; the 3-/4-colour tiers keep the third colour ≤ ~15%.
Also: add `[meta] look=liquid_acid` to every Scheme preset (rote).

## Proof block (§6/§7)
- Cycle off: parity md5 10E36EBF1A74EDFE609065D757300054; preset-identity -Tier fast -Baseline
  build2\shots\preset-identity-baseline-218c574.txt all UNCHANGED (new keys default-identical);
  dxbc-cmp fluid + ink IDENTICAL.
- Cycle on (cycle-final.ini, seed 1234, --cycle-dwell 20): a series showing (a) WE entry with content
  at first visible frame (mean_lum > 0.02 at fade-in end), (b) ink entry with a drop visible, (c) one
  tamed burst on an oil stage with mean_lum swing < 15% and the landing hue at an anchor, (d) an oil→oil
  scheme change with no black frame and the amts ramping, (e) the draw log over 200 draws showing the
  tier shares within ±10% of 70/20/10 and the home split as specified.
- Coverage sheet per C, plus a 4-scheme mini sheet (Magenta/Mint, Magenta/Cyan, Blue/Coral, Return to
  Form) at 150 s after the coverage change.
- Contact sheet of the final stage list (one steady frame per stage) → handoff\review\cycle-final\.
Commit on branch `final-cycle`; do not merge; do not push. Report ≤30 lines + §7.

## AUDITOR PRE-FLIGHT (2026-09-25, main ad54bce) — binding
1. The tamed burst must NOT use CommandHueShift / fm1.x (shaders.h:1674: a colour-matrix rotation of the
   FINISHED image after equal load; would turn magenta film full-brightness yellow). Do it on the acid
   PALETTE CLOCK, CPU only: palette hue = W(fmod(AnimatorTime(0)/P)) (fluid.cpp ~5153); a burst = a
   smoothstepped temporary advance of the ANIM_PALETTE accumulator; landing phase chosen by inverting
   the anchor-warp table so the absolute hue lands on the nearest anchor; keep the offset so the drift
   resumes from the landing. Equal load holds automatically (per pixel). API: AnimatorKick(ANIM_PALETTE,
   phaseDelta, sec) in animators.h/fluid.cpp; never called with the cycle off ⇒ identical, no DXBC
   change. Never use the director's lerp hue bridge (cycle.cpp:569, CommandHueShift) between two oil
   stages. Draw the burst moment only when the current stage is oil; otherwise redraw.
2. Oil→oil scheme change: only shadow_tone_period is phase-sensitive (gate frac(m_time/P), differs in 5
   presets); hue_sweep_period is 0 in all 16; film_hue2/3/share are harmless once swapped at amount 0;
   film_hue2_scale only affects new material. No cut needed IF the ramp covers shadow_tone and
   highlight_tone_amt as well as the hue2/hue3 amounts. Swap at 0 as a PLAIN SET, not the generic lerp
   (else film_hue2 lerps round the wheel while its amount is non-zero).
3. Entry content: ResetLookState sets m_firstFrame (fluid.cpp:2054) so the startup burst
   (MultipleSplats :1875, when idle_splats is on) fires after the clear but may fade during the
   sub-stepped warm-up → add a public wrapper around MultipleSplats and call it a few frames before
   fade-in ends (never before the clear frame). Ink: InjectDrop (:6052) is public; ink inis have
   idle_splats 0 → call it at the START of the warm-up (after the clear frame) so the drop has spread.
4. Coverage: existing keys cannot reach ~35%. The hue2 target is value noise 0.78·nz + 0.22·nz2 with
   mean ≈ 0.5; visible rows are pure advection and bilinear semi-Lagrangian advection pulls extremes
   toward the mean, so the share above the 0.62 seam shrinks over time (the observed 10-15% at 150 s).
   film_hue2_scale = patch SIZE not share; decay only hidden rows; thresholds 0.56-0.68 hard-coded.
   ONE KEY: film_hue2_cover = CPU bias tgt' = saturate(tgt + bias) in StepHueField; no slot; default 0
   = identical; also shrinks hue3's low end (suits the ≤15% third). Tuning: coverage = share of visible
   mix cells > 0.62, loggable on the CPU with no render → tune fast, render only the final sheet.
5. Settings tier column conflict: ui_window.cpp:1031-1122 INFERS the tier from the stage weight (7/2/1)
   and writes 7/2/1 when a tier is picked; with weight as a within-tier multiplier every default stage
   would show "wild". The pane must read/write stage_N_tier, weight = multiplier column (JSON dump
   :1603 too).
6. Proof: draw log with a fixed seed over 2,000 draws, tier shares within ±2 points (200 draws is
   noise). cycle-install.ps1 writes %APPDATA% → the maintainer runs it OUTSIDE the Claude desktop
   (MSIX shadow). Scheme presets use film_equal_load 0.75: measure the burst's <15% mean_lum limit when
   the palette passes yellow.
7. TWO executors: COVERAGE first (blocking, independent): film_hue2_cover + the coverage sheet vs
   proven-2 + [meta] look= on the 16 presets (rote part). DIRECTOR second: B.1-B.6, the palette kick,
   the tier column. Both touch fluid.cpp in different functions; merge separately.
Opinion (relayed verbatim to the user): structure right; do not ship before the coverage fix.
