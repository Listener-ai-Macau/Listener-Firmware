param()

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$checkScript = Join-Path $PSScriptRoot "check_ota_manifest.ps1"
$testRoot = Join-Path $projectRoot ".cache\ota_manifest_validation_tests"

if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

$firmwarePath = Join-Path $testRoot "firmware_ota.bin"
[System.IO.File]::WriteAllBytes($firmwarePath, [byte[]](0x4C, 0x49, 0x53, 0x54, 0x45, 0x4E, 0x45, 0x52))
$firmwareHash = (Get-FileHash -LiteralPath $firmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
$firmwareSize = (Get-Item -LiteralPath $firmwarePath).Length

function New-BaseManifest {
    return [ordered]@{
        schema_version = 2
        created_at_utc = "2026-05-26T06:00:00Z"
        channel = "development"
        firmware = [ordered]@{
            project = "voice-keyboard-firmware"
            version = "1.0.0"
            git_commit = "0123456789abcdef0123456789abcdef01234567"
            git_dirty = $false
            target = "esp32s3"
            file = "firmware_ota.bin"
            size_bytes = $firmwareSize
            sha256 = $firmwareHash
        }
        requirements = [ordered]@{
            hardware_revision = "keyboard-v2-n16r8"
            protocol_version = 1
            min_desktop_version = "1.0.0"
        }
        ble_identity = [ordered]@{
            name = "listener"
            appearance = "0x03C1"
            hid_service_uuid = "1812"
            audio_service_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091a"
            audio_notify_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091b"
            readiness_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091c"
            capabilities_uuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091d"
            dis = [ordered]@{
                manufacturer = "listener"
                model = "keyboard-v2"
                hardware_revision = "esp32s3-wroom-1-n16r8"
                firmware_revision = "1.0.0"
                software_revision_protocol = "1"
            }
        }
        rollback = [ordered]@{
            supported = $true
            method = "esp_idf_bootloader_rollback"
            instructions = "Bootloader rolls back if pending verify is not marked valid."
        }
        recovery = [ordered]@{
            factory_reflash = "Use the factory package over USB serial."
            serial_commands = "~OTA:STATUS and ~DIAGLOG expose recovery diagnostics."
        }
    }
}

function Copy-ManifestObject {
    param([Parameter(Mandatory = $true)]$Manifest)
    return ($Manifest | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
}

function Write-Manifest {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Manifest
    )
    $path = Join-Path $testRoot "$Name.json"
    $Manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

function Invoke-ManifestCheck {
    param([Parameter(Mandatory = $true)][string]$ManifestPath)

    $output = @(& pwsh -NoProfile -File $checkScript -ManifestPath $ManifestPath 2>&1)
    return [PSCustomObject]@{
        ExitCode = $LASTEXITCODE
        Output = ($output -join "`n")
    }
}

function Assert-ManifestPasses {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Manifest
    )

    $path = Write-Manifest -Name $Name -Manifest $Manifest
    $result = Invoke-ManifestCheck -ManifestPath $path
    if ($result.ExitCode -ne 0) {
        throw "Expected '$Name' to pass but it failed:`n$($result.Output)"
    }
    Write-Host "PASS expected pass: $Name"
}

function Assert-ManifestFails {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$ExpectedText
    )

    $path = Write-Manifest -Name $Name -Manifest $Manifest
    $result = Invoke-ManifestCheck -ManifestPath $path
    if ($result.ExitCode -eq 0) {
        throw "Expected '$Name' to fail but it passed:`n$($result.Output)"
    }
    if ($result.Output -notlike "*$ExpectedText*") {
        throw "Expected '$Name' failure to mention '$ExpectedText' but saw:`n$($result.Output)"
    }
    Write-Host "PASS expected fail: $Name -> $ExpectedText"
}

$base = New-BaseManifest
Assert-ManifestPasses -Name "valid_manifest" -Manifest $base

$missingCreatedAt = Copy-ManifestObject $base
$missingCreatedAt.PSObject.Properties.Remove("created_at_utc")
Assert-ManifestFails -Name "missing_created_at_utc" -Manifest $missingCreatedAt -ExpectedText "Missing created_at_utc"

$missingBleIdentity = Copy-ManifestObject $base
$missingBleIdentity.PSObject.Properties.Remove("ble_identity")
Assert-ManifestFails -Name "missing_ble_identity" -Manifest $missingBleIdentity -ExpectedText "Missing ble_identity section"

$missingRollbackMethod = Copy-ManifestObject $base
$missingRollbackMethod.rollback.PSObject.Properties.Remove("method")
Assert-ManifestFails -Name "missing_rollback_method" -Manifest $missingRollbackMethod -ExpectedText "Missing rollback.method"

$missingRollbackInstructions = Copy-ManifestObject $base
$missingRollbackInstructions.rollback.PSObject.Properties.Remove("instructions")
Assert-ManifestFails -Name "missing_rollback_instructions" -Manifest $missingRollbackInstructions -ExpectedText "Missing rollback.instructions"

$rollbackOnlySupported = Copy-ManifestObject $base
$rollbackOnlySupported.rollback = [PSCustomObject]@{ supported = $true }
Assert-ManifestFails -Name "rollback_only_supported" -Manifest $rollbackOnlySupported -ExpectedText "Missing rollback.method"

$missingRecovery = Copy-ManifestObject $base
$missingRecovery.PSObject.Properties.Remove("recovery")
Assert-ManifestFails -Name "missing_recovery" -Manifest $missingRecovery -ExpectedText "Missing recovery section"

$missingRecoveryFactory = Copy-ManifestObject $base
$missingRecoveryFactory.recovery.PSObject.Properties.Remove("factory_reflash")
Assert-ManifestFails -Name "missing_recovery_factory_reflash" -Manifest $missingRecoveryFactory -ExpectedText "Missing recovery.factory_reflash"

$missingRecoverySerial = Copy-ManifestObject $base
$missingRecoverySerial.recovery.PSObject.Properties.Remove("serial_commands")
Assert-ManifestFails -Name "missing_recovery_serial_commands" -Manifest $missingRecoverySerial -ExpectedText "Missing recovery.serial_commands"

$tooLongVersion = Copy-ManifestObject $base
$tooLongVersion.firmware.version = "1.0.0-local-build-226-g99934ff-dirty"
Assert-ManifestFails -Name "too_long_firmware_version" -Manifest $tooLongVersion -ExpectedText "Invalid firmware.version"

Write-Host "PASS: OTA manifest validation negative coverage completed."
