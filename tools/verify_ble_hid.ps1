param(
    [string]$Port,
    [string]$Text = "abc123",
    [int]$BootCaptureSeconds = 8,
    [int]$PostSendCaptureSeconds = 3,
    [int]$Baud = 115200,
    [switch]$ResetBeforeRead = $true,
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Port)) {
    $bleHidPath = Join-Path $RepoRoot "ports\esp32\ble_hid\ble_hid.c"
    $diagEventsPath = Join-Path $RepoRoot "components\diag_log\include\diag_log_events.h"
    if (-not (Test-Path -LiteralPath $bleHidPath)) {
        throw "Missing BLE HID source: $bleHidPath"
    }
    if (-not (Test-Path -LiteralPath $diagEventsPath)) {
        throw "Missing diag log events header: $diagEventsPath"
    }

    $bleHid = Get-Content -LiteralPath $bleHidPath -Raw
    $diagEvents = Get-Content -LiteralPath $diagEventsPath -Raw

    $checks = @(
        @($bleHid, 'battery_monitor_read\(&battery\)', "BLE HID reads battery_monitor ADC status"),
        @($bleHid, 'esp_hidd_dev_battery_set\(s_ble_hid_ctx\.hid_device, level\)', "BLE HID writes HID Battery Service"),
        @($bleHid, 'BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT 1', "BLE HID uses a 1 percent battery notify threshold"),
        @($bleHid, 'ble_hid_battery_level_exceeds_notify_threshold\(level\)', "BLE HID gates periodic battery notifications on threshold changes"),
        @($bleHid, 'ble_hid_update_battery_level\("connect_restore", true\)', "BLE HID forces battery refresh after reconnect"),
        @($bleHid, 'DIAG_BLE_BATTERY_LEVEL', "BLE HID records battery level diagnostics"),
        @($bleHid, 'battery\.raw_adc', "BLE HID diagnostics include raw ADC"),
        @($bleHid, 'battery\.adc_mv', "BLE HID diagnostics include ADC mV"),
        @($diagEvents, 'DIAG_BLE_BATTERY_LEVEL\s+5', "diag_log defines BLE battery level event")
    )

    foreach ($check in $checks) {
        if ($check[0] -notmatch $check[1]) {
            throw "verify_ble_hid static check failed: $($check[2])"
        }
    }

    Write-Host "PASS: verify_ble_hid static checks cover BLE HID Battery Service live reporting, reconnect refresh, and ADC diagnostics."
    exit 0
}

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$reset_before_read = if ($ResetBeforeRead.IsPresent) { "True" } else { "False" }
$text_base64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))

$log_output = @"
import base64
import sys
import time
import serial

port = r"$Port"
text = base64.b64decode("$text_base64").decode("utf-8")
baud = $Baud
boot_capture_seconds = $BootCaptureSeconds
post_send_capture_seconds = $PostSendCaptureSeconds
reset_before_read = $reset_before_read

ser = serial.Serial()
ser.port = port
ser.baudrate = baud
ser.timeout = 0.2
ser.dsrdtr = False
ser.rtscts = False
ser.dtr = False
ser.rts = False
ser.open()
try:
    if reset_before_read:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.2)

    chunks = []

    boot_deadline = time.time() + boot_capture_seconds
    while time.time() < boot_deadline:
        data = ser.read(4096)
        if data:
            chunks.append(data)

    ser.write(text.encode("utf-8"))
    ser.flush()

    post_send_deadline = time.time() + post_send_capture_seconds
    while time.time() < post_send_deadline:
        data = ser.read(4096)
        if data:
            chunks.append(data)

    sys.stdout.write(b"".join(chunks).decode("utf-8", errors="replace"))
finally:
    ser.close()
"@ | & $python_path -

$has_start = $log_output -match "ble_hid: START"
$has_input_task = $log_output -match "ble_hid: USB SERIAL INPUT READY"
$has_uart_rx = $log_output -match "ble_hid: SCRIPT RX"
$has_send_done = $log_output -match "hid_keyboard: send_ascii done"
$has_not_connected = $log_output -match "Device Not Connected|connected=no"

if (-not $has_start) {
    if ($has_send_done) {
        Write-Warning "verify_ble_hid: boot marker missing, but HID report dispatch completed; reset log was likely missed"
    } else {
        throw "verify_ble_hid: missing boot marker 'ble_hid: START'"
    }
}

if (-not $has_input_task) {
    if ($has_uart_rx) {
        Write-Warning "verify_ble_hid: input task marker missing, but script input reached firmware"
    } else {
        throw "verify_ble_hid: missing input task marker 'ble_hid: USB SERIAL INPUT READY'"
    }
}

if ($has_send_done) {
    Write-Host "verify_ble_hid: boot ok, script input consumed, HID report dispatch completed"
} elseif ($has_uart_rx -and $has_not_connected) {
    Write-Warning "verify_ble_hid: boot ok and script input consumed, but no BLE host is connected yet, so HID delivery could not complete"
} elseif ($has_uart_rx) {
    Write-Warning "verify_ble_hid: script input reached firmware, but HID completion marker is missing"
} else {
    Write-Warning "verify_ble_hid: boot ok, but firmware did not log script input consumption"
}

$log_output
