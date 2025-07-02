#pragma once

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IR signal resolution in Hz.
 * 
 * 1 MHz resolution means each tick represents 1 microsecond.
 */
#define IR_RESOLUTION_HZ 1000000

/**
 * @brief GPIO number used for IR transmission.
 */
#define IR_TX_GPIO_NUM GPIO_NUM_5

/**
 * @brief GPIO number used for IR reception.
 */
#define IR_RX_GPIO_NUM GPIO_NUM_4

/**
 * @brief GPIO number for button used to trigger IR transmission.
 */
#define IR_TX_BUTTON GPIO_NUM_15

/**
 * @brief Number of times to learn the IR signal before saving.
 */
#define IR_LEARN_COUNT 1

/**
 * @brief Starts the IR learning task and initializes NVS and RMT peripherals.
 * 
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t ir_task_start(void);

#ifdef __cplusplus
}
#endif