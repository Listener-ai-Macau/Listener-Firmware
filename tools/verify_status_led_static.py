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
        "status_led_set_recording",
        "status_led_set_recording_level",
        "status_led_set_processing",
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
        "STATUS_LED_BOOT_ACK_MS 2500U",
        "STATUS_LED_CHARGING_BREATH_PERIOD_MS 3600U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1050U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 80U",
        "STATUS_LED_CHARGING_BREATH_MIN_PERCENT 1U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT 100U",
        "STATUS_LED_CHARGE_FULL_DEBOUNCE_MS 10000U",
        "STATUS_LED_CHARGE_FULL_MIN_MV 4050U",
        "STATUS_LED_CHARGE_FULL_MIN_PERCENT 88U",
        "STATUS_LED_BATTERY_DISPLAY_GREEN_PERCENT 60U",
        "STATUS_LED_FULL_STEADY_PERCENT 100U",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT 100U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS 120U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS 7880U",
        "STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT 18U",
        "STATUS_LED_PWR_COLOR_AMBER",
        "STATUS_LED_DIAG_VIS_LOW_POWER_OFF",
        "battery_display_level_percent",
        "status_led_update_battery_display_locked",
        "battery_display_rise_suppressed",
        "status_led_log_power_input_locked",
        "status_led_log_visual_state_locked",
        "status_led_log_output_state_locked",
        "visible_pwr_flags",
        "status_led_log_power_input_locked();",
        "DIAG_LED_POWER_INPUT",
        "DIAG_LED_VISUAL_STATE",
        "DIAG_LED_OUTPUT_STATE",
        "STATUS_LED_IDLE_REFRESH_MS 1000U",
        "STATUS_LED_CONTRACT_REV \"status_key_isolated_charge_natural_breath_v15\"",
        "boot_feedback_until_ms",
        "status_led_force_boot_feedback",
        "status_led_boot_power_color_locked",
        "STATUS_LED_NVS_BRIGHTNESS_KEY \"brightness\"",
        "led_contract_rev=",
        "unchanged_tx_suppression=1",
        "timing=ws2812_4020_compatible",
        "effect_profile=product_v1",
        "profile_cap_percent=%u",
        "brightness_percent=%u",
        "user_brightness_percent=%u",
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
        "status_led_refresh_delay_ms_locked",
        "status_led_request_refresh",
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
        "s_state.rec_source = STATUS_LED_REC_SOURCE_NONE",
        "s_state.battery_valid = false",
        "s_state.error_until_ms = 0",
        "strcmp(s_state.last_reason, \"preview\") == 0",
        "now_ms - s_state.last_transition_ms",
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
        "~LED:STATUS profile=%s detail=summary",
        "semantic_order=LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN",
        "rec_not_available_shows=WARN+REC",
        "status_led_profile_budget_ma_locked",
        "status_led_estimate_current_ma",
        "status_led_clamp_current_locked",
        "status_led_render_error_locked",
        "status_led_render_recording_locked",
        "recording_level_percent",
        "recording_level_updated_ms",
        "status_led_set_recording_level",
        "STATUS_LED_REC_GOLD_R 255U",
        "STATUS_LED_REC_GOLD_G 172U",
        "status_led_rec_gold()",
        "STATUS_LED_RECORDING_BREATH_PERIOD_MS 1900U",
        "STATUS_LED_RECORDING_BREATH_MIN_PERCENT 24U",
        "STATUS_LED_RECORDING_BREATH_MAX_PERCENT 42U",
        "STATUS_LED_RECORDING_LEVEL_STALE_MS 300U",
        "STATUS_LED_RECORDING_LEVEL_BOOST_MULTIPLIER 4U",
        "STATUS_LED_RECORDING_LEVEL_FLOOR_PERCENT 35U",
        "STATUS_LED_RECORDING_LEVEL_RANGE_PERCENT 65U",
        "status_led_set_recording_level",
        "rec_level=%u",
        "uint8_t level = s_state.recording_level_percent",
        "uint8_t percent = level_percent > breath ? level_percent : breath",
        "status_led_token_locked(status_led_rec_gold(), percent, false)",
        "status_led_smoothstep_per_mille",
        "status_led_charging_breath_lerp_percent",
        "status_led_charging_breath_percent",
        "status_led_charging_breath_percent(now_ms)",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT",
        "s_state.external_power_present",
        "BOARD_PINS_USB_DET_IO",
        "bool external_power_present = usb_power_present",
        "s_state.charge_full_latched = true",
        "s_state.charge_full_latched = false",
        "raw_charging=%u raw_full=%u",
        "full_latched=%u full_candidate_ms=%",
        "external_power=%u charging=%u full=%u",
        "status_led_rgb(255, 255, 255), percent, false",
        "active_flags=PWR:%u,BLE:%u,REC:%u,AI:%u,OK:%u,WARN:%u,KEY:%u,EDGE:%u",
        "status_rgb=PWR:%u,%u,%u;BLE:%u,%u,%u;REC:%u,%u,%u",
        "if (changed) {\n            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;",
        "if (changed && state == STATUS_LED_BLE_CONNECTED && confidence_window)",
        "status_led_render_processing_locked",
        "status_led_render_edge_locked",
        "STATUS_LED_TX_MUTEX_WAIT_MS",
        "s_tx_mutex",
    ],
    "components/status_led/status_led_strip_backend.h": [
        "STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT 12U",
        "STATUS_LED_COLOR_ORDER_GRB",
        "STATUS_LED_COLOR_ORDER_RGB",
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
        "bit24=display_valid",
        "bits16_23=display_level",
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
        "status_led_notify_success(\"host_processing_done\")",
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
        "On battery, once confidence/status windows expire, connected BLE falls back to a sparse low-blue heartbeat",
        "External power overrides battery-color display on `PWR`",
        "continuous, natural white breath that reaches a real 1% valley",
        "steady white once charge-full has been debounced and latched",
        "User brightness scales the whole routine effect envelope before the profile cap is applied",
        "50% user brightness setting keeps the external-power breath's 1% valley near-off",
        "charging breath includes a short 1% valley hold, smooth inhale, and longer smooth exhale",
        "Recording is a gold voice-reactive semantic state",
        "exposes `rec_level`, and adds a visible VU brightness envelope",
        "speech makes `REC` visibly brighter",
        "desktop host starts streaming/ASR processing",
        "Firmware recording transfer does not animate `AI` by itself",
        "`VREC:PROCESSING:START`",
        "`VREC:PROCESSING:STOP`",
        "`VREC:PROCESSING:DONE`",
        "`DONE` turns `AI` off and flashes green `OK`",
        "audio transfer completion alone does not animate `AI`",
        "`OK` is a short success flash after the host reports processing done",
        "REC`, `OK`, and routine `AI` states do not recolor key LEDs",
        "Edge/frame LEDs are quiet in the standard product profile",
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
        "status_key_isolated_charge_natural_breath_v15",
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
        "STATUS_LED_CHARGING_BREATH_MIN_PERCENT 1U",
        "STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 300U",
        "STATUS_LED_CHARGING_BREATH_RISE_MS 1050U",
        "STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 80U",
        "STATUS_LED_CHARGING_BREATH_MAX_PERCENT 100U",
        "STATUS_LED_FULL_STEADY_PERCENT 100U",
        "STATUS_LED_FULL_STATUS_STEADY_PERCENT 100U",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: routine PWR peak must be user-capped, missing {token}")
    for token in (
        "STATUS_LED_RECORDING_LEVEL_STALE_MS",
        "STATUS_LED_RECORDING_LEVEL_BOOST_MULTIPLIER",
        "uint8_t level = s_state.recording_level_percent",
        "uint8_t percent = level_percent > breath ? level_percent : breath",
    ):
        if token not in status_led:
            failures.append(f"status_led.c: recording LED must visibly follow audio level, missing {token}")
    if "status_led_token_locked(status_led_rec_gold(), breath, false)" in status_led:
        failures.append("status_led.c: recording LED must not be fixed breath-only")
    voice_recording_control = read("components/voice_recording_control/voice_recording_control.c")
    if 'status_led_set_processing(true, "audio_session_finishing")' in voice_recording_control:
        failures.append("voice_recording_control.c: audio transfer must not light AI processing LED")
    if 'status_led_notify_success("recording_session_finished")' in voice_recording_control:
        failures.append("voice_recording_control.c: firmware transfer completion must not show OK before host completion")
    if "driver/rmt_" in status_led or "soc/soc_caps.h" in status_led:
        failures.append("status_led.c: business rendering layer must not include the RMT/WS2812 backend directly")
    if "status_led_set_max(&frame->key[0], status_led_scale_raw(rec" in status_led:
        failures.append("status_led.c: REC must not borrow KEY1; key LEDs are local white feedback only")
    if "status_led_set_max(&frame->key[index], ok)" in status_led:
        failures.append("status_led.c: OK success must not recolor key LEDs")
    if "status_led_rgb(160, 0, 255), 52U" in status_led:
        failures.append("status_led.c: routine AI processing must not recolor key LEDs purple")
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
        failures.append("status_led.c: REC must be voice-reactive, not a fixed 42-85 percent breath")
    if re.search(r"\.mem_block_symbols\s*=\s*64\b", status_led_backend):
        failures.append(
            "status_led_strip_backend.c: RMT mem_block_symbols=64 consumes two ESP32-S3 RMT blocks per strip and leaves fewer than four TX channels"
        )
    if not re.search(r"\.mem_block_symbols\s*=\s*SOC_RMT_MEM_WORDS_PER_CHANNEL\b", status_led_backend):
        failures.append(
            "status_led_strip_backend.c: RMT strip channels must use SOC_RMT_MEM_WORDS_PER_CHANNEL so all four V2 LED zones can initialize"
        )

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
        'status_led_notify_success("recording_session_finished")':
            "firmware transfer completion must not show OK before host PROCESSING:DONE",
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
