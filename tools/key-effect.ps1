# Per-key "does this key do anything" test (brief BJ, NEXT-rings-grain-aberration-refraction.md,
# from executor reviews 4/5: "silent no-ops are the worry -- keys get tuned, audited and argued
# over with no check that they move a pixel").
#
# Renders a BASELINE shot of the ini as shipped, then for every key in the chosen [section]s
# whose LIVE value differs from its parsed DEFAULT, writes a temp ini with just that key reset
# to default and renders again. Two renders that hash identically mean the key painted nothing
# on this frame (INERT); otherwise the whole-frame mean absolute difference (8-bit, 0-255) is
# reported so a merely-subtle key can be told apart from a genuinely dead one.
#
# Defaults are parsed from src\fluid.h: first a direct name match (ini key "meniscus_film_mix"
# -> struct field "meniscusFilmMix", the codebase's own convention), then, when that fails
# (irregular field names like swarm_scale_holes -> swarmScaleA), from the settings.cpp slider/
# checkbox table, which gives the exact field a key is bound to. Keys with no default found
# either way (string enums, colour triples/arrays, anything not exposed as a simple scalar) are
# skipped and listed separately -- this is expected, not an error.
#
# Usage:
#   tools\key-effect.ps1 -Exe <path> -Ini <preset.ini> [-Keys a,b,c]
#       [-Sections liquid_acid,post] [-Size 1280x720] [-Delay 10] [-Out <dir>] [-ScratchDir <dir>]
#
# GPU lock: held ONCE for the whole run (every render in the loop shares it), released at the end.
# The exe is a GUI app: Start-Process -PassThru + WaitForExit(), never `& exe` (that returns
# immediately, before the frame is even captured).

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Ini,
    [string[]]$Keys = @(),
    [string[]]$Sections = @('liquid_acid', 'post'),
    [string]$Size = '1280x720',
    [double]$Delay = 10,
    [string]$Out = '',
    [string]$ScratchDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'tools\gpu-lock.ps1')

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
if (-not (Test-Path $Exe)) { throw "Exe not found: $Exe" }
$exePath = (Resolve-Path $Exe).Path

$iniPath = if ([IO.Path]::IsPathRooted($Ini)) { $Ini } else { Join-Path $root $Ini }
if (-not (Test-Path $iniPath)) { throw "Ini not found: $iniPath" }
$iniPath = (Resolve-Path $iniPath).Path
$presetName = [IO.Path]::GetFileNameWithoutExtension($iniPath)

if (-not $Out) { $Out = Join-Path $root 'build2\shots\live' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

if (-not $ScratchDir) { $ScratchDir = Join-Path $env:TEMP "key-effect\$presetName" }
New-Item -ItemType Directory -Force -Path $ScratchDir | Out-Null

$headHash = (& git -C $root rev-parse --short HEAD 2>$null)
if (-not $headHash) { $headHash = 'nogit' }
$headHash = $headHash.Trim()

$fluidHPath = Join-Path $root 'src\fluid.h'
$settingsCppPath = Join-Path $root 'src\settings.cpp'
if (-not (Test-Path $fluidHPath)) { throw "Not found: $fluidHPath" }
if (-not (Test-Path $settingsCppPath)) { throw "Not found: $settingsCppPath" }

# ---------------------------------------------------------------------------
# src\fluid.h struct ranges, so a field name is looked up in the RIGHT config
# struct (several structs reuse field/key names, e.g. "vignette").
# ---------------------------------------------------------------------------
$fluidLines = Get-Content -Path $fluidHPath
$structBounds = @{}
$curName = $null
$curStart = 0
for ($i = 0; $i -lt $fluidLines.Count; $i++) {
    $m = [regex]::Match($fluidLines[$i], '^struct\s+(\w+)\s*\{')
    if ($m.Success) {
        $curName = $m.Groups[1].Value
        $curStart = $i + 1   # 1-based line number of the struct's first body line
    } elseif ($curName -and $fluidLines[$i] -match '^\};') {
        $structBounds[$curName] = @{ Start = $curStart; End = $i + 1 }
        $curName = $null
    }
}

$sectionStruct = @{
    'liquid_acid' = 'LiquidAcidConfig'
    'post'        = 'PostConfig'
    'ink'         = 'InkConfig'
    'drops'       = 'DropConfig'
}

function Get-FieldDefault {
    param([string]$StructName, [string]$FieldName)
    if (-not $StructName -or -not $structBounds.ContainsKey($StructName)) { return $null }
    $b = $structBounds[$StructName]
    $blockLines = $fluidLines[($b.Start - 1)..($b.End - 1)]
    $block = [string]::Join("`n", $blockLines)
    $pattern = '(?<![\w])' + [regex]::Escape($FieldName) + '\s*(\[\s*\d+\s*\])?\s*=\s*([^,;{}]+)[,;]'
    $m = [regex]::Match($block, $pattern)
    if ($m.Success) { return $m.Groups[2].Value.Trim() }
    return $null
}

function Get-CamelCase {
    param([string]$Snake)
    $parts = $Snake -split '_'
    $result = $parts[0]
    for ($i = 1; $i -lt $parts.Count; $i++) {
        if ($parts[$i].Length -gt 0) {
            $result += $parts[$i].Substring(0, 1).ToUpper() + $parts[$i].Substring(1)
        }
    }
    return $result
}

function Normalize-Default {
    param([string]$Raw)
    $v = $Raw.Trim()
    if ($v -match '^(true)$') { return '1' }
    if ($v -match '^(false)$') { return '0' }
    $v = $v -replace '(?i)f$', ''
    return $v.Trim()
}

# ---------------------------------------------------------------------------
# settings.cpp slider/checkbox table: (section,key) -> (struct-object, field),
# the fallback source of truth for keys whose ini name doesn't camelCase onto
# the fluid.h field name directly (e.g. swarm_scale_holes -> swarmScaleA).
# ---------------------------------------------------------------------------
$objStruct = @{ 'acid' = 'LiquidAcidConfig'; 'post' = 'PostConfig'; 'ink' = 'InkConfig'; 'drops' = 'DropConfig' }
$sliderMap = @{}
foreach ($line in Get-Content -Path $settingsCppPath) {
    $ptrMatch = [regex]::Match($line, '&c\.(\w+)\.(\w+)')
    if (-not $ptrMatch.Success) { continue }
    $pairs = [regex]::Matches($line, 'L"([^"]*)"\s*,\s*L"([^"]*)"')
    foreach ($p in $pairs) {
        $sec = $p.Groups[1].Value
        $key = $p.Groups[2].Value
        if ($sectionStruct.ContainsKey($sec)) {
            $mapKey = "$sec|$key"
            if (-not $sliderMap.ContainsKey($mapKey)) {
                $sliderMap[$mapKey] = @{ Obj = $ptrMatch.Groups[1].Value; Field = $ptrMatch.Groups[2].Value }
            }
        }
    }
}

function Resolve-KeyDefault {
    param([string]$Section, [string]$Key)
    $structName = $sectionStruct[$Section]
    $camel = Get-CamelCase $Key
    $raw = Get-FieldDefault -StructName $structName -FieldName $camel
    if ($null -eq $raw) {
        $mapKey = "$Section|$Key"
        if ($sliderMap.ContainsKey($mapKey)) {
            $entry = $sliderMap[$mapKey]
            $objName = if ($objStruct.ContainsKey($entry.Obj)) { $objStruct[$entry.Obj] } else { $null }
            if ($objName) { $raw = Get-FieldDefault -StructName $objName -FieldName $entry.Field }
        }
    }
    if ($null -eq $raw) { return $null }
    return Normalize-Default $raw
}

function Values-Equal {
    param([string]$A, [string]$B)
    $da = 0.0; $db = 0.0
    if ([double]::TryParse($A, [ref]$da) -and [double]::TryParse($B, [ref]$db)) {
        return [math]::Abs($da - $db) -lt 1e-9
    }
    return $A.Trim() -eq $B.Trim()
}

# ---------------------------------------------------------------------------
# Parse the ini into raw lines + a (section,key) -> lineIndex/value map, so a
# temp ini can be built by patching exactly one line.
# ---------------------------------------------------------------------------
$iniLines = Get-Content -Path $iniPath
$iniMap = @{}   # "$section|$key" -> @{ Line = idx; Value = string }
$curSection = ''
for ($i = 0; $i -lt $iniLines.Count; $i++) {
    $line = $iniLines[$i]
    $sm = [regex]::Match($line, '^\s*\[(\w+)\]\s*$')
    if ($sm.Success) { $curSection = $sm.Groups[1].Value; continue }
    $km = [regex]::Match($line, '^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*(;.*)?$')
    if ($km.Success -and $curSection) {
        $k = $km.Groups[1].Value
        $v = $km.Groups[2].Value
        $iniMap["$curSection|$k"] = @{ Line = $i; Value = $v }
    }
}

# ---------------------------------------------------------------------------
# Build the candidate key list: explicit -Keys (searched across -Sections in
# order), else every key present in each of -Sections.
# ---------------------------------------------------------------------------
$candidates = New-Object System.Collections.Generic.List[object]   # @{Section;Key}
$notInIni = New-Object System.Collections.Generic.List[string]
if ($Keys.Count -gt 0) {
    foreach ($k in $Keys) {
        $found = $false
        foreach ($sec in $Sections) {
            if ($iniMap.ContainsKey("$sec|$k")) {
                $candidates.Add(@{ Section = $sec; Key = $k })
                $found = $true
                break
            }
        }
        if (-not $found) { $notInIni.Add($k) }
    }
} else {
    foreach ($sec in $Sections) {
        foreach ($mapKey in $iniMap.Keys) {
            $parts = $mapKey.Split('|', 2)
            if ($parts[0] -eq $sec) { $candidates.Add(@{ Section = $sec; Key = $parts[1] }) }
        }
    }
}

# ---------------------------------------------------------------------------
# Resolve defaults; split into testable (live != default) and no-default.
# ---------------------------------------------------------------------------
$testable = New-Object System.Collections.Generic.List[object]
$noDefault = New-Object System.Collections.Generic.List[string]
$atDefault = New-Object System.Collections.Generic.List[string]
foreach ($c in $candidates) {
    $live = $iniMap["$($c.Section)|$($c.Key)"].Value
    $def = Resolve-KeyDefault -Section $c.Section -Key $c.Key
    if ($null -eq $def) {
        $noDefault.Add("$($c.Key) ($($c.Section))")
        continue
    }
    if (Values-Equal $live $def) {
        $atDefault.Add("$($c.Key) ($($c.Section)) = $live")
        continue
    }
    $testable.Add(@{ Section = $c.Section; Key = $c.Key; Live = $live; Default = $def })
}

Write-Output "key-effect: $($candidates.Count) keys in scope, $($testable.Count) testable (live != default), $($noDefault.Count) with no default found, $($atDefault.Count) already at default"

# ---------------------------------------------------------------------------
# Image diff helper (compiled once, native speed: a pure-PowerShell byte loop
# over a 1280x720x3 frame per key would be the real bottleneck otherwise).
# ---------------------------------------------------------------------------
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class KeyEffectImgDiff {
    public static double MeanAbsDiff(string pathA, string pathB) {
        using (Bitmap a = new Bitmap(pathA))
        using (Bitmap b = new Bitmap(pathB)) {
            int w = Math.Min(a.Width, b.Width);
            int h = Math.Min(a.Height, b.Height);
            Rectangle rect = new Rectangle(0, 0, w, h);
            BitmapData da = a.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            BitmapData db = b.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                int bytesPerRow = w * 3;
                byte[] rowA = new byte[bytesPerRow];
                byte[] rowB = new byte[bytesPerRow];
                long total = 0, count = 0;
                for (int y = 0; y < h; y++) {
                    Marshal.Copy(IntPtr.Add(da.Scan0, y * da.Stride), rowA, 0, bytesPerRow);
                    Marshal.Copy(IntPtr.Add(db.Scan0, y * db.Stride), rowB, 0, bytesPerRow);
                    for (int i = 0; i < bytesPerRow; i++) {
                        int diff = rowA[i] - rowB[i];
                        if (diff < 0) diff = -diff;
                        total += diff;
                        count++;
                    }
                }
                return (double)total / (double)count;
            } finally {
                a.UnlockBits(da);
                b.UnlockBits(db);
            }
        }
    }
}
"@

function Wait-StablePng {
    # Bounded at 20 min: a render that never lands must not hang the caller.
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

function Invoke-Shot {
    param([string]$RenderIniPath, [string]$OutPng)
    if (Test-Path $OutPng) { Remove-Item $OutPng -Force -ErrorAction SilentlyContinue }
    $argStr = '--shot "{0}" --ini "{1}" --hdr on --shot-delay {2} --shot-size {3} --shot-yield 8 --seed 1234' -f `
        $OutPng, $RenderIniPath, $Delay, $Size
    $p = Start-Process -FilePath $exePath -ArgumentList $argStr -PassThru
    $p.WaitForExit()
    if (-not (Wait-StablePng -Path $OutPng)) { return $false }
    return $true
}

# ---------------------------------------------------------------------------
# Render loop: GPU lock held once for the whole run.
# ---------------------------------------------------------------------------
$locked = $false
$rows = New-Object System.Collections.Generic.List[object]
$timedOut = New-Object System.Collections.Generic.List[string]
try {
    $locked = Wait-GpuLock -Owner 'key-effect' -TimeoutMinutes 40
    if (-not $locked) {
        Write-Output "key-effect: GPU lock not acquired within 40 min -- aborting, no renders performed."
        exit 1
    }

    $baselinePng = Join-Path $ScratchDir "$presetName-baseline.png"
    Write-Output "key-effect: rendering baseline -> $baselinePng"
    if (-not (Invoke-Shot -RenderIniPath $iniPath -OutPng $baselinePng)) {
        throw "Baseline render timed out (no stable PNG after 20 min): $baselinePng"
    }
    $baselineMd5 = (Get-FileHash -Algorithm MD5 -Path $baselinePng).Hash

    $n = 0
    foreach ($t in $testable) {
        $n++
        $key = $t.Key
        $section = $t.Section
        Write-Output "key-effect: [$n/$($testable.Count)] $section.$key  live=$($t.Live) default=$($t.Default)"

        $tempIniPath = Join-Path $ScratchDir "$presetName-$key.ini"
        $tempLines = [System.Collections.ArrayList]::new($iniLines)
        $lineIdx = $iniMap["$section|$key"].Line
        $tempLines[$lineIdx] = "$key = $($t.Default)"
        Set-Content -Encoding utf8 -Path $tempIniPath -Value $tempLines

        $keyPng = Join-Path $ScratchDir "$presetName-$key.png"
        if (-not (Invoke-Shot -RenderIniPath $tempIniPath -OutPng $keyPng)) {
            $timedOut.Add("$key ($section)")
            continue
        }

        $keyMd5 = (Get-FileHash -Algorithm MD5 -Path $keyPng).Hash
        $inert = ($keyMd5 -eq $baselineMd5)
        $mad = 0.0
        if (-not $inert) {
            $mad = [KeyEffectImgDiff]::MeanAbsDiff($baselinePng, $keyPng)
        }

        $note = @()
        if ($inert) {
            $note += 'INERT on this frame'
        } elseif ($mad -lt 0.05) {
            $note += 'no visible effect on this frame'
        }
        if ($key -match '(?i)(period|drift|fps|wobble|readjust|rise)') {
            $note += 'time/motion-named key: a 10 s still at 720p cannot show a slow or periodic effect'
        }

        $rows.Add([PSCustomObject]@{
            Key     = $key
            Section = $section
            Live    = $t.Live
            Default = $t.Default
            Inert   = $inert
            Mad     = $mad
            Note    = ($note -join '; ')
        })
    }
} finally {
    if ($locked) { Release-GpuLock }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$sorted = $rows | Sort-Object Mad
$inertCount = ($rows | Where-Object { $_.Inert }).Count
$lowMadCount = ($rows | Where-Object { -not $_.Inert -and $_.Mad -lt 0.05 }).Count

$reportPath = Join-Path $Out "key-effect-$presetName-$headHash.md"
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Key-effect report -- $presetName @ $headHash")
$md.Add("")
$md.Add("Exe: ``$exePath``")
$md.Add("Ini: ``$iniPath``")
$md.Add("Sections tested: $($Sections -join ', ')")
$md.Add("Size: $Size, delay: ${Delay}s, seed 1234, --hdr on, --shot-yield 8")
$md.Add("Baseline: ``$baselinePng`` md5 ``$baselineMd5``")
$md.Add("")
$md.Add("## Results (sorted by MAD ascending)")
$md.Add("")
$md.Add("| Key | Section | Live | Default | INERT? | MAD | Note |")
$md.Add("|---|---|---|---|---|---|---|")
foreach ($r in $sorted) {
    $madStr = '{0:N3}' -f $r.Mad
    $inertStr = if ($r.Inert) { 'YES' } else { 'no' }
    $md.Add("| $($r.Key) | $($r.Section) | $($r.Live) | $($r.Default) | $inertStr | $madStr | $($r.Note) |")
}
$md.Add("")
$md.Add("**Summary:** $($rows.Count) keys tested, $inertCount INERT (byte-identical), $lowMadCount with MAD < 0.05 ('no visible effect on this frame'), $($noDefault.Count) skipped (no default found), $($atDefault.Count) already at default (not tested), $($timedOut.Count) render timeouts.")

if ($noDefault.Count -gt 0) {
    $md.Add("")
    $md.Add("## Keys skipped -- no default found")
    $md.Add("")
    foreach ($k in $noDefault) { $md.Add("- $k") }
}
if ($timedOut.Count -gt 0) {
    $md.Add("")
    $md.Add("## Render timeouts (no stable PNG after 20 min)")
    $md.Add("")
    foreach ($k in $timedOut) { $md.Add("- $k") }
}
if ($notInIni.Count -gt 0) {
    $md.Add("")
    $md.Add("## -Keys not found in the chosen sections")
    $md.Add("")
    foreach ($k in $notInIni) { $md.Add("- $k") }
}

Set-Content -Encoding utf8 -Path $reportPath -Value $md
Write-Output "key-effect: report -> $reportPath"
