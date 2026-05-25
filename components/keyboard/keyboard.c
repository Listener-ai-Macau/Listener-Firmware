#include "keyboard.h"

#include <stdint.h>

#include "hid_keyboard.h"
#include "voice_recording_control.h"

static uint32_t s_key_press_count;
static uint32_t s_ec11_detent_count;

void keyboard_start(void)
{
    voice_recording_control_start();
    hid_keyboard_init();
}

uint32_t keyboard_get_key_press_count(void)
{
    return s_key_press_count;
}

uint32_t keyboard_get_ec11_detent_count(void)
{
    return s_ec11_detent_count;
}
