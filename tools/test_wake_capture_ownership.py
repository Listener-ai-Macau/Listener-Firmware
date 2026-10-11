"""Run the production ENSURE/adoption functions against an instrumented capture.

The audio/BLE edges are stubs; transaction decisions and endpoint configuration
are extracted from the firmware C. --source can replay a saved pre-fix source.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_power_source_filter import ROOT


def function(source, name):
    match = re.search(r'^[\w *]+\b' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('Missing production function: ' + name)
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def verify_source(source):
    enum = re.search(
        r'typedef enum \{[^}]*VOICE_RECORDING_ENSURE_STATUS_NONE[^}]*\}'
        r' voice_recording_ensure_status_t;', source).group()
    owner_pattern = r'typedef enum \{[^}]*VOICE_RECORDING_PENDING_OWNER_NONE[^}]*\} voice_recording_pending_owner_t;'
    owner = re.search(owner_pattern, source)
    has_pending_owner = owner is not None
    owner = owner or re.search(owner_pattern, (ROOT/'components/voice_recording_control/voice_recording_control.c').read_text(encoding='utf-8'))
    types = '\n'.join(re.search(r'typedef enum \{[^}]*\} ' + name + ';', source).group()
                      for name in ('voice_recording_state_t', 'voice_recording_flow_stage_t',
                                   'voice_recording_control_event_t', 'voice_recording_control_source_class_t',
                                   'voice_recording_control_effect_t'))
    types += '\n' + '\n'.join(re.search(r'typedef struct \{[^}]*\} ' + name + ';', source).group()
                               for name in ('voice_recording_control_snapshot_t', 'voice_recording_control_decision_t'))
    constants = '\n'.join(re.findall(
        r'^#define VOICE_RECORDING_CONTROL_(?:ENSURE_[A-Z_]+|'
        r'PENDING_START_[A-Z_]+|STORED_SOURCE_BYTES|AUTOMATIC_WAKE_MAX_SESSION_MS|INITIAL_BODY_WAIT_MS)[^\n]+', source, re.M))
    protocol = (ROOT/'protocols/listener_proto/include/listener_audio_proto.h').read_text(encoding='utf-8')
    constants += '\n' + '\n'.join(re.findall(
        r'^#define LISTENER_AUDIO_SESSION_START_ORIGIN_ENSURE_[^\n]+', protocol, re.M))
    process = function(source, 'voice_recording_control_process_voice_activity')
    policy = process[process.index('const bool hidden_automatic ='):
                     process.index('denzic_voice_activation_v1_input_t input =')]
    enter = function(source, 'voice_recording_control_enter_recording')
    initial = enter[enter.index('s_active_session_automatic = pre_roll_ms > 0u;'):
                    enter.index('voice_recording_control_clear_host_speech_protection();')]
    # Only the hardware edges are simulated; the real initializer still owns
    # the new capture's visibility, body evidence and endpoint lease.
    code = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "denzic_voice_activation_v1.h"
#include "voice_recording_endpoint_policy.h"
typedef int esp_err_t;
typedef uint32_t TickType_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2, ESP_ERR_TIMEOUT=3,
       ESP_ERR_NOT_FOUND=4 };
enum { STATUS_LED_REC_SOURCE_NONE=0, STATUS_LED_REC_SOURCE_DEVICE_MIC=1,
       STATUS_LED_REC_SOURCE_NOT_AVAILABLE=2, STATUS_LED_ERROR_DOMAIN_REC=1,
       STATUS_LED_ERROR_RETRYABLE=1, POWER_MANAGER_BLOCKER_RECORDING=1,
       POWER_MANAGER_BLOCKER_BLE_AUDIO=2, DIAG_SRC_VOICE_REC=1,
       DIAG_VREC_REJECTED=1, DIAG_SEV_WARN=1 };
#define TAG "capture-test"
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
static void log_stub(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "stub"; }
static const char *voice_recording_state_name(int state) { (void)state; return "stub"; }
'''
    code += types + '\n' + enum + '\n' + owner.group() + '\n' + constants + r'''
static voice_recording_state_t s_state;
static bool s_cancel_pending;
static bool s_active_session_automatic, s_active_session_visible;
static uint32_t s_active_session_id;
static bool s_active_session_enrollment, s_enrollment_start_pending;
static bool s_active_session_body_speech_seen, s_active_session_host_endpoint_owned;
static bool s_host_endpoint_ownership_logged;
static bool s_ensure_valid, s_ensure_cancelled_request_valid, s_ensure_request_watermark_valid;
static uint32_t s_ensure_request_id, s_ensure_previous_segment_id, s_ensure_actual_segment_id;
static uint32_t s_ensure_cancelled_request_id, s_ensure_request_watermark;
static voice_recording_ensure_status_t s_ensure_status;
static TickType_t s_ensure_deadline_tick, s_pending_start_deadline_tick, s_pending_start_next_retry_tick;
static bool s_pending_start;
static voice_recording_pending_owner_t s_pending_start_owner;
static const char *s_pending_start_source;
static char s_pending_start_source_storage[VOICE_RECORDING_CONTROL_STORED_SOURCE_BYTES];
static const char *s_pending_start_reason, *s_pending_start_detail;
static bool s_voice_auto_rearm_await_quiet_edge;
static denzic_voice_activation_v1_machine_t s_voice_activation_machine;
static denzic_voice_activation_v1_config_t s_voice_activation_config;
static void *s_vad_queue;
static uint32_t physical_id, packet_sequence;
static TickType_t now_tick;
static unsigned begin_calls, cancel_calls, marker_calls, queue_resets;
static uint32_t marker_id;
static uint16_t marker_origin;
static esp_err_t marker_result, begin_result;
static bool transport_ready=true, va_power_allowed=true;
static unsigned activity_calls;
static uint32_t selected_preroll;
static TickType_t xTaskGetTickCount(void) { return now_tick; }
static uint32_t audio_capture_session_id(void) { return physical_id; }
static bool audio_capture_session_is_active(void) { return s_state != VOICE_RECORDING_STATE_IDLE; }
static esp_err_t ble_audio_stream_send_session_start_marker(uint32_t id, uint16_t origin) {
    marker_calls++; marker_id=id; marker_origin=origin; return marker_result;
}
static void xQueueReset(void *queue) { (void)queue; queue_resets++; }
static void power_manager_record_activity(const char *reason) { (void)reason; activity_calls++; }
static bool power_manager_try_acquire_voice_activation(void) { return va_power_allowed; }
static void power_manager_set_blocker(int blocker, bool enabled) { (void)blocker; (void)enabled; }
static esp_err_t ble_hid_gap_request_active_connection(void) { return ESP_OK; }
static bool ble_audio_stream_is_ready(void) { return transport_ready; }
static esp_err_t voice_key_input_set_recording_output(bool enabled) { (void)enabled; return ESP_OK; }
static void status_led_set_recording(bool enabled, int source) { (void)enabled; (void)source; }
static void status_led_clear_error(int domain) { (void)domain; }
static void status_led_set_error(int domain, int level, const char *detail) { (void)domain; (void)level; (void)detail; }
static void voice_recording_control_clear_power_blockers(void) { }
static uint32_t voice_recording_source_code(const char *source) { (void)source; return 0; }
static void diag_log(int source, int event, int severity, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    (void)source; (void)event; (void)severity; (void)a; (void)b; (void)c; (void)d;
}
static void voice_recording_control_log_flow(voice_recording_flow_stage_t stage, const char *detail,
                                           const char *source, esp_err_t result, bool warn) {
    (void)stage; (void)detail; (void)source; (void)result; (void)warn;
}
static void voice_recording_control_log_device_error(const char *state, const char *detail, esp_err_t result) {
    (void)state; (void)detail; (void)result;
}
static void voice_recording_control_log_device_status(const char *state, const char *reason) {
    (void)state; (void)reason;
}
static void voice_recording_control_cancel(const char *source) {
    (void)source; cancel_calls++; s_cancel_pending=true; s_active_session_host_endpoint_owned=false;
}
static bool voice_recording_control_source_is_ensure(const char *source);
static void voice_recording_control_log_ensure_result(const char *, const char *, esp_err_t, const char *);
'''
    for name in ('store_source', 'reset_pending_start', 'cancel_pending_start',
                 'schedule_pending_start', 'timeout_pending_start'):
        code += function(source, 'voice_recording_control_' + name) + '\n'
    code += r'''
static esp_err_t voice_recording_control_enter_recording(const char *source, bool log, bool reconnect, uint32_t pre_roll_ms) {
    (void)source; (void)log; (void)reconnect; begin_calls++;
    if (begin_result != ESP_OK) return begin_result;
    physical_id++; packet_sequence=0; s_state=VOICE_RECORDING_STATE_RECORDING;
    selected_preroll=pre_roll_ms;
    voice_recording_control_reset_pending_start();
'''
    code += initial + 'return ESP_OK;\n}\n'
    functions = [
        'source_is_ensure', 'ensure_status_name', 'log_ensure_result',
        'parse_hex_u32_fixed', 'parse_ensure_action', 'parse_abort_action',
        'ensure_start_origin', 'tick_reached', 'ensure_status_terminal',
        'ensure_failed', 'ensure_pending_for_segment', 'ensure_deadline_reached',
        'expire_ensure_reservation', 'schedule_ensure_successor', 'poll_ensure_reservation',
    ]
    if re.search(r'voice_recording_control_adopt_active_wake_session\([^;]*\)\s*\{', source):
        functions += ['adopt_active_wake_session']
    functions += ['activate_automatic_session', 'ensure_wake_session', 'abort_wake_session']
    for name in functions:
        code += function(source, 'voice_recording_control_' + name) + '\n'
    code += r'''
/* The unrelated transition-table decision is a platform edge here; the actual
 * pending controller still owns role/preroll, deadline, start and completion. */
static voice_recording_control_snapshot_t voice_recording_control_make_snapshot(const char *source) {
    (void)source;
    return (voice_recording_control_snapshot_t){.state=s_state,.pending_start=s_pending_start,
        .audio_active=audio_capture_session_is_active(),.ble_ready=transport_ready};
}
static voice_recording_control_decision_t voice_recording_control_decide_transition(
        voice_recording_control_event_t event, const voice_recording_control_snapshot_t *snapshot) {
    (void)snapshot;
    assert(event==VOICE_RECORDING_EVENT_PENDING_READY || event==VOICE_RECORDING_EVENT_PENDING_TIMEOUT);
    return (voice_recording_control_decision_t){.result=event==VOICE_RECORDING_EVENT_PENDING_TIMEOUT
        ? ESP_ERR_TIMEOUT : ESP_OK, .flow_stage=VOICE_RECORDING_FLOW_PENDING_READY, .detail="test_edge"};
}
'''
    code += function(source, 'voice_recording_control_poll_pending_start') + '\n'
    schedule_args = ', owner' if has_pending_owner else ''
    code += '''static void schedule_owner(voice_recording_pending_owner_t owner, const char *source) {
    (void)owner;
    voice_recording_control_schedule_pending_start(source, "test_transport", "test_pending"''' + schedule_args + ''');
}
'''
    code += r'''
static bool endpoint_step(uint32_t elapsed_ms, bool speech) {
    s_voice_activation_config=denzic_voice_activation_v1_default_config();
'''
    code += policy + r'''
    denzic_voice_activation_v1_input_t input = {
        .elapsed_ms=elapsed_ms, .enabled=true, .auto_start_enabled=true,
        .auto_stop_enabled=!s_active_session_enrollment,
        .recording_active=s_state==VOICE_RECORDING_STATE_RECORDING,
        .speech_detected=speech || host_endpoint_owned_visible, .start_blocked=true
    };
    return denzic_voice_activation_v1_step(&s_voice_activation_machine, &step_config, &input).action
        == DENZIC_VOICE_ACTIVATION_V1_ACTION_STOP;
}
static void reset_capture(bool automatic, bool visible) {
    s_state=VOICE_RECORDING_STATE_RECORDING; physical_id=981798109u; packet_sequence=461;
    s_active_session_id=physical_id;
    s_active_session_automatic=automatic; s_active_session_visible=visible;
    s_active_session_enrollment=false; s_active_session_body_speech_seen=false;
    s_active_session_host_endpoint_owned=false; s_host_endpoint_ownership_logged=false;
    s_enrollment_start_pending=false; s_ensure_valid=false; s_ensure_cancelled_request_valid=false;
    s_ensure_request_watermark_valid=false; s_ensure_request_watermark=0;
    s_ensure_status=VOICE_RECORDING_ENSURE_STATUS_NONE;
    s_ensure_deadline_tick=0; voice_recording_control_reset_pending_start();
    s_pending_start_owner=VOICE_RECORDING_PENDING_OWNER_NONE;
    s_voice_auto_rearm_await_quiet_edge=false; transport_ready=va_power_allowed=true; activity_calls=0;
    s_cancel_pending=false; now_tick=1000; begin_calls=cancel_calls=marker_calls=queue_resets=0;
    marker_result=begin_result=ESP_OK; s_vad_queue=(void *)1;
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
}
static esp_err_t ensure(uint32_t request, uint32_t previous) {
    char action[40]; snprintf(action,sizeof(action),"ENSURE:%08" PRIX32 ":%08" PRIX32,request,previous);
    return voice_recording_control_ensure_wake_session(action,"test");
}
static esp_err_t abort_request(uint32_t request) {
    char action[24]; snprintf(action,sizeof(action),"ABORT:%08" PRIX32,request);
    return voice_recording_control_abort_wake_session(action,"test");
}
static void assert_adopted(void) {
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ACTIVE);
    assert(s_active_session_host_endpoint_owned && s_active_session_visible);
    assert(physical_id==981798109u && s_ensure_actual_segment_id==physical_id);
    assert(packet_sequence==461 && begin_calls==0 && cancel_calls==0);
    assert(marker_calls==1 && marker_id==physical_id);
    assert(marker_origin==voice_recording_control_ensure_start_origin(100));
    assert(!s_pending_start && s_ensure_deadline_tick==0);
    /* Hidden VAD backlog / silence / old max-duration cannot stop an accepted stream. */
    assert(!endpoint_step(6000,false));
    for (int n=0; n<1200; n++) assert(!endpoint_step(100,false));
}
int main(void) {
    reset_capture(true,true); /* Field ordering: ACTIVATE before ENSURE. */
    assert(ensure(100,physical_id)==ESP_OK); assert_adopted();
    assert(queue_resets==0); /* Existing body/metadata is not discarded twice. */
    assert(ensure(100,physical_id)==ESP_OK && marker_calls==1 && begin_calls==0);
    assert(ensure(100,physical_id+1)==ESP_ERR_INVALID_ARG);
    assert(ensure(101,physical_id)==ESP_ERR_INVALID_STATE);
    assert(ensure(99,physical_id)==ESP_ERR_INVALID_STATE);
    assert(abort_request(99)==ESP_OK && cancel_calls==0);
    assert(abort_request(100)==ESP_OK && cancel_calls==1);
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ABORTED);
    assert(abort_request(100)==ESP_OK && cancel_calls==1);
    assert(ensure(100,physical_id)==ESP_OK && marker_calls==1 && begin_calls==0);

    reset_capture(true,false); /* ENSURE owns hidden capture directly. */
    assert(ensure(100,physical_id)==ESP_OK); assert_adopted(); assert(queue_resets==1);
    reset_capture(false,true); /* Already visible, body acknowledgement still in flight. */
    assert(ensure(100,physical_id)==ESP_OK); assert_adopted();
    reset_capture(true,false); /* Legacy ACTIVATE still transfers ownership in place. */
    assert(voice_recording_control_activate_automatic_session("ACTIVATE")==ESP_OK);
    assert(s_active_session_host_endpoint_owned && marker_calls==0 && begin_calls==0);
    assert(!endpoint_step(6000,false));

    reset_capture(true,false); marker_result=ESP_ERR_TIMEOUT;
    assert(ensure(100,physical_id)==ESP_ERR_TIMEOUT);
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_FAILED);
    assert(!s_active_session_host_endpoint_owned && !s_active_session_visible);
    assert(begin_calls==0 && cancel_calls==1 && physical_id==981798109u);
    reset_capture(true,false);
    assert(ensure(100,physical_id+1)==ESP_ERR_INVALID_STATE && marker_calls==0 && begin_calls==0);
    reset_capture(false,true); s_active_session_enrollment=true;
    assert(ensure(100,physical_id)==ESP_ERR_INVALID_STATE && !s_ensure_valid && cancel_calls==0);
    reset_capture(true,false); s_enrollment_start_pending=true;
    assert(ensure(100,physical_id)==ESP_ERR_INVALID_STATE && !s_ensure_valid && cancel_calls==0);

    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    uint32_t ended_physical_id=physical_id; physical_id=0;
    assert(ensure(100,ended_physical_id)==ESP_OK && s_pending_start);
    assert(voice_recording_control_schedule_ensure_successor(ended_physical_id,"audio_cleanup_before_actor"));
    assert(s_ensure_previous_segment_id==ended_physical_id);
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    assert(ensure(100,physical_id)==ESP_OK && s_pending_start && marker_calls==0 && begin_calls==0);
    TickType_t deadline=s_ensure_deadline_tick;
    now_tick+=100; s_state=VOICE_RECORDING_STATE_IDLE;
    assert(voice_recording_control_schedule_ensure_successor(physical_id,"stop"));
    assert(s_pending_start_deadline_tick==deadline); /* STOP does not renew the lease. */
    assert(abort_request(100)==ESP_OK && !s_pending_start && cancel_calls==0);
    assert(!voice_recording_control_schedule_ensure_successor(physical_id,"late_stop"));
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    assert(ensure(100,physical_id)==ESP_OK); now_tick=s_ensure_deadline_tick;
    assert(!voice_recording_control_schedule_ensure_successor(physical_id,"late_stop"));
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_FAILED);
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    assert(ensure(100,physical_id)==ESP_OK); s_pending_start=false; now_tick=s_ensure_deadline_tick;
    voice_recording_control_poll_ensure_reservation();
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_FAILED);

    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    assert(ensure(100,physical_id)==ESP_OK && begin_calls==1 && marker_calls==0);
    assert(s_ensure_actual_segment_id==981798110u && s_active_session_host_endpoint_owned);
    assert(!endpoint_step(6000,false)); /* STOP-before-ENSURE retains its genuine successor. */
    reset_capture(true,false); assert(abort_request(100)==ESP_OK);
    assert(ensure(100,physical_id)==ESP_ERR_INVALID_STATE && begin_calls==0 && marker_calls==0);

    /* Actual field race: confirmed predecessor arrives while a later ambient
     * candidate is only queued, not physically started. Its label is incidental. */
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.unqualified");
    assert(s_pending_start && activity_calls==0);
    assert(ensure(100,physical_id)==ESP_OK && begin_calls==1);
    assert(s_active_session_host_endpoint_owned && !s_pending_start);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_NONE);
    assert(!endpoint_step(6000,false));

    /* Transport wait atomically replaces only the unqualified request. Its
     * confirmation deadline is retained and poll clears role on real start. */
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    begin_result=ESP_ERR_INVALID_STATE;
    assert(ensure(100,physical_id)==ESP_OK && s_pending_start);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION);
    deadline=s_ensure_deadline_tick;
    assert(ensure(100,physical_id)==ESP_OK && begin_calls==1);
    now_tick=s_pending_start_next_retry_tick; begin_result=ESP_OK;
    voice_recording_control_poll_pending_start();
    assert(!s_pending_start && s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_NONE);
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ACTIVE && s_ensure_deadline_tick==0);
    assert(s_active_session_host_endpoint_owned && selected_preroll==0 && begin_calls==2);
    assert(!endpoint_step(6000,false));

    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    assert(ensure(100,physical_id)==ESP_OK && begin_calls==0);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION);
    deadline=s_ensure_deadline_tick; now_tick+=100; s_state=VOICE_RECORDING_STATE_IDLE;
    assert(voice_recording_control_schedule_ensure_successor(physical_id,"stop"));
    assert(s_pending_start_deadline_tick==deadline);
    voice_recording_control_poll_pending_start();
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ACTIVE && begin_calls==1);

    /* Explicit start retains ownership, even with an automatic-looking label. */
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_EXPLICIT_CONTROL,"voice_activation.manual_start");
    assert(ensure(100,physical_id)==ESP_ERR_INVALID_STATE && s_pending_start);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_EXPLICIT_CONTROL);
    assert(begin_calls==0 && marker_calls==0);
    now_tick=s_pending_start_next_retry_tick; voice_recording_control_poll_pending_start();
    assert(begin_calls==1 && selected_preroll==0 && !s_active_session_host_endpoint_owned);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_NONE);

    /* A stale source must fail rather than steal, discard or fake fresh audio. */
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_TRANSFERRING;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    assert(ensure(100,physical_id+1)==ESP_ERR_INVALID_STATE && s_pending_start);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE && begin_calls==0);
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    begin_result=ESP_ERR_NOT_FOUND;
    assert(ensure(100,physical_id)==ESP_ERR_NOT_FOUND && s_pending_start);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE);
    assert(!s_active_session_host_endpoint_owned);
    begin_result=ESP_OK; now_tick=s_pending_start_next_retry_tick;
    s_voice_activation_config=denzic_voice_activation_v1_default_config();
    voice_recording_control_poll_pending_start();
    assert(selected_preroll==s_voice_activation_config.pre_roll_ms && !s_active_session_visible);

    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    begin_result=ESP_ERR_INVALID_STATE;
    assert(ensure(100,physical_id)==ESP_OK);
    assert(abort_request(100)==ESP_OK && !s_pending_start && cancel_calls==0);
    assert(s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_NONE);
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    begin_result=ESP_ERR_INVALID_STATE; assert(ensure(100,physical_id)==ESP_OK);
    now_tick=s_pending_start_deadline_tick; voice_recording_control_poll_pending_start();
    assert(!s_pending_start && s_pending_start_owner==VOICE_RECORDING_PENDING_OWNER_NONE);
    assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_FAILED && !s_active_session_host_endpoint_owned);
    /* Enrollment and idle power commit cannot be overridden by ambient work. */
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    s_enrollment_start_pending=true;
    assert(ensure(100,physical_id)==ESP_ERR_INVALID_STATE && s_pending_start && begin_calls==0);
    reset_capture(true,false); s_state=VOICE_RECORDING_STATE_IDLE; va_power_allowed=false;
    schedule_owner(VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,"voice_activation.auto_start");
    assert(!s_pending_start && activity_calls==0);

    reset_capture(true,false); assert(endpoint_step(6000,false)); /* Ambient remains bounded. */
    reset_capture(false,true); assert(endpoint_step(6000,false)); /* Empty manual remains bounded. */
    reset_capture(false,true); s_active_session_body_speech_seen=true;
    assert(!endpoint_step(6000,false)); /* Normal body follows the host endpoint. */
    reset_capture(false,true); s_active_session_enrollment=true;
    assert(!endpoint_step(6000,false)); /* Enrollment remains explicitly host timed. */
    puts("PASS: production ENSURE/pending lifecycle promotes queued ambient only; preserves source, explicit/enrollment owners, transport retry, cancel/timeout, 120s body and hidden rotations");
    return 0;
}
'''
    platform = ROOT/'third_party/denzic-platform/voice_activation/embedded/c'
    includes = [platform/'include', ROOT/'components/voice_recording_control/include']
    implementation = platform/'src/denzic_voice_activation_v1.c'
    with tempfile.TemporaryDirectory(prefix='listener-capture-owner-') as directory:
        tmp = Path(directory)
        (tmp/'test.c').write_text(code, encoding='utf-8')
        executable = tmp/('test.exe' if os.name=='nt' else 'test')
        if os.name=='nt':
            vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs = subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
            vcvars = Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            flags = ' '.join(f'/I"{p}"' for p in includes)
            (tmp/'build.cmd').write_text(
                f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
                f'cl /nologo /std:c11 /W4 /WX {flags} test.c "{implementation}" /Fe:test.exe\n',encoding='utf-8')
            subprocess.run(['cmd.exe','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
        else:
            subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Werror',
                            *['-I'+str(p) for p in includes], 'test.c',str(implementation),
                            '-o',str(executable)],cwd=tmp,check=True)
        subprocess.run([str(executable)],cwd=tmp,check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'components/voice_recording_control/voice_recording_control.c')
    args = parser.parse_args()
    verify_source(args.source.read_text(encoding='utf-8'))


if __name__=='__main__':
    main()
