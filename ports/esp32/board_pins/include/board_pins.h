#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_PINS_I2S_PORT          (0)
#define BOARD_PINS_I2S_MCLK_IO       (GPIO_NUM_NC)
#define BOARD_PINS_I2S_BCLK_IO       (GPIO_NUM_39)
#define BOARD_PINS_I2S_WS_IO         (GPIO_NUM_40)
#define BOARD_PINS_I2S_DIN_IO        (GPIO_NUM_41)
#define BOARD_PINS_I2S_DOUT_IO       (GPIO_NUM_NC)

#define BOARD_PINS_KEY1_IO           (GPIO_NUM_45)
#define BOARD_PINS_KEY2_IO           (GPIO_NUM_48)
#define BOARD_PINS_KEY3_IO           (GPIO_NUM_47)
#define BOARD_PINS_KEY4_IO           (GPIO_NUM_21)

#define BOARD_PINS_EC11_A_IO         (GPIO_NUM_36)
#define BOARD_PINS_EC11_B_IO         (GPIO_NUM_38)
#define BOARD_PINS_EC11_C_IO         (GPIO_NUM_37)
#define BOARD_PINS_EC11_KEY_IO       (GPIO_NUM_35)

#define BOARD_PINS_BAT_CHG_IO        (GPIO_NUM_3)
#define BOARD_PINS_BAT_STD_IO        (GPIO_NUM_46)
#define BOARD_PINS_BAT_V_ADC_IO      (GPIO_NUM_7)
#define BOARD_PINS_USB_DET_IO        (GPIO_NUM_9)

#ifdef __cplusplus
}
#endif

#endif
