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
    [switch]$KeepInputBetweenCommands,
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

$allCommands = @($Command | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if (-not [string]::IsNullOrWhiteSpace($CommandList)) {
    $allCommands += @($CommandList -split ";;" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}
$captureOnly = $allCommands.Count -eq 0 -and
    $PSBoundParameters.ContainsKey("CaptureSeconds") -and
    $CaptureSeconds -gt 0
if ($allCommands.Count -eq 0 -and -not $captureOnly) {
    throw "Provide -Command/-CommandList, or use -CaptureSeconds by itself for a bounded command-free capture."
}
foreach ($serialCommand in $allCommands) {
    $trimmed = $serialCommand.Trim()
    if ($trimmed -match '^~?DIAGLOG:DUMP$') {
        throw "Unbounded DIAGLOG:DUMP is unsafe during live validation. Use tools/dump_diag_log.ps1 explicitly, or request a bounded DIAGLOG:LAST:N:source tail."
    }
    if ($trimmed -match '^~?DIAGLOG:LAST:(\d+)$' -and [int]$Matches[1] -gt 128) {
        throw "Unfiltered DIAGLOG:LAST:$($Matches[1]) can block firmware logging long enough to trigger the 5 s task watchdog. Use DIAGLOG:LAST:N:source (for example LAST:96:ble_gap) or N <= 128."
    }
    # Control commands must never fall through to the HID text path.
    if ($trimmed -match '^(?i:(DIAGLOG|LED|POWER|DEVICE|OTA|BOARD|WDT|BOOT|VREC|EC11|KEY)(?::|$))' -and
        -not $trimmed.StartsWith('~')) {
        throw "Firmware control command '$trimmed' must start with '~'. Plain text is forwarded as HID input; use '~$trimmed' for the control plane."
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

if ($KeepInputBetweenCommands.IsPresent) {
    $args += "--keep-input-between-commands"
}

if ($captureOnly) {
    $args += @("--capture-only-ms", ([string]$CommandReadMs))
}

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
