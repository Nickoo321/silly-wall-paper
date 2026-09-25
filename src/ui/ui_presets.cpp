// Preset library: scan / classify / apply / partial save / duplicate / rename / Recycle-Bin
// delete. See ui_presets.h for the rules.
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "ui_presets.h"
#include "ui_model.h"
#include "ui_cycle.h"
#include "../app_state.h"

#pragma comment(lib, "shell32.lib")

namespace {
std::wstring s_dirOverride;               // headless test folder (--ui-presets-dir)
std::vector<UiPresetFile> s_lib;
bool s_scanned = false;

bool Exists(const std::wstring& p) { return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring FolderOf(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : path.substr(0, s);
}

std::wstring FullPath(const std::wstring& p) {
    wchar_t buf[MAX_PATH];
    return GetFullPathNameW(p.c_str(), MAX_PATH, buf, nullptr) ? std::wstring(buf) : p;
}

// every write goes through here: never while read-only, except inside the headless test folder
bool MayWrite(const std::wstring& path) {
    if (!s_dirOverride.empty()) {
        std::wstring dir = FullPath(s_dirOverride), full = FullPath(path);
        return full.size() > dir.size() && _wcsnicmp(full.c_str(), dir.c_str(), dir.size()) == 0 &&
               (full[dir.size()] == L'\\' || full[dir.size()] == L'/');
    }
    return !g_configReadOnly;
}

std::wstring Sanitize(const std::wstring& in) {
    std::wstring n;
    for (wchar_t ch : in) if (!wcschr(L"\\/:*?\"<>|", ch) && ch >= 32) n += ch;
    while (!n.empty() && (n.back() == L' ' || n.back() == L'.')) n.pop_back();
    while (!n.empty() && n.front() == L' ') n.erase(n.begin());
    return n;
}

const wchar_t* StyleOf(unsigned look) {
    return look == LOOK_I ? L"ink" : look == LOOK_A ? L"liquid_acid" : L"fluid";
}

bool HasSection(const std::vector<std::wstring>& secs, const wchar_t* name) {
    for (auto& s : secs) if (_wcsicmp(s.c_str(), name) == 0) return true;
    return false;
}

std::vector<std::wstring> SectionNames(const std::wstring& path) {
    std::vector<std::wstring> out;
    std::vector<wchar_t> buf(8192);
    DWORD n = GetPrivateProfileSectionNamesW(buf.data(), (DWORD)buf.size(), path.c_str());
    for (DWORD p = 0; p < n && buf[p];) {
        std::wstring s = &buf[p];
        out.push_back(s);
        p += (DWORD)s.size() + 1;
    }
    return out;
}

std::string N(const std::wstring& w) { return UiNarrow(w); }

void WriteKey(const std::wstring& path, const std::wstring& sec, const std::wstring& key,
              const std::wstring& val, std::vector<std::string>* log, const std::string& note = "") {
    WritePrivateProfileStringW(sec.c_str(), key.c_str(), val.c_str(), path.c_str());
    if (log) log->push_back("[" + N(sec) + "] " + N(key) + " = " + N(val) + note);
}

// start a new ini with an ASCII comment so WritePrivateProfileString keeps it 8-bit text
bool CreateWithComment(const std::wstring& path, const std::string& comment) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) return false;
    fputs(comment.c_str(), f);
    fclose(f);
    return true;
}

void WriteSplatColours(const std::wstring& path, const FluidConfig& ref, std::vector<std::string>* log) {
    const FluidConfig& c = UiCfg();
    for (int k = 0; k < 5; k++) {
        const float* a = c.splatColors + k * 3;
        const float* b = ref.splatColors + k * 3;
        if (fabsf(a[0] - b[0]) < 1e-4f && fabsf(a[1] - b[1]) < 1e-4f && fabsf(a[2] - b[2]) < 1e-4f) continue;
        wchar_t key[32], val[64];
        swprintf_s(key, L"splat_color_%d", k + 1);
        swprintf_s(val, L"%.4f %.4f %.4f", a[0], a[1], a[2]);
        WriteKey(path, L"color", key, val, log);
    }
}

unsigned LookOfConfig(const FluidConfig& c) {
    return c.ink.enabled ? LOOK_I : c.acid.enabled ? LOOK_A : LOOK_F;
}
} // namespace

std::wstring UiStemOf(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    std::wstring n = (s == std::wstring::npos) ? path : path.substr(s + 1);
    if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".ini") == 0) n.resize(n.size() - 4);
    return n;
}

std::wstring UiLibraryDir() { return s_dirOverride.empty() ? UiPresetsDir() : FullPath(s_dirOverride); }
void UiSetLibraryDirOverride(const std::wstring& dir) { s_dirOverride = dir; s_scanned = false; }
bool UiLibraryWritable() { return !s_dirOverride.empty() || !g_configReadOnly; }

std::wstring UiPresetBase(const std::wstring& path) {
    wchar_t b[MAX_PATH] = {};
    GetPrivateProfileStringW(L"meta", L"base", L"", b, MAX_PATH, path.c_str());
    if (!b[0]) return L"";
    std::wstring base = b;
    bool absolute = base.size() > 2 && (base[1] == L':' || (base[0] == L'\\' && base[1] == L'\\'));
    return FullPath(absolute ? base : FolderOf(path) + L"\\" + base);
}

UiPresetFile UiClassifyPreset(const std::wstring& path) {
    UiPresetFile f;
    f.path = path;
    f.name = UiStemOf(path);
    f.base = UiPresetBase(path);
    wchar_t sec[512] = {};
    f.hasLookSection = GetPrivateProfileSectionW(L"look", sec, 512, path.c_str()) > 0;
    wchar_t meta[32] = {};
    GetPrivateProfileStringW(L"meta", L"look", L"", meta, 32, path.c_str());
    if (meta[0]) {
        f.look = !_wcsicmp(meta, L"ink") ? LOOK_I : !_wcsicmp(meta, L"liquid_acid") ? LOOK_A
               : !_wcsicmp(meta, L"fluid") ? LOOK_F : (LOOK_F | LOOK_A | LOOK_I);
        f.overlay = f.look == (LOOK_F | LOOK_A | LOOK_I);
        f.partial = !f.hasLookSection;
        return f;
    }
    if (f.hasLookSection) {   // the tray's rule (main.cpp PresetLookGroup)
        wchar_t style[32] = {};
        GetPrivateProfileStringW(L"look", L"style", L"", style, 32, path.c_str());
        bool acid = _wcsicmp(style, L"liquid_acid") == 0, ink = _wcsicmp(style, L"ink") == 0;
        acid = GetPrivateProfileIntW(L"look", L"liquid_acid", acid ? 1 : 0, path.c_str()) != 0;
        ink = GetPrivateProfileIntW(L"look", L"ink", ink ? 1 : 0, path.c_str()) != 0;
        f.look = ink ? LOOK_I : acid ? LOOK_A : LOOK_F;
        return f;
    }
    // no [look]: a partial overlay; infer the look from the sections it carries
    f.partial = true;
    std::vector<std::wstring> secs = SectionNames(path);
    bool mirrorOnly = HasSection(secs, L"mirror");
    for (auto& s : secs)
        if (_wcsicmp(s.c_str(), L"mirror") && _wcsicmp(s.c_str(), L"meta")) mirrorOnly = false;
    if (mirrorOnly) { f.overlay = true; f.look = LOOK_F | LOOK_A | LOOK_I; }
    else if (HasSection(secs, L"liquid_acid")) f.look = LOOK_A;
    else if (HasSection(secs, L"ink")) f.look = LOOK_I;
    else if (HasSection(secs, L"sim") || HasSection(secs, L"color") || HasSection(secs, L"behavior") ||
             HasSection(secs, L"journey")) f.look = LOOK_F;
    else f.look = LOOK_F | LOOK_A | LOOK_I;
    return f;
}

const std::vector<UiPresetFile>& UiLibrary(bool rescan) {
    if (s_scanned && !rescan) return s_lib;
    s_scanned = true;
    s_lib.clear();
    std::wstring dir = UiLibraryDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.ini").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return s_lib;
    std::vector<std::wstring> names;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    for (auto& n : names) s_lib.push_back(UiClassifyPreset(dir + L"\\" + n));
    return s_lib;
}

void UiApplyPresetFile(const std::wstring& path) {
    std::wstring base = UiPresetBase(path);
    const UiHooks& h = UiGetHooks();
    // [meta] base first (a partial "Save as" sits on it), then the file itself
    if (!base.empty() && Exists(base)) {
        if (h.applyPreset) h.applyPreset(base); else UiApplyPresetHeadless(base);
    }
    if (h.applyPreset) h.applyPreset(path); else UiApplyPresetHeadless(path);
}

bool UiSavePartial(const std::wstring& target, std::vector<std::string>* log) {
    if (target.empty() || !Exists(target) || !MayWrite(target)) {
        if (log) log->push_back("refused: " + N(target) + (MayWrite(target) ? " (missing)" : " (read-only)"));
        return false;
    }
    const FluidConfig& base = UiComposedBase();
    int n = 0;
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        if (r.isLook || !UiDirty(i)) continue;          // dirty = differs from the composed base
        WriteKey(target, r.wsec, r.wkey, UiIniText(i, UiValue(i)), log,
                 "   (base " + UiNumText(i, UiTarget(i)) + ")");
        n++;
    }
    if (LookOfConfig(UiCfg()) != LookOfConfig(base)) {
        WriteKey(target, L"look", L"style", StyleOf(LookOfConfig(UiCfg())), log);
        WritePrivateProfileStringW(L"look", L"ink", nullptr, target.c_str());
        WritePrivateProfileStringW(L"look", L"liquid_acid", nullptr, target.c_str());
        n++;
    }
    WriteSplatColours(target, base, log);
    UiRecomputeTarget();                                 // the file now IS the base: dirty 0
    UiLibrary(true);
    printf("[ui] saved %d changed key(s) into %ls (partial)\n", n, target.c_str());
    return true;
}

bool UiSaveAsPartial(const std::wstring& nameIn, bool onlyChanges, std::wstring* outPath,
                     std::vector<std::string>* log) {
    std::wstring name = Sanitize(nameIn);
    if (name.empty()) return false;
    std::wstring dir = UiLibraryDir();
    std::wstring path = dir + L"\\" + name + L".ini";
    if (Exists(path) || !MayWrite(path)) {
        if (log) log->push_back("refused: " + N(path) + (Exists(path) ? " (name taken)" : " (read-only)"));
        return false;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring basePreset = onlyChanges ? UiSaveTarget() : L"";
    if (onlyChanges && (basePreset.empty() || !Exists(basePreset))) {
        onlyChanges = false;                             // nothing to sit on: self-contained
        if (log) log->push_back("no base preset: writing a self-contained partial instead");
    }
    const unsigned look = LookOfConfig(UiCfg());
    std::string comment = "; " + N(name) + ".ini -- saved by the Settings window.\r\n";
    if (onlyChanges)
        comment += "; PARTIAL overlay: only the keys that differ from [meta] base (applied first).\r\n";
    else
        comment += "; PARTIAL overlay: every key that differs from the code defaults (self-contained).\r\n";
    if (!CreateWithComment(path, comment)) return false;
    WriteKey(path, L"meta", L"look", StyleOf(look), log);
    if (onlyChanges) {
        // same folder -> the bare file name (the pair stays portable), else absolute
        std::wstring b = FullPath(basePreset);
        std::wstring rel = _wcsicmp(FolderOf(b).c_str(), FullPath(dir).c_str()) == 0 ? b.substr(b.find_last_of(L"\\/") + 1) : b;
        WriteKey(path, L"meta", L"base", rel, log);
    }
    WriteKey(path, L"look", L"style", StyleOf(look), log);
    static const FluidConfig kDefaults{};
    int n = 0;
    for (int i = 0; i < UiRowCount(); i++) {
        const KeyRow& r = UiRow(i);
        if (r.isLook || !UiCountsForDirty(i) || UiRowAnimLocked(i, nullptr)) continue;
        float v = UiValue(i);
        bool write = onlyChanges ? UiDirty(i)
                                 : (r.isPeak ? v != -1.0f
                                             : (r.f && !r.isCheck ? fabsf(v - UiDefault(i)) > r.step * 0.5f
                                                                  : lroundf(v) != lroundf(UiDefault(i))));
        if (!write) continue;
        WriteKey(path, r.wsec, r.wkey, UiIniText(i, v), log);
        n++;
    }
    WriteSplatColours(path, onlyChanges ? UiComposedBase() : kDefaults, log);
    UiNotifyPresetSaved(path);                           // the new file is now the active preset
    UiLibrary(true);
    if (outPath) *outPath = path;
    printf("[ui] saved as %ls: %d key(s), %s\n", path.c_str(), n, onlyChanges ? "changes only" : "self-contained");
    return true;
}

bool UiDuplicatePreset(const std::wstring& path, std::wstring* outPath) {
    if (!Exists(path)) return false;
    std::wstring dir = FolderOf(path), stem = UiStemOf(path), dst;
    for (int k = 1; k < 100; k++) {
        dst = dir + L"\\" + stem + (k == 1 ? L" copy" : L" copy " + std::to_wstring(k)) + L".ini";
        if (!Exists(dst)) break;
    }
    if (Exists(dst) || !MayWrite(dst)) return false;
    if (!CopyFileW(path.c_str(), dst.c_str(), TRUE)) return false;
    UiLibrary(true);
    if (outPath) *outPath = dst;
    return true;
}

bool UiRenamePreset(const std::wstring& path, const std::wstring& newName, std::wstring* outPath) {
    std::wstring name = Sanitize(newName);
    if (name.empty() || !Exists(path)) return false;
    std::wstring dst = FolderOf(path) + L"\\" + name + L".ini";
    if (Exists(dst) || !MayWrite(path) || !MayWrite(dst)) return false;
    if (!MoveFileW(path.c_str(), dst.c_str())) return false;
    // keep the references: the active preset, and every cycle stage that names the file
    if (_wcsicmp(UiActivePresetPath().c_str(), path.c_str()) == 0) UiNotifyPresetSaved(dst);
    UiCycleReplaceFile(path, dst);
    UiLibrary(true);
    if (outPath) *outPath = dst;
    return true;
}

bool UiDeletePreset(const std::wstring& path, bool dryRun, std::vector<std::string>* log) {
    if (!Exists(path)) { if (log) log->push_back("no such file: " + N(path)); return false; }
    // SHFileOperation needs a double-NUL-terminated list
    std::wstring from = FullPath(path);
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;   // Recycle Bin
    char flags[96];
    snprintf(flags, sizeof(flags), "FO_DELETE, fFlags=0x%04X (FOF_ALLOWUNDO|NOCONFIRMATION|SILENT|NOERRORUI)",
             (unsigned)op.fFlags);
    if (log) log->push_back(std::string(dryRun ? "dry run: would call" : "calling") + " SHFileOperationW(" +
                            flags + ") on " + N(FullPath(path)));
    if (dryRun) return true;
    if (!MayWrite(path)) { if (log) log->push_back("refused: read-only"); return false; }
    int rc = SHFileOperationW(&op);
    bool ok = rc == 0 && !op.fAnyOperationsAborted && !Exists(path);
    if (log) {
        char b[128];
        snprintf(b, sizeof(b), "SHFileOperationW returned %d, aborted=%d, file still there=%d -> %s",
                 rc, op.fAnyOperationsAborted ? 1 : 0, Exists(path) ? 1 : 0, ok ? "in the Recycle Bin" : "FAILED");
        log->push_back(b);
    }
    if (ok && UiCycleHasFile(path)) UiCycleSetFileIncluded(path, false);
    UiLibrary(true);
    return ok;
}
