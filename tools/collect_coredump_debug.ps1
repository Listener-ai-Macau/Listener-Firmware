[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "COM3",
    [string]$CoreFile = "",
    [string]$OutputDir = "",
    [switch]$OpenDebugger,
    [switch]$AllowNoCoreDump
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".artifacts\coredump-debug\$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$stdoutPath = Join-Path $OutputDir "coredump-info.stdout.txt"
$stderrPath = Join-Path $OutputDir "coredump-info.stderr.txt"
$summaryPath = Join-Path $OutputDir "summary.json"
$command = @("coredump-info")
if (-not [string]::IsNullOrWhiteSpace($CoreFile)) {
    $command += @("-c", (Resolve-Path -LiteralPath $CoreFile).Path)
} else {
    $command = @("-p", $Port) + $command
}

if ($OpenDebugger.IsPresent) {
    $stdoutPath = Join-Path $OutputDir "coredump-debug.stdout.txt"
    $stderrPath = Join-Path $OutputDir "coredump-debug.stderr.txt"
    $command = @("coredump-debug")
    if (-not [string]::IsNullOrWhiteSpace($CoreFile)) {
        $command += @("-c", (Resolve-Path -LiteralPath $CoreFile).Path)
    } else {
        $command = @("-p", $Port) + $command
    }
}

$startedAt = Get-Date
Push-Location $repoRoot
try {
    $idf = Join-Path $PSScriptRoot "idf.ps1"
    & pwsh -NoProfile -File $idf @command > $stdoutPath 2> $stderrPath
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
$endedAt = Get-Date

$stdoutText = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -Raw -LiteralPath $stdoutPath } else { "" }
$stderrText = if (Test-Path -LiteralPath $stderrPath) { Get-Content -Raw -LiteralPath $stderrPath } else { "" }
$combined = "$stdoutText`n$stderrText"
$hasCrashEvidence = $combined -match "Guru Meditation|Backtrace:|Stack memory|Registers|ELF file SHA256|Crashed task|Tasks snapshot"
$noCore = $combined -match "No core dump|not found|does not exist|No such file|not available|no coredump|Core dump can't be read from flash since this option is not enabled"
$status = if ($exitCode -eq 0 -and $hasCrashEvidence) {
    "PASS"
} elseif ($AllowNoCoreDump.IsPresent -and $noCore) {
    "NO_COREDUMP"
} else {
    "FAIL"
}

[ordered]@{
    schema_version = 1
    tool = "collect_coredump_debug"
    status = $status
    exit_code = $exitCode
    started_at = $startedAt.ToString("o")
    ended_at = $endedAt.ToString("o")
    repo_root = $repoRoot
    port = $Port
    core_file = $CoreFile
    open_debugger = $OpenDebugger.IsPresent
    command = "pwsh -NoProfile -File .\tools\idf.ps1 $($command -join ' ')"
    stdout = $stdoutPath
    stderr = $stderrPath
    has_crash_evidence = [bool]$hasCrashEvidence
    no_coredump_detected = [bool]$noCore
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "coredump_debug_status=$status"
Write-Host "summary=$summaryPath"
Write-Host "stdout=$stdoutPath"
Write-Host "stderr=$stderrPath"

if ($status -eq "PASS" -or $status -eq "NO_COREDUMP") {
    exit 0
}
exit 1
