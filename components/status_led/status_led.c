#include "status_led.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "battery_monitor.h"
#include "board.h"
#include "board_pins.h"
#include "device_settings.h"
#include "diag_log.h"
#include "power_manager.h"
#include "status_led_strip_backend.h"
#include "watchdog_platform.h"

#define STATUS_LED_STATUS_COUNT 6
#define STATUS_LED_EC11_COUNT 12
#define STATUS_LED_KEY_COUNT 4
#define STATUS_LED_EDGE_COUNT 6
#define STATUS_LED_STRIP_COUNT 4
#define STATUS_LED_MAX_STRIP_COUNT STATUS_LED_EC11_COUNT
#define STATUS_LED_STATUS_TAIL_GUARD_PIXELS 6U
#define STATUS_LED_KEY_TAIL_GUARD_PIXELS 0U
#define STATUS_LED_KEY_DARK_LATCH_RMT_WRITES 2U
#define STATUS_LED_STATUS_TAIL_REINFORCE_WRITES 3U
#define STATUS_LED_STATUS_TAIL_OVERLAP_REINFORCE_WRITES 1U
#define STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MIN_PERCENT 14U
#define STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MAX_PERCENT 20U
#define STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MIN_PERCENT 14U
#define STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MAX_PERCENT 40U
#define STATUS_LED_STATUS_TAIL_OVERLAP_BREATH_PERIOD_MS 6800U
#define STATUS_LED_STATUS_TAIL_OVERLAP_RISE_MS 2200U
#define STATUS_LED_STATUS_TAIL_OVERLAP_HIGH_HOLD_MS 500U
#define STATUS_LED_STATUS_TAIL_OVERLAP_FALL_MS 2500U
#define STATUS_LED_STATUS_TAIL_OVERLAP_LOW_HOLD_MS 1600U
#define STATUS_LED_STATUS_TAIL_OVERLAP_QUANTUM_PERCENT 1U
// Routine effects express their peaks against the Type/user-defined maximum; the
// renderer applies plugged/battery and per-zone caps after these ratios.
#define STATUS_LED_TYPE_DEFINED_MAX_PERCENT 100U
// REC/AI motion stays on the status strip; audio only updates a sampled envelope.
#define STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT 8U
#define STATUS_LED_RECORDING_LEVEL_EFFECT_MAX_PERCENT 100U
#define STATUS_LED_RECORDING_LEVEL_ATTACK_PERCENT_PER_SEC 100U
#define STATUS_LED_RECORDING_LEVEL_RELEASE_PERCENT_PER_SEC 45U
#define STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT 2U
#define STATUS_LED_RECORDING_LEVEL_LOCK_WAIT_MS 0U
#define STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT 0U
#define STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT 100U
#define STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT 2U
#define STATUS_LED_PROCESSING_THINK_PERIOD_MS 1950U
#define STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS 50U
#define STATUS_LED_PROCESSING_THINK_BEAT_HOLD_MS 50U
#define STATUS_LED_PROCESSING_THINK_BEAT_FALL_MS 60U
#define STATUS_LED_PROCESSING_THINK_GROUP_GAP_MS 520U
#define STATUS_LED_PROCESSING_THINK_BEAT_GAP_MS 50U
#define STATUS_LED_PROCESSING_THINK_EFFECT_BEAT2_PERCENT 78U
#define STATUS_LED_PROCESSING_THINK_EFFECT_BEAT3_PERCENT 100U
#define STATUS_LED_STRIP_MASK_STATUS (1U << STATUS_LED_STRIP_STATUS)
#define STATUS_LED_STRIP_MASK_EC11 (1U << STATUS_LED_STRIP_EC11)
#define STATUS_LED_STRIP_MASK_KEY (1U << STATUS_LED_STRIP_KEY)
#define STATUS_LED_STRIP_MASK_EDGE (1U << STATUS_LED_STRIP_EDGE)
#define STATUS_LED_STRIP_MASK_ALL \
    (STATUS_LED_STRIP_MASK_STATUS | STATUS_LED_STRIP_MASK_EC11 | STATUS_LED_STRIP_MASK_KEY | STATUS_LED_STRIP_MASK_EDGE)
#define STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS (1U << 0)
#define STATUS_LED_TRANSITION_CLEAR_BLE (1U << 1)
#define STATUS_LED_TRANSITION_CLEAR_EC11 (1U << 2)
#define STATUS_LED_TRANSITION_CLEAR_KEY (1U << 3)
#define STATUS_LED_TRANSITION_CLEAR_EDGE (1U << 4)
#define STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS (1U << 5)
#define STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS \
    (STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS | STATUS_LED_TRANSITION_CLEAR_EC11 | STATUS_LED_TRANSITION_CLEAR_EDGE)
#define STATUS_LED_TRANSITION_CLEAR_REPAIR \
    (STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS | STATUS_LED_TRANSITION_CLEAR_BLE | STATUS_LED_TRANSITION_CLEAR_EC11)

#define STATUS_LED_TASK_STACK_BYTES (5 * 1024)
#define STATUS_LED_TASK_PRIORITY 6U
#define STATUS_LED_TASK_CORE_ID 1
#define STATUS_LED_REFRESH_MS 50U
#define STATUS_LED_IDLE_REFRESH_MS 1000U
#define STATUS_LED_LOW_POWER_IDLE_REFRESH_MS 60000U
#define STATUS_LED_IDLE_TRANSITION_CLEAR_MS 80U
#define STATUS_LED_TRANSITION_CLEAR_WRITES 3U
#define STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES 3U
#define STATUS_LED_LOW_POWER_STATUS_RETRY_WRITES 3U
#define STATUS_LED_STATUS_HEALTH_RESYNC_MS 500U
#define STATUS_LED_KEY_DARK_RESYNC_MS 0U
#define STATUS_LED_TX_MUTEX_WAIT_MS 100
#define STATUS_LED_RMT_IDLE_RELEASE_MS 0U
#define STATUS_LED_EXTERNAL_POWER_POLL_MS 250U
#define STATUS_LED_POWER_POLL_MS 5000U
#define STATUS_LED_LOW_POWER_POLL_MS 60000U
#define STATUS_LED_CHARGER_STATUS_EXTERNAL_HOLD_MS 1000U
#define STATUS_LED_STATUS_WINDOW_MS 6000U
#define STATUS_LED_PREVIEW_BLE_OVERRIDE_MS 15000U
#define STATUS_LED_BOOT_BLE_READY_WAIT_MS STATUS_LED_STATUS_WINDOW_MS
#define STATUS_LED_BLE_CONFIDENCE_MS 8000U
#define STATUS_LED_BLE_REPAIR_CUE_MS 2700U
#define STATUS_LED_BLE_REPAIR_CUE_LEAD_CLEAR_MS (STATUS_LED_IDLE_TRANSITION_CLEAR_MS + STATUS_LED_REFRESH_MS)
#define STATUS_LED_BLE_REPAIR_WINDOW_MAX_MS 120000U
#define STATUS_LED_OOBE_CONFIDENCE_MS 25000U
#define STATUS_LED_ERROR_HOLD_MS 6000U
#define STATUS_LED_OK_TOTAL_MS 2000U
#define STATUS_LED_OK_PEAK_MS 360U
#define STATUS_LED_KEY_FEEDBACK_MS 700U
#define STATUS_LED_KEY_GESTURE_FEEDBACK_MS 1800U
#define STATUS_LED_KEY_LONG_GESTURE_FEEDBACK_MS 1000U
#define STATUS_LED_KEY_FLASH_ON_MS 420U
#define STATUS_LED_KEY_FLASH_GAP_MS 220U
#define STATUS_LED_KEY_FADE_MS 300U
#define STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS 160U
#define STATUS_LED_KEY_PRESS_PERCENT 55U
#define STATUS_LED_KEY_RELEASE_PERCENT 35U
#define STATUS_LED_KEY_GESTURE_PERCENT 85U
#define STATUS_LED_KEY_MULTI_KEY_INDEPENDENT_FADE 1U
#define STATUS_LED_EC11_FEEDBACK_MS 1400U
#define STATUS_LED_EC11_ROTATION_HOLD_MS 2600U
#define STATUS_LED_EC11_ROTATION_STEP_MS 150U
#define STATUS_LED_EC11_ROTATION_HEAD_START_PERCENT 72U
#define STATUS_LED_EC11_ROTATION_HEAD_END_PERCENT 44U
#define STATUS_LED_EC11_ROTATION_BASE_START_PERCENT 4U
#define STATUS_LED_EC11_ROTATION_BASE_END_PERCENT 1U
#define STATUS_LED_EC11_FEEDBACK_DIAG_MIN_MS 500U
#define STATUS_LED_CHARGING_BREATH_PERIOD_MS 3600U
#define STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U
#define STATUS_LED_CHARGING_BREATH_RISE_MS 1300U
#define STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U
#define STATUS_LED_CHARGING_BREATH_UNKNOWN_FLOOR_PERCENT 8U
#define STATUS_LED_CHARGING_BREATH_MAX_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT
#define STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 12U
#define STATUS_LED_CHARGE_FULL_DEBOUNCE_MS 10000U
#define STATUS_LED_CHARGE_FULL_MIN_MV 4050U
#define STATUS_LED_CHARGE_FULL_MIN_PERCENT 88U
#define STATUS_LED_BATTERY_DISPLAY_GREEN_PERCENT 60U
#define STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT 14U
#define STATUS_LED_BATTERY_STATUS_WINDOW_LOW_PROFILE_PWR_PERCENT STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT
#define STATUS_LED_LOW_BATTERY_STEADY_PERCENT 24U
#define STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT 10U
#define STATUS_LED_FULL_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT
#define STATUS_LED_FULL_STATUS_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT
#define STATUS_LED_BLE_ATTENTION_PERCENT 18U
#define STATUS_LED_BLE_REPAIR_MIN_PERCENT 30U
#define STATUS_LED_BLE_REPAIR_MAX_PERCENT 100U
#define STATUS_LED_BLE_PAIRING_PULSE_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT
#define STATUS_LED_BLE_RECONNECT_PULSE_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT
#define STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT 5U
#define STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT STATUS_LED_BLE_ATTENTION_PERCENT
#define STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS 2000U
#define STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT 14U
#define STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS 120U
#define STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS 7880U
#define STATUS_LED_STATUS_RGB_ENERGY_BALANCE_CONTRACT "status_rgb_channel_sum_matches_single_channel_peak"
#define STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT 4U
#define STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT 16U
#define STATUS_LED_RESULT_PEAK_PERCENT 100U
#define STATUS_LED_OK_SUCCESS_BLUE_BALANCE 0U
#define STATUS_LED_PWR_CONFIRM_AMBER_PERCENT 72U
#define STATUS_LED_PWR_CONFIRM_CUE_PERCENT 46U
#define STATUS_LED_BOOT_PWR_AMBER_PERCENT STATUS_LED_PWR_CONFIRM_AMBER_PERCENT
#define STATUS_LED_FULL_BRIGHTNESS_PERCENT 100U
#define STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA 2000U
#define STATUS_LED_LOW_PROFILE_CAP_PERCENT 100U
#define STATUS_LED_STANDARD_PROFILE_CAP_PERCENT 100U
#define STATUS_LED_AMBIENT_PROFILE_CAP_PERCENT 100U
#define STATUS_LED_LOW_PROFILE_BUDGET_MA 300U
#define STATUS_LED_STANDARD_PROFILE_BUDGET_MA 760U
#define STATUS_LED_AMBIENT_PROFILE_BUDGET_MA 620U
#define STATUS_LED_CHASE_DEFAULT_STEP_MS 250U
#define STATUS_LED_CONTRACT_REV "status_key_ec11_edge_true_state_v26"
#define STATUS_LED_NVS_NAMESPACE "status_led"
#define STATUS_LED_NVS_PROFILE_KEY "profile"
#define STATUS_LED_NVS_BRIGHTNESS_KEY "brightness"
#define STATUS_LED_NVS_STATUS_ORDER_KEY "ord_status"
#define STATUS_LED_NVS_EC11_ORDER_KEY "ord_ec11"
#define STATUS_LED_NVS_KEY_ORDER_KEY "ord_key"
#define STATUS_LED_NVS_EDGE_ORDER_KEY "ord_edge"
#define STATUS_LED_USB_PREFIX "LED:"
#define STATUS_LED_STATUS_FIRST_LED 1U
#define STATUS_LED_EC11_FIRST_LED 7U
#define STATUS_LED_KEY_FIRST_LED 11U
#define STATUS_LED_EDGE_FIRST_LED 17U
#define STATUS_LED_STATUS_PHYSICAL_MAP "LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN"
#define STATUS_LED_KEY_PHYSICAL_MAP "LED11:KEY1,LED12:KEY2,LED13:KEY3,LED14:KEY4"
#define STATUS_LED_STATUS_KEY_MAPPING_CONTRACT "status=LED1..LED6,key=LED11..LED14"
#define STATUS_LED_REC_GOLD_R 255U
#define STATUS_LED_REC_GOLD_G 176U
#define STATUS_LED_REC_GOLD_B 12U
#define STATUS_LED_RECORDING_BREATH_PERIOD_MS 1500U
#define STATUS_LED_RECORDING_BREATH_MIN_PERCENT 28U
#define STATUS_LED_RECORDING_BREATH_MAX_PERCENT 50U
#define STATUS_LED_RECORDING_LEVEL_STALE_MS 300U
#define STATUS_LED_RECORDING_LEVEL_HOLD_MAX_MS 120000U
#define STATUS_LED_DYNAMIC_STATUS_QUANTUM_PERCENT 4U
#define STATUS_LED_ACTIVE_WORK_REC_PERCENT 34U
#define STATUS_LED_ACTIVE_WORK_AI_PERCENT 32U
#define STATUS_LED_ACTIVE_WORK_REC_MAX_PERCENT 100U
#define STATUS_LED_PROCESSING_BREATH_PERIOD_MS 1800U
#define STATUS_LED_PROCESSING_BREATH_MIN_PERCENT 28U
#define STATUS_LED_PROCESSING_BREATH_MAX_PERCENT 100U
#define STATUS_LED_EC11_ACCENT_MIN_PERCENT 10U
#define STATUS_LED_EC11_ACCENT_MAX_PERCENT 100U
#define STATUS_LED_EC11_OK_ACCENT_MAX_PERCENT 36U
#define STATUS_LED_EDGE_ACCENT_MIN_PERCENT 8U
#define STATUS_LED_EDGE_ACCENT_MAX_PERCENT 100U
#define STATUS_LED_EDGE_OK_ACCENT_MAX_PERCENT 36U
#define STATUS_LED_EC11_RECORDING_ANCHOR_PERCENT 14U
#define STATUS_LED_EC11_PROCESSING_ANCHOR_PERCENT 18U
#define STATUS_LED_EC11_PROCESSING_SETTLED_PERCENT 15U
#define STATUS_LED_EC11_OVERLAP_RECORDING_ANCHOR_PERCENT 13U
#define STATUS_LED_EDGE_RECORDING_ANCHOR_PERCENT 10U
#define STATUS_LED_EDGE_PROCESSING_ANCHOR_PERCENT 13U
#define STATUS_LED_EDGE_PROCESSING_SETTLED_PERCENT 11U
#define STATUS_LED_EDGE_OVERLAP_RECORDING_ANCHOR_PERCENT 10U
#define STATUS_LED_ACCENT_BREATHE_QUANTUM_PERCENT 2U
#define STATUS_LED_EC11_RECORDING_BASE_MIN_PERCENT 12U
#define STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT 16U
#define STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MIN_PERCENT 10U
#define STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT 14U
#define STATUS_LED_ACCENT_ENTRY_RAMP_MS 900U
#define STATUS_LED_EC11_RECORDING_FLOW_STEP_MS 360U
#define STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS 720U
#define STATUS_LED_EC11_ORBIT_STEP_MS 240U
#define STATUS_LED_EDGE_ORBIT_STEP_MS 480U
#define STATUS_LED_EC11_PROCESSING_BASE_PERCENT 12U
#define STATUS_LED_EDGE_PROCESSING_BASE_PERCENT 10U
#define STATUS_LED_EC11_PROCESSING_ORBIT_PERCENT 40U
#define STATUS_LED_EDGE_PROCESSING_ORBIT_PERCENT 36U
#define STATUS_LED_OTA_OK_MIN_PERCENT 18U
#define STATUS_LED_OTA_OK_MAX_PERCENT 52U
#define STATUS_LED_OTA_OK_PULSE_MS 1300U
#define STATUS_LED_OTA_EC11_BASE_PERCENT 5U
#define STATUS_LED_OTA_EC11_FILL_PERCENT 18U
#define STATUS_LED_OTA_EC11_HEAD_PERCENT 42U
#define STATUS_LED_OTA_EC11_TAIL_PERCENT 22U
#define STATUS_LED_OTA_EC11_STEP_MS STATUS_LED_EC11_RECORDING_FLOW_STEP_MS
#define STATUS_LED_OTA_EDGE_BASE_PERCENT 4U
#define STATUS_LED_OTA_EDGE_HEAD_PERCENT 24U
#define STATUS_LED_OTA_EDGE_TAIL_PERCENT 14U
#define STATUS_LED_OTA_EDGE_FADE_PERCENT 8U
#define STATUS_LED_OTA_EDGE_STEP_MS 520U
#define STATUS_LED_SHUTDOWN_CONFIRM_MS 1200U
#define STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS 1400U
#define STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS UINT32_MAX
#define STATUS_LED_POWER_SOURCE_USB_DET (1U << 0)
#define STATUS_LED_POWER_SOURCE_CHARGER_STATUS (1U << 1)
#define STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG (1U << 2)

typedef enum {
    STATUS_LED_STRIP_STATUS = 0,
    STATUS_LED_STRIP_EC11,
    STATUS_LED_STRIP_KEY,
    STATUS_LED_STRIP_EDGE,
} status_led_strip_id_t;

typedef enum {
    STATUS_LED_PROFILE_OFF = 0,
    STATUS_LED_PROFILE_LOW,
    STATUS_LED_PROFILE_STANDARD,
    STATUS_LED_PROFILE_AMBIENT,
    STATUS_LED_PROFILE_FACTORY,
} status_led_profile_t;

#define STATUS_LED_STATUS_DEFAULT_COLOR_ORDER STATUS_LED_COLOR_ORDER_GRB
#define STATUS_LED_KEY_DEFAULT_COLOR_ORDER STATUS_LED_COLOR_ORDER_GRB

typedef enum {
    STATUS_LED_TEST_NONE = 0,
    STATUS_LED_TEST_RGBW,
    STATUS_LED_TEST_MAP,
    STATUS_LED_TEST_CHASE,
    STATUS_LED_TEST_PIXEL,
    STATUS_LED_TEST_STATIC_STATUS,
    STATUS_LED_TEST_STATUS_KEY_STRESS,
} status_led_test_mode_t;

typedef enum {
    STATUS_LED_SEM_PWR = 0,
    STATUS_LED_SEM_BLE,
    STATUS_LED_SEM_REC,
    STATUS_LED_SEM_AI,
    STATUS_LED_SEM_OK,
    STATUS_LED_SEM_WARN,
} status_led_semantic_t;

typedef enum {
    STATUS_LED_PWR_COLOR_OFF = 0,
    STATUS_LED_PWR_COLOR_GREEN = 1,
    STATUS_LED_PWR_COLOR_AMBER = 2,
    STATUS_LED_PWR_COLOR_RED = 3,
    STATUS_LED_PWR_COLOR_WHITE = 4,
    STATUS_LED_PWR_COLOR_BLUE = 5,
    STATUS_LED_PWR_COLOR_VIOLET = 6,
    STATUS_LED_PWR_COLOR_GOLD = 7,
    STATUS_LED_PWR_COLOR_OTHER = 15,
} status_led_power_color_class_t;

typedef enum {
    STATUS_LED_DIAG_REASON_BOOT = 1,
    STATUS_LED_DIAG_REASON_POWER_CHANGE = 2,
    STATUS_LED_DIAG_REASON_BLE_STATE = 3,
    STATUS_LED_DIAG_REASON_RECORDING_START = 4,
    STATUS_LED_DIAG_REASON_RECORDING_STOP = 5,
    STATUS_LED_DIAG_REASON_PROCESSING_START = 6,
    STATUS_LED_DIAG_REASON_PROCESSING_STOP = 7,
    STATUS_LED_DIAG_REASON_SUCCESS = 8,
    STATUS_LED_DIAG_REASON_ERROR = 9,
    STATUS_LED_DIAG_REASON_LOW_POWER_OFF = 10,
    STATUS_LED_DIAG_REASON_LOW_POWER_RESUME = 11,
    STATUS_LED_DIAG_REASON_PREPARE_SLEEP = 12,
    STATUS_LED_DIAG_REASON_MANUAL_OFF = 13,
    STATUS_LED_DIAG_REASON_DEVICE_SETTINGS = 14,
    STATUS_LED_DIAG_REASON_RENDER = 15,
    STATUS_LED_DIAG_REASON_PREVIEW = 16,
    STATUS_LED_DIAG_REASON_BRIGHTNESS = 17,
    STATUS_LED_DIAG_REASON_PROFILE = 18,
    STATUS_LED_DIAG_REASON_TEST = 19,
    STATUS_LED_DIAG_REASON_KEY = 20,
    STATUS_LED_DIAG_REASON_BOOTING = 21,
    STATUS_LED_DIAG_REASON_OTHER = 255,
} status_led_diag_reason_t;

#define STATUS_LED_DIAG_ACTIVE_PWR  (1U << 0)
#define STATUS_LED_DIAG_ACTIVE_BLE  (1U << 1)
#define STATUS_LED_DIAG_ACTIVE_REC  (1U << 2)
#define STATUS_LED_DIAG_ACTIVE_AI   (1U << 3)
#define STATUS_LED_DIAG_ACTIVE_OK   (1U << 4)
#define STATUS_LED_DIAG_ACTIVE_WARN (1U << 5)
#define STATUS_LED_DIAG_ACTIVE_KEY  (1U << 6)
#define STATUS_LED_DIAG_ACTIVE_EDGE (1U << 7)
#define STATUS_LED_DIAG_ACTIVE_EC11 (1U << 8)

#define STATUS_LED_DIAG_POWER_EXTERNAL       (1U << 0)
#define STATUS_LED_DIAG_POWER_CHARGING       (1U << 1)
#define STATUS_LED_DIAG_POWER_FULL           (1U << 2)
#define STATUS_LED_DIAG_POWER_RAW_CHARGING   (1U << 3)
#define STATUS_LED_DIAG_POWER_RAW_FULL       (1U << 4)
#define STATUS_LED_DIAG_POWER_FULL_LATCHED   (1U << 5)
#define STATUS_LED_DIAG_POWER_BATTERY_VALID  (1U << 6)
#define STATUS_LED_DIAG_POWER_LOW_POWER_OFF  (1U << 7)
#define STATUS_LED_DIAG_POWER_OUTPUT_OFF     (1U << 8)
#define STATUS_LED_DIAG_POWER_DISPLAY_VALID  (1U << 24)
#define STATUS_LED_DIAG_POWER_DISPLAY_RISE_SUPPRESSED (1U << 25)
#define STATUS_LED_DIAG_POWER_EXTERNAL_USB_DET (1U << 26)
#define STATUS_LED_DIAG_POWER_EXTERNAL_CHARGER_STATUS (1U << 27)
#define STATUS_LED_DIAG_POWER_EXTERNAL_USB_SERIAL_JTAG (1U << 28)
#define STATUS_LED_DIAG_POWER_DISPLAY_LEVEL_SHIFT 16U
#define STATUS_LED_DIAG_POWER_DISPLAY_LEVEL_MASK (0xFFU << STATUS_LED_DIAG_POWER_DISPLAY_LEVEL_SHIFT)

#define STATUS_LED_DIAG_VIS_EXTERNAL      (1U << 0)
#define STATUS_LED_DIAG_VIS_CHARGING      (1U << 1)
#define STATUS_LED_DIAG_VIS_FULL          (1U << 2)
#define STATUS_LED_DIAG_VIS_BATTERY_VALID (1U << 3)
#define STATUS_LED_DIAG_VIS_OUTPUT_OFF    (1U << 4)
#define STATUS_LED_DIAG_VIS_LOW_POWER_OFF (1U << 5)
#define STATUS_LED_DIAG_VIS_STATUS_WINDOW (1U << 6)
#define STATUS_LED_DIAG_VIS_BOOT_FEEDBACK (1U << 7)

typedef struct {
    status_led_rgb_t status[STATUS_LED_STATUS_COUNT];
    status_led_rgb_t ec11[STATUS_LED_EC11_COUNT];
    status_led_rgb_t key[STATUS_LED_KEY_COUNT];
    status_led_rgb_t edge[STATUS_LED_EDGE_COUNT];
} status_led_frame_t;

typedef struct {
    const char *name;
    gpio_num_t gpio;
    uint8_t led_count;
    uint8_t tail_guard_pixels;
    uint8_t dark_latch_rmt_writes;
    status_led_color_order_t color_order;
    bool prefer_dma;
    /* SPI+DMA strips (ec11/key) set transport=SPI and spi_host=SPI2/SPI3_HOST;
     * the data GPIO is reassigned to that host's MOSI via the GPIO matrix.
     * status/edge stay on RMT. See docs/features/status_led_dma_history.md. */
    status_led_strip_transport_t transport;
    int spi_host;
    status_led_strip_backend_t *backend;
} status_led_strip_t;

typedef struct {
    bool initialized;
    bool started;
    bool output_disabled;
    bool low_power_disabled;
    bool vdd_led_enable_assumed;
    bool ever_connected;
    status_led_profile_t profile;
    uint8_t brightness_percent;
    uint8_t status_zone_brightness_percent;
    uint8_t key_zone_brightness_percent;
    uint8_t ec11_zone_brightness_percent;
    uint8_t edge_zone_brightness_percent;
    status_led_ble_state_t ble_state;
    status_led_rec_source_t rec_source;
    status_led_error_domain_t error_domain;
    status_led_error_severity_t error_severity;
    bool recording_active;
    bool processing_active;
    bool ota_active;
    uint8_t recording_level_percent;
    uint8_t recording_level_visual_percent;
    bool battery_valid;
    bool external_power_present;
    uint32_t external_power_source_flags;
    bool charging;
    bool full;
    bool raw_charging;
    bool raw_full;
    bool charge_full_latched;
    uint8_t battery_level_percent;
    uint8_t battery_display_level_percent;
    bool battery_display_valid;
    bool battery_display_rise_suppressed;
    uint32_t battery_mv;
    uint32_t battery_display_mv;
    uint32_t status_window_until_ms;
    uint32_t boot_feedback_until_ms;
    uint32_t ble_confidence_until_ms;
    uint32_t ble_repair_until_ms;
    uint32_t ble_repair_cue_started_ms;
    uint32_t ble_repair_cue_until_ms;
    uint32_t oobe_confidence_until_ms;
    uint32_t ok_started_ms;
    uint32_t ok_until_ms;
    bool ok_warning;
    uint32_t shutdown_confirm_started_ms;
    uint32_t shutdown_confirm_until_ms;
    bool shutdown_confirm_final;
    uint32_t shutdown_final_all_zone_latched_started_ms;
    uint32_t error_started_ms;
    uint32_t error_until_ms;
    uint32_t recording_level_updated_ms;
    uint32_t recording_level_hold_until_ms;
    uint32_t recording_level_visual_updated_ms;
    uint32_t processing_started_ms;
    uint32_t ota_started_ms;
    size_t ota_bytes_written;
    size_t ota_expected_size;
    uint32_t ble_transition_ms;
    uint32_t last_transition_ms;
    uint32_t last_power_poll_ms;
    uint32_t charger_status_external_until_ms;
    uint32_t charge_full_candidate_since_ms;
    uint32_t last_estimated_current_ma;
    uint32_t last_current_budget_ma;
    uint8_t last_budget_scale_percent;
    bool current_limited_by_budget;
    uint32_t last_logged_visual_key;
    uint32_t last_logged_frame_rgb_key;
    bool visual_log_initialized;
    bool preview_suppress_accents;
    bool preview_effect_only;
    uint8_t transition_clear_mask;
    uint32_t preview_ble_override_until_ms;
    uint8_t key_pressed_mask;
    uint32_t key_until_ms[STATUS_LED_KEY_COUNT];
    uint32_t key_feedback_started_ms[STATUS_LED_KEY_COUNT];
    uint32_t key_feedback_until_ms[STATUS_LED_KEY_COUNT];
    status_led_key_feedback_t key_feedback[STATUS_LED_KEY_COUNT];
    uint8_t key_dark_latch_pending_mask;
    uint32_t ec11_feedback_started_ms;
    uint32_t ec11_feedback_until_ms;
    uint32_t ec11_feedback_last_step_ms;
    uint32_t ec11_feedback_motion_step;
    uint32_t ec11_feedback_last_diag_ms;
    status_led_ec11_feedback_t ec11_feedback;
    status_led_test_mode_t test_mode;
    uint8_t test_strip_mask;
    uint32_t test_started_ms;
    uint16_t test_step_ms;
    status_led_strip_id_t test_pixel_strip;
    uint8_t test_pixel_index;
    status_led_rgb_t test_pixel_color;
    uint8_t test_pixel_percent;
    char last_reason[32];
    uint8_t retry_strip_mask;
    status_led_frame_t last_frame;
} status_led_state_t;

typedef struct {
    status_led_profile_t profile;
    uint8_t brightness_percent;
    uint8_t status_zone_brightness_percent;
    uint8_t key_zone_brightness_percent;
    uint8_t ec11_zone_brightness_percent;
    uint8_t edge_zone_brightness_percent;
    status_led_ble_state_t ble_state;
    status_led_rec_source_t rec_source;
    status_led_error_domain_t error_domain;
    status_led_error_severity_t error_severity;
    bool recording_active;
    bool processing_active;
    bool ota_active;
    size_t ota_bytes_written;
    size_t ota_expected_size;
    uint8_t ota_progress_percent;
    uint8_t recording_level_percent;
    uint8_t recording_level_visual_percent;
    uint32_t recording_level_hold_until_ms;
    bool battery_valid;
    bool external_power_present;
    uint32_t external_power_source_flags;
    bool charging;
    bool full;
    bool raw_charging;
    bool raw_full;
    bool charge_full_latched;
    uint8_t battery_level_percent;
    uint8_t battery_display_level_percent;
    bool battery_display_valid;
    bool battery_display_rise_suppressed;
    uint32_t battery_mv;
    uint32_t battery_display_mv;
    uint32_t status_window_until_ms;
    uint32_t ble_confidence_until_ms;
    uint32_t ble_repair_until_ms;
    uint32_t ble_repair_cue_until_ms;
    uint32_t oobe_confidence_until_ms;
    uint32_t shutdown_confirm_started_ms;
    uint32_t shutdown_confirm_until_ms;
    bool shutdown_confirm_final;
    uint32_t charge_full_candidate_since_ms;
    uint32_t ble_transition_ms;
    uint32_t preview_ble_override_until_ms;
    uint32_t last_transition_ms;
    uint32_t last_estimated_current_ma;
    uint32_t last_current_budget_ma;
    uint8_t last_budget_scale_percent;
    bool current_limited_by_budget;
    bool output_disabled;
    bool low_power_disabled;
    bool preview_suppress_accents;
    bool preview_effect_only;
    uint8_t transition_clear_mask;
    uint8_t key_pressed_mask;
    status_led_test_mode_t test_mode;
    uint8_t test_strip_mask;
    char last_reason[32];
    status_led_frame_t last_frame;
} status_led_print_snapshot_t;

static const char *TAG = "status_led";

static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_tx_mutex;
static TaskHandle_t s_task_handle;
static status_led_state_t s_state;
static char s_print_line[384];
static status_led_strip_t s_strips[STATUS_LED_STRIP_COUNT] = {
    {
        .name = "status",
        .gpio = BOARD_PINS_RGB_STATUS_IO,
        .led_count = STATUS_LED_STATUS_COUNT,
        .tail_guard_pixels = STATUS_LED_STATUS_TAIL_GUARD_PIXELS,
        .color_order = STATUS_LED_STATUS_DEFAULT_COLOR_ORDER,
        // Keep advanced REC/AI status effects on the historical status-only DMA rail.
        // Low-power clear/final latch frames temporarily transmit without DMA.
        .prefer_dma = true,
    },
    {
        .name = "ec11",
        .gpio = BOARD_PINS_RGB_EC11_IO,
        .led_count = STATUS_LED_EC11_COUNT,
        .color_order = STATUS_LED_COLOR_ORDER_GRB,
        // EC11 flickers on non-DMA (interrupt-backed) RMT. Drive it from SPI2
        // MOSI + GDMA via the SPI clock-hack (4 SPI bits per WS2812 bit); only
        // MOSI is wired to the LED DIN (SCLK/MISO/CS internal). GPIO5 is
        // reassigned to SPI2 MOSI by the GPIO matrix - no hardware change.
        .transport = STATUS_LED_STRIP_TRANSPORT_SPI,
        .spi_host = SPI2_HOST,
    },
    {
        .name = "key",
        .gpio = BOARD_PINS_RGB_KEY_IO,
        .led_count = STATUS_LED_KEY_COUNT,
        .tail_guard_pixels = STATUS_LED_KEY_TAIL_GUARD_PIXELS,
        .dark_latch_rmt_writes = STATUS_LED_KEY_DARK_LATCH_RMT_WRITES,
        .color_order = STATUS_LED_KEY_DEFAULT_COLOR_ORDER,
        // Same rationale as ec11: SPI3 MOSI + GDMA removes the non-DMA RMT
        // flicker. GPIO13 is reassigned to SPI3 MOSI by the GPIO matrix.
        .transport = STATUS_LED_STRIP_TRANSPORT_SPI,
        .spi_host = SPI3_HOST,
    },
    {
        .name = "edge",
        .gpio = BOARD_PINS_RGB_EDGE_IO,
        .led_count = STATUS_LED_EDGE_COUNT,
        .color_order = STATUS_LED_COLOR_ORDER_GRB,
    },
};
static uint32_t s_strip_last_tx_ms[STATUS_LED_STRIP_COUNT];
static bool s_strip_transport_suspended[STATUS_LED_STRIP_COUNT] = {
    true,
    true,
    true,
    true,
};

static void status_led_force_all_off(bool force_non_dma);
static status_led_rgb_t status_led_boot_power_color_locked(void);
static uint8_t status_led_scale_effect_percent_locked(uint8_t percent, uint32_t now_ms);
static bool status_led_ec11_feedback_active_locked(uint32_t now_ms);

static uint32_t status_led_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static uint8_t status_led_clamp_u32_to_u8(uint32_t value)
{
    return value > UINT8_MAX ? UINT8_MAX : (uint8_t)value;
}

static status_led_rgb_t status_led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    status_led_rgb_t color = {
        .r = r,
        .g = g,
        .b = b,
    };
    return color;
}

static bool status_led_rgb_is_on(status_led_rgb_t color)
{
    return color.r != 0U || color.g != 0U || color.b != 0U;
}

static bool status_led_strip_has_light(const status_led_rgb_t *colors, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        if (status_led_rgb_is_on(colors[index])) {
            return true;
        }
    }
    return false;
}

static uint32_t status_led_pack_rgb(status_led_rgb_t color)
{
    return ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
}

static uint8_t status_led_rgb_max_channel(status_led_rgb_t color)
{
    uint8_t max = color.r;
    if (max < color.g) {
        max = color.g;
    }
    if (max < color.b) {
        max = color.b;
    }
    return max;
}

static status_led_power_color_class_t status_led_power_color_class(status_led_rgb_t color)
{
    if (!status_led_rgb_is_on(color)) {
        return STATUS_LED_PWR_COLOR_OFF;
    }
    if (color.r > 0U && color.g > 0U && color.b > 0U) {
        if (color.r == color.g && color.g == color.b) {
            return STATUS_LED_PWR_COLOR_WHITE;
        }
        if (color.r >= color.g && color.b >= color.g) {
            return STATUS_LED_PWR_COLOR_VIOLET;
        }
        return STATUS_LED_PWR_COLOR_OTHER;
    }
    if (color.r > 0U && color.g > 0U) {
        if (color.b == 0U &&
            color.r >= color.g &&
            ((uint32_t)color.g * 100U) >= ((uint32_t)color.r * 60U)) {
            return STATUS_LED_PWR_COLOR_GOLD;
        }
        if (color.r >= color.g) {
            return STATUS_LED_PWR_COLOR_AMBER;
        }
        return STATUS_LED_PWR_COLOR_GREEN;
    }
    if (color.g > 0U) {
        return STATUS_LED_PWR_COLOR_GREEN;
    }
    if (color.r > 0U) {
        return STATUS_LED_PWR_COLOR_RED;
    }
    if (color.b > 0U) {
        return STATUS_LED_PWR_COLOR_BLUE;
    }
    return STATUS_LED_PWR_COLOR_OTHER;
}

static uint32_t status_led_active_flags_from_frame(const status_led_frame_t *frame)
{
    uint32_t flags = 0;
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_PWR])) {
        flags |= STATUS_LED_DIAG_ACTIVE_PWR;
    }
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_BLE])) {
        flags |= STATUS_LED_DIAG_ACTIVE_BLE;
    }
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_REC])) {
        flags |= STATUS_LED_DIAG_ACTIVE_REC;
    }
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_AI])) {
        flags |= STATUS_LED_DIAG_ACTIVE_AI;
    }
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_OK])) {
        flags |= STATUS_LED_DIAG_ACTIVE_OK;
    }
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_WARN])) {
        flags |= STATUS_LED_DIAG_ACTIVE_WARN;
    }
    if (status_led_strip_has_light(frame->ec11, STATUS_LED_EC11_COUNT)) {
        flags |= STATUS_LED_DIAG_ACTIVE_EC11;
    }
    if (status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT)) {
        flags |= STATUS_LED_DIAG_ACTIVE_KEY;
    }
    if (status_led_strip_has_light(frame->edge, STATUS_LED_EDGE_COUNT)) {
        flags |= STATUS_LED_DIAG_ACTIVE_EDGE;
    }
    return flags;
}

static uint8_t status_led_strip_max_channel(const status_led_rgb_t *colors, size_t count)
{
    uint8_t max = 0;
    for (size_t index = 0; index < count; ++index) {
        uint8_t channel = status_led_rgb_max_channel(colors[index]);
        if (max < channel) {
            max = channel;
        }
    }
    return max;
}

static uint32_t status_led_pack_status_color_classes(const status_led_frame_t *frame)
{
    uint32_t packed = 0;
    for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
        packed |= ((uint32_t)status_led_power_color_class(frame->status[index]) & 0x0FU) <<
                  (index * 4U);
    }
    return packed;
}

static uint32_t status_led_pack_status_intensity_a(const status_led_frame_t *frame)
{
    return ((uint32_t)status_led_rgb_max_channel(frame->status[STATUS_LED_SEM_PWR])) |
           ((uint32_t)status_led_rgb_max_channel(frame->status[STATUS_LED_SEM_BLE]) << 8) |
           ((uint32_t)status_led_rgb_max_channel(frame->status[STATUS_LED_SEM_REC]) << 16) |
           ((uint32_t)status_led_rgb_max_channel(frame->status[STATUS_LED_SEM_AI]) << 24);
}

static uint32_t status_led_pack_status_intensity_b(const status_led_frame_t *frame)
{
    return ((uint32_t)status_led_rgb_max_channel(frame->status[STATUS_LED_SEM_OK])) |
           ((uint32_t)status_led_rgb_max_channel(frame->status[STATUS_LED_SEM_WARN]) << 8) |
           ((uint32_t)status_led_strip_max_channel(frame->ec11, STATUS_LED_EC11_COUNT) << 16) |
           ((uint32_t)status_led_strip_max_channel(frame->edge, STATUS_LED_EDGE_COUNT) << 24);
}

static uint32_t status_led_diag_reason_code(const char *reason)
{
    if (reason == NULL || reason[0] == '\0') {
        return STATUS_LED_DIAG_REASON_OTHER;
    }
    if (strcmp(reason, "boot") == 0) {
        return STATUS_LED_DIAG_REASON_BOOT;
    }
    if (strcmp(reason, "booting") == 0) {
        return STATUS_LED_DIAG_REASON_BOOTING;
    }
    if (strcmp(reason, "power_change") == 0) {
        return STATUS_LED_DIAG_REASON_POWER_CHANGE;
    }
    if (strcmp(reason, "ble_state") == 0) {
        return STATUS_LED_DIAG_REASON_BLE_STATE;
    }
    if (strcmp(reason, "recording_start") == 0) {
        return STATUS_LED_DIAG_REASON_RECORDING_START;
    }
    if (strcmp(reason, "recording_stop") == 0) {
        return STATUS_LED_DIAG_REASON_RECORDING_STOP;
    }
    if (strstr(reason, "processing_start") != NULL) {
        return STATUS_LED_DIAG_REASON_PROCESSING_START;
    }
    if (strstr(reason, "processing_stop") != NULL ||
        strcmp(reason, "host_processing_done") == 0) {
        return STATUS_LED_DIAG_REASON_PROCESSING_STOP;
    }
    if (strstr(reason, "warning") != NULL ||
        strstr(reason, "warn") != NULL ||
        strstr(reason, "failed") != NULL ||
        strstr(reason, "timeout") != NULL ||
        strstr(reason, "rejected") != NULL) {
        return STATUS_LED_DIAG_REASON_ERROR;
    }
    if (strcmp(reason, "success") == 0 ||
        strstr(reason, "_done") != NULL) {
        return STATUS_LED_DIAG_REASON_SUCCESS;
    }
    if (strstr(reason, "error") != NULL ||
        strstr(reason, "failed") != NULL ||
        strstr(reason, "timeout") != NULL ||
        strstr(reason, "rejected") != NULL) {
        return STATUS_LED_DIAG_REASON_ERROR;
    }
    if (strcmp(reason, "low_power_off") == 0) {
        return STATUS_LED_DIAG_REASON_LOW_POWER_OFF;
    }
    if (strcmp(reason, "low_power_resume") == 0) {
        return STATUS_LED_DIAG_REASON_LOW_POWER_RESUME;
    }
    if (strcmp(reason, "prepare_sleep") == 0) {
        return STATUS_LED_DIAG_REASON_PREPARE_SLEEP;
    }
    if (strcmp(reason, "manual_off") == 0) {
        return STATUS_LED_DIAG_REASON_MANUAL_OFF;
    }
    if (strcmp(reason, "device_settings") == 0) {
        return STATUS_LED_DIAG_REASON_DEVICE_SETTINGS;
    }
    if (strcmp(reason, "preview") == 0) {
        return STATUS_LED_DIAG_REASON_PREVIEW;
    }
    if (strcmp(reason, "brightness") == 0) {
        return STATUS_LED_DIAG_REASON_BRIGHTNESS;
    }
    if (strcmp(reason, "profile") == 0) {
        return STATUS_LED_DIAG_REASON_PROFILE;
    }
    if (strncmp(reason, "test_", 5) == 0) {
        return STATUS_LED_DIAG_REASON_TEST;
    }
    if (strncmp(reason, "key_", 4) == 0) {
        return STATUS_LED_DIAG_REASON_KEY;
    }
    return STATUS_LED_DIAG_REASON_OTHER;
}

static uint32_t status_led_power_flags_locked(void)
{
    uint32_t flags = 0;
    if (s_state.external_power_present) {
        flags |= STATUS_LED_DIAG_POWER_EXTERNAL;
    }
    if (s_state.charging) {
        flags |= STATUS_LED_DIAG_POWER_CHARGING;
    }
    if (s_state.full) {
        flags |= STATUS_LED_DIAG_POWER_FULL;
    }
    if (s_state.raw_charging) {
        flags |= STATUS_LED_DIAG_POWER_RAW_CHARGING;
    }
    if (s_state.raw_full) {
        flags |= STATUS_LED_DIAG_POWER_RAW_FULL;
    }
    if (s_state.charge_full_latched) {
        flags |= STATUS_LED_DIAG_POWER_FULL_LATCHED;
    }
    if (s_state.battery_valid) {
        flags |= STATUS_LED_DIAG_POWER_BATTERY_VALID;
    }
    if (s_state.low_power_disabled) {
        flags |= STATUS_LED_DIAG_POWER_LOW_POWER_OFF;
    }
    if (s_state.output_disabled) {
        flags |= STATUS_LED_DIAG_POWER_OUTPUT_OFF;
    }
    if (s_state.battery_display_valid) {
        flags |= STATUS_LED_DIAG_POWER_DISPLAY_VALID;
        flags |= ((uint32_t)s_state.battery_display_level_percent
                  << STATUS_LED_DIAG_POWER_DISPLAY_LEVEL_SHIFT) &
                 STATUS_LED_DIAG_POWER_DISPLAY_LEVEL_MASK;
    }
    if (s_state.battery_display_rise_suppressed) {
        flags |= STATUS_LED_DIAG_POWER_DISPLAY_RISE_SUPPRESSED;
    }
    if (s_state.external_power_source_flags & STATUS_LED_POWER_SOURCE_USB_DET) {
        flags |= STATUS_LED_DIAG_POWER_EXTERNAL_USB_DET;
    }
    if (s_state.external_power_source_flags & STATUS_LED_POWER_SOURCE_CHARGER_STATUS) {
        flags |= STATUS_LED_DIAG_POWER_EXTERNAL_CHARGER_STATUS;
    }
    if (s_state.external_power_source_flags & STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG) {
        flags |= STATUS_LED_DIAG_POWER_EXTERNAL_USB_SERIAL_JTAG;
    }
    return flags;
}

static uint8_t status_led_battery_display_level_locked(void)
{
    if (s_state.battery_display_valid) {
        return s_state.battery_display_level_percent;
    }
    return s_state.battery_valid ? s_state.battery_level_percent : 0xFFU;
}

static bool status_led_battery_display_available_locked(void)
{
    return s_state.battery_display_valid || s_state.battery_valid;
}

static bool status_led_update_battery_display_locked(
    bool external_power_present,
    bool battery_valid,
    uint32_t battery_mv,
    uint8_t battery_level)
{
    bool changed = false;
    bool suppressed_rise = false;

    if (!battery_valid) {
        changed = s_state.battery_display_valid ||
                  s_state.battery_display_level_percent != 0 ||
                  s_state.battery_display_mv != 0;
        s_state.battery_display_valid = false;
        s_state.battery_display_level_percent = 0;
        s_state.battery_display_mv = 0;
        s_state.battery_display_rise_suppressed = false;
        return changed;
    }

    if (external_power_present || !s_state.battery_display_valid) {
        changed = !s_state.battery_display_valid ||
                  s_state.battery_display_level_percent != battery_level ||
                  s_state.battery_display_mv != battery_mv;
        s_state.battery_display_valid = true;
        s_state.battery_display_level_percent = battery_level;
        s_state.battery_display_mv = battery_mv;
    } else if (battery_level <= s_state.battery_display_level_percent) {
        changed = s_state.battery_display_level_percent != battery_level ||
                  s_state.battery_display_mv != battery_mv;
        s_state.battery_display_level_percent = battery_level;
        s_state.battery_display_mv = battery_mv;
    } else {
        suppressed_rise = true;
    }

    s_state.battery_display_rise_suppressed = suppressed_rise;
    return changed;
}

static uint32_t status_led_visual_flags_locked(
    uint32_t now_ms,
    status_led_power_color_class_t power_class)
{
    uint32_t flags = 0;
    if (s_state.external_power_present) {
        flags |= STATUS_LED_DIAG_VIS_EXTERNAL;
    }
    if (s_state.charging) {
        flags |= STATUS_LED_DIAG_VIS_CHARGING;
    }
    if (s_state.full) {
        flags |= STATUS_LED_DIAG_VIS_FULL;
    }
    if (s_state.battery_valid) {
        flags |= STATUS_LED_DIAG_VIS_BATTERY_VALID;
    }
    if (s_state.output_disabled) {
        flags |= STATUS_LED_DIAG_VIS_OUTPUT_OFF;
    }
    if (s_state.low_power_disabled) {
        flags |= STATUS_LED_DIAG_VIS_LOW_POWER_OFF;
    }
    if (now_ms < s_state.status_window_until_ms) {
        flags |= STATUS_LED_DIAG_VIS_STATUS_WINDOW;
    }
    if (now_ms < s_state.boot_feedback_until_ms) {
        flags |= STATUS_LED_DIAG_VIS_BOOT_FEEDBACK;
    }
    flags |= ((uint32_t)power_class & 0x0FU) << 8;
    flags |= ((uint32_t)s_state.ble_state & 0x0FU) << 12;
    flags |= ((uint32_t)s_state.error_domain & 0x0FU) << 16;
    return flags;
}

static void status_led_log_power_input_locked(void)
{
    diag_log(
        DIAG_SRC_STATUS_LED,
        DIAG_LED_POWER_INPUT,
        DIAG_SEV_INFO,
        status_led_power_flags_locked(),
        s_state.battery_mv,
        s_state.battery_valid ? (uint32_t)s_state.battery_level_percent : 0xFFU,
        status_led_diag_reason_code(s_state.last_reason));
}

static void status_led_log_output_state_locked(uint32_t active_flags)
{
    status_led_log_power_input_locked();
    diag_log(
        DIAG_SRC_STATUS_LED,
        DIAG_LED_OUTPUT_STATE,
        DIAG_SEV_INFO,
        s_state.output_disabled ? 1U : 0U,
        s_state.low_power_disabled ? 1U : 0U,
        active_flags,
        status_led_diag_reason_code(s_state.last_reason));
}

static void status_led_log_visual_state_locked(const status_led_frame_t *frame, uint32_t now_ms)
{
    const status_led_rgb_t pwr = frame->status[STATUS_LED_SEM_PWR];
    const status_led_power_color_class_t pwr_class = status_led_power_color_class(pwr);
    const uint32_t active_flags = status_led_active_flags_from_frame(frame);
    const uint32_t visual_flags = status_led_visual_flags_locked(now_ms, pwr_class);
    const uint32_t reason_code = status_led_diag_reason_code(s_state.last_reason);
    const uint32_t frame_rgb_key = status_led_pack_status_color_classes(frame);
    const uint32_t visual_key =
        (active_flags & 0x1FFU) |
        ((visual_flags & 0x000FFFFFU) << 9);

    if (s_state.visual_log_initialized &&
        s_state.last_logged_visual_key == visual_key &&
        s_state.last_logged_frame_rgb_key == frame_rgb_key) {
        return;
    }
    s_state.visual_log_initialized = true;
    s_state.last_logged_visual_key = visual_key;
    s_state.last_logged_frame_rgb_key = frame_rgb_key;

    diag_log(
        DIAG_SRC_STATUS_LED,
        DIAG_LED_VISUAL_STATE,
        DIAG_SEV_INFO,
        active_flags,
        status_led_pack_rgb(pwr),
        visual_flags,
        reason_code);
    diag_log(
        DIAG_SRC_STATUS_LED,
        DIAG_LED_FRAME_RGB,
        DIAG_SEV_INFO,
        status_led_pack_status_color_classes(frame),
        status_led_pack_status_intensity_a(frame),
        status_led_pack_status_intensity_b(frame),
        reason_code);
}

static status_led_rgb_t status_led_rec_gold(void)
{
    return status_led_rgb(
        STATUS_LED_REC_GOLD_R,
        STATUS_LED_REC_GOLD_G,
        STATUS_LED_REC_GOLD_B);
}

static uint8_t status_led_linear_percent_to_255(uint8_t percent)
{
    return (uint8_t)status_led_clamp_u32_to_u8(((uint32_t)percent * 255U + 50U) / 100U);
}

static status_led_rgb_t status_led_scale_raw(status_led_rgb_t color, uint8_t percent)
{
    status_led_rgb_t out = {0};
    const uint8_t gain = status_led_linear_percent_to_255(percent);
    out.r = status_led_clamp_u32_to_u8(((uint32_t)color.r * gain + 127U) / 255U);
    out.g = status_led_clamp_u32_to_u8(((uint32_t)color.g * gain + 127U) / 255U);
    out.b = status_led_clamp_u32_to_u8(((uint32_t)color.b * gain + 127U) / 255U);
    return out;
}

static status_led_rgb_t status_led_scale_gamma(status_led_rgb_t color, uint8_t percent)
{
    return status_led_scale_raw(color, percent);
}

static void status_led_set_max(status_led_rgb_t *slot, status_led_rgb_t color)
{
    if (slot->r < color.r) {
        slot->r = color.r;
    }
    if (slot->g < color.g) {
        slot->g = color.g;
    }
    if (slot->b < color.b) {
        slot->b = color.b;
    }
}

static const char *status_led_profile_name(status_led_profile_t profile)
{
    switch (profile) {
    case STATUS_LED_PROFILE_OFF: return "off";
    case STATUS_LED_PROFILE_LOW: return "low";
    case STATUS_LED_PROFILE_STANDARD: return "standard";
    case STATUS_LED_PROFILE_AMBIENT: return "ambient";
    case STATUS_LED_PROFILE_FACTORY: return "factory";
    default: return "unknown";
    }
}

static const char *status_led_strip_transport_name(status_led_strip_transport_t transport, int spi_host)
{
    if (transport == STATUS_LED_STRIP_TRANSPORT_SPI) {
        if (spi_host == SPI2_HOST) {
            return "spi2";
        }
        if (spi_host == SPI3_HOST) {
            return "spi3";
        }
        return "spi";
    }
    return "rmt";
}

static const char *status_led_ble_name(status_led_ble_state_t state)
{
    switch (state) {
    case STATUS_LED_BLE_DISCONNECTED: return "disconnected";
    case STATUS_LED_BLE_PAIRING: return "pairing";
    case STATUS_LED_BLE_RECONNECTING: return "reconnecting";
    case STATUS_LED_BLE_CONNECTED: return "connected";
    case STATUS_LED_BLE_TYPE_READY: return "type_ready";
    case STATUS_LED_BLE_REPAIRING: return "repairing";
    default: return "unknown";
    }
}

static bool status_led_ble_state_ready_locked(status_led_ble_state_t state)
{
    return state == STATUS_LED_BLE_TYPE_READY;
}

static bool status_led_ble_state_attention_locked(status_led_ble_state_t state)
{
    return state == STATUS_LED_BLE_PAIRING ||
           state == STATUS_LED_BLE_RECONNECTING ||
           state == STATUS_LED_BLE_REPAIRING;
}

static const char *status_led_rec_source_name(status_led_rec_source_t source)
{
    switch (source) {
    case STATUS_LED_REC_SOURCE_NONE: return "none";
    case STATUS_LED_REC_SOURCE_DEVICE_MIC: return "device_mic";
    case STATUS_LED_REC_SOURCE_DESKTOP_MIC: return "desktop_mic";
    case STATUS_LED_REC_SOURCE_NOT_AVAILABLE: return "not_available";
    default: return "unknown";
    }
}

static const char *status_led_error_domain_name(status_led_error_domain_t domain)
{
    switch (domain) {
    case STATUS_LED_ERROR_DOMAIN_NONE: return "none";
    case STATUS_LED_ERROR_DOMAIN_BLE: return "ble";
    case STATUS_LED_ERROR_DOMAIN_REC: return "recording";
    case STATUS_LED_ERROR_DOMAIN_AI: return "ai";
    case STATUS_LED_ERROR_DOMAIN_OTA: return "ota";
    case STATUS_LED_ERROR_DOMAIN_POWER: return "power";
    case STATUS_LED_ERROR_DOMAIN_SYSTEM: return "system";
    default: return "unknown";
    }
}

static const char *status_led_error_severity_name(status_led_error_severity_t severity)
{
    switch (severity) {
    case STATUS_LED_ERROR_RETRYABLE: return "retryable";
    case STATUS_LED_ERROR_HARD: return "hard";
    default: return "unknown";
    }
}

static const char *status_led_external_power_source_name(uint32_t source_flags)
{
    switch (source_flags & (STATUS_LED_POWER_SOURCE_USB_DET |
                            STATUS_LED_POWER_SOURCE_CHARGER_STATUS |
                            STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG)) {
    case STATUS_LED_POWER_SOURCE_USB_DET:
        return "usb_det";
    case STATUS_LED_POWER_SOURCE_CHARGER_STATUS:
        return "charger_status";
    case STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG:
        return "usb_serial_jtag";
    case STATUS_LED_POWER_SOURCE_USB_DET | STATUS_LED_POWER_SOURCE_CHARGER_STATUS:
        return "usb_det|charger_status";
    case STATUS_LED_POWER_SOURCE_USB_DET | STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG:
        return "usb_det|usb_serial_jtag";
    case STATUS_LED_POWER_SOURCE_CHARGER_STATUS | STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG:
        return "charger_status|usb_serial_jtag";
    case STATUS_LED_POWER_SOURCE_USB_DET |
        STATUS_LED_POWER_SOURCE_CHARGER_STATUS |
        STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG:
        return "usb_det|charger_status|usb_serial_jtag";
    default:
        return "none";
    }
}

static bool status_led_charger_status_external_locked(
    bool raw_charging,
    bool raw_full_external,
    uint32_t now_ms)
{
    if (raw_charging || raw_full_external) {
        s_state.charger_status_external_until_ms =
            now_ms + STATUS_LED_CHARGER_STATUS_EXTERNAL_HOLD_MS;
        return true;
    }

    if (s_state.charger_status_external_until_ms != 0U &&
        now_ms < s_state.charger_status_external_until_ms) {
        return true;
    }

    s_state.charger_status_external_until_ms = 0U;
    return false;
}

static uint32_t status_led_power_poll_interval_ms_locked(void)
{
    if (s_state.external_power_present || s_state.charging || s_state.full) {
        return STATUS_LED_EXTERNAL_POWER_POLL_MS;
    }
    return s_state.low_power_disabled
        ? STATUS_LED_LOW_POWER_POLL_MS
        : STATUS_LED_POWER_POLL_MS;
}

static uint8_t status_led_profile_cap_percent_for(status_led_profile_t profile, bool safety)
{
    if (safety) {
        return STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    }
    switch (profile) {
    case STATUS_LED_PROFILE_OFF:
        return 0U;
    case STATUS_LED_PROFILE_LOW:
        return STATUS_LED_LOW_PROFILE_CAP_PERCENT;
    case STATUS_LED_PROFILE_AMBIENT:
        return STATUS_LED_AMBIENT_PROFILE_CAP_PERCENT;
    case STATUS_LED_PROFILE_FACTORY:
        return STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    case STATUS_LED_PROFILE_STANDARD:
    default:
        return STATUS_LED_STANDARD_PROFILE_CAP_PERCENT;
    }
}

static uint32_t status_led_profile_budget_ma_for(status_led_profile_t profile, bool safety)
{
    if (safety || profile == STATUS_LED_PROFILE_FACTORY) {
        return STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA;
    }
    switch (profile) {
    case STATUS_LED_PROFILE_OFF:
        return 0U;
    case STATUS_LED_PROFILE_LOW:
        return STATUS_LED_LOW_PROFILE_BUDGET_MA;
    case STATUS_LED_PROFILE_AMBIENT:
        return STATUS_LED_AMBIENT_PROFILE_BUDGET_MA;
    case STATUS_LED_PROFILE_STANDARD:
    default:
        return STATUS_LED_STANDARD_PROFILE_BUDGET_MA;
    }
}

static uint32_t status_led_profile_budget_ma_locked(bool safety)
{
    return status_led_profile_budget_ma_for(s_state.profile, safety);
}

static uint8_t status_led_effect_percent_locked(uint8_t desired_percent, bool safety)
{
    if (desired_percent == 0U) {
        return 0U;
    }
    uint32_t scaled = desired_percent;
    if (!safety) {
        scaled = desired_percent;
    }
    uint8_t cap = status_led_profile_cap_percent_for(s_state.profile, safety);
    if (scaled > cap) {
        scaled = cap;
    }
    return status_led_clamp_u32_to_u8(scaled);
}

static status_led_rgb_t status_led_token_locked(status_led_rgb_t color, uint8_t desired_percent, bool safety)
{
    return status_led_scale_gamma(color, status_led_effect_percent_locked(desired_percent, safety));
}

static uint8_t status_led_effect_percent_relative_to_peak(uint8_t desired_percent, uint8_t peak_percent)
{
    if (desired_percent == 0U || peak_percent == 0U) {
        return 0U;
    }
    uint32_t scaled =
        ((uint32_t)desired_percent * STATUS_LED_FULL_BRIGHTNESS_PERCENT +
         ((uint32_t)peak_percent / 2U)) /
        (uint32_t)peak_percent;
    if (scaled > STATUS_LED_FULL_BRIGHTNESS_PERCENT) {
        scaled = STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    }
    return (uint8_t)scaled;
}

static status_led_rgb_t status_led_token_relative_to_peak_locked(
    status_led_rgb_t color,
    uint8_t desired_percent,
    uint8_t peak_percent,
    bool safety)
{
    return status_led_token_locked(
        color,
        status_led_effect_percent_relative_to_peak(desired_percent, peak_percent),
        safety);
}

static uint8_t status_led_triangle_percent(uint32_t now_ms, uint32_t period_ms, uint8_t min_percent, uint8_t max_percent)
{
    if (period_ms == 0 || max_percent <= min_percent) {
        return max_percent;
    }
    uint32_t phase = now_ms % period_ms;
    uint32_t half = period_ms / 2U;
    uint32_t range = (uint32_t)(max_percent - min_percent);
    if (phase <= half) {
        return (uint8_t)(min_percent + (range * phase) / half);
    }
    return (uint8_t)(min_percent + (range * (period_ms - phase)) / half);
}

static uint8_t status_led_quantize_percent(uint8_t percent, uint8_t quantum_percent)
{
    if (percent == 0U || quantum_percent <= 1U) {
        return percent;
    }
    uint32_t quantized =
        (((uint32_t)percent + ((uint32_t)quantum_percent / 2U)) / quantum_percent) * quantum_percent;
    return quantized > 100U ? 100U : (uint8_t)quantized;
}

static uint32_t status_led_smoothstep_per_mille(uint32_t position_ms, uint32_t duration_ms)
{
    if (duration_ms == 0U || position_ms >= duration_ms) {
        return 1000U;
    }
    uint64_t x = ((uint64_t)position_ms * 1000ULL) / duration_ms;
    uint64_t x2 = x * x;
    uint64_t x3 = x2 * x;
    return (uint32_t)((3ULL * x2 * 1000ULL - 2ULL * x3 + 500000ULL) / 1000000ULL);
}

static uint8_t status_led_lerp_percent(uint8_t start_percent, uint8_t end_percent, uint32_t eased_per_mille)
{
    if (eased_per_mille >= 1000U) {
        return end_percent;
    }
    int32_t range = (int32_t)end_percent - (int32_t)start_percent;
    int32_t scaled = (int32_t)start_percent + (int32_t)((range * (int32_t)eased_per_mille + 500) / 1000);
    if (scaled < 0) {
        return 0U;
    }
    if (scaled > 100) {
        return 100U;
    }
    return (uint8_t)scaled;
}

static uint8_t status_led_decay_percent(uint32_t elapsed_ms, uint32_t duration_ms, uint8_t start_percent, uint8_t end_percent)
{
    if (duration_ms == 0U || elapsed_ms >= duration_ms) {
        return end_percent;
    }
    return status_led_lerp_percent(
        start_percent,
        end_percent,
        status_led_smoothstep_per_mille(elapsed_ms, duration_ms));
}

static uint8_t status_led_charging_breath_floor_percent_locked(void)
{
    uint8_t battery_level = status_led_battery_display_level_locked();
    if (battery_level == 0xFFU) {
        return STATUS_LED_CHARGING_BREATH_UNKNOWN_FLOOR_PERCENT;
    }
    if (battery_level > 100U) {
        battery_level = 100U;
    }
    return (uint8_t)(((uint32_t)STATUS_LED_CHARGING_BREATH_MAX_PERCENT * battery_level + 50U) / 100U);
}

static uint8_t status_led_charging_breath_lerp_percent(uint8_t floor_percent, uint32_t eased_per_mille)
{
    if (floor_percent >= STATUS_LED_CHARGING_BREATH_MAX_PERCENT) {
        return STATUS_LED_CHARGING_BREATH_MAX_PERCENT;
    }
    return status_led_lerp_percent(
        floor_percent,
        STATUS_LED_CHARGING_BREATH_MAX_PERCENT,
        eased_per_mille);
}

static uint8_t status_led_charging_breath_percent_locked(uint32_t now_ms)
{
    const uint8_t floor_percent = status_led_charging_breath_floor_percent_locked();
    if (STATUS_LED_CHARGING_BREATH_PERIOD_MS == 0U ||
        STATUS_LED_CHARGING_BREATH_MAX_PERCENT <= floor_percent) {
        return STATUS_LED_CHARGING_BREATH_MAX_PERCENT;
    }
    uint32_t phase = now_ms % STATUS_LED_CHARGING_BREATH_PERIOD_MS;
    if (phase < STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS) {
        return floor_percent;
    }
    phase -= STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS;

    if (phase < STATUS_LED_CHARGING_BREATH_RISE_MS) {
        uint32_t eased = status_led_smoothstep_per_mille(
            phase,
            STATUS_LED_CHARGING_BREATH_RISE_MS);
        return status_led_charging_breath_lerp_percent(floor_percent, eased);
    }
    phase -= STATUS_LED_CHARGING_BREATH_RISE_MS;

    if (phase < STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS) {
        return STATUS_LED_CHARGING_BREATH_MAX_PERCENT;
    }
    phase -= STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS;

    uint32_t fall_ms =
        STATUS_LED_CHARGING_BREATH_PERIOD_MS -
        STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS -
        STATUS_LED_CHARGING_BREATH_RISE_MS -
        STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS;
    if (fall_ms == 0U || phase >= fall_ms) {
        return floor_percent;
    }
    uint32_t eased = status_led_smoothstep_per_mille(phase, fall_ms);
    return status_led_charging_breath_lerp_percent(floor_percent, 1000U - eased);
}

static bool status_led_blink_on(uint32_t now_ms, uint32_t on_ms, uint32_t off_ms)
{
    uint32_t period = on_ms + off_ms;
    if (period == 0) {
        return true;
    }
    return (now_ms % period) < on_ms;
}

static bool status_led_double_pulse_on(uint32_t now_ms, uint32_t period_ms)
{
    uint32_t phase = now_ms % period_ms;
    return phase < 120U || (phase >= 240U && phase < 360U);
}

static bool status_led_error_pulse_on_locked(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s_state.error_started_ms;
    if (s_state.error_severity == STATUS_LED_ERROR_HARD) {
        return elapsed >= 1200U || status_led_blink_on(elapsed, 120U, 120U);
    }
    return elapsed < 1080U ? status_led_blink_on(elapsed, 180U, 180U) : true;
}

static uint8_t status_led_error_percent_locked(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s_state.error_started_ms;
    if (s_state.error_severity == STATUS_LED_ERROR_HARD) {
        return elapsed >= 1200U ? 32U : 80U;
    }
    return elapsed >= 1080U ? 24U : 55U;
}

static esp_err_t status_led_transmit_strip(
    status_led_strip_t *strip,
    const status_led_rgb_t *colors,
    bool force_non_dma)
{
    return force_non_dma
        ? status_led_strip_backend_transmit_non_dma_once(strip->backend, strip->color_order, colors)
        : status_led_strip_backend_transmit(strip->backend, strip->color_order, colors);
}

static bool status_led_frame_equal(const status_led_frame_t *left, const status_led_frame_t *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static uint8_t status_led_frame_changed_strip_mask(const status_led_frame_t *left, const status_led_frame_t *right)
{
    if (status_led_frame_equal(left, right)) {
        return 0U;
    }

    uint8_t mask = 0;
    if (memcmp(left->status, right->status, sizeof(left->status)) != 0) {
        mask |= STATUS_LED_STRIP_MASK_STATUS;
    }
    if (memcmp(left->ec11, right->ec11, sizeof(left->ec11)) != 0) {
        mask |= STATUS_LED_STRIP_MASK_EC11;
    }
    if (memcmp(left->key, right->key, sizeof(left->key)) != 0) {
        mask |= STATUS_LED_STRIP_MASK_KEY;
    }
    if (memcmp(left->edge, right->edge, sizeof(left->edge)) != 0) {
        mask |= STATUS_LED_STRIP_MASK_EDGE;
    }
    return mask;
}

static bool status_led_status_tail_reinforce_needed(const status_led_frame_t *frame)
{
    const bool rec_on = status_led_rgb_is_on(frame->status[STATUS_LED_SEM_REC]);
    const bool ai_on = status_led_rgb_is_on(frame->status[STATUS_LED_SEM_AI]);
    const bool ok_warn_off =
        !status_led_rgb_is_on(frame->status[STATUS_LED_SEM_OK]) &&
        !status_led_rgb_is_on(frame->status[STATUS_LED_SEM_WARN]);
    return ok_warn_off && (rec_on || ai_on);
}

static uint8_t status_led_status_tail_reinforce_write_count(const status_led_frame_t *frame)
{
    if (!status_led_status_tail_reinforce_needed(frame)) {
        return 1U;
    }
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_REC]) &&
        status_led_rgb_is_on(frame->status[STATUS_LED_SEM_AI])) {
        return STATUS_LED_STATUS_TAIL_OVERLAP_REINFORCE_WRITES;
    }
    return STATUS_LED_STATUS_TAIL_REINFORCE_WRITES;
}

static bool status_led_status_tail_dark(const status_led_frame_t *frame)
{
    return !status_led_rgb_is_on(frame->status[STATUS_LED_SEM_REC]) &&
           !status_led_rgb_is_on(frame->status[STATUS_LED_SEM_AI]) &&
           !status_led_rgb_is_on(frame->status[STATUS_LED_SEM_OK]) &&
           !status_led_rgb_is_on(frame->status[STATUS_LED_SEM_WARN]);
}

static bool status_led_status_health_rewrite_needed(
    const status_led_frame_t *frame,
    uint8_t changed_strip_mask,
    bool low_power_active,
    uint32_t now_ms)
{
    if (low_power_active || !status_led_status_tail_dark(frame)) {
        return false;
    }
    if ((changed_strip_mask & STATUS_LED_STRIP_MASK_STATUS) != 0U) {
        return false;
    }

    const uint8_t non_status_mask =
        STATUS_LED_STRIP_MASK_EC11 |
        STATUS_LED_STRIP_MASK_KEY |
        STATUS_LED_STRIP_MASK_EDGE;
    if ((changed_strip_mask & non_status_mask) != 0U) {
        return true;
    }

    uint32_t last_status_tx_ms = s_strip_last_tx_ms[STATUS_LED_STRIP_STATUS];
    return last_status_tx_ms == 0U ||
           (uint32_t)(now_ms - last_status_tx_ms) >= STATUS_LED_STATUS_HEALTH_RESYNC_MS;
}

static bool status_led_key_dark_rewrite_needed(
    const status_led_frame_t *frame,
    uint8_t changed_strip_mask,
    bool low_power_active,
    uint32_t now_ms)
{
    if (low_power_active || status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT)) {
        return false;
    }
    if ((changed_strip_mask & STATUS_LED_STRIP_MASK_KEY) != 0U) {
        return false;
    }

    uint32_t last_key_tx_ms = s_strip_last_tx_ms[STATUS_LED_STRIP_KEY];
    if (last_key_tx_ms == 0U) {
        return true;
    }
#if STATUS_LED_KEY_DARK_RESYNC_MS == 0U
    (void)now_ms;
    return false;
#else
    return (uint32_t)(now_ms - last_key_tx_ms) >= STATUS_LED_KEY_DARK_RESYNC_MS;
#endif
}

static void status_led_note_strip_transmitted(status_led_strip_id_t strip_index, uint32_t now_ms)
{
    if (strip_index >= STATUS_LED_STRIP_COUNT) {
        return;
    }
    s_strip_last_tx_ms[strip_index] = now_ms;
    s_strip_transport_suspended[strip_index] =
        !status_led_strip_backend_available(s_strips[strip_index].backend);
}

static uint8_t status_led_strip_mask_for_index(status_led_strip_id_t strip_index)
{
    switch (strip_index) {
    case STATUS_LED_STRIP_STATUS:
        return STATUS_LED_STRIP_MASK_STATUS;
    case STATUS_LED_STRIP_EC11:
        return STATUS_LED_STRIP_MASK_EC11;
    case STATUS_LED_STRIP_KEY:
        return STATUS_LED_STRIP_MASK_KEY;
    case STATUS_LED_STRIP_EDGE:
        return STATUS_LED_STRIP_MASK_EDGE;
    default:
        return 0U;
    }
}

static uint8_t status_led_note_strip_transmit_result(
    status_led_strip_id_t strip_index,
    esp_err_t ret,
    uint32_t tx_ms,
    bool force_non_dma)
{
    if (ret == ESP_OK) {
        status_led_note_strip_transmitted(strip_index, tx_ms);
        return 0U;
    }
    if (strip_index >= STATUS_LED_STRIP_COUNT) {
        return 0U;
    }
    ESP_LOGW(TAG,
             "LED strip tx failed strip=%s ret=%s force_non_dma=%u",
             s_strips[strip_index].name,
             esp_err_to_name(ret),
             force_non_dma ? 1U : 0U);
    diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
             (uint32_t)s_strips[strip_index].gpio,
             (uint32_t)ret,
             20U + (uint32_t)strip_index,
             force_non_dma ? 1U : 0U);
    return status_led_strip_mask_for_index(strip_index);
}

static void status_led_update_retry_strip_mask(uint8_t attempted_strip_mask, uint8_t failed_strip_mask)
{
    attempted_strip_mask = (uint8_t)(attempted_strip_mask & STATUS_LED_STRIP_MASK_ALL);
    failed_strip_mask = (uint8_t)(failed_strip_mask & attempted_strip_mask);
    if (attempted_strip_mask == 0U || s_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(STATUS_LED_TX_MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG,
                 "LED retry mask update skipped: attempted=0x%02x failed=0x%02x",
                 attempted_strip_mask,
                 failed_strip_mask);
        return;
    }
    const uint8_t succeeded_strip_mask = (uint8_t)(attempted_strip_mask & (uint8_t)(~failed_strip_mask));
    uint8_t retry_strip_mask =
        (uint8_t)(s_state.retry_strip_mask & (uint8_t)(~succeeded_strip_mask));
    retry_strip_mask = (uint8_t)(retry_strip_mask | failed_strip_mask);
    s_state.retry_strip_mask = (uint8_t)(retry_strip_mask & STATUS_LED_STRIP_MASK_ALL);
    xSemaphoreGive(s_mutex);
}

static bool status_led_idle_transport_release_pending(void)
{
    for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
        if (!s_strip_transport_suspended[index] && s_strip_last_tx_ms[index] != 0U) {
            return true;
        }
    }
    return false;
}

static bool status_led_transport_release_due(uint32_t now_ms, uint32_t last_tx_ms)
{
#if STATUS_LED_RMT_IDLE_RELEASE_MS == 0U
    (void)now_ms;
    (void)last_tx_ms;
    return true;
#else
    return (uint32_t)(now_ms - last_tx_ms) >= STATUS_LED_RMT_IDLE_RELEASE_MS;
#endif
}

static uint8_t status_led_suspended_strip_mask(void)
{
    uint8_t mask = 0U;
    for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
        if (s_strip_transport_suspended[index]) {
            mask = (uint8_t)(mask | status_led_strip_mask_for_index((status_led_strip_id_t)index));
        }
    }
    return mask;
}

static bool status_led_skip_suspended_spi_final_latch(
    status_led_strip_id_t strip_index,
    const status_led_rgb_t *colors,
    size_t count,
    bool force_non_dma)
{
    if (!force_non_dma || strip_index >= STATUS_LED_STRIP_COUNT) {
        return false;
    }
    if (s_strips[strip_index].transport != STATUS_LED_STRIP_TRANSPORT_SPI) {
        return false;
    }
    if (!s_strip_transport_suspended[strip_index]) {
        return false;
    }
    return !status_led_strip_has_light(colors, count);
}

static void status_led_suspend_quiet_idle_transports(bool low_power_active, uint32_t now_ms)
{
    if (!low_power_active || !status_led_idle_transport_release_pending()) {
        return;
    }
    for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
        if (s_strip_transport_suspended[index] || s_strip_last_tx_ms[index] == 0U) {
            continue;
        }
        if (!status_led_transport_release_due(now_ms, s_strip_last_tx_ms[index])) {
            continue;
        }
        (void)status_led_strip_backend_suspend(s_strips[index].backend);
        s_strip_transport_suspended[index] = true;
    }
}

static uint8_t status_led_transmit_changed_frame(
    const status_led_frame_t *frame,
    uint8_t strip_mask,
    bool force_non_dma,
    bool key_dark_latch_pending_tx)
{
    if (strip_mask == 0U) {
        return 0U;
    }

    bool tx_locked = false;
    if (s_tx_mutex != NULL) {
        if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(STATUS_LED_TX_MUTEX_WAIT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "LED transmit skipped: tx mutex timeout");
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                      0, (uint32_t)ESP_ERR_TIMEOUT, 2, 0);
            return strip_mask;
        }
        tx_locked = true;
    }

    const uint32_t tx_ms = status_led_now_ms();
    uint8_t failed_strip_mask = 0U;
    if ((strip_mask & STATUS_LED_STRIP_MASK_EC11) != 0U &&
        !status_led_skip_suspended_spi_final_latch(
            STATUS_LED_STRIP_EC11,
            frame->ec11,
            STATUS_LED_EC11_COUNT,
            force_non_dma)) {
        esp_err_t ret = status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EC11], frame->ec11, force_non_dma);
        failed_strip_mask = (uint8_t)(
            failed_strip_mask |
            status_led_note_strip_transmit_result(STATUS_LED_STRIP_EC11, ret, tx_ms, force_non_dma));
    }
    if ((strip_mask & STATUS_LED_STRIP_MASK_KEY) != 0U) {
        bool key_has_light = status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT);
        bool key_force_non_dma = force_non_dma || !key_has_light;
        if (!key_dark_latch_pending_tx &&
            status_led_skip_suspended_spi_final_latch(
                STATUS_LED_STRIP_KEY,
                frame->key,
                STATUS_LED_KEY_COUNT,
                key_force_non_dma)) {
            /* Suspended dark KEY frames are normally skipped to avoid periodic
             * idle chatter. A pending latch is the one bounded exception. */
        } else {
            esp_err_t ret = status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_KEY], frame->key, key_force_non_dma);
            failed_strip_mask = (uint8_t)(
                failed_strip_mask |
                status_led_note_strip_transmit_result(STATUS_LED_STRIP_KEY, ret, tx_ms, key_force_non_dma));
        }
    }
    if ((strip_mask & STATUS_LED_STRIP_MASK_EDGE) != 0U) {
        esp_err_t ret = status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EDGE], frame->edge, force_non_dma);
        failed_strip_mask = (uint8_t)(
            failed_strip_mask |
            status_led_note_strip_transmit_result(STATUS_LED_STRIP_EDGE, ret, tx_ms, force_non_dma));
    }
    if ((strip_mask & STATUS_LED_STRIP_MASK_STATUS) != 0U) {
        uint8_t status_writes = status_led_status_tail_reinforce_write_count(frame);
        if (force_non_dma && status_writes < STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES) {
            status_writes = STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES;
        }
        const uint8_t status_attempts = force_non_dma
            ? (uint8_t)(status_writes + STATUS_LED_LOW_POWER_STATUS_RETRY_WRITES)
            : status_writes;
        bool status_transmitted = false;
        esp_err_t last_status_ret = ESP_OK;
        uint8_t successful_writes = 0U;
        for (uint8_t write_index = 0;
             write_index < status_attempts && successful_writes < status_writes;
             ++write_index) {
            esp_err_t status_ret = status_led_transmit_strip(
                &s_strips[STATUS_LED_STRIP_STATUS],
                frame->status,
                force_non_dma);
            if (status_ret != ESP_OK) {
                last_status_ret = status_ret;
                if (!force_non_dma) {
                    break;
                }
                (void)status_led_strip_backend_suspend(s_strips[STATUS_LED_STRIP_STATUS].backend);
                s_strip_transport_suspended[STATUS_LED_STRIP_STATUS] = true;
                continue;
            }
            status_transmitted = true;
            ++successful_writes;
        }
        if (status_transmitted) {
            status_led_note_strip_transmitted(STATUS_LED_STRIP_STATUS, tx_ms);
        } else {
            failed_strip_mask = (uint8_t)(
                failed_strip_mask |
                status_led_note_strip_transmit_result(
                    STATUS_LED_STRIP_STATUS,
                    last_status_ret == ESP_OK ? ESP_FAIL : last_status_ret,
                    tx_ms,
                    force_non_dma));
        }
    }

    if (tx_locked) {
        xSemaphoreGive(s_tx_mutex);
    }
    return failed_strip_mask;
}

static void status_led_transmit_frame(const status_led_frame_t *frame)
{
    uint8_t failed_strip_mask = status_led_transmit_changed_frame(frame, STATUS_LED_STRIP_MASK_ALL, false, false);
    status_led_update_retry_strip_mask(STATUS_LED_STRIP_MASK_ALL, failed_strip_mask);
}

static uint32_t status_led_estimate_current_ma(const status_led_frame_t *frame)
{
    uint32_t channel_sum = 0;
    for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
        channel_sum += frame->status[index].r + frame->status[index].g + frame->status[index].b;
    }
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        channel_sum += frame->ec11[index].r + frame->ec11[index].g + frame->ec11[index].b;
    }
    for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        channel_sum += frame->key[index].r + frame->key[index].g + frame->key[index].b;
    }
    for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
        channel_sum += frame->edge[index].r + frame->edge[index].g + frame->edge[index].b;
    }
    return (channel_sum * 20U + 254U) / 255U;
}

static void status_led_scale_frame_percent(status_led_frame_t *frame, uint8_t percent)
{
    for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
        frame->status[index] = status_led_scale_raw(frame->status[index], percent);
    }
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        frame->ec11[index] = status_led_scale_raw(frame->ec11[index], percent);
    }
    for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        frame->key[index] = status_led_scale_raw(frame->key[index], percent);
    }
    for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
        frame->edge[index] = status_led_scale_raw(frame->edge[index], percent);
    }
}

static void status_led_scale_strip_percent(status_led_rgb_t *colors, size_t count, uint8_t percent)
{
    if (percent >= 100U) {
        return;
    }
    for (size_t index = 0; index < count; ++index) {
        colors[index] = status_led_scale_raw(colors[index], percent);
    }
}

static uint16_t status_led_rgb_channel_sum(status_led_rgb_t color)
{
    return (uint16_t)color.r + (uint16_t)color.g + (uint16_t)color.b;
}

static void status_led_distribute_channel_remainder(
    status_led_rgb_t source,
    status_led_rgb_t *scaled,
    uint32_t *rem_r,
    uint32_t *rem_g,
    uint32_t *rem_b)
{
    if (source.r > 0U && *rem_r >= *rem_g && *rem_r >= *rem_b) {
        scaled->r = status_led_clamp_u32_to_u8((uint32_t)scaled->r + 1U);
        *rem_r = 0U;
    } else if (source.g > 0U && *rem_g >= *rem_r && *rem_g >= *rem_b) {
        scaled->g = status_led_clamp_u32_to_u8((uint32_t)scaled->g + 1U);
        *rem_g = 0U;
    } else if (source.b > 0U) {
        scaled->b = status_led_clamp_u32_to_u8((uint32_t)scaled->b + 1U);
        *rem_b = 0U;
    }
}

static status_led_rgb_t status_led_scale_rgb_to_channel_sum(status_led_rgb_t color, uint8_t target_sum)
{
    uint16_t sum = status_led_rgb_channel_sum(color);
    if (target_sum == 0U || sum == 0U) {
        return status_led_rgb(0, 0, 0);
    }
    if (sum <= target_sum) {
        return color;
    }

    uint32_t r_num = (uint32_t)color.r * target_sum;
    uint32_t g_num = (uint32_t)color.g * target_sum;
    uint32_t b_num = (uint32_t)color.b * target_sum;
    status_led_rgb_t scaled = status_led_rgb(
        (uint8_t)(r_num / sum),
        (uint8_t)(g_num / sum),
        (uint8_t)(b_num / sum));
    uint32_t rem_r = r_num % sum;
    uint32_t rem_g = g_num % sum;
    uint32_t rem_b = b_num % sum;

    while (status_led_rgb_channel_sum(scaled) < target_sum) {
        status_led_distribute_channel_remainder(color, &scaled, &rem_r, &rem_g, &rem_b);
    }
    return scaled;
}

static status_led_rgb_t status_led_balance_status_rgb_energy_to_peak(status_led_rgb_t color)
{
    const uint16_t sum = status_led_rgb_channel_sum(color);
    const uint8_t peak = status_led_rgb_max_channel(color);
    if (sum <= peak) {
        return color;
    }
    return status_led_scale_rgb_to_channel_sum(color, peak);
}

static void status_led_balance_status_rgb_energy_to_peak_strip(status_led_rgb_t *colors, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        colors[index] = status_led_balance_status_rgb_energy_to_peak(colors[index]);
    }
}

static void status_led_apply_zone_brightness_caps_locked(status_led_frame_t *frame)
{
    status_led_scale_strip_percent(
        frame->status,
        STATUS_LED_STATUS_COUNT,
        s_state.status_zone_brightness_percent);
    status_led_balance_status_rgb_energy_to_peak_strip(frame->status, STATUS_LED_STATUS_COUNT);
    status_led_scale_strip_percent(
        frame->key,
        STATUS_LED_KEY_COUNT,
        s_state.key_zone_brightness_percent);
    status_led_scale_strip_percent(
        frame->ec11,
        STATUS_LED_EC11_COUNT,
        s_state.ec11_zone_brightness_percent);
    status_led_scale_strip_percent(
        frame->edge,
        STATUS_LED_EDGE_COUNT,
        s_state.edge_zone_brightness_percent);
}

static void status_led_clamp_current_locked(status_led_frame_t *frame, bool safety)
{
    s_state.last_budget_scale_percent = 100U;
    s_state.current_limited_by_budget = false;

    uint32_t estimated_ma = status_led_estimate_current_ma(frame);
    uint32_t budget_ma = status_led_profile_budget_ma_locked(safety);
    if (estimated_ma > budget_ma && estimated_ma > 0 && budget_ma > 0) {
        uint8_t scale_percent = (uint8_t)((budget_ma * 100U) / estimated_ma);
        if (scale_percent == 0) {
            scale_percent = 1;
        }
        status_led_scale_frame_percent(frame, scale_percent);
        s_state.last_budget_scale_percent = scale_percent;
        s_state.current_limited_by_budget = true;
        estimated_ma = status_led_estimate_current_ma(frame);
    } else if (budget_ma == 0 && estimated_ma > 0) {
        memset(frame, 0, sizeof(*frame));
        s_state.last_budget_scale_percent = 0U;
        s_state.current_limited_by_budget = true;
        estimated_ma = 0;
    }
    s_state.last_estimated_current_ma = estimated_ma;
    s_state.last_current_budget_ma = budget_ma;
}

static void status_led_copy_frame_locked(const status_led_frame_t *frame)
{
    memcpy(&s_state.last_frame, frame, sizeof(s_state.last_frame));
}

static bool status_led_frame_has_non_idle_accents(const status_led_frame_t *frame)
{
    return status_led_rgb_is_on(frame->status[STATUS_LED_SEM_REC]) ||
           status_led_rgb_is_on(frame->status[STATUS_LED_SEM_AI]) ||
           status_led_rgb_is_on(frame->status[STATUS_LED_SEM_OK]) ||
           status_led_rgb_is_on(frame->status[STATUS_LED_SEM_WARN]) ||
           status_led_strip_has_light(frame->ec11, STATUS_LED_EC11_COUNT) ||
           status_led_strip_has_light(frame->key, STATUS_LED_KEY_COUNT) ||
           status_led_strip_has_light(frame->edge, STATUS_LED_EDGE_COUNT);
}

static bool status_led_should_schedule_idle_transition_clear_locked(uint32_t now_ms)
{
    if (s_state.output_disabled || s_state.low_power_disabled) {
        return false;
    }
    if (status_led_frame_has_non_idle_accents(&s_state.last_frame)) {
        return true;
    }
    if (s_state.test_mode != STATUS_LED_TEST_NONE ||
        s_state.recording_active ||
        s_state.processing_active ||
        s_state.ota_active) {
        return true;
    }
    if (s_state.ble_state == STATUS_LED_BLE_PAIRING ||
        s_state.ble_state == STATUS_LED_BLE_RECONNECTING ||
        s_state.ble_state == STATUS_LED_BLE_REPAIRING) {
        return true;
    }
    return now_ms < s_state.ok_until_ms ||
           now_ms < s_state.error_until_ms ||
           now_ms < s_state.ble_repair_until_ms ||
           now_ms < s_state.ble_repair_cue_until_ms;
}

static void status_led_schedule_idle_transition_clear_locked(uint32_t now_ms)
{
    if (status_led_should_schedule_idle_transition_clear_locked(now_ms)) {
        s_state.transition_clear_mask |= STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS;
    }
}

static void status_led_force_transition_clear_locked(uint8_t mask)
{
    s_state.transition_clear_mask |= mask;
}

static bool status_led_render_transition_clear_locked(status_led_frame_t *frame, uint8_t *clear_mask_out)
{
    uint8_t clear_mask = s_state.transition_clear_mask;
    if (clear_mask == 0U) {
        return false;
    }
    /*
     * Scoped clear: start from the last transmitted frame and clear only the
     * layers named by transition_clear_mask. This keeps PWR/BLE/REC/AI/OK/WARN
     * and EC11/KEY/EDGE independent: a BLE state refresh cannot blank recording,
     * OK/AI cleanup cannot downgrade the connected BLE semantic slot, and routine
     * status cleanup cannot black-frame an in-flight physical key cue.
     */
    if (s_state.output_disabled ||
        (clear_mask & STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS) != 0U) {
        memset(frame, 0, sizeof(*frame));
    } else {
        *frame = s_state.last_frame;
        if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS) != 0U) {
            frame->status[STATUS_LED_SEM_REC] = (status_led_rgb_t){0};
            frame->status[STATUS_LED_SEM_AI] = (status_led_rgb_t){0};
            frame->status[STATUS_LED_SEM_OK] = (status_led_rgb_t){0};
            frame->status[STATUS_LED_SEM_WARN] = (status_led_rgb_t){0};
        }
        if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_BLE) != 0U) {
            frame->status[STATUS_LED_SEM_BLE] = (status_led_rgb_t){0};
        }
        if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_EC11) != 0U) {
            memset(frame->ec11, 0, sizeof(frame->ec11));
        }
        if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_KEY) != 0U) {
            memset(frame->key, 0, sizeof(frame->key));
        }
        if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_EDGE) != 0U) {
            memset(frame->edge, 0, sizeof(frame->edge));
        }
    }
    if (clear_mask_out != NULL) {
        *clear_mask_out = clear_mask;
    }
    s_state.transition_clear_mask = 0U;
    s_state.last_estimated_current_ma = 0;
    s_state.last_current_budget_ma = 0;
    s_state.last_budget_scale_percent = 0U;
    s_state.current_limited_by_budget = false;
    return true;
}

static uint8_t status_led_transition_clear_strip_mask_locked(
    const status_led_frame_t *previous,
    uint8_t clear_mask)
{
    if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS) != 0U ||
        previous == NULL) {
        return STATUS_LED_STRIP_MASK_ALL;
    }

    uint8_t mask = 0U;
    if ((clear_mask & (STATUS_LED_TRANSITION_CLEAR_STATUS_ACCENTS |
                       STATUS_LED_TRANSITION_CLEAR_BLE)) != 0U) {
        mask = (uint8_t)(mask | STATUS_LED_STRIP_MASK_STATUS);
    }
    if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_EC11) != 0U &&
        status_led_strip_has_light(previous->ec11, STATUS_LED_EC11_COUNT)) {
        mask = (uint8_t)(mask | STATUS_LED_STRIP_MASK_EC11);
    }
    if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_KEY) != 0U &&
        status_led_strip_has_light(previous->key, STATUS_LED_KEY_COUNT)) {
        mask = (uint8_t)(mask | STATUS_LED_STRIP_MASK_KEY);
    }
    if ((clear_mask & STATUS_LED_TRANSITION_CLEAR_EDGE) != 0U &&
        status_led_strip_has_light(previous->edge, STATUS_LED_EDGE_COUNT)) {
        mask = (uint8_t)(mask | STATUS_LED_STRIP_MASK_EDGE);
    }
    return mask;
}

static void status_led_request_refresh(void)
{
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

static bool status_led_ble_repair_active_locked(uint32_t now_ms)
{
    return now_ms < s_state.ble_repair_until_ms;
}

static bool status_led_ble_recovery_window_active_locked(uint32_t now_ms)
{
    return status_led_ble_repair_active_locked(now_ms) &&
           (s_state.ble_state == STATUS_LED_BLE_PAIRING ||
            s_state.ble_state == STATUS_LED_BLE_REPAIRING ||
            s_state.ble_state == STATUS_LED_BLE_RECONNECTING);
}

static uint32_t status_led_ble_elapsed_locked(uint32_t now_ms)
{
    uint32_t start_ms = s_state.ble_transition_ms != 0U
        ? s_state.ble_transition_ms
        : s_state.last_transition_ms;
    return now_ms - start_ms;
}

static uint32_t status_led_ble_repair_cue_elapsed_locked(uint32_t now_ms)
{
    if (s_state.ble_repair_cue_started_ms != 0U) {
        if (now_ms < s_state.ble_repair_cue_started_ms) {
            return 0U;
        }
        return now_ms - s_state.ble_repair_cue_started_ms;
    }
    return status_led_ble_elapsed_locked(now_ms);
}

static uint32_t status_led_ble_recovery_window_elapsed_locked(uint32_t now_ms)
{
    if (s_state.ble_repair_cue_until_ms != 0U) {
        if (now_ms < s_state.ble_repair_cue_until_ms) {
            return 0U;
        }
        return now_ms - s_state.ble_repair_cue_until_ms;
    }
    return status_led_ble_elapsed_locked(now_ms);
}

static bool status_led_ble_repair_cue_active_locked(uint32_t now_ms)
{
    if (s_state.ble_repair_cue_started_ms == 0U ||
        now_ms >= s_state.ble_repair_cue_until_ms) {
        return false;
    }
    if (now_ms < s_state.ble_repair_cue_started_ms) {
        return true;
    }
    return status_led_ble_repair_cue_elapsed_locked(now_ms) < STATUS_LED_BLE_REPAIR_CUE_MS;
}

static uint8_t status_led_ble_repair_percent_locked(
    uint32_t now_ms,
    uint8_t base_percent,
    uint8_t peak_percent)
{
    if (!status_led_ble_repair_cue_active_locked(now_ms)) {
        return 0U;
    }
    if (s_state.ble_repair_cue_started_ms != 0U &&
        now_ms < s_state.ble_repair_cue_started_ms) {
        return base_percent;
    }

    uint32_t ble_elapsed_ms = status_led_ble_repair_cue_elapsed_locked(now_ms);
    if (ble_elapsed_ms >= STATUS_LED_BLE_REPAIR_CUE_MS) {
        return 0U;
    }
    if (status_led_double_pulse_on(ble_elapsed_ms, 900U)) {
        return peak_percent;
    }
    return base_percent;
}

static bool status_led_timed_output_active_locked(uint32_t now_ms)
{
    if (s_state.output_disabled || s_state.low_power_disabled) {
        return false;
    }
    if (s_state.test_mode != STATUS_LED_TEST_NONE ||
        s_state.recording_active ||
        s_state.processing_active ||
        s_state.ota_active) {
        return true;
    }
    if (now_ms < s_state.boot_feedback_until_ms ||
        now_ms < s_state.status_window_until_ms ||
        now_ms < s_state.ble_confidence_until_ms ||
        now_ms < s_state.ble_repair_until_ms ||
        now_ms < s_state.ble_repair_cue_until_ms ||
        now_ms < s_state.oobe_confidence_until_ms ||
        now_ms < s_state.error_until_ms ||
        now_ms < s_state.ok_until_ms) {
        return true;
    }
    if (s_state.key_pressed_mask != 0U) {
        return true;
    }
    if (status_led_ec11_feedback_active_locked(now_ms)) {
        return true;
    }
    for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        if (now_ms < s_state.key_until_ms[index] ||
            now_ms < s_state.key_feedback_until_ms[index]) {
            return true;
        }
    }
    if (s_state.external_power_present && s_state.charging && !s_state.full) {
        return true;
    }
    if (!s_state.external_power_present && s_state.battery_valid &&
        s_state.battery_level_percent < 20U) {
        return true;
    }
    if (s_state.ble_state == STATUS_LED_BLE_PAIRING ||
        s_state.ble_state == STATUS_LED_BLE_REPAIRING ||
        s_state.ble_state == STATUS_LED_BLE_RECONNECTING ||
        s_state.ble_state == STATUS_LED_BLE_CONNECTED ||
        s_state.ble_state == STATUS_LED_BLE_TYPE_READY ||
        s_state.ble_state == STATUS_LED_BLE_DISCONNECTED) {
        return true;
    }
    return s_state.profile == STATUS_LED_PROFILE_AMBIENT;
}

static bool status_led_low_power_ble_ready_window_active_locked(uint32_t now_ms);

static uint32_t status_led_refresh_delay_ms_locked(uint32_t now_ms)
{
    if (s_state.output_disabled) {
        return STATUS_LED_LOW_POWER_IDLE_REFRESH_MS;
    }
    if (s_state.low_power_disabled) {
        if (status_led_low_power_ble_ready_window_active_locked(now_ms)) {
            return STATUS_LED_REFRESH_MS;
        }
        return s_state.external_power_present
            ? STATUS_LED_EXTERNAL_POWER_POLL_MS
            : STATUS_LED_LOW_POWER_IDLE_REFRESH_MS;
    }
    return status_led_timed_output_active_locked(now_ms)
        ? STATUS_LED_REFRESH_MS
        : STATUS_LED_IDLE_REFRESH_MS;
}

static void status_led_render_test_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    status_led_rgb_t color = {0};
    uint32_t elapsed = now_ms - s_state.test_started_ms;
    if (s_state.test_mode == STATUS_LED_TEST_RGBW) {
        switch ((elapsed / 1000U) % 4U) {
        case 0: color = status_led_token_locked(status_led_rgb(255, 0, 0), 100U, true); break;
        case 1: color = status_led_token_locked(status_led_rgb(0, 255, 0), 100U, true); break;
        case 2: color = status_led_token_locked(status_led_rgb(0, 0, 255), 100U, true); break;
        default: color = status_led_token_locked(status_led_rgb(255, 255, 255), 100U, true); break;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
                frame->status[index] = color;
            }
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
                frame->ec11[index] = color;
            }
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
                frame->key[index] = color;
            }
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE)) {
            for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
                frame->edge[index] = color;
            }
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_MAP) {
        color = status_led_token_locked(status_led_rgb(255, 255, 255), 80U, true);
        uint32_t step = elapsed / 600U;
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            frame->status[step % STATUS_LED_STATUS_COUNT] = color;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            frame->ec11[step % STATUS_LED_EC11_COUNT] = color;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            frame->key[step % STATUS_LED_KEY_COUNT] = color;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE)) {
            frame->edge[step % STATUS_LED_EDGE_COUNT] = color;
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_CHASE) {
        const uint32_t step_ms = s_state.test_step_ms > 0U
            ? (uint32_t)s_state.test_step_ms
            : STATUS_LED_CHASE_DEFAULT_STEP_MS;
        uint32_t total = 0;
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            total += STATUS_LED_STATUS_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            total += STATUS_LED_EC11_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            total += STATUS_LED_KEY_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE)) {
            total += STATUS_LED_EDGE_COUNT;
        }
        if (total == 0U) {
            return;
        }

        uint32_t step = elapsed / step_ms;
        uint32_t cursor = step % total;
        switch ((step / total) % 4U) {
        case 0: color = status_led_token_locked(status_led_rgb(255, 0, 0), 100U, true); break;
        case 1: color = status_led_token_locked(status_led_rgb(0, 255, 0), 100U, true); break;
        case 2: color = status_led_token_locked(status_led_rgb(0, 0, 255), 100U, true); break;
        default: color = status_led_token_locked(status_led_rgb(255, 255, 255), 100U, true); break;
        }

        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            if (cursor < STATUS_LED_STATUS_COUNT) {
                frame->status[cursor] = color;
                return;
            }
            cursor -= STATUS_LED_STATUS_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            if (cursor < STATUS_LED_EC11_COUNT) {
                frame->ec11[cursor] = color;
                return;
            }
            cursor -= STATUS_LED_EC11_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            if (cursor < STATUS_LED_KEY_COUNT) {
                frame->key[cursor] = color;
                return;
            }
            cursor -= STATUS_LED_KEY_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE) &&
            cursor < STATUS_LED_EDGE_COUNT) {
            frame->edge[cursor] = color;
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_PIXEL) {
        color = status_led_token_locked(s_state.test_pixel_color, s_state.test_pixel_percent, true);
        switch (s_state.test_pixel_strip) {
        case STATUS_LED_STRIP_STATUS:
            if (s_state.test_pixel_index < STATUS_LED_STATUS_COUNT) {
                frame->status[s_state.test_pixel_index] = color;
            }
            break;
        case STATUS_LED_STRIP_KEY:
            if (s_state.test_pixel_index < STATUS_LED_KEY_COUNT) {
                frame->key[s_state.test_pixel_index] = color;
            }
            break;
        case STATUS_LED_STRIP_EC11:
            if (s_state.test_pixel_index < STATUS_LED_EC11_COUNT) {
                frame->ec11[s_state.test_pixel_index] = color;
            }
            break;
        case STATUS_LED_STRIP_EDGE:
            if (s_state.test_pixel_index < STATUS_LED_EDGE_COUNT) {
                frame->edge[s_state.test_pixel_index] = color;
            }
            break;
        default:
            break;
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_STATIC_STATUS) {
        frame->status[STATUS_LED_SEM_REC] = status_led_token_locked(
            status_led_rec_gold(),
            STATUS_LED_ACTIVE_WORK_REC_PERCENT,
            false);
        frame->status[STATUS_LED_SEM_AI] = status_led_token_locked(
            status_led_rgb(160, 0, 255),
            STATUS_LED_ACTIVE_WORK_AI_PERCENT,
            false);
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_STATUS_KEY_STRESS) {
        uint8_t rec_percent = status_led_triangle_percent(elapsed, 520U, 28U, 58U);
        uint8_t ai_percent = status_led_triangle_percent(elapsed + 240U, 620U, 26U, 52U);
        status_led_rgb_t rec = status_led_token_locked(status_led_rec_gold(), rec_percent, false);
        status_led_rgb_t ai = status_led_token_locked(status_led_rgb(160, 0, 255), ai_percent, false);
        if (s_state.test_pixel_index == 3U) {
            frame->status[STATUS_LED_SEM_REC] = rec;
            frame->key[0] = rec;
        } else if (s_state.test_pixel_index == 4U) {
            frame->status[STATUS_LED_SEM_AI] = ai;
            frame->key[1] = ai;
        } else if (s_state.test_pixel_index == 34U) {
            frame->status[STATUS_LED_SEM_REC] = rec;
            frame->status[STATUS_LED_SEM_AI] = ai;
            frame->key[0] = rec;
            frame->key[1] = ai;
        } else {
            frame->status[STATUS_LED_SEM_REC] = rec;
            frame->status[STATUS_LED_SEM_AI] = ai;
            frame->key[0] = rec;
            frame->key[1] = ai;
            frame->key[2] = rec;
            frame->key[3] = ai;
        }
        frame->status[STATUS_LED_SEM_OK] = (status_led_rgb_t){0};
        frame->status[STATUS_LED_SEM_WARN] = (status_led_rgb_t){0};
    }
}

static bool status_led_test_mode_uses_zone_brightness_caps(status_led_test_mode_t mode)
{
    return mode == STATUS_LED_TEST_STATIC_STATUS ||
           mode == STATUS_LED_TEST_STATUS_KEY_STRESS;
}

static void status_led_render_power_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    if (s_state.preview_effect_only) {
        return;
    }

    const bool status_window = now_ms < s_state.status_window_until_ms;
    const bool active_work = s_state.recording_active || s_state.processing_active || s_state.ota_active;
    bool safety = false;
    uint8_t percent = 0;
    status_led_rgb_t color = {0};

    if (s_state.external_power_present) {
        if (s_state.full) {
            percent = status_window
                ? STATUS_LED_FULL_STATUS_STEADY_PERCENT
                : STATUS_LED_FULL_STEADY_PERCENT;
        } else if (active_work) {
            percent = STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT;
        } else {
            percent = status_led_charging_breath_percent_locked(now_ms);
        }
        color = status_led_token_relative_to_peak_locked(
            status_led_rgb(255, 255, 255),
            percent,
            STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT,
            false);
    } else if (s_state.battery_valid) {
        const uint8_t battery_level = status_led_battery_display_level_locked();
        if (battery_level < 10U) {
            safety = true;
            if (status_led_double_pulse_on(now_ms, 1800U)) {
                color = status_led_token_locked(status_led_rgb(255, 0, 0), 90U, true);
            }
        } else if (battery_level < 20U) {
            safety = true;
            color = status_led_token_locked(
                status_led_rgb(255, 48, 0),
                STATUS_LED_LOW_BATTERY_STEADY_PERCENT,
                true);
        } else {
            percent = (status_window || active_work)
                ? STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT
                : STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT;
            if (s_state.profile == STATUS_LED_PROFILE_LOW || s_state.profile == STATUS_LED_PROFILE_OFF) {
                percent = STATUS_LED_BATTERY_STATUS_WINDOW_LOW_PROFILE_PWR_PERCENT;
            }
            if (battery_level >= STATUS_LED_BATTERY_DISPLAY_GREEN_PERCENT) {
                color = status_led_token_relative_to_peak_locked(
                    status_led_rgb(0, 255, 0),
                    percent,
                    STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT,
                    false);
            } else {
                color = status_led_token_relative_to_peak_locked(
                    status_led_rgb(255, 140, 0),
                    percent,
                    STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT,
                    false);
            }
        }
    }

    status_led_set_max(&frame->status[STATUS_LED_SEM_PWR], color);
    if (now_ms < s_state.boot_feedback_until_ms) {
        frame->status[STATUS_LED_SEM_PWR] = status_led_boot_power_color_locked();
        safety = true;
    }
    *ret_safety = *ret_safety || safety;
}

static bool status_led_low_power_ble_ready_window_active_locked(uint32_t now_ms)
{
    if (s_state.ble_state != STATUS_LED_BLE_CONNECTED &&
        s_state.ble_state != STATUS_LED_BLE_TYPE_READY) {
        return false;
    }
    return now_ms < s_state.status_window_until_ms ||
           now_ms < s_state.ble_confidence_until_ms ||
           now_ms < s_state.oobe_confidence_until_ms;
}

static uint8_t status_led_low_power_ble_percent_locked(uint32_t now_ms, uint32_t ble_elapsed_ms)
{
    if (status_led_ble_recovery_window_active_locked(now_ms)) {
        return status_led_double_pulse_on(
                   ble_elapsed_ms,
                   STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS)
            ? STATUS_LED_BLE_RECONNECT_PULSE_PERCENT
            : 0U;
    }
    switch (s_state.ble_state) {
    case STATUS_LED_BLE_PAIRING:
    case STATUS_LED_BLE_REPAIRING:
        return status_led_blink_on(
            ble_elapsed_ms,
            STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS,
            STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS)
            ? STATUS_LED_BLE_ATTENTION_PERCENT
            : 0U;
    case STATUS_LED_BLE_RECONNECTING:
        return 0U;
    case STATUS_LED_BLE_CONNECTED:
        if (!status_led_low_power_ble_ready_window_active_locked(now_ms)) {
            return 0U;
        }
        return status_led_double_pulse_on(
                   ble_elapsed_ms,
                   STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS)
            ? STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT
            : STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT;
    case STATUS_LED_BLE_TYPE_READY:
        return status_led_low_power_ble_ready_window_active_locked(now_ms)
            ? STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT
            : 0U;
    case STATUS_LED_BLE_DISCONNECTED:
    default:
        return 0U;
    }
}

static uint8_t status_led_low_power_ble_peak_percent_locked(void)
{
    switch (s_state.ble_state) {
    case STATUS_LED_BLE_PAIRING:
    case STATUS_LED_BLE_REPAIRING:
        return STATUS_LED_BLE_PAIRING_PULSE_PERCENT;
    case STATUS_LED_BLE_CONNECTED:
        return STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT;
    case STATUS_LED_BLE_TYPE_READY:
        return STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT;
    case STATUS_LED_BLE_RECONNECTING:
    case STATUS_LED_BLE_DISCONNECTED:
    default:
        return STATUS_LED_BLE_ATTENTION_PERCENT;
    }
}

static bool status_led_ota_ble_steady_locked(uint32_t now_ms)
{
    (void)now_ms;
    return s_state.ota_active;
}

static bool status_led_connected_find_type_window_active_locked(uint32_t now_ms)
{
    (void)now_ms;
    return true;
}

static void status_led_render_ble_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (s_state.preview_effect_only) {
        return;
    }

    status_led_rgb_t ble_blue = status_led_rgb(0, 0, 255);
    status_led_rgb_t color = {0};
    const uint32_t ble_elapsed_ms = status_led_ble_elapsed_locked(now_ms);
    if (status_led_ble_repair_cue_active_locked(now_ms)) {
        uint8_t percent = status_led_ble_repair_percent_locked(
            now_ms,
            STATUS_LED_BLE_REPAIR_MIN_PERCENT,
            STATUS_LED_BLE_REPAIR_MAX_PERCENT);
        color = status_led_token_locked(ble_blue, percent, false);
        status_led_set_max(&frame->status[STATUS_LED_SEM_BLE], color);
        return;
    }
    if (status_led_ble_recovery_window_active_locked(now_ms)) {
        const uint32_t recovery_elapsed_ms =
            status_led_ble_recovery_window_elapsed_locked(now_ms);
        uint8_t percent = status_led_double_pulse_on(
                              recovery_elapsed_ms,
                              STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS)
            ? STATUS_LED_BLE_RECONNECT_PULSE_PERCENT
            : 0U;
        color = status_led_token_relative_to_peak_locked(
            ble_blue,
            percent,
            STATUS_LED_BLE_RECONNECT_PULSE_PERCENT,
            false);
        status_led_set_max(&frame->status[STATUS_LED_SEM_BLE], color);
        return;
    }

    switch (s_state.ble_state) {
    case STATUS_LED_BLE_PAIRING:
    case STATUS_LED_BLE_REPAIRING: {
        uint8_t percent = status_led_blink_on(ble_elapsed_ms, 420U, 680U)
            ? STATUS_LED_BLE_PAIRING_PULSE_PERCENT
            : 0U;
        color = status_led_token_relative_to_peak_locked(
            ble_blue,
            percent,
            STATUS_LED_BLE_PAIRING_PULSE_PERCENT,
            false);
        break;
    }
    case STATUS_LED_BLE_RECONNECTING: {
        uint8_t percent = status_led_double_pulse_on(
                              ble_elapsed_ms,
                              STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS)
            ? STATUS_LED_BLE_RECONNECT_PULSE_PERCENT
            : 0U;
        color = status_led_token_relative_to_peak_locked(
            ble_blue,
            percent,
            STATUS_LED_BLE_RECONNECT_PULSE_PERCENT,
            false);
        break;
    }
    case STATUS_LED_BLE_CONNECTED:
        if (status_led_ota_ble_steady_locked(now_ms)) {
            color = status_led_token_relative_to_peak_locked(
                ble_blue,
                STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT,
                STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT,
                false);
        } else if (status_led_connected_find_type_window_active_locked(now_ms)) {
            uint8_t percent = status_led_double_pulse_on(
                                  ble_elapsed_ms,
                                  STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS)
                ? STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT
                : STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT;
            color = status_led_token_relative_to_peak_locked(
                ble_blue,
                percent,
                STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT,
                false);
        }
        break;
    case STATUS_LED_BLE_TYPE_READY:
        {
            color = status_led_token_relative_to_peak_locked(
                ble_blue,
                STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT,
                STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT,
                false);
            break;
        }
    case STATUS_LED_BLE_DISCONNECTED: {
        color = (status_led_rgb_t){0};
        break;
    }
    default:
        break;
    }

    status_led_set_max(&frame->status[STATUS_LED_SEM_BLE], color);
}

static void status_led_render_low_power_power_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    if (s_state.external_power_present) {
        status_led_set_max(
            &frame->status[STATUS_LED_SEM_PWR],
            status_led_token_relative_to_peak_locked(
                status_led_rgb(255, 255, 255),
                STATUS_LED_FULL_STATUS_STEADY_PERCENT,
                STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT,
                false));
        return;
    }

    status_led_render_power_locked(frame, now_ms, ret_safety);
    if (status_led_rgb_is_on(frame->status[STATUS_LED_SEM_PWR])) {
        return;
    }

    status_led_rgb_t color = {0};
    if (status_led_battery_display_available_locked()) {
        const uint8_t battery_level = status_led_battery_display_level_locked();
        color = status_led_token_relative_to_peak_locked(
            battery_level >= STATUS_LED_BATTERY_DISPLAY_GREEN_PERCENT
                ? status_led_rgb(0, 255, 0)
                : status_led_rgb(255, 140, 0),
            STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT,
            STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT,
            false);
    } else {
        color = status_led_token_relative_to_peak_locked(
            status_led_rgb(255, 140, 0),
            STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT,
            STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT,
            false);
    }
    status_led_set_max(&frame->status[STATUS_LED_SEM_PWR], color);
}

static void status_led_render_low_power_ble_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    uint8_t percent = status_led_low_power_ble_percent_locked(
        now_ms,
        status_led_ble_elapsed_locked(now_ms));
    if (percent == 0U) {
        return;
    }
    uint8_t peak = status_led_low_power_ble_peak_percent_locked();
    status_led_set_max(
        &frame->status[STATUS_LED_SEM_BLE],
        status_led_token_relative_to_peak_locked(status_led_rgb(0, 0, 255), percent, peak, false));
}

static uint8_t status_led_step_percent_towards(uint8_t current, uint8_t target, uint8_t max_step)
{
    if (current == target || max_step == 0U) {
        return current;
    }
    if (current < target) {
        uint32_t next = (uint32_t)current + max_step;
        return next >= target ? target : (uint8_t)next;
    }
    uint32_t delta = (uint32_t)current - target;
    return delta <= max_step ? target : (uint8_t)(current - max_step);
}

static void status_led_reset_recording_level_visual_locked(uint32_t now_ms)
{
    s_state.recording_level_visual_percent = 0U;
    s_state.recording_level_visual_updated_ms = now_ms;
}

static uint8_t status_led_recording_level_target_percent_locked(uint32_t now_ms)
{
    if (!s_state.recording_active) {
        return 0U;
    }
    if (s_state.rec_source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
        return 0U;
    }
    bool level_held =
        s_state.recording_level_hold_until_ms != 0U &&
        now_ms < s_state.recording_level_hold_until_ms;
    bool level_recent =
        s_state.recording_level_updated_ms != 0U &&
        now_ms - s_state.recording_level_updated_ms <= STATUS_LED_RECORDING_LEVEL_STALE_MS;
    return (level_held || level_recent) ? s_state.recording_level_percent : 0U;
}

static uint8_t status_led_recording_level_smoothed_percent_locked(uint32_t now_ms)
{
    if (!s_state.recording_active ||
        s_state.rec_source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
        status_led_reset_recording_level_visual_locked(now_ms);
        return 0U;
    }

    uint8_t target = status_led_recording_level_target_percent_locked(now_ms);
    uint8_t current = s_state.recording_level_visual_percent;
    uint32_t last_ms = s_state.recording_level_visual_updated_ms;
    if (last_ms == 0U) {
        s_state.recording_level_visual_percent = target;
        s_state.recording_level_visual_updated_ms = now_ms;
        return target;
    }

    uint32_t elapsed_ms = now_ms - last_ms;
    if (elapsed_ms == 0U) {
        return current;
    }

    uint32_t rate = target > current
        ? STATUS_LED_RECORDING_LEVEL_ATTACK_PERCENT_PER_SEC
        : STATUS_LED_RECORDING_LEVEL_RELEASE_PERCENT_PER_SEC;
    uint32_t max_step = (rate * elapsed_ms + 999U) / 1000U;
    if (max_step == 0U) {
        max_step = 1U;
    } else if (max_step > 100U) {
        max_step = 100U;
    }

    uint8_t next = status_led_step_percent_towards(current, target, (uint8_t)max_step);
    s_state.recording_level_visual_percent = next;
    s_state.recording_level_visual_updated_ms = now_ms;
    return next;
}

static uint8_t status_led_recording_level_effect_percent_locked(uint32_t now_ms)
{
    if (!s_state.recording_active ||
        s_state.rec_source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
        return 0U;
    }

    uint8_t level = status_led_recording_level_smoothed_percent_locked(now_ms);
    uint32_t range =
        STATUS_LED_RECORDING_LEVEL_EFFECT_MAX_PERCENT - STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT;
    uint8_t percent = (uint8_t)(
        STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT +
        ((range * level + 50U) / 100U));
    return status_led_quantize_percent(percent, STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT);
}

static uint8_t status_led_recording_visual_percent_locked(uint32_t now_ms)
{
    return status_led_recording_level_effect_percent_locked(now_ms);
}

static uint8_t status_led_status_tail_desired_for_effect_percent_locked(
    uint8_t desired_percent,
    uint8_t target_effect_percent)
{
    if (desired_percent == 0U || target_effect_percent == 0U) {
        return 0U;
    }
    return target_effect_percent > desired_percent ? desired_percent : target_effect_percent;
}

static uint32_t status_led_processing_thinking_phase_ms_locked(uint32_t now_ms)
{
    uint32_t elapsed_ms = s_state.processing_started_ms != 0U
        ? now_ms - s_state.processing_started_ms
        : now_ms;
    return elapsed_ms % STATUS_LED_PROCESSING_THINK_PERIOD_MS;
}

static uint8_t status_led_processing_thinking_beat_percent(uint32_t phase_ms, uint8_t peak_percent)
{
    if (phase_ms < STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS) {
        return status_led_quantize_percent(
            status_led_lerp_percent(
                STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT,
                peak_percent,
                status_led_smoothstep_per_mille(phase_ms, STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS)),
            STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT);
    }
    phase_ms -= STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS;

    if (phase_ms < STATUS_LED_PROCESSING_THINK_BEAT_HOLD_MS) {
        return peak_percent;
    }
    phase_ms -= STATUS_LED_PROCESSING_THINK_BEAT_HOLD_MS;

    if (phase_ms < STATUS_LED_PROCESSING_THINK_BEAT_FALL_MS) {
        return status_led_quantize_percent(
            status_led_lerp_percent(
                peak_percent,
                STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT,
                status_led_smoothstep_per_mille(phase_ms, STATUS_LED_PROCESSING_THINK_BEAT_FALL_MS)),
            STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT);
    }
    return STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT;
}

static uint8_t status_led_processing_thinking_effect_percent_locked(uint32_t now_ms)
{
    if (!s_state.processing_active) {
        return 0U;
    }

    uint32_t phase_ms = status_led_processing_thinking_phase_ms_locked(now_ms);
    if (STATUS_LED_PROCESSING_THINK_PERIOD_MS == 0U ||
        STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT <= STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT) {
        return STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT;
    }

    const uint32_t beat_ms = STATUS_LED_PROCESSING_THINK_BEAT_RISE_MS +
                             STATUS_LED_PROCESSING_THINK_BEAT_HOLD_MS +
                             STATUS_LED_PROCESSING_THINK_BEAT_FALL_MS;

    if (phase_ms < beat_ms) {
        return status_led_processing_thinking_beat_percent(
            phase_ms,
            STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT);
    }
    phase_ms -= beat_ms;

    if (phase_ms < STATUS_LED_PROCESSING_THINK_GROUP_GAP_MS) {
        return STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT;
    }
    phase_ms -= STATUS_LED_PROCESSING_THINK_GROUP_GAP_MS;

    if (phase_ms < beat_ms) {
        return status_led_processing_thinking_beat_percent(
            phase_ms,
            STATUS_LED_PROCESSING_THINK_EFFECT_BEAT2_PERCENT);
    }
    phase_ms -= beat_ms;

    if (phase_ms < STATUS_LED_PROCESSING_THINK_BEAT_GAP_MS) {
        return STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT;
    }
    phase_ms -= STATUS_LED_PROCESSING_THINK_BEAT_GAP_MS;

    if (phase_ms < beat_ms) {
        return status_led_processing_thinking_beat_percent(
            phase_ms,
            STATUS_LED_PROCESSING_THINK_EFFECT_BEAT3_PERCENT);
    }
    phase_ms -= beat_ms;

    return STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT;
}

static status_led_rgb_t status_led_processing_thinking_color_locked(uint32_t now_ms)
{
    (void)now_ms;
    return status_led_rgb(160, 0, 255);
}

static status_led_rgb_t status_led_key_feedback_color_locked(void)
{
    return status_led_rgb(160, 0, 255);
}

static uint32_t status_led_key_feedback_duration_ms(status_led_key_feedback_t feedback)
{
    return feedback == STATUS_LED_KEY_FEEDBACK_LONG
        ? STATUS_LED_KEY_LONG_GESTURE_FEEDBACK_MS
        : STATUS_LED_KEY_GESTURE_FEEDBACK_MS;
}

static status_led_rgb_t status_led_key_feedback_token_locked(uint8_t percent)
{
    return status_led_token_relative_to_peak_locked(
        status_led_key_feedback_color_locked(),
        percent,
        STATUS_LED_KEY_GESTURE_PERCENT,
        false);
}

static uint8_t status_led_key_physical_percent_locked(uint8_t index, bool pressed, uint32_t now_ms)
{
    if (pressed) {
        return STATUS_LED_KEY_PRESS_PERCENT;
    }
    if (index >= STATUS_LED_KEY_COUNT || now_ms >= s_state.key_until_ms[index]) {
        return 0U;
    }
    uint32_t remaining_ms = s_state.key_until_ms[index] - now_ms;
    uint32_t elapsed_ms = remaining_ms >= STATUS_LED_KEY_FEEDBACK_MS
        ? 0U
        : STATUS_LED_KEY_FEEDBACK_MS - remaining_ms;
    return status_led_decay_percent(
        elapsed_ms,
        STATUS_LED_KEY_FEEDBACK_MS,
        STATUS_LED_KEY_RELEASE_PERCENT,
        0U);
}

static status_led_rgb_t status_led_key_physical_token_locked(uint8_t percent)
{
    return status_led_token_relative_to_peak_locked(
        status_led_rgb(255, 255, 255),
        percent,
        STATUS_LED_KEY_PRESS_PERCENT,
        false);
}

static uint8_t status_led_recording_status_percent_locked(uint32_t now_ms)
{
    uint8_t target_effect = status_led_recording_level_effect_percent_locked(now_ms);
    if (target_effect == 0U) {
        return 0U;
    }
    return status_led_status_tail_desired_for_effect_percent_locked(
        STATUS_LED_ACTIVE_WORK_REC_MAX_PERCENT,
        target_effect);
}

static uint8_t status_led_processing_status_percent_locked(uint32_t now_ms)
{
    if (!s_state.processing_active) {
        return 0U;
    }
    uint8_t target_effect = status_led_processing_thinking_effect_percent_locked(now_ms);
    return status_led_status_tail_desired_for_effect_percent_locked(
        STATUS_LED_PROCESSING_BREATH_MAX_PERCENT,
        target_effect);
}

static uint8_t status_led_ok_visual_percent_locked(uint32_t now_ms)
{
    if (now_ms >= s_state.ok_until_ms || s_state.ok_started_ms == 0) {
        return 0U;
    }
    uint32_t elapsed = now_ms - s_state.ok_started_ms;
    if (elapsed <= STATUS_LED_OK_PEAK_MS) {
        return STATUS_LED_RESULT_PEAK_PERCENT;
    }
    if (elapsed < STATUS_LED_OK_TOTAL_MS) {
        uint32_t remaining = STATUS_LED_OK_TOTAL_MS - elapsed;
        return (uint8_t)(((uint32_t)STATUS_LED_RESULT_PEAK_PERCENT * remaining) /
                         (STATUS_LED_OK_TOTAL_MS - STATUS_LED_OK_PEAK_MS));
    }
    return 0U;
}

static status_led_rgb_t status_led_result_color_locked(void)
{
    return s_state.ok_warning
        ? status_led_rgb(255, 140, 0)
        : status_led_rgb(0, 255, STATUS_LED_OK_SUCCESS_BLUE_BALANCE);
}

static status_led_rgb_t status_led_ota_color(void)
{
    return status_led_rgb(0, 255, 160);
}

static status_led_rgb_t status_led_power_confirm_amber(void)
{
    return status_led_rgb(255, 96, 0);
}

static uint32_t status_led_ota_elapsed_ms_locked(uint32_t now_ms)
{
    if (s_state.ota_started_ms == 0U || now_ms < s_state.ota_started_ms) {
        return 0U;
    }
    return now_ms - s_state.ota_started_ms;
}

static uint8_t status_led_ota_progress_percent_locked(void)
{
    if (s_state.ota_expected_size == 0U) {
        return 0U;
    }
    if (s_state.ota_bytes_written >= s_state.ota_expected_size) {
        return 100U;
    }
    uint64_t scaled = ((uint64_t)s_state.ota_bytes_written * 100U) +
                      ((uint64_t)s_state.ota_expected_size / 2U);
    return (uint8_t)(scaled / (uint64_t)s_state.ota_expected_size);
}

static uint8_t status_led_ota_ok_percent_locked(uint32_t now_ms)
{
    if (!s_state.ota_active) {
        return 0U;
    }
    return status_led_triangle_percent(
        status_led_ota_elapsed_ms_locked(now_ms),
        STATUS_LED_OTA_OK_PULSE_MS,
        STATUS_LED_OTA_OK_MIN_PERCENT,
        STATUS_LED_OTA_OK_MAX_PERCENT);
}

static bool status_led_shutdown_confirm_active_locked(uint32_t now_ms)
{
    if (s_state.shutdown_confirm_started_ms == 0U) {
        return false;
    }
    if (!s_state.shutdown_confirm_final) {
        return true;
    }
    if (now_ms >= s_state.shutdown_confirm_until_ms) {
        return false;
    }
    return true;
}

static bool status_led_error_active_locked(uint32_t now_ms)
{
    return s_state.error_domain != STATUS_LED_ERROR_DOMAIN_NONE && now_ms < s_state.error_until_ms;
}

static uint8_t status_led_recording_accent_percent_locked(
    uint32_t now_ms,
    uint8_t min_percent,
    uint8_t max_percent)
{
    uint8_t percent = min_percent + (uint8_t)(((uint32_t)(max_percent - min_percent) + 1U) / 2U);
    return status_led_scale_effect_percent_locked(
        status_led_quantize_percent(percent, STATUS_LED_ACCENT_BREATHE_QUANTUM_PERCENT),
        now_ms);
}

static uint32_t status_led_effect_elapsed_ms_locked(uint32_t now_ms)
{
    uint32_t started_ms = s_state.last_transition_ms;
    if (started_ms == 0U || now_ms < started_ms) {
        return now_ms;
    }
    return now_ms - started_ms;
}

static uint8_t status_led_effect_entry_scale_percent_locked(uint32_t now_ms)
{
    uint32_t elapsed = status_led_effect_elapsed_ms_locked(now_ms);
    if (elapsed >= STATUS_LED_ACCENT_ENTRY_RAMP_MS) {
        return 100U;
    }
    return (uint8_t)((status_led_smoothstep_per_mille(elapsed, STATUS_LED_ACCENT_ENTRY_RAMP_MS) + 5U) / 10U);
}

static uint8_t status_led_scale_effect_percent_locked(uint8_t percent, uint32_t now_ms)
{
    if (percent == 0U) {
        return 0U;
    }
    uint8_t scale = status_led_effect_entry_scale_percent_locked(now_ms);
    uint32_t scaled = ((uint32_t)percent * scale + 50U) / 100U;
    return status_led_clamp_u32_to_u8(scaled);
}

static void status_led_render_edge_clockwise_chase_locked(
    status_led_frame_t *frame,
    uint32_t elapsed_ms,
    status_led_rgb_t hue,
    uint8_t base_percent,
    uint8_t head_percent,
    uint8_t tail_percent,
    uint8_t fade_percent,
    uint32_t step_ms);

static void status_led_render_ec11_recording_flow_locked(
    status_led_frame_t *frame,
    uint32_t now_ms)
{
    status_led_rgb_t base = status_led_token_locked(
        status_led_rec_gold(),
        status_led_recording_accent_percent_locked(
            now_ms,
            STATUS_LED_EC11_RECORDING_BASE_MIN_PERCENT,
            STATUS_LED_EC11_RECORDING_BASE_MAX_PERCENT),
        false);
    status_led_rgb_t head = status_led_token_locked(
        status_led_rec_gold(),
        status_led_recording_accent_percent_locked(now_ms, 28U, 34U),
        false);
    status_led_rgb_t tail = status_led_scale_raw(head, 68U);
    status_led_rgb_t fade = status_led_scale_raw(head, 38U);
    uint32_t step = (status_led_effect_elapsed_ms_locked(now_ms) / STATUS_LED_EC11_RECORDING_FLOW_STEP_MS) %
                    STATUS_LED_EC11_COUNT;
    uint32_t head_index = (STATUS_LED_EC11_COUNT - 1U - step) % STATUS_LED_EC11_COUNT;

    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        status_led_set_max(&frame->ec11[index], base);
    }
    status_led_set_max(&frame->ec11[head_index], head);
    status_led_set_max(&frame->ec11[(head_index + STATUS_LED_EC11_COUNT - 1U) % STATUS_LED_EC11_COUNT], tail);
    status_led_set_max(&frame->ec11[(head_index + 1U) % STATUS_LED_EC11_COUNT], tail);
    status_led_set_max(&frame->ec11[(head_index + STATUS_LED_EC11_COUNT - 2U) % STATUS_LED_EC11_COUNT], fade);
    status_led_set_max(&frame->ec11[(head_index + 2U) % STATUS_LED_EC11_COUNT], fade);
}

static void status_led_render_edge_recording_flow_locked(
    status_led_frame_t *frame,
    uint32_t now_ms)
{
    uint8_t base_percent = status_led_recording_accent_percent_locked(
        now_ms,
        STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MIN_PERCENT,
        STATUS_LED_EDGE_RECORDING_SURFACE_BASE_MAX_PERCENT);
    status_led_render_edge_clockwise_chase_locked(
        frame,
        status_led_effect_elapsed_ms_locked(now_ms),
        status_led_rec_gold(),
        base_percent,
        status_led_scale_effect_percent_locked(30U, now_ms),
        status_led_scale_effect_percent_locked(20U, now_ms),
        status_led_scale_effect_percent_locked(12U, now_ms),
        STATUS_LED_EDGE_RECORDING_FLOW_STEP_MS);
}

static void status_led_render_recording_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    if (!s_state.recording_active ||
        s_state.rec_source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
        return;
    }
    status_led_rgb_t rec = status_led_token_locked(
        status_led_rec_gold(),
        status_led_recording_status_percent_locked(now_ms),
        false);
    status_led_set_max(&frame->status[STATUS_LED_SEM_REC], rec);
    (void)ret_safety;
}

static void status_led_render_processing_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!s_state.processing_active) {
        return;
    }
    status_led_rgb_t ai = status_led_token_locked(
        status_led_processing_thinking_color_locked(now_ms),
        status_led_processing_status_percent_locked(now_ms),
        false);
    status_led_set_max(&frame->status[STATUS_LED_SEM_AI], ai);
}

static void status_led_render_ota_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!s_state.ota_active) {
        return;
    }
    status_led_rgb_t ok = status_led_token_locked(
        status_led_ota_color(),
        status_led_effect_percent_relative_to_peak(
            status_led_ota_ok_percent_locked(now_ms),
            STATUS_LED_OTA_OK_MAX_PERCENT),
        false);
    status_led_set_max(&frame->status[STATUS_LED_SEM_OK], ok);
}

static void status_led_render_ok_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    uint8_t percent = status_led_ok_visual_percent_locked(now_ms);
    if (percent == 0U) {
        return;
    }
    status_led_rgb_t result = status_led_token_locked(status_led_result_color_locked(), percent, false);
    status_led_set_max(
        &frame->status[s_state.ok_warning ? STATUS_LED_SEM_WARN : STATUS_LED_SEM_OK],
        result);
}

static bool status_led_render_shutdown_confirm_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!status_led_shutdown_confirm_active_locked(now_ms)) {
        return false;
    }

    const uint32_t elapsed = now_ms - s_state.shutdown_confirm_started_ms;
    const bool final = s_state.shutdown_confirm_final;
    status_led_rgb_t amber = status_led_power_confirm_amber();
    uint8_t pwr_percent = final ? STATUS_LED_PWR_CONFIRM_AMBER_PERCENT : STATUS_LED_PWR_CONFIRM_CUE_PERCENT;

    if (final) {
        memset(frame->status, 0, sizeof(frame->status));
        memset(frame->ec11, 0, sizeof(frame->ec11));
        memset(frame->key, 0, sizeof(frame->key));
        memset(frame->edge, 0, sizeof(frame->edge));
        frame->status[STATUS_LED_SEM_PWR] = status_led_token_locked(amber, pwr_percent, false);
        return true;
    }

    frame->status[STATUS_LED_SEM_PWR] = status_led_token_locked(amber, pwr_percent, false);

    uint8_t accent_percent = 24U;
    uint32_t lit = (elapsed * STATUS_LED_EC11_COUNT) / STATUS_LED_SHUTDOWN_CONFIRM_MS;
    if (elapsed > 0U && lit == 0U) {
        lit = 1U;
    }
    if (lit > STATUS_LED_EC11_COUNT) {
        lit = STATUS_LED_EC11_COUNT;
    }
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        if (index < lit) {
            size_t clockwise_index = (STATUS_LED_EC11_COUNT - 1U - index) % STATUS_LED_EC11_COUNT;
            status_led_set_max(
                &frame->ec11[clockwise_index],
                status_led_token_locked(amber, accent_percent, false));
        }
    }

    return true;
}

static void status_led_render_error_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    if (s_state.error_domain == STATUS_LED_ERROR_DOMAIN_NONE || now_ms >= s_state.error_until_ms) {
        return;
    }
    if (!status_led_error_pulse_on_locked(now_ms)) {
        return;
    }

    bool hard = s_state.error_severity == STATUS_LED_ERROR_HARD;
    uint8_t percent = status_led_error_percent_locked(now_ms);
    status_led_rgb_t warn_color = hard
        ? status_led_rgb(255, 0, 0)
        : status_led_rgb(255, 140, 0);
    status_led_rgb_t warn = status_led_token_locked(warn_color, percent, true);

    status_led_set_max(&frame->status[STATUS_LED_SEM_WARN], warn);
    *ret_safety = true;
}

static void status_led_apply_status_tail_guard_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    uint8_t result_percent = status_led_ok_visual_percent_locked(now_ms);
    if (result_percent == 0U &&
        !s_state.ota_active &&
        !status_led_shutdown_confirm_active_locked(now_ms)) {
        frame->status[STATUS_LED_SEM_OK] = (status_led_rgb_t){0};
    }
    if (!status_led_error_active_locked(now_ms) &&
        !(result_percent > 0U && s_state.ok_warning)) {
        frame->status[STATUS_LED_SEM_WARN] = (status_led_rgb_t){0};
    }
}

static void status_led_render_edge_clockwise_chase_locked(
    status_led_frame_t *frame,
    uint32_t elapsed_ms,
    status_led_rgb_t hue,
    uint8_t base_percent,
    uint8_t head_percent,
    uint8_t tail_percent,
    uint8_t fade_percent,
    uint32_t step_ms)
{
    static const uint8_t clockwise_order[STATUS_LED_EDGE_COUNT] = {
        0U, 1U, 2U, 5U, 4U, 3U,
    };
    if (step_ms == 0U) {
        step_ms = 1U;
    }
    uint8_t step = (uint8_t)((elapsed_ms / step_ms) % STATUS_LED_EDGE_COUNT);
    uint8_t head_rank = step;
    status_led_rgb_t base = status_led_token_locked(hue, base_percent, false);
    status_led_rgb_t head = status_led_token_locked(hue, head_percent, false);
    status_led_rgb_t tail = status_led_token_locked(hue, tail_percent, false);
    status_led_rgb_t fade = status_led_token_locked(hue, fade_percent, false);
    for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
        status_led_set_max(&frame->edge[index], base);
    }
    status_led_set_max(&frame->edge[clockwise_order[head_rank]], head);
    status_led_set_max(
        &frame->edge[clockwise_order[(head_rank + STATUS_LED_EDGE_COUNT - 1U) % STATUS_LED_EDGE_COUNT]],
        tail);
    status_led_set_max(
        &frame->edge[clockwise_order[(head_rank + 1U) % STATUS_LED_EDGE_COUNT]],
        tail);
    status_led_set_max(
        &frame->edge[clockwise_order[(head_rank + STATUS_LED_EDGE_COUNT - 2U) % STATUS_LED_EDGE_COUNT]],
        fade);
    status_led_set_max(
        &frame->edge[clockwise_order[(head_rank + 2U) % STATUS_LED_EDGE_COUNT]],
        fade);
    status_led_set_max(
        &frame->edge[clockwise_order[(head_rank + (STATUS_LED_EDGE_COUNT / 2U)) % STATUS_LED_EDGE_COUNT]],
        fade);
}

static bool status_led_ec11_feedback_active_locked(uint32_t now_ms)
{
    return s_state.ec11_feedback_started_ms != 0U && now_ms < s_state.ec11_feedback_until_ms;
}

static bool status_led_ec11_feedback_is_rotation_locked(void)
{
    return s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW ||
           s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CCW;
}

static uint32_t status_led_ec11_feedback_motion_step_locked(uint32_t now_ms)
{
    /* Continuous time-driven orbit: the dot marches around the ring on a wall-
     * clock cadence so the ring reads as always-rotating while the cue is live. */
    uint32_t step = s_state.ec11_feedback_motion_step;
    if (s_state.ec11_feedback_started_ms != 0U &&
        now_ms >= s_state.ec11_feedback_started_ms &&
        STATUS_LED_EC11_ROTATION_STEP_MS != 0U) {
        step += (now_ms - s_state.ec11_feedback_started_ms) /
                STATUS_LED_EC11_ROTATION_STEP_MS;
    }
    return step % STATUS_LED_EC11_COUNT;
}

static uint32_t status_led_ec11_feedback_dot_from_step(
    status_led_ec11_feedback_t feedback,
    uint32_t motion_step)
{
    /* The EC11 strip's physical index order is counterclockwise on the assembled
     * ring, so visual clockwise motion must walk the strip indexes downward. */
    return feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW
        ? (STATUS_LED_EC11_COUNT - 1U - motion_step) % STATUS_LED_EC11_COUNT
        : motion_step % STATUS_LED_EC11_COUNT;
}

static uint32_t status_led_ec11_feedback_step_from_dot(
    status_led_ec11_feedback_t feedback,
    uint32_t dot)
{
    return feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW
        ? (STATUS_LED_EC11_COUNT - 1U - dot) % STATUS_LED_EC11_COUNT
        : dot % STATUS_LED_EC11_COUNT;
}

static uint32_t status_led_ec11_feedback_trail_index(
    status_led_ec11_feedback_t feedback,
    uint32_t dot,
    uint32_t offset)
{
    offset %= STATUS_LED_EC11_COUNT;
    return feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW
        ? (dot + offset) % STATUS_LED_EC11_COUNT
        : (dot + STATUS_LED_EC11_COUNT - offset) % STATUS_LED_EC11_COUNT;
}

static void status_led_render_ec11_repair_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    uint8_t percent = status_led_ble_repair_percent_locked(
        now_ms,
        STATUS_LED_EC11_REPAIR_BLINK_MIN_PERCENT,
        STATUS_LED_EC11_REPAIR_BLINK_MAX_PERCENT);
    status_led_rgb_t color = status_led_token_locked(status_led_rgb(0, 0, 255), percent, false);
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        status_led_set_max(&frame->ec11[index], color);
    }
}

static void status_led_render_ec11_ota_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    status_led_rgb_t hue = status_led_ota_color();
    uint8_t base_percent = status_led_scale_effect_percent_locked(
        STATUS_LED_OTA_EC11_BASE_PERCENT,
        now_ms);
    uint8_t fill_percent = status_led_scale_effect_percent_locked(
        STATUS_LED_OTA_EC11_FILL_PERCENT,
        now_ms);
    uint8_t head_percent = status_led_scale_effect_percent_locked(
        STATUS_LED_OTA_EC11_HEAD_PERCENT,
        now_ms);
    status_led_rgb_t base = status_led_token_locked(hue, base_percent, false);
    status_led_rgb_t fill = status_led_token_locked(hue, fill_percent, false);
    status_led_rgb_t head = status_led_token_locked(hue, head_percent, false);
    status_led_rgb_t tail = status_led_token_locked(
        hue,
        status_led_scale_effect_percent_locked(STATUS_LED_OTA_EC11_TAIL_PERCENT, now_ms),
        false);

    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        status_led_set_max(&frame->ec11[index], base);
    }

    if (s_state.ota_expected_size > 0U) {
        uint32_t lit_ranks =
            ((uint32_t)status_led_ota_progress_percent_locked() * STATUS_LED_EC11_COUNT + 99U) / 100U;
        if (lit_ranks > STATUS_LED_EC11_COUNT) {
            lit_ranks = STATUS_LED_EC11_COUNT;
        }
        for (uint32_t rank = 0; rank < lit_ranks; ++rank) {
            uint32_t index = (STATUS_LED_EC11_COUNT - 1U - rank) % STATUS_LED_EC11_COUNT;
            status_led_set_max(&frame->ec11[index], fill);
        }
    }

    uint32_t step = (status_led_ota_elapsed_ms_locked(now_ms) / STATUS_LED_OTA_EC11_STEP_MS) %
                    STATUS_LED_EC11_COUNT;
    uint32_t dot = (STATUS_LED_EC11_COUNT - 1U - step) % STATUS_LED_EC11_COUNT;
    status_led_set_max(&frame->ec11[dot], head);
    status_led_set_max(&frame->ec11[(dot + 1U) % STATUS_LED_EC11_COUNT], tail);
    status_led_set_max(&frame->ec11[(dot + STATUS_LED_EC11_COUNT - 1U) % STATUS_LED_EC11_COUNT], tail);
}

static bool status_led_render_ec11_feedback_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!status_led_ec11_feedback_active_locked(now_ms)) {
        return false;
    }
    uint32_t elapsed = now_ms - s_state.ec11_feedback_started_ms;

    uint8_t peak = status_led_triangle_percent(elapsed, STATUS_LED_EC11_FEEDBACK_MS, 10U, 30U);
    status_led_rgb_t white = status_led_token_locked(status_led_rgb(255, 255, 255), peak, false);
    if (s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_PRESS) {
        for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
            status_led_set_max(&frame->ec11[index], white);
        }
        return true;
    }

    uint32_t motion_elapsed = s_state.ec11_feedback_last_step_ms == 0U
        ? elapsed
        : now_ms - s_state.ec11_feedback_last_step_ms;
    uint32_t motion_step = status_led_ec11_feedback_motion_step_locked(now_ms);
    uint8_t peak_percent = status_led_decay_percent(
        motion_elapsed,
        STATUS_LED_EC11_ROTATION_HOLD_MS,
        STATUS_LED_EC11_ROTATION_HEAD_START_PERCENT,
        STATUS_LED_EC11_ROTATION_HEAD_END_PERCENT);
    uint8_t base_percent = status_led_decay_percent(
        motion_elapsed,
        STATUS_LED_EC11_ROTATION_HOLD_MS,
        STATUS_LED_EC11_ROTATION_BASE_START_PERCENT,
        STATUS_LED_EC11_ROTATION_BASE_END_PERCENT);
    white = status_led_token_locked(status_led_rgb(255, 255, 255), peak_percent, false);
    status_led_rgb_t base = status_led_token_locked(
        status_led_rgb(255, 255, 255),
        base_percent,
        false);
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        status_led_set_max(&frame->ec11[index], base);
    }

    uint32_t dot = status_led_ec11_feedback_dot_from_step(s_state.ec11_feedback, motion_step);
    /* High-contrast directional comet (bright dot over a dim base) so the chase
     * reads clearly as rotation; trailing tail+fade convey motion direction. */
    status_led_rgb_t tail = status_led_scale_raw(white, 60U);
    status_led_rgb_t fade = status_led_scale_raw(white, 32U);
    status_led_set_max(&frame->ec11[dot], white);
    status_led_set_max(
        &frame->ec11[status_led_ec11_feedback_trail_index(s_state.ec11_feedback, dot, 1U)],
        tail);
    status_led_set_max(
        &frame->ec11[status_led_ec11_feedback_trail_index(s_state.ec11_feedback, dot, 2U)],
        fade);
    return true;
}

static bool status_led_render_ec11_rotation_feedback_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!status_led_ec11_feedback_active_locked(now_ms) ||
        !status_led_ec11_feedback_is_rotation_locked()) {
        return false;
    }
    return status_led_render_ec11_feedback_locked(frame, now_ms);
}

static void status_led_render_ec11_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (status_led_error_active_locked(now_ms)) {
        return;
    }
    if (s_state.preview_suppress_accents) {
        return;
    }

    if (s_state.ota_active) {
        status_led_render_ec11_ota_locked(frame, now_ms);
        return;
    }

    uint8_t rec_percent = status_led_recording_visual_percent_locked(now_ms);
    uint8_t ok_percent = status_led_ok_visual_percent_locked(now_ms);
    if (ok_percent > 0U) {
        uint8_t percent = ok_percent > STATUS_LED_EC11_OK_ACCENT_MAX_PERCENT
            ? STATUS_LED_EC11_OK_ACCENT_MAX_PERCENT
            : ok_percent;
        status_led_rgb_t ok = status_led_token_locked(status_led_result_color_locked(), percent, false);
        for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
            status_led_set_max(&frame->ec11[index], ok);
        }
        return;
    }

    if ((status_led_ble_repair_active_locked(now_ms) ||
         status_led_ble_repair_cue_active_locked(now_ms)) &&
        status_led_ble_repair_cue_active_locked(now_ms)) {
        status_led_render_ec11_repair_locked(frame, now_ms);
        return;
    }

    if (s_state.processing_active && rec_percent > 0U) {
        status_led_render_ec11_recording_flow_locked(frame, now_ms);
        return;
    }

    if (s_state.processing_active) {
        uint8_t percent = STATUS_LED_EC11_PROCESSING_ORBIT_PERCENT;
        if (s_state.processing_started_ms != 0U &&
            now_ms - s_state.processing_started_ms > 10000U &&
            percent > 2U) {
            percent = (uint8_t)(percent - 2U);
        }
        percent = status_led_scale_effect_percent_locked(percent, now_ms);
        uint8_t base_percent = status_led_scale_effect_percent_locked(
            STATUS_LED_EC11_PROCESSING_BASE_PERCENT,
            now_ms);
        status_led_rgb_t base = status_led_token_locked(
            status_led_rgb(160, 0, 255),
            base_percent,
            false);
        for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
            status_led_set_max(&frame->ec11[index], base);
        }
        status_led_rgb_t head = status_led_token_locked(status_led_rgb(160, 0, 255), percent, false);
        status_led_rgb_t tail = status_led_scale_raw(head, 58U);
        status_led_rgb_t fade = status_led_scale_raw(head, 30U);
        uint32_t step = (status_led_effect_elapsed_ms_locked(now_ms) / STATUS_LED_EC11_ORBIT_STEP_MS) %
                        STATUS_LED_EC11_COUNT;
        uint32_t dot = (STATUS_LED_EC11_COUNT - 1U - step) % STATUS_LED_EC11_COUNT;
        status_led_set_max(&frame->ec11[dot], head);
        status_led_set_max(&frame->ec11[(dot + 1U) % STATUS_LED_EC11_COUNT], tail);
        status_led_set_max(&frame->ec11[(dot + STATUS_LED_EC11_COUNT - 1U) % STATUS_LED_EC11_COUNT], tail);
        status_led_set_max(&frame->ec11[(dot + STATUS_LED_EC11_COUNT - 2U) % STATUS_LED_EC11_COUNT], fade);
        status_led_render_ec11_rotation_feedback_locked(frame, now_ms);
        return;
    }

    if (rec_percent > 0U) {
        status_led_render_ec11_recording_flow_locked(frame, now_ms);
        return;
    }

    if (status_led_render_ec11_rotation_feedback_locked(frame, now_ms)) {
        return;
    }

    if (s_state.profile == STATUS_LED_PROFILE_AMBIENT &&
        s_state.ble_state == STATUS_LED_BLE_TYPE_READY) {
        uint8_t percent = status_led_triangle_percent(now_ms, 5200U, 6U, 14U);
        status_led_rgb_t color = status_led_token_locked(status_led_rgb(0, 0, 255), percent, false);
        for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
            status_led_set_max(&frame->ec11[index], color);
        }
    }

    status_led_render_ec11_feedback_locked(frame, now_ms);
}

static void status_led_render_key_active_work_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    (void)frame;
    (void)now_ms;
    return;
}

static bool status_led_render_key_feedback_locked(status_led_frame_t *frame, uint8_t index, uint32_t now_ms)
{
    if (index >= STATUS_LED_KEY_COUNT) {
        return false;
    }
    bool long_hold_active =
        s_state.key_feedback_started_ms[index] != 0U &&
        s_state.key_feedback[index] == STATUS_LED_KEY_FEEDBACK_LONG &&
        (s_state.key_pressed_mask & (1U << index)) != 0U;
    if (now_ms >= s_state.key_feedback_until_ms[index] && !long_hold_active) {
        return false;
    }

    uint32_t elapsed = now_ms - s_state.key_feedback_started_ms[index];
    status_led_rgb_t color = {0};
    switch (s_state.key_feedback[index]) {
    case STATUS_LED_KEY_FEEDBACK_DOUBLE:
        if (elapsed < STATUS_LED_KEY_FLASH_ON_MS ||
            (elapsed >= (STATUS_LED_KEY_FLASH_ON_MS + STATUS_LED_KEY_FLASH_GAP_MS) &&
             elapsed < (STATUS_LED_KEY_FLASH_ON_MS * 2U + STATUS_LED_KEY_FLASH_GAP_MS))) {
            color = status_led_key_feedback_token_locked(STATUS_LED_KEY_GESTURE_PERCENT);
        } else if (elapsed >= (STATUS_LED_KEY_FLASH_ON_MS * 2U + STATUS_LED_KEY_FLASH_GAP_MS) &&
                   elapsed < (STATUS_LED_KEY_FLASH_ON_MS * 2U + STATUS_LED_KEY_FLASH_GAP_MS +
                              STATUS_LED_KEY_FADE_MS)) {
            /* Gradual fade tail after the second flash, matching the original
             * KEY feedback feel while the SPI/DMA transport keeps it stable.
             * The design peak maps to the Type zone cap; the tail stays a
             * true proportion of that peak instead of being frame-normalized. */
            uint32_t fade_elapsed =
                elapsed - (STATUS_LED_KEY_FLASH_ON_MS * 2U + STATUS_LED_KEY_FLASH_GAP_MS);
            uint8_t fade_percent = status_led_decay_percent(
                fade_elapsed,
                STATUS_LED_KEY_FADE_MS,
                STATUS_LED_KEY_GESTURE_PERCENT,
                0U);
            color = status_led_key_feedback_token_locked(fade_percent);
        }
        break;
    case STATUS_LED_KEY_FEEDBACK_LONG:
        if (long_hold_active) {
            /* Steady gesture glow while the key is held. */
            color = status_led_key_feedback_token_locked(STATUS_LED_KEY_GESTURE_PERCENT);
        } else {
            /* Gradual fade tail after release, matching single/double-click.
             * elapsed is measured from the release moment (the release handler
             * re-anchors started_ms when a long-press ends). */
            uint8_t fade_percent = status_led_decay_percent(
                elapsed,
                STATUS_LED_KEY_FADE_MS,
                STATUS_LED_KEY_GESTURE_PERCENT,
                0U);
            color = status_led_key_feedback_token_locked(fade_percent);
        }
        break;
    case STATUS_LED_KEY_FEEDBACK_SINGLE:
    default:
        if (elapsed < STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS) {
            color = status_led_key_physical_token_locked(STATUS_LED_KEY_PRESS_PERCENT);
        } else if (elapsed < (STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS + STATUS_LED_KEY_FLASH_ON_MS)) {
            color = status_led_key_feedback_token_locked(STATUS_LED_KEY_GESTURE_PERCENT);
        } else if (elapsed < (STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS + STATUS_LED_KEY_FLASH_ON_MS + STATUS_LED_KEY_FADE_MS)) {
            /* Gradual fade tail after the visible flash; stability must come
             * from the KEY transport, not by removing the product fade. */
            uint32_t fade_elapsed = elapsed - STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS - STATUS_LED_KEY_FLASH_ON_MS;
            uint8_t fade_percent = status_led_decay_percent(
                fade_elapsed,
                STATUS_LED_KEY_FADE_MS,
                STATUS_LED_KEY_GESTURE_PERCENT,
                0U);
            color = status_led_key_feedback_token_locked(fade_percent);
        } else {
            return false;
        }
        break;
    }

    if (color.r == 0U && color.g == 0U && color.b == 0U) {
        return true;
    }
    status_led_set_max(&frame->key[index], color);
    return true;
}

static void status_led_render_keys_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!s_state.preview_effect_only) {
        for (uint8_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
            bool pressed = (s_state.key_pressed_mask & (1U << index)) != 0;
            bool gesture_active = status_led_render_key_feedback_locked(frame, index, now_ms);
            bool physical_feedback_active = pressed || now_ms < s_state.key_until_ms[index];
            if (!physical_feedback_active && !gesture_active) {
                continue;
            }
            if (physical_feedback_active && !gesture_active) {
                uint8_t physical_percent =
                    status_led_key_physical_percent_locked(index, pressed, now_ms);
                if (physical_percent > 0U) {
                    status_led_rgb_t color = status_led_key_physical_token_locked(physical_percent);
                    status_led_set_max(&frame->key[index], color);
                }
            }
        }
    }
    status_led_render_key_active_work_locked(frame, now_ms);
}

static void status_led_render_edge_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (status_led_error_active_locked(now_ms)) {
        return;
    }
    if (s_state.preview_suppress_accents) {
        return;
    }
    if (!s_state.preview_effect_only &&
        s_state.battery_valid && status_led_battery_display_level_locked() < 20U) {
        return;
    }

    if (s_state.ota_active) {
        status_led_render_edge_clockwise_chase_locked(
            frame,
            status_led_ota_elapsed_ms_locked(now_ms),
            status_led_ota_color(),
            STATUS_LED_OTA_EDGE_BASE_PERCENT,
            STATUS_LED_OTA_EDGE_HEAD_PERCENT,
            STATUS_LED_OTA_EDGE_TAIL_PERCENT,
            STATUS_LED_OTA_EDGE_FADE_PERCENT,
            STATUS_LED_OTA_EDGE_STEP_MS);
        return;
    }

    uint8_t rec_percent = status_led_recording_visual_percent_locked(now_ms);
    uint8_t ok_percent = status_led_ok_visual_percent_locked(now_ms);
    if (ok_percent > 0U) {
        uint8_t percent = ok_percent > STATUS_LED_EDGE_OK_ACCENT_MAX_PERCENT
            ? STATUS_LED_EDGE_OK_ACCENT_MAX_PERCENT
            : ok_percent;
        status_led_rgb_t color = status_led_token_locked(status_led_result_color_locked(), percent, false);
        for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
            status_led_set_max(&frame->edge[index], color);
        }
        return;
    }

    if (s_state.processing_active && rec_percent > 0U) {
        status_led_render_edge_recording_flow_locked(frame, now_ms);
        return;
    }

    if (s_state.processing_active) {
        status_led_render_edge_clockwise_chase_locked(
            frame,
            status_led_effect_elapsed_ms_locked(now_ms),
            status_led_rgb(160, 0, 255),
            STATUS_LED_EDGE_PROCESSING_BASE_PERCENT,
            STATUS_LED_EDGE_PROCESSING_ORBIT_PERCENT,
            13U,
            8U,
            STATUS_LED_EDGE_ORBIT_STEP_MS);
        return;
    }

    if (rec_percent > 0U) {
        status_led_render_edge_recording_flow_locked(frame, now_ms);
        return;
    }

    if (s_state.profile == STATUS_LED_PROFILE_AMBIENT) {
        uint8_t percent = status_led_triangle_percent(now_ms, 4200U, 12U, 24U);
        status_led_rgb_t color = status_led_token_locked(status_led_rgb(0, 0, 255), percent, false);
        for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
            status_led_set_max(&frame->edge[index], color);
        }
    }
}

static void status_led_render_frame_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    memset(frame, 0, sizeof(*frame));
    bool safety = false;

    if (s_state.output_disabled) {
        s_state.last_estimated_current_ma = 0;
        s_state.last_current_budget_ma = 0;
        s_state.last_budget_scale_percent = 0U;
        s_state.current_limited_by_budget = false;
        return;
    }
    if (s_state.low_power_disabled) {
        if (now_ms >= s_state.error_until_ms) {
            s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
        }
        status_led_render_low_power_power_locked(frame, now_ms, &safety);
        status_led_render_low_power_ble_locked(frame, now_ms);
        status_led_apply_zone_brightness_caps_locked(frame);
        status_led_clamp_current_locked(frame, safety);
        return;
    }

    if (s_state.test_mode != STATUS_LED_TEST_NONE) {
        const bool test_uses_zone_caps =
            status_led_test_mode_uses_zone_brightness_caps(s_state.test_mode);
        status_led_render_test_locked(frame, now_ms);
        if (test_uses_zone_caps) {
            status_led_apply_zone_brightness_caps_locked(frame);
            status_led_clamp_current_locked(frame, false);
        } else {
            status_led_clamp_current_locked(frame, true);
        }
        return;
    }

    if (now_ms >= s_state.error_until_ms) {
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
    }

    status_led_render_power_locked(frame, now_ms, &safety);
    status_led_render_ble_locked(frame, now_ms);
    if (status_led_render_shutdown_confirm_locked(frame, now_ms)) {
        status_led_apply_status_tail_guard_locked(frame, now_ms);
        status_led_apply_zone_brightness_caps_locked(frame);
        status_led_clamp_current_locked(frame, safety);
        return;
    }
    status_led_render_recording_locked(frame, now_ms, &safety);
    status_led_render_processing_locked(frame, now_ms);
    status_led_render_ota_locked(frame, now_ms);
    status_led_render_ec11_locked(frame, now_ms);
    status_led_render_keys_locked(frame, now_ms);
    status_led_render_edge_locked(frame, now_ms);
    status_led_render_ok_locked(frame, now_ms);
    status_led_render_error_locked(frame, now_ms, &safety);
    status_led_apply_status_tail_guard_locked(frame, now_ms);

    status_led_apply_zone_brightness_caps_locked(frame);
    status_led_clamp_current_locked(frame, safety);
}

static uint32_t status_led_refresh_once(void)
{
    status_led_frame_t frame;
    uint32_t now_ms = status_led_now_ms();
    uint8_t changed_strip_mask = 0;
    uint8_t tx_strip_mask = 0;
    bool force_clear_tx = false;
    uint8_t transition_clear_mask = 0U;
    bool low_power_active = false;
    bool shutdown_final_active = false;
    bool shutdown_final_all_zone_latch_needed = false;
    bool pwr_only_final_latch = false;
    bool key_dark_latch_pending_tx = false;
    uint32_t delay_ms = STATUS_LED_IDLE_REFRESH_MS;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return STATUS_LED_REFRESH_MS;
    }
    if (status_led_render_transition_clear_locked(&frame, &transition_clear_mask)) {
        force_clear_tx = true;
        delay_ms = STATUS_LED_IDLE_TRANSITION_CLEAR_MS;
    } else {
        status_led_render_frame_locked(&frame, now_ms);
        delay_ms = status_led_refresh_delay_ms_locked(now_ms);
    }
    changed_strip_mask = status_led_frame_changed_strip_mask(&s_state.last_frame, &frame);
    low_power_active = s_state.output_disabled || s_state.low_power_disabled;
    shutdown_final_active = !force_clear_tx &&
        s_state.shutdown_confirm_final &&
        status_led_shutdown_confirm_active_locked(now_ms);
    if (shutdown_final_active &&
        s_state.shutdown_final_all_zone_latched_started_ms != s_state.shutdown_confirm_started_ms) {
        shutdown_final_all_zone_latch_needed = true;
        s_state.shutdown_final_all_zone_latched_started_ms = s_state.shutdown_confirm_started_ms;
    }
    pwr_only_final_latch = low_power_active || shutdown_final_active;
    key_dark_latch_pending_tx = !force_clear_tx &&
        s_state.key_dark_latch_pending_mask != 0U &&
        s_state.key_pressed_mask == 0U &&
        !status_led_strip_has_light(frame.key, STATUS_LED_KEY_COUNT);
    tx_strip_mask = force_clear_tx
        ? status_led_transition_clear_strip_mask_locked(&s_state.last_frame, transition_clear_mask)
        : changed_strip_mask;
    if (!force_clear_tx && shutdown_final_all_zone_latch_needed) {
        tx_strip_mask = STATUS_LED_STRIP_MASK_ALL;
    }
    if (!force_clear_tx &&
        status_led_status_health_rewrite_needed(&frame, changed_strip_mask, low_power_active, now_ms)) {
        tx_strip_mask = (uint8_t)(tx_strip_mask | STATUS_LED_STRIP_MASK_STATUS);
    }
    if (!force_clear_tx &&
        status_led_key_dark_rewrite_needed(&frame, changed_strip_mask, low_power_active, now_ms)) {
        tx_strip_mask = (uint8_t)(tx_strip_mask | STATUS_LED_STRIP_MASK_KEY);
    }
    if (key_dark_latch_pending_tx) {
        tx_strip_mask = (uint8_t)(tx_strip_mask | STATUS_LED_STRIP_MASK_KEY);
    }
    if (!pwr_only_final_latch) {
        tx_strip_mask = (uint8_t)(tx_strip_mask | status_led_suspended_strip_mask());
    }
    tx_strip_mask = (uint8_t)(tx_strip_mask | s_state.retry_strip_mask);
    status_led_copy_frame_locked(&frame);
    status_led_log_visual_state_locked(&frame, now_ms);
    xSemaphoreGive(s_mutex);

    if (tx_strip_mask != 0U) {
        uint8_t final_failed_strip_mask = 0U;
        uint8_t write_count = force_clear_tx
            ? STATUS_LED_TRANSITION_CLEAR_WRITES
            : (pwr_only_final_latch ? STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES : 1U);
        for (uint8_t write_index = 0U; write_index < write_count; ++write_index) {
            /* Entering quiet low-power/final latch still uses the non-DMA path.
             * Interactive resume transition clears must keep EC11/KEY on their
             * SPI DMA transports so dynamic feedback does not regress into the
             * old interrupt-backed RMT flicker. */
            bool force_non_dma = pwr_only_final_latch;
            uint8_t failed_strip_mask =
                status_led_transmit_changed_frame(
                    &frame,
                    tx_strip_mask,
                    force_non_dma,
                    key_dark_latch_pending_tx);
            final_failed_strip_mask = (uint8_t)(final_failed_strip_mask | failed_strip_mask);
            status_led_update_retry_strip_mask(tx_strip_mask, failed_strip_mask);
        }
        if (key_dark_latch_pending_tx &&
            (final_failed_strip_mask & STATUS_LED_STRIP_MASK_KEY) == 0U &&
            xSemaphoreTake(s_mutex, pdMS_TO_TICKS(STATUS_LED_TX_MUTEX_WAIT_MS)) == pdTRUE) {
            if (s_state.key_pressed_mask == 0U &&
                !status_led_strip_has_light(s_state.last_frame.key, STATUS_LED_KEY_COUNT)) {
                s_state.key_dark_latch_pending_mask = 0U;
            }
            xSemaphoreGive(s_mutex);
        }
    }
    now_ms = status_led_now_ms();
    if (!force_clear_tx) {
        status_led_suspend_quiet_idle_transports(low_power_active, now_ms);
        if (low_power_active && status_led_idle_transport_release_pending() &&
            delay_ms > STATUS_LED_RMT_IDLE_RELEASE_MS) {
            delay_ms = STATUS_LED_RMT_IDLE_RELEASE_MS;
        }
    }
    return delay_ms;
}

static void status_led_set_last_reason_locked(const char *reason)
{
    snprintf(s_state.last_reason, sizeof(s_state.last_reason), "%s", reason != NULL ? reason : "none");
}

static void status_led_clear_ok_locked(void)
{
    s_state.ok_started_ms = 0;
    s_state.ok_until_ms = 0;
    s_state.ok_warning = false;
}

static void status_led_clear_ota_locked(void)
{
    s_state.ota_active = false;
    s_state.ota_started_ms = 0U;
    s_state.ota_bytes_written = 0U;
    s_state.ota_expected_size = 0U;
}

static void status_led_clear_ec11_feedback_locked(void)
{
    s_state.ec11_feedback_started_ms = 0U;
    s_state.ec11_feedback_until_ms = 0U;
    s_state.ec11_feedback_last_step_ms = 0U;
    s_state.ec11_feedback_motion_step = 0U;
    s_state.ec11_feedback_last_diag_ms = 0U;
    s_state.ec11_feedback = STATUS_LED_EC11_FEEDBACK_PRESS;
}

static bool status_led_clear_ec11_press_feedback_locked(void)
{
    if (s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_PRESS &&
        s_state.ec11_feedback_started_ms != 0U) {
        status_led_clear_ec11_feedback_locked();
        return true;
    }
    return false;
}

static void status_led_resume_interactive_output_locked(void)
{
    bool was_low_power_output = s_state.output_disabled || s_state.low_power_disabled;
    s_state.preview_suppress_accents = false;
    s_state.output_disabled = false;
    s_state.low_power_disabled = false;
    if (was_low_power_output) {
        s_state.last_power_poll_ms = 0U;
        status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
    } else if (s_state.transition_clear_mask != 0U) {
        s_state.last_power_poll_ms = 0U;
        status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS);
    }
}

static void status_led_clear_key_feedback_locked(void)
{
    s_state.key_pressed_mask = 0U;
    memset(s_state.key_until_ms, 0, sizeof(s_state.key_until_ms));
    memset(s_state.key_feedback_started_ms, 0, sizeof(s_state.key_feedback_started_ms));
    memset(s_state.key_feedback_until_ms, 0, sizeof(s_state.key_feedback_until_ms));
    memset(s_state.key_feedback, 0, sizeof(s_state.key_feedback));
    s_state.key_dark_latch_pending_mask =
        (uint8_t)((1U << STATUS_LED_KEY_COUNT) - 1U);
}

static bool status_led_clear_retryable_error_locked(status_led_error_domain_t domain)
{
    if (s_state.error_domain == domain &&
        s_state.error_severity == STATUS_LED_ERROR_RETRYABLE) {
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
        s_state.error_until_ms = 0;
        return true;
    }
    return false;
}

static uint32_t status_led_repair_hold_ms(uint32_t requested_ms)
{
    uint32_t hold_ms = requested_ms;
    const uint32_t cue_total_ms =
        STATUS_LED_BLE_REPAIR_CUE_LEAD_CLEAR_MS + STATUS_LED_BLE_REPAIR_CUE_MS;
    if (hold_ms < cue_total_ms) {
        hold_ms = cue_total_ms;
    }
    if (hold_ms > STATUS_LED_BLE_REPAIR_WINDOW_MAX_MS) {
        hold_ms = STATUS_LED_BLE_REPAIR_WINDOW_MAX_MS;
    }
    return hold_ms;
}

static void status_led_start_ble_repair_locked_for_ms(uint32_t now_ms, uint32_t hold_ms)
{
    hold_ms = status_led_repair_hold_ms(hold_ms);
    const bool cue_already_active = status_led_ble_repair_cue_active_locked(now_ms);
    const uint32_t repair_until_ms = now_ms + hold_ms;
    s_state.ble_state = STATUS_LED_BLE_REPAIRING;
    s_state.ble_transition_ms = now_ms;
    s_state.ble_confidence_until_ms = repair_until_ms;
    s_state.ble_repair_until_ms = repair_until_ms;
    if (!cue_already_active) {
        s_state.ble_repair_cue_started_ms = now_ms + STATUS_LED_BLE_REPAIR_CUE_LEAD_CLEAR_MS;
        s_state.ble_repair_cue_until_ms = s_state.ble_repair_cue_started_ms + STATUS_LED_BLE_REPAIR_CUE_MS;
    }
    s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
    s_state.recording_active = false;
    s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
    s_state.recording_level_percent = 0U;
    s_state.recording_level_updated_ms = 0U;
    s_state.recording_level_hold_until_ms = 0U;
    status_led_reset_recording_level_visual_locked(0U);
    s_state.processing_active = false;
    s_state.processing_started_ms = 0U;
    status_led_clear_ota_locked();
    status_led_clear_ok_locked();
    status_led_clear_ec11_feedback_locked();
    status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_BLE);
}

static void status_led_start_ble_repair_locked(uint32_t now_ms)
{
    status_led_start_ble_repair_locked_for_ms(now_ms, STATUS_LED_BLE_REPAIR_CUE_MS);
}

static void status_led_preview_clear_activity_locked(void)
{
    s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
    s_state.ble_transition_ms = 0U;
    s_state.ble_confidence_until_ms = 0U;
    s_state.ble_repair_until_ms = 0U;
    s_state.ble_repair_cue_started_ms = 0U;
    s_state.ble_repair_cue_until_ms = 0U;
    s_state.preview_ble_override_until_ms = 0U;
    s_state.oobe_confidence_until_ms = 0U;
    s_state.recording_active = false;
    s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
    s_state.recording_level_percent = 0U;
    s_state.recording_level_updated_ms = 0U;
    s_state.recording_level_hold_until_ms = 0U;
    status_led_reset_recording_level_visual_locked(0U);
    s_state.processing_active = false;
    s_state.processing_started_ms = 0U;
    status_led_clear_ota_locked();
    s_state.battery_valid = false;
    s_state.battery_level_percent = 0U;
    s_state.battery_mv = 0U;
    s_state.external_power_present = false;
    s_state.external_power_source_flags = 0U;
    s_state.charging = false;
    s_state.full = false;
    s_state.raw_charging = false;
    s_state.raw_full = false;
    s_state.charge_full_latched = false;
    s_state.charger_status_external_until_ms = 0U;
    s_state.charge_full_candidate_since_ms = 0U;
    s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
    s_state.error_until_ms = 0U;
    status_led_clear_key_feedback_locked();
    status_led_clear_ok_locked();
    status_led_clear_ec11_feedback_locked();
    s_state.shutdown_confirm_started_ms = 0U;
    s_state.shutdown_confirm_until_ms = 0U;
    s_state.shutdown_confirm_final = false;
    s_state.shutdown_final_all_zone_latched_started_ms = 0U;
}

static void status_led_preview_ready_baseline_locked(uint32_t now_ms)
{
    s_state.ble_state = STATUS_LED_BLE_TYPE_READY;
    s_state.ble_transition_ms = now_ms;
    s_state.ble_confidence_until_ms = 0;
    s_state.ble_repair_until_ms = 0U;
    s_state.ble_repair_cue_started_ms = 0U;
    s_state.ble_repair_cue_until_ms = 0U;
    if (!s_state.external_power_present && !s_state.battery_valid) {
        s_state.battery_valid = true;
        s_state.battery_level_percent = 80;
        s_state.battery_mv = 4000;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    }
}

static void status_led_preview_effect_only_baseline_locked(void)
{
    s_state.preview_effect_only = true;
    s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
    s_state.ble_transition_ms = 0;
    s_state.ble_confidence_until_ms = 0;
    s_state.ble_repair_until_ms = 0;
    s_state.ble_repair_cue_started_ms = 0U;
    s_state.ble_repair_cue_until_ms = 0U;
    s_state.preview_ble_override_until_ms = 0;
    s_state.oobe_confidence_until_ms = 0;
    s_state.recording_active = false;
    s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
    s_state.recording_level_percent = 0U;
    s_state.recording_level_updated_ms = 0U;
    s_state.recording_level_hold_until_ms = 0U;
    status_led_reset_recording_level_visual_locked(0U);
    s_state.processing_active = false;
    s_state.processing_started_ms = 0U;
    status_led_clear_ota_locked();
    s_state.battery_valid = false;
    s_state.battery_level_percent = 0;
    s_state.battery_mv = 0;
    s_state.external_power_present = false;
    s_state.external_power_source_flags = 0;
    s_state.charging = false;
    s_state.full = false;
    s_state.raw_charging = false;
    s_state.raw_full = false;
    s_state.charge_full_latched = false;
    s_state.charge_full_candidate_since_ms = 0;
    s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
    s_state.error_until_ms = 0;
    s_state.ok_started_ms = 0;
    s_state.ok_until_ms = 0;
    s_state.ok_warning = false;
    status_led_clear_ec11_feedback_locked();
    s_state.shutdown_confirm_started_ms = 0;
    s_state.shutdown_confirm_until_ms = 0;
    s_state.shutdown_confirm_final = false;
    s_state.shutdown_final_all_zone_latched_started_ms = 0U;
    status_led_clear_key_feedback_locked();
}

static void status_led_resume_output_locked(void)
{
    s_state.preview_suppress_accents = false;
    if (!s_state.low_power_disabled) {
        s_state.output_disabled = false;
    }
}

static void status_led_force_manual_off(void)
{
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.output_disabled = true;
        s_state.low_power_disabled = false;
        s_state.test_mode = STATUS_LED_TEST_NONE;
        status_led_clear_key_feedback_locked();
        status_led_clear_ota_locked();
        s_state.ok_until_ms = 0;
        s_state.ok_warning = false;
        status_led_clear_ec11_feedback_locked();
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
        s_state.error_until_ms = 0;
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        s_state.transition_clear_mask = 0U;
        s_state.status_window_until_ms = 0;
        s_state.ble_confidence_until_ms = 0;
        s_state.ble_repair_until_ms = 0;
        s_state.ble_repair_cue_started_ms = 0U;
        s_state.ble_repair_cue_until_ms = 0U;
        s_state.oobe_confidence_until_ms = 0;
        s_state.last_transition_ms = status_led_now_ms();
        status_led_set_last_reason_locked("manual_off");
        status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
        status_led_log_output_state_locked(0);
        xSemaphoreGive(s_mutex);
    }
    status_led_request_refresh();
    power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false);
}

static bool status_led_apply_device_settings_snapshot_locked(
    const device_settings_snapshot_t *settings,
    bool external_power_present)
{
    if (settings == NULL) {
        return false;
    }
    (void)external_power_present;
    const uint8_t neutral_brightness = STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    const bool changed =
        s_state.brightness_percent != neutral_brightness ||
        s_state.status_zone_brightness_percent != settings->status_led_brightness_percent ||
        s_state.key_zone_brightness_percent != settings->key_led_brightness_percent ||
        s_state.ec11_zone_brightness_percent != settings->ec11_led_brightness_percent ||
        s_state.edge_zone_brightness_percent != settings->edge_led_brightness_percent;
    s_state.brightness_percent = neutral_brightness;
    s_state.status_zone_brightness_percent = settings->status_led_brightness_percent;
    s_state.key_zone_brightness_percent = settings->key_led_brightness_percent;
    s_state.ec11_zone_brightness_percent = settings->ec11_led_brightness_percent;
    s_state.edge_zone_brightness_percent = settings->edge_led_brightness_percent;
    return changed;
}

static void status_led_poll_power_inputs(void)
{
    uint32_t now_ms = status_led_now_ms();
    bool should_poll = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool preview_window_active = now_ms < s_state.status_window_until_ms &&
                                     strcmp(s_state.last_reason, "preview") == 0;
        uint32_t poll_ms = status_led_power_poll_interval_ms_locked();
        should_poll = !preview_window_active &&
                      (s_state.last_power_poll_ms == 0 ||
                       now_ms - s_state.last_power_poll_ms >= poll_ms);
        if (should_poll) {
            s_state.last_power_poll_ms = now_ms;
        }
        xSemaphoreGive(s_mutex);
    }
    if (!should_poll) {
        return;
    }

    battery_monitor_status_t battery = {0};
    esp_err_t battery_ret = battery_monitor_read(&battery);
    bool battery_valid = battery_ret == ESP_OK && battery.valid;
    uint32_t battery_mv = battery_valid ? battery.voltage_mv : 0;
    uint8_t battery_level = battery_valid ? battery.level_percent : 0xFF;
    board_v2_power_input_snapshot_t power_input = {0};
    board_get_v2_power_input_snapshot(&power_input);
    bool usb_serial_jtag_sof_active = power_input.usb_serial_jtag_sof_active;
    bool usb_power_present = power_input.usb_power_present;
    bool raw_charging = power_input.bat_chg_level == 0;
    bool raw_full = power_input.bat_std_level == 0;
    bool battery_allows_full = !battery_valid ||
                               battery_mv >= STATUS_LED_CHARGE_FULL_MIN_MV ||
                               battery_level >= STATUS_LED_CHARGE_FULL_MIN_PERCENT;
    bool raw_full_external = raw_full && battery_allows_full;
    uint32_t external_power_source_flags = 0;
    if (usb_power_present && usb_serial_jtag_sof_active) {
        external_power_source_flags |= STATUS_LED_POWER_SOURCE_USB_SERIAL_JTAG;
    }
    bool external_power_present = usb_power_present;
    device_settings_snapshot_t device_settings = {0};

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool charger_status_external =
            status_led_charger_status_external_locked(
                raw_charging,
                raw_full_external,
                now_ms);
        if (charger_status_external) {
            external_power_source_flags |= STATUS_LED_POWER_SOURCE_CHARGER_STATUS;
            external_power_present = true;
        }
        device_settings_get_snapshot(&device_settings);
        bool brightness_settings_changed =
            status_led_apply_device_settings_snapshot_locked(&device_settings, external_power_present);
        if (!external_power_present) {
            s_state.charge_full_latched = false;
            s_state.charge_full_candidate_since_ms = 0;
        } else if (!s_state.charge_full_latched) {
            bool full_candidate = raw_full && !raw_charging && battery_allows_full;
            if (full_candidate) {
                if (s_state.charge_full_candidate_since_ms == 0) {
                    s_state.charge_full_candidate_since_ms = now_ms;
                } else if (now_ms - s_state.charge_full_candidate_since_ms >=
                           STATUS_LED_CHARGE_FULL_DEBOUNCE_MS) {
                    s_state.charge_full_latched = true;
                }
            } else {
                s_state.charge_full_candidate_since_ms = 0;
            }
        }
        bool full = external_power_present && s_state.charge_full_latched;
        bool charging = external_power_present && !full;
        bool battery_band_changed =
            s_state.battery_valid != battery_valid ||
            (battery_valid && (s_state.battery_level_percent / 10U) != (battery_level / 10U));
        bool previous_display_valid = s_state.battery_display_valid;
        uint8_t previous_display_level = s_state.battery_display_level_percent;
        bool battery_display_changed = status_led_update_battery_display_locked(
            external_power_present,
            battery_valid,
            battery_mv,
            battery_level);
        bool battery_display_band_changed =
            previous_display_valid != s_state.battery_display_valid ||
            (s_state.battery_display_valid &&
             (previous_display_level / 10U) !=
                 (s_state.battery_display_level_percent / 10U));
        bool power_visual_changed =
            s_state.external_power_present != external_power_present ||
            s_state.external_power_source_flags != external_power_source_flags ||
            s_state.charging != charging ||
            s_state.full != full ||
            brightness_settings_changed ||
            (!external_power_present && battery_display_band_changed);
        bool power_input_log_changed =
            power_visual_changed ||
            (!external_power_present && (battery_band_changed || battery_display_changed)) ||
            s_state.battery_display_rise_suppressed;
        s_state.battery_valid = battery_valid;
        s_state.battery_mv = battery_mv;
        s_state.battery_level_percent = battery_level;
        s_state.external_power_present = external_power_present;
        s_state.external_power_source_flags = external_power_source_flags;
        s_state.charging = charging;
        s_state.full = full;
        s_state.raw_charging = raw_charging;
        s_state.raw_full = raw_full;
        if (power_visual_changed && !s_state.preview_effect_only) {
            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
            s_state.last_transition_ms = now_ms;
            status_led_set_last_reason_locked("power_change");
        }
        if (power_input_log_changed) {
            status_led_log_power_input_locked();
        }
        xSemaphoreGive(s_mutex);
    }
}

void status_led_apply_device_settings(void)
{
    bool changed = false;
    if (s_mutex == NULL) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        device_settings_snapshot_t device_settings = {0};
        device_settings_get_snapshot(&device_settings);
        (void)status_led_apply_device_settings_snapshot_locked(
            &device_settings,
            s_state.external_power_present);
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked("device_settings");
        status_led_log_power_input_locked();
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

static void status_led_task(void *parameter)
{
    (void)parameter;
    ESP_LOGI(TAG,
             "status LED task running: priority=%u core=%d configured_core=%d",
             (unsigned)uxTaskPriorityGet(NULL),
             (int)xPortGetCoreID(),
             (int)STATUS_LED_TASK_CORE_ID);
    while (1) {
        status_led_poll_power_inputs();
        uint32_t delay_ms = status_led_refresh_once();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}

static esp_err_t status_led_init_strip_backend(status_led_strip_t *strip)
{
    status_led_strip_backend_config_t config = {
        .name = strip->name,
        .gpio = strip->gpio,
        .led_count = strip->led_count,
        .tail_guard_pixels = strip->tail_guard_pixels,
        .dark_latch_rmt_writes = strip->dark_latch_rmt_writes,
        .color_order = strip->color_order,
        .prefer_dma = strip->prefer_dma,
        .transport = strip->transport,
        .spi_host = strip->spi_host,
    };
    return status_led_strip_backend_new(&config, &strip->backend);
}

static bool status_led_gpio_is_valid(gpio_num_t gpio)
{
    return gpio != GPIO_NUM_NC && gpio >= 0 && gpio < GPIO_NUM_MAX;
}

static uint64_t status_led_gpio_mask(gpio_num_t gpio)
{
    return status_led_gpio_is_valid(gpio) ? (1ULL << (uint32_t)gpio) : 0ULL;
}

static void status_led_configure_power_inputs(void)
{
    uint64_t charge_mask = 0;
    charge_mask |= status_led_gpio_mask(BOARD_PINS_BAT_CHG_IO);
    charge_mask |= status_led_gpio_mask(BOARD_PINS_BAT_STD_IO);
    if (charge_mask != 0) {
        gpio_config_t charge_config = {
            .pin_bit_mask = charge_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&charge_config);
    }

    gpio_num_t usb_det_gpio = BOARD_PINS_USB_DET_IO;
    if (status_led_gpio_is_valid(usb_det_gpio)) {
        gpio_config_t usb_config = {
            .pin_bit_mask = status_led_gpio_mask(usb_det_gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&usb_config);
    }
}

static status_led_profile_t status_led_profile_from_u8(uint8_t value)
{
    if (value <= STATUS_LED_PROFILE_FACTORY) {
        return (status_led_profile_t)value;
    }
    return STATUS_LED_PROFILE_STANDARD;
}

static uint8_t status_led_brightness_from_u8(uint8_t value)
{
    return value <= 100U ? value : 100U;
}

static void status_led_load_persistent_config(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(STATUS_LED_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        return;
    }
    uint8_t profile = (uint8_t)STATUS_LED_PROFILE_STANDARD;
    if (nvs_get_u8(nvs, STATUS_LED_NVS_PROFILE_KEY, &profile) == ESP_OK) {
        s_state.profile = status_led_profile_from_u8(profile);
    }
    uint8_t brightness = 100U;
    if (nvs_get_u8(nvs, STATUS_LED_NVS_BRIGHTNESS_KEY, &brightness) == ESP_OK) {
        ESP_LOGW(TAG, "ignoring legacy global LED brightness=%u; per-zone brightness is authoritative",
                 (unsigned)status_led_brightness_from_u8(brightness));
        s_state.brightness_percent = STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    }
    uint8_t order = 0;
    if (nvs_get_u8(nvs, STATUS_LED_NVS_STATUS_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_STATUS].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    if (nvs_get_u8(nvs, STATUS_LED_NVS_EC11_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_EC11].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    if (nvs_get_u8(nvs, STATUS_LED_NVS_KEY_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_KEY].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    if (nvs_get_u8(nvs, STATUS_LED_NVS_EDGE_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_EDGE].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    nvs_close(nvs);
}

static void status_led_save_profile(status_led_profile_t profile)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(STATUS_LED_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "profile persist skipped: %s", esp_err_to_name(ret));
        return;
    }
    ret = nvs_set_u8(nvs, STATUS_LED_NVS_PROFILE_KEY, (uint8_t)profile);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "profile persist failed: %s", esp_err_to_name(ret));
    }
}

static void status_led_force_all_off(bool force_non_dma)
{
    status_led_frame_t frame = {0};
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_copy_frame_locked(&frame);
        xSemaphoreGive(s_mutex);
    }
    for (uint8_t write_index = 0U;
         write_index < STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES;
        ++write_index) {
        uint8_t failed_strip_mask =
            status_led_transmit_changed_frame(&frame, STATUS_LED_STRIP_MASK_ALL, force_non_dma, false);
        status_led_update_retry_strip_mask(STATUS_LED_STRIP_MASK_ALL, failed_strip_mask);
    }
}

static void status_led_suspend_all_strips(void)
{
    for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
        (void)status_led_strip_backend_suspend(s_strips[index].backend);
        s_strip_transport_suspended[index] = true;
    }
}

static status_led_rgb_t status_led_boot_power_color_locked(void)
{
    return status_led_token_locked(
        status_led_power_confirm_amber(),
        STATUS_LED_BOOT_PWR_AMBER_PERCENT,
        false);
}

static bool status_led_reason_is_boot_feedback(const char *reason)
{
    return reason != NULL &&
           (strcmp(reason, "boot") == 0 || strcmp(reason, "booting") == 0);
}

static void status_led_mark_boot_feedback_locked(uint32_t now_ms)
{
    uint32_t until_ms = now_ms + STATUS_LED_BOOT_BLE_READY_WAIT_MS;
    if (s_state.boot_feedback_until_ms < until_ms) {
        s_state.boot_feedback_until_ms = until_ms;
    }
}

static bool status_led_ble_state_completes_boot_feedback(status_led_ble_state_t state)
{
    return state == STATUS_LED_BLE_PAIRING ||
           state == STATUS_LED_BLE_REPAIRING ||
           state == STATUS_LED_BLE_RECONNECTING ||
           state == STATUS_LED_BLE_CONNECTED ||
           state == STATUS_LED_BLE_TYPE_READY;
}

static void status_led_release_boot_feedback_for_ble_locked(uint32_t now_ms, status_led_ble_state_t state)
{
    if (status_led_ble_state_completes_boot_feedback(state) &&
        now_ms < s_state.boot_feedback_until_ms) {
        s_state.boot_feedback_until_ms = now_ms;
    }
}

static void status_led_force_boot_feedback(void)
{
    status_led_frame_t frame = {0};
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_mark_boot_feedback_locked(now_ms);
        frame.status[STATUS_LED_SEM_PWR] = status_led_boot_power_color_locked();
        status_led_apply_zone_brightness_caps_locked(&frame);
        status_led_copy_frame_locked(&frame);
        xSemaphoreGive(s_mutex);
        status_led_transmit_frame(&frame);
    }
}

esp_err_t status_led_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_state.initialized) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.profile = STATUS_LED_PROFILE_STANDARD;
    s_state.brightness_percent = 100U;
    s_state.status_zone_brightness_percent = DEVICE_SETTINGS_DEFAULT_STATUS_LED_BRIGHTNESS_PERCENT;
    s_state.key_zone_brightness_percent = DEVICE_SETTINGS_DEFAULT_KEY_LED_BRIGHTNESS_PERCENT;
    s_state.ec11_zone_brightness_percent = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    s_state.edge_zone_brightness_percent = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
    s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
    s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
    s_state.vdd_led_enable_assumed = true;
    uint32_t now_ms = status_led_now_ms();
    s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
    s_state.boot_feedback_until_ms = now_ms + STATUS_LED_BOOT_BLE_READY_WAIT_MS;
    status_led_set_last_reason_locked("boot");

    status_led_load_persistent_config();
    status_led_configure_power_inputs();

    esp_err_t final_ret = ESP_OK;
    for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
        esp_err_t ret = status_led_init_strip_backend(&s_strips[index]);
        if (ret != ESP_OK && final_ret == ESP_OK) {
            final_ret = ret;
        }
    }

    s_state.initialized = true;
    ESP_LOGI(
        TAG,
        "status LED init: profile=%s status_gpio=%d ec11_gpio=%d key_gpio=%d edge_gpio=%d key_pin_contract=GPIO13 ec11_pin_contract=GPIO5 vdd_led_enable=always_on_assumed",
        status_led_profile_name(s_state.profile),
        (int)BOARD_PINS_RGB_STATUS_IO,
        (int)BOARD_PINS_RGB_EC11_IO,
        (int)BOARD_PINS_RGB_KEY_IO,
        (int)BOARD_PINS_RGB_EDGE_IO);
    diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
             (uint32_t)s_state.profile, (uint32_t)BOARD_PINS_RGB_STATUS_IO,
             (uint32_t)BOARD_PINS_RGB_KEY_IO, (uint32_t)BOARD_PINS_RGB_EDGE_IO);
    status_led_force_boot_feedback();
    return final_ret;
}

esp_err_t status_led_start(void)
{
    esp_err_t ret = status_led_init();
    if (ret != ESP_OK) {
        return ret;
    }
    if (s_state.started) {
        return ESP_OK;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_load_persistent_config();
        xSemaphoreGive(s_mutex);
    }
    status_led_force_boot_feedback();

    BaseType_t ok = xTaskCreatePinnedToCore(
        status_led_task,
        "status_led_task",
        STATUS_LED_TASK_STACK_BYTES,
        NULL,
        STATUS_LED_TASK_PRIORITY,
        &s_task_handle,
        STATUS_LED_TASK_CORE_ID);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_state.started = true;
    ESP_LOGI(TAG,
             "status LED task started: refresh_ms=%u idle_refresh_ms=%u task_priority=%u task_core=%d backend=rmt_ws2812_800khz",
             STATUS_LED_REFRESH_MS,
             STATUS_LED_IDLE_REFRESH_MS,
             STATUS_LED_TASK_PRIORITY,
             STATUS_LED_TASK_CORE_ID);
    return ESP_OK;
}

void status_led_show_status_window(const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        s_state.output_disabled = false;
        s_state.low_power_disabled = false;
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        if (status_led_reason_is_boot_feedback(reason)) {
            status_led_mark_boot_feedback_locked(now_ms);
        }
        status_led_set_last_reason_locked(reason);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_ble_state(status_led_ble_state_t state, bool confidence_window)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        const bool effect_only = s_state.preview_effect_only;
        const bool routine_low_power_ble =
            s_state.low_power_disabled && !status_led_ble_state_attention_locked(state);
        if (!effect_only && !routine_low_power_ble) {
            status_led_resume_output_locked();
        }
        if (now_ms < s_state.preview_ble_override_until_ms &&
            state != STATUS_LED_BLE_REPAIRING) {
            xSemaphoreGive(s_mutex);
            return;
        }
        bool state_changed = s_state.ble_state != state;
        changed = state_changed;
        /* During recovery the BLE pairing window may live much longer than the
         * user confirmation cue. Keep the 2.7s BLE+EC11 cue alive across the
         * expected PAIRING/RECONNECTING/CONNECTED/TYPE_READY transitions, but
         * allow a secure connection to close the long recovery window. */
        const bool keep_repair_window = status_led_ble_repair_active_locked(now_ms) &&
            (state == STATUS_LED_BLE_PAIRING || state == STATUS_LED_BLE_RECONNECTING);
        const bool keep_repair_cue = status_led_ble_repair_cue_active_locked(now_ms) &&
            state != STATUS_LED_BLE_DISCONNECTED;
        if (state == STATUS_LED_BLE_REPAIRING) {
            status_led_start_ble_repair_locked(now_ms);
            changed = true;
            state_changed = true;
        } else {
            if (!keep_repair_window) {
                s_state.ble_repair_until_ms = 0U;
            }
            if (!keep_repair_cue && !keep_repair_window) {
                s_state.ble_repair_cue_started_ms = 0U;
                s_state.ble_repair_cue_until_ms = 0U;
            }
        }
        if (state == STATUS_LED_BLE_DISCONNECTED) {
            s_state.ble_repair_until_ms = 0U;
            s_state.ble_repair_cue_started_ms = 0U;
            s_state.ble_repair_cue_until_ms = 0U;
        }
        if (state != STATUS_LED_BLE_DISCONNECTED &&
            status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_BLE)) {
            changed = true;
        }
        if (state_changed && state != STATUS_LED_BLE_REPAIRING) {
            s_state.ble_transition_ms = now_ms;
        }
        if (state_changed && !effect_only) {
            status_led_release_boot_feedback_for_ble_locked(now_ms, state);
        }
        const bool active_work = s_state.recording_active ||
            s_state.processing_active ||
            s_state.ota_active;
        if (state_changed && !effect_only && !routine_low_power_ble &&
            status_led_ble_state_ready_locked(state) &&
            !keep_repair_cue &&
            !active_work) {
            status_led_schedule_idle_transition_clear_locked(now_ms);
        }
        s_state.ble_state = state;
        if (state_changed && !effect_only && !routine_low_power_ble) {
            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        }
        if (state_changed && state == STATUS_LED_BLE_CONNECTED && confidence_window) {
            s_state.ble_confidence_until_ms = now_ms + STATUS_LED_BLE_CONFIDENCE_MS;
            if (!s_state.ever_connected) {
                s_state.oobe_confidence_until_ms = now_ms + STATUS_LED_OOBE_CONFIDENCE_MS;
            }
            s_state.ever_connected = true;
        }
        const bool ble_visual_transition =
            state_changed && !effect_only && !active_work;
        if (state_changed && !effect_only) {
            if (ble_visual_transition) {
                s_state.last_transition_ms = now_ms;
                status_led_set_last_reason_locked("ble_state");
            }
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                     1, (uint32_t)state, confidence_window ? 1U : 0U, 0);
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_ble_repairing(const char *reason)
{
    status_led_notify_ble_repairing_for_ms(reason, STATUS_LED_BLE_REPAIR_CUE_MS);
}

void status_led_notify_ble_repairing_for_ms(const char *reason, uint32_t hold_ms)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        s_state.preview_ble_override_until_ms = 0U;
        status_led_start_ble_repair_locked_for_ms(now_ms, hold_ms);
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "ble_repairing");
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                 1, (uint32_t)STATUS_LED_BLE_REPAIRING, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_recording(bool active, status_led_rec_source_t source)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.preview_effect_only = false;
        const bool was_recording_active = s_state.recording_active;
        const status_led_rec_source_t previous_source = s_state.rec_source;
        const bool next_recording_active = active && source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE;
        const bool recording_state_changed =
            was_recording_active != next_recording_active ||
            previous_source != source ||
            (active && source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
        if (was_recording_active && !next_recording_active) {
            status_led_schedule_idle_transition_clear_locked(now_ms);
        }
        s_state.recording_active = next_recording_active;
        s_state.rec_source = source;
        if (recording_state_changed || !next_recording_active) {
            s_state.recording_level_percent = 0U;
            s_state.recording_level_updated_ms = active ? now_ms : 0U;
            s_state.recording_level_hold_until_ms = 0U;
            status_led_reset_recording_level_visual_locked(now_ms);
        }
        if (s_state.recording_active) {
            s_state.transition_clear_mask = 0U;
            s_state.ble_repair_until_ms = 0U;
            s_state.ble_repair_cue_started_ms = 0U;
            s_state.ble_repair_cue_until_ms = 0U;
            if (status_led_ec11_feedback_active_locked(now_ms)) {
                status_led_clear_ec11_feedback_locked();
                changed = true;
            }
            status_led_clear_ok_locked();
            status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_REC);
        }
        if (recording_state_changed) {
            s_state.last_transition_ms = now_ms;
            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
            status_led_set_last_reason_locked(active ? "recording_start" : "recording_stop");
        }
        if (active && source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
            s_state.error_domain = STATUS_LED_ERROR_DOMAIN_REC;
            s_state.error_severity = STATUS_LED_ERROR_RETRYABLE;
            s_state.error_started_ms = now_ms;
            s_state.error_until_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
        }
        if (recording_state_changed) {
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                     2, active ? 1U : 0U, (uint32_t)source, 0);
            changed = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_recording_level(uint8_t level_percent)
{
    // Called at audio cadence; the LED task samples and rate-limits this value on its own refresh tick.
    if (s_mutex == NULL) {
        return;
    }
    if (level_percent > 100U) {
        level_percent = 100U;
    }
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(STATUS_LED_RECORDING_LEVEL_LOCK_WAIT_MS)) == pdTRUE) {
        if (s_state.recording_active && s_state.rec_source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
            if (s_state.recording_level_hold_until_ms == 0U ||
                now_ms >= s_state.recording_level_hold_until_ms) {
                s_state.recording_level_percent = level_percent;
                s_state.recording_level_updated_ms = now_ms;
                s_state.recording_level_hold_until_ms = 0U;
            }
        }
        xSemaphoreGive(s_mutex);
    }
}

static void status_led_force_recording_level_for_review(uint8_t level_percent, uint32_t hold_ms)
{
    if (level_percent > 100U) {
        level_percent = 100U;
    }
    if (hold_ms > STATUS_LED_RECORDING_LEVEL_HOLD_MAX_MS) {
        hold_ms = STATUS_LED_RECORDING_LEVEL_HOLD_MAX_MS;
    }
    if (hold_ms == 0U) {
        hold_ms = STATUS_LED_RECORDING_LEVEL_STALE_MS;
    }

    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_state.recording_active && s_state.rec_source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
            s_state.recording_level_percent = level_percent;
            s_state.recording_level_updated_ms = now_ms;
            s_state.recording_level_hold_until_ms = now_ms + hold_ms;
        }
        xSemaphoreGive(s_mutex);
    }
}

void status_led_set_processing(bool active, const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.preview_effect_only = false;
        if (s_state.processing_active && !active) {
            status_led_schedule_idle_transition_clear_locked(now_ms);
        }
        s_state.processing_active = active;
        if (active) {
            s_state.transition_clear_mask = 0U;
            s_state.ble_repair_until_ms = 0U;
            s_state.ble_repair_cue_started_ms = 0U;
            s_state.ble_repair_cue_until_ms = 0U;
            s_state.processing_started_ms = now_ms;
            (void)status_led_clear_ec11_press_feedback_locked();
            status_led_clear_ok_locked();
            status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_AI);
            status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_OTA);
        }
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : (active ? "processing_start" : "processing_stop"));
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                 3, active ? 1U : 0U, now_ms - s_state.processing_started_ms, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_ota_active(bool active, size_t bytes_written, size_t expected_size, const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool was_active = s_state.ota_active;
        uint8_t old_progress = status_led_ota_progress_percent_locked();
        status_led_resume_output_locked();
        s_state.preview_effect_only = false;
        s_state.preview_ble_override_until_ms = 0U;

        if (active) {
            if (!was_active) {
                s_state.ota_started_ms = now_ms;
                s_state.last_transition_ms = now_ms;
                s_state.ble_repair_until_ms = 0U;
                s_state.ble_repair_cue_started_ms = 0U;
                s_state.ble_repair_cue_until_ms = 0U;
                status_led_clear_ok_locked();
                status_led_clear_key_feedback_locked();
                status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_OTA);
            }
            s_state.ota_active = true;
            s_state.ota_bytes_written = bytes_written;
            s_state.ota_expected_size = expected_size;
            uint8_t new_progress = status_led_ota_progress_percent_locked();
            if (!was_active || new_progress != old_progress || reason != NULL) {
                status_led_set_last_reason_locked(reason != NULL ? reason : "ota_progress");
                changed = true;
            }
        } else {
            if (was_active) {
                status_led_schedule_idle_transition_clear_locked(now_ms);
                s_state.last_transition_ms = now_ms;
                status_led_set_last_reason_locked(reason != NULL ? reason : "ota_stop");
                changed = true;
            }
            status_led_clear_ota_locked();
        }

        if (changed) {
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                     7, active ? 1U : 0U, (uint32_t)status_led_ota_progress_percent_locked(), 0);
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_success(const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.preview_effect_only = false;
        s_state.ok_started_ms = now_ms;
        s_state.ok_until_ms = now_ms + STATUS_LED_OK_TOTAL_MS;
        s_state.ok_warning = false;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "success");
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO, 4, 1, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_warning(const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.preview_effect_only = false;
        s_state.ok_started_ms = now_ms;
        s_state.ok_until_ms = now_ms + STATUS_LED_OK_TOTAL_MS;
        s_state.ok_warning = true;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "warning");
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_ERROR, DIAG_SEV_WARN, 4, 1, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_key_event(uint8_t key_index, bool pressed)
{
    if (key_index >= STATUS_LED_KEY_COUNT) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool long_release_fade = false;
        bool gesture_release_active = false;
        if (s_state.preview_effect_only) {
            xSemaphoreGive(s_mutex);
            return;
        }
        /* Physical key acknowledgement is independent from whether active
         * recording/processing/OTA work accepts the resulting action. */
        status_led_resume_interactive_output_locked();
        if (pressed) {
            s_state.key_pressed_mask |= 1U << key_index;
            s_state.key_feedback_started_ms[key_index] = 0U;
            s_state.key_feedback_until_ms[key_index] = 0U;
            s_state.key_feedback[key_index] = STATUS_LED_KEY_FEEDBACK_SINGLE;
            s_state.key_dark_latch_pending_mask =
                (uint8_t)(s_state.key_dark_latch_pending_mask | (1U << key_index));
        } else {
            s_state.key_pressed_mask &= ~(1U << key_index);
            if (s_state.key_feedback[key_index] == STATUS_LED_KEY_FEEDBACK_LONG) {
                /* Re-anchor the gesture to the release moment so the long-press
                 * fades out like single/double-click instead of snapping off. */
                s_state.key_feedback_started_ms[key_index] = now_ms;
                s_state.key_feedback_until_ms[key_index] = now_ms + STATUS_LED_KEY_FADE_MS;
                long_release_fade = true;
            } else if (s_state.key_feedback_started_ms[key_index] != 0U &&
                       now_ms < s_state.key_feedback_until_ms[key_index]) {
                gesture_release_active = true;
            }
            s_state.key_dark_latch_pending_mask =
                (uint8_t)(s_state.key_dark_latch_pending_mask | (1U << key_index));
        }
        s_state.key_until_ms[key_index] = long_release_fade
            ? 0U
            : (gesture_release_active ? 0U : now_ms + STATUS_LED_KEY_FEEDBACK_MS);
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(pressed ? "key_press" : "key_release");
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_cancel_key_preview(uint8_t key_index)
{
    if (key_index >= STATUS_LED_KEY_COUNT) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_state.preview_effect_only) {
            xSemaphoreGive(s_mutex);
            return;
        }
        s_state.key_pressed_mask &= ~(1U << key_index);
        s_state.key_until_ms[key_index] = 0U;
        s_state.key_feedback_started_ms[key_index] = 0U;
        s_state.key_feedback_until_ms[key_index] = 0U;
        s_state.key_feedback[key_index] = STATUS_LED_KEY_FEEDBACK_SINGLE;
        s_state.key_dark_latch_pending_mask =
            (uint8_t)(s_state.key_dark_latch_pending_mask | (1U << key_index));
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked("key_preview_cancel");
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_key_feedback(uint8_t key_index, status_led_key_feedback_t feedback)
{
    if (key_index >= STATUS_LED_KEY_COUNT ||
        (feedback != STATUS_LED_KEY_FEEDBACK_SINGLE &&
         feedback != STATUS_LED_KEY_FEEDBACK_DOUBLE &&
         feedback != STATUS_LED_KEY_FEEDBACK_LONG)) {
        return;
    }

    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_state.preview_effect_only) {
            xSemaphoreGive(s_mutex);
            return;
        }
        /* Gesture feedback stays local even when HID/custom actions are
         * suppressed by active recording or processing work. */
        status_led_resume_interactive_output_locked();
        uint32_t duration_ms = status_led_key_feedback_duration_ms(feedback);
        if (feedback != STATUS_LED_KEY_FEEDBACK_LONG) {
            s_state.key_pressed_mask &= ~(1U << key_index);
        }
        s_state.key_until_ms[key_index] = 0U;
        s_state.key_feedback[key_index] = feedback;
        s_state.key_feedback_started_ms[key_index] = now_ms;
        s_state.key_feedback_until_ms[key_index] = now_ms + duration_ms;
        s_state.key_dark_latch_pending_mask =
            (uint8_t)(s_state.key_dark_latch_pending_mask | (1U << key_index));
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked("key_feedback");
        diag_log(
            DIAG_SRC_STATUS_LED,
            DIAG_LED_STATE,
            DIAG_SEV_INFO,
            7,
            (uint32_t)(key_index + 1U),
            (uint32_t)feedback,
            duration_ms);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

static bool status_led_ec11_press_feedback_should_yield_locked(uint32_t now_ms)
{
    if (status_led_error_active_locked(now_ms) ||
        status_led_shutdown_confirm_active_locked(now_ms) ||
        s_state.ota_active ||
        status_led_ok_visual_percent_locked(now_ms) > 0U ||
        status_led_ble_repair_cue_active_locked(now_ms) ||
        s_state.processing_active ||
        status_led_recording_visual_percent_locked(now_ms) > 0U) {
        return true;
    }

    if (status_led_ec11_feedback_active_locked(now_ms) &&
        (s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW ||
         s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CCW)) {
        return true;
    }

    return s_state.profile == STATUS_LED_PROFILE_AMBIENT &&
           s_state.ble_state == STATUS_LED_BLE_TYPE_READY;
}

static void status_led_apply_ec11_feedback(status_led_ec11_feedback_t feedback, bool advance_motion)
{
    if (feedback != STATUS_LED_EC11_FEEDBACK_PRESS &&
        feedback != STATUS_LED_EC11_FEEDBACK_ROTATE_CW &&
        feedback != STATUS_LED_EC11_FEEDBACK_ROTATE_CCW) {
        return;
    }

    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_state.preview_effect_only) {
            xSemaphoreGive(s_mutex);
            return;
        }
        if (feedback == STATUS_LED_EC11_FEEDBACK_PRESS &&
            status_led_ec11_press_feedback_should_yield_locked(now_ms)) {
            xSemaphoreGive(s_mutex);
            return;
        }
        status_led_resume_interactive_output_locked();
        bool rotation_feedback =
            feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW ||
            feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CCW;
        bool active_rotation_feedback =
            status_led_ec11_feedback_active_locked(now_ms) &&
            (s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CW ||
             s_state.ec11_feedback == STATUS_LED_EC11_FEEDBACK_ROTATE_CCW);
        if (rotation_feedback) {
            if (active_rotation_feedback && s_state.ec11_feedback != feedback) {
                uint32_t current_step = status_led_ec11_feedback_motion_step_locked(now_ms);
                uint32_t current_dot =
                    status_led_ec11_feedback_dot_from_step(s_state.ec11_feedback, current_step);
                s_state.ec11_feedback_motion_step =
                    status_led_ec11_feedback_step_from_dot(feedback, current_dot);
                s_state.ec11_feedback_started_ms = now_ms;
            } else if (!active_rotation_feedback) {
                s_state.ec11_feedback_started_ms = now_ms;
                s_state.ec11_feedback_motion_step = 0U;
            }
            s_state.ec11_feedback = feedback;
            s_state.ec11_feedback_last_step_ms = now_ms;
        } else {
            s_state.ec11_feedback = feedback;
            s_state.ec11_feedback_started_ms = now_ms;
            s_state.ec11_feedback_last_step_ms = 0U;
            s_state.ec11_feedback_motion_step = 0U;
        }
        uint32_t feedback_ms = rotation_feedback
            ? STATUS_LED_EC11_ROTATION_HOLD_MS
            : STATUS_LED_EC11_FEEDBACK_MS;
        s_state.ec11_feedback_until_ms = now_ms + feedback_ms;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(advance_motion ? "ec11_feedback" : "ec11_feedback_refresh");
        bool log_rotation_feedback = rotation_feedback &&
                                     advance_motion &&
                                     (s_state.ec11_feedback_last_diag_ms == 0U ||
                                      (uint32_t)(now_ms - s_state.ec11_feedback_last_diag_ms) >=
                                          STATUS_LED_EC11_FEEDBACK_DIAG_MIN_MS);
        if (log_rotation_feedback) {
            diag_log(
                DIAG_SRC_STATUS_LED,
                DIAG_LED_STATE,
                DIAG_SEV_INFO,
                6,
                (uint32_t)feedback,
                feedback_ms,
                0);
            s_state.ec11_feedback_last_diag_ms = now_ms;
        }
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_ec11_feedback(status_led_ec11_feedback_t feedback)
{
    status_led_apply_ec11_feedback(feedback, true);
}

void status_led_refresh_ec11_feedback(status_led_ec11_feedback_t feedback)
{
    if (feedback == STATUS_LED_EC11_FEEDBACK_PRESS) {
        return;
    }
    status_led_apply_ec11_feedback(feedback, false);
}

static bool status_led_notify_shutdown_confirm_with_wait_and_duration(
    bool final,
    const char *reason,
    TickType_t wait_ticks,
    uint32_t duration_ms)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, wait_ticks) == pdTRUE) {
        status_led_resume_interactive_output_locked();
        s_state.transition_clear_mask = 0U;
        s_state.preview_effect_only = false;
        s_state.shutdown_confirm_started_ms = now_ms;
        s_state.shutdown_confirm_until_ms =
            duration_ms == STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS
                ? STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS
                : now_ms + duration_ms;
        s_state.shutdown_confirm_final = final;
        if (final) {
            s_state.shutdown_final_all_zone_latched_started_ms = 0U;
        }
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : (final ? "shutdown_confirm_final" : "shutdown_confirm"));
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                 5, final ? 2U : 1U, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
    return changed;
}

void status_led_notify_shutdown_confirm(bool final, const char *reason)
{
    (void)status_led_notify_shutdown_confirm_with_wait_and_duration(
        final,
        reason,
        portMAX_DELAY,
        final ? STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS : STATUS_LED_SHUTDOWN_CONFIRM_MS);
}

bool status_led_try_notify_shutdown_confirm(bool final, const char *reason, uint32_t wait_ms)
{
    return status_led_notify_shutdown_confirm_with_wait_and_duration(
        final,
        reason,
        pdMS_TO_TICKS(wait_ms),
        final ? STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS : STATUS_LED_SHUTDOWN_CONFIRM_MS);
}

bool status_led_try_notify_shutdown_final_hold(const char *reason, uint32_t wait_ms)
{
    return status_led_notify_shutdown_confirm_with_wait_and_duration(
        true,
        reason,
        pdMS_TO_TICKS(wait_ms),
        STATUS_LED_SHUTDOWN_FINAL_HOLD_UNTIL_CANCEL_MS);
}

bool status_led_try_hold_shutdown_all_off(const char *reason, uint32_t wait_ms)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
        s_state.shutdown_confirm_started_ms = 0U;
        s_state.shutdown_confirm_until_ms = 0U;
        s_state.shutdown_confirm_final = false;
        s_state.shutdown_final_all_zone_latched_started_ms = 0U;
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        s_state.preview_ble_override_until_ms = 0U;
        s_state.ble_repair_until_ms = 0U;
        s_state.ble_repair_cue_started_ms = 0U;
        s_state.ble_repair_cue_until_ms = 0U;
        s_state.output_disabled = true;
        s_state.low_power_disabled = true;
        s_state.test_mode = STATUS_LED_TEST_NONE;
        status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
        status_led_clear_ok_locked();
        status_led_clear_ec11_feedback_locked();
        status_led_clear_key_feedback_locked();
        status_led_clear_ota_locked();
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "shutdown_all_off_hold");
        status_led_log_output_state_locked(0);
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                 5, 4, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
        status_led_force_all_off(true);
        status_led_suspend_all_strips();
    }
    return changed;
}

void status_led_cancel_shutdown_confirm(const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_state.shutdown_confirm_started_ms != 0U) {
            s_state.shutdown_confirm_started_ms = 0U;
            s_state.shutdown_confirm_until_ms = 0U;
            s_state.shutdown_confirm_final = false;
            s_state.shutdown_final_all_zone_latched_started_ms = 0U;
            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
            s_state.last_transition_ms = now_ms;
            status_led_set_last_reason_locked(reason != NULL ? reason : "shutdown_confirm_cancel");
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                     5, 0, 0, 0);
            changed = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_error(
    status_led_error_domain_t domain,
    status_led_error_severity_t severity,
    const char *reason)
{
    if (domain == STATUS_LED_ERROR_DOMAIN_NONE) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.preview_effect_only = false;
        s_state.preview_ble_override_until_ms = 0U;
        s_state.ble_repair_until_ms = 0U;
        s_state.ble_repair_cue_started_ms = 0U;
        s_state.ble_repair_cue_until_ms = 0U;
        if (s_state.ble_state == STATUS_LED_BLE_REPAIRING) {
            s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
            s_state.ble_transition_ms = now_ms;
        }
        if (domain == STATUS_LED_ERROR_DOMAIN_OTA && s_state.ota_active) {
            status_led_schedule_idle_transition_clear_locked(now_ms);
            status_led_clear_ota_locked();
        }
        s_state.error_domain = domain;
        s_state.error_severity = severity;
        s_state.error_started_ms = now_ms;
        s_state.error_until_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "error");
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_ERROR, DIAG_SEV_WARN,
                 (uint32_t)domain, (uint32_t)severity, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_clear_error(status_led_error_domain_t domain)
{
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (domain == STATUS_LED_ERROR_DOMAIN_NONE || s_state.error_domain == domain) {
            s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
            s_state.error_until_ms = 0;
            changed = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_low_power_disabled(bool disabled)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        const bool preserve_repair_cue =
            disabled && status_led_ble_repair_cue_active_locked(now_ms);
        const bool next_low_power_disabled = disabled && !preserve_repair_cue;
        bool was_low_power_output = s_state.low_power_disabled || s_state.output_disabled;
        changed = s_state.low_power_disabled != next_low_power_disabled ||
                  (s_state.output_disabled && !next_low_power_disabled);
        s_state.low_power_disabled = next_low_power_disabled;
        if (!next_low_power_disabled) {
            s_state.output_disabled = false;
            s_state.last_power_poll_ms = 0;
            /*
             * The WS2812/RMT channels may have been suspended for low-power idle.
             * Send one zero frame before normal rendering resumes so a wake edge
            * cannot visually latch stale bus state as an all-on status flash.
             */
            if (was_low_power_output) {
                status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
            }
        }
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        s_state.preview_ble_override_until_ms = 0U;
        if (next_low_power_disabled) {
            status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_NON_KEY_ACCENTS);
            s_state.ble_repair_until_ms = 0U;
            s_state.ble_repair_cue_started_ms = 0U;
            s_state.ble_repair_cue_until_ms = 0U;
            status_led_clear_ec11_feedback_locked();
            if (s_state.ble_state == STATUS_LED_BLE_REPAIRING) {
                s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
                s_state.ble_transition_ms = now_ms;
            }
        }
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(
            disabled
                ? (preserve_repair_cue ? "repair_low_power_hold" : "low_power_off")
                : "low_power_resume");
        status_led_log_output_state_locked(
            status_led_active_flags_from_frame(&s_state.last_frame));
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_prepare_sleep(void)
{
    bool changed = false;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.low_power_disabled = true;
        s_state.output_disabled = true;
        s_state.test_mode = STATUS_LED_TEST_NONE;
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        s_state.preview_ble_override_until_ms = 0U;
        s_state.ble_repair_until_ms = 0U;
        s_state.ble_repair_cue_started_ms = 0U;
        s_state.ble_repair_cue_until_ms = 0U;
        status_led_clear_ec11_feedback_locked();
        uint32_t now_ms = status_led_now_ms();
        if (s_state.ble_state == STATUS_LED_BLE_REPAIRING) {
            s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
            s_state.ble_transition_ms = now_ms;
        }
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked("prepare_sleep");
        status_led_log_output_state_locked(0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
    status_led_force_all_off(true);
    status_led_suspend_all_strips();
}

static const char *status_led_strip_prefix(const char *line)
{
    if (line == NULL) {
        return NULL;
    }
    if (*line == '~') {
        line++;
    }
    size_t prefix_len = strlen(STATUS_LED_USB_PREFIX);
    if (strncmp(line, STATUS_LED_USB_PREFIX, prefix_len) != 0) {
        return NULL;
    }
    return line + prefix_len;
}

static uint8_t status_led_parse_single_strip_mask(const char *text)
{
    if (text == NULL || *text == '\0' || strcasecmp(text, "all") == 0) {
        return (1U << STATUS_LED_STRIP_STATUS) |
               (1U << STATUS_LED_STRIP_EC11) |
               (1U << STATUS_LED_STRIP_KEY) |
               (1U << STATUS_LED_STRIP_EDGE);
    }
    if (strcasecmp(text, "status") == 0) {
        return 1U << STATUS_LED_STRIP_STATUS;
    }
    if (strcasecmp(text, "ec11") == 0 ||
        strcasecmp(text, "knob") == 0 ||
        strcasecmp(text, "ring") == 0) {
        return 1U << STATUS_LED_STRIP_EC11;
    }
    if (strcasecmp(text, "key") == 0) {
        return 1U << STATUS_LED_STRIP_KEY;
    }
    if (strcasecmp(text, "edge") == 0) {
        return 1U << STATUS_LED_STRIP_EDGE;
    }
    return 0;
}

static uint8_t status_led_parse_strip_mask(const char *text)
{
    if (text == NULL || *text == '\0') {
        return status_led_parse_single_strip_mask(text);
    }

    char copy[64] = {0};
    snprintf(copy, sizeof(copy), "%s", text);
    uint8_t mask = 0;
    char *save = NULL;
    char *token = strtok_r(copy, ",+| ", &save);
    while (token != NULL) {
        uint8_t part = status_led_parse_single_strip_mask(token);
        if (part == 0) {
            return 0;
        }
        mask |= part;
        token = strtok_r(NULL, ",+| ", &save);
    }
    return mask;
}

static bool status_led_parse_profile(const char *text, status_led_profile_t *profile)
{
    if (strcasecmp(text, "off") == 0) {
        *profile = STATUS_LED_PROFILE_OFF;
    } else if (strcasecmp(text, "low") == 0) {
        *profile = STATUS_LED_PROFILE_LOW;
    } else if (strcasecmp(text, "standard") == 0) {
        *profile = STATUS_LED_PROFILE_STANDARD;
    } else if (strcasecmp(text, "ambient") == 0) {
        *profile = STATUS_LED_PROFILE_AMBIENT;
    } else if (strcasecmp(text, "factory") == 0) {
        *profile = STATUS_LED_PROFILE_FACTORY;
    } else {
        return false;
    }
    return true;
}

static bool status_led_parse_domain(const char *text, status_led_error_domain_t *domain)
{
    if (strcasecmp(text, "ble") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_BLE;
    } else if (strcasecmp(text, "rec") == 0 || strcasecmp(text, "recording") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_REC;
    } else if (strcasecmp(text, "ai") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_AI;
    } else if (strcasecmp(text, "ota") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_OTA;
    } else if (strcasecmp(text, "power") == 0 || strcasecmp(text, "pwr") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_POWER;
    } else if (strcasecmp(text, "system") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_SYSTEM;
    } else {
        return false;
    }
    return true;
}

static bool status_led_parse_severity(const char *text, status_led_error_severity_t *severity)
{
    if (strcasecmp(text, "retryable") == 0 || strcasecmp(text, "warn") == 0 || strcasecmp(text, "warning") == 0) {
        *severity = STATUS_LED_ERROR_RETRYABLE;
    } else if (strcasecmp(text, "hard") == 0 || strcasecmp(text, "error") == 0 || strcasecmp(text, "fault") == 0) {
        *severity = STATUS_LED_ERROR_HARD;
    } else {
        return false;
    }
    return true;
}

static bool status_led_parse_calibration_strip(const char *text, status_led_strip_id_t *strip, uint8_t *count, uint8_t *first_led)
{
    if (strcasecmp(text, "status") == 0) {
        *strip = STATUS_LED_STRIP_STATUS;
        *count = STATUS_LED_STATUS_COUNT;
        *first_led = STATUS_LED_STATUS_FIRST_LED;
        return true;
    }
    if (strcasecmp(text, "ec11") == 0 ||
        strcasecmp(text, "knob") == 0 ||
        strcasecmp(text, "ring") == 0) {
        *strip = STATUS_LED_STRIP_EC11;
        *count = STATUS_LED_EC11_COUNT;
        *first_led = STATUS_LED_EC11_FIRST_LED;
        return true;
    }
    if (strcasecmp(text, "key") == 0) {
        *strip = STATUS_LED_STRIP_KEY;
        *count = STATUS_LED_KEY_COUNT;
        *first_led = STATUS_LED_KEY_FIRST_LED;
        return true;
    }
    if (strcasecmp(text, "edge") == 0) {
        *strip = STATUS_LED_STRIP_EDGE;
        *count = STATUS_LED_EDGE_COUNT;
        *first_led = STATUS_LED_EDGE_FIRST_LED;
        return true;
    }
    return false;
}

static bool status_led_parse_pixel_index(const char *text, uint8_t first_led, uint8_t count, uint8_t *zero_based_index)
{
    const char *cursor = text;
    bool led_prefixed = false;
    if (strncasecmp(cursor, "LED", 3) == 0) {
        cursor += 3;
        led_prefixed = true;
    }

    char *end = NULL;
    long value = strtol(cursor, &end, 10);
    if (cursor == end || *end != '\0') {
        return false;
    }

    long last_led = (long)first_led + (long)count - 1L;
    if (led_prefixed && value >= first_led && value <= last_led) {
        *zero_based_index = (uint8_t)(value - first_led);
        return true;
    }

    if (value >= 1 && value <= count) {
        *zero_based_index = (uint8_t)(value - 1);
        return true;
    }

    if (!led_prefixed && value >= first_led && value <= last_led) {
        *zero_based_index = (uint8_t)(value - first_led);
        return true;
    }

    return false;
}

static bool status_led_parse_color_token(const char *text, status_led_rgb_t *color)
{
    if (strcasecmp(text, "red") == 0) {
        *color = status_led_rgb(255, 0, 0);
    } else if (strcasecmp(text, "green") == 0) {
        *color = status_led_rgb(0, 255, 0);
    } else if (strcasecmp(text, "blue") == 0) {
        *color = status_led_rgb(0, 0, 255);
    } else if (strcasecmp(text, "white") == 0) {
        *color = status_led_rgb(255, 255, 255);
    } else if (strcasecmp(text, "off") == 0 || strcasecmp(text, "black") == 0) {
        *color = status_led_rgb(0, 0, 0);
    } else {
        return false;
    }
    return true;
}

static void status_led_run_pixel_test(
    status_led_strip_id_t strip,
    uint8_t index,
    status_led_rgb_t color,
    uint8_t percent)
{
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.test_mode = STATUS_LED_TEST_PIXEL;
        s_state.test_strip_mask = 1U << strip;
        s_state.test_pixel_strip = strip;
        s_state.test_pixel_index = index;
        s_state.test_pixel_color = color;
        s_state.test_pixel_percent = percent;
        s_state.test_started_ms = now_ms;
        s_state.output_disabled = false;
        s_state.low_power_disabled = false;
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
        status_led_set_last_reason_locked("test_pixel");
        xSemaphoreGive(s_mutex);
    }
    status_led_request_refresh();
}

static void status_led_print_strip_rgb_line(
    const char *detail,
    const char *name,
    const status_led_rgb_t *colors,
    size_t count)
{
    int written = snprintf(s_print_line, sizeof(s_print_line), "~LED:STATUS detail=%s %s_rgb=", detail, name);
    size_t offset = written > 0 ? (size_t)written : 0U;
    if (offset >= sizeof(s_print_line)) {
        offset = sizeof(s_print_line) - 1U;
    }
    for (size_t index = 0; index < count; ++index) {
        if (offset >= sizeof(s_print_line) - 1U) {
            break;
        }
        written = snprintf(
            s_print_line + offset,
            sizeof(s_print_line) - offset,
            "%spx%u:%u,%u,%u",
            index == 0 ? "" : ";",
            (unsigned)(index + 1U),
            colors[index].r,
            colors[index].g,
            colors[index].b);
        if (written <= 0) {
            break;
        }
        offset += (size_t)written;
    }
    s_print_line[sizeof(s_print_line) - 1U] = '\0';
    printf("%s\n", s_print_line);
    watchdog_platform_feed_current_task();
}

static void status_led_feed_after_print(void)
{
    watchdog_platform_feed_current_task();
}

static void status_led_fill_print_snapshot_locked(status_led_print_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->profile = s_state.profile;
    snapshot->brightness_percent = s_state.brightness_percent;
    snapshot->status_zone_brightness_percent = s_state.status_zone_brightness_percent;
    snapshot->key_zone_brightness_percent = s_state.key_zone_brightness_percent;
    snapshot->ec11_zone_brightness_percent = s_state.ec11_zone_brightness_percent;
    snapshot->edge_zone_brightness_percent = s_state.edge_zone_brightness_percent;
    snapshot->ble_state = s_state.ble_state;
    snapshot->rec_source = s_state.rec_source;
    snapshot->error_domain = s_state.error_domain;
    snapshot->error_severity = s_state.error_severity;
    snapshot->recording_active = s_state.recording_active;
    snapshot->processing_active = s_state.processing_active;
    snapshot->ota_active = s_state.ota_active;
    snapshot->ota_bytes_written = s_state.ota_bytes_written;
    snapshot->ota_expected_size = s_state.ota_expected_size;
    snapshot->ota_progress_percent = status_led_ota_progress_percent_locked();
    snapshot->recording_level_percent = s_state.recording_level_percent;
    snapshot->recording_level_visual_percent = s_state.recording_level_visual_percent;
    snapshot->recording_level_hold_until_ms = s_state.recording_level_hold_until_ms;
    snapshot->battery_valid = s_state.battery_valid;
    snapshot->external_power_present = s_state.external_power_present;
    snapshot->external_power_source_flags = s_state.external_power_source_flags;
    snapshot->charging = s_state.charging;
    snapshot->full = s_state.full;
    snapshot->raw_charging = s_state.raw_charging;
    snapshot->raw_full = s_state.raw_full;
    snapshot->charge_full_latched = s_state.charge_full_latched;
    snapshot->battery_level_percent = s_state.battery_level_percent;
    snapshot->battery_display_level_percent = s_state.battery_display_level_percent;
    snapshot->battery_display_valid = s_state.battery_display_valid;
    snapshot->battery_display_rise_suppressed = s_state.battery_display_rise_suppressed;
    snapshot->battery_mv = s_state.battery_mv;
    snapshot->battery_display_mv = s_state.battery_display_mv;
    snapshot->status_window_until_ms = s_state.status_window_until_ms;
    snapshot->ble_confidence_until_ms = s_state.ble_confidence_until_ms;
    snapshot->ble_repair_until_ms = s_state.ble_repair_until_ms;
    snapshot->ble_repair_cue_until_ms = s_state.ble_repair_cue_until_ms;
    snapshot->oobe_confidence_until_ms = s_state.oobe_confidence_until_ms;
    snapshot->shutdown_confirm_started_ms = s_state.shutdown_confirm_started_ms;
    snapshot->shutdown_confirm_until_ms = s_state.shutdown_confirm_until_ms;
    snapshot->shutdown_confirm_final = s_state.shutdown_confirm_final;
    snapshot->charge_full_candidate_since_ms = s_state.charge_full_candidate_since_ms;
    snapshot->ble_transition_ms = s_state.ble_transition_ms;
    snapshot->preview_ble_override_until_ms = s_state.preview_ble_override_until_ms;
    snapshot->last_transition_ms = s_state.last_transition_ms;
    snapshot->last_estimated_current_ma = s_state.last_estimated_current_ma;
    snapshot->last_current_budget_ma = s_state.last_current_budget_ma;
    snapshot->last_budget_scale_percent = s_state.last_budget_scale_percent;
    snapshot->current_limited_by_budget = s_state.current_limited_by_budget;
    snapshot->output_disabled = s_state.output_disabled;
    snapshot->low_power_disabled = s_state.low_power_disabled;
    snapshot->preview_suppress_accents = s_state.preview_suppress_accents;
    snapshot->preview_effect_only = s_state.preview_effect_only;
    snapshot->transition_clear_mask = s_state.transition_clear_mask;
    snapshot->key_pressed_mask = s_state.key_pressed_mask;
    snapshot->test_mode = s_state.test_mode;
    snapshot->test_strip_mask = s_state.test_strip_mask;
    snprintf(snapshot->last_reason, sizeof(snapshot->last_reason), "%s", s_state.last_reason);
    snapshot->last_frame = s_state.last_frame;
}

static void status_led_print_status(void)
{
    status_led_print_snapshot_t snapshot;
    gpio_num_t strip_gpios[STATUS_LED_STRIP_COUNT];
    uint8_t strip_counts[STATUS_LED_STRIP_COUNT];
    uint8_t strip_available[STATUS_LED_STRIP_COUNT];
    uint8_t strip_dma_requested[STATUS_LED_STRIP_COUNT];
    uint8_t strip_dma[STATUS_LED_STRIP_COUNT];
    uint8_t strip_dma_fallback[STATUS_LED_STRIP_COUNT];
    uint8_t strip_rmt_dma_requested[STATUS_LED_STRIP_COUNT];
    uint8_t strip_rmt_dma[STATUS_LED_STRIP_COUNT];
    uint8_t strip_rmt_dma_fallback[STATUS_LED_STRIP_COUNT];
    uint8_t strip_spi_dma_requested[STATUS_LED_STRIP_COUNT];
    uint8_t strip_spi_dma[STATUS_LED_STRIP_COUNT];
    uint8_t strip_spi_dma_fallback[STATUS_LED_STRIP_COUNT];
    status_led_strip_transport_t strip_transport_actual[STATUS_LED_STRIP_COUNT];
    unsigned int strip_mem_block_symbols[STATUS_LED_STRIP_COUNT];
    uint8_t status_tail_guard_pixels = 0U;
    uint8_t key_tail_guard_pixels = 0U;
    status_led_color_order_t strip_orders[STATUS_LED_STRIP_COUNT];
    device_settings_snapshot_t device_settings = {0};
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_frame_t sampled_frame;
        status_led_render_frame_locked(&sampled_frame, now_ms);
        status_led_fill_print_snapshot_locked(&snapshot);
        snapshot.last_frame = sampled_frame;
        for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
            strip_gpios[index] = s_strips[index].gpio;
            strip_counts[index] = s_strips[index].led_count;
            strip_orders[index] = s_strips[index].color_order;
            strip_available[index] = status_led_strip_backend_available(s_strips[index].backend) ? 1U : 0U;
            strip_dma_requested[index] =
                status_led_strip_backend_dma_requested(s_strips[index].backend) ? 1U : 0U;
            strip_dma[index] = status_led_strip_backend_uses_dma(s_strips[index].backend) ? 1U : 0U;
            strip_dma_fallback[index] =
                status_led_strip_backend_dma_fallback(s_strips[index].backend) ? 1U : 0U;
            strip_transport_actual[index] =
                status_led_strip_backend_transport(s_strips[index].backend);
            strip_rmt_dma_requested[index] =
                s_strips[index].transport == STATUS_LED_STRIP_TRANSPORT_RMT ? strip_dma_requested[index] : 0U;
            strip_rmt_dma[index] =
                strip_transport_actual[index] == STATUS_LED_STRIP_TRANSPORT_RMT ? strip_dma[index] : 0U;
            strip_rmt_dma_fallback[index] =
                s_strips[index].transport == STATUS_LED_STRIP_TRANSPORT_RMT ? strip_dma_fallback[index] : 0U;
            strip_spi_dma_requested[index] =
                s_strips[index].transport == STATUS_LED_STRIP_TRANSPORT_SPI ? 1U : 0U;
            strip_spi_dma[index] =
                strip_transport_actual[index] == STATUS_LED_STRIP_TRANSPORT_SPI ? strip_dma[index] : 0U;
            strip_spi_dma_fallback[index] =
                s_strips[index].transport == STATUS_LED_STRIP_TRANSPORT_SPI ? strip_dma_fallback[index] : 0U;
            strip_mem_block_symbols[index] =
                (unsigned int)status_led_strip_backend_mem_block_symbols(s_strips[index].backend);
        }
        status_tail_guard_pixels = s_strips[STATUS_LED_STRIP_STATUS].tail_guard_pixels;
        key_tail_guard_pixels = s_strips[STATUS_LED_STRIP_KEY].tail_guard_pixels;
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    device_settings_get_snapshot(&device_settings);

    const uint8_t profile_cap_percent = status_led_profile_cap_percent_for(snapshot.profile, false);
    const uint8_t effective_cap_percent = snapshot.brightness_percent < profile_cap_percent
        ? snapshot.brightness_percent
        : profile_cap_percent;
    const uint32_t status_window_ms_left =
        now_ms < snapshot.status_window_until_ms ? snapshot.status_window_until_ms - now_ms : 0U;
    const uint32_t ble_confidence_ms_left =
        now_ms < snapshot.ble_confidence_until_ms ? snapshot.ble_confidence_until_ms - now_ms : 0U;
    const uint32_t ble_repair_ms_left =
        now_ms < snapshot.ble_repair_until_ms ? snapshot.ble_repair_until_ms - now_ms : 0U;
    const uint32_t ble_repair_cue_ms_left =
        now_ms < snapshot.ble_repair_cue_until_ms ? snapshot.ble_repair_cue_until_ms - now_ms : 0U;
    const uint32_t oobe_confidence_ms_left =
        now_ms < snapshot.oobe_confidence_until_ms ? snapshot.oobe_confidence_until_ms - now_ms : 0U;
    const uint32_t preview_ble_override_ms_left =
        now_ms < snapshot.preview_ble_override_until_ms ? snapshot.preview_ble_override_until_ms - now_ms : 0U;
    const uint32_t shutdown_confirm_elapsed_ms =
        snapshot.shutdown_confirm_started_ms != 0U && now_ms >= snapshot.shutdown_confirm_started_ms
            ? now_ms - snapshot.shutdown_confirm_started_ms
            : 0U;
    const bool shutdown_confirm_active =
        snapshot.shutdown_confirm_started_ms != 0U &&
        (!snapshot.shutdown_confirm_final || now_ms < snapshot.shutdown_confirm_until_ms);
    const bool shutdown_confirm_latched =
        shutdown_confirm_active &&
        !snapshot.shutdown_confirm_final &&
        shutdown_confirm_elapsed_ms >= STATUS_LED_SHUTDOWN_CONFIRM_MS;
    const uint32_t charge_full_candidate_ms =
        snapshot.charge_full_candidate_since_ms != 0 && now_ms >= snapshot.charge_full_candidate_since_ms
            ? now_ms - snapshot.charge_full_candidate_since_ms
            : 0U;
    const uint8_t brightness_duty_255 =
        status_led_linear_percent_to_255(snapshot.brightness_percent);
    const uint32_t rec_level_hold_ms_left =
        now_ms < snapshot.recording_level_hold_until_ms ? snapshot.recording_level_hold_until_ms - now_ms : 0U;
    const uint8_t active_pwr = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_PWR]) ? 1U : 0U;
    const uint8_t active_ble = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_BLE]) ? 1U : 0U;
    const uint8_t active_rec = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_REC]) ? 1U : 0U;
    const uint8_t active_ai =
        (snapshot.processing_active || status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_AI])) ? 1U : 0U;
    const uint8_t active_ok = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_OK]) ? 1U : 0U;
    const uint8_t active_warn = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_WARN]) ? 1U : 0U;
    const uint8_t active_ec11 =
        status_led_strip_has_light(snapshot.last_frame.ec11, STATUS_LED_EC11_COUNT) ? 1U : 0U;
    const uint8_t active_key =
        status_led_strip_has_light(snapshot.last_frame.key, STATUS_LED_KEY_COUNT) ? 1U : 0U;
    const uint8_t active_edge =
        status_led_strip_has_light(snapshot.last_frame.edge, STATUS_LED_EDGE_COUNT) ? 1U : 0U;
    const uint8_t rmt_tx_dma_supported = status_led_strip_backend_dma_supported() ? 1U : 0U;
    const uint8_t rmt_strip_all_available =
        strip_available[STATUS_LED_STRIP_STATUS] != 0U &&
        strip_available[STATUS_LED_STRIP_EC11] != 0U &&
        strip_available[STATUS_LED_STRIP_KEY] != 0U &&
        strip_available[STATUS_LED_STRIP_EDGE] != 0U
            ? 1U
            : 0U;
    const uint8_t rmt_tx_dma_all_strips =
        strip_rmt_dma[STATUS_LED_STRIP_STATUS] != 0U &&
        strip_rmt_dma[STATUS_LED_STRIP_EC11] != 0U &&
        strip_rmt_dma[STATUS_LED_STRIP_KEY] != 0U &&
        strip_rmt_dma[STATUS_LED_STRIP_EDGE] != 0U
            ? 1U
            : 0U;

    printf(
        "~LED:STATUS detail=contract backend=mixed_rmt_and_spi_ws2812_800khz refresh_ms=%u reset_us=300 rmt_reset_us=300 spi_reset_us=600 spi_ws2812_waveform=4bit_3m2_0x8_0xE"
        " task_priority=%u task_core=%d task_affinity=cpu1"
        " strip_transport_requested=status:rmt,ec11:spi2,key:spi3,edge:rmt"
        " strip_transport_actual=status:%s,ec11:%s,key:%s,edge:%s"
        " spi_dma_outputs_requested=ec11:SPI2,key:SPI3"
        " spi_dma_requested=status:%u,ec11:%u,key:%u,edge:%u"
        " spi_dma_actual=status:%u,ec11:%u,key:%u,edge:%u"
        " spi_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u"
        " rmt_tx_dma_supported=%u rmt_tx_dma_strategy=status_strip_dma_full_frame_buffer"
        " rmt_strip_all_available=%u rmt_tx_dma_all_strips=%u"
        " rmt_tx_dma_requested=status:%u,ec11:%u,key:%u,edge:%u"
        " rmt_tx_dma_actual=status:%u,ec11:%u,key:%u,edge:%u"
        " rmt_tx_dma_fallback=status:%u,ec11:%u,key:%u,edge:%u"
        " rmt_mem_block_symbols=status:%u,ec11:%u,key:%u,edge:%u"
        " idle_refresh_ms=%u unchanged_tx_suppression=1 timing=ws2812_4020_compatible"
        " low_power_transport_suspend_ms=%u low_power_status_tx=non_dma_clear_and_final_frame"
        " low_power_all_zone_tx=non_dma_clear_and_final_frame"
        " low_power_spi_latch=spi_dma_prelatch_then_one_shot_rmt_gpio_low"
        " key_dark_idle_resync_ms=%u key_tail_guard_pixels=%u key_dark_latch_rmt_writes=%u"
        " key_dark_latch_expiry_dirty=1"
        " key_dark_clear_tx=spi_dma_prelatch_then_one_shot_rmt_gpio_low"
        " key_lit_edge_tx=spi_dma_only"
        " key_feedback_dynamic_tx=spi_dma_until_dark_latch key_release_fade_ms=%u key_single_white_hold_ms=%u"
        " key_multi_key_independent_fade=%u"
        " shutdown_final_status_tx=non_dma_pwr_only_latch"
        " shutdown_final_all_zone_tx=non_dma_pwr_only_latch_or_all_off"
        " low_power_final_latch_writes=%u"
        " low_power_status_retry_writes=%u"
        " status_tail_guard_pixels=%u status_tail_reinforce=recording_processing"
        " status_tail_reinforce_writes=%u"
        " status_tail_overlap_reinforce_writes=%u"
        " status_tail_legacy_safe_effect_percent=%u..%u"
        " status_tail_overlap_effect_percent=rec_audio_%u..%u_%upct_ai_think_%u..%u_%upct"
        " status_tail_overlap_style=dma_audio_rec_ai_da_dada"
        " status_tail_overlap_legacy_effect_percent=%u..%u_1pct_eased_dual_core"
        " status_tail_overlap_legacy_period_ms=%u status_tail_overlap_legacy_rise_ms=%u"
        " status_tail_overlap_legacy_high_hold_ms=%u status_tail_overlap_legacy_fall_ms=%u"
        " status_tail_overlap_legacy_low_hold_ms=%u status_tail_overlap_legacy_quantum_percent=%u"
        " recording_level_reactive=1 recording_level_lock_wait_ms=%u"
        " recording_level_effect_percent=%u..%u_smooth_%upct"
        " recording_level_smoothing=attack%u_release%u"
        " processing_thinking_style=single_then_double_beat"
        " processing_thinking_color=purple_static"
        " processing_thinking_effect_percent=%u..%u_%upct"
        " processing_thinking_scan_profile=da_long_gap_grouped_dada_rest"
        " processing_thinking_period_ms=%u"
        " ota_progress_style=LED5_OK_cyan_pulse_EC11_progress_EDGE_chase"
        " ota_progress_idle_blocker=POWER_MANAGER_BLOCKER_OTA"
        " strip_dirty_tx=1 strip_tx_failure_retry_dirty=1 suspended_strip_resume_dirty=1"
        " status_tx_last=1 rmt_idle_drive=active_dma_low_power_all_zone_non_dma_final_frame_then_release_gpio_low"
        " dynamic_active_accents=1"
        " status_query_samples_current_render=1"
        " effect_only_preview=1 preview_effect_zone_brightness=1 test_calibration_full_brightness=1"
        " active_work_status_dynamic=1 active_work_status_overlap_dynamic_1pct_eased=0"
        " active_work_status_audio_reactive_rec=1 active_work_status_thinking_ai=1"
        " led_contract_rev=" STATUS_LED_CONTRACT_REV
        " factory_full_brightness=1 safety_full_brightness=1"
        " semantic_order=LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN"
        " mapping_contract=" STATUS_LED_STATUS_KEY_MAPPING_CONTRACT
        " status_physical_map=" STATUS_LED_STATUS_PHYSICAL_MAP
        " key_physical_map=" STATUS_LED_KEY_PHYSICAL_MAP
        " separate_status_key_color_order=1 status_default_order=GRB key_default_order=GRB\n",
        STATUS_LED_REFRESH_MS,
        STATUS_LED_TASK_PRIORITY,
        STATUS_LED_TASK_CORE_ID,
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_STATUS],
            s_strips[STATUS_LED_STRIP_STATUS].spi_host),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_EC11],
            s_strips[STATUS_LED_STRIP_EC11].spi_host),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_KEY],
            s_strips[STATUS_LED_STRIP_KEY].spi_host),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_EDGE],
            s_strips[STATUS_LED_STRIP_EDGE].spi_host),
        strip_spi_dma_requested[STATUS_LED_STRIP_STATUS],
        strip_spi_dma_requested[STATUS_LED_STRIP_EC11],
        strip_spi_dma_requested[STATUS_LED_STRIP_KEY],
        strip_spi_dma_requested[STATUS_LED_STRIP_EDGE],
        strip_spi_dma[STATUS_LED_STRIP_STATUS],
        strip_spi_dma[STATUS_LED_STRIP_EC11],
        strip_spi_dma[STATUS_LED_STRIP_KEY],
        strip_spi_dma[STATUS_LED_STRIP_EDGE],
        strip_spi_dma_fallback[STATUS_LED_STRIP_STATUS],
        strip_spi_dma_fallback[STATUS_LED_STRIP_EC11],
        strip_spi_dma_fallback[STATUS_LED_STRIP_KEY],
        strip_spi_dma_fallback[STATUS_LED_STRIP_EDGE],
        rmt_tx_dma_supported,
        rmt_strip_all_available,
        rmt_tx_dma_all_strips,
        strip_rmt_dma_requested[STATUS_LED_STRIP_STATUS],
        strip_rmt_dma_requested[STATUS_LED_STRIP_EC11],
        strip_rmt_dma_requested[STATUS_LED_STRIP_KEY],
        strip_rmt_dma_requested[STATUS_LED_STRIP_EDGE],
        strip_rmt_dma[STATUS_LED_STRIP_STATUS],
        strip_rmt_dma[STATUS_LED_STRIP_EC11],
        strip_rmt_dma[STATUS_LED_STRIP_KEY],
        strip_rmt_dma[STATUS_LED_STRIP_EDGE],
        strip_rmt_dma_fallback[STATUS_LED_STRIP_STATUS],
        strip_rmt_dma_fallback[STATUS_LED_STRIP_EC11],
        strip_rmt_dma_fallback[STATUS_LED_STRIP_KEY],
        strip_rmt_dma_fallback[STATUS_LED_STRIP_EDGE],
        strip_mem_block_symbols[STATUS_LED_STRIP_STATUS],
        strip_mem_block_symbols[STATUS_LED_STRIP_EC11],
        strip_mem_block_symbols[STATUS_LED_STRIP_KEY],
        strip_mem_block_symbols[STATUS_LED_STRIP_EDGE],
        STATUS_LED_IDLE_REFRESH_MS,
        STATUS_LED_RMT_IDLE_RELEASE_MS,
        STATUS_LED_KEY_DARK_RESYNC_MS,
        key_tail_guard_pixels,
        STATUS_LED_KEY_DARK_LATCH_RMT_WRITES,
        STATUS_LED_KEY_FEEDBACK_MS,
        STATUS_LED_KEY_SINGLE_WHITE_HOLD_MS,
        STATUS_LED_KEY_MULTI_KEY_INDEPENDENT_FADE,
        STATUS_LED_LOW_POWER_FINAL_LATCH_WRITES,
        STATUS_LED_LOW_POWER_STATUS_RETRY_WRITES,
        STATUS_LED_STATUS_TAIL_GUARD_PIXELS,
        STATUS_LED_STATUS_TAIL_REINFORCE_WRITES,
        STATUS_LED_STATUS_TAIL_OVERLAP_REINFORCE_WRITES,
        STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MIN_PERCENT,
        STATUS_LED_STATUS_TAIL_SAFE_EFFECT_MAX_PERCENT,
        STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT,
        STATUS_LED_RECORDING_LEVEL_EFFECT_MAX_PERCENT,
        STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT,
        STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT,
        STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT,
        STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT,
        STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MIN_PERCENT,
        STATUS_LED_STATUS_TAIL_OVERLAP_EFFECT_MAX_PERCENT,
        STATUS_LED_STATUS_TAIL_OVERLAP_BREATH_PERIOD_MS,
        STATUS_LED_STATUS_TAIL_OVERLAP_RISE_MS,
        STATUS_LED_STATUS_TAIL_OVERLAP_HIGH_HOLD_MS,
        STATUS_LED_STATUS_TAIL_OVERLAP_FALL_MS,
        STATUS_LED_STATUS_TAIL_OVERLAP_LOW_HOLD_MS,
        STATUS_LED_STATUS_TAIL_OVERLAP_QUANTUM_PERCENT,
        STATUS_LED_RECORDING_LEVEL_LOCK_WAIT_MS,
        STATUS_LED_RECORDING_LEVEL_EFFECT_MIN_PERCENT,
        STATUS_LED_RECORDING_LEVEL_EFFECT_MAX_PERCENT,
        STATUS_LED_RECORDING_LEVEL_QUANTUM_PERCENT,
        STATUS_LED_RECORDING_LEVEL_ATTACK_PERCENT_PER_SEC,
        STATUS_LED_RECORDING_LEVEL_RELEASE_PERCENT_PER_SEC,
        STATUS_LED_PROCESSING_THINK_EFFECT_MIN_PERCENT,
        STATUS_LED_PROCESSING_THINK_EFFECT_MAX_PERCENT,
        STATUS_LED_PROCESSING_THINK_QUANTUM_PERCENT,
        STATUS_LED_PROCESSING_THINK_PERIOD_MS);
    status_led_feed_after_print();
    printf(
        "~LED:STATUS detail=brightness profile=%s effect_profile=product_v1"
        " profile_cap_percent=%u neutral_legacy_brightness_percent=%u effective_cap_percent=%u"
        " budget_scale_percent=%u budget_limited_by_current=%u"
        " legacy_plugged_brightness_percent=%u legacy_battery_brightness_percent=%u neutral_legacy_brightness_percent=%u"
        " status_zone_brightness_percent=%u key_zone_brightness_percent=%u"
        " ec11_zone_brightness_percent=%u edge_zone_brightness_percent=%u"
        " brightness_duty_255=%u"
        " zone_brightness_is_hard_cap=1 zone_brightness_effect_peak_cap=1 zone_brightness_preserves_effect_percent=1"
        " status_rgb_energy_balance=" STATUS_LED_STATUS_RGB_ENERGY_BALANCE_CONTRACT
        " legacy_brightness_neutral=1 profile_dimming_disabled=1\n",
        status_led_profile_name(snapshot.profile),
        profile_cap_percent,
        snapshot.brightness_percent,
        effective_cap_percent,
        snapshot.last_budget_scale_percent,
        snapshot.current_limited_by_budget ? 1U : 0U,
        device_settings.plugged_brightness_percent,
        device_settings.battery_brightness_percent,
        snapshot.brightness_percent,
        snapshot.status_zone_brightness_percent,
        snapshot.key_zone_brightness_percent,
        snapshot.ec11_zone_brightness_percent,
        snapshot.edge_zone_brightness_percent,
        brightness_duty_255);
    status_led_feed_after_print();
    printf(
        "~LED:STATUS detail=strips"
        " strips=status:gpio%d:count%u:order%s:transport%s:avail%u:dma_req%u:dma%u:dma_fb%u:rmt_dma_req%u:rmt_dma%u:rmt_dma_fb%u:spi_dma_req%u:spi_dma%u:spi_dma_fb%u:refsLED1..LED6,ec11:gpio%d:count%u:order%s:transport%s:avail%u:dma_req%u:dma%u:dma_fb%u:rmt_dma_req%u:rmt_dma%u:rmt_dma_fb%u:spi_dma_req%u:spi_dma%u:spi_dma_fb%u:refsLED7..LED10+LED15..LED16+LED23..LED28,key:gpio%d:count%u:order%s:transport%s:avail%u:dma_req%u:dma%u:dma_fb%u:rmt_dma_req%u:rmt_dma%u:rmt_dma_fb%u:spi_dma_req%u:spi_dma%u:spi_dma_fb%u:refsLED11..LED14,edge:gpio%d:count%u:order%s:transport%s:avail%u:dma_req%u:dma%u:dma_fb%u:rmt_dma_req%u:rmt_dma%u:rmt_dma_fb%u:spi_dma_req%u:spi_dma%u:spi_dma_fb%u:refsLED17..LED22"
        " status_tail_guard_pixels=%u key_tail_guard_pixels=%u"
        " key_pin_contract=PWM_RGB_KEY_GPIO13 ec11_pin_contract=PWM_RGB_EC11_GPIO5 edge_pin_contract=PWM_RGB_Edge_GPIO4 gpio14_reserved=BAT_CHG_IO vdd_led_enable=always_on_assumed"
        "\n",
        (int)strip_gpios[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_counts[STATUS_LED_STRIP_STATUS],
        status_led_color_order_name(strip_orders[STATUS_LED_STRIP_STATUS]),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_STATUS],
            s_strips[STATUS_LED_STRIP_STATUS].spi_host),
        (unsigned)strip_available[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_dma_requested[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_dma[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_dma_fallback[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_rmt_dma_requested[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_rmt_dma[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_rmt_dma_fallback[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_spi_dma_requested[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_spi_dma[STATUS_LED_STRIP_STATUS],
        (unsigned)strip_spi_dma_fallback[STATUS_LED_STRIP_STATUS],
        (int)strip_gpios[STATUS_LED_STRIP_EC11],
        (unsigned)strip_counts[STATUS_LED_STRIP_EC11],
        status_led_color_order_name(strip_orders[STATUS_LED_STRIP_EC11]),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_EC11],
            s_strips[STATUS_LED_STRIP_EC11].spi_host),
        (unsigned)strip_available[STATUS_LED_STRIP_EC11],
        (unsigned)strip_dma_requested[STATUS_LED_STRIP_EC11],
        (unsigned)strip_dma[STATUS_LED_STRIP_EC11],
        (unsigned)strip_dma_fallback[STATUS_LED_STRIP_EC11],
        (unsigned)strip_rmt_dma_requested[STATUS_LED_STRIP_EC11],
        (unsigned)strip_rmt_dma[STATUS_LED_STRIP_EC11],
        (unsigned)strip_rmt_dma_fallback[STATUS_LED_STRIP_EC11],
        (unsigned)strip_spi_dma_requested[STATUS_LED_STRIP_EC11],
        (unsigned)strip_spi_dma[STATUS_LED_STRIP_EC11],
        (unsigned)strip_spi_dma_fallback[STATUS_LED_STRIP_EC11],
        (int)strip_gpios[STATUS_LED_STRIP_KEY],
        (unsigned)strip_counts[STATUS_LED_STRIP_KEY],
        status_led_color_order_name(strip_orders[STATUS_LED_STRIP_KEY]),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_KEY],
            s_strips[STATUS_LED_STRIP_KEY].spi_host),
        (unsigned)strip_available[STATUS_LED_STRIP_KEY],
        (unsigned)strip_dma_requested[STATUS_LED_STRIP_KEY],
        (unsigned)strip_dma[STATUS_LED_STRIP_KEY],
        (unsigned)strip_dma_fallback[STATUS_LED_STRIP_KEY],
        (unsigned)strip_rmt_dma_requested[STATUS_LED_STRIP_KEY],
        (unsigned)strip_rmt_dma[STATUS_LED_STRIP_KEY],
        (unsigned)strip_rmt_dma_fallback[STATUS_LED_STRIP_KEY],
        (unsigned)strip_spi_dma_requested[STATUS_LED_STRIP_KEY],
        (unsigned)strip_spi_dma[STATUS_LED_STRIP_KEY],
        (unsigned)strip_spi_dma_fallback[STATUS_LED_STRIP_KEY],
        (int)strip_gpios[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_counts[STATUS_LED_STRIP_EDGE],
        status_led_color_order_name(strip_orders[STATUS_LED_STRIP_EDGE]),
        status_led_strip_transport_name(
            strip_transport_actual[STATUS_LED_STRIP_EDGE],
            s_strips[STATUS_LED_STRIP_EDGE].spi_host),
        (unsigned)strip_available[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_dma_requested[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_dma[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_dma_fallback[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_rmt_dma_requested[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_rmt_dma[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_rmt_dma_fallback[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_spi_dma_requested[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_spi_dma[STATUS_LED_STRIP_EDGE],
        (unsigned)strip_spi_dma_fallback[STATUS_LED_STRIP_EDGE],
        (unsigned)status_tail_guard_pixels,
        (unsigned)key_tail_guard_pixels);
    status_led_feed_after_print();
    printf(
        "~LED:STATUS detail=state ble=%s rec_active=%u rec_source=%s rec_level=%u rec_level_visual=%u rec_level_hold_ms_left=%" PRIu32 " processing=%u"
        " ota_active=%u ota_progress_percent=%u ota_bytes=%u ota_expected=%u"
        " ble_repair_ms_left=%" PRIu32 " ble_repair_cue_ms_left=%" PRIu32
        " error_domain=%s error_severity=%s output_disabled=%u low_power_disabled=%u"
        " idle_transition_clear_pending=%u transition_clear_mask=0x%02x"
        " preview_suppress_accents=%u preview_effect_only=%u preview_ble_override_ms_left=%" PRIu32
        " shutdown_confirm_active=%u shutdown_confirm_final=%u shutdown_confirm_latched=%u shutdown_confirm_elapsed_ms=%" PRIu32 "\n",
        status_led_ble_name(snapshot.ble_state),
        snapshot.recording_active ? 1U : 0U,
        status_led_rec_source_name(snapshot.rec_source),
        snapshot.recording_level_percent,
        snapshot.recording_level_visual_percent,
        rec_level_hold_ms_left,
        snapshot.processing_active ? 1U : 0U,
        snapshot.ota_active ? 1U : 0U,
        snapshot.ota_progress_percent,
        (unsigned)snapshot.ota_bytes_written,
        (unsigned)snapshot.ota_expected_size,
        ble_repair_ms_left,
        ble_repair_cue_ms_left,
        status_led_error_domain_name(snapshot.error_domain),
        status_led_error_severity_name(snapshot.error_severity),
        snapshot.output_disabled ? 1U : 0U,
        snapshot.low_power_disabled ? 1U : 0U,
        snapshot.transition_clear_mask != 0U ? 1U : 0U,
        (unsigned)snapshot.transition_clear_mask,
        snapshot.preview_suppress_accents ? 1U : 0U,
        snapshot.preview_effect_only ? 1U : 0U,
        preview_ble_override_ms_left,
        shutdown_confirm_active ? 1U : 0U,
        snapshot.shutdown_confirm_final ? 1U : 0U,
        shutdown_confirm_latched ? 1U : 0U,
        shutdown_confirm_elapsed_ms);
    status_led_feed_after_print();
    printf(
        "~LED:STATUS detail=power battery_valid=%u battery_level=%u battery_mv=%" PRIu32
        " battery_display_valid=%u battery_display_level=%u battery_display_mv=%" PRIu32
        " battery_display_rise_suppressed=%u"
        " external_power=%u external_power_source=%s source_flags=0x%02" PRIx32
        " charging=%u full=%u raw_charging=%u raw_full=%u"
        " full_latched=%u full_candidate_ms=%" PRIu32
        " full_debounce_ms=%u full_min_mv=%u full_min_percent=%u"
        " status_window_ms_left=%" PRIu32 " ble_confidence_ms_left=%" PRIu32
        " ble_repair_ms_left=%" PRIu32 " ble_repair_cue_ms_left=%" PRIu32
        " oobe_confidence_ms_left=%" PRIu32 " preview_ble_override_ms_left=%" PRIu32
        " ble_transition_ms=%" PRIu32 " last_transition_ms=%" PRIu32
        " current_ma=%" PRIu32 " current_budget_ma=%" PRIu32 "\n",
        snapshot.battery_valid ? 1U : 0U,
        snapshot.battery_level_percent,
        snapshot.battery_mv,
        snapshot.battery_display_valid ? 1U : 0U,
        snapshot.battery_display_level_percent,
        snapshot.battery_display_mv,
        snapshot.battery_display_rise_suppressed ? 1U : 0U,
        snapshot.external_power_present ? 1U : 0U,
        status_led_external_power_source_name(snapshot.external_power_source_flags),
        snapshot.external_power_source_flags,
        snapshot.charging ? 1U : 0U,
        snapshot.full ? 1U : 0U,
        snapshot.raw_charging ? 1U : 0U,
        snapshot.raw_full ? 1U : 0U,
        snapshot.charge_full_latched ? 1U : 0U,
        charge_full_candidate_ms,
        STATUS_LED_CHARGE_FULL_DEBOUNCE_MS,
        STATUS_LED_CHARGE_FULL_MIN_MV,
        STATUS_LED_CHARGE_FULL_MIN_PERCENT,
        status_window_ms_left,
        ble_confidence_ms_left,
        ble_repair_ms_left,
        ble_repair_cue_ms_left,
        oobe_confidence_ms_left,
        preview_ble_override_ms_left,
        snapshot.ble_transition_ms,
        snapshot.last_transition_ms,
        snapshot.last_estimated_current_ma,
        snapshot.last_current_budget_ma);
    status_led_feed_after_print();
    printf(
        "~LED:STATUS detail=rgb"
        " status_rgb=PWR:%u,%u,%u;BLE:%u,%u,%u;REC:%u,%u,%u;AI:%u,%u,%u;OK:%u,%u,%u;WARN:%u,%u,%u\n",
        snapshot.last_frame.status[STATUS_LED_SEM_PWR].r,
        snapshot.last_frame.status[STATUS_LED_SEM_PWR].g,
        snapshot.last_frame.status[STATUS_LED_SEM_PWR].b,
        snapshot.last_frame.status[STATUS_LED_SEM_BLE].r,
        snapshot.last_frame.status[STATUS_LED_SEM_BLE].g,
        snapshot.last_frame.status[STATUS_LED_SEM_BLE].b,
        snapshot.last_frame.status[STATUS_LED_SEM_REC].r,
        snapshot.last_frame.status[STATUS_LED_SEM_REC].g,
        snapshot.last_frame.status[STATUS_LED_SEM_REC].b,
        snapshot.last_frame.status[STATUS_LED_SEM_AI].r,
        snapshot.last_frame.status[STATUS_LED_SEM_AI].g,
        snapshot.last_frame.status[STATUS_LED_SEM_AI].b,
        snapshot.last_frame.status[STATUS_LED_SEM_OK].r,
        snapshot.last_frame.status[STATUS_LED_SEM_OK].g,
        snapshot.last_frame.status[STATUS_LED_SEM_OK].b,
        snapshot.last_frame.status[STATUS_LED_SEM_WARN].r,
        snapshot.last_frame.status[STATUS_LED_SEM_WARN].g,
        snapshot.last_frame.status[STATUS_LED_SEM_WARN].b);
    status_led_feed_after_print();
    status_led_print_strip_rgb_line("rgb_ec11", "ec11", snapshot.last_frame.ec11, STATUS_LED_EC11_COUNT);
    status_led_print_strip_rgb_line("rgb_key", "key", snapshot.last_frame.key, STATUS_LED_KEY_COUNT);
    status_led_print_strip_rgb_line("rgb_edge", "edge", snapshot.last_frame.edge, STATUS_LED_EDGE_COUNT);
    printf(
        "~LED:STATUS profile=%s detail=summary profile_cap_percent=%u"
        " budget_scale_percent=%u budget_limited_by_current=%u"
        " ble=%s rec_active=%u rec_source=%s processing=%u ota_active=%u ota_progress_percent=%u"
        " ble_repair_ms_left=%" PRIu32 " ble_repair_cue_ms_left=%" PRIu32
        " error_domain=%s error_severity=%s battery_level=%u external_power=%u external_power_source=%s charging=%u full=%u"
        " active_flags=PWR:%u,BLE:%u,REC:%u,AI:%u,OK:%u,WARN:%u,EC11:%u,KEY:%u,EDGE:%u"
        " key_mask=0x%02x test_mode=%u test_strip_mask=0x%02x"
        " preview_suppress_accents=%u preview_effect_only=%u preview_ble_override_ms_left=%" PRIu32
        " shutdown_confirm_active=%u shutdown_confirm_latched=%u"
        " last_reason=%s\n",
        status_led_profile_name(snapshot.profile),
        profile_cap_percent,
        snapshot.last_budget_scale_percent,
        snapshot.current_limited_by_budget ? 1U : 0U,
        status_led_ble_name(snapshot.ble_state),
        snapshot.recording_active ? 1U : 0U,
        status_led_rec_source_name(snapshot.rec_source),
        snapshot.processing_active ? 1U : 0U,
        snapshot.ota_active ? 1U : 0U,
        snapshot.ota_progress_percent,
        ble_repair_ms_left,
        ble_repair_cue_ms_left,
        status_led_error_domain_name(snapshot.error_domain),
        status_led_error_severity_name(snapshot.error_severity),
        snapshot.battery_level_percent,
        snapshot.external_power_present ? 1U : 0U,
        status_led_external_power_source_name(snapshot.external_power_source_flags),
        snapshot.charging ? 1U : 0U,
        snapshot.full ? 1U : 0U,
        active_pwr,
        active_ble,
        active_rec,
        active_ai,
        active_ok,
        active_warn,
        active_ec11,
        active_key,
        active_edge,
        snapshot.key_pressed_mask,
        (unsigned)snapshot.test_mode,
        (unsigned)snapshot.test_strip_mask,
        snapshot.preview_suppress_accents ? 1U : 0U,
        snapshot.preview_effect_only ? 1U : 0U,
        preview_ble_override_ms_left,
        shutdown_confirm_active ? 1U : 0U,
        shutdown_confirm_latched ? 1U : 0U,
        snapshot.last_reason);
    fflush(stdout);
    status_led_feed_after_print();
}

static bool status_led_is_status_command(const char *command)
{
    if (command == NULL || strncmp(command, "STATUS", strlen("STATUS")) != 0) {
        return false;
    }
    const char *arg = command + strlen("STATUS");
    while (*arg == ' ') {
        arg++;
    }
    return *arg == '\0' || strncmp(arg, "detail=", strlen("detail=")) == 0;
}

static void status_led_print_budget(void)
{
    status_led_state_t snapshot;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    printf(
        "~LED:BUDGET profile=%s cap_current_ma=%" PRIu32 " estimated_current_ma=%" PRIu32
        " profile_cap_percent=%u neutral_legacy_brightness_percent=%u effective_cap_percent=%u factory_brightness_percent=%u"
        " status_zone_brightness_percent=%u key_zone_brightness_percent=%u"
        " ec11_zone_brightness_percent=%u edge_zone_brightness_percent=%u"
        " legacy_brightness_duty_255=%u"
        " budget_scale_percent=%u budget_limited_by_current=%u"
        " configured_profile_budget_ma=%" PRIu32 " factory_budget_ma=%u"
        " product_effect_profile=1 zone_brightness_is_hard_cap=1 zone_brightness_effect_peak_cap=1 zone_brightness_preserves_effect_percent=1"
        " status_rgb_energy_balance=" STATUS_LED_STATUS_RGB_ENERGY_BALANCE_CONTRACT
        " legacy_brightness_neutral=1 profile_dimming_disabled=1 off_zero_brightness=1 safety_full_brightness=1"
        " per_led_full_white_ma=60 vdd_led_enable=always_on_assumed\n",
        status_led_profile_name(snapshot.profile),
        snapshot.last_current_budget_ma,
        snapshot.last_estimated_current_ma,
        status_led_profile_cap_percent_for(snapshot.profile, false),
        snapshot.brightness_percent,
        snapshot.brightness_percent < status_led_profile_cap_percent_for(snapshot.profile, false)
            ? snapshot.brightness_percent
            : status_led_profile_cap_percent_for(snapshot.profile, false),
        STATUS_LED_FULL_BRIGHTNESS_PERCENT,
        snapshot.status_zone_brightness_percent,
        snapshot.key_zone_brightness_percent,
        snapshot.ec11_zone_brightness_percent,
        snapshot.edge_zone_brightness_percent,
        status_led_linear_percent_to_255(snapshot.brightness_percent),
        snapshot.last_budget_scale_percent,
        snapshot.current_limited_by_budget ? 1U : 0U,
        status_led_profile_budget_ma_for(snapshot.profile, false),
        STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA);
    fflush(stdout);
}

static void status_led_print_privacy(void)
{
    status_led_state_t snapshot;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    printf(
        "~LED:PRIVACY rec_allowed=%u rec_active=%u capture_source=%s rec_not_available_shows=WARN_ONLY\n",
        snapshot.recording_active && snapshot.rec_source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE ? 1U : 0U,
        snapshot.recording_active ? 1U : 0U,
        status_led_rec_source_name(snapshot.rec_source));
    fflush(stdout);
}

static void status_led_preview_state(const char *state)
{
    uint32_t now_ms = status_led_now_ms();
    bool effect_only_after = false;
    bool keep_usb_command_blocker_after = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    status_led_schedule_idle_transition_clear_locked(now_ms);
    status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
    s_state.output_disabled = false;
    s_state.low_power_disabled = false;
    s_state.test_mode = STATUS_LED_TEST_NONE;
    s_state.preview_suppress_accents = false;
    s_state.preview_effect_only = false;
    s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
    s_state.last_power_poll_ms = now_ms;
    s_state.last_transition_ms = now_ms;
    status_led_preview_clear_activity_locked();
    s_state.preview_ble_override_until_ms = now_ms + STATUS_LED_PREVIEW_BLE_OVERRIDE_MS;
    status_led_set_last_reason_locked("preview");

    if (strcasecmp(state, "ready") == 0 || strcasecmp(state, "type_ready") == 0) {
        s_state.ble_state = STATUS_LED_BLE_TYPE_READY;
        s_state.ble_transition_ms = now_ms;
        s_state.ble_confidence_until_ms = 0;
        s_state.battery_valid = true;
        s_state.battery_level_percent = 80;
        s_state.battery_mv = 4000;
        s_state.external_power_present = false;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "disconnected") == 0 ||
               strcasecmp(state, "no_host") == 0 ||
               strcasecmp(state, "no-host") == 0) {
        s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
        s_state.ble_transition_ms = now_ms;
        s_state.ble_confidence_until_ms = 0;
        s_state.oobe_confidence_until_ms = 0;
        s_state.battery_valid = false;
        s_state.battery_level_percent = 0;
        s_state.battery_mv = 0;
        s_state.external_power_present = false;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "connected") == 0) {
        s_state.ble_state = STATUS_LED_BLE_CONNECTED;
        s_state.ble_transition_ms = now_ms;
        s_state.ble_confidence_until_ms = 0;
        s_state.battery_valid = true;
        s_state.battery_level_percent = 80;
        s_state.battery_mv = 4000;
        s_state.external_power_present = false;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "pairing") == 0) {
        s_state.ble_state = STATUS_LED_BLE_PAIRING;
        s_state.ble_transition_ms = now_ms;
    } else if (strcasecmp(state, "reconnect") == 0 || strcasecmp(state, "reconnecting") == 0) {
        s_state.ble_state = STATUS_LED_BLE_RECONNECTING;
        s_state.ble_transition_ms = now_ms;
    } else if (strcasecmp(state, "repair") == 0 ||
               strcasecmp(state, "repairing") == 0 ||
               strcasecmp(state, "re_pair") == 0 ||
               strcasecmp(state, "re-pair") == 0 ||
               strcasecmp(state, "ble_repair") == 0 ||
               strcasecmp(state, "recovery") == 0) {
        status_led_start_ble_repair_locked(now_ms);
    } else if (strcasecmp(state, "capture") == 0 ||
               strcasecmp(state, "device_mic") == 0 ||
               strcasecmp(state, "recording") == 0 ||
               strcasecmp(state, "recording_active") == 0 ||
               strcasecmp(state, "capture_active") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        status_led_clear_ok_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_REC);
        keep_usb_command_blocker_after = true;
    } else if (strcasecmp(state, "capture_led_only") == 0 ||
               strcasecmp(state, "recording_led_only") == 0 ||
               strcasecmp(state, "capture_effect_only") == 0 ||
               strcasecmp(state, "recording_effect_only") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        s_state.processing_active = false;
        s_state.processing_started_ms = 0;
    } else if (strcasecmp(state, "desktop_mic") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DESKTOP_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        status_led_clear_ok_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_REC);
    } else if (strcasecmp(state, "recording_processing") == 0 ||
               strcasecmp(state, "capture_processing") == 0 ||
               strcasecmp(state, "rec_ai") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        status_led_clear_ok_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_REC);
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_AI);
        keep_usb_command_blocker_after = true;
    } else if (strcasecmp(state, "recording_processing_led_only") == 0 ||
               strcasecmp(state, "capture_processing_led_only") == 0 ||
               strcasecmp(state, "rec_ai_led_only") == 0 ||
               strcasecmp(state, "recording_processing_effect_only") == 0 ||
               strcasecmp(state, "capture_processing_effect_only") == 0 ||
               strcasecmp(state, "rec_ai_effect_only") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
    } else if (strcasecmp(state, "recording_processing_status_led_only") == 0 ||
               strcasecmp(state, "capture_processing_status_led_only") == 0 ||
               strcasecmp(state, "rec_ai_status_led_only") == 0 ||
               strcasecmp(state, "recording_processing_status_effect_only") == 0 ||
               strcasecmp(state, "capture_processing_status_effect_only") == 0 ||
               strcasecmp(state, "rec_ai_status_effect_only") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        s_state.preview_suppress_accents = true;
    } else if (strcasecmp(state, "recording_processing_status_key_stress") == 0 ||
               strcasecmp(state, "capture_processing_status_key_stress") == 0 ||
               strcasecmp(state, "rec_ai_status_key_stress") == 0 ||
               strcasecmp(state, "status_key_stress") == 0 ||
               strcasecmp(state, "status_key_stress3") == 0 ||
               strcasecmp(state, "status_key_stress4") == 0 ||
               strcasecmp(state, "status_key_stress34") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 100U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = now_ms + STATUS_LED_RECORDING_LEVEL_HOLD_MAX_MS;
        s_state.recording_level_visual_percent = 100U;
        s_state.recording_level_visual_updated_ms = now_ms;
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        s_state.preview_suppress_accents = true;
        s_state.test_mode = STATUS_LED_TEST_STATUS_KEY_STRESS;
        s_state.test_strip_mask = STATUS_LED_STRIP_MASK_STATUS | STATUS_LED_STRIP_MASK_KEY;
        s_state.test_started_ms = now_ms;
        if (strcasecmp(state, "status_key_stress3") == 0) {
            s_state.test_pixel_index = 3U;
        } else if (strcasecmp(state, "status_key_stress4") == 0) {
            s_state.test_pixel_index = 4U;
        } else if (strcasecmp(state, "status_key_stress34") == 0) {
            s_state.test_pixel_index = 34U;
        } else {
            s_state.test_pixel_index = 0U;
        }
    } else if (strcasecmp(state, "recording_processing_status_only") == 0 ||
               strcasecmp(state, "capture_processing_status_only") == 0 ||
               strcasecmp(state, "rec_ai_status_only") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        s_state.preview_suppress_accents = true;
        status_led_clear_ok_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_REC);
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_AI);
        keep_usb_command_blocker_after = true;
    } else if (strcasecmp(state, "recording_processing_static_status_only") == 0 ||
               strcasecmp(state, "rec_ai_static_status_only") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = now_ms;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(now_ms);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        s_state.preview_suppress_accents = true;
        s_state.test_mode = STATUS_LED_TEST_STATIC_STATUS;
        s_state.test_strip_mask = STATUS_LED_STRIP_MASK_STATUS;
        s_state.test_started_ms = now_ms;
        status_led_clear_ok_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_REC);
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_AI);
        keep_usb_command_blocker_after = true;
    } else if (strcasecmp(state, "rec_not_available") == 0 || strcasecmp(state, "not_available") == 0) {
        s_state.recording_active = false;
        s_state.rec_source = STATUS_LED_REC_SOURCE_NOT_AVAILABLE;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = 0U;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(0U);
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_REC;
        s_state.error_severity = STATUS_LED_ERROR_RETRYABLE;
        s_state.error_started_ms = now_ms;
        s_state.error_until_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
    } else if (strcasecmp(state, "ota") == 0 ||
               strcasecmp(state, "ota_progress") == 0 ||
               strcasecmp(state, "ota_active") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.ota_active = true;
        s_state.ota_started_ms = now_ms;
        s_state.ota_bytes_written = 512U;
        s_state.ota_expected_size = 1024U;
        status_led_clear_ok_locked();
        status_led_clear_key_feedback_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_OTA);
        keep_usb_command_blocker_after = true;
    } else if (strcasecmp(state, "ota_led_only") == 0 ||
               strcasecmp(state, "ota_effect_only") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.ota_active = true;
        s_state.ota_started_ms = now_ms;
        s_state.ota_bytes_written = 512U;
        s_state.ota_expected_size = 1024U;
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_OTA);
        keep_usb_command_blocker_after = true;
    } else if (strcasecmp(state, "processing") == 0 || strcasecmp(state, "thinking") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        status_led_clear_ok_locked();
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_AI);
        status_led_clear_retryable_error_locked(STATUS_LED_ERROR_DOMAIN_OTA);
    } else if (strcasecmp(state, "processing_led_only") == 0 ||
               strcasecmp(state, "thinking_led_only") == 0 ||
               strcasecmp(state, "ai_led_only") == 0 ||
               strcasecmp(state, "processing_effect_only") == 0 ||
               strcasecmp(state, "thinking_effect_only") == 0 ||
               strcasecmp(state, "ai_effect_only") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.recording_active = false;
        s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = 0U;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(0U);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
    } else if (strcasecmp(state, "processing_status_led_only") == 0 ||
               strcasecmp(state, "thinking_status_led_only") == 0 ||
               strcasecmp(state, "ai_status_led_only") == 0 ||
               strcasecmp(state, "processing_status_effect_only") == 0 ||
               strcasecmp(state, "thinking_status_effect_only") == 0 ||
               strcasecmp(state, "ai_status_effect_only") == 0) {
        status_led_preview_effect_only_baseline_locked();
        s_state.recording_active = false;
        s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = 0U;
        s_state.recording_level_hold_until_ms = 0U;
        status_led_reset_recording_level_visual_locked(0U);
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
        s_state.preview_suppress_accents = true;
    } else if (strcasecmp(state, "ok") == 0 || strcasecmp(state, "success") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.ok_started_ms = now_ms;
        s_state.ok_until_ms = now_ms + STATUS_LED_OK_TOTAL_MS;
        s_state.ok_warning = false;
    } else if (strcasecmp(state, "warn") == 0 ||
               strcasecmp(state, "warning") == 0 ||
               strcasecmp(state, "fail") == 0 ||
               strcasecmp(state, "failed") == 0) {
        status_led_preview_ready_baseline_locked(now_ms);
        s_state.ok_started_ms = now_ms;
        s_state.ok_until_ms = now_ms + STATUS_LED_OK_TOTAL_MS;
        s_state.ok_warning = true;
    } else if (strcasecmp(state, "shutdown_confirm") == 0 ||
               strcasecmp(state, "power_hold") == 0 ||
               strcasecmp(state, "poweroff_confirm") == 0) {
        s_state.shutdown_confirm_started_ms = now_ms;
        s_state.shutdown_confirm_until_ms = now_ms + STATUS_LED_SHUTDOWN_CONFIRM_MS;
        s_state.shutdown_confirm_final = false;
        s_state.shutdown_final_all_zone_latched_started_ms = 0U;
    } else if (strcasecmp(state, "shutdown_final") == 0 ||
               strcasecmp(state, "poweroff_final") == 0) {
        s_state.shutdown_confirm_started_ms = now_ms;
        s_state.shutdown_confirm_until_ms = now_ms + STATUS_LED_SHUTDOWN_FINAL_CONFIRM_MS;
        s_state.shutdown_confirm_final = true;
        s_state.shutdown_final_all_zone_latched_started_ms = 0U;
    } else if (strcasecmp(state, "low_battery") == 0) {
        s_state.battery_valid = true;
        s_state.battery_level_percent = 15;
        s_state.battery_mv = 3500;
        s_state.external_power_present = false;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "critical_battery") == 0) {
        s_state.battery_valid = true;
        s_state.battery_level_percent = 5;
        s_state.battery_mv = 3300;
        s_state.external_power_present = false;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "charging") == 0) {
        s_state.external_power_present = true;
        s_state.external_power_source_flags = STATUS_LED_POWER_SOURCE_CHARGER_STATUS;
        s_state.charging = true;
        s_state.full = false;
        s_state.raw_charging = true;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charger_status_external_until_ms =
            now_ms + STATUS_LED_CHARGER_STATUS_EXTERNAL_HOLD_MS;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "full") == 0) {
        s_state.external_power_present = true;
        s_state.external_power_source_flags = STATUS_LED_POWER_SOURCE_CHARGER_STATUS;
        s_state.charging = false;
        s_state.full = true;
        s_state.raw_charging = false;
        s_state.raw_full = true;
        s_state.charge_full_latched = true;
        s_state.charger_status_external_until_ms =
            now_ms + STATUS_LED_CHARGER_STATUS_EXTERNAL_HOLD_MS;
        s_state.charge_full_candidate_since_ms = now_ms;
    } else if (strcasecmp(state, "sleep") == 0) {
        s_state.low_power_disabled = true;
        s_state.output_disabled = true;
        s_state.preview_ble_override_until_ms = 0;
        status_led_clear_ec11_feedback_locked();
    } else if (strcasecmp(state, "clear") == 0 || strcasecmp(state, "off") == 0) {
        const bool preview_off = strcasecmp(state, "off") == 0;
        s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
        s_state.ble_transition_ms = 0;
        s_state.ble_confidence_until_ms = 0;
        s_state.ble_repair_until_ms = 0;
        s_state.ble_repair_cue_started_ms = 0U;
        s_state.ble_repair_cue_until_ms = 0U;
        s_state.preview_ble_override_until_ms = 0;
        s_state.oobe_confidence_until_ms = 0;
        s_state.recording_active = false;
        s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
        s_state.processing_active = false;
        status_led_clear_ota_locked();
        s_state.battery_valid = false;
        s_state.battery_level_percent = 0;
        s_state.battery_mv = 0;
        s_state.external_power_present = false;
        s_state.external_power_source_flags = 0;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
        s_state.error_until_ms = 0;
        s_state.shutdown_confirm_started_ms = 0;
        s_state.shutdown_confirm_until_ms = 0;
        s_state.shutdown_confirm_final = false;
        s_state.shutdown_final_all_zone_latched_started_ms = 0U;
        s_state.preview_suppress_accents = false;
        s_state.preview_effect_only = false;
        status_led_clear_ok_locked();
        status_led_clear_ec11_feedback_locked();
        if (preview_off) {
            s_state.output_disabled = true;
            s_state.status_window_until_ms = 0;
        }
    } else {
        ESP_LOGW(TAG, "LED preview unknown state: %s", state);
    }
    (void)status_led_update_battery_display_locked(
        s_state.external_power_present,
        s_state.battery_valid,
        s_state.battery_mv,
        s_state.battery_level_percent);
    s_state.last_transition_ms = now_ms;
    effect_only_after = s_state.preview_effect_only || keep_usb_command_blocker_after;
    xSemaphoreGive(s_mutex);
    power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, effect_only_after);
    status_led_request_refresh();
}

bool status_led_consume_usb_command(const char *line)
{
    const char *command = status_led_strip_prefix(line);
    if (command == NULL) {
        return false;
    }

    if (status_led_is_status_command(command)) {
        status_led_print_status();
        return true;
    }
    if (strcmp(command, "BUDGET") == 0) {
        status_led_print_budget();
        return true;
    }
    if (strcmp(command, "PRIVACY") == 0) {
        status_led_print_privacy();
        return true;
    }

    power_manager_record_activity("usb_led_command");

    if (strcmp(command, "OFF") == 0) {
        status_led_force_manual_off();
        ESP_LOGI(TAG, "LED output manually forced off until the next status/key event");
        return true;
    }
    if (strcmp(command, "WAKE") == 0) {
        status_led_show_status_window("usb_wake");
        ESP_LOGI(TAG, "LED status window requested");
        return true;
    }

    if (strncmp(command, "BRIGHTNESS ", strlen("BRIGHTNESS ")) == 0) {
        const char *arg = command + strlen("BRIGHTNESS ");
        char *end = NULL;
        long value = strtol(arg, &end, 10);
        char *parsed_end = end;
        while (end != NULL && *end == ' ') {
            end++;
        }
        if (parsed_end == arg || end == NULL || *end != '\0' || value < 0 || value > 100) {
            ESP_LOGW(TAG, "LED brightness must be 0..100: %s", arg);
            return true;
        }
        uint8_t brightness = (uint8_t)value;
        status_led_profile_t profile = STATUS_LED_PROFILE_STANDARD;
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.brightness_percent = STATUS_LED_FULL_BRIGHTNESS_PERCENT;
            s_state.status_zone_brightness_percent = brightness;
            s_state.key_zone_brightness_percent = brightness;
            s_state.ec11_zone_brightness_percent = brightness;
            s_state.edge_zone_brightness_percent = brightness;
            profile = s_state.profile;
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.status_window_until_ms = status_led_now_ms() + STATUS_LED_STATUS_WINDOW_MS;
            status_led_set_last_reason_locked("brightness");
            xSemaphoreGive(s_mutex);
        }
        (void)device_settings_set_brightness_profiles(brightness, brightness);
        status_led_request_refresh();
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_PROFILE, DIAG_SEV_INFO,
                 (uint32_t)profile, brightness, 1, 0);
        ESP_LOGI(TAG, "legacy LED brightness mapped to all zone brightness=%u", (unsigned)brightness);
        return true;
    }

    if (strncmp(command, "PROFILE ", strlen("PROFILE ")) == 0) {
        status_led_profile_t profile;
        const char *arg = command + strlen("PROFILE ");
        if (!status_led_parse_profile(arg, &profile)) {
            ESP_LOGW(TAG, "LED profile unknown: %s", arg);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.profile = profile;
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.status_window_until_ms = status_led_now_ms() + STATUS_LED_STATUS_WINDOW_MS;
            status_led_set_last_reason_locked("profile");
            xSemaphoreGive(s_mutex);
        }
        status_led_save_profile(profile);
        status_led_request_refresh();
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_PROFILE, DIAG_SEV_INFO, (uint32_t)profile, 0, 0, 0);
        ESP_LOGI(TAG, "LED profile=%s", status_led_profile_name(profile));
        return true;
    }

    if (strncmp(command, "TEST:RGBW", strlen("TEST:RGBW")) == 0) {
        const char *arg = command + strlen("TEST:RGBW");
        while (*arg == ' ') {
            arg++;
        }
        uint8_t mask = status_led_parse_strip_mask(arg);
        if (mask == 0) {
            ESP_LOGW(TAG, "LED TEST:RGBW unknown strip: %s", arg);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.test_mode = STATUS_LED_TEST_RGBW;
            s_state.test_strip_mask = mask;
            s_state.test_started_ms = status_led_now_ms();
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.preview_suppress_accents = false;
            s_state.preview_effect_only = false;
            status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
            status_led_set_last_reason_locked("test_rgbw");
            xSemaphoreGive(s_mutex);
        }
        status_led_request_refresh();
        ESP_LOGI(TAG, "LED RGBW calibration running mask=0x%02x", mask);
        return true;
    }

    if (strncmp(command, "TEST:MAP", strlen("TEST:MAP")) == 0) {
        const char *arg = command + strlen("TEST:MAP");
        while (*arg == ' ') {
            arg++;
        }
        uint8_t mask = status_led_parse_strip_mask(arg);
        if (mask == 0) {
            ESP_LOGW(TAG, "LED TEST:MAP unknown strip: %s", arg);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.test_mode = STATUS_LED_TEST_MAP;
            s_state.test_strip_mask = mask;
            s_state.test_started_ms = status_led_now_ms();
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.preview_suppress_accents = false;
            s_state.preview_effect_only = false;
            status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
            status_led_set_last_reason_locked("test_map");
            xSemaphoreGive(s_mutex);
        }
        status_led_request_refresh();
        ESP_LOGI(TAG, "LED map test running mask=0x%02x status_order=PWR,BLE,REC,AI,OK,WARN ec11_order=LED7..LED10+LED15..LED16+LED23..LED28 key_order=KEY1,KEY2,KEY3,KEY4 edge_order=LED17..LED22", mask);
        return true;
    }

    if (strncmp(command, "CHASE", strlen("CHASE")) == 0) {
        char strip_text[48] = "status,key";
        unsigned step_ms = STATUS_LED_CHASE_DEFAULT_STEP_MS;
        const char *arg = command + strlen("CHASE");
        while (*arg == ' ') {
            arg++;
        }
        if (*arg != '\0') {
            (void)sscanf(arg, "%47s %u", strip_text, &step_ms);
        }
        if (step_ms < 80U) {
            step_ms = 80U;
        } else if (step_ms > 2000U) {
            step_ms = 2000U;
        }
        uint8_t mask = status_led_parse_strip_mask(strip_text);
        if (mask == 0) {
            ESP_LOGW(TAG, "LED CHASE unknown strip: %s", strip_text);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.test_mode = STATUS_LED_TEST_CHASE;
            s_state.test_strip_mask = mask;
            s_state.test_started_ms = status_led_now_ms();
            s_state.test_step_ms = (uint16_t)step_ms;
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.preview_suppress_accents = false;
            s_state.preview_effect_only = false;
            status_led_force_transition_clear_locked(STATUS_LED_TRANSITION_CLEAR_ALL_STRIPS);
            status_led_set_last_reason_locked("test_chase");
            xSemaphoreGive(s_mutex);
        }
        status_led_request_refresh();
        ESP_LOGI(TAG, "LED chase running mask=0x%02x step_ms=%u full_brightness=1", mask, step_ms);
        return true;
    }

    if (strncmp(command, "TEST:PIXEL ", strlen("TEST:PIXEL ")) == 0) {
        char strip_text[16] = {0};
        char led_text[16] = {0};
        char color_text[16] = {0};
        unsigned percent = 25U;
        int fields = sscanf(
            command + strlen("TEST:PIXEL "),
            "%15s %15s %15s %u",
            strip_text,
            led_text,
            color_text,
            &percent);
        if (fields < 3) {
            ESP_LOGW(TAG, "LED TEST:PIXEL requires <status|ec11|knob|ring|key|edge> <LEDn|index> <red|green|blue|white|off> [percent]");
            return true;
        }

        status_led_strip_id_t strip;
        uint8_t count = 0;
        uint8_t first_led = 0;
        uint8_t index = 0;
        status_led_rgb_t color = {0};
        if (!status_led_parse_calibration_strip(strip_text, &strip, &count, &first_led) ||
            !status_led_parse_pixel_index(led_text, first_led, count, &index) ||
            !status_led_parse_color_token(color_text, &color) ||
            percent > 100U) {
            ESP_LOGW(TAG, "LED TEST:PIXEL invalid args: strip=%s led=%s color=%s percent=%u", strip_text, led_text, color_text, percent);
            return true;
        }

        if (strcasecmp(color_text, "off") == 0 || strcasecmp(color_text, "black") == 0) {
            percent = 0U;
        }
        status_led_run_pixel_test(strip, index, color, (uint8_t)percent);
        bool status_key_only = strip == STATUS_LED_STRIP_STATUS || strip == STATUS_LED_STRIP_KEY;
        ESP_LOGI(
            TAG,
            "LED pixel calibration strip=%s led=LED%u index=%u color=%s percent=%u status_key_only=%u ec11_edge_touched=%u ec11_edge_untouched=%u mapping_contract=%s",
            strip_text,
            (unsigned)(first_led + index),
            (unsigned)index,
            color_text,
            percent,
            status_key_only ? 1U : 0U,
            status_key_only ? 0U : 1U,
            status_key_only ? 1U : 0U,
            STATUS_LED_STATUS_KEY_MAPPING_CONTRACT);
        return true;
    }

    if (strncmp(command, "PREVIEW ", strlen("PREVIEW ")) == 0) {
        status_led_preview_state(command + strlen("PREVIEW "));
        ESP_LOGI(TAG, "LED preview=%s", command + strlen("PREVIEW "));
        return true;
    }

    if (strncmp(command, "REC_LEVEL ", strlen("REC_LEVEL ")) == 0) {
        unsigned level = 0U;
        unsigned hold_ms = STATUS_LED_RECORDING_LEVEL_STALE_MS;
        int fields = sscanf(command + strlen("REC_LEVEL "), "%u %u", &level, &hold_ms);
        if (fields < 1 || level > 100U) {
            ESP_LOGW(TAG, "LED REC_LEVEL requires <0-100> [hold_ms]");
            return true;
        }
        status_led_force_recording_level_for_review((uint8_t)level, (uint32_t)hold_ms);
        ESP_LOGI(TAG, "LED recording level review level=%u hold_ms=%u", level, hold_ms);
        return true;
    }

    if (strncmp(command, "ERROR ", strlen("ERROR ")) == 0) {
        char domain_text[16] = {0};
        char severity_text[16] = {0};
        if (sscanf(command + strlen("ERROR "), "%15s %15s", domain_text, severity_text) != 2) {
            ESP_LOGW(TAG, "LED ERROR requires domain severity");
            return true;
        }
        status_led_error_domain_t domain;
        status_led_error_severity_t severity;
        if (!status_led_parse_domain(domain_text, &domain) ||
            !status_led_parse_severity(severity_text, &severity)) {
            ESP_LOGW(TAG, "LED ERROR unknown domain/severity: %s %s", domain_text, severity_text);
            return true;
        }
        status_led_set_error(domain, severity, "usb_error_preview");
        return true;
    }

    ESP_LOGW(TAG, "LED unknown command: %s", command);
    return true;
}
