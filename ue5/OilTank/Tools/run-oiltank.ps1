# run-oiltank.ps1 -- build and/or run the headless OilTank UE 5.8 scene, ONE process.
# Pattern: G:\ManhattanFPV\Tools\run-test.ps1 (reference/briefs/UE5-HEADLESS-LESSONS.md).
#
#   Build (no GPU slot needed; BelowNormal):
#     powershell -File G:\UE\OilTank\Tools\run-oiltank.ps1 -Build
#   Still:
#     powershell -File G:\UE\OilTank\Tools\run-oiltank.ps1 -Dump G:\UE\OilTank\Dumps\a\dump.json -Shot G:\UE\OilTank\Saved\Shots\a.png
#   10 s clip (300 PNGs) of one dump:
#     ... -Dump <json> -Clip G:\UE\OilTank\Saved\Clips\a
#   Perf-only run (no capture; json perf_seconds):
#     ... -Dump <json>
#   Synthetic test field (first smoke test, no dump): omit -Dump.
#
# Rules baked in (do not remove):
#   * ALWAYS -game -RenderOffScreen -nosplash -unattended, started with -WindowStyle Hidden;
#     NEVER -windowed/-fullscreen (opens a window).
#   * Refuses while a *-Win64-Shipping game is using the GPU (unless -IgnoreGames), and while any
#     other UnrealEditor process runs (never two UE processes).
#   * GPU RULE (the user's, 2026-10-15; REPLACES build2\shots\gpu.lock for UE runs and builds): at most
#     3 GPU render processes at once across ALL agents. Before launching UE this script counts running
#     FluidWallpaper.exe processes whose command line contains --shot, plus UnrealEditor processes;
#     while that is >= -MaxGpuProcs (3) it waits (poll 30 s, give up after -LockTimeoutMinutes).
#     gpu.lock is NOT taken or held. FluidWallpaper.exe is only counted: never launched, stopped or signalled.
#   * BelowNormal priority, hard timeout (first launch compiles shaders for tens of minutes:
#     use -TimeoutMinutes 120 the first time). Only OUR UnrealEditor is ever stopped (timeout/interrupt).
#   * Never touches FluidWallpaper.exe or Wallpaper Engine. Space-free -Oil* paths only (the
#     lessons' -ExecCmds gotcha: arguments with spaces get split).

[CmdletBinding()]
param(
    [string] $Dump = '',
    [string] $DumpDir = '',
    [string] $Shot = '',
    [string] $Clip = '',
    [string] $Config = '',
    [int]    $ResX = 2560,
    [int]    $ResY = 1440,
    [int]    $TimeoutMinutes = 30,
    [int]    $LockTimeoutMinutes = 60,
    [int]    $MaxGpuProcs = 3,
    [switch] $Build,
    [switch] $IgnoreGames,
    [ValidateSet('Idle', 'BelowNormal', 'Normal')][string] $Priority = 'BelowNormal',
    [string] $Engine = 'G:\UE_5.8',
    [string] $Project = ''
)

$ErrorActionPreference = 'Stop'
$projDir = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Project)) { $Project = Join-Path $projDir 'OilTank.uproject' }
if (-not (Test-Path $Project)) { throw "uproject not found: $Project" }
$projDir = Split-Path -Parent $Project
$editor = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$buildBat = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'

foreach ($pair in @(@('Project', $Project), @('Dump', $Dump), @('DumpDir', $DumpDir), @('Shot', $Shot), @('Clip', $Clip), @('Config', $Config))) {
    if ($pair[1] -match '\s') { throw "-$($pair[0]) must be a path without spaces (got '$($pair[1])'); copy the input under $projDir first" }
}
if ($Dump -and -not (Test-Path $Dump)) { throw "dump not found: $Dump" }
if ($Config -and -not (Test-Path $Config)) { throw "config not found: $Config (UE would silently fall back to the built-in defaults)" }
if ($DumpDir -and -not (Test-Path (Join-Path $DumpDir 'dump_0000.json'))) { throw "no dump_0000.json in $DumpDir" }

function Get-BusyGames {
    # A game that is actually drawing: per-process 3D-engine utilisation > 1 %.
    $games = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        ($_.ProcessName -like '*-Win64-Shipping' -and $_.ProcessName -notmatch '^(EOSOverlayRenderer|CrashReportClient)') -or
        $_.ProcessName -match '^(Deadlock|project8|RainbowSix|RainbowSix_DX11|RainbowSix_Vulkan|RainbowSix_BE)$' })
    if ($games.Count -eq 0) { return @() }
    $samples = $null
    try { $samples = (Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction Stop).CounterSamples } catch { }
    $busy = @()
    foreach ($g in $games) {
        if ($null -eq $samples) { $busy += "$($g.ProcessName) pid $($g.Id) (GPU counters unavailable: assuming busy)"; continue }
        $u = ($samples | Where-Object { $_.InstanceName -like "pid_$($g.Id)_*" } | Measure-Object -Property CookedValue -Sum).Sum
        if ($u -gt 1) { $busy += "$($g.ProcessName) pid $($g.Id) gpu $([math]::Round($u, 1))%" }
    }
    return $busy
}

function Get-GpuRenderCount {
    # FluidWallpaper.exe --shot renders (any agent, any worktree) + any UnrealEditor process.
    $fw = @(Get-CimInstance Win32_Process -Filter "Name = 'FluidWallpaper.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -match '--shot' })
    $ue = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'UnrealEditor*' })
    return @{ Total = $fw.Count + $ue.Count; Fw = $fw.Count; Ue = $ue.Count }
}

if (-not $Build) {
    if (-not $IgnoreGames) {
        $busy = @(Get-BusyGames)
        if ($busy.Count -gt 0) { Write-Host "[oiltank] REFUSING: a game is using the GPU: $($busy -join '; ')"; exit 3 }
    }
    $otherUE = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'UnrealEditor*' })
    if ($otherUE.Count -gt 0) { Write-Host "[oiltank] REFUSING: another UE process runs: $(($otherUE | ForEach-Object { "$($_.ProcessName) pid $($_.Id)" }) -join ', ')"; exit 4 }

    $deadline = (Get-Date).AddMinutes($LockTimeoutMinutes)
    while ($true) {
        $c = Get-GpuRenderCount
        if ($c.Total -lt $MaxGpuProcs) { Write-Host "[oiltank] $(Get-Date -Format s) GPU renders running: $($c.Fw) FluidWallpaper --shot + $($c.Ue) UE (< $MaxGpuProcs): go"; break }
        if ((Get-Date) -gt $deadline) { Write-Host "[oiltank] still $($c.Total) GPU renders after $LockTimeoutMinutes min; giving up"; exit 2 }
        Write-Host "[oiltank] $(Get-Date -Format s) waiting: $($c.Fw) FluidWallpaper --shot + $($c.Ue) UE >= $MaxGpuProcs"
        Start-Sleep -Seconds 30
    }
}

# DDC on the project's own drive (fairness trap 10): Config/DefaultEngine.ini replaces the Installed graph's
# Local node with %GAMEDIR%DerivedDataCache; this env var also covers the stock Local node.
$ddc = Join-Path $projDir 'DerivedDataCache'
New-Item -ItemType Directory -Force -Path $ddc | Out-Null
[Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', $ddc, 'Process')

$exitCode = 1
$p = $null
try {
    if ($Build) {
        Write-Host "[oiltank] building OilTankEditor Win64 Development"
        $t0 = Get-Date
        $b = Start-Process -FilePath $buildBat -ArgumentList "OilTankEditor Win64 Development -Project=`"$Project`" -WaitMutex" -PassThru -NoNewWindow
        $null = $b.Handle   # cache the handle, else ExitCode can come back empty
        try { $b.PriorityClass = $Priority } catch { }
        if (-not $b.WaitForExit($TimeoutMinutes * 60 * 1000)) { Stop-Process -Id $b.Id -Force; throw "build timed out after $TimeoutMinutes min" }
        $exitCode = $b.ExitCode
        Write-Host "[oiltank] build exit $exitCode after $([int]((Get-Date) - $t0).TotalSeconds) s"
    } else {
        if (-not (Test-Path $editor)) { throw "UnrealEditor.exe not found: $editor" }
        $a = @("`"$Project`"", '-game', '-RenderOffScreen', '-nosplash', '-unattended', '-nosound', '-log',
               "-ResX=$ResX", "-ResY=$ResY")
        if ($Dump)    { $a += "-OilDump=$Dump" }
        if ($DumpDir) { $a += "-OilDumpDir=$DumpDir" }
        if ($Shot)    { New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Shot) | Out-Null; $a += "-OilShot=$Shot" }
        if ($Clip)    { New-Item -ItemType Directory -Force -Path $Clip | Out-Null; $a += "-OilClip=$Clip" }
        if ($Config)  { $a += "-OilConfig=$Config" }
        if (($a -join ' ') -match '-windowed|-fullscreen') { throw 'refusing to open a window' }
        foreach ($must in @('-game', '-RenderOffScreen', '-nosplash', '-unattended')) { if ($a -notcontains $must) { throw "missing $must" } }

        $logs = Join-Path $projDir 'Saved\Logs'
        Write-Host "[oiltank] $editor $($a -join ' ')"
        $t0 = Get-Date
        $p = Start-Process -FilePath $editor -ArgumentList ($a -join ' ') -PassThru -WindowStyle Hidden
        $null = $p.Handle
        try { $p.PriorityClass = $Priority } catch { }
        if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
            Write-Host "[oiltank] TIMEOUT after $TimeoutMinutes min: stopping our UnrealEditor pid $($p.Id)"
            Stop-Process -Id $p.Id -Force
            $exitCode = 124
        } else {
            $exitCode = $p.ExitCode
        }
        Write-Host "[oiltank] exit $exitCode after $([int]((Get-Date) - $t0).TotalSeconds) s"

        foreach ($f in @('oiltank-perf.txt', 'oiltank-calib.txt')) {
            $pth = Join-Path $logs $f
            if (Test-Path $pth) { Write-Host "--- $f (tail)"; Get-Content $pth -Tail 8 | ForEach-Object { Write-Host $_ } }
        }
        $ue = Join-Path $logs 'OilTank.log'
        if (Test-Path $ue) {
            Write-Host '--- OilTank.log: LogOilTank / errors'
            Select-String -Path $ue -Pattern 'LogOilTank|Error:|Fatal' | Select-Object -Last 40 | ForEach-Object { Write-Host $_.Line }
        }
        if ($Shot -and -not (Test-Path $Shot)) { Write-Host "[oiltank] NO SHOT written at $Shot"; if ($exitCode -eq 0) { $exitCode = 5 } }
    }
} finally {
    # Only OUR UnrealEditor is ever stopped (timeout above, or this script interrupted).
    if ($p -and -not $p.HasExited) { try { Stop-Process -Id $p.Id -Force } catch { } }
}
exit $exitCode
