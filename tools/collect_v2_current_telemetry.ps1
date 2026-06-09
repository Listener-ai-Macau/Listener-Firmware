<#
.SYNOPSIS
Collect optional V2 current telemetry and low-power status evidence.

.DESCRIPTION
Reads firmware diagnostics and writes a focused report for the two optional
current telemetry inputs used by early V2 prototypes:
  - TPS63020_I_ADC / GPIO10 / BAT_IN -> VIN_TPS63020, or absent on revised boards
  - SY7088_I_ADC / GPIO9 / BAT_IN -> VIN_SY7088, or absent on revised boards

Serial mode also requests ~POWER:STATUS so the same report captures the
hardware-shutdown threshold, external-power blocker state, and PWR_HOLD status.
The report is telemetry-only. It does not enable power control, shutdown,
LED limiting, or any other firmware power decision.
#>
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$ReadSeconds = 8,
    [string]$InputLog = "",
    [string]$OutputDir = "tests\artifacts\v2_current_telemetry",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$expectedSensors = @(
    [ordered]@{
        branch = "TPS63020_input_branch"
        net = "TPS63020_I_ADC"
        allowed_gpios = @(-1, 10)
        role = "optional BAT_IN -> VIN_TPS63020 input branch current telemetry"
    },
    [ordered]@{
        branch = "SY7088_input_branch"
        net = "SY7088_I_ADC"
        allowed_gpios = @(-1, 9)
        role = "optional BAT_IN -> VIN_SY7088 input branch current telemetry"
    }
)

function Resolve-RepoRelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Convert-TokenValue {
    param([Parameter(Mandatory = $true)][string]$Value)

    $clean = $Value
    if ($clean.Length -ge 2 -and $clean.StartsWith('"') -and $clean.EndsWith('"')) {
        return $clean.Substring(1, $clean.Length - 2)
    }
    if ($clean -match '^-?\d+$') {
        return [int64]$clean
    }
    return $clean
}

function Convert-ToBool {
    param([object]$Value)

    if ($null -eq $Value) {
        return $false
    }
    if ($Value -is [bool]) {
        return $Value
    }
    if ($Value -is [int] -or $Value -is [long]) {
        return [int64]$Value -ne 0
    }
    $text = [string]$Value
    return $text -eq "1" -or $text -ieq "true" -or $text -ieq "yes"
}

function Parse-BoardPowerLine {
    param([Parameter(Mandatory = $true)][string]$Line)

    $trimmed = $Line.Trim()
    if ($trimmed -notmatch '^~BOARD:POWER\s+') {
        return $null
    }

    $item = [ordered]@{
        raw_line = $trimmed
    }
    foreach ($match in [regex]::Matches($trimmed, '([A-Za-z_][A-Za-z0-9_]*)=("[^"]*"|\S+)')) {
        $key = $match.Groups[1].Value
        $value = Convert-TokenValue -Value $match.Groups[2].Value
        $item[$key] = $value
    }
    return [pscustomobject]$item
}

function Parse-PowerStatusLine {
    param([Parameter(Mandatory = $true)][string]$Line)

    $trimmed = $Line.Trim()
    if ($trimmed -notmatch '^~POWER:STATUS\s+') {
        return $null
    }

    $item = [ordered]@{
        raw_line = $trimmed
    }
    foreach ($match in [regex]::Matches($trimmed, '([A-Za-z_][A-Za-z0-9_]*)=("[^"]*"|\S+)')) {
        $key = $match.Groups[1].Value
        $value = Convert-TokenValue -Value $match.Groups[2].Value
        $item[$key] = $value
    }
    return [pscustomobject]$item
}

function Get-FieldValue {
    param(
        [object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [object]$Default = $null
    )

    if ($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name) {
        return $Object.$Name
    }
    return $Default
}

function Read-DiagnosticsFromSerial {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$SerialBaud,
        [Parameter(Mandatory = $true)][int]$Seconds
    )

    $serialPort = $null
    $lines = New-Object System.Collections.Generic.List[string]
    try {
        $serialPort = [System.IO.Ports.SerialPort]::new(
            $SerialPortName,
            $SerialBaud,
            [System.IO.Ports.Parity]::None,
            8,
            [System.IO.Ports.StopBits]::One
        )
        $serialPort.ReadTimeout = 500
        $serialPort.WriteTimeout = 5000
        $serialPort.DtrEnable = $false
        $serialPort.RtsEnable = $false
        $serialPort.Open()
        $serialPort.DtrEnable = $false
        $serialPort.RtsEnable = $false

        Start-Sleep -Milliseconds 300
        while ($serialPort.BytesToRead -gt 0) {
            [void]$serialPort.ReadExisting()
            Start-Sleep -Milliseconds 20
        }

        $serialPort.Write("~BOARD:STATUS`n")
        Start-Sleep -Milliseconds 100
        $serialPort.Write("~POWER:STATUS`n")
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            try {
                $line = $serialPort.ReadLine().Trim()
                if ($line.Length -gt 0) {
                    $lines.Add($line)
                }
            } catch [System.TimeoutException] {
            }
        }
    } finally {
        if ($serialPort -and $serialPort.IsOpen) {
            $serialPort.Close()
        }
    }
    return @($lines)
}

function New-SelfTestLines {
    return @(
        "~BOARD:STATUS profile=voice-keyboard-v2-n16r8 battery_mv=4012 battery_valid=1",
        "~BOARD:POWER branch=TPS63020_input_branch present=0 gpio=-1 raw_adc=0 adc_mv=0 adc_calibrated=0 sample_count=0 calibration_status=current_telemetry_not_populated current_model=""not_populated"" current_calibrated=0 current_ma_valid=0 estimated_input_current_ma=0 battery_side_mv=0 battery_voltage_source=""BAT_V_ADC/GPIO8 68K/68K midpoint, VBAT~=2*ADC"" power_mw_valid=0 estimated_input_power_mw=0 result=ESP_ERR_NOT_SUPPORTED policy=v2_optional_current_telemetry_not_populated_battery_adc_only_no_power_decisions",
        "~BOARD:POWER branch=SY7088_input_branch present=0 gpio=-1 raw_adc=0 adc_mv=0 adc_calibrated=0 sample_count=0 calibration_status=current_telemetry_not_populated current_model=""not_populated"" current_calibrated=0 current_ma_valid=0 estimated_input_current_ma=0 battery_side_mv=0 battery_voltage_source=""BAT_V_ADC/GPIO8 68K/68K midpoint, VBAT~=2*ADC"" power_mw_valid=0 estimated_input_power_mw=0 result=ESP_ERR_NOT_SUPPORTED policy=v2_optional_current_telemetry_not_populated_battery_adc_only_no_power_decisions",
        "~POWER:STATUS state=CONNECTED_IDLE blockers=0x00000000 blocker_names=none shutdown_blockers=0x00000000 shutdown_blocker_names=none idle_ms=100 user_idle_ms=100 radio_idle_ms=100 ble_connected=1 automatic_shutdown_blocked_by_external_power=0 external_power_present=0 usb_power_present=0 charging=0 charge_full=0 usb_det_level=low bat_chg_level=high bat_std_level=high usb_det_policy=v2_gpio7_r37_r32_10K_10K_divider charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_mv=3988 battery_level=82 battery_valid=1 last_shutdown_reason=manual_command last_shutdown_idle_ms=100 last_shutdown_blockers=0x00000000 guard=1 audio_idle_ms=60000 connected_idle_ms=300000 disconnected_idle_ms=600000 hardware_shutdown_ms=1800000 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown voice_key_gpio=18 hardware_shutdown_user_action=""short-press hardware power key for cold boot after PWR_HOLD/GPIO11 shutdown-high"""
    )
}

function New-MarkdownReport {
    param(
        [Parameter(Mandatory = $true)][object]$Manifest,
        [Parameter(Mandatory = $true)][object[]]$Readings
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# V2 Current Telemetry Report")
    $lines.Add("")
    $lines.Add(("Captured at: {0}" -f $Manifest.captured_at))
    $lines.Add(("Source: {0} ({1})" -f $Manifest.source, $Manifest.source_mode))
    $lines.Add("")
    $lines.Add("Policy: telemetry only. Firmware does not use these readings for power control, shutdown, LED limiting, or user-visible power decisions.")
    $lines.Add("")
    $lines.Add("| Branch | Net | Allowed GPIOs | Reported GPIO | Hardware present | Raw ADC | ADC mV | ADC calibrated | Input current mA valid | Battery mV | Input power mW valid | Result |")
    $lines.Add("|---|---|---|---:|---|---:|---:|---|---|---:|---|---|")
    foreach ($reading in $Readings) {
        $lines.Add((
            "| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} | {11} |" -f
            $reading.branch,
            $reading.net,
            $reading.gpio_allowed,
            $reading.gpio_reported,
            $reading.present,
            $reading.raw_adc,
            $reading.adc_mv,
            $reading.adc_calibrated,
            $reading.current_ma_valid,
            $reading.battery_side_mv,
            $reading.power_mw_valid,
            $reading.result
        ))
    }
    $lines.Add("")
    $lines.Add(("All expected branches reported: {0}" -f $Manifest.summary.all_expected_present))
    $lines.Add(("Hardware sensors populated: {0}/{1}" -f $Manifest.summary.hardware_present_count, $Manifest.summary.expected_sensor_count))
    $lines.Add("")
    $lines.Add("## Power Status")
    if ($null -ne $Manifest.low_power_status -and $Manifest.low_power_status.present) {
        $lines.Add(("- Hardware shutdown ms: {0}" -f $Manifest.low_power_status.hardware_shutdown_ms))
        $lines.Add(("- Shutdown blockers: {0}" -f $Manifest.low_power_status.shutdown_blockers))
        $lines.Add(("- Auto shutdown blocked by external power: {0}" -f $Manifest.low_power_status.automatic_shutdown_blocked_by_external_power))
        $lines.Add(("- PWR_HOLD GPIO: {0}" -f $Manifest.low_power_status.pwr_hold_gpio))
        $lines.Add(("- PWR_HOLD level: {0}" -f $Manifest.low_power_status.pwr_hold_level))
        $lines.Add(("- Last shutdown reason: {0}" -f $Manifest.low_power_status.last_shutdown_reason))
        $lines.Add(("- Current battery mV: {0}" -f $Manifest.low_power_status.battery_mv))
    } else {
        $lines.Add("~POWER:STATUS was not present in the captured log.")
    }
    if (@($Manifest.summary.missing_branches).Count -gt 0) {
        $lines.Add(("Missing branches: {0}" -f (@($Manifest.summary.missing_branches) -join ", ")))
    }
    if (@($Manifest.summary.gpio_mismatches).Count -gt 0) {
        $lines.Add(("GPIO mismatches: {0}" -f (@($Manifest.summary.gpio_mismatches) -join "; ")))
    }
    return @($lines)
}

$sourceCount = 0
$hasPort = -not [string]::IsNullOrWhiteSpace($Port)
$hasInput = -not [string]::IsNullOrWhiteSpace($InputLog)
if ($SelfTest) { $sourceCount++ }
if ($hasPort) { $sourceCount++ }
if ($hasInput) { $sourceCount++ }
if ($sourceCount -ne 1) {
    throw "Provide exactly one source: -SelfTest, -InputLog <file>, or -Port COMx."
}

$cleanupOutputDir = $false
if ($SelfTest -and -not $PSBoundParameters.ContainsKey("OutputDir")) {
    $OutputDir = Join-Path ([System.IO.Path]::GetTempPath()) ("listener_v2_current_telemetry_selftest_{0}" -f ([guid]::NewGuid().ToString("N")))
    $cleanupOutputDir = $true
}

$resolvedOutputDir = Resolve-RepoRelativePath -Path $OutputDir
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$transcriptPath = Join-Path $resolvedOutputDir ("v2_current_telemetry_{0}_transcript.txt" -f $timestamp)
$jsonPath = Join-Path $resolvedOutputDir ("v2_current_telemetry_{0}.json" -f $timestamp)
$markdownPath = Join-Path $resolvedOutputDir ("v2_current_telemetry_{0}.md" -f $timestamp)

try {
    New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

    if ($SelfTest) {
        $sourceMode = "self_test"
        $source = "embedded_sample"
        $allLines = New-SelfTestLines
    } elseif ($hasInput) {
        $sourceMode = "file"
        $source = Resolve-RepoRelativePath -Path $InputLog
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Input log not found: $source"
        }
        $allLines = Get-Content -LiteralPath $source
    } else {
        $sourceMode = "serial"
        $source = $Port
        $allLines = Read-DiagnosticsFromSerial -SerialPortName $Port -SerialBaud $Baud -Seconds $ReadSeconds
    }

    Set-Content -LiteralPath $transcriptPath -Value @($allLines) -Encoding UTF8
    $boardPowerRows = @($allLines | ForEach-Object { Parse-BoardPowerLine -Line $_ } | Where-Object { $null -ne $_ })
    $powerStatusRows = @($allLines | ForEach-Object { Parse-PowerStatusLine -Line $_ } | Where-Object { $null -ne $_ })
    $latestPowerStatus = if (@($powerStatusRows).Count -gt 0) { $powerStatusRows[-1] } else { $null }

    $byBranch = @{}
    foreach ($row in $boardPowerRows) {
        if ($row.PSObject.Properties.Name -contains "branch") {
            $byBranch[[string]$row.branch] = $row
        }
    }

    $readings = @()
    $missingBranches = New-Object System.Collections.Generic.List[string]
    $gpioMismatches = New-Object System.Collections.Generic.List[string]
    foreach ($sensor in $expectedSensors) {
        $branch = [string]$sensor.branch
        $row = if ($byBranch.ContainsKey($branch)) { $byBranch[$branch] } else { $null }
        $present = $null -ne $row
        if (-not $present) {
            $missingBranches.Add($branch)
        }

        $gpio = if ($present -and ($row.PSObject.Properties.Name -contains "gpio")) { [int64]$row.gpio } else { $null }
        $allowedGpios = @($sensor.allowed_gpios | ForEach-Object { [int64]$_ })
        if ($present -and -not $allowedGpios.Contains($gpio)) {
            $gpioMismatches.Add(("{0}: expected one of GPIO {1}, got GPIO{2}" -f $branch, (@($allowedGpios) -join "/"), $gpio))
        }
        $hardwarePresent = if ($present -and ($row.PSObject.Properties.Name -contains "present")) {
            Convert-ToBool $row.present
        } else {
            $present
        }

        $readings += [pscustomobject][ordered]@{
            branch = $branch
            net = [string]$sensor.net
            role = [string]$sensor.role
            gpio_allowed = (@($allowedGpios) -join "/")
            gpio_reported = $gpio
            present = $hardwarePresent
            branch_reported = $present
            raw_adc = if ($present -and ($row.PSObject.Properties.Name -contains "raw_adc")) { [int64]$row.raw_adc } else { $null }
            adc_mv = if ($present -and ($row.PSObject.Properties.Name -contains "adc_mv")) { [int64]$row.adc_mv } else { $null }
            adc_calibrated = if ($present -and ($row.PSObject.Properties.Name -contains "adc_calibrated")) { Convert-ToBool $row.adc_calibrated } else { $false }
            sample_count = if ($present -and ($row.PSObject.Properties.Name -contains "sample_count")) { [int64]$row.sample_count } else { $null }
            current_ma_valid = if ($present -and ($row.PSObject.Properties.Name -contains "current_ma_valid")) { Convert-ToBool $row.current_ma_valid } else { $false }
            estimated_input_current_ma = if ($present -and ($row.PSObject.Properties.Name -contains "estimated_input_current_ma")) { [int64]$row.estimated_input_current_ma } else { $null }
            battery_side_mv = if ($present -and ($row.PSObject.Properties.Name -contains "battery_side_mv")) { [int64]$row.battery_side_mv } else { $null }
            power_mw_valid = if ($present -and ($row.PSObject.Properties.Name -contains "power_mw_valid")) { Convert-ToBool $row.power_mw_valid } else { $false }
            estimated_input_power_mw = if ($present -and ($row.PSObject.Properties.Name -contains "estimated_input_power_mw")) { [int64]$row.estimated_input_power_mw } else { $null }
            result = if ($present -and ($row.PSObject.Properties.Name -contains "result")) { [string]$row.result } else { $null }
            policy = if ($present -and ($row.PSObject.Properties.Name -contains "policy")) { [string]$row.policy } else { $null }
            raw_line = if ($present) { [string]$row.raw_line } else { $null }
        }
    }

    $expectedBranchNames = @($expectedSensors | ForEach-Object { [string]$_.branch })
    $unexpectedBranches = @($boardPowerRows |
        Where-Object { $_.PSObject.Properties.Name -contains "branch" } |
        Where-Object {
            $branchName = [string]$_.branch
            -not $expectedBranchNames.Contains($branchName)
        } |
        ForEach-Object { [string]$_.branch })

    $summary = [ordered]@{
        expected_sensor_count = @($expectedSensors).Count
        reading_count = @($boardPowerRows).Count
        all_expected_present = $missingBranches.Count -eq 0
        hardware_present_count = @($readings | Where-Object { $_.present }).Count
        low_power_status_present = $null -ne $latestPowerStatus
        missing_branches = @($missingBranches)
        unexpected_branches = @($unexpectedBranches)
        gpio_mismatches = @($gpioMismatches)
        software_power_control = $false
        telemetry_only = $true
        valid_for_power_decisions = $false
    }

    $lowPowerStatus = [ordered]@{
        present = $null -ne $latestPowerStatus
        shutdown_blockers = Get-FieldValue -Object $latestPowerStatus -Name "shutdown_blockers"
        automatic_shutdown_blocked_by_external_power = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "automatic_shutdown_blocked_by_external_power" -Default 0)
        hardware_shutdown_ms = Get-FieldValue -Object $latestPowerStatus -Name "hardware_shutdown_ms"
        pwr_hold_gpio = Get-FieldValue -Object $latestPowerStatus -Name "pwr_hold_gpio"
        pwr_hold_level = Get-FieldValue -Object $latestPowerStatus -Name "pwr_hold_level"
        pwr_hold_configured = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "pwr_hold_configured" -Default 0)
        pwr_hold_policy = Get-FieldValue -Object $latestPowerStatus -Name "pwr_hold_policy"
        battery_mv = Get-FieldValue -Object $latestPowerStatus -Name "battery_mv"
        battery_level = Get-FieldValue -Object $latestPowerStatus -Name "battery_level"
        battery_valid = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "battery_valid" -Default 0)
        last_shutdown_reason = Get-FieldValue -Object $latestPowerStatus -Name "last_shutdown_reason"
        last_shutdown_idle_ms = Get-FieldValue -Object $latestPowerStatus -Name "last_shutdown_idle_ms"
        last_shutdown_blockers = Get-FieldValue -Object $latestPowerStatus -Name "last_shutdown_blockers"
        voice_key_gpio = Get-FieldValue -Object $latestPowerStatus -Name "voice_key_gpio"
        hardware_shutdown_user_action = Get-FieldValue -Object $latestPowerStatus -Name "hardware_shutdown_user_action"
        raw_line = Get-FieldValue -Object $latestPowerStatus -Name "raw_line"
    }

    $manifest = [ordered]@{
        schema_version = 1
        schema_id = "listener.firmware.v2_current_telemetry_report.v1"
        captured_at = (Get-Date).ToString("o")
        source_mode = $sourceMode
        source = $source
        expected_sensors = $expectedSensors
        readings = $readings
        low_power_status = $lowPowerStatus
        summary = $summary
        transcript_path = $transcriptPath
        json_path = $jsonPath
        markdown_path = $markdownPath
    }

    ($manifest | ConvertTo-Json -Depth 12) | Set-Content -LiteralPath $jsonPath -Encoding UTF8
    New-MarkdownReport -Manifest ([pscustomobject]$manifest) -Readings $readings |
        Set-Content -LiteralPath $markdownPath -Encoding UTF8

    $powerStatusRequired = $SelfTest -or $hasPort
    $powerStatusOk = (-not $powerStatusRequired) -or $lowPowerStatus.present
    if ($summary.all_expected_present -and @($summary.gpio_mismatches).Count -eq 0 -and $powerStatusOk) {
        if ($SelfTest) {
            Write-Host "PASS: V2 current telemetry parser self-test passed."
        } else {
            Write-Host "PASS: V2 current telemetry captured."
        }
    } else {
        Write-Host "FAIL: V2 current telemetry is missing expected readings."
        if ($powerStatusRequired -and -not $lowPowerStatus.present) {
            Write-Host " - missing ~POWER:STATUS low-power status"
        }
        Write-Host ("JSON: {0}" -f $jsonPath)
        Write-Host ("Report: {0}" -f $markdownPath)
        exit 1
    }

    Write-Host ("JSON: {0}" -f $jsonPath)
    Write-Host ("Report: {0}" -f $markdownPath)
} finally {
    if ($cleanupOutputDir -and (Test-Path -LiteralPath $resolvedOutputDir)) {
        Remove-Item -LiteralPath $resolvedOutputDir -Recurse -Force
    }
}
