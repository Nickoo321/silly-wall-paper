# UI x ANIMATORS x CYCLE — the interaction model (DECIDED 2026-09-25, auditor-checked)

User: "very big decision: decide how the UI interacts with continuously changing things like hue, and
presets per mode". Binding for the ui1/1b executor AND the cycle executor. Shared header:
src/animators.h, landed FIRST by the cycle side so both build against it.

## 1. BASE vs LIVE
- Two kinds of animator: (a) DERIVED at constant-upload time, never written to Config: acid
  hue_rotate/sweep (effOil rotated per frame, fluid.cpp:4855), hue2 wobble, BU lamp-grey slide +
  split-tone clock, rig/lamp, m_hueAngle. Config IS the base. The UI shows live via const getters in
  fluid.h (e.g. current palette hue) — a stated exception to the ui1 render-path freeze; getters cannot
  change md5/DXBC. (b) WRITTEN into Config: mood conductor (LerpLook every frame during SHIFT/EMIT/
  RETURN, moods.cpp UpdateMoods), journeys (JourneyUpdate during DWELL), the director's future lerp.
  Base is recoverable: in DWELL Config == s_target (= base); during a transition s_target is base and
  Config is live.
- While a transition runs, its ~45 LerpLook fields + FlipDiscrete's fields are READ-ONLY in the UI
  ("animating" badge) for those seconds; journey-owned keys locked while a journey is attached.
- Dirty count is measured against the COMPOSED base (preset + current mood target).
- Phase is never written to disk: no base_mood ini writes (moods.cpp:593, MoodsForceMood) while cycling.

## 2. Settings window open ⇒ the director's DWELL TIMER pauses (colour clocks keep running unless
frozen). Header chip "paused for editing". Auto-resume after ~10 min without input (hidden taskbar:
a forgotten window must not stop the cycle forever). Edits apply live to the current stage's composed
config. SAVE writes a PARTIAL overlay: only keys that differ from the stage's base into the stage file,
never a full WriteConfigToIni dump. Revert reloads the stage. Closing with unsaved edits keeps them in
memory for the session ("unsaved"), no auto-write.

## 3. Presets per look: tag = [look] keys or `[meta] look=fluid|liquid_acid|ink|any` (mirror overlays
= any). NOT [ui] (never written into presets). "In cycle" flags live in settings.ini [cycle]; preset
files stay portable. The Modes page edits the director's ordered stage list with per-look presets
checked into it (one source of truth; order=alternate_random draws from that list).

## 4. Freezes (hue rotation, conductor, effect clocks, cycle): hold the phase, do not reset it.
Derived clocks need an offset: phase = fmod((m_time - frozenAccum)/P); offset 0.0 when never frozen
⇒ bit-identical, but it is a fluid.cpp edit ⇒ parity proof. Conductor freeze = a hold in moods.cpp.
Freezes AUTO-RELEASE (~15 min, and when the window closes). Freezing the rig/lamp stops all motion.

## 5. Header when cycling: "Cycle · 1/10 · WE parity · mood Neon (cycling) · 8:12 left · paused for
editing" — mood shown only when the conductor is enabled; preset name = the stage's file; dirty vs
composed base.

## Ownership
- cycle executor: src/animators.h API (CyclePause/CycleResume/CycleState/CycleStageBase,
  Freeze(animator)/Unfreeze with auto-release, derived-value getters), in-memory stage apply, [cycle]
  flags, freeze offsets in fluid.cpp (parity), conductor hold, ini-write suppression.
- ui1 / 1b executor: live marker, transition locks, dirty-vs-composed-base, per-look profile strip,
  header, Save-as-partial, freeze buttons, auto-resume UI, Modes page stage-list editor.
