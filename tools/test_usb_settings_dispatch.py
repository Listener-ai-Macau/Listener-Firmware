"""Compile actual USB dispatcher/KEY ACK functions with external subsystem stubs.
Confirms query/staging side effects and wire shape; not real USB/BLE reliability.
"""
import argparse,ctypes,json,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'ports/esp32/ble_hid/ble_hid.c');p.add_argument('--settings',type=Path,default=ROOT/'components/device_settings/device_settings.c');p.add_argument('--expect-before',action='store_true');p.add_argument('--output',type=Path);a=p.parse_args()
source=a.source.read_text(encoding='utf-8');settings=a.settings.read_text(encoding='utf-8');build=Path(tempfile.mkdtemp(prefix='listener-usb-settings-'))
def function(text,name):
 m=re.search(r'^(?:static\s+)?[\w *]+\b'+name+r'\([^;]*?\)\s*\{',text,re.M);assert m,name
 pos=text.index('{',m.start());end=pos+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]+'\n'
actual='\n'.join(function(source,n) for n in ['ble_hid_usb_command_matches','ble_hid_usb_command_starts_with','ble_hid_usb_command_is_apply_ble_name','ble_hid_dispatch_usb_command_line'])
has_key_ack='static void device_settings_print_key_ack(' in settings
ack=function(settings,'device_settings_print_key_ack') if has_key_ack else ''
fixture=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 1
#define POWER_MANAGER_BLOCKER_USB_COMMAND 1
#define POWER_MANAGER_BLOCKER_FLASH_WRITE 2
#define POWER_MANAGER_BLOCKER_DIAG_EXPORT 4
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static unsigned applies;
static bool ble_hid_usb_command_records_activity(const char*s){return false;}
static bool ble_hid_usb_command_is_fast_idle_diagnostic_key(const char*s){return false;}
static bool ble_hid_usb_command_is_passive_query(const char*s){return false;}
static void power_manager_record_activity(const char*s){}
static void power_manager_set_blocker(unsigned b,bool active){}
static bool power_manager_consume_usb_command(const char*s){return false;}
static bool device_settings_consume_usb_command(const char*s){if(*s=='~')s++;return strncmp(s,"DEVICE:",7)==0;}
static void status_led_apply_device_settings(void){applies++;}
static int ble_hid_gap_apply_pending_ble_name(void){return 0;}
static const char*esp_err_to_name(int e){return "controlled_error";}
static bool status_led_consume_usb_command(const char*s){return false;}
static bool board_consume_usb_command(const char*s){return false;}
static bool watchdog_platform_consume_usb_command(const char*s){return false;}
static bool boot_safety_consume_usb_command(const char*s){return false;}
static bool firmware_ota_consume_usb_command(const char*s){return false;}
static bool diag_log_consume_usb_command(const char*s){return false;}
static bool ec11_rotation_control_consume_command(const char*s,const char*t,esp_err_t*r){return false;}
static void ble_firmware_ota_log_gatt_state(void){}
static void ble_hid_log_dis_gatt_state(void){}
static void ble_diag_log_log_gatt_state(void){}
static void ble_hid_gap_print_status(void){}
static void ble_hid_dispatch_voice_recording_command(const char*s){}
static bool(*s_usb_command_handler)(const char*,esp_err_t*);
static char captured[4096];
static int capture_printf(const char*format,...){va_list ap;va_start(ap,format);int n=vsnprintf(captured,sizeof(captured),format,ap);va_end(ap);return n;}
#define printf capture_printf
#define fflush(file) (0)
static bool device_key_profile_format(char*out,size_t size,char sep){return snprintf(out,size," ki=98c377b4e88c kr=13 ks=3999 kc=0123abcd ko=48 kd=1234")<(int)size;}
static void device_settings_print_error(const char*k,const char*r){printf("~DEVICE:ERROR key=%s reason=%s\n",k,r);}
'''+actual+ack+r'''
__declspec(dllexport) unsigned dispatch(const char*line){applies=0;ble_hid_dispatch_usb_command_line(line);return applies;}
'''+('''__declspec(dllexport) const char*ack(void){captured[0]=0;device_settings_print_key_ack();return captured;}''' if has_key_ack else '')
(build/'fixture.c').write_text(fixture,encoding='utf-8');vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat')
(build/'build.cmd').write_text(f'@echo off\ncall "{vc}" -arch=x64 -host_arch=x64 >nul\ncd /d "{build}"\ncl /nologo /std:c11 /LD fixture.c /link /OUT:settings.dll\nexit /b %errorlevel%\n',encoding='utf-8')
cp=subprocess.run(['cmd.exe','/d','/c',str(build/'build.cmd')],capture_output=True,text=True);assert cp.returncode==0,cp.stdout+cp.stderr
d=ctypes.CDLL(str(build/'settings.dll'));d.dispatch.argtypes=[ctypes.c_char_p];d.dispatch.restype=ctypes.c_uint;cases=[]
for command in ['~DEVICE:SETTINGS','~DEVICE:STATUS','~DEVICE:KEY R 0000','~DEVICE:KEY B 0000000d 0f9f 1234abcd 11112222','~DEVICE:KEY D 11112222 0000 00','~DEVICE:KEY C 11112222','~DEVICE:KEY X 11112222','~DEVICE:HELP']:
 count=d.dispatch(command.encode());cases.append({'command':command.split(' ',1)[0],'ledApplies':count,'passed':count==0})
for command in ['~DEVICE:SET ls=45 rev=70','~DEVICE:RESET','~DEVICE:APPLY_BLE_NAME']:
 count=d.dispatch(command.encode());cases.append({'command':command.split(' ',1)[0],'ledApplies':count,'passed':count==1})
r={'source':str(a.source),'scope':__doc__,'cases':cases,'hasCompactKeyAck':has_key_ack}
if has_key_ack:
 d.ack.restype=ctypes.c_char_p;line=d.ack().decode();r['ack']=line;r['ackBytes']=len(line.encode());assert line.endswith('\n') and ' result=OK ' in line and ' ki=98c377b4e88c kr=13 ks=3999 kc=0123abcd ko=48 kd=1234' in line and len(line)<300
r['legacyFullSettingsAck']=bool(re.search(r'if\s*\(ret\s*==\s*ESP_OK\)\s*device_settings_print_status\("OK"\)',settings))
r['passed']=any(not c['passed'] for c in cases) if a.expect_before else all(c['passed'] for c in cases) and not has_key_ack and r['legacyFullSettingsAck']
if a.output:a.output.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(r,ensure_ascii=False));raise SystemExit(0 if r['passed'] else 1)
