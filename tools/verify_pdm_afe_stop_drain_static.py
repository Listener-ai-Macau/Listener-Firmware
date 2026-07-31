from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c"


REQUIRED_FRAGMENTS = (
    "s_pdm_afe_stop_drain_requested = true;",
    "s_pdm_afe_stop_drain_feed_cutoff_ack = true;",
    "audio_capture_pdm_afe_complete_stop_drain();",
    "s_pdm_afe_stop_drain_padding_samples += padding_samples;",
    "audio_capture_pdm_afe_emit(silence, padding_samples);",
    "s_pdm_afe_stop_drain_complete = true;",
    "audio_capture_process_frame(boundary_frame);",
    "if (stop_boundary_requested && s_pdm_afe_stop_drain_requested &&",
    "!s_pdm_afe_stop_drain_complete)",
)


ORDERED_SEQUENCES = (
    (
        "s_pdm_afe_stop_drain_feed_cutoff_ack = true;\n        return;",
        "capture must acknowledge the feed cutoff before accepting post-stop input",
    ),
    (
        "audio_capture_pdm_afe_emit(silence, padding_samples);\n"
        "    }\n\n"
        "    s_pdm_afe_stop_drain_complete = true;\n"
        "    int16_t boundary_frame[AUDIO_CAPTURE_FRAME_SAMPLES] = {0};\n"
        "    audio_capture_process_frame(boundary_frame);",
        "partial enhanced PCM must be emitted before the stop boundary is processed",
    ),
)


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    failures: list[str] = []

    for fragment in REQUIRED_FRAGMENTS:
        if fragment not in source:
            failures.append(f"missing required stop-drain fragment: {fragment!r}")

    for sequence, message in ORDERED_SEQUENCES:
        if sequence not in source:
            failures.append(message)

    stop_start = source.find("esp_err_t audio_capture_session_stop_with_origin(")
    stop_end = source.find("esp_err_t audio_capture_session_cancel(", stop_start)
    stop_body = source[stop_start:stop_end]
    request_index = stop_body.find("s_pdm_afe_stop_drain_requested = true;")
    unlock_index = stop_body.find("xSemaphoreGive(s_state_mutex);", request_index)
    if not 0 <= request_index < unlock_index:
        failures.append(
            "session stop must request AFE drain before releasing the state lock"
        )

    if failures:
        print("FAIL: PDM AFE stop-drain static contract")
        print("\n".join(failures))
        return 1

    print("PASS: PDM AFE stop-drain preserves pre-stop enhanced PCM before session stop")
    return 0


if __name__ == "__main__":
    sys.exit(main())
