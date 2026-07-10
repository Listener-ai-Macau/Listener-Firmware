[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [int]$IdleWaitSeconds = 70,
    [int]$CaptureSeconds = 55,
    [string]$OutputPath = "",
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputPath = Join-Path $repoRoot ".cache\validation\physical_ble_repair_guided_$stamp.log"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $repoRoot $OutputPath
}

$lines = [System.Collections.Generic.List[string]]::new()

function Add-Line {
    param([Parameter(Mandatory = $true)][string]$Text)
    $line = "{0} {1}" -f (Get-Date).ToString("o"), $Text
    Write-Host $line
    $lines.Add($line) | Out-Null
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
        Start-Sleep -Milliseconds 100
    }
}

function Invoke-SerialCommand {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMilliseconds = 1200
    )
    Add-Line ("> {0}" -f $Command)
    $Serial.Write(("{0}`n" -f $Command))
    Read-SerialFor -Serial $Serial -Milliseconds $ReadMilliseconds
}

function Show-OperatorPrompt {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message
    )
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    try {
        [System.Media.SystemSounds]::Exclamation.Play()
    } catch {
        Add-Line ("prompt_sound_error {0}" -f $_.Exception.Message)
    }
    Add-Line ("operator_prompt title={0} message={1}" -f $Title, ($Message -replace "`r?`n", " / "))

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = $Title
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.Size = [System.Drawing.Size]::new(640, 260)
    $form.MinimumSize = [System.Drawing.Size]::new(560, 240)
    $form.ShowInTaskbar = $true
    $form.TopMost = $true
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.AutoSize = $false
    $label.Location = [System.Drawing.Point]::new(24, 22)
    $label.Size = [System.Drawing.Size]::new(590, 120)
    $label.Text = $Message
    $label.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11)

    $okButton = [System.Windows.Forms.Button]::new()
    $okButton.Text = "确定，马上双击"
    $okButton.Size = [System.Drawing.Size]::new(150, 36)
    $okButton.Location = [System.Drawing.Point]::new(300, 164)
    $okButton.DialogResult = [System.Windows.Forms.DialogResult]::OK

    $cancelButton = [System.Windows.Forms.Button]::new()
    $cancelButton.Text = "取消"
    $cancelButton.Size = [System.Drawing.Size]::new(120, 36)
    $cancelButton.Location = [System.Drawing.Point]::new(466, 164)
    $cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel

    $form.Controls.Add($label)
    $form.Controls.Add($okButton)
    $form.Controls.Add($cancelButton)
    $form.AcceptButton = $okButton
    $form.CancelButton = $cancelButton

    $form.Add_Shown({
        $this.Activate()
        $this.BringToFront()
        $this.TopMost = $true
    })

    $result = $form.ShowDialog()
    $form.Dispose()
    Add-Line ("operator_prompt_result {0}" -f $result)
    return $result
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
    Invoke-SerialCommand -Serial $serial -Command "~POWER:STATUS" -ReadMilliseconds 900
    Invoke-SerialCommand -Serial $serial -Command "~LED:STATUS" -ReadMilliseconds 1200

    Add-Line ("idle_wait_start seconds={0}" -f $IdleWaitSeconds)
    $idleDeadline = (Get-Date).AddSeconds($IdleWaitSeconds)
    while ((Get-Date) -lt $idleDeadline) {
        Read-SerialFor -Serial $serial -Milliseconds 500
    }
    Add-Line "idle_wait_end"

    if (-not $NoPrompt.IsPresent) {
        $message = "点击确定后，马上双击一次 Listener 旋钮。`r`n`r`n只做这一步，然后不要再操作，也先不要点击 Windows 右下角连接弹窗（如果它还出现，先看着并告诉我）。预期是蓝牙灯和旋钮完整同步蓝色双闪三轮，随后 Type 自动静默重配对，不出现 Windows 连接失败。"
        $result = Show-OperatorPrompt -Title "Listener 双击重配对验证" -Message $message
        if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
            Add-Line "operator_cancelled"
            return
        }
    } else {
        Add-Line "operator_prompt_skipped"
    }

    Add-Line ("physical_double_click_capture_start seconds={0}" -f $CaptureSeconds)
    Read-SerialFor -Serial $serial -Milliseconds ($CaptureSeconds * 1000)
    Add-Line "physical_double_click_capture_end"

    Invoke-SerialCommand -Serial $serial -Command "~POWER:STATUS" -ReadMilliseconds 1200
    Invoke-SerialCommand -Serial $serial -Command "~DEVICE:SETTINGS" -ReadMilliseconds 1200
    Invoke-SerialCommand -Serial $serial -Command "~LED:STATUS" -ReadMilliseconds 1600
    Invoke-SerialCommand -Serial $serial -Command "~DIAGLOG:LAST:160:status_led" -ReadMilliseconds 1400
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    Add-Line "serial_closed"
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $lines | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host ("transcript={0}" -f (Resolve-Path -LiteralPath $OutputPath).Path)
}
