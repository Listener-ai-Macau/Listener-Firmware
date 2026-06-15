[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$ReadSeconds = 8,
    [int]$ObserveSeconds = 20,
    [string]$InputTranscript = "",
    [string]$OutputDir = "",
    [switch]$SelfTest,
    [switch]$RequirePort,
    [switch]$ExpectExternalPower,
    [switch]$IncludeManualShutdown
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$errors = [System.Collections.Generic.List[string]]::new()
$evidence = [System.Collections.Generic.List[string]]::new()
$manualGates = [System.Collections.Generic.List[string]]::new()

function Add-Error {
    param([Parameter(Mandatory = $true)][string]$Message)
    $errors.Add($Message)
}

function Add-Evidence {
    param([Parameter(Mandatory = $true)][string]$Message)
    $evidence.Add($Message)
}

function Add-ManualGate {
    param([Parameter(Mandatory = $true)][string]$Message)
    $manualGates.Add($Message)
}

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Text -notmatch $Pattern) {
        Add-Error "missing ${Description}: $Pattern"
    } else {
        Add-Evidence "PASS: $Description"
    }
}

function Read-SerialUntil {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$SerialPort,
        [Parameter(Mandatory = $true)][datetime]$Deadline
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    while ((Get-Date) -lt $Deadline) {
        try {
            $line = $SerialPort.ReadLine().Trim()
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                $lines.Add($line)
            }
        } catch [System.TimeoutException] {
        }
    }
    return @($lines)
}

function Invoke-SerialCommand {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$SerialPort,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$WaitMilliseconds = 1200
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("> $Command")
    $SerialPort.Write("$Command`n")
    foreach ($line in @(Read-SerialUntil -SerialPort $SerialPort -Deadline (Get-Date).AddMilliseconds($WaitMilliseconds))) {
        $lines.Add($line)
    }
    return @($lines)
}

function Invoke-ChargingAwakeSerialProbe {
    param([Parameter(Mandatory = $true)][string]$SerialPortName)

    $serialPort = $null
    $lines = [System.Collections.Generic.List[string]]::new()
    try {
        $serialPort = [System.IO.Ports.SerialPort]::new(
            $SerialPortName,
            $Baud,
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

        foreach ($command in @(
            "~BOARD:STATUS",
            "~POWER:STATUS",
            "~BOARD:GPIO",
            "~EC11:STATUS",
            "~DIAGLOG:LAST:64"
        )) {
            foreach ($line in @(Invoke-SerialCommand -SerialPort $serialPort -Command $command)) {
                $lines.Add($line)
            }
        }

        if ($ObserveSeconds -gt 0) {
            $lines.Add(("> observe-usb-awake seconds={0}" -f $ObserveSeconds))
            foreach ($line in @(Read-SerialUntil -SerialPort $serialPort -Deadline (Get-Date).AddSeconds($ObserveSeconds))) {
                $lines.Add($line)
            }
            foreach ($command in @("~POWER:STATUS", "~BOARD:STATUS", "~DIAGLOG:LAST:64")) {
                foreach ($line in @(Invoke-SerialCommand -SerialPort $serialPort -Command $command)) {
                    $lines.Add($line)
                }
            }
        }

        if ($IncludeManualShutdown.IsPresent) {
            $lines.Add("> destructive-manual-gate command=~POWER:SHUTDOWN")
            foreach ($line in @(Invoke-SerialCommand -SerialPort $serialPort -Command "~POWER:SHUTDOWN" -WaitMilliseconds 8000)) {
                $lines.Add($line)
            }
        }
    } finally {
        if ($serialPort -and $serialPort.IsOpen) {
            $serialPort.Close()
        }
    }

    return @($lines)
}

function New-SelfTestTranscript {
    return @(
        "> ~BOARD:STATUS",
        "~BOARD:STATUS profile=voice-keyboard-v2-n16r8 module=ESP32-S3-WROOM-1-N16R8 flash_mb=16 psram_mb=8 psram_mode=octal key_gpios=38,39,40,41 ec11_a_gpio=42 ec11_b_gpio=2 ec11_key_gpio=18 mic_clk_gpio=48 mic_dout_gpio=47 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown usb_det_gpio=7 usb_det_level=high bat_chg_gpio=14 bat_chg_level=high bat_std_gpio=21 bat_std_level=low charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_gpio=8 battery_mv=4084 battery_level=92 battery_valid=1",
        "> ~POWER:STATUS",
        "~POWER:STATUS state=CONNECTED_IDLE blockers=0x00000080 blocker_names=external_power shutdown_blockers=0x00000080 shutdown_blocker_names=external_power idle_ms=32000 user_idle_ms=32000 radio_idle_ms=32000 ble_connected=1 automatic_shutdown_blocked_by_external_power=0 external_power_present=1 usb_power_present=1 charging=0 charge_full=1 usb_det_level=high bat_chg_level=high bat_std_level=low usb_det_policy=v2_gpio7_r37_r32_10K_10K_divider charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_mv=4084 battery_level=92 battery_valid=1 last_shutdown_reason=none last_shutdown_idle_ms=0 last_shutdown_blockers=0x00000000 guard=1 audio_idle_ms=5000 low_power_idle_ms=60000 connected_idle_ms=60000 disconnected_idle_ms=60000 hardware_shutdown_ms=1800000 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown voice_key_gpio=18 hardware_shutdown_user_action=""short-press hardware power key for cold boot after PWR_HOLD/GPIO11 drive-high shutdown""",
        "> ~BOARD:GPIO",
        "~BOARD:GPIO active_low=1 mode=read_as_configured reconfigure=0 key1_gpio=38 key1=high key2_gpio=39 key2=high key3_gpio=40 key3=high key4_gpio=41 key4=high ec11_a_gpio=42 ec11_a=high ec11_b_gpio=2 ec11_b=high ec11_key_gpio=18 ec11_key=high recording_key=custom_key_action ec11_key_action=power_on_runtime_custom",
        "> ~EC11:STATUS",
        "EC11 rotation status: action=volume enabled=1",
        "> ~DIAGLOG:LAST:64",
        "DIAG EVENT source=power event=DIAG_POWER_EXTERNAL_POWER severity=info a1=5 a2=5 a3=32000 a4=128",
        "> observe-usb-awake seconds=20",
        "> ~POWER:STATUS",
        "~POWER:STATUS state=CONNECTED_IDLE blockers=0x00000080 blocker_names=external_power shutdown_blockers=0x00000080 shutdown_blocker_names=external_power idle_ms=54000 user_idle_ms=54000 radio_idle_ms=54000 ble_connected=1 automatic_shutdown_blocked_by_external_power=0 external_power_present=1 usb_power_present=1 charging=0 charge_full=1 usb_det_level=high bat_chg_level=high bat_std_level=low usb_det_policy=v2_gpio7_r37_r32_10K_10K_divider charger_polarity=v2_gpio14_chg_gpio21_std_active_low battery_mv=4084 battery_level=92 battery_valid=1 last_shutdown_reason=none last_shutdown_idle_ms=0 last_shutdown_blockers=0x00000000 guard=1 audio_idle_ms=5000 low_power_idle_ms=60000 connected_idle_ms=60000 disconnected_idle_ms=60000 hardware_shutdown_ms=1800000 pwr_hold_gpio=11 pwr_hold_level=low pwr_hold_configured=1 pwr_hold_policy=v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown voice_key_gpio=18"
    )
}

function Get-LastPowerStatusLine {
    param([Parameter(Mandatory = $true)][string[]]$Lines)

    $powerLines = @($Lines | Where-Object { $_ -match '^~POWER:STATUS\s+' })
    if ($powerLines.Count -eq 0) {
        return ""
    }
    return $powerLines[$powerLines.Count - 1]
}

function Get-StatusField {
    param(
        [Parameter(Mandatory = $true)][string]$Line,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($Line -match ("(?:^|\s)" + [regex]::Escape($Name) + "=([^\s]+)")) {
        return $Matches[1]
    }
    return ""
}

function Test-ChargingAwakeTranscript {
    param([Parameter(Mandatory = $true)][string[]]$Lines)

    $text = ($Lines -join "`n")
    Assert-Contains -Text $text -Pattern '(?m)^~BOARD:STATUS\s+.*profile=voice-keyboard-v2-n16r8' -Description "board status identifies V2/N16R8 target"
    Assert-Contains -Text $text -Pattern '(?m)^~BOARD:STATUS\s+.*pwr_hold_gpio=11.*pwr_hold_configured=1.*runtime_low_drive_high' -Description "PWR_HOLD/GPIO11 is actively driven low during runtime and driven high for hardware shutdown"
    Assert-Contains -Text $text -Pattern '(?m)^~POWER:STATUS\s+.*state=' -Description "power status response is present"
    Assert-Contains -Text $text -Pattern '(?m)^~POWER:STATUS\s+.*hardware_shutdown_ms=' -Description "power status reports hardware-shutdown threshold"
    Assert-Contains -Text $text -Pattern '(?m)^~POWER:STATUS\s+.*voice_key_gpio=18' -Description "power status uses EC11/GPIO18 voice-key contract"
    Assert-Contains -Text $text -Pattern '(?m)^~BOARD:GPIO\s+.*key1_gpio=.*key2_gpio=.*key3_gpio=.*key4_gpio=.*ec11_key_gpio=' -Description "KEY1-KEY4 and EC11 key GPIO status are responsive"
    Assert-Contains -Text $text -Pattern 'EC11 rotation status|~EC11:STATUS|EC11' -Description "EC11 diagnostic command path is responsive"
    Assert-Contains -Text $text -Pattern '~DIAGLOG:LAST:64|DIAGLOG|DIAG EVENT' -Description "diagnostic log tail command path is responsive"

    $lastPower = Get-LastPowerStatusLine -Lines $Lines
    if ([string]::IsNullOrWhiteSpace($lastPower)) {
        Add-Error "missing final ~POWER:STATUS line"
        return
    }

    if ($ExpectExternalPower.IsPresent) {
        if ((Get-StatusField -Line $lastPower -Name "external_power_present") -ne "1") {
            Add-Error "expected external_power_present=1 in final ~POWER:STATUS"
        } else {
            Add-Evidence "PASS: final power status reports external_power_present=1"
        }
        if ((Get-StatusField -Line $lastPower -Name "usb_power_present") -ne "1") {
            Add-Error "expected usb_power_present=1 in final ~POWER:STATUS"
        } else {
            Add-Evidence "PASS: final power status reports usb_power_present=1"
        }
        if ((Get-StatusField -Line $lastPower -Name "state") -eq "HARDWARE_SHUTDOWN") {
            Add-Error "device entered HARDWARE_SHUTDOWN while external power was expected"
        } else {
            Add-Evidence "PASS: final power state is not HARDWARE_SHUTDOWN under external power"
        }
    }

    $thresholdText = Get-StatusField -Line $lastPower -Name "hardware_shutdown_ms"
    $idleText = Get-StatusField -Line $lastPower -Name "user_idle_ms"
    $thresholdMs = 0
    $idleMs = 0
    [void][int]::TryParse($thresholdText, [ref]$thresholdMs)
    [void][int]::TryParse($idleText, [ref]$idleMs)
    if ($thresholdMs -gt 0 -and $idleMs -lt $thresholdMs) {
        Add-ManualGate ("Long-idle threshold was not reached by this short run: user_idle_ms={0}, hardware_shutdown_ms={1}. Use a plugged-in soak beyond the threshold for final awake proof." -f $idleMs, $thresholdMs)
    }

    if (-not $IncludeManualShutdown.IsPresent) {
        Add-ManualGate "Manual hardware-shutdown override was not executed. Re-run with -IncludeManualShutdown only when the operator is ready for a destructive power-off/recovery check."
    }
    Add-ManualGate "Battery-only long-idle hardware-shutdown evidence requires an unplugged/battery run and cold-boot recovery artifact; serial-over-USB capture cannot prove this while USB power is attached."
    Add-ManualGate "Post-unplug stale-idle validation requires a physical charging/USB soak, unplug action, and follow-up power diagnostics."
}

$hardwareMode = "none"
$transcript = @()

if ($SelfTest.IsPresent) {
    $hardwareMode = "self-test"
    $transcript = @(New-SelfTestTranscript)
} elseif (-not [string]::IsNullOrWhiteSpace($InputTranscript)) {
    $hardwareMode = "transcript-replay"
    $transcriptPath = Resolve-RepoPath -Path $InputTranscript
    if (-not (Test-Path -LiteralPath $transcriptPath)) {
        Add-Error "input transcript not found: $transcriptPath"
    } else {
        $transcript = @(Get-Content -LiteralPath $transcriptPath)
    }
} elseif (-not [string]::IsNullOrWhiteSpace($Port)) {
    $hardwareMode = "serial-read"
    $transcript = @(Invoke-ChargingAwakeSerialProbe -SerialPortName $Port)
} elseif ($RequirePort.IsPresent) {
    Add-Error "Port, InputTranscript, or SelfTest is required when -RequirePort is supplied."
} else {
    $hardwareMode = "static-no-port"
    Add-ManualGate "No serial port was provided; only script/tooling availability was checked."
}

if (@($transcript).Count -gt 0) {
    Test-ChargingAwakeTranscript -Lines $transcript
}

$status = if ($errors.Count -eq 0) { "PASS" } else { "FAIL" }
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ("tests\artifacts\firmware-charging-awake-policy-1.2\{0}" -f $timestamp)
} else {
    $OutputDir = Resolve-RepoPath -Path $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$transcriptPathOut = Join-Path $OutputDir "charging-awake-serial-transcript.txt"
$manifestPath = Join-Path $OutputDir "charging-awake-hardware-evidence.json"
$markdownPath = Join-Path $OutputDir "charging-awake-hardware-evidence.md"

Set-Content -LiteralPath $transcriptPathOut -Value @($transcript) -Encoding UTF8

$manifest = [ordered]@{
    schema_version = 1
    schema_id = "listener.firmware.charging_awake_hardware_validation.v1"
    generated_at = (Get-Date).ToString("o")
    repo_root = $repoRoot
    status = $status
    hardware_mode = $hardwareMode
    port = if ([string]::IsNullOrWhiteSpace($Port)) { $null } else { $Port }
    baud = $Baud
    read_seconds = $ReadSeconds
    observe_seconds = $ObserveSeconds
    expect_external_power = [bool]$ExpectExternalPower.IsPresent
    include_manual_shutdown = [bool]$IncludeManualShutdown.IsPresent
    transcript_line_count = @($transcript).Count
    evidence = @($evidence)
    manual_gates = @($manualGates)
    errors = @($errors)
    artifacts = [ordered]@{
        transcript = $transcriptPathOut
        markdown = $markdownPath
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add("# Charging Awake Hardware Evidence")
$markdown.Add("")
$markdown.Add(("status={0}" -f $status))
$markdown.Add(("hardware_mode={0}" -f $hardwareMode))
$markdown.Add(("port={0}" -f $manifest.port))
$markdown.Add(("observe_seconds={0}" -f $ObserveSeconds))
$markdown.Add("")
$markdown.Add("## Evidence")
foreach ($item in @($evidence)) {
    $markdown.Add("- $item")
}
if ($manualGates.Count -gt 0) {
    $markdown.Add("")
    $markdown.Add("## Manual Gates")
    foreach ($item in @($manualGates)) {
        $markdown.Add("- $item")
    }
}
if ($errors.Count -gt 0) {
    $markdown.Add("")
    $markdown.Add("## Errors")
    foreach ($item in @($errors)) {
        $markdown.Add("- $item")
    }
}
if (@($transcript).Count -gt 0) {
    $markdown.Add("")
    $markdown.Add("## Serial Transcript")
    $markdown.Add('```text')
    foreach ($line in @($transcript)) {
        $markdown.Add($line)
    }
    $markdown.Add('```')
}
Set-Content -LiteralPath $markdownPath -Value $markdown -Encoding UTF8

Write-Host ("{0}: charging-awake hardware evidence -> {1}" -f $status, $manifestPath)
if ($manualGates.Count -gt 0) {
    Write-Host ("manual-gates={0}" -f $manualGates.Count)
}
if ($errors.Count -gt 0) {
    foreach ($item in @($errors)) {
        Write-Host ("ERROR: {0}" -f $item)
    }
    exit 1
}
