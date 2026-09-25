# ui-proof.ps1 -- UI-REHAUL phase 1a proof block: headless settings-window shots + dumps.
# WARP only (--ui-shot / --ui-dump): no GPU render, no window on screen, config read-only.
# Usage: powershell -File tools\ui-proof.ps1 -Exe build2\FluidWallpaper.exe [-Out <dir>]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Out = 'C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\build2\shots\live\ui1'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exePath = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null

function Ini([string]$rel) { return (Join-Path $root $rel) }
function UiRun([string]$name, [string]$ini, [string[]]$extra) {
    $png = Join-Path $Out "$name.png"
    $json = Join-Path $Out "$name.json"
    $args = @('--ui-shot', $png, '--ui-dump', $json, '--ini', $ini, '--hdr', 'on', '--panel-max', '1000') + $extra
    $p = Start-Process -FilePath $exePath -ArgumentList ($args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -Wait -PassThru -NoNewWindow
    $j = Get-Content -Raw $json | ConvertFrom-Json
    "{0,-28} exit={1} look={2} preset={3} dirty={4} neon={5} | total={6} front={7} visible={8} disabled={9} hidden={10}" -f `
        $name, $p.ExitCode, $j.header.look, $j.header.preset, $j.header.dirty, $j.neon_in_header,
        $j.counts.total, $j.counts.front, $j.counts.visible, $j.counts.disabled, $j.counts.hidden
    return $j
}

$acid = Ini 'reference\configs\acid-rise-12.ini'
$we = Ini 'reference\configs\we-look-live.ini'
$ink = Ini 'reference\configs\ink-inverted.ini'
$water = Ini 'reference\presets\Liquid Acid - oil on ink water.ini'
$weP = Ini 'reference\presets\WE parity (fluid).ini'
$mirror = Ini 'reference\presets\Mirror - quad (overlay).ini'
$inkP = Ini 'reference\presets\Ink - inverted.ini'

$r = @{}
$r.acid   = UiRun 'all-acid-rise-12' $acid @()
$r.we     = UiRun 'all-we-look-live' $we @()
$r.ink    = UiRun 'all-ink-inverted' $ink @()
$r.lid    = UiRun 'search-lid-lid0' $acid @('--ui-search', 'lid', '--ui-script', 'set post.lid 0')
$r.set    = UiRun 'header-set-dirty1' $acid @('--ui-script', 'set post.bloom 0.5')
$r.undo   = UiRun 'header-undo-dirty0' $acid @('--ui-script', 'set post.bloom 0.5; undo')
$r.water  = UiRun 'all-oil-on-ink-water' $water @()
$r.weP    = UiRun 'all-we-parity-preset' $weP @()
$r.inkP   = UiRun 'all-ink-inverted-preset' $inkP @()
$r.mirror = UiRun 'mirror-quad-over-acid' $acid @('--ui-script', "apply $mirror")
$r.cyc    = UiRun 'cycle-tile-we' $we @('--ui-script', 'set cycle.enabled 1')
$r.inh    = UiRun 'search-droplet-dye' $acid @('--ui-search', 'droplet dye')
$r.every  = UiRun 'everything-acid-rise-12' $acid @('--ui-chips', 'everything', '--ui-search', 'swarm')
