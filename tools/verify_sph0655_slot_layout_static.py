from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c"


REQUIRED_FRAGMENTS = (
    "static const int s_pdm_selected_slot = 0;",
    "static const int s_pdm_active_buffer_index = 1;",
    "static const int s_pdm_selected_slot = 1;",
    "static const int s_pdm_active_buffer_index = 0;",
    "mono[i] = interleaved[(i * 2U) + (size_t)s_pdm_active_buffer_index];",
)


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    failures = [
        f"missing SPH0655 PDM slot-layout contract: {fragment!r}"
        for fragment in REQUIRED_FRAGMENTS
        if fragment not in source
    ]
    if "s_pdm_active_slot" in source:
        failures.append("legacy physical-slot-as-buffer-index mapping remains")

    if failures:
        print("FAIL: SPH0655 PDM stereo slot layout")
        print("\n".join(failures))
        return 1

    print("PASS: SPH0655 physical slot selection maps to ESP-IDF stereo buffer order")
    return 0


if __name__ == "__main__":
    sys.exit(main())
