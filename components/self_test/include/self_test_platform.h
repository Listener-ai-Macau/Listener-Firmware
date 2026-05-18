#ifndef SELF_TEST_PLATFORM_H
#define SELF_TEST_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void self_test_platform_init(void);
bool self_test_platform_check_nvs(void);
bool self_test_platform_check_spiram(void);
uint32_t self_test_platform_get_free_heap(void);

#ifdef __cplusplus
}
#endif

#endif
