param(
    [string]$LogPath = "",
    [string]$Port = "",
    [int]$LiveSeconds = 0,
    [int]$Baud = 115200,
    [int]$Tail = 80,
    [Alias("Events")]
    [int]$EventCount = 80,
    [int]$ScanLines = 4000,
    [switch]$RawTailOnly,
    [switch]$ListPorts
)

$ErrorActionPreference = "Stop"

function Write-Section {
    param([string]$Title)
    Write-Host ""
    Write-Host "== $Title =="
}

function Write-KeyValue {
    param(
        [string]$Key,
        [object]$Value
    )
    Write-Host ("{0}: {1}" -f $Key, $Value)
}

function Get-SerialPorts {
    [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
}

function Read-SerialLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SerialPort,
        [Parameter(Mandatory = $true)]
        [int]$Seconds,
        [Parameter(Mandatory = $true)]
        [int]$SerialBaud
    )

    $pythonPath = (Get-Command python -ErrorAction Stop).Path
    $script = @"
import sys
import time
import serial

port = r"$SerialPort"
baud = $SerialBaud
seconds = $Seconds

ser = serial.Serial()
ser.port = port
ser.baudrate = baud
ser.timeout = 0.2
ser.dsrdtr = False
ser.rtscts = False
ser.dtr = False
ser.rts = False
ser.open()
try:
    deadline = time.time() + seconds
    chunks = []
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            chunks.append(data)
    if chunks:
        sys.stdout.write(b"".join(chunks).decode("utf-8", errors="replace"))
    else:
        sys.stdout.write("<no serial output>\n")
finally:
    ser.close()
"@
    return @($script | & $pythonPath -)
}

function Get-RecentLines {
    param(
        [string[]]$Lines,
        [int]$Count
    )
    if ($Lines.Count -le $Count) {
        return @($Lines)
    }
    return @($Lines | Select-Object -Last $Count)
}

function Select-LastMatchingLine {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    $match = $Lines | Select-String -Pattern $Pattern | Select-Object -Last 1
    if ($match) { return $match.Line }
    return ""
}

function Select-Count {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    return @($Lines | Select-String -Pattern $Pattern).Count
}

Write-Section "Firmware log source"
Write-KeyValue "Repo" (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Write-KeyValue "Known serial ports" ((Get-SerialPorts) -join ", ")

if ($ListPorts) {
    exit 0
}

$sourceLabel = ""
$allLines = @()

if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
    if (-not (Test-Path -LiteralPath $LogPath)) {
        throw "Log file not found: $LogPath"
    }
    $sourceLabel = "file: $LogPath"
    $allLines = @(Get-Content -LiteralPath $LogPath)
} elseif (-not [string]::IsNullOrWhiteSpace($Port) -and $LiveSeconds -gt 0) {
    $sourceLabel = "serial: $Port for ${LiveSeconds}s @ $Baud"
    $allLines = @(Read-SerialLog -SerialPort $Port -Seconds $LiveSeconds -SerialBaud $Baud)
} else {
    Write-Host "No log source selected."
    Write-Host "Use -LogPath <file>, or -Port COMx -LiveSeconds <seconds>, or -ListPorts."
    exit 0
}

Write-KeyValue "Source" $sourceLabel
Write-KeyValue "TotalLines" $allLines.Count

if ($RawTailOnly) {
    $allLines | Select-Object -Last $Tail
    exit 0
}

$scan = Get-RecentLines -Lines $allLines -Count $ScanLines

Write-Section "Summary"
$patterns = [ordered]@{
    "Errors" = "\bE \(|\bERROR\b|ESP_ERROR_CHECK failed|Guru Meditation|panic|abort\(\)|assert failed|LoadProhibited|StoreProhibited|IllegalInstruction"
    "Warnings" = "\bW \(|\bWARN\b|warning"
    "Boot lines" = "boot:|rst:|ESP-ROM|project_version|app_desc|chip revision|flash size|SPIRAM|psram"
    "BLE/HID lines" = "ble_hid|BLE|GATT|HID|notify|connection|pair|bond"
    "Audio lines" = "audio|I2S|SPH0645|mic|pcm|packet|stream|voice"
    "Voice key lines" = "voice key|voice_key|VREC|GPIO35|ec11_key|button|key"
}

foreach ($entry in $patterns.GetEnumerator()) {
    Write-KeyValue $entry.Key (Select-Count -Lines $scan -Pattern $entry.Value)
}

$summaryPatterns = [ordered]@{
    "Last reset/boot" = "rst:|boot:|ESP-ROM|project_version|app_desc"
    "Last crash" = "ESP_ERROR_CHECK failed|Guru Meditation|panic|abort\(\)|assert failed|LoadProhibited|StoreProhibited|IllegalInstruction|Backtrace:"
    "Last memory issue" = "ESP_ERR_NO_MEM|NO_MEM|heap|mbuf|ENOMEM|out of memory|SPIRAM|psram"
    "Last BLE/HID event" = "ble_hid|BLE|GATT|HID|notify|connection|pair|bond"
    "Last audio event" = "audio|I2S|SPH0645|mic|pcm|packet|stream"
    "Last voice-key event" = "voice key|voice_key|VREC|GPIO35|ec11_key|button|key"
}

foreach ($entry in $summaryPatterns.GetEnumerator()) {
    $line = Select-LastMatchingLine -Lines $scan -Pattern $entry.Value
    if (-not [string]::IsNullOrWhiteSpace($line)) {
        Write-KeyValue $entry.Key $line
    }
}

Write-Section "Recent warnings and errors"
$warningPattern = "\bE \(|\bW \(|\bERROR\b|\bWARN\b|ESP_ERROR_CHECK failed|Guru Meditation|panic|abort\(\)|assert failed|Backtrace:|ESP_ERR_NO_MEM|NO_MEM|ENOMEM"
$warnings = @($scan | Select-String -Pattern $warningPattern | Select-Object -Last $EventCount)
if ($warnings.Count -eq 0) {
    Write-Host "No recent warning/error lines in scanned window."
} else {
    $warnings | ForEach-Object { $_.Line }
}

Write-Section "Recent key events"
$keyPattern = "rst:|boot:|project_version|app_desc|SPIRAM|psram|ble_hid|BLE|GATT|HID|notify|connection|pair|bond|audio|I2S|SPH0645|mic|pcm|packet|stream|voice key|voice_key|VREC|GPIO35|ec11_key|button|key"
$events = @($scan | Select-String -Pattern $keyPattern | Select-Object -Last $EventCount)
if ($events.Count -eq 0) {
    Write-Host "No key events in scanned window."
} else {
    $events | ForEach-Object { $_.Line }
}

Write-Section "Tail"
$allLines | Select-Object -Last $Tail
