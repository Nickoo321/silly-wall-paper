# Oil on/off switch, independent of the app's tray UI.
# Running  -> stop every FluidWallpaper.exe (this is the user's own off switch).
# Not running -> launch the LIVE build (build2\live) with whatever settings.ini
#                the last panel-check installed; the ini is not touched.
# Wired to the desktop shortcut "Oil on-off" (hotkey Ctrl+Alt+O).
$root = Split-Path -Parent $PSScriptRoot
$liveDir = Join-Path $root "build2\live"
$log = Join-Path $root "build2\shots\oil-toggle.log"
$p = Get-Process FluidWallpaper -ErrorAction SilentlyContinue
if ($p) {
    $p | Stop-Process -Force
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  OFF  (stopped pid $($p.Id -join ','))" | Out-File -Append $log
} else {
    Start-Process (Join-Path $liveDir "FluidWallpaper.exe") -WorkingDirectory $liveDir
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  ON   (launched build2\live\FluidWallpaper.exe)" | Out-File -Append $log
}
