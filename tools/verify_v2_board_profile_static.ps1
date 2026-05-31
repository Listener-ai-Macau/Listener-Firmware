param()

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$errors = @()

function Read-RepoFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $path = Join-Path $projectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $script:errors += "missing file: $RelativePath"
        return ""
    }
    return Get-Content -Raw -LiteralPath $path
}

function Add-CheckError {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:errors += $Message
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Text -notmatch $Pattern) {
        Add-CheckError "missing ${Description}: $Pattern"
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Text -match $Pattern) {
        Add-CheckError "stale ${Description}: $Pattern"
    }
}

function Get-PartitionSize {
    param(
        [Parameter(Mandatory = $true)][string]$PartitionsText,
        [Parameter(Mandatory = $true)][string]$Name
    )
    foreach ($line in ($PartitionsText -split "`r?`n")) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            continue
        }
        $parts = @($trimmed -split ",")
        if ($parts.Count -lt 5 -or $parts[0].Trim() -ne $Name) {
            continue
        }
        $size = $parts[4].Trim()
        if ($size -match "^0x([0-9A-Fa-f]+)$") {
            return [Convert]::ToInt64($Matches[1], 16)
        }
        return [int64]$size
    }
    Add-CheckError "missing partition: $Name"
    return 0
}

$boardPins = Read-RepoFile "ports\esp32\board_pins\include\board_pins.h"
$sdkconfig = Read-RepoFile "sdkconfig.defaults.esp32s3"
$partitions = Read-RepoFile "partitions.csv"
$boardKconfig = Read-RepoFile "ports\esp32\board_pins\Kconfig.projbuild"
$listenerDevice = Read-RepoFile "protocols\listener_device\include\listener_device.h"
$keyboard = Read-RepoFile "components\keyboard\keyboard.c"
$voiceRecordingControl = Read-RepoFile "components\voice_recording_control\voice_recording_control.c"
$voiceKeyInput = Read-RepoFile "ports\esp32\voice_key_input\voice_key_input_esp32.c"
$boardHelp = Read-RepoFile "components\board\board.c"
$powerManager = Read-RepoFile "components\power_manager\power_manager.c"
$otaPackage = Read-RepoFile "tools\package_ota_firmware.ps1"
$factoryPackage = Read-RepoFile "tools\package_factory_firmware.ps1"

foreach ($item in @(
    @($boardKconfig, "default LISTENER_BOARD_PROFILE_N4", "default N4 board profile"),
    @($boardKconfig, "LISTENER_BOARD_PROFILE_N4", "N4 board profile option"),
    @($boardPins, 'BOARD_PINS_PROFILE_ID\s+"voice-keyboard-n4"', "N4 board profile id"),
    @($boardPins, 'BOARD_PINS_MODULE\s+"ESP32-S3-WROOM-1-N4"', "N4 module id"),
    @($boardPins, "BOARD_PINS_FLASH_SIZE_MB\s+\(4\)", "4 MB flash board metadata"),
    @($boardPins, "BOARD_PINS_PSRAM_SIZE_MB\s+\(0\)", "no PSRAM board metadata"),
    @($boardPins, 'BOARD_PINS_PSRAM_MODE\s+"none"', "no PSRAM board mode"),
    @($boardPins, "BOARD_PINS_KEY1_IO\s+\(GPIO_NUM_45\)", "KEY1 GPIO45"),
    @($boardPins, "BOARD_PINS_KEY2_IO\s+\(GPIO_NUM_48\)", "KEY2 GPIO48"),
    @($boardPins, "BOARD_PINS_KEY3_IO\s+\(GPIO_NUM_47\)", "KEY3 GPIO47"),
    @($boardPins, "BOARD_PINS_KEY4_IO\s+\(GPIO_NUM_21\)", "KEY4 GPIO21"),
    @($boardPins, "BOARD_PINS_EC11_A_IO\s+\(GPIO_NUM_36\)", "EC11-A GPIO36"),
    @($boardPins, "BOARD_PINS_EC11_B_IO\s+\(GPIO_NUM_38\)", "EC11-B GPIO38"),
    @($boardPins, "BOARD_PINS_EC11_C_IO\s+\(GPIO_NUM_37\)", "EC11-C GPIO37"),
    @($boardPins, "BOARD_PINS_EC11_KEY_IO\s+\(GPIO_NUM_35\)", "EC11 key GPIO35"),
    @($boardPins, "BOARD_PINS_I2S_BCLK_IO\s+\(GPIO_NUM_39\)", "I2S BCLK GPIO39"),
    @($boardPins, "BOARD_PINS_I2S_WS_IO\s+\(GPIO_NUM_40\)", "I2S WS GPIO40"),
    @($boardPins, "BOARD_PINS_I2S_DIN_IO\s+\(GPIO_NUM_41\)", "I2S DIN GPIO41"),
    @($listenerDevice, 'LISTENER_DEVICE_HW_REV\s+"esp32s3-wroom-1-n4"', "N4 BLE/DIS hardware revision"),
    @($listenerDevice, "flash_4mb;no_psram", "N4 capability metadata"),
    @($keyboard, "key2\.gpio48\.w", "N4 KEY2 diagnostic label"),
    @($keyboard, "key4\.gpio21\.s", "N4 KEY4 diagnostic label"),
    @($voiceKeyInput, 'VOICE_KEY_INPUT_DIRECT_LABEL\s+"key1\.gpio45\.voice"', "N4 KEY1 voice diagnostic label"),
    @($voiceRecordingControl, "voice_key_input_get_active_source\(\)", "voice recovery source follows active key source"),
    @($boardHelp, "Voice Keyboard N4", "N4 board help text"),
    @($boardHelp, "KEY1/GPIO45", "N4 voice key help text"),
    @($powerManager, "POWER_MANAGER_WAKE_POLICY_KEY4_ONLY", "N4 KEY4 wake policy"),
    @($powerManager, "KEY4/GPIO21", "N4 wake key string"),
    @($powerManager, "KEY1/GPIO45 voice key", "N4 voice key wake limitation"),
    @($otaPackage, 'hardware_revision = "keyboard-n4"', "OTA package N4 hardware requirement"),
    @($factoryPackage, 'hardware_revision = "esp32s3-wroom-1-n4"', "factory package N4 DIS hardware revision")
)) {
    Assert-Contains -Text $item[0] -Pattern $item[1] -Description $item[2]
}

foreach ($token in @(
    "CONFIG_LISTENER_BOARD_PROFILE_N4=y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y",
    'CONFIG_ESPTOOLPY_FLASHSIZE="4MB"',
    "# CONFIG_SPIRAM is not set"
)) {
    Assert-Contains -Text $sdkconfig -Pattern ([regex]::Escape($token)) -Description "sdkconfig token $token"
}

foreach ($item in @(
    @($boardPins, "N16R8", "N16R8 board metadata in active board pins"),
    @($sdkconfig, "CONFIG_SPIRAM=y", "PSRAM-enabled N4 default"),
    @($sdkconfig, "CONFIG_ESPTOOLPY_FLASHSIZE_16MB", "16 MB flash N4 default"),
    @($voiceKeyInput, "gpio11\.ec11_key", "V2 GPIO11 EC11 diagnostic label in active code"),
    @($voiceKeyInput, "gpio35\.ec11_key", "N4 EC11 key diagnostic label in active voice key code"),
    @($voiceRecordingControl, "ec11_key_hold", "EC11 recovery source label while KEY1 is the voice key"),
    @($keyboard, "key1\.gpio38\.d", "V2 KEY1 diagnostic label in active code"),
    @($keyboard, "key1\.gpio45\.d", "KEY1 HID diagnostic label while KEY1 is the voice key")
)) {
    Assert-NotContains -Text $item[0] -Pattern $item[1] -Description $item[2]
}

$ota0 = Get-PartitionSize -PartitionsText $partitions -Name "ota_0"
$ota1 = Get-PartitionSize -PartitionsText $partitions -Name "ota_1"
$diagLog = Get-PartitionSize -PartitionsText $partitions -Name "diag_log"
if ($ota0 -lt 0x1B0000 -or $ota1 -lt 0x1B0000) {
    Add-CheckError "OTA app partitions must be at least 0x1B0000 bytes for the 4 MB N4 baseline."
}
if ($diagLog -ne 0x80000) {
    Add-CheckError "diag_log partition must be 0x80000 bytes for the 4 MB N4 baseline."
}

if ($errors.Count -gt 0) {
    Write-Host "FAIL: N4 board profile static verification failed"
    $errors | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host "PASS: N4 board profile, memory defaults, pin map, partitions, package identity, and stale V2 guard checks passed."
