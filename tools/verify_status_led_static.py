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
        "status_led_set_type_ota_link_active",
        "STATUS_LED_BLE_REPAIRING",
        "status_led_notify_ble_repairing",
        "status_led_notify_ble_repairing_for_ms",
        "status_led_set_recording",
        "status_led_set_recording_level",
        "status_led_set_processing",
        "status_led_set_ota_active",
        "status_led_notify_warning",
        "status_led_notify_shutdown_confirm",
        "status_led_try_notify_shutdown_confirm",
        "status_led_try_notify_shutdown_final_hold",
        "status_led_try_hold_shutdown_all_off",
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
        "STATUS_LED_RECORDING_LEVEL_LOCK_WAIT_MS 0U",
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
        "STATUS_LED_PROCESSING_STALE_TIMEOUT_MS 5000U",
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
        "STATUS_LED_TASK_PRIORITY 6U",
        "STATUS_LED_TASK_CORE_ID 1",
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
        "STATUS_LED_PWR_CONFIRM_AMBER_PERCENT 72U",
        "STATUS_LED_PWR_CONFIRM_CUE_PERCENT 46U",
        "STATUS_LED_BOOT_PWR_AMBER_PERCENT STATUS_LED_PWR_CONFIRM_AMBER_PERCENT",
        "status_led_power_confirm_amber",
        "status_led_boot_power_color_locked",
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
        "STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS 160U",
        "STATUS_LED_KEY_MULTI_KEY_INDEPENDENT_FADE 1U",
        "STATUS_LED_EC11_FEEDBACK_MS 1400U",
        "STATUS_LED_EC11_ROTATION_HOLD_MS 2600U",
        "STATUS_LED_EC11_ROTATION_STEP_MS 150U",
        "STATUS_LED_EC11_ROTATION_HEAD_START_PERCENT 72U",
        "STATUS_LED_EC11_ROTATION_HEAD_END_PERCENT 44U",
        "STATUS_LED_EC11_ROTATION_BASE_START_PERCENT 4U",
        "STATUS_LED_EC11_ROTATION_BASE_END_PERCENT 1U",
        "status_led_scale_raw(white, 60U)",
        "status_led_scale_raw(white, 32U)",
        "STATUS_LED_BOOT_BLE_READY_WAIT_MS STATUS_LED_STATUS_WINDOW_MS",
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
        "STATUS_LED_BATTERY_STATUS_WINDOW_LOW_PROFILE_PWR_PERCENT STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT",
        "STATUS_LED_LOW_BATTERY_STEADY_PERCENT 24U",
        "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT 10U",
        "STATUS_LED_FULL_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS 120U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS 7880U",
        "STATUS_LED_STATUS_RGB_ENERGY_BALANCE_CONTRACT \"status_rgb_channel_sum_matches_single_channel_peak\"",
        "status_led_rgb_channel_sum",
        "status_led_scale_rgb_to_channel_sum",
        "status_led_balance_status_rgb_energy_to_peak",
        "status_led_balance_status_rgb_energy_to_peak_strip",
        "STATUS_LED_OK_SUCCESS_BLUE_BALANCE 0U",
        "STATUS_LED_PREVIEW_BLE_OVERRIDE_MS 15000U",
        "STATUS_LED_BLE_REPAIR_CUE_MS 2700U",
        "STATUS_LED_BLE_REPAIR_CUE_LEAD_CLEAR_MS (STATUS_LED_IDLE_TRANSITION_CLEAR_MS + STATUS_LED_REFRESH_MS)",
        "STATUS_LED_BLE_REPAIR_WINDOW_MAX_MS 120000U",
        "STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT 4U",
        "STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT 16U",
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
        "STATUS_LED_KEY_DARK_RESYNC_MS 0U",
        "STATUS_LED_KEY_TAIL_GUARD_PIXELS 0U",
        "STATUS_LED_KEY_DARK_LATCH_RMT_WRITES 2U",
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
        "now_ms < s_state.ble_repair_cue_started_ms",
        "s_state.ble_repair_cue_started_ms = now_ms + STATUS_LED_BLE_REPAIR_CUE_LEAD_CLEAR_MS",
        "s_state.ble_repair_cue_until_ms = s_state.ble_repair_cue_started_ms + STATUS_LED_BLE_REPAIR_CUE_MS",
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
        "spi_ws2812_waveform=4bit_3m2_0x8_0xE",
        "status_tail_guard_pixels=%u",
        "key_tail_guard_pixels=%u",
        "key_dark_clear_tx=spi_dma_prelatch_then_one_shot_rmt_gpio_low",
        "key_lit_edge_tx=spi_dma_only",
        "key_feedback_dynamic_tx=spi_dma_until_dark_latch",
        "key_release_fade_ms=%u",
        "key_single_white_hold_ms=%u",
        "key_multi_key_independent_fade=%u",
        "strip_tx_failure_retry_dirty=1",
        "suspended_strip_resume_dirty=1",
        "startup_complete_gate=ble_visible_ready_frame_no_early_pwr_normal",
        "task_priority=%u task_core=%d task_affinity=cpu1",
        "preview_effect_zone_brightness=1",
        "test_calibration_full_brightness=1",
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
        "recording_level_lock_wait_ms=%u",
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
        "neutral_legacy_brightness_percent=%u",
        "status_zone_brightness_percent=%u",
        "key_zone_brightness_percent=%u",
        "ec11_zone_brightness_percent=%u",
        "edge_zone_brightness_percent=%u",
        "s_state.status_zone_brightness_percent = DEVICE_SETTINGS_DEFAULT_STATUS_LED_BRIGHTNESS_PERCENT",
        "s_state.key_zone_brightness_percent = DEVICE_SETTINGS_DEFAULT_KEY_LED_BRIGHTNESS_PERCENT",
        "status_led_apply_zone_brightness_caps_locked",
        "status_led_apply_device_settings_snapshot_locked",
        "STATUS_LED_ACCENT_ENTRY_RAMP_MS 900U",
        "STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT 16U",
        "STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT 14U",
        "STATUS_LED_EC11_RECORDING_FLOW_STEP_MS 360U",
        "STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS 720U",
        "STATUS_LED_OTA_EC11_STEP_MS STATUS_LED_EC11_RECORDING_FLOW_STEP_MS",
        "STATUS_LED_EC11_ORBIT_STEP_MS 240U",
        "STATUS_LED_EDGE_ORBIT_STEP_MS 480U",
        "status_led_render_key_active_work_locked",
        "effective_cap_percent=%u",
        "zone_brightness_is_hard_cap=1",
        "zone_brightness_effect_peak_cap=1",
        "zone_brightness_preserves_effect_percent=1",
        "status_rgb_energy_balance=",
        "legacy_brightness_neutral=1",
        "profile_dimming_disabled=1",
        "factory_full_brightness=1",
        "safety_full_brightness=1",
        "status_led_profile_cap_percent_for",
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
        "STATUS_LED_BLE_RECONNECT_PULSE_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT",
        "STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT 5U",
        "STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT",
        "STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT 14U",
        "case STATUS_LED_BLE_TYPE_READY: return \"type_ready\"",
        "status_led_ble_state_ready_locked",
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
        "bool key_force_non_dma =",
        "status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_KEY], frame->key, key_force_non_dma)",
        "status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EDGE], frame->edge, force_non_dma)",
        "low_power_all_zone_tx=non_dma_clear_and_final_frame",
        "shutdown_final_status_tx=dma_visible_pwr_no_black_latch",
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
        "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT,\n            false)",
        "active_flags=PWR:%u,BLE:%u,REC:%u,AI:%u,OK:%u,WARN:%u,EC11:%u,KEY:%u,EDGE:%u",
        "status_rgb=PWR:%u,%u,%u;BLE:%u,%u,%u;REC:%u,%u,%u",
        "explicit_ready_window",
        "confidence_window && status_led_ble_state_ready_locked(state)",
        "if (state_changed && !effect_only && !routine_low_power_ble) {\n            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;",
        "if (explicit_ready_window) {\n            s_state.ble_confidence_until_ms = now_ms + STATUS_LED_BLE_CONFIDENCE_MS;",
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
        "dark_latch_rmt_writes",
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
        "STATUS_LED_SPI_CLOCK_HZ       3200000",
        "STATUS_LED_SPI_BITS_PER_BIT   4U",
        "STATUS_LED_SPI_RESET_BYTES    240U",
        "0b1000",
        "0b1110",
        "0xEu",
        "0x8u",
        "spi_waveform=4bit_3m2_0x8_0xE",
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
        "spi_reset_us=600",
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
        "ble_hid_gap_is_securely_connected()",
        "status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false)",
        "ble_audio_stream_type_link_poll_wait_ms(",
        "ble_audio_stream_poll_type_link()",
    ],
    "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c": [
        "BLE_AUDIO_STREAM_TYPE_HEARTBEAT_TIMEOUT_MS 45000",
        "BLE_AUDIO_STREAM_TYPE_LED_READY_HOLD_MS 12000",
        "power_manager_record_activity",
        "ble_audio_stream_sync_power_manager_for_type_link(false, \"gap_connect\")",
        "TYPE:READY",
        "TYPE:HB",
        "status_led_set_type_ota_link_active(true, reason)",
        "status_led_set_type_ota_link_active(false, \"type_ota_handoff_failed\")",
        "TYPE:BYE",
        "ble_audio_stream_type_ready_visible_event",
        "power_manager_record_activity(\"type_ready\")",
        "ble_audio_stream_type_ready_visible_event(reason))",
        "ble_audio_stream_type_heartbeat_recent()",
        "ble_audio_stream_type_heartbeat_led_recent()",
        "ble_audio_stream_is_type_led_ready()",
        "ble_audio_stream_sync_status_led_for_type_link(\"type_heartbeat_timeout\")",
        "type_heartbeat_led_grace_timeout",
    ],
    "ports/esp32/ble_audio_stream/include/ble_audio_stream.h": [
        "ble_audio_stream_is_type_led_ready",
        "ble_audio_stream_consume_type_control_command",
        "ble_audio_stream_note_type_activity",
        "ble_audio_stream_type_link_poll_wait_ms",
        "ble_audio_stream_poll_type_link",
    ],
    "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c": [
        "connection failed while previous GAP link was still marked connected",
        "ble_audio_stream_on_gap_disconnect(stale_conn.conn_handle)",
        "pairing_window\n                ? STATUS_LED_BLE_PAIRING",
        ": STATUS_LED_BLE_RECONNECTING",
        "BLE reconnect request kept existing active advertising",
        "ble_hid_gap_recovery_pairing_window_open()\n                ? STATUS_LED_BLE_PAIRING",
        "ble_hid_gap_get_bonded_peer_count(&bonded_peer_count)",
        "keeping pairing window visible until secure reconnect",
        "recovery: Windows connection during pairing window; Listener waiting for Windows pairing security",
        "recovery: waiting for Windows pairing security",
        "global GAP event listener registered",
        "ble_hid_gap_request_recovery_security_once(event->mtu.conn_handle, \"mtu\")",
        "ble_hid_gap_request_recovery_security_once(event->subscribe.conn_handle, \"subscribe\")",
        "case BLE_GAP_EVENT_PASSKEY_ACTION:",
        "passkey numeric comparison auto-accepted",
        "status_led_notify_ble_repairing_for_ms(\"ble_recovery_clear_bonds\"",
        "status_led_notify_ble_repairing_for_ms(\"ble_recovery_refresh_pairing\"",
        "ble_hid_gap_hold_recovery_pairing_led(\"ble_recovery_pairing_window_open\")",
        "stable BLE identity",
        "pairing encryption failure",
        "keeping pairing advertising available for Windows retry",
        "security initiate requested conn=%u",
        "BLE identity ready and device is discoverable for re-pair",
        "BLE_HID_GAP_RANDOM_IDENTITY_KEY",
        "ble_hid_gap_rotate_native_recovery_identity",
        "ble_hid_gap_restore_random_identity_from_nvs",
        "s_swift_pair_mfg_data",
        "s_recovery_type_controlled_pairing",
        "BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_HID_UUID",
        "BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITHOUT_APPEARANCE",
        "ble_hid_gap_configure_swift_pair_fields",
        "ble_hid_gap_configure_type_controlled_recovery_adv_fields",
        'type_recovery_enabled ? "type_recovery" : "normal"',
        "status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE",
        "status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE)",
    ],
    "components/power_manager/power_manager.c": [
        "ble_audio_stream_is_type_link_ready()",
        "effective_connected",
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
        "voice_recording_control_note_ble_type_processing_activity(source, \"host_processing_start\")",
        "voice_recording_control_host_processing_start(source)",
        "voice_recording_control_note_ble_type_processing_activity(source, \"host_processing_done\")",
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
        "#define VOICE_KEY_INPUT_DOUBLE_CLICK_MIN_GAP_MS (60)",
        "voice_key_input_mark_recovery_double_candidate(button, now_tick, \"raw_edge\")",
        "voice_key_input_mark_recovery_double_candidate(\n                    &s_direct_gpio_state,\n                    now,\n                    \"isr_edge\")",
        "button->recovery_double_candidate =\n        voice_key_input_recovery_double_gap_ready(button, now_tick)",
        "voice_key_input_recovery_double_click_ready(button, now_tick)",
        "voice_key_input_clear_raw_feedback(button);\n            ESP_LOGI(TAG, \"%s long press reserved for power control",
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
        "three-cycle blue double-flash confirmation",
        "must not keep repeating the confirmation pattern forever",
        "If a recent Type heartbeat or Type-host presence window proves Listener-Type was active on this computer",
        "If Type is absent and no recent Type-host presence exists, native recovery rotates and persists a new static-random BLE identity",
        "The host must create a new bond through the recovery window instead of silently treating the double-click as an ordinary reconnect to the old bond",
        "ble_repair_ms_left",
        "A successful Type-ready transition is the steady blue connected indication",
        "Only ordinary HID-only `connected` keeps the low-floor blue double-flash Type-search cue while active",
        "BLE animation phase is tracked separately",
        "`ble_transition_ms`",
        "Manual `~LED:PREVIEW` scenes temporarily hold their requested BLE state",
        "for 15 seconds",
        "`preview_ble_override_ms_left`",
        "Disconnected/no-host stays dark",
        "pairing and user-requested re-pair can still blink BLE as attention states",
        "Re-pair uses a BLE-plus-EC11 confirmation cue",
        "current render-sampled RGB frame",
        "status_query_samples_current_render=1",
        "ordinary HID-only `connected` uses a continuous low-floor blue double-flash Type-search cue while active",
        "`TYPE_READY` is the Listener-Type-ready BLE state",
        "it uses steady blue",
        "30 second Type-ready hold",
        "Steady connected state is Type-gated",
        "so the low valley means a real BLE connection exists but Listener-Type is not ready yet",
        "Connected/disconnected low-power idle keeps PWR visible and leaves reconnecting/connected/TYPE_READY BLE dark",
        "External power overrides battery-color display on `PWR`",
        "continuous slow white breath",
        "steady white once charge-full has been debounced and latched",
        "design peak to the Type zone cap",
        "A 50% Type zone setting means the active effect's high point is 50%",
        "PWR, BLE, KEY, and OTA status cues all use that design-peak mapping in active and low-power states",
        "Low-power may decide which semantic LEDs stay visible, but it must not own brightness or apply a hidden dimming layer",
        "Low-power does not have separate brightness constants",
        "Status-strip mixed RGB colors are then balanced so their channel sum matches the single-channel peak",
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
        "accepted user-requested re-pair uses a synchronized BLE and EC11 three-cycle double-flash",
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
        "startup_complete_gate=ble_visible_ready_frame_no_early_pwr_normal",
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
        "`~LED:PREVIEW <ready|disconnected|pairing|reconnect|repairing|capture|recording_active|capture_active|capture_led_only|desktop_mic|recording_processing|recording_processing_led_only|recording_processing_status_only|recording_processing_status_led_only|recording_processing_status_key_stress|status_key_stress3|status_key_stress4|status_key_stress34|rec_not_available|processing|processing_led_only|processing_status_led_only|ota|ota_led_only|ok|low_battery|critical_battery|charging|full|shutdown_confirm|shutdown_final|sleep|clear>`",
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
        "It does not add a hidden percent cap above the Type-controlled zone caps",
        "`~LED:BRIGHTNESS <0-100>` is legacy compatibility that maps to all four zone brightness caps",
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
        "Get-BluetoothSteps",
        "~LED:PREVIEW recording_active",
        "Get-TailOnlySteps",
        "Get-ComboOnlySteps",
        'ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "Bluetooth", "FinalVisual", "FinalRetest", "FinalCombo", "RootCause", "StaticRoot", "Repro", "RecordingIndependence", "IdleTransition", "TailOnly", "ComboOnly", "Full")',
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
        "EC11 push raw press latched for gesture",
        "power_manager_record_activity(\"ec11_key_press\")",
        "power_manager_record_activity(\"ec11_key_hold\")",
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
    status_led_backend_header = read("components/status_led/status_led_strip_backend.h")
    ble_gap = read("ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c")
    ble_firmware_ota = read("ports/esp32/ble_firmware_ota/ble_firmware_ota_esp32.c")
    ble_audio_stream = read("ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c")
    audio_capture = read("ports/esp32/audio_capture/audio_capture_esp32.c")
    main_c = read("main/main.c")
    human_review = read("tools/status_led_human_effect_review.ps1")
    status_doc = read("docs/features/status_led.md")
    firmware_ota = read("components/firmware_ota/firmware_ota.c")
    keyboard = read("components/keyboard/keyboard.c")
    voice_key = read("ports/esp32/voice_key_input/voice_key_input_esp32.c")
    try:
        boot_power = extract_c_function(status_led, "status_led_boot_power_color_locked")
        boot_mark = extract_c_function(status_led, "status_led_mark_boot_feedback_locked")
        boot_feedback = extract_c_function(status_led, "status_led_force_boot_feedback")
        boot_ble_complete = extract_c_function(status_led, "status_led_ble_state_completes_boot_feedback")
        boot_feedback_active = extract_c_function(status_led, "status_led_boot_feedback_active_locked")
        boot_ble_release = extract_c_function(status_led, "status_led_release_boot_feedback_for_ble_locked")
        boot_apply = extract_c_function(status_led, "status_led_apply_boot_feedback_locked")
        boot_ble_ready_reason = extract_c_function(status_led, "status_led_boot_ready_reason_releases_feedback")
        boot_ble_ready_note = extract_c_function(status_led, "status_led_note_ble_boot_ready")
        render_power = extract_c_function(status_led, "status_led_render_power_locked")
        render_frame = extract_c_function(status_led, "status_led_render_frame_locked")
        shutdown_confirm = extract_c_function(status_led, "status_led_render_shutdown_confirm_locked")
        shutdown_notify = extract_c_function(status_led, "status_led_notify_shutdown_confirm_with_wait_and_duration")
        shutdown_resume = extract_c_function(status_led, "status_led_resume_shutdown_confirm_output_locked")
        shutdown_prepare = extract_c_function(status_led, "status_led_prepare_shutdown_confirm_frame_locked")
        shutdown_tx = extract_c_function(status_led, "status_led_transmit_shutdown_confirm_frame")
        status_init = extract_c_function(status_led, "status_led_init")
        status_start = extract_c_function(status_led, "status_led_start")
        status_task = extract_c_function(status_led, "status_led_task")
        set_ble_state = extract_c_function(status_led, "status_led_set_ble_state")
    except ValueError as exc:
        failures.append(f"status_led.c: {exc}")
    else:
        if "xTaskCreate(" in status_start:
            failures.append("status_led.c: status LED task must not regress to unpinned xTaskCreate")
        if not re.search(
            r"xTaskCreatePinnedToCore\(\s*"
            r"status_led_task,\s*"
            r"\"status_led_task\",\s*"
            r"STATUS_LED_TASK_STACK_BYTES,\s*"
            r"NULL,\s*"
            r"STATUS_LED_TASK_PRIORITY,\s*"
            r"&s_task_handle,\s*"
            r"STATUS_LED_TASK_CORE_ID\s*\)",
            status_start,
        ):
            failures.append("status_led.c: status LED task must stay priority-raised and pinned to CPU1 to avoid KEY DMA flicker regression")
        if (
            "xPortGetCoreID()" not in status_task
            or "uxTaskPriorityGet(NULL)" not in status_task
            or "configured_core=%d" not in status_task
        ):
            failures.append("status_led.c: status LED task must log its actual priority/core at runtime")
        if (
            "task_priority=%u task_core=%d task_affinity=cpu1" not in status_led
            or "STATUS_LED_TASK_PRIORITY" not in status_led
            or "STATUS_LED_TASK_CORE_ID" not in status_led
        ):
            failures.append("status_led.c: ~LED:STATUS detail=contract must report LED task priority/core")
        if not re.search(
            r"return\s+status_led_token_locked\(\s*"
            r"status_led_power_confirm_amber\(\),\s*"
            r"STATUS_LED_BOOT_PWR_AMBER_PERCENT,\s*"
            r"false\s*\);",
            boot_power,
        ):
            failures.append("status_led.c: boot PWR first frame must reuse the accepted shutdown amber color and brightness")
        if "STATUS_LED_BOOT_PWR_WHITE_PERCENT" in status_led or "status_led_rgb(255, 255, 255)" in boot_power:
            failures.append("status_led.c: boot PWR first frame must not regress to full-white or bypass the shutdown amber helper")
        if "STATUS_LED_BOOT_ACK_MS" in status_led:
            failures.append("status_led.c: boot PWR feedback must not keep a stale fixed ACK timer; BLE visible state releases the amber startup cue")
        if "s_state.boot_feedback_until_ms = now_ms + STATUS_LED_BOOT_BLE_READY_WAIT_MS;" not in status_init:
            failures.append("status_led.c: initial boot PWR amber cue must stay active until a truthful BLE-visible state releases it")
        if "uint32_t until_ms = now_ms + STATUS_LED_BOOT_BLE_READY_WAIT_MS;" not in boot_mark:
            failures.append("status_led.c: forced boot feedback must extend the BLE-ready startup window, not a fixed ACK window")
        for required_state in (
            "STATUS_LED_BLE_PAIRING",
            "STATUS_LED_BLE_REPAIRING",
            "STATUS_LED_BLE_RECONNECTING",
            "STATUS_LED_BLE_CONNECTED",
            "STATUS_LED_BLE_TYPE_READY",
        ):
            if required_state not in boot_ble_complete:
                failures.append(f"status_led.c: boot PWR amber release must treat {required_state} as a BLE-visible startup-complete state")
        if "STATUS_LED_BLE_DISCONNECTED" in boot_ble_complete:
            failures.append("status_led.c: disconnected BLE must not release boot PWR amber into normal PWR state")
        if (
            "s_state.boot_ble_ready_seen" not in boot_ble_release
            or "status_led_ble_state_completes_boot_feedback(s_state.ble_state)" not in boot_ble_release
            or "status_led_boot_feedback_active_locked(now_ms)" not in boot_ble_release
            or "status_led_rgb_is_on(frame->status[STATUS_LED_SEM_BLE])" not in boot_ble_release
            or "s_state.boot_ble_ready_notified = true;" not in boot_ble_release
            or "s_state.boot_feedback_until_ms = now_ms;" in boot_ble_release
        ):
            failures.append("status_led.c: BLE-visible startup release must wait for an actual lit BLE frame before flipping the boot-ready flag")
        if not re.search(
            r"status_led_boot_feedback_active_locked\([^)]*\)\s*\{[\s\S]*?"
            r"return\s+!\s*s_state\.boot_ble_ready_notified\s*&&\s*"
            r"now_ms\s*<\s*s_state\.boot_feedback_until_ms\s*;",
            status_led,
        ):
            failures.append("status_led.c: boot PWR amber render must require both not-yet-BLE-ready and inside the startup timeout")
        if "status_led_release_boot_feedback_for_ble_locked" in set_ble_state:
            failures.append("status_led.c: BLE state changes alone must not release boot PWR amber before advertising is truly started")
        for required_reason in (
            '"advertising_skip_connected"',
            '"directed_advertising_started"',
            '"undirected_advertising_started"',
        ):
            if required_reason not in boot_ble_ready_reason:
                failures.append(f"status_led.c: boot PWR amber release must allow {required_reason} as a truthful BLE-ready reason")
        for forbidden_reason in ('"secure_connection"', '"TYPE:READY"', '"TYPE:HB"'):
            if forbidden_reason in boot_ble_ready_reason:
                failures.append(f"status_led.c: boot PWR amber release must not treat early {forbidden_reason} as BLE advertising readiness")
        if (
            "status_led_boot_ready_reason_releases_feedback(reason)" not in boot_ble_ready_note
            or "s_state.boot_ble_ready_seen = true;" not in boot_ble_ready_note
            or "status_led_release_boot_feedback_for_ble_locked" in boot_ble_ready_note
        ):
            failures.append("status_led.c: BLE boot-ready notes may only arm visible-frame release, not release PWR amber before BLE lights")
        if (
            "status_led_power_confirm_amber()" not in shutdown_confirm
            or "final ? STATUS_LED_PWR_CONFIRM_AMBER_PERCENT : STATUS_LED_PWR_CONFIRM_CUE_PERCENT" not in shutdown_confirm
        ):
            failures.append("status_led.c: shutdown confirmation must keep the accepted amber helper and final/cue constants")
        if "status_led_resume_shutdown_confirm_output_locked();" not in shutdown_notify:
            failures.append("status_led.c: shutdown confirmation must use its no-black-frame output resume helper")
        if "status_led_resume_interactive_output_locked" in shutdown_notify:
            failures.append("status_led.c: shutdown confirmation must not reuse the interactive resume path that can queue a transition-clear black frame")
        if (
            "s_state.transition_clear_mask = 0U;" not in shutdown_resume
            or "status_led_force_transition_clear_locked" in shutdown_resume
            or "s_state.output_disabled = false;" not in shutdown_resume
            or "s_state.low_power_disabled = false;" not in shutdown_resume
        ):
            failures.append("status_led.c: shutdown confirmation resume must restore output and clear pending transition clears without scheduling an all-black clear frame")
        if (
            "const uint8_t pending_strip_mask =" not in shutdown_tx
            or "!status_led_strip_has_light(frame->ec11, STATUS_LED_EC11_COUNT)" not in shutdown_tx
            or "~STATUS_LED_STRIP_MASK_EC11" not in shutdown_tx
            or "status_led_transmit_changed_frame(frame, pending_strip_mask, false, false)" not in shutdown_tx
            or "final ? STATUS_LED_STRIP_MASK_ALL : pending_strip_mask" not in shutdown_tx
        ):
            failures.append("status_led.c: non-final shutdown-confirm zero-elapsed immediate TX must not send an all-black EC11 frame before the fill effect advances")
        if (
            "status_led_rgb_t previous_ec11[STATUS_LED_EC11_COUNT];" not in shutdown_prepare
            or "memcpy(previous_ec11, s_state.last_frame.ec11, sizeof(previous_ec11));" not in shutdown_prepare
            or "!s_state.shutdown_confirm_final" not in shutdown_prepare
            or "!status_led_strip_has_light(frame->ec11, STATUS_LED_EC11_COUNT)" not in shutdown_prepare
            or "status_led_strip_has_light(previous_ec11, STATUS_LED_EC11_COUNT)" not in shutdown_prepare
            or "memcpy(frame->ec11, previous_ec11, sizeof(frame->ec11));" not in shutdown_prepare
        ):
            failures.append("status_led.c: shutdown-confirm immediate frame must preserve the previous physical EC11 strip if any future zero-lit frame would otherwise blank EC11")

        boot_feedback_order = [
            boot_feedback.find("frame.status[STATUS_LED_SEM_PWR] = status_led_boot_power_color_locked();"),
            boot_feedback.find("status_led_apply_zone_brightness_caps_locked(&frame);"),
            boot_feedback.find("status_led_copy_frame_locked(&frame);"),
            boot_feedback.find("status_led_transmit_frame(&frame);"),
        ]
        if not all(index >= 0 for index in boot_feedback_order) or boot_feedback_order != sorted(boot_feedback_order):
            failures.append("status_led.c: boot PWR first frame must apply Type four-zone brightness caps before copy/transmit")

        if (
            "status_led_apply_boot_feedback_locked(frame, now_ms, &safety);" not in render_frame
            or render_frame.find("status_led_render_power_locked(frame, now_ms, &safety);")
               > render_frame.find("status_led_render_ble_locked(frame, now_ms);")
            or render_frame.find("status_led_render_ble_locked(frame, now_ms);")
               > render_frame.find("status_led_apply_boot_feedback_locked(frame, now_ms, &safety);")
        ):
            failures.append("status_led.c: boot PWR completion handoff must render PWR and BLE before applying the visible-BLE release")
        if (
            "status_led_release_boot_feedback_for_ble_locked(now_ms, frame)" not in boot_apply
            or "frame->status[STATUS_LED_SEM_PWR] = status_led_boot_power_color_locked();" not in boot_apply
            or "status_led_set_max" in boot_apply
        ):
            failures.append("status_led.c: boot PWR feedback must override charging/full PWR until the same frame has visible BLE")
        if "status_led_boot_feedback_active_locked(now_ms)" in render_power:
            failures.append("status_led.c: boot PWR amber override must stay after BLE render so the completion frame can show BLE plus normal PWR")

        shutdown_branch_index = render_frame.find("if (status_led_render_shutdown_confirm_locked(frame, now_ms))")
        shutdown_tail_index = render_frame.find("status_led_apply_status_tail_guard_locked(frame, now_ms);", shutdown_branch_index)
        shutdown_cap_index = render_frame.find("status_led_apply_zone_brightness_caps_locked(frame);", shutdown_branch_index)
        shutdown_clamp_index = render_frame.find("status_led_clamp_current_locked(frame, safety);", shutdown_branch_index)
        shutdown_return_index = render_frame.find("return;", shutdown_branch_index)
        if not (
            shutdown_branch_index >= 0
            and shutdown_tail_index >= 0
            and shutdown_cap_index >= 0
            and shutdown_clamp_index >= 0
            and shutdown_return_index >= 0
            and shutdown_branch_index < shutdown_tail_index < shutdown_cap_index < shutdown_clamp_index < shutdown_return_index
        ):
            failures.append("status_led.c: shutdown confirmation branch must apply Type four-zone brightness caps before returning")
        try:
            test_mode_caps = extract_c_function(status_led, "status_led_test_mode_uses_zone_brightness_caps")
        except ValueError as exc:
            failures.append(f"status_led.c: {exc}")
        else:
            if (
                "STATUS_LED_TEST_STATIC_STATUS" not in test_mode_caps
                or "STATUS_LED_TEST_STATUS_KEY_STRESS" not in test_mode_caps
                or "STATUS_LED_TEST_PIXEL" in test_mode_caps
            ):
                failures.append("status_led.c: effect preview test modes must use Type zone caps, while pixel calibration stays full-brightness")
            if not re.search(
                r"if\s*\(\s*s_state\.test_mode\s*!=\s*STATUS_LED_TEST_NONE\s*\)\s*\{[\s\S]*?"
                r"status_led_test_mode_uses_zone_brightness_caps\(s_state\.test_mode\)[\s\S]*?"
                r"status_led_render_test_locked\(frame,\s*now_ms\);[\s\S]*?"
                r"if\s*\(\s*test_uses_zone_caps\s*\)\s*\{[\s\S]*?"
                r"status_led_apply_zone_brightness_caps_locked\(frame\);[\s\S]*?"
                r"status_led_clamp_current_locked\(frame,\s*false\);[\s\S]*?"
                r"\}\s*else\s*\{[\s\S]*?"
                r"status_led_clamp_current_locked\(frame,\s*true\);",
                render_frame,
            ):
                failures.append("status_led.c: status/key effect previews must apply Type zone caps before logging RGB evidence, while calibration tests stay full-brightness")

        init_boot_index = status_init.find("status_led_force_boot_feedback();")
        init_return_index = status_init.find("return final_ret;")
        init_initialized_index = status_init.find("s_state.initialized = true;")
        if "status_led_force_all_off(true);" in status_init:
            failures.append("status_led.c: status_led_init must not black out PWR before the cold-boot amber cue")
        if not (0 <= init_initialized_index < init_boot_index < init_return_index):
            failures.append("status_led.c: status_led_init must latch the amber boot PWR frame immediately after strip init")
    if "status_led_active_work_locked" in status_led:
        failures.append("status_led.c: active recording/processing must not suppress physical key LED feedback")
    if (
        "#define VOICE_KEY_INPUT_SINGLE_CLICK_DISPATCH_MS (500)" not in voice_key
        or "#define VOICE_KEY_INPUT_RECOVERY_DOUBLE_CLICK_WINDOW_MS (500)" not in voice_key
        or "#define VOICE_KEY_INPUT_DOUBLE_CLICK_MIN_GAP_MS (60)" not in voice_key
    ):
        failures.append("voice_key_input_esp32.c: EC11 must keep the validated 500 ms physical double-click window synchronized with KEY1-KEY4 and a small raw-bounce min-gap guard")
    if (
        "voice_key_input_cancel_pending_single_click" not in voice_key
        or "voice_key_input_arm_pending_single_click" not in voice_key
        or "voice_key_input_accept_recovery_double_click" not in voice_key
        or "voice_key_input_mark_recovery_double_candidate" not in voice_key
        or "voice_key_input_prune_recovery_guard" not in voice_key
        or "button->recovery_double_candidate =\n        voice_key_input_recovery_double_gap_ready(button, now_tick)" not in voice_key
        or "voice_key_input_recovery_double_click_ready" not in voice_key
        or "button->recovery_double_candidate ||\n           voice_key_input_recovery_double_gap_ready(button, now_tick)" not in voice_key
        or "bool recovery_double_click =\n        voice_key_input_recovery_double_click_ready(button, now_tick)" not in voice_key
    ):
        failures.append("voice_key_input_esp32.c: EC11 push must require a second press candidate and re-check the 60 ms guard on release before accepting recovery double-click while single dispatch consumes the recovery guard")
    if re.search(
        r"button->pending_single_click\s*&&\s*\(\s*origin\s*!=\s*NULL\s*\|\|\s*button->recovery_double_candidate\s*\)",
        voice_key,
    ):
        failures.append("voice_key_input_esp32.c: EC11 raw-only release must not be enough to accept recovery double-click")
    try:
        raw_feedback = extract_c_function(keyboard, "keyboard_custom_apply_raw_feedback")
        raw_short_release = extract_c_function(keyboard, "keyboard_custom_handle_raw_short_release")
        send_gesture = extract_c_function(keyboard, "keyboard_custom_send_gesture_internal")
        generated_enqueue = extract_c_function(keyboard, "keyboard_custom_enqueue_generated_gesture")
        timers = extract_c_function(keyboard, "keyboard_custom_handle_timers")
        stable_transition = extract_c_function(keyboard, "keyboard_custom_apply_stable_transition")
    except ValueError as exc:
        failures.append(f"keyboard.c: {exc}")
    else:
        if not re.search(
            r"#define\s+KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES\s+\\\s*\n\s*"
            r"\(\(\(KEYBOARD_CUSTOM_DEBOUNCE_MS\s*\+\s*KEYBOARD_CUSTOM_POLL_MS\s*-\s*1\)\s*/\s*"
            r"KEYBOARD_CUSTOM_POLL_MS\)\s*\+\s*1\)",
            keyboard,
        ):
            failures.append("keyboard.c: KEY1-KEY4 debounce sample count must require the full 20 ms before stable LED/HID feedback")
        if (
            "#define KEYBOARD_CUSTOM_TASK_PRIORITY 7U" not in keyboard
            or "KEYBOARD_CUSTOM_TASK_PRIORITY,\n        &s_custom_task_handle" not in keyboard
            or "task_priority=%u audio_preempt_safe=1" not in keyboard
            or "~KEY:STATUS custom_keys=KEY1:F13/F17/F21" not in keyboard
        ):
            failures.append("keyboard.c: KEY1-KEY4 scan task must stay above the audio task so recording cannot expire the double-click window into a single click")
        if (
            "status_led_notify_key_event(key->index, true);" not in raw_feedback
            or "keyboard_custom_send_gesture" in raw_feedback
        ):
            failures.append("keyboard.c: KEY1-KEY4 raw edges must only light local white preview before debounce, never dispatch gestures")
        if (
            "custom key raw debounce candidate" not in raw_feedback
            or "key->raw_feedback_tick = now;" not in raw_feedback
            or "key->raw_feedback_pressed = true;" not in raw_feedback
            or "preview=1" not in raw_feedback
            or "power_manager_record_activity" in raw_feedback
        ):
            failures.append("keyboard.c: KEY1-KEY4 raw edges must timestamp a debounce candidate and mark the preview latch without recording activity")
        clear_raw_feedback = extract_c_function(keyboard, "keyboard_custom_clear_raw_feedback")
        if "status_led_cancel_key_preview(key->index);" not in clear_raw_feedback:
            failures.append("keyboard.c: KEY1-KEY4 raw-only preview cancel must clear white immediately without a release-tail")
        if (
            "keyboard_custom_send_gesture" in raw_short_release
            or "pending_single = true" in raw_short_release
            or "pending_single_due_tick" in raw_short_release
            or "STATUS_LED_KEY_FEEDBACK" in raw_short_release
        ):
            failures.append("keyboard.c: KEY1-KEY4 raw release handling must not bypass the stable gesture path with direct click or purple feedback")
        if (
            "raw_ms >= KEYBOARD_CUSTOM_DEBOUNCE_MS" not in raw_short_release
            or "custom key raw-duration tap accepted" not in raw_short_release
            or "keyboard_custom_apply_stable_transition(key, false, press_tick, \"raw_duration_press\");"
            not in raw_short_release
            or "keyboard_custom_apply_stable_transition(key, true, now, \"raw_duration_release\");"
            not in raw_short_release
        ):
            failures.append("keyboard.c: KEY1-KEY4 release-before-sample taps that already met the 20 ms debounce threshold must be promoted through normal stable press/release")
        if "custom key raw-only short transition ignored" not in raw_short_release:
            failures.append("keyboard.c: KEY1-KEY4 sub-20 ms raw-only release before debounce must be logged as ignored evidence")
        if (
            "pending_single_visual_started" not in keyboard
            or "custom key single visual already active" not in send_gesture
            or "status_led_notify_key_feedback(key->index, keyboard_custom_led_feedback_for_gesture(gesture));"
            not in send_gesture
        ):
            failures.append("keyboard.c: KEY1-KEY4 must allow immediate single-click purple visual feedback without restarting it when the delayed HID single fires")
        if (
            "keyboard_custom_cancel_other_pending_single_visuals" in keyboard
            or "custom key cross-key pending single visual canceled" in keyboard
            or "preserve_hid=%u" in keyboard
            or "key->pending_single ? 1U : 0U" in keyboard
        ):
            failures.append("keyboard.c: KEY1-KEY4 must not hide follow-light by clearing another key's accepted fade; each key effect must fade independently")
        if 'keyboard_custom_cancel_other_pending_single_visuals(key, "generated_enqueue");' in generated_enqueue:
            failures.append("keyboard.c: generated KEY stress must not clear another key's visual fade")
        if "keyboard_custom_cancel_other_pending_single_visuals(key, origin);" in raw_feedback:
            failures.append("keyboard.c: physical raw key feedback must not run a separate stale-visual cancel before lighting the current key")
        if "status_led_notify_key_event(key->index, true);" not in stable_transition:
            failures.append("keyboard.c: KEY1-KEY4 debounced press must still light the local white preview")
        if "status_led_notify_key_event(key->index, pressed);" in stable_transition:
            failures.append("keyboard.c: KEY1-KEY4 debounced single/double release must not first enqueue the white release tail before purple feedback")
        if "status_led_notify_key_event(key->index, false);" not in stable_transition:
            failures.append("keyboard.c: KEY1-KEY4 long release must still clear the physical press mask before the long fade")
        stable_press_tokens = (
            "if (pressed && !key->pressed)",
            "key->pressed = true;",
            "key->press_tick = now;",
            "key->long_sent = false;",
            "key->double_candidate = key->pending_single;",
        )
        if any(token not in stable_transition for token in stable_press_tokens):
            failures.append("keyboard.c: KEY1-KEY4 debounced press must still arm the validated single/double/long state machine")
        stable_release_tokens = (
            "else if (!pressed && key->pressed)",
            "key->pressed = false;",
            "status_led_notify_key_feedback(key->index, STATUS_LED_KEY_FEEDBACK_LONG);",
            "keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_LONG);",
            "key->double_candidate && key->pending_single",
            "keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_DOUBLE);",
            "key->pending_single = true;",
            "key->pending_single_visual_started = true;",
            "key->pending_single_due_tick = now + pdMS_TO_TICKS(KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS);",
            "status_led_notify_key_feedback(key->index, STATUS_LED_KEY_FEEDBACK_SINGLE);",
            "custom key single pending",
            "visual=immediate",
        )
        if any(token not in stable_transition for token in stable_release_tokens):
            failures.append("keyboard.c: KEY1-KEY4 debounced release must preserve long/double dispatch and start single-click purple visual feedback immediately while the HID single waits for the double-click window")
        timer_tokens = (
            "key->pressed && !key->long_sent",
            "KEYBOARD_CUSTOM_LONG_PRESS_MS",
            "key->long_sent = true;",
            "keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_LONG);",
            "!key->pressed && key->pending_single",
            "keyboard_custom_tick_reached(now, key->pending_single_due_tick)",
            "single_visual_started",
            "key->pending_single_visual_started = single_visual_started;",
            "keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_SINGLE);",
        )
        if any(token not in timers for token in timer_tokens):
            failures.append("keyboard.c: KEY1-KEY4 timers must keep stable long-hold and delayed-single dispatch without restarting already-visible single feedback")
    if re.search(
        r"second_click_too_soon|recovery_candidate_from_raw|voice_key_input_note_raw_press_edge|recent_short_click|recent_raw_press|raw_recovery_dispatched",
        voice_key,
    ):
        failures.append("voice_key_input_esp32.c: EC11 push must not restore the old special raw-recovery click path")
    if re.search(r"\bSTATUS_LED_TRANSITION_CLEAR_ACCENTS\b", status_led):
        failures.append("status_led.c: generic transition clear ACCENTS mask must not exist; use NON_KEY_ACCENTS or an explicit strip mask")
    resume_output = extract_c_function(status_led, "status_led_resume_interactive_output_locked")
    if "s_state.transition_clear_mask = 0U;" in resume_output:
        failures.append("status_led.c: interactive low-power resume must preserve the pending clear frame, not cancel it")
    if not re.search(
        r"if\s*\(\s*was_low_power_output\s*\)\s*\{[\s\S]*?"
        r"status_led_force_transition_clear_locked\(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS\);[\s\S]*?"
        r"\}\s*else\s+if\s*\(\s*s_state\.transition_clear_mask\s*!=\s*0U\s*\)\s*\{[\s\S]*?"
        r"status_led_force_transition_clear_locked\(STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS\);",
        resume_output,
    ):
        failures.append("status_led.c: interactive resume from manual-off/low-power must all-zone clear, while active scoped clears stay non-KEY")
    if "status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ACCENTS);" in resume_output:
        failures.append("status_led.c: interactive low-power resume must not black-frame the KEY strip through the generic accent clear")
    for function_name in ("status_led_notify_key_event", "status_led_cancel_key_preview", "status_led_notify_key_feedback"):
        try:
            body = extract_c_function(status_led, function_name)
        except ValueError as exc:
            failures.append(f"status_led.c: {exc}")
            continue
        if "status_led_clear_key_feedback_locked();" in body and "status_led_resume_interactive_output_locked();" not in body:
            failures.append(f"status_led.c: {function_name} must preserve local key feedback paths")
        if "recording_active" in body or "processing_active" in body or "ota_active" in body:
            failures.append(f"status_led.c: {function_name} must not gate key feedback on active work state")
        if function_name == "status_led_cancel_key_preview" and (
            "s_state.key_feedback_started_ms[key_index] = 0U;" not in body
            or "s_state.key_feedback_until_ms[key_index] = 0U;" not in body
        ):
            failures.append("status_led.c: key preview cancel must also clear pending gesture feedback timers so stale purple single-click visuals cannot bleed into the next key")
    try:
        key_feedback_duration = extract_c_function(status_led, "status_led_key_feedback_duration_ms")
        key_feedback_render = extract_c_function(status_led, "status_led_render_key_feedback_locked")
        key_physical_percent = extract_c_function(status_led, "status_led_key_physical_percent_locked")
        key_physical_token = extract_c_function(status_led, "status_led_key_physical_token_locked")
        key_render = extract_c_function(status_led, "status_led_render_keys_locked")
        key_event = extract_c_function(status_led, "status_led_notify_key_event")
        key_feedback = extract_c_function(status_led, "status_led_notify_key_feedback")
    except ValueError as exc:
        failures.append(f"status_led.c: {exc}")
    else:
        if "#define STATUS_LED_KEY_GESTURE_FEEDBACK_MS 1800U" not in status_led or "#define STATUS_LED_KEY_FADE_MS 300U" not in status_led:
            failures.append("status_led.c: KEY gesture feedback must keep the product fade timing constants")
        if "status_led_decay_percent" not in key_feedback_render or "fade_elapsed" not in key_feedback_render:
            failures.append("status_led.c: KEY feedback renderer must preserve the product fade effect")
        if "status_led_key_feedback_token_locked" not in status_led or "status_led_key_physical_token_locked" not in status_led:
            failures.append("status_led.c: KEY LEDs must map gesture and physical-feedback design peaks to the Type key-zone cap")
        if "status_led_key_feedback_token_locked" not in key_feedback_render:
            failures.append("status_led.c: KEY gesture feedback must normalize against the design peak, not the current rendered frame")
        if (
            "return STATUS_LED_KEY_PRESS_PERCENT;" not in key_physical_percent
            or "STATUS_LED_KEY_FEEDBACK_MS" not in key_physical_percent
            or "STATUS_LED_KEY_RELEASE_PERCENT" not in key_physical_percent
            or "status_led_decay_percent(" not in key_physical_percent
        ):
            failures.append("status_led.c: KEY physical release tail must fade continuously from release brightness to 0")
        if not re.search(
            r"status_led_decay_percent\(\s*"
            r"elapsed_ms,\s*"
            r"STATUS_LED_KEY_FEEDBACK_MS,\s*"
            r"STATUS_LED_KEY_RELEASE_PERCENT,\s*"
            r"0U\s*\)",
            key_physical_percent,
        ):
            failures.append("status_led.c: KEY physical release tail must use the whole feedback window for a continuous fade, not hold a low level before cutting/fading")
        if (
            "status_led_token_relative_to_peak_locked" not in key_physical_token
            or "STATUS_LED_KEY_PRESS_PERCENT" not in key_physical_token
        ):
            failures.append("status_led.c: KEY physical press/release brightness must stay relative to the press design peak before Type key cap")
        if (
            "status_led_key_physical_percent_locked(index, pressed, now_ms)" not in key_render
            or "status_led_key_physical_token_locked(physical_percent)" not in key_render
            or "status_led_key_physical_token_locked(pressed)" in key_render
        ):
            failures.append("status_led.c: KEY renderer must use the time-varying physical release fade percent")
        if "status_led_normalize_strip_peak_to_type_max" in status_led:
            failures.append("status_led.c: zone brightness caps must preserve KEY fade percentages; do not peak-normalize each rendered frame")
        if (
            "STATUS_LED_KEY_FEEDBACK_LONG" not in key_feedback_duration
            or "STATUS_LED_KEY_LONG_GESTURE_FEEDBACK_MS" not in key_feedback_duration
            or "STATUS_LED_KEY_GESTURE_FEEDBACK_MS" not in key_feedback_duration
        ):
            failures.append("status_led.c: KEY single/double/long feedback durations must keep the gradient gesture window")
        if (
            "feedback != STATUS_LED_KEY_FEEDBACK_LONG" not in key_feedback
            or "s_state.key_pressed_mask &= ~(1U << key_index);" not in key_feedback
            or "s_state.key_until_ms[key_index] = 0U;" not in key_feedback
        ):
            failures.append("status_led.c: KEY single/double feedback must atomically clear physical white and enter purple without a release-tail refresh")
        if (
            "STATUS_LED_KEY_FLASH_ON_MS * 2U + STATUS_LED_KEY_FLASH_GAP_MS +" not in key_feedback_render
            or "STATUS_LED_KEY_FADE_MS" not in key_feedback_render
            or "status_led_decay_percent" not in key_feedback_render
        ):
            failures.append("status_led.c: KEY double feedback must keep the second-flash fade tail")
        single_tail = re.search(
            r"case\s+STATUS_LED_KEY_FEEDBACK_SINGLE:[\s\S]*?"
            r"STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS[\s\S]*?"
            r"status_led_key_physical_token_locked\(STATUS_LED_KEY_PRESS_PERCENT\)[\s\S]*?"
            r"fade_elapsed\s*=\s*elapsed\s*-\s*STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS\s*-\s*STATUS_LED_KEY_FLASH_ON_MS;[\s\S]*?"
            r"status_led_decay_percent\(\s*"
            r"fade_elapsed,\s*"
            r"STATUS_LED_KEY_FADE_MS,\s*"
            r"STATUS_LED_KEY_GESTURE_PERCENT,\s*"
            r"0U\s*\)",
            key_feedback_render,
        )
        if not single_tail:
            failures.append("status_led.c: KEY single feedback must fade from the purple release brightness continuously to 0")
        double_tail = re.search(
            r"case\s+STATUS_LED_KEY_FEEDBACK_DOUBLE:[\s\S]*?"
            r"fade_elapsed\s*=[\s\S]*?"
            r"STATUS_LED_KEY_FLASH_ON_MS\s*\*\s*2U\s*\+\s*STATUS_LED_KEY_FLASH_GAP_MS[\s\S]*?"
            r"status_led_decay_percent\(\s*"
            r"fade_elapsed,\s*"
            r"STATUS_LED_KEY_FADE_MS,\s*"
            r"STATUS_LED_KEY_GESTURE_PERCENT,\s*"
            r"0U\s*\)",
            key_feedback_render,
        )
        if not double_tail:
            failures.append("status_led.c: KEY double feedback must fade from the second purple flash continuously to 0")
        long_tail = re.search(
            r"case\s+STATUS_LED_KEY_FEEDBACK_LONG:[\s\S]*?"
            r"status_led_decay_percent\(\s*"
            r"elapsed,\s*"
            r"STATUS_LED_KEY_FADE_MS,\s*"
            r"STATUS_LED_KEY_GESTURE_PERCENT,\s*"
            r"0U\s*\)",
            key_feedback_render,
        )
        if not long_tail:
            failures.append("status_led.c: KEY long feedback release must fade from purple continuously to 0")
        if "now_ms + STATUS_LED_KEY_FADE_MS" not in key_event or "long_release_fade" not in key_event:
            failures.append("status_led.c: KEY long release must fade out and suppress the white physical release tail")
        if (
            "status_led_clear_other_key_visuals_locked" in status_led
            or "key_cross_key_visual_cancel" in status_led
            or "STATUS_LED_KEY_CROSS_KEY_VISUAL_CANCEL" in status_led
        ):
            failures.append("status_led.c: KEY fix must not clear other keys' accepted fades to hide follow-light")
        if (
            "key_multi_key_independent_fade=%u" not in status_led
            or "STATUS_LED_KEY_MULTI_KEY_INDEPENDENT_FADE" not in status_led
            or "key_single_white_hold_ms=%u" not in status_led
            or "STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS" not in status_led
        ):
            failures.append("status_led.c: KEY status contract must expose accepted independent-fade and white-hold behavior")
        key_active_work = extract_c_function(status_led, "status_led_render_key_active_work_locked")
        if "frame->key" in key_active_work or "status_led_set_max" in key_active_work:
            failures.append("status_led.c: recording/processing/BLE/OTA active work must not borrow the KEY strip for status semantics")
        if "(void)frame;" not in key_active_work or "(void)now_ms;" not in key_active_work or "return;" not in key_active_work:
            failures.append("status_led.c: KEY active-work renderer must remain an explicit no-op; KEY LEDs are local feedback only")
    refresh_once = extract_c_function(status_led, "status_led_refresh_once")
    try:
        transmit_changed_frame = extract_c_function(status_led, "status_led_transmit_changed_frame")
        retry_update = extract_c_function(status_led, "status_led_update_retry_strip_mask")
        suspended_mask = extract_c_function(status_led, "status_led_suspended_strip_mask")
        note_transmitted = extract_c_function(status_led, "status_led_note_strip_transmitted")
        tx_result = extract_c_function(status_led, "status_led_note_strip_transmit_result")
        transmit_frame = extract_c_function(status_led, "status_led_transmit_frame")
        force_all_off = extract_c_function(status_led, "status_led_force_all_off")
    except ValueError as exc:
        failures.append(f"status_led.c: {exc}")
    else:
        if "uint8_t retry_strip_mask;" not in status_led:
            failures.append("status_led.c: status state must remember failed strip transmissions as a retry dirty mask")
        if "static uint8_t status_led_transmit_changed_frame" not in status_led:
            failures.append("status_led.c: changed-frame transmit must return a failed-strip mask")
        if (
            "uint8_t failed_strip_mask = 0U;" not in transmit_changed_frame
            or "return strip_mask;" not in transmit_changed_frame
            or "return failed_strip_mask;" not in transmit_changed_frame
        ):
            failures.append("status_led.c: tx mutex timeout and strip transmit errors must report failed strip masks")
        for strip in ("STATUS_LED_STRIP_EC11", "STATUS_LED_STRIP_KEY", "STATUS_LED_STRIP_EDGE", "STATUS_LED_STRIP_STATUS"):
            if not re.search(rf"status_led_note_strip_transmit_result\(\s*{strip}", transmit_changed_frame):
                failures.append(f"status_led.c: {strip} transmit failures must flow into the failed-strip mask")
        if (
            "LED strip tx failed strip=%s" not in tx_result
            or "status_led_strip_mask_for_index(strip_index)" not in tx_result
            or "DIAG_LED_OUTPUT_FAIL" not in tx_result
        ):
            failures.append("status_led.c: strip transmit failures must be logged and converted to retry dirty bits")
        if (
            "failed_strip_mask = (uint8_t)(failed_strip_mask & attempted_strip_mask);" not in retry_update
            or "attempted_strip_mask & (uint8_t)(~failed_strip_mask)" not in retry_update
            or "s_state.retry_strip_mask = (uint8_t)(retry_strip_mask & STATUS_LED_STRIP_MASK_ALL);" not in retry_update
        ):
            failures.append("status_led.c: retry dirty mask must clear succeeded strips and retain failed strips")
        if (
            "tx_strip_mask = (uint8_t)(tx_strip_mask | s_state.retry_strip_mask);" not in refresh_once
            or "status_led_update_retry_strip_mask(tx_strip_mask, failed_strip_mask);" not in refresh_once
        ):
            failures.append("status_led.c: refresh loop must retry failed EC11/KEY/EDGE/STATUS strip transmissions before last_frame suppression can hide them")
        if (
            "uint8_t key_lit_latch_pending_mask;" in status_led
            or "key_lit_latch_pending_tx" in status_led
            or "s_state.key_lit_latch_pending_mask" in status_led
            or "key_lit_edge_tx=spi_dma_prelatch_then_one_shot_rmt_gpio_low" in status_led
            or "key_feedback_dynamic_tx=spi_dma_between_latches" in status_led
        ):
            failures.append("status_led.c: KEY lit frames must not use one-shot RMT or a lit-latch pending path; active white/purple/fade frames stay on SPI DMA")
        if (
            "bool key_dark_latch_pending_tx = false;" not in refresh_once
            or "key_dark_latch_pending_tx = !force_clear_tx &&" not in refresh_once
            or "s_state.key_dark_latch_pending_mask != 0U" not in refresh_once
            or "tx_strip_mask = (uint8_t)(tx_strip_mask | STATUS_LED_STRIP_MASK_KEY);" not in refresh_once
            or "key_dark_latch_pending_tx)" not in refresh_once
            or "key_dark_latch_pending_tx" not in transmit_changed_frame
            or "bool key_has_light = status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT);" not in transmit_changed_frame
            or "bool key_force_non_dma = force_non_dma || !key_has_light;" not in transmit_changed_frame
            or "!key_dark_latch_pending_tx &&" not in transmit_changed_frame
            or "key_lit_edge_tx=spi_dma_only" not in status_led
            or "key_feedback_dynamic_tx=spi_dma_until_dark_latch" not in status_led
            or "key_dark_latch_expiry_dirty=1" not in status_led
        ):
            failures.append("status_led.c: KEY feedback must keep lit white/purple/fade frames on SPI DMA and leave a pending all-dark latch dirty until it is physically transmitted")
        if "status_led_key_feedback_latch_active_locked" in status_led or "key_feedback_latch_tx" in status_led:
            failures.append("status_led.c: KEY feedback latch must not stay active for the whole fade window; that reintroduces SPI/RMT transport churn flicker")
        if (
            "s_strip_transport_suspended[index]" not in suspended_mask
            or "status_led_strip_mask_for_index((status_led_strip_id_t)index)" not in suspended_mask
            or "tx_strip_mask = (uint8_t)(tx_strip_mask | status_led_suspended_strip_mask());" not in refresh_once
            or "if (!pwr_only_final_latch)" not in refresh_once
        ):
            failures.append("status_led.c: suspended EC11/KEY SPI transports must become dirty on resume so unchanged-frame suppression cannot leave them unavailable")
        if (
            "status_led_strip_backend_available(s_strips[strip_index].backend)" not in note_transmitted
            or "s_strip_transport_suspended[strip_index] =" not in note_transmitted
        ):
            failures.append("status_led.c: successful non-DMA one-shot transmits that leave a backend unavailable must still mark the strip suspended for active resume")
        if (
            "status_led_update_retry_strip_mask(STATUS_LED_STRIP_MASK_ALL, failed_strip_mask);" not in transmit_frame
            or "status_led_update_retry_strip_mask(STATUS_LED_STRIP_MASK_ALL, failed_strip_mask);" not in force_all_off
        ):
            failures.append("status_led.c: direct boot/all-off transmits must also preserve failed strips for retry")
        if "strip_tx_failure_retry_dirty=1" not in status_led:
            failures.append("status_led.c: ~LED:STATUS contract must expose failed-strip retry-dirty protection")
        if "suspended_strip_resume_dirty=1" not in status_led:
            failures.append("status_led.c: ~LED:STATUS contract must expose suspended-strip resume dirty protection")
    if "bool force_non_dma = pwr_only_final_latch;" not in refresh_once or "pwr_only_final_latch || force_clear_tx" in refresh_once:
        failures.append("status_led.c: interactive transition clears must keep SPI EC11/KEY strips on DMA; non-DMA latch use must stay scoped to low-power and non-status shutdown-final clearing")
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
        if "s_state.transition_clear_mask = 0U;" not in body:
            failures.append(
                f"status_led.c: {function_name} must cancel stale scoped transition clear before active work renders key feedback"
            )
    if (
        "BLE_HID_GAP_RANDOM_IDENTITY_KEY" not in ble_gap
        or "ble_hid_gap_rotate_native_recovery_identity" not in ble_gap
        or "ble_hid_gap_restore_random_identity_from_nvs" not in ble_gap
        or "ble_hs_id_gen_rnd(0, &addr)" not in ble_gap
        or "ble_hs_id_set_rnd(addr)" not in ble_gap
        or "nvs_set_blob(nvs, BLE_HID_GAP_RANDOM_IDENTITY_KEY, addr, 6)" not in ble_gap
        or "s_own_addr_type = BLE_OWN_ADDR_RANDOM;" not in ble_gap
    ):
        failures.append("ble_hid_gap_esp32.c: non-Type Windows-native recovery must rotate and persist a new static-random BLE identity so stale Windows bonds cannot loop")
    if "s_scan_rsp_fields.name_len = device_name_len > BLE_HID_SCAN_RSP_NAME_MAX_LEN" in ble_gap:
        failures.append("ble_hid_gap_esp32.c: BLE rename advertising must not silently truncate custom names")
    try:
        swift_pair_body = extract_c_function(ble_gap, "ble_hid_gap_configure_swift_pair_fields")
    except ValueError as exc:
        failures.append(f"ble_hid_gap_esp32.c: {exc}")
    else:
        if (
            "BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITHOUT_APPEARANCE" not in swift_pair_body
            or "return false;" not in swift_pair_body
            or "BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_HID_UUID" not in swift_pair_body
            or "s_adv_fields.uuids16 = &s_hid_service_uuid;" not in swift_pair_body
            or "s_adv_fields.uuids16_is_complete = 1;" not in swift_pair_body
            or "s_adv_fields.mfg_data = s_swift_pair_mfg_payload;" not in swift_pair_body
            or "s_adv_fields.mfg_data_len =" not in swift_pair_body
        ):
            failures.append(
                "ble_hid_gap_esp32.c: Swift Pair must keep HID UUID for short names and fall back to normal advertising when the full name cannot fit"
            )
    if "#define BLE_HID_GAP_RECOVERY_SWIFT_PAIR_PROMPT_MS 45000LL" not in ble_gap:
        failures.append(
            "ble_hid_gap_esp32.c: recovery must expose one bounded Swift Pair window for Windows native keyboard pairing"
        )
    if "#define BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS 0LL" not in ble_gap:
        failures.append(
            "ble_hid_gap_esp32.c: first pairing must not emit unsolicited Swift Pair popups"
        )
    try:
        type_recovery_body = extract_c_function(
            ble_gap, "ble_hid_gap_configure_type_controlled_recovery_adv_fields"
        )
    except ValueError as exc:
        failures.append(f"ble_hid_gap_esp32.c: {exc}")
    else:
        if (
            "s_scan_rsp_fields.uuids128 = &s_audio_stream_service_uuid;" not in type_recovery_body
            or "s_adv_fields.appearance_is_present = 1;" not in type_recovery_body
            or "s_adv_fields.uuids16 = &s_hid_service_uuid;" not in type_recovery_body
            or "s_adv_fields.mfg_data" in type_recovery_body
        ):
            failures.append(
                "ble_hid_gap_esp32.c: Type-controlled recovery fallback advertising must stay HID-pairable for Windows and Type-discoverable after the bounded Swift Pair window"
            )
    if (
        "s_recovery_suppress_swift_pair_prompt" not in ble_gap
        or "recovery: Swift Pair prompt suppressed for Type-controlled silent recovery" not in ble_gap
        or "ble_hid_gap_forget_bonds_and_repair_type_controlled_silent" not in ble_gap
        or "ble_hid_gap_forget_bonds_and_repair_inner(true, true)" not in ble_gap
        or "ble_hid_gap_forget_bonds_and_repair_inner(true, false)" not in ble_gap
        or "suppress_swift_pair=%u" not in ble_gap
    ):
        failures.append(
            "ble_hid_gap_esp32.c: BLE rename must have a Type-controlled silent recovery path while explicit Type recovery keeps Swift Pair available"
        )
    if (
        "pairing_window\n            ? ble_hid_gap_recovery_swift_pair_prompt_remaining_ms()" not in ble_gap
        or "if (type_recovery_requested && !swift_pair_requested)" not in ble_gap
    ):
        failures.append(
            "ble_hid_gap_esp32.c: Type-controlled recovery must allow the same bounded Windows Swift Pair prompt before falling back to the Type-discoverable profile"
        )
    try:
        recovery_body = extract_c_function(ble_gap, "ble_hid_gap_forget_bonds_and_repair_inner")
    except ValueError as exc:
        failures.append(f"ble_hid_gap_esp32.c: {exc}")
    else:
        async_reset_index = recovery_body.find("bond_delete=async_after_disconnect")
        reopen_index = recovery_body.find("ble_hid_gap_open_recovery_pairing_window(", async_reset_index)
        schedule_delete_index = recovery_body.find("ble_hid_gap_schedule_recovery_bond_delete", reopen_index)
        adv_index = recovery_body.find("esp_err_t adv_ret = ble_hid_gap_start_advertising();", reopen_index)
        if (
            async_reset_index < 0
            or reopen_index < 0
            or schedule_delete_index < 0
            or adv_index < 0
            or not (async_reset_index < reopen_index < schedule_delete_index < adv_index)
        ):
            failures.append(
                "ble_hid_gap_esp32.c: recovery must avoid synchronous ble_store_clear, open the stable-identity pairing window, schedule async local bond delete, then restart advertising"
            )
        if "ble_store_clear()" in recovery_body or "ble_store_clear_failed" in recovery_body:
            failures.append("ble_hid_gap_esp32.c: recovery must not synchronously clear the whole NimBLE NVS store in the double-click hot path")
        if not re.search(
            r"const\s+bool\s+type_link_ready_before_recovery\s*=\s*ble_audio_stream_is_type_link_ready\(\);\s*"
            r"const\s+bool\s+type_host_recent_before_recovery\s*=[\s\S]*?ble_audio_stream_was_type_host_recently_seen\(\);\s*"
            r"const\s+bool\s+type_controlled_recovery\s*=[\s\S]*?type_controlled_request[\s\S]*?type_link_ready_before_recovery[\s\S]*?type_host_recent_before_recovery\s*;",
            recovery_body,
        ):
            failures.append(
                "ble_hid_gap_esp32.c: recovery must use recent Type heartbeat or Type-host presence to preserve stable identity, while the advertising path still exposes the bounded Windows pairing prompt"
            )
        refresh_index = recovery_body.find("pairing window already active; refreshing advertising with stable BLE identity")
        refresh_reopen_index = recovery_body.find("ble_hid_gap_open_recovery_pairing_window(", refresh_index)
        refresh_adv_index = recovery_body.find(
            "esp_err_t adv_ret = ble_hid_gap_start_advertising();",
            refresh_index,
        )
        if (
            refresh_index < 0
            or refresh_reopen_index < 0
            or refresh_adv_index < 0
            or not (refresh_index < refresh_reopen_index < refresh_adv_index)
        ):
            failures.append(
                "ble_hid_gap_esp32.c: active recovery window must renew the full pairing window before refreshing stable-identity advertising"
            )
        if (
            "stable Type-controlled" not in recovery_body
            or "rotated native Windows" not in recovery_body
            or "ble_hid_gap_rotate_native_recovery_identity(\"recovery_pairing_reset\")" not in recovery_body
        ):
            failures.append(
                "ble_hid_gap_esp32.c: connected recovery must terminate first, delete the local bond asynchronously, keep Type identity stable, and rotate native Windows identity before advertising"
            )
    if "NimBLE advertising deferred: recovery async local bond delete pending" not in ble_gap:
        failures.append(
            "ble_hid_gap_esp32.c: advertising must be deferred while recovery async local bond delete is pending"
        )
    if "recovery: rejecting connection while async local bond delete is pending" not in ble_gap:
        failures.append(
            "ble_hid_gap_esp32.c: recovery must reject stale Windows connections while local bond delete is pending"
        )
    try:
        disconnect_body = extract_c_function(ble_gap, "ble_hid_gap_handle_disconnect")
    except ValueError as exc:
        failures.append(f"ble_hid_gap_esp32.c: {exc}")
        disconnect_body = ""
    disconnect_defer_index = disconnect_body.find(
        "advertising deferred after disconnect until async local bond delete completes"
    )
    disconnect_stable_index = disconnect_body.find("pairing reset continues after disconnect, BLE identity ready")
    disconnect_adv_index = disconnect_body.find("ble_hid_gap_start_advertising();")
    disconnect_bond_index = disconnect_body.find("bonded_peer_count > 0")
    disconnect_keep_index = disconnect_body.find("keeping pairing window visible until secure reconnect")
    disconnect_notify_index = disconnect_body.find(
        'ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_window_after_disconnect")'
    )
    disconnect_old_close_index = disconnect_body.find(
        'ble_hid_gap_close_recovery_pairing_window("bond restored after recovery disconnect")'
    )
    if (
        disconnect_adv_index < 0
        or disconnect_defer_index < 0
        or disconnect_stable_index < 0
        or not (disconnect_defer_index < disconnect_adv_index < disconnect_stable_index)
    ):
        failures.append(
            "ble_hid_gap_esp32.c: disconnect recovery must defer advertising for async local bond delete, then restart advertising with the ready Type/native BLE identity"
        )
    if (
        disconnect_bond_index < 0
        or disconnect_keep_index < 0
        or disconnect_notify_index < 0
        or disconnect_old_close_index >= 0
        or not (disconnect_bond_index < disconnect_keep_index < disconnect_adv_index < disconnect_notify_index)
    ):
        failures.append(
            "ble_hid_gap_esp32.c: recovery disconnect must keep pairing window visible through bond churn until secure reconnect"
        )
    enc_change_index = ble_gap.find("case BLE_GAP_EVENT_ENC_CHANGE:")
    enc_success_index = ble_gap.find(
        'ble_hid_gap_note_secure_connection(\n                        event->enc_change.conn_handle,\n                        "secure connection established")',
        enc_change_index,
    )
    enc_connected_led_index = ble_gap.find(
        "status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, false)",
        enc_success_index,
    )
    if (
        enc_change_index < 0
        or enc_success_index < 0
        or enc_connected_led_index < 0
        or not (enc_change_index < enc_success_index < enc_connected_led_index)
    ):
        failures.append(
            "ble_hid_gap_esp32.c: successful recovery pairing must move LED from pairing to connected find-Type state"
        )
    enc_failure_index = ble_gap.find("pairing encryption failure", enc_change_index)
    enc_wait_index = ble_gap.find("keeping pairing advertising available for Windows retry", enc_change_index)
    enc_pending_delete_terminate_index = ble_gap.find(
        "terminating encrypted connection while async local bond delete is pending",
        enc_change_index,
    )
    enc_next_case_index = ble_gap.find("case BLE_GAP_EVENT_NOTIFY_TX:", enc_change_index)
    enc_failure_terminate_index = ble_gap.find(
        "ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM)",
        enc_failure_index,
        enc_next_case_index,
    )
    enc_failed_handle_index = ble_gap.find(
        "s_recovery_security_failed_conn_handle = event->enc_change.conn_handle",
        enc_failure_index,
        enc_next_case_index,
    )
    enc_failed_skip_index = ble_gap.find(
        "after prior encryption failure; waiting for disconnect before retry",
    )
    if (
        enc_change_index < 0
        or enc_failure_index < 0
        or enc_wait_index < 0
        or not (enc_change_index < enc_failure_index < enc_wait_index)
    ):
        failures.append(
            "ble_hid_gap_esp32.c: recovery encryption failures must keep the pairing window available while Windows retries or disconnects"
        )
    if enc_pending_delete_terminate_index < 0:
        failures.append(
            "ble_hid_gap_esp32.c: ENC_CHANGE must reject stale encrypted connections only while async local bond delete is pending"
        )
    if enc_failed_handle_index < 0 or enc_failure_terminate_index < 0:
        failures.append(
            "ble_hid_gap_esp32.c: recovery ENC_CHANGE failure must quarantine and terminate the failed pairing connection before retry"
        )
    if enc_failed_skip_index < 0:
        failures.append(
            "ble_hid_gap_esp32.c: recovery security helper must not restart SMP on a connection that already failed encryption"
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
    if re.search(r"if\s*\(!safety\)\s*\{\s*status_led_apply_zone_brightness_caps_locked\(frame\);", status_led):
        failures.append(
            "status_led.c: safety warning states must still respect Type four-zone brightness caps"
        )
    if "status_led_normalize_strip_peak_to_type_max" in status_led:
        failures.append(
            "status_led.c: Type zone brightness must preserve effect percentages; frame peak normalization erases fade tails"
        )
    if not re.search(
        r"status_led_scale_strip_percent\(\s*frame->status[\s\S]*?"
        r"s_state\.status_zone_brightness_percent\);[\s\S]*?"
        r"status_led_balance_status_rgb_energy_to_peak_strip\(frame->status, STATUS_LED_STATUS_COUNT\);[\s\S]*?"
        r"status_led_scale_strip_percent\(\s*frame->key",
        status_led,
    ):
        failures.append(
            "status_led.c: status mixed RGB energy must be balanced after status-zone brightness scaling and before other strips"
        )
    if re.search(r"status_led_balance_status_rgb_energy_to_peak_strip\(frame->(key|ec11|edge)", status_led):
        failures.append(
            "status_led.c: mixed-RGB energy balancing is a status-strip correction only; do not change accepted key/EC11/edge effects"
        )
    if not re.search(
        r"status_led_balance_status_rgb_energy_to_peak[\s\S]*?"
        r"return\s+status_led_scale_rgb_to_channel_sum\(color, peak\);",
        status_led,
    ):
        failures.append(
            "status_led.c: status mixed RGB balance must scale channel sum to the current single-channel peak"
        )
    try:
        render_power = extract_c_function(status_led, "status_led_render_power_locked")
        render_ble = extract_c_function(status_led, "status_led_render_ble_locked")
        render_ota = extract_c_function(status_led, "status_led_render_ota_locked")
    except ValueError as exc:
        failures.append(f"status_led.c: {exc}")
    else:
        if (
            "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT" not in render_power
            or "status_led_token_relative_to_peak_locked" not in render_power
        ):
            failures.append("status_led.c: active PWR status brightness must map its design peak to the Type status-zone cap")
        if (
            "STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT" not in render_ble
            or "STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT" not in render_ble
            or "status_led_token_relative_to_peak_locked" not in render_ble
        ):
            failures.append("status_led.c: active BLE status brightness must map Type-ready/find-Type design peaks to the Type status-zone cap")
        if (
            "STATUS_LED_OTA_OK_MAX_PERCENT" not in render_ota
            or "status_led_effect_percent_relative_to_peak" not in render_ota
        ):
            failures.append("status_led.c: OTA status brightness must map its OK pulse peak to the Type status-zone cap")
    for token in (
        "STATUS_LED_CHARGING_BREATH_UNKNOWN_FLOOR_PERCENT 8U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1300U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT",
        "STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 12U",
        "STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT 14U",
        "STATUS_LED_BATTERY_STATUS_WINDOW_LOW_PROFILE_PWR_PERCENT STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT",
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
            if "STATUS_LED_LOW_POWER_PWR_WHITE_PERCENT" in branch:
                failures.append("status_led.c: plugged low-power PWR must not use a low-power brightness alias")
            if (
                "status_led_token_relative_to_peak_locked" not in branch or
                "STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT" not in branch
            ):
                failures.append("status_led.c: plugged low-power PWR must map to the same Type-capped peak as active PWR")
            if "status_led_token_locked" in branch:
                failures.append("status_led.c: plugged low-power PWR must not apply a hidden low-power dimming percent")
            if "status_led_render_power_locked" in branch:
                failures.append("status_led.c: plugged low-power PWR must not reuse charging/full breath rendering")
        if (
            "STATUS_LED_LOW_POWER_PWR_PERCENT" in status_led or
            "STATUS_LED_LOW_POWER_PWR_WHITE_PERCENT" in status_led or
            "STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT" in status_led
        ):
            failures.append("status_led.c: low-power must not define separate brightness constants; Type brightness owns brightness")
        if "status_led_battery_display_available_locked()" not in body:
            failures.append("status_led.c: battery low-power PWR must use display-valid fallback, not only live battery_valid")
        if "status_led_rgb(255, 140, 0)" not in body:
            failures.append("status_led.c: battery low-power PWR must keep an amber idle fallback when battery sampling is unavailable")
    low_power_ble_helper = re.search(
        r"static\s+uint8_t\s+status_led_low_power_ble_percent_locked\(uint32_t now_ms, uint32_t ble_elapsed_ms\)[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not low_power_ble_helper:
        failures.append("status_led.c: missing shared low-power BLE percent helper with now_ms timing context")
    else:
        body = low_power_ble_helper.group("body")
        if not re.search(
            r"case\s+STATUS_LED_BLE_PAIRING:\s*\n\s*case\s+STATUS_LED_BLE_REPAIRING:\s*\n\s*"
            r"[\s\S]*?"
            r"STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS[\s\S]*?"
            r"STATUS_LED_BLE_ATTENTION_PERCENT",
            body,
        ):
            failures.append("status_led.c: low-power BLE helper must blink pairing/re-pair instead of latching solid")
        if not re.search(
            r"case\s+STATUS_LED_BLE_RECONNECTING:\s*\n\s*"
            r"return\s+0U;",
            body,
        ):
            failures.append("status_led.c: low-power idle must keep ordinary reconnecting BLE dark")
        if not re.search(
            r"case\s+STATUS_LED_BLE_CONNECTED:[\s\S]*?"
            r"!\s*status_led_low_power_ble_ready_window_active_locked\(now_ms\)[\s\S]*?"
            r"return\s+0U;[\s\S]*?"
            r"status_led_double_pulse_on\(\s*ble_elapsed_ms,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?"
            r"STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT[\s\S]*?"
            r"STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT",
            body,
        ):
            failures.append("status_led.c: low-power connected must show the low-floor bounded find-Type cue only during the fresh connection window")
        if not re.search(
            r"case\s+STATUS_LED_BLE_TYPE_READY:[\s\S]*?"
            r"status_led_low_power_ble_ready_window_active_locked\(now_ms\)[\s\S]*?"
            r"STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT[\s\S]*?"
            r":\s*0U;",
            body,
        ):
            failures.append("status_led.c: low-power TYPE_READY must stay visible only during the fresh Type-ready window")
    if "static bool status_led_low_power_ble_ready_window_active_locked(uint32_t now_ms)" not in status_led:
        failures.append("status_led.c: low-power BLE timing must have an explicit ready-window helper")
    if "if (status_led_low_power_ble_ready_window_active_locked(now_ms)) {\n            return STATUS_LED_REFRESH_MS;\n        }" not in status_led:
        failures.append("status_led.c: low-power BLE ready window must use normal refresh until the short visible window expires")
    low_power_ble = re.search(
        r"static\s+void\s+status_led_render_low_power_ble_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not low_power_ble:
        failures.append("status_led.c: missing low-power BLE renderer")
    else:
        body = low_power_ble.group("body")
        if "status_led_low_power_ble_percent_locked(\n        now_ms,\n        status_led_ble_elapsed_locked(now_ms))" not in body:
            failures.append("status_led.c: low-power BLE renderer must use the shared low-power BLE helper")
        if (
            "status_led_low_power_ble_peak_percent_locked()" not in body or
            "status_led_token_relative_to_peak_locked" not in body
        ):
            failures.append("status_led.c: low-power BLE renderer must share the state-specific BLE peak mapper")
    if not re.search(
        r"static\s+uint8_t\s+status_led_low_power_ble_peak_percent_locked[\s\S]*?"
        r"case\s+STATUS_LED_BLE_CONNECTED:\s*\n\s*"
        r"return\s+STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT;",
        status_led,
    ):
        failures.append("status_led.c: low-power connected find-Type must preserve the same Type-capped peak mapping as active connected")
    if re.search(
        r"status_led_connected_hid_only_percent_locked|"
        r"STATUS_LED_BLE_CONNECTED_HEARTBEAT_PERIOD_MS|"
        r"STATUS_LED_BLE_CONNECTED_CONFIRM_MS|"
        r"STATUS_LED_BLE_CONNECTED_BASE_PERCENT",
        status_led,
    ):
        failures.append("status_led.c: active BLE rendering must not use the old low-base HID-only connected heartbeat/confirmation renderer")
    if "STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS 2000U" not in status_led:
        failures.append("status_led.c: HID-only connected must keep the bounded find-Type double-flash period")
    if "STATUS_LED_BLE_CONNECTED_FIND_TYPE_WINDOW_MS" in status_led:
        failures.append("status_led.c: active HID-only connected find-Type must not expire to dark from a short status window; only idle may turn BLE dark")
    if "STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT 5U" not in status_led:
        failures.append("status_led.c: HID-only connected find-Type must keep the operator-tuned low blue floor between the old too-bright floor and full dark")
    if "STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT" not in status_led:
        failures.append("status_led.c: HID-only connected find-Type pulse must keep the shared attention peak")
    if not re.search(
        r"case\s+STATUS_LED_BLE_CONNECTED:[\s\S]*?"
        r"if\s*\(\s*status_led_connected_find_type_window_active_locked\(now_ms\)\s*\)[\s\S]*?"
        r"status_led_double_pulse_on\(\s*ble_elapsed_ms,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?"
        r"STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT[\s\S]*?"
        r"STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT[\s\S]*?"
        r"status_led_token_relative_to_peak_locked\(\s*"
        r"ble_blue,\s*percent,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT,\s*false\s*\)[\s\S]*?"
        r"break;\s*case\s+STATUS_LED_BLE_TYPE_READY:[\s\S]*?"
        r"STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT",
        status_led,
    ):
        failures.append("status_led.c: active BLE rendering must make HID-only connected a continuous low-floor find-Type double flash with its peak mapped to the Type cap; only TYPE_READY may be steady blue")
    if not re.search(
        r"static\s+bool\s+status_led_connected_find_type_window_active_locked\(uint32_t now_ms\)[\s\S]*?"
        r"\(void\)now_ms;[\s\S]*?"
        r"return\s+true;",
        status_led,
    ):
        failures.append("status_led.c: active HID-only connected find-Type cue must stay visible until Type-ready, disconnect, or idle takes over")
    if not re.search(
        r"else\s+if\s*\(\s*strcasecmp\(state,\s*\"disconnected\"\)\s*==\s*0[\s\S]*?"
        r"strcasecmp\(state,\s*\"no_host\"\)\s*==\s*0[\s\S]*?"
        r"s_state\.ble_state\s*=\s*STATUS_LED_BLE_DISCONNECTED;[\s\S]*?"
        r"s_state\.ble_transition_ms\s*=\s*now_ms;",
        status_led,
    ):
        failures.append("status_led.c: preview disconnected/no_host must hold active disconnected BLE long enough for no-idle-dark validation")
    disconnected_case = re.search(
        r"case\s+STATUS_LED_BLE_DISCONNECTED:\s*\{(?P<body>[\s\S]*?)\n\s*\}",
        status_led,
    )
    if not disconnected_case:
        failures.append("status_led.c: missing active disconnected/no-host BLE render case")
    else:
        disconnected_body = disconnected_case.group("body")
        if (
            "(status_led_rgb_t){0}" not in disconnected_body
            or "status_led_token_relative_to_peak_locked" in disconnected_body
            or "STATUS_LED_BLE_DISCONNECTED_ACTIVE_PERCENT" in disconnected_body
        ):
            failures.append("status_led.c: disconnected/no-host BLE must not show the connected blue floor; low blue floor belongs only to connected BLE states")
    timed_output_active = extract_c_function(status_led, "status_led_timed_output_active_locked")
    if not timed_output_active:
        failures.append("status_led.c: missing timed-output active helper for active BLE refresh")
    else:
        for token, description in (
            ("s_state.ble_state == STATUS_LED_BLE_PAIRING", "pairing"),
            ("s_state.ble_state == STATUS_LED_BLE_REPAIRING", "repairing"),
            ("s_state.ble_state == STATUS_LED_BLE_RECONNECTING", "reconnecting"),
            ("s_state.ble_state == STATUS_LED_BLE_CONNECTED", "HID-only connected"),
            ("s_state.ble_state == STATUS_LED_BLE_TYPE_READY", "TYPE_READY"),
            ("s_state.ble_state == STATUS_LED_BLE_DISCONNECTED", "disconnected/no-host"),
        ):
            if token not in timed_output_active:
                failures.append(f"status_led.c: active {description} BLE must keep normal refresh until idle, independent of external power")
        if "!s_state.external_power_present" in timed_output_active and "s_state.ble_state != STATUS_LED_BLE_DISCONNECTED" in timed_output_active:
            failures.append("status_led.c: active BLE refresh must not be limited to unplugged power; BLE may go dark only after idle takes over")
    reconnect_case = re.search(
        r"case\s+STATUS_LED_BLE_RECONNECTING:\s*\{(?P<body>[\s\S]*?)\n\s*\}",
        status_led,
    )
    pairing_case = re.search(
        r"case\s+STATUS_LED_BLE_PAIRING:\s*\n\s*case\s+STATUS_LED_BLE_REPAIRING:\s*\{(?P<body>[\s\S]*?)\n\s*\}",
        status_led,
    )
    if not pairing_case:
        failures.append("status_led.c: missing active pairing/repairing BLE render case")
    else:
        pairing_body = pairing_case.group("body")
        if (
            ": 0U" not in pairing_body
            or "status_led_token_relative_to_peak_locked" not in pairing_body
            or "STATUS_LED_BLE_PAIRING_PULSE_PERCENT" not in pairing_body
        ):
            failures.append("status_led.c: pairing/repairing BLE must pulse with a dark off phase; low blue floor belongs only to connected BLE states")
    if not reconnect_case:
        failures.append("status_led.c: missing active reconnecting BLE render case")
    else:
        reconnect_body = reconnect_case.group("body")
        if "status_led_blink_on" in reconnect_body:
            failures.append("status_led.c: ordinary reconnecting must use the off-floor double pulse, not pairing-style blink")
        if not re.search(
            r"status_led_double_pulse_on\(\s*ble_elapsed_ms,\s*"
            r"STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?"
            r"STATUS_LED_BLE_RECONNECT_PULSE_PERCENT[\s\S]*?"
            r":\s*0U[\s\S]*?"
            r"status_led_token_relative_to_peak_locked\(\s*"
            r"ble_blue,\s*percent,\s*STATUS_LED_BLE_RECONNECT_PULSE_PERCENT,\s*false\s*\)",
            reconnect_body,
        ):
            failures.append("status_led.c: reconnecting BLE must double-pulse with a dark off phase; low blue floor belongs only to connected BLE states")
    ota_setter = extract_c_function(status_led, "status_led_set_ota_active")
    if not ota_setter:
        failures.append("status_led.c: missing status_led_set_ota_active")
    else:
        for forbidden in (
            "status_led_clear_key_feedback_locked()",
            "s_state.ble_repair_until_ms = 0U",
            "s_state.ble_repair_cue_started_ms = 0U",
            "s_state.ble_repair_cue_until_ms = 0U",
        ):
            if forbidden in ota_setter:
                failures.append(
                    f"status_led.c: OTA must not take over independent key/BLE state via {forbidden}"
                )
        if not re.search(
            r"s_state\.ota_type_link_active\s*&&\s*"
            r"s_state\.ble_state\s*==\s*STATUS_LED_BLE_CONNECTED[\s\S]*?"
            r"s_state\.ble_state\s*=\s*STATUS_LED_BLE_TYPE_READY",
            ota_setter,
        ):
            failures.append(
                "status_led.c: a Type-proven OTA must promote connected BLE to TYPE_READY"
            )
        if "s_state.ota_type_link_active = false;" not in ota_setter:
            failures.append(
                "status_led.c: OTA exit must clear the Type-OTA LED evidence"
            )
    ota_type_link_setter = extract_c_function(status_led, "status_led_set_type_ota_link_active")
    if "s_state.ota_type_link_active = active;" not in ota_type_link_setter:
        failures.append("status_led.c: missing explicit Type-OTA link evidence setter")
    if not re.search(
        r"s_state\.ota_active\s*&&\s*"
        r"s_state\.ota_type_link_active\s*&&\s*"
        r"state\s*==\s*STATUS_LED_BLE_CONNECTED[\s\S]*?"
        r"state\s*=\s*STATUS_LED_BLE_TYPE_READY",
        status_led,
    ):
        failures.append(
            "status_led.c: active Type OTA must not be demoted to connected-only BLE"
        )
    type_activity = extract_c_function(
        ble_audio_stream, "ble_audio_stream_note_type_activity"
    )
    ota_control_start = ble_audio_stream.find(
        'if (strcmp(command, "TYPE:OTA") == 0)'
    )
    ota_control_end = ble_audio_stream.find(
        'if (strcmp(command, "TYPE:BYE") == 0', ota_control_start
    )
    ota_control = ble_audio_stream[ota_control_start:ota_control_end]
    if (
        not re.search(
            r"if\s*\(\s*ota_activity\s*\)\s*\{\s*"
            r"status_led_set_type_ota_link_active\(true, reason\);",
            type_activity,
        )
        or ota_control_start < 0
        or ota_control_end < 0
        or "ble_audio_stream_note_type_activity(command);" not in ota_control
    ):
        failures.append(
            "ble_audio_stream: TYPE:OTA must establish Type evidence through the shared activity path"
        )
    ota_ble_helper = "status_led_ota_ble_steady_locked"
    if ota_ble_helper in status_led:
        failures.append(
            "status_led.c: OTA must not force the independent BLE indicator steady"
        )
    for stale_token in ("status_led_note_ota_activity", "STATUS_LED_OTA_BLE_STEADY_HOLD_MS"):
        if stale_token in status_led or stale_token in ble_firmware_ota:
            failures.append(f"BLE OTA must not use stale activity hold token {stale_token}")
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
    if (
        "STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MAX_PERCENT,\n        STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT" not in status_led
        or "STATUS_LED_STATUS_TAIL_OVERLAP_QUANTUM_PERCENT,\n        STATUS_LED_RECORDING_LEVEL_LOCK_WAIT_MS,\n        STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT" not in status_led
    ):
        failures.append("status_led.c: ~LED:STATUS contract printf arguments must keep recording_level_lock_wait_ms in its own field without shifting status-tail effect readback")
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
    elif "pdMS_TO_TICKS(STATUS_LED_RECORDING_LEVEL_LOCK_WAIT_MS)" not in recording_level_block.group(0):
        failures.append("status_led.c: live REC level must use a bounded nonblocking mutex wait")
    elif "portMAX_DELAY" in recording_level_block.group(0):
        failures.append("status_led.c: live REC level must not block the audio path on the LED state mutex")
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
    recording_set_body = re.search(
        r"void\s+status_led_set_recording\([^)]*\)[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_set_recording_level",
        status_led,
    )
    if (
        recording_set_body is None
        or "const bool recording_state_changed" not in recording_set_body.group(0)
        or "if (recording_state_changed || !next_recording_active)" not in recording_set_body.group(0)
        or "if (recording_state_changed) {\n            s_state.last_transition_ms = now_ms;" not in recording_set_body.group(0)
        or "status_led_clear_ec11_feedback_locked();" not in recording_set_body.group(0)
    ):
        failures.append(
            "status_led.c: recording active->active updates must not reset REC level/transition timing, and active recording must clear EC11 white feedback"
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
    if (
        not repair_envelope
        or "ble_elapsed_ms >= STATUS_LED_BLE_REPAIR_CUE_MS" not in repair_envelope.group("body")
        or "return 0U;" not in repair_envelope.group("body")
        or "status_led_double_pulse_on(ble_elapsed_ms, 900U)" not in repair_envelope.group("body")
    ):
        failures.append("status_led.c: BLE repair blink envelope must be a bounded three-cycle double-flash cue")
    elif "status_led_blink_on" in repair_envelope.group("body"):
        failures.append("status_led.c: BLE repair blink envelope must not add a third offset blink")
    if "STATUS_LED_BLE_REPAIR_MIN_PERCENT 30U" not in status_led or \
       "STATUS_LED_BLE_REPAIR_MAX_PERCENT 100U" not in status_led:
        failures.append("status_led.c: BLE re-pair confirmation must keep the legacy blue base-and-peak double-flash")
    if "now_ms < s_state.ble_repair_cue_started_ms) {\n        return base_percent;" not in status_led:
        failures.append("status_led.c: BLE re-pair lead-clear window must show the legacy blue base instead of going dark")
    if "status_led_ble_repair_cue_active_locked(now_ms)" not in status_led:
        failures.append("status_led.c: long recovery pairing windows must not render the three-cycle repair confirmation forever")
    repair_start = re.search(
        r"static\s+void\s+status_led_start_ble_repair_locked_for_ms[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if (
        not repair_start
        or "cue_already_active = status_led_ble_repair_cue_active_locked(now_ms)" not in repair_start.group("body")
        or "if (!cue_already_active)" not in repair_start.group("body")
    ):
        failures.append("status_led.c: repeated BLE repair notifications must not restart the active three-cycle cue")
    elif "status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_REPAIR)" in repair_start.group("body"):
        failures.append("status_led.c: BLE repair must not insert a transition clear before the accepted double-flash cue")
    recovery_window = extract_c_function(status_led, "status_led_ble_recovery_window_active_locked")
    if (
        "status_led_ble_repair_active_locked(now_ms)" not in recovery_window
        or "s_state.ble_state == STATUS_LED_BLE_PAIRING" not in recovery_window
        or "s_state.ble_state == STATUS_LED_BLE_REPAIRING" not in recovery_window
        or "s_state.ble_state == STATUS_LED_BLE_RECONNECTING" not in recovery_window
    ):
        failures.append("status_led.c: BLE recovery window must cover pairing/repairing/reconnecting states while repair is active")
    recovery_elapsed = extract_c_function(status_led, "status_led_ble_recovery_window_elapsed_locked")
    if (
        "s_state.ble_repair_cue_until_ms != 0U" not in recovery_elapsed
        or "return now_ms - s_state.ble_repair_cue_until_ms;" not in recovery_elapsed
        or "return status_led_ble_elapsed_locked(now_ms);" not in recovery_elapsed
    ):
        failures.append("status_led.c: BLE recovery window double-flash phase must be anchored after the accepted repair cue, not reset by BLE state transitions")
    render_ble = extract_c_function(status_led, "status_led_render_ble_locked")
    cue_idx = render_ble.find("status_led_ble_repair_cue_active_locked(now_ms)")
    window_idx = render_ble.find("status_led_ble_recovery_window_active_locked(now_ms)")
    switch_idx = render_ble.find("switch (s_state.ble_state)")
    recovery_block = render_ble[window_idx:switch_idx] if window_idx >= 0 and switch_idx > window_idx else ""
    if (
        cue_idx < 0
        or window_idx <= cue_idx
        or switch_idx <= window_idx
        or "status_led_double_pulse_on" not in recovery_block
        or "STATUS_LED_BLE_RECONNECT_PULSE_PERCENT" not in recovery_block
        or "status_led_token_relative_to_peak_locked" not in recovery_block
        or "? STATUS_LED_BLE_RECONNECT_PULSE_PERCENT\n            : 0U;" not in recovery_block
    ):
        failures.append("status_led.c: BLE recovery window must render the accepted reconnect double-flash instead of pairing blink")
    if (
        "status_led_ble_recovery_window_elapsed_locked(now_ms)" not in recovery_block
        or "recovery_elapsed_ms" not in recovery_block
        or "STATUS_LED_BLE_REPAIR_TO_RECOVERY_GAP_MS STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS" not in status_led
        or "recovery_elapsed_ms >= STATUS_LED_BLE_REPAIR_TO_RECOVERY_GAP_MS" not in recovery_block
        or "recovery_elapsed_ms - STATUS_LED_BLE_REPAIR_TO_RECOVERY_GAP_MS" not in recovery_block
        or "status_led_double_pulse_on(\n                    recovery_elapsed_ms - STATUS_LED_BLE_REPAIR_TO_RECOVERY_GAP_MS,\n                    STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS)" not in recovery_block
        or "status_led_double_pulse_on(ble_elapsed_ms" in recovery_block
    ):
        failures.append("status_led.c: BLE recovery window must wait through a full dark rearm gap before its phase-anchored reconnect double-flash")
    if "STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT 4U" not in status_led or \
       "STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT 16U" not in status_led:
        failures.append("status_led.c: EC11 re-pair ring must keep the legacy blue base-and-peak double-flash")
    repair_ring = re.search(
        r"static\s+void\s+status_led_render_ec11_repair_locked[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not repair_ring:
        failures.append("status_led.c: BLE re-pair must render the synchronized EC11 ring cue")
    else:
        repair_ring_text = repair_ring.group("body")
        if "status_led_ble_repair_percent_locked" not in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair must render from the same bounded BLE repair envelope")
        if "STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT" not in repair_ring_text or \
           "STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT" not in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair ring must use the named repair brightness constants")
        if "for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index)" not in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair must cover the intended full knob ring")
        if "status_led_set_max(&frame->ec11[index], color);" not in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair must light only the EC11 ring pixels")
        if "frame->edge" in repair_ring_text:
            failures.append("status_led.c: EC11 re-pair confirmation must not borrow the edge/frame LEDs")
    if not re.search(
        r"static\s+void\s+status_led_preview_state[^{]*\{[\s\S]*?"
        r"status_led_schedule_idle_transition_clear_locked\(now_ms\);[\s\S]*?"
        r"status_led_force_transition_clear_locked\(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS\);[\s\S]*?"
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
    bluetooth_block = re.search(
        r"function\s+Get-BluetoothSteps\s*\{([\s\S]*?)\nfunction\s+Get-ReviewSteps",
        human_review,
    )
    if not bluetooth_block:
        failures.append("status_led_human_effect_review.ps1: missing Bluetooth review block")
    else:
        bluetooth_text = bluetooth_block.group(1)
        for token in (
            'ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "Bluetooth", "FinalVisual", "FinalRetest", "FinalCombo", "RootCause", "StaticRoot", "Repro", "RecordingIndependence", "IdleTransition", "TailOnly", "ComboOnly", "Full")',
            'if ($Mode -eq "Bluetooth")',
            'bluetooth-type-ready',
            'bluetooth-hid-only-find-type',
            'bluetooth-pairing-native',
            'bluetooth-reconnecting',
            'bluetooth-double-repair',
            'bluetooth-ota-active',
            '~LED:PREVIEW type_ready',
            '~LED:PREVIEW connected',
            '~LED:PREVIEW pairing',
            '~LED:PREVIEW reconnecting',
            '~LED:PREVIEW repairing',
            '~LED:PREVIEW ota',
            '三次同步蓝色双闪',
            '不能稳蓝，也不能一直闪',
            'AI/WARN 不参与；OTA 阻止 idle',
        ):
            if token not in human_review:
                failures.append(f"status_led_human_effect_review.ps1: Bluetooth review must include {token}")
        if 'scene-recording-processing-live' in bluetooth_text or 'volume-capture-sweep' in bluetooth_text:
            failures.append("status_led_human_effect_review.ps1: Bluetooth review must stay focused on BLE scenarios, not full recording/volume tuning")
    idle_transition_block = re.search(
        r"function\s+Get-IdleTransitionSteps\s*\{([\s\S]*?)\nfunction\s+Get-TailOnlySteps",
        human_review,
    )
    if not idle_transition_block:
        failures.append("status_led_human_effect_review.ps1: missing IdleTransition review block")
    else:
        idle_transition_text = idle_transition_block.group(1)
        for token in (
            'ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "Bluetooth", "FinalVisual", "FinalRetest", "FinalCombo", "RootCause", "StaticRoot", "Repro", "RecordingIndependence", "IdleTransition", "TailOnly", "ComboOnly", "Full")',
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
    sdkconfig_defaults = read("sdkconfig.defaults")
    sdkconfig_defaults_esp32s3 = read("sdkconfig.defaults.esp32s3")
    if 'status_led_set_processing(true, "audio_session_finishing")' in voice_recording_control:
        failures.append(
            "voice_recording_control.c: post-stop AI cue must use recording_stop_processing_start, "
            "not the audio_session_finishing log detail"
        )
    if 'status_led_notify_ble_repairing("recovery_complete_pair_again")' in voice_recording_control:
        failures.append("voice_recording_control.c: recovery completion must not shorten the GAP-owned BLE pairing window")
    if (
        "RECOVERY:TYPE:SILENT" not in voice_recording_control
        or "ble_hid_gap_forget_bonds_and_repair_type_controlled_silent" not in voice_recording_control
        or "suppress_swift_pair_prompt ? 1u : 0u" not in voice_recording_control
    ):
        failures.append("voice_recording_control.c: Type silent recovery command must route BLE rename through no-Swift-Pair recovery without changing double-click recovery")
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
    if not re.search(
        r"esp_err_t\s+esp_hid_ble_gap_adv_init\(uint16_t appearance, const char \*device_name\)[\s\S]*?"
        r"ble_hs_cfg\.sm_io_cap\s*=\s*BLE_SM_IO_CAP_NO_IO;[\s\S]*?"
        r"ble_hs_cfg\.sm_bonding\s*=\s*1;[\s\S]*?"
        r"ble_hs_cfg\.sm_mitm\s*=\s*0;[\s\S]*?"
        r"legacy-compatible[\s\S]*?"
        r"ble_hs_cfg\.sm_sc\s*=\s*0;",
        ble_hid_gap,
    ):
        failures.append("ble_hid_gap_esp32.c: HID pairing must use no-IO legacy-compatible SMP for Windows HID pairing")
    for name, text in (
        ("sdkconfig.defaults", sdkconfig_defaults),
        ("sdkconfig.defaults.esp32s3", sdkconfig_defaults_esp32s3),
    ):
        if "# CONFIG_BT_CTRL_MODEM_SLEEP is not set" not in text or "CONFIG_BT_CTRL_MODEM_SLEEP=y" in text:
            failures.append(f"{name}: BLE controller modem sleep must stay disabled for Windows HID pairing stability")
    recovery_connect_index = ble_hid_gap.find("if (conn_desc_valid && recovery_pairing_window) {")
    recovery_consume_index = ble_hid_gap.find("s_recovery_swift_pair_consumed = true;", recovery_connect_index)
    recovery_led_index = ble_hid_gap.find(
        'ble_hid_gap_hold_recovery_pairing_led("ble_recovery_windows_connecting")',
        recovery_connect_index,
    )
    recovery_request_log_index = ble_hid_gap.find(
        "recovery: Windows connection during pairing window",
        recovery_connect_index,
    )
    recovery_request_index = ble_hid_gap.find(
        'ble_hid_gap_request_recovery_security_once(conn_handle, source != NULL ? source : "connect");',
        recovery_connect_index,
    )
    recovery_helper_index = ble_hid_gap.find("static void ble_hid_gap_request_recovery_security_once")
    recovery_helper_security_index = ble_hid_gap.find(
        "ble_gap_security_initiate(conn_handle)",
        recovery_helper_index,
        recovery_connect_index,
    )
    recovery_helper_log_index = ble_hid_gap.find(
        "recovery: waiting for Windows pairing security",
        recovery_helper_index,
        recovery_connect_index,
    )
    normal_security_index = ble_hid_gap.find("ble_gap_security_initiate(conn_handle)", recovery_connect_index)
    adv_connect_helper_index = ble_hid_gap.find('ble_hid_gap_handle_connect_established(event->connect.conn_handle, "adv_gap_connect")')
    global_listener_index = ble_hid_gap.find("static int ble_hid_gap_global_event_listener")
    global_connect_helper_index = ble_hid_gap.find('"global_gap_connect"', global_listener_index)
    if (
        "const bool conn_desc_valid = rc == 0;" not in ble_hid_gap
        or "const bool recovery_pairing_window = ble_hid_gap_recovery_pairing_window_open();" not in ble_hid_gap
        or recovery_connect_index < 0
        or recovery_led_index < 0
        or recovery_request_log_index < 0
        or recovery_request_index < 0
        or normal_security_index < 0
        or recovery_helper_index < 0
        or not (recovery_helper_index < recovery_helper_log_index < recovery_connect_index)
        or recovery_helper_security_index >= 0
        or "recovery: waiting for Windows pairing security" not in ble_hid_gap
        or "recovery: security initiate requested" in ble_hid_gap
        or not (recovery_connect_index < recovery_led_index < recovery_request_log_index < recovery_request_index < normal_security_index)
        or (recovery_consume_index >= 0 and recovery_connect_index < recovery_consume_index < normal_security_index)
        or adv_connect_helper_index < 0
        or global_listener_index < 0
        or global_connect_helper_index < 0
        or 'ble_hid_gap_close_recovery_for_type_audio(' not in ble_hid_gap
        or 'case BLE_GAP_EVENT_PASSKEY_ACTION:' not in ble_hid_gap
        or 'passkey numeric comparison auto-accepted' not in ble_hid_gap
        or 'BLE_SM_IOACT_DISP' not in ble_hid_gap
        or 'BLE_SM_IOACT_INPUT' not in ble_hid_gap
        or 'BLE_HID_GAP_PAIRING_PASSKEY' not in ble_hid_gap
        or 'ble_hid_gap_request_recovery_security_once(event->mtu.conn_handle, "mtu");' not in ble_hid_gap
        or 'ble_hid_gap_request_recovery_security_once(event->subscribe.conn_handle, "subscribe");' not in ble_hid_gap
        or "security initiate skipped: missing connection descriptor" not in ble_hid_gap
    ):
        failures.append("ble_hid_gap_esp32.c: recovery connect must use the shared GAP CONNECT helper, let Windows own pairing SMP during the pairing window, keep MTU/subscribe idempotent checks, log passkey actions, and allow Type audio to close only after secure pairing")
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
        r"STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT",
        status_led,
    ):
        failures.append("status_led.c: battery PWR idle must reuse the normal Type-capped status-window brightness")
    for token in (
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS",
        "STATUS_LED_BLE_ATTENTION_PERCENT",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: battery idle BLE attention blink constant missing: {token}")
    if "battery_quiet" in status_led:
        failures.append("status_led.c: quiet ACTIVE must not hide connected BLE before power_manager enters idle")
    if not re.search(
        r"status_led_render_low_power_ble_locked[^{]*\{[\s\S]*?"
        r"status_led_low_power_ble_percent_locked\(\s*now_ms,\s*status_led_ble_elapsed_locked\(now_ms\)\s*\)",
        status_led,
    ):
        failures.append("status_led.c: low-power idle must keep using the shared BLE helper with timing context so fresh connected/TYPE_READY windows render and then go dark")
    ble_set_state = re.search(
        r"void\s+status_led_set_ble_state[\s\S]*?"
        r"\n\}\n\nvoid\s+status_led_notify_ble_repairing",
        status_led,
    )
    if (
        "status_led_ble_state_attention_locked" not in status_led or
        ble_set_state is None or
        "const bool explicit_ready_window =\n            confidence_window && status_led_ble_state_ready_locked(state);" not in ble_set_state.group(0) or
        "s_state.low_power_disabled &&\n            !explicit_ready_window &&\n            !status_led_ble_state_attention_locked(state)" not in ble_set_state.group(0) or
        "if (!effect_only && !routine_low_power_ble)" not in ble_set_state.group(0) or
        "const bool active_work = s_state.recording_active ||\n            s_state.processing_active ||\n            s_state.ota_active;" not in ble_set_state.group(0) or
        "state_changed && !effect_only && !routine_low_power_ble &&" not in ble_set_state.group(0) or
        "status_led_ble_state_ready_locked(state) &&\n            !keep_repair_cue" not in ble_set_state.group(0) or
        "if (!keep_repair_cue && !keep_repair_window) {\n                s_state.ble_repair_cue_started_ms = 0U;\n                s_state.ble_repair_cue_until_ms = 0U;\n            }" not in ble_set_state.group(0) or
        "!active_work" not in ble_set_state.group(0) or
        "if (state_changed && !effect_only && !routine_low_power_ble)" not in ble_set_state.group(0) or
        "if (explicit_ready_window) {\n            s_state.ble_confidence_until_ms = now_ms + STATUS_LED_BLE_CONFIDENCE_MS;\n            changed = true;\n        }" not in ble_set_state.group(0) or
        "const bool ble_visual_transition =\n            state_changed && !effect_only && !active_work;" not in ble_set_state.group(0) or
        "if (ble_visual_transition) {\n                s_state.last_transition_ms = now_ms;" not in ble_set_state.group(0)
    ):
        failures.append("status_led.c: routine connected/TYPE_READY/DISCONNECTED BLE changes must stay quiet in low power, while explicit Type-ready confirmation may open the bounded BLE visible window")
    if "status_led_force_all_off();" in status_led:
        failures.append("status_led.c: all-off callers must explicitly choose whether the status strip may force non-DMA")
    all_off_body = re.search(
        r"static\s+void\s+status_led_force_all_off\(bool\s+force_non_dma\)[\s\S]*?"
        r"static\s+void\s+status_led_suspend_all_strips",
        status_led,
    )
    if (
        all_off_body is None
        or "status_led_transmit_changed_frame(&frame, STATUS_LED_STRIP_MASK_ALL, force_non_dma, false)" not in all_off_body.group(0)
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
        or "status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);" not in manual_off_body.group(0)
        or "status_led_request_refresh();" not in manual_off_body.group(0)
        or "status_led_force_all_off(" in manual_off_body.group(0)
    ):
        failures.append("status_led.c: manual/USB LED OFF must be queued to the LED task for non-DMA black latch, not transmitted inline")
    transition_clear_body = re.search(
        r"static\s+bool\s+status_led_render_transition_clear_locked\(status_led_frame_t\s+\*frame,\s*uint8_t\s+\*clear_mask_out\)[\s\S]*?"
        r"\n\}\n\nstatic\s+void\s+status_led_request_refresh",
        status_led,
    )
    if (
        transition_clear_body is None
        or "*frame = s_state.last_frame;" not in transition_clear_body.group(0)
        or "STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS" not in transition_clear_body.group(0)
        or "frame->status[STATUS_LED_SEM_REC] = (status_led_rgb_t){0};" not in transition_clear_body.group(0)
        or "STATUS_LED_TRANSITION_CLEAR_BLE" not in transition_clear_body.group(0)
        or "frame->status[STATUS_LED_SEM_BLE] = (status_led_rgb_t){0};" not in transition_clear_body.group(0)
        or "STATUS_LED_TRANSITION_CLEAR_EC11" not in transition_clear_body.group(0)
        or "STATUS_LED_TRANSITION_CLEAR_KEY" not in transition_clear_body.group(0)
        or "STATUS_LED_TRANSITION_CLEAR_EDGE" not in transition_clear_body.group(0)
        or "s_state.transition_clear_mask = 0U;" not in transition_clear_body.group(0)
    ):
        failures.append("status_led.c: transition clear must copy the previous frame and clear only scoped semantic/accent layers")
    if (
        "status_led_transition_clear_strip_mask_locked(&s_state.last_frame, transition_clear_mask)" not in status_led
        or "STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS" not in status_led
        or "uint8_t mask = 0U;" not in status_led
        or "STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS" not in status_led
        or "STATUS_LED_TRANSITION_CLEAR_BLE" not in status_led
        or "STATUS_LED_TRANSITION_CLEAR_EC11" not in status_led
        or "STATUS_LED_TRANSITION_CLEAR_KEY" not in status_led
        or "STATUS_LED_TRANSITION_CLEAR_EDGE" not in status_led
    ):
        failures.append("status_led.c: scoped transition clear must compute strip writes from the requested clear mask and previous lit accent strips")
    idle_clear = extract_c_function(status_led, "status_led_schedule_idle_transition_clear_locked")
    if "STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS" not in idle_clear:
        failures.append("status_led.c: routine idle transition clear must not include KEY strip feedback")
    if "STATUS_LED_TRANSITION_CLEAR_REPAIR \\\n    (STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS | STATUS_LED_TRANSITION_CLEAR_BLE | STATUS_LED_TRANSITION_CLEAR_EC11)" not in status_led:
        failures.append("status_led.c: BLE re-pair clear must be scoped to status/BLE plus EC11, not KEY/EDGE")
    key_dark_rewrite = extract_c_function(status_led, "status_led_key_dark_rewrite_needed")
    if (
        "low_power_active || status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT)" not in key_dark_rewrite
        or "(changed_strip_mask & STATUS_LED_STRIP_MASK_KEY) != 0U" not in key_dark_rewrite
        or "s_strip_last_tx_ms[STATUS_LED_STRIP_KEY]" not in key_dark_rewrite
        or "STATUS_LED_KEY_DARK_RESYNC_MS" not in key_dark_rewrite
        or "return false;" not in key_dark_rewrite
    ):
        failures.append("status_led.c: dark KEY strip must not be periodically rewritten during idle")
    if (
        "status_led_key_dark_latch_needed" in status_led
        or "STATUS_LED_KEY_DARK_CLEAR_WRITES" in status_led
        or "key_dark_clear_writes=%u" in status_led
            or "key_dark_clear_tx=spi_dma_prelatch_then_one_shot_rmt_gpio_low" not in status_led
            or "key_tail_guard_pixels=%u" not in status_led
            or "key_dark_latch_rmt_writes=%u" not in status_led
            or "key_dark_latch_expiry_dirty=1" not in status_led
            or "uint8_t key_dark_latch_pending_mask;" not in status_led
            or "s_state.key_dark_latch_pending_mask =" not in status_led
            or ".dark_latch_rmt_writes = STATUS_LED_KEY_DARK_LATCH_RMT_WRITES" not in status_led
            or ".dark_latch_rmt_writes = strip->dark_latch_rmt_writes" not in status_led
            or "bool key_has_light = status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT);" not in status_led
            or "bool key_force_non_dma = force_non_dma || !key_has_light;" not in status_led
        ):
        failures.append("status_led.c: KEY dark transitions must stay bounded: no periodic dark rewrites and no normal tail guard, but the all-dark latch must expose the fixed RMT black-latch write count")
    if not re.search(
        r"status_led_key_dark_rewrite_needed\(&frame,\s*changed_strip_mask,\s*low_power_active,\s*now_ms\)[\s\S]{0,160}"
        r"tx_strip_mask\s*=\s*\(uint8_t\)\(tx_strip_mask\s*\|\s*STATUS_LED_STRIP_MASK_KEY\);",
        status_led,
    ):
        failures.append("status_led.c: refresh loop must OR the KEY strip into tx_strip_mask for its initial dark-latch rewrite")
    refresh_once = extract_c_function(status_led, "status_led_refresh_once")
    if (
        "status_led_key_dark_transition_reinforce_needed(" in refresh_once
        or "write_count < STATUS_LED_KEY_DARK_CLEAR_WRITES" in refresh_once
        or "write_count = STATUS_LED_KEY_DARK_CLEAR_WRITES;" in refresh_once
    ):
        failures.append("status_led.c: KEY lit-to-dark frames must not add state-machine repeated dark writes; the bounded dark repeat belongs inside the backend one-shot latch")
    low_power_setter = re.search(
        r"void\s+status_led_set_low_power_disabled[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if not low_power_setter:
        failures.append("status_led.c: missing low-power disable setter")
    else:
        low_power_body = low_power_setter.group("body")
        if (
            "const bool preserve_repair_cue =" not in low_power_body
            or "disabled && status_led_ble_repair_cue_active_locked(now_ms)" not in low_power_body
            or "const bool next_low_power_disabled = disabled && !preserve_repair_cue;" not in low_power_body
            or "s_state.low_power_disabled = next_low_power_disabled;" not in low_power_body
            or "if (next_low_power_disabled)" not in low_power_body
            or '"repair_low_power_hold"' not in low_power_body
        ):
            failures.append("status_led.c: low-power idle must preserve the active BLE+EC11 repair cue instead of truncating the double-flash")
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
    if "EC11 shutdown ring fills from zero over the full confirmation window" not in status_doc:
        failures.append("status_led.md: docs must record that shutdown confirmation does not pre-light the first EC11 pixel")
    for stale in (
        "Low-power idle uses 12% for single-channel PWR colors",
        "4% per RGB channel for white PWR",
        "restrained PWR indicator",
        "restrained PWR-only status latch",
        "low-power level during unplugged idle wait",
    ):
        if stale in status_doc:
            failures.append(f"status_led.md: stale low-power brightness wording must not return: {stale}")
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
        r"static\s+bool\s+status_led_notify_shutdown_confirm_with_wait_and_duration\([^)]*\)[\s\S]*?"
        r"xSemaphoreTake\(s_mutex,\s*wait_ticks\)[\s\S]*?"
        r"status_led_resume_shutdown_confirm_output_locked\(\)",
        status_led,
    ):
        failures.append("status_led.c: shutdown confirmation must use a shared wait-bounded helper that wakes LED output without queuing a black clear frame")
    if "1U + ((elapsed * STATUS_LED_EC11_COUNT)" in status_led:
        failures.append("status_led.c: shutdown confirmation ring must not use a 1U+ shortcut that finishes the accepted fill effect early")
    if "uint32_t lit = (elapsed * STATUS_LED_EC11_COUNT) / STATUS_LED_SHUTDOWN_CONFIRM_MS;" not in status_led:
        failures.append("status_led.c: shutdown confirmation ring must fill over the full confirmation window")
    if "if (elapsed > 0U && lit == 0U)" in status_led or "if (lit == 0U)" not in shutdown_confirm:
        failures.append("status_led.c: accepted shutdown confirmation must clamp lit==0 to one EC11 amber pixel so the first frame cannot be all-black")
    if not re.search(
        r"void\s+status_led_notify_shutdown_confirm\([^)]*\)[\s\S]*?"
        r"status_led_notify_shutdown_confirm_with_wait_and_duration\([\s\S]*?"
        r"portMAX_DELAY[\s\S]*?"
        r"final\s*\?\s*STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS\s*:\s*STATUS_LED_SHUTDOWN_CONFIRM_MS",
        status_led,
    ):
        failures.append("status_led.c: legacy shutdown confirmation must preserve the blocking manual API")
    if not re.search(
        r"bool\s+status_led_try_notify_shutdown_confirm\([^)]*\)[\s\S]*?"
        r"status_led_notify_shutdown_confirm_with_wait_and_duration\([\s\S]*?"
        r"pdMS_TO_TICKS\(wait_ms\)[\s\S]*?"
        r"final\s*\?\s*STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS\s*:\s*STATUS_LED_SHUTDOWN_CONFIRM_MS",
        status_led,
    ):
        failures.append("status_led.c: automatic shutdown must have a bounded shutdown confirmation API")
    if (
        "STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS UINT32_MAX" not in status_led
        or not re.search(
            r"bool\s+status_led_try_notify_shutdown_final_hold\([^)]*\)[\s\S]*?"
            r"status_led_notify_shutdown_confirm_with_wait_and_duration\([\s\S]*?"
            r"true,[\s\S]*?"
            r"STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS",
            status_led,
        )
        or "duration_ms == STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS" not in status_led
    ):
        failures.append("status_led.c: status LED must retain the final PWR-only hold helper for bounded shutdown callers")
    if not re.search(
        r'else\s+if\s*\(\s*strcasecmp\(state,\s*"shutdown_final"\)\s*==\s*0[\s\S]*?'
        r"s_state\.shutdown_confirm_until_ms\s*=\s*STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS;",
        status_led,
    ):
        failures.append("status_led.c: shutdown_final preview must hold the final PWR-only amber state until clear/next preview instead of expiring to black")
    if not re.search(
        r"bool\s+status_led_try_hold_shutdown_all_off\([^)]*\)[\s\S]*?"
        r"xSemaphoreTake\(s_mutex,\s*pdMS_TO_TICKS\(wait_ms\)\)[\s\S]*?"
        r"s_state\.shutdown_confirm_started_ms\s*=\s*0U;[\s\S]*?"
        r"s_state\.shutdown_confirm_final\s*=\s*false;[\s\S]*?"
        r"s_state\.output_disabled\s*=\s*true;[\s\S]*?"
        r"s_state\.low_power_disabled\s*=\s*true;[\s\S]*?"
        r"status_led_force_all_off\(true\);[\s\S]*?"
        r"status_led_suspend_all_strips\(\);",
        status_led,
    ):
        failures.append("status_led.c: explicit sleep/all-off callers must have a bounded API that clears shutdown cue state and forces LEDs dark")
    if not re.search(
        r"void\s+status_led_cancel_shutdown_confirm\([^)]*\)[\s\S]*?"
        r"if\s*\(\s*s_state\.shutdown_confirm_started_ms\s*!=\s*0U\s*\)[\s\S]*?"
        r"s_state\.shutdown_confirm_final\s*=\s*false",
        status_led,
    ):
        failures.append("status_led.c: shutdown confirmation cancel must clear final and non-final cues after failed PWR_HOLD")
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
        "reset_us=300 rmt_reset_us=300 spi_reset_us=600 spi_ws2812_waveform=4bit_3m2_0x8_0xE" not in status_led or
        "spi_dma_requested=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "spi_dma_actual=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "spi_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer" not in status_led or
        "rmt_tx_dma_actual=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_tx_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_mem_block_symbols=status:%u,ec11:%u,key:%u,edge:%u" not in status_led or
        "rmt_idle_drive=active_dma_low_power_all_zone_non_dma_final_frame_then_release_gpio_low" not in status_led
        or "shutdown_final_status_tx=dma_visible_pwr_no_black_latch" not in status_led
        or "low_power_all_zone_tx=non_dma_clear_and_final_frame" not in status_led
        or "low_power_spi_latch=spi_dma_prelatch_then_one_shot_rmt_gpio_low" not in status_led
        or "shutdown_final_all_zone_tx=non_dma_pwr_only_latch_or_all_off" not in status_led
    ):
        failures.append("status_led.c: ~LED:STATUS contract must expose RMT/SPI transport, per-strip DMA actual/fallback state, reset-tail timing, buffer size, and idle-drive policy")
    if (
        "STATUS_LED_SPI_CLOCK_HZ       3200000" not in status_led_backend
        or "STATUS_LED_SPI_BITS_PER_BIT   4U" not in status_led_backend
        or "STATUS_LED_SPI_RESET_BYTES    240U" not in status_led_backend
        or "0xEu" not in status_led_backend
        or "0x8u" not in status_led_backend
        or "for (int p = 3; p >= 0; --p)" not in status_led_backend
        or "spi_waveform=4bit_3m2_0x8_0xE" not in status_led_backend
        or "STATUS_LED_SPI_CLOCK_HZ       2500000" in status_led_backend
        or "STATUS_LED_SPI_BITS_PER_BIT   3U" in status_led_backend
        or "0x6u" in status_led_backend
        or "0x4u" in status_led_backend
    ):
        failures.append(
            "status_led_strip_backend.c: SPI WS2812 output must use the conservative 4-bit 3.2 MHz waveform (0=0x8, 1=0xE) so KEY active frames do not bring up downstream pixels"
        )
    low_power_resume = re.search(
        r"void\s+status_led_set_low_power_disabled[^{]*\{(?P<body>[\s\S]*?)\n\}",
        status_led,
    )
    if (
        not low_power_resume
        or "status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);" not in low_power_resume.group("body")
    ):
        failures.append("status_led.c: low-power resume must all-zone clear so physical WS2812 latch state cannot survive wake")
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
        r"\.name\s*=\s*\"key\"[\s\S]*?\.tail_guard_pixels\s*=\s*STATUS_LED_KEY_TAIL_GUARD_PIXELS[\s\S]*?\.transport\s*=\s*STATUS_LED_STRIP_TRANSPORT_SPI[\s\S]*?\.spi_host\s*=\s*SPI3_HOST",
        status_led,
    ):
        failures.append("status_led.c: key strip must request SPI3 DMA and explicitly use the KEY tail-guard constant")
    if "key_tail_guard_pixels=%u" not in status_led:
        failures.append("status_led.c: ~LED:STATUS must expose the KEY tail-guard contract")
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
        failures.append("status_led.c: low-power idle and final shutdown PWR-only confirmation must keep bounded final-latch state")
    if "bool force_non_dma = force_clear_tx || low_power_active;" in status_led:
        failures.append("status_led.c: ordinary transition clear frames must not force non-DMA; scope force_non_dma to clear/latch paths only")
    if not re.search(r"bool\s+force_non_dma\s*=\s*pwr_only_final_latch\s*;", status_led):
        failures.append("status_led.c: low-power/final latch state must stay scoped; interactive transition clears keep EC11/KEY on SPI DMA and shutdown-final PWR/status stays on DMA")
    shutdown_final_latch = extract_c_function(status_led, "status_led_transmit_shutdown_final_latch_frame")
    status_dma_index = shutdown_final_latch.find(
        "status_led_transmit_changed_frame(frame, STATUS_LED_STRIP_MASK_STATUS, false, false)"
    )
    non_status_index = shutdown_final_latch.find(
        "status_led_transmit_changed_frame(frame, non_status_mask, true, key_dark_latch_pending_tx)"
    )
    if (
        status_dma_index < 0
        or non_status_index < 0
        or status_dma_index > non_status_index
        or "status_led_transmit_changed_frame(frame, STATUS_LED_STRIP_MASK_STATUS, true, false)" in shutdown_final_latch
    ):
        failures.append("status_led.c: shutdown-final PWR/status visible frame must stay on the normal DMA path before non-status strips use the dark latch")
    spi_final_skip = extract_c_function(status_led, "status_led_skip_suspended_spi_final_latch")
    if (
        "s_strips[strip_index].transport != STATUS_LED_STRIP_TRANSPORT_SPI" not in spi_final_skip
        or "!s_strip_transport_suspended[strip_index]" not in spi_final_skip
        or "return !status_led_strip_has_light(colors, count);" not in spi_final_skip
        or "status_led_skip_suspended_spi_final_latch(\n            STATUS_LED_STRIP_EC11" not in status_led
        or not re.search(
            r"!key_dark_latch_pending_tx\s*&&\s*"
            r"status_led_skip_suspended_spi_final_latch\(\s*STATUS_LED_STRIP_KEY",
            transmit_changed_frame,
        )
    ):
        failures.append(
            "status_led.c: SPI EC11/KEY suspended dark frames must normally skip re-entry; KEY feedback expiry may bypass that skip only while a pending dark latch is dirty"
        )
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
    spi_one_shot = extract_c_function(status_led_backend, "status_led_spi_transmit_non_dma_rmt_once")
    spi_acquire_index = spi_one_shot.find("status_led_strip_backend_ensure_transport(backend, true)")
    spi_prelatch_index = spi_one_shot.find("status_led_spi_transmit(backend, color_order, colors)")
    release_index = spi_one_shot.find("status_led_strip_backend_release_transport(backend)")
    rmt_latch_index = spi_one_shot.find("status_led_rmt_transmit_available(backend, color_order, colors)")
    if (
        spi_acquire_index < 0
        or spi_prelatch_index < 0
        or release_index < 0
        or rmt_latch_index < 0
        or not (spi_acquire_index < spi_prelatch_index < release_index < rmt_latch_index)
        or "SPI DMA pre-latch before non-DMA RMT latch" not in spi_one_shot
        or "SPI DMA pre-latch acquire failed before non-DMA RMT latch" not in spi_one_shot
        or "backend->available && backend->spi_device != NULL" not in spi_one_shot
        or "status_led_strip_backend_colors_all_dark(backend, colors)" not in spi_one_shot
        or "backend->dark_latch_rmt_writes" not in spi_one_shot
        or "rmt_writes=%u dark=%u" not in spi_one_shot
        or "for (uint8_t write_index = 0U; write_index < rmt_latch_writes; ++write_index)" not in spi_one_shot
    ):
        failures.append(
            "status_led_strip_backend.c: SPI EC11/KEY final latch must reacquire SPI DMA, send the requested frame on SPI DMA, then perform the bounded one-shot non-DMA RMT latch before GPIO-low suspend"
        )
    if (
        "uint8_t dark_latch_rmt_writes;" not in status_led_backend_header
        or "uint8_t dark_latch_rmt_writes;" not in status_led_backend
        or "status_led_strip_backend_colors_all_dark" not in status_led_backend
        or "config->dark_latch_rmt_writes > 0U ? config->dark_latch_rmt_writes : 1U" not in status_led_backend
    ):
        failures.append("status_led_strip_backend.c/.h: backend must expose configurable all-dark one-shot RMT latch writes with default 1")
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
