"""Run the production ownership-marker queue and consumer against audio backlog.

Only RTOS queue/notify edges are simulated. --source accepts the saved old C,
including its original worker case, to preserve the failing causal comparison.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_power_source_filter import ROOT
from test_wake_capture_ownership import function


def verify(source):
    names = ('ble_audio_stream_job_type_t', 'ble_audio_stream_transport_state_t')
    types = '\n'.join(re.search(r'typedef enum \{[^}]*\} ' + name + ';', source).group()
                      for name in names)
    types += '\n' + re.search(r'typedef struct \{[^}]*\} ble_audio_stream_job_t;', source).group()
    modern = 'static void ble_audio_stream_send_session_start_marker_internal(' in source
    if modern:
        consumer = function(source, 'ble_audio_stream_send_session_start_marker_internal')
    else:
        start = source.index('case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START_MARKER:')
        end = source.index('case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA:', start)
        consumer = ('static void ble_audio_stream_send_session_start_marker_internal('
                    'const ble_audio_stream_job_t *pending) { '
                    'ble_audio_stream_job_t job = *pending; do {\n'
                    + source[start:end].split(':', 1)[1] + '\n} while (0); }')
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
typedef int esp_err_t;
typedef int BaseType_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define TAG "test"
static void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define LISTENER_AUDIO_PACKET_TYPE_SESSION_START 1
''' + types + r'''
typedef struct { uint32_t connection_epoch; } ble_audio_stream_link_snapshot_t;
static bool s_started;
static void *s_export_queue;
static uint32_t s_transport_session_id, s_connection_epoch;
static uint32_t s_export_started_session_id, s_export_started_connection_epoch;
static int s_link_state_lock;
static ble_audio_stream_transport_state_t s_transport_state;
static ble_audio_stream_job_t queue[64];
static unsigned queued, notified, audio_count, elapsed, first_marker_ms, failures;
static bool backpressure;
static esp_err_t notify_result;
static ble_audio_stream_link_snapshot_t ble_audio_stream_get_link_snapshot(void) {
    return (ble_audio_stream_link_snapshot_t){s_connection_epoch};
}
static const char *esp_err_to_name(esp_err_t e) { (void)e; return "stub"; }
static void ble_audio_stream_set_backpressure_active(bool value) { backpressure=value; }
static esp_err_t ble_audio_stream_send_control_packet_repeated(int type, uint32_t id,
        uint16_t seq, uint16_t origin) {
    (void)type; (void)id; (void)seq;
    if (notify_result != ESP_OK) return notify_result;
    if (origin & 0x8000) { if (!notified) first_marker_ms=elapsed; notified++; }
    return ESP_OK;
}
static int xQueueSend(void *q, const ble_audio_stream_job_t *job, unsigned wait) {
    (void)q; (void)wait;
    if (queued==64) return 0;
    queue[queued++]=*job; return pdTRUE;
}
static int xQueueSendToFront(void *q, const ble_audio_stream_job_t *job, unsigned wait) {
    (void)q; (void)wait;
    if (queued==64) return 0;
    memmove(queue+1,queue,queued*sizeof(*queue)); queue[0]=*job; queued++; return pdTRUE;
}
''' + function(source, 'ble_audio_stream_transport_session_active') + '\n' + function(
        source, 'ble_audio_stream_send_session_start_internal') + '\n' + consumer + '\n' + function(
        source, 'ble_audio_stream_send_session_start_marker') + r'''
static void check(bool value, const char *name) {
    if (!value) { fprintf(stderr,"FAIL: %s\n",name); failures++; }
}
static void reset(void) {
    s_started=true; s_export_queue=queue; queued=notified=audio_count=elapsed=0;
    first_marker_ms=UINT32_MAX; s_transport_session_id=42; s_connection_epoch=3;
    s_export_started_session_id=42; s_export_started_connection_epoch=3;
    s_transport_state=BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING;
    backpressure=false; notify_result=ESP_OK;
}
static void add_audio(unsigned count) {
    for (unsigned i=0;i<count;i++) {
        ble_audio_stream_job_t job={.type=BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA,
                                    .session_id=42,.packet_sequence=(uint16_t)i};
        assert(xQueueSend(queue,&job,0));
    }
}
static void drain(void) {
    while (queued) {
        ble_audio_stream_job_t job=queue[0]; queued--;
        memmove(queue,queue+1,queued*sizeof(*queue));
        if (job.type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START_MARKER)
            ble_audio_stream_send_session_start_marker_internal(&job);
        else if (job.type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START)
            (void)ble_audio_stream_send_session_start_internal(job.session_id,job.start_origin);
        else if (job.type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA) {
            assert(job.packet_sequence==audio_count++); elapsed+=60;
        } else if (job.type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP) {
            s_transport_state=BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED;
            s_transport_session_id=0;
        }
    }
}
int main(void) {
    reset(); add_audio(53);
    assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    drain(); printf("53 queued PCM jobs: ownership receipt at %u ms, audio=%u\n",first_marker_ms,audio_count);
    check(notified==1 && first_marker_ms==0 && audio_count==53,"receipt cannot wait behind 3180ms PCM backlog");

    reset(); s_export_started_session_id=0;
    ble_audio_stream_job_t start={.type=BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,.session_id=42};
    assert(xQueueSend(queue,&start,0)); add_audio(2);
    assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    check(queue[0].type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,"receipt cannot overtake original START");
    drain(); check(notified==1 && audio_count==2,"unexported START retains ordered fallback");

    reset(); add_audio(3); assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    ble_audio_stream_job_t stop={.type=BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP,.session_id=42};
    assert(xQueueSend(queue,&stop,0)); s_transport_state=BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING;
    drain(); check(notified==1 && audio_count==3,"queued STOP does not revoke an accepted ownership receipt");

    reset(); assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    s_connection_epoch++; drain(); check(notified==0,"old connection receipt cannot confirm a new epoch");
    reset(); assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    s_transport_session_id=43; drain(); check(notified==0,"receipt cannot bind another capture");
    reset(); assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    s_transport_state=BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR; drain();
    check(notified==0,"failed capture cannot be confirmed");
    reset(); s_export_started_connection_epoch=2;
    assert(xQueueSend(queue,&start,0));
    assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    check(queue[0].type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,"previous-link START is not priority proof");
    drain(); check(notified==1,"fresh ordered start still delivers receipt");
    reset(); add_audio(64);
    check(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_ERR_TIMEOUT,
          "full queue cannot report ownership receipt accepted");
    check(queued==64,"failed marker does not discard PCM");
    reset(); backpressure=true;
    assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK); drain();
    check(backpressure,"ownership acknowledgement must not clear PCM capacity pressure");
    reset(); s_export_started_session_id=0; s_export_started_connection_epoch=0;
    notify_result=ESP_ERR_TIMEOUT;
    assert(ble_audio_stream_send_session_start_internal(42,0)==ESP_ERR_TIMEOUT);
    check(s_export_started_session_id==0 && s_export_started_connection_epoch==0,
          "a failed real START must not publish export proof");
    assert(xQueueSend(queue,&start,0));
    assert(ble_audio_stream_send_session_start_marker(42,0x8123)==ESP_OK);
    check(queue[0].type==BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,
          "failed START cannot authorize a priority ownership receipt");
    drain(); check(notified==0,"failed notification is not an ownership confirmation");
    /* Keep the saved old source build warning-free without modifying it. */
    (void)esp_err_to_name(0); (void)s_link_state_lock; (void)xQueueSendToFront;
    (void)ble_audio_stream_get_link_snapshot();
    printf("ownership marker failures=%u\n",failures); return failures?1:0;
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-marker-queue-', ignore_cleanup_errors=True) as directory:
        tmp = Path(directory)
        (tmp/'test.c').write_text(code, encoding='utf-8')
        executable = tmp/('test.exe' if os.name=='nt' else 'test')
        if os.name=='nt':
            vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs = subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
            vcvars = Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            (tmp/'build.cmd').write_text(
                f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
                'cl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n', encoding='utf-8')
            subprocess.run(['cmd.exe','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
        else:
            subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Werror',
                            'test.c','-o',str(executable)],cwd=tmp,check=True)
        subprocess.run([str(executable)],cwd=tmp,check=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c')
    verify(parser.parse_args().source.read_text(encoding='utf-8'))
