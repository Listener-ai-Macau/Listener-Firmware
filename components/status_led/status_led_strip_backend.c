/*
 * Physical WS2812 strip backend (RMT). Before changing the per-strip DMA selection,
 * adding a new LED peripheral (e.g. an SPI+DMA sibling backend), or widening DMA to
 * more strips, READ docs/features/status_led_dma_history.md first. It consolidates the
 * historical tuning experience and the hard constraints:
 *   - ESP32-S3 can use DMA on only ONE RMT TX channel, so only the status strip is on
 *     RMT DMA. Historical hardware validation showed that forcing all four strips
 *     to RMT DMA left EC11/key/edge unavailable.
 *   - EC11/key/edge flicker on non-DMA (interrupt-backed) RMT; the historical
 *     recommendation for them is a different DMA backend (SPI+DMA). DMA can NOT stay
 *     enabled through low-power idle (LED3-6 idle-latch corruption), so any DMA strip
 *     must follow the active-DMA / low-power-non-DMA-final-latch pattern.
 */
#include "status_led_strip_backend.h"

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

#include "diag_log.h"

#define STATUS_LED_RMT_RESOLUTION_HZ 10000000U
#define STATUS_LED_RMT_WAIT_MS 500
#define STATUS_LED_WS2812_RESET_TICKS 1500U
#define STATUS_LED_WS2812_T0H_TICKS 3U
#define STATUS_LED_WS2812_T0L_TICKS 10U
#define STATUS_LED_WS2812_T1H_TICKS 7U
#define STATUS_LED_WS2812_T1L_TICKS 6U
#define STATUS_LED_RMT_WITH_DMA SOC_RMT_SUPPORT_DMA
#define STATUS_LED_RMT_DMA_MEM_BLOCK_SYMBOLS 1024U

/* ---- SPI transport (WS2812 via SPI MOSI clock-hack) ----------------------
 * Each WS2812 bit is encoded as 4 SPI bits (0 -> 0b1000, 1 -> 0b1110) clocked
 * at STATUS_LED_SPI_CLOCK_HZ. At 3.2 MHz, 1 SPI bit = 312.5 ns, so one WS2812
 * bit = 1.25 us: T0H=312.5ns/T0L=937.5ns, T1H=937.5ns/T1L=312.5ns. This gives
 * the 3.3 V key chain a longer zero-low window than the older 3-bit 0b100 code,
 * which human review showed could still let KEY1 bring up downstream pixels.
 * SCLK is not routed to a GPIO; only MOSI is routed to the LED DIN. The DMA
 * buffer lives in internal DMA-capable RAM and is fed by SPI GDMA.
 * STATUS_LED_SPI_RESET_BYTES trailing zero bytes keep MOSI low long enough for
 * the WS2812/4020 reset latch while preserving the active SPI DMA path
 * (contract: rmt_reset_us=300 spi_reset_us=600, timing=ws2812_4020_compatible). */
#define STATUS_LED_SPI_CLOCK_HZ       3200000
#define STATUS_LED_SPI_BITS_PER_BIT   4U
#define STATUS_LED_SPI_RESET_BYTES    240U

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
    uint8_t dark_latch_rmt_writes;
    uint8_t transmit_led_count;
    status_led_strip_transport_t requested_transport;
    status_led_strip_transport_t transport;
    int spi_host;
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    size_t mem_block_symbols;
    uint8_t pixels[STATUS_LED_STRIP_BACKEND_MAX_LED_COUNT * 3U];
    /* SPI transport state (transport == STATUS_LED_STRIP_TRANSPORT_SPI).
     * spi_buf holds the clock-hack encoded frame plus the trailing reset-low
     * bytes; spi_buf_len is its byte length. spi_device / spi_bus_owned are
     * valid only while the SPI transport is acquired. */
    spi_device_handle_t spi_device;
    uint8_t *spi_buf;
    size_t spi_buf_len;
    bool spi_bus_owned;
    bool dma_requested;
    bool dma_enabled;
    bool dma_fallback;
    bool channel_enabled;
    bool available;
    bool gpio_idle_driven_low;
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

static bool status_led_strip_backend_colors_all_dark(
    const status_led_strip_backend_t *backend,
    const status_led_rgb_t *colors)
{
    if (backend == NULL || colors == NULL) {
        return false;
    }
    for (uint8_t index = 0; index < backend->led_count; ++index) {
        if (colors[index].r != 0U || colors[index].g != 0U || colors[index].b != 0U) {
            return false;
        }
    }
    return true;
}

/* ---- SPI transport implementation ---------------------------------------- */

static size_t status_led_spi_data_bytes(const status_led_strip_backend_t *backend)
{
    /* 4 SPI bits per WS2812 bit, 8 bits per channel, 3 channels per LED
     * -> 12 SPI bytes per LED. */
    return (size_t)backend->transmit_led_count * 3U * 8U * STATUS_LED_SPI_BITS_PER_BIT / 8U;
}

/* Encode backend->pixels (transmit_led_count * 3 channel bytes, already GRB/RGB
 * packed) into backend->spi_buf via the SPI clock-hack: each WS2812 bit expands
 * to 4 SPI bits (0 -> 0b1000, 1 -> 0b1110), MSB-first. Trailing bytes (the
 * reset-low latch) are left zero. */
static void status_led_spi_encode_frame(status_led_strip_backend_t *backend)
{
    memset(backend->spi_buf, 0, backend->spi_buf_len);
    size_t bit_pos = 0;
    size_t channel_bytes = (size_t)backend->transmit_led_count * 3U;
    for (size_t i = 0; i < channel_bytes; ++i) {
        uint8_t byte = backend->pixels[i];
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t pattern = (byte & (1u << bit)) ? 0xEu : 0x8u;  /* 0b1110 / 0b1000 */
            for (int p = 3; p >= 0; --p) {
                if ((pattern >> p) & 1u) {
                    size_t byte_idx = bit_pos / 8U;
                    uint8_t bit_idx = (uint8_t)(7U - (bit_pos % 8U));  /* MSB-first */
                    backend->spi_buf[byte_idx] |= (uint8_t)(1u << bit_idx);
                }
                ++bit_pos;
            }
        }
    }
}

static esp_err_t status_led_spi_init_transport(status_led_strip_backend_t *backend)
{
    backend->spi_buf_len = status_led_spi_data_bytes(backend) + STATUS_LED_SPI_RESET_BYTES;
    backend->spi_buf = heap_caps_malloc(backend->spi_buf_len, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (backend->spi_buf == NULL) {
        ESP_LOGW(TAG, "strip %s SPI DMA buffer alloc failed (%u bytes)",
                 backend->name, (unsigned)backend->spi_buf_len);
        return ESP_ERR_NO_MEM;
    }
    memset(backend->spi_buf, 0, backend->spi_buf_len);

    spi_bus_config_t buscfg = {
        .mosi_io_num = backend->gpio,
        .miso_io_num = -1,
        .sclk_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)backend->spi_buf_len,
    };
    esp_err_t ret = spi_bus_initialize(backend->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "strip %s spi_bus_initialize(host=%d gpio=%d) failed: %s",
                 backend->name, backend->spi_host, (int)backend->gpio, esp_err_to_name(ret));
        free(backend->spi_buf);
        backend->spi_buf = NULL;
        backend->spi_buf_len = 0U;
        return ret;
    }
    backend->spi_bus_owned = true;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = STATUS_LED_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ret = spi_bus_add_device(backend->spi_host, &devcfg, &backend->spi_device);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "strip %s spi_bus_add_device failed: %s",
                 backend->name, esp_err_to_name(ret));
        (void)spi_bus_free(backend->spi_host);
        backend->spi_bus_owned = false;
        free(backend->spi_buf);
        backend->spi_buf = NULL;
        backend->spi_buf_len = 0U;
        return ret;
    }

    backend->available = true;
    backend->dma_enabled = true;  /* SPI + GDMA is the reason this transport exists */
    backend->channel_enabled = true;
    backend->mem_block_symbols = 0U;
    backend->gpio_idle_driven_low = false;
    ESP_LOGI(
        TAG,
        "strip %s transport ready: gpio=%d leds=%u tx_leds=%u tail_guard_pixels=%u backend=spi_ws2812_800khz spi_host=%d spi_clk_hz=%d spi_reset_us=600 spi_waveform=4bit_3m2_0x8_0xE spi_dma=1 spi_buf_bytes=%u",
        backend->name,
        (int)backend->gpio,
        (unsigned)backend->led_count,
        (unsigned)backend->transmit_led_count,
        (unsigned)backend->tail_guard_pixels,
        backend->spi_host,
        STATUS_LED_SPI_CLOCK_HZ,
        (unsigned)backend->spi_buf_len);
    return ESP_OK;
}

static void status_led_spi_release_transport(status_led_strip_backend_t *backend)
{
    if (backend->spi_device != NULL) {
        (void)spi_bus_remove_device(backend->spi_device);
        backend->spi_device = NULL;
    }
    if (backend->spi_bus_owned) {
        (void)spi_bus_free(backend->spi_host);
        backend->spi_bus_owned = false;
    }
    if (backend->spi_buf != NULL) {
        free(backend->spi_buf);
        backend->spi_buf = NULL;
    }
    backend->spi_buf_len = 0U;
    backend->available = false;
    backend->dma_enabled = false;
    backend->channel_enabled = false;
    backend->gpio_idle_driven_low = false;
}

static esp_err_t status_led_spi_transmit(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    status_led_strip_backend_fill_pixels(backend, color_order, colors);
    status_led_spi_encode_frame(backend);

    spi_transaction_t t = {};
    t.length = backend->spi_buf_len * 8U;   /* total bits incl. reset-low tail */
    t.tx_buffer = backend->spi_buf;
    t.rx_buffer = NULL;
    esp_err_t ret = spi_device_polling_transmit(backend->spi_device, &t);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                 (uint32_t)backend->gpio, (uint32_t)ret, 4, 0);
        return ret;
    }
    return ESP_OK;
}

static void status_led_strip_backend_release_transport(status_led_strip_backend_t *backend)
{
    if (backend == NULL) {
        return;
    }
    if (backend->transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        status_led_spi_release_transport(backend);
        return;
    }
    if (backend->channel != NULL && backend->channel_enabled) {
        (void)rmt_disable(backend->channel);
        backend->channel_enabled = false;
    }
    if (backend->encoder != NULL) {
        (void)rmt_del_encoder(backend->encoder);
        backend->encoder = NULL;
    }
    if (backend->channel != NULL) {
        (void)rmt_del_channel(backend->channel);
        backend->channel = NULL;
    }
    backend->mem_block_symbols = 0U;
    backend->available = false;
    backend->dma_enabled = false;
    backend->gpio_idle_driven_low = false;
}

static esp_err_t status_led_strip_backend_new_channel(status_led_strip_backend_t *backend, bool with_dma);

static esp_err_t status_led_strip_backend_init_transport(
    status_led_strip_backend_t *backend,
    bool with_dma)
{
    if (backend == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (backend->gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }
    if (backend->transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        esp_err_t ret = status_led_spi_init_transport(backend);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG,
                 "strip %s SPI DMA transport failed gpio=%d host=%d: %s; falling back to non-DMA RMT",
                 backend->name,
                 (int)backend->gpio,
                 backend->spi_host,
                 esp_err_to_name(ret));
        status_led_spi_release_transport(backend);
        backend->transport = STATUS_LED_STRIP_TRANSPORT_RMT;
        backend->dma_requested = false;
        backend->dma_fallback = true;
        with_dma = false;
    }

    bool channel_with_dma = with_dma && STATUS_LED_RMT_WITH_DMA;
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

    backend->available = true;
    backend->dma_enabled = channel_with_dma;
    ESP_LOGI(
        TAG,
        "strip %s transport ready: gpio=%d leds=%u tx_leds=%u tail_guard_pixels=%u backend=rmt_ws2812_800khz rmt_dma_requested=%u rmt_dma=%u rmt_dma_fallback=%u mem_block_symbols=%u",
        backend->name,
        (int)backend->gpio,
        (unsigned)backend->led_count,
        (unsigned)backend->transmit_led_count,
        (unsigned)backend->tail_guard_pixels,
        backend->dma_requested ? 1U : 0U,
        backend->dma_enabled ? 1U : 0U,
        backend->dma_fallback ? 1U : 0U,
        (unsigned)backend->mem_block_symbols);
    return ESP_OK;
}

static esp_err_t status_led_strip_backend_ensure_transport(
    status_led_strip_backend_t *backend,
    bool with_dma)
{
    if (backend == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (backend->gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }

    if (backend->transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        /* SPI is DMA-backed by construction; ignore the with_dma hint and just
         * (re)acquire the bus if it is not already up. */
        if (backend->available && backend->spi_device != NULL) {
            return ESP_OK;
        }
        status_led_strip_backend_release_transport(backend);
        return status_led_strip_backend_init_transport(backend, true);
    }

    bool desired_dma = with_dma && backend->dma_requested && STATUS_LED_RMT_WITH_DMA;
    if (backend->available &&
        backend->channel != NULL &&
        backend->encoder != NULL &&
        backend->dma_enabled == desired_dma) {
        return ESP_OK;
    }

    status_led_strip_backend_release_transport(backend);
    return status_led_strip_backend_init_transport(backend, desired_dma);
}

static esp_err_t status_led_strip_backend_new_channel(status_led_strip_backend_t *backend, bool with_dma)
{
    const size_t mem_block_symbols = with_dma
        ? STATUS_LED_RMT_DMA_MEM_BLOCK_SYMBOLS
        : SOC_RMT_MEM_WORDS_PER_CHANNEL;
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = backend->gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = STATUS_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = mem_block_symbols,
        .trans_queue_depth = 1,
        .flags.with_dma = with_dma,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_config, &backend->channel);
    if (ret == ESP_OK) {
        backend->mem_block_symbols = mem_block_symbols;
    }
    return ret;
}

static esp_err_t status_led_strip_backend_set_channel_enabled(
    status_led_strip_backend_t *backend,
    bool enabled)
{
    if (backend == NULL || backend->channel == NULL || backend->channel_enabled == enabled) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (enabled) {
        if (backend->gpio_idle_driven_low) {
            ret = rmt_tx_switch_gpio(backend->channel, backend->gpio, false);
            if (ret != ESP_OK) {
                return ret;
            }
            backend->gpio_idle_driven_low = false;
        }
        ret = rmt_enable(backend->channel);
    } else {
        ret = rmt_disable(backend->channel);
    }
    if (ret == ESP_OK) {
        backend->channel_enabled = enabled;
    }
    return ret;
}

static void status_led_strip_backend_drive_idle_low(status_led_strip_backend_t *backend)
{
    if (backend == NULL || !GPIO_IS_VALID_OUTPUT_GPIO(backend->gpio)) {
        return;
    }
    gpio_num_t gpio = backend->gpio;
    (void)gpio_set_level(gpio, 0);
    (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    (void)gpio_set_level(gpio, 0);
    backend->gpio_idle_driven_low = true;
}

static esp_err_t status_led_rmt_transmit_available(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    if (!backend->available || backend->channel == NULL || backend->encoder == NULL) {
        return ESP_OK;
    }

    status_led_strip_backend_fill_pixels(backend, color_order, colors);
    (void)rmt_encoder_reset(backend->encoder);
    esp_err_t ret = status_led_strip_backend_set_channel_enabled(backend, true);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                 (uint32_t)backend->gpio, (uint32_t)ret, 2, 0);
        return ret;
    }
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    ret = rmt_transmit(
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
        (void)status_led_strip_backend_suspend(backend);
        return ret;
    }

    ret = rmt_tx_wait_all_done(backend->channel, STATUS_LED_RMT_WAIT_MS);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                 (uint32_t)backend->gpio, (uint32_t)ret, 1, 0);
        (void)status_led_strip_backend_suspend(backend);
        return ret;
    }
    return ESP_OK;
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
    backend->dark_latch_rmt_writes =
        config->dark_latch_rmt_writes > 0U ? config->dark_latch_rmt_writes : 1U;
    backend->transmit_led_count = config->led_count + config->tail_guard_pixels;
    backend->requested_transport = config->transport;
    backend->transport = config->transport;
    backend->spi_host = config->spi_host;
    /* SPI transport is DMA-backed by construction; RMT honors prefer_dma. */
    backend->dma_requested = (config->transport == STATUS_LED_STRIP_TRANSPORT_SPI)
        ? true
        : (config->prefer_dma && STATUS_LED_RMT_WITH_DMA);
    *ret_backend = backend;

    if (backend->gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "strip %s disabled: GPIO_NUM_NC", backend->name);
        return ESP_OK;
    }

    esp_err_t ret = status_led_strip_backend_init_transport(backend, backend->dma_requested);
    if (ret != ESP_OK) {
        return ret;
    }
    /* SPI strips already logged a transport-specific ready line from
     * status_led_spi_init_transport; only summarize RMT strips here. */
    if (backend->transport != STATUS_LED_STRIP_TRANSPORT_SPI) {
        ESP_LOGI(
            TAG,
            "strip %s ready: gpio=%d leds=%u tx_leds=%u tail_guard_pixels=%u backend=rmt_ws2812_800khz order=%s reset_us=300 timing=ws2812_4020_compatible rmt_dma_requested=%u rmt_dma=%u rmt_dma_fallback=%u mem_block_symbols=%u",
            backend->name,
            (int)backend->gpio,
            (unsigned)backend->led_count,
            (unsigned)backend->transmit_led_count,
            (unsigned)backend->tail_guard_pixels,
            status_led_color_order_name(config->color_order),
            backend->dma_requested ? 1U : 0U,
            backend->dma_enabled ? 1U : 0U,
            backend->dma_fallback ? 1U : 0U,
            (unsigned)backend->mem_block_symbols);
    }
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

status_led_strip_transport_t status_led_strip_backend_transport(const status_led_strip_backend_t *backend)
{
    return backend != NULL ? backend->transport : STATUS_LED_STRIP_TRANSPORT_RMT;
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

size_t status_led_strip_backend_mem_block_symbols(const status_led_strip_backend_t *backend)
{
    return backend != NULL ? backend->mem_block_symbols : 0U;
}

static esp_err_t status_led_strip_backend_transmit_mode(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors,
    bool with_dma)
{
    if (backend == NULL) {
        return ESP_OK;
    }
    esp_err_t ret = status_led_strip_backend_ensure_transport(backend, with_dma);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                 (uint32_t)backend->gpio, (uint32_t)ret, 3, 0);
        return ret;
    }
    if (backend->transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        if (!backend->available || backend->spi_device == NULL) {
            return ESP_OK;
        }
        return status_led_spi_transmit(backend, color_order, colors);
    }
    return status_led_rmt_transmit_available(backend, color_order, colors);
}

static esp_err_t status_led_spi_transmit_non_dma_rmt_once(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    status_led_strip_transport_t saved_transport = backend->transport;
    bool saved_dma_requested = backend->dma_requested;
    bool saved_dma_fallback = backend->dma_fallback;
    bool dark_latch = status_led_strip_backend_colors_all_dark(backend, colors);
    uint8_t rmt_latch_writes =
        (dark_latch && backend->dark_latch_rmt_writes > 0U)
            ? backend->dark_latch_rmt_writes
            : 1U;

    esp_err_t acquire_ret = status_led_strip_backend_ensure_transport(backend, true);
    if (acquire_ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "strip %s SPI DMA pre-latch acquire failed before non-DMA RMT latch: gpio=%d host=%d ret=%s",
            backend->name,
            (int)backend->gpio,
            backend->spi_host,
            esp_err_to_name(acquire_ret));
    }
    if (backend->available && backend->spi_device != NULL) {
        esp_err_t pre_ret = status_led_spi_transmit(backend, color_order, colors);
        if (pre_ret == ESP_OK) {
            ESP_LOGI(
                TAG,
                "strip %s SPI DMA pre-latch before non-DMA RMT latch: gpio=%d host=%d",
                backend->name,
                (int)backend->gpio,
                backend->spi_host);
        } else {
            ESP_LOGW(
                TAG,
                "strip %s SPI DMA pre-latch failed before non-DMA RMT latch: gpio=%d host=%d ret=%s",
                backend->name,
                (int)backend->gpio,
                backend->spi_host,
                esp_err_to_name(pre_ret));
        }
    }

    status_led_strip_backend_release_transport(backend);
    backend->transport = STATUS_LED_STRIP_TRANSPORT_RMT;
    backend->dma_requested = false;
    backend->dma_enabled = false;

    esp_err_t ret = status_led_strip_backend_init_transport(backend, false);
    if (ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "strip %s non-DMA one-shot via RMT for SPI DMA latch: gpio=%d host=%d rmt_writes=%u dark=%u",
            backend->name,
            (int)backend->gpio,
            backend->spi_host,
            (unsigned)rmt_latch_writes,
            dark_latch ? 1U : 0U);
        for (uint8_t write_index = 0U; write_index < rmt_latch_writes; ++write_index) {
            ret = status_led_rmt_transmit_available(backend, color_order, colors);
            if (ret != ESP_OK) {
                break;
            }
        }
    }

    status_led_strip_backend_release_transport(backend);
    backend->transport = saved_transport;
    backend->dma_requested = saved_dma_requested;
    backend->dma_fallback = saved_dma_fallback;
    backend->available = false;
    backend->dma_enabled = false;
    backend->channel_enabled = false;
    status_led_strip_backend_drive_idle_low(backend);
    return ret;
}

esp_err_t status_led_strip_backend_transmit(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    return status_led_strip_backend_transmit_mode(backend, color_order, colors, true);
}

esp_err_t status_led_strip_backend_transmit_non_dma_once(
    status_led_strip_backend_t *backend,
    status_led_color_order_t color_order,
    const status_led_rgb_t *colors)
{
    if (backend != NULL &&
        backend->requested_transport == STATUS_LED_STRIP_TRANSPORT_SPI &&
        backend->transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        return status_led_spi_transmit_non_dma_rmt_once(backend, color_order, colors);
    }
    return status_led_strip_backend_transmit_mode(backend, color_order, colors, false);
}

esp_err_t status_led_strip_backend_suspend(status_led_strip_backend_t *backend)
{
    if (backend == NULL || backend->gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }
    if (backend->transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        /* Release the SPI bus so the data GPIO is freed from the SPI output
         * matrix and can be driven low for the low-power idle latch (matches
         * the active-DMA / idle-GPIO-low pattern). The bus is re-acquired on
         * the next ensure_transport(). */
        if (backend->available) {
            status_led_spi_release_transport(backend);
        }
        status_led_strip_backend_drive_idle_low(backend);
        return ESP_OK;
    }
    status_led_strip_backend_release_transport(backend);
    status_led_strip_backend_drive_idle_low(backend);
    return ESP_OK;
}
