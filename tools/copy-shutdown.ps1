# "walk away" watcher for a big Explorer copy (2026-09-20), sibling of defrag-shutdown.ps1:
#   1. after -DarkMinutes: pause the live sim (black frame), pause Wallpaper Engine, OLED DDC 0
#   2. when the copy Source -> Dest is complete: shut the PC down.
# "Complete" = the destination disk has been idle for -QuietMinutes AND a robocopy /L dry run
# (list only, copies nothing) reports 0 bytes left to copy. If the disk goes idle but bytes remain
# (a skip/replace dialog, an error) it logs "stalled" and keeps waiting: it never shuts down early.
# Log: build2\shots\copy-shutdown.log.  Abort: create build2\shots\copy-shutdown.stop
param([string]$Source = 'G:\hdd backup 2', [string]$Dest = 'D:\HDD backup', [string]$DestDisk = 'd',
      [int]$DarkMinutes = 20, [int]$QuietMinutes = 3)

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$log  = Join-Path $root "build2\shots\copy-shutdown.log"
$stop = Join-Path $root "build2\shots\copy-shutdown.stop"
function Log($m) { Add-Content $log ("{0}  {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m) }

$start = Get-Date; $darkAt = $start.AddMinutes($DarkMinutes); $dark = $false; $quietSince = $null
Log "started: '$Source' -> '$Dest' (dark at $($darkAt.ToString('HH:mm')), quiet $QuietMinutes min)"

function Go-Dark {
    try { & powershell -NoProfile -File (Join-Path $root "tools\away-pause.ps1") -Once on -Explicit | Out-Null; Log "sim: CMD_PAUSE_ON sent" } catch { Log "sim pause failed: $_" }
    $we = "D:\SteamLibrary\steamapps\common\wallpaper_engine\wallpaper64.exe"
    if (Get-Process wallpaper64 -ErrorAction SilentlyContinue) { try { & $we -control pause; Log "wallpaper engine: paused" } catch { Log "WE pause failed: $_" } }
    try { $r = & powershell -NoProfile -File (Join-Path $root "tools\oled-brightness.ps1") 0; Log "oled: $($r -join ' | ')" } catch { Log "oled failed: $_" }
}
function DestBusy {
    $s = Get-Counter "\PhysicalDisk(*)\Disk Bytes/sec" -SampleInterval 2 -MaxSamples 1 -ErrorAction SilentlyContinue
    foreach ($c in $s.CounterSamples) { if ($c.InstanceName -match "\b$DestDisk`:" -and $c.CookedValue -gt 200KB) { return $true } }
    return $false
}
function BytesRemaining {
    # robocopy list mode: "Bytes :  total  copied  skipped ..." -> 'copied' = what a real run would still copy
    $out = & robocopy $Source $Dest /E /L /NFL /NDL /NJH /NP /BYTES /XJ /R:0 /W:0 2>&1
    $line = ($out | Where-Object { $_ -match '^\s*Bytes\s*:' } | Select-Object -Last 1)
    if (-not $line) { return -1 }
    $n = ($line -replace '^\s*Bytes\s*:\s*','') -split '\s+'
    return [int64]$n[1]
}

while ($true) {
    if (Test-Path $stop) { Log "stop file found, exiting (nothing shut down)"; Remove-Item $stop -Force; exit 0 }
    $now = Get-Date
    if (-not $dark -and $now -ge $darkAt) { $dark = $true; Go-Dark }

    if (DestBusy) { $quietSince = $null }
    elseif ($null -eq $quietSince) { $quietSince = $now; Log "dest disk idle" }
    elseif (($now - $quietSince).TotalMinutes -ge $QuietMinutes) {
        $rem = BytesRemaining
        Log ("idle {0:N0} min, bytes remaining: {1:N0}" -f ($now - $quietSince).TotalMinutes, $rem)
        if ($rem -eq 0) {
            Log "copy complete -> shutting down in 60 s"
            if (-not $dark) { $dark = $true; Go-Dark }
            & shutdown /s /f /t 60 /c "Copy done - shutting down (copy-shutdown.ps1). 'shutdown /a' aborts."
            exit 0
        }
        Log "stalled? disk idle but bytes remain (dialog or error?) - waiting"
        Start-Sleep -Seconds 270
    }
    Start-Sleep -Seconds 30
}
