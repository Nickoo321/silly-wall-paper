# Set or read the PRIMARY monitor's brightness over DDC/CI (dxva2, VCP 0x10).
# usage:  powershell -File tools\oled-brightness.ps1        -> prints current
#         powershell -File tools\oled-brightness.ps1 30     -> sets 30 (0..100)
# Verified 2026-09-16 on the X27U OLED (primary): set 30, readback 30. Reversible; touches
# nothing else. The port's tray window (class FluidWallpaperTray) is unaffected.
param([int]$Level = -1)
$code = @'
using System; using System.Runtime.InteropServices;
public class DdcB {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct PM { public IntPtr h; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string desc; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
  [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT p, uint f);
  [DllImport("dxva2.dll", SetLastError=true)] public static extern bool GetNumberOfPhysicalMonitorsFromHMONITOR(IntPtr hm, out uint n);
  [DllImport("dxva2.dll", SetLastError=true)] public static extern bool GetPhysicalMonitorsFromHMONITOR(IntPtr hm, uint n, [Out] PM[] arr);
  [DllImport("dxva2.dll", SetLastError=true)] public static extern bool GetMonitorBrightness(IntPtr h, out uint mn, out uint cur, out uint mx);
  [DllImport("dxva2.dll", SetLastError=true)] public static extern bool SetMonitorBrightness(IntPtr h, uint v);
  [DllImport("dxva2.dll", SetLastError=true)] public static extern bool DestroyPhysicalMonitors(uint n, PM[] arr);
}
'@
if (-not ([System.Management.Automation.PSTypeName]'DdcB').Type) { Add-Type -TypeDefinition $code }
$p = New-Object DdcB+POINT; $p.x = 10; $p.y = 10
$hm = [DdcB]::MonitorFromPoint($p, 1)
[uint32]$n = 0; [void][DdcB]::GetNumberOfPhysicalMonitorsFromHMONITOR($hm, [ref]$n)
$arr = New-Object 'DdcB+PM[]' $n; [void][DdcB]::GetPhysicalMonitorsFromHMONITOR($hm, $n, $arr)
$m = $arr[0]
if ($Level -ge 0) { $ok = [DdcB]::SetMonitorBrightness($m.h, [uint32]$Level); "set $Level ok=$ok"; Start-Sleep -Milliseconds 800 }
[uint32]$a=0;[uint32]$b=0;[uint32]$c=0; [void][DdcB]::GetMonitorBrightness($m.h, [ref]$a, [ref]$b, [ref]$c)
"primary monitor brightness: $b / $c"
[void][DdcB]::DestroyPhysicalMonitors($n, $arr)
