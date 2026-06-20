#include "status_led_strip_backend.h"

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

#include "diag_log.h"

#define STATUS_LED_RMT_RESOLUTION_HZ 10000000U
#define STATUS_LED_RMT_WAIT_MS 20
#define STATUS_LED_WS2812_RESET_TICKS 1500U
#define STATUS_LED_WS2812_T0H_TICKS 3U
#define STATUS_LED_WS2812_T0L_TICKS 10U
#define STATUS_LED_WS2812_T1H_TICKS 7U
#define STATUS_LED_WS2812_T1L_TICKS 6U
#define STATUS_LED_RMT_WITH_DMA SOC_RMT_SUPPORT_DMA

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} status_led_ws2812_encoder_t;

struct status_led_strip_backend {
    const char *name;
    gpio_num_t gpio;
    uint8_t led_count;
    uint8_t tail_guard_pixels;
    uint8_t transmit_led_count;
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    uint8_t pixels[STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT * 3U];
    bool dma_requested;
    bool dma_enabled;
    bool dma_fallback;
    bool available;
};

static const char *TAG = "status_led_strip_backend";

const char *status_led_color_order_name(status_led_color_order_t order)
{
    switch (order) {
    case STATUS_LED_COLOR_ORDER_GRB: return "GRB";
    case STATUS_LED_COLOR_ORDER_RGB: return "RGB";
    default: return "unknown";
    }
}

RMT_ENCODER_FUNC_ATTR
static size_t status_led_ws2812_encode(
    rmt_encoder_t *encoder,
    rmt_channel_handle_t channel,
    const void *primary_data,
    size_t data_size,
    rmt_encode_state_t *ret_state)
{
    status_led_ws2812_encoder_t *led_encoder =
        __containerof(encoder, status_led_ws2812_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led_encoder->state) {
    case 0:
        encoded_symbols += led_encoder->bytes_encoder->encode(
            led_encoder->bytes_encoder,
            channel,
            primary_data,
            data_size,
            &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        /* fall through */
    case 1:
        encoded_symbols += led_encoder->copy_encoder->encode(
            led_encoder->copy_encoder,
            channel,
            &led_encoder->reset_code,
            sizeof(led_encoder->reset_code),
            &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            state |= RMT_ENCODING_COMPLETE;
            led_encoder->state = RMT_ENCODING_RESET;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }

out:
    *ret_state = state;
    return encoded_symbols;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t status_led_ws2812_del(rmt_encoder_t *encoder)
{
    status_led_ws2812_encoder_t *led_encoder =
        __containerof(encoder, status_led_ws2812_encoder_t, base);
    if (led_encoder->bytes_encoder != NULL) {
        (void)rmt_del_encoder(led_encoder->bytes_encoder);
    }
    if (led_encoder->copy_encoder != NULL) {
        (void)rmt_del_encoder(led_encoder->copy_encoder);
    }
    free(led_encoder);
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t status_led_ws2812_reset(rmt_encoder_t *encoder)
{
    status_led_ws2812_encoder_t *led_encoder =
        __containerof(encoder, status_led_ws2812_encoder_t, base);
    (void)rmt_encoder_reset(led_encoder->bytes_encoder);
    (void)rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t status_led_new_ws2812_encoder(rmt_encoder_handle_t *ret_encoder)
{
    status_led_ws2812_encoder_t *led_encoder =
        rmt_alloc_encoder_mem(sizeof(status_led_ws2812_encoder_t));
    if (led_encoder == NULL) {
        return ESP_ERR_NO_MEM;
    }

    led_encoder->base.encode = status_led_ws2812_encode;
    led_encoder->base.del = status_led_ws2812_del;
    led_encoder->base.reset = status_led_ws2812_reset;
    led_encoder->state = 0;
    led_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = STATUS_LED_WS2812_RESET_TICKS,
        .level1 = 0,
        .duration1 = STATUS_LED_WS2812_RESET_TICKS,
    };

    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = STATUS_LED_WS2812_T0H_TICKS,
            .level1 = 0,
            .duration1 = STATUS_LED_WS2812_T0L_TICKS,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = STATUS_LED_WS2812_T1H_TICKS,
            .level1 = 0,
            .duration1 = STATUS_LED_WS2812_T1L_TICKS,
        },
        .flags.msb_first = 1,
    };
    esp_err_t ret = rmt_new_bytes_encoder(&bytes_config, &led_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        free(led_encoder);
        return ret;
    }

    rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &led_encoder->copy_encoder);
    if (ret != ESP_OK) {
        (void)rmt_del_encoder(led_encoder->bytes_encoder);
        free(led_encoder);
        return ret;
    }

    *ret_encoder = &led_encoder->base;
    return ESP_OK;
}

static void status_led_strip_backend_fill_pixels(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    memset(backend->pixels, 0, sizeof(backend->pixels));
    for (uint8_t index = 0; index < backend->led_count; ++index) {
        status_led_rgb_t color = colors[index];
        size_t base = (size_t)index * 3U;
        if (color_order == STATUS_LED_COLOR_ORDER_RGB) {
            backend->pixels[base + 0] = color.r;
            backend->pixels[base + 1] = color.g;
            backend->pixels[base + 2] = color.b;
        } else {
            backend->pixels[base + 0] = color.g;
            backend->pixels[base + 1] = color.r;
            backend->pixels[base + 2] = color.b;
        }
    }
}

static void status_led_strip_backend_release_transport(status_led_strip_backend_t *backend)
{
    if (backend->encoder != NULL) {
        (void)rmt_del_encoder(backend->encoder);
        backend->encoder = NULL;
    }
    if (backend->channel != NULL) {
        (void)rmt_del_channel(backend->channel);
        backend->channel = NULL;
    }
    backend->available = false;
    backend->dma_enabled = false;
}

static esp_err_t status_led_strip_backend_new_channel(status_led_strip_backend_t *backend, bool with_dma)
{
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = backend->gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = STATUS_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 1,
        .flags.with_dma = with_dma,
    };
    return rmt_new_tx_channel(&tx_config, &backend->channel);
}

esp_err_t status_led_strip_backend_new(
    const status_led_strip_backend_config_t *config,
    status_led_strip_backend_t **ret_backend)
{
    if (config == NULL || ret_backend == NULL || config->led_count > STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT ||
        config->tail_guard_pixels > STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT ||
        config->led_count + config->tail_guard_pixels > STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    status_led_strip_backend_t *backend = calloc(1, sizeof(*backend));
    if (backend == NULL) {
        return ESP_ERR_NO_MEM;
    }
    backend->name = config->name;
    backend->gpio = config->gpio;
    backend->led_count = config->led_count;
    backend->tail_guard_pixels = config->tail_guard_pixels;
    backend->transmit_led_count = config->led_count + config->tail_guard_pixels;
    backend->dma_requested = config->prefer_dma && STATUS_LED_RMT_WITH_DMA;
    *ret_backend = backend;

    if (backend->gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "strip %s disabled: GPIO_NUM_NC", backend->name);
        return ESP_OK;
    }

    bool channel_with_dma = backend->dma_requested;
    esp_err_t ret = status_led_strip_backend_new_channel(backend, channel_with_dma);
    if (ret != ESP_OK && channel_with_dma) {
        ESP_LOGW(TAG, "strip %s RMT DMA channel init failed gpio=%d: %s; falling back to non-DMA RMT",
                 backend->name, (int)backend->gpio, esp_err_to_name(ret));
        status_led_strip_backend_release_transport(backend);
        backend->dma_fallback = true;
        channel_with_dma = false;
        ret = status_led_strip_backend_new_channel(backend, channel_with_dma);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "strip %s RMT channel init failed gpio=%d: %s",
                 backend->name, (int)backend->gpio, esp_err_to_name(ret));
        status_led_strip_backend_release_transport(backend);
        return ret;
    }

    ret = status_led_new_ws2812_encoder(&backend->encoder);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "strip %s WS2812 encoder init failed: %s", backend->name, esp_err_to_name(ret));
        status_led_strip_backend_release_transport(backend);
        return ret;
    }

    ret = rmt_enable(backend->channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "strip %s RMT enable failed: %s", backend->name, esp_err_to_name(ret));
        status_led_strip_backend_release_transport(backend);
        return ret;
    }

    backend->available = true;
    backend->dma_enabled = channel_with_dma;
    ESP_LOGI(
        TAG,
        "strip %s ready: gpio=%d leds=%u tx_leds=%u tail_guard_pixels=%u backend=rmt_ws2812_800khz order=%s reset_us=300 timing=ws2812_4020_compatible rmt_dma_requested=%u rmt_dma=%u rmt_dma_fallback=%u",
        backend->name,
        (int)backend->gpio,
        (unsigned)backend->led_count,
        (unsigned)backend->transmit_led_count,
        (unsigned)backend->tail_guard_pixels,
        status_led_color_order_name(config->color_order),
        backend->dma_requested ? 1U : 0U,
        backend->dma_enabled ? 1U : 0U,
        backend->dma_fallback ? 1U : 0U);
    return ESP_OK;
}

bool status_led_strip_backend_available(const status_led_strip_backend_t *backend)
{
    return backend != NULL && backend->available;
}

bool status_led_strip_backend_dma_supported(void)
{
    return STATUS_LED_RMT_WITH_DMA;
}

bool status_led_strip_backend_dma_requested(const status_led_strip_backend_t *backend)
{
    return backend != NULL && backend->dma_requested;
}

bool status_led_strip_backend_uses_dma(const status_led_strip_backend_t *backend)
{
    return backend != NULL && backend->available && backend->dma_enabled;
}

bool status_led_strip_backend_dma_fallback(const status_led_strip_backend_t *backend)
{
    return backend != NULL && backend->dma_fallback;
}

esp_err_t status_led_strip_backend_transmit(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    if (backend == NULL || !backend->available || backend->channel == NULL || backend->encoder == NULL) {
        return ESP_OK;
    }

    status_led_strip_backend_fill_pixels(backend, color_order, colors);
    (void)rmt_encoder_reset(backend->encoder);
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };
    esp_err_t ret = rmt_transmit(
        backend->channel,
        backend->encoder,
        backend->pixels,
        (size_t)backend->transmit_led_count * 3U,
        &transmit_config);
    if (ret == ESP_ERR_INVALID_STATE) {
        (void)rmt_tx_wait_all_done(backend->channel, STATUS_LED_RMT_WAIT_MS);
        (void)rmt_encoder_reset(backend->encoder);
        ret = rmt_transmit(
            backend->channel,
            backend->encoder,
            backend->pixels,
            (size_t)backend->transmit_led_count * 3U,
            &transmit_config);
    }
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                 (uint32_t)backend->gpio, (uint32_t)ret, 0, 0);
        return ret;
    }

    ret = rmt_tx_wait_all_done(backend->channel, STATUS_LED_RMT_WAIT_MS);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                 (uint32_t)backend->gpio, (uint32_t)ret, 1, 0);
    }
    return ret;
}
