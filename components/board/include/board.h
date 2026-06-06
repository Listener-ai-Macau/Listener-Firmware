#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int usb_det_level;
    int bat_chg_level;
    int bat_std_level;
    int pwr_hold_level;
    const char *usb_det_policy;
    const char *charger_polarity_policy;
    const char *pwr_hold_policy;
} board_v2_power_input_snapshot_t;

void board_print_help(void);
void board_log_v2_diagnostics(void);
bool board_consume_usb_command(const char *line);
void board_get_v2_power_input_snapshot(board_v2_power_input_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif
