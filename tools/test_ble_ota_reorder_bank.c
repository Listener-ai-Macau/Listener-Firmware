/* Exercises the production bank and protocol with an actual failed HCI window.
 * The generated fixture contains offsets only; image bytes come from the exact
 * corresponding saved OTA artifact. No Bluetooth device is opened here. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions must remain enabled
#endif
#include "ble_firmware_ota_reorder_bank.h"
#include "hci9-bank-window.h"

static unsigned char image[1722064];
static size_t stored;
static unsigned allocations, releases, active, driver_begins;
static bool allocation_fails, finish_fails;
static denzic_ota_v1_context_t context;
static denzic_ota_v1_reorder_t reorder;
static denzic_ota_v1_reorder_slot_t base[16];
static ble_firmware_ota_reorder_bank_t bank = { &reorder, base, 16, 500, NULL };

static void *allocate(size_t count, size_t size)
{
    if (allocation_fails) return NULL;
    void *p = calloc(count, size);
    assert(p != NULL);
    allocations++;
    active++;
    return p;
}
static void release(void *p)
{
    assert(p != NULL && active != 0);
    releases++;
    active--;
    free(p);
}
static bool begin(void *p, uint32_t size)
{
    (void)p;
    driver_begins++;
    ble_firmware_ota_reorder_bank_release(&bank, release);
    if (!ble_firmware_ota_reorder_bank_reserve(&bank, context.window_chunks, allocate, release)) {
        ble_firmware_ota_reorder_bank_release(&bank, release);
        return false;
    }
    denzic_ota_v1_reorder_clear(&reorder);
    stored = 0;
    return size == sizeof(image);
}
static bool write_data(void *p, const uint8_t *data, size_t size)
{
    (void)p;
    assert(stored + size <= sizeof(image));
    assert(memcmp(data, image + stored, size) == 0);
    stored += size;
    return true;
}
static bool finish(void *p)
{
    (void)p;
    assert(stored == sizeof(image));
    ble_firmware_ota_reorder_bank_release(&bank, release);
    return !finish_fails;
}
static void abort_image(void *p)
{
    (void)p;
    ble_firmware_ota_reorder_bank_release(&bank, release);
}
static void put32(unsigned char *p, unsigned v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i));
}
static bool control(uint8_t op, unsigned window)
{
    unsigned char p[20] = { 0x44,0x4f,0x56,0x31,op,1,0,0 };
    put32(p + 8, sizeof(image));
    p[12] = 443 & 255; p[13] = 443 >> 8;
    p[14] = window & 255; p[15] = window >> 8;
    put32(p + 16, 0x12345678);
    return denzic_ota_v1_handle_control(&context, p, sizeof(p));
}
static bool data(unsigned offset, unsigned n)
{
    unsigned char p[504];
    assert(n <= 500 && offset + n <= sizeof(image));
    put32(p, offset);
    memcpy(p + 4, image + offset, n);
    return denzic_ota_v1_handle_data_ordered(&context, &reorder, p, n + 4);
}
static void prefix(unsigned end)
{
    for (unsigned off = 0; off < end; off += 443) assert(data(off, 443));
}
static void full_image(void)
{
    for (unsigned off = 0; off < sizeof(image); off += 443) {
        unsigned n = sizeof(image) - off;
        if (n > 443) n = 443;
        assert(data(off, n));
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(fread(image, 1, sizeof(image), f) == sizeof(image)); fclose(f);
    denzic_ota_v1_storage_driver_t driver = { begin, write_data, finish, abort_image };
    denzic_ota_v1_init(&context, driver, NULL, 500, 400);
    ble_firmware_ota_reorder_bank_release(&bank, release);

    assert(control(DENZIC_OTA_V1_OP_BEGIN, 400));
    assert(reorder.slot_count == 400 && bank.owned && active == 1);
    prefix(HCI_WINDOW_START);
    unsigned peak = 0;
    for (unsigned i = 0; i < 400; i++) {
        assert(data(hci_window[i][0], hci_window[i][1]));
        if (reorder.used > peak) peak = reorder.used;
    }
    assert(context.bytes_written == HCI_WINDOW_END && stored == HCI_WINDOW_END);
    assert(context.data_write_count == 1664 && reorder.used == 0 && peak == 17);
    /* Premature FINISH cannot discard the pending image's bank. */
    assert(!control(DENZIC_OTA_V1_OP_FINISH, 0));
    assert(active == 1);
    assert(control(DENZIC_OTA_V1_OP_ABORT, 0));
    assert(active == 0 && reorder.slots == base && reorder.slot_count == 16);

    /* Capacity follows the negotiated window, including its worst ordering. */
    assert(control(DENZIC_OTA_V1_OP_BEGIN, 400));
    prefix(HCI_WINDOW_START);
    for (int i = 399; i >= 0; i--) assert(data(HCI_WINDOW_START + i * 443, 443));
    assert(context.bytes_written == HCI_WINDOW_END && reorder.used == 0);
    unsigned before = context.bytes_written;
    assert(!data(before + 400 * 443, 443));
    assert(context.bytes_written == before && reorder.used == 0);
    assert(control(DENZIC_OTA_V1_OP_ABORT, 0));

    /* Same-image resume grows without clearing future raw packets. */
    assert(control(DENZIC_OTA_V1_OP_BEGIN, 32));
    assert(data(5 * 443, 443) && reorder.used == 1);
    void *old = bank.owned; unsigned began = driver_begins;
    assert(ble_firmware_ota_reorder_bank_reserve(&bank, 400, allocate, release));
    assert(bank.owned != old && reorder.used == 1 && active == 1);
    assert(control(DENZIC_OTA_V1_OP_BEGIN, 400));
    assert(driver_begins == began && context.window_chunks == 400);
    for (unsigned i = 0; i < 5; i++) assert(data(i * 443, 443));
    assert(context.bytes_written == 6 * 443 && reorder.used == 0);
    assert(data(7 * 443, 443) && reorder.used == 1);
    old = bank.owned;
    allocation_fails = true;
    assert(!ble_firmware_ota_reorder_bank_reserve(&bank, 65535, allocate, release));
    assert(bank.owned == old && active == 1 && context.window_chunks == 400 && reorder.used == 1);
    allocation_fails = false;
    assert(control(DENZIC_OTA_V1_OP_BEGIN, 16));
    assert(data(6 * 443, 443) && context.bytes_written == 8 * 443 && reorder.used == 0);
    assert(control(DENZIC_OTA_V1_OP_ABORT, 0));

    /* A failed fresh allocation acquires no image/bank; repeated cleanup safe. */
    allocation_fails = true;
    assert(!control(DENZIC_OTA_V1_OP_BEGIN, 400));
    assert(active == 0 && reorder.slots == base && !bank.owned);
    allocation_fails = false;
    assert(control(DENZIC_OTA_V1_OP_ABORT, 0));
    ble_firmware_ota_reorder_bank_release(&bank, release);
    assert(!ble_firmware_ota_reorder_bank_reserve(&bank, 0, allocate, release));

    /* Storage finish error and successful checksum handoff both retire memory. */
    assert(control(DENZIC_OTA_V1_OP_BEGIN, 400)); full_image();
    finish_fails = true;
    assert(!control(DENZIC_OTA_V1_OP_FINISH, 0));
    assert(active == 0 && reorder.slots == base);
    finish_fails = false;
    assert(control(DENZIC_OTA_V1_OP_BEGIN, 400)); full_image();
    assert(control(DENZIC_OTA_V1_OP_FINISH, 0));
    assert(context.state == DENZIC_OTA_V1_STATE_COMPLETE && active == 0);
    ble_firmware_ota_reorder_bank_release(&bank, release);
    assert(allocations == releases);
    printf("PASS original HCI400 peak=%u bytes=%u; reverse400, resume/grow/shrink/OOM/abort/reset/finish; allocations=%u releases=%u active=%u window_bytes=%zu\n",
           peak, HCI_WINDOW_END, allocations, releases, active, 400 * sizeof(base[0]));
    return 0;
}
