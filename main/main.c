#include "keyboard.h"
#include "ble_hid.h"
#include "self_test.h"

void app_main(void)
{
    self_test_init();
    self_test_run();

    keyboard_start();
    ble_hid_init();
    ble_hid_start();
}
