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
        r"BOARD_PINS_USB_DET_IO\s+\(GPIO_NUM_NC\)",
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
        "status_led_set_ota_active",
        "status_led_notify_warning",
        "status_led_notify_shutdown_confirm",
        "status_led_cancel_shutdown_confirm",
        "STATUS_LED_EC11_FEEDBACK_PRESS",
        "STATUS_LED_EC11_FEEDBACK_ROTATE_CW",
        "STATUS_LED_EC11_FEEDBACK_ROTATE_CCW",
        "status_led_notify_ec11_feedback",
        "status_led_prepare_sleep",
        "STATUS_LED_REC_SOURCE_DEVICE_MIC",
        "STATUS_LED_REC_SOURCE_DESKTOP_MIC",
        "STATUS_LED_REC_SOURCE_NOT_AVAILABLE",
        "STATUS_LED_ERROR_DOMAIN_OTA",
        "STATUS_LED_ERROR_DOMAIN_POWER",
        "STATUS_LED_ERROR_DOMAIN_SYSTEM",
    ],
    "components/status_led/status_led.c": [
        "STATUS_LED_STATUS_COUNT 6",
        "STATUS_LED_EC11_COUNT 12",
        "STATUS_LED_KEY_COUNT 4",
        "STATUS_LED_EDGE_COUNT 6",
        "STATUS_LED_STRIP_COUNT 4",
        "STATUS_LED_STATUS_TAIL_GUARD_PIXELS 6U",
        "STATUS_LED_STATUS_TAIL_REINFORCE_WRITES 3U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_REINFORCE_WRITES 1U",
        "STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MIN_PERCENT 14U",
        "STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MAX_PERCENT 20U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MIN_PERCENT 14U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MAX_PERCENT 40U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_BREATH_PERIOD_MS 6800U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_RISE_MS 2200U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_HIGH_HOLD_MS 500U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_FALL_MS 2500U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_LOW_HOLD_MS 1600U",
        "STATUS_LED_STATUS_TAIL_OVERLAP_QUANTUM_PERCENT 1U",
        "STATUS_LED_TYPE_DEFINED_MAX_PERCENT 100U",
        "STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT 8U",
        "STATUS_LED_RECORDING_LEVEL_EFFECT_MAX_PERCENT 100U",
        "STATUS_LED_RECORDING_LEVEL_ATTACK_PERCENT_PER_SEC 100U",
        "STATUS_LED_RECORDING_LEVEL_RELEASE_PERCENT_PER_SEC 45U",
        "STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT 2U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT 0U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT 100U",
        "STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT 2U",
        "STATUS_LED_PROCESSING_THINK_PERIOD_MS 1950U",
        "STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS 50U",
        "STATUS_LED_PROCESSING_THINK_BEAT_HOLD_MS 50U",
        "STATUS_LED_PROCESSING_THINK_BEAT_FALL_MS 60U",
        "STATUS_LED_PROCESSING_THINK_GROUP_GAP_MS 520U",
        "STATUS_LED_PROCESSING_THINK_BEAT_GAP_MS 50U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_BEAT2_PERCENT 78U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_BEAT3_PERCENT 100U",
        "recording_level_visual_percent",
        '"recording_active"',
        '"capture_active"',
        "status_led_processing_thinking_phase_ms_locked",
        "status_led_recording_level_smoothed_percent_locked",
        "status_led_recording_level_effect_percent_locked",
        "status_led_processing_thinking_effect_percent_locked",
        "status_led_processing_thinking_color_locked",
        "return status_led_rgb(160, 0, 255)",
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
        "STATUS_LED_KEY_FEEDBACK_MS 700U",
        "STATUS_LED_EC11_FEEDBACK_MS 1400U",
        "STATUS_LED_EC11_ROTATION_HOLD_MS 2600U",
        "STATUS_LED_EC11_ROTATION_STEP_MS 150U",
        "STATUS_LED_EC11_ROTATION_HEAD_START_PERCENT 72U",
        "STATUS_LED_EC11_ROTATION_HEAD_END_PERCENT 44U",
        "STATUS_LED_EC11_ROTATION_BASE_START_PERCENT 4U",
        "STATUS_LED_EC11_ROTATION_BASE_END_PERCENT 1U",
        "status_led_scale_raw(white, 60U)",
        "status_led_scale_raw(white, 32U)",
        "STATUS_LED_BOOT_ACK_MS 2500U",
        "STATUS_LED_OK_TOTAL_MS 2000U",
        "ok_warning",
        "status_led_notify_warning",
        "STATUS_LED_CHARGING_BREATH_PERIOD_MS 3600U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1300U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_UNKNOWN_FLOOR_PERCENT 8U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 12U",
        "STATUS_LED_CHARGE_FULL_DEBOUNCE_MS 10000U",
        "STATUS_LED_CHARGE_FULL_MIN_MV 4050U",
        "STATUS_LED_CHARGE_FULL_MIN_PERCENT 88U",
        "STATUS_LED_BATTERY_DISPLAY_GREEN_PERCENT 60U",
        "STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT 14U",
        "STATUS_LED_BATTERY_STATUS_WINDOW_LOW_PROFILE_PWR_PERCENT STATUS_LED_LOW_POWER_PWR_PERCENT",
        "STATUS_LED_LOW_BATTERY_STEADY_PERCENT 24U",
        "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT 10U",
        "STATUS_LED_FULL_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_LOW_POWER_PWR_PERCENT 12U",
        "STATUS_LED_LOW_POWER_PWR_WHITE_PERCENT 4U",
        "STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT 12U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS 120U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS 7880U",
        "STATUS_LED_OK_SUCCESS_BLUE_BALANCE 0U",
        "STATUS_LED_PREVIEW_BLE_OVERRIDE_MS 15000U",
        "STATUS_LED_BLE_REPAIR_CUE_MS 2700U",
        "STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT 4U",
        "STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT 16U",
        "STATUS_LED_BLE_CONNECTED_CONFIRM_MS 1600U",
        "STATUS_LED_PWR_COLOR_AMBER",
        "STATUS_LED_DIAG_VIS_LOW_POWER_OFF",
        "battery_display_level_percent",
        "status_led_battery_display_available_locked",
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
        "STATUS_LED_CONTRACT_REV \"status_key_ec11_edge_true_state_v26\"",
        "STATUS_LED_EC11_ACCENT_MIN_PERCENT",
        "STATUS_LED_EC11_ACCENT_MAX_PERCENT",
        "STATUS_LED_EC11_OK_ACCENT_MAX_PERCENT",
        "STATUS_LED_EDGE_ACCENT_MIN_PERCENT",
        "STATUS_LED_EDGE_ACCENT_MAX_PERCENT",
        "STATUS_LED_EDGE_OK_ACCENT_MAX_PERCENT",
        "STATUS_LED_POWER_SOURCE_CHARGER_STATUS",
        "STATUS_LED_DIAG_POWER_EXTERNAL_CHARGER_STATUS",
        "status_led_gpio_mask",
        "STATUS_LED_DIAG_ACTIVE_EC11",
        "boot_feedback_until_ms",
        "status_led_force_boot_feedback",
        "status_led_boot_power_color_locked",
        "STATUS_LED_NVS_BRIGHTNESS_KEY \"brightness\"",
        "led_contract_rev=",
        "rmt_tx_dma_supported=%u",
        "strip_transport_requested=status:rmt,ec11:spi2,key:spi3,edge:rmt",
        "strip_transport_actual=status:%s,ec11:%s,key:%s,edge:%s",
        "spi_dma_requested=status:%u,ec11:%u,key:%u,edge:%u",
        "spi_dma_actual=status:%u,ec11:%u,key:%u,edge:%u",
        "spi_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u",
        "rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer",
        "rmt_strip_all_available=%u",
        "rmt_tx_dma_all_strips=%u",
        "rmt_tx_dma_requested=status:%u,ec11:%u,key:%u,edge:%u",
        "rmt_tx_dma_actual=status:%u,ec11:%u,key:%u,edge:%u",
        "rmt_tx_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u",
        "rmt_mem_block_symbols=status:%u,ec11:%u,key:%u,edge:%u",
        "unchanged_tx_suppression=1",
        "timing=ws2812_4020_compatible",
        "status_tail_guard_pixels=%u",
        "status_tail_reinforce=recording_processing",
        "status_tail_overlap_reinforce_writes=%u",
        "status_tail_legacy_safe_effect_percent=%u..%u",
        "status_tail_overlap_effect_percent=rec_audio_%u..%u_%upct_ai_think_%u..%u_%upct",
        "status_tail_overlap_style=dma_audio_rec_ai_da_dada",
        "status_tail_overlap_legacy_effect_percent=%u..%u_1pct_eased_dual_core",
        "status_tail_overlap_legacy_period_ms=%u",
        "status_tail_overlap_legacy_rise_ms=%u",
        "status_tail_overlap_legacy_high_hold_ms=%u",
        "status_tail_overlap_legacy_fall_ms=%u",
        "status_tail_overlap_legacy_low_hold_ms=%u",
        "status_tail_overlap_legacy_quantum_percent=%u",
        "recording_level_reactive=1",
        "recording_level_effect_percent=%u..%u_smooth_%upct",
        "recording_level_smoothing=attack%u_release%u",
        "processing_thinking_style=single_then_double_beat",
        "processing_thinking_color=purple_static",
        "processing_thinking_effect_percent=%u..%u_%upct",
        "processing_thinking_scan_profile=da_long_gap_grouped_dada_rest",
        "processing_thinking_period_ms=%u",
        "rec_level_visual=%u",
        "active_work_status_overlap_dynamic_1pct_eased=0",
        "active_work_status_audio_reactive_rec=1",
        "active_work_status_thinking_ai=1",
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
        "STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT 16U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT 14U",
        "STATUS_LED_EC11_RECORDING_FLOW_STEP_MS 360U",
        "STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS 720U",
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
        "power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false)",
        "power_manager_record_activity(\"usb_led_command\")",
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
        "STATUS_LED_BLE_REPAIR_MIN_PERCENT 30U",
        "STATUS_LED_BLE_REPAIR_MAX_PERCENT 100U",
        "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT 10U",
        "STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 12U",
        "STATUS_LED_BLE_ATTENTION_PERCENT 18U",
        "STATUS_LED_BLE_PAIRING_PULSE_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT",
        "STATUS_LED_BLE_RECONNECT_MIN_PERCENT 10U",
        "STATUS_LED_BLE_RECONNECT_MAX_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT",
        "STATUS_LED_BLE_CONNECTED_CONFIRM_MIN_PERCENT 10U",
        "STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT 14U",
        "STATUS_LED_BLE_CONNECTED_BASE_PERCENT 4U",
        "STATUS_LED_BLE_CONNECTED_PULSE_PERCENT STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT",
        "STATUS_LED_BLE_CONNECTED_HEARTBEAT_PERIOD_MS 2600U",
        "STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT",
        "case STATUS_LED_BLE_TYPE_READY: return \"type_ready\"",
        "status_led_ble_state_ready_locked",
        "status_led_connected_hid_only_percent_locked",
        "ble_elapsed_ms < STATUS_LED_BLE_CONNECTED_CONFIRM_MS",
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
        "bool effect_only_after = false;",
        "bool keep_usb_command_blocker_after = false;",
        "keep_usb_command_blocker_after = true;",
        "effect_only_after = s_state.preview_effect_only || keep_usb_command_blocker_after;",
        "power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, effect_only_after)",
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
        "STATUS_LED_ACTIVE_WORK_REC_MAX_PERCENT 100U",
        "STATUS_LED_PROCESSING_BREATH_PERIOD_MS 1800U",
        "STATUS_LED_PROCESSING_BREATH_MIN_PERCENT 28U",
        "STATUS_LED_PROCESSING_BREATH_MAX_PERCENT 100U",
        "STATUS_LED_ACCENT_BREATHE_QUANTUM_PERCENT 2U",
        "STATUS_LED_EC11_RECORDING_BASE_MIN_PERCENT 12U",
        "STATUS_LED_EC11_ACCENT_MAX_PERCENT 100U",
        "STATUS_LED_EDGE_ACCENT_MAX_PERCENT 100U",
        "STATUS_LED_EC11_OK_ACCENT_MAX_PERCENT 36U",
        "STATUS_LED_EDGE_OK_ACCENT_MAX_PERCENT 36U",
        "STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT 16U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MIN_PERCENT 10U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT 14U",
        "STATUS_LED_ACCENT_ENTRY_RAMP_MS 900U",
        "STATUS_LED_EC11_ORBIT_STEP_MS 240U",
        "STATUS_LED_EDGE_ORBIT_STEP_MS 480U",
        "STATUS_LED_EC11_PROCESSING_BASE_PERCENT 12U",
        "STATUS_LED_EDGE_PROCESSING_BASE_PERCENT 10U",
        "STATUS_LED_EC11_PROCESSING_ORBIT_PERCENT 40U",
        "STATUS_LED_EDGE_PROCESSING_ORBIT_PERCENT 36U",
        "STATUS_LED_OTA_OK_MIN_PERCENT",
        "STATUS_LED_OTA_OK_MAX_PERCENT",
        "STATUS_LED_OTA_EC11_FILL_PERCENT",
        "STATUS_LED_OTA_EDGE_HEAD_PERCENT",
        "status_led_set_ota_active",
        "status_led_render_ota_locked",
        "status_led_render_ec11_ota_locked",
        "status_led_ota_progress_percent_locked",
        "ota_active=%u",
        "ota_progress_percent=%u",
        "ota_progress_style=LED5_OK_cyan_pulse_EC11_progress_EDGE_chase",
        "ota_progress_idle_blocker=POWER_MANAGER_BLOCKER_OTA",
        "status_led_effect_elapsed_ms_locked",
        "status_led_set_recording_level",
        "rec_level=%u",
        "rec_level_visual=%u",
        "return status_led_quantize_percent(percent, STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT)",
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
        "status_led_ec11_feedback_motion_step_locked(uint32_t now_ms)",
        "status_led_ec11_feedback_trail_index",
        "status_led_ec11_feedback_active_locked(now_ms)",
        "status_led_apply_ec11_feedback",
        "status_led_notify_ec11_feedback",
        "status_led_refresh_ec11_feedback",
        "advance_motion",
        "status_led_clear_ec11_feedback_locked",
        "STATUS_LED_SHUTDOWN_CONFIRM_MS 1200U",
        "STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS 1400U",
        "status_led_render_shutdown_confirm_locked",
        "if (final) {\n        memset(frame->status, 0, sizeof(frame->status));\n        memset(frame->ec11, 0, sizeof(frame->ec11));\n        memset(frame->key, 0, sizeof(frame->key));\n        memset(frame->edge, 0, sizeof(frame->edge));",
        "frame->status[STATUS_LED_SEM_PWR] = status_led_token_locked(amber, pwr_percent, false);",
        "uint8_t accent_percent = 24U;",
        "clockwise_index = (STATUS_LED_EC11_COUNT - 1U - index) % STATUS_LED_EC11_COUNT",
        "shutdown_confirm_active=%u shutdown_confirm_final=%u shutdown_confirm_latched=%u shutdown_confirm_elapsed_ms=%",
        "shutdown_confirm_active=%u shutdown_confirm_latched=%u",
        "status_led_apply_status_tail_guard_locked",
        "status_led_transmit_strip(\n                &s_strips[STATUS_LED_STRIP_STATUS],\n                frame->status,\n                force_non_dma)",
        "status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EC11], frame->ec11, force_non_dma)",
        "status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_KEY], frame->key, force_non_dma)",
        "status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EDGE], frame->edge, force_non_dma)",
        "low_power_all_zone_tx=non_dma_clear_and_final_frame",
        "shutdown_final_all_zone_tx=non_dma_pwr_only_latch_or_all_off",
        "status_led_notify_shutdown_confirm",
        "status_led_cancel_shutdown_confirm",
        "shutdown_confirm_started_ms",
        "s_state.low_power_disabled = false;",
        "shutdown_final",
        "status_led_smoothstep_per_mille",
        "STATUS_LED_CHARGING_BREATH_UNKNOWN_FLOOR_PERCENT",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "status_led_charging_breath_floor_percent_locked",
        "status_led_charging_breath_lerp_percent",
        "status_led_charging_breath_percent_locked",
        "status_led_charging_breath_percent_locked(now_ms)",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT",
        "STATUS_LED_EXTERNAL_POWER_POLL_MS 250U",
        "STATUS_LED_CHARGER_STATUS_EXTERNAL_HOLD_MS 1000U",
        "status_led_power_poll_interval_ms_locked",
        "s_state.external_power_present",
        "raw_full_external",
        "status_led_charger_status_external_locked(\n                raw_charging,\n                raw_full_external,",
        "external_power_source_flags |= STATUS_LED_POWER_SOURCE_CHARGER_STATUS",
        "s_state.charge_full_latched = true",
        "s_state.charge_full_latched = false",
        "raw_charging=%u raw_full=%u",
        "full_latched=%u full_candidate_ms=%",
        "external_power=%u external_power_source=%s",
        "status_led_rgb(255, 255, 255), percent, false",
        "active_flags=PWR:%u,BLE:%u,REC:%u,AI:%u,OK:%u,WARN:%u,EC11:%u,KEY:%u,EDGE:%u",
        "status_rgb=PWR:%u,%u,%u;BLE:%u,%u,%u;REC:%u,%u,%u",
        "if (state_changed && !effect_only && !routine_low_power_ble) {\n            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;",
        "if (state_changed && state == STATUS_LED_BLE_CONNECTED && confidence_window)",
        "state == STATUS_LED_BLE_PAIRING || state == STATUS_LED_BLE_RECONNECTING",
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
        "prefer_dma",
        "status_led_rgb_t",
        "status_led_strip_backend_t",
        "status_led_strip_backend_new",
        "status_led_strip_backend_dma_supported",
        "status_led_strip_backend_transport",
        "status_led_strip_backend_dma_requested",
        "status_led_strip_backend_uses_dma",
        "status_led_strip_backend_dma_fallback",
        "status_led_strip_backend_mem_block_symbols",
        "status_led_strip_backend_transmit",
        "status_led_strip_backend_suspend",
    ],
    "components/status_led/status_led_strip_backend.c": [
        "driver/rmt_encoder.h",
        "driver/rmt_tx.h",
        "driver/spi_master.h",
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
        "STATUS_LED_RMT_WITH_DMA SOC_RMT_SUPPORT_DMA",
        "STATUS_LED_RMT_DMA_MEM_BLOCK_SYMBOLS 1024U",
        "STATUS_LED_SPI_CLOCK_HZ       2500000",
        "STATUS_LED_SPI_BITS_PER_BIT   3U",
        "STATUS_LED_SPI_RESET_BYTES    96U",
        "spi_bus_initialize",
        "SPI_DMA_CH_AUTO",
        ".mosi_io_num = backend->gpio",
        ".miso_io_num = -1",
        ".sclk_io_num = -1",
        ".spics_io_num = -1",
        "falling back to non-DMA RMT",
        "status_led_strip_backend_new_channel",
        "rmt_dma_requested=%u",
        "rmt_dma_fallback=%u",
        "mem_block_symbols=%u",
        "status_led_strip_backend_dma_supported",
        "status_led_strip_backend_dma_requested",
        "status_led_strip_backend_uses_dma",
        "status_led_strip_backend_dma_fallback",
        "status_led_strip_backend_mem_block_symbols",
        "status_led_strip_backend_suspend",
        "SOC_RMT_MEM_WORDS_PER_CHANNEL",
        ".mem_block_symbols = mem_block_symbols",
        ".flags.with_dma = with_dma",
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
        "5=rotate_identity",
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
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_POWER, STATUS_LED_ERROR_HARD, \"power_manager_init_failed\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_POWER, STATUS_LED_ERROR_HARD, \"power_manager_start_failed\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_SYSTEM, STATUS_LED_ERROR_HARD, \"post_failed\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_SYSTEM, STATUS_LED_ERROR_HARD, \"boot_safety_safe_mode\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_SYSTEM, STATUS_LED_ERROR_HARD, \"keyboard_start_failed\")",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, \"ble_hid_init_failed\")",
    ],
    "ports/esp32/ble_hid/ble_hid.c": [
        "status_led_consume_usb_command(line)",
        "status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, true)",
        "status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false)",
        "ble_audio_stream_type_link_poll_wait_ms(",
        "ble_audio_stream_poll_type_link()",
    ],
    "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c": [
        "BLE_AUDIO_STREAM_TYPE_HEARTBEAT_TIMEOUT_MS 12000",
        "BLE_AUDIO_STREAM_TYPE_LED_READY_HOLD_MS 30000",
        "TYPE:READY",
        "TYPE:HB",
        "TYPE:BYE",
        "ble_audio_stream_type_heartbeat_recent()",
        "ble_audio_stream_type_heartbeat_led_recent()",
        "ble_audio_stream_is_type_led_ready()",
        "ble_audio_stream_sync_status_led_for_type_link(\"type_heartbeat_timeout\")",
        "type_heartbeat_led_grace_timeout",
    ],
    "ports/esp32/ble_audio_stream/include/ble_audio_stream.h": [
        "ble_audio_stream_is_type_led_ready",
        "ble_audio_stream_consume_type_control_command",
        "ble_audio_stream_type_link_poll_wait_ms",
        "ble_audio_stream_poll_type_link",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false)",
        "status_led_notify_ble_repairing(\"ble_recovery_clear_bonds\")",
        "status_led_notify_ble_repairing(\"ble_recovery_refresh_pairing\")",
        "s_recovery_identity_rotate_pending",
        "ble_hid_gap_store_static_random_identity",
        "ble_hid_gap_rotate_static_random_identity",
        "ble_hs_id_gen_rnd(0, &addr)",
        "BLE recovery identity rotated after disconnect",
        "BLE recovery identity rotated for re-pair",
        "BLE recovery identity rotated during active pairing window",
        "stale pairing encryption failure",
        "BLE identity rotated and device is discoverable for first-time pairing",
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
        "PROCESSING:WARN",
        "PROCESSING_WARN",
        "status_led_set_processing(true, \"recording_stop_processing_start\")",
        "voice_recording_control_host_processing_start(source)",
        "voice_recording_control_host_processing_stop(source)",
        "voice_recording_control_host_processing_done(source)",
        "voice_recording_control_host_processing_warning(source)",
        "status_led_notify_warning(\"host_processing_warning\")",
        "status_led_notify_success(\"host_processing_done\")",
        "status_led_notify_ble_repairing(\"voice_recovery_requested\")",
        "ble_audio_stream_consume_type_control_command(command, source)",
        "power_manager_record_activity(\"voice_recording_ble_control\")",
    ],
    "ports/esp32/audio_capture/audio_capture_esp32.c": [
        "#include \"status_led.h\"",
        "AUDIO_CAPTURE_LEVEL_NOISE_FLOOR",
        "AUDIO_CAPTURE_LEVEL_NOISE_FLOOR 160U",
        "AUDIO_CAPTURE_LEVEL_FULL_SCALE",
        "AUDIO_CAPTURE_LEVEL_FULL_SCALE 5000U",
        "audio_capture_frame_level_percent",
        "status_led_set_recording_level(audio_capture_frame_level_percent(frame_buffer))",
    ],
    "ports/esp32/audio_capture/CMakeLists.txt": [
        "status_led",
    ],
    "ports/esp32/system_health_platform/system_health_esp32.c": [
        "status_led_set_error",
        "SYSTEM_HEALTH_STATUS_LED_ERROR_DOMAIN_SYSTEM 6",
        "system_health_notify_led_warning(\n                SYSTEM_HEALTH_STATUS_LED_ERROR_DOMAIN_SYSTEM,\n                \"health_heap_pressure\")",
        "ble unstable:",
        "Do not turn idle into a WARN LED state",
    ],
    "components/firmware_ota/firmware_ota.c": [
        "firmware_ota_set_runtime_active(true, 0, image_size, \"ota_begin\")",
        "firmware_ota_set_runtime_active(false, bytes_written, expected_size, \"ota_finish\")",
        "status_led_set_ota_active(active, bytes_written, expected_size, reason)",
        "POWER_MANAGER_BLOCKER_OTA",
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA",
    ],
    "components/power_manager/power_manager.c": [
        "status_led_prepare_sleep",
        "status_led_set_low_power_disabled",
        "status_led_notify_shutdown_confirm",
        "status_led_set_error",
        "POWER_MANAGER_STATUS_LED_ERROR_DOMAIN_POWER 5",
        "POWER_MANAGER_STATUS_LED_ERROR_HARD 1",
        "power_manager_notify_power_led_error(true, \"hardware_shutdown_failed\")",
        "power_manager_notify_power_led_error(false, \"low_battery_shutdown_rejected\")",
        "POWER_MANAGER_SHUTDOWN_LED_CONFIRM_MS 1200U",
        "hardware_shutdown_confirmed",
    ],
    "ports/esp32/voice_key_input/voice_key_input_esp32.c": [
        "#include \"status_led.h\"",
        "status_led_notify_shutdown_confirm(false, \"ec11_long_press_shutdown_confirm\")",
        "status_led_cancel_shutdown_confirm(\"ec11_long_press_released\")",
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
        "rotates the stored BLE static-random identity before advertising again",
        "The old host must pair again instead of silently reconnecting to the previous device identity",
        "ble_repair_ms_left",
        "short blue connected-success confirmation",
        "BLE animation phase is tracked separately",
        "`ble_transition_ms`",
        "Manual `~LED:PREVIEW` scenes temporarily hold their requested BLE state",
        "for 15 seconds",
        "`preview_ble_override_ms_left`",
        "Reconnect keeps a low blue BLE floor",
        "pairing, reconnect, and user-requested re-pair can still blink BLE as attention states",
        "Re-pair uses a BLE plus EC11 confirmation cue",
        "current render-sampled RGB frame",
        "status_query_samples_current_render=1",
        "ordinary HID-only `connected` stays visible as a low-base blue double-flash heartbeat",
        "`TYPE_READY` is the Listener-Type-ready BLE state",
        "it uses steady blue",
        "30 second Type-ready hold",
        "quiet-but-not-idle time",
        "In connected/disconnected low-power idle, the low-power renderer keeps PWR visible and leaves connected/TYPE_READY BLE dark",
        "External power overrides battery-color display on `PWR`",
        "continuous slow white breath",
        "steady white once charge-full has been debounced and latched",
        "User brightness scales the whole routine effect envelope before the profile cap is applied",
        "50% user brightness setting scales the external-power breath's low and high points together",
        "charging breath is intentionally shallow and slow",
        "Source Of Truth",
        "Code constants and `~LED:STATUS detail=contract` are the source of truth",
        "Recording uses `rec_level` only for status `LED3=REC`",
        "smoothed, rate-limited, and quantized",
        "EC11, key, and edge/frame do not follow PCM brightness",
        "`~LED:REC_LEVEL <0-100> [hold_ms]`",
        "Processing uses status `LED4=AI` immediately after firmware accepts a recording stop",
        "exact scan parameters live in the code constants and `~LED:STATUS detail=contract`",
        "Firmware recording transfer starts `AI` locally as soon as stop is accepted",
        "`VREC:PROCESSING:START`",
        "`VREC:PROCESSING:STOP`",
        "`VREC:PROCESSING:DONE`",
        "`DONE` turns `AI` off and flashes green `OK`",
        "stop key has immediate thinking feedback",
        "`OK` is a visible 2.0 second success confirmation after the host reports processing done",
        "Key LEDs remain local transient feedback only",
        "Recording and processing do not borrow the key strip for status semantics",
        "starting recording, stopping recording into processing, or starting processing must not clear or black-frame an in-flight KEY1-KEY4 press/release or purple gesture window",
        "EC11 knob and edge/frame LEDs are independent accent surfaces",
        "`rec_level` drives only status `LED3=REC`",
        "EC11, key, and edge/frame do not follow PCM brightness",
        "processing-only uses matched low violet clockwise motion",
        "EC11 short press and rotation add a brief white local confirmation",
        "accepted user-requested re-pair uses a low blue EC11 full-ring double-pulse synchronized to `LED2=BLE`",
        "EC11 double-click recovery is not a key-style purple gesture",
        "EC11/edge accent-only motion does not repeatedly refresh the status rail",
        "Status-tail anti-flicker contract",
        "six black guard pixels",
        "status_tail_reinforce=recording_processing",
        "status_tail_reinforce_writes=3",
        "status_tail_overlap_reinforce_writes=1",
        "strip_transport_requested=status:rmt,ec11:spi2,key:spi3,edge:rmt",
        "spi_dma_requested=status:0,ec11:1,key:1,edge:0",
        "spi_dma_fallback",
        "rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer",
        "rmt_tx_dma_all_strips=0",
        "rmt_tx_dma_actual=status:1,ec11:0,key:0,edge:0",
        "rmt_mem_block_symbols=status:1024,ec11:0,key:0,edge:48",
        "dynamic_active_accents=1",
        "status_tail_overlap_style=dma_audio_rec_ai_da_dada",
        "recording_level_reactive=1",
        "processing_thinking_style=single_then_double_beat",
        "processing_thinking_color=purple_static",
        "processing_thinking_scan_profile=da_long_gap_grouped_dada_rest",
        "active_work_status_overlap_dynamic_1pct_eased=0",
        "active_work_status_audio_reactive_rec=1",
        "active_work_status_thinking_ai=1",
        "Long-press shutdown confirmation",
        "filling the EC11 ring clockwise over 1.8 seconds",
        "pending cue stays latched at full ring",
        "bright amber `PWR`-only cue",
        "automatic shutdown cannot look like every LED turned on",
        "Health-monitor BLE instability alerts are log-only",
        "must not light `BLE` or `WARN` during idle",
        "wait longer than 1.8 seconds",
        "confirm `PWR` is the only active LED and the EC11 ring is off",
        "`shutdown_confirm_active`",
        "`shutdown_confirm_latched`",
        "`shutdown_confirm_elapsed_ms`",
        "`~LED:PREVIEW <ready|pairing|reconnect|repairing|capture|recording_active|capture_active|capture_led_only|desktop_mic|recording_processing|recording_processing_led_only|recording_processing_status_only|recording_processing_status_led_only|recording_processing_status_key_stress|status_key_stress3|status_key_stress4|status_key_stress34|rec_not_available|processing|processing_led_only|processing_status_led_only|ota|ota_led_only|ok|low_battery|critical_battery|charging|full|shutdown_confirm|shutdown_final|sleep|clear>`",
        "`~LED:PREVIEW recording_active` and `~LED:PREVIEW capture_active` are aliases for active device-mic capture",
        "`~LED:PREVIEW pairing`, `~LED:PREVIEW reconnect`, and `~LED:PREVIEW repairing`",
        "`~LED:PREVIEW capture_led_only`",
        "`~LED:PREVIEW recording_processing_led_only`",
        "`~LED:PREVIEW recording_processing_status_led_only`",
        "`~LED:PREVIEW recording_processing_status_key_stress`",
        "`~LED:PREVIEW processing_led_only`",
        "`~LED:PREVIEW processing_status_led_only`",
        "`~LED:PREVIEW ota`",
        "`~LED:PREVIEW ota_led_only`",
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
        "status_key_ec11_edge_true_state_v26",
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
        "~LED:PREVIEW ota",
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
        "~LED:PREVIEW processing_status_led_only",
        "~LED:PREVIEW ota",
        "~LED:PREVIEW ota_led_only",
        "~LED:PREVIEW repairing",
        "scene-ec11-short-press",
        "scene-ec11-rotate",
        "scene-key-feedback",
        "应用时机",
        "scene-ble-repairing",
        "scene-processing-live",
        "scene-ota",
        "complex-ota-led-only",
        "scene-sleep",
        "Get-ReproSteps",
        "Get-RecordingIndependenceSteps",
        "~LED:PREVIEW recording_active",
        "Get-TailOnlySteps",
        "Get-ComboOnlySteps",
        'ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "FinalVisual", "FinalRetest", "FinalCombo", "RootCause", "StaticRoot", "Repro", "RecordingIndependence", "IdleTransition", "TailOnly", "ComboOnly", "Full")',
        "preview_effect_only=1",
        "effect-only preview commands",
        "Invoke-OperatorPromptSound",
        "[System.Media.SystemSounds]::Exclamation.Play()",
        "[Console]::Beep(880, 180)",
    ],
    "components/keyboard/keyboard.c": [
        "KEYBOARD_EC11_FEEDBACK_REVERSE_MIN_ACCUM 2",
        "keyboard_ec11_feedback_delta_from_accumulator",
        "keyboard_ec11_feedback_delta_from_accumulator(state, state->detent_accumulator)",
        "magnitude < KEYBOARD_EC11_FEEDBACK_REVERSE_MIN_ACCUM",
        "keyboard_ec11_refresh_feedback_for_delta(state, feedback_delta, was_low_power_idle)",
        "status_led_refresh_ec11_feedback(delta > 0",
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


def extract_c_function(source: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source)
    if not match:
        raise ValueError(f"missing function: {name}")
    brace = source.find("{", match.start())
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise ValueError(f"unterminated function: {name}")


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
    ble_gap = read("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    audio_capture = read("ports/esp32/audio_capture/audio_capture_esp32.c")
    main_c = read("main/main.c")
    human_review = read("tools/status_led_human_effect_review.ps1")
    status_doc = read("docs/features/status_led.md")
    firmware_ota = read("components/firmware_ota/firmware_ota.c")
    if "status_led_active_work_locked" in status_led:
        failures.append("status_led.c: active recording/processing must not suppress physical key LED feedback")
    for function_name in ("status_led_notify_key_event", "status_led_notify_key_feedback"):
        try:
            body = extract_c_function(status_led, function_name)
        except ValueError as exc:
            failures.append(f"status_led.c: {exc}")
            continue
        if "status_led_clear_key_feedback_locked();" in body and "status_led_resume_interactive_output_locked();" not in body:
            failures.append(f"status_led.c: {function_name} must preserve local key feedback paths")
        if "recording_active" in body or "processing_active" in body or "ota_active" in body:
            failures.append(f"status_led.c: {function_name} must not gate key feedback on active work state")
    for function_name in ("status_led_set_recording", "status_led_set_processing"):
        try:
            body = extract_c_function(status_led, function_name)
        except ValueError as exc:
            failures.append(f"status_led.c: {exc}")
            continue
        if "status_led_clear_key_feedback_locked();" in body:
            failures.append(
                f"status_led.c: {function_name} must not clear in-flight local key feedback"
            )
        if "s_state.idle_transition_clear_pending = false;" not in body:
            failures.append(
                f"status_led.c: {function_name} must cancel stale transition clear before active work renders key feedback"
            )
    if "BLE identity kept stable" in ble_gap or "stable BLE identity" in ble_gap:
        failures.append("ble_hid_gap_esp32.c: re-pair recovery must rotate BLE identity, not keep it stable")
    try:
        recovery_body = extract_c_function(ble_gap, "ble_hid_gap_forget_bonds_and_repair")
    except ValueError as exc:
        failures.append(f"ble_hid_gap_esp32.c: {exc}")
    else:
        clear_index = recovery_body.find("rc = ble_store_clear();")
        rotate_index = recovery_body.find(
            'ble_hid_gap_rotate_static_random_identity("BLE recovery identity rotated for re-pair")',
            clear_index,
        )
        adv_index = recovery_body.find("esp_err_t adv_ret = ble_hid_gap_start_advertising();", rotate_index)
        if clear_index < 0 or rotate_index < 0 or adv_index < 0 or not (clear_index < rotate_index < adv_index):
            failures.append(
                "ble_hid_gap_esp32.c: recovery must clear bonds, rotate BLE identity, then restart advertising"
            )
        refresh_index = recovery_body.find("pairing window already active; rotating identity and refreshing advertising")
        refresh_rotate_index = recovery_body.find(
            'ble_hid_gap_rotate_static_random_identity("BLE recovery identity rotated during active pairing window")',
            refresh_index,
        )
        refresh_adv_index = recovery_body.find(
            "esp_err_t adv_ret = ble_hid_gap_start_advertising();",
            refresh_rotate_index,
        )
        if (
            refresh_index < 0
            or refresh_rotate_index < 0
            or refresh_adv_index < 0
            or not (refresh_index < refresh_rotate_index < refresh_adv_index)
        ):
            failures.append(
                "ble_hid_gap_esp32.c: active recovery window must rotate BLE identity before refreshing advertising"
            )
        if "s_recovery_identity_rotate_pending = true;" not in recovery_body:
            failures.append(
                "ble_hid_gap_esp32.c: connected recovery must defer identity rotation until after disconnect"
            )
    disconnect_index = ble_gap.find("case BLE_GAP_EVENT_DISCONNECT:")
    pending_index = ble_gap.find("if (s_recovery_identity_rotate_pending)", disconnect_index)
    disconnect_adv_index = ble_gap.find("ble_hid_gap_start_advertising();", pending_index)
    if disconnect_index < 0 or pending_index < 0 or disconnect_adv_index < 0 or pending_index > disconnect_adv_index:
        failures.append(
            "ble_hid_gap_esp32.c: disconnect recovery must rotate pending identity before advertising restarts"
        )
    enc_change_index = ble_gap.find("case BLE_GAP_EVENT_ENC_CHANGE:")
    enc_failure_index = ble_gap.find("stale pairing encryption failure", enc_change_index)
    enc_pending_index = ble_gap.find("s_recovery_identity_rotate_pending = true;", enc_change_index)
    enc_terminate_index = ble_gap.find(
        "ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM)",
        enc_change_index,
    )
    if (
        enc_change_index < 0
        or enc_failure_index < 0
        or enc_pending_index < 0
        or enc_terminate_index < 0
        or not (enc_change_index < enc_pending_index < enc_failure_index < enc_terminate_index)
    ):
        failures.append(
            "ble_hid_gap_esp32.c: recovery encryption failures must terminate stale pairing and rotate identity after disconnect"
        )
    recording_active_preview = re.search(
        r"}\s*else\s+if\s*\(\s*strcasecmp\(state,\s*\"capture\"\)\s*==\s*0\s*\|\|"
        r"[\s\S]*?strcasecmp\(state,\s*\"recording_active\"\)\s*==\s*0"
        r"[\s\S]*?strcasecmp\(state,\s*\"capture_active\"\)\s*==\s*0"
        r"[\s\S]*?\)\s*\{(?P<body>[\s\S]*?)\n\s*\}\s*else\s+if",
        status_led,
    )
    if not recording_active_preview:
        failures.append("status_led.c: recording_active/capture_active preview aliases must share the capture branch")
    else:
        body = recording_active_preview.group("body")
        if "status_led_preview_ready_baseline_locked(now_ms);" not in body:
            failures.append("status_led.c: recording_active preview must preserve PWR/BLE ready baseline")
        if "status_led_preview_effect_only_baseline_locked" in body:
            failures.append("status_led.c: recording_active preview must not be effect-only")
        if "s_state.recording_active = true;" not in body:
            failures.append("status_led.c: recording_active preview must light the REC semantic state")
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
        "STATUS_LED_CHARGING_BREATH_UNKNOWN_FLOOR_PERCENT 8U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1300U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 12U",
        "STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT 14U",
        "STATUS_LED_BATTERY_STATUS_WINDOW_LOW_PROFILE_PWR_PERCENT STATUS_LED_LOW_POWER_PWR_PERCENT",
        "STATUS_LED_LOW_BATTERY_STEADY_PERCENT 24U",
        "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT 10U",
        "STATUS_LED_FULL_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: routine PWR white must be visually balanced and user-capped, missing {token}")
    if "status_led_rgb(0, 255, STATUS_LED_OK_SUCCESS_BLUE_BALANCE)" not in status_led:
        failures.append("status_led.c: OK success must use cool green, not pure green that reads yellow on the diffuser")
    if "} else if (active_work) {\n            percent = STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT;" not in status_led:
        failures.append("status_led.c: charging PWR must hold a steady readable level during recording/processing")
    low_power_power = re.search(
        r"static\s+void\s+status_led_render_low_power_power_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not low_power_power:
        failures.append("status_led.c: missing low-power PWR renderer")
    else:
        body = low_power_power.group("body")
        plugged_branch = re.search(
            r"if\s*\(s_state\.external_power_present\)\s*\{(?P<branch>[\s\S]*?)\n\s*return;\n\s*\}",
            body,
        )
        if not plugged_branch:
            failures.append("status_led.c: plugged low-power PWR must have a direct white branch")
        else:
            branch = plugged_branch.group("branch")
            if "STATUS_LED_LOW_POWER_PWR_WHITE_PERCENT" not in branch:
                failures.append("status_led.c: plugged low-power PWR must use the low-power white percent")
            if "status_led_render_power_locked" in branch:
                failures.append("status_led.c: plugged low-power PWR must not reuse charging/full breath rendering")
        if (
            "STATUS_LED_LOW_POWER_PWR_WHITE_PERCENT 4U" not in status_led or
            "STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT 12U" not in status_led
        ):
            failures.append("status_led.c: idle PWR white and BLE attention cues must use dim low-power levels")
        if "status_led_battery_display_available_locked()" not in body:
            failures.append("status_led.c: battery low-power PWR must use display-valid fallback, not only live battery_valid")
        if "status_led_rgb(255, 140, 0)" not in body:
            failures.append("status_led.c: battery low-power PWR must keep an amber idle fallback when battery sampling is unavailable")
    low_power_ble_helper = re.search(
        r"static\s+uint8_t\s+status_led_low_power_ble_percent_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not low_power_ble_helper:
        failures.append("status_led.c: missing shared low-power BLE percent helper")
    else:
        body = low_power_ble_helper.group("body")
        if not re.search(
            r"case\s+STATUS_LED_BLE_PAIRING:\s*\n\s*case\s+STATUS_LED_BLE_REPAIRING:\s*\n\s*"
            r"case\s+STATUS_LED_BLE_RECONNECTING:[\s\S]*?"
            r"STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS[\s\S]*?"
            r"STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT",
            body,
        ):
            failures.append("status_led.c: low-power BLE helper must blink pairing/reconnecting instead of latching solid")
        if not re.search(
            r"case\s+STATUS_LED_BLE_CONNECTED:\s*\n\s*"
            r"case\s+STATUS_LED_BLE_TYPE_READY:[\s\S]*?"
            r"return\s+0U;",
            body,
        ):
            failures.append("status_led.c: low-power idle must keep connected/TYPE_READY BLE dark")
    low_power_ble = re.search(
        r"static\s+void\s+status_led_render_low_power_ble_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not low_power_ble:
        failures.append("status_led.c: missing low-power BLE renderer")
    else:
        body = low_power_ble.group("body")
        if "status_led_low_power_ble_percent_locked(status_led_ble_elapsed_locked(now_ms))" not in body:
            failures.append("status_led.c: low-power BLE renderer must use the shared low-power BLE helper")
    if not re.search(
        r"status_led_connected_hid_only_percent_locked[^{]*\{[\s\S]*?"
        r"STATUS_LED_BLE_CONNECTED_HEARTBEAT_PERIOD_MS[\s\S]*?"
        r"STATUS_LED_BLE_CONNECTED_PULSE_PERCENT[\s\S]*?"
        r"STATUS_LED_BLE_CONNECTED_BASE_PERCENT[\s\S]*?"
        r"case\s+STATUS_LED_BLE_CONNECTED:\s*\n\s*case\s+STATUS_LED_BLE_TYPE_READY:\s*\{[\s\S]*?"
        r"STATUS_LED_BLE_CONNECTED_PULSE_PERCENT[\s\S]*?"
        r"STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT[\s\S]*?"
        r"connected_visible_until_idle[\s\S]*?"
        r"status_led_connected_hid_only_percent_locked\(ble_elapsed_ms\)",
        status_led,
    ):
        failures.append("status_led.c: active BLE rendering must keep HID-only connected visible as a distinct blue heartbeat and TYPE_READY as steady blue until idle")
    for token in (
        "STATUS_LED_RECORDING_LEVEL_STALE_MS",
        "STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT 8U",
        "STATUS_LED_RECORDING_LEVEL_EFFECT_MAX_PERCENT 100U",
        "STATUS_LED_RECORDING_LEVEL_ATTACK_PERCENT_PER_SEC 100U",
        "STATUS_LED_RECORDING_LEVEL_RELEASE_PERCENT_PER_SEC 45U",
        "STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT 2U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT 0U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT 100U",
        "STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT 2U",
        "STATUS_LED_PROCESSING_THINK_PERIOD_MS 1950U",
        "STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS 50U",
        "STATUS_LED_PROCESSING_THINK_BEAT_HOLD_MS 50U",
        "STATUS_LED_PROCESSING_THINK_BEAT_FALL_MS 60U",
        "STATUS_LED_PROCESSING_THINK_GROUP_GAP_MS 520U",
        "STATUS_LED_PROCESSING_THINK_BEAT_GAP_MS 50U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_BEAT2_PERCENT 78U",
        "STATUS_LED_PROCESSING_THINK_EFFECT_BEAT3_PERCENT 100U",
        "STATUS_LED_EC11_RECORDING_FLOW_STEP_MS 360U",
        "STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS 720U",
        "status_led_recording_level_target_percent_locked",
        "status_led_recording_level_smoothed_percent_locked",
        "status_led_recording_level_effect_percent_locked",
        "status_led_processing_thinking_phase_ms_locked",
        "status_led_processing_thinking_beat_percent",
        "status_led_processing_thinking_effect_percent_locked",
        "status_led_processing_thinking_color_locked",
        "return status_led_rgb(160, 0, 255)",
        "status_led_step_percent_towards",
        "status_led_status_tail_desired_for_effect_percent_locked",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MIN_PERCENT",
        "STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MAX_PERCENT",
        "STATUS_LED_STATUS_TAIL_OVERLAP_BREATH_PERIOD_MS",
        "STATUS_LED_STATUS_TAIL_OVERLAP_RISE_MS",
        "STATUS_LED_STATUS_TAIL_OVERLAP_HIGH_HOLD_MS",
        "STATUS_LED_STATUS_TAIL_OVERLAP_FALL_MS",
        "STATUS_LED_STATUS_TAIL_OVERLAP_LOW_HOLD_MS",
        "STATUS_LED_STATUS_TAIL_OVERLAP_QUANTUM_PERCENT",
        "status_led_render_ec11_recording_flow_locked",
        "status_led_render_edge_recording_flow_locked",
        "return status_led_quantize_percent(percent, STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT)",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: recording/AI status LEDs must use bounded audio-reactive REC and thinking AI paths, missing {token}")
    for token in (
        "uint8_t head_rank = step;",
        "status_led_scale_effect_percent_locked(12U, now_ms)",
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
    elif "s_mutex == NULL" not in recording_level_block.group(0):
        failures.append("status_led.c: live rec_level diagnostics must guard early calls before the LED mutex exists")
    elif "now_ms >= s_state.recording_level_hold_until_ms" not in recording_level_block.group(0):
        failures.append("status_led.c: live REC level must not override a manual review hold")
    if "should_update_recording_level" in audio_capture:
        failures.append("audio_capture_esp32.c: live REC level must be sampled every mic frame, not only inside an active BLE session")
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
        r"void\s+status_led_set_ota_active\([^)]*\)[\s\S]*?"
        r"if\s*\(\s*active\s*\)\s*\{[\s\S]*?"
        r"s_state\.ota_started_ms\s*=\s*now_ms;[\s\S]*?"
        r"s_state\.ota_bytes_written\s*=\s*bytes_written;[\s\S]*?"
        r"s_state\.ota_expected_size\s*=\s*expected_size;",
        status_led,
    ):
        failures.append("status_led.c: OTA LED state must have an independent active/progress API")
    if not re.search(
        r"static\s+void\s+status_led_render_ota_locked[\s\S]*?"
        r"frame->status\[STATUS_LED_SEM_OK\]",
        status_led,
    ):
        failures.append("status_led.c: OTA progress must own LED5=OK, not LED4=AI")
    if not re.search(
        r"static\s+void\s+status_led_render_ec11_locked[\s\S]*?"
        r"if\s*\(\s*s_state\.ota_active\s*\)\s*\{[\s\S]*?"
        r"status_led_render_ec11_ota_locked\(frame, now_ms\);",
        status_led,
    ):
        failures.append("status_led.c: OTA progress must render on the EC11 ring")
    if not re.search(
        r"static\s+void\s+status_led_render_edge_locked[\s\S]*?"
        r"if\s*\(\s*s_state\.ota_active\s*\)\s*\{[\s\S]*?"
        r"STATUS_LED_OTA_EDGE_STEP_MS",
        status_led,
    ):
        failures.append("status_led.c: OTA progress must render on the edge/frame strip")
    if re.search(r"status_led_set_processing\([^;]*ota_", firmware_ota):
        failures.append("firmware_ota.c: OTA must not borrow AI processing LED state")
    if "POWER_MANAGER_BLOCKER_OTA" not in firmware_ota:
        failures.append("firmware_ota.c: OTA must hold a persistent power-manager blocker while active")
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
    if "status_led_ble_repair_percent_locked(\n            now_ms,\n            STATUS_LED_BLE_REPAIR_MIN_PERCENT,\n            STATUS_LED_BLE_REPAIR_MAX_PERCENT)" not in status_led:
        failures.append("status_led.c: BLE repair status LED must use the shared repair blink envelope")
    repair_envelope = re.search(
        r"static\s+uint8_t\s+status_led_ble_repair_percent_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not repair_envelope or "status_led_double_pulse_on(ble_elapsed_ms, 900U)" not in repair_envelope.group("body"):
        failures.append("status_led.c: BLE repair blink envelope must be the two-hit double-pulse cue")
    elif "status_led_blink_on" in repair_envelope.group("body"):
        failures.append("status_led.c: BLE repair blink envelope must not add a third offset blink")
    if "STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT 4U" not in status_led or \
       "STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT 16U" not in status_led:
        failures.append("status_led.c: EC11 re-pair ring must use the restored low blue full-ring blink levels")
    repair_ring = re.search(
        r"static\s+void\s+status_led_render_ec11_repair_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not repair_ring:
        failures.append("status_led.c: BLE re-pair must render the restored EC11 blue full-ring confirmation")
    else:
        repair_ring_text = repair_ring.group("body")
        if "status_led_ble_repair_percent_locked" not in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair ring must share the BLE double-pulse envelope")
        if "for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index)" not in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair ring must cover the full knob ring")
        if "frame->edge" in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair confirmation must not borrow the edge/frame LEDs")
    if not re.search(
        r"static\s+void\s+status_led_preview_state[^{]*\{[\s\S]*?"
        r"status_led_schedule_idle_transition_clear_locked\(now_ms\);[\s\S]*?"
        r"status_led_preview_clear_activity_locked\(\);[\s\S]*?"
        r"status_led_set_last_reason_locked\(\"preview\"\);",
        status_led,
    ):
        failures.append("status_led.c: every preview state must clear stale LED frames and REC/AI/OK/WARN/BLE cue state first")
    if "status_led_idle_transition_target_name" in status_led:
        failures.append("status_led.c: preview transition clear must not be limited to idle target names")
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
    if not re.search(
        r"s_state\.preview_effect_only\s*=\s*false;[\s\S]*?"
        r"s_state\.recording_active\s*=\s*next_recording_active;",
        status_led,
    ):
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
        for token in ("~LED:PREVIEW capture", "~LED:PREVIEW recording_processing_led_only"):
            if token not in volume_text:
                failures.append(f"status_led_human_effect_review.ps1: Volume mode must use command {token}")
        live_volume_step = re.search(
            r'-Id\s+"volume-capture-sweep"([\s\S]*?)(?:New-LedReviewStep|return\s+@\(\$steps\))',
            volume_text,
        )
        if not live_volume_step:
            failures.append("status_led_human_effect_review.ps1: Volume mode must keep the live volume capture step")
        else:
            live_text = live_volume_step.group(1)
            for token in ("WAIT 12000", "麦克风实时音量", "真实声音"):
                if token not in live_text:
                    failures.append(f"status_led_human_effect_review.ps1: live Volume step must instruct real microphone audio, missing {token}")
            for stale in ("~LED:REC_LEVEL 0 4200", "~LED:REC_LEVEL 30 4200", "~LED:REC_LEVEL 65 4200", "~LED:REC_LEVEL 100 60000"):
                if stale in live_text:
                    failures.append(f"status_led_human_effect_review.ps1: live Volume step must not simulate audio with {stale}")
        for stale in ('"~LED:PREVIEW capture_led_only"', '"~LED:PREVIEW recording_processing"'):
            if stale in volume_text:
                failures.append(f"status_led_human_effect_review.ps1: Volume mode must not use stale preview {stale}")
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
            "~LED:PREVIEW processing_status_led_only",
            "~LED:PREVIEW ota_led_only",
        ):
            if token not in complex_text:
                failures.append(f"status_led_human_effect_review.ps1: Complex mode must use effect-only command {token}")
    product_block = re.search(
        r'if\s*\(\$Mode\s+-eq\s+"Product"\)\s*\{([\s\S]*?)\n\s*\$tailOnly',
        human_review,
    )
    if not product_block:
        failures.append("status_led_human_effect_review.ps1: missing Product review block")
    else:
        product_text = product_block.group(1)
        for token in ("scene-ec11-short-press", "scene-ec11-rotate", "scene-key-feedback", "scene-ota"):
            if token not in product_text:
                failures.append(f"status_led_human_effect_review.ps1: Product review must include physical input step {token}")
        for stale_token in (
            'volume-capture-sweep',
            'complex-recording-processing-product',
            'complex-processing-only',
        ):
            if stale_token in product_text:
                failures.append(f"status_led_human_effect_review.ps1: Product review must not include repeated tuning step {stale_token}")
    idle_transition_block = re.search(
        r"function\s+Get-IdleTransitionSteps\s*\{([\s\S]*?)\nfunction\s+Get-TailOnlySteps",
        human_review,
    )
    if not idle_transition_block:
        failures.append("status_led_human_effect_review.ps1: missing IdleTransition review block")
    else:
        idle_transition_text = idle_transition_block.group(1)
        for token in (
            'ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "FinalVisual", "FinalRetest", "FinalCombo", "RootCause", "StaticRoot", "Repro", "RecordingIndependence", "IdleTransition", "TailOnly", "ComboOnly", "Full")',
            'if ($Mode -eq "IdleTransition")',
            "~LED:PREVIEW recording_processing_status_led_only",
            "~LED:REC_LEVEL 100 60000",
            "~LED:PREVIEW connected",
            "~LED:PREVIEW reconnecting",
            "~LED:PREVIEW clear",
            "~DIAGLOG:LAST:80:status_led",
            "IdleTransition mode isolates idle-entry validation",
        ):
            if token not in human_review:
                failures.append(f"status_led_human_effect_review.ps1: IdleTransition review must include {token}")
        if "~LED:PREVIEW recording_processing_led_only" in idle_transition_text:
            failures.append("status_led_human_effect_review.ps1: IdleTransition must use status-only setup, not full combo recording_processing_led_only")
    for token in (
        'if ($Mode -eq "FinalVisual")',
        'if ($Mode -eq "FinalRetest")',
        'if ($Mode -eq "FinalCombo")',
        'Get-FinalVisualSteps',
        'Get-FinalRetestSteps',
        'Get-FinalZoneBrightnessComboStep',
        'scene-full',
        'scene-ok',
        'scene-ota',
        'scene-rec-not-available',
        'tail-only-ai-da-dada-grouped',
        'final-zone-100-recording-processing',
        '~DEVICE:SET led_status=100 led_key=100 led_ec11=100 led_edge=100',
        '~LED:REC_LEVEL 100 120000',
        'volume-capture-sweep',
        'scene-shutdown-confirm',
    ):
        if token not in human_review:
            failures.append(f"status_led_human_effect_review.ps1: FinalVisual review must include {token}")
    for stale in ("processing/OTA", "host-confirmed processing/OTA", "AI=处理/OTA"):
        if stale in human_review:
            failures.append(f"status_led_human_effect_review.ps1: OTA must not be documented as AI processing ({stale})")
    voice_recording_control = read("components/voice_recording_control/voice_recording_control.c")
    ble_hid_gap = read("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    if 'status_led_set_processing(true, "audio_session_finishing")' in voice_recording_control:
        failures.append(
            "voice_recording_control.c: post-stop AI cue must use recording_stop_processing_start, "
            "not the audio_session_finishing log detail"
        )
    if 'status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_RETRYABLE, "voice_recovery_requested")' in voice_recording_control:
        failures.append("voice_recording_control.c: user-requested recovery must use BLE re-pair cue, not WARN/error")
    if 'status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_RETRYABLE, "ble_recovery_clear_bonds")' in ble_hid_gap:
        failures.append("ble_hid_gap_esp32.c: clearing bonds for user-requested re-pair must use BLE cue, not WARN/error")
    if 'status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_RETRYABLE, "ble_connect_failed")' in ble_hid_gap:
        failures.append("ble_hid_gap_esp32.c: normal BLE connect failures must stay pairing/reconnecting cues, not WARN/error")
    if not re.search(
        r"ble_hid_gap_adv_start_deferred_rc[\s\S]{0,220}"
        r"BLE_HS_HCI_ERR\(BLE_ERR_CMD_DISALLOWED\)",
        ble_hid_gap,
    ):
        failures.append("ble_hid_gap_esp32.c: advertising start must treat controller command-disallowed as deferred, not fatal")
    if not re.search(
        r"esp_err_t\s+ble_hid_gap_request_reconnect\(void\)[\s\S]{0,1500}"
        r"if\s*\(\s*adv_active\s*&&\s*!s_low_power_advertising\s*&&\s*!s_key_wake_only_advertising\s*\)[\s\S]{0,260}"
        r"BLE reconnect request kept existing active advertising[\s\S]{0,260}"
        r"return\s+ESP_OK;",
        ble_hid_gap,
    ):
        failures.append("ble_hid_gap_esp32.c: HID reconnect requests must not stop/restart an already active normal advertisement")
    if (
        "const bool conn_desc_valid = rc == 0;" not in ble_hid_gap
        or "if (conn_desc_valid) {\n            rc = ble_gap_security_initiate(event->connect.conn_handle);" not in ble_hid_gap
        or 'ESP_LOGW(TAG, "security initiate skipped: missing connection descriptor");' not in ble_hid_gap
    ):
        failures.append("ble_hid_gap_esp32.c: security initiate must be guarded by a valid connection descriptor")
    if 'status_led_notify_success("recording_stop_done")' in voice_recording_control:
        failures.append("voice_recording_control.c: recording STOP must not show OK before Type final success")
    if 'status_led_notify_success("recording_session_done")' in voice_recording_control:
        failures.append("voice_recording_control.c: firmware transfer completion must not show OK before Type final success")
    for token in (
        'status_led_notify_success("host_processing_done")',
        'status_led_notify_warning("host_processing_warning")',
    ):
        if token not in voice_recording_control:
            failures.append(f"voice_recording_control.c: missing host final LED confirmation {token}")
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
    if re.search(
        r"percent\s*=\s*status_window\s*\?\s*\d+U\s*:\s*\(connected_ready\s*\?\s*\d+U\s*:\s*0U\)",
        status_led,
    ):
        failures.append("status_led.c: battery PWR must not stay on just because BLE is connected")
    if re.search(r"\(status_window\s*\|\|\s*active_work\)\s*\?\s*\d+U\s*:\s*0U", status_led):
        failures.append("status_led.c: battery PWR status-window brightness must use the low visual-balance constant")
    if not re.search(
        r"status_led_render_power_locked[^{]*\{[\s\S]*?"
        r"STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT[\s\S]*?"
        r"STATUS_LED_LOW_POWER_PWR_PERCENT",
        status_led,
    ):
        failures.append("status_led.c: battery PWR must keep the low-power level during unplugged idle wait")
    for token in (
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS",
        "STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: battery idle BLE attention blink constant missing: {token}")
    if "battery_quiet" in status_led:
        failures.append("status_led.c: quiet ACTIVE must not hide connected BLE before power_manager enters idle")
    if not re.search(
        r"status_led_render_low_power_ble_locked[^{]*\{[\s\S]*?"
        r"status_led_low_power_ble_percent_locked\(status_led_ble_elapsed_locked\(now_ms\)\)",
        status_led,
    ):
        failures.append("status_led.c: low-power idle must keep using the shared BLE helper so connected/TYPE_READY stays dark and attention states blink")
    ble_set_state = re.search(
        r"void\s+status_led_set_ble_state[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_notify_ble_repairing",
        status_led,
    )
    if (
        "status_led_ble_state_attention_locked" not in status_led or
        ble_set_state is None or
        "s_state.low_power_disabled && !status_led_ble_state_attention_locked(state)" not in ble_set_state.group(0) or
        "if (!effect_only && !routine_low_power_ble)" not in ble_set_state.group(0) or
        "state_changed && !effect_only && !routine_low_power_ble &&" not in ble_set_state.group(0) or
        "if (state_changed && !effect_only && !routine_low_power_ble)" not in ble_set_state.group(0)
    ):
        failures.append("status_led.c: routine connected/TYPE_READY/DISCONNECTED BLE changes must not reopen active BLE windows from low-power idle")
    if "status_led_force_all_off();" in status_led:
        failures.append("status_led.c: all-off callers must explicitly choose whether the status strip may force non-DMA")
    all_off_body = re.search(
        r"static\s+void\s+status_led_force_all_off\(bool\s+force_non_dma\)[\s\S]*?"
        r"static\s+void\s+status_led_suspend_all_strips",
        status_led,
    )
    if (
        all_off_body is None
        or "status_led_transmit_changed_frame(&frame, STATUS_LED_STRIP_MASK_ALL, force_non_dma)" not in all_off_body.group(0)
    ):
        failures.append("status_led.c: all-off helper must pass through the explicit non-DMA policy flag")
    manual_off_body = re.search(
        r"static\s+void\s+status_led_force_manual_off\(void\)[\s\S]*?"
        r"\n\}\n\nstatic\s+bool\s+status_led_apply_device_settings_snapshot_locked",
        status_led,
    )
    if (
        manual_off_body is None
        or "s_state.output_disabled = true;" not in manual_off_body.group(0)
        or "s_state.idle_transition_clear_pending = true;" not in manual_off_body.group(0)
        or "status_led_request_refresh();" not in manual_off_body.group(0)
        or "status_led_force_all_off(" in manual_off_body.group(0)
    ):
        failures.append("status_led.c: manual/USB LED OFF must be queued to the LED task for non-DMA black latch, not transmitted inline")
    idle_clear_body = re.search(
        r"static\s+bool\s+status_led_render_idle_transition_clear_locked\(status_led_frame_t\s+\*frame\)[\s\S]*?"
        r"\n\}\n\nstatic\s+void\s+status_led_request_refresh",
        status_led,
    )
    if (
        idle_clear_body is None
        or "if (!s_state.output_disabled)" not in idle_clear_body.group(0)
        or "frame->status[STATUS_LED_SEM_PWR] = s_state.last_frame.status[STATUS_LED_SEM_PWR];" not in idle_clear_body.group(0)
        or "frame->status[STATUS_LED_SEM_BLE] = s_state.last_frame.status[STATUS_LED_SEM_BLE];" not in idle_clear_body.group(0)
    ):
        failures.append("status_led.c: idle transition clear must preserve PWR/BLE only outside manual output-off")
    if not re.search(
        r"esp_err_t\s+status_led_init\(void\)[\s\S]*?"
        r"status_led_force_all_off\(false\);[\s\S]*?return\s+final_ret;",
        status_led,
    ):
        failures.append("status_led.c: init all-off clear must not force status-strip non-DMA rebuild")
    if not re.search(
        r"void\s+status_led_prepare_sleep\(void\)[\s\S]{0,1200}"
        r"status_led_force_all_off\(true\);[\s\S]{0,120}status_led_suspend_all_strips\(\);",
        status_led,
    ):
        failures.append("status_led.c: prepare_sleep must keep the non-DMA final latch before suspending LED transports")
    if not re.search(
        r"status_led_ec11_feedback_dot_from_step[\s\S]{0,360}"
        r"STATUS_LED_EC11_FEEDBACK_ROTATE_CW[\s\S]{0,80}"
        r"\?\s*\(STATUS_LED_EC11_COUNT\s*-\s*1U\s*-\s*motion_step\)\s*%\s*STATUS_LED_EC11_COUNT[\s\S]{0,120}"
        r":\s*motion_step\s*%\s*STATUS_LED_EC11_COUNT",
        status_led,
    ):
        failures.append("status_led.c: EC11 clockwise feedback must compensate for the counterclockwise physical strip order")
    if not re.search(
        r"status_led_ec11_feedback_trail_index[\s\S]{0,260}"
        r"STATUS_LED_EC11_FEEDBACK_ROTATE_CW[\s\S]{0,120}"
        r"\?\s*\(dot\s*\+\s*offset\)\s*%\s*STATUS_LED_EC11_COUNT[\s\S]{0,120}"
        r":\s*\(dot\s*\+\s*STATUS_LED_EC11_COUNT\s*-\s*offset\)\s*%\s*STATUS_LED_EC11_COUNT",
        status_led,
    ):
        failures.append("status_led.c: EC11 clockwise trail must follow the corrected visual direction")
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
    if "EC11/edge accent updates also include a final status-strip transmit" in status_doc:
        failures.append("status_led.md: docs must not claim EC11/edge accent-only motion retransmits the status rail")
    if "EC11/edge accent-only motion does not repeatedly refresh the status rail" not in status_doc:
        failures.append("status_led.md: docs must state accent-only motion leaves an unchanged status rail alone")
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
    if "status_led_status_tail_overlap_static_percent_locked" in status_led:
        failures.append("status_led.c: REC+AI overlap must not use the fixed static helper after dynamic LED3/4 retune")
    for stale_helper in (
        "status_led_status_tail_safe_dynamic_percent_locked",
        "status_led_status_tail_safe_range_percent_locked",
        "status_led_status_tail_overlap_dynamic_percent_locked",
        "status_led_status_tail_overlap_eased_wave_percent_locked",
    ):
        if stale_helper in status_led:
            failures.append(f"status_led.c: REC+AI status rendering must not keep stale overlap helper {stale_helper}")
    if not re.search(
        r"static\s+uint8_t\s+status_led_recording_status_percent_locked[\s\S]*?"
        r"status_led_recording_level_effect_percent_locked\(now_ms\)[\s\S]*?"
        r"status_led_status_tail_desired_for_effect_percent_locked",
        status_led,
    ):
        failures.append("status_led.c: REC status LED must use the smoothed rec_level effect through the tail-safe desired mapper")
    if not re.search(
        r"static\s+uint8_t\s+status_led_processing_status_percent_locked[\s\S]*?"
        r"status_led_processing_thinking_effect_percent_locked\(now_ms\)[\s\S]*?"
        r"status_led_status_tail_desired_for_effect_percent_locked",
        status_led,
    ):
        failures.append("status_led.c: AI status LED must use the smooth thinking pulse effect through the tail-safe desired mapper")
    tail_mapper = re.search(
        r"static\s+uint8_t\s+status_led_status_tail_desired_for_effect_percent_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not tail_mapper:
        failures.append("status_led.c: missing status tail dynamic brightness mapper")
    else:
        tail_mapper_body = tail_mapper.group("body")
        for forbidden in ("s_state.brightness_percent", "status_zone_brightness_percent", "combined_percent"):
            if forbidden in tail_mapper_body:
                failures.append(
                    "status_led.c: status tail dynamic brightness must remain a Type-cap ratio, not inverse-compensate user brightness"
                )
                break
        if "return target_effect_percent > desired_percent ? desired_percent : target_effect_percent;" not in tail_mapper_body:
            failures.append("status_led.c: status tail dynamic mapper must clamp effect percent before Type/user caps scale it")
    if not re.search(
        r"static\s+uint8_t\s+status_led_status_tail_reinforce_write_count[\s\S]*?"
        r"STATUS_LED_STATUS_TAIL_OVERLAP_REINFORCE_WRITES",
        status_led,
    ):
        failures.append("status_led.c: REC+AI overlap must limit status-tail reinforce writes to the overlap write count")
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
    if "STATUS_LED_RMT_DMA_MEM_BLOCK_SYMBOLS 1024U" not in status_led_backend:
        failures.append(
            "status_led_strip_backend.c: RMT DMA channels must use a full-frame-sized 1024-symbol DMA buffer"
        )
    if not re.search(
        r"const\s+size_t\s+mem_block_symbols\s*=\s*with_dma[\s\S]*?"
        r"STATUS_LED_RMT_DMA_MEM_BLOCK_SYMBOLS[\s\S]*?"
        r"SOC_RMT_MEM_WORDS_PER_CHANNEL[\s\S]*?"
        r"\.mem_block_symbols\s*=\s*mem_block_symbols",
        status_led_backend,
    ):
        failures.append(
            "status_led_strip_backend.c: DMA channels must use the larger DMA buffer while non-DMA fallback stays on one SOC RMT block"
        )
    if not re.search(r"\.flags\.with_dma\s*=\s*with_dma\b", status_led_backend):
        failures.append("status_led_strip_backend.c: RMT TX DMA must be selectable per strip")
    if not re.search(
        r"bool\s+status_led_strip_backend_uses_dma\([^)]*\)[\s\S]*?"
        r"backend\s*!=\s*NULL[\s\S]*?backend->available[\s\S]*?backend->dma_enabled",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: runtime status must expose whether each available strip uses RMT TX DMA")
    if (
        "strip_transport_requested=status:rmt,ec11:spi2,key:spi3,edge:rmt" not in status_led or
        "strip_transport_actual=status:%s,ec11:%s,key:%s,edge:%s" not in status_led or
        "spi_dma_requested=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "spi_dma_actual=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "spi_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer" not in status_led or
        "rmt_tx_dma_actual=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_tx_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_mem_block_symbols=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_idle_drive=active_dma_low_power_all_zone_non_dma_final_frame_then_release_gpio_low" not in status_led
        or "shutdown_final_status_tx=non_dma_pwr_only_latch" not in status_led
        or "low_power_all_zone_tx=non_dma_clear_and_final_frame" not in status_led
        or "shutdown_final_all_zone_tx=non_dma_pwr_only_latch_or_all_off" not in status_led
    ):
        failures.append("status_led.c: ~LED:STATUS contract must expose RMT/SPI transport, per-strip DMA actual/fallback state, buffer size, and idle-drive policy")
    if status_led.count(".prefer_dma = true") != 1 or not re.search(
        r"\.name\s*=\s*\"status\"[\s\S]*?\.prefer_dma\s*=\s*true",
        status_led,
    ):
        failures.append("status_led.c: current V2 hardware must request RMT TX DMA only for the status strip")
    if not re.search(
        r"\.name\s*=\s*\"ec11\"[\s\S]*?\.transport\s*=\s*STATUS_LED_STRIP_TRANSPORT_SPI[\s\S]*?\.spi_host\s*=\s*SPI2_HOST",
        status_led,
    ):
        failures.append("status_led.c: EC11 strip must request SPI2 DMA")
    if not re.search(
        r"\.name\s*=\s*\"key\"[\s\S]*?\.transport\s*=\s*STATUS_LED_STRIP_TRANSPORT_SPI[\s\S]*?\.spi_host\s*=\s*SPI3_HOST",
        status_led,
    ):
        failures.append("status_led.c: key strip must request SPI3 DMA")
    if re.search(
        r"\.name\s*=\s*\"edge\"[\s\S]*?\.transport\s*=\s*STATUS_LED_STRIP_TRANSPORT_SPI",
        status_led,
    ):
        failures.append("status_led.c: edge strip must stay on ordinary RMT")
    if ".flags.eot_level = 0" not in status_led_backend:
        failures.append("status_led_strip_backend.c: RMT transmit config must explicitly hold the WS2812 line low at EOT")
    if "disable_ret" in status_led_backend or re.search(
        r"ret\s*=\s*rmt_tx_wait_all_done\(backend->channel,\s*STATUS_LED_RMT_WAIT_MS\);[\s\S]*?"
        r"if\s*\(\s*ret\s*!=\s*ESP_OK\s*\)[\s\S]*?"
        r"status_led_strip_backend_set_channel_enabled\(backend,\s*false\);[\s\S]*?"
        r"return\s+ret;[\s\S]*?"
        r"status_led_strip_backend_set_channel_enabled\(backend,\s*false\);[\s\S]*?"
        r"return\s+ESP_OK;",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: successful dynamic RMT transmit must stay enabled; only idle/sleep paths may suspend")
    if status_led_backend.count("rmt_tx_switch_gpio") != 1:
        failures.append("status_led_strip_backend.c: low-power GPIO-low suspend must restore the RMT GPIO binding exactly once on resume")
    if not re.search(
        r"if\s*\(\s*backend->gpio_idle_driven_low\s*\)\s*\{[\s\S]*?"
        r"rmt_tx_switch_gpio\(backend->channel,\s*backend->gpio,\s*false\)[\s\S]*?"
        r"backend->gpio_idle_driven_low\s*=\s*false;[\s\S]*?"
        r"\}\s*ret\s*=\s*rmt_enable\(backend->channel\);",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: RMT enable must re-bind the data GPIO after low-power idle drove it low")
    if not re.search(
        r"status_led_strip_backend_drive_idle_low[\s\S]*?"
        r"gpio_set_direction\(gpio,\s*GPIO_MODE_OUTPUT\);[\s\S]*?"
        r"backend->gpio_idle_driven_low\s*=\s*true;",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: low-power idle GPIO-low drive must mark the strip for RMT GPIO restore")
    if not re.search(
        r"if\s*\(\s*enabled\s*\)\s*\{[\s\S]*?"
        r"ret\s*=\s*rmt_enable\(backend->channel\);[\s\S]*?"
        r"\}\s*else\s*\{[\s\S]*?"
        r"ret\s*=\s*rmt_disable\(backend->channel\);",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: backend must release PM locks with rmt_disable and re-enable RMT on the next transmit")
    if (
        "STATUS_LED_RMT_IDLE_RELEASE_MS" not in status_led or
        "status_led_suspend_quiet_idle_transports" not in status_led or
        "status_led_idle_transport_release_pending" not in status_led or
        "s_strip_transport_suspended" not in status_led or
        "s_strip_last_tx_ms" not in status_led or
        "low_power_active = s_state.output_disabled || s_state.low_power_disabled" not in status_led
        or "shutdown_final_active = !force_clear_tx" not in status_led
        or "pwr_only_final_latch = low_power_active || shutdown_final_active" not in status_led
    ):
        failures.append("status_led.c: low-power idle and final shutdown PWR-only confirmation must stay non-DMA for normal LED frames")
    if "bool force_non_dma = force_clear_tx || low_power_active;" in status_led:
        failures.append("status_led.c: ordinary transition clear frames must not force non-DMA; scope force_non_dma to clear/latch paths only")
    if not re.search(r"bool\s+force_non_dma\s*=\s*pwr_only_final_latch\s*\|\|\s*force_clear_tx\s*;", status_led):
        failures.append("status_led.c: clear frames and PWR-only latches must force every strip off its DMA transport")
    if (
        "shutdown_final_all_zone_latched_started_ms" not in status_led
        or "shutdown_final_all_zone_latch_needed" not in status_led
        or "if (!force_clear_tx && shutdown_final_all_zone_latch_needed) {\n        tx_strip_mask = STATUS_LED_STRIP_MASK_ALL;" not in status_led
    ):
        failures.append("status_led.c: shutdown-final latch must rewrite all strips once per final window so stale physical EC11/key/edge state is cleared")
    if (
        "status_led_spi_transmit_non_dma_rmt_once" not in status_led_backend
        or "backend->requested_transport == STATUS_LED_STRIP_TRANSPORT_SPI" not in status_led_backend
        or "backend->transport = STATUS_LED_STRIP_TRANSPORT_RMT" not in status_led_backend
        or "backend->transport = saved_transport" not in status_led_backend
    ):
        failures.append("status_led_strip_backend.c: SPI strips must use a one-shot non-DMA RMT latch path when clear/final frames force non-DMA")
    if not re.search(
        r"status_led_suspend_quiet_idle_transports[\s\S]*?"
        r"for\s*\(size_t\s+index\s*=\s*0;[\s\S]*?"
        r"status_led_strip_backend_suspend\(s_strips\[index\]\.backend\)",
        status_led,
    ):
        failures.append("status_led.c: low-power quiet suspend must release every strip only after the final non-DMA idle latch")
    if "status_led_suspend_all_strips" not in status_led or "status_led_strip_backend_suspend(s_strips[index].backend)" not in status_led:
        failures.append("status_led.c: prepare_sleep must explicitly suspend strip backends after the all-off frame")
    if not re.search(
        r"status_led_strip_backend_new_channel\(backend,\s*channel_with_dma\);[\s\S]*?"
        r"falling back to non-DMA RMT[\s\S]*?"
        r"status_led_strip_backend_new_channel\(backend,\s*channel_with_dma\)",
        status_led_backend,
    ):
        failures.append("status_led_strip_backend.c: DMA-preferring strip init must fall back to non-DMA RMT if DMA channel allocation fails")
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
            "post-stop AI cue must use recording_stop_processing_start, not the audio_session_finishing log detail",
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
        "USB validation hooks, camera/manual one-pixel status/key/EC11/edge harness, and sleep all-off path."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
