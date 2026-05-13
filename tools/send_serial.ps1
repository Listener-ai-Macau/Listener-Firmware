param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [Parameter(Mandatory = $true)]
    [string]$Text,
    [int]$Baud = 115200,
    [int]$PostWriteDelayMs = 200
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$text_base64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))

@"
import base64
import time
import serial

port = r"$Port"
text = base64.b64decode("$text_base64").decode("utf-8")
baud = $Baud
post_write_delay_ms = $PostWriteDelayMs

ser = serial.Serial(port, baud, timeout=0.2)
try:
    ser.dtr = False
    ser.rts = False
    ser.write(text.encode("utf-8"))
    ser.flush()
    time.sleep(post_write_delay_ms / 1000.0)
finally:
    ser.close()
"@ | & $python_path -
