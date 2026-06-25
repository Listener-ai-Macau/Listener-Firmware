#ifndef STATUS_LED_STRIP_BACKEND_H
#define STATUS_LED_STRIP_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT 12U

typedef enum {
    STATUS_LED_COLOR_ORDER_GRB = 0,
    STATUS_LED_COLOR_ORDER_RGB,
} status_led_color_order_t;

/* Physical transport used to clock WS2812 data out of the strip data GPIO.
 * RMT is the default (historical) transport. SPI drives WS2812 from the SPI MOSI
 * line via the SPI-clock-hack (3 SPI bits per WS2812 bit) and is used to get a
 * DMA-backed, flicker-free output on strips that cannot share the single RMT DMA
 * channel. See docs/features/status_led_dma_history.md. */
typedef enum {
    STATUS_LED_STRIP_TRANSPORT_RMT = 0,
    STATUS_LED_STRIP_TRANSPORT_SPI,
} status_led_strip_transport_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} status_led_rgb_t;

typedef struct status_led_strip_backend status_led_strip_backend_t;

typedef struct {
    const char *name;
    gpio_num_t gpio;
    uint8_t led_count;
    uint8_t tail_guard_pixels;
    status_led_color_order_t color_order;
    bool prefer_dma;
    /* When transport == SPI, the data GPIO is reassigned to this SPI host's MOSI
     * (e.g. SPI2_HOST / SPI3_HOST) via the ESP32-S3 GPIO matrix. Ignored for RMT. */
    status_led_strip_transport_t transport;
    int spi_host;
} status_led_strip_backend_config_t;

const char *status_led_color_order_name(status_led_color_order_t order);

esp_err_t status_led_strip_backend_new(
    const status_led_strip_backend_config_t *config,
    status_led_strip_backend_t **ret_backend);
bool status_led_strip_backend_available(const status_led_strip_backend_t *backend);
bool status_led_strip_backend_dma_supported(void);
status_led_strip_transport_t status_led_strip_backend_transport(const status_led_strip_backend_t *backend);
bool status_led_strip_backend_dma_requested(const status_led_strip_backend_t *backend);
bool status_led_strip_backend_uses_dma(const status_led_strip_backend_t *backend);
bool status_led_strip_backend_dma_fallback(const status_led_strip_backend_t *backend);
size_t status_led_strip_backend_mem_block_symbols(const status_led_strip_backend_t *backend);
esp_err_t status_led_strip_backend_transmit(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors);
esp_err_t status_led_strip_backend_transmit_non_dma_once(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors);
esp_err_t status_led_strip_backend_suspend(status_led_strip_backend_t *backend);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_STRIP_BACKEND_H */
