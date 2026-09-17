# Swap the LIVE wallpaper to a test look for an on-panel check, or restore it.
#   powershell -File tools\panel-check.ps1 -Ini reference\configs\ink-inverted.ini
#   powershell -File tools\panel-check.ps1 -Restore
# What it does: backs up %APPDATA%\FluidWallpaper\settings.ini (once, to settings-panelcheck-backup.ini),
# copies the chosen ini over it, stops the running FluidWallpaper.exe, and launches build2\FluidWallpaper.exe
# (the build with the new looks). -Restore copies the backup back and relaunches build\FluidWallpaper.exe.
# Wallpaper Engine is never touched. Run ONLY with the user's go-ahead: it changes what is on their screen.
param([string]$Ini = "", [switch]$Restore)
$root = Split-Path -Parent $PSScriptRoot
$live = Join-Path $env:APPDATA "FluidWallpaper\settings.ini"
$bak  = Join-Path $env:APPDATA "FluidWallpaper\settings-panelcheck-backup.ini"
function Stop-Port { Get-Process FluidWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 800 }
if ($Restore) {
    if (-not (Test-Path $bak)) { Write-Host "no backup found at $bak"; exit 1 }
    Copy-Item $bak $live -Force
    Stop-Port
    Start-Process (Join-Path $root "build\FluidWallpaper.exe") -WorkingDirectory (Join-Path $root "build")
    Write-Host "restored settings.ini from backup and relaunched build\FluidWallpaper.exe"
    exit 0
}
if (-not $Ini) { Write-Host "usage: -Ini <path> | -Restore"; exit 1 }
$src = if ([IO.Path]::IsPathRooted($Ini)) { $Ini } else { Join-Path $root $Ini }
if (-not (Test-Path $src)) { Write-Host "ini not found: $src"; exit 1 }
if (-not (Test-Path $bak)) { Copy-Item $live $bak; Write-Host "backup written: $bak" } else { Write-Host "backup already exists (kept): $bak" }
Copy-Item $src $live -Force
Stop-Port
Start-Process (Join-Path $root "build2\FluidWallpaper.exe") -WorkingDirectory (Join-Path $root "build2")
Write-Host "live settings.ini <- $src ; launched build2\FluidWallpaper.exe"
