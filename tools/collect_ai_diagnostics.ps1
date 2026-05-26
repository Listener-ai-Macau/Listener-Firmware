param(
    [string]$InputJsonl = "",
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$EventCount = 120,
    [int]$ReadSeconds = 8,
    [string]$OutputDir = "tests\artifacts\ai_diagnostics"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$decoder = Join-Path $PSScriptRoot "decode_diag_log.py"

function Resolve-RepoRelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Get-JsonLinesFromSerial {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$SerialBaud,
        [Parameter(Mandatory = $true)][int]$RecentEventCount,
        [Parameter(Mandatory = $true)][int]$Seconds,
        [Parameter(Mandatory = $true)][string]$TranscriptPath
    )

    $serialPort = $null
    $allLines = New-Object System.Collections.Generic.List[string]
    $jsonLines = New-Object System.Collections.Generic.List[string]
    try {
        $serialPort = [System.IO.Ports.SerialPort]::new(
            $SerialPortName,
            $SerialBaud,
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

        Start-Sleep -Milliseconds 300
        while ($serialPort.BytesToRead -gt 0) {
            [void]$serialPort.ReadExisting()
            Start-Sleep -Milliseconds 20
        }

        $serialPort.Write(("~DIAGLOG:LAST:{0}`n" -f $RecentEventCount))
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            try {
                $line = $serialPort.ReadLine().Trim()
                $allLines.Add($line)
                if ($line -match '^\s*\{') {
                    $jsonLines.Add($line)
                }
            } catch [System.TimeoutException] {
            }
        }
    } finally {
        if ($serialPort -and $serialPort.IsOpen) {
            $serialPort.Close()
        }
    }

    Set-Content -LiteralPath $TranscriptPath -Value @($allLines) -Encoding UTF8
    return @($jsonLines)
}

$hasInput = -not [string]::IsNullOrWhiteSpace($InputJsonl)
$hasPort = -not [string]::IsNullOrWhiteSpace($Port)
if ($hasInput -eq $hasPort) {
    throw "Provide exactly one source: -InputJsonl <file> for offline decode, or -Port COMx for serial collection."
}

$resolvedOutputDir = Resolve-RepoRelativePath -Path $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$rawJsonlPath = Join-Path $resolvedOutputDir "diag_log_raw.jsonl"
$decodedBundlePath = Join-Path $resolvedOutputDir "diag_log_ai_bundle.json"
$manifestPath = Join-Path $resolvedOutputDir "manifest.json"
$transcriptPath = Join-Path $resolvedOutputDir "serial_transcript.txt"

$sourceMode = ""
$sourcePath = ""
if ($hasInput) {
    $sourceMode = "file"
    $sourcePath = Resolve-RepoRelativePath -Path $InputJsonl
    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "Input JSONL not found: $sourcePath"
    }
    Copy-Item -LiteralPath $sourcePath -Destination $rawJsonlPath -Force
    if (Test-Path -LiteralPath $transcriptPath) {
        Remove-Item -LiteralPath $transcriptPath -Force
    }
} else {
    $sourceMode = "serial"
    $sourcePath = $Port
    $jsonLines = Get-JsonLinesFromSerial `
        -SerialPortName $Port `
        -SerialBaud $Baud `
        -RecentEventCount $EventCount `
        -Seconds $ReadSeconds `
        -TranscriptPath $transcriptPath
    Set-Content -LiteralPath $rawJsonlPath -Value @($jsonLines) -Encoding UTF8
    if (@($jsonLines).Count -eq 0) {
        throw "No JSON diag_log events captured from $Port. Transcript: $transcriptPath"
    }
}

$python = (Get-Command python -ErrorAction Stop).Path
& $python $decoder --input $rawJsonlPath --output $decodedBundlePath
if ($LASTEXITCODE -ne 0) {
    throw "decode_diag_log.py failed with exit code $LASTEXITCODE"
}

$bundle = Get-Content -LiteralPath $decodedBundlePath -Raw | ConvertFrom-Json
$manifest = [ordered]@{
    schema_version = 1
    schema_id = "listener.firmware.ai_diagnostics.collection.v1"
    source_mode = $sourceMode
    source = $sourcePath
    raw_jsonl_path = $rawJsonlPath
    decoded_bundle_path = $decodedBundlePath
    manifest_path = $manifestPath
    serial_transcript_path = if ($hasPort) { $transcriptPath } else { $null }
    event_count = [int]$bundle.summary.event_count
    warning_error_count = @($bundle.summary.recent_warning_error_refs).Count
    boot_segment_count = [int]$bundle.summary.boot_segment_count
}

$manifestJson = $manifest | ConvertTo-Json -Depth 8
$manifestJson | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$manifestJson
