#include "keyboard.h"

#include "hid_keyboard.h"
#include "voice_recording_control.h"

void keyboard_start(void)
{
    voice_recording_control_start();
    hid_keyboard_init();
}

uint32_t keyboard_get_key_press_count(void)
{
    return hid_keyboard_get_key_press_count();
}
