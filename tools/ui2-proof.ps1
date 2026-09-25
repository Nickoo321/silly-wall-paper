# ui2-proof.ps1 -- UI-REHAUL phase 1b proof block: headless settings-window shots + dumps.
# WARP only (--ui-shot / --ui-dump): no GPU render, no window on screen, config read-only.
# Preset-file operations run ONLY inside a throwaway copy of reference\presets under -Out
# (--ui-presets-dir): the user's %APPDATA% presets are never read for writing, never touched.
# Usage: powershell -File tools\ui2-proof.ps1 -Exe build2\FluidWallpaper.exe [-Out <dir>]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Out = 'C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\build2\shots\live\ui2'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exePath = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$pd = Join-Path $Out 'presets'
if (Test-Path $pd) { Remove-Item -Recurse -Force $pd }
Copy-Item -Recurse (Join-Path $root 'reference\presets') $pd

function Ini([string]$rel) { return (Join-Path $root $rel) }
function Q([string]$s) { if ($s -match '[\s;]') { return '"' + $s + '"' } else { return $s } }
function UiRun([string]$name, [string]$ini, [string[]]$extra) {
    $png = Join-Path $Out "$name.png"
    $json = Join-Path $Out "$name.json"
    $log = Join-Path $Out "$name.log"
    $a = @('--ui-shot', $png, '--ui-dump', $json, '--ini', $ini, '--hdr', 'on', '--panel-max', '1000',
           '--ui-presets-dir', $pd, '--ui-size', '1600x1150') + $extra
    $p = Start-Process -FilePath $exePath -ArgumentList ($a | ForEach-Object { Q $_ }) -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $log
    $j = Get-Content -Raw $json | ConvertFrom-Json
    Write-Host ("{0,-26} exit={1} pane={2} dirty={3} | {4}" -f $name, $p.ExitCode, $j.pane, $j.header.dirty, $j.header.text)
    return $j
}

$acid = Ini 'reference\configs\acid-rise-12.ini'
$we = Ini 'reference\configs\we-look-live.ini'
$ink = Ini 'reference\configs\ink-inverted.ini'
$cyc = Ini 'reference\configs\cycle-first.ini'

$r = @{}
$r.acid  = UiRun 'pane-liquid-acid' $acid @()
$r.fluid = UiRun 'pane-fluid' $we @()
$r.ink   = UiRun 'pane-ink' $ink @()
$r.cycle = UiRun 'pane-cycle-playlist' $cyc @('--ui-script', 'cycle stage 3 130 paused')
$r.edit  = UiRun 'header-cycle-stage3-edit' $cyc @('--ui-script', 'cycle stage 3 130 paused; pane A')
$r.ghost = UiRun 'ghost-tick-focus' $acid @('--ui-script', 'live rig.focus 0.72 moving; live hue2 150 moving')
$r.frz   = UiRun 'freeze-rig' $acid @('--ui-script', 'live rig.focus 0.72 moving; freeze rig')
# Save as = PARTIAL overlay of the changed keys on its [meta] base (a library preset), then Save
# (a partial in-place update) on it, then a Recycle-Bin delete of a temp copy (dry run + real)
$base = 'Liquid Acid - rising colours'
$r.save  = UiRun 'save-as-partial' $acid @('--ui-script',
    "applyname $base; set post.bloom 0.5; set liquid_acid.film_level 0.6; set liquid_acid.rise_speed 0.03; saveas UI2 test partial")
$r.save2 = UiRun 'save-partial-inplace' $acid @('--ui-script',
    "applyname UI2 test partial; set post.fog 0.4; save")
$r.del   = UiRun 'delete-recycle-bin' $acid @('--ui-script',
    "duplicate UI2 test partial; delete-dry UI2 test partial copy; delete UI2 test partial copy")

$written = Join-Path $pd 'UI2 test partial.ini'
"`n==== written overlay: $written"
Get-Content $written
"`n==== file ops (save-as / save / delete)"
$r.save.file_ops; $r.save2.file_ops; $r.del.file_ops
"`n==== temp copy still on disk: " + (Test-Path (Join-Path $pd 'UI2 test partial copy.ini'))
"==== ghost ticks: " + ($r.ghost.ghost_ticks | ConvertTo-Json -Compress)
"==== cycle header: " + $r.cycle.header.text
