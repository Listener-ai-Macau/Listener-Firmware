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
$voiceKeyInput = Read-RepoFile "ports\esp32\voice_key_input\voice_key_input_esp32.c"
$boardHelp = Read-RepoFile "components\board\board.c"
$powerManager = Read-RepoFile "components\power_manager\power_manager.c"
$batteryMonitor = Read-RepoFile "components\battery_monitor\battery_monitor.c"
$batteryMonitorHeader = Read-RepoFile "components\battery_monitor\include\battery_monitor.h"
$diagEvents = Read-RepoFile "components\diag_log\include\diag_log_events.h"
$diagPlatform = Read-RepoFile "ports\esp32\diag_log_platform\diag_log_flash.c"
$audioCapture = Read-RepoFile "ports\esp32\audio_capture\audio_capture_esp32.c"
$audioCaptureKconfig = Read-RepoFile "ports\esp32\audio_capture\Kconfig.projbuild"
$systemHealth = Read-RepoFile "ports\esp32\system_health_platform\system_health_esp32.c"
$otaPackage = Read-RepoFile "tools\package_ota_firmware.ps1"
$factoryPackage = Read-RepoFile "tools\package_factory_firmware.ps1"

foreach ($item in @(
    @($boardKconfig, "LISTENER_BOARD_PROFILE_V2_N16R8", "default V2/N16R8 board profile option"),
    @($boardKconfig, "LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE", "EC11 wake sign-off gate"),
    @($boardPins, 'BOARD_PINS_PROFILE_ID\s+"voice-keyboard-v2-n16r8"', "V2 board profile id"),
    @($boardPins, 'BOARD_PINS_MODULE\s+"ESP32-S3-WROOM-1-N16R8"', "N16R8 module id"),
    @($boardPins, "BOARD_PINS_FLASH_SIZE_MB\s+\(16\)", "16 MB flash board metadata"),
    @($boardPins, "BOARD_PINS_PSRAM_SIZE_MB\s+\(8\)", "8 MB PSRAM board metadata"),
    @($boardPins, 'BOARD_PINS_PSRAM_MODE\s+"octal"', "Octal PSRAM board metadata"),
    @($boardPins, "BOARD_PINS_KEY1_IO\s+\(GPIO_NUM_38\)", "KEY1 GPIO38"),
    @($boardPins, "BOARD_PINS_KEY2_IO\s+\(GPIO_NUM_39\)", "KEY2 GPIO39"),
    @($boardPins, "BOARD_PINS_KEY3_IO\s+\(GPIO_NUM_40\)", "KEY3 GPIO40"),
    @($boardPins, "BOARD_PINS_KEY4_IO\s+\(GPIO_NUM_41\)", "KEY4 GPIO41"),
    @($boardPins, "BOARD_PINS_EC11_A_IO\s+\(GPIO_NUM_42\)", "EC11-A GPIO42"),
    @($boardPins, "BOARD_PINS_EC11_B_IO\s+\(GPIO_NUM_2\)", "EC11-B GPIO2"),
    @($boardPins, "BOARD_PINS_EC11_KEY_IO\s+\(GPIO_NUM_11\)", "EC11 key GPIO11"),
    @($boardPins, "BOARD_PINS_MIC_CLK_IO\s+\(GPIO_NUM_48\)", "mic CLK GPIO48"),
    @($boardPins, "BOARD_PINS_MIC_DOUT_IO\s+\(GPIO_NUM_47\)", "mic DOUT GPIO47"),
    @($boardPins, "BOARD_PINS_USB_DET_IO\s+\(GPIO_NUM_7\)", "USB detect GPIO7"),
    @($boardPins, "BOARD_PINS_USB_DP_IO\s+\(GPIO_NUM_20\)", "USB D+ GPIO20"),
    @($boardPins, "BOARD_PINS_USB_DN_IO\s+\(GPIO_NUM_19\)", "USB D- GPIO19"),
    @($boardPins, "BOARD_PINS_BAT_CHG_IO\s+\(GPIO_NUM_14\)", "charge CHG GPIO14"),
    @($boardPins, "BOARD_PINS_BAT_STD_IO\s+\(GPIO_NUM_21\)", "charge STDBY GPIO21"),
    @($boardPins, "BOARD_PINS_BAT_V_ADC_IO\s+\(GPIO_NUM_8\)", "battery ADC GPIO8"),
    @($boardPins, "BOARD_PINS_RGB_STATUS_IO\s+\(GPIO_NUM_1\)", "status RGB GPIO1"),
    @($boardPins, "BOARD_PINS_RGB_KEY_IO\s+\(GPIO_NUM_13\)", "key RGB GPIO13"),
    @($boardPins, "BOARD_PINS_RGB_EDGE_IO\s+\(GPIO_NUM_4\)", "edge RGB GPIO4"),
    @($boardPins, "BOARD_PINS_PWR_HOLD_IO\s+\(GPIO_NUM_46\)", "PWR_HOLD GPIO46"),
    @($boardPins, "BOARD_PINS_TPS63020_I_ADC_IO\s+\(GPIO_NUM_10\)", "3.3 V current ADC GPIO10"),
    @($boardPins, "BOARD_PINS_SY7088_I_ADC_IO\s+\(GPIO_NUM_9\)", "LED/5 V current ADC GPIO9"),
    @($boardPins, "BOARD_PINS_N16R8_MSPI_GPIO35", "GPIO35 reserved marker"),
    @($boardPins, "BOARD_PINS_N16R8_MSPI_GPIO36", "GPIO36 reserved marker"),
    @($boardPins, "BOARD_PINS_N16R8_MSPI_GPIO37", "GPIO37 reserved marker"),
    @($listenerDevice, 'LISTENER_DEVICE_MODEL\s+"keyboard-v2"', "V2 BLE/DIS model"),
    @($listenerDevice, 'LISTENER_DEVICE_HW_REV\s+"esp32s3-wroom-1-n16r8"', "N16R8 BLE/DIS hardware revision"),
    @($keyboard, "key1\.gpio38\.d", "V2 KEY1 diagnostic label"),
    @($keyboard, "key4\.gpio41\.s", "V2 KEY4 diagnostic label"),
    @($voiceKeyInput, 'VOICE_KEY_INPUT_DIRECT_LABEL\s+"gpio11\.ec11_key"', "V2 EC11 key diagnostic label"),
    @($boardHelp, "Voice Keyboard V2/N16R8", "V2 board help text"),
    @($boardHelp, "~BOARD:STATUS", "V2 board diagnostic command"),
    @($boardHelp, "~LED:TEST:RGBW", "V2 LED RGBW diagnostic command"),
    @($boardHelp, "BOARD_V2_USB_DET_POLICY", "USB detect provisional policy"),
    @($boardHelp, "BOARD_V2_CHARGER_POLARITY", "charger polarity provisional policy"),
    @($boardHelp, "BOARD_V2_PWR_HOLD_POLICY", "PWR_HOLD sign-off policy"),
    @($boardHelp, "BOARD_V2_CURRENT_POLICY", "current telemetry provisional policy"),
    @($boardHelp, "BOARD_V2_LED_POLICY", "LED rail sign-off policy"),
    @($boardHelp, "board_get_v2_power_input_snapshot", "V2 board power input snapshot API"),
    @($batteryMonitorHeader, "battery_monitor_read_power_rail", "V2 current/power telemetry API"),
    @($batteryMonitorHeader, "BATTERY_MONITOR_POWER_RAIL_3V3", "3.3 V rail telemetry enum"),
    @($batteryMonitorHeader, "BATTERY_MONITOR_POWER_RAIL_LED_5V", "LED/5 V rail telemetry enum"),
    @($batteryMonitor, "BOARD_PINS_TPS63020_I_ADC_IO", "3.3 V current ADC implementation"),
    @($batteryMonitor, "BOARD_PINS_SY7088_I_ADC_IO", "LED/5 V current ADC implementation"),
    @($batteryMonitor, "current_ma_valid = false", "uncalibrated current status"),
    @($keyboard, "keyboard_ec11_task", "EC11 A/B polling task"),
    @($keyboard, "BOARD_PINS_EC11_A_IO", "EC11 A GPIO use"),
    @($keyboard, "BOARD_PINS_EC11_B_IO", "EC11 B GPIO use"),
    @($diagEvents, "DIAG_SRC_BOARD", "board diagnostics source"),
    @($diagEvents, "DIAG_BOARD_POWER_RAIL", "board current telemetry diag event"),
    @($diagEvents, "DIAG_BOARD_LED_RESOURCE", "LED resource diag event"),
    @($diagPlatform, 'case 0x0D: return "board"', "diag_log board source name"),
    @($audioCaptureKconfig, "AUDIO_CAPTURE_V2_MIC_INTERFACE_VALIDATED", "V2 mic validation gate"),
    @($audioCapture, "AUDIO_CAPTURE_V2_MIC_BLOCKER", "V2 mic blocker log"),
    @($audioCapture, "ESP_ERR_NOT_SUPPORTED", "V2 mic degraded return"),
    @($systemHealth, "board_get_v2_power_input_snapshot", "system health V2 USB/charger raw snapshot"),
    @($systemHealth, "bat_chg_raw", "system health raw charger CHG level"),
    @($systemHealth, "charger_policy", "system health charger provisional policy"),
    @($powerManager, "POWER_MANAGER_WAKE_POLICY_V2_EC11_PROVISIONAL", "V2 provisional wake policy"),
    @($powerManager, "CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE", "V2 wake sign-off config guard"),
    @($otaPackage, 'hardware_revision = "keyboard-v2-n16r8"', "OTA package V2 hardware revision"),
    @($factoryPackage, 'hardware_revision = "esp32s3-wroom-1-n16r8"', "factory package N16R8 DIS hardware revision")
)) {
    Assert-Contains -Text $item[0] -Pattern $item[1] -Description $item[2]
}

foreach ($item in @(
    @($boardPins, "BOARD_PINS_KEY4_IO\s+\(GPIO_NUM_21\)", "V1 KEY4 wake/key pin in board profile"),
    @($boardPins, "BOARD_PINS_EC11_[A-Z_]*IO\s+\(GPIO_NUM_3[567]\)", "N16R8/MSPI GPIO used as EC11 input"),
    @($boardPins, "BOARD_PINS_(KEY|MIC|BAT|USB|RGB|PWR|TPS63020|SY7088)[A-Z0-9_]*IO\s+\(GPIO_NUM_3[567]\)", "N16R8/MSPI GPIO used as ordinary V2 board signal"),
    @($sdkconfig, "# CONFIG_SPIRAM is not set", "PSRAM-disabled production default"),
    @($sdkconfig, "CONFIG_ESPTOOLPY_FLASHSIZE_4MB", "4 MB flash production default"),
    @($powerManager, "POWER_MANAGER_WAKE_POLICY_KEY4_ONLY", "V1 KEY4 wake policy in active code"),
    @($powerManager, "KEY4/GPIO21", "V1 KEY4 wake string in active code"),
    @($powerManager, "GPIO35 voice key", "V1 GPIO35 wake limitation in active code"),
    @($voiceKeyInput, "gpio35\.ec11_key", "V1 GPIO35 EC11 diagnostic label in active code")
)) {
    Assert-NotContains -Text $item[0] -Pattern $item[1] -Description $item[2]
}

foreach ($token in @(
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
    'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
    "CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8=y",
    "# CONFIG_LISTENER_V2_ENABLE_EC11_DEEP_SLEEP_WAKE is not set",
    "CONFIG_SPIRAM=y",
    "CONFIG_SPIRAM_MODE_OCT=y",
    "CONFIG_SPIRAM_TYPE_AUTO=y",
    "CONFIG_SPIRAM_SPEED_80M=y",
    "CONFIG_SPIRAM_USE_MALLOC=y"
)) {
    Assert-Contains -Text $sdkconfig -Pattern ([regex]::Escape($token)) -Description "sdkconfig token $token"
}

$ota0 = Get-PartitionSize -PartitionsText $partitions -Name "ota_0"
$ota1 = Get-PartitionSize -PartitionsText $partitions -Name "ota_1"
$diagLog = Get-PartitionSize -PartitionsText $partitions -Name "diag_log"
if ($ota0 -lt 0x600000 -or $ota1 -lt 0x600000) {
    Add-CheckError "OTA app partitions must be at least 0x600000 bytes for the 16 MB V2 baseline."
}
if ($diagLog -lt 0x200000) {
    Add-CheckError "diag_log partition must be at least 0x200000 bytes for the 16 MB V2 baseline."
}

if ($errors.Count -gt 0) {
    Write-Host "FAIL: V2/N16R8 board profile static verification failed"
    $errors | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host "PASS: V2/N16R8 board profile, memory defaults, pin map, partitions, package identity, and stale V1 guard checks passed."
