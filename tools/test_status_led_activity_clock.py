"""Compile actual LED lifecycle/clock functions against a clock/RTOS boundary fixture.

This is a native C logic counterexample, not physical LED/USB/BLE qualification.
The function bodies and timing constants come verbatim from status_led.c.
"""
import argparse,ctypes,json,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'components/status_led/status_led.c');p.add_argument('--expect-old-reset',action='store_true');p.add_argument('--output',type=Path);a=p.parse_args()
source=a.source.read_text(encoding='utf-8');build=Path(tempfile.mkdtemp(prefix='listener-led-clock-'))
def function(name):
 match=re.search(r'^(?:static\s+)?[\w *]+\b'+name+r'\([^;]*?\)\s*\{',source,re.M);assert match,name
 start=match.start();pos=source.index('{',match.start());depth=1;end=pos+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]+'\n'
names=['status_led_clamp_u32_to_u8','status_led_smoothstep_per_mille','status_led_effect_elapsed_ms_locked','status_led_effect_entry_scale_percent_locked','status_led_scale_effect_percent_locked','status_led_set_recording','status_led_set_processing','status_led_clear_stale_processing_locked']
defines='\n'.join(re.search(r'^#define '+name+r' .+$',source,re.M)[0] for name in ['STATUS_LED_ACCENT_ENTRY_RAMP_MS','STATUS_LED_STATUS_WINDOW_MS','STATUS_LED_ERROR_HOLD_MS','STATUS_LED_PROCESSING_STALE_TIMEOUT_MS'])
(build/'esp_err.h').write_text('typedef int esp_err_t;\n',encoding='utf-8')
fixture=r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "status_led.h"
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define DIAG_SRC_STATUS_LED 0
#define DIAG_LED_STATE 0
#define DIAG_SEV_INFO 0
#define DIAG_SEV_WARN 1
static uint32_t now;
static int s_mutex;
static struct {
 bool recording_active, processing_active, ota_active, preview_effect_only;
 status_led_rec_source_t rec_source;
 status_led_error_domain_t error_domain;
 status_led_error_severity_t error_severity;
 uint32_t recording_started_ms,processing_started_ms,processing_updated_ms,ota_started_ms,last_transition_ms;
 uint32_t recording_level_percent,recording_level_updated_ms,recording_level_hold_until_ms;
 uint32_t transition_clear_mask,ble_repair_until_ms,ble_repair_cue_started_ms,ble_repair_cue_until_ms;
 uint32_t status_window_until_ms,error_started_ms,error_until_ms;
} s_state;
static uint32_t status_led_now_ms(void){return now;}
static int xSemaphoreTake(int m,unsigned t){return 1;}
static void xSemaphoreGive(int m){}
static void status_led_resume_output_locked(void){}
static void status_led_schedule_idle_transition_clear_locked(uint32_t t){}
static void status_led_reset_recording_level_visual_locked(uint32_t t){}
static bool status_led_ec11_feedback_active_locked(uint32_t t){return false;}
static void status_led_clear_ec11_feedback_locked(void){}
static void status_led_clear_ok_locked(void){}
static void status_led_clear_retryable_error_locked(status_led_error_domain_t d){}
static bool status_led_clear_ec11_press_feedback_locked(void){return false;}
static void status_led_set_last_reason_locked(const char*r){}
static void status_led_request_refresh(void){}
static void diag_log(int a,int b,int c,unsigned d,unsigned e,unsigned f,unsigned g){}
'''+defines+'\n'+'\n'.join(function(name) for name in names)+r'''
__declspec(dllexport) void reset(void){memset(&s_state,0,sizeof(s_state));now=1000;}
__declspec(dllexport) void tick(uint32_t t){now=t;}
__declspec(dllexport) void record(int active,int source){status_led_set_recording(active,(status_led_rec_source_t)source);}
__declspec(dllexport) void process(int active){status_led_set_processing(active,"host_processing");}
__declspec(dllexport) uint32_t elapsed(void){return status_led_effect_elapsed_ms_locked(now);}
__declspec(dllexport) unsigned scale(void){return status_led_scale_effect_percent_locked(30,now);}
__declspec(dllexport) void unrelated_transition(void){s_state.last_transition_ms=now;}
__declspec(dllexport) void ota(int active){s_state.ota_active=active;s_state.ota_started_ms=now;s_state.last_transition_ms=now;}
__declspec(dllexport) unsigned stale_limit(void){return STATUS_LED_PROCESSING_STALE_TIMEOUT_MS;}
__declspec(dllexport) unsigned processing_live(void){status_led_clear_stale_processing_locked(now);return s_state.processing_active;}
'''
(build/'fixture.c').write_text(fixture,encoding='utf-8')
vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat')
(build/'build.cmd').write_text(f'@echo off\ncall "{vc}" -arch=x64 -host_arch=x64 >nul\ncd /d "{build}"\ncl /nologo /std:c11 /LD /I"{build}" /I"{ROOT / "components/status_led/include"}" fixture.c /link /OUT:clock.dll\nexit /b %errorlevel%\n',encoding='utf-8')
cp=subprocess.run(['cmd.exe','/d','/c',str(build/'build.cmd')],capture_output=True,text=True)
assert cp.returncode==0,cp.stdout+cp.stderr
d=ctypes.CDLL(str(build/'clock.dll'));d.tick.argtypes=[ctypes.c_uint32];d.elapsed.restype=ctypes.c_uint32;results=[]
def observe(name,expected_elapsed,expected_scale):
 x={'case':name,'elapsedMs':d.elapsed(),'scaled30':d.scale(),'expectedElapsedMs':expected_elapsed,'expectedScale':expected_scale};x['passed']=x['elapsedMs']==expected_elapsed and x['scaled30']==expected_scale;results.append(x)
d.reset();d.record(1,1);d.tick(5000);observe('recording_initial_ramp_complete',4000,30)
d.process(0);observe('duplicate_processing_stop_during_recording',4000,30)
d.tick(5500);d.process(1);observe('processing_start_during_same_recording',4500,30)
d.tick(6000);d.process(1);observe('duplicate_processing_start_during_recording',5000,30)
d.tick(6500);d.process(0);observe('processing_stop_during_same_recording',5500,30)
d.tick(7000);d.unrelated_transition();observe('other_status_update_during_recording',6000,30)
d.tick(7500);d.record(1,1);observe('same_recording_reaffirmed',6500,30)
d.tick(8000);d.record(0,0);d.tick(9000);d.record(1,1);observe('next_recording_enters_once',0,0)
if not a.expect_old_reset:
 d.reset();d.process(1);d.tick(5000);d.process(1);observe('processing_keepalive_does_not_reenter',4000,30)
 d.tick(5500);d.process(0);d.tick(6000);d.process(1);observe('new_processing_enters_once',0,0)
 d.reset();d.record(1,1);d.tick(5000);d.ota(1);observe('ota_has_its_own_entry',0,0)
 d.tick(6000);d.ota(0);observe('recording_survives_ota_overlay',5000,30)
 d.reset();d.process(1);limit=d.stale_limit();d.tick(1000+limit+1);d.process(1)
 results.append({'case':'keepalive_preserves_stale_protection','passed':d.processing_live()==1})
 d.tick(1000+2*limit+1);results.append({'case':'missing_keepalive_still_expires','passed':d.processing_live()==0})
 d.reset();d.tick(0xfffffff0);d.record(1,1);d.tick(0x20);observe('recording_clock_wraparound',48,0)
passed=(any(not x['passed'] for x in results) if a.expect_old_reset else all(x['passed'] for x in results))
r={'source':str(a.source),'actualFunctions':names,'actualTimingConstants':defines,'build':str(build),'scope':__doc__,'expectedOldFailure':a.expect_old_reset,'observations':results,'passed':passed}
if a.output:a.output.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(r,ensure_ascii=False,indent=2));raise SystemExit(0 if passed else 1)
