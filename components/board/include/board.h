#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int usb_det_level;
    bool usb_power_present;
    bool usb_serial_jtag_sof_active;
    bool usb_det_highz;
    bool usb_det_adc_valid;
    int usb_det_adc_mv;
    int usb_det_raw_adc;
    bool usb_det_adc_calibrated;
    uint8_t usb_det_adc_samples;
    esp_err_t usb_det_adc_result;
    bool usb_det_mismatch;
    int bat_chg_level;
    int bat_std_level;
    int pwr_hold_level;
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

typedef struct {
    bool charging;
    bool standby_full;
    bool vin_present;
} board_v2_charger_pin_decode_t;

void board_print_help(void);
void board_log_v2_diagnostics(void);
bool board_consume_usb_command(const char *line);
board_v2_charger_pin_decode_t board_decode_charger_status_pins(int bat_chg_level, int bat_std_level);
void board_get_v2_power_input_snapshot(board_v2_power_input_snapshot_t *out_snapshot);
void board_get_v2_power_hold_snapshot(board_v2_power_hold_snapshot_t *out_snapshot);
esp_err_t board_configure_power_hold_latch(void);
esp_err_t board_set_power_hold_enabled(bool enabled);
/* Manual plugged soft-off keeps USB power but disconnects the native
 * Serial/JTAG data PHY so host DTR/RTS scans cannot reset the chip. */
esp_err_t board_set_usb_serial_jtag_data_connected(bool connected);

#ifdef __cplusplus
}
#endif

#endif
