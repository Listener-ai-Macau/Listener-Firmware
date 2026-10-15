"""Execute production stop/cancel/inactive cleanup with controlled capture results.
No serial, BLE writes or user interaction. Platform outputs are observed stubs;
the production reducer and controller functions execute unchanged.
"""
from pathlib import Path
import argparse,re,subprocess,os
from test_power_source_filter import ROOT,function

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 s=(a.source or ROOT/'components/voice_recording_control/voice_recording_control.c').read_text(encoding='utf-8')
 code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
typedef int esp_err_t;
typedef int audio_capture_stop_origin_t;
typedef int TickType_t;
typedef enum {VOICE_RECORDING_STATE_IDLE,VOICE_RECORDING_STATE_RECORDING,VOICE_RECORDING_STATE_TRANSFERRING,VOICE_RECORDING_STATE_RECOVERY} voice_recording_state_t;
enum {ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_TIMEOUT=3,AUDIO_CAPTURE_STOP_ORIGIN_USER=2,
 STATUS_LED_REC_SOURCE_NONE=0,STATUS_LED_ERROR_DOMAIN_REC=1,STATUS_LED_ERROR_RETRYABLE=1};
#define TAG "inactive-test"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define diag_log(...) ((void)0)
'''
 for name in ['voice_recording_flow_stage_t','voice_recording_control_event_t','voice_recording_control_source_class_t','voice_recording_ensure_status_t','voice_recording_control_effect_t','voice_recording_control_snapshot_t','voice_recording_control_decision_t']:
  code+=re.search(r'typedef (?:enum|struct) \{[^}]*\} '+name+r';',s).group()+'\n'
 code+=r'''
static voice_recording_state_t s_state;
static bool s_active_session_automatic,s_active_session_visible,s_active_session_host_endpoint_owned,s_active_session_enrollment;
static bool s_cancel_pending,s_pending_start,s_enrollment_start_pending;
static const char *s_cancel_source,*s_active_session_source,*s_pending_start_source;
static char s_cancel_source_storage[128];
static TickType_t s_enrollment_start_deadline_tick,s_enrollment_start_next_retry_tick,s_ensure_deadline_tick;
static uint32_t s_active_session_id,s_session_count;
static bool s_ensure_valid;
static voice_recording_ensure_status_t s_ensure_status;
static bool capture_active,rec_led,processing_led,recording_output,diag_active,host_protected,power_held,release_order_bad;
static unsigned clear_count;
static esp_err_t stop_result,cancel_result;
static bool audio_capture_session_is_active(void){return capture_active;}
static esp_err_t audio_capture_session_stop_with_origin(int origin){(void)origin;return stop_result;}
static esp_err_t audio_capture_session_cancel(void){return cancel_result;}
static void power_manager_record_activity(const char *s){(void)s;}
static bool voice_recording_control_source_is_explicit_manual_control(const char *s){(void)s;return true;}
static bool voice_recording_control_source_is_host_control(const char *s){(void)s;return true;}
static bool voice_recording_control_source_is_user_start_intent(const char *s){(void)s;return false;}
static bool voice_recording_control_host_cleanup_toggle_guard_active(const char *s){(void)s;return false;}
static voice_recording_control_source_class_t voice_recording_control_classify_source(const char *s){(void)s;return VOICE_RECORDING_SOURCE_HOST_CONTROL;}
static bool ble_audio_stream_is_ready(void){return true;}
static void voice_recording_control_clear_auto_rearm_latch(const char *s){(void)s;}
static void voice_recording_control_clear_host_speech_protection(void){host_protected=false;}
static void voice_recording_control_clear_power_blockers(void){
 if(rec_led||recording_output||diag_active||host_protected)release_order_bad=true;
 power_held=false;clear_count++;
}
static void voice_stack_diag_set_session_active(bool b){diag_active=b;}
static int voice_key_input_set_recording_output(bool b){recording_output=b;return 0;}
static void status_led_set_recording(bool b,int source){(void)source;rec_led=b;}
static void status_led_set_processing(bool b,const char *s){(void)s;processing_led=b;}
static void status_led_set_error(int a,int b,const char *s){(void)a;(void)b;(void)s;}
static void voice_recording_control_log_flow(int a,const char*b,const char*c,int d,bool e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void voice_recording_control_log_device_error(const char*a,const char*b,int c){(void)a;(void)b;(void)c;}
static void voice_recording_control_log_device_status(const char*a,const char*b){(void)a;(void)b;}
static void voice_recording_control_log_toggle_ignored(const char*a,const char*b,int c,bool d){(void)a;(void)b;(void)c;(void)d;}
static void voice_recording_control_cancel_pending_start(const char*s){(void)s;}
static void voice_recording_control_complete_transfer_cleanup(const char*a,const char*b){(void)a;(void)b;}
static const char*voice_recording_control_store_source(char*to,size_t n,const char*s){snprintf(to,n,"%s",s);return to;}
static bool voice_recording_control_ensure_pending_for_segment(uint32_t s){(void)s;return false;}
static void voice_recording_control_log_ensure_result(const char*a,const char*b,int c,const char*d){(void)a;(void)b;(void)c;(void)d;}
static bool voice_recording_control_schedule_ensure_successor(uint32_t s,const char*r){(void)s;(void)r;return false;}
static void voice_recording_control_try_start_pending_enrollment(const char*s){(void)s;}
static void voice_recording_control_arm_host_cleanup_toggle_guard(void){}
static int voice_recording_control_ensure_failed(const char*a,int b,const char*c){(void)a;(void)c;return b;}
static void voice_recording_control_handle_session_inactive(void);
'''
 for name in ['voice_recording_control_make_decision','voice_recording_control_make_snapshot','voice_recording_control_decide_transition','voice_recording_control_exit_recording_with_origin','voice_recording_control_cancel','voice_recording_control_handle_session_inactive']:
  code+=function(s,name)+'\n'
 code+=r'''
static void reset_case(bool active){
 s_state=VOICE_RECORDING_STATE_RECORDING;s_active_session_id=42;s_active_session_source="type";
 s_active_session_visible=true;s_active_session_host_endpoint_owned=true;s_active_session_enrollment=false;s_active_session_automatic=false;
 s_cancel_pending=s_pending_start=false;s_cancel_source=NULL;s_pending_start_source=NULL;
 s_ensure_valid=true;s_ensure_status=VOICE_RECORDING_ENSURE_STATUS_ACTIVE;
 capture_active=active;rec_led=recording_output=diag_active=host_protected=power_held=true;
 processing_led=release_order_bad=false;clear_count=0;stop_result=cancel_result=ESP_ERR_INVALID_STATE;
}
static void require_closed(void){
 assert(s_state==VOICE_RECORDING_STATE_IDLE);assert(!rec_led);assert(!recording_output);assert(!diag_active);assert(!host_protected);
 assert(!power_held);assert(!release_order_bad);assert(clear_count==1);assert(s_active_session_id==0);assert(!s_cancel_pending);
}
int main(void){
 reset_case(false);assert(voice_recording_control_exit_recording_with_origin("type",AUDIO_CAPTURE_STOP_ORIGIN_USER)==ESP_ERR_INVALID_STATE);require_closed();
 assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_DONE);
 reset_case(false);voice_recording_control_cancel("type");require_closed();assert(!processing_led);assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ABORTED);
 reset_case(true);voice_recording_control_exit_recording_with_origin("type",AUDIO_CAPTURE_STOP_ORIGIN_USER);
 assert(s_state==VOICE_RECORDING_STATE_RECORDING&&rec_led&&recording_output&&power_held&&clear_count==0);
 reset_case(true);voice_recording_control_cancel("type");assert(s_state==VOICE_RECORDING_STATE_RECORDING&&rec_led&&recording_output&&power_held&&clear_count==0);
 /* Cancellation that finishes synchronously uses the same terminal owner. */
 reset_case(false);cancel_result=ESP_OK;voice_recording_control_cancel("type");require_closed();assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ABORTED);
 /* An accepted asynchronous cancel must retain outputs until capture ends. */
 reset_case(true);cancel_result=ESP_OK;voice_recording_control_cancel("type");assert(rec_led&&power_held&&s_cancel_pending);
 capture_active=false;voice_recording_control_handle_session_inactive();require_closed();assert(s_ensure_status==VOICE_RECORDING_ENSURE_STATUS_ABORTED);
 puts("PASS: actual stop/cancel terminal cleanup, active failure protection, synchronous/asynchronous cancellation, ensure bookkeeping and power-release ordering");
 return 0;
}
'''
 (a.output/'test.c').write_text(code,encoding='utf-8')
 vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe';vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip();vc=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
 (a.output/'build.cmd').write_text(f'@echo off\ncall "{vc}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W3 test.c /Fe:test.exe\n',encoding='utf-8')
 subprocess.run(['cmd.exe','/d','/c',str((a.output/'build.cmd').absolute())],cwd=a.output,check=True)
 raise SystemExit(subprocess.run([str((a.output/'test.exe').absolute())],cwd=a.output).returncode)
if __name__=='__main__':main()
