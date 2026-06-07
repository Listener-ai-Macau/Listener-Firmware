[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$DurationSeconds = 6,
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
. (Join-Path $repoRoot "tools\idf_env.ps1")

$commands = @(
    "~OTA:STATUS",
    "~DIS:GATT",
    "~OTA:GATT",
    "~DIAG:GATT",
    "~BOARD:STATUS",
    "~POWER:STATUS",
    "~DIAGLOG:COUNT",
    "~DIAGLOG:LAST:32"
)

$text = (($commands -join "`n") + "`n")
$textBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($text))
$pythonPath = (Get-Command python -ErrorAction Stop).Path

@"
import base64
import sys
import time
import serial

port = r"$Port"
baud = $Baud
duration_seconds = $DurationSeconds
text = base64.b64decode("$textBase64").decode("utf-8")

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
    ser.write(text.encode("utf-8"))
    ser.flush()

    chunks = []
    deadline = time.time() + duration_seconds
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
"@ | & $pythonPath -
