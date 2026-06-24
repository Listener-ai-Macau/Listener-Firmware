[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Baud = 115200,
    [string]$OutputDir = "",
    [int]$WaitSeconds = 90,
    [int]$ShutdownMinutes = 1,
    [int]$RestoreShutdownMinutes = 30,
    [switch]$PreserveShutdownSetting,
    [int]$ReappearTimeoutSeconds = 90,
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ($ShutdownMinutes -lt 1 -or $ShutdownMinutes -gt 1440) {
    throw "-ShutdownMinutes must be in the range 1..1440."
}
if ($RestoreShutdownMinutes -lt 1 -or $RestoreShutdownMinutes -gt 1440) {
    throw "-RestoreShutdownMinutes must be in the range 1..1440."
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot "docs/validation/voice-keyboard-firmware-full-function-test-1.7/battery-only-auto-shutdown-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$transcriptPath = Join-Path $OutputDir "battery_only_shutdown_transcript.txt"
$pollPath = Join-Path $OutputDir "battery_only_shutdown_poll.txt"
$summaryPath = Join-Path $OutputDir "battery_only_shutdown_summary.json"
$markdownPath = Join-Path $OutputDir "battery_only_shutdown_summary.md"

$transcript = [System.Collections.Generic.List[string]]::new()
$pollLines = [System.Collections.Generic.List[string]]::new()

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    $transcript.Add($line) | Out-Null
}

function Add-PollLine {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    $pollLines.Add($line) | Out-Null
}

function Add-RawTranscript {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return
    }
    foreach ($line in ($Text -split "`r?`n")) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            Write-Host $line
            $transcript.Add($line) | Out-Null
        }
    }
}

function Show-InfoPrompt {
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

function Show-YesNoPrompt {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Add-Transcript "operator_prompt_yes_no title=$Title message=$Message"
    if ($NoPrompt.IsPresent) {
        return $false
    }
    Add-Type -AssemblyName System.Windows.Forms
    $result = [System.Windows.Forms.MessageBox]::Show(
        $Message,
        $Title,
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Question)
    return $result -eq [System.Windows.Forms.DialogResult]::Yes
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

function Invoke-SerialCommands {
    param(
        [Parameter(Mandatory = $true)][string[]]$Commands,
        [int]$ReadMilliseconds = 1200
    )
    $serial = [System.IO.Ports.SerialPort]::new($Port, $Baud)
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    try {
        $serial.Open()
        Add-Transcript ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
        Start-Sleep -Milliseconds 300
        Add-RawTranscript $serial.ReadExisting()
        foreach ($cmd in $Commands) {
            Add-Transcript ("> {0}" -f $cmd)
            $serial.Write(("{0}`n" -f $cmd))
            $deadline = (Get-Date).AddMilliseconds($ReadMilliseconds)
            while ((Get-Date) -lt $deadline) {
                Add-RawTranscript $serial.ReadExisting()
                Start-Sleep -Milliseconds 100
            }
        }
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        Add-Transcript "serial_closed"
    }
}

Show-InfoPrompt `
    -Title "准备 battery-only 自动关机验证" `
    -Message "请确认新板电池已接好，USB-C 当前连接电脑，且没有其它串口工具占用 $Port。脚本会把电池自动关机设置为 $ShutdownMinutes 分钟，并清空本轮 diag_log。"

$initialPorts = @(Get-SerialPorts)
Add-PollLine ("initial target={0} ports={1}" -f $Port, (Format-Ports -Ports $initialPorts))

Invoke-SerialCommands -Commands @(
    "~DIAGLOG:CLEAR",
    "~DEVICE:SET auto_shutdown_minutes=$ShutdownMinutes plugged_low_power_enabled=1",
    "~DEVICE:SETTINGS",
    "~POWER:STATUS",
    "~BOARD:STATUS"
) -ReadMilliseconds 1500

Show-InfoPrompt `
    -Title "请拔掉 USB-C，保留电池供电" `
    -Message "现在请拔掉连接电脑的 USB-C，只保留电池。拔掉后点确定，脚本会等待 $WaitSeconds 秒，让固件在 battery-only 状态触发 1 分钟自动硬件关机。等待期间不要按任何按键。"

$disappear = Wait-PortPresence -TargetPort $Port -ShouldBePresent $false -TimeoutSeconds 20 -Label "after_unplug"
for ($i = 0; $i -lt $WaitSeconds; $i += 5) {
    $remaining = [Math]::Max(0, $WaitSeconds - $i)
    Add-PollLine ("battery_only_wait remaining_s={0}" -f $remaining)
    Start-Sleep -Seconds ([Math]::Min(5, $remaining))
}

$humanObservedOff = Show-YesNoPrompt `
    -Title "确认是否真实关机" `
    -Message "请观察板子：PWR/BLE/状态灯是否已经熄灭，且设备看起来已经断电？如果是，请点 是；如果仍亮着或仍在运行，请点 否。"

Show-InfoPrompt `
    -Title "请恢复连接并冷启动" `
    -Message "如果刚才已经断电，请短按一次硬件电源/EC11 键让板子冷启动，然后重新插回 USB-C。插回后点确定，脚本会读取状态和 flash diag。"

$reappear = Wait-PortPresence -TargetPort $Port -ShouldBePresent $true -TimeoutSeconds $ReappearTimeoutSeconds -Label "after_replug"

$postReadOk = $false
if ($reappear.observed) {
    try {
        $postCommands = @(
            "~POWER:STATUS",
            "~BOARD:STATUS",
            "~DIAGLOG:LAST:160:power"
        )
        if ($PreserveShutdownSetting.IsPresent) {
            $postCommands += "~DEVICE:SET auto_shutdown_minutes=$ShutdownMinutes"
        } else {
            $postCommands += "~DEVICE:SET auto_shutdown_minutes=$RestoreShutdownMinutes"
        }
        $postCommands += "~DEVICE:SETTINGS"
        Invoke-SerialCommands -Commands $postCommands -ReadMilliseconds 1800
        $postReadOk = $true
    } catch {
        Add-Transcript ("post_read_error {0}" -f $_.Exception.Message)
    }
}

$text = ($transcript -join "`n")
$sawNewPinMap = $text -match "pwr_hold_gpio=9" -and $text -match "battery_gpio=10" -and $text -match "current_telemetry_not_populated"
$sawShutdownEntry = $text -match "DIAG_POWER_SLEEP_ENTRY|last_shutdown_reason=(idle_timeout|critical_battery|manual_command)|hardware shutdown"
$result = if ($disappear.observed -and $reappear.observed -and $postReadOk -and $sawNewPinMap -and $humanObservedOff) { "PASS" } else { "FAIL" }

$summary = [ordered]@{
    schema_version = 1
    result = $result
    port = $Port
    output_dir = $OutputDir
    transcript = $transcriptPath
    port_poll = $pollPath
    wait_seconds = $WaitSeconds
    shutdown_minutes = $ShutdownMinutes
    preserve_shutdown_setting = [bool]$PreserveShutdownSetting
    restore_shutdown_minutes = if ($PreserveShutdownSetting.IsPresent) { $null } else { $RestoreShutdownMinutes }
    initial_ports = @($initialPorts)
    port_disappeared_after_unplug = [bool]$disappear.observed
    human_observed_power_off = [bool]$humanObservedOff
    port_reappeared_after_replug = [bool]$reappear.observed
    post_read_ok = [bool]$postReadOk
    saw_new_pin_map = [bool]$sawNewPinMap
    saw_shutdown_entry = [bool]$sawShutdownEntry
}

$transcript | Set-Content -LiteralPath $transcriptPath -Encoding utf8
$pollLines | Set-Content -LiteralPath $pollPath -Encoding utf8
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8
@(
    "# Battery-only Auto Shutdown Manual Evidence",
    "",
    "- Result: $result",
    "- Port: ``$Port``",
    "- Wait seconds: $WaitSeconds",
    "- Shutdown minutes: $ShutdownMinutes",
    "- Preserve shutdown setting: $([bool]$PreserveShutdownSetting)",
    "- Port disappeared after unplug: $($disappear.observed)",
    "- Human observed power off: $humanObservedOff",
    "- Port reappeared after replug: $($reappear.observed)",
    "- Post-read OK: $postReadOk",
    "- Saw new pin map: $sawNewPinMap",
    "- Saw shutdown entry: $sawShutdownEntry",
    "- Transcript: ``$transcriptPath``",
    "- Poll log: ``$pollPath``"
) | Set-Content -LiteralPath $markdownPath -Encoding utf8

if ($result -eq "PASS") {
    Write-Host "PASS: battery-only automatic shutdown manual evidence captured."
    exit 0
}

Write-Error "FAIL: battery-only automatic shutdown evidence incomplete. See $summaryPath"
exit 1
