// UI model: key table + the one per-key write path + reset target / dirty / gates / undo.
// See ui_model.h. No renderer calls here: UiHooks carries them (null when headless).

#include <windows.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "ui_model.h"
#include "../app_state.h"
#include "ui_cycle.h"

#pragma comment(lib, "advapi32.lib")

namespace {

FluidConfig*  s_cfg = nullptr;
UiHooks       s_hooks;
std::vector<KeyRow>   s_rows;
std::vector<GateNode> s_nodes;
std::vector<std::string> s_ptrExceptions, s_errors;
const FluidConfig s_defaults{};      // code defaults, same offsets as the live config
FluidConfig   s_target{};            // reset target = defaults + active preset file
float         s_peakTarget = -1.0f;
int           s_gamutTarget = 2;
std::wstring  s_activePreset;        // full path ([ui] active_preset), empty = none
std::vector<std::wstring> s_overlays;// applied files with no [look] section (names)
bool          s_autostart = false;

struct MeasuredInert { const char* preset; const char* sec; const char* key; const char* why; };
const MeasuredInert kMeasuredInert[] = {
#define MEASURED_INERT(p, s, k, w) { p, s, k, w },
#include "measured_inert.inc"
#undef MEASURED_INERT
};

// ---- ini + registry --------------------------------------------------------
void WriteIniFloat(const std::wstring& sec, const std::wstring& key, float v, int dec) {
    if (!g_iniPath[0] || g_configReadOnly) return;
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f", dec, v);
    WritePrivateProfileStringW(sec.c_str(), key.c_str(), buf, g_iniPath);
}
void WriteIniInt(const std::wstring& sec, const std::wstring& key, int v) {
    if (!g_iniPath[0] || g_configReadOnly) return;
    wchar_t buf[32];
    swprintf_s(buf, L"%d", v);
    WritePrivateProfileStringW(sec.c_str(), key.c_str(), buf, g_iniPath);
}
bool GetAutostart() {
    wchar_t path[MAX_PATH];
    DWORD sz = sizeof(path);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"FluidWallpaper", RRF_RT_REG_SZ, nullptr, path, &sz) == ERROR_SUCCESS;
}
void SetAutostart(bool on) {
    if (g_configReadOnly) return;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 4];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        swprintf_s(cmd, L"\"%s\"", exe);
        RegSetValueExW(key, L"FluidWallpaper", 0, REG_SZ,
                       (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"FluidWallpaper");
    }
    RegCloseKey(key);
    printf("autostart: %s\n", on ? "enabled" : "disabled");
}

std::wstring Stem(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    std::wstring n = (s == std::wstring::npos) ? path : path.substr(s + 1);
    if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".ini") == 0) n.resize(n.size() - 4);
    return n;
}

unsigned ParseLooks(const char* s) {
    unsigned m = 0;
    for (; s && *s; s++) {
        if (*s == 'F') m |= LOOK_F;
        else if (*s == 'A') m |= LOOK_A;
        else if (*s == 'I') m |= LOOK_I;
    }
    return m;
}

std::string Trim(const char* s, int* indent) {
    int n = 0;
    while (s[n] == ' ') n++;
    if (indent) *indent = n > 0 ? 1 : 0;
    std::string t = s + n;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

void ParseSpecial(KeyRow& r) {
    const std::string& sp = r.special;
    if (sp.rfind("enum:", 0) == 0) {
        std::string rest = sp.substr(5);
        size_t p = 0;
        while (p <= rest.size()) {
            size_t bar = rest.find('|', p);
            std::string item = rest.substr(p, bar == std::string::npos ? std::string::npos : bar - p);
            size_t eq = item.find('=');
            if (eq != std::string::npos)
                r.enums.push_back({ atoi(item.substr(0, eq).c_str()), item.substr(eq + 1) });
            if (bar == std::string::npos) break;
            p = bar + 1;
        }
    } else if (sp == "peak") {
        r.isPeak = true;
    } else if (sp.rfind("neg:", 0) == 0) {
        r.negName = sp.substr(4);
    } else if (sp.rfind("zero:", 0) == 0) {
        r.zeroName = sp.substr(5);
    } else if (sp == "look") {
        r.isLook = true;
    } else if (sp == "autostart") {
        r.isAutostart = true;
    }
}

// pointer -> offset inside FluidConfig, or an explicitly listed global (pre-flight item 1)
void ResolvePointer(KeyRow& r, FluidConfig& c) {
    const void* p = r.f ? (const void*)r.f : r.i ? (const void*)r.i : (const void*)r.b;
    const char* lo = reinterpret_cast<const char*>(&c);
    const char* hi = reinterpret_cast<const char*>(&c + 1);
    const char* pc = reinterpret_cast<const char*>(p);
    if (p && pc >= lo && pc < hi) { r.off = pc - lo; return; }
    r.off = -1;
    const void* allowed[] = {
        &g_hdrPeakNits, &g_gamutMode, &g_pauseOnFullscreen, &g_pauseOnMaximized,
    };
    bool ok = UiCycleIsPtr(p);
    for (const void* a : allowed) if (a == p) ok = true;
    if (!p && r.isAutostart) ok = true;
    std::string name = r.sec + "." + r.key;
    if (ok) s_ptrExceptions.push_back(name + (p ? " (global)" : " (registry)"));
    else    s_errors.push_back("pointer outside FluidConfig and not a listed global: " + name);
}

void AddRow(KeyRow r, const char* looks, const char* gate, const char* front, FluidConfig& c) {
    r.looks = ParseLooks(looks);
    r.front = ParseLooks(front);
    r.gateSrc = gate ? gate : "";
    r.wsec = UiWide(r.sec);
    r.wkey = UiWide(r.key);
    ParseSpecial(r);
    ResolvePointer(r, c);
    s_rows.push_back(std::move(r));
}

void AddSlider(const char* label, float mn, float mx, float step, int dec, float* f, int* i,
               const char* sec, const char* key, bool reinit, const char* tip, KeyGroup g,
               const char* looks, const char* gate, unsigned flags, const char* special,
               const char* front, FluidConfig& c) {
    KeyRow r;
    r.label = Trim(label, &r.indent);
    r.mn = mn; r.mx = mx; r.step = step; r.dec = dec; r.f = f; r.i = i;
    r.sec = sec; r.key = key; r.reinit = reinit; r.tip = tip; r.group = g; r.flags = flags;
    r.special = special;
    AddRow(std::move(r), looks, gate, front, c);
}
void AddCheck(const char* label, bool* b, const char* sec, const char* key, const char* tip,
              KeyGroup g, const char* looks, const char* gate, unsigned flags, const char* special,
              const char* front, FluidConfig& c) {
    KeyRow r;
    r.isCheck = true;
    r.label = Trim(label, &r.indent);
    r.mn = 0; r.mx = 1; r.step = 1; r.b = b;
    r.sec = sec; r.key = key; r.tip = tip; r.group = g; r.flags = flags; r.special = special;
    AddRow(std::move(r), looks, gate, front, c);
}

void BuildRows(FluidConfig& c) {
    s_rows.clear();
    s_ptrExceptions.clear();
    s_errors.clear();
#define KEY_SLIDER(lbl, mn, mx, st, dec, fp, ip, sec, key, re, tip, grp, looks, gate, flags, special, front) \
    AddSlider(lbl, (float)(mn), (float)(mx), (float)(st), dec, fp, ip, sec, key, re, tip, grp, looks, gate, \
              (unsigned)(flags), special, front, c);
#define KEY_CHECK(lbl, bp, sec, key, tip, grp, looks, gate, flags, special, front) \
    AddCheck(lbl, bp, sec, key, tip, grp, looks, gate, (unsigned)(flags), special, front, c);
#include "keys.inc"
#undef KEY_SLIDER
#undef KEY_CHECK
}

// ---- gate expressions --------------------------------------------------------
struct GateParser {
    const char* s; size_t p = 0; std::string err;
    void ws() { while (s[p] == ' ') p++; }
    bool eat(const char* t) {
        ws();
        size_t n = strlen(t);
        if (strncmp(s + p, t, n) == 0) { p += n; return true; }
        return false;
    }
    int node(GateNode n) { s_nodes.push_back(n); return (int)s_nodes.size() - 1; }
    int expr() {
        int a = term();
        while (a >= 0 && eat("||")) { int b = term(); if (b < 0) return -1; GateNode n; n.kind = GateNode::OR; n.a = a; n.b = b; a = node(n); }
        return a;
    }
    int term() {
        int a = factor();
        while (a >= 0 && eat("&&")) { int b = factor(); if (b < 0) return -1; GateNode n; n.kind = GateNode::AND; n.a = a; n.b = b; a = node(n); }
        return a;
    }
    int factor() {
        if (eat("(")) { int a = expr(); if (!eat(")")) { err = "missing )"; return -1; } return a; }
        ws();
        std::string id;
        while (isalnum((unsigned char)s[p]) || s[p] == '_' || s[p] == '.') id += s[p++];
        if (id.empty()) { err = "expected key"; return -1; }
        int op = -1;
        if (eat("==")) op = 0; else if (eat("!=")) op = 1; else if (eat(">=")) op = 4;
        else if (eat("<=")) op = 5; else if (eat(">")) op = 2; else if (eat("<")) op = 3;
        if (op < 0) { err = "expected operator after " + id; return -1; }
        ws();
        GateNode n;
        if (id == "look") {
            if (op != 0) { err = "look only supports =="; return -1; }
            char L = s[p++];
            n.kind = GateNode::LOOKIS;
            n.look = L == 'F' ? LOOK_F : L == 'A' ? LOOK_A : L == 'I' ? LOOK_I : 0;
            if (!n.look) { err = "look==F|A|I"; return -1; }
            return node(n);
        }
        size_t dot = id.find('.');
        if (dot == std::string::npos) { err = "expected sec.key: " + id; return -1; }
        int row = UiFindRow(id.substr(0, dot).c_str(), id.substr(dot + 1).c_str());
        if (row < 0) { err = "unknown key " + id; return -1; }
        char* end = nullptr;
        n.num = strtof(s + p, &end);
        if (end == s + p) { err = "expected number"; return -1; }
        p = end - s;
        n.kind = GateNode::CMP; n.row = row; n.op = op;
        return node(n);
    }
};

bool EvalGate(int n) {
    const GateNode& g = s_nodes[n];
    switch (g.kind) {
    case GateNode::AND: return EvalGate(g.a) && EvalGate(g.b);
    case GateNode::OR:  return EvalGate(g.a) || EvalGate(g.b);
    case GateNode::LOOKIS: return UiCurrentLook() == g.look;
    case GateNode::CMP: {
        float v = UiValue(g.row);
        switch (g.op) {
        case 0: return fabsf(v - g.num) < 1e-6f;
        case 1: return fabsf(v - g.num) >= 1e-6f;
        case 2: return v > g.num;
        case 3: return v < g.num;
        case 4: return v >= g.num;
        default: return v <= g.num;
        }
    }
    }
    return true;
}

std::string ShortLabel(const KeyRow& r) {
    std::string l = r.label;
    size_t par = l.find(" (");
    if (par != std::string::npos && par > 3) l.resize(par);
    if (!l.empty() && l[0] >= 'a' && l[0] <= 'z') l[0] = (char)(l[0] - 'a' + 'A');
    return l;
}

std::string GateReason(int n, int* jump) {
    const GateNode& g = s_nodes[n];
    switch (g.kind) {
    case GateNode::AND: return !EvalGate(g.a) ? GateReason(g.a, jump) : GateReason(g.b, jump);
    case GateNode::OR: {
        std::string a = GateReason(g.a, jump);
        int dummy;
        std::string b = GateReason(g.b, &dummy);
        if (b.rfind("needs ", 0) == 0) b = b.substr(6);
        return a + " or " + b;
    }
    case GateNode::LOOKIS:
        return std::string("needs the ") + UiLookName(g.look) + " look";
    case GateNode::CMP: {
        const KeyRow& r = s_rows[g.row];
        if (jump && *jump < 0) *jump = g.row;
        std::string t = "needs " + ShortLabel(r);
        if (r.isCheck) {
            bool wantOn = (g.op == 2 && g.num < 1) || (g.op == 1 && g.num == 0) || (g.op == 0 && g.num >= 1);
            return t + (wantOn ? " on" : " off");
        }
        if (!r.enums.empty() && g.op == 0) {
            for (auto& e : r.enums) if (e.first == (int)g.num) return t + " = " + e.second;
        }
        static const char* ops[] = { " = ", " != ", " > ", " < ", " >= ", " <= " };
        return t + ops[g.op] + UiNumText(g.row, g.num);
    }
    }
    return "";
}

float ReadAt(const FluidConfig& cfg, const KeyRow& r) {
    const char* base = reinterpret_cast<const char*>(&cfg) + r.off;
    if (r.f) return *reinterpret_cast<const float*>(base);
    if (r.i) return (float)*reinterpret_cast<const int*>(base);
    return *reinterpret_cast<const bool*>(base) ? 1.0f : 0.0f;
}

void RecomputeTarget() {
    s_target = FluidConfig{};
    s_peakTarget = -1.0f;
    s_gamutTarget = 2;
    if (!s_activePreset.empty() && GetFileAttributesW(s_activePreset.c_str()) != INVALID_FILE_ATTRIBUTES) {
        LoadConfigFromFile(s_activePreset.c_str(), s_target);
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(L"hdr", L"peak_nits", L"", buf, 64, s_activePreset.c_str());
        if (buf[0]) s_peakTarget = (float)_wtof(buf);
        int gm = (int)GetPrivateProfileIntW(L"hdr", L"gamut", 2, s_activePreset.c_str());
        if (gm >= 0 && gm <= 2) s_gamutTarget = gm;
    }
}

std::wstring JoinOverlays() {
    std::wstring s;
    for (auto& o : s_overlays) { if (!s.empty()) s += L";"; s += o; }
    return s;
}

void PersistUiSection() {
    if (g_configReadOnly || !g_iniPath[0]) return;
    WritePrivateProfileStringW(L"ui", L"active_preset", s_activePreset.c_str(), g_iniPath);
    WritePrivateProfileStringW(L"ui", L"overlays", JoinOverlays().c_str(), g_iniPath);
}

// ---- undo --------------------------------------------------------------------
struct Snapshot {
    std::shared_ptr<FluidConfig> cfg;
    float peak = -1; int gamut = 2;
    std::wstring preset; std::vector<std::wstring> overlays;
};
struct UndoEntry {
    bool whole = false;
    int row = -1; float oldV = 0, newV = 0;
    Snapshot before, after;
};
std::vector<UndoEntry> s_undo;
size_t s_undoPos = 0;            // entries [0, pos) are undoable
Snapshot s_pendingBefore;
bool s_pending = false;

Snapshot TakeSnapshot() {
    Snapshot s;
    s.cfg = std::make_shared<FluidConfig>(*s_cfg);
    s.peak = g_hdrPeakNits; s.gamut = g_gamutMode;
    s.preset = s_activePreset; s.overlays = s_overlays;
    return s;
}

void PushUndo(UndoEntry e) {
    s_undo.resize(s_undoPos);
    s_undo.push_back(std::move(e));
    if (s_undo.size() > 200) s_undo.erase(s_undo.begin());
    s_undoPos = s_undo.size();
}

void RestoreSnapshot(const Snapshot& s) {
    FluidConfig& c = *s_cfg;
    int oldSim = c.simRes, oldDye = c.dyeRes;
    c = *s.cfg;
    g_hdrPeakNits = s.peak;
    g_gamutMode = s.gamut;
    if ((c.simRes != oldSim || c.dyeRes != oldDye) && s_hooks.setResolutions)
        s_hooks.setResolutions(c.simRes, c.dyeRes);
    else if (s_hooks.reinitWanderers)
        s_hooks.reinitWanderers();
    if (s_hooks.ensureLook) s_hooks.ensureLook();
    if (!g_configReadOnly && g_iniPath[0]) {
        WriteConfigToIni(g_iniPath, c, true);
        PersistShellSettings();
    }
    s_activePreset = s.preset;
    s_overlays = s.overlays;
    PersistUiSection();
    RecomputeTarget();
}

} // namespace

// ============================================================================
std::string UiNarrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring UiWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

void UiModelInit(FluidConfig& cfg) {
    if (s_cfg == &cfg && !s_rows.empty()) return;
    s_cfg = &cfg;
    s_nodes.clear();
    BuildRows(cfg);
    for (KeyRow& r : s_rows) {
        if (r.gateSrc.empty()) continue;
        GateParser gp{ r.gateSrc.c_str() };
        int root = gp.expr();
        gp.ws();
        if (root >= 0 && gp.s[gp.p] != 0) { root = -1; gp.err = "trailing text"; }
        if (root < 0) s_errors.push_back("gate of " + r.sec + "." + r.key + ": " + gp.err + " in \"" + r.gateSrc + "\"");
        r.gate = root;
    }
    s_autostart = GetAutostart();
    RecomputeTarget();
    for (auto& e : s_errors) printf("[ui] keymeta error: %s\n", e.c_str());
}

bool UiModelReady() { return s_cfg != nullptr; }
FluidConfig& UiCfg() { return *s_cfg; }
void UiSetHooks(const UiHooks& h) { s_hooks = h; }
const UiHooks& UiGetHooks() { return s_hooks; }
int UiRowCount() { return (int)s_rows.size(); }
KeyRow& UiRow(int i) { return s_rows[i]; }
const std::vector<std::string>& UiPointerExceptions() { return s_ptrExceptions; }
const std::vector<std::string>& UiModelErrors() { return s_errors; }

int UiFindRow(const char* sec, const char* key) {
    for (int i = 0; i < (int)s_rows.size(); i++)
        if (s_rows[i].sec == sec && s_rows[i].key == key) return i;
    return -1;
}

unsigned UiCurrentLook() {
    if (!s_cfg) return LOOK_F;
    if (s_cfg->ink.enabled) return LOOK_I;
    if (s_cfg->acid.enabled) return LOOK_A;
    return LOOK_F;
}
const char* UiLookName(unsigned look) {
    return look == LOOK_I ? "Ink" : look == LOOK_A ? "Liquid Acid" : "Fluid (WE)";
}

float UiValue(int i) {
    const KeyRow& r = s_rows[i];
    if (r.isAutostart) return s_autostart ? 1.0f : 0.0f;
    if (r.f) return *r.f;
    if (r.i) return (float)*r.i;
    if (r.b) return *r.b ? 1.0f : 0.0f;
    return 0.0f;
}

float UiTarget(int i) {
    const KeyRow& r = s_rows[i];
    if (r.off >= 0) return ReadAt(s_target, r);
    if (r.isPeak) return s_peakTarget;
    if (r.i == &g_gamutMode) return (float)s_gamutTarget;
    return UiValue(i);   // machine / shell globals: never part of a preset
}

float UiDefault(int i) {
    const KeyRow& r = s_rows[i];
    if (r.off >= 0) return ReadAt(s_defaults, r);
    if (r.isPeak) return -1.0f;
    if (r.i == &g_gamutMode) return 2.0f;
    const void* p = r.f ? (const void*)r.f : r.i ? (const void*)r.i : (const void*)r.b;
    if (UiCycleIsPtr(p)) return UiCycleDefault(p);
    if (r.b == &g_pauseOnFullscreen || r.b == &g_pauseOnMaximized) return 1.0f;
    return 0.0f;
}

bool UiCountsForDirty(int i) {
    const KeyRow& r = s_rows[i];
    if (r.flags & (KF_MACHINE | KF_SHELL)) return false;
    return r.off >= 0 || r.isPeak;
}

static bool Differs(const KeyRow& r, float a, float b) {
    if (r.f && !r.isCheck) return fabsf(a - b) > r.step * 0.5f;
    return (int)lroundf(a) != (int)lroundf(b);
}

bool UiDirty(int i) {
    if (!UiCountsForDirty(i)) return false;
    return Differs(s_rows[i], UiValue(i), UiTarget(i));
}

int UiDirtyCount() {
    int n = 0;
    for (int i = 0; i < (int)s_rows.size(); i++) if (UiDirty(i)) n++;
    return n;
}

bool UiOutOfRange(int i) {
    const KeyRow& r = s_rows[i];
    if (r.isCheck || !r.enums.empty()) return false;
    float v = UiValue(i);
    if (r.isPeak && v < 0) return false;             // auto: a named sentinel, not a range fault
    if (!r.negName.empty() && v < 0) return false;   // inherit: named sentinel
    return v < r.mn - r.step * 0.01f || v > r.mx + r.step * 0.01f;
}

std::string UiNumText(int i, float v) {
    const KeyRow& r = s_rows[i];
    char buf[64];
    if (r.i || r.dec <= 0) snprintf(buf, sizeof(buf), "%.0f", v);
    else snprintf(buf, sizeof(buf), "%.*f", r.dec, v);
    return buf;
}

std::string UiValueText(int i) {
    const KeyRow& r = s_rows[i];
    float v = UiValue(i);
    char buf[96];
    if (r.isCheck) return v > 0.5f ? "on" : "off";
    if (r.isPeak) {
        if (v < 0) { snprintf(buf, sizeof(buf), "auto (%.0f nt)", g_maxNits); return buf; }
        if (v == 0) return "off (match SDR)";
        snprintf(buf, sizeof(buf), "%.0f nt", v);
        return buf;
    }
    if (!r.enums.empty()) {
        for (auto& e : r.enums) if (e.first == (int)lroundf(v)) return e.second;
    }
    if (!r.negName.empty() && v < 0) return r.negName;
    if (!r.zeroName.empty() && v == 0) return r.zeroName;
    return UiNumText(i, v);
}

RowState UiRowState(int i, std::string* reason, int* jumpRow) {
    const KeyRow& r = s_rows[i];
    unsigned look = UiCurrentLook();
    int jump = -1;
    std::string why;
    RowState st = RS_VISIBLE;
    if (!(r.looks & look)) {
        why = std::string("not read by the ") + UiLookName(look) + " look (";
        bool first = true;
        for (unsigned l : { (unsigned)LOOK_F, (unsigned)LOOK_A, (unsigned)LOOK_I })
            if (r.looks & l) { why += (first ? "" : ", "); why += UiLookName(l); first = false; }
        why += " only)";
        st = RS_HIDDEN;
    } else if (r.flags & KF_SHELL) {
        why = "read-only here (AGENTS.md: sim_res/dye_res live at 256/4096)";
        st = RS_DISABLED;
    } else if (r.flags & KF_SUPERSEDED) {
        why = "superseded by Droplet dye hue / saturation / brightness (KEY PASS)";
        st = RS_DISABLED;
    } else if (r.gate >= 0 && !EvalGate(r.gate)) {
        why = GateReason(r.gate, &jump);
        st = RS_DISABLED;
    } else if (!s_activePreset.empty()) {
        std::string stem = UiNarrow(Stem(s_activePreset));
        for (const MeasuredInert& m : kMeasuredInert) {
            if (_stricmp(m.preset, stem.c_str()) == 0 && r.sec == m.sec && r.key == m.key) {
                why = std::string("no effect on ") + m.preset + " (" + m.why + ")";
                st = RS_DISABLED;
                break;
            }
        }
    }
    if (reason) *reason = why;
    if (jumpRow) *jumpRow = jump;
    return st;
}

void UiSetValue(int i, float v) {
    KeyRow& r = s_rows[i];
    if (r.flags & KF_SHELL) return;
    FluidConfig& c = *s_cfg;
    if (r.isLook) {
        // Look switches are live and mutually exclusive (DisplayPso() would otherwise just
        // prefer acid); turning one ON may need its display PSO compiled first.
        bool on = v > 0.5f;
        *r.b = on;
        WriteIniInt(r.wsec, r.wkey, on ? 1 : 0);
        if (on) {
            if (r.b == &c.acid.enabled) { c.ink.enabled = false; WriteIniInt(L"look", L"ink", 0); }
            else                        { c.acid.enabled = false; WriteIniInt(L"look", L"liquid_acid", 0); }
        }
        if (s_hooks.ensureLook) s_hooks.ensureLook();
        return;
    }
    if (r.isAutostart) {
        s_autostart = v > 0.5f;
        SetAutostart(s_autostart);
        return;
    }
    if (r.b) {
        bool on = v > 0.5f;
        if (r.b == UiCycleEnabledPtr()) {
            UiCycleSetEnabled(on);   // persists + toggles the renderer's coverage readback
        } else {
            *r.b = on;
            WriteIniInt(r.wsec, r.wkey, on ? 1 : 0);
        }
        return;
    }
    if (r.i) {
        *r.i = (int)lroundf(v);
        if (r.reinit && s_hooks.reinitWanderers) s_hooks.reinitWanderers();
        WriteIniInt(r.wsec, r.wkey, *r.i);
        return;
    }
    if (r.f) {
        *r.f = v;
        if (r.reinit && s_hooks.reinitWanderers) s_hooks.reinitWanderers();
        WriteIniFloat(r.wsec, r.wkey, v, r.dec);
    }
}

void UiPushKeyUndo(int row, float oldV, float newV) {
    if (oldV == newV) return;
    UndoEntry e;
    e.row = row; e.oldV = oldV; e.newV = newV;
    PushUndo(std::move(e));
}

bool UiCanUndo() { return s_undoPos > 0; }
bool UiCanRedo() { return s_undoPos < s_undo.size(); }

void UiUndo() {
    if (!UiCanUndo()) return;
    UndoEntry& e = s_undo[--s_undoPos];
    if (e.whole) RestoreSnapshot(e.before);
    else UiSetValue(e.row, e.oldV);
}
void UiRedo() {
    if (!UiCanRedo()) return;
    UndoEntry& e = s_undo[s_undoPos++];
    if (e.whole) RestoreSnapshot(e.after);
    else UiSetValue(e.row, e.newV);
}

void UiBeginWholeChange() {
    if (!s_cfg) return;
    s_pendingBefore = TakeSnapshot();
    s_pending = true;
}
void UiEndWholeChange(const std::wstring& path) {
    if (!s_cfg) return;
    UiNotifyPresetApplied(path);
    if (s_pending) {
        UndoEntry e;
        e.whole = true;
        e.before = s_pendingBefore;
        e.after = TakeSnapshot();
        PushUndo(std::move(e));
        s_pending = false;
    }
}

bool UiFileHasLookSection(const std::wstring& path) {
    wchar_t buf[256] = {};
    DWORD n = GetPrivateProfileSectionW(L"look", buf, 256, path.c_str());
    return n > 0;
}

void UiNotifyPresetApplied(const std::wstring& path) {
    if (UiFileHasLookSection(path)) {
        s_activePreset = path;
        s_overlays.clear();
    } else {
        // overlay: one per family ("Mirror - quad" replaces "Mirror - off")
        std::wstring name = Stem(path);
        std::wstring fam = name.substr(0, name.find(L" - "));
        for (size_t k = 0; k < s_overlays.size();) {
            if (s_overlays[k].substr(0, s_overlays[k].find(L" - ")) == fam) s_overlays.erase(s_overlays.begin() + k);
            else k++;
        }
        s_overlays.push_back(name);
    }
    PersistUiSection();
    RecomputeTarget();
}

void UiNotifyPresetSaved(const std::wstring& path) {
    s_activePreset = path;
    PersistUiSection();
    RecomputeTarget();
}

void UiSetActivePresetPath(const std::wstring& path) {
    s_activePreset = path;
    RecomputeTarget();
}

void UiLoadActivePreset() {
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"ui", L"active_preset", L"", buf, MAX_PATH, g_configIniPath);
    s_activePreset = buf;
    if (s_activePreset.empty()) {
        // before [ui] existed, the last applied preset is the persisted base mood
        wchar_t bm[128] = {};
        GetPrivateProfileStringW(L"moods", L"base_mood", L"", bm, 128, g_configIniPath);
        if (bm[0]) {
            std::wstring p = UiPresetsDir() + L"\\" + bm + L".ini";
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) s_activePreset = p;
        }
    }
    wchar_t ov[1024] = {};
    GetPrivateProfileStringW(L"ui", L"overlays", L"", ov, 1024, g_configIniPath);
    s_overlays.clear();
    std::wstring o = ov;
    size_t p = 0;
    while (p < o.size()) {
        size_t semi = o.find(L';', p);
        std::wstring item = o.substr(p, semi == std::wstring::npos ? std::wstring::npos : semi - p);
        if (!item.empty()) s_overlays.push_back(item);
        if (semi == std::wstring::npos) break;
        p = semi + 1;
    }
    RecomputeTarget();
}

const std::wstring& UiActivePresetPath() { return s_activePreset; }

bool UiSaveActivePreset() {
    if (g_configReadOnly || s_activePreset.empty() || !UiFileHasLookSection(s_activePreset)) return false;
    WriteConfigToIni(s_activePreset.c_str(), *s_cfg, false);
    UiPresetsRescan();
    RecomputeTarget();
    printf("[ui] saved live config into %ls\n", s_activePreset.c_str());
    return true;
}

bool UiSavePresetAs(const std::wstring& nameIn, std::wstring* outPath) {
    if (g_configReadOnly) return false;
    std::wstring name;
    for (wchar_t ch : nameIn) if (!wcschr(L"\\/:*?\"<>|", ch)) name += ch;
    while (!name.empty() && name.back() == L' ') name.pop_back();
    if (name.empty()) return false;
    std::wstring dir = UiPresetsDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + name + L".ini";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;   // never overwrite silently
    WriteConfigToIni(path.c_str(), *s_cfg, false);
    UiPresetsRescan();
    UiNotifyPresetSaved(path);
    if (outPath) *outPath = path;
    return true;
}

void UiApplyPresetHeadless(const std::wstring& path) {
    // the same merge main.cpp ApplyPreset does, minus the renderer
    UiBeginWholeChange();
    FluidConfig fresh = *s_cfg;
    LoadConfigFromFile(path.c_str(), fresh);
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"hdr", L"peak_nits", L"", buf, 64, path.c_str());
    if (buf[0]) g_hdrPeakNits = (float)_wtof(buf);
    int gm = (int)GetPrivateProfileIntW(L"hdr", L"gamut", g_gamutMode, path.c_str());
    if (gm >= 0 && gm <= 2) g_gamutMode = gm;
    *s_cfg = fresh;
    if (s_hooks.ensureLook) s_hooks.ensureLook();
    UiEndWholeChange(path);
}

bool UiAutostartCached() { return s_autostart; }

void UiSetLook(unsigned look) {
    if (!s_cfg || look == UiCurrentLook()) return;
    Snapshot before = TakeSnapshot();
    int ra = UiFindRow("look", "liquid_acid"), ri = UiFindRow("look", "ink");
    if (ra < 0 || ri < 0) return;
    if (look == LOOK_A) UiSetValue(ra, 1.0f);
    else if (look == LOOK_I) UiSetValue(ri, 1.0f);
    else { UiSetValue(ra, 0.0f); UiSetValue(ri, 0.0f); }
    UndoEntry e;
    e.whole = true;
    e.before = before;
    e.after = TakeSnapshot();
    PushUndo(std::move(e));
}

UiHeader UiComputeHeader() {
    UiHeader h;
    h.look = UiLookName(UiCurrentLook());
    h.presetPath = UiNarrow(s_activePreset);
    if (s_activePreset.empty()) h.preset = "no preset";
    else {
        h.preset = UiNarrow(Stem(s_activePreset));
        if (GetFileAttributesW(s_activePreset.c_str()) == INVALID_FILE_ATTRIBUTES) h.preset += " (file missing)";
    }
    for (auto& o : s_overlays) { if (!h.overlays.empty()) h.overlays += ", "; h.overlays += UiNarrow(o); }
    // No mood / stage NAME in 1a: the conductor's name was the "Neon" lie (it named a mood
    // even with cycling off) and the conductor is being replaced; the stage name comes from
    // animators.h CycleState() in 1b.
    h.cycle = UiCycleOn() ? "Cycle on" : "Cycle off";
    static const char* gam[] = { "sRGB", "Display-P3", "BT.2020" };
    const char* gname = (g_gamutMode >= 0 && g_gamutMode <= 2) ? gam[g_gamutMode] : "?";
    char b[128];
    if (AppHdrActive()) {
        if (g_hdrPeakNits < 0) snprintf(b, sizeof(b), "HDR on, peak auto %.0f nt, %s", g_maxNits, gname);
        else if (g_hdrPeakNits == 0) snprintf(b, sizeof(b), "HDR on, peak off, %s", gname);
        else snprintf(b, sizeof(b), "HDR on, peak %.0f nt, %s", g_hdrPeakNits, gname);
    } else {
        snprintf(b, sizeof(b), "HDR off (SDR)");
    }
    h.hdr = b;
    int mm = s_cfg ? s_cfg->mirror.mode : 0;
    static const char* mir[] = { "Mirror off", "Mirror horizontal", "Mirror vertical", "Mirror quad", "Mirror kaleidoscope" };
    h.mirror = (mm >= 0 && mm <= 4) ? mir[mm] : "Mirror ?";
    h.dirty = UiDirtyCount();
    char d[48];
    if (h.dirty == 0) snprintf(d, sizeof(d), "no changes");
    else snprintf(d, sizeof(d), "%d change%s", h.dirty, h.dirty == 1 ? "" : "s");
    const char* sep = " \xC2\xB7 ";   // middle dot, UTF-8
    h.text = h.look + sep + h.preset;
    if (!h.overlays.empty()) h.text += " + " + h.overlays;
    h.text += sep + std::string(d) + sep + h.hdr + sep + h.mirror + sep + h.cycle;
    if (g_currentFps > 0.5f) {
        char f[32];
        snprintf(f, sizeof(f), "%.0f fps", g_currentFps);
        h.text += sep + std::string(f);
    }
    return h;
}
