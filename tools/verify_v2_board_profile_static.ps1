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
$boardKconfig = Read-RepoFile "ports\esp32\board_pins\Kconfig.projbuild"
$sdkconfig = Read-RepoFile "sdkconfig.defaults.esp32s3"
$partitions = Read-RepoFile "partitions.csv"
$listenerDevice = Read-RepoFile "protocols\listener_device\include\listener_device.h"
$keyboard = Read-RepoFile "components\keyboard\keyboard.c"
$voiceKeyInput = Read-RepoFile "ports\esp32\voice_key_input\voice_key_input_esp32.c"
$board = Read-RepoFile "components\board\board.c"
$powerManager = Read-RepoFile "components\power_manager\power_manager.c"
$batteryMonitor = Read-RepoFile "components\battery_monitor\battery_monitor.c"
$statusLed = Read-RepoFile "components\status_led\status_led.c"
$audioCapture = Read-RepoFile "ports\esp32\audio_capture\audio_capture_esp32.c"
$statusLedDoc = Read-RepoFile "docs\features\status_led.md"
$lowPowerDoc = Read-RepoFile "docs\features\low_power_wake_policy.md"
$currentTelemetryTool = Read-RepoFile "tools\collect_v2_current_telemetry.ps1"
$otaPackage = Read-RepoFile "tools\package_ota_firmware.ps1"
$factoryPackage = Read-RepoFile "tools\package_factory_firmware.ps1"

foreach ($item in @(
    @($boardKconfig, "default LISTENER_BOARD_PROFILE_V2_N16R8", "default V2 board profile"),
    @($boardKconfig, "Production V2 board profile", "V2 board profile help"),
    @($boardPins, 'BOARD_PINS_PROFILE_ID\s+"voice-keyboard-v2-n16r8"', "V2 board profile id"),
    @($boardPins, 'BOARD_PINS_MODULE\s+"ESP32-S3-WROOM-1-N16R8"', "N16R8 module id"),
    @($boardPins, "BOARD_PINS_FLASH_SIZE_MB\s+\(16\)", "16 MB flash board metadata"),
    @($boardPins, "BOARD_PINS_PSRAM_SIZE_MB\s+\(8\)", "8 MB PSRAM board metadata"),
    @($boardPins, 'BOARD_PINS_PSRAM_MODE\s+"octal"', "Octal PSRAM mode"),
    @($boardPins, 'BOARD_PINS_RESERVED_MSPI_GPIOS\s+"GPIO35,GPIO36,GPIO37"', "reserved MSPI GPIO metadata"),
    @($boardPins, "BOARD_PINS_KEY1_IO\s+\(GPIO_NUM_38\)", "KEY1 GPIO38"),
    @($boardPins, "BOARD_PINS_KEY2_IO\s+\(GPIO_NUM_39\)", "KEY2 GPIO39"),
    @($boardPins, "BOARD_PINS_KEY3_IO\s+\(GPIO_NUM_40\)", "KEY3 GPIO40"),
    @($boardPins, "BOARD_PINS_KEY4_IO\s+\(GPIO_NUM_41\)", "KEY4 GPIO41"),
    @($boardPins, "BOARD_PINS_EC11_A_IO\s+\(GPIO_NUM_42\)", "EC11-A GPIO42"),
    @($boardPins, "BOARD_PINS_EC11_B_IO\s+\(GPIO_NUM_2\)", "EC11-B GPIO2"),
    @($boardPins, "BOARD_PINS_EC11_KEY_IO\s+\(GPIO_NUM_18\)", "EC11 key GPIO18"),
    @($boardPins, "BOARD_PINS_MIC_CLK_IO\s+BOARD_PINS_I2S_BCLK_IO", "mic clock macro"),
    @($boardPins, "BOARD_PINS_I2S_BCLK_IO\s+\(GPIO_NUM_48\)", "mic CLK GPIO48"),
    @($boardPins, "BOARD_PINS_I2S_DIN_IO\s+\(GPIO_NUM_47\)", "mic DOUT GPIO47"),
    @($audioCapture, "CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM", "SPH0655 PDM mic compile-time selector"),
    @($audioCapture, "i2s_channel_init_pdm_rx_mode", "V2 PDM RX initialization"),
    @($audioCapture, "I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG", "V2 PDM2PCM slot configuration"),
    @($audioCapture, "AUDIO_CAPTURE_PDM_HW_AMPLIFY_NUM\s+8U", "V2 PDM hardware gain multiplier"),
    @($audioCapture, "AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM\s+8", "V2 PDM software gain multiplier"),
    @($audioCapture, "audio_capture_apply_pdm_software_gain\(frame_buffer\)", "V2 PDM software gain application"),
    @($audioCapture, "pdm_cfg\.slot_cfg\.amplify_num\s*=\s*AUDIO_CAPTURE_PDM_HW_AMPLIFY_NUM", "V2 PDM hardware gain application"),
    @($audioCapture, "SPH0655 PDM mic init", "SPH0655 PDM mic init log"),
    @($audioCapture, "sw_gain=%u", "SPH0655 PDM gain init log"),
    @($boardPins, "BOARD_PINS_BAT_CHG_IO\s+\(GPIO_NUM_14\)", "charger CHG GPIO14"),
    @($boardPins, "BOARD_PINS_BAT_STD_IO\s+\(GPIO_NUM_21\)", "charger STD GPIO21"),
    @($boardPins, "BOARD_PINS_BAT_V_ADC_IO\s+\(GPIO_NUM_8\)", "battery ADC GPIO8"),
    @($boardPins, "BOARD_PINS_USB_DET_IO\s+\(GPIO_NUM_7\)", "USB detect GPIO7"),
    @($boardPins, "BOARD_PINS_RGB_STATUS_IO\s+\(GPIO_NUM_1\)", "status strip GPIO1"),
    @($boardPins, "BOARD_PINS_RGB_EC11_IO\s+\(GPIO_NUM_5\)", "EC11 strip GPIO5"),
    @($boardPins, "BOARD_PINS_RGB_KEY_IO\s+\(GPIO_NUM_13\)", "key strip GPIO13"),
    @($boardPins, "BOARD_PINS_RGB_EDGE_IO\s+\(GPIO_NUM_4\)", "edge strip GPIO4"),
    @($boardPins, "BOARD_PINS_PWR_HOLD_IO\s+\(GPIO_NUM_11\)", "PWR_HOLD GPIO11"),
    @($boardPins, "BOARD_PINS_CURRENT_TELEMETRY_PRESENT\s+\(1\)", "current V2 board current telemetry present flag"),
    @($boardPins, "BOARD_PINS_TPS63020_I_ADC_IO\s+\(GPIO_NUM_10\)", "TPS63020 input current ADC GPIO10"),
    @($boardPins, "BOARD_PINS_SY7088_I_ADC_IO\s+\(GPIO_NUM_9\)", "SY7088 input current ADC GPIO9"),
    @($listenerDevice, 'LISTENER_DEVICE_HW_REV\s+"esp32s3-wroom-1-n16r8"', "BLE/DIS hardware revision"),
    @($listenerDevice, "board=voice-keyboard-v2-n16r8", "factory readiness board metadata"),
    @($listenerDevice, "flash_16mb;psram_8mb_octal", "V2 memory capabilities"),
    @($keyboard, "key1\.gpio38\.f13", "KEY1 V2 diagnostic label"),
    @($keyboard, "key4\.gpio41\.f16", "KEY4 V2 diagnostic label"),
    @($keyboard, "EC11 ready: a=gpio42 b=gpio2 key=gpio18", "EC11 V2 ready log"),
    @($voiceKeyInput, 'VOICE_KEY_INPUT_DIRECT_LABEL\s+"ec11_key\.gpio18"', "EC11 custom/recovery diagnostic label"),
    @($board, "v2_gpio7_r37_r32_10K_10K_divider", "USB_Det 10K/10K policy"),
    @($board, "PWR_HOLD/GPIO11", "PWR_HOLD help text"),
    @($board, "reserved_mspi_gpio=%s", "reserved MSPI status field"),
    @($board, "~BOARD:GPIO", "raw V2 key and EC11 GPIO diagnostics command"),
    @($board, "mode=read_as_configured reconfigure=0", "non-destructive GPIO diagnostics mode"),
    @($board, "recording_key=custom_key_action ec11_key_action=power_on_runtime_custom", "custom recording key and EC11 power/runtime-custom GPIO diagnostic"),
    @($board, "key_pressed_mask=0x%02", "V2 active-low custom key pressed mask"),
    @($board, "LED7..LED10\+LED15..LED16\+LED23..LED28", "EC11 LED refs in board diagnostics"),
    @($board, '\.led_refs = "LED11..LED14"', "V2 key LED refs in board diagnostics"),
    @($board, '\.led_refs = "LED17..LED22"', "V2 edge LED refs in board diagnostics"),
    @($batteryMonitor, "BATTERY_MONITOR_DIVIDER_NUMERATOR 2U", "68K/68K battery divider reconstruction"),
    @($batteryMonitor, "BATTERY_MONITOR_ABSOLUTE_MIN_MV 2700U", "absolute battery danger marker"),
    @($batteryMonitor, "BATTERY_MONITOR_EMPTY_MV 3000U", "protected product empty battery voltage"),
    @($batteryMonitor, "BATTERY_MONITOR_FULL_MV 4200U", "full battery voltage"),
    @($batteryMonitor, "BATTERY_MONITOR_V2_CURRENT_MA_PER_ADC_MV 2U", "INA180A2 10mR current model"),
    @($batteryMonitor, "TPS63020_input_branch", "TPS63020 input branch naming"),
    @($batteryMonitor, "SY7088_input_branch", "SY7088 input branch naming"),
    @($batteryMonitor, "ina180a2_10mR_adc_calibrated_battery_adc_calibrated", "current telemetry calibrated status"),
    @($board, "battery_side_mv", "battery-side power telemetry output"),
    @($board, "product_empty_3000mv_full_4200mv_absolute_min_2700mv", "battery percentage policy output"),
    @($board, "v2_battery_side_input_branch_current_ina180a2_10mR_adc_mv_x2_with_battery_mv_from_gpio8_div2", "current telemetry policy"),
    @($board, "present=%u gpio=%", "current telemetry present flag output"),
    @($board, 'current_model=\\"%s\\"', "current telemetry variable current model output"),
    @($currentTelemetryTool, 'branch\s*=\s*"TPS63020_input_branch"', "current telemetry TPS63020 branch parser"),
    @($currentTelemetryTool, 'branch\s*=\s*"SY7088_input_branch"', "current telemetry SY7088 branch parser"),
    @($currentTelemetryTool, "allowed_gpios = @\(-1, 10\)", "current telemetry TPS63020 optional GPIO parser"),
    @($currentTelemetryTool, "allowed_gpios = @\(-1, 9\)", "current telemetry SY7088 optional GPIO parser"),
    @($currentTelemetryTool, "ina180a2_10mR_adc_calibrated_battery_adc_calibrated", "current telemetry populated self-test"),
    @($currentTelemetryTool, "shutdown_blockers=0x00000000", "current telemetry self-test shutdown blockers"),
    @($currentTelemetryTool, "hardware_shutdown_ms=1800000", "current telemetry self-test hardware shutdown threshold"),
    @($currentTelemetryTool, "pwr_hold_gpio=11", "current telemetry self-test PWR_HOLD GPIO11"),
    @($currentTelemetryTool, "pwr_hold_level=low", "current telemetry self-test PWR_HOLD runtime pulldown readback"),
    @($currentTelemetryTool, "pwr_hold_configured=1", "current telemetry self-test PWR_HOLD runtime output-drive enabled state"),
    @($currentTelemetryTool, "v2_gpio11_power_latch_runtime_low_drive_high_for_hardware_shutdown", "current telemetry self-test PWR_HOLD runtime-low policy"),
    @($currentTelemetryTool, "voice_key_gpio=18", "current telemetry self-test voice key GPIO18"),
    @($powerManager, "POWER_MANAGER_STATE_HARDWARE_SHUTDOWN", "power manager hardware shutdown state"),
    @($powerManager, "board_set_power_hold_enabled\(false\)", "power manager drives PWR_HOLD high for shutdown"),
    @($lowPowerDoc, "PWR_HOLD/GPIO11", "low-power hardware shutdown PWR_HOLD doc"),
    @($statusLed, "STATUS_LED_EC11_COUNT 12", "EC11 12-LED strip count"),
    @($statusLed, "STATUS_LED_EDGE_COUNT 6", "edge 6-LED strip count"),
    @($statusLed, "STATUS_LED_STRIP_COUNT 4", "four LED strips"),
    @($statusLed, "BOARD_PINS_RGB_EC11_IO", "EC11 strip firmware resource"),
    @($statusLed, "ec11_order=LED7..LED10\+LED15..LED16\+LED23..LED28", "EC11 LED order status"),
    @($statusLedDoc, "GPIO5", "status LED doc EC11 GPIO5"),
    @($statusLedDoc, "LED17.*LED22", "status LED doc edge LED refs"),
    @($otaPackage, 'hardware_revision = "keyboard-v2-n16r8"', "OTA package V2 hardware requirement"),
    @($otaPackage, 'hardware_revision = "esp32s3-wroom-1-n16r8"', "OTA package V2 DIS revision"),
    @($factoryPackage, 'hardware_revision = "esp32s3-wroom-1-n16r8"', "factory package V2 DIS revision"),
    @($factoryPackage, "audio_control_uuid", "factory package audio control UUID"),
    @($factoryPackage, "ble_audio_control_v1", "factory package audio control capability"),
    @($factoryPackage, "board=voice-keyboard-v2-n16r8", "factory package V2 readiness token"),
    @($factoryPackage, "firmware_ota_v1", "factory package OTA capability"),
    @($factoryPackage, "flash_16mb", "factory package flash capability"),
    @($factoryPackage, "psram_8mb_octal", "factory package PSRAM capability"),
    @($factoryPackage, "post_failure_behavior", "factory package POST failure diagnostic contract"),
    @($factoryPackage, "~OTA:STATUS", "factory package serial OTA status diagnostic command")
)) {
    Assert-Contains -Text $item[0] -Pattern $item[1] -Description $item[2]
}

if ($board -match "(?s)static void board_print_gpio_status\(void\)\s*\{(?<body>.*?)\n\}") {
    $gpioStatusBody = $Matches["body"]
    Assert-NotContains -Text $gpioStatusBody -Pattern "board_configure_status_input\s*\(\s*BOARD_PINS_EC11_A_IO\s*\)" -Description "~BOARD:GPIO EC11 A interrupt reconfiguration"
    Assert-NotContains -Text $gpioStatusBody -Pattern "board_configure_status_input\s*\(\s*BOARD_PINS_EC11_B_IO\s*\)" -Description "~BOARD:GPIO EC11 B interrupt reconfiguration"
    Assert-Contains -Text $gpioStatusBody -Pattern "board_read_gpio_level\s*\(\s*BOARD_PINS_EC11_A_IO\s*\)" -Description "~BOARD:GPIO EC11 A non-destructive read"
    Assert-Contains -Text $gpioStatusBody -Pattern "board_read_gpio_level\s*\(\s*BOARD_PINS_EC11_B_IO\s*\)" -Description "~BOARD:GPIO EC11 B non-destructive read"
} else {
    Add-CheckError "missing board_print_gpio_status function"
}

foreach ($token in @(
    "CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8=y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
    'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
    "CONFIG_SPIRAM=y",
    "CONFIG_SPIRAM_MODE_OCT=y",
    "CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM=y",
    "CONFIG_AUDIO_CAPTURE_V2_MIC_INTERFACE_VALIDATED=y"
)) {
    Assert-Contains -Text $sdkconfig -Pattern ([regex]::Escape($token)) -Description "sdkconfig token $token"
}

foreach ($item in @(
    @($boardPins, "BOARD_PINS_EC11_KEY_IO\s+\(GPIO_NUM_(11|35)\)", "stale EC11 key GPIO"),
    @($boardPins, "BOARD_PINS_PWR_HOLD_IO\s+\(GPIO_NUM_46\)", "stale PWR_HOLD GPIO46"),
    @($boardPins, "BOARD_PINS_RGB_EC11_IO\s+\(GPIO_NUM_4\)", "stale EC11 strip on edge GPIO"),
    @($sdkconfig, "CONFIG_LISTENER_BOARD_PROFILE_N4=y", "active N4 sdkconfig"),
    @($sdkconfig, "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y", "active 4 MB flash sdkconfig"),
    @($sdkconfig, "# CONFIG_SPIRAM is not set", "disabled PSRAM sdkconfig"),
    @($sdkconfig, "CONFIG_AUDIO_CAPTURE_MIC_SPH0645=y", "stale SPH0645 microphone sdkconfig"),
    @($sdkconfig, "CONFIG_AUDIO_CAPTURE_SPH0645_SLOT_LEFT=y", "stale SPH0645 slot sdkconfig"),
    @($sdkconfig, "CONFIG_AUDIO_CAPTURE_SPH0645_GAIN=4", "stale SPH0645 gain sdkconfig"),
    @($sdkconfig, "# CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM is not set", "disabled SPH0655 microphone sdkconfig"),
    @($sdkconfig, "# CONFIG_AUDIO_CAPTURE_V2_MIC_INTERFACE_VALIDATED is not set", "disabled V2 microphone validation sdkconfig"),
    @($listenerDevice, "voice-keyboard-n4|esp32s3-wroom-1-n4|flash_4mb|no_psram", "N4 device metadata"),
    @($keyboard, "gpio35|gpio45|gpio48\.f14|gpio47\.f15|gpio21\.f16", "N4 keyboard diagnostic labels"),
    @($voiceKeyInput, "ec11_key\.gpio(11|35)", "stale EC11 key GPIO label"),
    @($board, "5\.1K|PWR_HOLD/GPIO46|Voice Keyboard N4|EC11 push/GPIO(11|35)|N4 deep sleep", "stale board diagnostics/help"),
    @($board, "LED11..LED16|LED15..LED28", "stale LED three-zone refs in board diagnostics"),
    @($currentTelemetryTool, 'rail=TPS63020_3V3|rail=SY7088_LED_5V|wake_key_gpio=|wake_user_action=""press_ec11_key_or_usb_reset""|voice_key_gpio=11|pwr_hold_gpio=46|PWR_HOLD/GPIO46', "stale current telemetry collector diagnostics"),
    @($powerManager, "KEY4/GPIO21|EC11-KEY/GPIO35|wake_policy|wake_gpio|PWR_HOLD/GPIO46|hold-low", "stale wake diagnostics"),
    @($lowPowerDoc, "EC11-KEY_IO/GPIO11|EC11-KEY/GPIO35|KEY4/GPIO21|wake_policy|wake_gpio|deep-sleep|PWR_HOLD/GPIO46", "stale low-power wake doc GPIO"),
    @($statusLed, "STATUS_LED_EC11_COUNT 4|STATUS_LED_EDGE_COUNT 14", "stale LED strip counts"),
    @($statusLedDoc, "LED11.*LED16", "stale status LED doc edge refs"),
    @($otaPackage, "keyboard-n4|esp32s3-wroom-1-n4", "N4 OTA package metadata"),
    @($factoryPackage, "esp32s3-wroom-1-n4", "N4 factory package metadata")
)) {
    Assert-NotContains -Text $item[0] -Pattern $item[1] -Description $item[2]
}

$ota0 = Get-PartitionSize -PartitionsText $partitions -Name "ota_0"
$ota1 = Get-PartitionSize -PartitionsText $partitions -Name "ota_1"
$diagLog = Get-PartitionSize -PartitionsText $partitions -Name "diag_log"
if ($ota0 -lt 0x600000 -or $ota1 -lt 0x600000) {
    Add-CheckError "OTA app partitions must be at least 0x600000 bytes for the 16 MB V2 baseline."
}
if ($diagLog -lt 0x100000) {
    Add-CheckError "diag_log partition must be at least 0x100000 bytes for the 16 MB V2 baseline."
}

if ($errors.Count -gt 0) {
    Write-Host "FAIL: V2 board profile static verification failed"
    $errors | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host "PASS: V2 N16R8 board profile, memory defaults, pin map, four-zone LED resources, battery-side current telemetry, USB_Det divider policy, diagnostics, partitions, and package identity checks passed."
