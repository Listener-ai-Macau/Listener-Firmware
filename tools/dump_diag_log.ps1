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
$command = "~DIAGLOG:DUMP`n"

Write-Host "Dumping diag_log from $Port to $outputFile ..."

try {
    $port = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
    $port.ReadTimeout = 5000
    $port.WriteTimeout = 5000
    $port.Open()

    # Drain any pending data
    Start-Sleep -Milliseconds 500
    while ($port.BytesToRead -gt 0) {
        $port.ReadByte() | Out-Null
    }

    # Send dump command
    $port.WriteLine($command)

    # Read output until timeout
    $output = @()
    $readStart = Get-Date
    while (((Get-Date) - $readStart).TotalSeconds -lt 10) {
        try {
            $line = $port.ReadLine()
            if ($line -match '^\{') {
                $output += $line
            }
        } catch [System.TimeoutException] {
            break
        }
    }

    $port.Close()
} catch {
    if ($port -and $port.IsOpen) {
        $port.Close()
    }
    throw
}

if ($output.Count -eq 0) {
    Write-Host "No diag_log events found."
    exit 0
}

$output | Set-Content -Path $outputFile -Encoding UTF8
Write-Host "Dumped $($output.Count) events to $outputFile"
