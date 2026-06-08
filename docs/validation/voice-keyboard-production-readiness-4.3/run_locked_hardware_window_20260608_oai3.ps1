param(
    [Parameter(Mandatory = $true)]
    [string]$Port
)

$ErrorActionPreference = "Continue"
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$artifactRoot = Join-Path $repoRoot "docs\validation\voice-keyboard-production-readiness-4.3"
$audioArtifactRoot = Join-Path $repoRoot "tests\artifacts\audio\voice-keyboard-production-readiness-4.3-20260608-oai3"
$physicalHidArtifact = Join-Path $artifactRoot "physical-custom-key-hid-20260608-oai3-rerun.md"
$bootloaderProbeScript = Join-Path $artifactRoot "run_bootloader_probe_20260607_oai3.ps1"

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

Set-Location -LiteralPath $repoRoot

Get-CimInstance Win32_SerialPort |
    Select-Object DeviceID, Name, PNPDeviceID |
    Format-List

$captureBefore = Invoke-LoggedStep -Name "serial-capture-reset-before-flash" -Body {
    & (Join-Path $repoRoot "tools\capture_serial.ps1") -Port $Port -DurationSeconds 3 -ResetBeforeRead
}

if ($captureBefore -ne 0) {
    Write-Host "serial_capture_before_flash_failed=$captureBefore"
}

$flashExit = Invoke-LoggedStep -Name "idf-flash" -Body {
    . (Join-Path $repoRoot "tools\idf_env.ps1")
    & idf.py -p $Port flash
}

if ($flashExit -ne 0) {
    Write-Host "hardware_window_result=FAIL"
    Write-Host "hardware_window_failure_class=hardware"
    Write-Host "hardware_window_failure=idf.py -p $Port flash failed; probing ESP32-S3 bootloader before releasing lock"
    if (Test-Path -LiteralPath $bootloaderProbeScript) {
        $probeExit = Invoke-LoggedStep -Name "bootloader-probe-after-flash-failure" -Body {
            & pwsh -NoProfile -File $bootloaderProbeScript -Port $Port
        }
        Write-Host "bootloader_probe_after_flash_failure_exit=$probeExit"
    }
    exit $flashExit
}

$serialToggleExit = Invoke-LoggedStep -Name "scripted-serial-toggle-audio-capture" -Body {
    New-Item -ItemType Directory -Force -Path $audioArtifactRoot | Out-Null
    & python (Join-Path $repoRoot "tools\verify_audio_capture_session_end_to_end.py") `
        --port $Port `
        --capture-seconds 5 `
        --trigger-mode serial-toggle `
        --artifacts-dir $audioArtifactRoot
}

if ($serialToggleExit -ne 0) {
    Write-Host "hardware_window_result=FAIL"
    Write-Host "hardware_window_failure_class=validation_or_runtime"
    Write-Host "hardware_window_failure=scripted serial-toggle audio capture did not pass"
    exit $serialToggleExit
}

$physicalAudioExit = Invoke-LoggedStep -Name "physical-key-audio-capture" -Body {
    & python (Join-Path $repoRoot "tools\verify_audio_capture_session_end_to_end.py") `
        --port $Port `
        --capture-seconds 5 `
        --trigger-mode physical-key `
        --artifacts-dir $audioArtifactRoot
}

if ($physicalAudioExit -ne 0) {
    Write-Host "hardware_window_result=FAIL"
    Write-Host "hardware_window_failure_class=hardware_or_physical_key"
    Write-Host "hardware_window_failure=physical-key audio capture did not pass"
    exit $physicalAudioExit
}

$hidExit = Invoke-LoggedStep -Name "physical-custom-key-hid" -Body {
    & pwsh -NoProfile -File (Join-Path $repoRoot "tools\verify_physical_custom_key_hid.ps1") `
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
