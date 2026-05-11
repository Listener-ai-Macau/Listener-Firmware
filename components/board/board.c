#include "board.h"

#include <stdio.h>

void board_init(void)
{
}

void board_print_help(void)
{
    static const char *help_string =
        "########################################################################\n"
        "BLE keyboard demo usage:\n"
        "Type characters in monitor, and the device sends them as HID keyboard events.\n"
        "########################################################################\n";

    printf("%s\n", help_string);
}
