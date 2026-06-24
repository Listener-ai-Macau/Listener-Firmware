[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$BatteryIdleSeconds = 75,
    [int]$PortDisappearTimeoutSeconds = 30,
    [int]$PortReappearTimeoutSeconds = 120,
    [string]$OutputDir = "",
    [switch]$SkipLowPower,
    [switch]$SkipStaticKeyPixel,
    [switch]$SkipPhysicalKeyFeedback,
    [string[]]$KeyFilter = @(),
    [string[]]$GestureFilter = @(),
    [switch]$SkipReadyPrompts,
    [switch]$NoAiwLock,
    [switch]$NoPrompt,
    [switch]$PlanOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$AgentName = "codex"
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$aiwPath = "C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\scripts\aiw.ps1"
$serialCaptureScript = Join-Path $PSScriptRoot "send_serial_and_capture.ps1"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".cache\validation\guided-low-power-button-led-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$sessionPath = Join-Path $OutputDir "guided-session.jsonl"
$summaryJsonPath = Join-Path $OutputDir "guided-summary.json"
$summaryMdPath = Join-Path $OutputDir "guided-summary.md"
$transcriptPath = Join-Path $OutputDir "guided-transcript.txt"
Set-Content -LiteralPath $sessionPath -Value @() -Encoding UTF8
Set-Content -LiteralPath $transcriptPath -Value @() -Encoding UTF8

$script:SerialCaptureIndex = 0
$records = [System.Collections.Generic.List[object]]::new()
$errors = [System.Collections.Generic.List[string]]::new()

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)

    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $transcriptPath -Value $line -Encoding UTF8
}

function Add-Record {
    param([Parameter(Mandatory = $true)][object]$Record)

    $records.Add($Record) | Out-Null
    ($Record | ConvertTo-Json -Depth 12 -Compress) | Add-Content -LiteralPath $sessionPath -Encoding UTF8
}

function Split-FilterValues {
    param([string[]]$Values)

    @($Values | ForEach-Object {
        if ($_ -ne $null) {
            $_ -split ","
        }
    } | ForEach-Object {
        $_.Trim()
    } | Where-Object {
        $_
    })
}

function Add-ErrorRecord {
    param([Parameter(Mandatory = $true)][string]$Message)

    $errors.Add($Message) | Out-Null
    Add-Transcript "ERROR: $Message"
}

function Play-PromptSound {
    try {
        [System.Media.SystemSounds]::Exclamation.Play()
        Start-Sleep -Milliseconds 180
        [System.Media.SystemSounds]::Asterisk.Play()
    } catch {
        try {
            [Console]::Beep(880, 140)
            [Console]::Beep(1175, 180)
        } catch {
            # Sound is best-effort; prompts must still work on muted/headless systems.
        }
    }
}

function Show-TopMostMessageBox {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [System.Windows.Forms.MessageBoxButtons]$Buttons = [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]$Icon = [System.Windows.Forms.MessageBoxIcon]::Information
    )

    Add-Transcript ("operator_prompt title=[{0}] {1} message={2}" -f $AgentName, $Title, ($Message -replace "`r?`n", " / "))
    if ($NoPrompt.IsPresent) {
        return [System.Windows.Forms.DialogResult]::OK
    }

    Play-PromptSound
    $owner = [System.Windows.Forms.Form]::new()
    try {
        $owner.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
        $owner.TopMost = $true
        $owner.ShowInTaskbar = $false
        $owner.WindowState = [System.Windows.Forms.FormWindowState]::Minimized
        $owner.Show()
        $owner.Hide()
        return [System.Windows.Forms.MessageBox]::Show($owner, $Message, "[$AgentName] $Title", $Buttons, $Icon)
    } finally {
        $owner.Dispose()
    }
}

function Show-ObservationForm {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Instruction,
        [Parameter(Mandatory = $true)][string]$Expected,
        [string]$ExtraLabel = "",
        [string]$ExtraDefault = ""
    )

    Add-Transcript ("observation_prompt title={0}" -f $Title)
    if ($NoPrompt.IsPresent) {
        return [PSCustomObject]@{
            result = "SKIP"
            observation = "NoPrompt mode"
            extra = ""
            notes = ""
        }
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] $Title"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 820
    $form.Height = 640
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 9)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $y = 12
    $titleLabel = [System.Windows.Forms.Label]::new()
    $titleLabel.Left = 12
    $titleLabel.Top = $y
    $titleLabel.Width = 770
    $titleLabel.Height = 28
    $titleLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
    $titleLabel.Text = $Title
    $form.Controls.Add($titleLabel)
    $y += 36

    $instructionBox = [System.Windows.Forms.TextBox]::new()
    $instructionBox.Left = 12
    $instructionBox.Top = $y
    $instructionBox.Width = 780
    $instructionBox.Height = 135
    $instructionBox.Multiline = $true
    $instructionBox.ReadOnly = $true
    $instructionBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $instructionBox.Text = $Instruction
    $form.Controls.Add($instructionBox)
    $y += 145

    $expectedBox = [System.Windows.Forms.TextBox]::new()
    $expectedBox.Left = 12
    $expectedBox.Top = $y
    $expectedBox.Width = 780
    $expectedBox.Height = 90
    $expectedBox.Multiline = $true
    $expectedBox.ReadOnly = $true
    $expectedBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $expectedBox.Text = $Expected
    $form.Controls.Add($expectedBox)
    $y += 102

    $extraText = $null
    if (-not [string]::IsNullOrWhiteSpace($ExtraLabel)) {
        $extraLabelControl = [System.Windows.Forms.Label]::new()
        $extraLabelControl.Left = 12
        $extraLabelControl.Top = $y + 4
        $extraLabelControl.Width = 210
        $extraLabelControl.Height = 24
        $extraLabelControl.Text = $ExtraLabel
        $form.Controls.Add($extraLabelControl)

        $extraText = [System.Windows.Forms.TextBox]::new()
        $extraText.Left = 225
        $extraText.Top = $y
        $extraText.Width = 567
        $extraText.Text = $ExtraDefault
        $form.Controls.Add($extraText)
        $y += 38
    }

    $observationLabel = [System.Windows.Forms.Label]::new()
    $observationLabel.Left = 12
    $observationLabel.Top = $y
    $observationLabel.Width = 760
    $observationLabel.Height = 22
    $observationLabel.Text = "你看到的现象："
    $form.Controls.Add($observationLabel)
    $y += 24

    $observationBox = [System.Windows.Forms.TextBox]::new()
    $observationBox.Left = 12
    $observationBox.Top = $y
    $observationBox.Width = 780
    $observationBox.Height = 74
    $observationBox.Multiline = $true
    $observationBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $form.Controls.Add($observationBox)
    $y += 86

    $notesLabel = [System.Windows.Forms.Label]::new()
    $notesLabel.Left = 12
    $notesLabel.Top = $y
    $notesLabel.Width = 760
    $notesLabel.Height = 22
    $notesLabel.Text = "备注（异常时写清楚哪颗灯、什么颜色、是否闪烁/串灯/过亮）："
    $form.Controls.Add($notesLabel)
    $y += 24

    $notesBox = [System.Windows.Forms.TextBox]::new()
    $notesBox.Left = 12
    $notesBox.Top = $y
    $notesBox.Width = 780
    $notesBox.Height = 56
    $notesBox.Multiline = $true
    $notesBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $form.Controls.Add($notesBox)
    $y += 72

    $holder = @{ result = "ABORT" }
    $passButton = [System.Windows.Forms.Button]::new()
    $passButton.Text = "通过"
    $passButton.Left = 450
    $passButton.Top = $y
    $passButton.Width = 100
    $passButton.Add_Click({ $holder.result = "PASS"; $form.Close() })
    $form.Controls.Add($passButton)

    $failButton = [System.Windows.Forms.Button]::new()
    $failButton.Text = "失败"
    $failButton.Left = 560
    $failButton.Top = $y
    $failButton.Width = 100
    $failButton.Add_Click({ $holder.result = "FAIL"; $form.Close() })
    $form.Controls.Add($failButton)

    $skipButton = [System.Windows.Forms.Button]::new()
    $skipButton.Text = "跳过/不确定"
    $skipButton.Left = 670
    $skipButton.Top = $y
    $skipButton.Width = 122
    $skipButton.Add_Click({ $holder.result = "SKIP"; $form.Close() })
    $form.Controls.Add($skipButton)

    [void]$form.ShowDialog()
    $extraValue = if ($null -ne $extraText) { $extraText.Text } else { "" }
    $result = [PSCustomObject]@{
        result = [string]$holder.result
        observation = [string]$observationBox.Text
        extra = [string]$extraValue
        notes = [string]$notesBox.Text
    }
    $form.Dispose()
    return $result
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
    if (@($Ports).Count -eq 0) {
        return "<none>"
    }
    return ($Ports -join ",")
}

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port.ToUpperInvariant()
    }

    $ports = @(Get-SerialPorts)
    if ($ports.Count -eq 1) {
        return $ports[0].ToUpperInvariant()
    }
    if ($NoPrompt.IsPresent) {
        throw "Expected exactly one serial port when -Port is omitted; found $(Format-Ports $ports)."
    }

    $default = if ($ports.Count -gt 0) { $ports[0] } else { "COM10" }
    $input = [Microsoft.VisualBasic.Interaction]::InputBox(
        "当前串口列表：$(Format-Ports $ports)`r`n请输入 Listener 当前串口，例如 COM10。",
        "[$AgentName] 选择串口",
        $default)
    if ([string]::IsNullOrWhiteSpace($input)) {
        throw "Serial port selection was cancelled."
    }
    return $input.Trim().ToUpperInvariant()
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
            return $ports[0].ToUpperInvariant()
        }
        Start-Sleep -Milliseconds 500
    }
    return ""
}

function Invoke-LoggedNative {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Add-Transcript ("native_start label={0} command=pwsh {1}" -f $Label, ($Arguments -join " "))
    $output = @(& pwsh @Arguments 2>&1)
    $exit = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } elseif ($?) { 0 } else { 1 }
    foreach ($line in @($output)) {
        Add-Transcript ("native_output label={0} {1}" -f $Label, [string]$line)
    }
    Add-Transcript ("native_exit label={0} exit={1}" -f $Label, $exit)
    if ($exit -ne 0) {
        throw "Command failed for $Label with exit code $exit."
    }
}

function Invoke-SerialCapture {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$Commands,
        [int]$InitialReadMs = 500,
        [int]$CommandReadMs = 1400
    )

    $script:SerialCaptureIndex++
    $safeLabel = ($Label -replace '[^A-Za-z0-9_.-]', '-')
    $capturePath = Join-Path $OutputDir ("serial-{0:00}-{1}.txt" -f $script:SerialCaptureIndex, $safeLabel)
    $commandList = ($Commands -join ";;")
    $baseArgs = @(
        "-NoProfile",
        "-File",
        $serialCaptureScript,
        "-Port",
        $SerialPortName,
        "-Baud",
        [string]$Baud,
        "-InitialReadMs",
        [string]$InitialReadMs,
        "-CommandReadMs",
        [string]$CommandReadMs,
        "-CommandList",
        $commandList,
        "-OutputPath",
        $capturePath
    )

    if ($NoAiwLock.IsPresent) {
        Invoke-LoggedNative -Arguments $baseArgs -Label $Label
    } else {
        $lockedArgs = @(
            "-NoProfile",
            "-File",
            $aiwPath,
            "with-lock",
            "-Resource",
            $SerialPortName,
            "-Wait",
            "-WaitTimeoutSeconds",
            "120",
            "-TimeoutMinutes",
            "2",
            "-Purpose",
            "oai2 guided low-power/button serial capture",
            "-Run",
            "pwsh"
        ) + $baseArgs
        Invoke-LoggedNative -Arguments $lockedArgs -Label $Label
    }

    return [PSCustomObject]@{
        label = $Label
        path = $capturePath
        commands = @($Commands)
    }
}

function Add-LiveCaptureLine {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][string]$Text
    )

    Write-Host $Text
    $Lines.Add($Text) | Out-Null
}

function Read-LiveSerial {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Lines
    )

    if (-not $Serial.IsOpen) {
        Add-LiveCaptureLine -Lines $Lines -Text "READ_SKIPPED serial_port_closed"
        return
    }

    try {
        $text = $Serial.ReadExisting()
        if (-not [string]::IsNullOrEmpty($text)) {
            foreach ($line in ($text -split "`r?`n")) {
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    Add-LiveCaptureLine -Lines $Lines -Text $line
                }
            }
        }
    } catch {
        Add-LiveCaptureLine -Lines $Lines -Text ("READ_ERROR {0}" -f $_.Exception.Message)
    }
}

function Read-LiveSerialFor {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][int]$Milliseconds
    )

    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    while ((Get-Date) -lt $deadline) {
        Read-LiveSerial -Serial $Serial -Lines $Lines
        Start-Sleep -Milliseconds 100
    }
}

function Send-LiveSerialCommand {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMs = 700
    )

    Add-LiveCaptureLine -Lines $Lines -Text ("> {0}" -f $Command)
    if (-not $Serial.IsOpen) {
        Add-LiveCaptureLine -Lines $Lines -Text "WRITE_SKIPPED serial_port_closed"
        return
    }
    $Serial.Write(("{0}`n" -f $Command))
    Read-LiveSerialFor -Serial $Serial -Lines $Lines -Milliseconds $ReadMs
}

function Invoke-AiwSimpleLock {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($NoAiwLock.IsPresent) {
        return
    }
    $args = @(
        "-NoProfile",
        "-File",
        $aiwPath,
        "lock",
        "-Resource",
        $SerialPortName,
        "-Owner",
        $AgentName,
        "-TimeoutMinutes",
        "2"
    )
    Invoke-LoggedNative -Arguments $args -Label ("lock-{0}" -f $Label)
}

function Release-AiwSimpleLock {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($NoAiwLock.IsPresent) {
        return
    }
    $args = @(
        "-NoProfile",
        "-File",
        $aiwPath,
        "unlock",
        "-Resource",
        $SerialPortName,
        "-Owner",
        $AgentName
    )
    try {
        Invoke-LoggedNative -Arguments $args -Label ("unlock-{0}" -f $Label)
    } catch {
        Add-ErrorRecord ("Failed to release serial lock for {0}: {1}" -f $SerialPortName, $_.Exception.Message)
    }
}

function Show-LiveKeyCaptureWindow {
    param(
        [Parameter(Mandatory = $true)][object]$Key,
        [Parameter(Mandatory = $true)][object]$Gesture,
        [Parameter(Mandatory = $true)][int]$Seconds,
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Lines
    )

    if ($NoPrompt.IsPresent) {
        Read-LiveSerialFor -Serial $Serial -Lines $Lines -Milliseconds ($Seconds * 1000)
        return
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] 实时采集 $($Key.logical) $($Gesture.title)"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 760
    $form.Height = 250
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $titleLabel = [System.Windows.Forms.Label]::new()
    $titleLabel.Left = 16
    $titleLabel.Top = 14
    $titleLabel.Width = 710
    $titleLabel.Height = 28
    $titleLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
    $titleLabel.Text = "现在测试 $($Key.logical) $($Gesture.title)，脚本正在实时抓串口"
    $form.Controls.Add($titleLabel)

    $instruction = [System.Windows.Forms.TextBox]::new()
    $instruction.Left = 16
    $instruction.Top = 52
    $instruction.Width = 710
    $instruction.Height = 84
    $instruction.Multiline = $true
    $instruction.ReadOnly = $true
    $instruction.Text = "请在倒计时结束前：$($Gesture.action)`r`n观察：$($Gesture.expected)"
    $form.Controls.Add($instruction)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.Left = 16
    $countdown.Top = 150
    $countdown.Width = 500
    $countdown.Height = 28
    $countdown.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($countdown)

    $closeButton = [System.Windows.Forms.Button]::new()
    $closeButton.Text = "提前结束"
    $closeButton.Left = 596
    $closeButton.Top = 146
    $closeButton.Width = 130
    $closeButton.Add_Click({ $form.Close() })
    $form.Controls.Add($closeButton)

    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Tag = [PSCustomObject]@{
        Deadline = (Get-Date).AddSeconds($Seconds)
        Countdown = $countdown
        Form = $form
        Serial = $Serial
        Lines = $Lines
    }
    $timer.Interval = 100
    $timer.Add_Tick({
        param($sender, $eventArgs)
        $state = $sender.Tag
        Read-LiveSerial -Serial $state.Serial -Lines $state.Lines
        $remaining = [Math]::Max(0, [int][Math]::Ceiling(($state.Deadline - (Get-Date)).TotalSeconds))
        $state.Countdown.Text = "剩余 $remaining 秒"
        if ((Get-Date) -ge $state.Deadline) {
            $sender.Stop()
            $state.Form.Close()
        }
    })

    try {
        $timer.Start()
        [void]$form.ShowDialog()
    } finally {
        $timer.Stop()
        $timer.Dispose()
        $form.Dispose()
        Read-LiveSerial -Serial $Serial -Lines $Lines
    }
}

function Invoke-LiveKeyPressCapture {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][object]$Key,
        [Parameter(Mandatory = $true)][object]$Gesture,
        [int]$Seconds = 8
    )

    $script:SerialCaptureIndex++
    $label = "live-press-{0}-{1}" -f $Key.logical.ToLowerInvariant(), $Gesture.id
    $capturePath = Join-Path $OutputDir ("serial-{0:00}-{1}.txt" -f $script:SerialCaptureIndex, $label)
    $lines = [System.Collections.Generic.List[string]]::new()
    Add-Transcript ("live_key_capture_start key={0} gesture={1} port={2} seconds={3} path={4}" -f $Key.logical, $Gesture.id, $SerialPortName, $Seconds, $capturePath)

    if (-not $SkipReadyPrompts.IsPresent) {
        [void](Show-TopMostMessageBox `
            -Title ("准备实时采集 {0} {1}" -f $Key.logical, $Gesture.title) `
            -Message ("点确定后会进入 {0} 秒实时采集窗口。窗口出现后请只操作 {1}：{2}。不要按 EC11 或其它按键。采集结束后会让你判断灯效是否通过。" -f $Seconds, $Key.logical, $Gesture.action) `
            -Buttons ([System.Windows.Forms.MessageBoxButtons]::OK) `
            -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
        )
    }

    $serial = [System.IO.Ports.SerialPort]::new($SerialPortName, $Baud)
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false

    Invoke-AiwSimpleLock -SerialPortName $SerialPortName -Label $label
    try {
        $serial.Open()
        Add-LiveCaptureLine -Lines $lines -Text ("serial_opened port={0} baud={1} dtr=0 rts=0 live_key={2}" -f $SerialPortName, $Baud, $Key.logical)
        Read-LiveSerialFor -Serial $serial -Lines $lines -Milliseconds 500
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:INPUTDBG:ON" -ReadMs 500
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~LED:WAKE" -ReadMs 500
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~LED:STATUS" -ReadMs 700
        Show-LiveKeyCaptureWindow -Key $Key -Gesture $Gesture -Seconds $Seconds -Serial $serial -Lines $lines
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 500
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~BOARD:GPIO" -ReadMs 900
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~LED:STATUS" -ReadMs 900
        Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:LAST:140:keyboard" -ReadMs 1000
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        Add-LiveCaptureLine -Lines $lines -Text "serial_closed"
        Release-AiwSimpleLock -SerialPortName $SerialPortName -Label $label
        $lines | Set-Content -LiteralPath $capturePath -Encoding UTF8
        Add-Transcript ("live_key_capture_done key={0} gesture={1} path={2}" -f $Key.logical, $Gesture.id, $capturePath)
    }

    return [PSCustomObject]@{
        label = $label
        path = $capturePath
        commands = @("~DIAGLOG:INPUTDBG:ON", "~LED:WAKE", "~LED:STATUS", "<live $($Gesture.id) $($Key.logical)>", "~DIAGLOG:INPUTDBG:OFF", "~BOARD:GPIO", "~LED:STATUS", "~DIAGLOG:LAST:140:keyboard")
    }
}

function New-StepRecord {
    param(
        [Parameter(Mandatory = $true)][string]$Phase,
        [Parameter(Mandatory = $true)][string]$StepId,
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Result,
        [object]$Human = $null,
        [object[]]$Artifacts = @(),
        [hashtable]$Data = @{}
    )

    return [PSCustomObject]@{
        at = (Get-Date).ToString("o")
        phase = $Phase
        step_id = $StepId
        title = $Title
        result = $Result
        human = $Human
        artifacts = @($Artifacts)
        data = $Data
    }
}

function Get-KeyLedSteps {
    $steps = @(
        [PSCustomObject]@{ logical = "KEY1"; led = "LED11"; gpio = "GPIO38"; usage = "F13" },
        [PSCustomObject]@{ logical = "KEY2"; led = "LED12"; gpio = "GPIO39"; usage = "F14" },
        [PSCustomObject]@{ logical = "KEY3"; led = "LED13"; gpio = "GPIO40"; usage = "F15" },
        [PSCustomObject]@{ logical = "KEY4"; led = "LED14"; gpio = "GPIO41"; usage = "F16" }
    )
    if ($KeyFilter.Count -gt 0) {
        $wanted = @(Split-FilterValues -Values $KeyFilter | ForEach-Object { $_.ToUpperInvariant() })
        $steps = @($steps | Where-Object { $wanted -contains $_.logical.ToUpperInvariant() })
    }
    return $steps
}

function Get-KeyGestureSteps {
    $steps = @(
        [PSCustomObject]@{
            id = "single"
            title = "短按"
            seconds = 8
            action = "短按一次目标键，按下和松开都要干脆；不要连按。"
            expected = "对应 KEY 灯按下时低亮白色，识别单击后紫色闪一下；其它 KEY 和 LED3-LED6 不应乱亮。"
        },
        [PSCustomObject]@{
            id = "double"
            title = "双击"
            seconds = 8
            action = "连续短按目标键两次，第二次要在第一次松开后约 250ms 内完成。"
            expected = "对应 KEY 灯先随两次按下给低亮白色反馈，识别双击后紫色闪两下；其它 KEY 和 LED3-LED6 不应乱亮。"
        },
        [PSCustomObject]@{
            id = "long"
            title = "长按"
            seconds = 9
            action = "按住目标键约 1.2 秒，看到长按确认后再松开。"
            expected = "对应 KEY 灯按住时低亮白色，识别长按后出现紫色长亮确认；其它 KEY 和 LED3-LED6 不应乱亮。"
        }
    )
    if ($GestureFilter.Count -gt 0) {
        $wanted = @(Split-FilterValues -Values $GestureFilter | ForEach-Object { $_.ToLowerInvariant() })
        $steps = @($steps | Where-Object { $wanted -contains $_.id.ToLowerInvariant() })
    }
    return $steps
}

function Write-Outputs {
    param([Parameter(Mandatory = $true)][string]$Result)

    $passCount = @($records | Where-Object { $_.result -eq "PASS" }).Count
    $failCount = @($records | Where-Object { $_.result -eq "FAIL" }).Count
    $skipCount = @($records | Where-Object { $_.result -eq "SKIP" }).Count
    $summary = [ordered]@{
        schema = "listener.guided_low_power_button_led.v1"
        generated_at = (Get-Date).ToString("o")
        agent = $AgentName
        repo_root = $repoRoot
        result = $Result
        port = $script:ResolvedPort
        output_dir = $OutputDir
        session_jsonl = $sessionPath
        transcript = $transcriptPath
        pass_count = $passCount
        fail_count = $failCount
        skip_count = $skipCount
        error_count = $errors.Count
        errors = @($errors)
    }
    ($summary | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Guided low-power and button LED validation") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("- Result: $Result") | Out-Null
    $lines.Add("- Agent: $AgentName") | Out-Null
    $lines.Add("- Port: $script:ResolvedPort") | Out-Null
    $lines.Add("- Pass/Fail/Skip: $passCount / $failCount / $skipCount") | Out-Null
    $lines.Add("- Output dir: $OutputDir") | Out-Null
    $lines.Add("- Session: $sessionPath") | Out-Null
    $lines.Add("- Transcript: $transcriptPath") | Out-Null
    if ($errors.Count -gt 0) {
        $lines.Add("") | Out-Null
        $lines.Add("## Errors") | Out-Null
        foreach ($errorItem in @($errors)) {
            $lines.Add("- $errorItem") | Out-Null
        }
    }
    $lines.Add("") | Out-Null
    $lines.Add("## Steps") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("| # | phase | step | result | observation | extra | notes | artifacts |") | Out-Null
    $lines.Add("|---:|---|---|---|---|---|---|---|") | Out-Null
    for ($i = 0; $i -lt $records.Count; $i++) {
        $record = $records[$i]
        $human = $record.human
        $observation = if ($null -ne $human) { [string]$human.observation } else { "" }
        $extra = if ($null -ne $human) { [string]$human.extra } else { "" }
        $notes = if ($null -ne $human) { [string]$human.notes } else { "" }
        $artifactText = (@($record.artifacts) | ForEach-Object {
            if ($_.PSObject.Properties["path"]) { [string]$_.path } else { [string]$_ }
        }) -join "<br>"
        $row = "| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f @(
            ($i + 1),
            (($record.phase -replace "\|", "/") -replace "`r?`n", " "),
            (($record.title -replace "\|", "/") -replace "`r?`n", " "),
            $record.result,
            (($observation -replace "\|", "/") -replace "`r?`n", " "),
            (($extra -replace "\|", "/") -replace "`r?`n", " "),
            (($notes -replace "\|", "/") -replace "`r?`n", " "),
            (($artifactText -replace "\|", "/") -replace "`r?`n", " "))
        $lines.Add($row) | Out-Null
    }
    $lines | Set-Content -LiteralPath $summaryMdPath -Encoding UTF8

    Write-Host "result=$Result"
    Write-Host "summary=$summaryMdPath"
    Write-Host "session=$sessionPath"
    Write-Host "transcript=$transcriptPath"
}

$script:ResolvedPort = if ($PlanOnly.IsPresent) { if ($Port) { $Port.ToUpperInvariant() } else { "" } } else { Resolve-TestPort }
Add-Transcript ("start agent={0} port={1} output_dir={2} skip_low_power={3} skip_static_key_pixel={4} skip_physical_key_feedback={5} key_filter={6} gesture_filter={7} skip_ready_prompts={8}" -f $AgentName, $script:ResolvedPort, $OutputDir, $SkipLowPower.IsPresent, $SkipStaticKeyPixel.IsPresent, $SkipPhysicalKeyFeedback.IsPresent, (($KeyFilter | ForEach-Object { $_ }) -join ","), (($GestureFilter | ForEach-Object { $_ }) -join ","), $SkipReadyPrompts.IsPresent)

if ($PlanOnly.IsPresent) {
    Add-Record (New-StepRecord -Phase "plan" -StepId "low-power-idle" -Title "拔电 idle 电流和状态灯确认" -Result "SKIP")
    $planKeySteps = @(Get-KeyLedSteps)
    $planGestureSteps = @(Get-KeyGestureSteps)
    foreach ($key in $planKeySteps) {
        Add-Record (New-StepRecord -Phase "plan" -StepId ("pixel-{0}" -f $key.logical.ToLowerInvariant()) -Title ("{0}/{1} 单灯定位" -f $key.logical, $key.led) -Result "SKIP")
        foreach ($gesture in $planGestureSteps) {
            Add-Record (New-StepRecord -Phase "plan" -StepId ("press-{0}-{1}" -f $key.logical.ToLowerInvariant(), $gesture.id) -Title ("实体 {0} {1}灯效" -f $key.logical, $gesture.title) -Result "SKIP")
        }
    }
    Write-Outputs -Result "PLAN_ONLY"
    exit 0
}

try {
    if (-not $SkipLowPower.IsPresent) {
        $preCapture = Invoke-SerialCapture -SerialPortName $script:ResolvedPort -Label "pre-low-power-status" -Commands @(
            "~BOOT:STATUS",
            "~DEVICE:SETTINGS",
            "~POWER:STATUS",
            "~LED:STATUS"
        ) -InitialReadMs 1200 -CommandReadMs 1700

        $intro = Show-TopMostMessageBox `
            -Title "低功耗验证：准备拔电" `
            -Message "这一步验证刚才的低功耗修复。请确认万用表/电流表已经接好。点确定后拔掉 Listener 的 USB-C，脚本会等待 $script:ResolvedPort 从 Windows 消失。拔掉后不要按任何按键。" `
            -Buttons ([System.Windows.Forms.MessageBoxButtons]::OKCancel) `
            -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
        if ($intro -ne [System.Windows.Forms.DialogResult]::OK) {
            Add-Record (New-StepRecord -Phase "low_power" -StepId "operator-cancel-before-unplug" -Title "用户取消拔电低功耗验证" -Result "SKIP" -Artifacts @($preCapture))
        } else {
            $absent = Wait-PortAbsent -SerialPortName $script:ResolvedPort -TimeoutSeconds $PortDisappearTimeoutSeconds
            if (-not $absent) {
                Add-ErrorRecord "$script:ResolvedPort did not disappear after unplug within ${PortDisappearTimeoutSeconds}s."
                Add-Record (New-StepRecord -Phase "low_power" -StepId "port-disappear" -Title "拔电后串口消失" -Result "FAIL" -Artifacts @($preCapture))
            } else {
                Add-Record (New-StepRecord -Phase "low_power" -StepId "port-disappear" -Title "拔电后串口消失" -Result "PASS" -Artifacts @($preCapture))
                [void](Show-TopMostMessageBox `
                    -Title "低功耗验证：等待 idle" `
                    -Message "已经检测到串口消失。现在开始等待 $BatteryIdleSeconds 秒，让设备进入离电 idle/light-sleep。等待期间请不要按 EC11/KEY1-KEY4，也不要插回 USB。等待结束后会弹窗让你填写电流和灯的状态。" `
                    -Buttons ([System.Windows.Forms.MessageBoxButtons]::OK) `
                    -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
                )
                Start-Sleep -Seconds $BatteryIdleSeconds
                Add-Transcript ("battery_idle_wait_complete seconds={0}" -f $BatteryIdleSeconds)

                $idleHuman = Show-ObservationForm `
                    -Title "离电 idle 电流/灯状态" `
                    -Instruction "请现在看万用表/电流表和设备灯。`r`n1. 填当前电流，建议写范围，例如 9-20mA 或稳定 47mA。`r`n2. 看 PWR/BLE 是否符合当前低功耗口径：低功耗允许非常省电，但不能出现 LED3-LED6 全亮/乱闪。`r`n3. 先不要插回 USB。" `
                    -Expected "预期：离电 idle 应明显低于原来的 40-50mA；如果仍稳定 40mA 以上，算失败。状态灯不应全亮/乱闪。" `
                    -ExtraLabel "当前电流读数：" `
                    -ExtraDefault "mA"
                Add-Record (New-StepRecord -Phase "low_power" -StepId "battery-idle-current" -Title "离电 idle 电流/状态灯" -Result $idleHuman.result -Human $idleHuman -Artifacts @($preCapture))

                $wakeHuman = Show-ObservationForm `
                    -Title "离电 idle：EC11 短按唤醒" `
                    -Instruction "请现在短按一次 EC11 圆形旋钮中键，不要旋转，也不要按 KEY1-KEY4。观察电流和灯是否从 idle 快速恢复到 active；如果串口不在是正常的，因为 USB 仍未插回。" `
                    -Expected "预期：短按 EC11 后设备应快速唤醒，不能死机、不能重启循环，状态灯不能 LED3-LED6 全亮/乱闪。唤醒后电流升高到 active 水平是正常现象。" `
                    -ExtraLabel "唤醒后电流读数：" `
                    -ExtraDefault "mA"
                Add-Record (New-StepRecord -Phase "low_power" -StepId "ec11-wake-from-idle" -Title "EC11 从离电 idle 唤醒" -Result $wakeHuman.result -Human $wakeHuman -Artifacts @($preCapture))

                [void](Show-TopMostMessageBox `
                    -Title "低功耗验证：插回 USB-C" `
                    -Message "现在请插回 Listener 的 USB-C。插好后点击确定；脚本会等待 $script:ResolvedPort 恢复，然后抓取 POWER/LED/按键诊断日志。" `
                    -Buttons ([System.Windows.Forms.MessageBoxButtons]::OK) `
                    -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
                )
                $postPort = Wait-PortPresent -SerialPortName $script:ResolvedPort -TimeoutSeconds $PortReappearTimeoutSeconds
                if ([string]::IsNullOrWhiteSpace($postPort)) {
                    Add-ErrorRecord "$script:ResolvedPort did not reappear after replug within ${PortReappearTimeoutSeconds}s."
                    Add-Record (New-StepRecord -Phase "low_power" -StepId "port-reappear" -Title "回插后串口恢复" -Result "FAIL")
                } else {
                    $script:ResolvedPort = $postPort.ToUpperInvariant()
                    $postCapture = Invoke-SerialCapture -SerialPortName $script:ResolvedPort -Label "post-low-power-status-diag" -Commands @(
                        "~BOOT:STATUS",
                        "~POWER:STATUS",
                        "~LED:STATUS",
                        "~BOARD:GPIO",
                        "~DIAGLOG:LAST:120:power",
                        "~DIAGLOG:LAST:120:keyboard",
                        "~DIAGLOG:LAST:120:voice_key"
                    ) -InitialReadMs 2500 -CommandReadMs 2300
                    Add-Record (New-StepRecord -Phase "low_power" -StepId "port-reappear" -Title "回插后串口恢复并抓诊断" -Result "PASS" -Artifacts @($postCapture))
                }
            }
        }
    }

    $keySteps = @(Get-KeyLedSteps)
    if ($keySteps.Count -eq 0) {
        throw "KeyFilter did not match any KEY. Valid values: KEY1, KEY2, KEY3, KEY4."
    }
    if (-not $SkipStaticKeyPixel.IsPresent) {
        foreach ($key in $keySteps) {
            $cmd = "~LED:TEST:PIXEL key $($key.led) white 25"
            $capture = Invoke-SerialCapture -SerialPortName $script:ResolvedPort -Label ("pixel-{0}" -f $key.logical.ToLowerInvariant()) -Commands @(
                "~LED:PREVIEW clear",
                "~LED:OFF",
                $cmd,
                "~LED:STATUS"
            ) -InitialReadMs 400 -CommandReadMs 1200
            $human = Show-ObservationForm `
                -Title ("{0}/{1} 单灯定位" -f $key.logical, $key.led) `
                -Instruction ("脚本已经发送 `{0}`。请只看按键灯区域，不要按实体按键。`r`n这个步骤确认 {1} 对应 {2}，位置正确且不会污染 PWR/BLE/REC/AI/OK/WARN、EC11 或边框灯。" -f $cmd, $key.logical, $key.led) `
                -Expected ("预期：只亮 {0}/{1} 白色低亮度，亮度接近蓝灯/不刺眼；其它 KEY、状态灯 LED1-LED6、EC11、边框都不应跟亮或闪。" -f $key.logical, $key.led)
            Add-Record (New-StepRecord -Phase "button_led_static" -StepId ("pixel-{0}" -f $key.logical.ToLowerInvariant()) -Title ("{0}/{1} 单灯定位" -f $key.logical, $key.led) -Result $human.result -Human $human -Artifacts @($capture))
            [void](Invoke-SerialCapture -SerialPortName $script:ResolvedPort -Label ("pixel-{0}-clear" -f $key.logical.ToLowerInvariant()) -Commands @("~LED:OFF", "~LED:STATUS") -InitialReadMs 200 -CommandReadMs 700)
        }
    }

    if (-not $SkipPhysicalKeyFeedback.IsPresent) {
        $gestureSteps = @(Get-KeyGestureSteps)
        if ($gestureSteps.Count -eq 0) {
            throw "GestureFilter did not match any gesture. Valid values: single, double, long."
        }
        foreach ($key in $keySteps) {
            foreach ($gesture in $gestureSteps) {
                $liveCapture = Invoke-LiveKeyPressCapture -SerialPortName $script:ResolvedPort -Key $key -Gesture $gesture -Seconds ([int]$gesture.seconds)
                $human = Show-ObservationForm `
                    -Title ("实体 {0} {1}灯效" -f $key.logical, $gesture.title) `
                    -Instruction ("刚才的窗口已经实时采集了实体 {0}（{1}，单击 fallback {2}）的{3}。请根据刚才看到的现象判断；如果失败，请写清楚是按键没有反应、灯没有亮、串灯、全亮，还是亮度/时长不对。" -f $key.logical, $key.gpio, $key.usage, $gesture.title) `
                    -Expected ("预期：{0}。PWR/BLE 保持自己的状态；REC/AI/OK/WARN、EC11、边框不应被点亮；不能出现 LED3-LED6 全亮/乱闪。" -f $gesture.expected)
                Add-Record (New-StepRecord -Phase "button_led_physical" -StepId ("press-{0}-{1}" -f $key.logical.ToLowerInvariant(), $gesture.id) -Title ("实体 {0} {1}灯效" -f $key.logical, $gesture.title) -Result $human.result -Human $human -Artifacts @($liveCapture))
            }
        }
    }

    [void](Invoke-SerialCapture -SerialPortName $script:ResolvedPort -Label "final-clear-status" -Commands @(
        "~LED:PREVIEW clear",
        "~LED:STATUS",
        "~POWER:STATUS"
    ) -InitialReadMs 300 -CommandReadMs 1200)
} catch {
    Add-ErrorRecord $_.Exception.Message
}

$result = "PASS"
if ($errors.Count -gt 0 -or @($records | Where-Object { $_.result -eq "FAIL" }).Count -gt 0) {
    $result = "FAIL"
} elseif (@($records | Where-Object { $_.result -eq "SKIP" -or $_.result -eq "ABORT" }).Count -gt 0) {
    $result = "INCOMPLETE"
}

Write-Outputs -Result $result
if ($result -ne "PASS") {
    exit 1
}
