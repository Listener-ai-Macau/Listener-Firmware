param()

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$dirty = @(& git -C $repoRoot status --porcelain=v1 --untracked-files=all 2>$null)
if ($dirty.Count -gt 0) {
    $preview = ($dirty | Select-Object -First 80) -join "`n"
    $hidden = if ($dirty.Count -gt 80) { "`n... $($dirty.Count - 80) more changed paths hidden" } else { "" }
    throw "Release hygiene requires a clean Listener Firmware worktree. Commit accepted changes, stash/delete unrelated work, or split pending work before release.`nDirty paths ($($dirty.Count)):`n$preview$hidden"
}

$checks = @(
    @{ Label = "release version"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "check_release_version.ps1")) },
    @{ Label = "OTA manifest validation tests"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "test_ota_manifest_validation.ps1")) },
    @{ Label = "OTA package release rules"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "test_ota_package_release_rules.ps1")) },
    @{ Label = "ESP-IDF environment wrapper"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_idf_env_static.ps1")) },
    @{ Label = "V2 board static contract"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_v2_board_profile_static.ps1")) },
    @{ Label = "power manager static contract"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_power_manager_static.py")) },
    @{ Label = "validation tooling shell hygiene"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_validation_tooling_hygiene.py")) },
    @{ Label = "status LED static contract"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_status_led_static.py")) },
    @{ Label = "shared audio leveling adapter"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_audio_leveling_platform_static.py")) },
    @{ Label = "BLE status LED sync contract"; File = "pwsh"; Args = @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_ble_status_led_connected_sync.ps1")) },
    @{ Label = "BLE rename recovery contract"; File = "python"; Args = @((Join-Path $PSScriptRoot "verify_ble_rename_recovery_static.py")) },
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
