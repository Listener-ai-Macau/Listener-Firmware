#include "keyboard.h"
#include "ble_hid.h"

void app_main(void)
{
    keyboard_start();
    ble_hid_init();
    ble_hid_start();
}
