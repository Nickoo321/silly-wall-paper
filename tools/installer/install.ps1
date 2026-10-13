# Fluid Wallpaper - installer / updater (per user, no admin rights needed).
# Runs from the unpacked package: program\ and data\ sit next to this file.
#
#   install.ps1 [-KeepSettings] [-Quiet]
#
# Test switches (so nothing real is touched):
#   -InstallDir <dir>   program folder   (default %LOCALAPPDATA%\Programs\FluidWallpaper)
#   -DataDir <dir>      data folder      (default %APPDATA%\FluidWallpaper - the app's fixed folder)
#   -ShortcutDir <dir>  shortcuts go to <dir>\Desktop and <dir>\StartMenu instead of the real ones
#   -NoRegistry         no autostart value, no Uninstall entry
#   -NoLaunch           do not start the wallpaper at the end
#   -NoStop             do not stop a running copy first
#   -Quiet              no message boxes
#
# Update: an existing settings.ini is copied to settings-backup-<yyyyMMdd-HHmmss>.ini
# and then replaced by the package's (unless -KeepSettings). Presets with the same
# name are replaced; the user's other presets stay.
[CmdletBinding()]
param(
    [string]$InstallDir = '',
    [string]$DataDir = '',
    [string]$ShortcutDir = '',
    [switch]$NoRegistry,
    [switch]$NoLaunch,
    [switch]$NoStop,
    [switch]$Quiet,
    [switch]$KeepSettings
)
$ErrorActionPreference = 'Stop'

$pkg = $PSScriptRoot
$srcProgram = Join-Path $pkg 'program'
$srcData = Join-Path $pkg 'data'
if (-not (Test-Path -LiteralPath (Join-Path $srcProgram 'FluidWallpaper.exe'))) {
    throw "Package incomplete: $srcProgram\FluidWallpaper.exe not found."
}
. (Join-Path $srcProgram 'fw-common.ps1')

if (-not $InstallDir) { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\FluidWallpaper' }
if (-not $DataDir)    { $DataDir = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'FluidWallpaper' }
if ($ShortcutDir) {
    $desktopDir = Join-Path $ShortcutDir 'Desktop'
    $startDir   = Join-Path $ShortcutDir 'StartMenu\Fluid Wallpaper'
} else {
    $desktopDir = [Environment]::GetFolderPath('Desktop')
    $startDir   = Join-Path ([Environment]::GetFolderPath('Programs')) 'Fluid Wallpaper'
}
$version = (Get-Content -LiteralPath (Join-Path $srcProgram 'version.txt') -TotalCount 1).Trim()

function Say([string]$t) { Write-Host $t }

Say "Fluid Wallpaper $version"
Say "  program: $InstallDir"
Say "  data:    $DataDir"

$exeDst = Join-Path $InstallDir 'FluidWallpaper.exe'
$isUpdate = Test-Path -LiteralPath $exeDst

# 1. stop a running copy (tray Exit first, then force)
if (-not $NoStop) {
    if (-not (Stop-FluidWallpaper 5)) { throw 'Could not stop the running FluidWallpaper.exe; close it from its tray icon and run setup again.' }
}

# 2. program files
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Get-ChildItem -LiteralPath $srcProgram -File | ForEach-Object {
    $d = Join-Path $InstallDir $_.Name
    Copy-Item -LiteralPath $_.FullName -Destination $d -Force
    try { Unblock-File -LiteralPath $d } catch { }   # drop the downloaded-file mark on the installed copy
}

# 3. data folder
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
$settingsDst = Join-Path $DataDir 'settings.ini'
$backup = ''
if (Test-Path -LiteralPath $settingsDst) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $DataDir ('settings-backup-' + $stamp + '.ini')
    for ($n = 2; Test-Path -LiteralPath $backup; $n++) { $backup = Join-Path $DataDir ('settings-backup-' + $stamp + '-' + $n + '.ini') }
    Copy-Item -LiteralPath $settingsDst -Destination $backup -Force
    Say "  settings backup: $backup"
    if (-not $KeepSettings) { Copy-Item -LiteralPath (Join-Path $srcData 'settings.ini') -Destination $settingsDst -Force }
    else { Say '  -KeepSettings: settings.ini left as it was' }
} else {
    Copy-Item -LiteralPath (Join-Path $srcData 'settings.ini') -Destination $settingsDst -Force
}
# presets\ and looks\ : same names replaced, everything else in there kept
foreach ($sub in @('presets', 'looks')) {
    $from = Join-Path $srcData $sub
    if (-not (Test-Path -LiteralPath $from)) { continue }
    Get-ChildItem -LiteralPath $from -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($from.Length).TrimStart('\')
        $d = Join-Path (Join-Path $DataDir $sub) $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $d) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $d -Force
    }
}

# 4. shortcuts
$wsh = New-Object -ComObject WScript.Shell
$wscript = Join-Path $env:SystemRoot 'System32\wscript.exe'
$psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$vbs = Join-Path $InstallDir 'toggle.vbs'
$hotkey = ''
$hkFile = Join-Path $srcProgram 'hotkey.txt'
if (Test-Path -LiteralPath $hkFile) { $hotkey = (Get-Content -LiteralPath $hkFile -TotalCount 1).Trim() }

function New-FwShortcut([string]$path, [string]$target, [string]$arguments, [string]$hk, [string]$desc, [string]$icon = '') {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    $s = $wsh.CreateShortcut($path)
    $s.TargetPath = $target
    $s.Arguments = $arguments
    $s.WorkingDirectory = $InstallDir
    if ($icon) { $s.IconLocation = "$icon,0" } else { $s.IconLocation = "$exeDst,0" }
    $s.Description = $desc
    if ($hk) { $s.Hotkey = $hk }
    $s.Save()
}
$toggleArgs = '"' + $vbs + '"'
New-FwShortcut (Join-Path $desktopDir 'Fluid Wallpaper on-off.lnk') $wscript $toggleArgs $hotkey 'Turn Fluid Wallpaper on or off'
New-FwShortcut (Join-Path $startDir 'Fluid Wallpaper on-off.lnk') $wscript $toggleArgs '' 'Turn Fluid Wallpaper on or off'
# "next mode": the exe's own --cycle-next switch messages the running copy and exits
# (it never starts a wallpaper; with none running it does nothing)
$nextIco = Join-Path $InstallDir 'skip-next.ico'
if (-not (Test-Path -LiteralPath $nextIco)) { $nextIco = '' }
$nextHotkey = ''
if ($hotkey) { $nextHotkey = 'CTRL+ALT+N' }     # only when the on-off shortcut carries a hotkey too
New-FwShortcut (Join-Path $desktopDir 'Fluid Wallpaper next.lnk') $exeDst '--cycle-next' $nextHotkey 'Skip to the next Fluid Wallpaper mode' $nextIco
New-FwShortcut (Join-Path $startDir 'Fluid Wallpaper next.lnk') $exeDst '--cycle-next' '' 'Skip to the next Fluid Wallpaper mode' $nextIco
New-FwShortcut (Join-Path $startDir 'Uninstall Fluid Wallpaper.lnk') $psExe ('-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $InstallDir 'uninstall.ps1') + '"') '' 'Remove Fluid Wallpaper'

# 5. registry: autostart (the app's own "start with Windows" value, src/ui/ui_model.cpp
#    SetAutostart: HKCU\...\Run, value FluidWallpaper = "<exe path>" in quotes) + Uninstall entry
if (-not $NoRegistry) {
    $run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $hadRun = $null -ne (Get-ItemProperty -Path $run -Name 'FluidWallpaper' -ErrorAction SilentlyContinue)
    if (-not $isUpdate -or $hadRun) {   # an update keeps the user's own "start with Windows" choice
        New-ItemProperty -Path $run -Name 'FluidWallpaper' -Value ('"' + $exeDst + '"') -PropertyType String -Force | Out-Null
    }
    $u = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FluidWallpaper'
    New-Item -Path $u -Force | Out-Null
    $vals = [ordered]@{
        DisplayName     = 'Fluid Wallpaper'
        DisplayVersion  = $version
        Publisher       = 'Fluid Wallpaper'
        InstallLocation = $InstallDir
        DisplayIcon     = "$exeDst,0"
        UninstallString = ('"' + $psExe + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $InstallDir 'uninstall.ps1') + '"')
    }
    foreach ($k in $vals.Keys) { New-ItemProperty -Path $u -Name $k -Value $vals[$k] -PropertyType String -Force | Out-Null }
    New-ItemProperty -Path $u -Name 'NoModify' -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $u -Name 'NoRepair' -Value 1 -PropertyType DWord -Force | Out-Null
}

# 6. start it
if (-not $NoLaunch) { Start-Process -FilePath $exeDst -WorkingDirectory $InstallDir }

$msg = "Fluid Wallpaper $version is installed." + $(if ($isUpdate) { ' (update)' } else { '' }) +
       "`r`n`r`nTurn it on or off with the 'Fluid Wallpaper on-off' shortcut on the desktop" +
       $(if ($hotkey) { " (" + ($hotkey -replace '^Alt\+Ctrl\+', 'Ctrl+Alt+') + ")" } else { '' }) +
       ".`r`nSkip to the next mode with 'Fluid Wallpaper next'" + $(if ($nextHotkey) { ' (Ctrl+Alt+N)' } else { '' }) +
       ".`r`nSettings: the Fluid Wallpaper tray icon." +
       $(if ($backup) { "`r`n`r`nYour previous settings were saved as`r`n$backup" } else { '' })
Say ''
Say $msg
if (-not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.MessageBox]::Show($msg, 'Fluid Wallpaper', 'OK', 'Information')
}
exit 0
