# cycle-install.ps1 -- install the shipped cycle (brief FINAL-CYCLE.md B.6).
# Copies the [cycle] block of reference\configs\cycle-final.ini into
# %APPDATA%\FluidWallpaper\settings.ini (replacing any [cycle] section there), with every
# stage_N_file / stage_N_base made ABSOLUTE (resolved against the cycle ini's folder), so
# the stages load the repo's files in place. settings.ini is backed up first
# (settings.ini.bak-cycle-<time>) and keeps its encoding. `current` is dropped (the cycle
# starts on a fresh draw).
#
# RUN BY THE MAINTAINER, OUTSIDE THE CLAUDE DESKTOP APP (its MSIX virtualization would put
# the write in a shadow copy of %APPDATA% the wallpaper never reads). No hot reload: the
# wallpaper picks the cycle up at its next start.
#   powershell -ExecutionPolicy Bypass -File tools\cycle-install.ps1 [-CycleIni <ini>] [-WhatIf]
param(
    [string]$CycleIni = (Join-Path $PSScriptRoot '..\reference\configs\cycle-final.ini'),
    [string]$Settings = (Join-Path $env:APPDATA 'FluidWallpaper\settings.ini'),
    [switch]$WhatIf
)
$ErrorActionPreference = 'Stop'
$CycleIni = (Resolve-Path $CycleIni).Path
$folder = Split-Path -Parent $CycleIni

# 1. the [cycle] block, paths made absolute
$block = New-Object System.Collections.Generic.List[string]
$inCycle = $false
foreach ($line in (Get-Content -LiteralPath $CycleIni)) {
    $t = $line.Trim()
    if ($t -match '^\[(.+)\]$') { $inCycle = ($Matches[1] -ieq 'cycle'); if ($inCycle) { $block.Add('[cycle]') }; continue }
    if (-not $inCycle -or $t -eq '' -or $t.StartsWith(';')) { continue }
    if ($t -match '^(stage_\d+_(file|base))\s*=\s*(.+)$') {
        $v = $Matches[3].Trim()
        if (-not ([IO.Path]::IsPathRooted($v))) { $v = [IO.Path]::GetFullPath((Join-Path $folder $v)) }
        if (-not (Test-Path -LiteralPath $v)) { throw "stage file missing: $v" }
        $block.Add("$($Matches[1])=$v")
    } elseif ($t -notmatch '^current\s*=') {
        $block.Add(($t -replace '\s*=\s*', '='))
    }
}
if ($block.Count -lt 2) { throw "no [cycle] section in $CycleIni" }

# 2. settings.ini without its old [cycle] section, same encoding
$enc = [Text.Encoding]::Default
$old = @()
if (Test-Path -LiteralPath $Settings) {
    $bytes = [IO.File]::ReadAllBytes($Settings)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) { $enc = [Text.Encoding]::Unicode }
    elseif ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) { $enc = New-Object Text.UTF8Encoding($true) }
    $old = $enc.GetString($bytes).TrimStart([char]0xFEFF) -split "`r?`n"
}
$out = New-Object System.Collections.Generic.List[string]
$skip = $false
foreach ($line in $old) {
    if ($line.Trim() -match '^\[(.+)\]$') { $skip = ($Matches[1] -ieq 'cycle') }
    if (-not $skip) { $out.Add($line) }
}
while ($out.Count -gt 0 -and $out[$out.Count - 1].Trim() -eq '') { $out.RemoveAt($out.Count - 1) }
$out.Add('')
$out.AddRange($block)
$text = ($out -join "`r`n") + "`r`n"

if ($WhatIf) { Write-Output "would write $($block.Count - 1) [cycle] keys to $Settings"; $block; return }
if (Test-Path -LiteralPath $Settings) {
    Copy-Item -LiteralPath $Settings -Destination ("$Settings.bak-cycle-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
} else {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Settings) | Out-Null
}
$pre = $enc.GetPreamble()
[IO.File]::WriteAllBytes($Settings, [byte[]]($pre + $enc.GetBytes($text)))
Write-Output "installed $($block.Count - 1) [cycle] keys from $CycleIni into $Settings (restart the wallpaper)"
