[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$PluggedIdleSeconds = 70,
    [int]$BatteryIdleSeconds = 75,
    [int]$BatteryShutdownTotalSeconds = 135,
    [int]$PortDisappearTimeoutSeconds = 30,
    [int]$PortReappearTimeoutSeconds = 120,
    [ValidateRange(0, 3600)]
    [int]$PromptTimeoutSeconds = 240,
    [int]$PostReplugSettleSeconds = 8,
    [string]$OutputDir = "",
    [switch]$NoAiwLock,
    [switch]$NoPrompt,
    [switch]$NoPromptTimeout,
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

if ($BatteryShutdownTotalSeconds -lt $BatteryIdleSeconds) {
    throw "-BatteryShutdownTotalSeconds must be >= -BatteryIdleSeconds."
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".cache\validation\low-power-acceptance-guided-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$sessionPath = Join-Path $OutputDir "guided-session.jsonl"
$transcriptPath = Join-Path $OutputDir "guided-transcript.txt"
$summaryJsonPath = Join-Path $OutputDir "guided-summary.json"
$summaryMdPath = Join-Path $OutputDir "guided-summary.md"
Set-Content -LiteralPath $sessionPath -Value @() -Encoding UTF8
Set-Content -LiteralPath $transcriptPath -Value @() -Encoding UTF8

$script:Records = [System.Collections.Generic.List[object]]::new()
$script:Errors = [System.Collections.Generic.List[string]]::new()
$script:SerialCaptureIndex = 0

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)

    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $transcriptPath -Value $line -Encoding UTF8
}

function Add-Record {
    param([Parameter(Mandatory = $true)][object]$Record)

    $script:Records.Add($Record) | Out-Null
    ($Record | ConvertTo-Json -Depth 12 -Compress) | Add-Content -LiteralPath $sessionPath -Encoding UTF8
}

function Add-ErrorRecord {
    param([Parameter(Mandatory = $true)][string]$Message)

    $script:Errors.Add($Message) | Out-Null
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
            # Sound is best-effort; prompts must still appear on muted/headless systems.
        }
    }
}

function Get-ActivePromptTimeoutSeconds {
    if ($NoPromptTimeout.IsPresent) {
        return 0
    }
    return [Math]::Max(0, $PromptTimeoutSeconds)
}

function Add-PromptTimeoutTick {
    param(
        [Parameter(Mandatory = $true)][System.Windows.Forms.Form]$Form,
        [Parameter(Mandatory = $true)][System.Windows.Forms.Label]$Label,
        [Parameter(Mandatory = $true)][hashtable]$Holder,
        [Parameter(Mandatory = $true)][string]$TimeoutResult,
        [scriptblock]$OnTimeout = $null
    )

    $timeoutSeconds = Get-ActivePromptTimeoutSeconds
    if ($timeoutSeconds -le 0) {
        $Label.Text = "此弹窗不会自动关闭。"
        return $null
    }

    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    $Label.Text = "若无操作，$timeoutSeconds 秒后自动继续并记录 timeout。"
    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Interval = 250
    $tickHandler = {
        $remaining = [Math]::Max(0, [int][Math]::Ceiling(($deadline - (Get-Date)).TotalSeconds))
        $Label.Text = "若无操作，$remaining 秒后自动继续并记录 timeout。"
        if ((Get-Date) -ge $deadline) {
            $timer.Stop()
            $Holder.result = $TimeoutResult
            $Holder.timed_out = $true
            if ($null -ne $OnTimeout) {
                & $OnTimeout
            }
            $Form.Close()
        }
    }.GetNewClosure()
    $timer.Add_Tick($tickHandler)
    return $timer
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
    $form = [System.Windows.Forms.Form]::new()
    $timer = $null
    $holder = @{ result = [System.Windows.Forms.DialogResult]::OK; timed_out = $false }
    try {
        $form.Text = "[$AgentName] $Title"
        $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
        $form.TopMost = $true
        $form.Width = 720
        $form.Height = 360
        $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
        $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
        $form.MaximizeBox = $false
        $form.MinimizeBox = $false

        $titleLabel = [System.Windows.Forms.Label]::new()
        $titleLabel.Left = 16
        $titleLabel.Top = 14
        $titleLabel.Width = 660
        $titleLabel.Height = 28
        $titleLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
        $titleLabel.Text = $Title
        $form.Controls.Add($titleLabel)

        $messageBox = [System.Windows.Forms.TextBox]::new()
        $messageBox.Left = 16
        $messageBox.Top = 52
        $messageBox.Width = 668
        $messageBox.Height = 174
        $messageBox.Multiline = $true
        $messageBox.ReadOnly = $true
        $messageBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
        $messageBox.Text = $Message
        $form.Controls.Add($messageBox)

        $timeoutLabel = [System.Windows.Forms.Label]::new()
        $timeoutLabel.Left = 16
        $timeoutLabel.Top = 238
        $timeoutLabel.Width = 420
        $timeoutLabel.Height = 24
        $form.Controls.Add($timeoutLabel)

        $buttonDefs = switch ($Buttons) {
            ([System.Windows.Forms.MessageBoxButtons]::OKCancel) {
                @(
                    @{ Text = "确定"; Result = [System.Windows.Forms.DialogResult]::OK },
                    @{ Text = "取消"; Result = [System.Windows.Forms.DialogResult]::Cancel }
                )
                break
            }
            ([System.Windows.Forms.MessageBoxButtons]::YesNo) {
                @(
                    @{ Text = "是"; Result = [System.Windows.Forms.DialogResult]::Yes },
                    @{ Text = "否"; Result = [System.Windows.Forms.DialogResult]::No }
                )
                break
            }
            default {
                @(@{ Text = "确定"; Result = [System.Windows.Forms.DialogResult]::OK })
                break
            }
        }

        $buttonWidth = 96
        $gap = 12
        $totalWidth = ($buttonDefs.Count * $buttonWidth) + (($buttonDefs.Count - 1) * $gap)
        $x = 684 - $totalWidth
        foreach ($buttonDef in $buttonDefs) {
            $button = [System.Windows.Forms.Button]::new()
            $button.Text = [string]$buttonDef.Text
            $button.Left = $x
            $button.Top = 268
            $button.Width = $buttonWidth
            $button.Height = 32
            $button.Add_Click({
                $holder.result = $buttonDef.Result
                $form.Close()
            }.GetNewClosure())
            $form.Controls.Add($button)
            $x += $buttonWidth + $gap
        }

        $timer = Add-PromptTimeoutTick -Form $form -Label $timeoutLabel -Holder $holder -TimeoutResult ([System.Windows.Forms.DialogResult]::OK)
        if ($null -ne $timer) {
            $timer.Start()
        }
        [void]$form.ShowDialog()
        if ($holder.timed_out) {
            Add-Transcript ("operator_prompt_timeout title=[{0}] {1} auto_result={2}" -f $AgentName, $Title, $holder.result)
        }
        return $holder.result
    } finally {
        if ($null -ne $timer) {
            $timer.Stop()
            $timer.Dispose()
        }
        $form.Dispose()
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

    Add-Transcript ("observation_prompt title=[{0}] {1}" -f $AgentName, $Title)
    if ($NoPrompt.IsPresent) {
        return [PSCustomObject]@{
            result = "SKIP"
            observation = "NoPrompt mode"
            extra = $ExtraDefault
            notes = ""
        }
    }

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] $Title"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 840
    $form.Height = 700
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 9)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $y = 14
    $titleLabel = [System.Windows.Forms.Label]::new()
    $titleLabel.Left = 14
    $titleLabel.Top = $y
    $titleLabel.Width = 790
    $titleLabel.Height = 28
    $titleLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
    $titleLabel.Text = $Title
    $form.Controls.Add($titleLabel)
    $y += 36

    $instructionBox = [System.Windows.Forms.TextBox]::new()
    $instructionBox.Left = 14
    $instructionBox.Top = $y
    $instructionBox.Width = 795
    $instructionBox.Height = 135
    $instructionBox.Multiline = $true
    $instructionBox.ReadOnly = $true
    $instructionBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $instructionBox.Text = $Instruction
    $form.Controls.Add($instructionBox)
    $y += 145

    $expectedBox = [System.Windows.Forms.TextBox]::new()
    $expectedBox.Left = 14
    $expectedBox.Top = $y
    $expectedBox.Width = 795
    $expectedBox.Height = 92
    $expectedBox.Multiline = $true
    $expectedBox.ReadOnly = $true
    $expectedBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $expectedBox.Text = $Expected
    $form.Controls.Add($expectedBox)
    $y += 104

    $extraText = $null
    if (-not [string]::IsNullOrWhiteSpace($ExtraLabel)) {
        $extraLabelControl = [System.Windows.Forms.Label]::new()
        $extraLabelControl.Left = 14
        $extraLabelControl.Top = $y + 4
        $extraLabelControl.Width = 230
        $extraLabelControl.Height = 24
        $extraLabelControl.Text = $ExtraLabel
        $form.Controls.Add($extraLabelControl)

        $extraText = [System.Windows.Forms.TextBox]::new()
        $extraText.Left = 252
        $extraText.Top = $y
        $extraText.Width = 557
        $extraText.Text = $ExtraDefault
        $form.Controls.Add($extraText)
        $y += 38
    }

    $observationLabel = [System.Windows.Forms.Label]::new()
    $observationLabel.Left = 14
    $observationLabel.Top = $y
    $observationLabel.Width = 760
    $observationLabel.Height = 22
    $observationLabel.Text = "你看到的现象："
    $form.Controls.Add($observationLabel)
    $y += 24

    $observationBox = [System.Windows.Forms.TextBox]::new()
    $observationBox.Left = 14
    $observationBox.Top = $y
    $observationBox.Width = 795
    $observationBox.Height = 74
    $observationBox.Multiline = $true
    $observationBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $form.Controls.Add($observationBox)
    $y += 86

    $notesLabel = [System.Windows.Forms.Label]::new()
    $notesLabel.Left = 14
    $notesLabel.Top = $y
    $notesLabel.Width = 780
    $notesLabel.Height = 22
    $notesLabel.Text = "备注：异常时写清楚哪颗灯、什么颜色、是否全亮、是否闪烁、当前电流是否稳定。"
    $form.Controls.Add($notesLabel)
    $y += 24

    $notesBox = [System.Windows.Forms.TextBox]::new()
    $notesBox.Left = 14
    $notesBox.Top = $y
    $notesBox.Width = 795
    $notesBox.Height = 60
    $notesBox.Multiline = $true
    $notesBox.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $form.Controls.Add($notesBox)
    $y += 76

    $holder = @{ result = "ABORT"; timed_out = $false }
    $passButton = [System.Windows.Forms.Button]::new()
    $passButton.Text = "通过"
    $passButton.Left = 468
    $passButton.Top = $y
    $passButton.Width = 100
    $passButton.Add_Click({ $holder.result = "PASS"; $form.Close() })
    $form.Controls.Add($passButton)

    $failButton = [System.Windows.Forms.Button]::new()
    $failButton.Text = "失败"
    $failButton.Left = 580
    $failButton.Top = $y
    $failButton.Width = 100
    $failButton.Add_Click({ $holder.result = "FAIL"; $form.Close() })
    $form.Controls.Add($failButton)

    $skipButton = [System.Windows.Forms.Button]::new()
    $skipButton.Text = "跳过/不确定"
    $skipButton.Left = 692
    $skipButton.Top = $y
    $skipButton.Width = 118
    $skipButton.Add_Click({ $holder.result = "SKIP"; $form.Close() })
    $form.Controls.Add($skipButton)

    $y += 36
    $timeoutLabel = [System.Windows.Forms.Label]::new()
    $timeoutLabel.Left = 14
    $timeoutLabel.Top = $y
    $timeoutLabel.Width = 795
    $timeoutLabel.Height = 24
    $form.Controls.Add($timeoutLabel)

    $timeoutTimer = Add-PromptTimeoutTick -Form $form -Label $timeoutLabel -Holder $holder -TimeoutResult "SKIP" -OnTimeout {
        if ([string]::IsNullOrWhiteSpace($notesBox.Text)) {
            $notesBox.Text = "timeout: 弹窗超时未填写，自动按跳过/不确定记录。"
        } else {
            $notesBox.Text = $notesBox.Text + "`r`ntimeout: 弹窗超时，自动按跳过/不确定记录。"
        }
    }
    Play-PromptSound
    try {
        if ($null -ne $timeoutTimer) {
            $timeoutTimer.Start()
        }
        [void]$form.ShowDialog()
        if ($holder.timed_out) {
            Add-Transcript ("observation_prompt_timeout title=[{0}] {1} auto_result={2}" -f $AgentName, $Title, $holder.result)
        }
        $extraValue = if ($null -ne $extraText) { $extraText.Text } else { "" }
        $result = [PSCustomObject]@{
            result = [string]$holder.result
            observation = [string]$observationBox.Text
            extra = [string]$extraValue
            notes = [string]$notesBox.Text
            timed_out = [bool]$holder.timed_out
            timeout_seconds = (Get-ActivePromptTimeoutSeconds)
        }
    } finally {
        if ($null -ne $timeoutTimer) {
            $timeoutTimer.Stop()
            $timeoutTimer.Dispose()
        }
        $form.Dispose()
    }
    return $result
}

function Show-CountdownWindow {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][int]$Seconds,
        [bool]$AllowEarlyContinue = $false
    )

    Add-Transcript ("countdown_prompt title=[{0}] {1} seconds={2}" -f $AgentName, $Title, $Seconds)
    if ($Seconds -le 0) {
        return
    }
    if ($NoPrompt.IsPresent) {
        Start-Sleep -Seconds $Seconds
        return
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] $Title"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 700
    $form.Height = 260
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $titleLabel = [System.Windows.Forms.Label]::new()
    $titleLabel.Left = 16
    $titleLabel.Top = 14
    $titleLabel.Width = 640
    $titleLabel.Height = 28
    $titleLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
    $titleLabel.Text = $Title
    $form.Controls.Add($titleLabel)

    $messageBox = [System.Windows.Forms.TextBox]::new()
    $messageBox.Left = 16
    $messageBox.Top = 52
    $messageBox.Width = 650
    $messageBox.Height = 86
    $messageBox.Multiline = $true
    $messageBox.ReadOnly = $true
    $messageBox.Text = $Message
    $form.Controls.Add($messageBox)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.Left = 16
    $countdown.Top = 154
    $countdown.Width = 420
    $countdown.Height = 34
    $countdown.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 14, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($countdown)

    if ($AllowEarlyContinue) {
        $continueButton = [System.Windows.Forms.Button]::new()
        $continueButton.Text = "提前继续"
        $continueButton.Left = 530
        $continueButton.Top = 154
        $continueButton.Width = 136
        $continueButton.Height = 34
        $continueButton.Add_Click({ $form.Close() })
        $form.Controls.Add($continueButton)
    }

    $deadline = (Get-Date).AddSeconds($Seconds)
    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Interval = 200
    $timer.Add_Tick({
        $remaining = [Math]::Max(0, [int][Math]::Ceiling(($deadline - (Get-Date)).TotalSeconds))
        $countdown.Text = "剩余 $remaining 秒"
        if ((Get-Date) -ge $deadline) {
            $timer.Stop()
            $form.Close()
        }
    }.GetNewClosure())

    try {
        $timer.Start()
        [void]$form.ShowDialog()
    } finally {
        $timer.Stop()
        $timer.Dispose()
        $form.Dispose()
    }
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

function Show-PortPicker {
    param([string[]]$Ports)

    if ($NoPrompt.IsPresent) {
        throw "Expected one serial port when -Port is omitted; found $(Format-Ports $Ports)."
    }

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] 选择 Listener 串口"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 520
    $form.Height = 210
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.Left = 14
    $label.Top = 16
    $label.Width = 470
    $label.Height = 44
    $label.Text = "请选择 Listener 当前串口。没有看到时可以直接输入，例如 COM10。"
    $form.Controls.Add($label)

    $combo = [System.Windows.Forms.ComboBox]::new()
    $combo.Left = 14
    $combo.Top = 70
    $combo.Width = 470
    $combo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDown
    foreach ($portName in @($Ports)) {
        [void]$combo.Items.Add($portName)
    }
    if ($combo.Items.Count -gt 0) {
        $combo.SelectedIndex = 0
    } else {
        $combo.Text = "COM10"
    }
    $form.Controls.Add($combo)

    $holder = @{ port = "" }
    $okButton = [System.Windows.Forms.Button]::new()
    $okButton.Text = "确定"
    $okButton.Left = 292
    $okButton.Top = 116
    $okButton.Width = 90
    $okButton.Add_Click({ $holder.port = $combo.Text; $form.Close() })
    $form.Controls.Add($okButton)

    $cancelButton = [System.Windows.Forms.Button]::new()
    $cancelButton.Text = "取消"
    $cancelButton.Left = 394
    $cancelButton.Top = 116
    $cancelButton.Width = 90
    $cancelButton.Add_Click({ $holder.port = ""; $form.Close() })
    $form.Controls.Add($cancelButton)

    [void]$form.ShowDialog()
    $form.Dispose()
    if ([string]::IsNullOrWhiteSpace($holder.port)) {
        throw "Serial port selection was cancelled."
    }
    return $holder.port.Trim().ToUpperInvariant()
}

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port.Trim().ToUpperInvariant()
    }

    $ports = @(Get-SerialPorts)
    if ($ports.Count -eq 1) {
        return $ports[0].ToUpperInvariant()
    }
    return Show-PortPicker -Ports $ports
}

function Wait-PortPresence {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPort,
        [Parameter(Mandatory = $true)][bool]$ShouldBePresent,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $targetUpper = $TargetPort.ToUpperInvariant()
    $lastPorts = @()
    $poll = 0
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        $lastPorts = $ports
        $present = @($ports | ForEach-Object { $_.ToUpperInvariant() }) -contains $targetUpper
        Add-Transcript ("port_poll label={0} poll={1} expect_present={2} target={3} present={4} ports={5}" -f $Label, $poll, $ShouldBePresent, $TargetPort, $present, (Format-Ports $ports))
        if ($present -eq $ShouldBePresent) {
            return [PSCustomObject]@{
                observed = $true
                port = $TargetPort
                last_ports = @($lastPorts)
            }
        }
        Start-Sleep -Milliseconds 500
        $poll += 1
    }
    return [PSCustomObject]@{
        observed = $false
        port = $TargetPort
        last_ports = @($lastPorts)
    }
}

function Wait-PortPresent {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPort,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $targetUpper = $TargetPort.ToUpperInvariant()
    $lastPorts = @()
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        $lastPorts = $ports
        Add-Transcript ("port_poll label=after_replug target={0} ports={1}" -f $TargetPort, (Format-Ports $ports))
        if (@($ports | ForEach-Object { $_.ToUpperInvariant() }) -contains $targetUpper) {
            return [PSCustomObject]@{ observed = $true; port = $TargetPort; last_ports = @($lastPorts) }
        }
        if ($ports.Count -eq 1) {
            return [PSCustomObject]@{ observed = $true; port = $ports[0].ToUpperInvariant(); last_ports = @($lastPorts) }
        }
        Start-Sleep -Milliseconds 500
    }
    return [PSCustomObject]@{ observed = $false; port = ""; last_ports = @($lastPorts) }
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
        [int]$InitialReadMs = 800,
        [int]$CommandReadMs = 1600
    )

    $script:SerialCaptureIndex += 1
    $safeLabel = ($Label -replace '[^A-Za-z0-9_.-]', '-')
    $capturePath = Join-Path $OutputDir ("serial-{0:00}-{1}.txt" -f $script:SerialCaptureIndex, $safeLabel)
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
        ($Commands -join ";;"),
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
            "3",
            "-Purpose",
            "$AgentName guided low-power acceptance serial capture",
            "-Run",
            "pwsh"
        ) + $baseArgs
        Invoke-LoggedNative -Arguments $lockedArgs -Label $Label
    }

    return $capturePath
}

function Get-LatestLine {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Prefix
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }
    $escapedPrefix = [regex]::Escape($Prefix)
    $lines = @(Get-Content -LiteralPath $Path | Where-Object { $_ -match $escapedPrefix })
    if ($lines.Count -eq 0) {
        return ""
    }
    return $lines[-1]
}

function Convert-KeyValueLine {
    param([string]$Line)

    $map = [ordered]@{}
    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $map
    }
    foreach ($part in ($Line -split "\s+")) {
        if ($part -match "^([^=\s]+)=(.*)$") {
            $map[$matches[1]] = $matches[2].Trim('"')
        }
    }
    return $map
}

function Test-ZeroPixelLine {
    param(
        [string]$Line,
        [string]$FieldName
    )

    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $false
    }
    if ($Line -match "$([regex]::Escape($FieldName))=0,0,0(\|0,0,0)*") {
        return $true
    }
    $matches = [regex]::Matches($Line, "px\d+:([0-9]+),([0-9]+),([0-9]+)")
    if ($matches.Count -eq 0) {
        return $false
    }
    foreach ($match in $matches) {
        if ($match.Groups[1].Value -ne "0" -or
            $match.Groups[2].Value -ne "0" -or
            $match.Groups[3].Value -ne "0") {
            return $false
        }
    }
    return $true
}

function Test-PwrOnlyLedLine {
    param(
        [string]$StatusLine,
        [string]$Ec11Line,
        [string]$KeyLine,
        [string]$EdgeLine
    )

    if ([string]::IsNullOrWhiteSpace($StatusLine)) {
        return $false
    }
    $statusRgbOk = $StatusLine -match "status_rgb=PWR:[0-9]+,[0-9]+,[0-9]+;BLE:0,0,0;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0"
    $ec11Ok = Test-ZeroPixelLine -Line $Ec11Line -FieldName "rgb_ec11"
    $keyOk = Test-ZeroPixelLine -Line $KeyLine -FieldName "rgb_key"
    $edgeOk = Test-ZeroPixelLine -Line $EdgeLine -FieldName "rgb_edge"
    return ($statusRgbOk -and $ec11Ok -and $keyOk -and $edgeOk)
}

function Analyze-Capture {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Label = ""
    )

    $text = if (Test-Path -LiteralPath $Path) { Get-Content -Raw -LiteralPath $Path } else { "" }
    $powerLine = Get-LatestLine -Path $Path -Prefix "~POWER:STATUS"
    $ledRgbLine = Get-LatestLine -Path $Path -Prefix "~LED:STATUS detail=rgb "
    $ledEc11Line = Get-LatestLine -Path $Path -Prefix "~LED:STATUS detail=rgb_ec11 "
    $ledKeyLine = Get-LatestLine -Path $Path -Prefix "~LED:STATUS detail=rgb_key "
    $ledEdgeLine = Get-LatestLine -Path $Path -Prefix "~LED:STATUS detail=rgb_edge "
    $ledStateLine = Get-LatestLine -Path $Path -Prefix "~LED:STATUS detail=state"
    $settingsLine = Get-LatestLine -Path $Path -Prefix "~DEVICE:SETTINGS"
    $powerMap = Convert-KeyValueLine -Line $powerLine
    $settingsMap = Convert-KeyValueLine -Line $settingsLine
    $pwrOnly = Test-PwrOnlyLedLine -StatusLine $ledRgbLine -Ec11Line $ledEc11Line -KeyLine $ledKeyLine -EdgeLine $ledEdgeLine
    $connectedIdle = ($powerMap.Contains("state") -and $powerMap["state"] -eq "CONNECTED_IDLE")
    $idleState = ($powerMap.Contains("state") -and @("CONNECTED_IDLE", "DISCONNECTED_IDLE") -contains $powerMap["state"])
    $settingsOk =
        ($settingsMap.Contains("low_power_idle_ms") -and $settingsMap["low_power_idle_ms"] -eq "60000") -and
        ($settingsMap.Contains("battery_auto_shutdown_ms") -and $settingsMap["battery_auto_shutdown_ms"] -eq "120000")
    $sawShutdown =
        $text -match "DIAG_POWER_SLEEP_ENTRY" -or
        $text -match '"src":"power","evt":2' -or
        $text -match '"src":"power","evt":1[^`r`n]*"a2":3' -or
        $text -match "last_shutdown_reason=(idle_timeout|critical_battery|manual_command)" -or
        $text -match "~POWER:STATUS state=HARDWARE_SHUTDOWN"

    return [PSCustomObject]@{
        label = $Label
        artifact = $Path
        power_status = $powerLine
        led_state = $ledStateLine
        led_rgb = $ledRgbLine
        led_rgb_ec11 = $ledEc11Line
        led_rgb_key = $ledKeyLine
        led_rgb_edge = $ledEdgeLine
        settings = $settingsLine
        connected_idle = [bool]$connectedIdle
        idle_state = [bool]$idleState
        pwr_only_led = [bool]$pwrOnly
        settings_ok = [bool]$settingsOk
        saw_shutdown_evidence = [bool]$sawShutdown
    }
}

function Write-Summary {
    param(
        [Parameter(Mandatory = $true)][string]$Result,
        [string]$InitialPort = "",
        [string]$PostPort = ""
    )

    $summary = [ordered]@{
        schema = "listener.low_power_acceptance_guided.v1"
        generated_at = (Get-Date).ToString("o")
        agent = $AgentName
        result = $Result
        initial_port = $InitialPort
        post_port = $PostPort
    plugged_idle_seconds = $PluggedIdleSeconds
    battery_idle_seconds = $BatteryIdleSeconds
    battery_shutdown_total_seconds = $BatteryShutdownTotalSeconds
    prompt_timeout_seconds = (Get-ActivePromptTimeoutSeconds)
    output_dir = $OutputDir
        transcript = $transcriptPath
        session = $sessionPath
        records = @($script:Records)
        errors = @($script:Errors)
    }
    ($summary | ConvertTo-Json -Depth 12) | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Guided Low-Power Acceptance") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add(("- Result: {0}" -f $Result)) | Out-Null
    $lines.Add(("- Agent: {0}" -f $AgentName)) | Out-Null
    $lines.Add(("- Initial port: {0}" -f $InitialPort)) | Out-Null
    $lines.Add(("- Post port: {0}" -f $PostPort)) | Out-Null
    $lines.Add(("- Output dir: ``{0}``" -f $OutputDir)) | Out-Null
    $lines.Add(("- Transcript: ``{0}``" -f $transcriptPath)) | Out-Null
    $lines.Add(("- Session JSONL: ``{0}``" -f $sessionPath)) | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("## Records") | Out-Null
    foreach ($record in @($script:Records)) {
        $phase = if ($record.PSObject.Properties.Name -contains "phase") { $record.phase } else { "record" }
        $lines.Add(("- {0}: {1}" -f $phase, ($record | ConvertTo-Json -Depth 8 -Compress))) | Out-Null
    }
    if ($script:Errors.Count -gt 0) {
        $lines.Add("") | Out-Null
        $lines.Add("## Errors") | Out-Null
        foreach ($errorLine in @($script:Errors)) {
            $lines.Add(("- {0}" -f $errorLine)) | Out-Null
        }
    }
    $lines | Set-Content -LiteralPath $summaryMdPath -Encoding UTF8
}

Add-Transcript ("start agent={0} port={1} output_dir={2} plan_only={3}" -f $AgentName, $Port, $OutputDir, $PlanOnly.IsPresent)

if ($PlanOnly.IsPresent) {
    Add-Record ([PSCustomObject]@{
        phase = "plan"
        steps = @(
            "select serial port",
            "set low_power_idle_minutes=1, plugged_low_power_enabled=1, plugged_auto_shutdown_minutes=off, battery_auto_shutdown_minutes=2",
            "capture plugged baseline",
            "wait 70s and capture plugged idle LED/POWER state",
            "operator records plugged LED observation",
            "operator unplugs USB-C and records battery idle current",
            "wait until 135s total battery runtime and operator confirms power-off",
            "operator reboots/replugs and script captures retained power/LED diag logs"
        )
    })
    Write-Summary -Result "PLAN_ONLY"
    Write-Host ("result=PLAN_ONLY`nsummary={0}`ntranscript={1}" -f $summaryMdPath, $transcriptPath)
    exit 0
}

$initialPort = Resolve-TestPort
$postPort = ""
$operatorResults = [System.Collections.Generic.List[string]]::new()

Show-TopMostMessageBox `
    -Title "低功耗验收向导：准备开始" `
    -Message "这轮会按当前目标配置：idle 1 分钟，电池自动关机 2 分钟，插电允许进 idle 但不自动关机。`r`n`r`n请确认 Listener 现在通过 USB-C 连着电脑，串口是 $initialPort；不要按键，不要录音，不要切换灯效。点确定后我会清本轮 diag_log 并读取设置/灯状态。"

$setupCapture = Invoke-SerialCapture -SerialPortName $initialPort -Label "01-setup-plugged" -Commands @(
    "~DIAGLOG:CLEAR",
    "~DEVICE:SET low_power_idle_minutes=1 plugged_low_power_enabled=1 plugged_auto_shutdown_minutes=off battery_auto_shutdown_minutes=2",
    "~DEVICE:SETTINGS",
    "~POWER:STATUS",
    "~LED:STATUS",
    "~BOARD:STATUS"
) -InitialReadMs 800 -CommandReadMs 1800
$setupAnalysis = Analyze-Capture -Path $setupCapture -Label "setup_plugged"
Add-Record ([PSCustomObject]@{
    phase = "setup_plugged"
    analysis = $setupAnalysis
})
if (-not $setupAnalysis.settings_ok) {
    Add-ErrorRecord "setup settings did not confirm low_power_idle_ms=60000 and battery_auto_shutdown_ms=120000; see $setupCapture"
}

Show-CountdownWindow `
    -Title "插电 idle 等待" `
    -Message "现在保持 USB-C 连接，不要按键、不要录音、不要转旋钮。预期 $PluggedIdleSeconds 秒后进入 CONNECTED_IDLE 或 DISCONNECTED_IDLE；插电只进 idle，不应该自动关机。" `
    -Seconds $PluggedIdleSeconds

$pluggedObservation = Show-ObservationForm `
    -Title "观察插电 idle 灯" `
    -Instruction "请现在看 Listener 当前灯。重点看 LED3 到 LED6、蓝牙灯、EC11 环、边灯。`r`n`r`n这一步是在 USB 插着的情况下：允许进入 idle，但不允许自动关机。请按点击确认前看到的硬件灯判断；确认后脚本才会打开串口抓机器证据，串口抓取可能短暂唤醒设备。" `
    -Expected "预期：只保留一个很暗的电源灯。蓝牙灯不应该常亮；LED3 到 LED6 不应该全亮；EC11 环、按键灯、边灯应关闭。`r`n`r`n请先完成肉眼判断，再点 PASS/FAIL；串口 LED 行会在下一步自动记录。" `
    -ExtraLabel "插电电流读数 mA（可空）："
Add-Record ([PSCustomObject]@{
    phase = "plugged_idle_observation"
    result = $pluggedObservation.result
    current_ma = $pluggedObservation.extra
    observation = $pluggedObservation.observation
    notes = $pluggedObservation.notes
})
$operatorResults.Add($pluggedObservation.result) | Out-Null
if ($pluggedObservation.result -eq "FAIL") {
    Add-ErrorRecord ("operator marked plugged idle LED as FAIL before serial capture: {0} {1}" -f $pluggedObservation.observation, $pluggedObservation.notes)
}

$pluggedIdleCapture = Invoke-SerialCapture -SerialPortName $initialPort -Label "02-plugged-idle" -Commands @(
    "~POWER:STATUS",
    "~POWER:IDLE",
    "~POWER:PM",
    "~LED:STATUS",
    "~DIAGLOG:LAST:120:status_led",
    "~DIAGLOG:LAST:80:power"
) -InitialReadMs 1200 -CommandReadMs 1800
$pluggedIdleAnalysis = Analyze-Capture -Path $pluggedIdleCapture -Label "plugged_idle"
Add-Record ([PSCustomObject]@{
    phase = "plugged_idle_capture"
    analysis = $pluggedIdleAnalysis
})
if (-not $pluggedIdleAnalysis.idle_state) {
    Add-ErrorRecord "plugged idle capture did not report CONNECTED_IDLE or DISCONNECTED_IDLE; see $pluggedIdleCapture"
}
if (-not $pluggedIdleAnalysis.pwr_only_led) {
    Add-ErrorRecord "plugged idle LED was not parsed as PWR-only; see $pluggedIdleCapture"
}

Show-TopMostMessageBox `
    -Title "准备电池低功耗测量" `
    -Message "现在准备电流表/电源表。下一步会让你拔掉 USB-C，只保留电池供电。`r`n`r`n拔掉后串口会消失，这是正常的；脚本会用弹窗计时。等待期间不要按键、不要插回 USB、不要打开 Type 录音。"

Show-TopMostMessageBox `
    -Title "现在拔掉 USB-C" `
    -Message "请现在拔掉 Listener 的 USB-C 线，只保留电池供电。拔掉后点击确定，我会等待 Windows 中的 $initialPort 消失。"

$disappear = Wait-PortPresence -TargetPort $initialPort -ShouldBePresent $false -TimeoutSeconds $PortDisappearTimeoutSeconds -Label "after_unplug"
Add-Record ([PSCustomObject]@{
    phase = "unplug"
    port_disappeared = [bool]$disappear.observed
    last_ports = @($disappear.last_ports)
})
if (-not $disappear.observed) {
    Add-ErrorRecord "serial port $initialPort did not disappear after USB-C unplug."
}

Show-CountdownWindow `
    -Title "电池 idle 等待" `
    -Message "保持电池供电等待 $BatteryIdleSeconds 秒。预期约 1 分钟后进入低功耗 idle。请观察灯和电流是否下降；这段时间不要按任何按键。" `
    -Seconds $BatteryIdleSeconds

$batteryIdleObservation = Show-ObservationForm `
    -Title "记录电池 idle 电流和灯" `
    -Instruction "请读取电流表，并观察灯。这个读数才接近真实低功耗；USB 插着时看到的 20mA 不能当作最终低功耗结论。" `
    -Expected "预期：进入 idle 后电流应明显下降；目前 20mA 左右可以先记录为待确认，不直接判死刑。灯光预期仍然是安静状态：不要 LED3 到 LED6 全亮，不要蓝牙灯常亮。"`
    -ExtraLabel "电池 idle 电流 mA："
Add-Record ([PSCustomObject]@{
    phase = "battery_idle_observation"
    result = $batteryIdleObservation.result
    current_ma = $batteryIdleObservation.extra
    observation = $batteryIdleObservation.observation
    notes = $batteryIdleObservation.notes
})
$operatorResults.Add($batteryIdleObservation.result) | Out-Null
if ($batteryIdleObservation.result -eq "FAIL") {
    Add-ErrorRecord ("operator marked battery idle as FAIL: current={0} observation={1} notes={2}" -f $batteryIdleObservation.extra, $batteryIdleObservation.observation, $batteryIdleObservation.notes)
}

$shutdownRemaining = [Math]::Max(0, $BatteryShutdownTotalSeconds - $BatteryIdleSeconds)
Show-CountdownWindow `
    -Title "等待电池自动关机" `
    -Message "继续保持电池供电，不要按键。总等待目标是 $BatteryShutdownTotalSeconds 秒，覆盖 2 分钟自动关机。到点后请观察是否真正熄灯/断电。" `
    -Seconds $shutdownRemaining

$shutdownObservation = Show-ObservationForm `
    -Title "确认电池自动关机" `
    -Instruction "请观察 Listener 是否已经自动关机。重点看：PWR、蓝牙灯、LED3 到 LED6、EC11 环、边灯是否都已经熄灭；设备是否不再响应普通灯效。" `
    -Expected "预期：电池模式下约 2 分钟无操作后进入硬件关机，所有灯熄灭。若任意灯保持常亮，尤其 LED3 到 LED6 全亮，请点失败并写清楚。"
Add-Record ([PSCustomObject]@{
    phase = "battery_auto_shutdown_observation"
    result = $shutdownObservation.result
    observation = $shutdownObservation.observation
    notes = $shutdownObservation.notes
})
$operatorResults.Add($shutdownObservation.result) | Out-Null
if ($shutdownObservation.result -eq "FAIL") {
    Add-ErrorRecord ("operator marked battery auto-shutdown as FAIL: {0} {1}" -f $shutdownObservation.observation, $shutdownObservation.notes)
}

Show-TopMostMessageBox `
    -Title "恢复连接并抓取关机证据" `
    -Message "现在请短按一次硬件电源/EC11 键让板子冷启动，然后重新插回 USB-C。插回后点确定；我会等待串口恢复，并读取 POWER/LED/diag_log。"

$reappear = Wait-PortPresent -TargetPort $initialPort -TimeoutSeconds $PortReappearTimeoutSeconds
$postPort = $reappear.port
Add-Record ([PSCustomObject]@{
    phase = "replug"
    port_reappeared = [bool]$reappear.observed
    post_port = $postPort
    last_ports = @($reappear.last_ports)
})
if (-not $reappear.observed -or [string]::IsNullOrWhiteSpace($postPort)) {
    Add-ErrorRecord "serial port did not reappear after reboot/replug."
} else {
    Show-CountdownWindow `
        -Title "等待 USB/固件稳定" `
        -Message "串口已恢复为 $postPort。等待 $PostReplugSettleSeconds 秒后抓取 retained diag 和当前状态。" `
        -Seconds $PostReplugSettleSeconds

    $postCapture = Invoke-SerialCapture -SerialPortName $postPort -Label "03-post-replug-diag" -Commands @(
        "~POWER:STATUS",
        "~LED:STATUS",
        "~DEVICE:SETTINGS",
        "~DIAGLOG:LAST:180:power",
        "~DIAGLOG:LAST:180:status_led",
        "~DIAGLOG:LAST:80:health"
    ) -InitialReadMs 5000 -CommandReadMs 1800
    $postAnalysis = Analyze-Capture -Path $postCapture -Label "post_replug_diag"
    Add-Record ([PSCustomObject]@{
        phase = "post_replug_capture"
        analysis = $postAnalysis
    })
    if (-not $postAnalysis.saw_shutdown_evidence) {
        Add-ErrorRecord "post-replug diagnostics did not include obvious shutdown evidence; see $postCapture"
    }
}

$result = if ($script:Errors.Count -gt 0) {
    "FAIL"
} elseif (@($operatorResults | Where-Object { $_ -eq "SKIP" -or $_ -eq "ABORT" }).Count -gt 0) {
    "REVIEW"
} else {
    "PASS"
}

Write-Summary -Result $result -InitialPort $initialPort -PostPort $postPort
Add-Transcript ("summary={0}" -f $summaryJsonPath)
Add-Transcript ("summary_md={0}" -f $summaryMdPath)

Show-TopMostMessageBox `
    -Title "低功耗验收向导完成：$result" `
    -Message "结果：$result`r`n`r`n证据目录：$OutputDir`r`nsummary：$summaryMdPath`r`ntranscript：$transcriptPath`r`n`r`n把弹窗关掉后我会继续看这些记录。"

Write-Host ("result={0}`nsummary={1}`ntranscript={2}" -f $result, $summaryMdPath, $transcriptPath)
if ($result -eq "FAIL") {
    exit 1
}
exit 0
