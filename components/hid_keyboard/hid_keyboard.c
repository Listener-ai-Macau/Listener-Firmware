#include "hid_keyboard.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "diag_log.h"

#define USB_HID_MODIFIER_LEFT_SHIFT 0x02
#define USB_HID_SPACE 0x2C
#define USB_HID_DOT 0x37
#define USB_HID_NEWLINE 0x28
#define USB_HID_FORWARD_SLASH 0x38
#define USB_HID_BACK_SLASH 0x31
#define USB_HID_COMMA 0x36

#define HID_KEYBOARD_REPORT_ID 1
#define HID_KEYBOARD_REPORT_SIZE 7

#define KEY_CASE(input_value, modifier_value, key_value) \
    case input_value:                                    \
        report_buffer[0] = modifier_value;               \
        report_buffer[2] = key_value;                    \
        break

static const char *TAG = "hid_keyboard";
static uint32_t s_key_press_count;

static const uint8_t s_keyboard_report_map[] = {
    0x05, 0x01,
    0x09, 0x06,
    0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x03,
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x03,
    0x95, 0x05,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,
    0xC0,
};

static void hid_keyboard_translate_ascii(uint8_t *report_buffer, char input_char)
{
    if (input_char >= 'a' && input_char <= 'z') {
        report_buffer[2] = (uint8_t)(4 + (input_char - 'a'));
        return;
    }

    if (input_char >= 'A' && input_char <= 'Z') {
        report_buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT;
        report_buffer[2] = (uint8_t)(4 + ((input_char - 'A')));
        return;
    }

    if (input_char >= '1' && input_char <= '9') {
        report_buffer[2] = (uint8_t)(30 + (input_char - '1'));
        return;
    }

    if (input_char == '0') {
        report_buffer[2] = 39;
        return;
    }

    switch (input_char) {
    KEY_CASE(' ', 0, USB_HID_SPACE);
    KEY_CASE('.', 0, USB_HID_DOT);
    KEY_CASE('\n', 0, USB_HID_NEWLINE);
    KEY_CASE('\r', 0, USB_HID_NEWLINE);
    KEY_CASE('?', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_FORWARD_SLASH);
    KEY_CASE('/', 0, USB_HID_FORWARD_SLASH);
    KEY_CASE('\\', 0, USB_HID_BACK_SLASH);
    KEY_CASE('|', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_BACK_SLASH);
    KEY_CASE(',', 0, USB_HID_COMMA);
    KEY_CASE('<', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_COMMA);
    KEY_CASE('>', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_DOT);
    KEY_CASE('@', USB_HID_MODIFIER_LEFT_SHIFT, 31);
    KEY_CASE('!', USB_HID_MODIFIER_LEFT_SHIFT, 30);
    KEY_CASE('#', USB_HID_MODIFIER_LEFT_SHIFT, 32);
    KEY_CASE('$', USB_HID_MODIFIER_LEFT_SHIFT, 33);
    KEY_CASE('%', USB_HID_MODIFIER_LEFT_SHIFT, 34);
    KEY_CASE('^', USB_HID_MODIFIER_LEFT_SHIFT, 35);
    KEY_CASE('&', USB_HID_MODIFIER_LEFT_SHIFT, 36);
    KEY_CASE('*', USB_HID_MODIFIER_LEFT_SHIFT, 37);
    KEY_CASE('(', USB_HID_MODIFIER_LEFT_SHIFT, 38);
    KEY_CASE(')', USB_HID_MODIFIER_LEFT_SHIFT, 39);
    KEY_CASE('-', 0, 0x2D);
    KEY_CASE('_', USB_HID_MODIFIER_LEFT_SHIFT, 0x2D);
    KEY_CASE('=', 0, 0x2E);
    KEY_CASE('+', USB_HID_MODIFIER_LEFT_SHIFT, 0x2E);
    KEY_CASE(8, 0, 0x2A);
    KEY_CASE('\t', 0, 0x2B);
    KEY_CASE(';', 0, 0x33);
    KEY_CASE(':', USB_HID_MODIFIER_LEFT_SHIFT, 0x33);
    KEY_CASE('\'', 0, 0x34);
    KEY_CASE('"', USB_HID_MODIFIER_LEFT_SHIFT, 0x34);
    KEY_CASE('`', 0, 0x35);
    KEY_CASE('~', USB_HID_MODIFIER_LEFT_SHIFT, 0x35);
    KEY_CASE('[', 0, 0x2F);
    KEY_CASE(']', 0, 0x30);
    KEY_CASE('{', USB_HID_MODIFIER_LEFT_SHIFT, 0x2F);
    KEY_CASE('}', USB_HID_MODIFIER_LEFT_SHIFT, 0x30);
    default:
        report_buffer[0] = 0;
        report_buffer[2] = 0;
        break;
    }
}

static bool hid_keyboard_is_supported_ascii(const uint8_t *report_buffer)
{
    return report_buffer[0] != 0 || report_buffer[2] != 0;
}

void hid_keyboard_init(void)
{
}

const uint8_t *hid_keyboard_get_report_map(void)
{
    return s_keyboard_report_map;
}

size_t hid_keyboard_get_report_map_size(void)
{
    return sizeof(s_keyboard_report_map);
}

uint32_t hid_keyboard_get_key_press_count(void)
{
    return s_key_press_count;
}

esp_err_t hid_keyboard_send_ascii(char input_char, esp_hidd_dev_t *hid_device)
{
    uint8_t report_buffer[HID_KEYBOARD_REPORT_SIZE] = {0};
    unsigned int input_value = (unsigned char)input_char;
    char display_char = (input_value >= 32 && input_value <= 126) ? input_char : '.';
    esp_err_t ret;

    if (hid_device == NULL) {
        ESP_LOGE(TAG, "send_ascii called without HID device");
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_KEY_FAIL, DIAG_SEV_WARN,
                 input_value, ESP_ERR_INVALID_ARG, 0, 0);
        return ESP_ERR_INVALID_ARG;
    }

    bool connected = esp_hidd_dev_connected(hid_device);
    hid_keyboard_translate_ascii(report_buffer, input_char);
    if (!hid_keyboard_is_supported_ascii(report_buffer)) {
        ESP_LOGW(TAG, "unsupported ascii input=0x%02X display=%c", input_value, display_char);
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_KEY_FAIL, DIAG_SEV_WARN,
                 input_value, ESP_ERR_NOT_SUPPORTED, connected ? 1 : 0, 0);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(
        TAG,
        "send_ascii input=0x%02X display=%c modifier=0x%02X key=0x%02X connected=%s",
        input_value,
        display_char,
        report_buffer[0],
        report_buffer[2],
        connected ? "yes" : "no");

    ret = esp_hidd_dev_input_set(hid_device, 0, HID_KEYBOARD_REPORT_ID, report_buffer, HID_KEYBOARD_REPORT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "input press failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_KEY_FAIL, DIAG_SEV_WARN,
                 input_value, ret, connected ? 1 : 0, 0);
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    memset(report_buffer, 0, sizeof(report_buffer));
    ret = esp_hidd_dev_input_set(hid_device, 0, HID_KEYBOARD_REPORT_ID, report_buffer, HID_KEYBOARD_REPORT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "input release failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_KEY_FAIL, DIAG_SEV_WARN,
                 input_value, ret, connected ? 1 : 0, 0);
        return ret;
    }

    s_key_press_count++;
    diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_KEY_PRESS, DIAG_SEV_INFO,
             input_value, 0, connected ? 1 : 0, s_key_press_count);
    ESP_LOGI(TAG, "send_ascii done input=0x%02X display=%c", input_value, display_char);
    return ESP_OK;
}
