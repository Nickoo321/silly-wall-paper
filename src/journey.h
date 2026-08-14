// Journey director: while dwelling in a journey-enabled mood (a mood ini with
// a [journey] file=<name> section), the field travels an ordered list of
// color-family legs (journeys\<name>.txt, one leg per line:
//   hueCenter hueRange darkFloor dwellSec emit [blendSec] [glideSec] [shiftDeg]
// ). v2: a leg controls TWO independent knobs — the emission band
// (hueCenter/hueRange) and the field rotation — and the resultant color is
// emitted hue + field shift angle. shiftDeg present && != 0 rotates the field
// RELATIVE to the held angle (held + shiftDeg, monotonic continuation, no
// wrap snap); hueCenter then means ONLY the emission band center (WheelHue
// counter-rotation lands fresh dye there). shiftDeg == 0: band change only,
// no rotation stage. shiftDeg absent: v1 behavior — the field glides to the
// family center. emit=0 legs are dark intermissions: emission off, field
// decays. blendSec>0 legs glide in two stages — halfway along the arc, a
// BLEND_HOLD of blendSec seconds marbles both families together (the WE
// in-between state), then the remaining half. glideSec (default 8) is the
// per-leg duration of each glide stage.
// After N full loops (settings.ini [journey] loops=N, default 2) the journey
// ends in place and the mood's normal dwell/transition resumes.
#pragma once
#include "fluid.h"

// Called by the conductor when a transition finishes INTO a mood (and once at
// startup): reads [journey] file= from the mood ini; empty/missing/bad file
// leaves the journey inactive (normal static mood).
void JourneyAttach(const wchar_t* moodPath);
void JourneyDetach();                        // stops legs; renderer untouched
bool JourneyActive();
void JourneyUpdate(FluidRenderer& r, float dt);   // DWELL-branch tick
// UI: returns 1 when active and fills 0-based leg index + leg count, else 0.
int  JourneyLegInfo(int* legIndex, int* legCount);
// UI: true while a leg sits in its BLEND_HOLD (both families marbled).
bool JourneyInBlendHold();
