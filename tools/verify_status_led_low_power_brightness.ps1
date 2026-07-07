[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string]$OutputDir = "",
    [int]$WaitSeconds = 65,
    [switch]$NoAiwLock
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$serialCaptureScript = Join-Path $PSScriptRoot "send_serial_and_capture.ps1"
$logVerifier = Join-Path $PSScriptRoot "verify_status_led_low_power_brightness_log.py"
$aiwPath = $null
if ($env:AI_WORKFLOW_REPO) {
    $candidateAiwPath = Join-Path $env:AI_WORKFLOW_REPO "scripts\aiw.ps1"
    if (Test-Path -LiteralPath $candidateAiwPath) {
        $aiwPath = (Resolve-Path -LiteralPath $candidateAiwPath).Path
    }
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".cache\validation\status-led-low-power-brightness-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

function Get-SerialPorts {
    try {
        return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    } catch {
        return @()
    }
}

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port
    }
    $ports = @(Get-SerialPorts)
    if ($ports.Count -ne 1) {
        throw "Expected exactly one serial port when -Port is omitted; found $($ports -join ',')."
    }
    return $ports[0]
}

function Invoke-SerialCapture {
    param(
        [Parameter(Mandatory = $true)][string]$CommandList,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [int]$InitialReadMs = 300,
        [int]$CommandReadMs = 1200,
        [int]$CommandDelayMs = 150
    )

    $baseArgs = @(
        "-NoProfile",
        "-File", $serialCaptureScript,
        "-Port", $script:ResolvedPort,
        "-Baud", [string]$Baud,
        "-InitialReadMs", [string]$InitialReadMs,
        "-CommandReadMs", [string]$CommandReadMs,
        "-CommandDelayMs", [string]$CommandDelayMs,
        "-CommandList", $CommandList,
        "-OutputPath", $OutputPath
    )
    if ($NoAiwLock.IsPresent -or $null -eq $aiwPath) {
        & pwsh @baseArgs
    } else {
        & $aiwPath with-lock -Resource $script:ResolvedPort -Wait -WaitTimeoutSeconds 120 -TimeoutMinutes 3 `
            -Purpose "status LED low-power brightness verification" `
            -Run pwsh @baseArgs
    }
    if ($LASTEXITCODE -ne 0) {
        throw "serial capture failed with exit code $LASTEXITCODE"
    }
}

function Get-LatestLine {
    param(
        [Parameter(Mandatory = $true)][string[]]$Lines,
        [Parameter(Mandatory = $true)][string]$Needle
    )
    $matches = @($Lines | Where-Object { $_ -like "*$Needle*" })
    if ($matches.Count -eq 0) {
        return ""
    }
    return $matches[-1]
}

function Get-Field {
    param(
        [Parameter(Mandatory = $true)][string]$Line,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $match = [regex]::Match($Line, "(^| )$([regex]::Escape($Name))=(?<value>[^ \r\n]+)")
    if (-not $match.Success) {
        return ""
    }
    return $match.Groups["value"].Value
}

$script:ResolvedPort = Resolve-TestPort
$settingsBeforePath = Join-Path $OutputDir "settings-before.txt"
$settingsRestorePath = Join-Path $OutputDir "settings-restore.txt"
$lowPowerPath = Join-Path $OutputDir "low-power-status.txt"
$summaryPath = Join-Path $OutputDir "summary.json"
$restoreCommand = ""
$result = "FAIL"

try {
    Invoke-SerialCapture -CommandList "~DEVICE:SETTINGS;;~POWER:IDLE" -OutputPath $settingsBeforePath -InitialReadMs 300 -CommandReadMs 1000
    $beforeLines = @(Get-Content -LiteralPath $settingsBeforePath)
    $settingsLine = Get-LatestLine -Lines $beforeLines -Needle "~DEVICE:SETTINGS"
    if ([string]::IsNullOrWhiteSpace($settingsLine)) {
        throw "missing initial ~DEVICE:SETTINGS line"
    }
    $restoreValues = @(
        (Get-Field -Line $settingsLine -Name "led_status"),
        (Get-Field -Line $settingsLine -Name "led_key"),
        (Get-Field -Line $settingsLine -Name "led_ec11"),
        (Get-Field -Line $settingsLine -Name "led_edge"),
        (Get-Field -Line $settingsLine -Name "plugged_low_power_enabled"),
        (Get-Field -Line $settingsLine -Name "plugged_low_power_idle_ms"),
        (Get-Field -Line $settingsLine -Name "battery_low_power_idle_ms")
    )
    if (@($restoreValues | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count -ne 0) {
        throw "failed to parse device settings for restoration: $settingsLine"
    }
    $restoreCommand = "~DEVICE:SET led_status={0} led_key={1} led_ec11={2} led_edge={3} plugged_low_power_enabled={4} plugged_low_power_idle_ms={5} battery_low_power_idle_ms={6}" -f $restoreValues

    Invoke-SerialCapture `
        -CommandList "~DEVICE:SET led_status=80 plugged_low_power_enabled=1 plugged_low_power_idle_ms=60000;;~DEVICE:SETTINGS;;~POWER:IDLE" `
        -OutputPath (Join-Path $OutputDir "settings-test.txt") `
        -InitialReadMs 200 `
        -CommandReadMs 1000

    Write-Host "Waiting $WaitSeconds seconds for real low-power final latch..."
    Start-Sleep -Seconds $WaitSeconds

    Invoke-SerialCapture `
        -CommandList "~POWER:IDLE;;~LED:STATUS detail=brightness;;~LED:STATUS detail=state;;~LED:STATUS detail=power;;~LED:STATUS detail=rgb;;~LED:STATUS detail=summary" `
        -OutputPath $lowPowerPath `
        -InitialReadMs 300 `
        -CommandReadMs 1400 `
        -CommandDelayMs 100

    & python $logVerifier $lowPowerPath
    if ($LASTEXITCODE -ne 0) {
        throw "low-power brightness log verification failed"
    }
    $result = "PASS"
    Write-Host "PASS: status LED low-power brightness verification completed."
} finally {
    if (-not [string]::IsNullOrWhiteSpace($restoreCommand)) {
        try {
            Invoke-SerialCapture -CommandList "$restoreCommand;;~DEVICE:SETTINGS" -OutputPath $settingsRestorePath -InitialReadMs 200 -CommandReadMs 1000
        } catch {
            Write-Warning "Failed to restore device settings after low-power brightness verification: $($_.Exception.Message)"
        }
    }
    [pscustomobject]@{
        schema = "listener.status_led.low_power_brightness.v1"
        result = $result
        port = $script:ResolvedPort
        settings_before = $settingsBeforePath
        low_power_transcript = $lowPowerPath
        settings_restore = $settingsRestorePath
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    Write-Host "Summary: $summaryPath"
}
