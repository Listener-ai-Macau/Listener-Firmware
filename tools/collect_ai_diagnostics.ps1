param(
    [string]$InputJsonl = "",
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$EventCount = 120,
    [int]$RecentEventCount = 0,
    [int]$ReadSeconds = 8,
    [string[]]$EnableSource = @(),
    [string[]]$Source = @(),
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
        [Parameter(Mandatory = $true)][string]$TranscriptPath,
        [Parameter(Mandatory = $true)][string]$SourceStatePath,
        [string[]]$TemporaryEnableSources = @(),
        [string[]]$TailSources = @()
    )

    $serialPort = $null
    $allLines = [System.Collections.Generic.List[string]]::new()
    $jsonLines = [System.Collections.Generic.List[string]]::new()
    $sourceLines = [System.Collections.Generic.List[string]]::new()

    function Read-UntilDeadline {
        param([Parameter(Mandatory = $true)][datetime]$Deadline)

        while ((Get-Date) -lt $Deadline) {
            try {
                $line = $serialPort.ReadLine().Trim()
                $allLines.Add($line)
                if ($line -match '^\s*\{') {
                    $jsonLines.Add($line)
                }
                if ($line -match '^~DIAGLOG:SOURCE\s+') {
                    $sourceLines.Add($line)
                }
            } catch [System.TimeoutException] {
            }
        }
    }

    function Send-DiagCommand {
        param(
            [Parameter(Mandatory = $true)][string]$Command,
            [int]$WaitMilliseconds = 700
        )

        $allLines.Add(("> ~DIAGLOG:{0}" -f $Command))
        $serialPort.Write(("~DIAGLOG:{0}`n" -f $Command))
        Read-UntilDeadline -Deadline (Get-Date).AddMilliseconds($WaitMilliseconds)
    }

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

        Send-DiagCommand -Command "SOURCES"
        foreach ($sourceName in @($TemporaryEnableSources)) {
            if (-not [string]::IsNullOrWhiteSpace($sourceName)) {
                Send-DiagCommand -Command ("ENABLE {0}" -f $sourceName)
            }
        }

        if (@($TailSources).Count -gt 0) {
            foreach ($sourceName in @($TailSources)) {
                if (-not [string]::IsNullOrWhiteSpace($sourceName)) {
                    Send-DiagCommand -Command ("LAST:{0}:{1}" -f $RecentEventCount, $sourceName) -WaitMilliseconds ($Seconds * 1000)
                }
            }
        } else {
            Send-DiagCommand -Command ("LAST:{0}" -f $RecentEventCount) -WaitMilliseconds ($Seconds * 1000)
        }
    } finally {
        if ($serialPort -and $serialPort.IsOpen) {
            foreach ($sourceName in @($TemporaryEnableSources)) {
                if (-not [string]::IsNullOrWhiteSpace($sourceName)) {
                    try {
                        Send-DiagCommand -Command ("DISABLE {0}" -f $sourceName)
                    } catch {
                        $allLines.Add("cleanup disable failed for ${sourceName}: $($_.Exception.Message)")
                    }
                }
            }
            try {
                Send-DiagCommand -Command "SOURCES"
            } catch {
                $allLines.Add("final sources failed: $($_.Exception.Message)")
            }
            $serialPort.Close()
        }
    }

    Set-Content -LiteralPath $TranscriptPath -Value @($allLines) -Encoding UTF8
    Set-Content -LiteralPath $SourceStatePath -Value @($sourceLines) -Encoding UTF8
    return @($jsonLines)
}

$hasInput = -not [string]::IsNullOrWhiteSpace($InputJsonl)
$hasPort = -not [string]::IsNullOrWhiteSpace($Port)
if ($hasInput -eq $hasPort) {
    throw "Provide exactly one source: -InputJsonl <file> for offline decode, or -Port COMx for serial collection."
}

if ($RecentEventCount -gt 0) {
    $EventCount = $RecentEventCount
}

$resolvedOutputDir = Resolve-RepoRelativePath -Path $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$rawJsonlPath = Join-Path $resolvedOutputDir "diag_log_raw.jsonl"
$decodedBundlePath = Join-Path $resolvedOutputDir "diag_log_ai_bundle.json"
$manifestPath = Join-Path $resolvedOutputDir "manifest.json"
$transcriptPath = Join-Path $resolvedOutputDir "serial_transcript.txt"
$sourceStatePath = Join-Path $resolvedOutputDir "diag_log_sources.txt"

$sourceMode = ""
$sourcePath = ""
if ($hasInput) {
    $sourceMode = "file"
    $sourcePath = Resolve-RepoRelativePath -Path $InputJsonl
    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "Input JSONL not found: $sourcePath"
    }
    Copy-Item -LiteralPath $sourcePath -Destination $rawJsonlPath -Force
    foreach ($stalePath in @($transcriptPath, $sourceStatePath)) {
        if (Test-Path -LiteralPath $stalePath) {
            Remove-Item -LiteralPath $stalePath -Force
        }
    }
} else {
    $sourceMode = "serial"
    $sourcePath = $Port
    $jsonLines = Get-JsonLinesFromSerial `
        -SerialPortName $Port `
        -SerialBaud $Baud `
        -RecentEventCount $EventCount `
        -Seconds $ReadSeconds `
        -TranscriptPath $transcriptPath `
        -SourceStatePath $sourceStatePath `
        -TemporaryEnableSources $EnableSource `
        -TailSources $Source
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
    source_state_path = if ($hasPort) { $sourceStatePath } else { $null }
    bounded_export = $true
    command_path = if ($hasPort) { "~DIAGLOG:SOURCES + ~DIAGLOG:ENABLE/DISABLE + ~DIAGLOG:LAST:N[:source]" } else { "offline decode" }
    recent_event_count = $EventCount
    source_filters = @($Source)
    temporary_enabled_sources = @($EnableSource)
    event_count = [int]$bundle.summary.event_count
    warning_error_count = @($bundle.summary.recent_warning_error_refs).Count
    boot_segment_count = [int]$bundle.summary.boot_segment_count
}

$manifestJson = $manifest | ConvertTo-Json -Depth 8
$manifestJson | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$manifestJson
