<#
.SYNOPSIS
Dump diag_log from device to a timestamped file.

.PARAMETER Port
Serial port (default: COM5)

.PARAMETER OutputDir
Directory for output files (default: tests/artifacts)
#>
param(
    [string]$Port = "COM5",
    [string]$OutputDir = "tests\artifacts"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputFile = Join-Path $OutputDir "diag_log_$timestamp.jsonl"

# Send DIAGLOG:DUMP command and capture output
$command = "~DIAGLOG:DUMP"

Write-Host "Dumping diag_log from $Port to $outputFile ..."

try {
    $serialPort = [System.IO.Ports.SerialPort]::new(
        $Port,
        115200,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serialPort.ReadTimeout = 5000
    $serialPort.WriteTimeout = 5000
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false
    $serialPort.Open()
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false

    # Drain any pending data
    Start-Sleep -Milliseconds 500
    while ($serialPort.BytesToRead -gt 0) {
        $serialPort.ReadByte() | Out-Null
    }

    # Send dump command
    $serialPort.Write("$command`n")

    # Read output until timeout
    $output = @()
    $readStart = Get-Date
    while (((Get-Date) - $readStart).TotalSeconds -lt 10) {
        try {
            $line = $serialPort.ReadLine()
            if ($line -match '^\{') {
                $output += $line
            }
        } catch [System.TimeoutException] {
            break
        }
    }

    $serialPort.Close()
} catch {
    if ($serialPort -and $serialPort.IsOpen) {
        $serialPort.Close()
    }
    throw
}

if ($output.Count -eq 0) {
    Write-Host "No diag_log events found."
    exit 0
}

$output | Set-Content -Path $outputFile -Encoding UTF8
Write-Host "Dumped $($output.Count) events to $outputFile"
