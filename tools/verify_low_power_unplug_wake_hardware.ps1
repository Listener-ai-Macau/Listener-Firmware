[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$BatteryIdleSeconds = 75,
    [int]$DisappearTimeoutSeconds = 30,
    [int]$ReappearTimeoutSeconds = 90,
    [string]$OutputDir = "",
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Windows.Forms

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot "docs/validation/adhoc-low-power-unplug-wake-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$transcriptPath = Join-Path $OutputDir "unplug_wake_transcript.txt"
$summaryPath = Join-Path $OutputDir "unplug_wake_summary.json"

$AgentName = "codex"
$transcript = [System.Collections.Generic.List[string]]::new()
$errors = [System.Collections.Generic.List[string]]::new()
Set-Content -LiteralPath $transcriptPath -Value @() -Encoding utf8

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    $transcript.Add($line) | Out-Null
    Add-Content -LiteralPath $transcriptPath -Value $line -Encoding utf8
}

function Add-RawTranscript {
    param([Parameter(Mandatory = $true)][string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return
    }
    foreach ($line in ($Text -split "`r?`n")) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            Add-Transcript $line.TrimEnd()
        }
    }
}

function Add-ErrorLine {
    param([Parameter(Mandatory = $true)][string]$Message)
    Add-Transcript "ERROR: $Message"
    $errors.Add($Message) | Out-Null
}

function Show-OperatorPrompt {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [System.Windows.Forms.MessageBoxIcon]$Icon = [System.Windows.Forms.MessageBoxIcon]::Information
    )

    $displayTitle = "[{0}] {1}" -f $AgentName, $Title
    Add-Transcript "operator_prompt title=$displayTitle message=$Message"
    if ($NoPrompt.IsPresent) {
        return
    }

    [void][System.Windows.Forms.MessageBox]::Show(
        $Message,
        $displayTitle,
        [System.Windows.Forms.MessageBoxButtons]::OK,
        $Icon,
        [System.Windows.Forms.MessageBoxDefaultButton]::Button1,
        [System.Windows.Forms.MessageBoxOptions]::ServiceNotification)
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

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port
    }
    $ports = @(Get-SerialPorts)
    if ($ports.Count -ne 1) {
        throw "Expected exactly one serial port when -Port is omitted; found $(Format-Ports $ports)."
    }
    return $ports[0]
}

function Read-SerialFor {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][int]$Milliseconds
    )

    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $text = $Serial.ReadExisting()
            if (-not [string]::IsNullOrEmpty($text)) {
                Add-RawTranscript $text
            }
        } catch {
            Add-Transcript ("serial_read_error message={0}" -f $_.Exception.Message)
            break
        }
        Start-Sleep -Milliseconds 100
    }
}

function Invoke-SerialSession {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][string[]]$Commands,
        [int]$InitialReadMs = 800,
        [int]$CommandReadMs = 1500
    )

    $serial = [System.IO.Ports.SerialPort]::new(
        $SerialPortName,
        $Baud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false

    try {
        $serial.Open()
        Add-Transcript ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $SerialPortName, $Baud)
        Read-SerialFor -Serial $serial -Milliseconds $InitialReadMs
        foreach ($cmd in $Commands) {
            Add-Transcript ("> {0}" -f $cmd)
            $serial.Write(("{0}`n" -f $cmd))
            Read-SerialFor -Serial $serial -Milliseconds $CommandReadMs
        }
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        Add-Transcript "serial_closed"
    }
}

function Wait-PortAbsent {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        Add-Transcript ("port_poll expect=absent target={0} ports={1}" -f $SerialPortName, (Format-Ports $ports))
        if ($ports -notcontains $SerialPortName) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-PortPresent {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        Add-Transcript ("port_poll expect=present target={0} ports={1}" -f $SerialPortName, (Format-Ports $ports))
        if ($ports -contains $SerialPortName) {
            return $SerialPortName
        }
        if ($ports.Count -eq 1) {
            return $ports[0]
        }
        Start-Sleep -Milliseconds 500
    }
    return ""
}

$testPort = Resolve-TestPort
Add-Transcript ("test_start port={0} battery_idle_seconds={1} output_dir={2}" -f $testPort, $BatteryIdleSeconds, $OutputDir)

$preCommands = @(
    "~DEVICE:SET plugged_low_power_enabled=0",
    "~DEVICE:SETTINGS",
    "~POWER:STATUS"
)
Invoke-SerialSession -SerialPortName $testPort -Commands $preCommands -InitialReadMs 500 -CommandReadMs 1600

Show-OperatorPrompt `
    -Title "Listener 低功耗验证：拔掉 USB-C" `
    -Message "现在请拔掉 Listener 的 USB-C 线。拔掉后等待 Windows 里的 $testPort 消失，再点击确定。接下来 $BatteryIdleSeconds 秒内不要按按键，也不要重新插线；如果你在看功耗，记录电流是否进入低功耗波动区间。"

$portDisappeared = Wait-PortAbsent -SerialPortName $testPort -TimeoutSeconds $DisappearTimeoutSeconds
if (-not $portDisappeared) {
    Add-ErrorLine ("serial port did not disappear after unplug request: port={0} timeout_s={1}" -f $testPort, $DisappearTimeoutSeconds)
} else {
    Add-Transcript ("port_disappeared port={0}" -f $testPort)
}

if ($portDisappeared) {
    Show-OperatorPrompt `
        -Title "Listener 低功耗验证：等待离电 idle" `
        -Message "已检测到 $testPort 消失。现在脚本会等待 $BatteryIdleSeconds 秒，让设备在电池供电下进入 idle/light-sleep 策略。等待期间请不要按设备按键，也不要插回 USB。点击确定开始等待。"
    Start-Sleep -Seconds $BatteryIdleSeconds
    Add-Transcript ("battery_idle_wait_complete seconds={0}" -f $BatteryIdleSeconds)

    Show-OperatorPrompt `
        -Title "Listener 低功耗验证：插回 USB-C" `
        -Message "现在请插回 Listener 的 USB-C 线。插好后点击确定；脚本会等待 $testPort 恢复，并抓取串口状态。预期是设备快速恢复串口，状态回到 ACTIVE。"
}

$postPort = ""
if ($portDisappeared) {
    $postPort = Wait-PortPresent -SerialPortName $testPort -TimeoutSeconds $ReappearTimeoutSeconds
    if ([string]::IsNullOrWhiteSpace($postPort)) {
        Add-ErrorLine ("serial port did not reappear after replug request: target={0} timeout_s={1}" -f $testPort, $ReappearTimeoutSeconds)
    } else {
        Add-Transcript ("port_reappeared target={0} actual={1}" -f $testPort, $postPort)
        Start-Sleep -Milliseconds 1000
        $postCommands = @(
            "~DEVICE:SETTINGS",
            "~POWER:STATUS",
            "~POWER:PM",
            "~BOARD:STATUS"
        )
        Invoke-SerialSession -SerialPortName $postPort -Commands $postCommands -InitialReadMs 2500 -CommandReadMs 1700
    }
}

$transcriptText = ($transcript -join "`n")
$postActive = $transcriptText -match "~POWER:STATUS .*state=ACTIVE"
$postExternal = $transcriptText -match "~POWER:STATUS .*external_power_present=1"
$postWakeConfigured = $transcriptText -match "~POWER:STATUS .*power_input_wake_configured=1"
$postResetBannerSeen = $transcriptText -match "rst:0x"
$postUsbSerialJtagResetAfterOpen = [regex]::IsMatch(
    $transcriptText,
    "serial_opened port=.*?[\s\S]*?rst:0x15 \(USB_UART_CHIP_RESET\)")
$postRuntimeLogBeforeReset = [regex]::IsMatch(
    $transcriptText,
    "serial_opened port=.*?[\s\S]*?I \(\d{5,}\)[\s\S]*?rst:0x15 \(USB_UART_CHIP_RESET\)")
$postUnexpectedReset = $postResetBannerSeen -and -not ($postUsbSerialJtagResetAfterOpen -and $postRuntimeLogBeforeReset)
$postNoReset = -not $postUnexpectedReset

if ($portDisappeared -and -not [string]::IsNullOrWhiteSpace($postPort)) {
    if (-not $postActive) {
        Add-ErrorLine "post-replug POWER:STATUS did not show state=ACTIVE"
    }
    if (-not $postExternal) {
        Add-ErrorLine "post-replug POWER:STATUS did not show external_power_present=1"
    }
    if (-not $postWakeConfigured) {
        Add-ErrorLine "post-replug POWER:STATUS did not show power_input_wake_configured=1"
    }
    if ($postUnexpectedReset) {
        Add-ErrorLine "serial transcript contains an unexpected reset banner after replug"
    } elseif ($postResetBannerSeen) {
        Add-Transcript "warning: post-replug serial diagnostics observed USB Serial/JTAG reset after COM open; wake criteria use COM recovery and POWER:STATUS instead of failing this hardware gate"
    }
}

$summary = [ordered]@{
    schema = "listener.low_power_unplug_wake.v1"
    generated_at = (Get-Date).ToString("o")
    port = $testPort
    post_port = $postPort
    battery_idle_seconds = $BatteryIdleSeconds
    port_disappeared = $portDisappeared
    port_reappeared = -not [string]::IsNullOrWhiteSpace($postPort)
    post_active = $postActive
    post_external_power = $postExternal
    post_power_input_wake_configured = $postWakeConfigured
    post_no_reset_banner = $postNoReset
    post_reset_banner_seen = $postResetBannerSeen
    post_usb_serial_jtag_reset_after_open = $postUsbSerialJtagResetAfterOpen
    post_runtime_log_before_reset = $postRuntimeLogBeforeReset
    errors = @($errors)
    transcript = $transcriptPath
}

($transcript -join "`n") | Set-Content -LiteralPath $transcriptPath -Encoding utf8
($summary | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $summaryPath -Encoding utf8
Add-Transcript ("transcript={0}" -f $transcriptPath)
Add-Transcript ("summary={0}" -f $summaryPath)

if ($errors.Count -gt 0) {
    Write-Host ("FAIL: low-power unplug/wake hardware validation errors={0}" -f $errors.Count)
    exit 1
}

Write-Host "PASS: low-power unplug/wake hardware validation completed."
