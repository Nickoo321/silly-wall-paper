# Usage:
#   . tools\gpu-lock.ps1                                                    # dot-source for functions
#   if (Wait-GpuLock -Owner "my job") { try { <gpu work> } finally { Release-GpuLock } }
#   powershell -File tools\gpu-lock.ps1 -Acquire -Owner "x" [-TimeoutMinutes 30]  # exit 0/1
#   powershell -File tools\gpu-lock.ps1 -Release                                  # exit 0/1
# Stale = holder's pid= is dead, or (no pid= recorded) the file is >45 min old; reclaims are
# logged to build2\shots\gpu-lock.log. Release only deletes a lock stamped with this PID.

param(
    [switch]$Acquire,
    [switch]$Release,
    [string]$Owner = 'gpu-lock-cli',
    [int]$TimeoutMinutes = 30
)

function Get-GpuLockPaths {
    $root = Split-Path -Parent $PSScriptRoot
    [PSCustomObject]@{
        Lock = Join-Path $root 'build2\shots\gpu.lock'
        Log  = Join-Path $root 'build2\shots\gpu-lock.log'
    }
}

function Test-GpuLockStale {
    param([Parameter(Mandatory = $true)][string]$LockPath)
    try {
        $text = Get-Content -Path $LockPath -Raw -ErrorAction Stop
    } catch {
        return $null   # can't read it right now (e.g. it just vanished) -- not our call, re-check next loop
    }
    if ($text -match 'pid=(\d+)') {
        $lockPid = [int]$Matches[1]
        $proc = Get-Process -Id $lockPid -ErrorAction SilentlyContinue
        if (-not $proc) { return "pid=$lockPid is not running" }
        return $null
    } else {
        $age = (Get-Date) - (Get-Item -Path $LockPath).LastWriteTime
        if ($age.TotalMinutes -gt 45) { return "no pid= in lock content, file is $([int]$age.TotalMinutes) min old" }
        return $null
    }
}

function Wait-GpuLock {
    param(
        [Parameter(Mandatory = $true)][string]$Owner,
        [int]$TimeoutMinutes = 30
    )
    $p = Get-GpuLockPaths
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p.Lock) | Out-Null
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)

    while ($true) {
        if (Test-Path $p.Lock) {
            $staleReason = Test-GpuLockStale -LockPath $p.Lock
            if ($staleReason) {
                try {
                    Add-Content -Encoding utf8 -Path $p.Log -Value "$(Get-Date -Format o) reclaimed by '$Owner' (pid=$PID): $staleReason"
                } catch {}
                Remove-Item -Path $p.Lock -Force -ErrorAction SilentlyContinue
                # fall through and try to take it immediately, in the same iteration
            } else {
                if ((Get-Date) -ge $deadline) { return $false }
                Start-Sleep -Seconds 3
                continue
            }
        }

        $content = "$Owner pid=$PID $(Get-Date -Format o)"
        $bytes = [Text.Encoding]::UTF8.GetBytes($content)
        $fs = $null
        try {
            $fs = [IO.File]::Open($p.Lock, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $fs.Write($bytes, 0, $bytes.Length)
            $fs.Close()
            return $true
        } catch [IO.IOException] {
            if ($fs) { $fs.Dispose() }
            if ((Get-Date) -ge $deadline) { return $false }
            Start-Sleep -Seconds 3
            continue
        }
    }
}

function Release-GpuLock {
    $p = Get-GpuLockPaths
    if (-not (Test-Path $p.Lock)) { return $true }
    try {
        $text = Get-Content -Path $p.Lock -Raw -ErrorAction Stop
    } catch {
        return $false
    }
    if ($text -match "pid=$PID(\D|$)") {
        Remove-Item -Path $p.Lock -Force -ErrorAction SilentlyContinue
        return $true
    } else {
        try {
            Add-Content -Encoding utf8 -Path $p.Log -Value "$(Get-Date -Format o) pid=$PID refused to release (not the owner), lock content: $text"
        } catch {}
        return $false
    }
}

if ($Acquire) {
    if (Wait-GpuLock -Owner $Owner -TimeoutMinutes $TimeoutMinutes) { exit 0 } else { exit 1 }
} elseif ($Release) {
    if (Release-GpuLock) { exit 0 } else { exit 1 }
}
