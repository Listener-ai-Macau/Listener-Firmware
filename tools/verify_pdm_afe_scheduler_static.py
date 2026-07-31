from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = REPO_ROOT / "ports/esp32/audio_capture/audio_capture_esp32.c"
SDKCONFIG_DEFAULTS_PATH = REPO_ROOT / "sdkconfig.defaults"


REQUIRED_FRAGMENTS = (
    "#define AUDIO_CAPTURE_AFE_FETCH_TASK_PRIORITY 5",
    "s_pdm_afe_handle->fetch_with_delay(",
    "audio_capture_pdm_afe_fetch_task",
    "xTaskCreatePinnedToCore(",
    "config->ns_init = true;",
    "config->vad_init = true;",
    'config->vad_model_name = vad_model_name;',
    "config->vad_mode = VAD_MODE_1;",
    'esp_srmodel_filter(s_pdm_srmodels, "vadnet1_medium", NULL);',
    "srmodel_load(",
    "config->agc_init = false;",
    "s_pdm_agc_handle = esp_agc_open(AGC_MODE_2, AUDIO_CAPTURE_SAMPLE_RATE_HZ);",
    "audio_capture_pdm_agc_process(",
    "audio_capture_pdm_vad_process(",
    "audio_capture_pdm_apply_final_limiter(",
    "#define AUDIO_CAPTURE_PDM_LIMITER_CEILING 23170",
    "#define AUDIO_CAPTURE_PDM_VAD_LOW_SNR_NUMERATOR 3U",
    "#define AUDIO_CAPTURE_PDM_VAD_LOW_SNR_DENOMINATOR 2U",
    "#define AUDIO_CAPTURE_PDM_VAD_LOW_SNR_MIN_MARGIN 3U",
    "PDM VAD low-SNR speech preserved:",
    "config->afe_ringbuf_size = AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES;",
    "#define AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES 24",
    "#define AUDIO_CAPTURE_PDM_AFE_IDLE_BUDGET_FETCHES 64U",
    "#define AUDIO_CAPTURE_I2S_READ_TIMEOUT_MS 250U",
    "#define AUDIO_CAPTURE_TASK_CORE 0",
    "#define AUDIO_CAPTURE_AFE_FETCH_TASK_CORE 1",
    "#define AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM 1",
    "config->afe_linear_gain = 1.0f;",
)


def main() -> int:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    sdkconfig_defaults = SDKCONFIG_DEFAULTS_PATH.read_text(encoding="utf-8")
    failures = [
        f"missing PDM AFE scheduler contract: {fragment!r}"
        for fragment in REQUIRED_FRAGMENTS
        if fragment not in source
    ]

    multicore_core_contract = source[
        source.find("#else", source.find("#if CONFIG_FREERTOS_UNICORE")):
        source.find("#endif", source.find("#else", source.find("#if CONFIG_FREERTOS_UNICORE")))
    ]
    if (
        "#define AUDIO_CAPTURE_TASK_CORE 0" not in multicore_core_contract
        or "#define AUDIO_CAPTURE_AFE_FETCH_TASK_CORE 1" not in multicore_core_contract
    ):
        failures.append(
            "multicore ESP-SR feed and fetch/DSP tasks must stay on separate cores"
        )

    process_body = source[
        source.find("static void audio_capture_pdm_afe_process("):
        source.find("#endif", source.find("static void audio_capture_pdm_afe_process("))
    ]
    raw_index = process_body.find("audio_capture_note_pdm_afe_session_input(")
    pre_vad_index = process_body.find("audio_capture_note_pdm_afe_session_pre_vad(")
    feed_index = process_body.find("s_pdm_afe_handle->feed(")
    if not 0 <= raw_index < pre_vad_index < feed_index:
        failures.append(
            "raw telemetry -> unboosted pre-VAD telemetry -> AFE feed order is broken"
        )

    fetch_body = source[
        source.find("static void audio_capture_pdm_afe_fetch_task("):
        source.find("static esp_err_t audio_capture_pdm_afe_init(")
    ]
    vad_index = fetch_body.find("audio_capture_pdm_vad_process(")
    agc_index = fetch_body.find("audio_capture_pdm_agc_process(")
    if not 0 <= vad_index < agc_index:
        failures.append("post-NS VAD must classify before the one adaptive AGC")
    idle_budget_count_index = fetch_body.find(
        "fetches_since_idle_budget >=\n"
        "                AUDIO_CAPTURE_PDM_AFE_IDLE_BUDGET_FETCHES"
    )
    idle_budget_delay_index = fetch_body.find(
        "vTaskDelay(1);", idle_budget_count_index
    )
    if not 0 <= agc_index < idle_budget_count_index < idle_budget_delay_index:
        failures.append(
            "continuous VADNet fetch must reserve one idle-task tick at the bounded cadence"
        )
    low_snr_branch = source[
        source.find("if (mean_abs < diagnostic_threshold) {"):
        source.find("/* VADNet is the candidate authority.", vad_index)
    ]
    if "PDM VAD low-SNR speech preserved:" not in low_snr_branch:
        failures.append("low-SNR telemetry must stay inside the low-SNR branch")

    agc_emit_body = source[
        source.find("static void audio_capture_pdm_agc_emit_frame("):
        source.find("static void audio_capture_pdm_agc_process(")
    ]
    limiter_index = agc_emit_body.find("audio_capture_pdm_apply_final_limiter(")
    emit_index = agc_emit_body.find("audio_capture_pdm_afe_emit(")
    if not 0 <= limiter_index < emit_index:
        failures.append("final limiter must run after AGC and before PCM emit")

    for forbidden in ("vad_create(", "s_pdm_vad_handle"):
        if forbidden in source:
            failures.append(
                f"legacy standalone WebRTC VAD must be absent: {forbidden}"
            )
    if re.search(r"(?<!pdm_)vad_process\(", source):
        failures.append("legacy standalone WebRTC VAD must be absent: vad_process(")

    pdm_section = source.find("/* ========== SPH0655 PDM hardware path ========== */")
    capture_start = source.find("static void audio_capture_task(void *arg)", pdm_section)
    capture_end = source.find("static esp_err_t audio_capture_i2s_init(void)", capture_start)
    capture_body = source[capture_start:capture_end]
    if "i2s_channel_read(" not in capture_body:
        failures.append("PDM capture task must read from the configured I2S RX channel")
    if "portMAX_DELAY" in capture_body:
        failures.append("watchdog-subscribed PDM capture must not use an unbounded wait")
    for fragment in (
        "&bytes_read, AUDIO_CAPTURE_I2S_READ_TIMEOUT_MS)",
        "if (ret == ESP_ERR_TIMEOUT) {",
        "i2s_channel_disable(s_i2s_rx_handle)",
        "i2s_channel_enable(s_i2s_rx_handle)",
        "PDM I2S stall recovery:",
    ):
        if fragment not in capture_body:
            failures.append(f"missing bounded PDM I2S recovery contract: {fragment!r}")

    for fragment in (
        "CONFIG_ESP_TASK_WDT_TIMEOUT_S=5",
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y",
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y",
    ):
        if fragment not in sdkconfig_defaults:
            failures.append(
                f"Task WDT must remain strict instead of masking audio starvation: {fragment}"
            )

    if failures:
        print("FAIL: PDM AFE scheduler static contract")
        print("\n".join(failures))
        return 1

    print(
        "PASS: PDM AFE retains bounded buffering, scheduler idle budget, "
        "and in-place recovery from bounded I2S stalls"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
