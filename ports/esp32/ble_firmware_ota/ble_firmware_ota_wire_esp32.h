#pragma once

#include "firmware_ota_wire.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "miniz.h"

/* Entire decoder state is temporary PSRAM. In particular tinfl's tables must
 * never be put on the 4096-byte OTA worker/callback stack. */
typedef struct {
    firmware_ota_wire_t wire;
    tinfl_decompressor inflate;
    mbedtls_sha256_context hash;
    uint64_t inflate_us;
    uint32_t inflate_max_us;
    uint8_t input[FIRMWARE_OTA_WIRE_BLOCK_BYTES];
    uint8_t output[FIRMWARE_OTA_WIRE_BLOCK_BYTES];
} ble_firmware_ota_wire_t;

static bool ble_firmware_ota_wire_inflate(void *context,
    const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size)
{
    ble_firmware_ota_wire_t *codec = context;
    size_t consumed = input_size;
    size_t produced = output_size;
    tinfl_init(&codec->inflate);
    int64_t started = esp_timer_get_time();
    tinfl_status status = tinfl_decompress(&codec->inflate, input, &consumed,
        output, output, &produced,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    uint32_t elapsed = (uint32_t)(esp_timer_get_time() - started);
    codec->inflate_us += elapsed;
    if (elapsed > codec->inflate_max_us) codec->inflate_max_us = elapsed;
    return status == TINFL_STATUS_DONE && consumed == input_size && produced == output_size;
}

static bool ble_firmware_ota_wire_hash_update(void *context,
    const uint8_t *bytes, size_t size)
{
    return mbedtls_sha256_update(&((ble_firmware_ota_wire_t *)context)->hash,
                                  bytes, size) == 0;
}

static bool ble_firmware_ota_wire_hash_finish(void *context, uint8_t digest[32])
{
    return mbedtls_sha256_finish(&((ble_firmware_ota_wire_t *)context)->hash, digest) == 0;
}

static void ble_firmware_ota_wire_free(ble_firmware_ota_wire_t *codec)
{
    if (codec != NULL) {
        mbedtls_sha256_free(&codec->hash);
        heap_caps_free(codec);
    }
}

static ble_firmware_ota_wire_t *ble_firmware_ota_wire_create(
    uint32_t wire_size, uint32_t raw_size,
    bool (*write)(void *, const uint8_t *, size_t))
{
    if (!firmware_ota_wire_sizes_valid(wire_size, raw_size)) return NULL;
    ble_firmware_ota_wire_t *codec = heap_caps_calloc(1, sizeof(*codec),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (codec == NULL) return NULL;
    mbedtls_sha256_init(&codec->hash);
    firmware_ota_wire_driver_t driver = {
        .inflate = ble_firmware_ota_wire_inflate,
        .hash_update = ble_firmware_ota_wire_hash_update,
        .hash_finish = ble_firmware_ota_wire_hash_finish,
        .write = write,
    };
    if (mbedtls_sha256_starts(&codec->hash, 0) != 0 ||
        !firmware_ota_wire_init(&codec->wire, wire_size, raw_size,
            codec->input, codec->output, driver, codec)) {
        ble_firmware_ota_wire_free(codec);
        return NULL;
    }
    return codec;
}
