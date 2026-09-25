// UI model for the settings window (UI-REHAUL phase 1a).
//
// The window is immediate-mode: every frame it reads each value through the key table's
// pointer, so nothing is cached and nothing can go stale (R1). This model owns:
//   - the key table built from src/ui/keys.inc (label, range, pointer, ini key + keymeta),
//   - the ONE per-key write path (pointer write, hooks, per-key ini write) that sliders,
//     undo and redo all use (auditor pre-flight item 2),
//   - the reset target (code defaults + the active preset file) and the dirty count,
//   - gates / looks (R2), the header facts (R1), undo/redo.
// It never touches the renderer directly: the live window installs UiHooks, the headless
// --ui-shot run leaves them null.
#pragma once
#include <string>
#include <vector>
#include "../fluid.h"

enum KeyGroup {
    G_FILM, G_MASSES, G_DROPLETS, G_OIL, G_COLOUR, G_OPTICS, G_LID, G_POST, G_MOTION,
    G_INK, G_FLUID, G_OUTPUT, G_SYSTEM, G_COUNT
};
enum KeyFlag : unsigned {
    KF_MOTION = 1, KF_UNVERIFIED = 2, KF_SUPERSEDED = 4, KF_SHELL = 8, KF_MACHINE = 16,
    KF_GLOBAL = 32   // right column: settings that live outside any mode (DECISIONS 1, D1)
};
enum LookBit : unsigned { LOOK_F = 1, LOOK_A = 2, LOOK_I = 4 };

struct GateNode {                 // tiny expression tree: && / || / compare
    enum Kind { AND, OR, CMP, LOOKIS } kind = CMP;
    int a = -1, b = -1;           // children (AND/OR)
    int row = -1;                 // CMP: row index of sec.key
    int op = 0;                   // CMP: 0 == 1 != 2 > 3 < 4 >= 5 <=
    float num = 0;
    unsigned look = 0;            // LOOKIS
};

struct KeyRow {
    bool isCheck = false;
    std::string label;            // trimmed display label
    int indent = 0;               // leading spaces in the old table = sub-key
    float mn = 0, mx = 1, step = 1;
    int dec = 0;
    float* f = nullptr;
    int* i = nullptr;
    bool* b = nullptr;
    std::string sec, key;         // ini section / key (UTF-8)
    std::wstring wsec, wkey;
    bool reinit = false;
    std::string tip;
    KeyGroup group = G_SYSTEM;
    unsigned looks = 7;
    std::string gateSrc;
    int gate = -1;                // root GateNode index, -1 = always on
    unsigned flags = 0;
    std::string special;          // raw keymeta 'special'
    unsigned front = 0;           // looks whose front page carries this knob (phase 1b)
    // derived
    ptrdiff_t off = -1;           // offset inside FluidConfig, -1 = global / registry
    std::vector<std::pair<int, std::string>> enums;   // special enum:
    std::string negName, zeroName; // special neg: / zero:
    bool isPeak = false, isLook = false, isAutostart = false;
};

// Hooks the per-key write path calls (live window: the renderer; headless: null).
struct UiHooks {
    void (*reinitWanderers)() = nullptr;
    void (*ensureLook)() = nullptr;
    void (*setResolutions)(int simRes, int dyeRes) = nullptr;
    void (*applyPreset)(const std::wstring& path) = nullptr; // live: main.cpp ApplyPreset
};

struct UiHeader {
    std::string look;             // "Fluid (WE)" / "Liquid Acid" / "Ink"
    std::string preset;           // stem of the active preset, or "no preset"
    std::string presetPath;
    std::string overlays;         // "Mirror - quad (overlay)" ...
    std::string cycle;            // "Cycle off" / "Cycle on" (stage name: animators.h, 1b)
    std::string hdr;              // "HDR on 1000 nt BT.2020"
    std::string mirror;           // "Mirror off" / "Mirror quad"
    int dirty = 0;
    std::string text;             // the whole strip, joined with " · "
};

enum RowState { RS_VISIBLE, RS_DISABLED, RS_HIDDEN };

void UiModelInit(FluidConfig& cfg);           // idempotent for the same cfg
bool UiModelReady();
FluidConfig& UiCfg();
void UiSetHooks(const UiHooks& h);
const UiHooks& UiGetHooks();
int  UiRowCount();
KeyRow& UiRow(int i);
int  UiFindRow(const char* sec, const char* key);
const std::vector<std::string>& UiPointerExceptions();  // globals outside FluidConfig
const std::vector<std::string>& UiModelErrors();        // unresolved gates, bad pointers

unsigned UiCurrentLook();                     // LOOK_F / LOOK_A / LOOK_I
const char* UiLookName(unsigned look);
float UiValue(int row);
float UiTarget(int row);                      // reset target: defaults + active preset
float UiDefault(int row);                     // code default (FluidConfig{} / global default)
bool  UiCountsForDirty(int row);
bool  UiDirty(int row);
int   UiDirtyCount();
bool  UiOutOfRange(int row);
std::string UiValueText(int row);             // sentinel-aware display text
std::string UiNumText(int row, float v);

// state for the current look: hidden (wrong look), disabled (gate / measured-inert /
// superseded / read-only) with a one-line reason, or visible.
RowState UiRowState(int row, std::string* reason, int* jumpRow = nullptr);

// THE write path. pushUndo=false while a drag is in flight (one drag = one entry).
void UiSetValue(int row, float v);
void UiPushKeyUndo(int row, float oldV, float newV);
bool UiCanUndo();
bool UiCanRedo();
void UiUndo();
void UiRedo();

// whole-config changes (preset apply / revert): snapshot before, entry after
void UiBeginWholeChange();
void UiEndWholeChange(const std::wstring& path);
void UiNotifyPresetApplied(const std::wstring& path);   // sets [ui] active_preset / overlays
void UiNotifyPresetSaved(const std::wstring& path);
void UiLoadActivePreset();                    // startup: [ui] active_preset, else base_mood
void UiSetActivePresetPath(const std::wstring& path);   // headless: the --ini file itself
const std::wstring& UiActivePresetPath();
bool UiSaveActivePreset();                    // overwrite the active preset file
bool UiSavePresetAs(const std::wstring& name, std::wstring* outPath);
void UiApplyPresetHeadless(const std::wstring& path);   // same merge as main.cpp ApplyPreset

void UiSetLook(unsigned look);               // look radio: one undo entry, same write path
UiHeader UiComputeHeader();
bool UiAutostartCached();
bool UiFileHasLookSection(const std::wstring& path);
std::string UiNarrow(const std::wstring& w);
std::wstring UiWide(const std::string& s);
