# Preset identity check: renders every ini the project ships (driven by
# tools\preset-identity.manifest.psd1) headless with a given exe, md5's the
# PNGs, and either saves them as a baseline or diffs them against one.
#
# The fluid look (reference\configs\we-look-live.ini) is ALWAYS rendered
# first and reported separately: its md5 must equal the fluid parity
# constant below regardless of -Tier/-Presets/-Baseline, because that check
# is sacred (EXECUTOR-CARD.md hard rules) and independent of whatever
# presets are being audited. Its manifest row is never rendered a second time.
#
# Usage:
#   tools\preset-identity.ps1 -Exe <path> [-Tier fast|full|all] [-Manifest <psd1>]
#                             [-Baseline <file>] [-Save <file>] [-Presets <list>]
#                             [-Delay <s>] [-ScratchDir <dir>] [-Yield <ms>]
#   tools\preset-identity.ps1 -DryRun [-Tier ...]      # no exe, no GPU: lists what would render
#
# Tiers (manifest column Tier): fast = the fast rows (~15, default);
# full = fast + full rows; all = every row including skip (test inis).
# -Presets <paths> renders exactly those inis instead of a tier (their
# manifest row, if any, still supplies the overlay base and the delay).
# -Delay, when passed explicitly, overrides every row's manifest delay
# (and, as before, the parity render's delay: parity is defined at 60 s).
#
# Overlays (manifest Base non-empty, e.g. reference\presets\Mirror - *.ini)
# are composed as base text + overlay text into a BOM-less temp ini under
# <ScratchDir>\composed\ and that file is rendered. The Win32 profile API
# reads the FIRST section of a given name, so an overlay section that also
# exists in its base would be shadowed: -DryRun flags that as COLLISION.
#
# Baseline format v2 (written by -Save):
#   # exe-sha256 <hash>
#   # git-head <sha>
#   # tier <t>
#   <md5><TAB><repo-relative ini path>
# -Baseline reads v2 and v1 (name=md5 lines as written by the v1 script, or
# bare "md5 path" lines) and prints CHANGED / UNCHANGED / MISSING (in the
# baseline, not rendered) / NEW (rendered, not in the baseline).
#
# Examples:
#   tools\preset-identity.ps1 -Exe build2\FluidWallpaper.exe -Save build2\shots\preset-identity-baseline-abc1234.txt
#   tools\preset-identity.ps1 -Exe build2\FluidWallpaper.exe -Tier full -Baseline build2\shots\preset-identity-baseline-abc1234.txt
#   tools\preset-identity.ps1 -DryRun -Tier all
#
# Exit code: 1 if the fluid parity check fails, or (with -Baseline) on any
# CHANGED or MISSING row. -DryRun exits 1 if an ini on disk has no manifest
# row, a manifest row has no file, or an overlay's base is missing/collides.
# 0 otherwise.

param(
    [string]$Exe = '',
    [string]$Baseline = '',
    [string]$Save = '',
    [string[]]$Presets = @(),
    [double]$Delay = 60,
    [string]$ScratchDir = '',
    [int]$Yield = 8,        # --shot-yield ms per frame; the image does not depend on it
    [ValidateSet('fast', 'full', 'all')][string]$Tier = 'fast',
    [string]$Manifest = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$FluidIni = Join-Path $root 'reference\configs\we-look-live.ini'
$FluidKey = 'reference\configs\we-look-live.ini'
$FluidParityMd5 = '10E36EBF1A74EDFE609065D757300054'
$CoveredDirs = @('reference\configs', 'reference\presets', 'reference\moods')

# ---------------------------------------------------------------------------
# Pure helpers (no GPU, no globals): tools\tests\preset-identity-diff.ps1
# extracts these by name from this file's AST, so keep them self-contained.
# ---------------------------------------------------------------------------

function Get-IdentityKey {
    # Canonical baseline key for an ini: repo-relative, backslashes, no ".\".
    param([string]$Path, [string]$Root = '')
    $p = $Path -replace '/', '\'
    if ($Root -and [System.IO.Path]::IsPathRooted($p)) {
        $full = [System.IO.Path]::GetFullPath($p)
        $r = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
        if ($full.StartsWith($r, [System.StringComparison]::OrdinalIgnoreCase)) {
            $p = $full.Substring($r.Length)
        }
    }
    while ($p.StartsWith('.\')) { $p = $p.Substring(2) }
    return $p
}

function Read-IdentityManifest {
    param([string]$Path)
    $data = Import-PowerShellDataFile -Path $Path
    $rows = @()
    foreach ($r in $data.Rows) {
        $delay = 60
        if ($r.ContainsKey('Delay') -and $null -ne $r.Delay) { $delay = [double]$r.Delay }
        $base = ''
        if ($r.ContainsKey('Base') -and $r.Base) { $base = Get-IdentityKey -Path ([string]$r.Base) }
        $note = ''
        if ($r.ContainsKey('Note') -and $r.Note) { $note = [string]$r.Note }
        $tier = ([string]$r.Tier).ToLowerInvariant()
        if (@('fast', 'full', 'skip') -notcontains $tier) {
            throw "manifest row '$($r.Path)': tier must be fast|full|skip, got '$($r.Tier)'"
        }
        $look = ([string]$r.Look).ToLowerInvariant()
        if (@('fluid', 'acid', 'ink', 'overlay', 'test', 'cycle') -notcontains $look) {
            throw "manifest row '$($r.Path)': look must be fluid|acid|ink|overlay|test|cycle, got '$($r.Look)'"
        }
        $rows += [PSCustomObject]@{
            Path  = (Get-IdentityKey -Path ([string]$r.Path))
            Look  = $look
            Base  = $base
            Delay = $delay
            Tier  = $tier
            Note  = $note
        }
    }
    return $rows
}

function Select-IdentityRows {
    param($Rows, [string]$Tier)
    $want = @('fast')
    if ($Tier -eq 'full') { $want = @('fast', 'full') }
    if ($Tier -eq 'all') { $want = @('fast', 'full', 'skip') }
    return @($Rows | Where-Object { $want -contains $_.Tier })
}

function Get-IniSections {
    param([string]$Text)
    $names = @()
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*\[([^\]]+)\]') { $names += $Matches[1].Trim().ToLowerInvariant() }
    }
    return $names
}

function New-ComposedIni {
    # base + overlay -> $OutPath, UTF-8 WITHOUT a byte-order mark. Returns the
    # overlay section names that also appear in the base. The profile API reads
    # the FIRST section of a name and the FIRST key of a name inside it, so a
    # colliding overlay section is MERGED (brief BW, for the Scheme partials on
    # monotone-post-0924): its key lines are inserted right after the base's
    # first header of that section, where they win over the base's own lines.
    # No collision: plain base + overlay concatenation, exactly as before.
    param([string]$BasePath, [string]$OverlayPath, [string]$OutPath)
    $baseText = [System.IO.File]::ReadAllText($BasePath)       # BOM-aware; a BOM is dropped
    $overText = [System.IO.File]::ReadAllText($OverlayPath)
    if (-not ($baseText.EndsWith("`n"))) { $baseText += "`r`n" }
    $baseSecs = Get-IniSections -Text $baseText
    $collide = @()
    foreach ($s in (Get-IniSections -Text $overText)) {
        if (($baseSecs -contains $s) -and ($collide -notcontains $s)) { $collide += $s }
    }
    if ($collide.Count -eq 0) {
        $text = "; preset-identity composed ini: base + overlay`r`n" + $baseText + $overText
    } else {
        # overlay -> prelude + ordered sections (name, body lines)
        $prelude = New-Object System.Collections.Generic.List[string]
        $secNames = New-Object System.Collections.Generic.List[string]
        $secBody = @{}
        $cur = $null
        foreach ($line in ($overText -split "`r?`n")) {
            if ($line -match '^\s*\[([^\]]+)\]') {
                $cur = $Matches[1].Trim().ToLowerInvariant()
                if (-not $secBody.ContainsKey($cur)) {
                    $secNames.Add($cur)
                    $secBody[$cur] = New-Object System.Collections.Generic.List[string]
                    $secBody[$cur].Add($line)       # the header, for the appended form
                }
                continue
            }
            if ($null -eq $cur) { $prelude.Add($line) } else { $secBody[$cur].Add($line) }
        }
        $out = New-Object System.Collections.Generic.List[string]
        $out.Add('; preset-identity composed ini: base + overlay (colliding sections merged: overlay keys first)')
        $done = @{}
        foreach ($line in ($baseText -split "`r?`n")) {
            $out.Add($line)
            if ($line -match '^\s*\[([^\]]+)\]') {
                $n = $Matches[1].Trim().ToLowerInvariant()
                if (($collide -contains $n) -and -not $done.ContainsKey($n)) {
                    $done[$n] = $true
                    $body = $secBody[$n]
                    for ($k = 1; $k -lt $body.Count; $k++) { if ($body[$k].Trim()) { $out.Add($body[$k]) } }
                }
            }
        }
        foreach ($l in $prelude) { $out.Add($l) }
        foreach ($n in $secNames) {
            if ($collide -contains $n) { continue }
            foreach ($l in $secBody[$n]) { $out.Add($l) }
        }
        $text = ($out -join "`r`n") + "`r`n"
    }
    $dir = Split-Path -Parent $OutPath
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [System.IO.File]::WriteAllText($OutPath, $text, (New-Object System.Text.UTF8Encoding($false)))
    return $collide
}

function Read-IdentityBaseline {
    # v2: "# exe-sha256 / # git-head / # tier" headers + "md5<TAB>path" lines.
    # v1: "name=md5" lines (the v1 script's -Save), or bare "md5 path" lines.
    param([string]$Path)
    $entries = [ordered]@{}
    $headers = @{}
    foreach ($raw in (Get-Content -Path $Path)) {
        $line = $raw.Trim([char]0xFEFF).Trim()
        if (-not $line) { continue }
        if ($line -match '^#\s*([A-Za-z0-9_-]+)\s+(.*)$') {
            $headers[$Matches[1].ToLowerInvariant()] = $Matches[2].Trim()
            continue
        }
        if ($line.StartsWith('#')) { continue }
        if ($line -match '^([0-9A-Fa-f]{32})\s+(.+)$') {
            $entries[(Get-IdentityKey -Path $Matches[2].Trim())] = $Matches[1].ToUpperInvariant()
        } elseif ($line -match '^(.+?)=([0-9A-Fa-f]{32})$') {
            $entries[$Matches[1].Trim()] = $Matches[2].ToUpperInvariant()
        }
    }
    $version = 1
    if ($headers.ContainsKey('exe-sha256') -or $headers.ContainsKey('git-head') -or $headers.ContainsKey('tier')) {
        $version = 2
    }
    $tierHdr = ''
    if ($headers.ContainsKey('tier')) { $tierHdr = $headers['tier'] }
    $exeHdr = ''
    if ($headers.ContainsKey('exe-sha256')) { $exeHdr = $headers['exe-sha256'] }
    $gitHdr = ''
    if ($headers.ContainsKey('git-head')) { $gitHdr = $headers['git-head'] }
    return [PSCustomObject]@{
        Version   = $version
        ExeSha256 = $exeHdr
        GitHead   = $gitHdr
        Tier      = $tierHdr
        Entries   = $entries
    }
}

function Compare-IdentityResults {
    # $BaselineEntries: ordered key->md5 (keys are paths, or v1 bare names).
    # $Results: ordered path-key->md5 of what was rendered this run.
    # A baseline key that is a bare name (no '\', no '.ini') matches a result
    # by file name without extension, so v1 baselines diff against v2 runs.
    param($BaselineEntries, $Results)
    $byName = @{}
    foreach ($k in $Results.Keys) {
        $n = [System.IO.Path]::GetFileNameWithoutExtension($k)
        if (-not $byName.ContainsKey($n)) { $byName[$n] = $k }
    }
    $matched = @{}
    $out = @()
    foreach ($bk in $BaselineEntries.Keys) {
        $isPath = ($bk.Contains('\') -or $bk.ToLowerInvariant().EndsWith('.ini'))
        $rk = $null
        if ($isPath) {
            if ($Results.Contains($bk)) { $rk = $bk }
        } else {
            if ($byName.ContainsKey($bk)) { $rk = $byName[$bk] }
        }
        $bmd5 = ([string]$BaselineEntries[$bk]).ToUpperInvariant()
        if ($null -eq $rk) {
            $out += [PSCustomObject]@{ Key = $bk; Status = 'MISSING'; Baseline = $bmd5; Actual = '' }
            continue
        }
        $matched[$rk] = $true
        $amd5 = ([string]$Results[$rk]).ToUpperInvariant()
        if ($amd5 -eq $bmd5) {
            $out += [PSCustomObject]@{ Key = $rk; Status = 'UNCHANGED'; Baseline = $bmd5; Actual = $amd5 }
        } else {
            $out += [PSCustomObject]@{ Key = $rk; Status = 'CHANGED'; Baseline = $bmd5; Actual = $amd5 }
        }
    }
    foreach ($k in $Results.Keys) {
        if (-not $matched.ContainsKey($k)) {
            $out += [PSCustomObject]@{ Key = $k; Status = 'NEW'; Baseline = ''; Actual = ([string]$Results[$k]).ToUpperInvariant() }
        }
    }
    return $out
}

# ---------------------------------------------------------------------------
# Row selection
# ---------------------------------------------------------------------------

# -Presets was passed explicitly but resolved to zero files (e.g. a glob that matched
# nothing) -- that used to fall through to the default list, silently testing the
# WRONG presets instead of telling the caller their list was empty. Only the
# "supplied but empty" case is an error; omitting -Presets means "use the tier".
if ($PSBoundParameters.ContainsKey('Presets') -and $Presets.Count -eq 0) {
    Write-Error "preset-identity.ps1: -Presets was supplied but resolved to zero files. Pass at least one preset path, or omit -Presets entirely to use the manifest tier."
    exit 1
}

if (-not $Manifest) { $Manifest = Join-Path $root 'tools\preset-identity.manifest.psd1' }
if (-not [System.IO.Path]::IsPathRooted($Manifest)) { $Manifest = Join-Path $root $Manifest }
if (-not (Test-Path $Manifest)) { throw "Manifest not found: $Manifest" }
$allRows = @(Read-IdentityManifest -Path $Manifest)
$rowByKey = @{}
foreach ($r in $allRows) {
    if ($rowByKey.ContainsKey($r.Path)) { throw "manifest: duplicate row for $($r.Path)" }
    $rowByKey[$r.Path] = $r
}

$runTier = $Tier
if ($PSBoundParameters.ContainsKey('Presets')) {
    $runTier = 'presets'
    $rows = @()
    foreach ($p in $Presets) {
        $k = Get-IdentityKey -Path $p -Root $root
        if ($rowByKey.ContainsKey($k)) {
            $rows += $rowByKey[$k]
        } else {
            $rows += [PSCustomObject]@{ Path = $k; Look = '?'; Base = ''; Delay = 60; Tier = 'explicit'; Note = 'not in the manifest' }
        }
    }
} else {
    $rows = Select-IdentityRows -Rows $allRows -Tier $Tier
}

# Parity is rendered first on its own; never as a row.
$rows = @($rows | Where-Object { $_.Path -ne $FluidKey })

$delayOverride = $PSBoundParameters.ContainsKey('Delay')
function Get-RowDelay {
    param($Row)
    if ($delayOverride) { return $Delay }
    return $Row.Delay
}

function Resolve-IniPath {
    param([string]$Ini)
    if ([System.IO.Path]::IsPathRooted($Ini)) { return $Ini }
    return (Join-Path $root $Ini)
}

if (-not $ScratchDir) {
    $ScratchDir = Join-Path $env:TEMP 'preset-identity'
}

function Get-SafeName {
    param([string]$Key)
    return ($Key -replace '[\\/:*?"<>|]', '_')
}

# ---------------------------------------------------------------------------
# -DryRun: classify + list, no exe, no GPU, no lock.
# ---------------------------------------------------------------------------

if ($DryRun) {
    $problems = 0
    Write-Output "DRY RUN  tier=$runTier  manifest=$(Get-IdentityKey -Path $Manifest -Root $root)"
    if ($Exe) {
        if (Test-Path $Exe) {
            Write-Output "exe-sha256 $((Get-FileHash -Algorithm SHA256 -Path $Exe).Hash)"
        } else {
            Write-Output "exe not found (ignored in -DryRun): $Exe"
        }
    }
    Write-Output ("PARITY (first, always)  delay={0}  {1}  expect {2}" -f $Delay, $FluidKey, $FluidParityMd5)
    Write-Output ''
    Write-Output ("{0,-5} {1,-7} {2,5}  {3}" -f 'tier', 'look', 'delay', 'path  [<= base]')
    $composedDir = Join-Path $ScratchDir 'composed-dryrun'
    foreach ($r in $rows) {
        $line = "{0,-5} {1,-7} {2,5}  {3}" -f $r.Tier, $r.Look, (Get-RowDelay $r), $r.Path
        $iniPath = Resolve-IniPath $r.Path
        if (-not (Test-Path -LiteralPath $iniPath)) {
            $line += '   !! FILE NOT FOUND'
            $problems++
        }
        if ($r.Base) {
            $line += "   <= $($r.Base)"
            $basePath = Resolve-IniPath $r.Base
            if (-not (Test-Path -LiteralPath $basePath)) {
                $line += '   !! BASE NOT FOUND'
                $problems++
            } elseif (Test-Path -LiteralPath $iniPath) {
                $tmp = Join-Path $composedDir ((Get-SafeName $r.Path))
                $collide = @(New-ComposedIni -BasePath $basePath -OverlayPath $iniPath -OutPath $tmp)
                if ($collide.Count -gt 0) {
                    $line += "   merged [$($collide -join '],[')] (overlay keys first)"
                }
            }
        }
        Write-Output $line
    }

    # Coverage: every ini on disk under the covered dirs has a manifest row
    # and every manifest row has a file.
    $disk = @()
    foreach ($d in $CoveredDirs) {
        $full = Join-Path $root $d
        if (Test-Path -LiteralPath $full) {
            $disk += @(Get-ChildItem -LiteralPath $full -Recurse -File -Filter '*.ini' |
                Where-Object { $_.Extension -eq '.ini' } |
                ForEach-Object { Get-IdentityKey -Path $_.FullName -Root $root })
        }
    }
    Write-Output ''
    foreach ($k in $disk) {
        if (-not $rowByKey.ContainsKey($k)) { Write-Output "UNCLASSIFIED (on disk, no manifest row): $k"; $problems++ }
    }
    foreach ($r in $allRows) {
        if ($disk -notcontains $r.Path) { Write-Output "STALE (manifest row, no file): $($r.Path)"; $problems++ }
    }
    $nFast = @($allRows | Where-Object { $_.Tier -eq 'fast' }).Count
    $nFull = @($allRows | Where-Object { $_.Tier -eq 'full' }).Count
    $nSkip = @($allRows | Where-Object { $_.Tier -eq 'skip' }).Count
    $nOver = @($allRows | Where-Object { $_.Base }).Count
    Write-Output ("manifest rows {0} (fast {1} / full {2} / skip {3}; overlays {4})  inis on disk {5}" -f `
        $allRows.Count, $nFast, $nFull, $nSkip, $nOver, $disk.Count)
    Write-Output ("would render: parity + {0} rows (tier {1}); problems {2}" -f $rows.Count, $runTier, $problems)
    if ($problems -gt 0) { exit 1 } else { exit 0 }
}

# ---------------------------------------------------------------------------
# Render mode
# ---------------------------------------------------------------------------

if (-not $Exe) { throw "-Exe is required (or pass -DryRun)" }
if (-not (Test-Path $Exe)) { throw "Exe not found: $Exe" }
$exePath = (Resolve-Path $Exe).Path

. (Join-Path $root 'tools\gpu-lock.ps1')

New-Item -ItemType Directory -Force -Path $ScratchDir | Out-Null

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
    param([string]$Name, [string]$IniPath, [string]$OutPng, [double]$ShotDelay)
    if (Test-Path $OutPng) { Remove-Item $OutPng -Force -ErrorAction SilentlyContinue }
    & $exePath --shot $OutPng --ini $IniPath --hdr on --shot-delay $ShotDelay `
        --shot-size 2560x1440 --shot-yield $Yield --seed 1234 2>&1 | Out-Null
    if (-not (Wait-StablePng -Path $OutPng)) {
        Write-Output "$Name TIMEOUT (no stable PNG after 20 min): $OutPng"
        return $null
    }
    return (Get-FileHash -Algorithm MD5 -Path $OutPng).Hash
}

function Get-GitHead {
    $head = 'unknown'
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $h = & git -C $root rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $h) { $head = ([string]$h).Trim() }
    } catch {
        $head = 'unknown'
    } finally {
        $ErrorActionPreference = $old
    }
    return $head
}

$locked = $false
try {
    $locked = Wait-GpuLock -Owner 'identity' -TimeoutMinutes 2
    # proceed even if the wait timed out (user: render at full speed)

    $results = [ordered]@{}
    $anyDiff = $false

    # Fluid parity: always first, always reported separately.
    $fluidOut = Join-Path $ScratchDir 'we-look-live.png'
    $fluidMd5 = Get-PresetMd5 -Name 'we-look-live' -IniPath $FluidIni -OutPng $fluidOut -ShotDelay $Delay
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
        $results[$FluidKey] = $fluidMd5
    }

    # Manifest rows (or -Presets).
    $composedDir = Join-Path $ScratchDir 'composed'
    foreach ($r in $rows) {
        $iniPath = Resolve-IniPath $r.Path
        if (-not (Test-Path -LiteralPath $iniPath)) {
            Write-Output "$($r.Path) SKIPPED (file not found)"
            continue
        }
        $renderIni = $iniPath
        if ($r.Base) {
            $basePath = Resolve-IniPath $r.Base
            if (-not (Test-Path -LiteralPath $basePath)) {
                Write-Output "$($r.Path) SKIPPED (base not found: $($r.Base))"
                continue
            }
            $renderIni = Join-Path $composedDir ((Get-SafeName $r.Path))
            $collide = @(New-ComposedIni -BasePath $basePath -OverlayPath $iniPath -OutPath $renderIni)
            if ($collide.Count -gt 0) {
                Write-Output "$($r.Path): base also has [$($collide -join '],[')] -- merged, overlay keys first"
            }
        }
        $outPng = Join-Path $ScratchDir ((Get-SafeName $r.Path) -replace '\.ini$', '.png')
        $md5 = Get-PresetMd5 -Name $r.Path -IniPath $renderIni -OutPng $outPng -ShotDelay (Get-RowDelay $r)
        if ($null -eq $md5) {
            $anyDiff = $true
            continue
        }
        Write-Output "$md5  $($r.Path)"
        $results[$r.Path] = $md5
    }

    if ($Save) {
        $savePath = if ([System.IO.Path]::IsPathRooted($Save)) { $Save } else { Join-Path $root $Save }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $savePath) | Out-Null
        $lines = @()
        $lines += "# exe-sha256 $((Get-FileHash -Algorithm SHA256 -Path $exePath).Hash)"
        $lines += "# git-head $(Get-GitHead)"
        $lines += "# tier $runTier"
        foreach ($k in $results.Keys) { $lines += "$($results[$k])`t$k" }
        [System.IO.File]::WriteAllText($savePath, (($lines -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
        Write-Output "saved baseline (v2) -> $savePath"
    }

    if ($Baseline) {
        $basePath = if ([System.IO.Path]::IsPathRooted($Baseline)) { $Baseline } else { Join-Path $root $Baseline }
        if (-not (Test-Path $basePath)) { throw "Baseline file not found: $basePath" }
        $base = Read-IdentityBaseline -Path $basePath
        Write-Output "baseline v$($base.Version): $basePath"
        if ($base.Tier -and ($base.Tier -ne $runTier)) {
            Write-Output "WARNING: baseline tier '$($base.Tier)' != this run's tier '$runTier' -- rows outside this run show as MISSING"
        }
        $diff = @(Compare-IdentityResults -BaselineEntries $base.Entries -Results $results)
        foreach ($d in $diff) {
            switch ($d.Status) {
                'CHANGED'   { Write-Output "CHANGED   $($d.Key) (baseline=$($d.Baseline) actual=$($d.Actual))"; $anyDiff = $true }
                'MISSING'   { Write-Output "MISSING   $($d.Key) (in baseline, not rendered)"; $anyDiff = $true }
                'NEW'       { Write-Output "NEW       $($d.Key) $($d.Actual)" }
                default     { Write-Output "UNCHANGED $($d.Key)" }
            }
        }
        $c = @($diff | Where-Object { $_.Status -eq 'CHANGED' }).Count
        $m = @($diff | Where-Object { $_.Status -eq 'MISSING' }).Count
        $n = @($diff | Where-Object { $_.Status -eq 'NEW' }).Count
        $u = @($diff | Where-Object { $_.Status -eq 'UNCHANGED' }).Count
        Write-Output "diff: $u unchanged, $c changed, $m missing, $n new"
    }

    if ($anyDiff) { exit 1 } else { exit 0 }
} finally {
    if ($locked) { Release-GpuLock }
}
