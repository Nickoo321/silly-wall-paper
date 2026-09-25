# Planted-drift test for tools\preset-identity.ps1's baseline diff (no exe, no GPU).
#
# Pulls the pure helper functions out of preset-identity.ps1 by name (AST, so
# the script's param block / render loop never runs), writes a v2 baseline
# and a v1 baseline into a temp dir, feeds each a fake result set with one
# planted CHANGED, one MISSING and one NEW row, and asserts every status.
# Also checks that an overlay composition is written without a BOM and that
# a base/overlay section collision is reported.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\preset-identity-diff.ps1
# Exit code: 0 = all assertions pass, 1 = any failure.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$target = Join-Path $root 'tools\preset-identity.ps1'

$tokens = $null
$errs = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($target, [ref]$tokens, [ref]$errs)
if ($errs -and $errs.Count -gt 0) {
    Write-Output "FAIL parse errors in preset-identity.ps1: $($errs[0].Message)"
    exit 1
}
$want = @('Get-IdentityKey', 'Read-IdentityBaseline', 'Compare-IdentityResults', 'Get-IniSections', 'New-ComposedIni')
$fns = $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)
$found = @()
foreach ($f in $fns) {
    if ($want -contains $f.Name) {
        . ([ScriptBlock]::Create($f.Extent.Text))
        $found += $f.Name
    }
}
foreach ($w in $want) {
    if ($found -notcontains $w) { Write-Output "FAIL function $w not found in preset-identity.ps1"; exit 1 }
}

$script:fail = 0
$script:pass = 0
function Assert-Eq {
    param($Actual, $Expected, [string]$What)
    if ([string]$Actual -eq [string]$Expected) {
        $script:pass++
        Write-Output "PASS $What"
    } else {
        $script:fail++
        Write-Output "FAIL $What (expected '$Expected', got '$Actual')"
    }
}
function Get-Status {
    param($Diff, [string]$Key)
    $hit = @($Diff | Where-Object { $_.Key -eq $Key })
    if ($hit.Count -ne 1) { return "<$($hit.Count) rows>" }
    return $hit[0].Status
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('preset-identity-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
try {
    $P = '10E36EBF1A74EDFE609065D757300054'   # parity
    $A = '9F1382C816EB1E9ABCD893DC8DCFA5C7'
    $B = '3E9A3AB31907718D58C404A3EC8920CD'
    $C = 'B46B448A1441046719D0D8AA8732614D'
    $D = '6ED262A3343F5713C23B424D8C16DF80'
    $X = 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'   # planted drift
    $N = 'BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB'

    # ---- v2 baseline --------------------------------------------------------
    $v2 = Join-Path $tmp 'baseline-v2.txt'
    $v2Lines = @(
        '# exe-sha256 0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF',
        '# git-head 3215bc9',
        '# tier fast',
        "$P`treference\configs\we-look-live.ini",
        "$A`treference\configs\acid-rise-12.ini",
        "$B`treference\presets\Mirror - quad (overlay).ini",
        "$C`treference\configs\ink-paper.ini",
        "$D`treference\presets\Liquid Acid A.ini"
    )
    [System.IO.File]::WriteAllText($v2, (($v2Lines -join "`r`n") + "`r`n"), $utf8)
    $base = Read-IdentityBaseline -Path $v2
    Assert-Eq $base.Version 2 'v2: version detected'
    Assert-Eq $base.Tier 'fast' 'v2: tier header'
    Assert-Eq $base.GitHead '3215bc9' 'v2: git-head header'
    Assert-Eq $base.Entries.Count 5 'v2: 5 entries parsed'
    Assert-Eq $base.Entries['reference\presets\Mirror - quad (overlay).ini'] $B 'v2: path with spaces/parens keyed'

    $results = [ordered]@{}
    $results['reference\configs\we-look-live.ini'] = $P                 # UNCHANGED
    $results['reference\configs\acid-rise-12.ini'] = $X                 # CHANGED (planted)
    $results['reference\presets\Mirror - quad (overlay).ini'] = $B.ToLowerInvariant()  # UNCHANGED (case-insensitive md5)
    $results['reference\configs\ink-paper.ini'] = $C                    # UNCHANGED
    # reference\presets\Liquid Acid A.ini not rendered                  -> MISSING
    $results['reference\configs\monotone-post-0924.ini'] = $N           # NEW

    $diff = @(Compare-IdentityResults -BaselineEntries $base.Entries -Results $results)
    Assert-Eq $diff.Count 6 'v2: 6 diff rows'
    Assert-Eq (Get-Status $diff 'reference\configs\we-look-live.ini') 'UNCHANGED' 'v2: parity UNCHANGED'
    Assert-Eq (Get-Status $diff 'reference\configs\acid-rise-12.ini') 'CHANGED' 'v2: planted drift CHANGED'
    Assert-Eq (Get-Status $diff 'reference\presets\Mirror - quad (overlay).ini') 'UNCHANGED' 'v2: lower-case md5 UNCHANGED'
    Assert-Eq (Get-Status $diff 'reference\configs\ink-paper.ini') 'UNCHANGED' 'v2: ink-paper UNCHANGED'
    Assert-Eq (Get-Status $diff 'reference\presets\Liquid Acid A.ini') 'MISSING' 'v2: unrendered row MISSING'
    Assert-Eq (Get-Status $diff 'reference\configs\monotone-post-0924.ini') 'NEW' 'v2: extra row NEW'
    $changed = @($diff | Where-Object { $_.Status -eq 'CHANGED' })
    Assert-Eq $changed[0].Baseline $A 'v2: CHANGED carries the baseline md5'
    Assert-Eq $changed[0].Actual $X 'v2: CHANGED carries the actual md5'
    $fails = @($diff | Where-Object { $_.Status -eq 'CHANGED' -or $_.Status -eq 'MISSING' }).Count
    Assert-Eq $fails 2 'v2: exactly 2 failing rows (CHANGED + MISSING)'

    # Clean run against the same baseline: nothing fails.
    $clean = [ordered]@{}
    foreach ($k in $base.Entries.Keys) { $clean[$k] = $base.Entries[$k] }
    $diffClean = @(Compare-IdentityResults -BaselineEntries $base.Entries -Results $clean)
    Assert-Eq @($diffClean | Where-Object { $_.Status -ne 'UNCHANGED' }).Count 0 'v2: identical run is all UNCHANGED'

    # ---- v1 baseline (the v1 script's name=md5 with a UTF-8 BOM) -------------
    $v1 = Join-Path $tmp 'baseline-v1.txt'
    $v1Lines = @(
        "we-look-live=$P",
        "acid-rise-12=$A",
        "acid-rise-2hue=$B",
        "Liquid Acid - rising colours (camera medium)=$C"
    )
    [System.IO.File]::WriteAllText($v1, (($v1Lines -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($true)))
    $base1 = Read-IdentityBaseline -Path $v1
    Assert-Eq $base1.Version 1 'v1: version detected'
    Assert-Eq $base1.Entries.Count 4 'v1: 4 entries parsed (BOM stripped)'
    Assert-Eq $base1.Entries['we-look-live'] $P 'v1: first line keyed without the BOM'

    $r1 = [ordered]@{}
    $r1['reference\configs\we-look-live.ini'] = $P                                        # UNCHANGED by name
    $r1['reference\configs\acid-rise-12.ini'] = $X                                        # CHANGED by name
    $r1['reference\presets\Liquid Acid - rising colours (camera medium).ini'] = $C        # UNCHANGED by name
    $r1['reference\presets\Ink - inverted.ini'] = $N                                      # NEW
    # acid-rise-2hue not rendered                                                         -> MISSING
    $diff1 = @(Compare-IdentityResults -BaselineEntries $base1.Entries -Results $r1)
    Assert-Eq (Get-Status $diff1 'reference\configs\we-look-live.ini') 'UNCHANGED' 'v1: name matches path UNCHANGED'
    Assert-Eq (Get-Status $diff1 'reference\configs\acid-rise-12.ini') 'CHANGED' 'v1: planted drift CHANGED'
    Assert-Eq (Get-Status $diff1 'reference\presets\Liquid Acid - rising colours (camera medium).ini') 'UNCHANGED' 'v1: tray name with parens UNCHANGED'
    Assert-Eq (Get-Status $diff1 'acid-rise-2hue') 'MISSING' 'v1: unrendered name MISSING'
    Assert-Eq (Get-Status $diff1 'reference\presets\Ink - inverted.ini') 'NEW' 'v1: extra row NEW'

    # ---- bare "md5 path" v1 lines --------------------------------------------
    $v1b = Join-Path $tmp 'baseline-v1b.txt'
    [System.IO.File]::WriteAllText($v1b, "$A reference\configs\acid-rise-12.ini`r`n$B  reference/presets/Liquid Acid A.ini`r`n", $utf8)
    $base1b = Read-IdentityBaseline -Path $v1b
    Assert-Eq $base1b.Version 1 'v1 md5-path: version detected'
    Assert-Eq $base1b.Entries['reference\presets\Liquid Acid A.ini'] $B 'v1 md5-path: forward slashes normalised'

    # ---- overlay composition -------------------------------------------------
    $baseIni = Join-Path $tmp 'base.ini'
    $overIni = Join-Path $tmp 'over.ini'
    $outIni = Join-Path $tmp 'composed\out.ini'
    [System.IO.File]::WriteAllText($baseIni, "[look]`r`nstyle=liquid_acid`r`n[post]`r`nbloom=0.30", (New-Object System.Text.UTF8Encoding($true)))
    [System.IO.File]::WriteAllText($overIni, "; overlay`r`n[mirror]`r`nmode=3`r`n", (New-Object System.Text.UTF8Encoding($true)))
    $col = @(New-ComposedIni -BasePath $baseIni -OverlayPath $overIni -OutPath $outIni)
    $bytes = [System.IO.File]::ReadAllBytes($outIni)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    Assert-Eq $hasBom $false 'compose: no byte-order mark'
    $text = [System.IO.File]::ReadAllText($outIni)
    Assert-Eq ($text.IndexOf([char]0xFEFF) -lt 0) $true 'compose: no BOM mid-file either'
    Assert-Eq ($text -match "bloom=0\.30`r`n; overlay`r`n\[mirror\]") $true 'compose: base then overlay, newline-joined'
    Assert-Eq $col.Count 0 'compose: no collision for [mirror] on an acid base'
    [System.IO.File]::WriteAllText($overIni, "[post]`r`nbloom=0`r`n", $utf8)
    $col2 = @(New-ComposedIni -BasePath $baseIni -OverlayPath $overIni -OutPath $outIni)
    Assert-Eq ($col2 -join ',') 'post' 'compose: [post] collision reported'
} finally {
    Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue
}

Write-Output ''
Write-Output "preset-identity-diff: $($script:pass) passed, $($script:fail) failed"
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
