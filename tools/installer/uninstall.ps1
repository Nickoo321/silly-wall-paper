# Fluid Wallpaper - uninstaller. Lives in the program folder.
#
#   uninstall.ps1 [-RemoveData] [-Quiet]
#
# Removes the program folder, the shortcuts, the autostart value and the
# Uninstall entry. The data folder (settings, presets) is removed only with
# -RemoveData, or when the user says Yes to the question (not asked with -Quiet).
# Test switches: -InstallDir <dir> -DataDir <dir> -ShortcutDir <dir> -NoRegistry -NoStop -Quiet
[CmdletBinding()]
param(
    [string]$InstallDir = '',
    [string]$DataDir = '',
    [string]$ShortcutDir = '',
    [switch]$NoRegistry,
    [switch]$NoStop,
    [switch]$NoLaunch,   # accepted for symmetry with install.ps1; unused
    [switch]$Quiet,
    [switch]$RemoveData
)
$ErrorActionPreference = 'Stop'

function Show-FwMessage([string]$text, [string]$icon) {
    if ($Quiet) { return }
    try {
        Add-Type -AssemblyName System.Windows.Forms
        [void][System.Windows.Forms.MessageBox]::Show($text, 'Fluid Wallpaper', 'OK', $icon)
    } catch { }
}
# Started from a shortcut, the console window closes right after the script ends: an
# error must reach the user as a message box, not only as red console text.
trap {
    Write-Host "Uninstall failed: $_"
    Show-FwMessage ("Uninstall did not finish:`r`n$_") 'Error'
    exit 1
}

if (-not $InstallDir) { $InstallDir = $PSScriptRoot }
if (-not $DataDir)    { $DataDir = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'FluidWallpaper' }
if ($ShortcutDir) {
    $desktopDir = Join-Path $ShortcutDir 'Desktop'
    $startDir   = Join-Path $ShortcutDir 'StartMenu\Fluid Wallpaper'
} else {
    $desktopDir = [Environment]::GetFolderPath('Desktop')
    $startDir   = Join-Path ([Environment]::GetFolderPath('Programs')) 'Fluid Wallpaper'
}

# sanity: never delete a folder that is not a Fluid Wallpaper program folder
if (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'FluidWallpaper.exe')) -and
    -not (Test-Path -LiteralPath (Join-Path $InstallDir 'version.txt'))) {
    if (Test-Path -LiteralPath $InstallDir) { throw "$InstallDir does not look like a Fluid Wallpaper folder; nothing removed." }
}

# The program folder is about to go. Set-Location alone does not move the PROCESS's
# current directory, and the Start Menu shortcut starts us with the program folder as
# working directory: that open handle kept the (emptied) folder from being removed.
Set-Location -LiteralPath $env:TEMP
[Environment]::CurrentDirectory = $env:TEMP

# 1. stop the wallpaper
if (-not $NoStop) {
    $common = Join-Path $InstallDir 'fw-common.ps1'
    if (Test-Path -LiteralPath $common) { . $common; [void](Stop-FluidWallpaper 5) }
    else { Get-Process -Name FluidWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force }
}

# 2. shortcuts
Remove-Item -LiteralPath (Join-Path $desktopDir 'Fluid Wallpaper on-off.lnk') -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $desktopDir 'Fluid Wallpaper next.lnk') -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $startDir) { Remove-Item -LiteralPath $startDir -Recurse -Force }

# 3. registry
if (-not $NoRegistry) {
    Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'FluidWallpaper' -ErrorAction SilentlyContinue
    $u = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FluidWallpaper'
    if (Test-Path $u) { Remove-Item -Path $u -Recurse -Force }
}

# 4. program folder
if (Test-Path -LiteralPath $InstallDir) {
    for ($i = 0; $i -lt 10; $i++) {
        try { Remove-Item -LiteralPath $InstallDir -Recurse -Force; break }
        catch { Start-Sleep -Milliseconds 500 }   # the exe may still be closing
    }
}

# 5. data folder: only on request
$removeData = [bool]$RemoveData
$asked = $false
if (-not $removeData -and -not $Quiet -and (Test-Path -LiteralPath $DataDir)) {
    $asked = $true
    Add-Type -AssemblyName System.Windows.Forms
    $r = [System.Windows.Forms.MessageBox]::Show(
        "Fluid Wallpaper is removed.`r`n`r`nAlso delete its settings and presets?`r`n$DataDir",
        'Fluid Wallpaper', 'YesNo', 'Question')
    $removeData = ($r -eq [System.Windows.Forms.DialogResult]::Yes)
}
if ($removeData -and (Test-Path -LiteralPath $DataDir)) {
    Remove-Item -LiteralPath $DataDir -Recurse -Force
    Write-Host "Removed $DataDir"
} else {
    Write-Host "Kept settings in $DataDir"
}
$left = Test-Path -LiteralPath $InstallDir
if ($left) {
    $m = "Could not remove everything in $InstallDir (a file is in use); delete it by hand."
    Write-Host $m
    Show-FwMessage $m 'Warning'
} else {
    Write-Host 'Fluid Wallpaper is uninstalled.'
    if (-not $asked) { Show-FwMessage 'Fluid Wallpaper is uninstalled.' 'Information' }
}
exit 0
