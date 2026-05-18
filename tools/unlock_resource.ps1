# unlock_resource.ps1 — 释放硬件资源锁
# 用法: .\tools\unlock_resource.ps1 -Resource COM3 [-Owner codex]
param(
    [Parameter(Mandatory=$true)]
    [string]$Resource,
    [string]$Owner = $env:USERNAME,
    [string]$LockDir = ".cache/resource_locks"
)

$ErrorActionPreference = "Stop"

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
