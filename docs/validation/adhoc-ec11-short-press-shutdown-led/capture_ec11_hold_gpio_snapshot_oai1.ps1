param(
    [string]$Port = "COM10",
    [int]$Baud = 115200,
    [string]$OutputPath = ".\docs\validation\adhoc-ec11-short-press-shutdown-led\ec11-hold-gpio-snapshot-oai1.txt",
    [int]$PrepareSeconds = 5,
    [int]$HoldSeconds = 12,
    [int]$PressSettleMilliseconds = 1200,
    [int]$PollMilliseconds = 300,
    [int]$ConfirmTimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$resolvedOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$outputDir = Split-Path -Parent $resolvedOutput
if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

$lines = [System.Collections.Generic.List[string]]::new()

function Add-Log {
    param([string]$Text)
    $line = "{0:o} {1}" -f (Get-Date), $Text
    $lines.Add($line) | Out-Null
    $line | Out-Host
}

function Read-Available {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$DurationMs
    )
    $deadline = (Get-Date).AddMilliseconds($DurationMs)
    while ((Get-Date) -lt $deadline) {
        try {
            $text = $Serial.ReadExisting()
            if ($text.Length -gt 0) {
                foreach ($entry in ($text -split "`r?`n")) {
                    if ($entry.Trim().Length -gt 0) {
                        Add-Log $entry
                    }
                }
            }
        } catch {
            Add-Log ("READ_ERROR {0}" -f $_.Exception.Message)
        }
        Start-Sleep -Milliseconds 20
    }
}

function Send-Command {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [int]$ReadMs = 600
    )
    Add-Log ("> {0}" -f $Command)
    $Serial.Write("$Command`n")
    if ($ReadMs -gt 0) {
        Read-Available -Serial $Serial -DurationMs $ReadMs
    }
}

function New-HoldForm {
    param([int]$Seconds)

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "EC11 按住态 GPIO 采集中"
    $form.StartPosition = "CenterScreen"
    $form.TopMost = $true
    $form.Width = 620
    $form.Height = 240
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.AutoSize = $false
    $label.Left = 22
    $label.Top = 18
    $label.Width = 560
    $label.Height = 122
    $label.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12)
    $label.Text = "准备采集 EC11 中键按住态。开始后请按住 Listener 设备上的 EC11 圆形旋钮中键，不要旋转，不要按 KEY1-KEY4，也不要点电脑键盘。"
    $form.Controls.Add($label)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.AutoSize = $false
    $countdown.Left = 22
    $countdown.Top = 152
    $countdown.Width = 560
    $countdown.Height = 34
    $countdown.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 14, [System.Drawing.FontStyle]::Bold)
    $countdown.Text = "剩余 {0} 秒" -f $Seconds
    $form.Controls.Add($countdown)

    return [pscustomobject]@{
        Form = $form
        Label = $label
        Countdown = $countdown
    }
}

function Show-TimedConfirmation {
    param([int]$TimeoutSeconds)

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "确认 EC11 按住动作"
    $form.StartPosition = "CenterScreen"
    $form.TopMost = $true
    $form.Width = 560
    $form.Height = 210
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.AutoSize = $false
    $label.Left = 20
    $label.Top = 18
    $label.Width = 500
    $label.Height = 82
    $label.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12)
    $label.Text = "刚才倒计时期间，你是否确实按住了 EC11 圆形旋钮中键，并一直按到倒计时结束？"
    $form.Controls.Add($label)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.AutoSize = $false
    $countdown.Left = 20
    $countdown.Top = 102
    $countdown.Width = 500
    $countdown.Height = 28
    $countdown.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $form.Controls.Add($countdown)

    $yes = [System.Windows.Forms.Button]::new()
    $yes.Left = 250
    $yes.Top = 138
    $yes.Width = 120
    $yes.Height = 34
    $yes.Text = "是，按住了"
    $form.Controls.Add($yes)

    $no = [System.Windows.Forms.Button]::new()
    $no.Left = 386
    $no.Top = 138
    $no.Width = 120
    $no.Height = 34
    $no.Text = "没有/不确定"
    $form.Controls.Add($no)

    $script:oai1Ec11ConfirmResult = "Timeout"
    $script:oai1Ec11ConfirmRemaining = $TimeoutSeconds
    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Interval = 1000
    $timer.Add_Tick({
        $script:oai1Ec11ConfirmRemaining -= 1
        $countdown.Text = "未选择会在 {0} 秒后自动关闭" -f [Math]::Max(0, $script:oai1Ec11ConfirmRemaining)
        if ($script:oai1Ec11ConfirmRemaining -le 0) {
            $timer.Stop()
            $form.Close()
        }
    })
    $yes.Add_Click({
        $script:oai1Ec11ConfirmResult = "Yes"
        $timer.Stop()
        $form.Close()
    })
    $no.Add_Click({
        $script:oai1Ec11ConfirmResult = "No"
        $timer.Stop()
        $form.Close()
    })

    $countdown.Text = "未选择会在 {0} 秒后自动关闭" -f $script:oai1Ec11ConfirmRemaining
    $timer.Start()
    $form.ShowDialog() | Out-Null
    $timer.Dispose()
    return $script:oai1Ec11ConfirmResult
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 50
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-Log ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Read-Available -Serial $serial -DurationMs 4500

    Send-Command -Serial $serial -Command "~DIAGLOG:ENABLE voice_key" -ReadMs 700
    Send-Command -Serial $serial -Command "~DIAGLOG:ENABLE keyboard" -ReadMs 700
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:ON" -ReadMs 700
    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700

    $ui = New-HoldForm -Seconds $HoldSeconds
    $ui.Form.Show()
    $ui.Form.Activate()
    for ($prep = $PrepareSeconds; $prep -gt 0; $prep -= 1) {
        $ui.Countdown.Text = "{0} 秒后开始按住采集" -f $prep
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 1000
    }

    Add-Log "PROMPT_HOLD_CAPTURE_START"
    $ui.Label.Text = "现在请按住 Listener 设备上的 EC11 圆形旋钮中键，并一直按到倒计时结束。不要旋转，不要按 KEY1-KEY4，也不要点电脑键盘。"
    $settleEnd = (Get-Date).AddMilliseconds($PressSettleMilliseconds)
    while ((Get-Date) -lt $settleEnd) {
        $ui.Countdown.Text = "请先按稳 EC11 中键，即将读取 GPIO"
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 60
    }
    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700

    $start = Get-Date
    $end = $start.AddSeconds($HoldSeconds)
    $scanSent = $false
    $nextGpioPoll = (Get-Date)
    while ((Get-Date) -lt $end) {
        if (-not $scanSent) {
            $scanSent = $true
            Add-Log "GPIO_SCAN_DURING_HOLD begin"
            Send-Command -Serial $serial -Command "~BOARD:GPIO-SCAN" -ReadMs 0
            $nextGpioPoll = (Get-Date).AddMilliseconds(5600)
        } elseif ((Get-Date) -ge $nextGpioPoll) {
            Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 0
            $nextGpioPoll = (Get-Date).AddMilliseconds($PollMilliseconds)
        }

        $remaining = [Math]::Max(0, [Math]::Ceiling(($end - (Get-Date)).TotalSeconds))
        $ui.Countdown.Text = "保持按住，剩余 {0} 秒" -f $remaining
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 60
    }
    $ui.Form.Close()
    Add-Log "HOLD_CAPTURE_WINDOW_DONE"

    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:240:voice_key" -ReadMs 1800
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:200:keyboard" -ReadMs 1800
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 700

    $result = Show-TimedConfirmation -TimeoutSeconds $ConfirmTimeoutSeconds
    Add-Log ("OPERATOR_CONFIRMED_HELD {0}" -f $result)
} catch {
    Add-Log ("SCRIPT_ERROR {0}" -f $_.Exception.Message)
    throw
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    Add-Log "serial_closed"
    $lines | Set-Content -LiteralPath $resolvedOutput -Encoding UTF8
    Write-Host ("WROTE {0}" -f $resolvedOutput)
}
