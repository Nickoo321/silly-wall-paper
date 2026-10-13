# Fluid Wallpaper - shared helpers for toggle.ps1 / install.ps1 / uninstall.ps1.
# Dot-sourced. Windows PowerShell 5.1 compatible. Keep this file pure ASCII.

# The tray window class and the Exit command id of FluidWallpaper.exe
# (src/main.cpp: wc.lpszClassName = L"FluidWallpaperTray"; CMD_EXIT = 3).
$script:FwTrayClass = 'FluidWallpaperTray'
$script:FwCmdExit   = 3

function Get-FwProcesses {
    @(Get-Process -Name FluidWallpaper -ErrorAction SilentlyContinue)
}

function Initialize-FwNative {
    if (-not ('FluidWp.Native' -as [type])) {
        Add-Type -Namespace 'FluidWp' -Name 'Native' -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr FindWindowW(string cls, string title);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PostMessageW(System.IntPtr hwnd, uint msg, System.IntPtr wp, System.IntPtr lp);
'@
    }
}

# Ask the running wallpaper to exit through its tray window (the same as tray
# menu -> Exit), wait up to $TimeoutSec, then Stop-Process whatever is left.
# Returns $true when no FluidWallpaper.exe is left running.
function Stop-FluidWallpaper([int]$TimeoutSec = 5) {
    if ((Get-FwProcesses).Count -eq 0) { return $true }
    try {
        Initialize-FwNative
        $hwnd = [FluidWp.Native]::FindWindowW($script:FwTrayClass, $null)
        if ($hwnd -ne [IntPtr]::Zero) {
            [void][FluidWp.Native]::PostMessageW($hwnd, 0x0111, [IntPtr]$script:FwCmdExit, [IntPtr]::Zero)  # WM_COMMAND
        }
    } catch { }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-FwProcesses).Count -gt 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    $left = Get-FwProcesses
    if ($left.Count -gt 0) {
        $left | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
    return ((Get-FwProcesses).Count -eq 0)
}
