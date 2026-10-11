"""Execute production monitoring policy across drain, idle, disconnect and OTA."""
import os
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_power_source_filter import ROOT, function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ble-source', type=Path)
    args = parser.parse_args()
    source = (ROOT / 'components/voice_recording_control/voice_recording_control.c').read_text(encoding='utf-8')
    ble_source = (args.ble_source or ROOT / 'ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c').read_text(encoding='utf-8')
    type_link_ready = function(ble_source, 'ble_audio_stream_is_type_link_ready')
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <inttypes.h>
enum { VOICE_RECORDING_STATE_IDLE, VOICE_RECORDING_STATE_RECORDING,
       VOICE_RECORDING_STATE_TRANSFERRING };
typedef enum {
    BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED = 0,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_CONNECTED,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_MTU_READY,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR,
} ble_audio_stream_transport_state_t;
enum { POWER_MANAGER_STATE_ACTIVE, POWER_MANAGER_STATE_CONNECTED_IDLE,
       POWER_MANAGER_STATE_DISCONNECTED_IDLE };
#define POWER_MANAGER_BLOCKER_OTA 1u
#define ESP_OK 0
#define TAG "voice-monitoring-fixture"
static void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) test_log(__VA_ARGS__)
typedef int esp_err_t;
typedef struct { bool voice_auto_start_enabled, voice_auto_stop_enabled; } device_settings_snapshot_t;
typedef struct { unsigned state, blockers; } power_manager_snapshot_t;
static device_settings_snapshot_t fake_settings;
static power_manager_snapshot_t fake_power;
static bool s_voice_auto_start_enabled, s_voice_auto_stop_enabled;
static bool s_active_session_automatic, s_cancel_pending, s_active_session_enrollment;
static bool s_voice_monitoring, actual_monitoring, link_ready, heartbeat_recent, ota_hold;
static bool s_voice_auto_rearm_await_quiet_edge, s_voice_auto_rearm_suppression_logged;
static uint32_t s_voice_auto_rearm_episode_id, s_voice_auto_rearm_terminal_session_count;
static uint32_t s_voice_auto_rearm_probe_count;
static ble_audio_stream_transport_state_t s_transport_state;
static unsigned s_state, s_voice_activation_machine, resets, writes;
static void *s_vad_queue;
static void device_settings_get_snapshot(device_settings_snapshot_t *p) { *p = fake_settings; }
static void power_manager_get_snapshot(power_manager_snapshot_t *p) { *p = fake_power; }
static bool ble_audio_stream_transport_link_ready(void) { return link_ready; }
static bool ble_audio_stream_type_heartbeat_recent(void) { return heartbeat_recent; }
static const char *power_manager_state_name(unsigned state) { (void)state; return "fixture"; }
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "fixture"; }
static bool ble_audio_stream_is_ready(void) { return link_ready; }
'''
    if 'static bool ble_audio_stream_transport_state_type_ready(' in ble_source:
        code += function(ble_source, 'ble_audio_stream_transport_state_type_ready')
    code += type_link_ready
    code += r'''
static bool ble_audio_stream_type_ota_hold_is_active(void) { return ota_hold; }
static bool audio_capture_voice_activation_monitoring_is_enabled(void) { return actual_monitoring; }
static esp_err_t audio_capture_set_voice_activation_monitoring(bool on) {
    actual_monitoring = on; writes++; return ESP_OK;
}
static void denzic_voice_activation_v1_reset(unsigned *p) { *p = 0; resets++; }
static void voice_recording_control_cancel(const char *reason) { (void)reason; s_cancel_pending = true; }
static void xQueueReset(void *p) { (void)p; }
'''
    code += function(source, 'voice_recording_control_clear_auto_rearm_latch')
    code += function(source, 'voice_recording_control_refresh_voice_monitoring')
    code += r'''
static void refresh(bool expected) {
    voice_recording_control_refresh_voice_monitoring();
    assert(actual_monitoring == expected && s_voice_monitoring == expected);
}
int main(void) {
    fake_settings.voice_auto_start_enabled = true;
    fake_settings.voice_auto_stop_enabled = true;
    fake_power.state = POWER_MANAGER_STATE_ACTIVE;
    link_ready = true;
    heartbeat_recent = true;
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY;
    s_state = VOICE_RECORDING_STATE_IDLE;
    refresh(true);
    unsigned initial_writes = writes;
    /* N records, stops and drains. N+1 has not started; retain its prefix. */
    s_state = VOICE_RECORDING_STATE_RECORDING;
    s_active_session_automatic = true;
    refresh(true);
    s_state = VOICE_RECORDING_STATE_TRANSFERRING;
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING;
    refresh(true);
    assert(writes == initial_writes && resets == 0);
    /* The export task publishes each of these phases while finishing STOP.
     * None changes the live notify link or Type heartbeat. Every observation
     * by the control task must retain capture, including the intermediate ones. */
    s_state = VOICE_RECORDING_STATE_IDLE;
    s_active_session_automatic = false;
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED;
    refresh(true);
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED;
    refresh(true);
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY;
    refresh(true);
    assert(writes == initial_writes && resets == 0);
    /* A session error also leaves the same Type connection available; the
     * ordinary start gate, rather than capture monitoring, owns its recovery. */
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR;
    refresh(true);
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED;
    heartbeat_recent = false;
    refresh(false);
    heartbeat_recent = true;
    refresh(true);
    s_state = VOICE_RECORDING_STATE_IDLE;
    s_active_session_automatic = false;
    refresh(true);
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED;
    link_ready = false;
    heartbeat_recent = false;
    refresh(false);
    s_transport_state = BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY;
    link_ready = true;
    /* Merely subscribing after reconnect cannot resurrect a previous owner. */
    refresh(false);
    heartbeat_recent = true;
    refresh(true);
    /* A real idle transition and disconnect must still release listening. */
    fake_power.state = POWER_MANAGER_STATE_CONNECTED_IDLE;
    refresh(false);
    fake_power.state = POWER_MANAGER_STATE_ACTIVE;
    refresh(true);
    link_ready = false;
    refresh(false);
    link_ready = true;
    refresh(true);
    fake_power.state = POWER_MANAGER_STATE_DISCONNECTED_IDLE;
    refresh(false);
    fake_power.state = POWER_MANAGER_STATE_ACTIVE;
    refresh(true);
    /* Both OTA paths and disabling auto-start must stop idle monitoring. */
    fake_power.blockers = POWER_MANAGER_BLOCKER_OTA;
    refresh(false);
    fake_power.blockers = 0;
    refresh(true);
    ota_hold = true;
    refresh(false);
    ota_hold = false;
    s_voice_auto_rearm_await_quiet_edge = true;
    s_voice_auto_rearm_suppression_logged = true;
    s_voice_auto_rearm_terminal_session_count = 3;
    s_voice_auto_rearm_probe_count = 7;
    fake_settings.voice_auto_start_enabled = false;
    refresh(false);
    assert(!s_voice_auto_rearm_await_quiet_edge);
    assert(!s_voice_auto_rearm_suppression_logged);
    assert(s_voice_auto_rearm_terminal_session_count == 0 && s_voice_auto_rearm_probe_count == 0);
    /* Manual recording still supports auto-stop even without auto-start. */
    s_state = VOICE_RECORDING_STATE_RECORDING;
    refresh(true);
    s_active_session_enrollment = true;
    refresh(false);
    s_active_session_enrollment = false;
    s_state = VOICE_RECORDING_STATE_IDLE;
    fake_settings.voice_auto_start_enabled = true;
    refresh(true);
    /* Reapply if the capture layer independently entered fake_power save. */
    actual_monitoring = false;
    refresh(true);
    puts("PASS: production voice monitoring retains capture through every STOP phase and releases unauthenticated link/idle/OTA/disconnect");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(
        prefix='listener-voice-monitoring-', ignore_cleanup_errors=True) as directory:
        tmp = Path(directory)
        (tmp/'test.c').write_text(code, encoding='utf-8')
        if os.name == 'nt':
            vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
            vcvars = Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n', encoding='utf-8')
            subprocess.run(['cmd.exe', '/d', '/c', str(tmp/'build.cmd')], cwd=tmp, check=True)
            executable = tmp/'test.exe'
        else:
            executable = tmp/'test'
            subprocess.run([shutil.which('cc') or 'cc', '-std=c11', '-Wall', '-Werror', 'test.c', '-o', str(executable)], cwd=tmp, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
