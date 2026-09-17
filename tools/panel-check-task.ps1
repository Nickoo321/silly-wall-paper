# Run tools\panel-check.ps1 OUTSIDE the Claude desktop package via a one-shot scheduled task.
#   powershell -File tools\panel-check-task.ps1 -Ini reference\configs\acid-rise-12.ini [-InstallPresets]
#   powershell -File tools\panel-check-task.ps1 -Restore
# Why: processes spawned by Claude Code tools are MSIX-virtualized; their %APPDATA% writes land in
# C:\Users\abg77\AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Roaming\... and the real
# FluidWallpaper never sees them (and a shadow copy then MASKS the real file for later reads).
# A scheduled task runs as the plain interactive user, so the swap, backup and preset install are real.
# Output: build2\shots\panel-check.log (written by panel-check.ps1). Task is unregistered afterwards.
param([string]$Ini = "", [switch]$Restore, [switch]$InstallPresets)
$root   = Split-Path -Parent $PSScriptRoot
$script = Join-Path $root "tools\panel-check.ps1"
$a = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
if ($Restore) { $a += " -Restore" } elseif ($Ini) { $a += " -Ini `"$Ini`"" } else { Write-Host "usage: -Ini <path> [-InstallPresets] | -Restore"; exit 1 }
if ($InstallPresets) { $a += " -InstallPresets" }
$name = "FluidPanelCheckOnce"
$action    = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $a
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
$settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $name
Start-Sleep -Seconds 8
$info = Get-ScheduledTaskInfo -TaskName $name
Unregister-ScheduledTask -TaskName $name -Confirm:$false
Write-Host ("task result {0}; log tail:" -f $info.LastTaskResult)
Get-Content (Join-Path $root "build2\shots\panel-check.log") -Tail 4
# Shadow copies from earlier virtualized runs mask the real files for Claude's own reads: clear them.
$shadow = "C:\Users\abg77\AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Roaming\FluidWallpaper"
if (Test-Path $shadow) { Get-ChildItem $shadow -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue }
