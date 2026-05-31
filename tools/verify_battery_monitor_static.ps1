param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

$batteryMonitor = Join-Path $RepoRoot "components\battery_monitor\battery_monitor.c"
if (-not (Test-Path -LiteralPath $batteryMonitor)) {
    throw "Missing battery monitor source: $batteryMonitor"
}

$text = Get-Content -LiteralPath $batteryMonitor -Raw

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_EMPTY_MV 2700U\r?$') {
    throw "Battery empty voltage must be 2700mV."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_FULL_MV 4200U\r?$') {
    throw "Battery full voltage must be 4200mV."
}

if ($text -notmatch 'battery_mv <= BATTERY_MONITOR_EMPTY_MV[\s\S]*return 0;') {
    throw "Battery percentage must clamp at empty voltage."
}

if ($text -notmatch 'battery_mv >= BATTERY_MONITOR_FULL_MV[\s\S]*return 100;') {
    throw "Battery percentage must clamp at full voltage."
}

if ($text -notmatch 'BATTERY_MONITOR_FULL_MV - BATTERY_MONITOR_EMPTY_MV') {
    throw "Battery percentage must derive range from configured endpoints."
}

Write-Host "PASS: battery monitor static checks passed for 2700mV empty and 4200mV full."
