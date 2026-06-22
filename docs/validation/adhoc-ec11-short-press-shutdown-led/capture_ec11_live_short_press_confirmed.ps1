param(
    [string]$Port = "COM10",
    [int]$Baud = 115200,
    [string]$OutputPath = ".\docs\validation\adhoc-ec11-short-press-shutdown-led\ec11-live-short-press-confirmed.txt",
    [int]$CaptureSeconds = 15,
    [int]$PrepareSeconds = 5,
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
    Read-Available -Serial $Serial -DurationMs $ReadMs
}

function New-CaptureForm {
    param([int]$Seconds)

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "EC11 短按采集中"
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
    $label.Height = 108
    $label.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12)
    $label.Text = "正在采集串口日志。请连续短按/松开 Listener 设备上的 EC11 圆形旋钮中键 3 次，尽量在倒计时一开始就按。不要旋转，不要按 KEY1-KEY4，也不要点电脑键盘。"
    $form.Controls.Add($label)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.AutoSize = $false
    $countdown.Left = 20
    $countdown.Top = 132
    $countdown.Width = 500
    $countdown.Height = 32
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
    $form.Text = "确认 EC11 人工动作"
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
    $label.Text = "刚才倒计时期间，你是否确实短按/松开了 EC11 圆形旋钮中键 2-3 次？"
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
    $yes.Text = "是，按了"
    $form.Controls.Add($yes)

    $no = [System.Windows.Forms.Button]::new()
    $no.Left = 386
    $no.Top = 138
    $no.Width = 120
    $no.Height = 34
    $no.Text = "没有/不确定"
    $form.Controls.Add($no)

    $result = "Timeout"
    $remaining = $TimeoutSeconds
    $timer = [System.Windows.Forms.Timer]::new()
    $timer.Interval = 1000
    $timer.Add_Tick({
        $script:remaining -= 1
        $countdown.Text = "未选择会在 {0} 秒后自动关闭" -f [Math]::Max(0, $script:remaining)
        if ($script:remaining -le 0) {
            $timer.Stop()
            $form.Close()
        }
    })
    $yes.Add_Click({
        $script:result = "Yes"
        $timer.Stop()
        $form.Close()
    })
    $no.Add_Click({
        $script:result = "No"
        $timer.Stop()
        $form.Close()
    })

    $script:remaining = $remaining
    $script:result = $result
    $countdown.Text = "未选择会在 {0} 秒后自动关闭" -f $script:remaining
    $timer.Start()
    $form.ShowDialog() | Out-Null
    $timer.Dispose()
    return $script:result
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

    $ui = New-CaptureForm -Seconds $CaptureSeconds
    $ui.Form.Show()
    $ui.Form.Activate()
    $ui.Label.Text = "准备采集 EC11 中键。倒计时结束后，请连续短按/松开 Listener 设备上的 EC11 圆形旋钮中键 3 次。不要旋转，不要按 KEY1-KEY4，也不要点电脑键盘。"
    for ($prep = $PrepareSeconds; $prep -gt 0; $prep -= 1) {
        $ui.Countdown.Text = "{0} 秒后开始采集" -f $prep
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 1000
    }
    Add-Log "PROMPT_AUTO_CAPTURE_START"
    $ui.Label.Text = "正在采集串口日志。请现在连续短按/松开 Listener 设备上的 EC11 圆形旋钮中键 3 次，尽量一开始就按。不要旋转，不要按 KEY1-KEY4，也不要点电脑键盘。"

    $scanRepeatCount = [Math]::Max(1, [int][Math]::Ceiling($CaptureSeconds / 5.0))
    $scanIndex = 0
    $nextScanAt = Get-Date
    $start = Get-Date
    $end = $start.AddSeconds($CaptureSeconds)
    while ((Get-Date) -lt $end) {
        if ($scanIndex -lt $scanRepeatCount -and (Get-Date) -ge $nextScanAt) {
            $scanIndex += 1
            Add-Log ("GPIO_SCAN_WINDOW index={0}/{1}" -f $scanIndex, $scanRepeatCount)
            Send-Command -Serial $serial -Command "~BOARD:GPIO-SCAN" -ReadMs 0
            $nextScanAt = (Get-Date).AddMilliseconds(5400)
        }
        $remaining = [Math]::Max(0, [Math]::Ceiling(($end - (Get-Date)).TotalSeconds))
        $ui.Countdown.Text = "剩余 {0} 秒" -f $remaining
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 80
    }
    $ui.Form.Close()
    Add-Log "CAPTURE_WINDOW_DONE"

    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:240:voice_key" -ReadMs 1800
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:200:keyboard" -ReadMs 1800
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 700

    $result = Show-TimedConfirmation -TimeoutSeconds $ConfirmTimeoutSeconds
    Add-Log ("OPERATOR_CONFIRMED_PRESS {0}" -f $result)
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    Add-Log "serial_closed"
    $lines | Set-Content -LiteralPath $resolvedOutput -Encoding UTF8
    Write-Host ("WROTE {0}" -f $resolvedOutput)
}
