# unlock_resource.ps1 — 释放硬件资源锁
# 用法: .\tools\unlock_resource.ps1 -Resource COM3 [-Owner codex]
param(
    [Parameter(Mandatory=$true)]
    [string]$Resource,
    [string]$Owner = $env:USERNAME,
    [string]$LockDir = "C:\Users\Billy\Desktop\listener\.cache\resource_locks",
    [int]$MutexTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$mutex = [System.Threading.Mutex]::new($false, "Local\ListenerAiResourceLock")
$hasMutex = $false

try {
    $hasMutex = $mutex.WaitOne([TimeSpan]::FromSeconds($MutexTimeoutSeconds))
    if (-not $hasMutex) {
        Write-Error "Timed out waiting for resource lock mutex after $MutexTimeoutSeconds seconds."
        exit 1
    }

    $lockFile = Join-Path $LockDir "$Resource.lock.json"

    if (-not (Test-Path $lockFile)) {
        Write-Output "No lock found for '$Resource'"
        exit 0
    }

    $existing = Get-Content $lockFile -Raw | ConvertFrom-Json

    if ($existing.owner -ne $Owner) {
        Write-Error "Lock for '$Resource' is owned by '$($existing.owner)', not '$Owner'. Cannot release."
        exit 1
    }

    Remove-Item $lockFile -Force
    Write-Output "Released '$Resource' (was locked by '$Owner')"
} finally {
    if ($hasMutex) {
        $mutex.ReleaseMutex() | Out-Null
    }
    $mutex.Dispose()
}
