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
        "ble_hid_gap_begin_recovery_pairing_window(type_controlled_recovery, false);",
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
    if "ble_hid_gap_open_recovery_pairing_window(" in body:
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: EC11 fast recovery must defer recovery UI/blocker activation until advertising is accepted"
        )


def main() -> int:
    failures: list[str] = []
    gap = REPO_ROOT / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c"
    voice = REPO_ROOT / "components/voice_recording_control/voice_recording_control.c"
    diag = REPO_ROOT / "ports/esp32/diag_log_platform/diag_log_flash.c"
    diag_events = REPO_ROOT / "components/diag_log/include/diag_log_events.h"

    for fragment in (
        "ble_hid_gap_forget_bonds_and_repair_ec11_fast",
        "ble_hid_gap_forget_bonds_and_repair_ec11_fast_inner",
        "BLE_HID_GAP_RECOVERY_BOND_COUNT_UNKNOWN",
        "#define BLE_HID_GAP_RECOVERY_BOND_DELETE_TASK_PRIO 5U",
        "DIAG_GAP_RECOVERY,\n        DIAG_SEV_INFO,\n        22,",
        "ble_hid_gap_defer_native_recovery_identity_rotation(\n            \"recovery_pairing_reset_connected_fast\")",
        "int delete_rc = type_controlled_recovery\n                ? ble_store_util_delete_peer(&bonded_peers[index])\n                : ble_gap_unpair(&bonded_peers[index]);",
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
    require_fragment(diag_events, "22=transaction_begin", failures)
    require_ordered(
        gap,
        "rc = ble_gap_adv_start(s_own_addr_type, NULL, adv_duration_ms,\n                           &adv_params, nimble_hid_gap_event, NULL);",
        "ble_hid_gap_log_ec11_recovery_timing(\"advertising_command_accepted\", false);",
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
        "fast_recovery_ret = ble_hid_gap_forget_bonds_and_repair_ec11_fast();",
        failures,
    )

    for fragment in (
        "DIAG_LOG_WRITE_QUEUE_DEPTH",
        "xQueueCreate(DIAG_LOG_WRITE_QUEUE_DEPTH, sizeof(diag_event_t))",
        "diag_log_platform_writer_task",
        "xQueueSend(s_write_queue, &evt, 0)",
        "static void diag_log_platform_write_now(const diag_event_t *evt)",
    ):
        require_fragment(diag, fragment, failures)
    require_ordered(
        diag,
        "static void diag_log_platform_write_now(const diag_event_t *evt)",
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
