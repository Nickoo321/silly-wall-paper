# ink3d-shot.ps1 — run ONE Ink3DWallpaper --shot under the machine-wide GPU lock.
#
# AGENTS.md "GPU lock": only one GPU render (any --shot run, any target, any
# worktree) may run at a time — two at once starved DWM and greyed the user's
# OLED. Wait for build2/shots/gpu.lock to disappear (poll 5 s, give up after
# 30 min), create it with our name + time, run, delete it in a finally.
#
# Usage:
#   & tools/ink3d-shot.ps1 -Out build3/shots/m0-sphere.png `
#         -ExtraArgs @('--static-scene', '--shot-delay', '1.0', '--timings')
#   & tools/ink3d-shot.ps1 -Out build3/shots/m1-drop.png -TimeoutMin 20 `
#         -ExtraArgs @('--shot-series', '3:1')
#
# ExtraArgs, not Args: $Args is a PowerShell automatic variable.
#
# Never stops FluidWallpaper.exe or Wallpaper Engine. Never launches the live
# (non-shot) exe.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Out,
    [string[]] $ExtraArgs = @(),
    [string]   $Exe = '',
    [int]      $TimeoutMin = 30,
    [string]   $Agent = 'ink3d-executor',
    [int]      $YieldMs = 2
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Exe)) { $Exe = Join-Path $root 'build3\Ink3DWallpaper.exe' }
if (-not (Test-Path $Exe)) { throw "Ink3DWallpaper.exe not found at $Exe (build it into build3/ first)" }

$lock = Join-Path $root 'build2\shots\gpu.lock'
$lockDir = Split-Path -Parent $lock
if (-not (Test-Path $lockDir)) { New-Item -ItemType Directory -Force $lockDir | Out-Null }

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# --- wait for the lock ------------------------------------------------------
$deadline = (Get-Date).AddMinutes($TimeoutMin)
while (Test-Path $lock) {
    if ((Get-Date) -gt $deadline) {
        $holder = ''
        try { $holder = (Get-Content $lock -Raw).Trim() } catch { }
        throw "GPU lock still held after $TimeoutMin min (holder: $holder). Giving up."
    }
    Write-Host "[gpu-lock] held, waiting 5 s..."
    Start-Sleep -Seconds 5
}

# --- take the lock ----------------------------------------------------------
$stamp = (Get-Date).ToString('s')
"$Agent $stamp pid=$PID" | Out-File -FilePath $lock -Encoding utf8 -Force
Write-Host "[gpu-lock] taken by $Agent at $stamp"

$exitCode = 1
try {
    $all = @('--shot', $Out, '--shot-yield', "$YieldMs") + $ExtraArgs
    Write-Host "[shot] $Exe $($all -join ' ')"
    $p = Start-Process -FilePath $Exe -ArgumentList $all -NoNewWindow -PassThru -Wait
    $exitCode = $p.ExitCode
    Write-Host "[shot] exit code $exitCode"
} finally {
    Remove-Item $lock -Force -ErrorAction SilentlyContinue
    Write-Host "[gpu-lock] released"
}

# tail the log so the caller sees timings/stats even though the exe is /SUBSYSTEM:WINDOWS
$log = Join-Path $env:TEMP 'Ink3DWallpaper-shot.log'
if (Test-Path $log) {
    Write-Host "---- $log (tail) ----"
    Get-Content $log -Tail 60
}
exit $exitCode
