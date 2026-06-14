param(
    [string]$Port = "COM7",
    [string]$OutputDir = $PSScriptRoot
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$outputDirResolved = (Resolve-Path -LiteralPath $OutputDir).Path
$env:ARTIFACT_DIR = $outputDirResolved
$env:ARTIFACT_PORT = $Port

$python = @'
import json
import os
import sys
import time
from pathlib import Path

import serial

port = os.environ["ARTIFACT_PORT"]
out_dir = Path(os.environ["ARTIFACT_DIR"])
out_dir.mkdir(parents=True, exist_ok=True)
transcript = out_dir / "shutdown_serial_transcript.txt"
summary_path = out_dir / "shutdown_summary.json"
lines = []

def note(message):
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    line = f"[{stamp}] {message}"
    print(line)
    lines.append(line + "\n")

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.15
ser.write_timeout = 0.5
ser.dsrdtr = False
ser.rtscts = False
ser.dtr = False
ser.rts = False
opened = False
read_error = None

try:
    ser.open()
    opened = True
    ser.setDTR(False)
    ser.setRTS(False)
    note("serial_opened dtr=0 rts=0")
    time.sleep(0.5)

    for command, wait_s in [
        ("~POWER:STATUS\n", 2.0),
        ("~BOARD:STATUS\n", 2.0),
        ("~POWER:SHUTDOWN\n", 8.0),
    ]:
        note("tx " + command.strip())
        ser.write(command.encode("utf-8"))
        ser.flush()
        end = time.time() + wait_s
        while time.time() < end:
            try:
                data = ser.read(4096)
            except Exception as exc:
                read_error = repr(exc)
                note("serial_read_error " + read_error)
                raise
            if data:
                text = data.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                lines.append(text)
finally:
    if opened:
        try:
            ser.close()
            note("serial_closed")
        except Exception as exc:
            note("serial_close_error " + repr(exc))

    text = "".join(lines)
    transcript.write_text(text, encoding="utf-8", errors="replace")
    summary = {
        "port": port,
        "transcript": str(transcript),
        "read_error": read_error,
        "saw_power_status": "~POWER:STATUS" in text,
        "saw_board_status": "~BOARD:STATUS" in text,
        "saw_shutdown_entry": (
            "manual_command" in text
            or "hardware shutdown" in text
            or "POWER:SHUTDOWN" in text
        ),
        "saw_readback_mismatch": "readback mismatch" in text,
        "saw_restore_runtime_low": (
            "restoring runtime low" in text
            or "runtime-low restore" in text
        ),
        "saw_pwr_hold_high_request": (
            "requested_level=1" in text
            or "driven high for hardware shutdown" in text
            or "released high for hardware shutdown" in text
            or "release-high" in text
        ),
        "saw_pwr_hold_low_runtime": (
            "pwr_hold_level=low" in text
            or "requested_level=0 actual_level=0" in text
        ),
    }
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
'@

$python | python -
$pythonExit = $LASTEXITCODE

$pollLines = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt 24; $i++) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    if ($ports.Count -gt 0) {
        $portText = $ports -join ","
    } else {
        $portText = "<none>"
    }
    $line = "poll {0:00} {1:o} ports={2}" -f $i, (Get-Date), $portText
    Write-Host $line
    $pollLines.Add($line) | Out-Null
    Start-Sleep -Milliseconds 500
}

Set-Content -LiteralPath (Join-Path $outputDirResolved "port_poll_after_shutdown.txt") -Value $pollLines -Encoding UTF8

if ($pythonExit -ne 0) {
    throw "shutdown probe python step failed with exit code $pythonExit"
}
