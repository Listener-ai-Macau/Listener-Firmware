"""Execute the controller's actual window configuration with the portable VA machine."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_power_source_filter import ROOT, function


def main():
    source = (ROOT / 'components/voice_recording_control/voice_recording_control.c').read_text(encoding='utf-8')
    process = function(source, 'voice_recording_control_process_voice_activity')
    policy = process[process.index('const bool hidden_automatic ='):process.index('denzic_voice_activation_v1_input_t input =')]
    constants = '\n'.join(re.findall(r'^#define VOICE_RECORDING_CONTROL_(?:AUTOMATIC_WAKE_MAX_SESSION_MS|INITIAL_BODY_WAIT_MS)[^\n]+', source, re.M))
    platform = ROOT / 'third_party/denzic-platform/voice_activation/embedded/c'
    code = '''
#include <assert.h>
#include <stdio.h>
#include "denzic_voice_activation_v1.h"
#include "voice_recording_endpoint_policy.h"
enum { VOICE_RECORDING_STATE_RECORDING = 1 };
static denzic_voice_activation_v1_config_t config_for(bool hidden, bool body, bool enrolled) {
    bool s_active_session_automatic = hidden;
    bool s_active_session_visible = !hidden;
    bool s_active_session_enrollment = enrolled;
    bool s_active_session_body_speech_seen = body;
    bool s_active_session_host_endpoint_owned = false;
    int s_state = VOICE_RECORDING_STATE_RECORDING;
    denzic_voice_activation_v1_config_t s_voice_activation_config = denzic_voice_activation_v1_default_config();
'''
    code = constants + '\n' + code + policy + '''
    (void)host_endpoint_owned_visible;
    return step_config;
}
int main(void) {
    denzic_voice_activation_v1_machine_t machine = {0};
    denzic_voice_activation_v1_config_t hidden = config_for(true, false, false);
    denzic_voice_activation_v1_input_t input = {
        .enabled=true, .auto_start_enabled=true, .auto_stop_enabled=true,
        .recording_active=true, .speech_detected=false, .elapsed_ms=100
    };
    for (int ms=100; ms<4000; ms+=100) {
        assert(denzic_voice_activation_v1_step(&machine,&hidden,&input).action == DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    assert(denzic_voice_activation_v1_step(&machine,&hidden,&input).stop_reason == DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION);
    denzic_voice_activation_v1_reset(&machine);
    denzic_voice_activation_v1_config_t manual = config_for(false, false, false);
    assert(manual.silence_stop_ms == VOICE_RECORDING_CONTROL_INITIAL_BODY_WAIT_MS);
    const uint32_t manual_stop_ms = manual.silence_stop_ms + manual.tail_ms;
    uint32_t manual_elapsed_ms = 0;
    while (manual_elapsed_ms < manual_stop_ms) {
        const uint32_t remaining_ms = manual_stop_ms - manual_elapsed_ms;
        input.elapsed_ms = remaining_ms < 100u ? remaining_ms : 100u;
        manual_elapsed_ms += input.elapsed_ms;
        denzic_voice_activation_v1_decision_t decision = denzic_voice_activation_v1_step(&machine,&manual,&input);
        if (manual_elapsed_ms < manual_stop_ms) {
            assert(decision.action == DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
        } else {
            assert(decision.stop_reason == DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_SILENCE);
        }
    }
    denzic_voice_activation_v1_config_t owned = config_for(false, true, false);
    denzic_voice_activation_v1_config_t enrolled = config_for(false, true, true);
    assert(owned.max_session_ms == 0);
    assert(enrolled.max_session_ms == denzic_voice_activation_v1_default_config().max_session_ms);
    denzic_voice_activation_v1_reset(&machine);
    input.elapsed_ms = 100;
    input.speech_detected = true;
    for (int ms=100; ms<=60000; ms+=100) {
        assert(denzic_voice_activation_v1_step(&machine,&owned,&input).action == DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    puts("PASS: hidden wake remains four-second bounded; empty manual retains fallback; accepted body waits for host endpoint; enrollment stays bounded");
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-wake-test-') as directory:
        tmp = Path(directory)
        (tmp/'test.c').write_text(code, encoding='utf-8')
        includes = str(platform/'include')
        policy_include = str(ROOT/'components/voice_recording_control/include')
        implementation = str(platform/'src/denzic_voice_activation_v1.c')
        executable = tmp/('test.exe' if os.name == 'nt' else 'test')
        if os.name == 'nt':
            vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
            vcvars = Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX /I"{includes}" /I"{policy_include}" test.c "{implementation}" /Fe:test.exe\n', encoding='utf-8')
            subprocess.run(['cmd.exe', '/d', '/c', str(tmp/'build.cmd')], cwd=tmp, check=True)
        else:
            subprocess.run([shutil.which('cc') or 'cc','-std=c11','-Wall','-Werror','-I'+includes,'-I'+policy_include,'test.c',implementation,'-o',str(executable)],cwd=tmp,check=True)
        subprocess.run([str(executable)],check=True)


if __name__ == '__main__':
    main()
