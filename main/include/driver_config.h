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
#define LIGHT_STATE CONFIG_LIGHT_STATE_GPIO
/**
 * @brief GPIO number for button used to trigger IR transmission.
 */
#define IR_BUTTON CONFIG_BUTTON_GPIO


/**
 * @brief GPIO number for relay controlling the device.
 */
#define RELAY_GPIO CONFIG_RELAY_GPIO

/**
 * @brief Initialize device peripherals (e.g., GPIOs, LEDs, buttons).
 */
void app_driver_init();

/**
 * @brief Set the light state using a specific GPIO.
 *
 * @param gpio_num GPIO number controlling the light state.
 */
void set_light_state(gpio_num_t gpio_num);

void set_relay_state();

#ifdef __cplusplus
}
#endif