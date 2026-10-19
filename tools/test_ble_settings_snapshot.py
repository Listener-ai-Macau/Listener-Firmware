"""Execute the production BLE settings encoder, including ATT size boundaries."""
from pathlib import Path
import os, re, shutil, subprocess, tempfile
from test_power_source_filter import function

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'components/device_settings/device_settings.c').read_text(encoding='utf-8')
profile_source = (ROOT/'components/device_settings/device_key_profile.c').read_text(encoding='utf-8')
header = (ROOT/'components/device_settings/include/device_settings.h').read_text(encoding='utf-8')
snapshot = re.search(r'typedef struct \{[^}]+\} device_settings_snapshot_t;', header).group()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define DEVICE_SETTINGS_BLE_NAME_MAX_LEN 29
'''+snapshot+r'''
static device_settings_snapshot_t settings;
typedef struct { bool usb_power_present; int bat_chg_level, bat_std_level; } board_v2_power_input_snapshot_t;
typedef struct { bool standby_full, charging; } board_v2_charger_pin_decode_t;
static void device_settings_get_snapshot(device_settings_snapshot_t *out) { *out=settings; }
static void board_get_v2_power_input_snapshot(board_v2_power_input_snapshot_t *out) { out->usb_power_present=true; }
static board_v2_charger_pin_decode_t board_decode_charger_status_pins(int chg, int std) { (void)chg; (void)std; board_v2_charger_pin_decode_t out={0}; return out; }
static int ec11_rotation_control_get_action(void) { return 0; }
static const char *ec11_rotation_control_action_name(int action) { (void)action; return "system_volume"; }
typedef struct { uint32_t revision,length,crc; uint8_t data[4096]; } record_t;
static record_t saved={UINT32_MAX,4096,UINT32_MAX,{0}};
static uint32_t page_offset=0;
static const char *device_uid="ffffffffffff";
static void *lock=(void*)1;
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
static int xSemaphoreTake(void *m,unsigned t){(void)m;(void)t;return 1;}
static void xSemaphoreGive(void *m){(void)m;}
'''+function(profile_source,'device_key_profile_format')+function(source,'device_settings_format_ble_snapshot')+r'''
int main(void) {
 char out[512];
 assert(!device_settings_format_ble_snapshot(NULL,512));
 assert(!device_settings_format_ble_snapshot(out,0));
 assert(!device_settings_format_ble_snapshot(out,sizeof(out)));
 settings.status_led_brightness_percent=100;
 settings.key_led_brightness_percent=100;
 settings.ec11_led_brightness_percent=100;
 settings.edge_led_brightness_percent=100;
 settings.plugged_auto_shutdown_ms=UINT32_MAX;
 settings.settings_revision=UINT32_MAX;
 settings.plugged_low_power_idle_ms=UINT32_MAX;
 settings.battery_low_power_idle_ms=UINT32_MAX;
 settings.battery_auto_shutdown_ms=UINT32_MAX;
 settings.voice_auto_start_enabled=true;
 settings.voice_auto_stop_enabled=true;
 memset(settings.ble_name,'X',29); settings.ble_name[29]=0;
 assert(device_settings_format_ble_snapshot(out,sizeof(out)));
 assert(strlen(out)<512);
 assert(strstr(out,"schema=listener.device_settings.v1;settings_revision=4294967295;snapshot=1;"));
 assert(strstr(out,";vs=1;ve=1;"));
 assert(strstr(out,";up=1;"));
 struct { char out[64]; char sentinel[8]; } small;
 memset(&small,0x55,sizeof(small));
 assert(!device_settings_format_ble_snapshot(small.out,sizeof(small.out)));
 assert(small.out[0]==0);
 for(size_t i=0;i<sizeof(small.sentinel);i++) assert(small.sentinel[i]==0x55);
 assert(strstr(out,";ki=ffffffffffff;kr=4294967295;ks=4096;"));
 puts("PASS: actual settings + onboard key page encoders, maximum fields/name/profile, ATT bound and short-buffer sentinel");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='listener-settings-test-') as directory:
    tmp=Path(directory); (tmp/'test.c').write_text(code,encoding='utf-8')
    if os.name=='nt':
        vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
        vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
        (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n',encoding='utf-8')
        subprocess.run(['cmd','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
        exe=tmp/'test.exe'
    else:
        exe=tmp/'test'; subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Werror','test.c','-o',str(exe)],cwd=tmp,check=True)
    subprocess.run([str(exe)],check=True)
