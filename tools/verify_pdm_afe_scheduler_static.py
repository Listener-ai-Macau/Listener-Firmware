from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c"


REQUIRED_FRAGMENTS = (
    "#define AUDIO_CAPTURE_TASK_CORE 1",
    "#define AUDIO_CAPTURE_AFE_FETCH_TASK_CORE 1",
    "#define AUDIO_CAPTURE_AFE_FETCH_TASK_PRIORITY 5",
    "s_pdm_afe_handle->fetch_with_delay(",
    "audio_capture_pdm_afe_fetch_task",
    "xTaskCreatePinnedToCore(",
    "config->ns_init = true;",
    "config->agc_init = false;",
    "config->afe_ringbuf_size = AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES;",
    "#define AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES 6",
)


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    failures = [
        f"missing PDM AFE scheduler contract: {fragment!r}"
        for fragment in REQUIRED_FRAGMENTS
        if fragment not in source
    ]

    if "#define AUDIO_CAPTURE_AFE_FETCH_TASK_CORE 0" in source.split("#else", 1)[-1]:
        failures.append("multicore fetch must not run on the NimBLE/serial core")

    if failures:
        print("FAIL: PDM AFE scheduler static contract")
        print("\n".join(failures))
        return 1

    print("PASS: PDM AFE fetch stays on the audio core and retains bounded buffering")
    return 0


if __name__ == "__main__":
    sys.exit(main())
