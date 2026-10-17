"""Execute the production queue-completion boundary behind USB recovery ACKs.

Legacy mode replays the original task block to demonstrate that stable recovery
executes but produces no result. It does not send device commands or editor input.
"""
from pathlib import Path
import argparse, json, os, re, subprocess, tempfile
from test_power_source_filter import ROOT, function

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT/'components/voice_recording_control/voice_recording_control.c')
parser.add_argument('--legacy', action='store_true')
parser.add_argument('--type-repo', type=Path, default=ROOT.parent/'Listener-Type')
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
type_transport = (args.type_repo/'src-tauri/src/embedded_ble/windows_ble/mod.rs').read_text(encoding='utf-8')
type_requests = (args.type_repo/'src-tauri/src/embedded_ble/windows_ble/recording_control.rs').read_text(encoding='utf-8')
commands = sorted(set(re.findall(r'send_recovery_control_command_via_usb_serial\(\s*"([^"]+)"',type_requests)))
assert commands, 'No real Type recovery callers were found'
if args.legacy:
    task = function(source, 'voice_recording_control_task')
    block = task[task.index('esp_err_t command_ret ='):task.index('watchdog_platform_feed_current_task();', task.index('esp_err_t command_ret ='))]
    production = 'static void voice_recording_control_execute_queued_command(const voice_recording_control_command_t *input) {\nvoice_recording_control_command_t queued=*input;\n'+block+'\n}'
else:
    production = function(source, 'voice_recording_control_execute_queued_command')
    task = function(source, 'voice_recording_control_task')
    dispatch = function(source, 'voice_recording_control_dispatch_control_command')
    assert 'voice_recording_control_execute_queued_command(&queued);' in task
    assert 'VREC:RESULT' not in dispatch and 'execute_queued_command' not in dispatch

fixture = r'''
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { char command[96]; char source[24]; } voice_recording_control_command_t;
static int executed, emitted, failures, handler_result;
static bool returned;
static char response[256];
static uint32_t s_ensure_actual_segment_id;
static bool s_active_session_host_endpoint_owned;
static struct {bool valid; uint32_t request_id,previous_segment_id,actual_segment_id; esp_err_t result; uint64_t uptime_ms; bool host_endpoint_owned;} s_last_ensure_execution;
static int64_t esp_timer_get_time(void) {return 1234567000;}
#define VOICE_RECORDING_CONTROL_VREC_PREFIX "VREC:"
#define VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS 8u
static esp_err_t voice_recording_control_handle_control_command(const char *command, const char *source) {
    (void)command; (void)source; ++executed; returned=true; return handler_result;
}
static const char *esp_err_to_name(esp_err_t result) { return result==7 ? "ESP_ERR_INVALID_STATE" : "ESP_FAIL"; }
static int capture_printf(const char *format, ...) {
    if (!returned) { ++failures; }
    ++emitted;
    va_list args; va_start(args,format);
    int count=vsnprintf(response,sizeof(response),format,args); va_end(args); return count;
}
#define printf capture_printf
'''+(('\n'.join(function(source,name) for name in ('voice_recording_control_parse_hex_u32_fixed','voice_recording_control_parse_ensure_action','voice_recording_control_observe_ensure_execution'))+'\n') if not args.legacy else '')+production+r'''
#undef printf
static void run(const char *command, const char *origin, int result, bool expects_ack) {
    voice_recording_control_command_t queued={0};
    memcpy(queued.command,command,strlen(command)+1); memcpy(queued.source,origin,strlen(origin)+1);
    executed=0; emitted=0; returned=false; response[0]=0; handler_result=result;
    voice_recording_control_execute_queued_command(&queued);
    char expected[256]; snprintf(expected,sizeof(expected),"~VREC:RESULT command=%s result=%s\n",command,result==0 ? "OK" : esp_err_to_name(result));
    if (executed!=1 || emitted!=(expects_ack ? 1 : 0) || (expects_ack && strcmp(expected,response)!=0)) {
        ++failures; fprintf(stderr,"FAIL origin=%s command=%s result=%d executions=%d ACKs=%d\n",origin,command,result,executed,emitted);
    }
    if (expects_ack && emitted==1) { fputs(response,stdout); }
}
int main(void) {
    const char *commands[]={TYPE_COMMANDS};
    for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
        run(commands[i],"usb",0,true); run(commands[i],"usb",7,true);
        run(commands[i],"ble",0,false); run(commands[i],"ec11",7,false);
    }
    run("VREC:STOP","usb",0,false);
    run("VREC:RECOVERY:TYPE:SILENT:FRESH:WRONG","usb",7,false);
    run("VREC:RECOVERY:TYPE:SILENT","usb.other",0,false);
    OBSERVATION_CASES
    if (failures) {fprintf(stderr,"%d failed completion boundaries\n",failures);return 1;}
    puts("PASS: real Type request coverage; handler result, exact command/source, one ACK after execution, no queue-admission ACK");
    return 0;
}
'''
fixture = fixture.replace('TYPE_COMMANDS',','.join(json.dumps(c) for c in commands))
fixture = fixture.replace('OBSERVATION_CASES', '' if args.legacy else r'''
    s_ensure_actual_segment_id=0x081E0047; s_active_session_host_endpoint_owned=true;
    run("VREC:ENSURE:6AD31B5D:081E005F","ble",0,false);
    if (!s_last_ensure_execution.valid || s_last_ensure_execution.request_id!=0x6AD31B5D || s_last_ensure_execution.previous_segment_id!=0x081E005F || s_last_ensure_execution.actual_segment_id!=0x081E0047 || s_last_ensure_execution.result!=0 || !s_last_ensure_execution.host_endpoint_owned || s_last_ensure_execution.uptime_ms!=1234567) ++failures;
    s_active_session_host_endpoint_owned=false;
    run("VREC:ENSURE:6AD31B5E:081E0060","ble",7,false);
    run("VREC:ABORT:6AD31B5E","ble",0,false);
    run("VREC:STOP","ble",0,false);
    if (s_last_ensure_execution.request_id!=0x6AD31B5E || s_last_ensure_execution.previous_segment_id!=0x081E0060 || s_last_ensure_execution.result!=7 || s_last_ensure_execution.host_endpoint_owned) ++failures;
''')
with tempfile.TemporaryDirectory(prefix='listener-recovery-result-',ignore_cleanup_errors=True) as directory:
    tmp=Path(directory);(tmp/'test.c').write_text(fixture,encoding='utf-8')
    vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
    vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
    (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd.exe','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
    result=subprocess.run([str(tmp/'test.exe')],check=True,capture_output=True,text=True)
    print(result.stdout,end='')
    (tmp/'results.txt').write_text(result.stdout,encoding='utf-8')
    start=type_transport.index('fn complete_recovery_control_result(')
    end=type_transport.index('\n#[cfg(test)]',start)
    rust=type_transport[start:end]+'''\nfn main() {
    let text=std::fs::read_to_string(std::env::args().nth(1).unwrap()).unwrap();
    let commands=[TYPE_COMMANDS];
    for command in commands {
        for expected in ["OK", "ESP_ERR_INVALID_STATE"] {
            let line=text.lines().find(|line| line.contains(&format!("command={command} result={expected}"))).expect("missing firmware execution result");
            assert_eq!(complete_recovery_control_result(line,command).as_deref(),Some(expected));
            assert!(complete_recovery_control_result(line,"VREC:RECOVERY:TYPE:WRONG").is_none());
        }
    }
    println!("PASS: actual Type parser consumes real firmware completion/error; unrelated commands rejected");
}\n'''
    rust=rust.replace('TYPE_COMMANDS',','.join(json.dumps(c) for c in commands))
    (tmp/'interop.rs').write_text(rust,encoding='utf-8')
    subprocess.run([str(Path.home()/'.cargo/bin/rustc.exe'),'--edition=2021',str(tmp/'interop.rs'),'-o',str(tmp/'interop.exe')],check=True)
    subprocess.run([str(tmp/'interop.exe'),str(tmp/'results.txt')],check=True)
