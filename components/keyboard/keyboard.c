#include "keyboard.h"

#include "ble_hid.h"
#include "hid_keyboard.h"
#include "board.h"
#include "voice_recording_control.h"

void keyboard_start(void)
{
    board_init();
    voice_recording_control_start();
    hid_keyboard_init();
    ble_hid_init();
    ble_hid_start();
}
