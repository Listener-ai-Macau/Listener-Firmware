from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


CHECKS = {
    "ports/esp32/board_pins/include/board_pins.h": [
        r"BOARD_PINS_RGB_STATUS_IO\s+\(GPIO_NUM_1\)",
        r"BOARD_PINS_RGB_EC11_IO\s+\(GPIO_NUM_5\)",
        r"BOARD_PINS_RGB_KEY_IO\s+\(GPIO_NUM_13\)",
        r"BOARD_PINS_RGB_EDGE_IO\s+\(GPIO_NUM_4\)",
        r"BOARD_PINS_BAT_CHG_IO\s+\(GPIO_NUM_14\)",
    ],
    "components/status_led/include/status_led.h": [
        "status_led_init",
        "status_led_start",
        "status_led_consume_usb_command",
        "status_led_set_ble_state",
        "STATUS_LED_BLE_REPAIRING",
        "status_led_notify_ble_repairing",
        "status_led_set_recording",
        "status_led_set_recording_level",
        "status_led_set_processing",
        "status_led_notify_shutdown_confirm",
        "STATUS_LED_EC11_FEEDBACK_PRESS",
        "STATUS_LED_EC11_FEEDBACK_ROTATE_CW",
        "STATUS_LED_EC11_FEEDBACK_ROTATE_CCW",
        "status_led_notify_ec11_feedback",
        "status_led_prepare_sleep",
        "STATUS_LED_REC_SOURCE_DEVICE_MIC",
        "STATUS_LED_REC_SOURCE_DESKTOP_MIC",
        "STATUS_LED_REC_SOURCE_NOT_AVAILABLE",
        "STATUS_LED_ERROR_DOMAIN_OTA",
    ],
    "components/status_led/status_led.c": [
        "STATUS_LED_STATUS_COUNT 6",
        "STATUS_LED_EC11_COUNT 12",
        "STATUS_LED_KEY_COUNT 4",
        "STATUS_LED_EDGE_COUNT 6",
        "STATUS_LED_STRIP_COUNT 4",
        "STATUS_LED_STATUS_TAIL_GUARD_PIXELS 6U",
        "STATUS_LED_STATUS_TAIL_REINFORCE_WRITES 3U",
        "STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MIN_PERCENT 14U",
        "STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MAX_PERCENT 20U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MIN_PERCENT 14U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MAX_PERCENT 16U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_BREATH_PERIOD_MS 3600U",
        "STATUS_LED_STATUS_FIRST_LED 1U",
        "STATUS_LED_EC11_FIRST_LED 7U",
        "STATUS_LED_KEY_FIRST_LED 11U",
        "STATUS_LED_EDGE_FIRST_LED 17U",
        "STATUS_LED_STATUS_PHYSICAL_MAP",
        "STATUS_LED_KEY_PHYSICAL_MAP",
        "STATUS_LED_STATUS_KEY_MAPPING_CONTRACT",
        "STATUS_LED_STRIP_STATUS",
        "STATUS_LED_STRIP_EC11",
        "STATUS_LED_STRIP_KEY",
        "STATUS_LED_STRIP_EDGE",
        "STATUS_LED_PROFILE_OFF",
        "STATUS_LED_PROFILE_LOW",
        "STATUS_LED_PROFILE_STANDARD",
        "STATUS_LED_PROFILE_AMBIENT",
        "STATUS_LED_PROFILE_FACTORY",
        "STATUS_LED_COLOR_ORDER_GRB",
        "STATUS_LED_COLOR_ORDER_RGB",
        "STATUS_LED_STATUS_DEFAULT_COLOR_ORDER STATUS_LED_COLOR_ORDER_GRB",
        "STATUS_LED_KEY_DEFAULT_COLOR_ORDER STATUS_LED_COLOR_ORDER_GRB",
        "STATUS_LED_FULL_BRIGHTNESS_PERCENT 100U",
        "STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA 2000U",
        "STATUS_LED_LOW_PROFILE_CAP_PERCENT 100U",
        "STATUS_LED_STANDARD_PROFILE_CAP_PERCENT 100U",
        "STATUS_LED_AMBIENT_PROFILE_CAP_PERCENT 100U",
        "STATUS_LED_LOW_PROFILE_BUDGET_MA 300U",
        "STATUS_LED_STANDARD_PROFILE_BUDGET_MA 760U",
        "STATUS_LED_AMBIENT_PROFILE_BUDGET_MA 620U",
        "STATUS_LED_CHASE_DEFAULT_STEP_MS 250U",
        "STATUS_LED_KEY_FEEDBACK_MS 240U",
        "STATUS_LED_EC11_FEEDBACK_MS 900U",
        "STATUS_LED_BOOT_ACK_MS 2500U",
        "STATUS_LED_OK_TOTAL_MS 2000U",
        "STATUS_LED_CHARGING_BREATH_PERIOD_MS 3600U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1300U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_MIN_PERCENT 8U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT 38U",
        "STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 28U",
        "STATUS_LED_CHARGE_FULL_DEBOUNCE_MS 10000U",
        "STATUS_LED_CHARGE_FULL_MIN_MV 4050U",
        "STATUS_LED_CHARGE_FULL_MIN_PERCENT 88U",
        "STATUS_LED_BATTERY_DISPLAY_GREEN_PERCENT 60U",
        "STATUS_LED_LOW_BATTERY_STEADY_PERCENT 38U",
        "STATUS_LED_FULL_STEADY_PERCENT 100U",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT 100U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS 120U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS 7880U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT 18U",
        "STATUS_LED_PREVIEW_BLE_OVERRIDE_MS 15000U",
        "STATUS_LED_BLE_REPAIR_CUE_MS 3500U",
        "STATUS_LED_BLE_CONNECTED_CONFIRM_MS 1600U",
        "STATUS_LED_PWR_COLOR_AMBER",
        "STATUS_LED_DIAG_VIS_LOW_POWER_OFF",
        "battery_display_level_percent",
        "status_led_update_battery_display_locked",
        "battery_display_rise_suppressed",
        "status_led_log_power_input_locked",
        "status_led_log_visual_state_locked",
        "status_led_log_output_state_locked",
        "last_logged_frame_rgb_key",
        "active_flags & 0x1FFU",
        "status_led_log_power_input_locked();",
        "DIAG_LED_POWER_INPUT",
        "DIAG_LED_VISUAL_STATE",
        "DIAG_LED_OUTPUT_STATE",
        "DIAG_LED_FRAME_RGB",
        "STATUS_LED_IDLE_REFRESH_MS 1000U",
        "STATUS_LED_CONTRACT_REV \"status_key_ec11_edge_true_state_v19\"",
        "STATUS_LED_EC11_ACCENT_MIN_PERCENT",
        "STATUS_LED_EC11_ACCENT_MAX_PERCENT",
        "STATUS_LED_EDGE_ACCENT_MIN_PERCENT",
        "STATUS_LED_EDGE_ACCENT_MAX_PERCENT",
        "STATUS_LED_POWER_SOURCE_CHARGER_STATUS",
        "STATUS_LED_DIAG_POWER_EXTERNAL_CHARGER_STATUS",
        "STATUS_LED_DIAG_ACTIVE_EC11",
        "boot_feedback_until_ms",
        "status_led_force_boot_feedback",
        "status_led_boot_power_color_locked",
        "STATUS_LED_NVS_BRIGHTNESS_KEY \"brightness\"",
        "led_contract_rev=",
        "unchanged_tx_suppression=1",
        "timing=ws2812_4020_compatible",
        "status_tail_guard_pixels=%u",
        "status_tail_reinforce=recording_processing",
        "status_tail_safe_effect_percent=%u..%u",
        "status_tail_overlap_effect_percent=%u..%u_slow",
        "effect_only_preview=1",
        "effect_profile=product_v1",
        "profile_cap_percent=%u",
        "brightness_percent=%u",
        "user_brightness_percent=%u",
        "status_zone_brightness_percent=%u",
        "key_zone_brightness_percent=%u",
        "ec11_zone_brightness_percent=%u",
        "edge_zone_brightness_percent=%u",
        "status_led_apply_zone_brightness_caps_locked",
        "status_led_apply_device_settings_snapshot_locked",
        "STATUS_LED_ACCENT_ENTRY_RAMP_MS 900U",
        "STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT 5U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT 5U",
        "STATUS_LED_EC11_RECORDING_FLOW_STEP_MS 360U",
        "STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS 720U",
        "STATUS_LED_EC11_REPAIR_ORBIT_STEP_MS 220U",
        "STATUS_LED_EC11_ORBIT_STEP_MS 240U",
        "STATUS_LED_EDGE_ORBIT_STEP_MS 480U",
        "status_led_render_key_active_work_locked",
        "effective_cap_percent=%u",
        "user_brightness_is_hard_cap=1",
        "profile_dimming_disabled=1",
        "factory_full_brightness=1",
        "safety_full_brightness=1",
        "status_led_profile_cap_percent_for",
        "uint8_t user_brightness = s_state.brightness_percent",
        "scaled = ((uint32_t)desired_percent * user_brightness + 50U) / 100U",
        "STATUS_LED_TEST_CHASE",
        "status_led_parse_single_strip_mask",
        "strtok_r(copy, \",+| \"",
        "LED chase running",
        "status_led_strip_backend.h",
        "status_led_strip_backend_new",
        "status_led_strip_backend_transmit",
        "status_led_frame_equal",
        "status_led_status_tail_reinforce_needed",
        "status_led_refresh_delay_ms_locked",
        "status_led_request_refresh",
        "status_query_samples_current_render=1",
        "status_led_render_frame_locked(&sampled_frame, now_ms);",
        "snapshot.last_frame = sampled_frame;",
        "ulTaskNotifyTake",
        "status_led_force_manual_off",
        "manual_off",
        "~LED:STATUS",
        "mapping_contract=",
        "status_physical_map=",
        "key_physical_map=",
        "separate_status_key_color_order=1",
        "~LED:BUDGET",
        "BRIGHTNESS ",
        "~LED:PRIVACY",
        "TEST:RGBW",
        "TEST:MAP",
        "TEST:PIXEL",
        "STATUS_LED_TEST_PIXEL",
        "status_led_parse_calibration_strip",
        "led_prefixed",
        "status_key_only=%u",
        "ec11_edge_touched=%u",
        "ec11_edge_untouched=%u",
        "mapping_contract=%s",
        "ec11_order=LED7..LED10+LED15..LED16+LED23..LED28",
        "PREVIEW ",
        "ERROR ",
        "PROFILE ",
        "s_state.ble_state = STATUS_LED_BLE_DISCONNECTED",
        "status_led_start_ble_repair_locked",
        "ble_transition_ms",
        "status_led_ble_elapsed_locked",
        "preview_ble_override_until_ms",
        "preview_ble_override_ms_left",
        "state != STATUS_LED_BLE_REPAIRING",
        "uint8_t percent = status_led_double_pulse_on(ble_elapsed_ms, 2000U) ? 58U : 22U;",
        "ble_elapsed_ms < STATUS_LED_BLE_CONNECTED_CONFIRM_MS",
        "STATUS_LED_BLE_CONNECTED_CONFIRM_MS,\n                    32U,\n                    70U",
        "status_led_preview_clear_activity_locked",
        "status_led_render_edge_clockwise_chase_locked",
        "ble_repair_ms_left=%",
        "ble_transition_ms=%",
        "s_state.rec_source = STATUS_LED_REC_SOURCE_NONE",
        "s_state.battery_valid = false",
        "s_state.error_until_ms = 0",
        "strcmp(s_state.last_reason, \"preview\") == 0",
        "status_led_ble_elapsed_locked(now_ms)",
        "PWM_RGB_EC11_GPIO5",
        "PWM_RGB_KEY_GPIO13",
        "gpio14_reserved=BAT_CHG_IO",
        "vdd_led_enable=always_on_assumed",
        "~LED:STATUS detail=contract",
        "~LED:STATUS detail=brightness",
        "~LED:STATUS detail=strips",
        "~LED:STATUS detail=state",
        "~LED:STATUS detail=power",
        "~LED:STATUS detail=rgb",
        "status_led_is_status_command",
        "strncmp(arg, \"detail=\", strlen(\"detail=\")) == 0",
        "status_led_print_strip_rgb_line(\"rgb_ec11\"",
        "status_led_print_strip_rgb_line(\"rgb_key\"",
        "status_led_print_strip_rgb_line(\"rgb_edge\"",
        "~LED:STATUS profile=%s detail=summary",
        "external_power_source=%s",
        "semantic_order=LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN",
        "rec_not_available_shows=WARN_ONLY",
        "status_led_profile_budget_ma_locked",
        "status_led_estimate_current_ma",
        "status_led_clamp_current_locked",
        "status_led_render_error_locked",
        "status_led_render_recording_locked",
        "status_led_clear_ok_locked",
        "status_led_clear_retryable_error_locked",
        "status_led_preview_ready_baseline_locked",
        "status_led_preview_effect_only_baseline_locked",
        "preview_effect_only",
        "capture_led_only",
        "recording_processing_led_only",
        "recording_processing_status_led_only",
        "recording_processing_status_key_stress",
        "status_key_stress3",
        "status_key_stress4",
        "status_key_stress34",
        "STATUS_LED_TEST_STATUS_KEY_STRESS",
        "processing_led_only",
        "repairing",
        "recording_processing",
        "recording_level_percent",
        "recording_level_updated_ms",
        "status_led_set_recording_level",
        "STATUS_LED_REC_GOLD_R 255U",
        "STATUS_LED_REC_GOLD_G 176U",
        "STATUS_LED_REC_GOLD_B 12U",
        "status_led_rec_gold()",
        "STATUS_LED_RECORDING_BREATH_PERIOD_MS 1500U",
        "STATUS_LED_RECORDING_BREATH_MIN_PERCENT 28U",
        "STATUS_LED_RECORDING_BREATH_MAX_PERCENT 50U",
        "STATUS_LED_RECORDING_LEVEL_STALE_MS 300U",
        "STATUS_LED_RECORDING_LEVEL_HOLD_MAX_MS 120000U",
        "STATUS_LED_ACTIVE_WORK_REC_PERCENT 34U",
        "STATUS_LED_ACTIVE_WORK_AI_PERCENT 32U",
        "STATUS_LED_ACTIVE_WORK_REC_MAX_PERCENT 44U",
        "STATUS_LED_PROCESSING_BREATH_PERIOD_MS 1800U",
        "STATUS_LED_PROCESSING_BREATH_MIN_PERCENT 28U",
        "STATUS_LED_PROCESSING_BREATH_MAX_PERCENT 52U",
        "STATUS_LED_ACCENT_BREATHE_QUANTUM_PERCENT 2U",
        "STATUS_LED_EC11_RECORDING_BASE_MIN_PERCENT 4U",
        "STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT 5U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MIN_PERCENT 4U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT 5U",
        "STATUS_LED_ACCENT_ENTRY_RAMP_MS 900U",
        "STATUS_LED_EC11_ORBIT_STEP_MS 240U",
        "STATUS_LED_EDGE_ORBIT_STEP_MS 480U",
        "STATUS_LED_EC11_PROCESSING_BASE_PERCENT 7U",
        "STATUS_LED_EDGE_PROCESSING_BASE_PERCENT 6U",
        "STATUS_LED_EC11_PROCESSING_ORBIT_PERCENT 26U",
        "STATUS_LED_EDGE_PROCESSING_ORBIT_PERCENT 24U",
        "status_led_effect_elapsed_ms_locked",
        "status_led_set_recording_level",
        "rec_level=%u",
        "return status_led_quantize_percent(breath, STATUS_LED_DYNAMIC_STATUS_QUANTUM_PERCENT)",
        "recording_level_hold_until_ms",
        "status_led_recording_status_percent_locked",
        "status_led_processing_status_percent_locked",
        "REC_LEVEL ",
        "rec_level_hold_ms_left=%",
        "STATUS_LED_ACTIVE_WORK_REC_PERCENT",
        "STATUS_LED_ACTIVE_WORK_AI_PERCENT",
        "status_led_recording_accent_percent_locked",
        "status_led_render_ec11_recording_flow_locked",
        "status_led_render_edge_recording_flow_locked",
        "status_led_render_ec11_repair_locked",
        "status_led_render_ec11_feedback_locked",
        "status_led_notify_ec11_feedback",
        "status_led_clear_ec11_feedback_locked",
        "STATUS_LED_SHUTDOWN_CONFIRM_MS 1800U",
        "STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS 700U",
        "status_led_render_shutdown_confirm_locked",
        "if (!s_state.shutdown_confirm_final) {\n        return true;",
        "uint8_t accent_percent = final ? 36U : 24U;",
        "clockwise_index = (STATUS_LED_EC11_COUNT - 1U - index) % STATUS_LED_EC11_COUNT",
        "shutdown_confirm_active=%u shutdown_confirm_final=%u shutdown_confirm_latched=%u shutdown_confirm_elapsed_ms=%",
        "shutdown_confirm_active=%u shutdown_confirm_latched=%u",
        "status_led_apply_status_tail_guard_locked",
        "status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_STATUS], frame->status)",
        "status_led_notify_shutdown_confirm",
        "shutdown_confirm_started_ms",
        "s_state.low_power_disabled = false;",
        "shutdown_final",
        "status_led_smoothstep_per_mille",
        "status_led_charging_breath_lerp_percent",
        "status_led_charging_breath_percent",
        "status_led_charging_breath_percent(now_ms)",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT",
        "s_state.external_power_present",
        "BOARD_PINS_USB_DET_IO",
        "external_power_source_flags |= STATUS_LED_POWER_SOURCE_CHARGER_STATUS",
        "s_state.charge_full_latched = true",
        "s_state.charge_full_latched = false",
        "raw_charging=%u raw_full=%u",
        "full_latched=%u full_candidate_ms=%",
        "external_power=%u external_power_source=%s",
        "status_led_rgb(255, 255, 255), percent, false",
        "active_flags=PWR:%u,BLE:%u,REC:%u,AI:%u,OK:%u,WARN:%u,EC11:%u,KEY:%u,EDGE:%u",
        "status_rgb=PWR:%u,%u,%u;BLE:%u,%u,%u;REC:%u,%u,%u",
        "if (changed && !effect_only) {\n            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;",
        "if (changed && state == STATUS_LED_BLE_CONNECTED && confidence_window)",
        "state == STATUS_LED_BLE_PAIRING && status_led_ble_repair_active_locked(now_ms)",
        "status_led_render_processing_locked",
        "status_led_render_ec11_locked",
        "status_led_render_edge_locked",
        "STATUS_LED_TX_MUTEX_WAIT_MS",
        "s_tx_mutex",
        "status_led_print_strip_rgb_line",
        "status_led_pack_status_color_classes",
        "status_led_pack_status_intensity_a",
        "status_led_pack_status_intensity_b",
    ],
    "components/status_led/status_led_strip_backend.h": [
        "STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT 12U",
        "STATUS_LED_COLOR_ORDER_GRB",
        "STATUS_LED_COLOR_ORDER_RGB",
        "tail_guard_pixels",
        "status_led_rgb_t",
        "status_led_strip_backend_t",
        "status_led_strip_backend_new",
        "status_led_strip_backend_transmit",
    ],
    "components/status_led/status_led_strip_backend.c": [
        "driver/rmt_encoder.h",
        "driver/rmt_tx.h",
        "soc/soc_caps.h",
        "status_led_new_ws2812_encoder",
        "RMT_CLK_SRC_DEFAULT",
        "STATUS_LED_RMT_RESOLUTION_HZ 10000000U",
        "RMT_ENCODER_FUNC_ATTR",
        "STATUS_LED_WS2812_RESET_TICKS 1500U",
        "STATUS_LED_WS2812_T0H_TICKS 3U",
        "STATUS_LED_WS2812_T0L_TICKS 10U",
        "STATUS_LED_WS2812_T1H_TICKS 7U",
        "STATUS_LED_WS2812_T1L_TICKS 6U",
        "SOC_RMT_MEM_WORDS_PER_CHANNEL",
        "status_led_strip_backend_fill_pixels",
        "memset(backend->pixels, 0, sizeof(backend->pixels));",
        "transmit_led_count",
        "tail_guard_pixels=%u",
        "config->led_count + config->tail_guard_pixels",
        "rmt_encoder_reset(backend->encoder)",
        "status_led_color_order_name",
        "reset_us=300",
        "diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL",
    ],
    "components/status_led/CMakeLists.txt": [
        '"status_led.c" "status_led_strip_backend.c"',
    ],
    "components/diag_log/include/diag_log_events.h": [
        "DIAG_SRC_STATUS_LED",
        "DIAG_LED_STATE",
        "DIAG_LED_ERROR",
        "DIAG_LED_OUTPUT_FAIL",
        "DIAG_LED_PROFILE",
        "DIAG_LED_POWER_INPUT",
        "DIAG_LED_VISUAL_STATE",
        "DIAG_LED_OUTPUT_STATE",
        "DIAG_LED_FRAME_RGB",
        "bit24=display_valid",
        "bits16_23=display_level",
        "bit26=external_from_usb_det",
        "bit27=external_from_charger_status",
    ],
    "tools/verify_unplugged_flash_diag_bundle.py": [
        "status_led\", \"power_input",
        "status_led\", \"visual_state",
        "status_led\", \"output_state",
        "power\", \"external_power",
        "power\", \"sleep_wake",
        "off followed by visible-on recovery",
        "--expect-pwr-class",
    ],
    "tools/decode_diag_log.py": [
        "(\"status_led\", \"led_frame_rgb\")",
        "\"frame_rgb\"",
        "add_led_frame_rgb_summary",
        "external_from_charger_status",
        "status_color_classes",
        "zone_intensity",
    ],
    "main/main.c": [
        "status_led_init()",
        "status_led_start()",
    ],
    "ports/esp32/ble_hid/ble_hid.c": [
        "status_led_consume_usb_command(line)",
        "status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, true)",
        "status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false)",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false)",
        "status_led_notify_ble_repairing(\"ble_recovery_clear_bonds\")",
        "status_led_notify_ble_repairing(\"ble_recovery_refresh_pairing\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE",
        "status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE)",
    ],
    "components/keyboard/keyboard.c": [
        "status_led_notify_key_event",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE",
    ],
    "components/voice_recording_control/voice_recording_control.c": [
        "status_led_set_recording(true, STATUS_LED_REC_SOURCE_DEVICE_MIC)",
        "status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE)",
        "suppress_retry_led_error",
        "status_led_clear_error(STATUS_LED_ERROR_DOMAIN_REC)",
        "PROCESSING:START",
        "PROCESSING_START",
        "PROCESSING:STOP",
        "PROCESSING_STOP",
        "PROCESSING:DONE",
        "PROCESSING_DONE",
        "voice_recording_control_host_processing_start(source)",
        "voice_recording_control_host_processing_stop(source)",
        "voice_recording_control_host_processing_done(source)",
        "status_led_notify_success(\"recording_stop_done\")",
        "status_led_notify_success(\"recording_session_done\")",
        "status_led_notify_success(\"host_processing_done\")",
        "status_led_notify_ble_repairing(\"voice_recovery_requested\")",
    ],
    "ports/esp32/audio_capture/audio_capture_esp32.c": [
        "#include \"status_led.h\"",
        "AUDIO_CAPTURE_LEVEL_NOISE_FLOOR",
        "AUDIO_CAPTURE_LEVEL_FULL_SCALE",
        "audio_capture_frame_level_percent",
        "status_led_set_recording_level(audio_capture_frame_level_percent(frame_buffer))",
    ],
    "ports/esp32/audio_capture/CMakeLists.txt": [
        "status_led",
    ],
    "components/firmware_ota/firmware_ota.c": [
        "status_led_set_processing(true, \"ota_begin\")",
        "status_led_set_processing(false, \"ota_finish\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA",
    ],
    "components/power_manager/power_manager.c": [
        "status_led_prepare_sleep",
        "status_led_set_low_power_disabled",
        "status_led_notify_shutdown_confirm",
        "POWER_MANAGER_SHUTDOWN_LED_CONFIRM_MS 700U",
        "hardware_shutdown_confirmed",
    ],
    "ports/esp32/voice_key_input/voice_key_input_esp32.c": [
        "#include \"status_led.h\"",
        "status_led_notify_shutdown_confirm(false, \"ec11_long_press_shutdown_confirm\")",
    ],
    "ports/esp32/voice_key_input/CMakeLists.txt": [
        "status_led",
    ],
    "docs/features/status_led.md": [
        "GPIO5",
        "GPIO13",
        "`GPIO14` is reserved for `BAT_CHG_IO`",
        "`LED1=PWR`",
        "`LED6=WARN`",
        "`LED11=KEY1`",
        "`LED14=KEY4`",
        "status and key strips keep separate color-order storage",
        "`standard` is the product default",
        "Product Effect Language",
        "Repeated same-state BLE callbacks are idempotent",
        "User-requested re-pairing",
        "status_led_notify_ble_repairing()",
        "ble_repair_ms_left",
        "short blue connected-success confirmation",
        "BLE animation phase is tracked separately",
        "`ble_transition_ms`",
        "Manual `~LED:PREVIEW` scenes temporarily hold their requested BLE state",
        "for 15 seconds",
        "`preview_ble_override_ms_left`",
        "Reconnect keeps a low blue BLE floor",
        "Pairing and reconnect use only the `BLE` semantic LED",
        "User-requested re-pair/reset uses `BLE` plus a low blue EC11 confirmation orbit",
        "current render-sampled RGB frame",
        "status_query_samples_current_render=1",
        "On battery, once confidence/status windows expire, connected BLE falls back to a sparse low-blue heartbeat",
        "External power overrides battery-color display on `PWR`",
        "continuous slow white breath",
        "steady white once charge-full has been debounced and latched",
        "User brightness scales the whole routine effect envelope before the profile cap is applied",
        "50% user brightness setting scales the external-power breath's low and high points together",
        "charging breath is intentionally shallow and slow",
        "Recording is a controlled warm-gold semantic state",
        "exposes `rec_level`",
        "no longer uses PCM level to drive brightness",
        "`~LED:REC_LEVEL <0-100> [hold_ms]`",
        "Processing is a controlled saturated purple semantic state",
        "Firmware recording transfer does not animate `AI` by itself",
        "`VREC:PROCESSING:START`",
        "`VREC:PROCESSING:STOP`",
        "`VREC:PROCESSING:DONE`",
        "`DONE` turns `AI` off and flashes green `OK`",
        "local recording stop/session completion",
        "A successful local stop and a completed local recording session each refresh the green `OK` confirmation window",
        "audio transfer completion alone does not animate `AI`",
        "`OK` is a visible 2.0 second success confirmation after local recording stop/session completion and after the host reports processing done",
        "Key LEDs remain local transient feedback only",
        "Recording and processing no longer light the key strip",
        "EC11 knob and edge/frame LEDs are independent accent surfaces",
        "low warm-gold base plus a broad slow deterministic flow",
        "low deterministic warm-gold same-cycle flow accents",
        "`rec_level` remains diagnostic input but does not change the product brightness envelope",
        "keeps the EC11 ring fully present with a low warm-gold base and broad slow deterministic flow on all 12 LEDs",
        "keeps a low warm-gold base and a wider same-cycle clockwise flow on all 6 LEDs",
        "low violet clockwise motion",
        "EC11 short press and rotation add a brief white confirmation",
        "adds a low blue EC11 ring orbit as the user-action confirmation",
        "actively re-zeroed during advanced effects",
        "Status-tail anti-flicker guard",
        "six black guard pixels",
        "status_tail_reinforce=recording_processing",
        "status_tail_reinforce_writes=3",
        "dynamic_active_accents=1",
        "active_work_status_dynamic=1",
        "active_work_status_static=0",
        "Long-press shutdown confirmation",
        "filling the EC11 ring clockwise over 1.8 seconds",
        "pending cue stays latched at full ring",
        "wait longer than 1.8 seconds",
        "`shutdown_confirm_active`",
        "`shutdown_confirm_latched`",
        "`shutdown_confirm_elapsed_ms`",
        "`~LED:PREVIEW <ready|pairing|reconnect|repairing|capture|capture_led_only|desktop_mic|recording_processing|recording_processing_led_only|recording_processing_status_only|recording_processing_status_led_only|recording_processing_status_key_stress|status_key_stress3|status_key_stress4|status_key_stress34|rec_not_available|processing|processing_led_only|ok|low_battery|critical_battery|charging|full|shutdown_confirm|shutdown_final|sleep|clear>`",
        "`~LED:PREVIEW pairing`, `~LED:PREVIEW reconnect`, and `~LED:PREVIEW repairing`",
        "`~LED:PREVIEW capture_led_only`",
        "`~LED:PREVIEW recording_processing_led_only`",
        "`~LED:PREVIEW recording_processing_status_led_only`",
        "`~LED:PREVIEW recording_processing_status_key_stress`",
        "`~LED:PREVIEW processing_led_only`",
        "`preview_effect_only=1`",
        "keep PWR/BLE and physical key-press feedback out of the rendered frame",
        "the key strip stays off",
        "`~DEVICE:SET led_status=<0-100>`",
        "`~DEVICE:SET led_key=<0-100>`",
        "`~DEVICE:SET led_ec11=<0-100>`",
        "`~DEVICE:SET led_edge=<0-100>`",
        "`~LED:REC_LEVEL <0-100> [hold_ms]`",
        "It does not add a hidden percent cap above the user plugged/battery brightness setting",
        "user brightness cap is persisted through `~LED:BRIGHTNESS <0-100>` and applies as the hard routine-product brightness limit",
        "clear semantic colors",
        "RGBW, map, chase, and pixel test commands remain calibration tools",
        "`~LED:TEST:RGBW <status|ec11|knob|ring|key|edge|all>`",
        "`~LED:TEST:PIXEL <status|ec11|knob|ring|key|edge> <LEDn|index> <red|green|blue|white|off> [percent]`",
    ],
    "tools/status_led_camera_calibration.ps1": [
        "voice-keyboard-camera-status-key-led-tuning-1.2",
        "[switch]$VerifyMapping",
        "voice-keyboard-camera-status-key-led-tuning-1.3",
        "STEP_BY_MODE",
        "FIRMWARE_MAPPING_CONTRACT",
        "mapping_verification",
        "rgbw-single-led",
        "semantic-preview",
        "STATUS_EFFECT_BASELINE",
        "status_key_ec11_edge_true_state_v19",
        "\"expected_leds\": [\"PWR\", \"BLE\", \"REC\", \"AI\", \"EC11\", \"EDGE\"]",
        "\"forbidden_leds\": [\"OK\", \"WARN\"]",
        "make_semantic_sequence",
        "write_status_effects_markdown",
        "status-effects.md",
        "profile_cap_observations",
        "capture_semantic_samples",
        "sample_count",
        "sample_interval_ms",
        "state_expect",
        "state_matches_expect",
        "status_matches_expect",
        "semantic_status_matches",
        "return 1",
        "status,key",
        "data_gpio",
        "firmware_color_order",
        "~LED:TEST:PIXEL",
        "~LED:PREVIEW ready",
        "~LED:PREVIEW capture",
        "~LED:PREVIEW rec_not_available",
        "~LED:PREVIEW processing",
        "~LED:ERROR ai retryable",
        "~LED:ERROR system hard",
        "LED1",
        "LED14",
        "cv2.VideoCapture",
        "serial.tools",
        "manifest.json",
        "serial-transcript.txt",
        "per_led_results",
        "brightness_steps",
    ],
    "tools/status_led_manual_calibration.ps1": [
        "voice-keyboard-camera-status-key-led-tuning-1.2",
        "manual_observation",
        "status_key_only",
        "ec11_edge_untouched",
        "manual-feedback-template.md",
        "manual-session.jsonl",
        "open_serial_without_reset",
        "[int]$Percent = 100",
        "[switch]$RunSequence",
        "manual_sequence",
        "manual-sequence-summary.json",
        "preclear_command",
        "~LED:TEST:PIXEL",
        "~LED:OFF",
        "LED1",
        "LED14",
        "red",
        "green",
        "blue",
        "white",
        "off",
    ],
    "tools/status_led_human_effect_review.ps1": [
        "Get-VolumeSteps",
        "Get-ComplexSteps",
        "~LED:PREVIEW capture_led_only",
        "~LED:PREVIEW recording_processing_led_only",
        "~LED:PREVIEW recording_processing_status_led_only",
        "~LED:PREVIEW status_key_stress3",
        "~LED:PREVIEW status_key_stress4",
        "~LED:PREVIEW status_key_stress34",
        "~LED:PREVIEW recording_processing_status_key_stress",
        "~LED:PREVIEW processing_led_only",
        "~LED:PREVIEW repairing",
        "scene-ec11-short-press",
        "scene-ec11-rotate",
        "scene-key-feedback",
        "应用时机",
        "scene-ble-repairing",
        "scene-processing-live",
        "scene-sleep",
        "Get-ReproSteps",
        "Get-TailOnlySteps",
        "Get-ComboOnlySteps",
        'ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "RootCause", "StaticRoot", "Repro", "TailOnly", "ComboOnly", "Full")',
        "preview_effect_only=1",
        "effect-only preview commands",
    ],
    "components/keyboard/keyboard.c": [
        "status_led_notify_ec11_feedback(direction == EC11_ROTATION_DIRECTION_CW",
        "STATUS_LED_EC11_FEEDBACK_ROTATE_CW",
        "STATUS_LED_EC11_FEEDBACK_ROTATE_CCW",
    ],
    "ports/esp32/voice_key_input/voice_key_input_esp32.c": [
        "status_led_notify_ec11_feedback(STATUS_LED_EC11_FEEDBACK_PRESS)",
    ],
}


def read(relative: str) -> str:
    path = REPO_ROOT / relative
    if not path.is_file():
        raise FileNotFoundError(f"missing file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    failures: list[str] = []
    for relative, tokens in CHECKS.items():
        try:
            text = read(relative)
        except FileNotFoundError as exc:
            failures.append(str(exc))
            continue
        for token in tokens:
            if token.startswith("r\""):
                raise AssertionError("internal verifier token format error")
            if token.startswith("^"):
                if not re.search(token, text, re.MULTILINE):
                    failures.append(f"{relative}: missing pattern {token!r}")
            elif "\\" in token and re.search(r"\\[sSdDwW]", token):
                if not re.search(token, text, re.MULTILINE):
                    failures.append(f"{relative}: missing pattern {token!r}")
            elif token not in text:
                failures.append(f"{relative}: missing token {token!r}")

    board = read("ports/esp32/board_pins/include/board_pins.h")
    if re.search(r"BOARD_PINS_RGB_KEY_IO\s+\(GPIO_NUM_14\)", board):
        failures.append("board_pins.h: RGB key strip must not use GPIO14; GPIO14 is BAT_CHG_IO")
    if re.search(r"BOARD_PINS_RGB_EC11_IO\s+\(GPIO_NUM_4\)", board):
        failures.append("board_pins.h: EC11 knob ring must use GPIO5, not the edge GPIO4 strip")

    status_led = read("components/status_led/status_led.c")
    status_led_backend = read("components/status_led/status_led_strip_backend.c")
    main_c = read("main/main.c")
    human_review = read("tools/status_led_human_effect_review.ps1")
    if "bit-bang" in status_led.lower() or "bit-bang" in status_led_backend.lower():
        failures.append("status_led: do not bit-bang WS2812 timing")
    if "STATUS_LED_EC11_COUNT 4" in status_led:
        failures.append("status_led.c: stale EC11 four-LED strip count")
    if "STATUS_LED_EDGE_COUNT 14" in status_led:
        failures.append("status_led.c: stale edge/frame fourteen-LED strip count")
    for profile_name in ("LOW", "STANDARD", "AMBIENT"):
        if f"STATUS_LED_{profile_name}_PROFILE_CAP_PERCENT 100U" not in status_led:
            failures.append(
                f"status_led.c: {profile_name.lower()} profile must not add a hidden percent cap over user brightness"
            )
    for token in (
        "STATUS_LED_CHARGING_BREATH_MIN_PERCENT 8U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1300U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT 38U",
        "STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 28U",
        "STATUS_LED_LOW_BATTERY_STEADY_PERCENT 38U",
        "STATUS_LED_FULL_STEADY_PERCENT 100U",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT 100U",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: routine PWR peak must be user-capped, missing {token}")
    if "} else if (active_work) {\n            percent = STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT;" not in status_led:
        failures.append("status_led.c: charging PWR must hold a steady readable level during recording/processing")
    for token in (
        "STATUS_LED_RECORDING_LEVEL_STALE_MS",
        "STATUS_LED_EC11_RECORDING_FLOW_STEP_MS 360U",
        "STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS 720U",
        "status_led_status_tail_safe_dynamic_percent_locked",
        "status_led_status_tail_safe_range_percent_locked",
        "status_led_status_tail_overlap_breath_percent_locked",
        "status_led_status_tail_desired_for_effect_percent_locked",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MIN_PERCENT",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MAX_PERCENT",
        "STATUS_LED_STATUS_TAIL_OVERLAP_BREATH_PERIOD_MS",
        "status_led_render_ec11_recording_flow_locked",
        "status_led_render_edge_recording_flow_locked",
        "return status_led_quantize_percent(breath, STATUS_LED_DYNAMIC_STATUS_QUANTUM_PERCENT)",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: recording LED must use deterministic low-amplitude flow without PCM brightness drive, missing {token}")
    for token in (
        "uint8_t head_rank = step;",
        "status_led_scale_effect_percent_locked(5U, now_ms)",
        "(head_rank + (STATUS_LED_EDGE_COUNT / 2U)) % STATUS_LED_EDGE_COUNT",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: edge/frame accent must keep the current full-frame wide clockwise flow, missing {token}")
    for stale_token, reason in (
        ("uint8_t percent = level_fresh ? level_percent : breath", "recording LED must not switch brightness from fresh PCM rec_level"),
        ("status_led_recording_level_is_fresh_locked", "recording render must not gate visuals on fresh PCM rec_level"),
        ("status_led_recording_level_fresh_locked", "recording render must not read fresh PCM rec_level"),
        ("status_led_render_ec11_recording_arc_locked", "EC11 recording accent must be steady full-ring warm gold"),
        ("status_led_render_edge_recording_rails_locked", "edge recording accent must be steady full-frame warm gold"),
        ("status_led_recording_highlight_color", "recording accent must not add moving warm-gold highlights"),
        ("status_led_gleam_rank_locked", "recording accent must not keep gleam rank motion"),
        ("status_led_wrapped_rank_distance", "recording accent must not keep wrapped gleam distance"),
        ("status_led_render_ec11_recording_steady_locked", "EC11 recording accent must use the current low fixed-flow renderer"),
        ("status_led_render_edge_recording_steady_locked", "edge recording accent must use the current low fixed-flow renderer"),
    ):
        if stale_token in status_led:
            failures.append(f"status_led.c: {reason}")
    recording_level_block = re.search(
        r"void\s+status_led_set_recording_level[\s\S]*?\nstatic\s+void\s+status_led_force_recording_level_for_review",
        status_led,
    )
    if not recording_level_block:
        failures.append("status_led.c: missing status_led_set_recording_level block")
    elif "status_led_request_refresh" in recording_level_block.group(0):
        failures.append("status_led.c: live rec_level diagnostics must not request LED refresh")
    forced_level_block = re.search(
        r"static\s+void\s+status_led_force_recording_level_for_review[\s\S]*?\nvoid\s+status_led_set_processing",
        status_led,
    )
    if not forced_level_block:
        failures.append("status_led.c: missing status_led_force_recording_level_for_review block")
    elif "status_led_request_refresh" in forced_level_block.group(0):
        failures.append("status_led.c: review rec_level diagnostics must not request LED refresh")
    edge_block = re.search(
        r"static\s+void\s+status_led_render_edge_locked[\s\S]*?\nstatic\s+void\s+status_led_render_frame_locked",
        status_led,
    )
    if not edge_block:
        failures.append("status_led.c: missing status_led_render_edge_locked block")
    else:
        edge_text = edge_block.group(0)
        for stale_token, reason in (
            ("status_led_ble_repair_active_locked", "BLE re-pair must not use edge/frame cues"),
            ("s_state.ble_state == STATUS_LED_BLE_PAIRING", "pairing must not use edge/frame cues"),
            ("s_state.ble_state == STATUS_LED_BLE_RECONNECTING", "reconnect must not use edge/frame cues"),
        ):
            if stale_token in edge_text:
                failures.append(f"status_led.c: {reason}")
    if not re.search(
        r"void\s+status_led_set_recording\([^)]*\)[\s\S]*?"
        r"if\s*\(\s*s_state\.recording_active\s*\)\s*\{[\s\S]*?"
        r"status_led_clear_ok_locked\(\);[\s\S]*?"
        r"status_led_clear_retryable_error_locked\(STATUS_LED_ERROR_DOMAIN_REC\);",
        status_led,
    ):
        failures.append(
            "status_led.c: recording start must atomically clear stale OK and retryable REC warning windows"
        )
    if not re.search(
        r"void\s+status_led_set_processing\([^)]*\)[\s\S]*?"
        r"if\s*\(\s*active\s*\)\s*\{[\s\S]*?"
        r"status_led_clear_ok_locked\(\);[\s\S]*?"
        r"status_led_clear_retryable_error_locked\(STATUS_LED_ERROR_DOMAIN_AI\);[\s\S]*?"
        r"status_led_clear_retryable_error_locked\(STATUS_LED_ERROR_DOMAIN_OTA\);",
        status_led,
    ):
        failures.append(
            "status_led.c: processing start must atomically clear stale OK and retryable AI/OTA warning windows"
        )
    if not re.search(
        r"recording_processing[\s\S]*?"
        r"capture_processing[\s\S]*?"
        r"rec_ai[\s\S]*?"
        r"status_led_preview_ready_baseline_locked\(now_ms\);[\s\S]*?"
        r"s_state\.recording_active\s*=\s*true;[\s\S]*?"
        r"s_state\.processing_active\s*=\s*true;",
        status_led,
    ):
        failures.append(
            "status_led.c: recording_processing preview must add REC/AI over a ready PWR/BLE baseline"
        )
    if "uint32_t dot = (STATUS_LED_EC11_COUNT - 1U - step) % STATUS_LED_EC11_COUNT;" not in status_led:
        failures.append("status_led.c: EC11 processing and rotation motion must use the current clockwise index convention")
    if "uint32_t head_index = (STATUS_LED_EC11_COUNT - 1U - step) % STATUS_LED_EC11_COUNT;" not in status_led:
        failures.append("status_led.c: BLE re-pair EC11 orbit must use the current clockwise index convention")
    if not re.search(
        r"static\s+void\s+status_led_preview_state[^{]*\{[\s\S]*?"
        r"status_led_preview_clear_activity_locked\(\);[\s\S]*?"
        r"status_led_set_last_reason_locked\(\"preview\"\);",
        status_led,
    ):
        failures.append("status_led.c: every preview state must clear stale REC/AI/OK/WARN/BLE cue state first")
    if not re.search(
        r"status_led_render_power_locked[^{]*\{[\s\S]*?"
        r"if\s*\(\s*s_state\.preview_effect_only\s*\)\s*\{[\s\S]*?return;",
        status_led,
    ):
        failures.append("status_led.c: effect-only preview must suppress PWR rendering")
    if not re.search(
        r"status_led_render_ble_locked[^{]*\{[\s\S]*?"
        r"if\s*\(\s*s_state\.preview_effect_only\s*\)\s*\{[\s\S]*?return;",
        status_led,
    ):
        failures.append("status_led.c: effect-only preview must suppress BLE rendering")
    if "s_state.preview_effect_only = false;\n        s_state.recording_active = active" not in status_led:
        failures.append("status_led.c: real recording state changes must exit effect-only preview")
    if not re.search(
        r"status_led_render_keys_locked[^{]*\{[\s\S]*?"
        r"if\s*\(\s*!s_state\.preview_effect_only\s*\)\s*\{[\s\S]*?"
        r"status_led_set_max\(&frame->key\[index\]",
        status_led,
    ):
        failures.append("status_led.c: effect-only preview must suppress physical key feedback rendering")
    if "status_led_key_recording_percent_locked" in status_led:
        failures.append("status_led.c: recording must not light key-strip active-work accents")
    if "status_led_key_processing_percent_locked" in status_led:
        failures.append("status_led.c: processing must not light key-strip active-work accents")
    if not re.search(
        r"status_led_render_key_active_work_locked[^{]*\{[\s\S]*?"
        r"\(void\)frame;[\s\S]*?\(void\)now_ms;[\s\S]*?return;",
        status_led,
    ):
        failures.append("status_led.c: active-work key renderer must be an intentional no-op")
    if not re.search(
        r"void\s+status_led_notify_key_event[^{]*\{[\s\S]*?"
        r"if\s*\(\s*s_state\.preview_effect_only\s*\)\s*\{[\s\S]*?"
        r"xSemaphoreGive\(s_mutex\);[\s\S]*?return;",
        status_led,
    ):
        failures.append("status_led.c: key events must not exit effect-only preview")
    if not re.search(
        r"if\s*\(\s*power_visual_changed\s*&&\s*!s_state\.preview_effect_only\s*\)\s*\{[\s\S]*?"
        r"s_state\.last_transition_ms\s*=\s*now_ms;[\s\S]*?"
        r"status_led_set_last_reason_locked\(\"power_change\"\);",
        status_led,
    ):
        failures.append("status_led.c: effect-only power samples must not reset LED effect phase")
    volume_block = re.search(
        r"function\s+Get-VolumeSteps\s*\{([\s\S]*?)\nfunction\s+Get-RootCauseSteps",
        human_review,
    )
    if not volume_block:
        failures.append("status_led_human_effect_review.ps1: missing Get-VolumeSteps block")
    else:
        volume_text = volume_block.group(1)
        for token in ("~LED:PREVIEW capture_led_only", "~LED:PREVIEW recording_processing_led_only"):
            if token not in volume_text:
                failures.append(f"status_led_human_effect_review.ps1: Volume mode must use effect-only command {token}")
        for stale in ('"~LED:PREVIEW capture"', '"~LED:PREVIEW recording_processing"'):
            if stale in volume_text:
                failures.append(f"status_led_human_effect_review.ps1: Volume mode must not use mixed product preview {stale}")
    complex_block = re.search(
        r"function\s+Get-ComplexSteps\s*\{([\s\S]*?)\nfunction\s+Get-VolumeSteps",
        human_review,
    )
    if not complex_block:
        failures.append("status_led_human_effect_review.ps1: missing Get-ComplexSteps block")
    else:
        complex_text = complex_block.group(1)
        for token in (
            "~LED:PREVIEW recording_processing_led_only",
            "~LED:PREVIEW recording_processing_status_led_only",
            "~LED:PREVIEW capture_led_only",
            "~LED:PREVIEW processing_led_only",
        ):
            if token not in complex_text:
                failures.append(f"status_led_human_effect_review.ps1: Complex mode must use effect-only command {token}")
    product_block = re.search(
        r'if\s*\(\$Mode\s+-eq\s+"Product"\)\s*\{([\s\S]*?)\n\s*\$rootCause',
        human_review,
    )
    if not product_block:
        failures.append("status_led_human_effect_review.ps1: missing Product review block")
    else:
        product_text = product_block.group(1)
        for token in ("scene-ec11-short-press", "scene-ec11-rotate", "scene-key-feedback"):
            if token not in product_text:
                failures.append(f"status_led_human_effect_review.ps1: Product review must include physical input step {token}")
        for stale_token in (
            'volume-capture-sweep',
            'complex-recording-processing-product',
            'complex-processing-only',
        ):
            if stale_token in product_text:
                failures.append(f"status_led_human_effect_review.ps1: Product review must not include repeated tuning step {stale_token}")
    voice_recording_control = read("components/voice_recording_control/voice_recording_control.c")
    ble_hid_gap = read("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    if 'status_led_set_processing(true, "audio_session_finishing")' in voice_recording_control:
        failures.append("voice_recording_control.c: audio transfer must not light AI processing LED")
    if 'status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_RETRYABLE, "voice_recovery_requested")' in voice_recording_control:
        failures.append("voice_recording_control.c: user-requested recovery must use BLE re-pair cue, not WARN/error")
    if 'status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_RETRYABLE, "ble_recovery_clear_bonds")' in ble_hid_gap:
        failures.append("ble_hid_gap_esp32.c: clearing bonds for user-requested re-pair must use BLE cue, not WARN/error")
    for token in (
        'status_led_notify_success("recording_stop_done")',
        'status_led_notify_success("recording_session_done")',
        'status_led_notify_success("host_processing_done")',
    ):
        if token not in voice_recording_control:
            failures.append(f"voice_recording_control.c: missing success LED confirmation {token}")
    if "driver/rmt_" in status_led or "soc/soc_caps.h" in status_led:
        failures.append("status_led.c: business rendering layer must not include the RMT/WS2812 backend directly")
    if "STATUS_LED_KEY_RECORDING_MAX_PERCENT" in status_led:
        failures.append("status_led.c: key recording active-work cap should be removed")
    if "STATUS_LED_KEY_PROCESSING_MAX_PERCENT" in status_led:
        failures.append("status_led.c: key processing active-work cap should be removed")
    if "status_led_set_max(&frame->key[index], ok)" in status_led:
        failures.append("status_led.c: OK success must not recolor key LEDs")
    if "status_led_rgb(160, 0, 255), 52U" in status_led:
        failures.append("status_led.c: routine AI processing must not recolor key LEDs with the old high purple level")
    if "status_led_token_locked(status_led_rec_gold(), breath, true)" in status_led:
        failures.append("status_led.c: REC must respect user brightness; do not render it as safety brightness")
    if "status_led_token_locked(status_led_rgb(160, 0, 255), breath, true)" in status_led:
        failures.append("status_led.c: routine AI must respect user brightness; do not render it as safety brightness")
    if "status_led_token_locked(status_led_rgb(255, 255, 255), percent, true)" in status_led:
        failures.append("status_led.c: routine external-power PWR white must respect user brightness")
    if "percent = status_window ? 46U : (connected_ready ? 30U : 0U)" in status_led:
        failures.append("status_led.c: battery PWR must not stay on just because BLE is connected")
    if "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT" not in status_led:
        failures.append("status_led.c: battery idle BLE heartbeat constants are missing")
    if "(!external_power_present && battery_display_band_changed)" not in status_led:
        failures.append("status_led.c: plugged/raw battery-percent jitter must not extend status windows")
    if "s_state.profile == STATUS_LED_PROFILE_STANDARD && now_ms < s_state.status_window_until_ms" in status_led:
        failures.append("status_led.c: standard profile edge LEDs must not light from generic status windows")
    if "return desired_percent < cap ? desired_percent : cap" in status_led:
        failures.append("status_led.c: user brightness must scale the whole routine effect envelope, not only clamp max brightness")
    if "status_led_triangle_percent(now_ms, 2400U, 42U, 85U)" in status_led:
        failures.append("status_led.c: REC must not use the old high-amplitude breath on the status rail")
    if "strip_mask |= STATUS_LED_STRIP_MASK_STATUS" in status_led:
        failures.append("status_led.c: unchanged status rail must not be retransmitted just because EC11/edge accents changed")
    if not re.search(
        r"status_led_render_recording_locked[\s\S]*?"
        r"status_led_recording_status_percent_locked\(now_ms\)",
        status_led,
    ):
        failures.append("status_led.c: recording semantic LED must use the capped quantized status brightness helper")
    if not re.search(
        r"status_led_render_processing_locked[\s\S]*?"
        r"status_led_processing_status_percent_locked\(now_ms\)",
        status_led,
    ):
        failures.append("status_led.c: processing semantic LED must use the capped quantized status brightness helper")
    if not re.search(
        r"status_led_render_power_locked\(frame, now_ms, &safety\);[\s\S]*?"
        r"status_led_render_ble_locked\(frame, now_ms\);[\s\S]*?"
        r"status_led_render_shutdown_confirm_locked\(frame, now_ms\)[\s\S]*?"
        r"status_led_apply_status_tail_guard_locked\(frame, now_ms\);[\s\S]*?"
        r"return;[\s\S]*?"
        r"status_led_render_recording_locked\(frame, now_ms, &safety\);",
        status_led,
    ):
        failures.append("status_led.c: shutdown confirmation must render after PWR/BLE and before REC/AI effects")
    if not re.search(
        r"status_led_render_ok_locked\(frame, now_ms\);[\s\S]*?"
        r"status_led_render_error_locked\(frame, now_ms, &safety\);[\s\S]*?"
        r"status_led_apply_status_tail_guard_locked\(frame, now_ms\);",
        status_led,
    ):
        failures.append("status_led.c: OK/WARN tail guard must run after normal OK/error rendering")
    if "frame->status[STATUS_LED_SEM_OK] = (status_led_rgb_t){0};" not in status_led:
        failures.append("status_led.c: OK tail guard must explicitly clear LED5 when inactive")
    if "frame->status[STATUS_LED_SEM_WARN] = (status_led_rgb_t){0};" not in status_led:
        failures.append("status_led.c: WARN tail guard must explicitly clear LED6 when inactive")
    if not re.search(
        r"void\s+status_led_notify_shutdown_confirm\([^)]*\)[\s\S]*?"
        r"s_state\.low_power_disabled\s*=\s*false;[\s\S]*?"
        r"s_state\.output_disabled\s*=\s*false;",
        status_led,
    ):
        failures.append("status_led.c: shutdown confirmation must wake LED output even from low-power-off state")
    if re.search(r"\.mem_block_symbols\s*=\s*64\b", status_led_backend):
        failures.append(
            "status_led_strip_backend.c: RMT mem_block_symbols=64 consumes two ESP32-S3 RMT blocks per strip and leaves fewer than four TX channels"
        )
    if not re.search(r"\.mem_block_symbols\s*=\s*SOC_RMT_MEM_WORDS_PER_CHANNEL\b", status_led_backend):
        failures.append(
            "status_led_strip_backend.c: RMT strip channels must use SOC_RMT_MEM_WORDS_PER_CHANNEL so all four V2 LED zones can initialize"
        )
    if not re.search(
        r"status_led_strip_backend_fill_pixels\(backend, color_order, colors\);[\s\S]*?"
        r"rmt_encoder_reset\(backend->encoder\);[\s\S]*?"
        r"rmt_transmit\(",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: RMT encoder must reset before each strip transmit")
    if status_led_backend.count("rmt_encoder_reset(backend->encoder)") < 2:
        failures.append("status_led_strip_backend.c: invalid-state retry must also reset the RMT encoder")

    led_init_index = main_c.find("status_led_init()")
    led_start_index = main_c.find("status_led_start()")
    diag_log_index = main_c.find("diag_log_init()")
    post_index = main_c.find("self_test_run()")
    ble_init_index = main_c.find("ble_hid_init()")
    keyboard_start_index = main_c.find("keyboard_start")
    boot_window_index = main_c.find('status_led_show_status_window("booting")')
    if not (0 <= led_init_index < led_start_index < post_index):
        failures.append(
            "main.c: status_led_start() must run immediately after status_led_init() and before POST for cold-boot feedback"
        )
    if not (0 <= led_init_index < led_start_index < diag_log_index):
        failures.append(
            "main.c: status_led_start() must run before diag_log_init() so flash log replay cannot delay the PWR boot light"
        )
    if not (0 <= led_start_index < ble_init_index and led_start_index < keyboard_start_index):
        failures.append(
            "main.c: status_led_start() must not wait for BLE HID or keyboard startup"
        )
    if not (0 <= led_start_index < boot_window_index < post_index):
        failures.append(
            'main.c: cold boot must request status_led_show_status_window("booting") before POST'
        )

    board_leds = read("components/board/board.c")
    if "BOARD_LED_PREFIX" in board_leds or 'board_command_matches(line, BOARD_LED_PREFIX' in board_leds:
        failures.append("board.c: board_consume_usb_command must not swallow LED: commands before status_led")
    if 'key="LED7..LED10" edge="LED11..LED16"' in board_leds:
        failures.append("board.c: stale three-zone LED map")
    if "LED15..LED28" in board_leds and "LED23..LED28" not in board_leds:
        failures.append("board.c: stale edge LED15..LED28 map")

    ble_hid = read("ports/esp32/ble_hid/ble_hid.c")
    status_dispatch = ble_hid.find("status_led_consume_usb_command(line)")
    board_dispatch = ble_hid.find("board_consume_usb_command(line)")
    if status_dispatch < 0:
        failures.append("ble_hid.c: missing status_led_consume_usb_command dispatch")
    if board_dispatch >= 0 and status_dispatch > board_dispatch:
        failures.append("ble_hid.c: status_led_consume_usb_command must run before board_consume_usb_command")

    voice_recording_control = read("components/voice_recording_control/voice_recording_control.c")
    forbidden_voice_recording_tokens = {
        'status_led_set_processing(true, "audio_session_finishing")':
            "audio transfer must not light AI processing LED; wait for host PROCESSING:START",
        'status_led_set_processing(false, "recording_session_cleanup")':
            "host cleanup/STOP must not clear AI processing LED; wait for host PROCESSING:STOP or DONE",
        'status_led_set_processing(false, "recording_session_finished")':
            "firmware transfer completion must not clear AI processing LED; wait for host PROCESSING:STOP or DONE",
    }
    for token, message in forbidden_voice_recording_tokens.items():
        if token in voice_recording_control:
            failures.append(f"voice_recording_control.c: {message}")

    if failures:
        print("FAIL: status LED static verification failed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(
        "PASS: status LED static verification covers V2 four-zone WS2812 resources, "
        "EC11 GPIO5/count12, key GPIO13/count4, edge GPIO4/count6, diagnostics, "
        "USB validation hooks, camera/manual one-pixel status/key/EC11/edge harness, and low-power off path."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
