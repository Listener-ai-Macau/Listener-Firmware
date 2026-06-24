[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$IdleSeconds = 70,
    [int]$RotateSeconds = 10,
    [string]$OutputDir = "",
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$AgentName = "codex"
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$serialCaptureScript = Join-Path $PSScriptRoot "send_serial_and_capture.ps1"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".cache\validation\idle-ec11-interaction-guided-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$transcriptPath = Join-Path $OutputDir "guided-transcript.txt"
Set-Content -LiteralPath $transcriptPath -Value @() -Encoding UTF8

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)

    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $transcriptPath -Value $line -Encoding UTF8
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

function Show-TimedNotice {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [int]$Seconds = 10
    )

    Add-Transcript ("timed_prompt title=[{0}] {1} seconds={2} message={3}" -f $AgentName, $Title, $Seconds, ($Message -replace "`r?`n", " / "))
    if ($NoPrompt.IsPresent) {
        return
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] $Title"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 720
    $form.Height = 240
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.Left = 16
    $label.Top = 16
    $label.Width = 660
    $label.Height = 100
    $label.Text = $Message
    $form.Controls.Add($label)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.Left = 16
    $countdown.Top = 132
    $countdown.Width = 420
    $countdown.Height = 30
    $countdown.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($countdown)

    $closeButton = [System.Windows.Forms.Button]::new()
    $closeButton.Text = "马上继续"
    $closeButton.Left = 540
    $closeButton.Top = 130
    $closeButton.Width = 120
    $closeButton.Add_Click({ $form.Close() })
    $form.Controls.Add($closeButton)

    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Tag = [PSCustomObject]@{
        Deadline = (Get-Date).AddSeconds($Seconds)
        Countdown = $countdown
        Form = $form
    }
    $timer.Interval = 100
    $timer.Add_Tick({
        param($sender, $eventArgs)
        $state = $sender.Tag
        $remaining = [Math]::Max(0, [int][Math]::Ceiling(($state.Deadline - (Get-Date)).TotalSeconds))
        $state.Countdown.Text = "$remaining 秒后自动继续"
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
    }
}

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port.ToUpperInvariant()
    }

    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($ports -contains "COM10") {
        return "COM10"
    }
    if ($ports.Count -eq 1) {
        return $ports[0].ToUpperInvariant()
    }
    throw "Port was not provided and COM10 was not detected. Available ports: $($ports -join ', ')"
}

function Add-LiveLine {
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

    try {
        $text = $Serial.ReadExisting()
        if (-not [string]::IsNullOrEmpty($text)) {
            foreach ($line in ($text -split "`r?`n")) {
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    Add-LiveLine -Lines $Lines -Text $line
                }
            }
        }
    } catch {
        Add-LiveLine -Lines $Lines -Text ("READ_ERROR {0}" -f $_.Exception.Message)
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
        [int]$ReadMs = 900
    )

    Add-LiveLine -Lines $Lines -Text ("> {0}" -f $Command)
    $Serial.Write(("{0}`n" -f $Command))
    Read-LiveSerialFor -Serial $Serial -Lines $Lines -Milliseconds $ReadMs
}

function Show-LiveRotateWindow {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$Lines,
        [Parameter(Mandatory = $true)][int]$Seconds
    )

    Add-Transcript ("rotate_window_start seconds={0}" -f $Seconds)
    if ($NoPrompt.IsPresent) {
        Read-LiveSerialFor -Serial $Serial -Lines $Lines -Milliseconds ($Seconds * 1000)
        Add-Transcript "rotate_window_end no_prompt=1"
        return
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] 插电 idle：EC11 连续旋转采集"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 820
    $form.Height = 300
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $titleLabel = [System.Windows.Forms.Label]::new()
    $titleLabel.Left = 16
    $titleLabel.Top = 14
    $titleLabel.Width = 760
    $titleLabel.Height = 30
    $titleLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12, [System.Drawing.FontStyle]::Bold)
    $titleLabel.Text = "请现在连续旋转 EC11 旋钮"
    $form.Controls.Add($titleLabel)

    $instruction = [System.Windows.Forms.TextBox]::new()
    $instruction.Left = 16
    $instruction.Top = 56
    $instruction.Width = 760
    $instruction.Height = 115
    $instruction.Multiline = $true
    $instruction.ReadOnly = $true
    $instruction.Text = "在倒计时结束前一直转，顺时针或逆时针都可以。`r`n预期：EC11 旋钮白色旋转反馈应持续，不应转到一半灯效消失；状态灯 LED3-LED6 不应全亮；按键/边框不应乱亮。"
    $form.Controls.Add($instruction)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.Left = 16
    $countdown.Top = 190
    $countdown.Width = 520
    $countdown.Height = 32
    $countdown.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 13, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($countdown)

    $closeButton = [System.Windows.Forms.Button]::new()
    $closeButton.Text = "提前结束"
    $closeButton.Left = 630
    $closeButton.Top = 188
    $closeButton.Width = 140
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
        Add-Transcript "rotate_window_end no_prompt=0"
    }
}

function Show-RotateObservationForm {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$TimeoutSeconds = 30
    )

    if ($NoPrompt.IsPresent) {
        Set-Content -LiteralPath $Path -Value "operator_observation=no_prompt" -Encoding UTF8
        Add-Transcript "operator_observation skipped no_prompt=1"
        return
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] EC11 idle 旋转观察记录"
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.Width = 760
    $form.Height = 360
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $title = [System.Windows.Forms.Label]::new()
    $title.Left = 16
    $title.Top = 16
    $title.Width = 700
    $title.Height = 30
    $title.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12, [System.Drawing.FontStyle]::Bold)
    $title.Text = "刚才 idle 倒计时窗口期间，请勾选真实观察结果"
    $form.Controls.Add($title)

    $didRotate = [System.Windows.Forms.CheckBox]::new()
    $didRotate.Left = 24
    $didRotate.Top = 62
    $didRotate.Width = 680
    $didRotate.Height = 28
    $didRotate.Text = "我确实连续旋转了 EC11 旋钮"
    $form.Controls.Add($didRotate)

    $sawFeedback = [System.Windows.Forms.CheckBox]::new()
    $sawFeedback.Left = 24
    $sawFeedback.Top = 96
    $sawFeedback.Width = 680
    $sawFeedback.Height = 28
    $sawFeedback.Text = "EC11 白色旋转灯能持续跟随旋转"
    $form.Controls.Add($sawFeedback)

    $statusAllOn = [System.Windows.Forms.CheckBox]::new()
    $statusAllOn.Left = 24
    $statusAllOn.Top = 130
    $statusAllOn.Width = 680
    $statusAllOn.Height = 28
    $statusAllOn.Text = "状态灯 LED3-LED6 又全亮或异常亮起"
    $form.Controls.Add($statusAllOn)

    $notesLabel = [System.Windows.Forms.Label]::new()
    $notesLabel.Left = 24
    $notesLabel.Top = 170
    $notesLabel.Width = 680
    $notesLabel.Height = 24
    $notesLabel.Text = "补充说明（可空）："
    $form.Controls.Add($notesLabel)

    $notes = [System.Windows.Forms.TextBox]::new()
    $notes.Left = 24
    $notes.Top = 198
    $notes.Width = 690
    $notes.Height = 58
    $notes.Multiline = $true
    $form.Controls.Add($notes)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.Left = 24
    $countdown.Top = 266
    $countdown.Width = 420
    $countdown.Height = 24
    $form.Controls.Add($countdown)

    $ok = [System.Windows.Forms.Button]::new()
    $ok.Text = "提交"
    $ok.Left = 590
    $ok.Top = 282
    $ok.Width = 120
    $ok.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.AcceptButton = $ok
    $form.Controls.Add($ok)

    $observationState = @{ AutoTimeout = $false }
    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Tag = [PSCustomObject]@{
        Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        Countdown = $countdown
        Form = $form
        ObservationState = $observationState
    }
    $timer.Interval = 100
    $timer.Add_Tick({
        param($sender, $eventArgs)
        $state = $sender.Tag
        $remaining = [Math]::Max(0, [int][Math]::Ceiling(($state.Deadline - (Get-Date)).TotalSeconds))
        $state.Countdown.Text = "$remaining 秒后自动提交"
        if ((Get-Date) -ge $state.Deadline) {
            $state.ObservationState.AutoTimeout = $true
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
    }

    $result = @(
        "auto_timeout={0}" -f ($(if ($observationState.AutoTimeout) { "1" } else { "0" }))
        "did_rotate={0}" -f ($(if ($didRotate.Checked) { "1" } else { "0" }))
        "saw_feedback={0}" -f ($(if ($sawFeedback.Checked) { "1" } else { "0" }))
        "status_all_on={0}" -f ($(if ($statusAllOn.Checked) { "1" } else { "0" }))
        "notes={0}" -f (($notes.Text -replace "`r?`n", " / "))
    )
    $result | Set-Content -LiteralPath $Path -Encoding UTF8
    Add-Transcript ("operator_observation {0}" -f ($result -join " "))
    $form.Dispose()
}

$resolvedPort = Resolve-TestPort
Add-Transcript ("start port={0} output_dir={1} idle_seconds={2} rotate_seconds={3}" -f $resolvedPort, $OutputDir, $IdleSeconds, $RotateSeconds)

$setupPath = Join-Path $OutputDir "01-setup-before-idle.txt"
& $serialCaptureScript `
    -Port $resolvedPort `
    -Baud $Baud `
    -CommandList "~DIAGLOG:CLEAR;;~DIAGLOG:INPUTDBG:OFF;;~DEVICE:SET low_power_idle_ms=60000;;~DEVICE:SET plugged_auto_shutdown_ms=0;;~POWER:STATUS;;~LED:STATUS" `
    -InitialReadMs 1200 `
    -CommandReadMs 1400 `
    -OutputPath $setupPath

Show-TimedNotice `
    -Title "准备进入插电 idle" `
    -Message "请先不要操作设备。我会等待 $IdleSeconds 秒让它进入插电 idle；等待结束会再次响铃，并弹出连续旋转 EC11 的倒计时窗口。" `
    -Seconds 10

Start-Sleep -Seconds $IdleSeconds
Add-Transcript ("idle_wait_complete seconds={0}" -f $IdleSeconds)

$capturePath = Join-Path $OutputDir "02-idle-ec11-rotate-live.txt"
$observationPath = Join-Path $OutputDir "03-operator-observation.txt"
$lines = [System.Collections.Generic.List[string]]::new()
$serial = [System.IO.Ports.SerialPort]::new($resolvedPort, $Baud)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-LiveLine -Lines $lines -Text ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $resolvedPort, $Baud)
    Read-LiveSerialFor -Serial $serial -Lines $lines -Milliseconds 700
    Show-LiveRotateWindow -Serial $serial -Lines $lines -Seconds $RotateSeconds
    Show-RotateObservationForm -Path $observationPath
    Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~POWER:STATUS" -ReadMs 1200
    Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~LED:STATUS" -ReadMs 1600
    Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:LAST:180:keyboard" -ReadMs 1600
    Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:LAST:220:status_led" -ReadMs 1800
    Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:LAST:120:power" -ReadMs 1400
    Send-LiveSerialCommand -Serial $serial -Lines $lines -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 700
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    Add-LiveLine -Lines $lines -Text "serial_closed"
    $lines | Set-Content -LiteralPath $capturePath -Encoding UTF8
}

Add-Transcript ("capture_done path={0}" -f $capturePath)
Write-Host ("output_dir={0}" -f $OutputDir)
Write-Host ("transcript={0}" -f $transcriptPath)
Write-Host ("setup={0}" -f $setupPath)
Write-Host ("capture={0}" -f $capturePath)
Write-Host ("observation={0}" -f $observationPath)
