[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string[]]$Command = @(),
    [string]$CommandList = "",
    [int]$Baud = 115200,
    [int]$InitialReadMs = 800,
    [int]$CommandReadMs = 1200,
    [int]$CaptureSeconds = 0,
    [int]$WriteTimeoutMs = 5000,
    [int]$WriteRetries = 3,
    [int]$WriteRetryDelayMs = 250,
    [int]$CommandDelayMs = 250,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($PSBoundParameters.ContainsKey("CaptureSeconds")) {
    if ($CaptureSeconds -lt 0) {
        throw "-CaptureSeconds must be >= 0. Use -CommandReadMs for exact millisecond control."
    }
    if (-not $PSBoundParameters.ContainsKey("CommandReadMs")) {
        $CommandReadMs = $CaptureSeconds * 1000
    } else {
        Write-Warning "-CaptureSeconds ignored because -CommandReadMs was also provided."
    }
}

$helper = Join-Path $PSScriptRoot "serial_no_reset_capture.py"
if (-not (Test-Path -LiteralPath $helper)) {
    throw "Missing no-reset serial helper: $helper"
}

$args = @(
    $helper,
    "--port", $Port,
    "--baud", ([string]$Baud),
    "--initial-read-ms", ([string]$InitialReadMs),
    "--command-read-ms", ([string]$CommandReadMs),
    "--write-timeout-ms", ([string]$WriteTimeoutMs),
    "--write-retries", ([string]$WriteRetries),
    "--write-retry-delay-ms", ([string]$WriteRetryDelayMs),
    "--command-delay-ms", ([string]$CommandDelayMs)
)

foreach ($cmd in @($Command | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })) {
    $args += @("--command", $cmd)
}

if (-not [string]::IsNullOrWhiteSpace($CommandList)) {
    $args += @("--command-list", $CommandList)
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $args += @("--output-path", $OutputPath)
}

& python @args
if ($LASTEXITCODE -ne 0) {
    throw "serial no-reset capture failed with exit code $LASTEXITCODE"
}
