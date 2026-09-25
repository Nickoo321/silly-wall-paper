# Brief BX — "Grey heart" (a centred Y-flat desaturation for the Lightroom-effects family)

Status: brief. Origin: the user, via the creative chat "Settings and handoff review", 2026-09-25 evening.
The user's words: "don't apply colored after filters, have a true color and make it look grayscale
in the middle". Decision log: handoff\review\DECISIONS.md, row "request 5a".

## 1. What it is (spec, not up for debate)

- The frame keeps its TRUE stage colours everywhere: film, second colour, seams, split tone,
  highlights. Nothing is tinted or added. (The tint-after-filter version was REJECTED:
  handoff\review\colour-mocks\bw-vignette-mock.png is kept only for contrast.)
- A luminance-preserving (Y-flat, linear luminance) desaturation is FULL in the centre and fades
  to ZERO toward the edges. The middle reads greyscale; the stage's own colour survives as a
  natural colour "vignette" around it. The oil stays black, so the centre looks like a B&W print.
- Mask (radius as a fraction of the half-frame, i.e. distance from the frame centre with x and y
  each normalised by their half-extent, so the mask is an ELLIPSE that matches the frame): fully
  grey inside ~0.45, smoothstep to no desaturation at ~1.15. Mock:
  handoff\review\colour-mocks\grey-heart-mock.png (four home stages, as-is vs grey heart).
- It is the SAME machinery as BU's lamp grey (shaders.h "LAMP GREY" block in the acid display PS:
  Y-flat desaturation about DsToLin luminance, then the post_chroma luminance hold), with a
  centred radial mask instead of the far-corner disc. The creative chat's call: a mask mode on lamp
  grey rather than a new effect. Build it as a SECOND MASK feeding the same block, so the two can
  coexist and the shader code is shared.
- It applies to ANY oil (liquid_acid) stage. Ships as a PARTIAL overlay preset and as WILD-tier
  cycle stages, until the user has seen it on the panel.
- Open point, to be judged LIVE on the panel (HDR on and off), not now: with Y-flat the centre
  is a mid grey at the film's brightness, which reads flatter than the paper-white diff image the
  user liked. A small lift of the grey may be wanted, so the lift is a key (default 0) with its own
  A/B sheet, and the parent asks the user.

## 2. Keys (all [liquid_acid], all default = today's behaviour = off)

| key | range / step | default | meaning |
|---|---|---|---|
| lamp_grey_heart | 0..1, 0.05 | 0 | amount of the centred Y-flat desaturation (1 = fully grey inside the core). 0 = off, byte-identical. |
| lamp_grey_heart_size | 0.6..1.6, 0.05 | 1.15 | outer radius of the mask in half-frame units (elliptical, see §1); no desaturation beyond it. The core (full grey) is 0.4 x size (0.46 at default, the mock's 0.45). |
| lamp_grey_heart_lift | 0..0.5, 0.05 | 0 | lifts the linear luminance of the greyed pixels by up to this fraction, scaled by mask x amount (only the greyed centre lifts; edges and black oil never move). |

- Same Y-flat maths as the corner: keep `gk_corner` as today and add `gk_heart =
  LA_HEART_K * (1 - smoothstep(0.4*S, S, r))` with `r = length(uvc)` where uvc is the centred,
  PRE-aspect uv (x and y each -1..1 over the frame; NOT pp, which is aspect-scaled — see the two
  `float2 pp = float2(uv.x * aspect, uv.y)` sites and check how uv is centred there). Combined
  weight `gk = max(gk_corner, gk_heart)` into the existing desaturation; the cool white balance
  applies ONLY with the corner weight (the heart is neutral grey by spec). The lift: after the
  desaturation, `gl *= 1 + LA_HEART_LIFT * gk_heart` on linear rgb, BEFORE the post_chroma hold,
  and the hold must then target the LIFTED luminance (compute its target after the lift). Read the
  block END TO END first: today the DarkSat-style hold targets the pre-grey luminance.
- Black oil: verify with a crop that a black mass in the centre stays black (Y 0 x anything = 0;
  the lift is multiplicative, so black stays black).
- Slots: laP38.y = HEART_K, laP38.z = HEART_SIZE, laP38.w = HEART_LIFT (laP38.x is taken by BW's
  film_equal_load_patches on branch `bw`; that branch merges to main before you merge — see §5).
  Rows in src\acid_slots.h; `tools\slot-check.ps1` must pass; `-Fix` regenerates the comment table.
- keys.inc rows next to the lamp grey rows (section "liquid_acid", group G_OPTICS, front column
  "A", size and lift gated on `liquid_acid.lamp_grey_heart>0`), help text in the BU style (what it
  does, what the numbers mean, "0 = off (brief BX)"). tools\keymeta-check.ps1 must pass.
- fluid.h config fields + defaults, main.cpp getF/putF, fluid.cpp UploadAcidConstants `slot()`
  lines beside LA_GREY_*.

## 3. Presets and cycle

- reference\presets\Grey heart (overlay).ini — PARTIAL overlay, `[meta] look=liquid_acid`,
  `[liquid_acid] lamp_grey_heart=1`, size 1.15, lift = the value your lift sheet suggests (0 if in
  doubt; the parent may change it after the user's verdict).
- reference\configs\cycle-final.ini: add ONE wild-tier stage with `stage_N_overlay=1`, dwell 150,
  weight 1, the same way the Mirror quad overlay stage (stage_20) does. Read the [cycle] header
  and how an overlay stage resolves onto the running look first: it must land on an OIL stage
  (the keys do nothing on fluid). If overlays can land on WE, make ONE composed stage instead:
  `stage_N_file=monotone-grey-heart.ini` with a small reference\configs\monotone-grey-heart.ini
  ([meta] base=monotone-post-0924.ini + the heart keys). Say in the report which you shipped and why.
- reference\FEATURES.md row(s); manifest row in tools\preset-identity.manifest.psd1 for the new
  overlay preset (tier full).

## 4. Proof (PROOF BLOCK per EXECUTOR-CARD §6, plus the §7 review questions)

1. Parity: we-look-live.ini 60 s 2560x1440 seed 1234 --hdr on → md5 10E36EBF1A74EDFE609065D757300054.
2. `tools\preset-identity.ps1 -Exe <your build> -Tier fast -Baseline <newest baseline in build2\shots\>`:
   every row UNCHANGED (after your merge of main the parent will have re-saved the baseline for BW's
   changes; if not, name the rows BW changed on purpose).
3. A/B sheets (2560x1440 --hdr on, seed 1234, 60 s, nearest-neighbour crops + full frames), copied
   into the MAIN repo's build2\shots\live\bx\:
   - ab-lamp_grey_heart.png: amount 0 / 0.5 / 1 on monotone-post-0924.ini AND on one Scheme home
     stage (pick the one the mock's labels show, read grey-heart-mock.png).
   - ab-lamp_grey_heart_size.png: 0.8 / 1.15 / 1.5 at amount 1.
   - ab-lamp_grey_heart_lift.png: lift 0 / 0.15 / 0.3 / 0.5 at amount 1 — this one answers the
     open point; write ONE line of your own opinion (which lift, why) in the report.
   - heart-home-4.png: the mock's four home stages rendered for real, as-is vs heart (amount 1, lift 0).
4. Measurements in the report: frame mean_lum at amount 0 vs 1 (within 1%, Y-flat), the greyed
   centre's mean chroma (~0), a centre black-mass crop's max value (stays black).
5. `tools\slot-check.ps1`, `tools\keymeta-check.ps1`, `--ui-dump` showing the three keys with
   their gates, `--ui-shot` of the pane showing them (WARP headless).
6. dxbc-cmp: the FLUID PSO is IDENTICAL (the block is in the acid PS only).

## 5. Branch and merge

- Worktree C:\Users\abg77\fw-bx, branch `bx` from main. BW (branch `bw`, worktree fw-bw) is in
  flight and takes laP35.w + laP38.x; it touches shaders.h, acid_slots.h, fluid.cpp, keys.inc,
  cycle-final.ini, FEATURES.md and the manifest. Do your code first; BEFORE your proof renders,
  merge main (the parent merges bw into main; if bw is not on main yet when you get there, merge
  branch `bw` itself and say so), resolve the slot table (you own laP38.yzw), re-run slot-check,
  then prove.
- Render throttle: `--shot-yield 30` (the user is at the PC this evening). Builds take the lock.
- One feature per branch; commit on `bx`; do NOT merge to main yourself; STOP and report per
  EXECUTOR-CARD §6 + §7 with the sheet paths and a rote list.

## AUDITOR PRE-FLIGHT (2026-09-25, main 12f277e) — binding
1. MASK / uv: `uv` in PSMain is NOT centred: VSMain (shaders.h:1013-1017) emits 0..1, origin
   top-left, y DOWN, and PSMain:1054 replaces it with the MIRROR-FOLDED uv (pp = uv*aspect at :1064
   is folded too). Use the raw screen interpolant, so a mirror fold never moves the heart (same rule
   as the vignette/grain, :1050): `float2 hq = i.uv * 2.0 - 1.0; float gh = LA_HEART_K * (1.0 -
   smoothstep(0.4 * LA_HEART_SIZE, LA_HEART_SIZE, length(hq)));` = the ellipse of §1 (edge midpoints
   r 1, corners r 1.41; at size 1.15 the edge midpoints keep ~90% colour, as in the mock). No
   aspect term, no y flip (symmetric). HEART_K = clamp(amount,0,1) with NO 0.6 factor (the corner's
   `0.6f *` at fluid.cpp:5821 must not be copied). Clamp size 0.6..1.6, lift 0..0.5 on the CPU.
2. WHERE (the one thing that would waste a render): the BU block (shaders.h:2470-2513) runs right
   after `col = lerp(inkC, oilC, alpha)` (:2389) and BEFORE fluor, penumbra, halo, mass_rim, droplet
   lens, oil_glow, meniscus, speckle, cellulose, shadows, toe (:2530-3084). monotone-post-0924 has
   mass_rim 0.35, halo 0.18, oil_glow 0.45, oil_penumbra_hue 12, droplet_lens 0.55: fed into that
   block, every droplet/mass edge in the grey centre stays FILM-COLOURED (the mock greys them; the
   "centre chroma ~0" proof would fail). So: factor the block body into ONE helper (put it next to
   DarkSat, :721, a small literal piece) `float3 LampGreyY(float3 col, float gk, float gkCool, float
   liftMul)`; the corner calls it at :2481 with `(col, gk, gk*saturate(LA_GREY_COOL), 1.0)` (literal
   1.0 folds away: corner byte-identical); the heart calls it IMMEDIATELY BEFORE "final trim"
   (:3085, after toe_tint) inside `[branch] if (LA_HEART_K > 0.0)`, cool 0. Code shared, two sites;
   drop the max(gk_corner, gk_heart) merge (the regions barely overlap; sequential = 1-(1-a)(1-b)).
   The hold is exactly right there: post_chroma is literally the next operation. Grain after it is
   a scalar (stays grey). Residual chroma will remain only from the [post] lateral aberration.
3. ORDER + HOLD: grey `gl = gY + (gl - gY)*(1 - gk)` (:2489), cool lerp (corner only), THEN
   `gl *= liftMul`, then DsToSrgb, then the hold. §2's "compute the target after the lift" is WRONG:
   the target from a lifted GREY pixel is its own luminance (post_chroma is a no-op on grey), so the
   hold would do nothing and lose the +chroma luminance the saturated original gets (the -1..-2%
   mean_lum the hold exists for; far more per pixel on saturated magenta). Keep t0 from the
   ORIGINAL col as today (`float t0 = dot(DsToLin(max(y0 + (col - y0) * pc, 0.0)), GW);`, :2502)
   and multiply: `t0 *= liftMul`. With post_chroma == 1 the branch is skipped and liftMul alone acts.
4. BLACK MASSES: the block sees the composite with alpha applied (film = alpha, masses/holes =
   1-alpha; see the tone mask at :2415). Multiplicative lift keeps exact 0 at 0, but monotone's black
   is NOT 0: shadow_tone_lift 0.06 tints it, and lift 0.5 would raise it x1.5. Weight the lift by the
   film: `liftMul = 1.0 + LA_HEART_LIFT * gh * alpha` (alpha is live at :3132). The heart WILL grey
   the split-tone tint in the centre's masses (spec: everything greys). Proof 4.4 then = the centre
   mass crop's max at amount 1 equals amount 0 (within grain), not "absolute black".
5. SLOTS: laP38.y HEART_K, .z HEART_SIZE, .w HEART_LIFT. On main laP38 is declared empty
   (shaders.h:613, acid_slots.h:42/221, fluid.cpp:2768 comments); bw (c8221b0, not on main) has
   HUE2_SEAM 35.w + EQUAL_LOAD_P 38.x (its acid_slots.h:205-206, header :45-46, count :225). Merge:
   take bw's text, add the three rows right after EQUAL_LOAD_P, header "Brief BX took laP38.yzw
   ...: laP38 full", count comment "laP38 full", then `slot-check.ps1 -Fix` (regenerates :613).
   slot() lines go after LA_GREY_CY (fluid.cpp:5825), inside the same always-run scope. Expect a
   textual conflict in cycle-final.ini (stage_count) and FEATURES/manifest too. Check slot-check's
   largest-literal print: the :2530-3140 piece gets the heart call.
6. CYCLE (§3): overlays land ONLY on WE in tier mode, so the overlay option is wrong. PickTier:
   after WE, `DrawTiered(false, true, "after WE")` allows overlays (cycle.cpp:632-634); an oil/ink
   stage is always followed by DrawFluid (:639-642) and its midpoint draw (:1180-1187) is queued for
   AFTER that WE; cycle-final.ini's header says the same ("WE ... ALTERNATES with every oil / ink /
   overlay stage"). SoftPoint (:981-990) keeps the base look, so on WE the keys are dead. Ship the
   COMPOSED stage, with no new configs file (stage_3's pattern, :61-62):
   `stage_21_file=../presets/Grey heart (overlay).ini`, `stage_21_base=monotone-post-0924.ini`,
   `stage_21_tier=wild`, `stage_21_dwell=180` (oil dwell, not 150), `stage_21_transition=fade`,
   no `_overlay`; stage_count=21. `transition=fade` is needed: the stage shares the Schemes' base,
   so a manual Scheme -> heart jump would take the no-black scheme swap (WantsScheme :856-862), where
   the heart is not a ramped amount (GetAmts :844) and would snap on; `fade` is only read at :861.
7. keys.inc (looks = "A", front = "", NOT front "A"; §2 confuses the two; flags 0, static):
   `KEY_SLIDER("Grey heart", 0, 1, 0.05f, 2, &c.acid.lampGreyHeart, nullptr, "liquid_acid",
   "lamp_grey_heart", false, "<tip ... 0 = off (brief BX)>",` / `G_OPTICS, "A", "", 0, "", "")`;
   size: `0.6f, 1.6f, 0.05f, 2, &c.acid.lampGreyHeartSize, ... "lamp_grey_heart_size" ...` /
   `G_OPTICS, "A", "liquid_acid.lamp_grey_heart>0", 0, "", "")`; lift: `0, 0.5f, 0.05f, 2,
   &c.acid.lampGreyHeartLift, ... "lamp_grey_heart_lift" ...` / same gate row. Put them after the
   lamp_grey_cool row (:470-471); edit lamp_grey's tip "Never the centre" -> "(the centre: Grey
   heart)" and fluid.h:851-855 likewise.
8. PROOF fixes: §4.3's "one Scheme home stage the mock's labels show" is ambiguous (four labels):
   use Scheme - Blue Coral (blue's low Y makes the darkest grey: the lift sheet's hardest case) and
   put Magenta/Mint in the lift sheet too. mean_lum within 1% applies at lift 0 only; report lift's
   mean_lum/ABL cost (+x%) per value. Measure centre chroma on flat film, not across droplet edges
   ([post] aberration). The corner (lamp_grey 0.6 in monotone) stays on in every sheet.
Opinion: the spec reads well: a B&W print framed by the stage's own colour is a stronger idea than
the tint version, and Y-flat is the honest start. I expect the lift to matter most on blue schemes
(dark grey); a film-only lift ~0.15 is my guess, above ~0.3 it will cost ABL and read as a hotspot.
