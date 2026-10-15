# oil3dlab-shot.ps1 — run ONE Oil3DLab --shot under the machine-wide GPU lock.
#
# AGENTS.md "GPU lock": one GPU render at a time, any target, any worktree.
# Uses the MAIN repo's tools\gpu-lock.ps1 (the lock file is always the main
# repo's build2\shots\gpu.lock). The exe runs at BelowNormal priority (it also
# lowers itself) and is headless: nothing appears on screen, no focus change.
#
# Usage:
#   & tools/oil3dlab-shot.ps1 -Out 'C:\...\build2\shots\oil3d\lab-01.png' `
#         -ExtraArgs @('--shot-size','1280x720','--shot-delay','15','--shade','gloss')
#
# Never stops FluidWallpaper.exe or Wallpaper Engine.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Out,
    [string[]] $ExtraArgs = @(),
    [string]   $Exe = '',
    [string]   $Agent = 'oil3d-lab',
    [int]      $YieldMs = 30
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Exe)) { $Exe = Join-Path $root 'build2\Oil3DLab.exe' }
if (-not (Test-Path $Exe)) { throw "Oil3DLab.exe not found at $Exe" }
. 'C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\tools\gpu-lock.ps1'

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

if (-not (Wait-GpuLock -Owner $Agent -TimeoutMinutes 30)) { throw "GPU lock not acquired in 30 min" }
$exitCode = 1
try {
    # Start-Process joins -ArgumentList without quoting: quote anything with a space
    $all = @('--shot', $Out, '--shot-yield', "$YieldMs") + $ExtraArgs
    $quoted = $all | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    Write-Host "[shot] $Exe $($quoted -join ' ')"
    $p = Start-Process -FilePath $Exe -ArgumentList $quoted -NoNewWindow -PassThru
    $null = $p.Handle   # cache the handle, or ExitCode reads back empty
    try { $p.PriorityClass = 'BelowNormal' } catch { }
    $p.WaitForExit()
    $exitCode = $p.ExitCode
    Write-Host "[shot] exit code $exitCode"
} finally {
    Release-GpuLock | Out-Null
    Write-Host "[gpu-lock] released"
}
$log = Join-Path $env:TEMP 'Oil3DLab-shot.log'
if (Test-Path $log) { Get-Content $log -Tail 24 }
exit $exitCode
