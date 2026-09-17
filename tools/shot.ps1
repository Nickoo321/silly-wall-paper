# One headless --shot render, serialised on build2/shots/gpu.lock.
# Only ONE GPU render may run at a time (two at once starved DWM and greyed the
# user's OLED), so every render in the repo goes through this: wait while the
# lock exists, create it with our name + time, render, delete it in a finally.
#   tools\shot.ps1 -Out build2\shots\x\a.png -Ini reference\configs\y.ini `
#                  -Delay 60 [-Size 960x540] [-Series "3:0.5"] [-Extra @('--foo')]
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string]$Ini,
    [double]$Delay = 60,
    [string]$Size = '960x540',
    [string]$Series = '',
    [int]$Seed = 1234,
    [int]$Yield = 2,
    [string]$Who = 'fable-droplets',
    [string[]]$Extra = @()
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lock = Join-Path $root 'build2\shots\gpu.lock'
$exe  = Join-Path $root 'build2\FluidWallpaper.exe'
New-Item -ItemType Directory -Force (Split-Path -Parent (Join-Path $root $Out)) | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $lock) | Out-Null

$waited = 0
while (Test-Path $lock) {
    if ($waited -ge 1800) { throw "gpu.lock held for 30 min by: $(Get-Content $lock -Raw)" }
    Start-Sleep -Seconds 5; $waited += 5
}
Set-Content -Encoding utf8 $lock "$Who $(Get-Date -Format o)"
try {
    $a = @('--shot', (Join-Path $root $Out), '--ini', (Join-Path $root $Ini),
           '--hdr', 'on', '--shot-delay', $Delay, '--shot-size', $Size,
           '--shot-yield', $Yield, '--seed', $Seed)
    if ($Series) { $a += @('--shot-series', $Series) }
    $a += $Extra
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & $exe @a 2>&1 | Out-String | Write-Output
    $sw.Stop()
    Write-Output ("shot.ps1 done in {0:N1}s -> {1}" -f $sw.Elapsed.TotalSeconds, $Out)
} finally {
    Remove-Item $lock -Force -ErrorAction SilentlyContinue
}
