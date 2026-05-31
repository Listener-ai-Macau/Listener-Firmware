#include "board.h"

#include "esp_log.h"

void board_print_help(void)
{
    static const char *help_string =
        "########################################################################\n"
        "BLE keyboard demo usage:\n"
        "Board profile: Voice Keyboard V2/N16R8, ESP32-S3-WROOM-1-N16R8, 16MB flash, 8MB Octal PSRAM.\n"
        "Inject test bytes with tools/send_serial.ps1 or type in monitor.\n"
        "Capture 3s audio WAV with tools/capture_audio_wav.ps1 -Port COM3.\n"
        "Capture toggle session WAV with tools/capture_audio_session_wav.ps1 -Port COM3.\n"
        "Hardware voice key uses EC11_KEY/GPIO11: press once to start, press again to stop.\n"
        "Physical keys: KEY1/GPIO38=d, KEY2/GPIO39=w, KEY3/GPIO40=a, KEY4/GPIO41=s.\n"
        "KEY1-KEY4 send BLE HID d/w/a/s when a host is connected.\n"
        "Hold the hardware voice key for 5s or send ~VREC:RECOVERY to clear pairing/session state.\n"
        "Power diagnostics: ~POWER:STATUS reports state/blockers/battery/wake policy, ~POWER:SLEEP requests manual sleep.\n"
        "Watchdog diagnostics: ~WDT:STATUS reports config, ~WDT:DEADLOCK intentionally triggers Task WDT reset.\n"
        "Boot safety diagnostics: ~BOOT:STATUS reports crash counter, ~BOOT:CRASH restarts for validation, ~BOOT:CLEAR clears safe mode.\n"
        "V2 deep-sleep EC11_KEY/GPIO11 wake is disabled until hardware isolation/off-state sign-off.\n"
        "Use ~OTA:STATUS, ~OTA:BLOCKER, or ~OTA:ABORT for firmware OTA diagnostics.\n"
        "Use ~DIAGLOG:COUNT, ~DIAGLOG:LAST:N, ~DIAGLOG:DUMP, or ~DIAGLOG:CLEAR for diagnostics.\n"
        "Device status logs use ready, recording, transferring, error, and recovery.\n"
        "End-to-end keystroke delivery still requires a BLE host connection.\n"
        "########################################################################";

    ESP_LOGI("board", "%s", help_string);
}
