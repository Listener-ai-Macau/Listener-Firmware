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


def extract_void_function(source: str, name: str) -> str:
    marker = f"static void {name}("
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


def verify_manual_pairing_handoff(source: str) -> None:
    recovery_body = extract_void_function(source, "voice_recording_control_recovery")
    manual_start = recovery_body.find("if (manual_pairing_visual)")
    if manual_start < 0:
        fail("manual pairing recovery branch is missing")
    return_start = recovery_body.find("return;", manual_start)
    if return_start < 0:
        fail("manual pairing recovery branch has no early return")
    snapshot_start = recovery_body.find("voice_recording_control_make_snapshot", manual_start)
    if snapshot_start >= 0 and snapshot_start < return_start:
        fail("manual pairing cue must return before the normal recovery snapshot")
    manual_branch = recovery_body[manual_start : return_start + len("return;")]
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
        'voice_recording_control_recovery(source, true, false, true);',
        "manual RECOVERY:TYPE:MANUAL command is not routed to the handoff cue",
    )


def main() -> int:
    source = VOICE_CONTROL.read_text(encoding="utf-8")
    capture_source = CAPTURE_ADAPTER.read_text(encoding="utf-8")
    ble_audio_source = BLE_AUDIO_STREAM.read_text(encoding="utf-8")
    cases = extract_transition_artifact(source)

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
