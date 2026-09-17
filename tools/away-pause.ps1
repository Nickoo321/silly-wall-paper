# Pause the LIVE FluidWallpaper sim while the user is away, resume when they come back.
#   powershell -File tools\away-pause.ps1 [-IdleMinutes 10] [-PauseNow] [-Once <on|off>]
# How: the running FluidWallpaper.exe (whatever pid, normally the user's build\ copy) has a tray
# window (class FluidWallpaperTray) whose WM_COMMAND id 1 (CMD_PAUSE) TOGGLES manual pause: the
# render loop stops (GPU ~0). Since the rise-mode commit a MANUAL pause also presents ONE black
# frame, so the panel goes dark instead of holding the last frame. Nothing is stopped or
# relaunched.
# The command is a toggle with no readback, so this script owns the state: it assumes the sim is
# running when it starts (unless -PauseNow says pause immediately) and never sends two pauses in
# a row. If the user toggles Pause from the tray by hand while this runs, the state desyncs —
# stop the script (or use -Once) and toggle by hand.
#   -Once on|off : send exactly one command and exit (on = pause, off = resume)
#   -Explicit    : use the NON-toggling ids CMD_PAUSE_ON=8 / CMD_PAUSE_OFF=9 instead of the
#                  toggle, so this script never has to track state and a hand-toggle from the
#                  tray cannot desync it. Those ids landed with the rise-mode commit: an exe
#                  built BEFORE it ignores 8/9 silently, so leave this off until the live
#                  build\ copy has been rebuilt and relaunched by the user.
#   -IdleMinutes : idle (no keyboard/mouse) threshold before pausing (default 10)
#   -PauseNow    : the user said they are away right now: pause immediately, then watch for input
# Log: build2\shots\away-pause.log. Never uses SC_MONITORPOWER (that killed the port once).
param([int]$IdleMinutes = 10, [switch]$PauseNow, [string]$Once = "", [switch]$Explicit)

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$log  = Join-Path $root "build2\shots\away-pause.log"
function Log($m) { $line = "{0}  {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m; Add-Content -Path $log -Value $line; Write-Host $line }

Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Collections.Generic;
public static class AwayNative {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO p);
  public static List<IntPtr> found = new List<IntPtr>();
  public static bool Cb(IntPtr h, IntPtr l) {
    var sb = new StringBuilder(64); GetClassName(h, sb, 64);
    if (sb.ToString() == "FluidWallpaperTray") found.Add(h);
    return true;
  }
  public static uint PidOf(IntPtr h) { uint pid; GetWindowThreadProcessId(h, out pid); return pid; }
  public static List<IntPtr> AllTrays() { found.Clear(); EnumWindows(Cb, IntPtr.Zero); return new List<IntPtr>(found); }
  public static double IdleSeconds() {
    var li = new LASTINPUTINFO(); li.cbSize = (uint)Marshal.SizeOf(li); GetLastInputInfo(ref li);
    return (Environment.TickCount - (int)li.dwTime) / 1000.0;
  }
}
"@

# $want = $true to pause, $false to resume. With -Explicit that is one of the non-toggling
# ids and $want is obeyed literally; without it, the single toggle id, and the caller is the
# one keeping track of which side of the toggle we are on.
function Find-LiveTray {
  # Headless --shot renders (agents' A/B tests) may own a FluidWallpaperTray window too; on
  # 2026-09-17 the first pause landed on one of those and the live sim kept running. Pick the tray
  # whose process is the LIVE wallpaper: a FluidWallpaper.exe whose command line has no --shot.
  $live = @(Get-CimInstance Win32_Process -Filter "Name='FluidWallpaper.exe'" |
            Where-Object { $_.CommandLine -notlike '*--shot*' } | ForEach-Object { [uint32]$_.ProcessId })
  foreach ($h in [AwayNative]::AllTrays()) { if ($live -contains [AwayNative]::PidOf($h)) { return $h } }
  return [IntPtr]::Zero
}
function Send-Pause([bool]$want) {
  $h = Find-LiveTray
  if ($h -eq [IntPtr]::Zero) { Log "no LIVE FluidWallpaperTray window found (sim not running, or only --shot instances)"; return $false }
  $id = 1                                            # CMD_PAUSE (toggle)
  if ($Explicit) { if ($want) { $id = 8 } else { $id = 9 } }   # CMD_PAUSE_ON / CMD_PAUSE_OFF
  [AwayNative]::PostMessage($h, 0x0111, [IntPtr]$id, [IntPtr]0) | Out-Null   # WM_COMMAND
  return $true
}

if ($Once -ne "") {
  if (Send-Pause ($Once -eq "on")) { Log ("one-shot sent (intent: {0}, explicit: {1})" -f $Once, [bool]$Explicit) }
  exit 0
}

$paused = $false
if ($PauseNow) { if (Send-Pause $true) { $paused = $true; Log "paused now (user is away)" } }
Log ("watching: idle threshold {0} min" -f $IdleMinutes)
while ($true) {
  Start-Sleep -Seconds 5
  $idle = [AwayNative]::IdleSeconds()
  if (-not $paused -and $idle -ge $IdleMinutes * 60) {
    if (Send-Pause $true) { $paused = $true; Log ("paused: idle {0:N0} s" -f $idle) }
  } elseif ($paused -and $idle -lt 5) {
    if (Send-Pause $false) { $paused = $false; Log "resumed: input detected" }
  }
}
