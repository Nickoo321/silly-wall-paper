# CYCLE DIRECTOR — brief (2026-09-24)

User (verbatim gist): "all the modes and settings that work get cycled, so basically that's WE, then the
oil, with monotone, 2 colors, some other settings, the light room effects. Build it first, then go
decision through decision on what actually ends up in the thing."
Read EXECUTOR-CARD.md + AGENTS.md first. Memory context: the fluid parity md5
10E36EBF1A74EDFE609065D757300054 (we-look-live.ini, 60 s, 2560x1440, seed 1234, --hdr on) must hold
with the cycle OFF. Build on main after the UI 1a branch (ui1) has merged: it provides the single
X-macro key table src/ui/keys.inc (label, range, pointer, section, key, group, looks, gate, flags),
which the director reuses for generic value lerps.

## 1. What it is
A stage list that the app walks forever. A STAGE = one preset/ini overlay (full or partial) + a dwell
time + how to get there. Stages may change the LOOK (fluid / liquid_acid / ink) or only values inside
a look. The existing mood conductor (moods.cpp, fluid fields only, DWELL→SHIFT→EMIT→RETURN) stays as
is for fluid-internal colour moods; the director sits ABOVE it and can enable/disable it per stage.

## 2. Config ([cycle] in settings.ini; never written into presets/moods)
  enabled=0|1            default 0 → bit-identical to today (parity)
  loop=1
  stage_count=N
  stage_N_file=<presets\...ini or reference\configs\...ini path relative to the ini folder>
  stage_N_dwell=<seconds>            default 600
  stage_N_transition=cut|fade|lerp   default: fade when the look changes, lerp otherwise
  stage_N_fade=<seconds>             default 4 (fade = through black, both halves)
  stage_N_lerp=<seconds>             default 20 (lerp = linear ramp of every float key in keys.inc that
                                     differs between the current live values and the stage's values;
                                     ints/bools/enums and strings flip at the midpoint)
  shuffle=0|1                        default 0
Tray: "Cycle" submenu = On/Off, Next stage, Previous stage, list of stages with the current one
checked. CLI: --cycle-stage N (start at stage N), --cycle-next (message the running app, like
CMD_PAUSE_ON/OFF: new WM_COMMAND ids, non-toggling). Settings UI (phase 1b Modes page) shows the
stage list, current stage, time left, and edits it; that part belongs to the UI 1b executor, so the
director exposes a small C++ API: CycleGet(), CycleSet(list), CycleJump(i), CycleState() (stage,
remaining, transitioning).

## 3. Look switch mechanics (the hard part)
- A look switch today = set acid.enabled / ink.enabled and EnsureLookResources (PSO compile on first
  use) + sim restart. In the director: at fade-out end (screen black) apply the stage overlay, switch
  look, let the sim run N warm-up seconds BLACK (configurable stage_N_warmup, default 3; the acid
  masses need seconds to form), then fade in. Never present a half-formed frame.
- Fade = a display-pass multiply by a scalar already routed? If no free root constant exists, use the
  existing post exposure/film_level path ONLY if it is byte-identical at 1.0, else add ONE root
  constant to b3 only if the 64-DWORD cap allows; otherwise fold into an existing packed spare
  (rg1.y spares are promised to AP: do not take those; say what you take).
- With enabled=0 the render path must be untouched: dxbc-cmp IDENTICAL on every PSO at defaults
  and the parity md5 holds. The fade scalar at 1.0 must be bit-identical (multiply by exactly 1.0 or
  branch-free select) — prove it with dxbc-cmp AND the md5.
- Hue angle invariant (AGENTS conventions): never zero m_hueAngle abruptly; a stage lerp glides.

## 4. Within-look lerp
Generic: for each float key in keys.inc whose value differs, ramp linearly over stage_N_lerp seconds,
per-frame, through the same setter/hook path the Settings UI uses (ReinitWanderers etc. fire once at
the end, not per frame, unless the key needs it). Keys flagged SHELL (sim_res/dye_res) are never
lerped or applied. Keys whose change forces a sim restart are listed by the executor and treated as a
cut at the midpoint. The BU lamp-grey corner slide and the split-tone coprime clock keep running across
stages (they are display-time effects, not stage values).

## 5. The first cycle to build (the user's list; the creative chat trims it later)
  1 WE parity fluid (reference/configs/we-look-live.ini; the user's "WE" — keep its mood conductor on)
  2 Oil monotone (reference/configs/monotone-post-0924.ini, the live look)
  3 Oil two colours (acid-rise-2hue.ini values on the monotone post)
  4 Oil, other settings (acid-rise-12.ini; acid-rise-rotate.ini; lapd-look-candidate.ini)
  5 Oil + Lightroom effects (monotone-post-0924 with lamp_grey and shadow_tone on, values from the BU
    sheet the user picks)
  (ink and mirror: NOT in the first cycle unless the user says so; see QUESTIONS)
Dwell 10 min each for the proof render (use --shot-series with shortened dwell, e.g. 20 s per stage,
to capture every transition).

## 6. Proof block (EXECUTOR-CARD §6)
- WORKS: a headless --shot-series across the whole cycle with 20 s dwell showing every stage and every
  transition (fade frames black, no half-formed frame after a look switch, lerp frames intermediate);
  the tray Next/Prev drive the same code; --cycle-next works on a running instance.
- PARITY: cycle off → parity md5 + dxbc-cmp IDENTICAL + preset-identity fast tier MATCH.
- DIFFERENCE: a contact sheet of the stages + one sentence per transition type.
- WHAT ADDS: which keys lerp vs cut per stage pair (table), warm-up seconds needed per look, cost of
  the PSO compile at the first switch (ms), GPU cost of the fade (none expected).
Deliver: branch `cycle`, renders in build2\shots\live\cycle\, copies of the stage frames into
handoff\review\cycle\ for the creative chat.

## QUESTIONS FOR THE USER (asked, not blocking; assumptions in brackets)
1. Fade through black between looks, or a crossfade? [fade through black: a crossfade needs both looks
   rendered at once]
2. Ink in the cycle? Mirror overlay stages? [no, until asked]
3. Dwell per stage? [10 min]
