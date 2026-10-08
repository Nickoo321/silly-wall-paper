# One-off "walk away" watcher (2026-09-20):
#   1. after -DarkMinutes (default 20): pause the LIVE sim (explicit CMD_PAUSE_ON = black frame,
#      GPU ~0), pause Wallpaper Engine, set the OLED to DDC brightness 0  -> panel is pure black
#   2. when the Optimize Drives run finishes: shut the PC down.
# "Finished" = a Microsoft-Windows-Defrag event (258 done / 259,264 failed-or-cancelled ...) logged
# after this script started for an HDD volume, followed by -QuietMinutes with no new defrag event
# and the HDDs idle (so a queued second drive keeps us waiting). Fallback: defragsvc stopping.
# Log: build2\shots\defrag-shutdown.log.  Abort: create build2\shots\defrag-shutdown.stop
#   powershell -File tools\defrag-shutdown.ps1 [-DarkMinutes 20] [-QuietMinutes 3] [-Drives d,f]
param([int]$DarkMinutes = 20, [int]$QuietMinutes = 3, [string[]]$Drives = @('d','f'))

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$log  = Join-Path $root "build2\shots\defrag-shutdown.log"
$stop = Join-Path $root "build2\shots\defrag-shutdown.stop"
function Log($m) { Add-Content $log ("{0}  {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $m) }

$start = Get-Date
$darkAt = $start.AddMinutes($DarkMinutes)
$dark = $false
$doneEvent = $null
$quietSince = $null
Log "started (dark at $($darkAt.ToString('HH:mm')), drives: $($Drives -join ','), quiet $QuietMinutes min)"

function Go-Dark {
    try { & powershell -NoProfile -File (Join-Path $root "tools\away-pause.ps1") -Once on -Explicit | Out-Null; Log "sim: CMD_PAUSE_ON sent (black frame)" } catch { Log "sim pause failed: $_" }
    $we = "D:\SteamLibrary\steamapps\common\wallpaper_engine\wallpaper64.exe"
    if (Get-Process wallpaper64 -ErrorAction SilentlyContinue) { try { & $we -control pause; Log "wallpaper engine: paused" } catch { Log "WE pause failed: $_" } }
    try { $r = & powershell -NoProfile -File (Join-Path $root "tools\oled-brightness.ps1") 0; Log "oled: $($r -join ' | ')" } catch { Log "oled failed: $_" }
}
function HddBusy {
    $s = Get-Counter '\PhysicalDisk(*)\% Disk Time' -SampleInterval 2 -MaxSamples 1 -ErrorAction SilentlyContinue
    $busy = $false
    foreach ($c in $s.CounterSamples) { foreach ($d in $Drives) { if ($c.InstanceName -match "\b$d`:" -and $c.CookedValue -gt 25) { $busy = $true } } }
    return $busy
}
function NewDefragEvents {
    Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Microsoft-Windows-Defrag'; StartTime=$start} -ErrorAction SilentlyContinue |
        Where-Object { $m = $_.Message; ($Drives | Where-Object { $m -match "\($($_.ToUpper()):\)" }).Count -gt 0 }
}

$svcSeenRunning = $false      # the "service stopped" fallback counts only after the service has run
while ($true) {
    if (Test-Path $stop) { Log "stop file found, exiting (nothing shut down)"; Remove-Item $stop -Force; exit 0 }
    $now = Get-Date
    if (-not $dark -and $now -ge $darkAt) { $dark = $true; Go-Dark }

    $ev = NewDefragEvents | Sort-Object TimeCreated -Descending | Select-Object -First 1
    if ($ev -and ($null -eq $doneEvent -or $ev.TimeCreated -gt $doneEvent.TimeCreated)) {
        $doneEvent = $ev; $quietSince = $null
        Log ("defrag event {0}: {1}" -f $ev.Id, $ev.Message.Split("`n")[0])
    }
    $svcStopped = (Get-Service defragsvc).Status -eq 'Stopped'
    if (-not $svcStopped) { $svcSeenRunning = $true }
    $busy = HddBusy
    $finished = $false
    if ($doneEvent) {
        if ($busy) { $quietSince = $null }
        elseif ($null -eq $quietSince) { $quietSince = $now }
        elseif (($now - $quietSince).TotalMinutes -ge $QuietMinutes) { $finished = $true }
    }
    # defragsvc is demand-start and sits in Stopped whenever idle: without "seen running" this
    # forced a shutdown a minute after the script started, before any defrag (review 2026-10-04)
    if ($svcSeenRunning -and $svcStopped -and -not $busy) { Log "defragsvc stopped"; $finished = $true }

    if ($finished) {
        Log "defrag run finished -> shutting down in 60 s"
        if (-not $dark) { $dark = $true; Go-Dark }
        & shutdown /s /f /t 60 /c "Defrag done - shutting down (defrag-shutdown.ps1). Run 'shutdown /a' to abort."
        exit 0
    }
    Start-Sleep -Seconds 30
}
