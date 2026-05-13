param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$DurationSeconds = 8,
    [int]$Baud = 115200,
    [switch]$ResetBeforeRead
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$reset_before_read = if ($ResetBeforeRead.IsPresent) { "True" } else { "False" }

@"
import sys
import time
import serial

port = r"$Port"
baud = $Baud
duration_seconds = $DurationSeconds
reset_before_read = $reset_before_read

ser = serial.Serial(port, baud, timeout=0.2)
try:
    if reset_before_read:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.2)

    end = time.time() + duration_seconds
    chunks = []
    while time.time() < end:
        data = ser.read(4096)
        if data:
            chunks.append(data)

    if chunks:
        sys.stdout.write(b"".join(chunks).decode("utf-8", errors="replace"))
    else:
        sys.stdout.write("<no serial output>\n")
finally:
    ser.close()
"@ | & $python_path -
