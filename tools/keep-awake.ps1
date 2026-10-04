# Holds ES_SYSTEM_REQUIRED so the PC does not idle-sleep while overnight work runs.
# Stops when build2\shots\keep-awake.stop exists. Changes no user settings.
Add-Type -Namespace KA -Name N -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
$stop = Join-Path $PSScriptRoot "..\build2\shots\keep-awake.stop"
while (-not (Test-Path $stop)) { [KA.N]::SetThreadExecutionState(0x80000001) | Out-Null; Start-Sleep -Seconds 60 }
[KA.N]::SetThreadExecutionState(0x80000000) | Out-Null
