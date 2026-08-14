// Journey director implementation. See journey.h for the concept.
// Coherence contract with the hue-shift machinery (fluid.cpp):
//  - a leg's glides issue CommandHueShift(target, glideSec) and the command
//    is HELD for the whole leg (never released mid-journey): the rotation IS
//    the journey, and WheelHue's commanded path counter-rotates emission so
//    new dye keeps landing in-family while old dye visibly glides over.
//  - legs with blendSec>0 glide in TWO stages: held angle -> midpoint (half
//    the shortest arc to the leg center), BLEND_HOLD there (both families
//    marbled together), then midpoint -> leg center. blendSec=0 legs behave
//    exactly like v1 (single glide).
//  - v2 shift legs (8th field present, != 0) command held + shiftDeg — a
//    RELATIVE, monotonic rotation with no arc computation; the leg's
//    hueCenter only moves the emission band (resultant color = emitted hue +
//    field shift). shiftDeg == 0 skips the rotation stage entirely (band
//    change only). Mixed files are fine: a v1 leg after shift legs targets
//    the hueCenter equivalent nearest the accumulated held angle.
//  - r.Config() is touched ONLY at leg boundaries (this file's StartLeg) —
//    never per frame.
//  - the conductor calls ReleaseHueShift(false) when a journey mood is left;
//    this file never calls it (JourneyDetach has no renderer access).

#include "journey.h"
#include "app_state.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

namespace {

struct JourneyLeg {
    float hueCenter;   // degrees, emission band center (v1 legs: also the
                       // field's glide target)
    float hueRange;    // degrees half-width of the emission band
    float darkFloor;   // auto-pause governor floor while the leg runs
    float dwellSec;    // dwell after the glide stage(s)
    int   emit;        // 0 = dark intermission (emission off)
    float blendSec;    // >0: hold at the arc midpoint this long (families marbled)
    float glideSec;    // per-leg duration of EACH glide stage
    float shiftDeg;    // v2 (hasShift): RELATIVE field rotation this leg
                       // (0 = band change only, no rotation stage)
    int   hasShift;    // 8th field present -> shift-leg semantics, not v1
};

const float kDefaultGlideSec = 8.0f;   // v1 behavior when the leg omits glideSec

// leg phases: -1 entered-but-not-started (Attach has no renderer), then in
// order; GLIDE_TO_MID/BLEND_HOLD only occur when blendSec > 0
enum { PH_NOT_STARTED = -1, PH_GLIDE_TO_MID = 0, PH_BLEND_HOLD, PH_GLIDE, PH_DWELL };

std::vector<JourneyLeg> s_legs;
bool  s_active = false;
int   s_leg = -1;                // index into s_legs
int   s_legPhase = 0;            // PH_*
float s_legT = 0.0f;
float s_legTarget = 0.0f;        // absolute angle of the leg center (monotonic)
int   s_loopsDone = 0;
int   s_maxLoops = 2;            // settings.ini [journey] loops=N
int   s_moodWanderers = 1;       // emit=1 restore values, from the mood stub
int   s_moodIdleSplats = 1;

void JourneysDir(wchar_t out[MAX_PATH]) {
    wcscpy_s(out, MAX_PATH, g_iniPath);
    wchar_t* sl = wcsrchr(out, L'\\');
    if (sl) *(sl + 1) = 0;
    wcscat_s(out, MAX_PATH, L"journeys");
}

const char* kAuroraTxt =
    "# Aurora journey - violet/blue family arc with a dark intermission\r\n"
    "275 30 35 45 1   # violet dense\r\n"
    "240 25 35 40 1   # blue\r\n"
    "195 20 30 35 1   # cyan-ice\r\n"
    "10 20 5 25 0     # DARK INTERMISSION: emission off, field decays\r\n"
    "350 20 40 45 1   # magenta-red reveal\r\n"
    "300 25 35 40 1   # purple return\r\n";

const char* kStonerTxt =
    "# Stoner journey - high-contrast families, long slow blends\r\n"
    "120 30 35 40 1 20 12   # green, blend in slowly\r\n"
    "25 25 35 40 1 20 12    # orange-red, blend\r\n"
    "240 25 35 40 1 20 12   # blue, blend\r\n"
    "10 20 5 20 0           # dark intermission (no blend fields = clean)\r\n"
    "300 25 35 40 1 20 12   # magenta, blend\r\n";

const char* kDuetTxt =
    "# Duet - blue/purple choreography: resultant = emitted + shift\r\n"
    "257 35 35 40 1 0 8 0     # steady mix\r\n"
    "275 15 35 35 1 0 8 0     # purple only\r\n"
    "275 15 35 40 1 10 8 -35  # emit purple, shift old field to blue; blend the duet\r\n"
    "240 15 35 35 1 0 8 0     # emit blue\r\n"
    "240 15 35 40 1 10 8 +35  # emit blue, shift old field to purple\r\n"
    "10 20 5 20 0             # dark intermission (v1 5-field)\r\n";

// ship the sample journeys next to settings.ini (write-if-missing, like the
// built-in moods) so users have curated files to copy and edit
void WriteTextIfMissing(const wchar_t* path, const char* text) {
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, text, (DWORD)strlen(text), &w, nullptr);
    CloseHandle(f);
}

void EnsureBuiltinJourneys() {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    JourneysDir(dir);
    CreateDirectoryW(dir, nullptr);
    swprintf_s(path, L"%s\\Aurora.txt", dir);
    WriteTextIfMissing(path, kAuroraTxt);
    swprintf_s(path, L"%s\\Stoner.txt", dir);
    WriteTextIfMissing(path, kStonerTxt);
    swprintf_s(path, L"%s\\Duet.txt", dir);
    WriteTextIfMissing(path, kDuetTxt);
}

// strict parse: any malformed content line fails the whole file (a journey is
// a curated artifact — half-parsed legs would put the field who knows where)
bool ParseJourneyFile(const wchar_t* path) {
    s_legs.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return false;
    char line[512];
    int lineno = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof(line), f)) {
        lineno++;
        // strip inline comments ('#' or ';') and trailing whitespace
        for (char* p = line; *p; p++) {
            if (*p == '#' || *p == ';') { *p = 0; break; }
        }
        char* end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' ||
                              end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) continue;   // blank / comment-only line
        JourneyLeg leg;
        int emit = 0;
        leg.blendSec = 0.0f;              // optional fields default to v1:
        leg.glideSec = kDefaultGlideSec;  // no blend hold, 8 s glide
        leg.shiftDeg = 0.0f;              // 8th field: %f parses signs, so
        leg.hasShift = 0;                 // "-35" and "+35" both work
        int got = sscanf_s(s, "%f %f %f %f %d %f %f %f", &leg.hueCenter,
                           &leg.hueRange, &leg.darkFloor, &leg.dwellSec,
                           &emit, &leg.blendSec, &leg.glideSec, &leg.shiftDeg);
        if (got < 5) {
            printf("[journey] %ls:%d: malformed leg line, journey disabled\n",
                   path, lineno);
            ok = false;
            break;
        }
        leg.hueCenter = fmodf(fmodf(leg.hueCenter, 360.0f) + 360.0f, 360.0f);
        leg.hueRange  = fmaxf(0.0f, fminf(180.0f, leg.hueRange));
        leg.darkFloor = fmaxf(0.0f, fminf(100.0f, leg.darkFloor));
        leg.dwellSec  = fmaxf(1.0f, leg.dwellSec);
        leg.emit      = emit != 0 ? 1 : 0;
        leg.blendSec  = got >= 6 ? fmaxf(0.0f, fminf(600.0f, leg.blendSec)) : 0.0f;
        leg.glideSec  = got >= 7 ? fmaxf(0.5f, fminf(120.0f, leg.glideSec))
                                 : kDefaultGlideSec;
        leg.hasShift  = got >= 8;   // present (even 0) -> v2 shift-leg semantics
        leg.shiftDeg  = fmaxf(-360.0f, fminf(360.0f, leg.shiftDeg));
        s_legs.push_back(leg);
    }
    fclose(f);
    if (ok && s_legs.empty()) {
        printf("[journey] %ls: no legs, journey disabled\n", path);
        ok = false;
    }
    if (!ok) s_legs.clear();
    return ok;
}

// shortest signed arc from angle a to angle b, degrees in [-180, 180)
float ShortestSignedDelta(float a, float b) {
    return fmodf(b - a + 540.0f, 360.0f) - 180.0f;
}

// leg boundary: the ONLY place journey writes into the live config
void StartLeg(FluidRenderer& r, int i) {
    const JourneyLeg& leg = s_legs[i];
    FluidConfig& c = r.Config();
    c.hueCenter = leg.hueCenter;
    c.hueRange  = leg.hueRange;
    c.darkFloor = leg.darkFloor;
    if (leg.emit) {   // restore the mood stub's own emission switches
        c.wanderers  = s_moodWanderers != 0;
        c.idleSplats = s_moodIdleSplats != 0;
    } else {          // dark intermission: emission off, field decays
        c.wanderers  = false;
        c.idleSplats = false;
    }
    // command target stays monotonic (like the conductor's bridge), never a
    // wrapped snap. WheelHue counter-rotates emission, so fresh dye lands
    // in-band for the whole leg while old dye visibly glides.
    float held = r.HueAngleDeg();
    if (leg.hasShift) {
        // v2 shift leg: hueCenter ONLY moves the emission band (the counter-
        // rotation lands fresh dye there); the field rotation is RELATIVE —
        // held + shiftDeg, a monotonic continuation with no arc computation.
        if (leg.shiftDeg != 0.0f) {
            s_legTarget = held + leg.shiftDeg;
            if (leg.blendSec > 0.0f) {
                // same two-stage pattern as v1: marbling hold halfway through
                // the shift, then the remaining half
                r.CommandHueShift(held + leg.shiftDeg * 0.5f, leg.glideSec);
                s_legPhase = PH_GLIDE_TO_MID;
            } else {
                r.CommandHueShift(s_legTarget, leg.glideSec);
                s_legPhase = PH_GLIDE;
            }
        } else {
            // explicit 0: no rotation stage this leg — band change only,
            // straight to dwell (the previous command stays held)
            s_legTarget = held;
            s_legPhase = PH_DWELL;
        }
    } else {
        // v1: absolute angle = held + shortest arc to the family center.
        // After shift legs this targets the hueCenter equivalent nearest the
        // accumulated held angle, so the held angle keeps accumulating
        // monotonically across mixed leg kinds.
        float delta = ShortestSignedDelta(held, leg.hueCenter);
        s_legTarget = held + delta;
        if (leg.blendSec > 0.0f) {
            // stage 1: glide only HALF the arc, then marbling hold at the
            // midpoint where both families coexist; stage 2 fires after it
            r.CommandHueShift(held + delta * 0.5f, leg.glideSec);
            s_legPhase = PH_GLIDE_TO_MID;
        } else {
            r.CommandHueShift(s_legTarget, leg.glideSec);
            s_legPhase = PH_GLIDE;
        }
    }
    s_legT = 0.0f;
    if (leg.hasShift)
        printf("[journey] leg %d/%zu: hue %.0f range %.0f floor %.0f dwell %.0f emit %d blend %.0f glide %.0f shift %+.0f\n",
               i + 1, s_legs.size(), leg.hueCenter, leg.hueRange, leg.darkFloor,
               leg.dwellSec, leg.emit, leg.blendSec, leg.glideSec, leg.shiftDeg);
    else
        printf("[journey] leg %d/%zu: hue %.0f range %.0f floor %.0f dwell %.0f emit %d blend %.0f glide %.0f\n",
               i + 1, s_legs.size(), leg.hueCenter, leg.hueRange, leg.darkFloor,
               leg.dwellSec, leg.emit, leg.blendSec, leg.glideSec);
}

} // namespace

void JourneyAttach(const wchar_t* moodPath) {
    JourneyDetach();
    if (!moodPath || !moodPath[0]) return;
    wchar_t name[128];
    GetPrivateProfileStringW(L"journey", L"file", L"", name, 128, moodPath);
    if (!name[0]) return;   // mood doesn't opt in
    EnsureBuiltinJourneys();
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    JourneysDir(dir);
    swprintf_s(path, L"%s\\%s.txt", dir, name);
    if (!ParseJourneyFile(path)) return;   // missing/empty/bad: static mood
    // emit=1 legs restore the stub's own emission switches (absent = on)
    s_moodWanderers  = GetPrivateProfileIntW(L"behavior", L"wanderers", 1, moodPath);
    s_moodIdleSplats = GetPrivateProfileIntW(L"behavior", L"idle_splats", 1, moodPath);
    s_maxLoops = GetPrivateProfileIntW(L"journey", L"loops", 2, g_iniPath);
    if (s_maxLoops < 1) s_maxLoops = 1;
    s_active = true;
    s_loopsDone = 0;
    s_leg = 0;
    // StartLeg needs the renderer, which Attach doesn't have: the first
    // JourneyUpdate tick enters leg 0 (PH_NOT_STARTED)
    s_legPhase = PH_NOT_STARTED;
    printf("[journey] attached %ls (%zu leg(s), %d loop(s))\n",
           name, s_legs.size(), s_maxLoops);
}

void JourneyDetach() {
    s_active = false;
    s_legs.clear();
    s_leg = -1;
    s_legPhase = PH_NOT_STARTED;
    s_legT = 0.0f;
    s_loopsDone = 0;
}

bool JourneyActive() { return s_active; }

void JourneyUpdate(FluidRenderer& r, float dt) {
    if (!s_active || s_leg < 0) return;
    if (s_legPhase == PH_NOT_STARTED) {   // first tick after attach: enter leg 0
        StartLeg(r, s_leg);
        return;
    }
    s_legT += dt;
    const JourneyLeg& leg = s_legs[s_leg];
    if (s_legPhase == PH_GLIDE_TO_MID) {
        if (s_legT >= leg.glideSec) { s_legPhase = PH_BLEND_HOLD; s_legT = 0.0f; }
        return;
    }
    if (s_legPhase == PH_BLEND_HOLD) {
        if (s_legT >= leg.blendSec) {
            // stage 2: glide the remaining half of the arc to the leg center
            r.CommandHueShift(s_legTarget, leg.glideSec);
            s_legPhase = PH_GLIDE;
            s_legT = 0.0f;
        }
        return;
    }
    if (s_legPhase == PH_GLIDE) {
        if (s_legT >= leg.glideSec) { s_legPhase = PH_DWELL; s_legT = 0.0f; }
        return;
    }
    // PH_DWELL
    if (s_legT < leg.dwellSec) return;
    int next = s_leg + 1;
    if (next >= (int)s_legs.size()) {
        next = 0;
        s_loopsDone++;
        if (s_loopsDone >= s_maxLoops) {
            // journey over: the field stays exactly where it is (command still
            // held); the mood's normal dwell/transition resumes from here and
            // the conductor's next bridge re-commands/releases as usual
            printf("[journey] %d loop(s) done, journey ended\n", s_loopsDone);
            s_active = false;
            return;
        }
    }
    s_leg = next;
    StartLeg(r, s_leg);
}

int JourneyLegInfo(int* legIndex, int* legCount) {
    if (!s_active || s_leg < 0) return 0;
    if (legIndex) *legIndex = s_leg;
    if (legCount) *legCount = (int)s_legs.size();
    return 1;
}

bool JourneyInBlendHold() {
    return s_active && s_leg >= 0 && s_legPhase == PH_BLEND_HOLD;
}
