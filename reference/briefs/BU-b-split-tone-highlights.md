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
