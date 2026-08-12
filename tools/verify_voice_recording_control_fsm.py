from __future__ import annotations

import pathlib
import re
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
VOICE_CONTROL = REPO_ROOT / "components" / "voice_recording_control" / "voice_recording_control.c"
CAPTURE_ADAPTER = (
    REPO_ROOT / "ports" / "esp32" / "audio_capture" / "audio_capture_esp32.c"
)
BLE_AUDIO_STREAM = (
    REPO_ROOT
    / "ports"
    / "esp32"
    / "ble_audio_stream"
    / "ble_audio_stream_esp32.c"
)
POWER_MANAGER = REPO_ROOT / "components" / "power_manager" / "power_manager.c"


EXPECTED_CASES = {
    "rapid_next_start_while_transferring": [
        "VOICE_RECORDING_STATE_TRANSFERRING",
        "VOICE_RECORDING_EVENT_TOGGLE",
        "VOICE_RECORDING_SOURCE_USER_START_INTENT",
        "VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START",
        "toggle_start_pending_transfer",
    ],
    "host_cleanup_toggle_after_abort": [
        "VOICE_RECORDING_STATE_IDLE",
        "VOICE_RECORDING_EVENT_TOGGLE",
        "VOICE_RECORDING_SOURCE_HOST_CONTROL",
        "VOICE_RECORDING_EFFECT_IGNORE",
        "host_cleanup_toggle_ignored_after_abort",
    ],
    "host_cleanup_clears_host_pending_start": [
        "VOICE_RECORDING_EFFECT_CLEAR_PENDING_START",
        "host_cleanup_toggle_cleared_pending_start",
    ],
    "host_cleanup_preserves_user_pending_start": [
        "VOICE_RECORDING_EFFECT_IGNORE",
        "host_cleanup_toggle_ignored_user_pending",
    ],
    "transport_not_ready_user_start": [
        "VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY",
        "VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START",
        "recording_waiting_for_ble_audio",
    ],
    "transport_not_ready_host_cleanup": [
        "VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY",
        "VOICE_RECORDING_EFFECT_IGNORE",
        "host_toggle_transport_not_ready_no_pending",
    ],
    "cancel_while_pending_start": [
        "VOICE_RECORDING_EVENT_CANCEL",
        "VOICE_RECORDING_EFFECT_CLEAR_PENDING_START",
        "recording_pending_start_canceled",
    ],
    "cancel_while_transferring": [
        "VOICE_RECORDING_STATE_TRANSFERRING",
        "VOICE_RECORDING_EVENT_CANCEL",
        "VOICE_RECORDING_EFFECT_CANCEL_RECORDING",
        "cancel_requested",
    ],
    "recovery_during_recording": [
        "VOICE_RECORDING_STATE_RECORDING",
        "VOICE_RECORDING_EVENT_RECOVERY",
        "VOICE_RECORDING_EFFECT_RECOVERY",
        "recovery_requested",
    ],
    "audio_session_finished_after_stop": [
        "VOICE_RECORDING_STATE_TRANSFERRING",
        "VOICE_RECORDING_EVENT_SESSION_INACTIVE",
        "VOICE_RECORDING_EFFECT_FINISH_TRANSFER",
        "recording_session_finished",
    ],
    "audio_session_aborted_without_active_capture": [
        "VOICE_RECORDING_STATE_RECORDING",
        "VOICE_RECORDING_EVENT_SESSION_INACTIVE",
        "VOICE_RECORDING_EFFECT_FINISH_ABORT",
        "session_aborted_without_stop",
    ],
    "stale_stop_command": [
        "VOICE_RECORDING_STATE_IDLE",
        "VOICE_RECORDING_EVENT_STOP",
        "VOICE_RECORDING_EFFECT_IGNORE",
        "stop_ignored_no_active_session",
    ],
    "stale_cancel_command": [
        "VOICE_RECORDING_STATE_IDLE",
        "VOICE_RECORDING_EVENT_CANCEL",
        "VOICE_RECORDING_EFFECT_IGNORE",
        "cancel_ignored_no_active_session",
    ],
}


DISALLOWED_DECISION_EFFECTS = [
    "audio_capture_session_",
    "ble_audio_stream_send_",
    "ble_hid_gap_request_",
    "ble_hid_gap_forget_",
    "power_manager_",
    "status_led_",
    "voice_key_input_set_recording_output",
    "diag_log(",
    "ESP_LOG",
]


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


def extract_function(source: str, name: str) -> str:
    marker = f"static voice_recording_control_decision_t {name}("
    start = source.find(marker)
    if start < 0:
        fail(f"missing {name}()")
    brace = source.find("{", start)
    if brace < 0:
        fail(f"missing body for {name}()")
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    fail(f"unterminated {name}()")
    return ""


def extract_transition_artifact(source: str) -> dict[str, str]:
    match = re.search(
        r"VOICE_RECORDING_CONTROL_FSM_ARTIFACT\[\]\s*=\s*\{(?P<body>.*?)\n\};",
        source,
        re.S,
    )
    if not match:
        fail("missing VOICE_RECORDING_CONTROL_FSM_ARTIFACT table")
    table_body = match.group("body")
    cases: dict[str, str] = {}
    for entry in re.finditer(r"\{\s*\"(?P<id>[^\"]+)\"(?P<body>.*?)\n\s*\},", table_body, re.S):
        cases[entry.group("id")] = entry.group(0)
    return cases


def require_contains(source: str, token: str, message: str) -> None:
    if token not in source:
        fail(message)


def extract_static_function(source: str, name: str) -> str:
    match = re.search(rf"static\s+\w+\s+{re.escape(name)}\(", source)
    start = match.start() if match else -1
    if start < 0:
        fail(f"missing {name}()")
    brace = source.find("{", start)
    if brace < 0:
        fail(f"missing body for {name}()")
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    fail(f"unterminated {name}()")
    return ""


def extract_void_function(source: str, name: str) -> str:
    return extract_static_function(source, name)


def verify_manual_pairing_handoff(source: str) -> None:
    recovery_body = extract_static_function(source, "voice_recording_control_recovery")
    manual_start = recovery_body.find("if (manual_pairing_visual)")
    if manual_start < 0:
        fail("manual pairing recovery branch is missing")
    return_start = recovery_body.find("return ESP_OK;", manual_start)
    if return_start < 0:
        fail("manual pairing recovery branch has no early return")
    snapshot_start = recovery_body.find("voice_recording_control_make_snapshot", manual_start)
    if snapshot_start >= 0 and snapshot_start < return_start:
        fail("manual pairing cue must return before the normal recovery snapshot")
    manual_branch = recovery_body[manual_start : return_start + len("return ESP_OK;")]
    for token in [
        '"manual_windows_unpair_wait"',
        "waiting for physical EC11 recovery reset",
    ]:
        if token not in manual_branch:
            fail(f"manual pairing branch is missing {token}")
    if "status_led_notify_ble_manual_pairing_for_ms" in manual_branch:
        fail(
            "manual pairing branch must not arm an extended low-brightness wait cue; "
            "owner direction 2026-07-21 reserves low-brightness blue for an established link"
        )
    if "ble_hid_gap_forget_bonds_and_repair" in manual_branch:
        fail("manual pairing cue must not reset bonds before physical EC11 recovery")
    require_contains(
        source,
        'return voice_recording_control_recovery(source, true, false, true, false);',
        "manual RECOVERY:TYPE:MANUAL command is not routed to the handoff cue",
    )
    for token in [
        "static esp_err_t voice_recording_control_recovery(",
        'strcmp(queued.command, "VREC:RECOVERY:TYPE:SILENT:FRESH") == 0',
        '"~VREC:RESULT command=%s result=%s\\n"',
        'command_ret == ESP_OK ? "OK" : esp_err_to_name(command_ret)',
    ]:
        require_contains(
            source,
            token,
            f"fresh-identity recovery execution acknowledgement is missing {token}",
        )


def main() -> int:
    source = VOICE_CONTROL.read_text(encoding="utf-8")
    capture_source = CAPTURE_ADAPTER.read_text(encoding="utf-8")
    ble_audio_source = BLE_AUDIO_STREAM.read_text(encoding="utf-8")
    power_source = POWER_MANAGER.read_text(encoding="utf-8")
    cases = extract_transition_artifact(source)

    for token in [
        "#define VOICE_RECORDING_CONTROL_DICTATION_SILENCE_STOP_MS 2000",
        "#define VOICE_RECORDING_CONTROL_DICTATION_TAIL_MS 0",
        "#define VOICE_RECORDING_CONTROL_HOST_SPEECH_PROTECTION_MS 2000",
        'strcmp(action, "SPEECH") == 0',
        "voice_recording_control_note_host_speech();",
        "voice_recording_control_host_speech_protection_active()",
        "if (xQueueSend(s_vad_queue, &event, 0) != pdTRUE)",
    ]:
        require_contains(
            source,
            token,
            f"target-speaker 2000ms auto-end contract is missing {token}",
        )
    if "VOICE_RECORDING_CONTROL_DICTATION_SILENCE_STOP_MS 1700" in source:
        fail("2s endpoint must not be split into a 1700ms threshold plus a tail")

    missing = sorted(set(EXPECTED_CASES) - set(cases))
    if missing:
        fail(f"transition artifact missing cases: {', '.join(missing)}")

    for case_id, tokens in EXPECTED_CASES.items():
        body = cases[case_id]
        for token in tokens:
            if token not in body:
                fail(f"{case_id} is missing {token}")

    for token in [
        "VOICE_RECORDING_STATE_IDLE",
        "VOICE_RECORDING_STATE_RECORDING",
        "VOICE_RECORDING_STATE_TRANSFERRING",
        "VOICE_RECORDING_STATE_RECOVERY",
        "VOICE_RECORDING_EVENT_TOGGLE",
        "VOICE_RECORDING_EVENT_STOP",
        "VOICE_RECORDING_EVENT_CANCEL",
        "VOICE_RECORDING_EVENT_RECOVERY",
        "VOICE_RECORDING_EVENT_PENDING_READY",
        "VOICE_RECORDING_EVENT_PENDING_TIMEOUT",
        "VOICE_RECORDING_EVENT_SESSION_INACTIVE",
        "VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY",
    ]:
        require_contains(source, token, f"FSM artifact/source missing {token}")

    activate_start = source.find(
        "static esp_err_t voice_recording_control_activate_automatic_session("
    )
    activate_end = source.find(
        "esp_err_t voice_recording_control_dispatch_control_command(",
        activate_start,
    )
    if activate_start < 0 or activate_end < 0:
        fail("automatic-session activation function is missing")
    activate_body = source[activate_start:activate_end]
    reset_at = activate_body.find(
        "denzic_voice_activation_v1_reset("
    )
    visible_at = activate_body.find(
        "s_active_session_visible = true;"
    )
    recording_led_at = activate_body.find(
        "status_led_set_recording(true, STATUS_LED_REC_SOURCE_DEVICE_MIC);"
    )
    if reset_at < 0:
        fail("accepted automatic session must reset endpointing counters")
    if visible_at < 0 or recording_led_at < 0:
        fail("automatic-session visible recording boundary is incomplete")
    if not reset_at < visible_at < recording_led_at:
        fail(
            "endpoint reset must happen before the accepted session becomes visible"
        )
    require_contains(
        activate_body,
        "automatic recording activated with fresh endpoint window",
        "automatic-session activation must log the fresh endpoint boundary",
    )

    enrollment_body = extract_static_function(
        source, "voice_recording_control_start_enrollment"
    )
    for token in [
        'strcmp(action, "ENROLL") == 0',
        "s_active_session_enrollment = true;",
        "s_enrollment_start_pending = true;",
        "owner enrollment queued behind hidden automatic candidate",
        "owner enrollment waiting for audio transport",
        "owner_enrollment_pending_start_timeout",
        "s_enrollment_start_pending ||",
        "voice_recording_control_try_start_pending_enrollment",
        "denzic_voice_activation_v1_reset(&s_voice_activation_machine);",
        "owner enrollment recording started; host owns bounded stop",
        "!s_active_session_enrollment &&",
    ]:
        require_contains(
            source
            if token.startswith('strcmp')
            or token.startswith('!')
            or token.startswith('voice_recording_control_try')
            or token == "owner_enrollment_pending_start_timeout"
            or token == "s_enrollment_start_pending ||"
            else enrollment_body,
            token,
            f"host-timed owner enrollment contract is missing {token}",
        )

    decision_body = extract_function(source, "voice_recording_control_decide_transition")
    for token in DISALLOWED_DECISION_EFFECTS:
        if token in decision_body:
            fail(f"transition decision contains effectful operation token: {token}")

    for event in [
        "VOICE_RECORDING_EVENT_TOGGLE",
        "VOICE_RECORDING_EVENT_STOP",
        "VOICE_RECORDING_EVENT_CANCEL",
        "VOICE_RECORDING_EVENT_RECOVERY",
        "VOICE_RECORDING_EVENT_PENDING_READY",
        "VOICE_RECORDING_EVENT_PENDING_TIMEOUT",
        "VOICE_RECORDING_EVENT_SESSION_INACTIVE",
        "VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY",
    ]:
        if not re.search(r"voice_recording_control_decide_transition\(\s*" + event, source):
            fail(f"runtime path does not consult transition decision for {event}")

    for token in [
        "voice_recording_control_source_is_user_start_intent",
        "voice_recording_control_source_is_host_control",
        "voiceflow event=%s source=%s result=%s state=%s pending=%u cancel_pending=%u session_count=%",
        "device_status state=%s detail=%s",
        "DIAG_VREC_FLOW",
        "fsm_artifact_cases=%u",
    ]:
        require_contains(source, token, f"missing diagnostic/source token: {token}")

    for boundary in [
        "voice_recording_control_enter_recording",
        "voice_recording_control_exit_recording",
        "voice_recording_control_cancel",
        "voice_recording_control_recovery",
        "voice_recording_control_handle_session_inactive",
    ]:
        require_contains(source, boundary, f"missing effect boundary {boundary}")

    for token in [
        "s_active_session_automatic = pre_roll_ms > 0u;",
        "s_active_session_visible = !s_active_session_automatic;",
        "voice_key_input_set_recording_output(s_active_session_visible)",
        "bool processing_feedback_allowed =",
        "!s_active_session_automatic || s_active_session_visible;",
        "if (processing_feedback_allowed) {",
        "hidden automatic candidate stopped without processing feedback",
        'strcmp(action, "ACTIVATE") == 0',
        '"automatic_candidate_accepted"',
        "!s_voice_auto_start_enabled &&",
        "s_active_session_automatic &&",
        "bool idle_start_monitoring =",
        "s_state == VOICE_RECORDING_STATE_IDLE &&",
        "ble_audio_stream_is_ready() &&",
        "bool active_stop_monitoring =",
        "s_state == VOICE_RECORDING_STATE_RECORDING &&",
        "(s_active_session_automatic || s_voice_auto_stop_enabled);",
        "(idle_start_monitoring || active_stop_monitoring);",
        'voice_recording_control_cancel(\n            "voice_activation.auto_start_disabled");',
    ]:
        require_contains(
            source,
            token,
            f"automatic session disable edge is missing {token}",
        )

    cleanup_start = source.find(
        "static void voice_recording_control_complete_transfer_cleanup("
    )
    cleanup_end = source.find(
        "static void voice_recording_control_stop(", cleanup_start
    )
    if cleanup_start < 0 or cleanup_end < 0:
        fail("transfer cleanup boundary is missing")
    cleanup_body = source[cleanup_start:cleanup_end]
    for token in [
        "const bool hidden_va =",
        "} else {\n        /* The audio drain is now complete",
        "denzic_voice_activation_v1_reset(&s_voice_activation_machine);",
        "s_state = VOICE_RECORDING_STATE_IDLE;",
        "s_cancel_pending = false;",
        "s_cancel_source = NULL;",
    ]:
        require_contains(
            cleanup_body,
            token,
            f"hidden candidate immediate re-arm contract is missing {token}",
        )

    refresh_start = source.find(
        "static void voice_recording_control_refresh_voice_monitoring("
    )
    refresh_end = source.find(
        "void voice_recording_control_on_power_state_changed(", refresh_start
    )
    if refresh_start < 0 or refresh_end < 0:
        fail("voice monitoring refresh boundary is missing")
    refresh_body = source[refresh_start:refresh_end]
    for token in [
        "ble_audio_stream_is_ready() &&",
        "power.state == POWER_MANAGER_STATE_ACTIVE &&",
        "(idle_start_monitoring || active_stop_monitoring);",
    ]:
        require_contains(
            refresh_body,
            token,
            f"non-idle voice monitoring contract is missing {token}",
        )
    if (
        "POWER_MANAGER_STATE_CONNECTED_IDLE" in refresh_body
        or "POWER_MANAGER_STATE_DISCONNECTED_IDLE" in refresh_body
    ):
        fail("Bluetooth-light-off idle must not keep voice monitoring enabled")

    idle_cancel_body = extract_void_function(
        source, "voice_recording_control_cancel_hidden_candidate_for_idle"
    )
    for token in [
        "power->state == POWER_MANAGER_STATE_ACTIVE",
        "s_active_session_automatic",
        "!s_active_session_visible",
        "audio_capture_session_cancel()",
        '"voice_activation.idle_suspend"',
        "voice_recording_control_handle_session_inactive()",
    ]:
        require_contains(
            idle_cancel_body,
            token,
            f"idle-racing hidden candidate cancellation is missing {token}",
        )
    if "power_manager_record_activity(" in idle_cancel_body:
        fail("idle-racing hidden candidate cancellation must not record activity")

    power_change_start = source.find(
        "static void voice_recording_control_apply_power_state_change("
    )
    power_change_end = source.find(
        "void voice_recording_control_on_power_state_changed(",
        power_change_start,
    )
    if power_change_start < 0 or power_change_end < 0:
        fail("power-state voice recording boundary is missing")
    power_change_body = source[power_change_start:power_change_end]
    require_contains(
        power_change_body,
        "voice_recording_control_cancel_hidden_candidate_for_idle(&power);",
        "power-state boundary must cancel an idle-racing hidden candidate",
    )

    for forbidden in [
        "s_state_mutex",
        "voice_recording_control_lock()",
        "xSemaphoreTake(",
        "portMAX_DELAY",
    ]:
        if forbidden in source:
            fail(
                "recording controller must remain a single-writer actor "
                f"without shared unbounded state locking: {forbidden}"
            )
    for token in [
        "VOICE_RECORDING_CONTROL_COMMAND_QUEUE_LENGTH",
        "voice_recording_control_command_t",
        "xQueueSend(s_command_queue, &queued, 0)",
        "xQueueReceive(s_command_queue, &queued, 0)",
        "VOICE_RECORDING_CONTROL_COMMANDS_PER_TICK",
        "__atomic_store_n(",
        "&s_power_state_refresh_pending",
        "__atomic_load_n(&s_session_count, __ATOMIC_RELAXED)",
        "voice_recording_control_handle_control_command(",
        "voice_recording_control_store_source(",
        "s_pending_start_source_storage",
        "s_active_session_source_storage",
        "s_cancel_source_storage",
    ]:
        require_contains(
            source,
            token,
            f"single-writer recording event contract is missing {token}",
        )

    fast_idle_start = power_source.find(
        "static void power_manager_apply_fast_idle_actions("
    )
    fast_idle_end = power_source.find(
        "static void power_manager_guard_runtime_power_hold_low(", fast_idle_start
    )
    if fast_idle_start < 0 or fast_idle_end < 0:
        fail("power-manager fast idle boundary is missing")
    fast_idle_body = power_source[fast_idle_start:fast_idle_end]
    for token in [
        "if (state != POWER_MANAGER_STATE_ACTIVE)",
        "power_manager_audio_idle_blockers(blockers) == 0",
        "power_manager_set_audio_idle_power_save(true);",
    ]:
        require_contains(
            fast_idle_body,
            token,
            f"idle audio power-save retry is missing {token}",
        )

    for token in [
        "#define AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM 1",
        "#define AUDIO_CAPTURE_VOICE_PREROLL_MAX_MS 1000U",
        "config->vad_init = true;",
        'config->vad_model_name = vad_model_name;',
        'esp_srmodel_filter(s_pdm_srmodels, "vadnet1_medium", NULL);',
        "srmodel_load(",
        "config->agc_init = false;",
        "s_pdm_agc_handle = esp_agc_open(AGC_MODE_2, AUDIO_CAPTURE_SAMPLE_RATE_HZ);",
        "#define AUDIO_CAPTURE_PDM_AGC_COMPRESSION_DB 48",
        "#define AUDIO_CAPTURE_PDM_AGC_TARGET_DBFS 6",
        "#define AUDIO_CAPTURE_PDM_LIMITER_CEILING 23170",
        "#define AUDIO_CAPTURE_PDM_VAD_LOW_SNR_NUMERATOR 3U",
        "#define AUDIO_CAPTURE_PDM_VAD_LOW_SNR_DENOMINATOR 2U",
        "#define AUDIO_CAPTURE_PDM_VAD_LOW_SNR_MIN_MARGIN 3U",
        "AUDIO_CAPTURE_PDM_AGC_COMPRESSION_DB,\n        1,\n        AUDIO_CAPTURE_PDM_AGC_TARGET_DBFS);",
        '" requested_preroll_ms=%" PRIu32 " actual_preroll_ms=%u"',
        '" input_clipped_samples=%" PRIu64',
        '" pre_vad_peak=%" PRIu32 " pre_vad_mean_abs=%" PRIu32',
        '" agc_input_mean_abs=%" PRIu32 " agc_output_mean_abs=%" PRIu32',
        '" effective_gain_permille=%" PRIu32',
        '" output_clipped_samples=%" PRIu64',
        '" limiter_max_reduction_permille=%" PRIu32',
        '" vad_noise_floor_mean_abs=%" PRIu32',
        '" vad_low_snr_speech_frames=%" PRIu32',
        "if (!session_active && state != VAD_SPEECH)",
        "PDM VAD low-SNR speech preserved:",
        "handler(speech_detected, elapsed_ms);",
    ]:
        require_contains(
            capture_source,
            token,
            f"1.0.4 root audio-level contract is missing {token}",
        )
    if "AUDIO_CAPTURE_VAD_SPEECH_MIN_PEAK" in capture_source:
        fail("fixed raw-volume VAD gate is forbidden by the 1.0.4 wake contract")
    for forbidden in ("vad_create(", "s_pdm_vad_handle"):
        if forbidden in capture_source:
            fail(f"legacy standalone WebRTC VAD must be absent: {forbidden}")
    if re.search(r"(?<!pdm_)vad_process\(", capture_source):
        fail("legacy standalone WebRTC VAD must be absent: vad_process(")
    process_start = capture_source.find(
        "static void audio_capture_pdm_afe_process("
    )
    process_end = capture_source.find("#endif", process_start)
    process_body = capture_source[process_start:process_end]
    raw_index = process_body.find("audio_capture_note_pdm_afe_session_input(")
    pre_vad_index = process_body.find("audio_capture_note_pdm_afe_session_pre_vad(")
    feed_index = process_body.find("s_pdm_afe_handle->feed(")
    if not 0 <= raw_index < pre_vad_index < feed_index:
        fail("raw and unboosted pre-VAD telemetry must precede AFE feed")

    fetch_start = capture_source.find(
        "static void audio_capture_pdm_afe_fetch_task("
    )
    fetch_end = capture_source.find(
        "static esp_err_t audio_capture_pdm_afe_init(", fetch_start
    )
    fetch_body = capture_source[fetch_start:fetch_end]
    vad_index = fetch_body.find("audio_capture_pdm_vad_process(")
    agc_index = fetch_body.find("audio_capture_pdm_agc_process(")
    if not 0 <= vad_index < agc_index:
        fail("post-NS VAD must classify before the one adaptive AGC")
    agc_emit_start = capture_source.find(
        "static void audio_capture_pdm_agc_emit_frame("
    )
    agc_emit_end = capture_source.find(
        "static void audio_capture_pdm_agc_process(", agc_emit_start
    )
    agc_emit_body = capture_source[agc_emit_start:agc_emit_end]
    limiter_index = agc_emit_body.find("audio_capture_pdm_apply_final_limiter(")
    emit_index = agc_emit_body.find("audio_capture_pdm_afe_emit(")
    if not 0 <= limiter_index < emit_index:
        fail("final limiter must run after AGC and before PCM emit")

    stop_start = source.find(
        "static esp_err_t voice_recording_control_exit_recording_with_origin"
    )
    stop_end = source.find(
        "static esp_err_t voice_recording_control_exit_recording(",
        stop_start,
    )
    stop_body = source[stop_start:stop_end]
    gate_index = stop_body.find("bool processing_feedback_allowed =")
    visibility_clear_index = stop_body.find("s_active_session_visible = false;")
    processing_index = stop_body.find(
        'status_led_set_processing(true, "recording_stop_processing_start")'
    )
    if not 0 <= gate_index < visibility_clear_index < processing_index:
        fail(
            "stop path must capture automatic-candidate visibility before clearing it "
            "and before starting processing feedback"
        )

    verify_manual_pairing_handoff(source)

    require_contains(
        ble_audio_source,
        """ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP,
        session_id,
        expected_packet_count,
        0,
        1,
        NULL,
        0,
        stop_origin,
        0);""",
        "SESSION_STOP must encode stop_origin in packet_pcm_bytes, not payload_len",
    )
    for token in [
        "audio_capture_send_session_preroll(\n"
        "                            session_id,\n"
        "                            s_session_preroll_count,\n"
        "                            &pre_roll_packet_count);",
        "s_export_state.stream_next_packet_sequence =\n"
        "                                    pre_roll_packet_count;",
        '" frames=%u packets=%u ms=%u"',
    ]:
        require_contains(
            capture_source,
            token,
            "pre-roll sequence must advance from packets counted after the "
            "lossless session negotiation",
        )

    print(
        "PASS: voice_recording_control FSM artifact covers "
        f"{len(EXPECTED_CASES)} extreme transition cases and keeps decisions effect-free."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
