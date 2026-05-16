#include "keyboard.h"

#include "hid_keyboard.h"
#include "voice_recording_control.h"

void keyboard_start(void)
{
    voice_recording_control_start();
    hid_keyboard_init();
}
