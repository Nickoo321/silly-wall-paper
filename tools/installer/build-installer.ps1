# Build the Fluid Wallpaper package for a friend: dist\FluidWallpaper-<version>.zip
# and dist\FluidWallpaper-Setup-<version>.exe (IExpress), from what is live now.
#
#   tools\installer\build-installer.ps1 [-Exe <path>] [-Settings <ini>] [-PresetsDir <dir>]
#                                       [-Version <text>] [-OutDir <dir>]
#
# Defaults: -Exe build2\live\FluidWallpaper.exe, -Settings / -PresetsDir = the
# owner's live %APPDATA%\FluidWallpaper (read only), -Version <yyyy.MM.dd>-<git short hash>,
# -OutDir dist. See tools\installer\README.md. Re-running it is the whole update procedure.
#
# Never runs the exe, never writes outside -OutDir.
[CmdletBinding()]
param(
    [string]$Exe = '',
    [string]$Settings = '',
    [string]$PresetsDir = '',
    [string]$Version = '',
    [string]$Commit = '',
    [string]$OutDir = '',
    [string]$ToggleShortcut = '',      # the owner's on/off shortcut (its hotkey is carried over)
    [string]$Hotkey = '',              # override; 'none' = no hotkey
    [int]$ExpectWeEvery = 2,           # source sanity check (today's live cycle)
    [string]$ExpectOilLayout = 'Layout - Few giants',
    [switch]$NoSourceCheck,
    [switch]$LookPeakAuto,             # kept for old command lines; stripping is the default now
    [switch]$KeepLookPeak,             # ship the looks' / presets' own [hdr] peak_nits (the owner's panel values)
    [switch]$NoSetupExe
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
# A stage file's [hdr] peak_nits overrides the user's own value while that stage runs
# (src/cycle.cpp Compose: "the stage's value when it carries one (file over base)"), and
# monotone-post-0924.ini (the base of 15 stages) carries the owner's 1055. Left in, the
# friend's peak_nits=-1 (auto) would almost never apply, so the looks / presets lose it.
$LookPeakAuto = -not $KeepLookPeak

$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
$liveData = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'FluidWallpaper'
if (-not $Exe)        { $Exe = Join-Path $repo 'build2\live\FluidWallpaper.exe' }
if (-not $Settings)   { $Settings = Join-Path $liveData 'settings.ini' }
if (-not $PresetsDir) { $PresetsDir = Join-Path $liveData 'presets' }
if (-not $OutDir)     { $OutDir = Join-Path $repo 'dist' }
if (-not $ToggleShortcut) { $ToggleShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Oil on-off.lnk' }
$Exe = (Resolve-Path $Exe).Path
$Settings = (Resolve-Path $Settings).Path
$PresetsDir = (Resolve-Path $PresetsDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

if (-not $Commit) {
    try { $Commit = (& git -C $repo rev-parse --short=7 HEAD 2>$null) } catch { $Commit = '' }
    if (-not $Commit) { $Commit = '4de5a67' }
}
if (-not $Version) { $Version = (Get-Date -Format 'yyyy.MM.dd') + '-' + $Commit }
if ($Version -notmatch '^[A-Za-z0-9._-]+$') { throw "Version '$Version' must be letters, digits, . _ - only" }

$failures = New-Object System.Collections.Generic.List[string]
function Fail([string]$m) { $failures.Add($m); Write-Host "  FAIL  $m" -ForegroundColor Red }
function Info([string]$m) { Write-Host "  $m" }

# ---------------------------------------------------------------------------
# ini text helpers: keep each file's encoding (UTF-16 LE with/without BOM, or
# bytes as Latin-1 which round-trips UTF-8 / ANSI unchanged)
# ---------------------------------------------------------------------------
$latin1 = [Text.Encoding]::GetEncoding(28591)
function Read-Ini([string]$path) {
    $b = $null
    for ($i = 0; $i -lt 10; $i++) {
        try { $b = [IO.File]::ReadAllBytes($path); break } catch { Start-Sleep -Milliseconds 200 }
    }
    if ($null -eq $b) { throw "cannot read $path" }
    $enc = 'latin1'
    if ($b.Length -ge 2 -and $b[0] -eq 0xFF -and $b[1] -eq 0xFE) { $enc = 'utf16bom'; $t = [Text.Encoding]::Unicode.GetString($b, 2, $b.Length - 2) }
    elseif ($b.Length -ge 2 -and $b[0] -ne 0 -and $b[1] -eq 0) { $enc = 'utf16'; $t = [Text.Encoding]::Unicode.GetString($b) }
    else { $t = $latin1.GetString($b) }
    $nl = "`r`n"
    if ($t.IndexOf("`r`n") -lt 0 -and $t.IndexOf("`n") -ge 0) { $nl = "`n" }
    $endsNl = $t.EndsWith("`n")
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($l in ($t -split "`r?`n")) { $lines.Add($l) }
    if ($endsNl) { $lines.RemoveAt($lines.Count - 1) }
    [pscustomobject]@{ Path = $path; Enc = $enc; NL = $nl; EndsNl = $endsNl; Lines = $lines }
}
function Write-Ini($ini, [string]$path) {
    $t = [string]::Join($ini.NL, $ini.Lines.ToArray())
    if ($ini.EndsNl) { $t += $ini.NL }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    switch ($ini.Enc) {
        'utf16bom' { $b = [byte[]](0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes($t) }
        'utf16'    { $b = [Text.Encoding]::Unicode.GetBytes($t) }
        default    { $b = $latin1.GetBytes($t) }
    }
    [IO.File]::WriteAllBytes($path, [byte[]]$b)
}
function Get-SecName([string]$line) {
    $s = $line.Trim()
    if ($s.StartsWith('[') -and $s.Contains(']')) { return $s.Substring(1, $s.IndexOf(']') - 1).Trim() }
    return $null
}
# indices of the key lines of [sec] (all occurrences of the section); $key = $null for all keys
function Find-Keys($ini, [string]$sec, [string]$key) {
    $out = @(); $in = $false
    for ($i = 0; $i -lt $ini.Lines.Count; $i++) {
        $l = $ini.Lines[$i]
        $s = Get-SecName $l
        if ($null -ne $s) { $in = ($s -ieq $sec); continue }
        if (-not $in) { continue }
        $t = $l.TrimStart()
        if ($t.StartsWith(';') -or -not $t.Contains('=')) { continue }
        $k = $t.Substring(0, $t.IndexOf('=')).Trim()
        if (-not $key -or $k -ieq $key) { $out += $i }
    }
    return ,$out
}
function Get-LineKey([string]$l) { $t = $l.TrimStart(); return $t.Substring(0, $t.IndexOf('=')).Trim() }
function Get-LineVal([string]$l) { $t = $l.TrimStart(); return $t.Substring($t.IndexOf('=') + 1).Trim() }
function Get-IniVal($ini, [string]$sec, [string]$key) {
    $ix = Find-Keys $ini $sec $key
    if ($ix.Count -eq 0) { return '' }
    return (Get-LineVal $ini.Lines[$ix[0]])
}
function Set-IniVal($ini, [string]$sec, [string]$key, [string]$val) {
    $ix = Find-Keys $ini $sec $key
    if ($ix.Count -gt 0) {
        $l = $ini.Lines[$ix[0]]
        $ini.Lines[$ix[0]] = $l.Substring(0, $l.IndexOf('=') + 1) + $val
        for ($j = $ix.Count - 1; $j -ge 1; $j--) { $ini.Lines.RemoveAt($ix[$j]) }
        return
    }
    for ($i = 0; $i -lt $ini.Lines.Count; $i++) {
        $s = Get-SecName $ini.Lines[$i]
        if ($null -ne $s -and $s -ieq $sec) { $ini.Lines.Insert($i + 1, "$key=$val"); return }
    }
    $ini.Lines.Add("[$sec]"); $ini.Lines.Add("$key=$val")
}
function Remove-IniKey($ini, [string]$sec, [string]$key) {
    $ix = Find-Keys $ini $sec $key
    for ($j = $ix.Count - 1; $j -ge 0; $j--) { $ini.Lines.RemoveAt($ix[$j]) }
    return $ix.Count
}
function Remove-KeyAnySection($ini, [string]$key) {
    $n = 0
    for ($i = $ini.Lines.Count - 1; $i -ge 0; $i--) {
        $t = $ini.Lines[$i].TrimStart()
        if ($t.StartsWith(';') -or $t.StartsWith('[') -or -not $t.Contains('=')) { continue }
        if ((Get-LineKey $t) -ieq $key) { $ini.Lines.RemoveAt($i); $n++ }
    }
    return $n
}
function Remove-Comments($ini, [string]$onlyMatching) {
    for ($i = $ini.Lines.Count - 1; $i -ge 0; $i--) {
        if ($ini.Lines[$i].TrimStart().StartsWith(';')) {
            if (-not $onlyMatching -or $ini.Lines[$i] -match $onlyMatching) { $ini.Lines.RemoveAt($i) }
        }
    }
}

# The app's path rule (src/cycle.cpp Resolve, src/main.cpp PresetMetaBase): '/' -> '\',
# absolute = "X:..." or "\\...", else relative to the folder of the ini that names it.
function Test-AbsPath([string]$p) { return ($p.Length -gt 1 -and ($p[1] -eq ':' -or ($p[0] -eq '\' -and $p[1] -eq '\') -or ($p[0] -eq '/' -and $p[1] -eq '/'))) }
function Resolve-AppPath([string]$rel, [string]$iniPath) {
    if (-not $rel) { return '' }
    $p = $rel.Replace('/', '\')
    if (-not (Test-AbsPath $p)) { $p = (Split-Path -Parent $iniPath) + '\' + $p }
    return [IO.Path]::GetFullPath($p)
}
function Get-RelPath([string]$fromDir, [string]$toFile) {
    $u1 = New-Object Uri (($fromDir.TrimEnd('\')) + '\')
    $u2 = New-Object Uri $toFile
    return [Uri]::UnescapeDataString($u1.MakeRelativeUri($u2).ToString()).Replace('/', '\')
}
$forbidden = 'abg77|OneDrive|C:\\Users'

Write-Host "Fluid Wallpaper package $Version"
Info "exe:      $Exe"
Info "settings: $Settings"
Info "presets:  $PresetsDir"

# ---------------------------------------------------------------------------
# 0. source sanity
# ---------------------------------------------------------------------------
$src = Read-Ini $Settings
if ((Find-Keys $src 'cycle' $null).Count -eq 0) { throw "source settings.ini has no [cycle] section: stop" }
if (-not $NoSourceCheck) {
    $we = Get-IniVal $src 'cycle' 'we_every'
    $ol = Get-IniVal $src 'cycle' 'oil_layout'
    if ($we -ne [string]$ExpectWeEvery) { throw "source sanity: [cycle] we_every='$we', expected $ExpectWeEvery (pass -ExpectWeEvery or -NoSourceCheck if the cycle changed on purpose)" }
    if ($ol -notlike "*$ExpectOilLayout*") { throw "source sanity: [cycle] oil_layout='$ol' does not name '$ExpectOilLayout' (pass -ExpectOilLayout or -NoSourceCheck)" }
}
$srcStageCount = [int](Get-IniVal $src 'cycle' 'stage_count')
$srcStageFiles = 0
for ($k = 1; $k -le $srcStageCount; $k++) { if (Get-IniVal $src 'cycle' "stage_${k}_file") { $srcStageFiles++ } }
Info "source cycle: stage_count=$srcStageCount, $srcStageFiles stage file(s)"

# ---------------------------------------------------------------------------
# 1. stage folder
# ---------------------------------------------------------------------------
$stage = Join-Path $OutDir 'stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
$stProg = Join-Path $stage 'program'
$stData = Join-Path $stage 'data'
New-Item -ItemType Directory -Force -Path $stProg, $stData | Out-Null

# ---------------------------------------------------------------------------
# 2. look files: every [cycle] file / base / oil_layout + their [meta] base chain
# ---------------------------------------------------------------------------
$looks = [ordered]@{}     # lower(source full path) -> @{ Src; Rel }
function Add-Look([string]$srcPath, [int]$depth) {
    $key = $srcPath.ToLowerInvariant()
    if ($looks.Contains($key)) { return $looks[$key].Rel }
    if (-not (Test-Path -LiteralPath $srcPath -PathType Leaf)) { Fail "look file not found: $srcPath"; return '' }
    if ($srcPath -match '\\photos\\' -or $srcPath -match '\\moods\\') { Fail "refusing to ship from photos\ or moods\: $srcPath"; return '' }
    $parent = Split-Path -Leaf (Split-Path -Parent $srcPath)
    $rel = 'looks\' + $parent + '\' + (Split-Path -Leaf $srcPath)
    foreach ($v in $looks.Values) { if ($v.Rel -ieq $rel) { Fail "two different files map to $rel"; return '' } }
    $looks[$key] = @{ Src = $srcPath; Rel = $rel }
    $ini = Read-Ini $srcPath
    if ((Find-Keys $ini 'photo' $null).Count -gt 0 -or ($ini.Lines | Where-Object { (Get-SecName $_) -ieq 'photo' })) {
        Fail "photo stage files are not shipped: $srcPath"
    }
    $mb = Get-IniVal $ini 'meta' 'base'
    if ($mb) {
        if ($depth -ge 8) { Fail "[meta] base chain too deep at $srcPath" }
        else { [void](Add-Look (Resolve-AppPath $mb $srcPath) ($depth + 1)) }
    }
    return $rel
}

$set = Read-Ini $Settings
Remove-Comments $set ''
foreach ($i in (Find-Keys $set 'cycle' $null)) {
    $l = $set.Lines[$i]; $k = Get-LineKey $l; $v = Get-LineVal $l
    if ($k -match '^stage_\d+_journey$' -and $v) { Fail "[cycle] ${k}: journeys are not packaged yet" }
    if ($k -match '^stage_\d+_photo$' -and $v -ne '0') { Fail "[cycle] ${k}: photo stages are not shipped" }
    if ($k -match '^stage_\d+_(file|base)$' -or $k -ieq 'oil_layout') {
        if (-not $v) { continue }
        $rel = Add-Look (Resolve-AppPath $v $Settings) 0
        if ($rel) { $set.Lines[$i] = $l.Substring(0, $l.IndexOf('=') + 1) + $rel }
    }
}
[void](Remove-IniKey $set 'cycle' 'current')
Set-IniVal $set 'hdr' 'peak_nits' '-1'
Set-IniVal $set 'general' 'mirror_second' '0'
foreach ($k in @('active_preset', 'overlays', 'window')) { [void](Remove-IniKey $set 'ui' $k) }
# anything else holding an absolute path, or pointing at photos / moods: drop it
$sec = ''
$drop = @()
for ($i = 0; $i -lt $set.Lines.Count; $i++) {
    $l = $set.Lines[$i]; $s = Get-SecName $l
    if ($null -ne $s) { $sec = $s; continue }
    if (-not $l.Contains('=')) { continue }
    $v = Get-LineVal $l
    if ((Test-AbsPath $v) -or $v -match '(^|[\\/])(photos|moods)([\\/]|$)') { $drop += $i; Info "dropped [$sec] $(Get-LineKey $l) (path: $v)" }
}
for ($j = $drop.Count - 1; $j -ge 0; $j--) { $set.Lines.RemoveAt($drop[$j]) }
$stSettings = Join-Path $stData 'settings.ini'
Write-Ini $set $stSettings

# write the look files; [meta] base rewritten when it would not resolve to the staged copy
foreach ($v in $looks.Values) {
    $ini = Read-Ini $v.Src
    Remove-Comments $ini $forbidden
    if ($LookPeakAuto) { [void](Remove-IniKey $ini 'hdr' 'peak_nits') }
    $dst = Join-Path $stData $v.Rel
    $mb = Get-IniVal $ini 'meta' 'base'
    if ($mb) {
        $target = $looks[(Resolve-AppPath $mb $v.Src).ToLowerInvariant()]
        if ($target) {
            $want = Join-Path $stData $target.Rel
            if ((Resolve-AppPath $mb $dst) -ne [IO.Path]::GetFullPath($want)) {
                $new = Get-RelPath (Split-Path -Parent $dst) $want
                Set-IniVal $ini 'meta' 'base' $new
                Info "rewrote [meta] base in $($v.Rel): $mb -> $new"
            }
        }
    }
    Write-Ini $ini $dst
}
Info "looks: $($looks.Count) file(s)"

# ---------------------------------------------------------------------------
# 3. tray presets (machine keys removed)
# ---------------------------------------------------------------------------
$stPresets = Join-Path $stData 'presets'
New-Item -ItemType Directory -Force -Path $stPresets | Out-Null
$nPresets = 0
foreach ($f in (Get-ChildItem -LiteralPath $PresetsDir -Filter '*.ini' -File)) {
    $ini = Read-Ini $f.FullName
    $n = 0
    foreach ($k in @('sim_res', 'dye_res', 'fps_limit', 'mirror_second')) { $n += (Remove-KeyAnySection $ini $k) }
    if ($LookPeakAuto) { [void](Remove-IniKey $ini 'hdr' 'peak_nits') }
    Remove-Comments $ini $forbidden
    $mb = Get-IniVal $ini 'meta' 'base'
    if ($mb) {
        $t = Resolve-AppPath $mb $f.FullName
        if ((Split-Path -Parent $t) -ine $PresetsDir.TrimEnd('\')) { Fail "preset $($f.Name): [meta] base points outside the presets folder ($mb)" }
    }
    Write-Ini $ini (Join-Path $stPresets $f.Name)
    $nPresets++
    if ($n) { Info "preset $($f.Name): $n machine key line(s) removed" }
}
Info "presets: $nPresets"

# ---------------------------------------------------------------------------
# 4. program files + installer scripts
# ---------------------------------------------------------------------------
Copy-Item -LiteralPath $Exe -Destination (Join-Path $stProg 'FluidWallpaper.exe')
function Copy-Text([string]$from, [string]$to, [hashtable]$repl) {
    $t = [IO.File]::ReadAllText($from)
    if ($repl) { foreach ($k in $repl.Keys) { $t = $t.Replace($k, $repl[$k]) } }
    $t = ($t -replace "`r`n", "`n") -replace "`n", "`r`n"          # CRLF (cmd needs it)
    foreach ($ch in $t.ToCharArray()) { if ([int]$ch -gt 127) { Fail "non-ASCII text in $from"; break } }
    [IO.File]::WriteAllText($to, $t, (New-Object Text.ASCIIEncoding))
}
# hotkey of the owner's shortcut
$hk = ''
if ($Hotkey) { if ($Hotkey -ne 'none') { $hk = $Hotkey } }
elseif (Test-Path -LiteralPath $ToggleShortcut) {
    try { $hk = (New-Object -ComObject WScript.Shell).CreateShortcut($ToggleShortcut).Hotkey } catch { $hk = '' }
}
if ($hk) { Info "hotkey: $hk" } else { Info 'hotkey: none' }
$hkText = 'none set'
if ($hk) { $hkText = ($hk -replace '^Alt\+Ctrl\+', 'Ctrl+Alt+') + ' (works while the desktop shortcut exists)' }
foreach ($n in @('fw-common.ps1', 'toggle.ps1', 'toggle.vbs', 'uninstall.ps1')) { Copy-Text (Join-Path $here $n) (Join-Path $stProg $n) $null }
# the icon of the "next mode" shortcut (install.ps1 falls back to the exe icon without it)
$skipIco = Join-Path $repo 'assets\skip-next.ico'
if (Test-Path -LiteralPath $skipIco) { Copy-Item -LiteralPath $skipIco -Destination (Join-Path $stProg 'skip-next.ico') -Force }
else { Info 'assets\skip-next.ico not found: the next shortcut will use the exe icon' }
Copy-Text (Join-Path $here 'README.txt') (Join-Path $stProg 'README.txt') @{ '{HOTKEY}' = $hkText }
Copy-Text (Join-Path $here 'README.txt') (Join-Path $stage 'README.txt') @{ '{HOTKEY}' = $hkText }
Copy-Text (Join-Path $here 'install.ps1') (Join-Path $stage 'install.ps1') $null
Copy-Text (Join-Path $here 'Install.cmd') (Join-Path $stage 'Install.cmd') $null
[IO.File]::WriteAllText((Join-Path $stProg 'version.txt'),
    "$Version`r`nbuilt $(Get-Date -Format 'yyyy-MM-dd HH:mm') from commit $Commit`r`n", (New-Object Text.ASCIIEncoding))
if ($hk) { [IO.File]::WriteAllText((Join-Path $stProg 'hotkey.txt'), "$hk`r`n", (New-Object Text.ASCIIEncoding)) }

# ---------------------------------------------------------------------------
# 5. checks (each one fails the build)
# ---------------------------------------------------------------------------
Write-Host 'checks:'
$stDataFull = [IO.Path]::GetFullPath($stData).TrimEnd('\') + '\'
function Test-Staged([string]$p, [string]$what) {
    if (-not $p) { Fail "${what}: empty"; return $false }
    if (-not $p.StartsWith($stDataFull, [StringComparison]::OrdinalIgnoreCase)) { Fail "$what resolves outside the package: $p"; return $false }
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail "$what does not resolve to a staged file: $p"; return $false }
    return $true
}
$chk = Read-Ini $stSettings
$cnt = [int](Get-IniVal $chk 'cycle' 'stage_count')
$files = 0
for ($k = 1; $k -le $cnt; $k++) {
    $f = Get-IniVal $chk 'cycle' "stage_${k}_file"
    if (-not $f) { continue }
    $files++
    $fp = Resolve-AppPath $f $stSettings
    if (-not (Test-Staged $fp "stage $k file")) { continue }
    $b = Get-IniVal $chk 'cycle' "stage_${k}_base"
    if ($b) { [void](Test-Staged (Resolve-AppPath $b $stSettings) "stage $k base") }
    else {
        $mb = Get-IniVal (Read-Ini $fp) 'meta' 'base'      # cycle.cpp ResolveStage: relative to the FILE
        if ($mb) { [void](Test-Staged (Resolve-AppPath $mb $fp) "stage $k [meta] base") }
    }
}
$ol = Get-IniVal $chk 'cycle' 'oil_layout'
if ($ol) { [void](Test-Staged (Resolve-AppPath $ol $stSettings) 'oil_layout') }
foreach ($f in (Get-ChildItem -LiteralPath $stData -Recurse -Filter '*.ini' -File)) {
    $fi = Read-Ini $f.FullName
    $mb = Get-IniVal $fi 'meta' 'base'
    if ($mb) { [void](Test-Staged (Resolve-AppPath $mb $f.FullName) "[meta] base of $($f.Name)") }
    foreach ($l in $fi.Lines) {
        if ($l.TrimStart().StartsWith(';') -or -not $l.Contains('=')) { continue }
        if (Test-AbsPath (Get-LineVal $l)) { Fail "absolute path in $($f.Name): $l" }
    }
    if ($LookPeakAuto -and $f.FullName -ne $stSettings -and (Find-Keys $fi 'hdr' 'peak_nits').Count -gt 0) {
        Fail "[hdr] peak_nits left in $($f.Name) (it would override the friend's auto peak)"
    }
}
if ($cnt -ne $srcStageCount -or $files -ne $srcStageFiles) { Fail "stage count: staged $cnt/$files, source $srcStageCount/$srcStageFiles" }
else { Info "stage count $cnt = source" }
if ((Get-IniVal $chk 'hdr' 'peak_nits') -ne '-1') { Fail 'staged settings.ini: [hdr] peak_nits is not -1' }
foreach ($l in $chk.Lines) {
    if ($l -match '[A-Za-z]:[\\/]' -or $l -match '\\\\') { Fail "staged settings.ini holds an absolute path: $l" }
}
if (Get-IniVal $chk 'cycle' 'current') { Fail 'staged settings.ini still has [cycle] current' }
# privacy
$textExt = @('.ini', '.ps1', '.vbs', '.txt', '.cmd', '.md')
foreach ($f in (Get-ChildItem -LiteralPath $stage -Recurse -File)) {
    if ($f.FullName -match '\\(photos|moods)\\') { Fail "photos/moods content staged: $($f.FullName)" }
    if ($textExt -contains $f.Extension.ToLowerInvariant()) {
        $txt = [string]::Join("`n", (Read-Ini $f.FullName).Lines.ToArray())
        if ($txt -match $forbidden) { Fail "privacy: '$($Matches[0])' in $($f.FullName.Substring($stage.Length))" }
    } else {
        $b = [IO.File]::ReadAllBytes($f.FullName)
        foreach ($t in @($latin1.GetString($b), [Text.Encoding]::Unicode.GetString($b), [Text.Encoding]::Unicode.GetString($b, 1, $b.Length - 1))) {
            if ($t -match 'abg77') { Fail "privacy: 'abg77' inside $($f.Name)"; break }
        }
    }
}
if ($failures.Count -gt 0) { throw "BUILD FAILED: $($failures.Count) check(s) failed (see above)" }
Info 'all checks passed'

# ---------------------------------------------------------------------------
# 6. zip (forward-slash entry names) and the IExpress Setup exe
# ---------------------------------------------------------------------------
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
function New-Zip([string]$dir, [string]$zipPath) {
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    $fs = [IO.File]::Open($zipPath, 'CreateNew')
    $zip = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $root = [IO.Path]::GetFullPath($dir).TrimEnd('\') + '\'
        foreach ($f in (Get-ChildItem -LiteralPath $dir -Recurse -File | Sort-Object FullName)) {
            $name = $f.FullName.Substring($root.Length).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $name, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $zip.Dispose(); $fs.Dispose() }
}
$zipOut = Join-Path $OutDir "FluidWallpaper-$Version.zip"
New-Zip $stage $zipOut
Info "zip:   $zipOut ($([math]::Round((Get-Item $zipOut).Length / 1MB, 2)) MB)"

if (-not $NoSetupExe) {
    # IExpress cannot hold folders: the Setup exe carries payload.zip + Install.cmd,
    # and Install.cmd unpacks the zip to %TEMP% and runs install.ps1 from it.
    $work = Join-Path $env:TEMP ('fwsfx-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    try {
        Copy-Item -LiteralPath $zipOut -Destination (Join-Path $work 'payload.zip')
        Copy-Item -LiteralPath (Join-Path $stage 'Install.cmd') -Destination (Join-Path $work 'Install.cmd')
        $target = Join-Path $work 'setup.exe'
        $sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$target
FriendlyName=Fluid Wallpaper $Version Setup
AppLaunched=cmd.exe /c .\Install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=cmd.exe /c .\Install.cmd -Quiet
UserQuietInstCmd=cmd.exe /c .\Install.cmd -Quiet
FILE0="payload.zip"
FILE1="Install.cmd"
[SourceFiles]
SourceFiles0=$work\
[SourceFiles0]
%FILE0%=
%FILE1%=
"@
        $sedPath = Join-Path $work 'setup.sed'
        [IO.File]::WriteAllText($sedPath, ($sed -replace "`r?`n", "`r`n"), (New-Object Text.ASCIIEncoding))
        $p = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\iexpress.exe') -ArgumentList '/N /Q setup.sed' -WorkingDirectory $work -PassThru -Wait -WindowStyle Hidden   # a quoted absolute SED path makes iexpress exit 1
        if (-not (Test-Path -LiteralPath $target)) { throw "iexpress did not produce $target (exit $($p.ExitCode))" }
        $b = [IO.File]::ReadAllBytes($target)
        foreach ($t in @($latin1.GetString($b), [Text.Encoding]::Unicode.GetString($b))) {
            if ($t -match 'abg77') { throw "privacy: 'abg77' inside the Setup exe" }
        }
        $exeOut = Join-Path $OutDir "FluidWallpaper-Setup-$Version.exe"
        Copy-Item -LiteralPath $target -Destination $exeOut -Force
        Info "setup: $exeOut ($([math]::Round((Get-Item $exeOut).Length / 1MB, 2)) MB)"
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Write-Host "done: $Version"
