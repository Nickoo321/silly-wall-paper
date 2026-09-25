# keymeta-check.ps1 -- validates src\ui\keys.inc (the settings window's key table + keymeta).
# No GPU, no build, < 1 s. Part of the merge checklist next to tools\slot-check.ps1.
#
# Fails (exit 1) on:
#   - a row whose meta is missing or malformed (group not G_*, looks not a subset of FAI,
#     unknown flag, unknown 'special' form, front not a subset of looks)
#   - a duplicate section.key
#   - a gate that does not parse or names an unknown key (look==F|A|I is the only pseudo-key)
#   - a row whose key string is never read by src\main.cpp / src\cycle.cpp (typo guard)
#   - a measured_inert.inc entry naming an unknown key
# Usage: powershell -File tools\keymeta-check.ps1 [-Root <repo or worktree>]

param([string]$Root = '')
$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Split-Path -Parent $PSScriptRoot }
$inc = Join-Path $Root 'src\ui\keys.inc'
$inert = Join-Path $Root 'src\ui\measured_inert.inc'
$text = [System.IO.File]::ReadAllText($inc)
$mainSrc = [System.IO.File]::ReadAllText((Join-Path $Root 'src\main.cpp')) + [System.IO.File]::ReadAllText((Join-Path $Root 'src\cycle.cpp'))

function Split-Args([string]$s) {
    $out = New-Object System.Collections.Generic.List[string]
    $cur = New-Object System.Text.StringBuilder
    $inStr = $false; $depth = 0
    for ($i = 0; $i -lt $s.Length; $i++) {
        $ch = $s[$i]
        if ($inStr) {
            [void]$cur.Append($ch)
            if ($ch -eq '\') { $i++; [void]$cur.Append($s[$i]); continue }
            if ($ch -eq '"') { $inStr = $false }
            continue
        }
        if ($ch -eq '"') { $inStr = $true; [void]$cur.Append($ch); continue }
        if ($ch -eq '(') { $depth++ }
        if ($ch -eq ')') { $depth-- }
        if ($ch -eq ',' -and $depth -eq 0) { $out.Add($cur.ToString().Trim()); [void]$cur.Clear(); continue }
        [void]$cur.Append($ch)
    }
    if ($cur.Length -gt 0) { $out.Add($cur.ToString().Trim()) }
    return ,$out
}
function Unq([string]$s) { if ($s.StartsWith('"') -and $s.EndsWith('"')) { return $s.Substring(1, $s.Length - 2) } return $s }

# rows: KEY_SLIDER( ... ) / KEY_CHECK( ... ), each ends with ")" at end of a line
$rows = @()
$lines = $text -split "`r?`n"
$buf = ''
foreach ($ln in $lines) {
    if ($ln -match '^\s*//') { continue }
    if ($buf -eq '' -and $ln -notmatch '^\s*KEY_(SLIDER|CHECK)\(') { continue }
    $buf += ' ' + $ln.Trim()
    if ($buf.TrimEnd().EndsWith(')')) {
        $m = [regex]::Match($buf.Trim(), '^KEY_(SLIDER|CHECK)\((.*)\)$')
        if (-not $m.Success) { throw "unparsable row: $buf" }
        $rows += [PSCustomObject]@{ Kind = $m.Groups[1].Value; Args = (Split-Args $m.Groups[2].Value) }
        $buf = ''
    }
}

$groups = 'G_FILM','G_MASSES','G_DROPLETS','G_OIL','G_COLOUR','G_OPTICS','G_LID','G_POST','G_MOTION','G_INK','G_FLUID','G_OUTPUT','G_SYSTEM'
$flagNames = 'KF_MOTION','KF_UNVERIFIED','KF_SUPERSEDED','KF_SHELL','KF_MACHINE','KF_GLOBAL'
$errors = New-Object System.Collections.Generic.List[string]
$keys = @{}
$meta = @()
foreach ($r in $rows) {
    $a = $r.Args
    if ($r.Kind -eq 'SLIDER') {
        if ($a.Count -ne 17) { $errors.Add("SLIDER row with $($a.Count) args (want 17): $($a[0])"); continue }
        $sec = Unq $a[7]; $key = Unq $a[8]; $g = $a[11]; $looks = Unq $a[12]; $gate = Unq $a[13]; $flags = $a[14]; $spec = Unq $a[15]; $front = Unq $a[16]
    } else {
        if ($a.Count -ne 11) { $errors.Add("CHECK row with $($a.Count) args (want 11): $($a[0])"); continue }
        $sec = Unq $a[2]; $key = Unq $a[3]; $g = $a[5]; $looks = Unq $a[6]; $gate = Unq $a[7]; $flags = $a[8]; $spec = Unq $a[9]; $front = Unq $a[10]
    }
    $id = "$sec.$key"
    if ($keys.ContainsKey($id)) { $errors.Add("duplicate key $id") }
    $keys[$id] = $true
    if ($groups -notcontains $g) { $errors.Add("$id : group '$g' is not a G_* name") }
    if ($looks -notmatch '^[FAI]{1,3}$') { $errors.Add("$id : looks '$looks' must be a non-empty subset of FAI") }
    if ($front -and $front -notmatch '^[FAI]{1,3}$') { $errors.Add("$id : front '$front' must be a subset of FAI") }
    foreach ($ch in $front.ToCharArray()) { if ($looks.IndexOf($ch) -lt 0) { $errors.Add("$id : front look $ch is not in looks $looks") } }
    if ($flags -ne '0') { foreach ($f in ($flags -split '\|')) { if ($flagNames -notcontains $f.Trim()) { $errors.Add("$id : unknown flag '$f'") } } }
    if ($spec -and $spec -notmatch '^(enum:(-?\d+=[^|]+)(\|-?\d+=[^|]+)*|peak|neg:.+|zero:.+|look|autostart)$') { $errors.Add("$id : bad special '$spec'") }
    if ($sec -ne 'system' -and $mainSrc.IndexOf("L`"$key`"") -lt 0) { $errors.Add("$id : key string L`"$key`" is never read in main.cpp / cycle.cpp") }
    $meta += [PSCustomObject]@{ Id = $id; Gate = $gate; Flags = $flags; Looks = $looks }
}

# gates: tokens sec.key <op> number, look==X, && || ( )
foreach ($m in $meta) {
    if (-not $m.Gate) { continue }
    $g = $m.Gate
    $atoms = [regex]::Matches($g, '([a-z_0-9]+(?:\.[a-z_0-9]+)?)\s*(==|!=|>=|<=|>|<)\s*([-0-9.]+|[FAI])')
    $rest = [regex]::Replace($g, '([a-z_0-9]+(?:\.[a-z_0-9]+)?)\s*(==|!=|>=|<=|>|<)\s*([-0-9.]+|[FAI])', 'X')
    $rest = $rest -replace '\s', ''
    $tok = ($rest -replace '&&', 'A') -replace '\|\|', 'O'
    $flat = $tok -replace '[()]', ''
    if ($tok -notmatch '^[XAO()]+$' -or $flat -notmatch '^X([AO]X)*$') { $errors.Add("$($m.Id) : gate does not parse: '$g'") }
    $opens = ($rest.ToCharArray() | Where-Object { $_ -eq '(' }).Count
    $closes = ($rest.ToCharArray() | Where-Object { $_ -eq ')' }).Count
    if ($opens -ne $closes) { $errors.Add("$($m.Id) : unbalanced parens in gate '$g'") }
    foreach ($a in $atoms) {
        $name = $a.Groups[1].Value
        if ($name -eq 'look') {
            if ($a.Groups[2].Value -ne '==' -or $a.Groups[3].Value -notmatch '^[FAI]$') { $errors.Add("$($m.Id) : look atom must be look==F|A|I") }
            continue
        }
        if (-not $keys.ContainsKey($name)) { $errors.Add("$($m.Id) : gate names unknown key '$name'") }
    }
}

# measured inert entries
if (Test-Path $inert) {
    foreach ($ln in [System.IO.File]::ReadAllLines($inert)) {
        $mm = [regex]::Match($ln, '^\s*MEASURED_INERT\("([^"]+)",\s*"([^"]+)",\s*"([^"]+)"')
        if ($mm.Success) {
            $id = "$($mm.Groups[2].Value).$($mm.Groups[3].Value)"
            if (-not $keys.ContainsKey($id)) { $errors.Add("measured_inert.inc names unknown key $id") }
        }
    }
}

$nGate = ($meta | Where-Object { $_.Gate }).Count
$nUnv = ($meta | Where-Object { $_.Flags -match 'KF_UNVERIFIED' }).Count
$nMot = ($meta | Where-Object { $_.Flags -match 'KF_MOTION' }).Count
"keymeta-check: $($rows.Count) rows ($(($rows | Where-Object Kind -eq 'SLIDER').Count) sliders, $(($rows | Where-Object Kind -eq 'CHECK').Count) checks), $nGate gated, $nMot motion, $nUnv unverified"
if ($errors.Count -gt 0) {
    $errors | ForEach-Object { "  FAIL $_" }
    "keymeta-check: FAILED ($($errors.Count) problem(s))"
    exit 1
}
"keymeta-check: OK"
exit 0
