# Pause agent renders while a game runs (user 2026-09-26 23:30: "pause renderers while I'm in game").
# Every 15 s: if a game process is running (Path under steamapps/Games/Riot/Epic, minus launchers,
# overlays and Wallpaper Engine), SUSPEND every FluidWallpaper.exe that is NOT the live wallpaper
# (live = build2\live\FluidWallpaper.exe); when no game runs, RESUME them. The render queue scripts
# just wait on the suspended shot, so nothing is lost and the GPU lock stays valid.
# Never touches the live exe, Wallpaper Engine or any other process. Log: build2\shots\game-pause.log
# Stop: create build2\shots\game-pause.stop
param([int]$PollSec = 15)
$root = Split-Path -Parent $PSScriptRoot
$log  = Join-Path $root "build2\shots\game-pause.log"
$stop = Join-Path $root "build2\shots\game-pause.stop"
Add-Type -Name NT -Namespace P -MemberDefinition @'
[DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
[DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
'@
function L($m) { Add-Content $log ("{0:yyyy-MM-dd HH:mm:ss}  {1}" -f (Get-Date), $m) }
$suspended = @{}
L "start poll=$PollSec"
while (-not (Test-Path $stop)) {
    $game = Get-Process | Where-Object { $_.Path -match 'steamapps|\\Games\\|Riot|Epic' -and
        $_.Name -notmatch 'EOSOverlay|EpicWebHelper|wallpaper|winrtutil|EpicGamesLauncher|EpicOnline|steamwebhelper|steam$' } |
        Select-Object -First 1 -Expand Name
    $renders = Get-Process FluidWallpaper -ErrorAction SilentlyContinue | Where-Object { $_.Path -notmatch '\\build2\\live\\' }
    if ($game) {
        foreach ($p in $renders) { if (-not $suspended.ContainsKey($p.Id)) {
            if ([P.NT]::NtSuspendProcess($p.Handle) -eq 0) { $suspended[$p.Id] = $p.Path; L "SUSPEND $($p.Id) ($game running)" } } }
    } else {
        foreach ($id in @($suspended.Keys)) {
            $p = Get-Process -Id $id -ErrorAction SilentlyContinue
            if ($p) { [void][P.NT]::NtResumeProcess($p.Handle); L "RESUME $id" }
            $suspended.Remove($id) }
    }
    # a suspended process that ended (or was ended by the user) drops out of the table
    foreach ($id in @($suspended.Keys)) { if (-not (Get-Process -Id $id -ErrorAction SilentlyContinue)) { $suspended.Remove($id) } }
    Start-Sleep -Seconds $PollSec
}
foreach ($id in @($suspended.Keys)) { $p = Get-Process -Id $id -ErrorAction SilentlyContinue; if ($p) { [void][P.NT]::NtResumeProcess($p.Handle); L "RESUME $id (stop)" } }
Remove-Item $stop -ErrorAction SilentlyContinue
L "stop"
