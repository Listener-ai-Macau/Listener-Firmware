from __future__ import annotations

import pathlib
import re
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
VOICE_CONTROL = REPO_ROOT / "components" / "voice_recording_control" / "voice_recording_control.c"


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


def main() -> int:
    source = VOICE_CONTROL.read_text(encoding="utf-8")
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

    print(
        "PASS: voice_recording_control FSM artifact covers "
        f"{len(EXPECTED_CASES)} extreme transition cases and keeps decisions effect-free."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
