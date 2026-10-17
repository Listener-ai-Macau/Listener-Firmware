"""Production DLE configuration lifetime vs optional effective-length events.

Simulated SDK edges follow HCI semantics: command success need not produce a
Data Length Change event when the effective peer-limited parameters stay equal.
No radio/latency result is inferred. --source retains the original failing C.
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
    request=function(source,'ble_hid_gap_request_audio_data_length')
    reset=function(source,'ble_hid_gap_set_connection_state')
    globals_=sorted(set(re.findall(r'\bs_[a-z0-9_]+',request+reset)))
    declarations=[]
    for name in globals_:
        actual=re.search(r'^static\s+(bool|uint\d+_t|TickType_t)\s+'+re.escape(name)+r'\b',source,re.M)
        declarations.append('static '+(actual.group(1) if actual else 'uint32_t')+' '+name+';')
    event_body=source.split('case BLE_GAP_EVENT_DATA_LEN_CHG:',1)[1].split(
        'case BLE_GAP_EVENT_ENC_CHANGE:',1)[0]
    observation='static int apply_observation(const struct ble_gap_event *event) {\n'+event_body+'\n}'
    phy_body=source.split('case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:',1)[1].split(
        'case BLE_GAP_EVENT_ADV_COMPLETE:',1)[0]
    phy_observation='static int apply_phy_observation(const struct ble_gap_event *event) {\n'+phy_body+'\n}'
    constants='\n'.join(re.findall(r'^#define BLE_HID_GAP_AUDIO_DATA_LEN_[^\n]+',source,re.M))
    refresh=(function(source,'ble_hid_gap_refresh_audio_data_length_readiness_locked')
             if 'static void ble_hid_gap_refresh_audio_data_length_readiness_locked(' in source else '')
    code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
typedef int esp_err_t;
typedef uint32_t TickType_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_EALREADY 3
#define BLE_HCI_LE_PHY_2M 2
#define DIAG_SRC_BLE_GAP 0
#define DIAG_GAP_PHY 0
#define DIAG_SEV_INFO 0
#define DIAG_SEV_WARN 0
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define TAG "test"
static void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
static void diag_log(int a,int b,int c,uint32_t d,uint32_t e,uint32_t f,uint32_t g) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGD(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
''' + constants + '\n' + '\n'.join(declarations) + r'''
typedef struct { bool connected, secure_connected; uint16_t conn_handle; } ble_hid_gap_connection_snapshot_t;
static unsigned now_tick, calls, failures, mode;
static int hci_result;
struct ble_gap_event { struct { uint16_t conn_handle,max_tx_octets,max_rx_octets,
                                      max_tx_time,max_rx_time; } data_len_chg;
    struct {int status;uint16_t conn_handle;uint8_t tx_phy,rx_phy;} phy_updated; };
static int apply_observation(const struct ble_gap_event *event);
static int apply_phy_observation(const struct ble_gap_event *event);
static const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }
static esp_err_t ble_hid_gap_schedule_active_connection_with_delay(unsigned delay, const char *reason) {
    (void)delay; (void)reason; return ESP_OK;
}
static TickType_t xTaskGetTickCount(void) { return now_tick; }
static ble_hid_gap_connection_snapshot_t ble_hid_gap_reconcile_connection_snapshot(const char *why) {
    (void)why; return (ble_hid_gap_connection_snapshot_t){s_ble_gap_connected!=0,
                      s_ble_gap_secure_connected!=0,(uint16_t)s_ble_gap_conn_handle};
}
static esp_err_t ble_hid_gap_request_audio_data_length(const char *policy);
static void ble_hid_gap_set_connection_state(bool connected, uint16_t conn_handle);
static int ble_gap_set_data_len(uint16_t handle, uint16_t octets, uint16_t time_us) {
    assert(handle==1 && octets==251 && time_us==1590); calls++;
    unsigned operation=mode; mode=0;
    if (operation==1) {
        /* The synchronous HCI call is still live after the old 750ms limit.
         * A second caller must not steal its command slot. */
        now_tick+=800;
        (void)ble_hid_gap_request_audio_data_length("concurrent while original HCI alive");
    } else if (operation==2) {
        /* Old HCI completion may return after disconnection and a new link
         * uses the same numeric handle. Its failure cannot erase new state. */
        ble_hid_gap_set_connection_state(false,BLE_HS_CONN_HANDLE_NONE);
        ble_hid_gap_set_connection_state(true,1); s_ble_gap_secure_connected=true;
        assert(ble_hid_gap_request_audio_data_length("new same-handle link")==ESP_OK);
        return 9;
    } else if (operation==3) {
        struct ble_gap_event event={.data_len_chg={1,251,251,340,340}};
        (void)apply_observation(&event);
        now_tick+=20;
        (void)ble_hid_gap_request_audio_data_length("caller after peer length observation, HCI still alive");
    } else if (operation>=4 && operation<=7) {
        /* Retained HCI has real 965 -> 340 transitions on the same link.
         * A newer downgrade must survive an older command's late success. */
        s_audio_tx_phy=2; s_audio_rx_phy=2;
        struct ble_gap_event event={.data_len_chg={1,251,251,965,965}};
        (void)apply_observation(&event);
        event.data_len_chg.max_tx_time=340;event.data_len_chg.max_rx_time=340;
        if(operation==7) {
            struct ble_gap_event phy={.phy_updated={0,1,1,1}};
            (void)apply_phy_observation(&phy);
        } else {(void)apply_observation(&event);}
        if(operation==5)return 9;
        if(operation==6) {
            event.data_len_chg.max_tx_time=965;event.data_len_chg.max_rx_time=965;
            (void)apply_observation(&event);
        }
    }
    return hci_result;
}
''' + reset + '\n' + function(source,'ble_hid_gap_data_length_ready_for_phy') + '\n' + refresh + '\n' + observation + '\n' + phy_observation + '\n' + request + r'''
static void check(bool value, const char *message) {
    if(!value) {fprintf(stderr,"FAIL: %s\n",message);failures++;}
}
static void init(void) {
    ble_hid_gap_set_connection_state(false,BLE_HS_CONN_HANDLE_NONE);
    ble_hid_gap_set_connection_state(true,1);s_ble_gap_secure_connected=true;
    now_tick=1000;calls=0;mode=0;hci_result=0;
}
int main(void) {
    init();
    assert(ble_hid_gap_request_audio_data_length("initial request")==ESP_OK);
    for(unsigned i=0;i<5;i++) {now_tick+=8200;
        assert(ble_hid_gap_request_audio_data_length("same peer-limited tuple, no change event")==ESP_OK);}
    printf("unchanged effective tuple: HCI commands=%u\n",calls);
    check(calls==1,"successful configuration is not retried waiting for an optional change event");
    check(!s_audio_data_length_request_pending,"command complete must release its own pending slot");
    check(!s_audio_data_length_ready,"command complete does not invent effective full-packet readiness");
    init();mode=1;assert(ble_hid_gap_request_audio_data_length("original HCI request")==ESP_OK);
    check(calls==1,"live synchronous HCI call cannot expire into a concurrent duplicate");
    init();mode=3;assert(ble_hid_gap_request_audio_data_length("in-flight peer observation")==ESP_OK);
    check(calls==1,"length change observation cannot release the live HCI command slot");
    check(!s_audio_data_length_ready,"observed 340us tuple remains below existing readiness policy");
    init();hci_result=9;
    check(ble_hid_gap_request_audio_data_length("failed command")!=ESP_OK,"HCI error is preserved");
    check(!s_audio_data_length_request_pending,"failed command releases pending slot");
    hci_result=0;assert(ble_hid_gap_request_audio_data_length("retry after actual failure")==ESP_OK);
    now_tick+=900;assert(ble_hid_gap_request_audio_data_length("successful retry, no event")==ESP_OK);
    check(calls==2,"only actual command failure requires another configuration attempt");
    init();assert(ble_hid_gap_request_audio_data_length("first link")==ESP_OK);
    ble_hid_gap_set_connection_state(false,BLE_HS_CONN_HANDLE_NONE);
    ble_hid_gap_set_connection_state(true,1);s_ble_gap_secure_connected=true;
    assert(ble_hid_gap_request_audio_data_length("reused numeric handle")==ESP_OK);
    check(calls==2,"new link must configure even with reused numeric connection handle");
    init();mode=2;(void)ble_hid_gap_request_audio_data_length("old link call");
    now_tick+=900;assert(ble_hid_gap_request_audio_data_length("after old return")==ESP_OK);
    check(calls==2,"old return cannot clear new same-handle configuration ownership");
    init();s_ble_gap_secure_connected=false;
    check(ble_hid_gap_request_audio_data_length("unencrypted")!=ESP_OK && calls==0,
          "unencrypted link never receives a configuration request");
    init();s_audio_data_length_ready=true;
    assert(ble_hid_gap_request_audio_data_length("observed sufficient link")==ESP_OK);
    check(calls==0,"actual sufficient effective length still avoids needless HCI");
    init();
    assert(ble_hid_gap_request_audio_data_length("initial peer-limited link")==ESP_OK);
    s_audio_tx_phy=2;s_audio_rx_phy=2;
    struct ble_gap_event event={.data_len_chg={1,251,251,965,965}};
    (void)apply_observation(&event);
    check(s_audio_data_length_ready,"actual full-policy observation is retained");
    event.data_len_chg.max_tx_time=340;event.data_len_chg.max_rx_time=340;
    (void)apply_observation(&event);
    assert(ble_hid_gap_request_audio_data_length("new actual same-link downgrade")==ESP_OK);
    check(calls==2,"past command acceptance cannot suppress restoration after a new actual downgrade");
    for(unsigned i=0;i<4;i++) {
        (void)apply_observation(&event);now_tick+=8200;
        assert(ble_hid_gap_request_audio_data_length("unchanged downgraded tuple")==ESP_OK);
    }
    check(calls==2,"unchanged low tuple must not re-arm the configuration repeatedly");
    init();mode=4;
    assert(ble_hid_gap_request_audio_data_length("old call across a fresh downgrade")==ESP_OK);
    assert(ble_hid_gap_request_audio_data_length("request for the newer observation")==ESP_OK);
    check(calls==2,"new downgrade cannot be overwritten by an older in-flight HCI success");
    init();mode=5;
    check(ble_hid_gap_request_audio_data_length("new observation and real HCI error")!=ESP_OK,
          "new observation cannot hide an actual HCI command error");
    check(calls==1,"actual HCI errors do not enter the observation coalescing loop");
    assert(ble_hid_gap_request_audio_data_length("later caller after actual error")==ESP_OK);
    check(calls==2,"failed fresh observation request remains retryable");
    init();mode=6;
    assert(ble_hid_gap_request_audio_data_length("observed recovery before old completion")==ESP_OK);
    check(calls==1 && s_audio_data_length_ready,"already recovered actual state needs no follow-up");
    init();mode=7;
    assert(ble_hid_gap_request_audio_data_length("PHY-only readiness loss while HCI alive")==ESP_OK);
    check(calls==2,"PHY-only loss of readiness shares the same command observation boundary");
    init();assert(ble_hid_gap_request_audio_data_length("configure before PHY observations")==ESP_OK);
    event.data_len_chg.max_tx_time=965;event.data_len_chg.max_rx_time=965;
    (void)apply_observation(&event);
    struct ble_gap_event phy={.phy_updated={0,1,2,2}};
    (void)apply_phy_observation(&phy);
    check(s_audio_data_length_ready && calls==1,"actual 2M readiness does not reload DLE");
    phy.phy_updated.tx_phy=1;phy.phy_updated.rx_phy=1;
    (void)apply_phy_observation(&phy);
    check(calls==2,"actual PHY-only downgrade may issue one fresh configuration");
    (void)apply_phy_observation(&phy);
    check(calls==2,"unchanged PHY observation does not repeatedly reconfigure");
    /* This function is only needed by old source's incorrect elapsed policy. */
    (void)xTaskGetTickCount();
    printf("DLE command ownership failures=%u\n",failures);return failures?1:0;
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-dle-owner-',ignore_cleanup_errors=True) as directory:
        tmp=Path(directory);(tmp/'test.c').write_text(code,encoding='utf-8')
        executable=tmp/('test.exe' if os.name=='nt' else 'test')
        if os.name=='nt':
            vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
            vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
                                      'cl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n',encoding='utf-8')
            subprocess.run(['cmd.exe','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
        else:
            subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Werror','test.c','-o',str(executable)],cwd=tmp,check=True)
        subprocess.run([str(executable)],cwd=tmp,check=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c')
    verify(parser.parse_args().source.read_text(encoding='utf-8'))
