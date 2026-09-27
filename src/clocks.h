// clocks.h -- brief CLOCKS phase 1 (reference/briefs/CLOCKS.md + the binding
// auditor pre-flight + Fable decisions): every clocked key on a slow,
// skewed, long-tailed clock whose MEAN is the preset value.
//
//   live[key] = clamp(base[key] * F_group(t) * j_key(t) + add[key] * E_group(t))
//
// F_group: a smooth glide between log-normal draws F = exp(s z - s^2/2)
//          (E[F] = 1 exactly, mode exp(-1.5 s^2) < 1), one draw per segment of
//          U[draw_min_s, draw_max_s]; direction-flipped keys use the SAME z as
//          F' = exp(-s z - s^2/2) (pre-flight 19, never 1/F).
// j_key:   per-key log-normal jitter (sigma_key), its own segment phase.
// TAIL:    a draw above tail_at, admitted by the GOVERNOR (one tail at a time,
//          tail_gap_min_min since the last one ended, forced at
//          tail_gap_max_min, never the same group twice in a row, tailw_<name>
//          weights); in / out over fade_min_s, held tail_len_s. E_group is its
//          envelope, which drives the declared episode ADDS (tail_add_<key>).
//
// ADOPT-ON-CHANGE (pre-flight 1-6): the clocks own no copy of the config. Per
// clocked key they keep {base, lastOut}; each tick, a live value that is not
// the one they wrote last frame was written by someone else (a stage apply, a
// lerp, a slider) and is ADOPTED as the new base. enabled=0 (the default)
// returns before touching anything: bit-identical to the app without clocks.
#pragma once
#include "fluid.h"

// [clocks] from settings.ini or the --ini file (phase 1: nowhere else).
void  ClocksLoad(const wchar_t* ini);
void  ClocksSetLogger(void (*fn)(const char*));
// The seed the cycle director uses (CycleSeed(): --cycle-seed, else --seed
// under --shot, else [cycle] seed; 0 = wall clock). The clocks run their own
// splitmix streams from it -- never the director's NextRand, never rand().
void  ClocksSeed(unsigned seed);
// --clocks-force <group>=<F>|mode|tail[,<group>=...] (group "all" = every
// group). Pins those groups for a proof render, every other group at F = 1,
// jitter off; ticks with the cycle off (pre-flight 26f). false = bad spec.
bool  ClocksForce(const wchar_t* spec);

// Once per shown frame, right AFTER CycleTick, in both loops (pre-flight 2).
void  ClocksTick(FluidRenderer& r, float dt);
bool  ClocksActive();                    // ticking this frame (enabled && cycle on, or forced)

// Snapshots take the BASE (pre-flight 3): replace every clocked key of a COPY
// of the live config that still holds the clocks' own output with its base.
void  ClocksBaseCopy(FluidConfig& inout);
// The cycle director lets go (GoOff, pre-flight 4): put the base back into
// the live config synchronously, before anyone reads it.
void  ClocksRestoreBase(FluidConfig& live, const char* why);

// Settings window (pre-flight 7): true at create and on the input re-arm,
// false ONLY in WM_DESTROY. Holding = every factor glides to 1 over
// fade_min_s and the scheduler stops; on release the factors glide back.
void  ClocksHold(bool on);
bool  ClocksHeld();
// While holding, a clocked key whose live value is not yet its base
// (pre-flight 8): the UI keeps such rows out of dirty and Save-as-partial.
bool  ClocksSettling();
bool  ClocksRowSettling(const char* sec, const char* key, float* base = nullptr);

// --clocks-dryrun HOURS: the scheduler alone on the CPU at 10 Hz virtual time
// (every group active, as on an oil stage), one line per draw + a summary.
void  ClocksDryRun(double hours);

// Shot log: the per-key max |delta| per frame since the start (proof 3e).
void  ClocksReport();
struct ClocksStatus {
    bool  enabled = false, active = false, held = false, frozen = false, forced = false;
    int   keys = 0, groups = 0;
    int   tailGroup = -1;                // group index of the running tail, -1 none
    float nextTailMinSec = 0.0f;         // earliest the governor admits the next tail
    float engage = 0.0f;                 // 0 = factors at 1 (held / off), 1 = full
};
ClocksStatus ClocksState();
const char*  ClocksGroupName(int g);
