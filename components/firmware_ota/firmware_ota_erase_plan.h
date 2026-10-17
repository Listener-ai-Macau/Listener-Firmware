#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Plan one bounded erase ahead of an owned sequential write. No write may
 * reach an un-erased byte, and no range may extend beyond this image's rounded
 * sector boundary or its inactive partition. The caller advances its frontier
 * only after the erase succeeds, under the same lock as write/abort/finish. */
typedef struct {
    size_t offset;
    size_t length;
} firmware_ota_erase_range_t;

static inline bool firmware_ota_next_erase_range(
    size_t partition_size,
    size_t image_size,
    size_t sector_size,
    size_t erased_through,
    size_t write_end,
    firmware_ota_erase_range_t *range)
{
    const size_t block_bytes = 64U * 1024U;
    if (range == NULL || sector_size == 0 || sector_size > block_bytes ||
        block_bytes % sector_size != 0 || image_size == 0 ||
        image_size > partition_size || write_end > image_size ||
        image_size > SIZE_MAX - (sector_size - 1)) {
        return false;
    }
    const size_t limit = ((image_size + sector_size - 1) / sector_size) * sector_size;
    if (limit > partition_size || erased_through > limit ||
        erased_through % sector_size != 0) {
        return false;
    }
    range->offset = erased_through;
    range->length = 0;
    if (write_end > erased_through) {
        const size_t remaining = limit - erased_through;
        range->length = remaining < block_bytes ? remaining : block_bytes;
    }
    return true;
}
