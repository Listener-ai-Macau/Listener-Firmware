"""Run the actual firmware profile C code with an isolated NVS/RTOS fixture.
No hardware, Bluetooth, installed Type, pairing or user settings are touched.
"""
from pathlib import Path
import ctypes,json,os,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
IDF=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/esp-idf')))
BUILD=Path(tempfile.mkdtemp(prefix='listener-key-profile-'))
def put(name,text):
    p=BUILD/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8');return p
put('esp_err.h','#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NO_MEM 3\n#define ESP_FAIL 4\n')
put('device_settings.h','#pragma once\n#include "esp_err.h"\nesp_err_t device_settings_run_flash_job(esp_err_t (*job)(void*),void*);\n')
put('freertos/FreeRTOS.h','#pragma once\n#include <stdint.h>\ntypedef uint32_t TickType_t;\n#define portMAX_DELAY 0xffffffff\n#define pdTRUE 1\n#define pdMS_TO_TICKS(v) (v)\n')
put('freertos/semphr.h','#pragma once\ntypedef int StaticSemaphore_t;\ntypedef void* SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*);\nint xSemaphoreTake(SemaphoreHandle_t,unsigned);\nint xSemaphoreGive(SemaphoreHandle_t);\n')
put('freertos/task.h','#pragma once\n#include "FreeRTOS.h"\nTickType_t xTaskGetTickCount(void);\n')
put('esp_mac.h','#pragma once\n#include <stdint.h>\n#include "esp_err.h"\nesp_err_t esp_efuse_mac_get_default(uint8_t*);\n')
put('nvs.h','#pragma once\n#include <stddef.h>\n#include "esp_err.h"\ntypedef int nvs_handle_t;\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\nesp_err_t nvs_open(const char*,int,nvs_handle_t*);\nesp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);\nesp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);\nesp_err_t nvs_commit(nvs_handle_t);\nvoid nvs_close(nvs_handle_t);\n')
# Header lookup for nested #include must use the fixture first.
source=(ROOT/'components/device_settings/device_key_profile.c').read_text().replace('#include "device_settings.h"','#include <device_settings.h>')
put('actual_profile.c',source)
put('fixture.c',r"""
#include "actual_profile.c"
static unsigned char banks[2][8192],write_buffer[8192];static size_t sizes[2],write_size;static int board,fail_commit;static TickType_t ticks;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*p){return p;}
int xSemaphoreTake(SemaphoreHandle_t p,unsigned n){return 1;}
int xSemaphoreGive(SemaphoreHandle_t p){return 1;}
TickType_t xTaskGetTickCount(void){return ticks;}
esp_err_t esp_efuse_mac_get_default(uint8_t*p){memset(p,0,6);p[5]=(uint8_t)(board+1);return 0;}
esp_err_t nvs_open(const char*a,int b,nvs_handle_t*c){*c=1;return 0;}
esp_err_t nvs_get_blob(nvs_handle_t a,const char*b,void*c,size_t*d){if(!sizes[board]||*d<sizes[board])return 1;memcpy(c,banks[board],sizes[board]);*d=sizes[board];return 0;}
esp_err_t nvs_set_blob(nvs_handle_t a,const char*b,const void*c,size_t d){memcpy(write_buffer,c,d);write_size=d;return 0;}
esp_err_t nvs_commit(nvs_handle_t a){if(fail_commit)return 4;memcpy(banks[board],write_buffer,write_size);sizes[board]=write_size;return 0;}
void nvs_close(nvs_handle_t a){}
esp_err_t device_settings_run_flash_job(esp_err_t(*job)(void*),void*c){return job(c);}
__declspec(dllexport) void select_board(int b){board=b;lock=NULL;memset(&saved,0,sizeof(saved));memset(&pending,0,sizeof(pending));memset(live_hid,0,sizeof(live_hid));uploading=false;transaction=received=page_offset=0;device_key_profile_init();}
__declspec(dllexport) int command(const char*c){return device_key_profile_command(c);}
__declspec(dllexport) int status(char*p,unsigned n){return device_key_profile_format(p,n,' ');}
__declspec(dllexport) void fail(int f){fail_commit=f;}
__declspec(dllexport) void advance(unsigned ms){ticks+=ms;}
__declspec(dllexport) unsigned native(unsigned k,unsigned g){uint8_t u=0,m=0;uint8_t kind=device_key_profile_hid(k,g,&u,&m);return (kind<<16)|(m<<8)|u;}
""")
vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat')
cmd=put('build.cmd',f'@echo off\ncall "{vc}" -arch=x64 -host_arch=x64 >nul\ncd /d "{BUILD}"\ncl /nologo /std:c11 /LD /DCJSON_HIDE_SYMBOLS /I"{BUILD}" /I"{ROOT / "components/device_settings/include"}" /I"{IDF / "components/json/cJSON"}" fixture.c "{IDF / "components/json/cJSON/cJSON.c"}" /link /OUT:profile.dll\nexit /b %errorlevel%\n')
result=subprocess.run(['cmd.exe','/d','/c',str(cmd)],capture_output=True,text=True)
if result.returncode:raise RuntimeError(result.stdout+result.stderr)
dll=ctypes.CDLL(str(BUILD/'profile.dll'));dll.command.argtypes=[ctypes.c_char_p];dll.status.argtypes=[ctypes.c_char_p,ctypes.c_uint];dll.native.restype=ctypes.c_uint
checks=[]
def check(name,condition):
    assert condition,name
    checks.append(name)
def command(text):
    check('bounded_command',len('DEVICE:KEY '+text)+1<=63)
    return dll.command(text.encode())
def status():
    buf=ctypes.create_string_buffer(224);assert dll.status(buf,len(buf));return dict(v.split('=',1) for v in buf.value.decode().split())
def upload(payload,rev,tx=0x1234,commit=True):
    begin=f'B {rev:08x} {len(payload):04x} {zlib.crc32(payload):08x} {tx:08x}'
    assert command(begin)==0
    check('same_begin_replay_before_chunks_safe',command(begin)==0)
    for i in range(0,len(payload),16):
        chunk=f'D {tx:08x} {i:04x} {payload[i:i+16].hex()}'
        assert command(chunk)==0
        check('identical_chunk_replay_safe',command(chunk)==0)
    return command(f'C {tx:08x}') if commit else 0
p=json.dumps({'schema':1,'hid':[[2,1,6]]+[[1,0,0]]*14,'deviceCustomKeys':{'key1':{'action':'copyShortcut'}}},separators=(',',':')).encode()
dll.select_board(0);check('fresh_A_defaults',status()['ks']=='0')
upload(p,1);check('A_commit_confirmed',status()['kr']=='2');check('A_native_exact',dll.native(1,0)==0x020106)
dll.select_board(1);check('B_does_not_inherit_A',status()['ks']=='0' and dll.native(1,0)==0)
q=p.replace(b'[2,1,6]',b'[2,1,25]');upload(q,1);check('B_own_native',dll.native(1,0)==0x020119)
dll.select_board(0);check('A_power_cycle_persistent',status()['kr']=='2' and dll.native(1,0)==0x020106)
check('wrong_revision_rejected',command(f'B 00000001 {len(q):04x} {zlib.crc32(q):08x} 00000033')!=0)
upload(q,2,commit=False);check('incomplete_upload_not_live',status()['kr']=='2' and dll.native(1,0)==0x020106)
check('foreign_transaction_rejected',command('D 00005678 0000 00')!=0)
dll.fail(1);check('NVS_failure_reported',command('C 00001234')!=0);check('NVS_failure_keeps_live_A',status()['kr']=='2' and dll.native(1,0)==0x020106)
dll.fail(0);command('X 00001234');dll.select_board(0);check('NVS_failure_keeps_persistent_A',status()['kr']=='2' and dll.native(1,0)==0x020106)
# A stalled client cannot permanently block another edit; it also cannot steal a live transaction.
command(f'B 00000002 {len(q):04x} {zlib.crc32(q):08x} 00001234');check('live_upload_not_stolen',command(f'B 00000002 {len(q):04x} {zlib.crc32(q):08x} 00005678')!=0)
dll.advance(30001);check('abandoned_upload_expires',command(f'B 00000002 {len(q):04x} {zlib.crc32(q):08x} 00005678')==0)
# Persisted content/page CRC, including beyond the first 64 bytes.
command('X 00005678');assembled=b''
while len(assembled)<len(p):
    command(f'R {len(assembled):04x}');page=status();check('page_offset',int(page['ko'])==len(assembled));assembled+=bytes.fromhex(page['kd'])
check('all_pages_exact',assembled==p)
big=json.loads(p);big['deviceCustomKeys']['key4']={'action':'disabled','pasteTemplate':''}
encode=lambda:json.dumps(big,separators=(',',':')).encode()
big['deviceCustomKeys']['key4']['pasteTemplate']='x'*(4096-len(encode()))
maximum=encode();check('capacity_fixture_exactly4096',len(maximum)==4096)
upload(maximum,2);dll.select_board(0)
check('maximum_profile_reloads_from_own_nvs',status()['ks']=='4096' and status()['kr']=='3')
assembled=b''
while len(assembled)<len(maximum):
    assert command(f'R {len(assembled):04x}')==0
    assembled+=bytes.fromhex(status()['kd'])
check('maximum_profile_all_bytes_crc_exact_after_reboot',assembled==maximum)
print(json.dumps({'source':str(ROOT/'components/device_settings/device_key_profile.c'),'checks':list(dict.fromkeys(checks)),'result':'PASS','build':str(BUILD)},indent=2))
