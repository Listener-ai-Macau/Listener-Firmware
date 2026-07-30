#!/usr/bin/env python3
"""Focused regression contract for the Listener 1.0.3 OTA repair."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TYPE_ROOT = ROOT.parent / "Listener-Type"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    ota = read(ROOT / "components/firmware_ota/firmware_ota.c")
    main_source = read(ROOT / "main/main.c")
    gap = read(ROOT / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    ota_adapter = read(
        ROOT / "ports/esp32/ble_firmware_ota/ble_firmware_ota_esp32.c"
    )
    ota_adapter_cmake = read(
        ROOT / "ports/esp32/ble_firmware_ota/CMakeLists.txt"
    )
    diag_adapter = read(
        ROOT / "ports/esp32/ble_diag_log/ble_diag_log_esp32.c"
    )
    power_manager = read(
        ROOT / "components/power_manager/power_manager.c"
    )
    voice_recording = read(
        ROOT / "components/voice_recording_control/voice_recording_control.c"
    )
    audio_capture = read(
        ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c"
    )
    audio_capture_header = read(
        ROOT / "ports/esp32/audio_capture/include/audio_capture.h"
    )
    audio_stream = read(
        ROOT / "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c"
    )
    led = read(ROOT / "components/status_led/status_led.c")
    type_ble = read(
        TYPE_ROOT / "src-tauri/src/embedded_ble/windows_ble/mod.rs"
    )
    type_notify = read(
        TYPE_ROOT / "src-tauri/src/embedded_ble/windows_ble/notify_open.rs"
    )
    type_capture = read(
        TYPE_ROOT / "src-tauri/src/embedded_ble/windows_ble/capture_events.rs"
    )
    type_ota_transfer = read(
        TYPE_ROOT / "src-tauri/src/embedded_ble/windows_ble/ota_transfer.rs"
    )
    type_ota_open = read(
        TYPE_ROOT / "src-tauri/src/embedded_ble/windows_ble/ota_open.rs"
    )
    type_gatt_open = read(
        TYPE_ROOT / "src-tauri/src/embedded_ble/windows_ble/gatt_open.rs"
    )
    type_firmware = read(
        TYPE_ROOT / "src-tauri/src/commands/device/firmware.rs"
    )
    type_ota_harness = read(
        TYPE_ROOT / "scripts/run-installed-ota-cdp.mjs"
    )
    platform_cache = read(
        TYPE_ROOT
        / "third_party/denzic-platform/ble_pairing/host/rust/src/gatt_cache.rs"
    )
    worker_start = ota_adapter.index(
        "static void ble_firmware_ota_worker_task(void *arg)"
    )
    worker_end = ota_adapter.index(
        "static void ble_firmware_ota_drain_queued_data_locked(void)",
        worker_start,
    )
    worker_body = ota_adapter[worker_start:worker_end]

    require(
        all(
            token in ota
            for token in (
                "bool self_check_recorded;",
                "bool boot_ble_ready;",
                "if (!self_check_recorded)",
                "s_ota.boot_ble_ready = ble_ready;",
                "s_ota.self_check_recorded = true;",
            )
        ),
        "pending-verify decisions must wait for recorded startup evidence",
    )
    require(
        "s_ota.ble_ready = ble_ready;" not in ota
        and "s_ota.ble_ready = true;" in ota,
        "early encrypted-link evidence must remain latched across self-check recording",
    )
    pending_verify_start = ota.index(
        "esp_err_t firmware_ota_confirm_pending_verify_if_ready(void)"
    )
    pending_verify_end = ota.index(
        "void firmware_ota_note_runtime_ble_readiness_and_confirm(void)",
        pending_verify_start,
    )
    pending_verify = ota[pending_verify_start:pending_verify_end]
    require(
        "const bool boot_ble_ready = s_ota.boot_ble_ready;" in pending_verify
        and "if (post_ok && boot_ble_ready && keyboard_ready && !ble_ready)"
        in pending_verify
        and "OTA pending verify deferred awaiting encrypted BLE link" in pending_verify
        and "const bool decision_ble_ready = boot_ble_ready && ble_ready;"
        in pending_verify
        and pending_verify.index(
            "if (post_ok && boot_ble_ready && keyboard_ready && !ble_ready)"
        )
        < pending_verify.index(
            "denzic_ota_orchestration_v1_decide_pending_verify("
        ),
        "pending-verify must defer an in-flight encrypted link while retaining immediate rollback for definitive startup failures",
    )
    require(
        re.search(
            r"firmware_ota_record_self_check\([^;]+;[\s\S]{0,240}"
            r"firmware_ota_confirm_pending_verify_if_ready\(\);",
            main_source,
        )
        is not None,
        "app_main must decide only after recording startup evidence",
    )
    dle_start = gap.index(
        "static esp_err_t ble_hid_gap_request_audio_data_length(const char *policy)"
    )
    dle_end = gap.index(
        "static bool ble_hid_gap_configure_swift_pair_fields",
        dle_start,
    )
    dle_request = gap[dle_start:dle_end]
    require(
        "if (!conn.secure_connected)" in dle_request
        and dle_request.index("if (!conn.secure_connected)")
        < dle_request.index("ble_gap_set_data_len("),
        "DLE must be deferred until the tracked connection is encrypted",
    )
    require(
        "data length update already pending" in dle_request
        and re.search(
            r"if \(pending\) \{[\s\S]{0,260}return ESP_OK;",
            dle_request,
        )
        is not None,
        "concurrent DLE callers must coalesce behind one in-flight HCI request",
    )
    ota_ready_start = gap.index("bool ble_hid_gap_ota_connection_ready(void)")
    ota_ready_end = gap.index(
        "static void ble_hid_gap_ota_reconnect_task",
        ota_ready_start,
    )
    ota_ready = gap[ota_ready_start:ota_ready_end]
    require(
        all(
            token in ota_ready
            for token in (
                "!conn.secure_connected",
                "desc.conn_itvl >= BLE_HID_GAP_ACTIVE_ITVL_MIN",
                "desc.conn_itvl <= BLE_HID_GAP_ACTIVE_FALLBACK_ITVL_MAX",
                "conn.audio_tx_phy == BLE_HCI_LE_PHY_2M",
                "conn.audio_rx_phy == BLE_HCI_LE_PHY_2M",
                "conn.audio_data_length_ready",
                "conn.audio_data_length_max_tx_octets >=",
                "BLE_HID_GAP_AUDIO_DATA_LEN_OCTETS",
                "conn.audio_data_length_max_tx_time_us >=",
                "BLE_HID_GAP_AUDIO_DATA_LEN_TIME_US",
            )
        )
        and "desc.conn_itvl <= 24U" not in ota_ready,
        "OTA full-window readiness must require secure 7.5-15 ms, 2M/2M and 251-byte/1590-us DLE",
    )
    require(
        "#define BLE_HID_GAP_AUDIO_DATA_LEN_TIME_US 1590U" in gap,
        "the LE 2M full-packet DLE target must remain 1590 us",
    )
    require(
        "restore_full_data_length" in gap
        and '"data length downgraded"' in gap
        and re.search(
            r"case BLE_GAP_EVENT_DATA_LEN_CHG:[\s\S]{0,4000}"
            r"ble_hid_gap_schedule_active_connection_with_delay\(\s*0,\s*"
            r'"data length downgraded"\s*\)',
            gap,
        )
        is not None,
        "a mid-transfer DLE time downgrade must schedule asynchronous full-target recovery",
    )
    require(
        "BLE_HID_GAP_CONN_PARAM_REJECT_BACKOFF_MS 750U" in gap
        and "ble_hid_gap_defer_conn_param_retry_after_rejection" in gap,
        "Windows connection-parameter rejection must use bounded retry backoff",
    )

    for token in (
        "STATUS_LED_OTA_EC11_BASE_PERCENT 5U",
        "STATUS_LED_OTA_EC11_FILL_PERCENT 18U",
        "STATUS_LED_OTA_EC11_HEAD_PERCENT 42U",
        "STATUS_LED_OTA_EC11_TAIL_PERCENT 22U",
        "STATUS_LED_OTA_EDGE_BASE_PERCENT 4U",
        "STATUS_LED_OTA_EDGE_HEAD_PERCENT 24U",
        "STATUS_LED_OTA_EDGE_TAIL_PERCENT 14U",
        "STATUS_LED_OTA_EDGE_FADE_PERCENT 8U",
        "STATUS_LED_OTA_EDGE_STEP_MS 520U",
    ):
        require(token in led, f"v1.0.2 OTA accent contract missing: {token}")
    require(
        "status_led_render_ec11_ota_locked(frame, now_ms);" in led
        and "STATUS_LED_OTA_EDGE_STEP_MS);" in led,
        "OTA must restore the v1.0.2 EC11 and edge render paths",
    )
    ec11_renderer = led[
        led.index("static void status_led_render_ec11_locked"):
        led.index("static void status_led_render_key_active_work_locked")
    ]
    edge_renderer = led[
        led.index("static void status_led_render_edge_locked"):
        led.index("static void status_led_render_frame_locked")
    ]
    require(
        ec11_renderer.index("if (s_state.ota_active)")
        < ec11_renderer.index("if (status_led_error_active_locked")
        and edge_renderer.index("if (s_state.ota_active)")
        < edge_renderer.index("if (status_led_error_active_locked"),
        "OTA accent renderers must outrank transient 1.0.3 BLE/error suppression",
    )
    require(
        "STATUS_LED_OTA_OK_MIN_PERCENT 1U" in led
        and "STATUS_LED_OTA_OK_MAX_PERCENT 90U" in led
        and "STATUS_LED_OTA_PROGRESS_STEPS 10U" in led
        and re.search(
            r"static uint8_t status_led_ota_ok_percent_locked\(uint32_t now_ms\)"
            r"[\s\S]{0,320}uint8_t progress = status_led_ota_progress_percent_locked\(\);"
            r"[\s\S]{0,480}uint32_t step = \(\(uint32_t\)progress \* STATUS_LED_OTA_PROGRESS_STEPS\) / 100U;"
            r"[\s\S]{0,240}STATUS_LED_OTA_OK_MIN_PERCENT \+"
            r"[\s\S]{0,160}\(span \* step\) / STATUS_LED_OTA_PROGRESS_STEPS",
            led,
        )
        is not None
        and "STATUS_LED_OTA_OK_PULSE_MS" not in led
        and "STATUS_LED_OTA_OK_WRITE_TICK" not in led,
        "OTA status light must map progress through clearly visible 1%-90% steps with no pulse or write ticks",
    )

    for token in (
        "LISTENER_OTA_V1_DEFAULT_WINDOW_CHUNKS: usize = 400",
        "LISTENER_OTA_V1_WWR_PIPELINE_DEPTH: usize = 32",
        "LISTENER_OTA_V1_INACTIVE_LINK_WINDOW_CHUNKS: usize = 64",
    ):
        require(token in type_ble, f"1.0.3 OTA speed contract regressed: {token}")
    require(
        "MIN_PROTOCOL_TRANSFER_BYTES_PER_SECOND = 60 * 1024"
        in type_ota_harness
        and "protocolTransferBytesPerSecond > MIN_PROTOCOL_TRANSFER_BYTES_PER_SECOND"
        in type_ota_harness,
        "installed-Type OTA acceptance must require strictly more than 60.0 KiB/s",
    )
    require(
        gap.count("BLE_HID_GAP_ACTIVE_ITVL_MIN 6U") == 1
        and gap.count("BLE_HID_GAP_ACTIVE_ITVL_MAX 6U") == 1
        and gap.count("BLE_HID_GAP_ACTIVE_FALLBACK_ITVL_MAX 12U") == 1
        and gap.count("BLE_HID_GAP_ACTIVE_MIN_CE_LEN 12U") == 1
        and gap.count("BLE_HID_GAP_ACTIVE_MAX_CE_LEN 24U") == 1,
        "active BLE policy must prefer 7.5 ms and retain a 15 ms Windows fallback",
    )
    require(
        "target.post_ota_preserved_cccd = ota_post_confirm_address.is_some();"
        in type_notify
        and "if cleanup.target.post_ota_preserved_cccd" in type_capture
        and "reusing Windows-restored notify CCCD after verified OTA reconnect"
        in type_capture,
        "verified post-OTA reconnect must reuse Windows' restored CCCD instead of rewriting it",
    )
    require(
        "POST_OTA_VERIFIED_CACHED_CACHE_MODES: [BluetoothCacheMode; 1]"
        in type_ble
        and "[BluetoothCacheMode::Cached]" in type_ble
        and "&POST_OTA_VERIFIED_CACHED_CACHE_MODES" in type_ota_open
        and "&POST_OTA_UNCACHED_CACHE_MODES" not in type_ota_open[
            type_ota_open.index("fn open_notify_target_for_post_confirm_native_windows_hid("):
            type_ota_open.index("fn open_notify_target_for_device(address:")
        ],
        "challenge-proven post-confirm audio takeover must use cached-only GATT handles",
    )
    require(
        "FIRMWARE_OTA_POST_CONFIRM_SETTLE" not in type_firmware
        and "post-confirm settle" not in type_firmware,
        "a successful audio-control ATT probe must proceed directly to notify takeover without a redundant fixed settle",
    )
    require(
        "BluetoothLEPreferredConnectionParameters" not in type_ble
        and "RequestPreferredConnectionParameters" not in type_ble
        and "ThroughputOptimized" not in type_ble,
        "Type must leave OTA connection-parameter ownership to firmware's bounded 7.5 ms request",
    )
    require(
        "BLE_FIRMWARE_OTA_WORKER_QUEUE_DEPTH 32" in ota_adapter
        and "BLE_FIRMWARE_OTA_WORKER_BATCH_JOBS" not in ota_adapter
        and "} while (xQueueReceive(s_ota_worker_queue, &job, 0) == pdTRUE);"
        in worker_body
        and "watchdog_platform_feed_current_task();" in worker_body
        and "jobs_processed" not in worker_body
        and "vTaskDelay(1);" not in worker_body
        and "vTaskDelay(pdMS_TO_TICKS(1));" not in worker_body,
        "dual-lane OTA must keep the 32-entry pipeline and original continuous worker drain",
    )
    require(
        "ble_firmware_ota_wait_worker_queue_drain" not in ota_adapter
        and "ble_firmware_ota_drain_queued_data_locked" in ota_adapter
        and ota_adapter.count("ble_firmware_ota_drain_queued_data_locked();") >= 2
        and "xQueueReceive(s_ota_worker_queue, &job, 0)" in ota_adapter,
        "SYNC/status must retain the bounded 32-entry 1.0.3 tail-drain fast path",
    )
    require(
        "BLE_FIRMWARE_OTA_STORAGE_BATCH_CHUNKS 8" in ota_adapter
        and "BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES" in ota_adapter
        and "static uint8_t *s_ota_storage_batch;" in ota_adapter
        and "heap_caps_malloc(" in ota_adapter
        and "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in ota_adapter
        and ota_adapter.count("heap_caps_free(s_ota_storage_batch);") >= 4
        and "ble_firmware_ota_storage_flush()" in ota_adapter
        and "memcpy(s_ota_storage_batch + s_ota_storage_batch_length"
        in ota_adapter
        and ota_adapter.index("if (!ble_firmware_ota_storage_flush())")
        < ota_adapter.index("esp_err_t ret = firmware_ota_finish(false);")
        and "s_ota_storage_batch_length = 0;" in ota_adapter
        and "EXT_RAM_BSS_ATTR" not in ota_adapter[
            ota_adapter.index("static uint8_t *s_ota_storage_batch;"):
            ota_adapter.index("static volatile bool s_core_reset_pending;")
        ],
        "device storage must allocate its eight-chunk batch only during OTA in internal memory and release it on every exit",
    )
    require(
        '#include "audio_capture.h"' in ota_adapter
        and "BLE_FIRMWARE_OTA_AUDIO_SUSPEND_TIMEOUT_MS 250" in ota_adapter
        and "audio_capture_set_ota_suspended(true)" in ota_adapter
        and "audio_capture_idle_power_save_is_applied()" in ota_adapter
        and ota_adapter.count("audio_capture_set_ota_suspended(false)") >= 4
        and "PRIV_REQUIRES audio_capture" in ota_adapter_cmake
        and "bool audio_capture_idle_power_save_is_applied(void);"
        in audio_capture_header
        and "esp_err_t audio_capture_set_ota_suspended(bool suspended);"
        in audio_capture_header
        and "if (!enabled && s_ota_suspended)" in audio_capture
        and "if (s_ota_suspended)" in audio_capture
        and "i2s_del_channel(s_i2s_rx_handle)" in audio_capture
        and "if (s_i2s_rx_handle == NULL)" in audio_capture
        and "audio_capture_i2s_init()" in audio_capture,
        "device OTA must suspend idle PDM capture before BEGIN and restore it on every non-reboot exit",
    )
    require(
        "AUDIO_CAPTURE_VOICE_PREROLL_BYTES" in audio_capture
        and "static int16_t (*s_voice_preroll)[AUDIO_CAPTURE_FRAME_SAMPLES];"
        in audio_capture
        and "heap_caps_calloc(" in audio_capture
        and "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in audio_capture
        and audio_capture.count(
            "memset(s_voice_preroll, 0, AUDIO_CAPTURE_VOICE_PREROLL_BYTES);"
        )
        >= 1
        and re.search(
            r"if \(s_voice_preroll != NULL\) \{\s+memset\(\s+"
            r"s_voice_preroll,\s+0,\s+AUDIO_CAPTURE_VOICE_PREROLL_BYTES\);",
            audio_capture,
        )
        is not None
        and "static int16_t\n    s_voice_preroll[" not in audio_capture
        and "xTaskCreatePinnedToCoreWithCaps(" not in audio_capture,
        "only the task-owned voice pre-roll may move to PSRAM; safe-mode cleanup must tolerate it being absent and both audio task stacks must remain internal",
    )
    require(
        "BLE_FIRMWARE_OTA_WORKER_TASK_STACK_BYTES 4096" in ota_adapter
        and "BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES 4096" in ota_adapter
        and 'sizeof(StackType_t) == 1' in ota_adapter
        and "StackType_t s_ota_worker_stack[BLE_FIRMWARE_OTA_WORKER_TASK_STACK_BYTES]"
        in ota_adapter
        and "uxTaskGetStackHighWaterMark(s_ota_worker_task)" in ota_adapter
        and "worker stack high-water bytes=" in ota_adapter,
        "OTA worker must retain its proven 4096-byte Xtensa stack with runtime high-water evidence",
    )
    set_blocker = power_manager[
        power_manager.index("void power_manager_set_blocker"):
        power_manager.index("void power_manager_set_ble_connected")
    ]
    require(
        "if (!blocker_changed && !state_changed)" in set_blocker
        and set_blocker.index("if (!blocker_changed && !state_changed)")
        < set_blocker.index("xTaskNotifyGive(s_task_handle)"),
        "unchanged OTA blocker writes must not wake power_manager_task for every 500-byte packet",
    )
    inactivity_refresh_start = ota.index(
        "static void firmware_ota_refresh_inactivity_timeout(void)"
    )
    inactivity_callback_start = ota.index(
        "static void firmware_ota_inactivity_timer_callback(void *arg)",
        inactivity_refresh_start,
    )
    inactivity_fallback_start = ota.index(
        "static void firmware_ota_pending_verify_fallback_callback(void *arg)",
        inactivity_callback_start,
    )
    inactivity_refresh = ota[inactivity_refresh_start:inactivity_callback_start]
    inactivity_callback = ota[inactivity_callback_start:inactivity_fallback_start]
    require(
        "esp_timer_is_active(s_inactivity_timer)" in inactivity_refresh
        and "esp_timer_stop(s_inactivity_timer)" not in inactivity_refresh,
        "OTA DATA writes must update the inactivity deadline without reprogramming the active timer",
    )
    require(
        "deadline_us - now_us" in inactivity_callback
        and "esp_timer_start_once(s_inactivity_timer, (uint64_t)remaining_us)"
        in inactivity_callback,
        "OTA inactivity callback must re-arm for the remaining absolute deadline",
    )
    require(
        "ble_hid_gap_prepare_shutdown_disconnect()" in ota_adapter
        and "ble_hid_gap_is_connected()" in ota_adapter
        and "BLE_FIRMWARE_OTA_REBOOT_DISCONNECT_TIMEOUT_MS 1000" in ota_adapter
        and ota_adapter.index("vTaskDelay(pdMS_TO_TICKS(BLE_FIRMWARE_OTA_REBOOT_DELAY_MS))")
        < ota_adapter.index("ble_hid_gap_prepare_shutdown_disconnect()")
        < ota_adapter.index("firmware_ota_reboot_to_pending_image()"),
        "OTA reboot must terminate and observe the old GAP connection after FINISH response time and before restart",
    )
    require(
        ".flags = BLE_GATT_CHR_F_READ," in ota_adapter
        and "BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY" not in ota_adapter
        and 's_ota_ready_marker[] = "DOTA_READY_V1"' in diag_adapter
        and "ble_diag_log_on_gap_subscribe(" in diag_adapter
        and "ble_diag_log_notify_ota_ready(conn_handle, \"mtu_ready\")"
        in diag_adapter
        and 'strcmp(op, "ota_ready") == 0' in diag_adapter
        and 'ble_diag_log_notify_ota_ready(conn_handle, "ota_ready_challenge")'
        in diag_adapter
        and "ble_gatts_notify_custom(conn_handle, s_data_val_handle, om)"
        in diag_adapter
        and "OTA ready marker notify" in diag_adapter
        and "ble_diag_log_on_gap_subscribe(" in gap
        and "denzic_ota_v6_ota_control_cache_refresh" in gap,
        "firmware must emit post-reboot readiness on the stable diagnostic notify surface without changing OTA GATT schema",
    )
    require(
        "OtaRebootDisconnectObserver::arm(&fresh" in type_ble
        and "observer.wait_for_new_generation(transfer_guard.session_id())" in type_ble
        and "armed reboot disconnect observer before FINISH"
        in type_ota_transfer
        and "observed new-generation device reconnect after OTA reboot" in type_ota_transfer
        and "old-to-new connected generation boundary confirmed" in type_ota_transfer
        and "OTA_REBOOT_NEW_GENERATION_CONNECTED.swap(false" in type_ota_transfer
        and "complete generation boundary retained; final response-bearing TYPE:READY owns ATT proof"
        in type_ota_transfer
        and "request_new_generation_audio_control_ready" not in type_ota_transfer
        and "OTA_REBOOT_NEW_GENERATION_ATT_READY.swap(false" in type_ota_transfer
        and "fresh.handoff_new_generation_to_post_confirm(" in type_ble
        and "self.device.take()" in type_ota_transfer
        and "self.session.take()" in type_ota_transfer
        and "self.service.take()" in type_ota_transfer
        and "hold_listener_ota_post_confirm_session(&session)" in type_ota_transfer
        and "handed new-generation device connection to post-confirm ownership without explicit Close"
        in type_ota_transfer
        and "BluetoothDeviceId::FromId(&device_id)" not in type_ota_transfer
        and "GattSession::FromDeviceIdAsync(&device_id)" not in type_ota_transfer
        and "observed fresh new-generation ATT PDU readiness" not in type_ota_transfer,
        "Type must use a fresh cached diagnostic challenge after the full old-disconnected/new-connected generation",
    )
    require(
        "let transfer_elapsed_ms = stats.protocol_transfer_elapsed_ms;" in type_firmware
        and "transfer_wall_elapsed_ms.saturating_sub(transfer_elapsed_ms)" in type_firmware
        and ".saturating_add(transfer_fixed_elapsed_ms)" in type_firmware,
        "Type must separate protocol throughput from reboot/ATT fixed recovery time",
    )
    require(
        "fn release_gatt_maintain_request(" in type_gatt_open
        and "session.SetMaintainConnection(false)" in type_gatt_open
        and "replaced stale post-OTA confirmation hold" in type_ble
        and "post-OTA TYPE:READY ownership takeover" in type_ble
        and "poisoned notify target drop without Close" in type_ble
        and "release_gatt_maintain_request(&session, \"Listener OTA target drop\")"
        in type_ble
        and "release_gatt_maintain_request(&session, \"notify target drop\")"
        in type_ble,
        "every WinRT GATT maintain request must be balanced, including no-Close poisoned cleanup",
    )
    enter_recording = voice_recording[
        voice_recording.index("static esp_err_t voice_recording_control_enter_recording("):
        voice_recording.index("static esp_err_t voice_recording_control_exit_recording_with_origin(")
    ]
    refresh_monitoring = voice_recording[
        voice_recording.index("static void voice_recording_control_refresh_voice_monitoring(void)"):
        voice_recording.index("static void voice_recording_control_process_voice_activity(void)")
    ]
    require(
        "(power.blockers & POWER_MANAGER_BLOCKER_OTA) != 0u" in enter_recording
        and "ble_audio_stream_type_ota_hold_is_active()" in enter_recording
        and enter_recording.index("(power.blockers & POWER_MANAGER_BLOCKER_OTA) != 0u")
        < enter_recording.index("audio_capture_session_begin"),
        "recording start must be rejected during both OTA preparation and transfer",
    )
    require(
        "(power.blockers & POWER_MANAGER_BLOCKER_OTA) != 0u" in refresh_monitoring
        and "ble_audio_stream_type_ota_hold_is_active()" in refresh_monitoring
        and "!ota_active" in refresh_monitoring
        and refresh_monitoring.index("!ota_active")
        < refresh_monitoring.index("audio_capture_set_voice_activation_monitoring"),
        "OTA must disable and reset voice-activation monitoring before queued VAD input is processed",
    )
    type_ota_handoff = audio_stream[
        audio_stream.index('if (strcmp(command, "TYPE:OTA") == 0)'):
        audio_stream.index('if (strcmp(command, "TYPE:BYE") == 0')
    ]
    require(
        "ble_audio_stream_note_type_activity(command);" in type_ota_handoff
        and "ble_audio_stream_is_busy()" in type_ota_handoff
        and '"VREC:CANCEL"' in type_ota_handoff
        and type_ota_handoff.index("ble_audio_stream_note_type_activity(command);")
        < type_ota_handoff.index('"VREC:CANCEL"'),
        "TYPE:OTA must establish its preparation lease before canceling an existing hidden audio candidate",
    )
    heartbeat_update = audio_stream[
        audio_stream.index("static void ble_audio_stream_set_type_heartbeat_active("):
        audio_stream.index("static bool ble_audio_stream_transport_link_ready(")
    ]
    require(
        'strcmp(reason, "TYPE:READY") == 0' in heartbeat_update
        and 'strcmp(reason, "TYPE:HB") == 0' in heartbeat_update
        and "if (ota_hold)" in heartbeat_update
        and "else if (ota_recovery)" in heartbeat_update
        and "s_type_ota_hold_until_tick = 0;" in heartbeat_update,
        "non-recovery Type activity must not clear the TYPE:OTA preparation lease",
    )

    print(
        "PASS: pending-verify ordering, CCCD-preserving post-OTA reconnect, v1.0.2 accents plus steady OTA progress light, "
        "continuous worker flash scheduling, timer-efficient DATA writes, graceful reboot disconnect, OTA/recording exclusion, and 1.0.3 throughput constants are protected."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
