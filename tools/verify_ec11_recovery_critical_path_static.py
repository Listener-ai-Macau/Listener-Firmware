from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def require_fragment(path: Path, fragment: str, failures: list[str]) -> None:
    if fragment not in path.read_text(encoding="utf-8"):
        failures.append(f"{path.relative_to(REPO_ROOT)}: missing {fragment!r}")


def require_ordered(path: Path, earlier: str, later: str, failures: list[str]) -> None:
    source = path.read_text(encoding="utf-8")
    if source.find(earlier) >= source.find(later):
        failures.append(
            f"{path.relative_to(REPO_ROOT)}: expected {earlier!r} before {later!r}"
        )


def main() -> int:
    failures: list[str] = []
    gap = REPO_ROOT / "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c"
    voice = REPO_ROOT / "components/voice_recording_control/voice_recording_control.c"
    diag = REPO_ROOT / "ports/esp32/diag_log_platform/diag_log_flash.c"

    for fragment in (
        "ble_hid_gap_forget_bonds_and_repair_ec11_fast",
        "ec11_fast_path && type_controlled_recovery",
        "ble_hid_gap_begin_recovery_pairing_window(",
        "ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM)",
        "The worker owns bond deletion and the single recovery advertising start.",
        "The planned terminate owns old-session teardown; disconnect resets audio atomically.",
        "ble_hid_gap_notify_recovery_bond_delete_disconnect();\n    ESP_LOGI(TAG, \"disconnect;",
    ):
        require_fragment(gap, fragment, failures)

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
