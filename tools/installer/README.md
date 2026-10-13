# Friend installer (tools/installer)

Builds a per-user installer of what is live on this PC: the exe, the current
`settings.ini` (its `[cycle]`), every look file the cycle names, and the tray presets.

## Update procedure (the whole thing)

```powershell
# from PowerShell, in the repo
tools\installer\build-installer.ps1                       # live exe + live settings + presets
tools\installer\build-installer.ps1 -Exe build2\live\FluidWallpaper.exe -Commit <short hash>
```

Send the friend `dist\FluidWallpaper-Setup-<version>.exe` (or the `.zip`: unzip, double-click
`Install.cmd`). Running it over an old install is the update: their `settings.ini` is copied to
`settings-backup-<yyyyMMdd-HHmmss>.ini` and replaced by yours (so they get your current cycle),
same-named presets are replaced, their own presets stay, their "start with Windows" choice stays.

The version label takes the commit from `git rev-parse --short HEAD` unless `-Commit` is given:
pass `-Commit <hash the exe was built from>` when HEAD has moved on since the exe was built
(the first package: `-Commit 4de5a67`, while HEAD was c0c576f).

Options: `-Version <text>` (default `<yyyy.MM.dd>-<commit>`; `-Commit` default = `git rev-parse
--short HEAD`), `-Settings <ini>`, `-PresetsDir <dir>`, `-OutDir <dir>` (default `dist`),
`-Hotkey none|<key>` (default = the hotkey of the desktop shortcut "Oil on-off.lnk"),
`-KeepLookPeak` (ship the looks' / presets' own `[hdr] peak_nits`; by default they are stripped,
because a stage file's peak overrides the user's own while the stage runs, src/cycle.cpp `Compose`,
and `monotone-post-0924.ini`, the base of 15 stages, carries this panel's 1055; stripped, the
friend's `peak_nits=-1` auto applies on every stage), `-NoSetupExe`.

The source sanity check expects `[cycle] we_every=2` and an `oil_layout` naming
"Layout - Few giants" (today's live cycle). When the cycle changes on purpose, pass
`-ExpectWeEvery N -ExpectOilLayout <name>` or `-NoSourceCheck`.

## What the build does

* `dist\stage\` = `Install.cmd`, `install.ps1`, `README.txt`, `program\` (exe, toggle.vbs/.ps1,
  fw-common.ps1, uninstall.ps1, README.txt, version.txt, hotkey.txt), `data\` (settings.ini,
  presets\, looks\configs\, looks\presets\).
* settings.ini: comments dropped, every `[cycle]` path rewritten to `looks\...` (the app resolves
  relative `stage_N_file/base` and `oil_layout` against the folder of settings.ini:
  src/cycle.cpp `Resolve()` + `CycleLoad` passing `s_iniPath`), `[hdr] peak_nits=-1`,
  `[general] mirror_second=0`, `[cycle] current` removed, `[ui] active_preset/overlays/window`
  removed, any other absolute or photos/moods path dropped.
* Look files: copied with their folder name kept (`configs`, `presets`) so a relative
  `[meta] base` (resolved against the file's own folder) still works; an absolute one is rewritten.
* Tray presets: `sim_res`, `dye_res`, `fps_limit`, `mirror_second` lines removed; encoding
  (UTF-16 with or without BOM, or 8-bit) kept byte for byte otherwise.
* Checks that fail the build: every stage file / base / oil_layout / `[meta] base` resolves to a
  staged file; no "abg77", "OneDrive", "C:\Users" in any staged text file (nor "abg77" in the
  exe / Setup exe); nothing from photos\ or moods\; staged settings.ini has `peak_nits=-1` and no
  absolute path; staged stage count = source stage count.
* `dist\FluidWallpaper-<version>.zip` and `dist\FluidWallpaper-Setup-<version>.exe` (IExpress;
  it carries payload.zip + Install.cmd, Install.cmd unpacks to %TEMP% and runs install.ps1).
  The SED launches `cmd.exe /c .\Install.cmd` (explicit `.\`): with
  `NoDefaultCurrentDirectoryInExePath` set, a bare `Install.cmd` is not found and the Setup exe
  exits 0 having installed nothing.

## Testing without touching this PC

```powershell
dist\stage\install.ps1 -InstallDir <scratch>\prog -DataDir <scratch>\data -ShortcutDir <scratch>\links -NoRegistry -NoLaunch -NoStop -Quiet
<scratch>\prog\uninstall.ps1 -DataDir <scratch>\data -ShortcutDir <scratch>\links -NoRegistry -NoStop -Quiet [-RemoveData]
FluidWallpaper-Setup-<version>.exe /Q /C /T:<scratch>\x      # extract only, runs nothing
```

Never run install.ps1 / uninstall.ps1 / toggle on this PC without those switches: they stop the
running FluidWallpaper.exe and write the real %APPDATA%, Desktop, Start Menu and HKCU.
