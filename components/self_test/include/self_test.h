#ifndef SELF_TEST_H
#define SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool nvs_ok;
    bool ble_ok;
    bool audio_codec_ok;
    bool spiram_ok;
    uint32_t free_heap;
    const char *fw_version;
    const char *protocol_version;
} self_test_result_t;

void self_test_init(void);
self_test_result_t self_test_run(void);

#ifdef __cplusplus
}
#endif

#endif
