# oil3d-shot.ps1 — run ONE Oil3DWallpaper --shot under the machine-wide GPU lock.
#
# Copy of tools/ink3d-shot.ps1 for the 3D oil prototype (src_oil3d/, brief
# reference/briefs/NEXT-oil3d.md).
#
# AGENTS.md "GPU lock": only one GPU render (any --shot run, any target, any
# worktree) may run at a time — two at once starved DWM and greyed the user's
# OLED. Wait for build2/shots/gpu.lock to disappear (poll 5 s, give up after
# 30 min), create it with our name + time, run, delete it in a finally.
#
# Usage (from the repo root):
#   & tools/oil3d-shot.ps1 -Out build2/shots/oil3d/proto-015.png `
#         -ExtraArgs @('--shot-delay','15','--shot-size','1280x720','--ini','reference/configs/oil3d-default.ini')
#   & tools/oil3d-shot.ps1 -Out build2/shots/oil3d/scheme-mc.png -ExtraArgs @(
#         '--ini','reference/configs/monotone-post-0924.ini',
#         '--ini','reference/presets/Layout - Few giants.ini',
#         '--ini','reference/presets/Scheme - Magenta Cyan.ini', '--timings')
#
# ExtraArgs, not Args: $Args is a PowerShell automatic variable. Arguments
# that contain spaces ("Layout - Few giants.ini") are quoted here:
# Start-Process joins -ArgumentList with plain spaces and would split them.
#
# Never stops FluidWallpaper.exe or Wallpaper Engine. Never launches the live
# exe; Oil3DWallpaper.exe itself is headless (no window, no focus change).

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Out,
    [string[]] $ExtraArgs = @(),
    [string]   $Exe = '',
    [int]      $TimeoutMin = 30,
    [string]   $Agent = 'oil3d-executor',
    [int]      $YieldMs = 2
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Exe)) { $Exe = Join-Path $root 'build2\Oil3DWallpaper.exe' }
if (-not (Test-Path $Exe)) { throw "Oil3DWallpaper.exe not found at $Exe (build2.cmd Oil3DWallpaper first)" }

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
    $quoted = $all | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    Write-Host "[shot] $Exe $($quoted -join ' ')"
    $p = Start-Process -FilePath $Exe -ArgumentList $quoted -WorkingDirectory $root `
                       -NoNewWindow -PassThru -Wait
    $exitCode = $p.ExitCode
    Write-Host "[shot] exit code $exitCode"
} finally {
    Remove-Item $lock -Force -ErrorAction SilentlyContinue
    Write-Host "[gpu-lock] released"
}

# tail the log so the caller sees timings even though the exe is /SUBSYSTEM:WINDOWS
$log = Join-Path $env:TEMP 'Oil3DWallpaper-shot.log'
if (Test-Path $log) {
    Write-Host "---- $log (tail) ----"
    Get-Content $log -Tail 60
}
exit $exitCode
