<#
tools\rig-check.ps1 -- the post pass's rig block b3, cross-checked (brief CLOUD-1 (b)).

No GPU, no build, < 1 s. Checker only: nothing is generated. Parses

  src/rig_slots.h          the table: RIG_SLOTS rows X(NAME, vec, comp, shift, bits, "doc")
  src/fluid.cpp            RunPostPass, whose only way to write the block is
                           RigPut(RG_<NAME>, v), quantised by RigQ/RigQz(RG_<NAME>, ...)
  src/shaders/post.hlsl    cbuffer RigCB (rg0..rg4), every rgN.c read, and the
                           unpack helpers (BDU6 / LidU8 / LidU12 / LidU7764 -- any
                           float<N> F(float v) built from floor(v * (1.0 / 2^k)))

and fails (exit 1) on drift:
  table   duplicate name; vec past rg<kRigVecs-1>; a whole float (bits 0) sharing
          its float with another row or given a shift; a field past bit 24;
          two fields overlapping in one float
  C++     a row written 0 or 2+ times; RigPut/RigQ of a name not in the table;
          RigQ on a whole-float row; RigPut(RG_A, RigQ(RG_B ...)) with A != B;
          any raw rig[<n>] write; the rig array not sized kRigFloats
  HLSL    cbuffer RigCB not exactly float4 rg0..rg<kRigVecs-1>; a read of a float
          with no row; an unpack helper whose own shifts and widths disagree;
          a helper applied to a whole float, or to a packed float whose table
          fields are not fields of that helper (shift AND width); an unpacked
          component (F(rgN.c).y, or v.y of v = F(rgN.c)) that has no row; a
          packed float read bare other than as a '> 0.5' / '>= 2^shift' test
          on one of its fields; a row that is never read
Usage:  powershell -File tools\rig-check.ps1 [-Root <repo>] [-Table]
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$Table
)
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding $false
$fails = New-Object System.Collections.Generic.List[string]
function Fail([string]$m) { $script:fails.Add($m) }
function ReadText([string]$p) { [IO.File]::ReadAllText($p, $utf8).Replace("`r`n", "`n") }
function StripComments([string]$t) {
    $t = [regex]::Replace($t, '/\*.*?\*/', '', 'Singleline')
    return [regex]::Replace($t, '//[^\n]*', '')
}
function LineOf([string]$t, [int]$idx) { ([regex]::Matches($t.Substring(0, $idx), "`n")).Count + 1 }
function Log2Exact([double]$v) {
    if ($v -lt 1) { return -1 }
    $k = [math]::Round([math]::Log($v, 2))
    if ([math]::Pow(2, $k) -eq $v) { return [int]$k }
    return -1
}

$tabPath  = Join-Path $Root 'src/rig_slots.h'
$cppPath  = Join-Path $Root 'src/fluid.cpp'
$postPath = Join-Path $Root 'src/shaders/post.hlsl'
$comps = 'x', 'y', 'z', 'w'

# ---- 1. the table ----------------------------------------------------------------
$tab = ReadText $tabPath
$m = [regex]::Match($tab, 'kRigVecs\s*=\s*(\d+)')
if (-not $m.Success) { throw "rig_slots.h: kRigVecs not found" }
$nVec = [int]$m.Groups[1].Value
$rows = @()
foreach ($r in [regex]::Matches($tab, '\bX\(\s*(\w+)\s*,\s*(\d+)\s*,\s*([xyzw])\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*\)')) {
    $rows += [pscustomobject]@{
        Name = $r.Groups[1].Value; Vec = [int]$r.Groups[2].Value; Comp = $r.Groups[3].Value
        Shift = [int]$r.Groups[4].Value; Bits = [int]$r.Groups[5].Value; Doc = $r.Groups[6].Value
        W = 0; R = 0
    }
}
if ($rows.Count -eq 0) { throw "rig_slots.h: no X(...) rows parsed" }
$byName = @{}; $byFloat = @{}
foreach ($r in $rows) {
    if ($byName.ContainsKey($r.Name)) { Fail "table: $($r.Name) appears twice" }
    $byName[$r.Name] = $r
    if ($r.Vec -ge $nVec) { Fail "table: $($r.Name) is in rg$($r.Vec), past rg$($nVec - 1)" }
    $f = "$($r.Vec).$($r.Comp)"
    if (-not $byFloat.ContainsKey($f)) { $byFloat[$f] = @() }
    $byFloat[$f] += $r
}
foreach ($f in $byFloat.Keys) {
    $fr = @($byFloat[$f])
    $whole = @($fr | Where-Object { $_.Bits -eq 0 })
    if ($whole.Count -gt 0) {
        if ($fr.Count -gt 1) { Fail "table: rg$f is a whole float ($($whole[0].Name)) but has $($fr.Count) rows" }
        foreach ($w in $whole) { if ($w.Shift -ne 0) { Fail "table: $($w.Name) is a whole float with shift $($w.Shift)" } }
        continue
    }
    foreach ($a in $fr) {
        if ($a.Shift + $a.Bits -gt 24) { Fail "table: $($a.Name) (rg$f bits $($a.Shift)..$($a.Shift + $a.Bits - 1)) is past float32's 24 exact bits" }
        foreach ($b in $fr) {
            if ($a.Name -lt $b.Name -and $a.Shift -lt $b.Shift + $b.Bits -and $b.Shift -lt $a.Shift + $a.Bits) {
                Fail "table: $($a.Name) and $($b.Name) overlap in rg$f"
            }
        }
    }
}
function IsPacked([string]$f) { $script:byFloat.ContainsKey($f) -and @($script:byFloat[$f] | Where-Object { $_.Bits -gt 0 }).Count -gt 0 }

# ---- 2. C++: RunPostPass ------------------------------------------------------------
$cpp = StripComments (ReadText $cppPath)
$s = $cpp.IndexOf('void FluidRenderer::RunPostPass(')
if ($s -lt 0) { throw "fluid.cpp: RunPostPass not found" }
$b = $cpp.IndexOf('{', $s); $depth = 0; $e = -1
for ($i = $b; $i -lt $cpp.Length; $i++) {
    $c = $cpp[$i]
    if ($c -eq '{') { $depth++ } elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { $e = $i; break } }
}
$body = $cpp.Substring($b, $e - $b + 1)
if (-not [regex]::IsMatch($body, 'float\s+rig\s*\[\s*kRigFloats\s*\]')) { Fail "RunPostPass: 'float rig[kRigFloats]' not found" }
foreach ($raw in [regex]::Matches($body, '\brig\s*\[\s*[^\]\s]+\s*\]')) {
    if ($raw.Value -match 'kRigFloats|r\.index') { continue }
    Fail "RunPostPass: raw '$($raw.Value)' bypasses the table (use RigPut(RG_<NAME>, v))"
}
foreach ($w in [regex]::Matches($body, '\bRigPut\s*\(\s*RG_(\w+)\s*,\s*([^;]*)')) {
    $n = $w.Groups[1].Value
    if (-not $byName.ContainsKey($n)) { Fail "RunPostPass writes RG_$n, which is not in the table"; continue }
    $byName[$n].W++
    $q = [regex]::Match($w.Groups[2].Value, '^\s*RigQz?\s*\(\s*RG_(\w+)')
    if ($q.Success -and $q.Groups[1].Value -ne $n) { Fail "RigPut(RG_$n, ...) is quantised with RG_$($q.Groups[1].Value)'s width" }
}
foreach ($q in [regex]::Matches($body, '\bRigQz?\s*\(\s*RG_(\w+)')) {
    $n = $q.Groups[1].Value
    if (-not $byName.ContainsKey($n)) { Fail "RunPostPass quantises RG_$n, which is not in the table" }
    elseif ($byName[$n].Bits -eq 0) { Fail "RunPostPass quantises RG_$n, a whole float" }
}

# ---- 3. HLSL: post.hlsl -----------------------------------------------------------------
$hl = StripComments (ReadText $postPath)
# 3a. cbuffer RigCB
$m = [regex]::Match($hl, 'cbuffer\s+RigCB\s*:\s*register\s*\(\s*b3\s*\)\s*\{(.*?)\};', 'Singleline')
if (-not $m.Success) { Fail "post.hlsl: cbuffer RigCB : register(b3) not found" }
else {
    $got = @([regex]::Matches($m.Groups[1].Value, '(\w+)\s+(\w+)\s*;') | ForEach-Object { "$($_.Groups[1].Value) $($_.Groups[2].Value)" })
    $want = @(0..($nVec - 1) | ForEach-Object { "float4 rg$_" })
    if (($got -join ', ') -ne ($want -join ', ')) { Fail "cbuffer RigCB is '$($got -join ', ')', the table wants '$($want -join ', ')'" }
}
# 3b. unpack helpers: float<N> F(float v) { ... floor(x * (1.0 / 2^k)) ... return ... (1.0 / (2^b - 1)) }
$helpers = @{}
foreach ($fm in [regex]::Matches($hl, 'float([234])\s+(\w+)\s*\(\s*float\s+v\s*\)\s*\{(.*?)\n\}', 'Singleline')) {
    $name = $fm.Groups[2].Value; $n = [int]$fm.Groups[1].Value; $fb = $fm.Groups[3].Value
    $divs = @([regex]::Matches($fb, 'floor\s*\(\s*\w+\s*\*\s*\(\s*1\.0\s*/\s*([\d.]+)\s*\)\s*\)') | ForEach-Object { [double]$_.Groups[1].Value })
    if ($divs.Count -eq 0) { continue }   # not an unpack helper
    $ret = [regex]::Match($fb, 'return(.*?);', 'Singleline').Groups[1].Value
    $norms = @([regex]::Matches($ret, '\(\s*1\.0\s*/\s*([\d.]+)\s*\)') | ForEach-Object { [double]$_.Groups[1].Value })
    if ($divs.Count -ne $n - 1) { Fail "helper $name returns float$n but splits $($divs.Count + 1) fields"; continue }
    if ($norms.Count -eq 1) { $norms = @(1..$n | ForEach-Object { $norms[0] }) }
    if ($norms.Count -ne $n) { Fail "helper ${name}: cannot read its per-field normalisation"; continue }
    $shifts = @($divs | ForEach-Object { Log2Exact $_ }) + @(0)
    $widths = @($norms | ForEach-Object { Log2Exact ($_ + 1) })
    $okH = $true
    for ($k = 0; $k -lt $n; $k++) {
        if ($shifts[$k] -lt 0) { Fail "helper ${name}: divisor $($divs[$k]) is not a power of two"; $okH = $false }
        if ($widths[$k] -lt 1) { Fail "helper ${name}: field $k normalises by $($norms[$k]), not 2^b-1"; $okH = $false }
    }
    for ($k = 1; $k -lt $n -and $okH; $k++) {
        if ($shifts[$k - 1] -ne $shifts[$k] + $widths[$k]) {
            Fail "helper ${name}: field $k is $($widths[$k]) bits at $($shifts[$k]) but field $($k - 1) starts at $($shifts[$k - 1])"; $okH = $false
        }
    }
    if ($okH -and $shifts[0] + $widths[0] -gt 24) { Fail "helper ${name}: top field ends past bit 24" }
    $helpers[$name] = [pscustomobject]@{ Shifts = $shifts; Widths = $widths; N = $n; Ok = $okH }
}
if ($helpers.Count -eq 0) { Fail "post.hlsl: no unpack helpers found" }
function HelperField($h, [int]$k) {
    $sh = $h.Shifts[$k]; $bt = $h.Widths[$k]
    return ,@($script:rowsInFloat | Where-Object { $_.Shift -eq $sh -and $_.Bits -eq $bt })
}
function MarkRead([string]$f, $rowsHit) {
    foreach ($r in $rowsHit) { $r.R++ }
}
# 3c. every rgN.c read
$consumed = @{}   # match index -> handled
$vars = @()
foreach ($u in [regex]::Matches($hl, '\b(\w+)\s*\(\s*rg(\d+)\.([xyzw])\s*\)')) {
    $hn = $u.Groups[1].Value; $f = "$($u.Groups[2].Value).$($u.Groups[3].Value)"
    $at = "post.hlsl line ~$(LineOf $hl $u.Index): $hn(rg$f)"
    $consumed[$u.Index + $u.Value.IndexOf('rg')] = $true
    if (-not $helpers.ContainsKey($hn)) { continue }   # not an unpack: treated below as a bare read
    $h = $helpers[$hn]
    if (-not $byFloat.ContainsKey($f)) { Fail "${at}: rg$f has no row in the table"; continue }
    if (-not (IsPacked $f)) { Fail "${at}: rg$f is a whole float ($($byFloat[$f][0].Name)), not a pack"; continue }
    $script:rowsInFloat = @($byFloat[$f])
    foreach ($r in $rowsInFloat) {
        $hit = $false
        for ($k = 0; $k -lt $h.N; $k++) { if ($h.Shifts[$k] -eq $r.Shift -and $h.Widths[$k] -eq $r.Bits) { $hit = $true } }
        if (-not $hit) { Fail "${at}: table row $($r.Name) ($($r.Bits) bits at $($r.Shift)) is not a field of $hn (shifts $($h.Shifts -join '/'), widths $($h.Widths -join '/'))" }
    }
    # which components of the unpacked value are used
    $tail = $hl.Substring($u.Index + $u.Length)
    $sw = [regex]::Match($tail, '^\s*\.([xyzw]+)\b')
    $used = @()
    if ($sw.Success) { $used = @($sw.Groups[1].Value.ToCharArray() | ForEach-Object { "$_" }) }
    else {
        $pre = $hl.Substring(0, $u.Index)
        $vm = [regex]::Match($pre, '\b(\w+)\s*=\s*$')
        if ($vm.Success) {
            $v = $vm.Groups[1].Value
            foreach ($vu in [regex]::Matches($hl.Substring($u.Index + $u.Length), "\b$v\b(\s*\.\s*([xyzw]+)\b)?")) {
                if ($vu.Groups[2].Success) { $used += @($vu.Groups[2].Value.ToCharArray() | ForEach-Object { "$_" }) }
                else { $used += @($comps[0..($h.N - 1)]) }
            }
        } else { $used = @($comps[0..($h.N - 1)]) }
    }
    foreach ($c in ($used | Sort-Object -Unique)) {
        $k = 'xyzw'.IndexOf($c)
        if ($k -ge $h.N) { Fail "${at}: .$c past the helper's $($h.N) fields"; continue }
        $rh = HelperField $h $k
        if ($rh.Count -eq 0) { Fail "${at}: unpacks field $k (.$c, $($h.Widths[$k]) bits at $($h.Shifts[$k])), which has no row -- spare or drifted" }
        else { MarkRead $f $rh }
    }
}
foreach ($u in [regex]::Matches($hl, '\brg(\d+)\.([xyzw]{1,4})\b')) {
    if ($consumed.ContainsKey($u.Index) -and $helpers.ContainsKey(([regex]::Match($hl.Substring(0, $u.Index), '(\w+)\s*\(\s*$')).Groups[1].Value)) { continue }
    $at = "post.hlsl line ~$(LineOf $hl $u.Index): rg$($u.Groups[1].Value).$($u.Groups[2].Value)"
    foreach ($c in $u.Groups[2].Value.ToCharArray()) {
        $f = "$($u.Groups[1].Value).$c"
        if ([int]$u.Groups[1].Value -ge $nVec) { Fail "${at}: rg$($u.Groups[1].Value) is past rg$($nVec - 1)"; continue }
        if (-not $byFloat.ContainsKey($f)) { Fail "${at}: rg$f has no row in the table"; continue }
        if (-not (IsPacked $f)) { MarkRead $f $byFloat[$f]; continue }
        # a packed float read bare: only as an on/off test of one of its fields
        $cmp = [regex]::Match($hl.Substring($u.Index + $u.Length), '^\s*(>=|>)\s*([\d.]+)')
        if (-not $cmp.Success) { Fail "${at}: packed rg$f read as a plain number -- unpack it"; continue }
        $t = [double]$cmp.Groups[2].Value
        if ($t -eq 0.5) { continue }   # 'any field set'
        $sh = Log2Exact $t
        $hit = @($byFloat[$f] | Where-Object { $_.Shift -eq $sh })
        if ($hit.Count -eq 0) { Fail "${at} >= $($cmp.Groups[2].Value): no field of rg$f starts at that bit" }
    }
}

# ---- 4. cross-check -----------------------------------------------------------------
foreach ($r in $rows) {
    $at = "RG_$($r.Name) (rg$($r.Vec).$($r.Comp)"
    if ($r.Bits -gt 0) { $at += " bits $($r.Shift)..$($r.Shift + $r.Bits - 1)" }
    $at += ')'
    if ($r.W -eq 0) { Fail "$at is never written (RigPut)" }
    if ($r.W -gt 1) { Fail "$at is written $($r.W) times" }
    if ($r.R -eq 0) { Fail "$at is never read in post.hlsl" }
}

# ---- report --------------------------------------------------------------------------
if ($Table) {
    '{0,-10} {1,-20} {2,2} {3,3}  {4}' -f 'float', 'name', 'W', 'R', 'doc'
    foreach ($r in ($rows | Sort-Object Vec, @{ Expression = { 'xyzw'.IndexOf($_.Comp) } }, @{ Expression = { -$_.Shift } })) {
        $where = "rg$($r.Vec).$($r.Comp)"; if ($r.Bits -gt 0) { $where += "[$($r.Shift + $r.Bits - 1):$($r.Shift)]" }
        '{0,-10} {1,-20} {2,2} {3,3}  {4}' -f $where, $r.Name, $r.W, $r.R, $r.Doc
    }
    ''
}
$packs = @($byFloat.Keys | Where-Object { IsPacked $_ }).Count
"rig-check: $($rows.Count) rows in rg0..rg$($nVec - 1) ($packs packed floats); helpers " + (($helpers.Keys | Sort-Object | ForEach-Object { "$_[" + ($helpers[$_].Widths -join '.') + "]" }) -join ' ')
if ($fails.Count -gt 0) {
    foreach ($f in $fails) { "FAIL: $f" }
    "rig-check: FAILED ($($fails.Count))"
    exit 1
}
"rig-check: PASS"
exit 0
