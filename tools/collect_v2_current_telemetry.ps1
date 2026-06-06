<#
.SYNOPSIS
Collect V2 current telemetry and low-power status evidence.

.DESCRIPTION
Reads firmware diagnostics and writes a focused report for the two current
telemetry inputs only:
  - TPS63020_I_ADC / GPIO10 / BAT_IN -> VIN_TPS63020
  - SY7088_I_ADC / GPIO9 / BAT_IN -> VIN_SY7088

Serial mode also requests ~POWER:STATUS so the same report captures sleep
duration, sleep-entry battery, wake battery, and drain rate after a wake.
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
        gpio = 10
        role = "BAT_IN -> VIN_TPS63020 input branch current telemetry"
    },
    [ordered]@{
        branch = "SY7088_input_branch"
        net = "SY7088_I_ADC"
        gpio = 9
        role = "BAT_IN -> VIN_SY7088 input branch current telemetry"
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
        "~BOARD:POWER branch=TPS63020_input_branch gpio=10 raw_adc=1234 adc_mv=995 adc_calibrated=1 sample_count=4 calibration_status=uncalibrated current_model=""INA180A2 10mR current_mA=adc_mv*2"" current_calibrated=0 current_ma_valid=0 estimated_input_current_ma=0 battery_side_mv=4012 battery_voltage_source=""BAT_V_ADC/GPIO8 68K/68K midpoint, VBAT~=2*ADC"" power_mw_valid=0 estimated_input_power_mw=0 result=ESP_OK policy=v2_battery_side_input_branch_current_ina180a2_10mR_adc_mv_x2_with_battery_mv_from_gpio8_div2",
        "~BOARD:POWER branch=SY7088_input_branch gpio=9 raw_adc=2345 adc_mv=1888 adc_calibrated=1 sample_count=4 calibration_status=uncalibrated current_model=""INA180A2 10mR current_mA=adc_mv*2"" current_calibrated=0 current_ma_valid=0 estimated_input_current_ma=0 battery_side_mv=4012 battery_voltage_source=""BAT_V_ADC/GPIO8 68K/68K midpoint, VBAT~=2*ADC"" power_mw_valid=0 estimated_input_power_mw=0 result=ESP_OK policy=v2_battery_side_input_branch_current_ina180a2_10mR_adc_mv_x2_with_battery_mv_from_gpio8_div2",
        "~POWER:STATUS state=CONNECTED_IDLE blockers=0x00000000 blocker_names=none sleep_blockers=0x00000000 sleep_blocker_names=none idle_ms=100 user_idle_ms=100 radio_idle_ms=100 ble_connected=1 automatic_sleep_blocked_by_external_power=0 external_power_present=0 usb_power_present=0 charging=0 charge_full=0 usb_det_level=low bat_chg_level=high bat_std_level=high usb_det_policy=v2_gpio7_r37_r32_10K_10K_divider charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_mv=3988 battery_level=82 battery_valid=1 last_sleep_reason=MANUAL_COMMAND last_wake_source=GPIO guard=1 audio_idle_ms=60000 connected_idle_ms=300000 disconnected_idle_ms=600000 overnight_sleep_ms=1800000 sleep_stats_valid=1 sleep_duration_ms=28800000 sleep_entry_battery_mv=4012 sleep_entry_battery_level=84 sleep_entry_battery_valid=1 wake_battery_mv=3988 wake_battery_level=82 wake_battery_valid=1 sleep_drain_mv=24 sleep_drain_level=2 sleep_drain_mv_per_hour=3 sleep_drain_level_per_hour_x100=25 wake_policy=v2_ec11_provisional wake_gpio_mask=0x0000000000000000 wake_capable_keys=EC11_KEY/GPIO11 wake_key_gpio=11 wake_key_rtc_capable=1 voice_key_gpio=11 voice_key_rtc_capable=1 voice_key_deep_sleep_wake=0 voice_key_limitation=""V2 EC11-KEY_IO/GPIO11 is the RTC-capable wake candidate; deep-sleep wake remains disabled until power-latch isolation, leakage, pull policy, and false-wake behavior are signed off"" wake_user_action=""use USB reset or power cycle until EC11 wake is signed off"""
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
    $lines.Add("| Branch | Net | GPIO | Present | Raw ADC | ADC mV | ADC calibrated | Input current mA valid | Battery mV | Input power mW valid | Result |")
    $lines.Add("|---|---|---:|---|---:|---:|---|---|---:|---|---|")
    foreach ($reading in $Readings) {
        $lines.Add((
            "| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} |" -f
            $reading.branch,
            $reading.net,
            $reading.gpio_expected,
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
    $lines.Add(("All expected sensors present: {0}" -f $Manifest.summary.all_expected_present))
    $lines.Add("")
    $lines.Add("## Low Power Status")
    if ($null -ne $Manifest.low_power_status -and $Manifest.low_power_status.present) {
        $lines.Add(("- Sleep stats valid: {0}" -f $Manifest.low_power_status.sleep_stats_valid))
        $lines.Add(("- Sleep duration ms: {0}" -f $Manifest.low_power_status.sleep_duration_ms))
        $lines.Add(("- Entry battery mV: {0}" -f $Manifest.low_power_status.sleep_entry_battery_mv))
        $lines.Add(("- Wake battery mV: {0}" -f $Manifest.low_power_status.wake_battery_mv))
        $lines.Add(("- Sleep drain mV: {0}" -f $Manifest.low_power_status.sleep_drain_mv))
        $lines.Add(("- Sleep drain mV/hour: {0}" -f $Manifest.low_power_status.sleep_drain_mv_per_hour))
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
        if ($present -and $gpio -ne [int64]$sensor.gpio) {
            $gpioMismatches.Add(("{0}: expected GPIO{1}, got GPIO{2}" -f $branch, $sensor.gpio, $gpio))
        }

        $readings += [pscustomobject][ordered]@{
            branch = $branch
            net = [string]$sensor.net
            role = [string]$sensor.role
            gpio_expected = [int]$sensor.gpio
            gpio_reported = $gpio
            present = $present
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
        sleep_stats_valid = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "sleep_stats_valid" -Default 0)
        sleep_duration_ms = Get-FieldValue -Object $latestPowerStatus -Name "sleep_duration_ms"
        sleep_entry_battery_mv = Get-FieldValue -Object $latestPowerStatus -Name "sleep_entry_battery_mv"
        sleep_entry_battery_level = Get-FieldValue -Object $latestPowerStatus -Name "sleep_entry_battery_level"
        sleep_entry_battery_valid = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "sleep_entry_battery_valid" -Default 0)
        wake_battery_mv = Get-FieldValue -Object $latestPowerStatus -Name "wake_battery_mv"
        wake_battery_level = Get-FieldValue -Object $latestPowerStatus -Name "wake_battery_level"
        wake_battery_valid = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "wake_battery_valid" -Default 0)
        sleep_drain_mv = Get-FieldValue -Object $latestPowerStatus -Name "sleep_drain_mv"
        sleep_drain_level = Get-FieldValue -Object $latestPowerStatus -Name "sleep_drain_level"
        sleep_drain_mv_per_hour = Get-FieldValue -Object $latestPowerStatus -Name "sleep_drain_mv_per_hour"
        sleep_drain_level_per_hour_x100 = Get-FieldValue -Object $latestPowerStatus -Name "sleep_drain_level_per_hour_x100"
        battery_mv = Get-FieldValue -Object $latestPowerStatus -Name "battery_mv"
        battery_level = Get-FieldValue -Object $latestPowerStatus -Name "battery_level"
        battery_valid = Convert-ToBool (Get-FieldValue -Object $latestPowerStatus -Name "battery_valid" -Default 0)
        last_sleep_reason = Get-FieldValue -Object $latestPowerStatus -Name "last_sleep_reason"
        last_wake_source = Get-FieldValue -Object $latestPowerStatus -Name "last_wake_source"
        wake_policy = Get-FieldValue -Object $latestPowerStatus -Name "wake_policy"
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
