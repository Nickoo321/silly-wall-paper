// Settings window (UI-REHAUL phase 1a + 1b): a Dear ImGui "DX11 island".
// 1b: the look panes (preset strip, big knobs, freezes, Advanced), the Cycle playlist pane, the
// cycle header, transition locks + ghost ticks, pause-for-editing, tile thumbnails (embedded
// RCDATA 201-204, src/ui/thumbs/*.png from handoff\images renders).
//
// Rules (auditor pre-flight 4/5/7):
//  - its OWN D3D11 device + FLIP_DISCARD R8G8B8A8_UNORM swap chain on the settings HWND, created
//    on open and released on close; zero shared state with the D3D12 renderer;
//  - main thread, inside the existing message pump; drawn only on input / a 30 Hz WM_TIMER while
//    something is happening (4 Hz otherwise for live values), never per wallpaper frame;
//    Present(0, 0) and SetMaximumFrameLatency(1), never Present(1);
//  - recreated on device-removed; per-monitor v2 DPI (process-wide, main.cpp): WM_DPICHANGED
//    rescales, ImGui's DPI enabler is never called;
//  - never appears on its own: only CMD_SETTINGS (tray / second launch) opens it.
// Headless (--ui-shot / --ui-dump): WARP device + offscreen texture, no window, no tray, no
// renderer, config read-only (main.cpp returns before the mutex, like --shot).

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ui_model.h"
#include "../app_state.h"
#include "ui_cycle.h"
#include "ui_presets.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using Microsoft::WRL::ComPtr;

namespace {

// ---------------------------------------------------------------------------- view state
struct View {
    char search[128] = {};
    bool thisLook = true;          // hide rows the current look does not read
    bool changed = false;          // only rows that differ from the preset
    bool everything = false;       // reveal hidden rows and unlock disabled ones
    bool focusSearch = false;
    int  dragRow = -1;
    float dragOld = 0;
    char saveAsName[96] = {};
    bool openSaveAs = false;
    std::string status;            // one-line result of the last action
    bool live = false;             // false = headless (--ui-shot)
    // phase 1b
    int  pane = -1;                // centre pane override (-1 = the running tile); "edit current stage"
    std::wstring selPath;          // selected preset in the strip
    bool saveAsChanges = true;     // Save as: only my changes (partial on the base) / self-contained
    char renameBuf[96] = {};
    bool openRename = false, openDelete = false;
    std::vector<std::string> opLog;// file operations (Save / Save as / Delete), for the dump
    bool noEditPause = false;      // the user said "let the cycle run" while the window is open
    ULONGLONG lastPauseArm = 0;
    ULONGLONG lastRescan = 0;
    int  addStageSel = -1;
    int  dwellEditRow = -1;
    float dwellEditVal = 0;
    UiLiveValues liveNow;          // sampled once per frame (motion detection needs one sample)
};
View s_view;
FluidConfig* s_headlessCfg = nullptr;   // --ui-shot: the config the scripted director writes

const char* kGroupNames[G_COUNT] = {
    "Film", "Masses", "Droplets", "Oil shape & sim", "Colour & animation", "Lamp & lens optics",
    "Lid", "Post & film stock", "Motion & rig", "Ink", "Fluid sim", "Output / HDR", "Behaviour & system",
};

const ImVec4 kAccent   = ImVec4(0.478f, 0.722f, 1.000f, 1.0f);
const ImVec4 kDim      = ImVec4(0.60f, 0.61f, 0.66f, 1.0f);
const ImVec4 kWarn     = ImVec4(1.00f, 0.72f, 0.35f, 1.0f);
const ImVec4 kChanged  = ImVec4(1.00f, 0.80f, 0.30f, 1.0f);

// ---------------------------------------------------------------------------- style
void ApplyStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0;
    st.FrameRounding = 6;
    st.GrabRounding = 6;
    st.ChildRounding = 6;
    st.PopupRounding = 6;
    st.TabRounding = 6;
    st.ScrollbarRounding = 6;
    st.FramePadding = ImVec2(8, 4);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.WindowPadding = ImVec2(14, 12);
    st.CellPadding = ImVec2(6, 3);
    st.GrabMinSize = 12;
    st.ScrollbarSize = 14;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.110f, 0.114f, 0.133f, 1);
    c[ImGuiCol_ChildBg]          = ImVec4(0.110f, 0.114f, 0.133f, 1);
    c[ImGuiCol_PopupBg]          = ImVec4(0.140f, 0.145f, 0.170f, 1);
    c[ImGuiCol_FrameBg]          = ImVec4(0.180f, 0.186f, 0.216f, 1);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.230f, 0.240f, 0.280f, 1);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.260f, 0.280f, 0.330f, 1);
    c[ImGuiCol_SliderGrab]       = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.85f, 1.0f, 1);
    c[ImGuiCol_CheckMark]        = kAccent;
    c[ImGuiCol_Button]           = ImVec4(0.200f, 0.210f, 0.245f, 1);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.270f, 0.330f, 0.420f, 1);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.300f, 0.420f, 0.580f, 1);
    c[ImGuiCol_Header]           = ImVec4(0.170f, 0.200f, 0.260f, 1);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.220f, 0.270f, 0.360f, 1);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.250f, 0.320f, 0.440f, 1);
    c[ImGuiCol_Tab]              = ImVec4(0.160f, 0.170f, 0.200f, 1);
    c[ImGuiCol_TabHovered]       = ImVec4(0.270f, 0.330f, 0.420f, 1);
    c[ImGuiCol_TabSelected]      = ImVec4(0.230f, 0.300f, 0.410f, 1);
    c[ImGuiCol_Separator]        = ImVec4(0.250f, 0.260f, 0.300f, 1);
    c[ImGuiCol_Text]             = ImVec4(0.880f, 0.890f, 0.920f, 1);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.470f, 0.480f, 0.520f, 1);
    c[ImGuiCol_TableRowBgAlt]    = ImVec4(1, 1, 1, 0.025f);
    st.ScaleAllSizes(scale);
    st.FontSizeBase = 16.0f;
    st.FontScaleDpi = scale;
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\SegUIVar.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    ImFont* f = nullptr;
    for (const char* p : candidates)
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES && (f = io.Fonts->AddFontFromFileTTF(p, 16.0f)))
            break;
    if (!f) io.Fonts->AddFontDefault();
    // symbols the UI prints (middle dot, arrows, bullets) come from Segoe UI Symbol when present
    if (f && GetFileAttributesA("C:\\Windows\\Fonts\\seguisym.ttf") != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig mc;
        mc.MergeMode = true;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 16.0f, &mc);
    }
    io.IniFilename = nullptr;       // no imgui.ini next to the exe
    io.LogFilename = nullptr;
}

// ---------------------------------------------------------------------------- helpers
bool IContains(const std::string& hay, const char* needle) {
    if (!needle[0]) return true;
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= hay.size(); i++)
        if (_strnicmp(hay.c_str() + i, needle, n) == 0) return true;
    return false;
}

// every query word must START a word of the label or a _-token of the key (so "lid" finds
// lid_* and "Lid (cover glass)", not "glide"); help text is searched only for words >= 4 chars
bool WordPrefix(const std::string& hay, const std::string& w) {
    size_t n = w.size();
    for (size_t i = 0; i + n <= hay.size(); i++) {
        bool start = i == 0 || !isalnum((unsigned char)hay[i - 1]);
        if (start && _strnicmp(hay.c_str() + i, w.c_str(), n) == 0) return true;
    }
    return false;
}

bool RowMatchesSearch(const KeyRow& r, const char* q) {
    if (!q[0]) return true;
    std::string all = q, w;
    std::vector<std::string> words;
    for (char ch : all) { if (ch == ' ') { if (!w.empty()) words.push_back(w); w.clear(); } else w += ch; }
    if (!w.empty()) words.push_back(w);
    std::string key = r.sec + "." + r.key;
    for (const std::string& word : words) {
        bool hit = WordPrefix(r.label, word) || WordPrefix(key, word) || IContains(key, word.c_str()) && word.find('_') != std::string::npos;
        if (!hit && word.size() >= 4) hit = IContains(r.tip, word.c_str());
        if (!hit) return false;
    }
    return true;
}

bool Chip(const char* label, bool* v) {
    ImGui::PushStyleColor(ImGuiCol_Button, *v ? ImVec4(0.23f, 0.36f, 0.52f, 1) : ImVec4(0.18f, 0.19f, 0.22f, 1));
    bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor();
    if (clicked) *v = !*v;
    return clicked;
}

void Badge(const char* text, const ImVec4& col) {
    ImGui::SameLine(0, 6);
    ImGui::TextColored(col, "%s", text);
}

std::string EscapeFmt(const std::string& s) {   // literal text as an ImGui format string
    std::string o;
    for (char ch : s) { if (ch == '%') o += '%'; o += ch; }
    return o;
}

// the row's displayed value inside the slider: sentinel names verbatim, numbers with decimals
std::string SliderFormat(int i) {
    const KeyRow& r = UiRow(i);
    float v = UiValue(i);
    if ((r.isPeak && v <= 0) || (!r.negName.empty() && v < 0) || (!r.zeroName.empty() && v == 0))
        return EscapeFmt(UiValueText(i));
    char f[16];
    snprintf(f, sizeof(f), "%%.%df", (r.i || r.dec < 0) ? 0 : r.dec);
    return f;
}

float Quantize(const KeyRow& r, float v) {
    if (r.i) return (float)lroundf(v);
    if (v < r.mn || v > r.mx || r.step <= 0) return v;     // typed out-of-range values stay as typed
    float q = r.mn + roundf((v - r.mn) / r.step) * r.step;
    return q < r.mn ? r.mn : (q > r.mx ? r.mx : q);
}

void SetWithUndo(int i, float v) {
    float old = UiValue(i);
    UiSetValue(i, v);
    UiPushKeyUndo(i, old, UiValue(i));
}

void Tooltip(int i, const std::string& reason) {
    const KeyRow& r = UiRow(i);
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
    if (!r.tip.empty()) ImGui::TextUnformatted(r.tip.c_str());
    ImGui::TextColored(kDim, "[%s] %s", r.sec.c_str(), r.key.c_str());
    if (UiCountsForDirty(i)) {
        ImGui::TextColored(kDim, "preset: %s   default: %s", UiNumText(i, UiTarget(i)).c_str(),
                           UiNumText(i, UiDefault(i)).c_str());
    }
    if (r.flags & KF_MOTION) ImGui::TextColored(kAccent, "Motion: the effect only shows over time, not in a still.");
    if (r.flags & KF_UNVERIFIED) ImGui::TextColored(kWarn, "?: which looks read it / its gate is not verified yet.");
    if (!reason.empty()) ImGui::TextColored(kWarn, "%s", reason.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------- live markers
// D5: a thin ghost tick ONLY on keys that are animating right now. Two sources:
//  - the DERIVED animators (animators.h getters, sampled once per frame): the knob shows the
//    base (Config), the ghost shows where the animator has it now;
//  - a stage transition's lerp / a journey leg WRITING the key: the knob is locked (read-only
//    for those seconds) and the ghost shows where it is going.
const ImVec4 kGhost = ImVec4(0.45f, 0.95f, 0.85f, 1.0f);

bool GhostOf(int i, float* gv, std::string* what) {
    const KeyRow& r = UiRow(i);
    std::string why;
    float tgt = 0;
    if (UiRowAnimLocked(i, &why, &tgt)) {
        *gv = tgt;
        *what = why;
        return fabsf(tgt - UiValue(i)) > r.step * 0.5f;
    }
    const UiLiveValues& lv = s_view.liveNow;
    if (!lv.valid || !r.f) return false;
    int anim = -1;
    float v = 0;
    const std::string k = r.sec + "." + r.key;
    if (k == "post.camera_focus")            { v = lv.rig[5]; anim = UA_RIG; }
    else if (k == "post.focus_tilt_angle")   { v = lv.rig[4]; anim = UA_RIG; }
    else if (k == "post.light_x")            { v = lv.rig[0]; anim = UA_RIG; }
    else if (k == "post.light_y")            { v = lv.rig[1]; anim = UA_RIG; }
    else if (k == "liquid_acid.film_hue2")   { v = lv.hue2Deg; anim = UA_HUE2; }
    else if (k == "color.post_hue" && UiCurrentLook() == LOOK_F) {
        v = fmodf(UiValue(i) + lv.hueAngleDeg, 360.0f);
        if (v < 0) v += 360.0f;
        anim = UA_HUE_SHIFT;
    }
    if (anim < 0 || !(lv.moving & (1u << anim))) return false;
    if (fabsf(v - UiValue(i)) <= r.step * 0.5f) return false;
    *gv = v;
    *what = "moving now (" + UiAnimatorName(anim) + "): " + UiNumText(i, v);
    return true;
}

// period keys on the front page read as a speed sentence; the ini key is unchanged
std::string PeriodText(float v, bool lap) {
    if (v <= 0.0f) return "off";
    char b[64];
    int s = (int)lroundf(v);
    if (s < 90) snprintf(b, sizeof(b), "one %s every %d s", lap ? "lap" : "turn", s);
    else if (s < 5400 && s % 60 == 0) snprintf(b, sizeof(b), "one %s every %d min", lap ? "lap" : "turn", s / 60);
    else if (s < 5400) snprintf(b, sizeof(b), "one %s every %dm %02ds", lap ? "lap" : "turn", s / 60, s % 60);
    else snprintf(b, sizeof(b), "one %s every %.1f h", lap ? "lap" : "turn", v / 3600.0f);
    return b;
}

// ---------------------------------------------------------------------------- one row
// label != nullptr: a front-page knob (friendly name, speed wording for periods)
void DrawRow(int i, RowState st, const std::string& reasonIn, int jump, bool compact, const char* label = nullptr) {
    KeyRow& r = UiRow(i);
    ImGui::PushID(i);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    std::string reason = reasonIn;
    std::string animWhy;
    bool animLocked = UiRowAnimLocked(i, &animWhy);
    if (animLocked) reason = animWhy;              // never unlocked by "Show everything"
    bool locked = animLocked || (st != RS_VISIBLE && !s_view.everything);
    bool dirty = UiDirty(i);
    const bool indent = r.indent && !label;   // big knobs stand on their own
    if (indent) ImGui::Indent(ImGui::GetFontSize() * 0.9f);
    ImGui::AlignTextToFramePadding();
    if (dirty) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x - 7, p.y + h * 0.5f), 3.2f,
                                                    ImGui::GetColorU32(kChanged));
    }
    const char* shown = label ? label : r.label.c_str();
    if (st != RS_VISIBLE || animLocked) ImGui::TextDisabled("%s", shown);
    else ImGui::TextUnformatted(shown);
    if (r.flags & KF_MOTION) Badge("motion", kAccent);
    if (r.flags & KF_UNVERIFIED) Badge("?", kWarn);
    if (UiOutOfRange(i)) Badge("outside slider range", kWarn);
    if (!reason.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, animLocked ? kGhost : st == RS_HIDDEN ? kDim : kWarn);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::SmallButton(reason.c_str()) && jump >= 0) {
            // jump to the key the gate names: search for it, show it
            snprintf(s_view.search, sizeof(s_view.search), "%s", UiRow(jump).key.c_str());
            s_view.changed = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    if (indent) ImGui::Unindent(ImGui::GetFontSize() * 0.9f);

    ImGui::TableSetColumnIndex(1);
    ImGui::BeginDisabled(locked);
    float v = UiValue(i);
    if (r.isCheck) {
        bool b = v > 0.5f;
        if (ImGui::Checkbox("##v", &b)) SetWithUndo(i, b ? 1.0f : 0.0f);
        Tooltip(i, reason);
    } else if (!r.enums.empty() && (compact || r.enums.size() > 3)) {
        int cur = (int)lroundf(v);
        std::string curName = UiValueText(i);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##v", curName.c_str())) {
            for (auto& e : r.enums)
                if (ImGui::Selectable(e.second.c_str(), e.first == cur) && e.first != cur) SetWithUndo(i, (float)e.first);
            ImGui::EndCombo();
        }
        Tooltip(i, reason);
    } else if (!r.enums.empty()) {
        int cur = (int)lroundf(v);
        for (size_t k = 0; k < r.enums.size(); k++) {
            if (k) ImGui::SameLine();
            ImGui::PushID((int)k);
            if (ImGui::RadioButton(r.enums[k].second.c_str(), cur == r.enums[k].first) && cur != r.enums[k].first)
                SetWithUndo(i, (float)r.enums[k].first);
            Tooltip(i, reason);
            ImGui::PopID();
        }
    } else {
        float tmp = v;
        std::string fmt = SliderFormat(i);
        if (label && r.key.size() > 7 && r.key.compare(r.key.size() - 7, 7, "_period") == 0)
            fmt = EscapeFmt(PeriodText(v, r.key == "color_cycle_period"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool changed = ImGui::SliderFloat("##v", &tmp, r.mn, r.mx, fmt.c_str(),
                                          ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemActivated()) { s_view.dragRow = i; s_view.dragOld = v; }
        if (changed) UiSetValue(i, Quantize(r, tmp));
        if (ImGui::IsItemDeactivatedAfterEdit() && s_view.dragRow == i) {
            UiPushKeyUndo(i, s_view.dragOld, UiValue(i));
            s_view.dragRow = -1;
        }
        // preset value tick on the track
        float t = UiTarget(i);
        if (UiCountsForDirty(i) && t >= r.mn && t <= r.mx && r.mx > r.mn) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            float g = ImGui::GetStyle().GrabMinSize * 0.5f + 2;
            float x = a.x + g + (t - r.mn) / (r.mx - r.mn) * (b.x - a.x - 2 * g);
            ImU32 col = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.55f));
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y), ImVec2(x, a.y + 4), col, 2);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, b.y - 4), ImVec2(x, b.y), col, 2);
        }
        // the ghost tick: only while something animates this key right now
        float gv = 0;
        std::string gwhat;
        if (GhostOf(i, &gv, &gwhat) && r.mx > r.mn) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            float g = ImGui::GetStyle().GrabMinSize * 0.5f + 2;
            float t = (std::min)((std::max)((gv - r.mn) / (r.mx - r.mn), 0.0f), 1.0f);
            float x = a.x + g + t * (b.x - a.x - 2 * g);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y + 1), ImVec2(x, b.y - 1), ImGui::GetColorU32(kGhost), 1.5f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", gwhat.c_str());
        }
        Tooltip(i, reason);
        if (ImGui::BeginPopupContextItem("ctx")) {
            std::string pt = "Reset to preset (" + UiNumText(i, UiTarget(i)) + ")";
            std::string dt = "Reset to default (" + UiNumText(i, UiDefault(i)) + ")";
            if (ImGui::MenuItem(pt.c_str(), nullptr, false, UiCountsForDirty(i))) SetWithUndo(i, UiTarget(i));
            if (ImGui::MenuItem(dt.c_str())) SetWithUndo(i, UiDefault(i));
            ImGui::EndPopup();
        }
    }
    ImGui::EndDisabled();

    if (!compact) {   // compact (right column): reset lives in the right-click menu
        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(!dirty || locked);
        if (ImGui::SmallButton("Reset")) SetWithUndo(i, UiTarget(i));
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------- pages
// Where a row lives (DECISIONS 1, D1 "every setting lives in exactly ONE place"):
// centre = the selected mode's Advanced expander; right column = KF_GLOBAL keys (outside any
// mode); the cycle keys = the Cycle tile's centre; the two look keys = the left tiles.
enum Place { P_CENTRE, P_GLOBAL, P_CYCLE, P_LOOK };
Place PlaceOf(const KeyRow& r) {
    if (r.isLook) return P_LOOK;
    if (r.sec == "cycle") return P_CYCLE;
    if (r.flags & KF_GLOBAL) return P_GLOBAL;
    return P_CENTRE;
}

struct Counts { int total = 0, visible = 0, disabled = 0, hidden = 0, front = 0, global = 0, cycle = 0; };

Counts CountRows() {
    Counts c;
    unsigned look = UiCurrentLook();
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        Place pl = PlaceOf(r);
        if (pl == P_LOOK) continue;
        if (pl == P_GLOBAL) { c.global++; continue; }
        if (pl == P_CYCLE) { c.cycle++; continue; }
        c.total++;
        RowState st = UiRowState(i, nullptr);
        if (st == RS_VISIBLE) c.visible++;
        else if (st == RS_DISABLED) c.disabled++;
        else c.hidden++;
        if (r.front & look) c.front++;
    }
    return c;
}

bool BeginRowTable(const char* id, bool compact) {
    if (!ImGui::BeginTable(id, compact ? 2 : 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, compact ? 0.52f : 0.46f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, compact ? 0.48f : 0.54f);
    if (!compact) ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.2f);
    return true;
}

void DrawRowsOf(Place place, bool compact, const char* tableId) {
    if (!BeginRowTable(tableId, compact)) return;
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        if (PlaceOf(r) != place) continue;
        std::string why;
        int jump = -1;
        RowState st = UiRowState(i, &why, &jump);
        if (st == RS_HIDDEN && !s_view.everything) continue;
        DrawRow(i, st, why, jump, compact);
    }
    ImGui::EndTable();
}

// the selected mode's Advanced expander: the generated key table filtered to that mode's keys
void DrawAdvanced() {
    ImGuiIO& io = ImGui::GetIO();
    if (s_view.focusSearch || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))) {
        ImGui::SetKeyboardFocusHere();
        s_view.focusSearch = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 17);
    ImGui::InputTextWithHint("##search", "Search label, key or help (Ctrl+F)", s_view.search, sizeof(s_view.search));
    ImGui::SameLine();
    if (s_view.search[0] && ImGui::Button("Clear")) s_view.search[0] = 0;
    ImGui::SameLine(0, 14);
    Chip("This look", &s_view.thisLook);
    ImGui::SameLine();
    Chip("Changed", &s_view.changed);
    ImGui::SameLine();
    Chip("Show everything", &s_view.everything);
    if (s_view.everything) s_view.thisLook = false;

    Counts cn = CountRows();
    std::vector<int> byGroup[G_COUNT];
    std::vector<std::string> reasons(UiRowCount());
    std::vector<RowState> states(UiRowCount());
    std::vector<int> jumps(UiRowCount(), -1);
    int shown = 0, onFront = 0;
    const unsigned look = UiCurrentLook();
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        if (PlaceOf(r) != P_CENTRE) continue;
        // one-place rule (D1): the big knobs above are not repeated in Advanced
        if (r.front & look) { if (s_view.search[0] && RowMatchesSearch(r, s_view.search)) onFront++; continue; }
        states[i] = UiRowState(i, &reasons[i], &jumps[i]);
        if (states[i] == RS_HIDDEN && s_view.thisLook) continue;
        if (s_view.changed && !UiDirty(i)) continue;
        if (!RowMatchesSearch(r, s_view.search)) continue;
        byGroup[r.group].push_back(i);
        shown++;
    }
    ImGui::TextColored(kDim, "%d keys for this look: %d live, %d disabled (reason shown), %d other-look keys hidden  |  showing %d",
                       cn.visible + cn.disabled, cn.visible, cn.disabled, cn.hidden, shown);
    bool searching = s_view.search[0] != 0 || s_view.changed;
    for (int g = 0; g < G_COUNT; g++) {
        if (byGroup[g].empty()) continue;
        char title[96];
        snprintf(title, sizeof(title), "%s  (%d)###grp%d", kGroupNames[g], (int)byGroup[g].size(), g);
        if (searching) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (!ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                      ImGuiTreeNodeFlags_SpanAvailWidth))
            continue;
        ImGui::PushID(g);
        if (BeginRowTable("t", false)) {
            for (int i : byGroup[g]) DrawRow(i, states[i], reasons[i], jumps[i], false);
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::TreePop();
    }
    if (onFront) ImGui::TextColored(kDim, "%d match%s %s a big knob above.", onFront, onFront == 1 ? "" : "es", onFront == 1 ? "is" : "are");
    if (shown == 0 && !onFront) ImGui::TextColored(kDim, "Nothing matches. Try \"Show everything\".");
}

void DrawPalette() {
    FluidConfig& c = UiCfg();
    ImGui::TextColored(kDim, "Palette (used when \"Random color (hue wheel)\" is off)");
    for (int k = 0; k < 5; k++) {
        ImGui::PushID(1000 + k);
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "Color %d", k + 1);
        if (ImGui::ColorEdit3(lbl, c.splatColors + k * 3, ImGuiColorEditFlags_NoInputs)) {
            if (!g_configReadOnly && g_iniPath[0]) {
                wchar_t key[32], val[64];
                swprintf_s(key, L"splat_color_%d", k + 1);
                float* col = c.splatColors + k * 3;
                swprintf_s(val, L"%.4f %.4f %.4f", col[0], col[1], col[2]);
                WritePrivateProfileStringW(L"color", key, val, g_iniPath);
            }
        }
        if (k < 4) ImGui::SameLine(0, 14);
        ImGui::PopID();
    }
}

// ---- thumbnails: embedded RCDATA 201-204 (src/ui/thumbs/*.png, 400x225, from the handoff\images
// renders) decoded with WIC into D3D11 textures on whichever device draws the window
ComPtr<ID3D11ShaderResourceView> s_thumbs[4];

void LoadThumbs(ID3D11Device* dev) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return;
    for (int t = 0; t < 4; t++) {
        s_thumbs[t].Reset();
        HRSRC rs = FindResourceW(nullptr, MAKEINTRESOURCEW(201 + t), RT_RCDATA);
        HGLOBAL rg = rs ? LoadResource(nullptr, rs) : nullptr;
        const void* data = rg ? LockResource(rg) : nullptr;
        DWORD size = rs ? SizeofResource(nullptr, rs) : 0;
        if (!data || !size) continue;
        ComPtr<IWICStream> st;
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> fr;
        ComPtr<IWICFormatConverter> cv;
        if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromMemory((BYTE*)data, size)) ||
            FAILED(f->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) ||
            FAILED(dec->GetFrame(0, &fr)) || FAILED(f->CreateFormatConverter(&cv)) ||
            FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                  WICBitmapPaletteTypeCustom)))
            continue;
        UINT w = 0, h = 0;
        cv->GetSize(&w, &h);
        std::vector<uint8_t> px((size_t)w * h * 4);
        if (FAILED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) continue;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { px.data(), w * 4, 0 };
        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &tex)))
            dev->CreateShaderResourceView(tex.Get(), nullptr, &s_thumbs[t]);
    }
}
void ReleaseThumbs() { for (auto& t : s_thumbs) t.Reset(); }

// ---- LEFT: the mode tiles, one radio group = "what the wallpaper is doing" (D2)
enum Tile { T_FLUID, T_ACID, T_INK, T_CYCLE };
const char* kTileNames[4] = { "Fluid (WE)", "Liquid Acid", "Ink", "Cycle" };
int SelectedTile() {
    if (UiCycleOn()) return T_CYCLE;
    unsigned l = UiCurrentLook();
    return l == LOOK_A ? T_ACID : l == LOOK_I ? T_INK : T_FLUID;
}
int TileOfLook(unsigned l) { return l == LOOK_A ? T_ACID : l == LOOK_I ? T_INK : T_FLUID; }
int ShownPane() { return s_view.pane >= 0 ? s_view.pane : SelectedTile(); }

void SelectTile(int t) {
    int ce = UiFindRow("cycle", "enabled");
    s_view.pane = -1;
    if (t == T_CYCLE) {
        if (ce >= 0 && UiValue(ce) < 0.5f) SetWithUndo(ce, 1.0f);
        return;
    }
    if (ce >= 0 && UiValue(ce) > 0.5f) SetWithUndo(ce, 0.0f);
    UiSetLook(t == T_ACID ? LOOK_A : t == T_INK ? LOOK_I : LOOK_F);
}

void DrawTiles() {
    static const char* subs[4] = { "native fluid sim", "oil on inked water", "ink in water", "the playlist in turn" };
    int sel = SelectedTile();
    int shown = ShownPane();
    float w = ImGui::GetContentRegionAvail().x;
    float thumbH = (w - 14) * 9.0f / 16.0f;
    float tileH = thumbH + ImGui::GetTextLineHeightWithSpacing() * 2 + 18;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int t = 0; t < 4; t++) {
        ImGui::PushID(t);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("tile", ImVec2(w, tileH));
        bool hov = ImGui::IsItemHovered();
        ImU32 bg = ImGui::GetColorU32(t == sel ? ImVec4(0.17f, 0.24f, 0.34f, 1)
                                               : hov ? ImVec4(0.17f, 0.18f, 0.22f, 1) : ImVec4(0.14f, 0.145f, 0.17f, 1));
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + tileH), bg, 8);
        if (t == sel) dl->AddRect(p, ImVec2(p.x + w, p.y + tileH), ImGui::GetColorU32(kAccent), 8.0f, ImDrawFlags_None, 2.0f);
        else if (t == shown) dl->AddRect(p, ImVec2(p.x + w, p.y + tileH), ImGui::GetColorU32(kDim), 8.0f, ImDrawFlags_None, 1.0f);
        // static thumbnail (a headless render of the look; phase 3 = per-preset thumbnails)
        ImVec2 a(p.x + 7, p.y + 7), b(p.x + w - 7, p.y + 7 + thumbH);
        if (s_thumbs[t])
            dl->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)s_thumbs[t].Get()), a, b, ImVec2(0, 0), ImVec2(1, 1),
                                IM_COL32_WHITE, 5.0f);
        else {
            dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.09f, 1)), 5);
            dl->AddText(ImVec2(a.x + 8, b.y - ImGui::GetTextLineHeight() - 6), ImGui::GetColorU32(kDim), "thumbnail");
        }
        float ty = b.y + 5;
        dl->AddText(ImVec2(p.x + 10, ty), ImGui::GetColorU32(t == sel ? kAccent : ImVec4(0.88f, 0.89f, 0.92f, 1)), kTileNames[t]);
        dl->AddText(ImVec2(p.x + 10, ty + ImGui::GetTextLineHeightWithSpacing()), ImGui::GetColorU32(kDim), subs[t]);
        if (t == sel)   // running dot
            dl->AddCircleFilled(ImVec2(p.x + w - 14, ty + ImGui::GetTextLineHeight() * 0.5f), 5,
                                ImGui::GetColorU32(ImVec4(0.35f, 0.85f, 0.45f, 1)));
        if (clicked) {
            if (t != sel) SelectTile(t);
            else s_view.pane = -1;           // back to the running tile's own pane
        }
        if (hov) ImGui::SetTooltip(t == T_CYCLE ? "Walk the playlist: looks in turn, fading through black between looks"
                                                : "Switch the wallpaper to this look");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PopID();
    }
}

// ---- CENTRE (look panes): preset strip, big knobs, freezes, Advanced
struct FrontItem { const char* heading; const char* sec; const char* key; const char* label; };
// DECISIONS 1 D4: Liquid Acid = the knobs of the current direction (black oil, one lit colour,
// filmic lens); mass colour, droplet colour and lid moved to Advanced. Fluid and Ink = section 3B
// minus mirror / peak nits (those live in the right column only). Rows must carry the look in
// their keys.inc `front` flag; any flagged row missing here is appended under MORE.
const FrontItem kFrontAcid[] = {
    { "FILM", "liquid_acid", "film_level", "Film brightness" },
    { "FILM", "*", "film_colour", "Film colour" },            // read-only: no single key (flagged)
    { "FILM", "liquid_acid", "hue_rotate_period", "Colour speed" },
    { "FILM", "liquid_acid", "film_hue2_amt", "Second colour" },
    { "FILM", "liquid_acid", "shadow_tone", "Split tone" },
    { "DROPLETS", "liquid_acid", "droplets", "Droplet amount" },
    { "MOTION", "liquid_acid", "rise_speed", "Rise speed" },
    { "LENS", "post", "camera_focus", "Focus" },
    { "LENS", "post", "dof_max_px", "Depth of field" },
    { "LENS", "post", "bloom", "Glow" },
    { "LENS", "post", "fog", "Haze" },
    { "LENS", "post", "film_grain", "Grain" },
};
const FrontItem kFrontFluid[] = {
    { "COLOUR", "color", "hue_center", "Hue band centre" },
    { "COLOUR", "color", "hue_range", "Hue band width" },
    { "COLOUR", "behavior", "color_cycle_period", "Colour speed" },
    { "COLOUR", "behavior", "hueshift_enabled", "Hue bursts" },
    { "COLOUR", "color", "post_saturation", "Saturation" },
    { "MOTION", "sim", "splat_radius", "Blob size" },
    { "MOTION", "behavior", "wanderer_count", "Emitters" },
    { "MOTION", "behavior", "show_mouse", "Mouse stirs the fluid" },
};
const FrontItem kFrontInk[] = {
    { "INK", "ink", "inverted", "Inverted (pale ink on black)" },
    { "INK", "ink", "chroma", "Chroma" },
    { "INK", "ink", "density", "Density" },
    { "INK", "ink", "pair_sweep_period", "Duotone rotation" },
    { "INK", "ink", "hdr_core", "HDR core" },
    { "DROPS", "drops", "drops", "Drops" },
    { "DROPS", "drops", "interval", "Drop interval" },
    { "DROPS", "sim", "gravity", "Gravity" },
};

struct FrontRow { std::string heading; int row; std::string label; };   // row -1 = film colour line
std::vector<FrontRow> FrontRows(unsigned look) {
    const FrontItem* items = look == LOOK_A ? kFrontAcid : look == LOOK_I ? kFrontInk : kFrontFluid;
    size_t n = look == LOOK_A ? std::size(kFrontAcid) : look == LOOK_I ? std::size(kFrontInk) : std::size(kFrontFluid);
    std::vector<FrontRow> out;
    std::vector<bool> used(UiRowCount(), false);
    for (size_t k = 0; k < n; k++) {
        if (items[k].sec[0] == '*') { out.push_back({ items[k].heading, -1, items[k].label }); continue; }
        int r = UiFindRow(items[k].sec, items[k].key);
        if (r < 0 || !(UiRow(r).front & look)) continue;
        used[r] = true;
        out.push_back({ items[k].heading, r, items[k].label });
    }
    for (int r = 0; r < UiRowCount(); r++)
        if (!used[r] && (UiRow(r).front & look) && PlaceOf(UiRow(r)) == P_CENTRE)
            out.push_back({ "MORE", r, UiRow(r).label });
    return out;
}

float HueOfRgb(const float* c) {
    float mx = (std::max)({ c[0], c[1], c[2] }), mn = (std::min)({ c[0], c[1], c[2] }), d = mx - mn;
    if (d < 1e-5f) return 0.0f;
    float h = mx == c[0] ? fmodf((c[1] - c[2]) / d, 6.0f) : mx == c[1] ? (c[2] - c[0]) / d + 2.0f : (c[0] - c[1]) / d + 4.0f;
    h *= 60.0f;
    return h < 0 ? h + 360.0f : h;
}

// Film colour: NO single key sets it (oil_color_1..4 = four RGB triples, ini-only; the palette
// sweep list overrides them; hue_rotate_period turns the result). Per D4 it is flagged to the
// user and shown read-only here, never a new key.
void DrawFilmColourLine(const std::string& label) {
    const FluidConfig& c = UiCfg();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    Badge("read-only", kDim);
    ImGui::TableSetColumnIndex(1);
    const float* oc = c.acid.oilColors;
    ImGui::ColorButton("##film", ImVec4(oc[0], oc[1], oc[2], 1), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                       ImVec2(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight()));
    ImGui::SameLine();
    float base = HueOfRgb(oc);
    if (c.acid.hueSweepPeriod > 0.01f)
        ImGui::TextColored(kDim, "from the palette sweep list");
    else if (c.acid.hueRotatePeriod > 0.01f && s_view.liveNow.valid)
        ImGui::TextColored(kDim, "base %.0f\xC2\xB0, turned %.0f\xC2\xB0 now", base, s_view.liveNow.paletteHueDeg);
    else
        ImGui::TextColored(kDim, "hue %.0f\xC2\xB0 (oil_color_1), no single key", base);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("No single key sets the film colour: it is oil_color_1..4 (four RGB values, ini only),\n"
                          "replaced by the sweep list when Palette sweep is on, then turned by Colour speed.\n"
                          "Flagged to the user (DECISIONS 1, D4) instead of inventing a key.");
}

void DrawFrontKnobs(unsigned look) {
    std::vector<FrontRow> rows = FrontRows(look);
    std::string heading;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 5));
    bool open = false;
    for (const FrontRow& fr : rows) {
        if (fr.heading != heading) {
            if (open) ImGui::EndTable();
            heading = fr.heading;
            ImGui::SeparatorText(heading.c_str());
            open = BeginRowTable(("front" + heading).c_str(), false);
        }
        if (!open) continue;
        if (fr.row < 0) { DrawFilmColourLine(fr.label); continue; }
        std::string why;
        int jump = -1;
        RowState st = UiRowState(fr.row, &why, &jump);
        DrawRow(fr.row, st, why, jump, false, fr.label.c_str());
    }
    if (open) ImGui::EndTable();
    ImGui::PopStyleVar(2);
}

// freeze buttons: hold an animator's phase (never reset it); auto-release after 15 min and
// when the window closes (animators.h)
struct FreezeItem { int anim; const char* label; };
void DrawFreezes(unsigned look, bool cyclePane) {
    const FluidConfig& c = UiCfg();
    std::vector<FreezeItem> items;
    if (cyclePane) items.push_back({ UA_TRANSITION, "stage transition" });
    else if (look == LOOK_A) {
        items.push_back({ UA_PALETTE, "colour rotation" });
        items.push_back({ UA_HUE2, "second colour swing" });
        items.push_back({ UA_RIG, "camera rig" });
    } else if (look == LOOK_I) {
        items.push_back({ UA_PALETTE, "duotone rotation" });
        items.push_back({ UA_RIG, "camera rig" });
    } else {
        items.push_back({ UA_HUE_SHIFT, "hue bursts" });
    }
    ImGui::SeparatorText("HOLD THE MOTION");
    for (size_t k = 0; k < items.size(); k++) {
        const FreezeItem& it = items[k];
        bool running = true;
        const char* idle = "";
        if (it.anim == UA_PALETTE) {
            running = look == LOOK_I ? c.ink.pairSweepPeriod > 0 : (c.acid.hueRotatePeriod > 0 || c.acid.hueSweepPeriod > 0);
            idle = look == LOOK_I ? "Duotone rotation is off" : "Colour speed and Palette sweep are off";
        } else if (it.anim == UA_HUE2) {
            running = c.acid.filmHue2Amt > 0 && fabsf(c.acid.filmHue2Wobble) > 0.001f;
            idle = "Second colour is 0 or its swing is 0";
        } else if (it.anim == UA_HUE_SHIFT) {
            running = c.hsEnabled;
            idle = "Hue bursts are off";
        } else if (it.anim == UA_TRANSITION) {
            running = UiCycleStatus().on;
            idle = "the cycle is off";
        }
        bool frozen = UiIsFrozen(it.anim);
        ImGui::PushID((int)k + 500);
        if (k) ImGui::SameLine();
        char b[128];
        if (frozen) {
            snprintf(b, sizeof(b), "Held: %s  %s  (release)", it.label, [&] {
                static char t[16];
                int s = (int)ceilf(UiFrozenLeftSec(it.anim));
                snprintf(t, sizeof(t), "%d:%02d", s / 60, s % 60);
                return t;
            }());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.42f, 1));
            if (ImGui::Button(b)) UiUnfreeze(it.anim);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Held in place; lets go by itself after 15 min or when this window closes");
        } else {
            snprintf(b, sizeof(b), "Hold %s", it.label);
            ImGui::BeginDisabled(!running);
            if (ImGui::Button(b)) UiFreeze(it.anim);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (running) ImGui::SetTooltip("Hold this motion where it is now (15 min, or until the window closes)");
                else ImGui::SetTooltip("Nothing to hold: %s", idle);
            }
        }
        ImGui::PopID();
    }
}

void DrawPresetStrip(unsigned look) {
    ULONGLONG now = GetTickCount64();
    bool rescan = s_view.live && now - s_view.lastRescan > 3000;
    if (rescan) s_view.lastRescan = now;
    const std::vector<UiPresetFile>& lib = UiLibrary(rescan);
    std::vector<const UiPresetFile*> mine;
    std::wstring running = UiSaveTarget();
    // the running preset first, even when it lives outside the presets folder (a cycle stage
    // file, an --ini config): the strip never hides what is on screen
    static UiPresetFile s_outside;
    bool runningListed = false;
    for (auto& p : lib) if (_wcsicmp(p.path.c_str(), running.c_str()) == 0) runningListed = true;
    if (!runningListed && !running.empty() && GetFileAttributesW(running.c_str()) != INVALID_FILE_ATTRIBUTES) {
        s_outside = UiClassifyPreset(running);
        mine.push_back(&s_outside);
    }
    for (auto& p : lib) if ((p.look & look) && !p.overlay) mine.push_back(&p);
    char title[128];
    snprintf(title, sizeof(title), "PRESETS  (%d for %s)", (int)mine.size(), UiLookName(look));
    ImGui::SeparatorText(title);
    if (s_view.selPath.empty() || GetFileAttributesW(s_view.selPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        s_view.selPath = running;
    float rowH = ImGui::GetFrameHeightWithSpacing();
    float h = rowH * (float)(std::min)((std::max)((int)mine.size(), 3), 7) + 10;
    if (ImGui::BeginChild("presets", ImVec2(0, h), ImGuiChildFlags_Borders)) {
        if (mine.empty()) ImGui::TextColored(kDim, "No %s presets in %s", UiLookName(look), UiNarrow(UiLibraryDir()).c_str());
        if (ImGui::BeginTable("pl", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.64f);
            ImGui::TableSetupColumn("tag", ImGuiTableColumnFlags_WidthStretch, 0.22f);
            ImGui::TableSetupColumn("cyc", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.6f);
            for (size_t k = 0; k < mine.size(); k++) {
                const UiPresetFile& p = *mine[k];
                ImGui::PushID((int)k);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                bool isRunning = _wcsicmp(p.path.c_str(), running.c_str()) == 0;
                bool sel = _wcsicmp(p.path.c_str(), s_view.selPath.c_str()) == 0;
                std::string name = (isRunning ? "\xE2\x97\x8F " : "    ") + UiNarrow(p.name);
                if (isRunning) ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                if (ImGui::Selectable(name.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                    s_view.selPath = p.path;
                    if (ImGui::IsMouseDoubleClicked(0)) { UiApplyPresetFile(p.path); s_view.status = "Applied " + UiNarrow(p.name); }
                }
                if (isRunning) ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (isRunning) ImGui::TextColored(kAccent, &p == &s_outside ? "running (outside the folder)" : "running");
                else if (!p.base.empty()) ImGui::TextColored(kDim, "on %s", UiNarrow(UiStemOf(p.base)).c_str());
                else if (p.partial) ImGui::TextColored(kDim, "partial");
                ImGui::TableSetColumnIndex(2);
                bool in = UiCycleHasFile(p.path);
                if (ImGui::Checkbox("cycle", &in)) {
                    if (in) UiCycleAddStage(p.path, p.base, p.overlay, 180.0f);
                    else UiCycleSetFileIncluded(p.path, false);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("In the cycle's playlist (settings.ini [cycle]; the preset file stays portable)");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    bool haveSel = !s_view.selPath.empty() && GetFileAttributesW(s_view.selPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    bool writable = UiLibraryWritable();
    int dirty = UiDirtyCount();
    ImGui::BeginDisabled(!haveSel);
    if (ImGui::Button("Apply")) { UiApplyPresetFile(s_view.selPath); s_view.status = "Applied " + UiNarrow(UiStemOf(s_view.selPath)); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(running.empty() || dirty == 0 || !writable);
    if (ImGui::Button("Save")) {
        std::vector<std::string> log;
        bool ok = UiSavePartial(running, &log);
        s_view.opLog.insert(s_view.opLog.end(), log.begin(), log.end());
        s_view.status = ok ? "Saved " + std::to_string(log.size()) + " changed key(s) into " + UiNarrow(UiStemOf(running)) : "Save failed";
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Write ONLY the keys you changed into %s (a partial update, never a full dump)",
                          running.empty() ? "the running preset" : UiNarrow(UiStemOf(running)).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!writable);
    if (ImGui::Button("Save as...")) s_view.openSaveAs = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!haveSel);
    if (ImGui::Button("Duplicate")) {
        std::wstring out;
        s_view.status = UiDuplicatePreset(s_view.selPath, &out) ? "Duplicated as " + UiNarrow(UiStemOf(out)) : "Duplicate failed";
        if (!out.empty()) s_view.selPath = out;
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename...")) {
        snprintf(s_view.renameBuf, sizeof(s_view.renameBuf), "%s", UiNarrow(UiStemOf(s_view.selPath)).c_str());
        s_view.openRename = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) s_view.openDelete = true;
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    // popups
    if (s_view.openSaveAs) { ImGui::OpenPopup("Save preset as"); s_view.openSaveAs = false; }
    if (ImGui::BeginPopupModal("Save preset as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("New preset name (saved in the presets folder):");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##name", s_view.saveAsName, sizeof(s_view.saveAsName), ImGuiInputTextFlags_EnterReturnsTrue);
        std::string baseName = running.empty() ? std::string("(none)") : UiNarrow(UiStemOf(running));
        std::string r1 = "Only my changes, on top of " + baseName;
        if (ImGui::RadioButton(r1.c_str(), s_view.saveAsChanges)) s_view.saveAsChanges = true;
        if (ImGui::RadioButton("Self-contained (every key that differs from the code defaults)", !s_view.saveAsChanges))
            s_view.saveAsChanges = false;
        if (ImGui::Button("Save") || enter) {
            std::vector<std::string> log;
            std::wstring out;
            if (UiSaveAsPartial(UiWide(s_view.saveAsName), s_view.saveAsChanges, &out, &log)) {
                s_view.status = "Saved as " + std::string(s_view.saveAsName);
                s_view.selPath = out;
            } else s_view.status = "Not saved (empty name, name taken, or read-only)";
            s_view.opLog.insert(s_view.opLog.end(), log.begin(), log.end());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (s_view.openRename) { ImGui::OpenPopup("Rename preset"); s_view.openRename = false; }
    if (ImGui::BeginPopupModal("Rename preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##rn", s_view.renameBuf, sizeof(s_view.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Rename") || enter) {
            std::wstring out;
            s_view.status = UiRenamePreset(s_view.selPath, UiWide(s_view.renameBuf), &out) ? "Renamed to " + std::string(s_view.renameBuf)
                                                                                        : "Rename failed (name taken?)";
            if (!out.empty()) s_view.selPath = out;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (s_view.openDelete) { ImGui::OpenPopup("Delete preset"); s_view.openDelete = false; }
    if (ImGui::BeginPopupModal("Delete preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Move \"%s\" to the Recycle Bin?", UiNarrow(UiStemOf(s_view.selPath)).c_str());
        ImGui::TextColored(kDim, "You can restore it from the Recycle Bin. It also leaves the cycle.");
        if (ImGui::Button("Move to Recycle Bin")) {
            std::vector<std::string> log;
            bool ok = UiDeletePreset(s_view.selPath, false, &log);
            s_view.opLog.insert(s_view.opLog.end(), log.begin(), log.end());
            s_view.status = ok ? "Moved to the Recycle Bin" : "Delete failed";
            if (ok) s_view.selPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

std::string MinSec(float sec) {
    int s = (int)ceilf((std::max)(sec, 0.0f));
    char b[32];
    snprintf(b, sizeof(b), "%d:%02d", s / 60, s % 60);
    return b;
}

// ---- CENTRE (Cycle pane): the playlist (D2)
struct Tier { const char* name; float w; };
const Tier kTiers[] = { { "proven", 7.0f }, { "moderate", 2.0f }, { "wild", 1.0f } };

void DrawPlaylist() {
    UiCycleStatusView cs = UiCycleStatus();
    int n = UiCycleStageCount();
    if (cs.on) {
        UiStageInfo cur = UiCycleStage(cs.stage);
        std::string line = "Now " + std::to_string(cs.stage + 1) + "/" + std::to_string(cs.count) + "  " +
                           UiNarrow(cur.name) + "  (" + (cur.overlay ? "overlay" : UiLookName(cur.look)) + ")  \xC2\xB7  ";
        if (cs.phase == 1) line += MinSec(cs.remainingSec) + " left";
        else if (cs.lerp) line += "gliding to " + UiNarrow(UiCycleStage(cs.next).name);
        else line += "fading";
        ImGui::TextColored(kAccent, "%s", line.c_str());
        if (cs.paused) {
            ImGui::TextColored(kWarn, "Paused for editing: the stage timer waits while this window is open (colours keep moving);");
            ImGui::TextColored(kWarn, "it resumes by itself after %s without input, or when you close the window.", MinSec(cs.pauseLeftSec).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Let it run")) { UiCycleResumeEditing(); s_view.noEditPause = true; }
        }
    } else {
        ImGui::TextColored(kDim, "The cycle is off. Click the Cycle tile to start it; the list below is what it will play.");
    }
    ImGui::BeginDisabled(!cs.on);
    if (ImGui::Button("\xE2\x97\x80 Previous")) UiCyclePrev();
    ImGui::SameLine();
    if (ImGui::Button("Next \xE2\x96\xB6")) UiCycleNext();
    ImGui::SameLine();
    UiStageInfo curSt = UiCycleStage(cs.stage);
    if (ImGui::Button("Edit current stage")) s_view.pane = curSt.overlay ? TileOfLook(UiCurrentLook()) : TileOfLook(curSt.look);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Open this stage's look page: knobs edit the stage live, Save writes only your changes into its file");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 24);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Order:");
    ImGui::SameLine();
    int order = UiCycleOrder();
    if (ImGui::RadioButton("alternate looks at random", order == 1) && order != 1) UiCycleSetOrder(1);
    ImGui::SameLine();
    if (ImGui::RadioButton("fixed", order == 0) && order != 0) UiCycleSetOrder(0);

    float rowH = ImGui::GetFrameHeightWithSpacing();
    float h = rowH * (float)(std::min)((std::max)(n, 3), 14) + rowH + 10;
    if (ImGui::BeginChild("playlist", ImVec2(0, h), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("stages", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            float fs = ImGui::GetFontSize();
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 2.4f);
            ImGui::TableSetupColumn("in", ImGuiTableColumnFlags_WidthFixed, fs * 1.8f);
            ImGui::TableSetupColumn("stage", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("look", ImGuiTableColumnFlags_WidthStretch, 0.16f);
            ImGui::TableSetupColumn("tier", ImGuiTableColumnFlags_WidthStretch, 0.18f);
            ImGui::TableSetupColumn("dwell", ImGuiTableColumnFlags_WidthStretch, 0.28f);
            ImGui::TableSetupColumn("order", ImGuiTableColumnFlags_WidthFixed, fs * 3.6f);
            ImGui::TableHeadersRow();
            int removeAt = -1, moveAt = -1, moveDir = 0;
            for (int i = 0; i < n; i++) {
                UiStageInfo st = UiCycleStage(i);
                bool cur = cs.on && i == cs.stage;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                if (cur) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.17f, 0.24f, 0.34f, 1)));
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                if (cur) ImGui::TextColored(kAccent, "\xE2\x96\xB6 %d", i + 1);
                else ImGui::Text("   %d", i + 1);
                ImGui::TableSetColumnIndex(1);
                bool in = true;
                if (ImGui::Checkbox("##in", &in) && !in) removeAt = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uncheck to take this stage out of the cycle");
                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                std::string nm = UiNarrow(st.name) + (st.ok ? "" : "  [missing]");
                if (cur) ImGui::TextColored(kAccent, "%s", nm.c_str());
                else if (!st.ok) ImGui::TextColored(kWarn, "%s", nm.c_str());
                else ImGui::TextUnformatted(nm.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UiNarrow(st.path).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kDim, "%s", st.overlay ? "overlay" : UiLookName(st.look));
                ImGui::TableSetColumnIndex(4);
                int tier = -1;
                for (int t = 0; t < 3; t++) if (fabsf(st.weight - kTiers[t].w) < 0.01f) tier = t;
                char tl[48];
                if (tier >= 0) snprintf(tl, sizeof(tl), "%s", kTiers[tier].name);
                else snprintf(tl, sizeof(tl), "weight %g", st.weight);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##tier", tl)) {
                    for (int t = 0; t < 3; t++) {
                        char o[48];
                        snprintf(o, sizeof(o), "%s (weight %g)", kTiers[t].name, kTiers[t].w);
                        if (ImGui::Selectable(o, t == tier) && t != tier) UiCycleSetStageWeight(i, kTiers[t].w);
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableSetColumnIndex(5);
                float dv = s_view.dwellEditRow == i ? s_view.dwellEditVal : st.dwellSec;
                ImGui::SetNextItemWidth(-FLT_MIN);
                const char* df = st.ownDwell ? "%.0f s" : "%.0f s (default)";
                if (ImGui::SliderFloat("##dwell", &dv, 30.0f, 240.0f, df)) {
                    s_view.dwellEditRow = i;
                    s_view.dwellEditVal = roundf(dv / 10.0f) * 10.0f;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && s_view.dwellEditRow == i) {
                    UiCycleSetStageDwell(i, s_view.dwellEditVal);   // one write per drag
                    s_view.dwellEditRow = -1;
                }
                ImGui::TableSetColumnIndex(6);
                ImGui::BeginDisabled(i == 0);
                if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveAt = i; moveDir = -1; }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 2);
                ImGui::BeginDisabled(i == n - 1);
                if (ImGui::ArrowButton("##dn", ImGuiDir_Down)) { moveAt = i; moveDir = 1; }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (removeAt >= 0) UiCycleRemoveStage(removeAt);
            else if (moveAt >= 0) UiCycleMoveStage(moveAt, moveDir);
        }
        if (n == 0) ImGui::TextColored(kDim, "No stages yet: add presets below, or tick \"cycle\" in a look's preset list.");
    }
    ImGui::EndChild();

    // Add stage from any preset
    const std::vector<UiPresetFile>& lib = UiLibrary(false);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Add stage:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
    std::string cur = s_view.addStageSel >= 0 && s_view.addStageSel < (int)lib.size() ? UiNarrow(lib[s_view.addStageSel].name)
                                                                                     : std::string("pick any preset...");
    if (ImGui::BeginCombo("##add", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int k = 0; k < (int)lib.size(); k++) {
            const UiPresetFile& p = lib[k];
            std::string item = UiNarrow(p.name) + "   (" + (p.overlay ? "overlay" : UiLookName(p.look)) + ")";
            if (ImGui::Selectable(item.c_str(), k == s_view.addStageSel)) s_view.addStageSel = k;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s_view.addStageSel < 0 || s_view.addStageSel >= (int)lib.size());
    if (ImGui::Button("Add to the end")) {
        const UiPresetFile& p = lib[s_view.addStageSel];
        UiCycleAddStage(p.path, p.base, p.overlay, 180.0f);
        s_view.status = "Added " + UiNarrow(p.name) + " to the cycle";
    }
    ImGui::EndDisabled();

    DrawFreezes(UiCurrentLook(), true);
    ImGui::SeparatorText("TIMING");
    if (BeginRowTable("cyc", false)) {
        for (int i = 0; i < UiRowCount(); i++) {
            const KeyRow& r = UiRow(i);
            if (PlaceOf(r) != P_CYCLE || r.key == "enabled") continue;   // enabled = the Cycle tile
            std::string why; int jump = -1;
            RowState st = UiRowState(i, &why, &jump);
            DrawRow(i, st, why, jump, false);
        }
        ImGui::EndTable();
    }
}

void DrawCentre() {
    int pane = ShownPane();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::TextUnformatted(kTileNames[pane]);
    ImGui::PopFont();
    if (pane == T_CYCLE) { DrawPlaylist(); return; }
    unsigned look = pane == T_ACID ? LOOK_A : pane == T_INK ? LOOK_I : LOOK_F;
    if (look != UiCurrentLook()) { s_view.pane = -1; pane = SelectedTile(); look = UiCurrentLook(); }
    UiCycleStatusView cs = UiCycleStatus();
    if (cs.on) {
        std::string stage = UiNarrow(UiCycleStage(cs.stage).name);
        ImGui::TextColored(kWarn, "Editing the cycle's stage %d/%d (%s): edits are live, Save writes only your changes into its file.",
                           cs.stage + 1, cs.count, stage.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Back to the playlist")) s_view.pane = -1;
    }
    DrawPresetStrip(look);
    DrawFrontKnobs(look);
    DrawFreezes(look, false);
    ImGui::Spacing();
    char adv[96];
    snprintf(adv, sizeof(adv), "Advanced: every other %s setting###adv", kTileNames[TileOfLook(look)]);
    if (ImGui::CollapsingHeader(adv)) {
        DrawAdvanced();
        if (look == LOOK_F || s_view.everything) { ImGui::Spacing(); DrawPalette(); }
    }
}

// ---- RIGHT: settings that live outside any mode (KF_GLOBAL rows) + wallpaper buttons
void DrawRight() {
    ImGui::SeparatorText("Output / HDR");
    ImGui::TextColored(kDim, "Windows HDR %s, panel max %.0f nt", AppHdrActive() ? "on" : "off (SDR)", g_maxNits);
    if (BeginRowTable("hdr", true)) {
        for (int i = 0; i < UiRowCount(); i++) {
            const KeyRow& r = UiRow(i);
            if (PlaceOf(r) != P_GLOBAL || r.sec != "hdr") continue;
            std::string why; int jump = -1;
            DrawRow(i, UiRowState(i, &why, &jump), why, jump, true);
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Mirror overlay");
    if (BeginRowTable("mir", true)) {
        for (int i = 0; i < UiRowCount(); i++) {
            const KeyRow& r = UiRow(i);
            if (PlaceOf(r) != P_GLOBAL || r.sec != "mirror") continue;
            std::string why; int jump = -1;
            DrawRow(i, UiRowState(i, &why, &jump), why, jump, true);
        }
        ImGui::EndTable();
    }
    {   // the overlay presets (Mirror - *): folds over whatever look runs, so they live here
        const std::vector<UiPresetFile>& lib = UiLibrary(false);
        int k = 0;
        for (const UiPresetFile& p : lib) {
            if (!p.overlay) continue;
            std::string nm = UiNarrow(p.name);
            size_t dash = nm.find(" - ");
            std::string chip = dash != std::string::npos ? nm.substr(dash + 3) : nm;
            size_t par = chip.find(" (overlay)");
            if (par != std::string::npos) chip.resize(par);
            ImGui::PushID(900 + k);
            if (k++ && ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(chip.c_str()).x + 30) ImGui::SameLine();
            if (ImGui::SmallButton(chip.c_str())) { UiApplyPresetFile(p.path); s_view.status = "Applied " + nm; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply the overlay preset \"%s\"", nm.c_str());
            ImGui::PopID();
        }
    }
    ImGui::SeparatorText("System");
    if (BeginRowTable("sys", true)) {
        for (int i = 0; i < UiRowCount(); i++) {
            const KeyRow& r = UiRow(i);
            if (PlaceOf(r) != P_GLOBAL || r.sec == "hdr" || r.sec == "mirror") continue;
            std::string why; int jump = -1;
            DrawRow(i, UiRowState(i, &why, &jump), why, jump, true);
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Wallpaper");
    if (ImGui::Button("HDR analyzer")) { if (s_view.live) ShowAnalyzerWindow(); }
    ImGui::SameLine();
    if (ImGui::Button("Presets folder")) {
        if (s_view.live) ShellExecuteW(nullptr, L"open", UiPresetsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Exit wallpaper")) { if (s_view.live) RequestExit(); }
}

void DrawHeader() {
    UiHeader h = UiComputeHeader();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.18f);
    ImGui::TextColored(kAccent, "%s", h.look.c_str());
    ImGui::SameLine(0, 0);
    std::string rest = h.text.substr(h.look.size());
    ImGui::TextUnformatted(rest.c_str());
    ImGui::PopFont();

    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    ImGui::BeginDisabled(!UiCanUndo());
    if (ImGui::Button("Undo") || (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && UiCanUndo())) UiUndo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!UiCanRedo());
    if (ImGui::Button("Redo") || (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y) && UiCanRedo())) UiRedo();
    ImGui::EndDisabled();
    ImGui::SameLine(0, 18);
    // Revert: the cycle's stage (in memory, its composed base) or the active preset
    const UiCycleStatusView cs = UiCycleStatus();
    std::wstring target = UiSaveTarget();
    ImGui::BeginDisabled(target.empty() || h.dirty == 0);
    if (ImGui::Button("Revert")) {
        if (cs.on) { UiCycleRevertStage(); UiRecomputeTarget(); }
        else UiApplyPresetFile(target);
        s_view.status = "Reverted to " + h.preset;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(cs.on ? "Put the current stage back to its file (in memory)" : "Re-apply the active preset (undoable)");
    ImGui::SameLine(0, 18);
    if (ImGui::Button(IsManualPaused() ? "Resume wallpaper" : "Pause wallpaper")) { if (s_view.live) TogglePause(); }
    if (cs.on && cs.paused) {
        ImGui::SameLine(0, 18);
        if (ImGui::Button("Let the cycle run")) { UiCycleResumeEditing(); s_view.noEditPause = true; }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The stage timer waits while this window is open; this lets it run on (until the window reopens)");
    }
    if (!s_view.status.empty()) {
        ImGui::SameLine(0, 18);
        ImGui::TextColored(kDim, "%s", s_view.status.c_str());
    }
}

// header strip across the top; LEFT tiles | CENTRE selected mode | RIGHT outside-any-mode (D1)
void DrawUi(float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    UiModelTick();                       // re-target when the director moved on
    s_view.liveNow = UiLive();           // ONE sample per frame (motion detection)
    DrawHeader();
    ImGui::Separator();
    float fs = ImGui::GetFontSize();
    float leftW = fs * 11.5f, rightW = fs * 21.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail - leftW - rightW < fs * 26) rightW = std::max(fs * 16.0f, avail - leftW - fs * 26);
    ImGui::BeginChild("left", ImVec2(leftW, 0), ImGuiChildFlags_None);
    DrawTiles();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("centre", ImVec2(avail - leftW - rightW - ImGui::GetStyle().ItemSpacing.x * 2, 0),
                      ImGuiChildFlags_Borders);
    DrawCentre();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, 0), ImGuiChildFlags_None);
    DrawRight();
    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================ live window
HWND                    s_wnd = nullptr;
ComPtr<ID3D11Device>    s_dev;
ComPtr<ID3D11DeviceContext> s_ctx;
ComPtr<IDXGISwapChain1> s_swap;
ComPtr<ID3D11RenderTargetView> s_rtv;
ImGuiContext*           s_imgui = nullptr;
float                   s_scale = 1.0f;
ULONGLONG               s_lastInput = 0, s_lastDraw = 0;
bool                    s_deviceLost = false;
const UINT_PTR          kTimerId = 7;

void ReleaseRtv() { s_rtv.Reset(); }
void CreateRtv() {
    ComPtr<ID3D11Texture2D> bb;
    if (s_swap && SUCCEEDED(s_swap->GetBuffer(0, IID_PPV_ARGS(&bb))))
        s_dev->CreateRenderTargetView(bb.Get(), nullptr, &s_rtv);
}

bool CreateDeviceAndSwap(HWND hwnd) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl, 2,
                                   D3D11_SDK_VERSION, &s_dev, nullptr, &s_ctx);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, fl, 2,
                               D3D11_SDK_VERSION, &s_dev, nullptr, &s_ctx);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIDevice1> dxgiDev;
    if (SUCCEEDED(s_dev.As(&dxgiDev))) dxgiDev->SetMaximumFrameLatency(1);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // not _SRGB: ImGui colours are gamma-space
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = factory->CreateSwapChainForHwnd(s_dev.Get(), hwnd, &sd, nullptr, nullptr, &s_swap);
    if (FAILED(hr)) {
        sd.Scaling = DXGI_SCALING_STRETCH;
        hr = factory->CreateSwapChainForHwnd(s_dev.Get(), hwnd, &sd, nullptr, nullptr, &s_swap);
    }
    if (FAILED(hr)) return false;
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    CreateRtv();
    return true;
}

void DestroyDevice() {
    ReleaseRtv();
    s_swap.Reset();
    s_ctx.Reset();
    s_dev.Reset();
}

void RenderLive() {
    if (!s_wnd || !s_imgui || IsIconic(s_wnd)) return;
    ImGui::SetCurrentContext(s_imgui);
    if (s_deviceLost) {
        ImGui_ImplDX11_Shutdown();
        DestroyDevice();
        ReleaseThumbs();
        if (!CreateDeviceAndSwap(s_wnd)) return;
        ImGui_ImplDX11_Init(s_dev.Get(), s_ctx.Get());
        LoadThumbs(s_dev.Get());
        s_deviceLost = false;
    }
    RECT rc;
    GetClientRect(s_wnd, &rc);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawUi((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
    ImGui::Render();
    const float clear[4] = { 0.110f, 0.114f, 0.133f, 1 };
    s_ctx->OMSetRenderTargets(1, s_rtv.GetAddressOf(), nullptr);
    s_ctx->ClearRenderTargetView(s_rtv.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = s_swap->Present(0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) s_deviceLost = true;
    s_lastDraw = GetTickCount64();
}

void SaveWindowRect() {
    if (!s_wnd || g_configReadOnly || !g_iniPath[0]) return;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(s_wnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    wchar_t b[96];
    swprintf_s(b, L"%ld %ld %ld %ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
    WritePrivateProfileStringW(L"ui", L"window", b, g_iniPath);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (s_imgui) {
        ImGui::SetCurrentContext(s_imgui);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    }
    switch (msg) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SETFOCUS: case WM_KILLFOCUS:
        s_lastInput = GetTickCount64();
        // window open => the cycle's dwell timer waits; each input re-arms the 10-minute
        // auto-resume (a forgotten window must not stop the cycle forever)
        if (!s_view.noEditPause && s_lastInput - s_view.lastPauseArm > 5000) {
            s_view.lastPauseArm = s_lastInput;
            UiCyclePauseForEditing();
        }
        break;
    case WM_TIMER:
        if (wp == kTimerId) {
            ULONGLONG now = GetTickCount64();
            // 30 Hz for a second after any input, 4 Hz otherwise (live values: fps, cycle, look)
            if (now - s_lastInput < 1200 || now - s_lastDraw >= 250) RenderLive();
        }
        return 0;
    case WM_SIZE:
        if (s_swap && wp != SIZE_MINIMIZED) {
            ReleaseRtv();
            s_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            CreateRtv();
            RenderLive();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        UINT dpi = GetDpiForWindow(hwnd);
        RECT r = { 0, 0, MulDiv(900, dpi, 96), MulDiv(640, dpi, 96) };
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, dpi);
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_DPICHANGED: {
        s_scale = HIWORD(wp) / 96.0f;
        if (s_imgui) ApplyStyle(s_scale);
        const RECT* r = (const RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SaveWindowRect();
        KillTimer(hwnd, kTimerId);
        UiCycleResumeEditing();          // closing resumes the cycle ...
        UiUnfreezeAll();                 // ... and lets every held animator go
        ReleaseThumbs();
        if (s_imgui) {
            ImGui::SetCurrentContext(s_imgui);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(s_imgui);
            s_imgui = nullptr;
        }
        DestroyDevice();
        s_wnd = nullptr;   // the wallpaper keeps running
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================ headless
struct HeadlessOpts {
    std::wstring shot, dump, search, script, chips, presetsDir, pane;
    int w = 1600, h = 1000;
};

bool WritePngRgba(const std::wstring& path, const uint8_t* rgba, UINT w, UINT h, UINT pitch) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    enc->CreateNewFrame(&fr, nullptr);
    fr->Initialize(nullptr);
    fr->SetSize(w, h);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    fr->SetPixelFormat(&pf);
    std::vector<uint8_t> bgra((size_t)w * h * 4);
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++) {
            const uint8_t* s = rgba + y * pitch + x * 4;
            uint8_t* d = &bgra[((size_t)y * w + x) * 4];
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
    fr->WritePixels(h, w * 4, (UINT)bgra.size(), bgra.data());
    fr->Commit();
    enc->Commit();
    return true;
}

std::string J(const std::string& s) {     // JSON string literal
    std::string o = "\"";
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch == '\n') o += "\\n";
        else if (ch < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", ch); o += b; }
        else o += (char)ch;
    }
    return o + "\"";
}

bool WriteDump(const std::wstring& path, const std::vector<std::string>& scriptLog) {
    UiModelTick();
    UiHeader h = UiComputeHeader();
    Counts cn = CountRows();
    std::string o = "{\n";
    o += "  \"header\": {\"look\": " + J(h.look) + ", \"preset\": " + J(h.preset) + ", \"preset_path\": " + J(h.presetPath) +
         ", \"overlays\": " + J(h.overlays) + ", \"cycle\": " + J(h.cycle) + ", \"hdr\": " + J(h.hdr) +
         ", \"mirror\": " + J(h.mirror) + ", \"dirty\": " + std::to_string(h.dirty) + ", \"text\": " + J(h.text) + "},\n";
    o += "  \"neon_in_header\": " + std::string(h.text.find("Neon") != std::string::npos ? "true" : "false") + ",\n";
    o += "  \"selected_tile\": " + std::to_string(SelectedTile()) + ",\n";
    o += "  \"counts\": {\"centre_total\": " + std::to_string(cn.total) + ", \"front\": " + std::to_string(cn.front) +
         ", \"visible\": " + std::to_string(cn.visible) + ", \"disabled\": " + std::to_string(cn.disabled) +
         ", \"hidden\": " + std::to_string(cn.hidden) + ", \"right_column\": " + std::to_string(cn.global) +
         ", \"cycle_tile\": " + std::to_string(cn.cycle) + "},\n";
    // ---- phase 1b: the pane, the cycle, the knobs, the presets, the freezes, the file ops
    s_view.liveNow = UiLive();
    {
        int pane = ShownPane();
        const char* pn[4] = { "fluid", "liquid_acid", "ink", "cycle" };
        o += "  \"pane\": " + J(pn[pane]) + ",\n";
        UiCycleStatusView cs = UiCycleStatus();
        char cb[256];
        snprintf(cb, sizeof(cb), "{\"on\": %s, \"stage\": %d, \"count\": %d, \"phase\": %d, \"remaining_s\": %.1f, \"paused\": %s, \"scripted\": %s, \"order\": %s}",
                 cs.on ? "true" : "false", cs.stage + 1, UiCycleStageCount(), cs.phase, cs.remainingSec,
                 cs.paused ? "true" : "false", cs.scripted ? "true" : "false", UiCycleOrder() == 0 ? "\"fixed\"" : "\"alternate_random\"");
        o += "  \"cycle\": " + std::string(cb) + ",\n";
        o += "  \"playlist\": [";
        for (int i = 0; i < UiCycleStageCount(); i++) {
            UiStageInfo st = UiCycleStage(i);
            const char* tier = "custom";
            for (auto& t : kTiers) if (fabsf(st.weight - t.w) < 0.01f) tier = t.name;
            char b[160];
            snprintf(b, sizeof(b), ", \"look\": \"%s\", \"tier\": \"%s\", \"weight\": %g, \"dwell_s\": %.0f, \"ok\": %s, \"current\": %s}",
                     st.overlay ? "overlay" : UiLookName(st.look), tier, st.weight, st.dwellSec, st.ok ? "true" : "false",
                     cs.on && cs.stage == i ? "true" : "false");
            o += std::string(i ? ",\n    " : "\n    ") + "{\"n\": " + std::to_string(i + 1) + ", \"name\": " + J(UiNarrow(st.name)) + b;
        }
        o += "],\n";
        unsigned look = UiCurrentLook();
        o += "  \"front_knobs\": [";
        bool first = true;
        for (const FrontRow& fr : FrontRows(look)) {
            std::string e;
            if (fr.row < 0) e = "{\"label\": " + J(fr.label) + ", \"key\": \"(none: oil_color_1..4, read-only, flagged)\"}";
            else {
                std::string why;
                RowState st = UiRowState(fr.row, &why);
                float gv = 0;
                std::string gw;
                bool ghost = GhostOf(fr.row, &gv, &gw);
                const KeyRow& r = UiRow(fr.row);
                e = "{\"label\": " + J(fr.label) + ", \"key\": " + J(r.sec + "." + r.key) + ", \"value\": " + J(UiValueText(fr.row)) +
                    ", \"state\": " + J(st == RS_VISIBLE ? "live" : st == RS_DISABLED ? "disabled: " + why : "hidden") +
                    (ghost ? ", \"ghost\": " + J(UiNumText(fr.row, gv)) + ", \"ghost_why\": " + J(gw) : std::string()) + "}";
            }
            o += std::string(first ? "\n    " : ",\n    ") + e;
            first = false;
        }
        o += "],\n";
        // every row with a ghost tick right now (front or Advanced), and every animation lock
        std::string ghosts, locks;
        for (int i = 0; i < UiRowCount(); i++) {
            float gv = 0;
            std::string gw, lw;
            if (GhostOf(i, &gv, &gw))
                ghosts += (ghosts.empty() ? "" : ", ") + std::string("{\"key\": ") + J(UiRow(i).sec + "." + UiRow(i).key) +
                          ", \"knob\": " + J(UiNumText(i, UiValue(i))) + ", \"ghost\": " + J(UiNumText(i, gv)) + "}";
            if (UiRowAnimLocked(i, &lw))
                locks += (locks.empty() ? "" : ", ") + J(UiRow(i).sec + "." + UiRow(i).key + ": " + lw);
        }
        o += "  \"ghost_ticks\": [" + ghosts + "],\n";
        o += "  \"animation_locks\": [" + locks + "],\n";
        o += "  \"freezes\": [";
        for (int a = 0; a < UA_COUNT; a++) {
            char b[160];
            snprintf(b, sizeof(b), "{\"animator\": \"%s\", \"frozen\": %s, \"auto_release_s\": %.0f}", UiAnimatorName(a).c_str(),
                     UiIsFrozen(a) ? "true" : "false", UiFrozenLeftSec(a));
            o += std::string(a ? ", " : "") + b;
        }
        o += "],\n";
        o += "  \"library_dir\": " + J(UiNarrow(UiLibraryDir())) + ",\n";
        o += "  \"presets_for_look\": [";
        first = true;
        for (const UiPresetFile& p : UiLibrary(true)) {
            if (!(p.look & look) || p.overlay) continue;
            o += std::string(first ? "" : ", ") + J(UiNarrow(p.name) + (UiCycleHasFile(p.path) ? " [in cycle]" : "") +
                                                    (p.base.empty() ? "" : " [on " + UiNarrow(UiStemOf(p.base)) + "]"));
            first = false;
        }
        o += "],\n";
        o += "  \"overlay_presets\": [";
        first = true;
        for (const UiPresetFile& p : UiLibrary(false))
            if (p.overlay) { o += std::string(first ? "" : ", ") + J(UiNarrow(p.name)); first = false; }
        o += "],\n";
        o += "  \"save_target\": " + J(UiNarrow(UiSaveTarget())) + ",\n";
        o += "  \"file_ops\": [";
        for (size_t k = 0; k < s_view.opLog.size(); k++) o += (k ? ",\n    " : "\n    ") + J(s_view.opLog[k]);
        o += "],\n";
    }
    o += "  \"script\": [";
    for (size_t k = 0; k < scriptLog.size(); k++) o += (k ? ", " : "") + J(scriptLog[k]);
    o += "],\n";
    std::string dis, hid, dirty, sentinels, front, right;
    unsigned look = UiCurrentLook();
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        Place pl = PlaceOf(r);
        if (pl == P_LOOK) continue;
        std::string why;
        RowState st = UiRowState(i, &why);
        std::string k = r.sec + "." + r.key;
        std::string vt = UiValueText(i);
        if (UiDirty(i)) dirty += (dirty.empty() ? "" : ", ") + std::string("{\"key\": ") + J(k) + ", \"value\": " + J(vt) +
                                 ", \"preset\": " + J(UiNumText(i, UiTarget(i))) + "}";
        if (r.isPeak || !r.negName.empty() || UiOutOfRange(i))
            sentinels += (sentinels.empty() ? "" : ", ") + std::string("{\"key\": ") + J(k) + ", \"raw\": " +
                         J(UiNumText(i, UiValue(i))) + ", \"shown\": " + J(vt) + ", \"out_of_range\": " +
                         (UiOutOfRange(i) ? "true" : "false") + "}";
        if (pl == P_GLOBAL) {
            right += (right.empty() ? "" : ", ") + J(k + (st == RS_DISABLED ? " (disabled: " + why + ")" : ""));
            continue;
        }
        if (pl != P_CENTRE) continue;   // lists below = the selected mode's centre rows
        if (st == RS_DISABLED) dis += (dis.empty() ? "" : ",\n    ") + std::string("{\"key\": ") + J(k) + ", \"reason\": " + J(why) + "}";
        if (st == RS_HIDDEN) hid += (hid.empty() ? "" : ",\n    ") + std::string("{\"key\": ") + J(k) + ", \"reason\": " + J(why) + "}";
        if (r.front & look) front += (front.empty() ? "" : ", ") + J(k + (st == RS_VISIBLE ? "" : " (" + std::string(st == RS_DISABLED ? "disabled" : "hidden") + ")"));
    }
    o += "  \"front\": [" + front + "],\n";
    o += "  \"right_column\": [" + right + "],\n";
    o += "  \"disabled\": [\n    " + dis + "\n  ],\n";
    o += "  \"hidden\": [\n    " + hid + "\n  ],\n";
    o += "  \"dirty_keys\": [" + dirty + "],\n";
    o += "  \"sentinels\": [" + sentinels + "],\n";
    o += "  \"pointer_exceptions\": [";
    for (size_t k = 0; k < UiPointerExceptions().size(); k++) o += (k ? ", " : "") + J(UiPointerExceptions()[k]);
    o += "],\n  \"model_errors\": [";
    for (size_t k = 0; k < UiModelErrors().size(); k++) o += (k ? ", " : "") + J(UiModelErrors()[k]);
    o += "]\n}\n";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) return false;
    fwrite(o.data(), 1, o.size(), f);
    fclose(f);
    return true;
}

std::string RunScript(const std::wstring& script, std::vector<std::string>& log) {
    // "set sec.key v; undo; redo; apply <ini>; look F|A|I"
    std::string s = UiNarrow(script);
    size_t p = 0;
    while (p <= s.size()) {
        size_t semi = s.find(';', p);
        std::string cmd = s.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        while (!cmd.empty() && cmd.front() == ' ') cmd.erase(cmd.begin());
        while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
        if (!cmd.empty()) {
            std::string res = "ok";
            if (cmd.rfind("set ", 0) == 0) {
                char key[128] = {}; float v = 0;
                if (sscanf_s(cmd.c_str() + 4, "%127s %f", key, (unsigned)sizeof(key), &v) == 2) {
                    std::string k = key;
                    size_t dot = k.find('.');
                    int row = dot == std::string::npos ? -1 : UiFindRow(k.substr(0, dot).c_str(), k.substr(dot + 1).c_str());
                    if (row < 0) res = "unknown key";
                    else { float old = UiValue(row); UiSetValue(row, v); UiPushKeyUndo(row, old, UiValue(row)); }
                } else res = "bad set";
            } else if (cmd == "undo") { if (UiCanUndo()) UiUndo(); else res = "nothing to undo"; }
            else if (cmd == "redo") { if (UiCanRedo()) UiRedo(); else res = "nothing to redo"; }
            else if (cmd.rfind("apply ", 0) == 0) {
                std::wstring path = UiWide(cmd.substr(6));
                if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) res = "file not found";
                else UiApplyPresetFile(path);
            } else if (cmd.rfind("look ", 0) == 0) {
                char L = cmd.size() > 5 ? cmd[5] : 'F';
                UiSetLook(L == 'A' ? LOOK_A : L == 'I' ? LOOK_I : LOOK_F);
            } else if (cmd.rfind("cycle ", 0) == 0 || cmd.rfind("live ", 0) == 0 ||
                       cmd.rfind("freeze ", 0) == 0 || cmd.rfind("unfreeze ", 0) == 0) {
                res = s_headlessCfg ? UiCycleScript(cmd, *s_headlessCfg) : "no config";
                UiRecomputeTarget();
                s_view.liveNow = UiLive();
            } else if (cmd.rfind("select ", 0) == 0) {          // select <preset name> (library)
                std::wstring p = UiLibraryDir() + L"\\" + UiWide(cmd.substr(7)) + L".ini";
                if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) res = "no such preset";
                else s_view.selPath = p;
            } else if (cmd.rfind("applyname ", 0) == 0) {       // apply a library preset by name
                std::wstring p = UiLibraryDir() + L"\\" + UiWide(cmd.substr(10)) + L".ini";
                if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) res = "no such preset";
                else { UiApplyPresetFile(p); s_view.selPath = p; }
            } else if (cmd == "save") {
                std::vector<std::string> l;
                if (!UiSavePartial(UiSaveTarget(), &l)) res = "save refused";
                s_view.opLog.insert(s_view.opLog.end(), l.begin(), l.end());
            } else if (cmd.rfind("saveas ", 0) == 0 || cmd.rfind("saveasfull ", 0) == 0) {
                bool full = cmd.rfind("saveasfull ", 0) == 0;
                std::vector<std::string> l;
                std::wstring out;
                if (!UiSaveAsPartial(UiWide(cmd.substr(full ? 11 : 7)), !full, &out, &l)) res = "save as refused";
                else { s_view.selPath = out; l.insert(l.begin(), "wrote " + UiNarrow(out)); }
                s_view.opLog.insert(s_view.opLog.end(), l.begin(), l.end());
            } else if (cmd.rfind("duplicate ", 0) == 0) {
                std::wstring out;
                if (!UiDuplicatePreset(UiLibraryDir() + L"\\" + UiWide(cmd.substr(10)) + L".ini", &out)) res = "duplicate failed";
                else s_view.opLog.push_back("duplicated -> " + UiNarrow(out));
            } else if (cmd.rfind("rename ", 0) == 0) {          // rename <old> => <new>
                size_t arrow = cmd.find(" => ");
                std::wstring out;
                if (arrow == std::string::npos) res = "usage: rename <old> => <new>";
                else if (!UiRenamePreset(UiLibraryDir() + L"\\" + UiWide(cmd.substr(7, arrow - 7)) + L".ini",
                                         UiWide(cmd.substr(arrow + 4)), &out)) res = "rename failed";
                else s_view.opLog.push_back("renamed -> " + UiNarrow(out));
            } else if (cmd.rfind("delete ", 0) == 0 || cmd.rfind("delete-dry ", 0) == 0) {
                bool dry = cmd.rfind("delete-dry ", 0) == 0;
                std::vector<std::string> l;
                std::wstring p = UiLibraryDir() + L"\\" + UiWide(cmd.substr(dry ? 11 : 7)) + L".ini";
                if (!UiDeletePreset(p, dry, &l)) res = "delete failed";
                s_view.opLog.insert(s_view.opLog.end(), l.begin(), l.end());
            } else if (cmd.rfind("incycle ", 0) == 0) {         // incycle <preset name> 0|1
                size_t sp = cmd.find_last_of(' ');
                std::wstring p = UiLibraryDir() + L"\\" + UiWide(cmd.substr(8, sp - 8)) + L".ini";
                if (cmd.substr(sp + 1) == "1") UiCycleAddStage(p, UiPresetBase(p), UiClassifyPreset(p).overlay, 180.0f);
                else UiCycleSetFileIncluded(p, false);
            } else if (cmd.rfind("pane ", 0) == 0) {
                std::string pn = cmd.substr(5);
                s_view.pane = pn == "cycle" ? T_CYCLE : pn == "A" ? T_ACID : pn == "I" ? T_INK : pn == "F" ? T_FLUID : -1;
            } else res = "unknown command";
            char b[64];
            snprintf(b, sizeof(b), " -> %s, dirty %d", res.c_str(), UiDirtyCount());
            log.push_back(cmd + b);
        }
        if (semi == std::string::npos) break;
        p = semi + 1;
    }
    return {};
}

} // namespace

// ============================================================================ public
void CloseSettingsWindow() {
    if (s_wnd) DestroyWindow(s_wnd);
}

void ShowSettingsWindow() {
    if (s_wnd) {
        if (IsIconic(s_wnd)) ShowWindow(s_wnd, SW_RESTORE);
        ShowWindow(s_wnd, SW_SHOW);
        SetForegroundWindow(s_wnd);
        return;
    }
    if (!g_renderer || !UiModelReady()) return;
    s_view.live = true;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"FluidWallpaperSettings2";
        RegisterClassExW(&wc);
        registered = true;
    }
    // remembered rect ([ui] window = x y w h), else 1100x820 at the primary monitor's DPI
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 0, h = 0;
    {
        wchar_t b[96] = {};
        GetPrivateProfileStringW(L"ui", L"window", L"", b, 96, g_configIniPath);
        long rx, ry, rw, rh;
        if (b[0] && swscanf_s(b, L"%ld %ld %ld %ld", &rx, &ry, &rw, &rh) == 4 && rw > 200 && rh > 200) {
            RECT test = { rx, ry, rx + rw, ry + rh };
            if (MonitorFromRect(&test, MONITOR_DEFAULTTONULL)) { x = rx; y = ry; w = rw; h = rh; }
        }
    }
    if (w == 0) {
        UINT dpi = GetDpiForSystem();
        RECT r = { 0, 0, MulDiv(1360, dpi, 96), MulDiv(860, dpi, 96) };
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, dpi);
        w = r.right - r.left; h = r.bottom - r.top;
    }
    s_wnd = CreateWindowExW(WS_EX_APPWINDOW, L"FluidWallpaperSettings2", L"Fluid Wallpaper \u2014 Settings",
                            WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!s_wnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(s_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (!CreateDeviceAndSwap(s_wnd)) {
        DestroyWindow(s_wnd);
        return;
    }
    IMGUI_CHECKVERSION();
    s_imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(s_imgui);
    s_scale = GetDpiForWindow(s_wnd) / 96.0f;
    LoadFonts();
    ApplyStyle(s_scale);
    ImGui_ImplWin32_Init(s_wnd);
    ImGui_ImplDX11_Init(s_dev.Get(), s_ctx.Get());
    LoadThumbs(s_dev.Get());
    s_view.noEditPause = false;
    s_view.lastPauseArm = GetTickCount64();
    UiCyclePauseForEditing();            // D3: window open => the dwell timer pauses
    SetTimer(s_wnd, kTimerId, 33, nullptr);
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
    s_lastInput = GetTickCount64();
    RenderLive();
}

// --ui-shot <png> / --ui-dump <json> [--ui-search s] [--ui-script "..."]
// [--ui-chips everything,changed,alllooks] [--ui-size WxH]. main.cpp has set g_configReadOnly and
// loaded cfg from --ini; nothing here writes an ini, the registry or %APPDATA%.
int UiRunHeadless(FluidConfig& cfg, int argc, wchar_t** argv) {
    HeadlessOpts o;
    for (int i = 1; i < argc; i++) {
        auto next = [&](std::wstring& dst) { if (i + 1 < argc) dst = argv[++i]; };
        if (!wcscmp(argv[i], L"--ui-shot")) next(o.shot);
        else if (!wcscmp(argv[i], L"--ui-dump")) next(o.dump);
        else if (!wcscmp(argv[i], L"--ui-search")) next(o.search);
        else if (!wcscmp(argv[i], L"--ui-script")) next(o.script);
        else if (!wcscmp(argv[i], L"--ui-chips")) next(o.chips);
        else if (!wcscmp(argv[i], L"--ui-presets-dir")) next(o.presetsDir);
        else if (!wcscmp(argv[i], L"--ui-pane")) next(o.pane);
        else if (!wcscmp(argv[i], L"--ui-size") && i + 1 < argc) swscanf_s(argv[++i], L"%dx%d", &o.w, &o.h);
    }
    UiModelInit(cfg);
    if (!UiModelErrors().empty()) {
        for (auto& e : UiModelErrors()) fprintf(stderr, "[ui] %s\n", e.c_str());
    }
    // the [cycle] list of the --ini file (the live app loads it at boot); presets folder override
    UiCycleLoadHeadless(g_configIniPath);
    if (!o.presetsDir.empty()) UiSetLibraryDirOverride(o.presetsDir);
    UiRecomputeTarget();
    s_view = View();
    s_view.live = false;
    s_headlessCfg = &cfg;
    std::vector<std::string> log;
    if (!o.script.empty()) RunScript(o.script, log);
    for (auto& l : log) printf("[ui-script] %s\n", l.c_str());
    for (auto& l : s_view.opLog) printf("[ui-op] %s\n", l.c_str());
    if (!o.pane.empty()) {
        std::string pn = UiNarrow(o.pane);
        s_view.pane = pn == "cycle" ? T_CYCLE : pn == "A" ? T_ACID : pn == "I" ? T_INK : pn == "F" ? T_FLUID : -1;
    }
    snprintf(s_view.search, sizeof(s_view.search), "%s", UiNarrow(o.search).c_str());
    if (o.chips.find(L"everything") != std::wstring::npos) s_view.everything = true;
    if (o.chips.find(L"changed") != std::wstring::npos) s_view.changed = true;
    if (o.chips.find(L"alllooks") != std::wstring::npos) s_view.thisLook = false;

    int rc = 0;
    if (!o.dump.empty()) {
        if (WriteDump(o.dump, log)) printf("[ui] dump: %ls\n", o.dump.c_str());
        else { fprintf(stderr, "[ui] cannot write %ls\n", o.dump.c_str()); rc = 2; }
    }
    if (!o.shot.empty()) {
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
            fprintf(stderr, "[ui] WARP device failed\n");
            return 3;
        }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = o.w; td.Height = o.h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> rt, staging;
        dev->CreateTexture2D(&td, nullptr, &rt);
        ComPtr<ID3D11RenderTargetView> rtv;
        dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        dev->CreateTexture2D(&td, nullptr, &staging);

        ImGuiContext* ctxIm = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctxIm);
        LoadFonts();
        ApplyStyle(1.0f);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)o.w, (float)o.h);
        io.DeltaTime = 1.0f / 30.0f;
        ImGui_ImplDX11_Init(dev.Get(), ctx.Get());
        LoadThumbs(dev.Get());
        for (int f = 0; f < 4; f++) {      // a few frames so layout / tab selection settle
            io.DeltaTime = 1.0f / 30.0f;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            DrawUi((float)o.w, (float)o.h);
            ImGui::Render();
            const float clear[4] = { 0.110f, 0.114f, 0.133f, 1 };
            ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
            ctx->ClearRenderTargetView(rtv.Get(), clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        ctx->CopyResource(staging.Get(), rt.Get());
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
            bool ok = WritePngRgba(o.shot, (const uint8_t*)m.pData, o.w, o.h, m.RowPitch);
            ctx->Unmap(staging.Get(), 0);
            if (ok) printf("[ui] shot: %ls (%dx%d, WARP)\n", o.shot.c_str(), o.w, o.h);
            else { fprintf(stderr, "[ui] cannot write %ls\n", o.shot.c_str()); rc = 2; }
        }
        ReleaseThumbs();
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(ctxIm);
    }
    return UiModelErrors().empty() ? rc : 4;
}
