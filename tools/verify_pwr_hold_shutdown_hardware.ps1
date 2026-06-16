[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Baud = 115200,
    [string]$OutputDir = "",
    [int]$PreCommandReadMs = 1200,
    [int]$ShutdownReadSeconds = 30,
    [int]$DisappearTimeoutSeconds = 35,
    [int]$ReappearTimeoutSeconds = 60,
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot "docs/validation/voice-keyboard-firmware-full-function-test-1.7/pwr-hold-new-board-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$transcriptPath = Join-Path $OutputDir "shutdown_serial_transcript.txt"
$pollPath = Join-Path $OutputDir "port_poll_after_shutdown.txt"
$summaryPath = Join-Path $OutputDir "shutdown_summary.json"
$markdownPath = Join-Path $OutputDir "shutdown_summary.md"

$transcript = [System.Collections.Generic.List[string]]::new()
$pollLines = [System.Collections.Generic.List[string]]::new()

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    $transcript.Add($line) | Out-Null
}

function Add-RawTranscript {
    param([Parameter(Mandatory = $true)][string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return
    }
    Write-Host $Text -NoNewline
    $transcript.Add($Text.TrimEnd("`r", "`n")) | Out-Null
}

function Add-PollLine {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    $pollLines.Add($line) | Out-Null
}

function Show-OperatorPrompt {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Add-Transcript "operator_prompt title=$Title message=$Message"
    if ($NoPrompt.IsPresent) {
        return
    }
    Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.MessageBox]::Show(
        $Message,
        $Title,
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information)
}

function Get-SerialPorts {
    try {
        return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    } catch {
        return @()
    }
}

function Format-Ports {
    param([string[]]$Ports)
    if ($Ports.Count -eq 0) {
        return "<none>"
    }
    return ($Ports -join ",")
}

function Test-SerialPortOpenable {
    param([Parameter(Mandatory = $true)][string]$SerialPortName)
    $probe = [System.IO.Ports.SerialPort]::new(
        $SerialPortName,
        $Baud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One)
    $probe.ReadTimeout = 100
    $probe.WriteTimeout = 100
    $probe.DtrEnable = $false
    $probe.RtsEnable = $false
    try {
        $probe.Open()
        return $true
    } catch {
        $message = $_.Exception.Message
        Add-PollLine ("port_open_probe_failed port={0} message={1}" -f $SerialPortName, $message)
        if ($message -match "Could not find file|does not exist|The system cannot find") {
            return $false
        }
        return $true
    } finally {
        if ($probe.IsOpen) {
            $probe.Close()
        }
    }
}

function Wait-PortPresence {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPort,
        [Parameter(Mandatory = $true)][bool]$ShouldBePresent,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $observed = $false
    $lastPorts = @()
    $pollIndex = 0
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        $lastPorts = $ports
        $present = @($ports | ForEach-Object { $_.ToUpperInvariant() }) -contains $TargetPort.ToUpperInvariant()
        if ($present -and (-not $ShouldBePresent)) {
            $present = Test-SerialPortOpenable -SerialPortName $TargetPort
        }
        Add-PollLine ("{0} poll={1:00} target={2} present={3} ports={4}" -f $Label, $pollIndex, $TargetPort, $present, (Format-Ports -Ports $ports))
        if ($present -eq $ShouldBePresent) {
            $observed = $true
            break
        }
        $pollIndex += 1
        Start-Sleep -Milliseconds 500
    }
    return [ordered]@{
        observed = $observed
        last_ports = @($lastPorts)
    }
}

function Open-ValidationSerial {
    param([Parameter(Mandatory = $true)][string]$SerialPortName)
    $serial = [System.IO.Ports.SerialPort]::new(
        $SerialPortName,
        $Baud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 100
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    return $serial
}

function Read-SerialWindow {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][int]$Milliseconds,
        [switch]$StopWhenPortGone
    )

    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $Serial.ReadExisting()
            if (-not [string]::IsNullOrEmpty($chunk)) {
                Add-RawTranscript $chunk
            }
        } catch {
            Add-Transcript ("serial_read_error {0}" -f $_.Exception.Message)
            break
        }
        if ($StopWhenPortGone.IsPresent) {
            $ports = @(Get-SerialPorts)
            $present = @($ports | ForEach-Object { $_.ToUpperInvariant() }) -contains $Port.ToUpperInvariant()
            if (-not $present) {
                Add-Transcript ("serial_port_disappeared_while_reading port={0}" -f $Port)
                break
            }
        }
        Start-Sleep -Milliseconds 50
    }
}

function Invoke-SerialCommand {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMilliseconds = 1200
    )
    Add-Transcript ("> $Command")
    $Serial.Write("$Command`n")
    $Serial.BaseStream.Flush()
    Read-SerialWindow -Serial $Serial -Milliseconds $ReadMilliseconds
}

Show-OperatorPrompt `
    -Title "准备验证新板硬件关机" `
    -Message "请确认正在测试的是新板，电池已接好，USB-C 已连接电脑，当前没有其它串口工具占用 $Port。确认后脚本会发送 ~POWER:SHUTDOWN，设备会尝试真正断电。"

$initialPorts = @(Get-SerialPorts)
Add-PollLine ("initial target={0} ports={1}" -f $Port, (Format-Ports -Ports $initialPorts))

$serial = $null
$serialOpen = $false
try {
    $serial = Open-ValidationSerial -SerialPortName $Port
    $serialOpen = $true
    Add-Transcript ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Start-Sleep -Milliseconds 250
    Read-SerialWindow -Serial $serial -Milliseconds 300

    Invoke-SerialCommand -Serial $serial -Command "~POWER:STATUS" -ReadMilliseconds $PreCommandReadMs
    Invoke-SerialCommand -Serial $serial -Command "~BOARD:STATUS" -ReadMilliseconds $PreCommandReadMs
    Invoke-SerialCommand -Serial $serial -Command "~POWER:SHUTDOWN" -ReadMilliseconds ($ShutdownReadSeconds * 1000)
    Read-SerialWindow -Serial $serial -Milliseconds 500 -StopWhenPortGone
} finally {
    if ($serial -and $serialOpen -and $serial.IsOpen) {
        try {
            $serial.Close()
            Add-Transcript "serial_closed"
        } catch {
            Add-Transcript ("serial_close_error {0}" -f $_.Exception.Message)
        }
    }
}

$disappear = Wait-PortPresence -TargetPort $Port -ShouldBePresent $false -TimeoutSeconds $DisappearTimeoutSeconds -Label "after_shutdown"
$portDisappeared = [bool]$disappear.observed

if ($portDisappeared) {
    Show-OperatorPrompt `
        -Title "新板已进入硬件关机" `
        -Message "脚本已检测到 $Port 消失。现在请短按一次硬件电源键/EC11 旋钮按键，让新板冷启动；看到灯亮或 Windows 重新出现串口后，确认继续采集恢复证据。"
} else {
    Show-OperatorPrompt `
        -Title "未检测到串口消失" `
        -Message "脚本没有在 ${DisappearTimeoutSeconds} 秒内检测到 $Port 消失。请观察新板是否真的断电；如果还亮着，请不要强行拔线，直接确认让脚本记录失败证据。"
}

$reappear = Wait-PortPresence -TargetPort $Port -ShouldBePresent $true -TimeoutSeconds $ReappearTimeoutSeconds -Label "after_power_key"
$portReappeared = [bool]$reappear.observed
$postRestoreProbeError = ""
$postRestoreStartIndex = $transcript.Count

if ($portReappeared) {
    $restoreDeadline = (Get-Date).AddSeconds(25)
    $restoreAttempt = 0
    while ((Get-Date) -lt $restoreDeadline) {
        $serial = $null
        $serialOpen = $false
        try {
            Add-Transcript ("reopen_after_cold_boot_attempt attempt={0}" -f $restoreAttempt)
            $serial = Open-ValidationSerial -SerialPortName $Port
            $serialOpen = $true
            Add-Transcript ("reopen_after_cold_boot port={0}" -f $Port)
            Start-Sleep -Milliseconds 2500
            Read-SerialWindow -Serial $serial -Milliseconds 2500
            Invoke-SerialCommand -Serial $serial -Command "~POWER:STATUS" -ReadMilliseconds $PreCommandReadMs
            Invoke-SerialCommand -Serial $serial -Command "~BOARD:STATUS" -ReadMilliseconds $PreCommandReadMs
            $postRestoreProbeError = ""
            break
        } catch {
            $postRestoreProbeError = $_.Exception.Message
            Add-Transcript ("reopen_after_cold_boot_error attempt={0} {1}" -f $restoreAttempt, $postRestoreProbeError)
            Start-Sleep -Milliseconds 1000
        } finally {
            if ($serial -and $serialOpen -and $serial.IsOpen) {
                try {
                    $serial.Close()
                    Add-Transcript "serial_closed_after_reopen"
                } catch {
                    Add-Transcript ("serial_close_after_reopen_error {0}" -f $_.Exception.Message)
                }
            }
        }
        $restoreAttempt += 1
    }
}

$text = ($transcript -join "`n")
$pollText = ($pollLines -join "`n")
$postRestoreLines = @()
if ($transcript.Count -gt $postRestoreStartIndex) {
    for ($i = $postRestoreStartIndex; $i -lt $transcript.Count; $i++) {
        $postRestoreLines += $transcript[$i]
    }
}
$postRestoreText = ($postRestoreLines -join "`n")
$sawPowerStatus = $text -match "~POWER:STATUS"
$sawBoardStatus = $text -match "~BOARD:STATUS"
$sawPostRestorePowerStatus = $postRestoreText -match "~POWER:STATUS\s+state="
$sawPostRestoreBoardStatus = $postRestoreText -match "~BOARD:STATUS\s+profile="
$sawShutdownEntry = $text -match "hardware shutdown|manual_command|POWER:SHUTDOWN"
$sawShutdownAccepted = $text -match "~POWER:SHUTDOWN result=accepted"
$sawHighRequest = $text -match "requested_level=1|driven high for hardware shutdown|shutdown-high set|entering hardware shutdown"
$sawHighReadback = $text -match "requested_level=1 actual_level=1|settled high|settled after minimum high hold|observed requested level"
$sawReadbackMismatch = $text -match "readback mismatch|did not settle high"
$sawRestoreRuntimeLow = $text -match "restoring runtime low|runtime-low restore|SHUTDOWN_FAILED_RESTORE"
$sawColdBootStatus = $portReappeared -and $sawPostRestorePowerStatus -and $sawPostRestoreBoardStatus
$pass = $sawPowerStatus -and
        $sawBoardStatus -and
        $sawShutdownEntry -and
        $sawShutdownAccepted -and
        $sawHighRequest -and
        (-not $sawReadbackMismatch) -and
        (-not $sawRestoreRuntimeLow) -and
        $portDisappeared -and
        $portReappeared -and
        $sawColdBootStatus

$summary = [ordered]@{
    schema_version = 1
    result = $(if ($pass) { "PASS" } else { "FAIL" })
    port = $Port
    output_dir = $OutputDir
    transcript = $transcriptPath
    port_poll = $pollPath
    saw_power_status = $sawPowerStatus
    saw_board_status = $sawBoardStatus
    saw_post_restore_power_status = $sawPostRestorePowerStatus
    saw_post_restore_board_status = $sawPostRestoreBoardStatus
    saw_shutdown_entry = $sawShutdownEntry
    saw_shutdown_accepted = $sawShutdownAccepted
    saw_pwr_hold_high_request = $sawHighRequest
    saw_pwr_hold_high_readback = $sawHighReadback
    saw_readback_mismatch = $sawReadbackMismatch
    saw_restore_runtime_low = $sawRestoreRuntimeLow
    port_disappeared = $portDisappeared
    port_reappeared_after_power_key = $portReappeared
    saw_cold_boot_status = $sawColdBootStatus
    post_restore_probe_error = $postRestoreProbeError
    initial_ports = @($initialPorts)
    disappear_last_ports = @($disappear.last_ports)
    reappear_last_ports = @($reappear.last_ports)
}

Set-Content -LiteralPath $transcriptPath -Value $transcript -Encoding UTF8
Set-Content -LiteralPath $pollPath -Value $pollLines -Encoding UTF8
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

$markdown = @(
    "# PWR_HOLD New Board Hardware Shutdown Evidence",
    "",
    "- Result: $($summary.result)",
    ("- Port: ``{0}``" -f $Port),
    "- Shutdown command accepted: $sawShutdownAccepted",
    "- PWR_HOLD high request observed: $sawHighRequest",
    "- PWR_HOLD high readback observed: $sawHighReadback",
    "- Serial port disappeared after shutdown: $portDisappeared",
    "- Serial port reappeared after hardware power key: $portReappeared",
    "- Cold-boot status captured: $sawColdBootStatus",
    "- Post-restore POWER status captured: $sawPostRestorePowerStatus",
    "- Post-restore BOARD status captured: $sawPostRestoreBoardStatus",
    "- Post-restore probe error: $postRestoreProbeError",
    "- Runtime-low restore after failure observed: $sawRestoreRuntimeLow",
    "- Readback mismatch observed: $sawReadbackMismatch",
    "",
    "Artifacts:",
    "",
    "- ``shutdown_serial_transcript.txt``",
    "- ``port_poll_after_shutdown.txt``",
    "- ``shutdown_summary.json``"
)
Set-Content -LiteralPath $markdownPath -Value $markdown -Encoding UTF8

if ($pass) {
    Write-Host "PASS: shutdown command, COM disappearance, and cold-boot recovery were observed."
    exit 0
}

Write-Error "FAIL: PWR_HOLD hardware shutdown evidence incomplete. See $summaryPath"
