[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$ReadSeconds = 6,
    [string]$InputTranscript = "",
    [string]$OutputPath = "",
    [string]$OutputJson = "",
    [switch]$Scan,
    [switch]$RequirePort
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$errors = [System.Collections.Generic.List[string]]::new()
$evidence = [System.Collections.Generic.List[string]]::new()

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Read-RepoText {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $errors.Add("missing file: $RelativePath")
        return ""
    }
    return Get-Content -LiteralPath $path -Raw
}

function Add-Error {
    param([Parameter(Mandatory = $true)][string]$Message)
    $errors.Add($Message)
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Text -notmatch $Pattern) {
        Add-Error "missing ${Description}: $Pattern"
    } else {
        $evidence.Add("PASS: $Description")
    }
}

function Read-SerialUntil {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$SerialPort,
        [Parameter(Mandatory = $true)][datetime]$Deadline
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    while ((Get-Date) -lt $Deadline) {
        try {
            $line = $SerialPort.ReadLine().Trim()
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                $lines.Add($line)
            }
        } catch [System.TimeoutException] {
        }
    }
    return @($lines)
}

function Invoke-SerialKeyStatus {
    param([Parameter(Mandatory = $true)][string]$SerialPortName)

    $commands = @(
        "~BOARD:STATUS",
        "~BOARD:GPIO",
        "~DIAGLOG:INPUTDBG:STATUS",
        "~DIAGLOG:SOURCES",
        "~DIAGLOG:LAST:80:keyboard",
        "~EC11:STATUS"
    )
    if ($Scan.IsPresent) {
        $commands += "~BOARD:GPIO-SCAN"
    }

    $serialPort = $null
    $lines = [System.Collections.Generic.List[string]]::new()

    try {
        $serialPort = [System.IO.Ports.SerialPort]::new(
            $SerialPortName,
            $Baud,
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

        foreach ($command in $commands) {
            $lines.Add("> $command")
            $serialPort.Write("$command`n")
            $waitMs = if ($command -eq "~BOARD:GPIO-SCAN") {
                6500
            } else {
                [Math]::Max(500, [Math]::Min(1500, $ReadSeconds * 1000))
            }
            foreach ($line in @(Read-SerialUntil -SerialPort $serialPort -Deadline (Get-Date).AddMilliseconds($waitMs))) {
                $lines.Add($line)
            }
        }
    } finally {
        if ($serialPort -and $serialPort.IsOpen) {
            try {
                $serialPort.Close()
            } catch {
                # Non-critical: some USB bridges throw on close after transient disconnects.
                Write-Verbose "serial close failed: $($_.Exception.Message)"
            }
        }
    }

    return @($lines)
}

function Test-KeyStatusTranscript {
    param([Parameter(Mandatory = $true)][string]$TranscriptText)

    Assert-Contains -Text $TranscriptText -Pattern "~BOARD:STATUS" -Description "serial transcript includes board status"
    Assert-Contains -Text $TranscriptText -Pattern "~BOARD:GPIO .*key1_gpio=.*key2_gpio=.*key3_gpio=.*key4_gpio=.*ec11_key_gpio=" -Description "serial transcript includes key GPIO snapshot"
    Assert-Contains -Text $TranscriptText -Pattern "recording_key=EC11_KEY/GPIO18" -Description "serial transcript uses EC11 push GPIO18 recording-key contract"
    Assert-Contains -Text $TranscriptText -Pattern "DIAGLOG INPUTDBG|~DIAGLOG:SOURCE" -Description "serial transcript includes input debug or diagnostic source state"
    Assert-Contains -Text $TranscriptText -Pattern "EC11 rotation status|~EC11:STATUS|EC11" -Description "serial transcript includes EC11 status command response"
    if ($Scan.IsPresent) {
        Assert-Contains -Text $TranscriptText -Pattern "~BOARD:GPIO_SCAN begin" -Description "serial transcript includes GPIO scan begin"
        Assert-Contains -Text $TranscriptText -Pattern "~BOARD:GPIO_SCAN done" -Description "serial transcript includes GPIO scan completion"
    }
}

$board = Read-RepoText "components\board\board.c"
$keyboard = Read-RepoText "components\keyboard\keyboard.c"
$diagLog = Read-RepoText "components\diag_log\diag_log.c"
$ec11 = Read-RepoText "components\ec11_rotation_control\ec11_rotation_control.c"
$featureMap = Read-RepoText "docs\features\firmware-feature-map.md"

Assert-Contains -Text $board -Pattern "~BOARD:GPIO active_low=1 mode=read_as_configured reconfigure=0" -Description "~BOARD:GPIO non-destructive GPIO status"
Assert-Contains -Text $board -Pattern "key1_gpio=%d[\s\S]*key2_gpio=%d[\s\S]*key3_gpio=%d[\s\S]*key4_gpio=%d[\s\S]*ec11_key_gpio=%d" -Description "KEY1-KEY4 and EC11 key GPIO status fields"
Assert-Contains -Text $board -Pattern "recording_key=EC11_KEY/GPIO18" -Description "EC11 push GPIO18 recording-key identity"
Assert-Contains -Text $board -Pattern "~BOARD:GPIO_SCAN begin samples=%u interval_ms=%u mode=read_as_configured reconfigure=0" -Description "~BOARD:GPIO-SCAN bounded non-destructive scan"
Assert-Contains -Text $board -Pattern "~BOARD:GPIO_SCAN done change_mask=" -Description "~BOARD:GPIO-SCAN completion summary"
Assert-Contains -Text $keyboard -Pattern "custom key raw transition: logical=%s source=%s raw_high=%d stable_high=%d" -Description "custom key raw transition diagnostic"
Assert-Contains -Text $keyboard -Pattern "custom key stable transition: logical=%s source=%s raw_high=%d pressed=%d" -Description "custom key stable transition diagnostic"
Assert-Contains -Text $keyboard -Pattern "custom key fallback queued: logical=%s source=%s usage=F%u gesture=single" -Description "custom key fallback dispatch diagnostic"
Assert-Contains -Text $keyboard -Pattern "DIAG_KBD_CUSTOM_KEY" -Description "custom key flash diagnostic event"
Assert-Contains -Text $diagLog -Pattern "INPUTDBG:ON" -Description "input debug enable command"
Assert-Contains -Text $diagLog -Pattern "INPUTDBG:OFF" -Description "input debug disable command"
Assert-Contains -Text $diagLog -Pattern "LAST:" -Description "bounded diagnostic tail command"
Assert-Contains -Text $ec11 -Pattern "EC11 rotation status: action=%s" -Description "EC11 status command response"
Assert-Contains -Text $featureMap -Pattern "GPIO38/GPIO39/GPIO40/GPIO41" -Description "feature map documents KEY1-KEY4 GPIO identity"
Assert-Contains -Text $featureMap -Pattern "EC11 push/GPIO18" -Description "feature map documents EC11 recording key GPIO18"

$hardwareMode = "static-contract-only"
$transcript = @()

if (-not [string]::IsNullOrWhiteSpace($InputTranscript)) {
    $hardwareMode = "transcript-replay"
    $transcriptPath = Resolve-RepoPath -Path $InputTranscript
    if (-not (Test-Path -LiteralPath $transcriptPath)) {
        Add-Error "input transcript not found: $transcriptPath"
    } else {
        $transcript = @(Get-Content -LiteralPath $transcriptPath)
        Test-KeyStatusTranscript -TranscriptText ($transcript -join "`n")
    }
} elseif (-not [string]::IsNullOrWhiteSpace($Port)) {
    $hardwareMode = "serial-read"
    $transcript = @(Invoke-SerialKeyStatus -SerialPortName $Port)
    Test-KeyStatusTranscript -TranscriptText ($transcript -join "`n")
} elseif ($RequirePort.IsPresent) {
    Add-Error "Port or InputTranscript is required when -RequirePort is supplied."
}

$status = if ($errors.Count -eq 0) { "PASS" } else { "FAIL" }
$artifact = [ordered]@{
    schema_version = 1
    schema_id = "listener.firmware.key_status_validation.v1"
    generated_at = (Get-Date).ToString("o")
    repo_root = $repoRoot
    status = $status
    hardware_mode = $hardwareMode
    port = if ([string]::IsNullOrWhiteSpace($Port)) { $null } else { $Port }
    input_transcript = if ([string]::IsNullOrWhiteSpace($InputTranscript)) { $null } else { (Resolve-RepoPath -Path $InputTranscript) }
    baud = $Baud
    read_seconds = $ReadSeconds
    scan = [bool]$Scan.IsPresent
    evidence = @($evidence)
    errors = @($errors)
    transcript_line_count = @($transcript).Count
}

$markdownLines = [System.Collections.Generic.List[string]]::new()
$markdownLines.Add("# Firmware key status validation")
$markdownLines.Add("")
$markdownLines.Add("status=$status")
$markdownLines.Add("hardware_mode=$hardwareMode")
$markdownLines.Add("port=$($artifact.port)")
$markdownLines.Add("scan=$($artifact.scan)")
$markdownLines.Add("")
$markdownLines.Add("## Evidence")
foreach ($line in @($evidence)) {
    $markdownLines.Add("- $line")
}
if ($errors.Count -gt 0) {
    $markdownLines.Add("")
    $markdownLines.Add("## Errors")
    foreach ($errorItem in @($errors)) {
        $markdownLines.Add("- $errorItem")
    }
}
if (@($transcript).Count -gt 0) {
    $markdownLines.Add("")
    $markdownLines.Add("## Serial Transcript")
    $markdownLines.Add('```text')
    foreach ($line in @($transcript)) {
        $markdownLines.Add($line)
    }
    $markdownLines.Add('```')
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $resolvedOutputPath = Resolve-RepoPath -Path $OutputPath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputPath) | Out-Null
    Set-Content -LiteralPath $resolvedOutputPath -Value (($markdownLines -join "`n") + "`n") -Encoding UTF8
    Write-Output "artifact=$resolvedOutputPath"
}

if (-not [string]::IsNullOrWhiteSpace($OutputJson)) {
    $resolvedOutputJson = Resolve-RepoPath -Path $OutputJson
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputJson) | Out-Null
    ($artifact | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $resolvedOutputJson -Encoding UTF8
    Write-Output "json=$resolvedOutputJson"
}

if ($status -ne "PASS") {
    Write-Output "FAIL: firmware key status validation failed"
    foreach ($errorItem in @($errors)) {
        Write-Output " - $errorItem"
    }
    exit 1
}

Write-Output "PASS: firmware key status validation covers KEY1-KEY4 GPIO state, EC11 GPIO18 key identity, input debug diagnostics, bounded diagnostic tailing, and optional serial/transcript capture."
