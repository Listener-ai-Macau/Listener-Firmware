param(
    [string]$Port = "COM10",
    [int]$Baud = 115200,
    [string]$OutputPath = ".\docs\validation\adhoc-ec11-short-press-shutdown-led\ec11-wait-for-press-oai1.txt",
    [int]$MaxSeconds = 60,
    [int]$PollMilliseconds = 500
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
$detected = $false
$detectedReason = "timeout"

function Add-Log {
    param([string]$Text)
    $line = "{0:o} {1}" -f (Get-Date), $Text
    $lines.Add($line) | Out-Null
    $line | Out-Host

    if ($Text -match "ec11_key_level=low|ec11_key_pressed=1|EC11 push key level changed: source=ec11_key\.gpio18 raw_high=0|single-click custom fallback queued") {
        $script:detected = $true
        $script:detectedReason = $Text
    }
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

function New-WaitForm {
    param([int]$Seconds)

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "等待 EC11 短按"
    $form.StartPosition = "CenterScreen"
    $form.TopMost = $true
    $form.Width = 620
    $form.Height = 230
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.AutoSize = $false
    $label.Left = 22
    $label.Top = 18
    $label.Width = 560
    $label.Height = 118
    $label.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 12)
    $label.Text = "请现在短按/松开 Listener 设备上的 EC11 圆形旋钮中键。不要旋转，不要按 KEY1-KEY4，也不要点电脑键盘。检测到 GPIO18 低电平或 EC11 single-click 后会自动结束。"
    $form.Controls.Add($label)

    $countdown = [System.Windows.Forms.Label]::new()
    $countdown.AutoSize = $false
    $countdown.Left = 22
    $countdown.Top = 150
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

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 50
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-Log ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Read-Available -Serial $serial -DurationMs 2500

    Send-Command -Serial $serial -Command "~DIAGLOG:ENABLE voice_key" -ReadMs 700
    Send-Command -Serial $serial -Command "~DIAGLOG:ENABLE keyboard" -ReadMs 700
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:ON" -ReadMs 700
    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700

    $ui = New-WaitForm -Seconds $MaxSeconds
    $ui.Form.Show()
    $ui.Form.Activate()

    $start = Get-Date
    $end = $start.AddSeconds($MaxSeconds)
    $nextPoll = Get-Date
    while (-not $detected -and (Get-Date) -lt $end) {
        if ((Get-Date) -ge $nextPoll) {
            Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 0
            $nextPoll = (Get-Date).AddMilliseconds($PollMilliseconds)
        }
        $remaining = [Math]::Max(0, [Math]::Ceiling(($end - (Get-Date)).TotalSeconds))
        $ui.Countdown.Text = "等待 EC11 短按，剩余 {0} 秒" -f $remaining
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 80
    }

    if ($detected) {
        $ui.Label.Text = "已检测到 EC11 输入。"
        $ui.Countdown.Text = "检测成功"
        [System.Windows.Forms.Application]::DoEvents()
        Read-Available -Serial $serial -DurationMs 700
    }
    $ui.Form.Close()

    Add-Log ("DETECTION_RESULT detected={0} reason={1}" -f ($detected ? 1 : 0), $detectedReason)
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:240:voice_key" -ReadMs 1800
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:200:keyboard" -ReadMs 1800
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 700
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
