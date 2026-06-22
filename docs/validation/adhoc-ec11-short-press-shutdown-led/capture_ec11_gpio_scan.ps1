[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$Baud = 115200
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
        [int]$ReadMs = 800
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
$serial.ReadBufferSize = 65536
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-Line ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Read-SerialFor -Serial $serial -Milliseconds 800
    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700

    Add-Line "> ~BOARD:GPIO-SCAN"
    $serial.Write("~BOARD:GPIO-SCAN`n")
    Show-Prompt `
        -Title "oai3 / Listener 硬件 UI：GPIO-SCAN" `
        -Seconds 5 `
        -Message "agent=oai3。扫描正在进行。请只操作 Listener 设备上的硬件 EC11 圆形旋钮中间按压键：连续按下/松开 2-3 次，不要旋转，不要按 KEY1-KEY4，也不是电脑窗口按钮。"
    Read-SerialFor -Serial $serial -Milliseconds 3500
    Send-Command -Serial $serial -Command "~BOARD:GPIO" -ReadMs 700
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
