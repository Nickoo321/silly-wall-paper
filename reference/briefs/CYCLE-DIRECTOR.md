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

## AUDITOR PRE-FLIGHT (2026-09-24, main 61ed50a) — binding corrections
1. The sim does NOT restart on a look switch: ApplyPreset (main.cpp:1990-2010) overwrites Config and
   calls EnsureLookResources, which compiles the PSO once and reseeds the acid population only on that
   first compile (fluid.cpp:486-494); velocity/dye textures carry over. The director needs a black-point
   reset: public FluidRenderer::ResetLookState() = clear dye + velocity (m_psoClear4 / ClearV),
   m_acidSeeded=false, m_dropletSeededFor=-1, re-seed the mix field; callable only while cycling.
2. Warm-up: while black, sub-step the sim (e.g. 8 steps/frame) so 10-20 s of sim time passes in ~2 s
   wall time. MEASURE warm-up to steady state per look (droplet count, mean_lum within 5%); no
   hard-coded 3 s.
3. State that leaks across a switch: a mood-conductor SHIFT keeps writing shared fluid fields
   (vorticity, dissipation) → freeze it on leaving a fluid stage, restart from DWELL on re-entry; the
   conductor writes base_mood to the ini (moods.cpp:593) → suppress while cycling; a journey keeps
   driving keys → detach at every stage switch; m_hueAngle → glide to the next full turn when leaving
   fluid (AGENTS rule) and confirm the hue-burst term (fm1.x, shaders.h:1647) never runs under acid.
4. Partial overlays merge onto live state → compose EVERY stage onto a declared base (FluidConfig{} +
   stage file, or stage_N_base). Apply stages in memory: reuse ApplyPreset's merge WITHOUT
   CloseSettingsWindow, SaveFullConfig, MoodsAdoptPath. No ini writes while cycling except [cycle] state.
5. FADE = fold into m_sdrScale on the CPU (display b0.sdrScale; the post pass normalises by the same
   value pp2.z, so grain/fog/bloom scale with it). At 1.0 the constant is bit-identical → DXBC identical,
   parity holds. Below 0.01 switch to PresentBlack() for the hold + warm-up (avoids the post pass
   max(sdr,1e-3) floor). Do NOT use the spare fm2.yzw (adds an instruction). Proof: dxbc-cmp IDENTICAL,
   the md5, and a fade series where mean_lum is proportional to the fade within 2% and monotonic.
6. Generic lerp: NEVER through the UI setter (per-key ini writes at 144 fps = disk/OneDrive/MSIX churn).
   Write Config fields in memory, fire hooks once at the end. Flags needed (keys.inc on ui1, or a
   director-local list until it merges): PERIOD never lerp (hue_rotate_period, hue_sweep_period,
   film_hue2_wobble_period, weather_period_s, shadow_tone_period: phase = fmod(t/P) spins) → cut inside
   a fade; HUE lerp the short way round (dye_hue, film_hue2); RESTART cut inside a fade (wanderer_count,
   film_hue2_seed_rows, blob_count + per-kind fractions, droplets, enums, ink_mode); SHELL never apply.
   Colour triples and sweep lists are not in keys.inc → fade. Any stage pair differing in a
   PERIOD/RESTART/enum key fades even within one look.
7. PHASING: phase 1 = fade-only director on main NOW (the first cycle is all look changes / structural
   differences, no lerps needed). Phase 2 = generic lerp once ui1's keys.inc lands with the flags.
8. Defaults from the opinion: fade out ~1.5 s, black hold = measured warm-up, fade in 2-3 s; dwell WE
   longer than the oil stages (oil 12-15 min).

## SURVEY + DECISION 2026-09-25 ~00:45: the conductor is ABSORBED, not frozen (supersedes §1 and
pre-flight item 3 where they say the conductor "stays as is")
User: "cycles and modes are Kimi's work, delete or alter however much you want; it broke its own
system; see if there's anything cool in there." Read-only survey findings (moods.cpp/journey.cpp/
scenes.cpp): with [moods] enabled=0 and no base_mood, none of it changes the rendered image (UpdateMoods
returns at :509; InitMoods' rand() is re-seeded in InitCommon; ApplyMoodPeakNits/SetCoverageWanted are
no-ops) → deletable without touching parity, provided the hue-shift functions in fluid.cpp stay.
Broken: rotation = whole folder (67 files, 41 are acid/ink/mirror copies) alphabetically; FinishTransition
copies the full config incl. look flags but never calls EnsureLookResources; moods inherit from the
previous one (partial overlays on live); MoodsAdoptPath can undo a just-applied preset (:583-585);
JourneyDetach without ReleaseHueShift leaves the rotation locked (:600, journey.cpp:320); any tray
apply persists base_mood (:593); Scenes Overwrite dumps the full settings.ini incl. sim_res/peak_nits.
KEEP (absorb into the director): hue bridge maths (moods.cpp:138-143 + fluid.cpp:2326-2350
CommandHueShift/ReleaseHueShift/FieldAvgHueDeg — rotation only moves forward), the 4 s smoothstep
curve with the midpoint flip of non-numeric values (:530, :92) = the director's within-FLUID lerp
(LerpLook's ~45 fluid fields), dwell jitter (:175 → stage_N_jitter, default 0.3), the dark-screen
trigger (:513-519: past min dwell, field ≥92% dark for 10 s → switch now; fluid stages only),
per-mood [hdr] peak_nits (:157), the ●/○/* change markers idea (Modes page), journeys as a
fluid-only stage type (stage_N_journey=<name>; must ReleaseHueShift(false) on detach/end).
DELETE: moods.cpp conductor/rotation/skip list/base_mood/MoodsApplyBase/auto-written Neon+Clouds/
one-time preset copy, scenes.cpp + window, the settings mood bar. Call sites to remove: main.cpp
1096-1124, 1248-1271, 2014, 2031, 2592-2594, 2682, 2828-2829, 3192; settings.cpp (being replaced by
ui1 anyway) 654-733, 960, 1001-1029, 1073-1120, 1233, 1397; app_state.h:30. Replace MoodsGetDirectory
(main.cpp:1041/1262/2024): the tray Presets menu uses the moods folder as its recipe folder → the
director's preset folder. Proof after removal: parity md5 + dxbc-cmp IDENTICAL.
WE variants for the cycle = the ~26 fluid moods (only "WE parity (fluid)" + the 4 Journey moods matter
for the first cycle); the 41 acid/ink/mirror copies in moods\ are redundant with reference/presets.
