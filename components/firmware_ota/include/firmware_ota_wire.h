#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DOVZ is a transport envelope, never a boot image. Storage/boot ownership
 * remains with firmware_ota; this parser owns only a bounded wire cursor. */
#define FIRMWARE_OTA_WIRE_BLOCK_BYTES 16384U
#define FIRMWARE_OTA_WIRE_HEADER_BYTES 48U
#define FIRMWARE_OTA_WIRE_BLOCK_HEADER_BYTES 8U
#define FIRMWARE_OTA_WIRE_IMAGE_MAX_BYTES 0x600000U

typedef struct {
    bool (*inflate)(void *context, const uint8_t *input, size_t input_size,
                    uint8_t *output, size_t output_size);
    bool (*hash_update)(void *context, const uint8_t *bytes, size_t size);
    bool (*hash_finish)(void *context, uint8_t digest[32]);
    bool (*write)(void *context, const uint8_t *bytes, size_t size);
} firmware_ota_wire_driver_t;

typedef struct {
    firmware_ota_wire_driver_t driver;
    void *context;
    uint8_t *input;
    uint8_t *output;
    uint32_t wire_size;
    uint32_t raw_size;
    uint32_t wire_received;
    uint32_t raw_written;
    uint32_t blocks;
    uint8_t header[FIRMWARE_OTA_WIRE_HEADER_BYTES];
    uint8_t expected_sha256[32];
    size_t header_used;
    size_t body_used;
    uint16_t body_size;
    uint16_t decoded_size;
    uint8_t kind;
    bool header_valid;
    bool failed;
    bool finished;
} firmware_ota_wire_t;

bool firmware_ota_wire_sizes_valid(uint32_t wire_size, uint32_t raw_size);
bool firmware_ota_wire_init(firmware_ota_wire_t *wire,
                            uint32_t wire_size, uint32_t raw_size,
                            uint8_t *input, uint8_t *output,
                            firmware_ota_wire_driver_t driver, void *context);
bool firmware_ota_wire_write(firmware_ota_wire_t *wire,
                             const uint8_t *bytes, size_t size);
bool firmware_ota_wire_finish(firmware_ota_wire_t *wire);
