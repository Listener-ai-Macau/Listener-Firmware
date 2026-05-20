# lock_status.ps1 - show shared hardware resource locks.
param(
    [string]$Resource,
    [string]$LockDir = "C:\Users\Billy\Desktop\listener\.cache\resource_locks"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $LockDir)) {
    Write-Output "No lock directory found: $LockDir"
    exit 0
}

$pattern = if ($Resource) { "$Resource.lock.json" } else { "*.lock.json" }
$files = @(Get-ChildItem $LockDir -Filter $pattern -File -ErrorAction SilentlyContinue)

if (-not $files) {
    Write-Output "No resource locks found."
    exit 0
}

$now = Get-Date
$rows = foreach ($file in $files) {
    try {
        $lock = Get-Content $file.FullName -Raw | ConvertFrom-Json
        $lockedAt = [DateTime]::Parse($lock.locked_at)
        $expiresAt = $lockedAt.AddMinutes([int]$lock.timeout_minutes)
        [PSCustomObject]@{
            resource = $lock.resource
            owner = $lock.owner
            status = if ($now -lt $expiresAt) { "active" } else { "expired" }
            locked_at = $lockedAt.ToString("yyyy-MM-dd HH:mm:ss")
            expires_at = $expiresAt.ToString("yyyy-MM-dd HH:mm:ss")
            pid = $lock.pid
            file = $file.FullName
        }
    } catch {
        [PSCustomObject]@{
            resource = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
            owner = ""
            status = "invalid"
            locked_at = ""
            expires_at = ""
            pid = ""
            file = $file.FullName
        }
    }
}

$rows | Sort-Object resource | Format-Table -AutoSize
