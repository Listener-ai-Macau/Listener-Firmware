[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$Baud = 115200,
    [int]$HoldSeconds = 7
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$lines = [System.Collections.Generic.List[string]]::new()

function Add-Line {
    param([Parameter(Mandatory = $true)][string]$Text)
    $line = "{0} {1}" -f (Get-Date).ToString("o"), $Text
    Write-Host $line
    $lines.Add($line) | Out-Null
}

function New-TopmostOwner {
    $owner = New-Object System.Windows.Forms.Form
    $owner.TopMost = $true
    $owner.ShowInTaskbar = $false
    $owner.StartPosition = "CenterScreen"
    $owner.Size = New-Object System.Drawing.Size(1, 1)
    $owner.Opacity = 0
    $owner.Show()
    $owner.Activate()
    return $owner
}

function Show-TopmostMessage {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Add-Line ("PROMPT {0}" -f $Message)
    $owner = New-TopmostOwner
    try {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $owner,
            $Message,
            $Title,
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Information)
        Add-Line ("PROMPT_RESULT {0}" -f $result)
    } finally {
        $owner.Close()
        $owner.Dispose()
    }
}

function Show-TopmostChoice {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Add-Line ("PROMPT_CHOICE {0}" -f $Message)
    $owner = New-TopmostOwner
    try {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $owner,
            $Message,
            $Title,
            [System.Windows.Forms.MessageBoxButtons]::YesNo,
            [System.Windows.Forms.MessageBoxIcon]::Question,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1)
        Add-Line ("PROMPT_CHOICE_RESULT {0}" -f $result)
        return $result
    } finally {
        $owner.Close()
        $owner.Dispose()
    }
}

function Show-TimedInstruction {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][int]$Seconds
    )
    Add-Line ("PROMPT_TIMED seconds={0} {1}" -f $Seconds, $Message)
    $form = New-Object System.Windows.Forms.Form
    $form.Text = $Title
    $form.TopMost = $true
    $form.StartPosition = "CenterScreen"
    $form.Size = New-Object System.Drawing.Size(620, 260)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = New-Object System.Windows.Forms.Label
    $label.AutoSize = $false
    $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $label.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 14)
    $label.Dock = [System.Windows.Forms.DockStyle]::Fill
    $form.Controls.Add($label)

    $remaining = $Seconds
    $timer = New-Object System.Windows.Forms.Timer
    $timer.Interval = 1000
    $timer.Add_Tick({
        $script:remainingForPrompt -= 1
        $label.Text = "{0}`r`n`r`n剩余 {1} 秒" -f $Message, $script:remainingForPrompt
        if ($script:remainingForPrompt -le 0) {
            $timer.Stop()
            $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
            $form.Close()
        }
    })
    $script:remainingForPrompt = $remaining
    $label.Text = "{0}`r`n`r`n剩余 {1} 秒" -f $Message, $remaining
    $form.Add_Shown({
        $form.Activate()
        $timer.Start()
    })
    [void]$form.ShowDialog()
    $timer.Dispose()
    $form.Dispose()
    Add-Line "PROMPT_TIMED_DONE"
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
                foreach ($line in ($text -split "`r?`n")) {
                    if (-not [string]::IsNullOrWhiteSpace($line)) {
                        Add-Line $line
                    }
                }
            }
        } catch {
            Add-Line ("READ_ERROR {0}" -f $_.Exception.Message)
            break
        }
        Start-Sleep -Milliseconds 50
    }
}

function Send-Command {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMs = 900
    )
    Add-Line ("> {0}" -f $Command)
    $Serial.Write(("{0}`n" -f $Command))
    Read-SerialFor -Serial $Serial -Milliseconds $ReadMs
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-Line ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Read-SerialFor -Serial $serial -Milliseconds 800

    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:ON" -ReadMs 700
    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~LED:STATUS" -ReadMs 900

    $targetUi = "Listener硬件EC11圆形旋钮中间（不是电脑界面，也不是 KEY1/KEY2/KEY3/KEY4）"
    $prepTitle = "oai3 / $targetUi"
    $prepMessage = "agent=oai3；UI=$targetUi。点确定后立刻用手按住圆形旋钮中间，不要旋转，不要按其他按键。"
    Show-TopmostMessage -Title $prepTitle -Message $prepMessage

    $holdTitle = "oai3 / 正在测试 $targetUi"
    $holdMessage = "agent=oai3；UI=$targetUi。现在持续按住圆形旋钮中间，并一直按住。观察 EC11 灯环是否出现关机确认灯效；不要松手，直到下一窗口提示松开。"
    Show-TimedInstruction -Title $holdTitle -Seconds $HoldSeconds -Message $holdMessage

    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~LED:STATUS" -ReadMs 900
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:120:voice_key" -ReadMs 1400
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:120:status_led" -ReadMs 1400

    $releaseTitle = "oai3 / 松开 $targetUi"
    $releaseMessage = "agent=oai3；UI=$targetUi。现在请松开圆形旋钮中间。松开后点确定。"
    Show-TopmostMessage -Title $releaseTitle -Message $releaseMessage

    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~LED:STATUS" -ReadMs 900
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:140:voice_key" -ReadMs 1400
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:140:status_led" -ReadMs 1400
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 700

    $confirmTitle = "oai3 / 操作确认 $targetUi"
    $confirmMessage = "agent=oai3；UI=$targetUi。刚才这轮测试中，你是否真的从倒计时开始按住这个旋钮中间，并一直按到提示松开？"
    $answer = Show-TopmostChoice -Title $confirmTitle -Message $confirmMessage
    Add-Line ("OPERATOR_CONFIRMED_HELD {0}" -f $answer)
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    Add-Line "serial_closed"
}

$parent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$lines | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host ("transcript={0}" -f (Resolve-Path -LiteralPath $OutputPath).Path)
