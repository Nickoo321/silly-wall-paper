# Oil on/off switch, independent of the app's tray UI.
# The LIVE wallpaper (build2\live\FluidWallpaper.exe) running -> stop it (the user's own off switch).
# Not running -> launch it with whatever settings.ini the last panel-check installed; the ini is
#                not touched.
# Only the live copy counts and only the live copy is stopped. Agent renders (--shot runs of other
# builds) are left alone: until 2026-10-04 any FluidWallpaper.exe counted as "running", so with a
# render in progress the switch stopped the render and did NOT turn the wallpaper on (it took a
# second press), and every "off" also killed the render.
# Wired to the desktop shortcut "Oil on-off" (hotkey Ctrl+Alt+O).
$root = Split-Path -Parent $PSScriptRoot
$liveDir = Join-Path $root "build2\live"
$log = Join-Path $root "build2\shots\oil-toggle.log"
$live = @(Get-Process FluidWallpaper -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$liveDir\*" })
if ($live.Count -gt 0) {
    $live | Stop-Process -Force
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  OFF  (stopped pid $($live.Id -join ','))" | Out-File -Append $log
} else {
    Start-Process (Join-Path $liveDir "FluidWallpaper.exe") -WorkingDirectory $liveDir
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  ON   (launched build2\live\FluidWallpaper.exe)" | Out-File -Append $log
}
