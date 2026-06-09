#include "board.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "battery_monitor.h"
#include "board_pins.h"
#include "diag_log.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "watchdog_platform.h"

static const char *TAG = "board";

#define BOARD_USB_PREFIX "BOARD:"

#define BOARD_V2_USB_DET_POLICY "v2_gpio7_r37_r32_10K_10K_divider"
#define BOARD_V2_CHARGER_POLARITY "v2_gpio14_chg_gpio21_std_active_low"
#define BOARD_V2_PWR_HOLD_POLICY "v2_gpio11_power_latch_hold_low_release_high_for_hardware_shutdown"
#define BOARD_V2_LED_POLICY "v2_four_zone_ws2812_status_gpio1_ec11_gpio5_key_gpio13_edge_gpio4"
#define BOARD_V2_MIC_POLICY "v2_sph0655_pdm_clk_gpio48_dout_gpio47_enabled_for_a1_a2_hardware_validation"
#if BOARD_PINS_CURRENT_TELEMETRY_PRESENT
#define BOARD_V2_CURRENT_POLICY "v2_battery_side_input_branch_current_ina180a2_10mR_adc_mv_x2_with_battery_mv_from_gpio8_div2"
#else
#define BOARD_V2_CURRENT_POLICY "v2_optional_current_telemetry_not_populated_battery_adc_only_no_power_decisions"
#endif

typedef struct {
    const char *name;
    gpio_num_t data_gpio;
    uint8_t first_led;
    uint8_t led_count;
    uint8_t brightness_cap_percent;
    const char *led_refs;
    const char *policy;
} board_led_group_t;

static const board_led_group_t s_led_groups[] = {
    {
        .name = "status",
        .data_gpio = BOARD_PINS_RGB_STATUS_IO,
        .first_led = 1,
        .led_count = 6,
        .brightness_cap_percent = 8,
        .led_refs = "LED1..LED6",
        .policy = "LED1..LED6 semantic PWR/BLE/REC/AI/OK/WARN rail",
    },
    {
        .name = "ec11",
        .data_gpio = BOARD_PINS_RGB_EC11_IO,
        .first_led = 7,
        .led_count = 12,
        .brightness_cap_percent = 8,
        .led_refs = "LED7..LED10+LED15..LED16+LED23..LED28",
        .policy = "EC11 knob ring feedback on the dedicated PWM_RGB_EC11 strip",
    },
    {
        .name = "key",
        .data_gpio = BOARD_PINS_RGB_KEY_IO,
        .first_led = 11,
        .led_count = 4,
        .brightness_cap_percent = 8,
        .led_refs = "LED11..LED14",
        .policy = "LED11..LED14 transient local key feedback",
    },
    {
        .name = "edge",
        .data_gpio = BOARD_PINS_RGB_EDGE_IO,
        .first_led = 17,
        .led_count = 6,
        .brightness_cap_percent = 4,
        .led_refs = "LED17..LED22",
        .policy = "LED17..LED22 restrained edge/frame effects",
    },
};

static bool s_power_hold_configured;

static bool board_command_matches(const char *line, const char *prefix, const char **out_command)
{
    if (line == NULL || prefix == NULL || out_command == NULL) {
        return false;
    }
    if (*line == '~') {
        line++;
    }
    size_t prefix_len = strlen(prefix);
    if (strncmp(line, prefix, prefix_len) != 0) {
        return false;
    }
    *out_command = line + prefix_len;
    return true;
}

static void board_configure_status_input(gpio_num_t gpio)
{
    if (gpio == GPIO_NUM_NC || gpio < 0 || gpio >= GPIO_NUM_MAX) {
        return;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "status input config failed: gpio=%d ret=%s", (int)gpio, esp_err_to_name(ret));
    }
}

static const char *board_gpio_level_name(int level)
{
    if (level < 0) {
        return "unknown";
    }
    return level ? "high" : "low";
}

static int board_read_gpio_level(gpio_num_t gpio)
{
    if (gpio == GPIO_NUM_NC || gpio < 0 || gpio >= GPIO_NUM_MAX) {
        return -1;
    }
    return gpio_get_level(gpio);
}

static bool board_gpio_scan_is_valid(gpio_num_t gpio)
{
    if (gpio == GPIO_NUM_NC || gpio < 0 || gpio >= GPIO_NUM_MAX) {
        return false;
    }
#ifdef GPIO_IS_VALID_GPIO
    return GPIO_IS_VALID_GPIO(gpio);
#else
    return true;
#endif
}

static const char *board_gpio_scan_label(gpio_num_t gpio)
{
    if (gpio == BOARD_PINS_KEY1_IO) {
        return "KEY1";
    }
    if (gpio == BOARD_PINS_KEY2_IO) {
        return "KEY2";
    }
    if (gpio == BOARD_PINS_KEY3_IO) {
        return "KEY3";
    }
    if (gpio == BOARD_PINS_KEY4_IO) {
        return "KEY4";
    }
    if (gpio == BOARD_PINS_EC11_A_IO) {
        return "EC11_A";
    }
    if (gpio == BOARD_PINS_EC11_B_IO) {
        return "EC11_B";
    }
    if (gpio == BOARD_PINS_EC11_KEY_IO) {
        return "EC11_KEY";
    }
    if (gpio == BOARD_PINS_USB_DET_IO) {
        return "USB_DET";
    }
    if (gpio == BOARD_PINS_BAT_CHG_IO) {
        return "BAT_CHG";
    }
    if (gpio == BOARD_PINS_BAT_STD_IO) {
        return "BAT_STD";
    }
    if (gpio == BOARD_PINS_BAT_V_ADC_IO) {
        return "BAT_V_ADC";
    }
    if (gpio == BOARD_PINS_TPS63020_I_ADC_IO) {
        return "TPS_I_ADC";
    }
    if (gpio == BOARD_PINS_SY7088_I_ADC_IO) {
        return "SY7088_I_ADC";
    }
    if (gpio == BOARD_PINS_I2S_BCLK_IO) {
        return "MIC_CLK";
    }
    if (gpio == BOARD_PINS_I2S_DIN_IO) {
        return "MIC_DOUT";
    }
    if (gpio == BOARD_PINS_RGB_STATUS_IO) {
        return "RGB_STATUS";
    }
    if (gpio == BOARD_PINS_RGB_EC11_IO) {
        return "RGB_EC11";
    }
    if (gpio == BOARD_PINS_RGB_KEY_IO) {
        return "RGB_KEY";
    }
    if (gpio == BOARD_PINS_RGB_EDGE_IO) {
        return "RGB_EDGE";
    }
    if (gpio == BOARD_PINS_PWR_HOLD_IO) {
        return "PWR_HOLD";
    }
    return "-";
}

esp_err_t board_configure_power_hold_latch(void)
{
    if (BOARD_PINS_PWR_HOLD_IO == GPIO_NUM_NC ||
        BOARD_PINS_PWR_HOLD_IO < 0 ||
        BOARD_PINS_PWR_HOLD_IO >= GPIO_NUM_MAX) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)BOARD_PINS_PWR_HOLD_IO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "PWR_HOLD/GPIO11 output config failed: gpio=%d ret=%s",
            (int)BOARD_PINS_PWR_HOLD_IO,
            esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_level(BOARD_PINS_PWR_HOLD_IO, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "PWR_HOLD/GPIO11 hold-low failed: gpio=%d ret=%s",
            (int)BOARD_PINS_PWR_HOLD_IO,
            esp_err_to_name(ret));
        return ret;
    }

    s_power_hold_configured = true;
    return ESP_OK;
}

esp_err_t board_set_power_hold_enabled(bool enabled)
{
    esp_err_t ret = board_configure_power_hold_latch();
    if (ret != ESP_OK) {
        return ret;
    }

    int level = enabled ? 0 : 1;
    ret = gpio_set_level(BOARD_PINS_PWR_HOLD_IO, level);
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "PWR_HOLD/GPIO11 set failed: enabled=%u gpio=%d ret=%s",
            enabled ? 1u : 0u,
            (int)BOARD_PINS_PWR_HOLD_IO,
            esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGW(
        TAG,
        "PWR_HOLD/GPIO11 %s: gpio=%d level=%d policy=%s",
        enabled ? "held low" : "released high for hardware shutdown",
        (int)BOARD_PINS_PWR_HOLD_IO,
        level,
        BOARD_V2_PWR_HOLD_POLICY);
    return ESP_OK;
}

void board_get_v2_power_hold_snapshot(board_v2_power_hold_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }

    if (!s_power_hold_configured) {
        (void)board_configure_power_hold_latch();
    }

    *out_snapshot = (board_v2_power_hold_snapshot_t){
        .gpio = (int)BOARD_PINS_PWR_HOLD_IO,
        .level = board_read_gpio_level(BOARD_PINS_PWR_HOLD_IO),
        .configured = s_power_hold_configured,
        .policy = BOARD_V2_PWR_HOLD_POLICY,
    };
}

void board_get_v2_power_input_snapshot(board_v2_power_input_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }

    board_configure_status_input(BOARD_PINS_USB_DET_IO);
    board_configure_status_input(BOARD_PINS_BAT_CHG_IO);
    board_configure_status_input(BOARD_PINS_BAT_STD_IO);
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);

    *out_snapshot = (board_v2_power_input_snapshot_t){
        .usb_det_level = board_read_gpio_level(BOARD_PINS_USB_DET_IO),
        .bat_chg_level = board_read_gpio_level(BOARD_PINS_BAT_CHG_IO),
        .bat_std_level = board_read_gpio_level(BOARD_PINS_BAT_STD_IO),
        .pwr_hold_level = power_hold.level,
        .usb_det_policy = BOARD_V2_USB_DET_POLICY,
        .charger_polarity_policy = BOARD_V2_CHARGER_POLARITY,
        .pwr_hold_policy = BOARD_V2_PWR_HOLD_POLICY,
    };
}

static void board_print_power_rail_status(battery_monitor_power_rail_t rail)
{
    battery_monitor_power_rail_status_t status = {0};
    esp_err_t ret = battery_monitor_read_power_rail(rail, &status);
    uint32_t rail_code = rail == BATTERY_MONITOR_POWER_RAIL_3V3 ? 1u : 2u;
    diag_log(DIAG_SRC_BOARD, DIAG_BOARD_POWER_RAIL,
             status.valid || !status.current_telemetry_present ? DIAG_SEV_INFO : DIAG_SEV_WARN,
             rail_code,
             (uint32_t)(status.raw_adc < 0 ? 0 : status.raw_adc),
             (uint32_t)(status.adc_mv < 0 ? 0 : status.adc_mv),
             status.adc_calibrated ? 1u : 0u);
    printf(
        "~BOARD:POWER branch=%s present=%u gpio=%" PRId32
        " raw_adc=%d adc_mv=%d adc_calibrated=%u sample_count=%u"
        " calibration_status=%s current_model=\"%s\""
        " current_calibrated=%u current_ma_valid=%u estimated_input_current_ma=%" PRId32
        " battery_side_mv=%" PRIu32 " battery_voltage_source=\"BAT_V_ADC/GPIO8 68K/68K midpoint, VBAT~=2*ADC\""
        " power_mw_valid=%u estimated_input_power_mw=%" PRId32
        " result=%s policy=%s\n",
        status.rail_name,
        status.current_telemetry_present ? 1u : 0u,
        status.gpio,
        status.raw_adc,
        status.adc_mv,
        status.adc_calibrated ? 1u : 0u,
        status.sample_count,
        status.calibration_status,
        status.current_telemetry_present ? "INA180A2 10mR current_mA=adc_mv*2" : "not_populated",
        status.current_calibrated ? 1u : 0u,
        status.current_ma_valid ? 1u : 0u,
        status.estimated_current_ma,
        status.nominal_rail_mv,
        status.power_mw_valid ? 1u : 0u,
        status.estimated_power_mw,
        esp_err_to_name(ret),
        BOARD_V2_CURRENT_POLICY);
}

static void board_print_led_status(void)
{
    for (size_t i = 0; i < sizeof(s_led_groups) / sizeof(s_led_groups[0]); ++i) {
        const board_led_group_t *group = &s_led_groups[i];
        diag_log(DIAG_SRC_BOARD, DIAG_BOARD_LED_RESOURCE, DIAG_SEV_INFO,
                 (uint32_t)(i + 1u),
                 (uint32_t)group->data_gpio,
                 group->first_led,
                 group->led_count);
        printf(
            "~LED:STATUS group=%s transport=WS2812 data_gpio=%d first_led=%u led_count=%u led_refs=\"%s\""
            " brightness_cap_percent=%u vdd_led_signed_off=0 full_white_allowed=0"
            " rgbw_calibration_path=provisional policy=\"%s\"\n",
            group->name,
            (int)group->data_gpio,
            group->first_led,
            group->led_count,
            group->led_refs,
            group->brightness_cap_percent,
            group->policy);
    }
    printf("~LED:STATUS policy=%s\n", BOARD_V2_LED_POLICY);
    fflush(stdout);
}

static void board_print_gpio_status(void)
{
    /* Read as configured so diagnostics do not clear EC11 edge interrupts. */
    int key1 = board_read_gpio_level(BOARD_PINS_KEY1_IO);
    int key2 = board_read_gpio_level(BOARD_PINS_KEY2_IO);
    int key3 = board_read_gpio_level(BOARD_PINS_KEY3_IO);
    int key4 = board_read_gpio_level(BOARD_PINS_KEY4_IO);
    int ec11_a = board_read_gpio_level(BOARD_PINS_EC11_A_IO);
    int ec11_b = board_read_gpio_level(BOARD_PINS_EC11_B_IO);
    int ec11_key = board_read_gpio_level(BOARD_PINS_EC11_KEY_IO);
    uint32_t key_pressed_mask =
        (key1 == 0 ? 0x01u : 0u) |
        (key2 == 0 ? 0x02u : 0u) |
        (key3 == 0 ? 0x04u : 0u) |
        (key4 == 0 ? 0x08u : 0u);
    uint32_t ec11_ab_state =
        (ec11_a > 0 ? 0x01u : 0u) |
        (ec11_b > 0 ? 0x02u : 0u);

    printf(
        "~BOARD:GPIO active_low=1 mode=read_as_configured reconfigure=0"
        " key1_gpio=%d key1_level=%s key1_pressed=%u"
        " key2_gpio=%d key2_level=%s key2_pressed=%u"
        " key3_gpio=%d key3_level=%s key3_pressed=%u"
        " key4_gpio=%d key4_level=%s key4_pressed=%u"
        " key_pressed_mask=0x%02" PRIx32
        " ec11_a_gpio=%d ec11_a_level=%s"
        " ec11_b_gpio=%d ec11_b_level=%s"
        " ec11_ab_state=0x%02" PRIx32
        " ec11_key_gpio=%d ec11_key_level=%s ec11_key_pressed=%u"
        " recording_key=EC11_KEY/GPIO18\n",
        (int)BOARD_PINS_KEY1_IO,
        board_gpio_level_name(key1),
        key1 == 0 ? 1u : 0u,
        (int)BOARD_PINS_KEY2_IO,
        board_gpio_level_name(key2),
        key2 == 0 ? 1u : 0u,
        (int)BOARD_PINS_KEY3_IO,
        board_gpio_level_name(key3),
        key3 == 0 ? 1u : 0u,
        (int)BOARD_PINS_KEY4_IO,
        board_gpio_level_name(key4),
        key4 == 0 ? 1u : 0u,
        key_pressed_mask,
        (int)BOARD_PINS_EC11_A_IO,
        board_gpio_level_name(ec11_a),
        (int)BOARD_PINS_EC11_B_IO,
        board_gpio_level_name(ec11_b),
        ec11_ab_state,
        (int)BOARD_PINS_EC11_KEY_IO,
        board_gpio_level_name(ec11_key),
        ec11_key == 0 ? 1u : 0u);
    fflush(stdout);
}

static void board_print_gpio_scan_mask(const char *prefix, uint64_t mask)
{
    printf("%s", prefix);
    for (int gpio = 0; gpio < GPIO_NUM_MAX; ++gpio) {
        if ((mask & (1ULL << (uint32_t)gpio)) == 0) {
            continue;
        }
        printf(" gpio%d:%s", gpio, board_gpio_scan_label((gpio_num_t)gpio));
    }
    printf("\n");
}

static void board_print_gpio_scan(void)
{
    enum {
        sample_count = 250,
        interval_ms = 20,
    };
    int previous_levels[GPIO_NUM_MAX];
    bool valid_gpios[GPIO_NUM_MAX];
    memset(previous_levels, 0xff, sizeof(previous_levels));
    memset(valid_gpios, 0, sizeof(valid_gpios));

    uint64_t valid_mask = 0;
    for (int gpio = 0; gpio < GPIO_NUM_MAX; ++gpio) {
        gpio_num_t gpio_num = (gpio_num_t)gpio;
        valid_gpios[gpio] = board_gpio_scan_is_valid(gpio_num);
        if (valid_gpios[gpio]) {
            valid_mask |= 1ULL << (uint32_t)gpio;
        }
    }

    printf("~BOARD:GPIO_SCAN begin samples=%u interval_ms=%u mode=read_as_configured reconfigure=0 valid_mask=0x%016llx\n",
           (unsigned)sample_count,
           (unsigned)interval_ms,
           (unsigned long long)valid_mask);
    fflush(stdout);

    uint64_t final_low_mask = 0;
    uint64_t change_mask = 0;
    for (unsigned sample = 0; sample < sample_count; ++sample) {
        watchdog_platform_feed_current_task();
        uint64_t low_mask = 0;
        uint64_t high_mask = 0;
        for (int gpio = 0; gpio < GPIO_NUM_MAX; ++gpio) {
            if (!valid_gpios[gpio]) {
                continue;
            }
            int level = board_read_gpio_level((gpio_num_t)gpio);
            if (level == 0) {
                low_mask |= 1ULL << (uint32_t)gpio;
            } else if (level > 0) {
                high_mask |= 1ULL << (uint32_t)gpio;
            }

            if (sample == 0) {
                previous_levels[gpio] = level;
                continue;
            }
            if (level != previous_levels[gpio]) {
                previous_levels[gpio] = level;
                change_mask |= 1ULL << (uint32_t)gpio;
                printf("~BOARD:GPIO_SCAN_CHANGE sample=%u elapsed_ms=%u gpio=%d label=%s level=%s\n",
                       sample,
                       sample * interval_ms,
                       gpio,
                       board_gpio_scan_label((gpio_num_t)gpio),
                       board_gpio_level_name(level));
                fflush(stdout);
            }
        }
        if (sample == 0) {
            printf("~BOARD:GPIO_SCAN_INITIAL low_mask=0x%016llx high_mask=0x%016llx\n",
                   (unsigned long long)low_mask,
                   (unsigned long long)high_mask);
            board_print_gpio_scan_mask("~BOARD:GPIO_SCAN_INITIAL_LOW", low_mask);
            fflush(stdout);
        }
        final_low_mask = low_mask;
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
    watchdog_platform_feed_current_task();

    printf("~BOARD:GPIO_SCAN done change_mask=0x%016llx final_low_mask=0x%016llx\n",
           (unsigned long long)change_mask,
           (unsigned long long)final_low_mask);
    board_print_gpio_scan_mask("~BOARD:GPIO_SCAN_CHANGED", change_mask);
    board_print_gpio_scan_mask("~BOARD:GPIO_SCAN_FINAL_LOW", final_low_mask);
    fflush(stdout);
}

static void board_print_status(void)
{
    battery_monitor_status_t battery = {0};
    esp_err_t battery_ret = battery_monitor_read(&battery);
    board_v2_power_input_snapshot_t power_inputs = {0};
    board_get_v2_power_input_snapshot(&power_inputs);
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);

    printf(
        "~BOARD:STATUS profile=%s module=%s flash_mb=%u psram_mb=%u psram_mode=%s"
        " key_gpios=%d,%d,%d,%d ec11_a_gpio=%d ec11_b_gpio=%d ec11_key_gpio=%d"
        " ec11_key_provisional=1 mic_clk_gpio=%d mic_dout_gpio=%d mic_policy=%s"
        " pwr_hold_gpio=%d pwr_hold_level=%s pwr_hold_configured=%u pwr_hold_policy=%s"
        " usb_det_gpio=%d usb_det_level=%s usb_det_policy=%s"
        " bat_chg_gpio=%d bat_chg_level=%s bat_std_gpio=%d bat_std_level=%s charger_polarity=%s"
        " battery_gpio=%d battery_mv=%" PRIu32 " battery_adc_mv=%d battery_raw=%d"
        " battery_level=%u battery_valid=%u battery_adc_calibrated=%u battery_samples=%u battery_result=%s"
        " battery_scaling=\"68K/68K divider, VBAT~=2*ADC\" battery_policy=\"source_impedance_filter_calibration_provisional\""
        " reserved_mspi_gpio=%s\n",
        BOARD_PINS_PROFILE_ID,
        BOARD_PINS_MODULE,
        (unsigned)BOARD_PINS_FLASH_SIZE_MB,
        (unsigned)BOARD_PINS_PSRAM_SIZE_MB,
        BOARD_PINS_PSRAM_MODE,
        (int)BOARD_PINS_KEY1_IO,
        (int)BOARD_PINS_KEY2_IO,
        (int)BOARD_PINS_KEY3_IO,
        (int)BOARD_PINS_KEY4_IO,
        (int)BOARD_PINS_EC11_A_IO,
        (int)BOARD_PINS_EC11_B_IO,
        (int)BOARD_PINS_EC11_KEY_IO,
        (int)BOARD_PINS_MIC_CLK_IO,
        (int)BOARD_PINS_MIC_DOUT_IO,
        BOARD_V2_MIC_POLICY,
        power_hold.gpio,
        board_gpio_level_name(power_hold.level),
        power_hold.configured ? 1u : 0u,
        power_hold.policy,
        (int)BOARD_PINS_USB_DET_IO,
        board_gpio_level_name(power_inputs.usb_det_level),
        power_inputs.usb_det_policy,
        (int)BOARD_PINS_BAT_CHG_IO,
        board_gpio_level_name(power_inputs.bat_chg_level),
        (int)BOARD_PINS_BAT_STD_IO,
        board_gpio_level_name(power_inputs.bat_std_level),
        power_inputs.charger_polarity_policy,
        (int)BOARD_PINS_BAT_V_ADC_IO,
        battery.voltage_mv,
        battery.adc_mv,
        battery.raw_adc,
        battery.level_percent,
        battery.valid ? 1u : 0u,
        battery.adc_calibrated ? 1u : 0u,
        battery.sample_count,
        esp_err_to_name(battery_ret),
        BOARD_PINS_RESERVED_MSPI_GPIOS);
    board_print_power_rail_status(BATTERY_MONITOR_POWER_RAIL_3V3);
    board_print_power_rail_status(BATTERY_MONITOR_POWER_RAIL_LED_5V);
    board_print_led_status();
    fflush(stdout);
}

void board_log_v2_diagnostics(void)
{
    esp_err_t pwr_hold_ret = board_configure_power_hold_latch();
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);

    ESP_LOGI(
        TAG,
        "board profile: id=%s module=%s flash=%uMB psram=%uMB %s key_gpios=%d,%d,%d,%d ec11=%d,%d,%d mic=%d,%d usb_det=%d charger=%d,%d battery_adc=%d current_adc=%d,%d rgb=%d,%d,%d,%d pwr_hold=%d pwr_hold_level=%s pwr_hold_configured=%u reserved_mspi=%s",
        BOARD_PINS_PROFILE_ID,
        BOARD_PINS_MODULE,
        (unsigned)BOARD_PINS_FLASH_SIZE_MB,
        (unsigned)BOARD_PINS_PSRAM_SIZE_MB,
        BOARD_PINS_PSRAM_MODE,
        (int)BOARD_PINS_KEY1_IO,
        (int)BOARD_PINS_KEY2_IO,
        (int)BOARD_PINS_KEY3_IO,
        (int)BOARD_PINS_KEY4_IO,
        (int)BOARD_PINS_EC11_A_IO,
        (int)BOARD_PINS_EC11_B_IO,
        (int)BOARD_PINS_EC11_KEY_IO,
        (int)BOARD_PINS_MIC_CLK_IO,
        (int)BOARD_PINS_MIC_DOUT_IO,
        (int)BOARD_PINS_USB_DET_IO,
        (int)BOARD_PINS_BAT_CHG_IO,
        (int)BOARD_PINS_BAT_STD_IO,
        (int)BOARD_PINS_BAT_V_ADC_IO,
        (int)BOARD_PINS_TPS63020_I_ADC_IO,
        (int)BOARD_PINS_SY7088_I_ADC_IO,
        (int)BOARD_PINS_RGB_STATUS_IO,
        (int)BOARD_PINS_RGB_EC11_IO,
        (int)BOARD_PINS_RGB_KEY_IO,
        (int)BOARD_PINS_RGB_EDGE_IO,
        (int)BOARD_PINS_PWR_HOLD_IO,
        board_gpio_level_name(power_hold.level),
        power_hold.configured ? 1u : 0u,
        BOARD_PINS_RESERVED_MSPI_GPIOS);
    if (pwr_hold_ret != ESP_OK) {
        ESP_LOGW(TAG, "PWR_HOLD/GPIO11 hold-low setup failed: %s", esp_err_to_name(pwr_hold_ret));
    }
    ESP_LOGW(TAG, "board hardware provisional: usb_det=%s charger=%s pwr_hold=%s current=%s led=%s mic=%s",
             BOARD_V2_USB_DET_POLICY,
             BOARD_V2_CHARGER_POLARITY,
             BOARD_V2_PWR_HOLD_POLICY,
             BOARD_V2_CURRENT_POLICY,
             BOARD_V2_LED_POLICY,
             BOARD_V2_MIC_POLICY);
    diag_log(DIAG_SRC_BOARD, DIAG_BOARD_PROFILE, DIAG_SEV_INFO,
             BOARD_PINS_FLASH_SIZE_MB, BOARD_PINS_PSRAM_SIZE_MB,
             (uint32_t)BOARD_PINS_KEY1_IO, (uint32_t)BOARD_PINS_EC11_KEY_IO);
    diag_log(DIAG_SRC_BOARD, DIAG_BOARD_PROVISIONAL, DIAG_SEV_WARN,
             (uint32_t)BOARD_PINS_USB_DET_IO,
             (uint32_t)BOARD_PINS_PWR_HOLD_IO,
             (uint32_t)BOARD_PINS_TPS63020_I_ADC_IO,
             (uint32_t)BOARD_PINS_SY7088_I_ADC_IO);
}

void board_print_help(void)
{
    static const char *help_string =
        "########################################################################\n"
        "Listener voice keyboard firmware usage:\n"
        "Board profile: Voice Keyboard V2, ESP32-S3-WROOM-1-N16R8, 16MB flash, 8MB Octal PSRAM.\n"
        "Inject test bytes with tools/send_serial.ps1 or type in monitor.\n"
        "Capture 3s audio WAV with tools/capture_audio_wav.ps1 -Port COM3.\n"
        "Capture toggle session WAV with tools/capture_audio_session_wav.ps1 -Port COM3.\n"
        "EC11 push/GPIO18 controls recording: single click starts or stops after the 200 ms double-click window.\n"
        "EC11 push fast double-click clears BLE pairing/session state after recording has been idle; long press is reserved for hardware power control.\n"
        "Logical custom keys: single-click KEY1-KEY4 fallback=F13-F16, double-click=F17-F20, long-press=F21-F24.\n"
        "KEY1/GPIO38, KEY2/GPIO39, KEY3/GPIO40, KEY4/GPIO41 send safe non-text BLE HID usages while Listener-Type custom actions are unavailable.\n"
        "Send ~VREC:RECOVERY to clear pairing/session state over USB.\n"
        "Board diagnostics: ~BOARD:STATUS reports V2 pin, USB, charger, battery, PWR_HOLD/GPIO11, mic, reserved MSPI, and LED resource status.\n"
        "Board GPIO diagnostics: ~BOARD:GPIO reads raw KEY1-KEY4 and EC11 A/B/key levels without reconfiguring pins.\n"
        "Board GPIO scan: ~BOARD:GPIO-SCAN samples all valid GPIO levels without reconfiguring pins and prints changed GPIOs.\n"
        "Input flash debug: ~DIAGLOG:INPUTDBG:ON records high-volume key/EC11 debug events until ~DIAGLOG:INPUTDBG:OFF or reboot.\n"
        "Power diagnostics: ~POWER:STATUS reports state/blockers/battery/power-hold status, ~POWER:SHUTDOWN requests manual hardware shutdown.\n"
        "LED diagnostics: ~LED:STATUS reports four WS2812 groups; ~LED:TEST:RGBW and ~LED:TEST:MAP stay brightness-gated until VDD_LED sign-off.\n"
        "Watchdog diagnostics: ~WDT:STATUS reports config, ~WDT:DEADLOCK intentionally triggers Task WDT reset.\n"
        "Boot safety diagnostics: ~BOOT:STATUS reports crash counter, ~BOOT:CRASH restarts for validation, ~BOOT:CLEAR clears safe mode.\n"
        "Use ~OTA:STATUS, ~OTA:BLOCKER, or ~OTA:ABORT for firmware OTA diagnostics.\n"
        "Use ~DIAGLOG:COUNT, ~DIAGLOG:LAST:N, ~DIAGLOG:DUMP, or ~DIAGLOG:CLEAR for diagnostics.\n"
        "Device status logs use ready, recording, transferring, error, and recovery.\n"
        "End-to-end keystroke delivery still requires a BLE host connection.\n"
        "########################################################################";

    ESP_LOGI("board", "%s", help_string);
}

bool board_consume_usb_command(const char *line)
{
    const char *command = NULL;
    if (board_command_matches(line, BOARD_USB_PREFIX, &command)) {
        if (strcmp(command, "STATUS") == 0) {
            board_print_status();
            return true;
        }
        if (strcmp(command, "GPIO") == 0) {
            board_print_gpio_status();
            return true;
        }
        if (strcmp(command, "GPIO-SCAN") == 0) {
            board_print_gpio_scan();
            return true;
        }
        ESP_LOGW(TAG, "BOARD: unknown command: %s", command);
        return true;
    }

    return false;
}
