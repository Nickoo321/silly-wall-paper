# Acid-side equivalent of the fluid parity check. Renders a list of presets
# headless with a given exe, md5's the PNGs, and either saves them as a
# baseline or compares them against a saved baseline / each other.
#
# The fluid look (reference\configs\we-look-live.ini) is ALWAYS rendered
# first and reported separately: its md5 must equal the fluid parity
# constant below regardless of -Presets/-Baseline, because that check is
# sacred (EXECUTOR-CARD.md hard rules) and independent of whatever acid
# presets are being audited.
#
# Usage:
#   tools\preset-identity.ps1 -Exe <path> [-Baseline <file>] [-Save <file>] `
#                              [-Presets <list>] [-Delay 60]
#
# Examples:
#   tools\preset-identity.ps1 -Exe build2\FluidWallpaper.exe -Save build2\shots\preset-identity-baseline-abc1234.txt
#   tools\preset-identity.ps1 -Exe build2\FluidWallpaper.exe -Baseline build2\shots\preset-identity-baseline-abc1234.txt
#
# Exit code: 1 if the fluid parity check fails, or (with -Baseline) if any
# preset's md5 differs from the baseline. 0 otherwise.

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Baseline = '',
    [string]$Save = '',
    [string[]]$Presets = @(),
    [double]$Delay = 60,
    [string]$ScratchDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

. (Join-Path $root 'tools\gpu-lock.ps1')

$FluidIni = Join-Path $root 'reference\configs\we-look-live.ini'
$FluidParityMd5 = '10E36EBF1A74EDFE609065D757300054'

# -Presets was passed explicitly but resolved to zero files (e.g. a glob that matched
# nothing) -- that used to fall through to the hardcoded default list below, silently
# testing the WRONG presets instead of telling the caller their list was empty. Only
# the "supplied but empty" case is an error; omitting -Presets entirely still means
# "use the default list" (checked via $PSBoundParameters, not just .Count).
if ($PSBoundParameters.ContainsKey('Presets') -and $Presets.Count -eq 0) {
    Write-Error "preset-identity.ps1: -Presets was supplied but resolved to zero files. Pass at least one preset path, or omit -Presets entirely to use the default list."
    exit 1
}

if ($Presets.Count -eq 0) {
    $Presets = @(
        'reference\configs\acid-rise-12.ini',
        'reference\configs\acid-rise-2hue.ini',
        'reference\configs\acid-rise-8020.ini',
        'reference\configs\acid-rise-rotate.ini',
        'reference\presets\Liquid Acid - rising colours (camera medium).ini'
    )
}

if (-not $ScratchDir) {
    $ScratchDir = Join-Path $env:TEMP 'preset-identity'
}
New-Item -ItemType Directory -Force -Path $ScratchDir | Out-Null

if (-not (Test-Path $Exe)) { throw "Exe not found: $Exe" }
$exePath = (Resolve-Path $Exe).Path

function Resolve-IniPath {
    param([string]$Ini)
    if ([System.IO.Path]::IsPathRooted($Ini)) { return $Ini }
    return (Join-Path $root $Ini)
}

function Wait-StablePng {
    # Bounded at 20 min (was 180 s, which could fire mid-render on a slow/contended
    # GPU): a render that never lands must not hang the caller or throw an
    # unhandled exception out of the try/finally -- it prints a clear TIMEOUT
    # line and lets the caller move on to the next preset.
    param([string]$Path, [int]$StableSeconds = 3, [int]$TimeoutSeconds = 1200)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastSize = -1
    $stableSince = $null
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $Path) {
            $size = (Get-Item $Path).Length
            if ($size -eq $lastSize -and $size -gt 0) {
                if (-not $stableSince) { $stableSince = Get-Date }
                if (((Get-Date) - $stableSince).TotalSeconds -ge $StableSeconds) { return $true }
            } else {
                $lastSize = $size
                $stableSince = $null
            }
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Get-PresetMd5 {
    param([string]$Name, [string]$IniPath, [string]$OutPng)
    if (Test-Path $OutPng) { Remove-Item $OutPng -Force -ErrorAction SilentlyContinue }
    & $exePath --shot $OutPng --ini $IniPath --hdr on --shot-delay $Delay `
        --shot-size 2560x1440 --shot-yield 8 --seed 1234 2>&1 | Out-Null
    if (-not (Wait-StablePng -Path $OutPng)) {
        Write-Output "$Name TIMEOUT (no stable PNG after 20 min): $OutPng"
        return $null
    }
    return (Get-FileHash -Algorithm MD5 -Path $OutPng).Hash
}

$locked = $false
try {
    $locked = Wait-GpuLock -Owner 'identity' -TimeoutMinutes 2
    # proceed even if the wait timed out (user: render at full speed)

    $results = [ordered]@{}
    $anyDiff = $false

    # Fluid parity: always first, always reported separately.
    $fluidOut = Join-Path $ScratchDir 'we-look-live.png'
    $fluidMd5 = Get-PresetMd5 -Name 'we-look-live' -IniPath $FluidIni -OutPng $fluidOut
    if ($null -eq $fluidMd5) {
        Write-Output "PARITY: TIMEOUT"
        $anyDiff = $true
    } else {
        Write-Output "we-look-live $fluidMd5"
        if ($fluidMd5 -eq $FluidParityMd5) {
            Write-Output "PARITY: MATCH ($FluidParityMd5)"
        } else {
            Write-Output "PARITY: DIFFERS (expected $FluidParityMd5, got $fluidMd5)"
            $anyDiff = $true
        }
        $results['we-look-live'] = $fluidMd5
    }

    # Acid-side presets.
    foreach ($p in $Presets) {
        $iniPath = Resolve-IniPath $p
        if (-not (Test-Path $iniPath)) { continue }  # e.g. "if present" defaults
        $name = [System.IO.Path]::GetFileNameWithoutExtension($iniPath)
        $safeName = $name -replace '[\\/:*?"<>|]', '_'
        $outPng = Join-Path $ScratchDir "$safeName.png"
        $md5 = Get-PresetMd5 -Name $name -IniPath $iniPath -OutPng $outPng
        if ($null -eq $md5) {
            $anyDiff = $true
            continue
        }
        Write-Output "$name $md5"
        $results[$name] = $md5
    }

    if ($Save) {
        $savePath = if ([System.IO.Path]::IsPathRooted($Save)) { $Save } else { Join-Path $root $Save }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $savePath) | Out-Null
        $lines = foreach ($k in $results.Keys) { "$k=$($results[$k])" }
        Set-Content -Encoding utf8 -Path $savePath -Value $lines
        Write-Output "saved baseline -> $savePath"
    }

    if ($Baseline) {
        $basePath = if ([System.IO.Path]::IsPathRooted($Baseline)) { $Baseline } else { Join-Path $root $Baseline }
        if (-not (Test-Path $basePath)) { throw "Baseline file not found: $basePath" }
        $baseMap = @{}
        foreach ($line in Get-Content $basePath) {
            if ($line -match '^\s*([^=]+)=(.+?)\s*$') { $baseMap[$Matches[1]] = $Matches[2] }
        }
        foreach ($k in $results.Keys) {
            if (-not $baseMap.ContainsKey($k)) {
                Write-Output "$k SKIPPED (not in baseline)"
                continue
            }
            if ($results[$k] -eq $baseMap[$k]) {
                Write-Output "$k MATCH"
            } else {
                Write-Output "$k DIFFERS (baseline=$($baseMap[$k]) actual=$($results[$k]))"
                $anyDiff = $true
            }
        }
    }

    if ($anyDiff) { exit 1 } else { exit 0 }
} finally {
    if ($locked) { Release-GpuLock }
}
