from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def require_fragment(path: Path, fragment: str, failures: list[str]) -> None:
    if fragment not in path.read_text(encoding="utf-8"):
        failures.append(f"{path.relative_to(REPO_ROOT)}: missing {fragment!r}")


def require_ordered(path: Path, earlier: str, later: str, failures: list[str]) -> None:
    source = path.read_text(encoding="utf-8")
    earlier_index = source.find(earlier)
    later_index = source.find(later)
    if earlier_index < 0 or later_index < 0:
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: missing ordered fragments "
            f"{earlier!r} or {later!r}"
        )
    elif earlier_index >= later_index:
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: expected {earlier!r} before {later!r}"
        )


def require_fast_recovery_worker_boundary(path: Path, failures: list[str]) -> None:
    source = path.read_text(encoding="utf-8")
    start = source.find("static esp_err_t ble_hid_gap_forget_bonds_and_repair_ec11_fast_inner(void)\n{")
    end = source.find("static esp_err_t ble_hid_gap_forget_bonds_and_repair_inner(", start)
    if start < 0 or end < 0:
        failures.append(f"{path.relative_to(REPO_ROOT)}: missing EC11 fast recovery boundary")
        return
    body = source[start:end]
    if "ble_store_util_bonded_peers(" in body:
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: EC11 fast recovery must defer NVS bond enumeration to the cleanup worker"
        )
    for fragment in (
        "BLE_HID_GAP_RECOVERY_BOND_COUNT_UNKNOWN",
        "ble_hid_gap_open_recovery_pairing_window(type_controlled_recovery, false);",
        "known_type_peer ? &type_peer : NULL",
        "ble_hid_gap_schedule_recovery_bond_delete(",
        "ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM)",
    ):
        if fragment not in body:
            failures.append(f"{path.relative_to(REPO_ROOT)}: fast recovery missing {fragment!r}")
    if body.find("ble_hid_gap_schedule_recovery_bond_delete(") >= body.find(
        "ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM)"
    ):
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: EC11 cleanup worker must be armed before GAP termination"
        )
    recovery_window_index = body.find(
        "ble_hid_gap_open_recovery_pairing_window(type_controlled_recovery, false);"
    )
    cleanup_worker_index = body.find("ble_hid_gap_schedule_recovery_bond_delete(")
    terminate_index = body.find(
        "ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM)"
    )
    if not (
        recovery_window_index >= 0
        and cleanup_worker_index >= 0
        and terminate_index >= 0
        and recovery_window_index < cleanup_worker_index < terminate_index
    ):
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: EC11 fast recovery must hold its window before cleanup and GAP termination"
        )


def require_retryable_security_error_classification(path: Path, failures: list[str]) -> None:
    source = path.read_text(encoding="utf-8")
    start = source.find(
        "static void ble_hid_gap_platform_device_control_note_retryable_security_failure(int status)"
    )
    end = source.find("\nstatic ", start + 1)
    if start < 0 or end < 0:
        failures.append(f"{path.relative_to(REPO_ROOT)}: missing retryable security classification boundary")
        return
    body = source[start:end]
    for fragment in (
        "status == BLE_ERR_UNK_CONN_ID ||\n               status == BLE_HS_HCI_ERR(BLE_ERR_UNK_CONN_ID)",
        "DENZIC_DEVICE_CONTROL_V1_ERROR_CATEGORY_TRANSPORT",
    ):
        if fragment not in body:
            failures.append(
                f"{path.relative_to(REPO_ROOT)}: missing transport classification for stale controller connection"
            )


def require_type_recovery_ack_boundary(path: Path, failures: list[str]) -> None:
    source = path.read_text(encoding="utf-8")
    start = source.find("esp_err_t ble_audio_stream_send_type_recovery_notice(void)\n{")
    end = source.find("esp_err_t ble_audio_stream_send_session_cancel(", start)
    if start < 0 or end < 0:
        failures.append(f"{path.relative_to(REPO_ROOT)}: missing Type recovery acknowledgement boundary")
        return
    body = source[start:end]
    for fragment in (
        "ble_audio_stream_arm_type_recovery_ack(&link);",
        "ble_gatts_notify_custom(link.conn_handle, s_notify_attr_handle, om);",
        "esp_err_t ble_audio_stream_wait_for_type_recovery_ack(uint32_t timeout_ms)",
        "type recovery acknowledgement timed out",
    ):
        if fragment not in body:
            failures.append(
                f"{path.relative_to(REPO_ROOT)}: Type recovery acknowledgement boundary missing {fragment!r}"
            )
    if body.find("ble_audio_stream_arm_type_recovery_ack(&link);") >= body.find(
        "ble_gatts_notify_custom(link.conn_handle, s_notify_attr_handle, om);"
    ):
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: recovery acknowledgement must be armed before its notice is sent"
        )


def require_precleanup_advertising_boundary(path: Path, failures: list[str]) -> None:
    source = path.read_text(encoding="utf-8")
    start = source.find("static void ble_hid_gap_recovery_bond_delete_task(void *arg)\n{")
    end = source.find("static esp_err_t ble_hid_gap_schedule_recovery_bond_delete(", start)
    if start < 0 or end < 0:
        failures.append(f"{path.relative_to(REPO_ROOT)}: missing recovery cleanup worker boundary")
        return
    body = source[start:end]
    for fragment in (
        "bool advertising_started_before_bond_delete = false;",
        "ble_hid_gap_set_recovery_advertising_while_bond_delete(true);",
        "ble_hid_gap_start_type_recovery_warmup_advertising();",
        "ble_hid_gap_set_recovery_advertising_while_bond_delete(false);",
        "ble_store_util_delete_peer(&type_peer)",
        "if (advertising_started_before_bond_delete && ble_gap_adv_active())",
        "Type-controlled connectable advertising started after local bond cleanup",
    ):
        if fragment not in body:
            failures.append(
                f"{path.relative_to(REPO_ROOT)}: pre-cleanup advertising boundary missing {fragment!r}"
            )
    if body.find("ble_hid_gap_start_type_recovery_warmup_advertising();") >= body.find(
        "ble_store_util_delete_peer(&type_peer)"
    ):
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: controller warm-up advertising must start before the synchronous peer cleanup"
        )
    for fragment in (
        "recovery: rejecting connection while async local bond delete is pending",
        "recovery: terminating encrypted connection while async local bond delete is pending",
        "Type-controlled recovery advertising starts while old peer cleanup quarantines connections",
        "Type-controlled recovery advertising reuses applied BLE identity",
        "type_recovery_requested && s_adv_device_name != NULL",
        "BLE_GAP_CONN_MODE_NON",
        "Type-controlled warm-up advertising started non-connectable until local bond cleanup completes",
    ):
        if fragment not in source:
            failures.append(
                f"{path.relative_to(REPO_ROOT)}: pre-cleanup advertising must retain peer quarantine {fragment!r}"
            )


def main() -> int:
    failures: list[str] = []
    gap = REPO_ROOT / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c"
    voice = REPO_ROOT / "components/voice_recording_control/voice_recording_control.c"
    audio = REPO_ROOT / "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c"
    platform_contract = (
        REPO_ROOT
        / "third_party/denzic-platform/device_control/protocol/device_control_v1.json"
    )
    key_input = REPO_ROOT / "ports/esp32/voice_key_input/voice_key_input_esp32.c"
    diag = REPO_ROOT / "ports/esp32/diag_log_platform/diag_log_flash.c"
    diag_events = REPO_ROOT / "components/diag_log/include/diag_log_events.h"

    for fragment in (
        "ble_hid_gap_forget_bonds_and_repair_ec11_fast",
        "ble_hid_gap_forget_bonds_and_repair_ec11_fast_inner",
        "BLE_HID_GAP_RECOVERY_BOND_COUNT_UNKNOWN",
        "#define BLE_HID_GAP_RECOVERY_BOND_DELETE_TASK_PRIO 5U",
        "DIAG_GAP_RECOVERY,\n        DIAG_SEV_INFO,\n        22,",
        "ble_hid_gap_defer_native_recovery_identity_rotation(\n            \"recovery_pairing_reset_connected_fast\")",
        "int delete_rc = denzic_ble_pairing_v1_orch_bond_delete_use_unpair_api(\n                                type_controlled_recovery)\n                ? ble_gap_unpair(&bonded_peers[index])\n                : ble_store_util_delete_peer(&bonded_peers[index]);",
        "recovery: Type-controlled bond records cleared without rotating the local IRK",
        "advertising_command_accepted_ms=%lld target_ms=250",
        "ble_hid_gap_begin_recovery_pairing_window(",
        "ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM)",
        "The worker owns bond deletion and the single recovery advertising start.",
        "The planned terminate owns old-session teardown; disconnect resets audio atomically.",
        "ble_hid_gap_notify_recovery_bond_delete_disconnect();\n    ESP_LOGI(TAG, \"disconnect;",
    ):
        require_fragment(gap, fragment, failures)
    require_fast_recovery_worker_boundary(gap, failures)
    require_retryable_security_error_classification(gap, failures)
    require_type_recovery_ack_boundary(audio, failures)
    require_precleanup_advertising_boundary(gap, failures)
    require_fragment(diag_events, "22=transaction_begin", failures)
    require_ordered(
        gap,
        "ble_hid_gap_start_type_recovery_warmup_advertising();",
        "ble_store_util_delete_peer(&type_peer)",
        failures,
    )
    require_ordered(
        gap,
        "ble_hid_gap_log_ec11_recovery_timing(\"advertising_command_accepted\", false);",
        "// Keep recovery diagnostics, but never put synchronous serial output before the",
        failures,
    )

    require_ordered(
        voice,
        "fast_recovery_ret = ble_hid_gap_forget_bonds_and_repair_ec11_fast();",
        "voice_recording_control_log_flow(\n        decision.flow_stage,",
        failures,
    )
    require_ordered(
        voice,
        "ble_audio_stream_send_type_recovery_notice();",
        "ble_audio_stream_wait_for_type_recovery_ack(",
        failures,
    )
    require_ordered(
        voice,
        "ble_audio_stream_wait_for_type_recovery_ack(",
        "fast_recovery_ret = ble_hid_gap_forget_bonds_and_repair_ec11_fast();",
        failures,
    )
    for fragment in (
        "#define BLE_AUDIO_STREAM_TYPE_RECOVERY_ACK_TEXT DENZIC_DEVICE_CONTROL_V1_EC11_RECOVERY_ACK",
        "static SemaphoreHandle_t s_type_recovery_ack_sem;",
        "ble_audio_stream_arm_type_recovery_ack(&link);",
        "ble_audio_stream_arm_type_recovery_notice_tx(&link);",
        "bool ble_audio_stream_consume_type_control_command(const char *command, const char *source)",
        "if (strcmp(command, BLE_AUDIO_STREAM_TYPE_RECOVERY_ACK_TEXT) == 0)",
        "ble_audio_stream_note_type_recovery_ack(source)",
        "esp_err_t ble_audio_stream_wait_for_type_recovery_ack(uint32_t timeout_ms)",
        "type recovery acknowledgement timed out",
        "ble_audio_stream_prepare_type_recovery_ack(void)",
        "#define BLE_AUDIO_STREAM_TYPE_RECOVERY_PREPARE_ACK_TEXT DENZIC_DEVICE_CONTROL_V1_EC11_RECOVERY_PREPARE_ACK",
        "type recovery pre-authorization consumed before EC11 pairing reset",
    ):
        require_fragment(audio, fragment, failures)
    for fragment in (
        '"notice": "listener-ec11-recovery-v1"',
        '"prepare_notice": "listener-ec11-recovery-prepare-v1"',
        '"ack": "TYPE:EC11:RECOVERY:ACK"',
        '"prepare_ack": "TYPE:EC11:RECOVERY:PREPARE:ACK"',
    ):
        require_fragment(platform_contract, fragment, failures)
    for fragment in (
        "if (button == &s_direct_gpio_state &&\n        !s_fast_idle_recording_hid_suppression_pending)",
        "ble_audio_stream_prepare_type_recovery_ack();",
        "a single click or long press never resets BLE",
    ):
        require_fragment(key_input, fragment, failures)
    for fragment in (
        "Type-controlled current peer records deleted directly without peer enumeration",
        "Type-controlled recovery advertising skips post-cleanup peer enumeration",
    ):
        require_fragment(gap, fragment, failures)

    for fragment in (
        "DIAG_LOG_WRITE_QUEUE_DEPTH",
        "xQueueCreate(DIAG_LOG_WRITE_QUEUE_DEPTH, sizeof(denzic_diag_log_event_t))",
        "diag_log_platform_writer_task",
        "xQueueSend(s_write_queue, &evt, 0)",
        "denzic_diag_log_event_t evt",
    ):
        require_fragment(diag, fragment, failures)
    require_ordered(
        diag,
        "denzic_diag_log_event_t evt",
        "void diag_log_platform_write(\n",
        failures,
    )

    if failures:
        print("FAIL: EC11 recovery critical-path static contract")
        print("\n".join(failures))
        return 1

    print("PASS: EC11 recovery critical-path static contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
