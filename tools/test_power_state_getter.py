from pathlib import Path
import ctypes,json,subprocess,tempfile,time,re

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'components/power_manager/power_manager.c').read_text()
start=source.index('power_manager_state_t power_manager_get_state(void)')
opening=source.index('{',start);depth=1;end=opening+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
actual=source[start:end]
state=re.search(r'^static .*\bs_state\s*=.*;$',source,re.M).group()
build=Path(tempfile.mkdtemp(prefix='listener-power-state-'))
c='''#include <windows.h>
#include <stdatomic.h>
typedef int power_manager_state_t;
#define POWER_MANAGER_STATE_ACTIVE 0
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
static void *s_mutex=(void*)1;
static int mutex_calls;
static int xSemaphoreTake(void*p,unsigned t){mutex_calls++;Sleep(30);return 0;}
static void xSemaphoreGive(void*p){}
'''+state+'\n'+actual+'''
__declspec(dllexport) int read_state(int value){s_state=value;return power_manager_get_state();}
__declspec(dllexport) int calls(void){return mutex_calls;}
'''
(build/'actual.c').write_text(c)
vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
(build/'build.cmd').write_text(f'@echo off\ncall "{vc}" -arch=x64 -host_arch=x64 >nul\ncd /d "{build}"\ncl /nologo /std:c11 /experimental:c11atomics /LD actual.c /link /OUT:actual.dll\nexit /b %errorlevel%\n')
r=subprocess.run(['cmd','/d','/c',str(build/'build.cmd')],capture_output=True,text=True)
assert r.returncode==0,r.stdout+r.stderr
lib=ctypes.CDLL(str(build/'actual.dll'));started=time.monotonic()
for value in range(8):assert lib.read_state(value)==value
receipt={'source':'components/power_manager/power_manager.c::power_manager_get_state','valuesCorrect':True,'mutexCallsWhileHeld':lib.calls(),'elapsedMs':round((time.monotonic()-started)*1000),'nativeBuild':str(build)}
print(json.dumps(receipt),flush=True)
assert receipt['mutexCallsWhileHeld']==0,'A single state read must not wait for the PM mutex while a device-settings caller holds its own mutex'
