#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DEVICE_KEY_PROFILE_MAX 4096u
void device_key_profile_init(void);
/* Complete bounded page; separator is ';' for BLE and ' ' for USB. */
bool device_key_profile_format(char *out, size_t size, char separator);
esp_err_t device_key_profile_command(const char *command);
/* 0 = legacy/host action; 1 = disabled; 2 = native keyboard shortcut. */
bool device_key_profile_event(char *out, size_t size, uint8_t key, uint8_t gesture);
uint8_t device_key_profile_hid(uint8_t key, uint8_t gesture, uint8_t *usage, uint8_t *modifier);
