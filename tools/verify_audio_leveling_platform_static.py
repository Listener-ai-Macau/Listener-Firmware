from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c"
CAPTURE_CMAKE = ROOT / "ports/esp32/audio_capture/CMakeLists.txt"
ADAPTER_CMAKE = ROOT / "components/audio_leveling_platform/CMakeLists.txt"
PROTOCOL = ROOT / "third_party/denzic-platform/audio/protocol/audio_v1.json"


def main() -> int:
    source = CAPTURE.read_text(encoding="utf-8")
    capture_cmake = CAPTURE_CMAKE.read_text(encoding="utf-8")
    adapter_cmake = ADAPTER_CMAKE.read_text(encoding="utf-8")
    protocol = json.loads(PROTOCOL.read_text(encoding="utf-8"))
    failures: list[str] = []

    for token in (
        '#include "denzic_audio_leveling_v1.h"',
        "denzic_audio_leveling_v1_default_config()",
        "denzic_audio_leveling_v1_reset(",
        "denzic_audio_leveling_v1_step(",
        "denzic_audio_leveling_v1_stats_reset(",
        "denzic_audio_leveling_v1_stats_observe(",
        "denzic_audio_leveling_v1_stats_summarize(",
        "PDM audio leveling paired stats:",
        "voiced_post_agc_p10_p50_p90=",
        "noise_floor_mean_abs=",
        "gain_ceiling_frames=",
    ):
        if token not in source:
            failures.append(f"missing Listener leveling adapter token: {token}")

    if "audio_leveling_platform" not in capture_cmake:
        failures.append("audio_capture must require audio_leveling_platform")
    if "third_party/denzic-platform/audio/embedded/c/src/denzic_audio_leveling_v1.c" not in adapter_cmake:
        failures.append("adapter must compile the pinned platform leveling core")

    emit_start = source.find("static void audio_capture_pdm_agc_emit_frame(")
    emit_end = source.find("static void audio_capture_pdm_agc_process(", emit_start)
    emit = source[emit_start:emit_end]
    ordered = [
        "esp_agc_process(",
        "denzic_audio_leveling_v1_step(",
        "leveling_output.scale_permille",
        "audio_capture_pdm_apply_final_limiter(",
        "denzic_audio_leveling_v1_stats_observe(",
        "audio_capture_pdm_afe_emit(",
    ]
    positions = [emit.find(token) for token in ordered]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        failures.append(
            "required order is ESP AGC -> shared governor -> attenuation -> final limiter -> paired stats -> PCM emit"
        )

    fetch_start = source.find("static void audio_capture_pdm_afe_fetch_task(")
    fetch_end = source.find("static esp_err_t audio_capture_pdm_afe_init(", fetch_start)
    fetch = source[fetch_start:fetch_end]
    if not 0 <= fetch.find("audio_capture_pdm_vad_process(") < fetch.find(
        "audio_capture_pdm_agc_process("
    ):
        failures.append("authoritative VAD must remain ahead of AGC/governor")

    leveling = protocol.get("leveling", {})
    expected = {
        "maximum_effective_gain_permille": 24000,
        "maximum_noise_output_mean_abs": 64,
        "speech_hold_ms": 300,
        "attenuation_attack_ms": 80,
        "attenuation_release_ms": 40,
    }
    for key, value in expected.items():
        if leveling.get(key) != value:
            failures.append(
                f"platform leveling {key} must remain {value}, got {leveling.get(key)!r}"
            )

    if failures:
        print("FAIL: Listener shared audio-leveling adapter contract")
        print("\n".join(failures))
        return 1
    print(
        "PASS: shared governor follows VAD and ESP AGC, caps gain/noise with "
        "bounded hold/attack/release, preserves the final limiter, and emits "
        "paired voiced telemetry"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
