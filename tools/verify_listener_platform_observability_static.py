#!/usr/bin/env python3
"""Verify the bounded Firmware adapter for Denzic observability v1."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, token: str, label: str, failures: list[str]) -> None:
    if token not in text:
        failures.append(f"{label}: missing {token!r}")


def require_ordered(text: str, before: str, after: str, label: str, failures: list[str]) -> None:
    before_index = text.find(before)
    after_index = text.find(after, before_index + len(before)) if before_index >= 0 else -1
    if before_index < 0 or after_index < 0:
        failures.append(f"{label}: expected {before!r} before {after!r}")


def main() -> int:
    failures: list[str] = []
    generated = read("third_party/denzic-platform/observability/embedded/c/include/denzic_observability_v1_generated.h")
    component_cmake = read("components/diag_log/CMakeLists.txt")
    diag_header = read("components/diag_log/include/diag_log.h")
    diag_events = read("components/diag_log/include/diag_log_events.h")
    diag = read("components/diag_log/diag_log.c")
    recording = read("components/voice_recording_control/voice_recording_control.c")
    ota = read("components/firmware_ota/firmware_ota.c")
    ble_audio = read("ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c")

    for token in (
        'DENZIC_OBSERVABILITY_V1_CONTRACT_NAME "denzic_observability_v1"',
        "DENZIC_OBSERVABILITY_V1_CONTRACT_VERSION (1u)",
        "DENZIC_OBSERVABILITY_V1_CAPABILITY_BLE",
        "DENZIC_OBSERVABILITY_V1_CAPABILITY_AUDIO",
        "DENZIC_OBSERVABILITY_V1_CAPABILITY_OTA",
        "DENZIC_OBSERVABILITY_V1_TIMING_METRIC_EDGE_TO_RECORD_DISPATCH_MS",
        "DENZIC_OBSERVABILITY_V1_TIMING_METRIC_BLE_RECOVERY_MS",
        "DENZIC_OBSERVABILITY_V1_TIMING_METRIC_OTA_TRANSFER_MS",
    ):
        require(generated, token, "generated observability contract", failures)

    require(diag_header, "typedef struct {", "diag_log wire definition", failures)
    require(diag_header, "diag_log_event_wire_t", "diag_log wire definition", failures)
    require(diag_header, "sizeof(diag_log_event_wire_t) == DIAG_LOG_EVENT_WIRE_BYTES", "diag_log wire definition", failures)
    require(diag_events, "DIAG_VREC_TIMING", "recording timing event", failures)
    require(diag_events, "DIAG_OTA_CORRELATION", "OTA correlation event", failures)
    require(
        component_cmake,
        "third_party/denzic-platform/observability/embedded/c/include",
        "diag_log component include path",
        failures,
    )

    for token in (
        '#include "denzic_observability_v1_generated.h"',
        "DIAG_PLATFORM_EXPORT_MAX_EVENTS 128U",
        "diag_log_dump_platform_last",
        '"PLATFORM:LAST:"',
        r'\"contract_version\"',
        r'\"correlation_id\"',
        r'\"event_sequence\"',
        r'\"timing_metric\"',
        "diag_platform_assign_correlation",
        "DIAG_GAP_RECOVERY",
        "DIAG_OTA_SET_BOOT",
        "DIAG_VREC_TIMING",
    ):
        require(diag, token, "Firmware Platform export", failures)

    fast_start = recording.find("static void voice_recording_control_handle_fast_idle_ec11_start")
    fast_end = recording.find("static void voice_recording_control_host_processing_start", fast_start)
    fast_handler = recording[fast_start:fast_end] if fast_start >= 0 and fast_end >= 0 else ""
    require_ordered(
        fast_handler,
        'voice_recording_control_toggle("ec11.fast_idle")',
        "DIAG_VREC_TIMING",
        "EC11 timing observation",
        failures,
    )
    require_ordered(
        fast_handler,
        "EC11 fast Idle recording dispatch",
        "DIAG_VREC_TIMING",
        "EC11 timing observation",
        failures,
    )
    require(
        fast_handler,
        "s_state == VOICE_RECORDING_STATE_RECORDING && !s_pending_start ? s_session_count : 0U",
        "EC11 timing correlation",
        failures,
    )
    require_ordered(
        fast_handler,
        "recording_session_id",
        "DIAG_VREC_TIMING",
        "EC11 timing correlation",
        failures,
    )

    for token in (
        'BLE_AUDIO_STREAM_TYPE_OBSERVABILITY_OTA_PREFIX "TYPE:OBS:OTA:"',
        "BLE_AUDIO_STREAM_TYPE_OBSERVABILITY_CORRELATION_HEX_BYTES 16U",
        "ble_audio_stream_consume_ota_observability_context",
        "firmware_ota_set_observability_correlation(correlation_id)",
    ):
        require(ble_audio, token, "OTA context adapter", failures)

    for token in (
        "firmware_ota_set_observability_correlation",
        "pending_observability_correlation_id",
        "firmware_ota_log_observability_correlation",
        "DIAG_OTA_CORRELATION",
    ):
        require(ota, token, "OTA correlation persistence", failures)
    require_ordered(
        ota,
        "firmware_ota_log_observability_correlation(correlation_id)",
        "firmware_ota_blocker_t blocker = firmware_ota_get_blocker();",
        "OTA correlation persistence ordering",
        failures,
    )

    if failures:
        print("FAIL: Listener Platform observability static validation")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("PASS: Listener Platform v1 Firmware adapter preserves 24-byte diagnostics and exports bounded BLE/audio/OTA correlation and timing envelopes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
