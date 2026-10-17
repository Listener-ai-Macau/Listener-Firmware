"""Execute production capture/stop/resume C with numbered PCM source frames.

Only platform locks, clock and BLE transport are stubs. The frame processor,
history selection and session lifecycle are extracted unchanged. PDM stop
ownership is checked separately from this synchronous source-frame pump.
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
    source = (args.source or ROOT/'ports/esp32/audio_capture/audio_capture_esp32.c').read_text(encoding='utf8')
    names = ('SAMPLE_RATE_HZ', 'FRAME_MS', 'FRAME_SAMPLES', 'FRAME_BYTES',
             'LOG_INTERVAL_FRAMES', 'STREAM_BATCH_FRAMES', 'STREAM_BATCH_BYTES',
             'STREAM_PROGRESS_LOG_PACKET_INTERVAL', 'VOICE_PREROLL_MAX_MS',
             'WAKE_RESUME_LEASE_MS', 'VOICE_PREROLL_FRAMES', 'VOICE_PREROLL_BYTES')
    constants = '\n'.join(re.search(r'^#define AUDIO_CAPTURE_'+name+r'[^\n]*(?:\n[ \t]+[^\n]+)?', source, re.M).group() for name in names)
    types = re.search(r'typedef enum \{[^}]*\} audio_capture_export_mode_t;', source).group()
    types += re.search(r'typedef struct \{[^}]*\} audio_capture_export_state_t;', source).group()
    code = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#ifdef _MSC_VER
#pragma warning(disable:4505 4189)
#endif
typedef int esp_err_t;
typedef unsigned TickType_t;
enum { ESP_OK=0, ESP_ERR_INVALID_STATE=1, ESP_ERR_INVALID_ARG=2,
       ESP_ERR_INVALID_SIZE=3, ESP_ERR_NOT_SUPPORTED=4, ESP_ERR_NOT_FOUND=5, ESP_ERR_NO_MEM=6 };
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool fail_allocation;
static void *heap_caps_malloc(size_t n,int caps) {(void)caps;return fail_allocation?NULL:malloc(n);}
static void heap_caps_free(void *p) {free(p);}
enum { AUDIO_CAPTURE_STOP_ORIGIN_USER=0, AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION=1,
       AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION_MAX_DURATION=2 };
typedef int audio_capture_stop_origin_t;
enum { LISTENER_AUDIO_SESSION_START_ORIGIN_USER=0, LISTENER_AUDIO_SESSION_START_ORIGIN_VOICE_ACTIVATION=1,
       LISTENER_AUDIO_SESSION_STOP_ORIGIN_USER=0, LISTENER_AUDIO_SESSION_ERROR_NONE=0 };
#define TAG "test"
#define pdTRUE 1
#define portMAX_DELAY 0
#define DIAG_SRC_AUDIO 0
#define DIAG_AUDIO_SESSION_REJ 0
#define DIAG_AUDIO_SESSION 0
#define DIAG_SEV_WARN 0
#define DIAG_SEV_INFO 0
#define DIAG_AUDIO_DROP 0
static void log_stub(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGE(...) log_stub(__VA_ARGS__)
#define diag_log(...) ((void)0)
static const char *esp_err_to_name(esp_err_t e) {(void)e;return "stub";}
static int xSemaphoreTake(void *m,int wait) {(void)m;(void)wait;return pdTRUE;}
static void xSemaphoreGive(void *m) {(void)m;}
static const char *audio_capture_static_unavailable_reason(void) {return NULL;}
static esp_err_t audio_capture_apply_idle_power_save(bool enabled) {(void)enabled;return ESP_OK;}
static void audio_capture_wake_task(void) {}
static bool ble_audio_stream_is_ready(void) {return true;}
static uint16_t ble_audio_stream_get_audio_payload_bytes(void) {return 480;}
static uint32_t audio_capture_packet_safe_total_frames(uint16_t n) {(void)n;return 100000;}
static int64_t esp_timer_get_time(void) {return 0;}
static uint32_t next_session_id;
static uint32_t audio_capture_next_session_id(void) {return ++next_session_id;}
static uint16_t output[2000];static unsigned output_count,start_count,stop_count;
static bool fail_stop;
static uint16_t ble_audio_stream_count_audio_packets(const uint8_t *p,uint16_t n) {(void)p;return (uint16_t)((n+479u)/480u);}
static esp_err_t ble_audio_stream_send_session_start_with_origin(uint32_t id,uint16_t origin) {(void)id;(void)origin;start_count++;return ESP_OK;}
static esp_err_t ble_audio_stream_send_session_stop_with_origin(uint32_t id,uint16_t n,uint16_t origin) {(void)id;(void)n;(void)origin;stop_count++;return fail_stop?ESP_ERR_INVALID_STATE:ESP_OK;}
static esp_err_t ble_audio_stream_send_session_cancel(uint32_t id,uint16_t n) {(void)id;(void)n;return ESP_OK;}
static esp_err_t ble_audio_stream_send_session_error(uint32_t id,uint16_t n,uint16_t err) {(void)id;(void)n;(void)err;return ESP_OK;}
static uint16_t audio_capture_session_error_from_stream_result(esp_err_t e) {(void)e;return 1;}
'''
    code += constants+'\n'+types+r'''
static bool s_started=true,s_ota_suspended,s_idle_power_save_requested;
static void *s_state_mutex=(void *)1;
static audio_capture_export_state_t s_export_state;
static uint32_t s_frame_captured_count,s_frame_count;
static uint64_t s_source_frame_id,s_session_packet_planning_us;
static uint32_t s_session_packet_planning_max_us;
static bool s_capture_transport_backpressure_active,s_voice_activation_monitoring=true;
static uint32_t s_capture_transport_backpressure_events;
static uint8_t s_raw_input_level_percent;
static int16_t history[AUDIO_CAPTURE_VOICE_PREROLL_FRAMES][AUDIO_CAPTURE_FRAME_SAMPLES];
static int16_t (*s_voice_preroll)[AUDIO_CAPTURE_FRAME_SAMPLES]=history;
static uint8_t s_voice_preroll_levels[AUDIO_CAPTURE_VOICE_PREROLL_FRAMES];
static uint64_t s_voice_preroll_frame_ids[AUDIO_CAPTURE_VOICE_PREROLL_FRAMES];
static uint16_t s_voice_preroll_write_index,s_voice_preroll_count,s_session_preroll_start_index,s_session_preroll_count;
static uint32_t s_wake_resume_session_id;
static uint64_t s_wake_resume_next_frame_id;
static uint32_t s_prior_wake_resume_session_id,s_wake_resume_lease_request_id,s_wake_resume_lease_session_id;
static uint64_t s_prior_wake_resume_next_frame_id,s_wake_resume_lease_next_frame_id;
static uint8_t *s_session_resume_snapshot;
static uint16_t s_session_resume_snapshot_frames;
static uint16_t s_next_session_start_origin_override=UINT16_MAX;
static bool s_pdm_afe_stop_drain_requested,s_pdm_afe_stop_drain_feed_cutoff_ack,s_pdm_afe_stop_drain_complete;
static esp_err_t ble_audio_stream_send_session_audio(uint32_t id,uint16_t seq,const uint8_t *p,uint16_t n,uint16_t count,uint8_t level) {
    (void)id;(void)seq;(void)count;(void)level;
    assert(n%AUDIO_CAPTURE_FRAME_BYTES==0);
    for(unsigned i=0;i<n;i+=AUDIO_CAPTURE_FRAME_BYTES) {
        uint16_t marker;memcpy(&marker,p+i,sizeof(marker));
        assert(output_count<2000);output[output_count++]=marker;
    }
    return ESP_OK;
}
'''
    for name in ('audio_capture_export_cleanup', 'audio_capture_send_session_preroll',
                 'audio_capture_count_audio_packets', 'audio_capture_packet_sequence_can_advance'):
        code += function(source,name)+'\n'
    if 'static esp_err_t audio_capture_session_begin_internal(' in source:
        for name in ('audio_capture_select_wake_resume','audio_capture_reserve_wake_resume',
                     'audio_capture_release_wake_resume','audio_capture_session_begin_internal',
                     'audio_capture_session_begin_with_preroll', 'audio_capture_session_begin_with_preroll_and_origin',
                     'audio_capture_session_resume_wake'):
            code += function(source,name)+'\n'
    else:
        for name in ('audio_capture_session_begin_with_preroll','audio_capture_session_begin_with_preroll_and_origin'):
            code += function(source,name)+'\n'
        # This is the actual pre-fix control call for a late ENSURE successor.
        code += 'static esp_err_t audio_capture_session_resume_wake(uint32_t previous,uint16_t origin) {(void)previous;return audio_capture_session_begin_with_preroll_and_origin(0,origin);}\n'
    code += '#define CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM 1\n#define CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC 1\n'
    code += function(source,'audio_capture_session_stop_with_origin')+'\n'
    code += '#undef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM\n#undef CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC\n'
    code += function(source,'audio_capture_process_frame')+r'''
static unsigned marker;
static void pump(unsigned frames) {
    while(frames--) {
        int16_t pcm[AUDIO_CAPTURE_FRAME_SAMPLES];++marker;
        for(unsigned i=0;i<AUDIO_CAPTURE_FRAME_SAMPLES;i++)pcm[i]=(int16_t)marker;
        audio_capture_process_frame(pcm);
    }
}
static void reset(void) {
    audio_capture_export_cleanup();memset(history,0,sizeof(history));
    memset(s_voice_preroll_frame_ids,0,sizeof(s_voice_preroll_frame_ids));
    s_source_frame_id=0;s_voice_preroll_count=0;s_voice_preroll_write_index=0;
    s_session_preroll_count=0;output_count=start_count=stop_count=marker=0;
    s_pdm_afe_stop_drain_requested=s_pdm_afe_stop_drain_feed_cutoff_ack=s_pdm_afe_stop_drain_complete=false;
    s_voice_activation_monitoring=true;fail_stop=false;
}
static uint32_t expired_wake(unsigned gap) {
    reset();pump(100);
    assert(audio_capture_session_begin_with_preroll(2000)==ESP_OK);
    const uint32_t previous=s_export_state.session_id;
    pump(200);
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION_MAX_DURATION)==ESP_OK);
    assert(!s_pdm_afe_stop_drain_requested); /* a hidden window must not cut acoustic input */
    pump(1);assert(!s_export_state.active);pump(gap);
    return previous;
}
int main(void) {
    const unsigned gaps[]={0,1,24,76,99};
    for(unsigned i=0;i<sizeof(gaps)/sizeof(gaps[0]);i++) {
        uint32_t previous=expired_wake(gaps[i]);
        assert(audio_capture_session_resume_wake(previous,0x8001)==ESP_OK);
        pump(100);
        assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_USER)==ESP_OK);
        assert(s_pdm_afe_stop_drain_requested);pump(1);
        assert(output_count==marker-1 && start_count==2 && stop_count==2);
        for(unsigned k=0;k<output_count;k++)assert(output[k]==k+1); /* no loss or overlap */
        assert(audio_capture_session_resume_wake(previous,0x8001)!=ESP_OK);
    }
    uint32_t previous=expired_wake(100);
    assert(audio_capture_session_resume_wake(previous,0x8001)!=ESP_OK && !s_export_state.active);
    /* Same predecessor already finished; only its BLE tail is draining.
     * A transaction admitted while its checkpoint is fresh must pin the
     * original frame cursor before that independent drain exceeds two seconds.
     * No later ambient session or larger unowned age allowance is involved. */
    previous=expired_wake(5);
    const unsigned same_predecessor_prefix=output_count;
    assert(audio_capture_reserve_wake_resume(previous,90)==ESP_OK);
    pump(110);
    assert(audio_capture_session_resume_wake(previous,0x805a)==ESP_OK);
    pump(10);
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_USER)==ESP_OK);pump(1);
    assert(output_count==marker-1 && output_count>same_predecessor_prefix);
    for(unsigned k=0;k<output_count;k++)assert(output[k]==k+1);
    assert(s_session_resume_snapshot==NULL && s_wake_resume_lease_request_id==0);
    previous=expired_wake(5);
    assert(audio_capture_reserve_wake_resume(previous,91)==ESP_OK);
    pump(110);audio_capture_release_wake_resume(91);
    assert(audio_capture_session_resume_wake(previous,0x805b)==ESP_ERR_NOT_FOUND);
    previous=expired_wake(24);
    assert(audio_capture_session_resume_wake(previous+1,0x8001)!=ESP_OK);
    assert(audio_capture_session_resume_wake(previous,0x8001)==ESP_OK);
    reset();pump(100);assert(audio_capture_session_begin_with_preroll(2000)==ESP_OK);pump(200);
    previous=s_export_state.session_id;
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_USER)==ESP_OK);
    assert(s_pdm_afe_stop_drain_requested);pump(1);
    assert(audio_capture_session_resume_wake(previous,0x8001)!=ESP_OK);
    reset();pump(100);assert(audio_capture_session_begin_with_preroll(2000)==ESP_OK);pump(200);
    previous=s_export_state.session_id;fail_stop=true;
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION_MAX_DURATION)==ESP_OK);pump(1);
    assert(audio_capture_session_resume_wake(previous,0x8001)!=ESP_OK);
    /* Reproduce the actual missing lifecycle boundary: a newer hidden
     * segment has started while Type is still confirming the old phrase.
     * Its PCM is unowned and must not be spliced into the accepted body. */
    previous=expired_wake(24);
    const unsigned owner_prefix_count=output_count;
    assert(audio_capture_session_begin_with_preroll(2000)==ESP_OK);pump(25);
    uint16_t index=0,frames=0;
    printf("rotation checkpoint previous=%u primary=%u next=%llu source=%llu active=%u\n",previous,s_wake_resume_session_id,(unsigned long long)s_wake_resume_next_frame_id,(unsigned long long)s_source_frame_id,s_export_state.active?1:0);fflush(stdout);
    assert(audio_capture_select_wake_resume(previous,&index,&frames)==ESP_OK);
    assert(audio_capture_reserve_wake_resume(previous,100)==ESP_OK);
    assert(audio_capture_reserve_wake_resume(previous,101)==ESP_ERR_INVALID_STATE);
    audio_capture_release_wake_resume(99); /* A stale release cannot revoke the lease. */
    assert(s_wake_resume_lease_request_id==100);
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION_MAX_DURATION)==ESP_OK);
    pump(1);pump(120); /* Existing transaction may drain >2 s; no new expiry. */
    assert(audio_capture_select_wake_resume(previous,&index,&frames)==ESP_OK && frames>100);
    output_count=owner_prefix_count; /* Type withheld all unconfirmed ambient PCM. */
    fail_allocation=true;
    assert(audio_capture_session_resume_wake(previous,0x8001)==ESP_ERR_NO_MEM);
    assert(!s_export_state.active && s_wake_resume_lease_request_id==100);
    fail_allocation=false;
    assert(audio_capture_session_resume_wake(previous,0x8001)==ESP_OK);
    pump(10);
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_USER)==ESP_OK);pump(1);
    assert(output_count==marker-1);
    for(unsigned k=0;k<output_count;k++)assert(output[k]==k+1);
    assert(s_session_resume_snapshot==NULL && s_wake_resume_lease_request_id==0);
    /* Enlarged storage cannot authorize an already stale source. */
    previous=expired_wake(100);
    assert(audio_capture_reserve_wake_resume(previous,200)==ESP_ERR_NOT_FOUND);
    previous=expired_wake(24);
    assert(audio_capture_reserve_wake_resume(previous,201)==ESP_OK);
    pump(110);audio_capture_release_wake_resume(201);
    assert(audio_capture_session_resume_wake(previous,0x8001)==ESP_ERR_NOT_FOUND);
    /* An explicit stop still severs continuity, including a reserved cursor. */
    previous=expired_wake(24);
    assert(audio_capture_session_begin_with_preroll(2000)==ESP_OK);pump(1);
    assert(audio_capture_reserve_wake_resume(previous,202)==ESP_OK);
    assert(audio_capture_session_stop_with_origin(AUDIO_CAPTURE_STOP_ORIGIN_USER)==ESP_OK);pump(1);
    assert(audio_capture_session_resume_wake(previous,0x8001)==ESP_ERR_NOT_FOUND);
    puts("PASS: production wake rotation/resume PCM has no gaps or overlaps; user STOP drains; expired/mismatched/failed histories rejected");
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-wake-continuity-',ignore_cleanup_errors=True) as directory:
        tmp=Path(directory);(tmp/'test.c').write_text(code,encoding='utf8')
        if os.name=='nt':
            vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
            vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n',encoding='utf8')
            subprocess.run(['cmd.exe','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
            subprocess.run([str(tmp/'test.exe')],cwd=tmp,check=True)
        else:
            subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Wextra','test.c','-o','test'],cwd=tmp,check=True)
            subprocess.run([str(tmp/'test')],cwd=tmp,check=True)


if __name__=='__main__':
    main()
