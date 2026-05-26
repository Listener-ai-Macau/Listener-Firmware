#ifndef FIRMWARE_OTA_H
#define FIRMWARE_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FIRMWARE_OTA_BLOCKER_NONE = 0,
    FIRMWARE_OTA_BLOCKER_IN_PROGRESS = 1,
    FIRMWARE_OTA_BLOCKER_PENDING_VERIFY = 2,
    FIRMWARE_OTA_BLOCKER_RECORDING_ACTIVE = 3,
    FIRMWARE_OTA_BLOCKER_BLE_AUDIO_ACTIVE = 4,
    FIRMWARE_OTA_BLOCKER_DIAG_EXPORT_ACTIVE = 5,
    FIRMWARE_OTA_BLOCKER_LOW_BATTERY = 6,
    FIRMWARE_OTA_BLOCKER_NO_PARTITION = 7,
} firmware_ota_blocker_t;

typedef struct {
    const char *running_partition;
    const char *boot_partition;
    const char *update_partition;
    uint32_t running_offset;
    uint32_t boot_offset;
    uint32_t update_offset;
    uint32_t update_size;
    const char *running_version;
    const char *target_version;
    size_t bytes_written;
    size_t expected_size;
    bool active;
    bool pending_verify;
    uint32_t running_state;
    firmware_ota_blocker_t blocker;
} firmware_ota_status_t;

void firmware_ota_init(void);
void firmware_ota_record_self_check(bool post_ok, bool ble_ready, bool keyboard_ready);
esp_err_t firmware_ota_confirm_pending_verify_if_ready(void);
esp_err_t firmware_ota_begin(size_t image_size, const char *target_version);
esp_err_t firmware_ota_write(const void *data, size_t size);
esp_err_t firmware_ota_finish(bool reboot_after_set_boot);
void firmware_ota_abort(uint32_t reason);
void firmware_ota_reboot_to_pending_image(void);
bool firmware_ota_consume_usb_command(const char *line);
firmware_ota_status_t firmware_ota_get_status(void);
firmware_ota_blocker_t firmware_ota_get_blocker(void);
const char *firmware_ota_blocker_name(firmware_ota_blocker_t blocker);
void firmware_ota_note_battery(uint8_t percent, uint32_t voltage_mv, bool valid);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_OTA_H */
