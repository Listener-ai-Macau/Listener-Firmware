#include "keyboard.h"

#include "ble_hid.h"
#include "hid_keyboard.h"
#include "board.h"

void keyboard_start(void)
{
    board_init();
    hid_keyboard_init();
    ble_hid_init();
    ble_hid_start();
}
