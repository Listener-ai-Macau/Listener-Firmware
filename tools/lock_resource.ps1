# lock_resource.ps1 — 获取硬件资源锁
# 用法: .\tools\lock_resource.ps1 -Resource COM3 [-Owner codex] [-TimeoutMinutes 60]
param(
    [Parameter(Mandatory=$true)]
    [string]$Resource,
    [string]$Owner = $env:USERNAME,
    [int]$TimeoutMinutes = 60,
    [string]$LockDir = ".cache/resource_locks"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $LockDir)) {
    New-Item -ItemType Directory -Path $LockDir -Force | Out-Null
}

$lockFile = Join-Path $LockDir "$Resource.lock.json"

# 检查现有锁
if (Test-Path $lockFile) {
    $existing = Get-Content $lockFile -Raw | ConvertFrom-Json
    $lockedAt = [DateTime]::Parse($existing.locked_at)
    $expiresAt = $lockedAt.AddMinutes($existing.timeout_minutes)

    if ((Get-Date) -lt $expiresAt -and $existing.owner -ne $Owner) {
        Write-Error "Resource '$Resource' is locked by '$($existing.owner)' until $($expiresAt.ToString('yyyy-MM-dd HH:mm:ss'))"
        exit 1
    }

    # 过期锁或自己是 owner，可以覆盖
}

# 写锁
$lockData = @{
    resource = $Resource
    owner = $Owner
    locked_at = (Get-Date).ToString("o")
    timeout_minutes = $TimeoutMinutes
    pid = $PID
} | ConvertTo-Json

Set-Content -Path $lockFile -Value $lockData -Encoding UTF8
Write-Output "Locked '$Resource' for '$Owner' ($TimeoutMinutes min)"
