#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_start(void);
uint32_t keyboard_get_key_press_count(void);

#ifdef __cplusplus
}
#endif

#endif
