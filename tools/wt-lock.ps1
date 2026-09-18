# Run a command while holding the MAIN repo's shared GPU lock. The GPU is
# shared with the other executors and the user's live wallpaper, so every
# build and every render in this worktree goes through here.
param([Parameter(Mandatory=$true)][string]$Cmd,
      [string]$Who = "fw-cam",
      [int]$TimeoutSec = 3600)
$lock = "C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code\build2\shots\gpu.lock"
$dir  = Split-Path $lock
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$t0 = Get-Date
while (Test-Path $lock) {
    if (((Get-Date) - $t0).TotalSeconds -gt $TimeoutSec) { Write-Error "gpu.lock held too long"; exit 2 }
    Start-Sleep -Seconds 3
}
"$Who $(Get-Date -Format o)" | Out-File -Encoding utf8 $lock
try {
    & cmd /c $Cmd
    $code = $LASTEXITCODE
} finally {
    Remove-Item -Force $lock -ErrorAction SilentlyContinue
}
exit $code
