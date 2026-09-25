// Settings window (UI-REHAUL phase 1a): a Dear ImGui "DX11 island".
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
};
View s_view;

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

bool RowMatchesSearch(const KeyRow& r, const char* q) {
    if (!q[0]) return true;
    return IContains(r.label, q) || IContains(r.key, q) || IContains(r.sec + "." + r.key, q) ||
           IContains(r.tip, q);
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

// ---------------------------------------------------------------------------- one row
void DrawRow(int i, RowState st, const std::string& reason, int jump, bool compact) {
    KeyRow& r = UiRow(i);
    ImGui::PushID(i);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    bool locked = st != RS_VISIBLE && !s_view.everything;
    bool dirty = UiDirty(i);
    if (r.indent) ImGui::Indent(ImGui::GetFontSize() * 0.9f);
    ImGui::AlignTextToFramePadding();
    if (dirty) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x - 7, p.y + h * 0.5f), 3.2f,
                                                    ImGui::GetColorU32(kChanged));
    }
    if (st != RS_VISIBLE) ImGui::TextDisabled("%s", r.label.c_str());
    else ImGui::TextUnformatted(r.label.c_str());
    if (r.flags & KF_MOTION) Badge("motion", kAccent);
    if (r.flags & KF_UNVERIFIED) Badge("?", kWarn);
    if (UiOutOfRange(i)) Badge("outside slider range", kWarn);
    if (!reason.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, st == RS_HIDDEN ? kDim : kWarn);
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
    if (r.indent) ImGui::Unindent(ImGui::GetFontSize() * 0.9f);

    ImGui::TableSetColumnIndex(1);
    ImGui::BeginDisabled(locked);
    float v = UiValue(i);
    if (r.isCheck) {
        bool b = v > 0.5f;
        if (ImGui::Checkbox("##v", &b)) SetWithUndo(i, b ? 1.0f : 0.0f);
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
    if (r.sec == "moods") return P_CYCLE;
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
    int shown = 0;
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        if (PlaceOf(r) != P_CENTRE) continue;
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
    if (shown == 0) ImGui::TextColored(kDim, "Nothing matches. Try \"Show everything\".");
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

// ---- LEFT: the mode tiles, one radio group = "what the wallpaper is doing" (D2)
enum Tile { T_FLUID, T_ACID, T_INK, T_CYCLE };
int SelectedTile() {
    if (UiCycleOn()) return T_CYCLE;
    unsigned l = UiCurrentLook();
    return l == LOOK_A ? T_ACID : l == LOOK_I ? T_INK : T_FLUID;
}

void SelectTile(int t) {
    int ce = UiFindRow("moods", "enabled");
    if (t == T_CYCLE) {
        if (ce >= 0 && UiValue(ce) < 0.5f) SetWithUndo(ce, 1.0f);
        return;
    }
    if (ce >= 0 && UiValue(ce) > 0.5f) SetWithUndo(ce, 0.0f);
    UiSetLook(t == T_ACID ? LOOK_A : t == T_INK ? LOOK_I : LOOK_F);
}

void DrawTiles() {
    static const char* names[4] = { "Fluid (WE)", "Liquid Acid", "Ink", "Cycle" };
    static const char* subs[4] = { "native fluid sim", "oil on inked water", "ink in water", "presets in turn" };
    int sel = SelectedTile();
    float w = ImGui::GetContentRegionAvail().x;
    float thumbH = w * 9.0f / 16.0f;
    float tileH = thumbH + ImGui::GetTextLineHeightWithSpacing() * 2 + 14;
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
        // static thumbnail slot (headless --shot renders land here in phase 1b/3)
        ImVec2 a(p.x + 7, p.y + 7), b(p.x + w - 7, p.y + 7 + thumbH - 7);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.09f, 1)), 5);
        dl->AddText(ImVec2(a.x + 8, b.y - ImGui::GetTextLineHeight() - 6), ImGui::GetColorU32(kDim), "thumbnail");
        float ty = p.y + thumbH + 6;
        dl->AddText(ImVec2(p.x + 10, ty), ImGui::GetColorU32(t == sel ? kAccent : ImVec4(0.88f, 0.89f, 0.92f, 1)), names[t]);
        dl->AddText(ImVec2(p.x + 10, ty + ImGui::GetTextLineHeightWithSpacing()), ImGui::GetColorU32(kDim), subs[t]);
        if (t == sel) {   // running dot
            float r = 5;
            dl->AddCircleFilled(ImVec2(p.x + w - 14, ty + ImGui::GetTextLineHeight() * 0.5f), r,
                                ImGui::GetColorU32(ImVec4(0.35f, 0.85f, 0.45f, 1)));
        }
        if (clicked && t != sel) SelectTile(t);
        if (hov) ImGui::SetTooltip(t == T_CYCLE ? "Cycle through the presets in the cycle (fluid presets only today)"
                                                : "Switch the wallpaper to this look");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PopID();
    }
}

// ---- CENTRE: the selected tile. 1a = the Advanced expander only (presets + knobs are 1b)
void DrawCentre() {
    int sel = SelectedTile();
    static const char* names[4] = { "Fluid (WE)", "Liquid Acid", "Ink", "Cycle" };
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::TextUnformatted(names[sel]);
    ImGui::PopFont();
    if (sel == T_CYCLE) {
        ImGui::TextColored(kDim, "The playlist (stages, order, in-cycle checks) arrives with the cycle director (phase 1b).");
        if (BeginRowTable("cyc", false)) {
            for (int i = 0; i < UiRowCount(); i++) {
                if (PlaceOf(UiRow(i)) != P_CYCLE) continue;
                std::string why; int jump = -1;
                RowState st = UiRowState(i, &why, &jump);
                DrawRow(i, st, why, jump, false);
            }
            ImGui::EndTable();
        }
        return;
    }
    ImGui::TextColored(kDim, "Presets and the big knobs for this look arrive in phase 1b; everything is under Advanced.");
    ImGui::Spacing();
    char adv[96];
    snprintf(adv, sizeof(adv), "Advanced: every %s setting###adv", names[sel]);
    if (ImGui::CollapsingHeader(adv, ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawAdvanced();
        if (sel == T_FLUID || s_view.everything) { ImGui::Spacing(); DrawPalette(); }
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
    bool canSave = !UiActivePresetPath().empty() && UiFileHasLookSection(UiActivePresetPath()) && !g_configReadOnly;
    ImGui::BeginDisabled(!canSave || h.dirty == 0);
    if (ImGui::Button("Save") || (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && canSave && h.dirty > 0))
        s_view.status = UiSaveActivePreset() ? "Saved into " + h.preset : "Save failed";
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Overwrite the active preset file with the live settings");
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) s_view.openSaveAs = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(UiActivePresetPath().empty() || h.dirty == 0);
    if (ImGui::Button("Revert")) {
        std::wstring p = UiActivePresetPath();
        if (s_view.live && UiGetHooks().applyPreset) UiGetHooks().applyPreset(p);
        else UiApplyPresetHeadless(p);
        s_view.status = "Reverted to " + h.preset;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Re-apply the active preset (undoable)");
    ImGui::SameLine(0, 18);
    if (ImGui::Button(IsManualPaused() ? "Resume" : "Pause")) { if (s_view.live) TogglePause(); }
    if (!s_view.status.empty()) {
        ImGui::SameLine(0, 18);
        ImGui::TextColored(kDim, "%s", s_view.status.c_str());
    }

    if (s_view.openSaveAs) { ImGui::OpenPopup("Save preset as"); s_view.openSaveAs = false; }
    if (ImGui::BeginPopupModal("Save preset as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("New preset name (saved in the presets folder):");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##name", s_view.saveAsName, sizeof(s_view.saveAsName),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Save") || enter) {
            if (UiSavePresetAs(UiWide(s_view.saveAsName), nullptr)) s_view.status = "Saved as " + std::string(s_view.saveAsName);
            else s_view.status = "Not saved (empty name, name taken, or read-only)";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// header strip across the top; LEFT tiles | CENTRE selected mode | RIGHT outside-any-mode (D1)
void DrawUi(float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
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
        if (!CreateDeviceAndSwap(s_wnd)) return;
        ImGui_ImplDX11_Init(s_dev.Get(), s_ctx.Get());
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
    std::wstring shot, dump, search, script, chips;
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
                else UiApplyPresetHeadless(path);
            } else if (cmd.rfind("look ", 0) == 0) {
                char L = cmd.size() > 5 ? cmd[5] : 'F';
                UiSetLook(L == 'A' ? LOOK_A : L == 'I' ? LOOK_I : LOOK_F);
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
        else if (!wcscmp(argv[i], L"--ui-size") && i + 1 < argc) swscanf_s(argv[++i], L"%dx%d", &o.w, &o.h);
    }
    UiModelInit(cfg);
    if (!UiModelErrors().empty()) {
        for (auto& e : UiModelErrors()) fprintf(stderr, "[ui] %s\n", e.c_str());
    }
    std::vector<std::string> log;
    if (!o.script.empty()) RunScript(o.script, log);
    for (auto& l : log) printf("[ui-script] %s\n", l.c_str());

    s_view = View();
    s_view.live = false;
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
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(ctxIm);
    }
    return UiModelErrors().empty() ? rc : 4;
}
