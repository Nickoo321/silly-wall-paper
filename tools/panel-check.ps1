# Swap the LIVE wallpaper to a test look for an on-panel check, or restore it.
#   powershell -File tools\panel-check.ps1 -Ini reference\configs\ink-inverted.ini
#   powershell -File tools\panel-check.ps1 -Restore
# What it does: backs up %APPDATA%\FluidWallpaper\settings.ini (once, to settings-panelcheck-backup.ini),
# copies the chosen ini over it, stops the running FluidWallpaper.exe, and launches build2\FluidWallpaper.exe
# (the build with the new looks). -Restore copies the backup back and relaunches build\FluidWallpaper.exe.
# Wallpaper Engine is never touched. Run ONLY with the user's go-ahead: it changes what is on their screen.
#   -InstallPresets : also copy reference\presets\*.ini into %APPDATA%\FluidWallpaper\moods (tray Presets menu)
#   -Exe <path>     : launch a copy of THIS exe instead of build2\FluidWallpaper.exe (use a build of a COMMITTED
#                     state: on 2026-09-17 two uncommitted work-in-progress build2 exes crashed live at startup)
# NOTE: run this via a scheduled task (tools\panel-check-task.ps1) when driven from Claude Code — its
# processes are MSIX-virtualized and their %APPDATA% writes land in a shadow folder the user never sees.
param([string]$Ini = "", [switch]$Restore, [switch]$InstallPresets, [string]$Exe = "")
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$logf = Join-Path $root "build2\shots\panel-check.log"
function Log($m) { $l = "{0}  {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m; Add-Content -Path $logf -Value $l; Write-Host $l }
Log ("start ini='{0}' restore={1} installPresets={2} appdata={3}" -f $Ini, [bool]$Restore, [bool]$InstallPresets, $env:APPDATA)
if ($InstallPresets) {
    $moods = Join-Path $env:APPDATA "FluidWallpaper\moods"
    if (-not (Test-Path $moods)) { New-Item -ItemType Directory -Path $moods | Out-Null }
    $n = 0
    Get-ChildItem (Join-Path $root "reference\presets") -Filter *.ini | ForEach-Object { Copy-Item $_.FullName $moods -Force; $n++ }
    Log ("installed {0} presets into {1} (now {2} inis)" -f $n, $moods, (Get-ChildItem $moods -Filter *.ini).Count)
}
$root = Split-Path -Parent $PSScriptRoot
$live = Join-Path $env:APPDATA "FluidWallpaper\settings.ini"
$bak  = Join-Path $env:APPDATA "FluidWallpaper\settings-panelcheck-backup.ini"
function Stop-Port { Get-Process FluidWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 800 }
if ($Restore) {
    if (-not (Test-Path $bak)) { Log "no backup found at $bak"; exit 1 }
    Copy-Item $bak $live -Force
    Stop-Port
    Start-Process (Join-Path $root "build\FluidWallpaper.exe") -WorkingDirectory (Join-Path $root "build")
    Log "restored settings.ini from backup and relaunched build\FluidWallpaper.exe"
    exit 0
}
if (-not $Ini) { Log "usage: -Ini <path> | -Restore"; exit 1 }
$src = if ([IO.Path]::IsPathRooted($Ini)) { $Ini } else { Join-Path $root $Ini }
if (-not (Test-Path $src)) { Log "ini not found: $src"; exit 1 }
if (-not (Test-Path $bak)) { Copy-Item $live $bak; Log "backup written: $bak" } else { Log "backup already exists (kept): $bak" }
Copy-Item $src $live -Force
Stop-Port
# Launch a COPY so agents can keep rebuilding build2\FluidWallpaper.exe while the live one runs
$liveDir = Join-Path $root "build2\live"
if (-not (Test-Path $liveDir)) { New-Item -ItemType Directory -Path $liveDir | Out-Null }
$exeSrc = if ($Exe) { $Exe } else { Join-Path $root "build2\FluidWallpaper.exe" }
if (-not (Test-Path $exeSrc)) { Log "exe not found: $exeSrc"; exit 1 }
Copy-Item $exeSrc (Join-Path $liveDir "FluidWallpaper.exe") -Force
Get-ChildItem (Join-Path $root "build2") -Filter *.dll -ErrorAction SilentlyContinue | Copy-Item -Destination $liveDir -Force
Start-Process (Join-Path $liveDir "FluidWallpaper.exe") -WorkingDirectory $liveDir
Log "live settings.ini <- $src ; launched build2\live\FluidWallpaper.exe (copy of $exeSrc, built $((Get-Item $exeSrc).LastWriteTime.ToString('HH:mm:ss')))"
