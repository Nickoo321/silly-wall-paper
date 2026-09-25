// Preset library for the settings window (UI-REHAUL phase 1b): the per-look preset strip.
//
// A preset's look tag (UI-ANIMATORS-MODEL section 3): [meta] look=fluid|liquid_acid|ink|any,
// else its [look] keys (style= string, int forms override, ink wins -- the tray's rule),
// else inferred from the sections it carries (a Scheme file with only [liquid_acid] is an acid
// partial; a file with only [mirror] is an overlay = "any"). [ui] is never read from a preset.
//
// Save / Save as write PARTIAL overlays (never a full WriteConfigToIni dump):
//   Save     -> into the active preset (or the cycle's current stage file): ONLY the keys whose
//               live value differs from the composed base, updated in place.
//   Save as  -> a new file: [meta] look= + base=<the base preset's file name> + [look] style=
//               + only the changed keys. Apply / Add-to-cycle resolve [meta] base first.
// Delete goes to the Recycle Bin (SHFileOperation FOF_ALLOWUNDO), never a hard delete.
// Writes are refused while the config is read-only (--shot / --ui-shot) EXCEPT inside an
// explicit headless test folder (--ui-presets-dir), so a proof run can exercise them without
// ever touching %APPDATA%.
#pragma once
#include <string>
#include <vector>

struct UiPresetFile {
    std::wstring path, name;       // absolute, stem
    unsigned look = 0;             // LOOK_F|LOOK_A|LOOK_I bits; 7 = any (overlay)
    bool overlay = false;          // no [look] and only display keys (Mirror - *)
    bool partial = false;          // no [look] section: merges over whatever runs
    bool hasLookSection = false;
    std::wstring base;             // [meta] base= (resolved absolute), empty = none
};

std::wstring UiLibraryDir();                       // the presets folder (or the headless test dir)
void UiSetLibraryDirOverride(const std::wstring& dir);   // headless: --ui-presets-dir
bool UiLibraryWritable();
const std::vector<UiPresetFile>& UiLibrary(bool rescan = false);
UiPresetFile UiClassifyPreset(const std::wstring& path);
std::wstring UiPresetBase(const std::wstring& path);   // [meta] base resolved, empty = none
std::wstring UiStemOf(const std::wstring& path);

// Apply (live: main.cpp ApplyPreset via the hook; headless: the same merge). [meta] base first.
void UiApplyPresetFile(const std::wstring& path);
// Save: partial update of the target file. log receives one line per written key.
bool UiSavePartial(const std::wstring& target, std::vector<std::string>* log);
// Save as: new partial file in the library. onlyChanges=false -> every key that differs from
// the code defaults (self-contained, no base). Returns the new path.
bool UiSaveAsPartial(const std::wstring& name, bool onlyChanges, std::wstring* outPath,
                     std::vector<std::string>* log);
bool UiDuplicatePreset(const std::wstring& path, std::wstring* outPath);
bool UiRenamePreset(const std::wstring& path, const std::wstring& newName, std::wstring* outPath);
// dryRun: log the SHFileOperation it would run, touch nothing.
bool UiDeletePreset(const std::wstring& path, bool dryRun, std::vector<std::string>* log);
