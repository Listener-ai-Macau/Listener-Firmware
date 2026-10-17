"""Execute the production controller/FSM across hidden-window rotations.

Transport readiness and the VAD queue are platform stubs. The controller's
rotation, rearm latch, configuration and portable state machine are unchanged
source extracts. --source tests the preserved pre-repair controller.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_power_source_filter import ROOT, function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    source = (args.source or ROOT / 'components/voice_recording_control/voice_recording_control.c').read_text(encoding='utf-8')
    platform = ROOT / 'third_party/denzic-platform/voice_activation/embedded/c'
    constants = '\n'.join(re.findall(
        r'^#define VOICE_RECORDING_CONTROL_(?:AUTOMATIC_WAKE_MAX_SESSION_MS|INITIAL_BODY_WAIT_MS|AUTO_REARM_[A-Z_]+)[^\n]+',
        source, re.M))
    config = '\n'.join(re.findall(r'^    s_voice_activation_config\.(?:speech_confirm_ms|pre_roll_ms|cooldown_ms) = [^\n]+', source, re.M))
    event_type = re.search(r'typedef struct \{[^}]*bool speech_detected;[^}]*\} voice_recording_control_vad_event_t;', source).group()
    pending_owner = re.search(r'typedef enum \{[^}]*VOICE_RECORDING_PENDING_OWNER_NONE[^}]*\} voice_recording_pending_owner_t;', source)
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include "denzic_voice_activation_v1.h"
#include "voice_recording_endpoint_policy.h"
typedef int esp_err_t;
typedef uint32_t TickType_t;
enum { ESP_OK=0, ESP_ERR_INVALID_STATE=1 };
enum { VOICE_RECORDING_STATE_IDLE=0, VOICE_RECORDING_STATE_RECORDING=1,
       VOICE_RECORDING_STATE_TRANSFERRING=2 };
enum { AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION=1,
       AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION_MAX_DURATION=2 };
#define TAG "rotation-test"
#define pdTRUE 1
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
#define __ATOMIC_RELAXED 0
#define __atomic_load_n(address, order) (*(address))
static void log_stub(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
'''
    code += constants + '\n' + event_type + '\n' + (pending_owner.group() if pending_owner else '') + r'''
static int s_state;
static bool s_voice_monitoring=true, s_voice_auto_start_enabled=true, s_voice_auto_stop_enabled=true;
static bool s_active_session_automatic, s_active_session_visible, s_active_session_enrollment;
static bool s_active_session_body_speech_seen, s_active_session_host_endpoint_owned;
static bool s_pending_start, s_enrollment_start_pending, s_cancel_pending;
static bool s_voice_auto_rearm_await_quiet_edge, s_voice_auto_rearm_suppression_logged;
static uint32_t s_voice_auto_rearm_episode_id, s_voice_auto_rearm_terminal_session_count;
static uint32_t s_session_count, s_voice_auto_rearm_latch_vad_events, s_voice_auto_rearm_probe_count;
static uint32_t s_vad_event_total, s_vad_queue_drop_count;
static TickType_t s_voice_auto_rearm_latch_set_tick, stub_tick;
static denzic_voice_activation_v1_machine_t s_voice_activation_machine;
static denzic_voice_activation_v1_config_t s_voice_activation_config;
static void *s_vad_queue=(void *)1;
static voice_recording_control_vad_event_t next_event;
static bool event_ready, stub_transport_ready=true, speech_protected;
static unsigned starts, stops;
static uint32_t last_start_at, last_stop_at, selected_preroll;
static esp_err_t stop_result=ESP_OK;
static TickType_t xTaskGetTickCount(void) { return stub_tick; }
static int xQueueReceive(void *queue, voice_recording_control_vad_event_t *event, int wait) {
    (void)queue; (void)wait;
    if (!event_ready) return 0;
    *event=next_event; event_ready=false; return pdTRUE;
}
static bool ble_audio_stream_is_ready(void) { return stub_transport_ready; }
static bool voice_recording_control_host_speech_protection_active(void) { return speech_protected; }
static esp_err_t voice_recording_control_enter_recording(const char *source, bool log, bool reconnect, uint32_t preroll) {
    (void)source; (void)log; (void)reconnect;
    if (!stub_transport_ready) return ESP_ERR_INVALID_STATE;
    starts++; s_session_count++; last_start_at=stub_tick; selected_preroll=preroll;
    s_state=VOICE_RECORDING_STATE_RECORDING; s_active_session_automatic=true;
    s_active_session_visible=false; return ESP_OK;
}
static esp_err_t voice_recording_control_exit_recording_with_origin(const char *source, int origin) {
    (void)source; (void)origin;
    stops++; last_stop_at=stub_tick;
    if (stop_result == ESP_OK) s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    return stop_result;
}
static void voice_recording_control_schedule_pending_start(const char *source, const char *reason, const char *detail, ...) {
    (void)source; (void)reason; (void)detail; s_pending_start=true;
}
'''
    for name in ('voice_recording_control_set_auto_rearm_latch',
                 'voice_recording_control_clear_auto_rearm_latch',
                 'voice_recording_control_note_auto_rearm_vad_event',
                 'voice_recording_control_process_voice_activity'):
        code += function(source, name) + '\n'
    code += r'''
static void reset_case(void) {
    stub_tick=0; s_state=VOICE_RECORDING_STATE_IDLE; starts=stops=0;
    s_active_session_automatic=s_active_session_visible=s_active_session_enrollment=false;
    s_active_session_body_speech_seen=s_active_session_host_endpoint_owned=false;
    s_pending_start=s_enrollment_start_pending=s_cancel_pending=false;
    s_voice_auto_rearm_await_quiet_edge=s_voice_auto_rearm_suppression_logged=false;
    s_voice_auto_rearm_probe_count=0; s_vad_event_total=0;
    s_voice_monitoring=s_voice_auto_start_enabled=s_voice_auto_stop_enabled=true;
    stop_result=ESP_OK; stub_transport_ready=true; speech_protected=false;
    s_voice_activation_config=denzic_voice_activation_v1_default_config();
'''
    code += config + r'''
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
}
static void tick(bool speech) {
    stub_tick+=20u; s_vad_event_total++;
    next_event=(voice_recording_control_vad_event_t){.speech_detected=speech,.elapsed_ms=20u};
    event_ready=true; voice_recording_control_process_voice_activity();
}
static void drain_complete(void) {
    /* Same reset as production hidden transfer cleanup; transport drain alone
     * does not own the controller's rearm decision. */
    s_state=VOICE_RECORDING_STATE_IDLE;
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
}
int main(void) {
    /* A cue beginning 500ms before rotation and ending 300ms after it must
     * remain wholly covered by the next real pre-roll. Continuous foreign
     * speech never supplies a quiet edge. Repeat enough to exercise backoff. */
    reset_case();
    for (unsigned rotation=0; rotation<8; rotation++) {
        while (s_state != VOICE_RECORDING_STATE_RECORDING) tick(true);
        const unsigned start_count=starts;
        const uint32_t start_at=last_start_at;
        while (s_state == VOICE_RECORDING_STATE_RECORDING) tick(true);
        const uint32_t boundary=last_stop_at;
        assert(boundary-start_at == VOICE_RECORDING_CONTROL_AUTOMATIC_WAKE_MAX_SESSION_MS);
        for (unsigned drain=0; drain<10; drain++) { tick(true); assert(starts==start_count); }
        drain_complete();
        while (starts==start_count) { tick(true); assert(stub_tick-boundary < 12000u); }
        assert(selected_preroll==s_voice_activation_config.pre_roll_ms);
        if (last_start_at-selected_preroll > boundary-500u) {
            fprintf(stderr,"FAIL: hidden rotation lost cue prefix: rotation=%u boundary=%u next=%u preroll=%u\n",
                    rotation,boundary,last_start_at,selected_preroll);
            return 2;
        }
    }
    /* A VAD quiet edge still clears a failure-recovery latch. */
    reset_case(); voice_recording_control_set_auto_rearm_latch("stop_failed");
    tick(false); assert(!s_voice_auto_rearm_await_quiet_edge); assert(starts==0);
    for (unsigned i=0; i<100; i++) tick(false);
    assert(starts==0);
    /* Stop failure remains failure; no fresh physical session can start. */
    reset_case(); while (starts==0) tick(true); stop_result=ESP_ERR_INVALID_STATE;
    for (unsigned i=0; i<200; i++) tick(true);
    assert(s_state==VOICE_RECORDING_STATE_RECORDING);
    assert(s_voice_auto_rearm_await_quiet_edge); assert(starts==1);
    /* Drain/unavailable transport and explicit control owners still block. */
    reset_case(); stub_transport_ready=false;
    for (unsigned i=0; i<100; i++) tick(true);
    assert(starts==0); assert(s_pending_start);
    reset_case(); s_enrollment_start_pending=true;
    for (unsigned i=0; i<100; i++) tick(true);
    assert(starts==0);
    reset_case(); s_cancel_pending=true;
    for (unsigned i=0; i<100; i++) tick(true);
    assert(starts==0);
    reset_case(); s_voice_monitoring=false;
    for (unsigned i=0; i<100; i++) tick(true);
    assert(starts==0);
    /* The visible host-owned body continues to own its existing STOP. */
    reset_case(); s_state=VOICE_RECORDING_STATE_RECORDING;
    s_active_session_visible=true; s_active_session_host_endpoint_owned=true;
    for (unsigned i=0; i<6000; i++) tick(false);
    assert(stops==0);
    puts("PASS: continuous-noise cue coverage across eight bounded rotations; quiet, stop failure, transport, enrollment, cancel, monitoring and host-owned body");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-wake-coverage-') as directory:
        tmp = Path(directory)
        (tmp / 'test.c').write_text(code, encoding='utf-8')
        includes = str(platform / 'include')
        policy = str(ROOT / 'components/voice_recording_control/include')
        implementation = str(platform / 'src/denzic_voice_activation_v1.c')
        executable = tmp / ('test.exe' if os.name == 'nt' else 'test')
        if os.name == 'nt':
            vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
            vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
            vcvars = Path(vs) / 'VC/Auxiliary/Build/vcvars64.bat'
            (tmp / 'build.cmd').write_text(
                f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
                f'cl /nologo /std:c11 /W4 /WX /I"{includes}" /I"{policy}" test.c "{implementation}" /Fe:test.exe\n', encoding='utf-8')
            subprocess.run(['cmd.exe', '/d', '/c', str(tmp / 'build.cmd')], cwd=tmp, check=True)
        else:
            subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-Wall', '-Werror',
                            '-I' + includes, '-I' + policy, 'test.c', implementation, '-o', str(executable)], cwd=tmp, check=True)
        result = subprocess.run([str(executable)], check=False)
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
