#include "firmware_ota_wire.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

bool firmware_ota_wire_sizes_valid(uint32_t wire_size, uint32_t raw_size)
{
    if (raw_size == 0 || raw_size > FIRMWARE_OTA_WIRE_IMAGE_MAX_BYTES) {
        return false;
    }
    uint32_t blocks = (raw_size + FIRMWARE_OTA_WIRE_BLOCK_BYTES - 1) /
                      FIRMWARE_OTA_WIRE_BLOCK_BYTES;
    return wire_size >= FIRMWARE_OTA_WIRE_HEADER_BYTES +
                            blocks * (FIRMWARE_OTA_WIRE_BLOCK_HEADER_BYTES + 1) &&
           wire_size <= raw_size + FIRMWARE_OTA_WIRE_HEADER_BYTES +
                            blocks * FIRMWARE_OTA_WIRE_BLOCK_HEADER_BYTES;
}

bool firmware_ota_wire_init(firmware_ota_wire_t *wire,
                            uint32_t wire_size, uint32_t raw_size,
                            uint8_t *input, uint8_t *output,
                            firmware_ota_wire_driver_t driver, void *context)
{
    if (wire == NULL) return false;
    memset(wire, 0, sizeof(*wire));
    wire->failed = true;
    if (!firmware_ota_wire_sizes_valid(wire_size, raw_size) ||
        input == NULL || output == NULL || input == output ||
        driver.inflate == NULL || driver.hash_update == NULL ||
        driver.hash_finish == NULL || driver.write == NULL) {
        return false;
    }
    wire->wire_size = wire_size;
    wire->raw_size = raw_size;
    wire->input = input;
    wire->output = output;
    wire->driver = driver;
    wire->context = context;
    wire->failed = false;
    return true;
}

static bool read_header(firmware_ota_wire_t *wire)
{
    const uint8_t *header = wire->header;
    if (memcmp(header, "DOVZ", 4) != 0 || header[4] != 1 || header[5] != 1 ||
        read_u16(header + 6) != FIRMWARE_OTA_WIRE_HEADER_BYTES ||
        read_u32(header + 8) != wire->raw_size ||
        read_u32(header + 12) != FIRMWARE_OTA_WIRE_BLOCK_BYTES) return false;
    memcpy(wire->expected_sha256, header + 16, 32);
    wire->header_valid = true;
    wire->header_used = 0;
    return true;
}

static bool read_block_header(firmware_ota_wire_t *wire)
{
    const uint8_t *header = wire->header;
    uint32_t remaining = wire->raw_size - wire->raw_written;
    uint32_t expected = remaining < FIRMWARE_OTA_WIRE_BLOCK_BYTES
                            ? remaining : FIRMWARE_OTA_WIRE_BLOCK_BYTES;
    wire->body_size = read_u16(header);
    wire->decoded_size = read_u16(header + 2);
    wire->kind = header[4];
    if (expected == 0 || wire->decoded_size != expected ||
        wire->body_size == 0 || wire->body_size > wire->decoded_size ||
        header[5] != 0 || header[6] != 0 || header[7] != 0 ||
        (wire->kind == 0 ? wire->body_size != wire->decoded_size :
         wire->kind != 1 || wire->body_size >= wire->decoded_size) ||
        wire->body_size > wire->wire_size - wire->wire_received) return false;
    wire->header_used = 0;
    wire->body_used = 0;
    return true;
}

static bool write_block(firmware_ota_wire_t *wire)
{
    const uint8_t *decoded = wire->input;
    if (wire->kind == 1) {
        if (!wire->driver.inflate(wire->context, wire->input, wire->body_size,
                                  wire->output, wire->decoded_size)) return false;
        decoded = wire->output;
    }
    if (!wire->driver.hash_update(wire->context, decoded, wire->decoded_size) ||
        !wire->driver.write(wire->context, decoded, wire->decoded_size)) return false;
    wire->raw_written += wire->decoded_size;
    wire->blocks++;
    wire->body_size = 0;
    wire->body_used = 0;
    return true;
}

bool firmware_ota_wire_write(firmware_ota_wire_t *wire,
                             const uint8_t *bytes, size_t size)
{
    if (wire == NULL || wire->failed || wire->finished) return false;
    if (bytes == NULL || size == 0 || size > wire->wire_size - wire->wire_received) {
        wire->failed = true;
        return false;
    }
    while (size != 0) {
        if (wire->body_size != 0) {
            size_t take = wire->body_size - wire->body_used;
            if (take > size) take = size;
            memcpy(wire->input + wire->body_used, bytes, take);
            wire->body_used += take;
            wire->wire_received += (uint32_t)take;
            bytes += take;
            size -= take;
            if (wire->body_used == wire->body_size && !write_block(wire)) goto fail;
        } else {
            /* A complete decoded image cannot accept another block/header. */
            if (wire->header_valid && wire->raw_written == wire->raw_size) goto fail;
            size_t need = wire->header_valid ? FIRMWARE_OTA_WIRE_BLOCK_HEADER_BYTES
                                            : FIRMWARE_OTA_WIRE_HEADER_BYTES;
            size_t take = need - wire->header_used;
            if (take > size) take = size;
            memcpy(wire->header + wire->header_used, bytes, take);
            wire->header_used += take;
            wire->wire_received += (uint32_t)take;
            bytes += take;
            size -= take;
            if (wire->header_used == need &&
                !(wire->header_valid ? read_block_header(wire) : read_header(wire))) goto fail;
        }
    }
    return true;
fail:
    wire->failed = true;
    return false;
}

bool firmware_ota_wire_finish(firmware_ota_wire_t *wire)
{
    if (wire == NULL || wire->failed) return false;
    if (wire->finished) return true;
    uint8_t digest[32];
    if (!wire->header_valid || wire->wire_received != wire->wire_size ||
        wire->raw_written != wire->raw_size || wire->header_used != 0 ||
        wire->body_size != 0 || !wire->driver.hash_finish(wire->context, digest) ||
        memcmp(digest, wire->expected_sha256, sizeof(digest)) != 0) {
        wire->failed = true;
        return false;
    }
    wire->finished = true;
    return true;
}
