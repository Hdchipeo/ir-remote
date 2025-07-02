#pragma once

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO pin used to indicate the light state.
 */
#define LIGHT_STATE GPIO_NUM_18

/**
 * @brief Initialize device peripherals (e.g., GPIOs, LEDs, buttons).
 */
void app_device_init(void);

/**
 * @brief Set the light state using a specific GPIO.
 *
 * @param gpio_num GPIO number controlling the light state.
 */
void set_light_state(gpio_num_t gpio_num);

/**
 * @brief Configure GPIO pins used for input buttons.
 */
void config_btn_gpio(void);

#ifdef __cplusplus
}
#endif