<#
.SYNOPSIS
Capture a bounded diag_log tail from a device to a timestamped JSONL file.

.PARAMETER Port
Serial port (default: COM5)

.PARAMETER OutputDir
Directory for output files (default: tests/artifacts)

.PARAMETER Count
Recent event count for the normal bounded capture path.

.PARAMETER Source
Optional source name for firmware-side source-filtered tail capture.

.PARAMETER Full
Explicitly request the legacy unbounded dump. Normal AI validation should use
bounded LAST captures or tools/collect_ai_diagnostics.ps1 instead.
#>
param(
    [string]$Port = "COM5",
    [string]$OutputDir = "tests\artifacts",
    [int]$Count = 200,
    [string]$Source = "",
    [switch]$Full,
    [int]$ReadSeconds = 10
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputFile = Join-Path $OutputDir "diag_log_$timestamp.jsonl"

if ($Full) {
    $command = "~DIAGLOG:DUMP"
    Write-Warning "Using unbounded ~DIAGLOG:DUMP because -Full was specified. Prefer bounded LAST captures for AI validation."
} elseif (-not [string]::IsNullOrWhiteSpace($Source)) {
    $command = ("~DIAGLOG:LAST:{0}:{1}" -f $Count, $Source)
} else {
    $command = ("~DIAGLOG:LAST:{0}" -f $Count)
}

Write-Host "Capturing diag_log from $Port to $outputFile with $command ..."

$serialPort = $null
try {
    $serialPort = [System.IO.Ports.SerialPort]::new(
        $Port,
        115200,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One
    )
    $serialPort.ReadTimeout = 500
    $serialPort.WriteTimeout = 5000
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false
    $serialPort.Open()
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false

    Start-Sleep -Milliseconds 500
    while ($serialPort.BytesToRead -gt 0) {
        [void]$serialPort.ReadExisting()
        Start-Sleep -Milliseconds 20
    }

    $serialPort.Write("$command`n")

    $output = [System.Collections.Generic.List[string]]::new()
    $readDeadline = (Get-Date).AddSeconds($ReadSeconds)
    while ((Get-Date) -lt $readDeadline) {
        try {
            $line = $serialPort.ReadLine().Trim()
            if ($line -match '^\{') {
                $output.Add($line)
            }
        } catch [System.TimeoutException] {
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
Write-Host "Captured $($output.Count) events to $outputFile"
