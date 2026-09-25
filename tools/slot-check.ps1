<#
tools\slot-check.ps1 -- the packed Liquid Acid cbuffer, cross-checked (brief BE).

No GPU, no build, < 1 s. Parses the three places a packed acid scalar lives and
fails (exit 1) on any disagreement between them:

  src\acid_slots.h   the table: ACID_SLOTS rows X(NAME, vec, comp, "doc")
  src\fluid.cpp      struct AcidParamsGPU, and UploadAcidConstants, whose only
                     way to write a scalar is slot(LA_<NAME>, value)
  src\shaders\*.hlsl cbuffer AcidCB (display.hlsl) and every HLSL source; the acid
                     code reads LA_<NAME> (a define kAcidSlotMacros hands to
                     D3DCompile). cmake/embed_hlsl.cmake embeds them at build time.

Fails on:
  - a table row that is never written, written twice, or never read
  - a read or write of a name that is not in the table
  - two names on one component (vec, comp), or a vec outside laP0..laP<N-1>
  - any raw laP<n>.<c> left in the .hlsl, or a raw p.p<n> write in the upload
  - AcidParamsGPU vs cbuffer AcidCB member order/size mismatch, or a
    static_assert size that disagrees with the struct
  - the generated comment table in cbuffer AcidCB out of date (-Fix rewrites it)
  - LA_<NAME> used in a source other than display.hlsl / kDisplaySrc (it
    would not compile)
  - a source line over -LiteralMax bytes: the generator splits each source
    into literal pieces of at most -LiteralMax bytes on line boundaries (MSVC
    caps one piece at 16380), so only a single longer line can break it. The
    report prints the three largest pieces the generator will emit (the same
    split, simulated here; UTF-8 bytes, CRLF counted as one byte)

Run it before merging anything that touches UploadAcidConstants, AcidCB or an
acid literal:   powershell -File tools\slot-check.ps1 [-Fix] [-Table]
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$Fix,          # rewrite the generated comment table in display.hlsl
    [switch]$Table,        # print every slot with its write/read counts
    [int]$LiteralMax = 16000
)
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding $false
$fails = New-Object System.Collections.Generic.List[string]
function Fail([string]$m) { $script:fails.Add($m) }
function ReadText([string]$p) { [IO.File]::ReadAllText($p, $utf8) }
function StripComments([string]$t) {
    $t = [regex]::Replace($t, '/\*.*?\*/', '', 'Singleline')
    return [regex]::Replace($t, '//[^\r\n]*', '')
}
function LineOf([string]$t, [int]$idx) { ([regex]::Matches($t.Substring(0, $idx), "`n")).Count + 1 }

# forward slashes: fine on Windows PowerShell 5.1, and the script also runs under pwsh elsewhere
$slotsPath  = Join-Path $Root 'src/acid_slots.h'
$cppPath    = Join-Path $Root 'src/fluid.cpp'
$shaderDir  = Join-Path $Root 'src/shaders'
$shaderPath = Join-Path $shaderDir 'display.hlsl'   # holds cbuffer AcidCB
# file -> the variable cmake/embed_hlsl.cmake emits it as (CMakeLists.txt SHADER_NAMES)
$sources = [ordered]@{ 'compute' = 'kComputeSrc'; 'display' = 'kDisplaySrc'; 'post' = 'kPostSrc'; 'gradient' = 'kGradientSrc' }
$comps = 'x', 'y', 'z', 'w'

# ---- 1. the table ----------------------------------------------------------
$tab = ReadText $slotsPath
$m = [regex]::Match($tab, 'kAcidSlotVecs\s*=\s*(\d+)')
if (-not $m.Success) { throw "acid_slots.h: kAcidSlotVecs not found" }
$nVec = [int]$m.Groups[1].Value
$rows = @()
foreach ($r in [regex]::Matches($tab, '(?m)^\s*X\(\s*(\w+)\s*,\s*(\d+)\s*,\s*([xyzw])\s*,\s*"([^"]*)"\s*\)')) {
    $rows += [pscustomobject]@{
        Name = $r.Groups[1].Value; Vec = [int]$r.Groups[2].Value; Comp = $r.Groups[3].Value
        Doc = $r.Groups[4].Value; W = 0; R = 0
    }
}
if ($rows.Count -eq 0) { throw "acid_slots.h: no X(...) rows parsed" }
$byName = @{}; $byKey = @{}
foreach ($r in $rows) {
    if ($byName.ContainsKey($r.Name)) { Fail "table: name $($r.Name) appears twice" }
    $byName[$r.Name] = $r
    $key = "$($r.Vec).$($r.Comp)"
    if ($byKey.ContainsKey($key)) { Fail "table: laP$key used under two names: $($byKey[$key].Name) and $($r.Name)" }
    else { $byKey[$key] = $r }
    if ($r.Vec -ge $nVec) { Fail "table: $($r.Name) is in laP$($r.Vec), past laP$($nVec - 1)" }
}

# ---- 2. C++ struct AcidParamsGPU -------------------------------------------
$cppRaw = ReadText $cppPath
$cpp = StripComments $cppRaw
$m = [regex]::Match($cpp, 'struct\s+AcidParamsGPU\s*\{(.*?)\};', 'Singleline')
if (-not $m.Success) { throw "fluid.cpp: struct AcidParamsGPU not found" }
$cppMembers = @()
foreach ($decl in [regex]::Matches($m.Groups[1].Value, 'float\s+([^;]+);')) {
    foreach ($part in $decl.Groups[1].Value.Split(',')) {
        $pm = [regex]::Match($part.Trim(), '^(\w+)((?:\[\d+\])+)$')
        if (-not $pm.Success) { Fail "AcidParamsGPU: cannot parse member '$($part.Trim())'"; continue }
        $dims = @([regex]::Matches($pm.Groups[2].Value, '\d+') | ForEach-Object { [int]$_.Value })
        if ($dims[-1] -ne 4) { Fail "AcidParamsGPU: $($pm.Groups[1].Value) is not float4-shaped" }
        $count = 1; if ($dims.Count -eq 2) { $count = $dims[0] }
        $n = $pm.Groups[1].Value
        $cppMembers += [pscustomobject]@{ Name = $n; Hlsl = 'la' + $n.Substring(0, 1).ToUpper() + $n.Substring(1); Count = $count; Array = ($dims.Count -eq 2) }
    }
}
$cppFloat4 = ($cppMembers | Measure-Object -Property Count -Sum).Sum
$m = [regex]::Match($cpp, 'static_assert\s*\(\s*sizeof\s*\(\s*AcidParamsGPU\s*\)\s*==\s*(\d+)')
if ($m.Success) {
    if ([int]$m.Groups[1].Value -ne $cppFloat4 * 16) { Fail "static_assert says $($m.Groups[1].Value) bytes, the struct is $($cppFloat4 * 16)" }
} else { Fail "fluid.cpp: static_assert(sizeof(AcidParamsGPU) == ...) not found" }
for ($v = 0; $v -lt $nVec; $v++) {
    $hit = @($cppMembers | Where-Object { $_.Name -eq "p$v" -and -not $_.Array })
    if ($hit.Count -ne 1) { Fail "AcidParamsGPU: expected exactly one float p$v[4]" }
}

# ---- 3. UploadAcidConstants: writes ------------------------------------------
$s = $cpp.IndexOf('void FluidRenderer::UploadAcidConstants()')
if ($s -lt 0) { throw "fluid.cpp: UploadAcidConstants not found" }
$b = $cpp.IndexOf('{', $s); $depth = 0; $e = -1
for ($i = $b; $i -lt $cpp.Length; $i++) {
    $c = $cpp[$i]
    if ($c -eq '{') { $depth++ } elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { $e = $i; break } }
}
$body = $cpp.Substring($b, $e - $b + 1)
$m = [regex]::Match($body, 'float\s*\*\s*const\s+V\s*\[\s*kAcidSlotVecs\s*\]\s*=\s*\{([^}]*)\}')
if (-not $m.Success) { Fail "UploadAcidConstants: the V[kAcidSlotVecs] = { p.p0, ... } table is missing" }
else {
    $vl = @($m.Groups[1].Value.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $want = @(0..($nVec - 1) | ForEach-Object { "p.p$_" })
    if (($vl -join ',') -ne ($want -join ',')) { Fail "UploadAcidConstants: V[] must list p.p0 .. p.p$($nVec - 1) in order" }
    $body = $body.Remove($m.Index, $m.Length)
}
foreach ($raw in [regex]::Matches($body, '\bp\.p\d+\b')) {
    Fail "UploadAcidConstants: raw '$($raw.Value)' write bypasses the slot table (use slot(LA_<NAME>, v))"
}
foreach ($w in [regex]::Matches($body, '\bslot\s*\(\s*LA_(\w+)\s*,')) {
    $n = $w.Groups[1].Value
    if ($byName.ContainsKey($n)) { $byName[$n].W++ } else { Fail "upload writes LA_$n, which is not in the table" }
}

# ---- 4. the .hlsl sources: cbuffer, reads, piece sizes ----------------------
$shRaw = ReadText $shaderPath
$nl = "`n"; if ($shRaw.Contains("`r`n")) { $nl = "`r`n" }
$beginTag = '// ---- BEGIN GENERATED by tools\slot-check.ps1 -Fix'
$endTag   = '// ---- END GENERATED'

# 4a. the generated cbuffer body
$gen = New-Object System.Collections.Generic.List[string]
$gen.Add("    $beginTag from src\acid_slots.h + AcidParamsGPU;")
$gen.Add("    // do not hand-edit. The acid code reads each packed scalar as LA_<NAME>, a")
$gen.Add("    // '#define LA_<NAME> laP<n>.<c>' that kAcidSlotMacros (fluid.cpp) passes to")
$gen.Add("    // D3DCompile. Units, ini keys and hardcoded values: src\acid_slots.h.")
$fixed = @{
    oil = 'oil palette, rgb'
    ink = 'ink ramp stops (dark -> bright), rgb'
    men = 'meniscus halo colour, rgb'
    mix = 'hue2 mix field: 20x12 cells, four per float4 (brief AE)'
}
foreach ($cm in $cppMembers) {
    $decl = "float4 $($cm.Hlsl)"; if ($cm.Array) { $decl += "[$($cm.Count)]" }
    $decl = ($decl + ';').PadRight(17)
    if ($cm.Name -match '^p(\d+)$') {
        $v = [int]$Matches[1]
        $parts = foreach ($c in $comps) {
            $k = "$v.$c"; if ($byKey.ContainsKey($k)) { "$c $($byKey[$k].Name)" } else { "$c -" }
        }
        $gen.Add("    $decl // " + ($parts -join '  '))
    } elseif ($fixed.ContainsKey($cm.Name)) {
        $gen.Add("    $decl // $($fixed[$cm.Name])")
    } else {
        $gen.Add("    $decl // (no description: add '$($cm.Name)' to slot-check.ps1)")
    }
}
$gen.Add("    $endTag")
$genText = ($gen -join $nl)

$bi = $shRaw.IndexOf($beginTag); $ei = $shRaw.IndexOf($endTag)
if ($bi -lt 0 -or $ei -lt 0 -or $ei -lt $bi) { throw "display.hlsl: GENERATED markers not found in cbuffer AcidCB" }
$bLine = $shRaw.LastIndexOf("`n", $bi) + 1
$eLineEnd = $shRaw.IndexOf("`n", $ei); if ($shRaw[$eLineEnd - 1] -eq "`r") { $eLineEnd-- }
$curText = $shRaw.Substring($bLine, $eLineEnd - $bLine)
$fixedNow = $false
if ($curText -ne $genText) {
    if ($Fix) {
        $shRaw = $shRaw.Substring(0, $bLine) + $genText + $shRaw.Substring($eLineEnd)
        [IO.File]::WriteAllText($shaderPath, $shRaw, $utf8)
        $fixedNow = $true
    } else { Fail "display.hlsl: the generated cbuffer AcidCB table is stale -- run tools\slot-check.ps1 -Fix" }
}

# 4b. cbuffer AcidCB member order vs AcidParamsGPU
$m = [regex]::Match((StripComments $shRaw), 'cbuffer\s+AcidCB\s*:\s*register\s*\(\s*b1\s*\)\s*\{(.*?)\};', 'Singleline')
if (-not $m.Success) { Fail "display.hlsl: cbuffer AcidCB not found" }
else {
    $h = @([regex]::Matches($m.Groups[1].Value, 'float4\s+(\w+)(?:\s*\[\s*(\d+)\s*\])?\s*;') | ForEach-Object {
        $cnt = 1; if ($_.Groups[2].Success) { $cnt = [int]$_.Groups[2].Value }; "$($_.Groups[1].Value)x$cnt" })
    $c = @($cppMembers | ForEach-Object { "$($_.Hlsl)x$($_.Count)" })
    if (($h -join ' ') -ne ($c -join ' ')) {
        Fail "cbuffer AcidCB and AcidParamsGPU disagree on member order/size:`n    HLSL: $($h -join ' ')`n    C++ : $($c -join ' ')"
    }
}

# 4c. every source: piece sizes, raw laP, reads
$lits = @()
foreach ($src in $sources.Keys) {
    $owner = $sources[$src]
    $path = Join-Path $shaderDir "$src.hlsl"
    if (-not (Test-Path $path)) { Fail "src/shaders/$src.hlsl not found"; continue }
    $txt = ReadText $path
    if ($src -eq 'display') { $txt = $shRaw }   # after a -Fix rewrite
    $lf = $txt.Replace("`r`n", "`n")
    # the generator's split (cmake/embed_hlsl.cmake), simulated: pieces of at
    # most $LiteralMax bytes, cut after the last newline that fits
    $bytes = $utf8.GetBytes($lf); $at = 0; $piece = 0
    while ($at -lt $bytes.Length) {
        $len = $bytes.Length - $at
        if ($len -gt $LiteralMax) {
            $cut = -1
            for ($k = $at + $LiteralMax - 1; $k -ge $at; $k--) { if ($bytes[$k] -eq 10) { $cut = $k + 1; break } }
            if ($cut -lt 0) { Fail "$src.hlsl: a line longer than $LiteralMax bytes near byte $at -- the generator cannot split it"; break }
            $len = $cut - $at
        }
        $piece++
        $lits += [pscustomobject]@{ Var = $owner; Line = "$src.hlsl piece $piece"; Bytes = $len }
        $at += $len
    }
    # Everything outside the generated table: no raw laP<n>.<c>, even in a comment.
    $scan = $txt
    $gb = $scan.IndexOf($beginTag); $ge = $scan.IndexOf($endTag)
    if ($gb -ge 0 -and $ge -gt $gb) { $scan = $scan.Remove($gb, $ge - $gb) }
    foreach ($raw in [regex]::Matches($scan, '\blaP\d+\s*\.\s*\w+')) {
        $ln = LineOf $scan $raw.Index
        Fail "$src.hlsl line ~${ln}: raw '$($raw.Value)' -- read it by name (LA_<NAME>)"
    }
    foreach ($rd in [regex]::Matches((StripComments $scan), '\bLA_(\w+)\b')) {
        $n = $rd.Groups[1].Value
        if ($owner -ne 'kDisplaySrc') { Fail "LA_$n is read in $src.hlsl ($owner), which is compiled without kAcidSlotMacros" }
        if ($byName.ContainsKey($n)) { $byName[$n].R++ } else { Fail "$src.hlsl reads LA_$n, which is not in the table" }
    }
}

# ---- 5. cross-check -------------------------------------------------------------
foreach ($r in $rows) {
    $at = "LA_$($r.Name) (laP$($r.Vec).$($r.Comp))"
    if ($r.W -eq 0 -and $r.R -gt 0) { Fail "$at is read $($r.R)x but never written" }
    elseif ($r.W -eq 0) { Fail "$at is in the table but neither written nor read -- drop the row" }
    elseif ($r.R -eq 0) { Fail "$at is written but never read" }
    if ($r.W -gt 1) { Fail "$at is written $($r.W) times" }
}

# ---- report -----------------------------------------------------------------------
if ($Table) {
    '{0,-8} {1,-22} {2,2} {3,3}  {4}' -f 'slot', 'name', 'W', 'R', 'doc'
    foreach ($r in ($rows | Sort-Object Vec, @{ Expression = { 'xyzw'.IndexOf($_.Comp) } })) {
        '{0,-8} {1,-22} {2,2} {3,3}  {4}' -f "laP$($r.Vec).$($r.Comp)", $r.Name, $r.W, $r.R, $r.Doc
    }
    ''
}
$free = @()
for ($v = 0; $v -lt $nVec; $v++) { foreach ($c in $comps) { if (-not $byKey.ContainsKey("$v.$c")) { $free += "laP$v.$c" } } }
$reads = ($rows | Measure-Object -Property R -Sum).Sum
$top = @($lits | Sort-Object Bytes -Descending | Select-Object -First 3)
"slot-check: $($rows.Count) named slots in laP0..laP$($nVec - 1), $reads reads; free: $($free.Count) ($($free -join ' '))"
"slot-check: AcidParamsGPU = cbuffer AcidCB = $($cppFloat4 * 16) bytes, $($cppMembers.Count) members in the same order"
"slot-check: $($lits.Count) generated literal pieces; largest " + (($top | ForEach-Object { "$($_.Bytes) ($($_.Line))" }) -join ', ') + " -- limit $LiteralMax, MSVC cap 16380"
if ($fixedNow) { "slot-check: rewrote the generated cbuffer table in src/shaders/display.hlsl" }
if ($fails.Count -gt 0) {
    foreach ($f in $fails) { "FAIL: $f" }
    "slot-check: FAILED ($($fails.Count))"
    exit 1
}
"slot-check: PASS"
exit 0
