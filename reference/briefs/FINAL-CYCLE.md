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
