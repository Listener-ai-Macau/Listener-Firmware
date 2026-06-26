param()

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$checks = @(
    @{ Label = "release version"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "check_release_version.ps1")) },
    @{ Label = "OTA manifest validation tests"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "test_ota_manifest_validation.ps1")) },
    @{ Label = "OTA package release rules"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "test_ota_package_release_rules.ps1")) },
    @{ Label = "V2 board static contract"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_v2_board_profile_static.ps1")) },
    @{ Label = "power manager static contract"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_power_manager_static.py")) },
    @{ Label = "status LED static contract"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_status_led_static.py")) },
    @{ Label = "BLE status LED sync contract"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_ble_status_led_connected_sync.ps1")) },
    @{ Label = "diagnostic log coverage"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_diagnostic_log_coverage.ps1")) }
)

foreach ($check in $checks) {
    Write-Host ""
    Write-Host "=== $($check.Label) ==="
    & $check.File @($check.Args)
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "PASS: Listener Firmware release checks completed."
