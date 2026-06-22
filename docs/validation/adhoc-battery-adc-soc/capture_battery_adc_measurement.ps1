[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$Baud = 115200,
    [int]$InitialReadMs = 5000,
    [int]$CommandReadMs = 1600,
    [string]$MeasuredVoltage,
    [switch]$Calibrate
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

function Show-TopmostMeasurementPrompt {
    $form = New-Object System.Windows.Forms.Form
    $form.Text = "oai3 / Listener硬件电池ADC校准"
    $form.TopMost = $true
    $form.StartPosition = "CenterScreen"
    $form.Size = New-Object System.Drawing.Size(720, 330)
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = New-Object System.Windows.Forms.Label
    $label.AutoSize = $false
    $label.Location = New-Object System.Drawing.Point(20, 18)
    $label.Size = New-Object System.Drawing.Size(670, 170)
    $label.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 11)
    $label.Text = "agent=oai3，UI=Listener硬件电池ADC校准。现在请用万用表测 Listener 硬件 BAT_V_ADC/GPIO10 的 68K/68K 分压点到 GND 的电压。不是电池端总电压，不要按 KEY1-KEY4，也不要按 EC11。测到后在下面输入电压，单位 V，例如 2.105。固件会抓取 driver ADC、NVS DMM trim、最终 ADC、电池电压和电量；如果本脚本带 -Calibrate，会把该值写入固件 NVS trim。"
    $form.Controls.Add($label)

    $textBox = New-Object System.Windows.Forms.TextBox
    $textBox.Location = New-Object System.Drawing.Point(20, 205)
    $textBox.Size = New-Object System.Drawing.Size(180, 32)
    $textBox.Font = New-Object System.Drawing.Font("Consolas", 14)
    $textBox.Text = "2.105"
    $form.Controls.Add($textBox)

    $unitLabel = New-Object System.Windows.Forms.Label
    $unitLabel.AutoSize = $true
    $unitLabel.Location = New-Object System.Drawing.Point(210, 211)
    $unitLabel.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 11)
    $unitLabel.Text = "V"
    $form.Controls.Add($unitLabel)

    $okButton = New-Object System.Windows.Forms.Button
    $okButton.Text = "确定并抓取串口"
    $okButton.Location = New-Object System.Drawing.Point(430, 238)
    $okButton.Size = New-Object System.Drawing.Size(125, 34)
    $okButton.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.Controls.Add($okButton)

    $cancelButton = New-Object System.Windows.Forms.Button
    $cancelButton.Text = "取消"
    $cancelButton.Location = New-Object System.Drawing.Point(565, 238)
    $cancelButton.Size = New-Object System.Drawing.Size(90, 34)
    $cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancelButton)

    $form.AcceptButton = $okButton
    $form.CancelButton = $cancelButton
    $form.Add_Shown({
        $form.Activate()
        $textBox.SelectAll()
        $textBox.Focus()
    })

    Add-Line "PROMPT agent=oai3 ui=Listener硬件电池ADC校准 target=BAT_V_ADC/GPIO10_divider_to_GND action=measure_adc_voltage"
    $result = $form.ShowDialog()
    $value = $textBox.Text
    $form.Dispose()

    Add-Line ("PROMPT_RESULT {0} value={1}" -f $result, $value)
    if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
        throw "operator cancelled battery ADC measurement prompt"
    }
    return $value
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
        Start-Sleep -Milliseconds 80
    }
}

function Send-Command {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMs = $CommandReadMs
    )
    Add-Line ("> {0}" -f $Command)
    $Serial.Write(("{0}`n" -f $Command))
    Read-SerialFor -Serial $Serial -Milliseconds $ReadMs
}

function Get-LastMatchInt {
    param(
        [Parameter(Mandatory = $true)][string]$Pattern
    )
    $value = $null
    foreach ($line in $lines) {
        if ($line -match $Pattern) {
            $value = [int]$Matches[1]
        }
    }
    return $value
}

$measuredVoltage = $MeasuredVoltage
if ([string]::IsNullOrWhiteSpace($measuredVoltage)) {
    $measuredVoltage = Show-TopmostMeasurementPrompt
} else {
    Add-Line ("PROMPT_SKIPPED measured_voltage={0}" -f $measuredVoltage)
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-Line ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Read-SerialFor -Serial $serial -Milliseconds $InitialReadMs

    Send-Command -Serial $serial -Command "~BATTERY:STATUS"
    Send-Command -Serial $serial -Command "~BOARD:STATUS"
    $normalized = $measuredVoltage.Trim().Replace(",", ".")
    $parsedVoltage = 0.0
    $dmmAdcMv = $null
    if ([double]::TryParse($normalized, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$parsedVoltage)) {
        $dmmAdcMv = [int][Math]::Round($parsedVoltage * 1000.0)
        if ($Calibrate) {
            Send-Command -Serial $serial -Command ("~BATTERY:CAL:DMM {0}" -f $dmmAdcMv) -ReadMs 2200
            Send-Command -Serial $serial -Command "~BATTERY:STATUS"
            Send-Command -Serial $serial -Command "~BOARD:STATUS"
        } else {
            Add-Line ("CALIBRATION_SKIPPED dmm_adc_mv={0}" -f $dmmAdcMv)
        }
    } else {
        Add-Line ("CALIBRATION_SKIPPED invalid_dmm_voltage={0}" -f $measuredVoltage)
    }
    Send-Command -Serial $serial -Command "~POWER:STATUS"
    Send-Command -Serial $serial -Command "~LED:STATUS" -ReadMs 2200
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:120:ble_hid" -ReadMs 1800

    if ($null -ne $dmmAdcMv) {
        $firmwareAdcMv = Get-LastMatchInt -Pattern '\bbattery_adc_mv=(-?\d+)'
        $firmwareRawAdcMv = Get-LastMatchInt -Pattern '\bbattery_adc_raw_mv=(-?\d+)'
        $firmwareDriverAdcMv = Get-LastMatchInt -Pattern '\bbattery_adc_driver_mv=(-?\d+)'
        $firmwareTrimMv = Get-LastMatchInt -Pattern '\bbattery_adc_trim_mv=(-?\d+)'
        $firmwareCorrectionMv = Get-LastMatchInt -Pattern '\bbattery_adc_correction_mv=(-?\d+)'
        $batteryMv = Get-LastMatchInt -Pattern '\bbattery_mv=(\d+)'
        $batteryLevel = Get-LastMatchInt -Pattern '\bbattery_level=(\d+)'
        if ($null -ne $firmwareAdcMv) {
            $deltaMv = $firmwareAdcMv - $dmmAdcMv
            Add-Line ("COMPARE dmm_adc_mv={0} firmware_adc_mv={1} driver_adc_mv={2} raw_adc_mv={3} trim_mv={4} correction_alias_mv={5} delta_mv={6} battery_mv={7} battery_level={8}" -f $dmmAdcMv, $firmwareAdcMv, $firmwareDriverAdcMv, $firmwareRawAdcMv, $firmwareTrimMv, $firmwareCorrectionMv, $deltaMv, $batteryMv, $batteryLevel)
        } else {
            Add-Line ("COMPARE dmm_adc_mv={0} firmware_adc_mv=missing" -f $dmmAdcMv)
        }
    } else {
        Add-Line ("COMPARE skipped invalid_dmm_voltage={0}" -f $measuredVoltage)
    }
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
