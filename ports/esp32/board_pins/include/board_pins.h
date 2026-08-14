#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8
#define CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8 1
#endif

#if !CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8
#error "This build baseline is configured for the Voice Keyboard V2 N16R8 board profile."
#endif

#define BOARD_PINS_PROFILE_ID        "voice-keyboard-v2-n16r8"
#define BOARD_PINS_MODULE            "ESP32-S3-WROOM-1-N16R8"
#define BOARD_PINS_FLASH_SIZE_MB     (16)
#define BOARD_PINS_PSRAM_SIZE_MB     (8)
#define BOARD_PINS_PSRAM_MODE        "octal"
#define BOARD_PINS_RESERVED_MSPI_GPIOS "GPIO35,GPIO36,GPIO37"

#define BOARD_PINS_I2S_PORT          (0)
#define BOARD_PINS_I2S_MCLK_IO       (GPIO_NUM_NC)
#define BOARD_PINS_I2S_BCLK_IO       (GPIO_NUM_48)
#define BOARD_PINS_I2S_WS_IO         (GPIO_NUM_NC)
#define BOARD_PINS_I2S_DIN_IO        (GPIO_NUM_47)
#define BOARD_PINS_I2S_DOUT_IO       (GPIO_NUM_NC)
#define BOARD_PINS_MIC_CLK_IO        BOARD_PINS_I2S_BCLK_IO
#define BOARD_PINS_MIC_DOUT_IO       BOARD_PINS_I2S_DIN_IO

#define BOARD_PINS_KEY1_IO           (GPIO_NUM_38)
#define BOARD_PINS_KEY2_IO           (GPIO_NUM_39)
#define BOARD_PINS_KEY3_IO           (GPIO_NUM_40)
#define BOARD_PINS_KEY4_IO           (GPIO_NUM_41)

#define BOARD_PINS_EC11_A_IO         (GPIO_NUM_42)
#define BOARD_PINS_EC11_B_IO         (GPIO_NUM_2)
#define BOARD_PINS_EC11_C_IO         (GPIO_NUM_NC)
#define BOARD_PINS_EC11_KEY_IO       (GPIO_NUM_18)

#define BOARD_PINS_BAT_CHG_IO        (GPIO_NUM_14)
#define BOARD_PINS_BAT_STD_IO        (GPIO_NUM_21)
#define BOARD_PINS_BAT_V_ADC_IO      (GPIO_NUM_10)
/* USB_DET divider is retired; keep physical GPIO7 high-Z and do not use it for runtime power decisions. */
#define BOARD_PINS_USB_DET_DISABLED_IO (GPIO_NUM_7)
#define BOARD_PINS_USB_DET_IO        (GPIO_NUM_NC)
#define BOARD_PINS_USB_DP_IO         (GPIO_NUM_20)
#define BOARD_PINS_USB_DN_IO         (GPIO_NUM_19)

/* Voice Keyboard V2.2 reduces the four independent WS2812 data rails to two.
   PWM_RGB/GPIO1 clocks the daisy-chained status + EC11 + key LEDs; the
   PWM_RGB_Edge/GPIO5 rail remains a separate six-pixel chain. */
#define BOARD_PINS_RGB_MAIN_IO       (GPIO_NUM_1)
#define BOARD_PINS_RGB_EDGE_IO       (GPIO_NUM_5)

/* Logical-zone aliases. These are deliberately the same physical GPIO on
   V2.2; status_led owns a single composite backend and must not initialize
   one peripheral per alias. */
#define BOARD_PINS_RGB_STATUS_IO     BOARD_PINS_RGB_MAIN_IO
#define BOARD_PINS_RGB_EC11_IO       BOARD_PINS_RGB_MAIN_IO
#define BOARD_PINS_RGB_KEY_IO        BOARD_PINS_RGB_MAIN_IO

#define BOARD_PINS_PWR_HOLD_IO       (GPIO_NUM_9)

/* Latest V2 N16R8 board revision does not populate battery-side current
   telemetry chips. Keep the diagnostic rails as not-populated so GPIO9 can be
   PWR_HOLD and GPIO10 can be BAT_V_ADC without accidental reuse. */
#define BOARD_PINS_CURRENT_TELEMETRY_PRESENT (0)
#define BOARD_PINS_TPS63020_I_ADC_IO (GPIO_NUM_NC)
#define BOARD_PINS_SY7088_I_ADC_IO   (GPIO_NUM_NC)

#ifdef __cplusplus
}
#endif

#endif
