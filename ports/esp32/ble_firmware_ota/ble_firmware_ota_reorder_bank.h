#ifndef BLE_FIRMWARE_OTA_REORDER_BANK_H
#define BLE_FIRMWARE_OTA_REORDER_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "denzic_ota_v1_reorder.h"

/* Caller holds the OTA owner lock. The permanent small bank remains available
 * outside OTA; a negotiated send window owns temporary external memory. */
typedef struct {
    denzic_ota_v1_reorder_t *target;
    denzic_ota_v1_reorder_slot_t *base;
    uint16_t base_count;
    uint16_t max_payload;
    denzic_ota_v1_reorder_slot_t *owned;
} ble_firmware_ota_reorder_bank_t;

typedef void *(*ble_firmware_ota_reorder_calloc_fn)(size_t, size_t);
typedef void (*ble_firmware_ota_reorder_free_fn)(void *);

static inline void ble_firmware_ota_reorder_bank_release(
    ble_firmware_ota_reorder_bank_t *bank, ble_firmware_ota_reorder_free_fn release)
{
    denzic_ota_v1_reorder_slot_t *old = bank->owned;
    bank->owned = NULL;
    denzic_ota_v1_reorder_init(
        bank->target, bank->base, bank->base_count, bank->max_payload);
    if (old != NULL) {
        release(old);
    }
}

static inline bool ble_firmware_ota_reorder_bank_reserve(
    ble_firmware_ota_reorder_bank_t *bank, uint16_t window_chunks,
    ble_firmware_ota_reorder_calloc_fn allocate,
    ble_firmware_ota_reorder_free_fn release)
{
    if (window_chunks == 0) {
        return false;
    }
    const size_t bytes = (size_t)window_chunks * sizeof(denzic_ota_v1_reorder_slot_t);
    if (bytes / window_chunks != sizeof(denzic_ota_v1_reorder_slot_t)) {
        return false;
    }
    if (bank->target->slot_count >= window_chunks) {
        return true;
    }
    denzic_ota_v1_reorder_slot_t *next = allocate(window_chunks, sizeof(*next));
    if (next == NULL) {
        /* A resume which cannot grow retains the whole prior owner/bank. */
        return false;
    }
    if (bank->target->slots != NULL && bank->target->slot_count != 0) {
        memcpy(next, bank->target->slots,
               bank->target->slot_count * sizeof(*next));
    }
    denzic_ota_v1_reorder_slot_t *old = bank->owned;
    bank->target->slots = next;
    bank->target->slot_count = window_chunks;
    bank->owned = next;
    if (old != NULL) {
        release(old);
    }
    return true;
}

#endif
