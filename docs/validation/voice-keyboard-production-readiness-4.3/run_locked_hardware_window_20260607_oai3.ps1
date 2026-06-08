param(
    [Parameter(Mandatory = $true)]
    [string]$Port
)

$ErrorActionPreference = "Continue"
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$artifactRoot = Join-Path $repoRoot "docs\validation\voice-keyboard-production-readiness-4.3"
$audioArtifactRoot = Join-Path $repoRoot "tests\artifacts\audio"
$physicalHidArtifact = Join-Path $artifactRoot "physical-custom-key-hid-20260607-oai3-rerun.md"

function Invoke-LoggedStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Body
    )

    Write-Host "=== $Name begin $(Get-Date -Format o) ==="
    $global:LASTEXITCODE = 0
    try {
        & $Body *>&1 | ForEach-Object { Write-Host $_ }
        $exitCode = if ($null -eq $global:LASTEXITCODE) { 0 } else { [int]$global:LASTEXITCODE }
        Write-Host "=== $Name exit_code=$exitCode $(Get-Date -Format o) ==="
        return $exitCode
    } catch {
        Write-Host "=== $Name exception: $($_.Exception.Message) ==="
        return 1
    }
}

Write-Host "hardware_window_agent=oai3"
Write-Host "hardware_window_port=$Port"
Write-Host "hardware_window_started_at=$(Get-Date -Format o)"

Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name, PNPDeviceID |
    Format-List

$captureBefore = Invoke-LoggedStep -Name "serial-capture-reset-before-flash" -Body {
    & (Join-Path $repoRoot "tools\capture_serial.ps1") -Port $Port -DurationSeconds 3 -ResetBeforeRead
}

if ($captureBefore -ne 0) {
    Write-Host "serial_capture_before_flash_failed=$captureBefore"
}

$flashExit = Invoke-LoggedStep -Name "flash-no-build" -Body {
    & (Join-Path $repoRoot "tools\flash.ps1") -Port $Port -NoBuild
}

if ($flashExit -ne 0) {
    Write-Host "hardware_window_result=FAIL"
    Write-Host "hardware_window_failure_class=hardware"
    Write-Host "hardware_window_failure=flash-no-build failed; device did not enter/respond as ESP32-S3 bootloader"
    exit $flashExit
}

$audioExit = Invoke-LoggedStep -Name "physical-key-audio-capture" -Body {
    & (Join-Path $repoRoot "tools\verify_audio_capture_session_end_to_end.ps1") `
        -Port $Port `
        -CaptureSeconds 5 `
        -PhysicalKey
}

if ($audioExit -ne 0) {
    Write-Host "hardware_window_result=FAIL"
    Write-Host "hardware_window_failure_class=hardware_or_physical_key"
    Write-Host "hardware_window_failure=physical-key audio capture did not pass"
    exit $audioExit
}

$hidExit = Invoke-LoggedStep -Name "physical-custom-key-hid" -Body {
    & (Join-Path $repoRoot "tools\verify_physical_custom_key_hid.ps1") `
        -Port $Port `
        -TimeoutSeconds 45 `
        -OutputPath $physicalHidArtifact
}

if ($hidExit -ne 0) {
    Write-Host "hardware_window_result=FAIL"
    Write-Host "hardware_window_failure_class=hardware_or_physical_key"
    Write-Host "hardware_window_failure=physical KEY1-KEY4 HID validation did not pass"
    exit $hidExit
}

Write-Host "hardware_window_result=PASS"
Write-Host "audio_artifacts=$audioArtifactRoot"
Write-Host "physical_hid_artifact=$physicalHidArtifact"
