#include "board.h"

#include "esp_log.h"

void board_print_help(void)
{
    static const char *help_string =
        "########################################################################\n"
        "BLE keyboard demo usage:\n"
        "Inject test bytes with tools/send_serial.ps1 or type in monitor.\n"
        "Capture 3s audio WAV with tools/capture_audio_wav.ps1 -Port COM3.\n"
        "Capture toggle session WAV with tools/capture_audio_session_wav.ps1 -Port COM3.\n"
        "Hardware voice key uses EC11_KEY GPIO35: press once to start, press again to stop.\n"
        "KEY1-KEY4 send BLE HID d/w/a/s when a host is connected.\n"
        "Hold the hardware voice key for 10s or send ~VREC:RECOVERY to clear pairing/session state.\n"
        "Device status logs use ready, recording, transferring, error, and recovery.\n"
        "End-to-end keystroke delivery still requires a BLE host connection.\n"
        "########################################################################";

    ESP_LOGI("board", "%s", help_string);
}
