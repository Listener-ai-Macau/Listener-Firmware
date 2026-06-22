#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int usb_det_level;
    int bat_chg_level;
    int bat_std_level;
    int pwr_hold_level;
    bool usb_serial_jtag_sof_active;
    bool usb_power_present;
    bool usb_det_highz;
    const char *usb_det_policy;
    const char *charger_polarity_policy;
    const char *pwr_hold_policy;
} board_v2_power_input_snapshot_t;

typedef struct {
    int gpio;
    int level;
    bool configured;
    const char *policy;
} board_v2_power_hold_snapshot_t;

void board_print_help(void);
void board_log_v2_diagnostics(void);
bool board_consume_usb_command(const char *line);
void board_get_v2_power_input_snapshot(board_v2_power_input_snapshot_t *out_snapshot);
void board_get_v2_power_hold_snapshot(board_v2_power_hold_snapshot_t *out_snapshot);
esp_err_t board_configure_power_hold_latch(void);
esp_err_t board_set_power_hold_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
