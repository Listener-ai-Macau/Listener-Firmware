"""Compile the production pacing adapter; verify rate and bounded IDLE yields."""
import os
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_power_source_filter import ROOT, function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c')
    source = parser.parse_args().source.read_text(encoding='utf-8')
    platform = ROOT / 'third_party/denzic-platform/audio/embedded/c'
    constants = '\n'.join(re.findall(
        r'^#define BLE_AUDIO_STREAM_(?:PREROLL_IDLE_BLOCK_PCM_BYTES|COOPERATIVE_IDLE_BLOCK_MS|AUDIO_PACKET_IDLE_BLOCK_MS)[^\n]+', source, re.M))
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "denzic_audio_transport_v1.h"
typedef uint32_t TickType_t;
typedef uint32_t listener_audio_packet_type_t;
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10u))
#define LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA 1u
static TaskHandle_t s_export_task_handle = (TaskHandle_t)1;
static TaskHandle_t current_task = (TaskHandle_t)1;
static uint32_t s_transport_session_id = 7u;
static uint32_t connection_epoch = 1u;
static int transition_while_blocked;
typedef struct { uint32_t connection_epoch; } test_link_t;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
static test_link_t ble_audio_stream_get_link_snapshot(void) {
    return (test_link_t){ connection_epoch };
}
static denzic_audio_transport_v1_pacing_t s_audio_pacing;
static uint32_t s_audio_pacing_cooperative_credit_ticks;
static uint32_t s_audio_pacing_burst_remaining_pcm_bytes;
static struct { uint32_t audio_preroll_burst_pcm_bytes; } s_session_stats;
static uint32_t waited, since_yield, max_between_yields;
enum { BLE_AUDIO_WAIT_MEDIA, BLE_AUDIO_WAIT_TRANSPORT, BLE_AUDIO_WAIT_PREROLL };
static int64_t esp_timer_get_time(void) { return (int64_t)waited * 10000; }
static void ble_audio_stream_stats_block_wait(uint32_t session, int kind, int64_t began) {
    (void)session; (void)kind; (void)began;
}
static void vTaskDelay(TickType_t ticks) {
    if (ticks > 0u) {
        waited += ticks;
        if (since_yield > max_between_yields) max_between_yields = since_yield;
        since_yield = 0;
        if (transition_while_blocked == 1) s_transport_session_id++;
        if (transition_while_blocked == 2) connection_epoch++;
    }
}
static void watchdog_platform_feed_current_task(void) {}
static void ble_audio_stream_stats_audio_pace_wait(uint32_t session, uint32_t ticks) {
    (void)session; (void)ticks;
}
/* This normal-media fixture never enters preroll; its actual precise blocker
 * and saved preroll packet sequence are covered by test_audio_cooperative_wait. */
static void ble_audio_cooperative_wait_ms(uint32_t ms) { vTaskDelay((ms + 9u) / 10u); }
'''
    code += constants + '\n' + function(source, 'ble_audio_stream_delay_ms')
    code += '\n' + function(source, 'ble_audio_stream_notify_success_delay')
    code += r'''
static void run(const uint16_t *sizes, uint32_t count) {
    denzic_audio_transport_v1_pacing_init(&s_audio_pacing);
    s_audio_pacing_cooperative_credit_ticks = 0;
    waited = since_yield = max_between_yields = 0;
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < 20000u; i++) {
        uint16_t size = sizes[i % count];
        bytes += size;
        since_yield += size;
        ble_audio_stream_notify_success_delay(1u, 7u, size);
        /* No missed media budget and at most one early cooperative tick. */
        assert(waited >= bytes / 384u);
        assert(waited <= bytes / 384u + 1u);
        /* Small packets cannot starve IDLE0 between credited waits. */
        assert(since_yield <= 2u * 384u);
    }
    assert(max_between_yields <= 2u * 384u + 480u);
    printf("PASS: %u bytes, %u pacing ticks, max %u PCM bytes between yields\n",
        bytes, waited, max_between_yields);
}
static void reset(void) {
    denzic_audio_transport_v1_pacing_init(&s_audio_pacing);
    s_audio_pacing_cooperative_credit_ticks = 0;
    s_audio_pacing_burst_remaining_pcm_bytes = 0;
    s_transport_session_id = 7u;
    connection_epoch = 1u;
    current_task = s_export_task_handle;
    waited = since_yield = max_between_yields = 0;
    transition_while_blocked = 0;
}
static void pressure_wait_already_supplies_this_media_quantum(void) {
    reset();
    /* A 10ms admission retry is sufficient for 384 bytes at 38.4kB/s.
     * The old success adapter sleeps another 10ms and halves that bound.
     * This executes both real firmware functions; only RTOS edges are fake. */
    for (unsigned i = 0; i < 1000; ++i) {
        since_yield += 384u;
        ble_audio_stream_delay_ms(1u);
        ble_audio_stream_notify_success_delay(1u, 7u, 384u);
        if (waited != i + 1u) {
            fprintf(stderr, "FAIL: media quantum waited twice; packet=%u actual_ticks=%u expected_ticks=%u\n",
                i, waited, i + 1u);
            exit(4);
        }
    }
    assert(max_between_yields <= 384u);
    puts("PASS: actual retry/job blocker is not charged again by the media clock");

    reset();
    ble_audio_stream_delay_ms(2000u);
    /* A long suspension supplies one bounded allowance, not 200 future ticks. */
    ble_audio_stream_notify_success_delay(1u, 7u, 384u);
    ble_audio_stream_notify_success_delay(1u, 7u, 384u);
    assert(waited == 201u);

    reset();
    s_audio_pacing_burst_remaining_pcm_bytes = 64000u;
    ble_audio_stream_delay_ms(1u);
    assert(s_audio_pacing_cooperative_credit_ticks == 0u);
    reset();
    current_task = (TaskHandle_t)2;
    ble_audio_stream_delay_ms(1u);
    assert(s_audio_pacing_cooperative_credit_ticks == 0u);
    for (int edge = 1; edge <= 2; ++edge) {
        reset(); transition_while_blocked = edge;
        ble_audio_stream_delay_ms(1u);
        assert(s_audio_pacing_cooperative_credit_ticks == 0u);
    }
    reset(); s_transport_session_id = 0u;
    ble_audio_stream_delay_ms(1u);
    assert(s_audio_pacing_cooperative_credit_ticks == 0u);
    reset(); ble_audio_stream_delay_ms(0u);
    assert(waited == 0u && s_audio_pacing_cooperative_credit_ticks == 0u);
    puts("PASS: preroll, foreign worker, source/link transition and zero wait stay isolated");
    reset();
}
int main(void) {
    (void)ble_audio_stream_delay_ms;
    (void)xTaskGetCurrentTaskHandle;
    (void)ble_audio_stream_get_link_snapshot;
    (void)esp_timer_get_time;
    (void)ble_audio_stream_stats_block_wait;
    pressure_wait_already_supplies_this_media_quantum();
    /* Full ATT packets, smallest tails, and the installed-session mixture. */
    for (uint16_t size = 32u; size <= 480u; size += 16u) run(&size, 1u);
    const uint16_t mixed[] = {288u, 160u, 352u, 480u, 128u, 64u, 224u};
    run(mixed, 7u);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-pacing-test-') as directory:
        tmp = Path(directory)
        (tmp / 'test.c').write_text(code, encoding='utf-8')
        implementation = platform / 'src/denzic_audio_transport_v1.c'
        include = platform / 'include'
        if os.name == 'nt':
            vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
            vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
            vcvars = Path(vs) / 'VC/Auxiliary/Build/vcvars64.bat'
            (tmp / 'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX /I"{include}" test.c "{implementation}" /Fe:test.exe\n', encoding='utf-8')
            subprocess.run(['cmd.exe', '/d', '/c', str(tmp / 'build.cmd')], cwd=tmp, check=True)
            executable = tmp / 'test.exe'
        else:
            executable = tmp / 'test'
            subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-Wall', '-Werror', '-I'+str(include), 'test.c', str(implementation), '-o', str(executable)], cwd=tmp, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
