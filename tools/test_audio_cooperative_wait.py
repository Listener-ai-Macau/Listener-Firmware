"""Run the real optional blocker and real preroll branch with virtual RTOS/timer edges.

The saved packet-size sequence reproduces tick inflation without replaying voice,
device control or editor input. Virtual elapsed time is not a radio benchmark or
proof that the physical IDLE0 task actually ran.
"""
from pathlib import Path
import argparse,json,os,subprocess,tempfile,shutil,re
from test_power_source_filter import ROOT, function

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=ROOT/'ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c')
parser.add_argument('--saved-events',type=Path,required=True)
parser.add_argument('--legacy-preroll-only',action='store_true')
parser.add_argument('--case')
parser.add_argument('--blocker-source',type=Path,default=ROOT/'ports/esp32/ble_audio_stream/ble_audio_cooperative_wait.c')
args=parser.parse_args()
source=args.source.read_text(encoding='utf-8')
sizes=[]
for line in args.saved_events.open(encoding='utf-8'):
    event=json.loads(line)
    if event.get('sessionId')==1377173510 and event.get('boundary')=='ble_receive' and event.get('packetType')=='AudioData':
        sizes.append((event['packetSequence'],event['pcmBytes']))
sizes.sort();prefix=[];total=0
for sequence,count in sizes:
    assert sequence==len(prefix)
    prefix.append(count);total+=count
    if total>=64000:break
assert len(prefix)==207 and total==64000
constants='\n'.join(re.findall(r'^#define BLE_AUDIO_STREAM_(?:PREROLL_IDLE_BLOCK_PCM_BYTES|COOPERATIVE_IDLE_BLOCK_MS|AUDIO_PACKET_IDLE_BLOCK_MS)[^\n]+',source,re.M))
headers={
'freertos/FreeRTOS.h':r'''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdTRUE 1
#define pdFALSE 0
#define portTICK_PERIOD_MS 10u
#define pdMS_TO_TICKS(x) ((TickType_t)((x)/10u))
''',
'freertos/task.h':r'''#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum {eRunning,eBlocked} eTaskState;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
eTaskState eTaskGetState(TaskHandle_t);
void vTaskDelay(TickType_t);
''',
'freertos/semphr.h':r'''#pragma once
#include "FreeRTOS.h"
struct TestSemaphore;typedef struct TestSemaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t,TickType_t);
void vSemaphoreDelete(SemaphoreHandle_t);
''',
'esp_timer.h':r'''#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_TIMER_TASK 0
struct TestTimer;typedef struct TestTimer *esp_timer_handle_t;
typedef struct {void(*callback)(void*);void *arg;int dispatch_method;const char *name;bool skip_unhandled_events;} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);
esp_err_t esp_timer_start_once(esp_timer_handle_t,uint64_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
int64_t esp_timer_get_time(void);
const char *esp_err_to_name(esp_err_t);
''',
'esp_log.h':r'''#pragma once
void test_log(const char*,const char*,...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
'''}
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "ble_audio_cooperative_wait.h"
#include "denzic_audio_transport_v1.h"
struct TestSemaphore {unsigned token;bool deleted;};
struct TestTimer {esp_timer_create_args_t args;bool armed;uint64_t due;};
static struct TestSemaphore semaphore;
static struct TestTimer timer;
static uint64_t now_us,requested_us;
static unsigned allocations,deletions,starts,stops,blocks,tick_blocks,timer_callbacks,callbacks_while_blocked;
static bool semaphore_failure,timer_failure,start_failure,drop_callback,blocked,early_callback;
static TaskHandle_t caller=(TaskHandle_t)(uintptr_t)1;
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return caller;}
eTaskState eTaskGetState(TaskHandle_t owner) {(void)owner;return blocked?eBlocked:eRunning;}
void test_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
const char *esp_err_to_name(esp_err_t code) {(void)code;return "fixture";}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {allocations++;return semaphore_failure?NULL:&semaphore;}
void vSemaphoreDelete(SemaphoreHandle_t sem) {assert(sem && !timer.armed);sem->deleted=true;deletions++;}
int xSemaphoreGive(SemaphoreHandle_t sem) {assert(sem && !sem->deleted);sem->token=1;return pdTRUE;}
static void dispatch(void) {
    timer_callbacks++;if(blocked)callbacks_while_blocked++;
    timer.armed=false;timer.args.callback(timer.args.arg);
}
int xSemaphoreTake(SemaphoreHandle_t sem,TickType_t ticks) {
    assert(sem && !sem->deleted);
    if(sem->token){sem->token=0;return pdTRUE;}
    if(!ticks)return pdFALSE;
    blocked=true;blocks++;
    if(timer.armed && !drop_callback && timer.due<=now_us+(uint64_t)ticks*10000u) {
        now_us=timer.due;dispatch();
    } else now_us+=(uint64_t)ticks*10000u;
    blocked=false;
    if(sem->token){sem->token=0;return pdTRUE;}return pdFALSE;
}
void vTaskDelay(TickType_t ticks) {assert(ticks>0);tick_blocks++;blocks++;now_us+=(uint64_t)ticks*10000u;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *handle) {
    if(timer_failure)return ESP_ERR_NO_MEM;
    assert(args->dispatch_method==ESP_TIMER_TASK && args->skip_unhandled_events);
    timer.args=*args;*handle=&timer;return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t handle,uint64_t us) {
    assert(handle==&timer);starts++;
    if(start_failure)return ESP_ERR_NO_MEM;
    assert(!timer.armed);timer.armed=true;timer.due=now_us+us;requested_us+=us;
    if(early_callback){now_us=timer.due;dispatch();}
    return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t handle) {assert(handle==&timer);timer.armed=false;stops++;return ESP_OK;}
int64_t esp_timer_get_time(void) {return (int64_t)now_us;}
typedef uint32_t listener_audio_packet_type_t;
#define LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA 1u
enum { BLE_AUDIO_WAIT_MEDIA, BLE_AUDIO_WAIT_TRANSPORT, BLE_AUDIO_WAIT_PREROLL };
static void ble_audio_stream_stats_block_wait(uint32_t session, int kind, int64_t began) {
    (void)session; (void)kind; (void)began;
}
static denzic_audio_transport_v1_pacing_t s_audio_pacing;
static uint32_t s_audio_pacing_cooperative_credit_ticks;
static uint32_t s_audio_pacing_burst_remaining_pcm_bytes;
static struct {uint32_t audio_preroll_burst_pcm_bytes;} s_session_stats;
static void watchdog_platform_feed_current_task(void) {}
static void ble_audio_stream_stats_audio_pace_wait(uint32_t session,uint32_t ticks) {(void)session;(void)ticks;}
static void ble_audio_stream_delay_ms(uint32_t ms) {requested_us+=(uint64_t)ms*1000u;vTaskDelay((ms+9u)/10u);}
'''+constants+'\n'+function(source,'ble_audio_stream_notify_success_delay')+r'''
int main(int argc,char **argv) {
    assert(argc==2);const char *scenario=argv[1];
    semaphore_failure=!strcmp(scenario,"semaphore-failure");
    timer_failure=!strcmp(scenario,"timer-create-failure");
    ble_audio_cooperative_wait_init();
    (void)ble_audio_stream_delay_ms;
    if(!strcmp(scenario,"preroll")) {
        const uint16_t saved[] = {SAVED_PACKET_SIZES};
        s_audio_pacing_burst_remaining_pcm_bytes=64000;
        denzic_audio_transport_v1_pacing_init(&s_audio_pacing);
        for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++)
            ble_audio_stream_notify_success_delay(1u,42u,saved[i]);
        printf("saved_preroll_packets=207 pcm_bytes=%u blocked_calls=%u requested_us=%llu virtual_elapsed_us=%llu precise_callbacks=%u\n",
            s_session_stats.audio_preroll_burst_pcm_bytes,blocks,(unsigned long long)requested_us,(unsigned long long)now_us,timer_callbacks);
        assert(s_audio_pacing_burst_remaining_pcm_bytes==0 && s_session_stats.audio_preroll_burst_pcm_bytes==64000);
        assert(blocks==207 && callbacks_while_blocked==timer_callbacks);
        if(now_us!=requested_us || starts!=207 || tick_blocks!=0) {
            fprintf(stderr,"FAIL sub-tick requests inflated into whole tick waits\n");return 1;
        }
    } else if(semaphore_failure || timer_failure) {
        ble_audio_cooperative_wait_ms(1);ble_audio_cooperative_wait_init();ble_audio_cooperative_wait_ms(2);
        assert(now_us==20000 && tick_blocks==2 && starts==0 && allocations==1);
        assert(deletions==(timer_failure?1u:0u));
    } else if(!strcmp(scenario,"start-failure") || !strcmp(scenario,"timeout")) {
        start_failure=!strcmp(scenario,"start-failure");drop_callback=!start_failure;
        ble_audio_cooperative_wait_ms(1);uint64_t failed_at=now_us;
        assert(starts==1 && stops==1 && tick_blocks==1);
        dispatch(); /* A retired callback remains safe, but grants no new wait. */
        start_failure=drop_callback=false;
        ble_audio_cooperative_wait_ms(1);
        assert(starts==1 && now_us==failed_at+10000u && tick_blocks==2 && deletions==0);
    } else if(!strcmp(scenario,"early-callback")) {
        early_callback=true;ble_audio_cooperative_wait_ms(1);
        if(blocks==0 || tick_blocks!=1) {
            fprintf(stderr,"FAIL early timer token bypassed the required real Blocked interval\n");return 1;
        }
        unsigned previous_starts=starts;ble_audio_cooperative_wait_ms(1);
        assert(starts==previous_starts && tick_blocks==2);
    } else if(!strcmp(scenario,"stale-token")) {
        semaphore.token=1;ble_audio_cooperative_wait_ms(1);ble_audio_cooperative_wait_ms(1);
        assert(starts==0 && stops==1 && tick_blocks==2 && now_us==20000);
    } else if(!strcmp(scenario,"foreign-owner")) {
        caller=(TaskHandle_t)(uintptr_t)2;ble_audio_cooperative_wait_init();ble_audio_cooperative_wait_ms(1);
        assert(starts==0 && tick_blocks==1 && allocations==1);
        caller=(TaskHandle_t)(uintptr_t)1;ble_audio_cooperative_wait_ms(1);
        assert(starts==1 && tick_blocks==1 && now_us==11000);
    } else if(!strcmp(scenario,"cross-boundary")) {
        denzic_audio_transport_v1_pacing_init(&s_audio_pacing);s_audio_pacing_burst_remaining_pcm_bytes=100;
        ble_audio_stream_notify_success_delay(1u,42u,480u);
        assert(s_session_stats.audio_preroll_burst_pcm_bytes==100 && s_audio_pacing_burst_remaining_pcm_bytes==0);
        assert(starts==0 && tick_blocks==1 && s_audio_pacing_cooperative_credit_ticks==1);
    } else if(!strcmp(scenario,"coarse-and-zero")) {
        ble_audio_cooperative_wait_ms(0);assert(now_us==0 && starts==0 && blocks==0);
        ble_audio_cooperative_wait_ms(10);assert(now_us==10000 && tick_blocks==1 && starts==0);
    } else assert(false);
    printf("PASS %s\n",scenario);return 0;
}
'''
fixture=fixture.replace('SAVED_PACKET_SIZES',','.join(str(n) for n in prefix))
component=ROOT/'ports/esp32/ble_audio_stream';platform=ROOT/'third_party/denzic-platform/audio/embedded/c'
with tempfile.TemporaryDirectory(prefix='listener-cooperative-wait-',ignore_cleanup_errors=True) as folder:
    folder=Path(folder)
    for name,body in headers.items():
        path=folder/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(body,encoding='utf-8')
    (folder/'test.c').write_text(fixture,encoding='utf-8')
    sources=['test.c',str(args.blocker_source),str(platform/'src/denzic_audio_transport_v1.c')]
    includes=[folder,component,platform/'include']
    exe=folder/('test.exe' if os.name=='nt' else 'test')
    if os.name=='nt':
        vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
        vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
        (folder/'build.cmd').write_text('@echo off\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX '+
            ' '.join('/I"'+str(x)+'"' for x in includes)+' '+' '.join('"'+x+'"' for x in sources)+' /Fe:test.exe\n',encoding='utf-8')
        subprocess.run(['cmd.exe','/d','/c',str(folder/'build.cmd')],cwd=folder,check=True)
    else:
        subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Werror',*[f'-I{x}' for x in includes],*sources,'-o',str(exe)],cwd=folder,check=True)
    cases=[args.case] if args.case else ['preroll'] if args.legacy_preroll_only else ['preroll','semaphore-failure','timer-create-failure','start-failure','timeout','early-callback','stale-token','foreign-owner','cross-boundary','coarse-and-zero']
    for case in cases:subprocess.run([str(exe),case],cwd=folder,check=True,timeout=5)
