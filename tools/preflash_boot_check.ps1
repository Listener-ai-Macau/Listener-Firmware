#requires -Version 7.0
<#
.SYNOPSIS
Pre-flash boot-health gate for Listener devices.

Checks ~BOOT:STATUS over the shared Global\Listener_<port> mutex before any
wired flash. A crash-class state (non-zero crash_count, crash reset reason,
safe_mode=1, or an unreachable device) blocks the flash unless -Repair is
given; -Repair performs one bounded recovery flash of the current build and
re-checks. Keeps every wait bounded and leaves the serial port closed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Baud = 115200,
    [int]$MaxCrashCount = 0,
    [int]$ResponseTimeoutMs = 1600,
    [switch]$Repair
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot

function Read-BootStatus {
    param([Parameter(Mandatory = $true)][string]$PortName)

    $created = $false
    $mutex = [System.Threading.Mutex]::new($false, "Global\Listener_$PortName", [ref]$created)
    $acquired = $false
    $port = $null
    $raw = ""
    try {
        $acquired = $mutex.WaitOne(1500)
        if (-not $acquired) {
            return [pscustomobject]@{ reachable = $false; error = "Global\\Listener_$PortName mutex timed out" }
        }
        $port = [System.IO.Ports.SerialPort]::new($PortName, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $port.ReadTimeout = 80
        $port.WriteTimeout = 500
        $port.Open()
        $port.DiscardInBuffer()
        $port.Write("~BOOT:STATUS`r`n")
        $deadline = (Get-Date).AddMilliseconds($ResponseTimeoutMs)
        while ((Get-Date) -lt $deadline) {
            $raw += $port.ReadExisting()
            Start-Sleep -Milliseconds 40
        }
        $raw += $port.ReadExisting()
    } catch {
        return [pscustomobject]@{ reachable = $false; error = $_.Exception.Message }
    } finally {
        if ($port) { $port.Dispose() }
        if ($acquired) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }

    $match = [regex]::Match($raw, 'reset_reason=(?<reason>[a-z_]+)\(\d+\) crash_count=(?<crash>\d+) .*safe_mode=(?<safe>[01])')
    if (-not $match.Success) {
        return [pscustomobject]@{ reachable = $false; error = "BOOT:STATUS not observed in $($ResponseTimeoutMs) ms"; raw = $raw }
    }
    [pscustomobject]@{
        reachable    = $true
        reset_reason = $match.Groups['reason'].Value
        crash_count  = [int64]$match.Groups['crash'].Value
        safe_mode    = [int]$match.Groups['safe'].Value
        raw          = $raw
    }
}

function Test-BootHealth {
    param([Parameter(Mandatory = $true)]$Status)

    $crashReasons = @('panic', 'interrupt_wdt', 'task_wdt', 'other_wdt', 'cpu_lockup')
    $issues = @()
    if (-not $Status.reachable) { $issues += "boot status unreachable: $($Status.error)" }
    else {
        if ($Status.crash_count -gt $MaxCrashCount) { $issues += "crash_count=$($Status.crash_count) exceeds $MaxCrashCount" }
        if ($Status.reset_reason -in $crashReasons) { $issues += "crash-class reset_reason=$($Status.reset_reason)" }
        if ($Status.safe_mode -ne 0) { $issues += "safe_mode=$($Status.safe_mode)" }
    }
    return $issues
}

$before = Read-BootStatus -PortName $Port
$issues = @(Test-BootHealth -Status $before)
$result = [ordered]@{
    schema       = 'listener.preflash.boot-check.v1'
    port         = $Port
    before       = $before
    issues       = $issues
    repair       = $null
    verdict      = 'PASS'
}

if ($issues.Count -eq 0) {
    Write-Host "[preflash] boot health OK: reset_reason=$($before.reset_reason) crash_count=$($before.crash_count) safe_mode=$($before.safe_mode)"
    $result | ConvertTo-Json -Depth 6
    exit 0
}

Write-Warning "[preflash] boot health issues: $($issues -join '; ')"
if (-not $Repair.IsPresent) {
    $result.verdict = 'NO_GO'
    $result | ConvertTo-Json -Depth 6
    exit 1
}

Write-Host "[preflash] -Repair requested: reflashing current build to recover boot health"
& pwsh -NoProfile -File (Join-Path $PSScriptRoot "flash.ps1") -Port $Port -NoBuild
if ($LASTEXITCODE -ne 0) {
    $result.verdict = 'NO_GO'
    $result.repair = [ordered]@{ attempted = $true; flash_exit = $LASTEXITCODE }
    $result | ConvertTo-Json -Depth 6
    exit 1
}
Start-Sleep -Seconds 6
$after = Read-BootStatus -PortName $Port
$afterIssues = Test-BootHealth -Status $after
$result.repair = [ordered]@{ attempted = $true; flash_exit = 0; after = $after; after_issues = $afterIssues }
if ($afterIssues.Count -gt 0) {
    $result.verdict = 'NO_GO'
    $result | ConvertTo-Json -Depth 6
    exit 1
}
Write-Host "[preflash] repair succeeded: reset_reason=$($after.reset_reason) crash_count=$($after.crash_count) safe_mode=$($after.safe_mode)"
$result | ConvertTo-Json -Depth 6
exit 0
