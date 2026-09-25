# BU-b — split tone: HIGHLIGHTS tint + BALANCE (Lightroom semantics) — brief 2026-09-25

User (via the creative chat, ticked): "copy Lightroom's effects, not just the resultant colors". The
BV scheme "T3b Lightroom Triad" and the home "Lightroom" entry are a GRADE over the magenta film:
Shadows hue ~175-185 teal (user's edit: 171 / sat 100) + Highlights hue ~45 gold + Balance. The colour
follows the frame's own light (lit film warms to gold, darks around the oil go teal); not flat zones.
Reference: handoff\lightroom\bu-lr-1-splittone-shadows-teal.webp (+ bu-lr-3/4 = graduated temp /
desaturate, same idea). Depends on BU (branch bu: shadow_tone* keys + shadow_tone_hue override) merged.
Read EXECUTOR-CARD.md + AGENTS.md first.

## Keys ([liquid_acid], display pass, next to BU's shadow tone; slots laP39; never laP36/37 (BU),
laP35.yzw/laP38 (BV))
- highlight_tone_hue   -1 = complement of shadow_tone hue (default), else absolute degrees
- highlight_tone_amt   0..1 (default 0 → bit-identical)
- highlight_tone_sat   0..1
- tone_balance        -1..1 (Lightroom Balance: shifts the luminance split point between shadows
                       and highlights; 0 = Lightroom's default split)
Semantics = Lightroom split toning: weight_sh = smoothstep on scene luma below the split, weight_hi
above it, both soft; the tint is added in a Y-preserving way (BU's shadow tint is Y-flat: reuse the
same toneAdd construction for highlights). The existing BU on/off clock (shadow_tone_period /_fade)
gates BOTH halves together.

## Proof block (EXECUTOR-CARD §6)
key-effect MAD per key; defaults bit-identical (parity md5, dxbc-cmp, preset-identity fast MATCH);
one sheet: monotone-post-0924 with shadows 171 sat 1 + highlights 45 at amt 0.3/0.6/1.0 and balance
-0.5/0/+0.5, beside the user's bu-lr-1 crop; a luminance check that Y is preserved within 1% at
amt 1; "what adds": whether highlights + balance alone reproduce bu-lr-1 or whether bu-lr-3's
graduated temperature is also needed (say so, do not build it here).

## AUDITOR PRE-FLIGHT (2026-09-25, main ff969f2, BU at fw-bu) — binding
1. BU's shadow tone is ADDITIVE (col += toneAdd·tM, tM = (1-alpha)·(1-smoothstep(0.02,0.15,tY)));
   it lifts black on the masses only (user-approved). Highlights must NOT reuse it (would add light to
   the lit film = most of the frame = ABL). HIGHLIGHTS HOLD Y: a white-balance multiply toward the
   tint renormalised to the pixel's own Y, as BU's lamp grey does (gc *= gY / dot(gc, GW)). The two
   bands differ on purpose: shadows lift, highlights shift colour at constant luminance.
2. Luma point: same as BU — linear Y of col after lerp(inkC, oilC, alpha), before rims, bloom and
   post (rims and bloom cores stay ungraded; the glow inherits the tinted film).
3. Balance: split defined on ENCODED luma (Lightroom semantics), converted to linear (encoded 0.5 ≈
   linear 0.21); balance moves it ±0.25 encoded. Shadow weight = (1-alpha)·(1-smoothstep(lo(b),hi(b),Y))
   (keeps BU's masses-only rule, a deliberate difference from Lightroom); highlight weight =
   alpha·smoothstep(lo2(b), hi2(b), Y).
4. CAP: holding Y raises the dominant channel (magenta→gold drives red; bright HDR film can clip at
   panel peak and shift hue): scale the tinted colour so max(c') ≤ max(c), no negative channels. Watch
   the stack with halation_warmth 0.75 (already warms highlights): gold on top can reach the
   "blinding yellow" BV rule 2 forbids.
5. Slots: fold highlight hue, sat, amount and BU's on/off gate into one RGB on the CPU (as toneAdd) →
   laP39.xyz; tone_balance → laP39.w. shadow_tone_hue is CPU-only (folded into toneAdd), laP38.x is
   free for BV. "-1 = complement" solved in OKLab on the CPU from the RESOLVED shadow hue.
6. Build on the MERGED BU head. Proof additions: 99.9th-percentile nits unchanged within 2% at
   amount 1; mean_lum within 1%; OKLab hue of lit film pixels moves toward 45°; one HDR on/off pair.
Opinion (relayed verbatim to the user): bu-lr-1 = teal-lifted oil (BU has it) + film pushed from hot
magenta to softer salmon = warm highlight tint + a small saturation drop; highlights + balance get
most of the way, plus a touch of film desaturation; the graduated temperature ramp is bu-lr-3's.
→ add key `highlight_tone_desat` 0..1 (default 0) if a spare float exists (fold amount·desat into
the CPU RGB or take laP39 fully and put balance elsewhere: executor decides, says which).
