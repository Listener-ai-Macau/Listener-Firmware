[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$Baud = 115200,
    [int]$HoldPromptSeconds = 5
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

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

function Show-Prompt {
    param(
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][string]$Title,
        [int]$Seconds = 0
    )
    Add-Line ("PROMPT {0}" -f $Message)
    $shell = New-Object -ComObject WScript.Shell
    $result = $shell.Popup($Message, $Seconds, $Title, 64)
    Add-Line ("PROMPT_RESULT {0}" -f $result)
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

    Show-Prompt `
        -Title "oai3 / Listener 硬件 UI：EC11 旋钮按压采集" `
        -Seconds $HoldPromptSeconds `
        -Message "agent=oai3。请操作 Listener 设备上的硬件 EC11 旋钮按压键：按住圆形旋钮中间，不要旋转，不要按 KEY1-KEY4。这个窗口会自动消失；消失后继续按住，直到下一窗口提示松开。"

    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~LED:STATUS" -ReadMs 900
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:80:voice_key" -ReadMs 1200
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:80:status_led" -ReadMs 1200

    Show-Prompt `
        -Title "oai3 / Listener 硬件 UI：EC11 旋钮按压采集" `
        -Seconds 3 `
        -Message "agent=oai3。请松开刚才按住的 Listener 硬件 EC11 旋钮按压键。松开后等待这个窗口关闭。"

    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
    Send-Command -Serial $serial -Command "~LED:STATUS" -ReadMs 900
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:100:voice_key" -ReadMs 1400
    Send-Command -Serial $serial -Command "~DIAGLOG:LAST:100:status_led" -ReadMs 1400
    Send-Command -Serial $serial -Command "~DIAGLOG:INPUTDBG:OFF" -ReadMs 700
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
