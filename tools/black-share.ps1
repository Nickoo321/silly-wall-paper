# black-share.ps1 -- black % of a look over a long window (brief BZ BLACK-MASS, pre-flight 13-14).
#
# Renders headless --shot series (one per seed) and measures, on each 8-bit SDR <out>.png,
# the share of pixels with max(R,G,B) < 40 (tools\black-share.py). Prints mean/min/max per
# seed and the mean of the seed means. Never reads the shared %TEMP% ShotLog: the exe's own
# stdout goes to <OutDir>\s<seed>.log (the [turnover] lines land there too).
#
#   tools\black-share.ps1 -Ini <full.ini>                              # a full ini
#   tools\black-share.ps1 -Ini <partial.ini> -Base <full.ini>          # a partial, merged onto a base
#   tools\black-share.ps1 -Base <full.ini> -Set 'liquid_acid.threshold=0.9','liquid_acid.blob_count=60'
#       [-Seeds 1234,5678,9012] [-Hdr on|off] [-Delay 30] [-Series 31:10] [-Size 1280x720]
#       [-Yield 30] [-Label name] [-OutDir dir] [-Exe exe] [-Extra '--shot-preset','x.ini','AT','5']
#       [-NoLock] [-LockMinutes 240] [-KeepFrames]
#
# Defaults = the binding run (pre-flight 14): 1280x720, --shot-series 31:10 from T0 = 30 s
# (a 300 s window), --shot-yield 30, --shot-png-only. With blob_turnover_s on, pass
# -Delay >= blob_turnover_s (the population is not the seed's until one period has passed).
# Partials are merged into a scratch FULL ini (<OutDir>\composed.ini, overlay keys first).
# GPU: this process drops itself to BelowNormal (children inherit it) and takes the MAIN
# repo's build2\shots\gpu.lock per seed (tools\gpu-lock.ps1), unless -NoLock.
param(
    [string]$Ini = '',
    [string]$Base = '',
    [string[]]$Set = @(),
    [int[]]$Seeds = @(1234, 5678, 9012),
    [ValidateSet('on', 'off')][string]$Hdr = 'on',
    [double]$Delay = 30,
    [string]$Series = '31:10',
    [string]$Size = '1280x720',
    [int]$Yield = 30,
    [string]$Label = '',
    [string]$OutDir = '',
    [string]$Exe = '',
    [string[]]$Extra = @(),
    [switch]$NoLock,
    [int]$LockMinutes = 240,
    [switch]$KeepFrames
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
try { [System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal' } catch {}
if (-not $Exe) { $Exe = Join-Path $root 'build2\FluidWallpaper.exe' }
if (-not (Test-Path $Exe)) { throw "no exe: $Exe" }
if (-not $Ini -and -not $Base) { throw 'need -Ini and/or -Base' }
if (-not $Label) {
    $Label = if ($Ini) { [IO.Path]::GetFileNameWithoutExtension($Ini) } else { [IO.Path]::GetFileNameWithoutExtension($Base) }
    $Label = ($Label -replace '[^A-Za-z0-9._-]', '_')
}
if (-not $OutDir) { $OutDir = Join-Path $root "build2\shots\blackmass\$Label-$Hdr" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$py = Join-Path $PSScriptRoot 'black-share.py'

# ---- the ini actually rendered -------------------------------------------------------------
$runIni = $Ini
if ($Base -or $Set.Count -gt 0) {
    $b = if ($Base) { $Base } else { $Ini }
    $o = if ($Base -and $Ini) { $Ini } else { '-' }
    $runIni = Join-Path $OutDir 'composed.ini'
    & python $py merge $b $o $runIni @Set | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'merge failed' }
}
$runIni = (Resolve-Path $runIni).Path

. (Join-Path $PSScriptRoot 'gpu-lock.ps1')

$groups = @()
foreach ($s in $Seeds) {
    $stem = Join-Path $OutDir "s$s"
    Get-ChildItem -Path $OutDir -Filter "s$s*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
    $log = "$stem.log"
    $args2 = @('--shot', "$stem.png", '--ini', $runIni, '--hdr', $Hdr, '--seed', "$s",
               '--shot-size', $Size, '--shot-delay', "$Delay", '--shot-series', $Series,
               '--shot-yield', "$Yield", '--shot-png-only') + $Extra
    $argStr = ($args2 | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $got = $true
    if (-not $NoLock) { $got = Wait-GpuLock -Owner "BLACKMASS black-share $Label hdr=$Hdr seed=$s" -TimeoutMinutes $LockMinutes }
    if (-not $got) { Write-Output "LOCK TIMEOUT after $LockMinutes min: $Label seed $s skipped"; continue }
    $t0 = Get-Date
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $argStr -PassThru -NoNewWindow `
                           -RedirectStandardOutput $log -RedirectStandardError "$stem.err"
        $null = $p.Handle   # cache the handle, or ExitCode reads empty after WaitForExit
        try { $p.PriorityClass = 'BelowNormal' } catch {}
        $p.WaitForExit()
    } finally {
        if (-not $NoLock) { [void](Release-GpuLock) }
    }
    $wall = [int]((Get-Date) - $t0).TotalSeconds
    Write-Output "[$Label hdr=$Hdr seed=$s] exit $($p.ExitCode), $wall s wall"
    $groups += "seed$s=$stem-*.png"
}
$json = Join-Path $OutDir 'black-share.json'
& python $py --deltas --json $json @groups
Add-Content -Path (Join-Path $OutDir 'summary.txt') -Value ("{0} ini={1} set=[{2}] hdr={3} delay={4} series={5} seeds={6}" -f `
    (Get-Date -Format s), $runIni, ($Set -join ' '), $Hdr, $Delay, $Series, ($Seeds -join ','))
& python $py @groups | Add-Content -Path (Join-Path $OutDir 'summary.txt')
if (-not $KeepFrames) {
    # keep the first, middle and last frame of each seed for sheets; the rest is ~1 MB a frame
    foreach ($s in $Seeds) {
        $f = @(Get-ChildItem -Path $OutDir -Filter "s$s-*.png" | Sort-Object Name)
        if ($f.Count -le 3) { continue }
        $keep = @($f[0].Name, $f[[int]($f.Count / 2)].Name, $f[$f.Count - 1].Name)
        $f | Where-Object { $keep -notcontains $_.Name } | Remove-Item -Force
    }
}
