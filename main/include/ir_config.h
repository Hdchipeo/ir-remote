#pragma once

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "ir_learn.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ir_config.h
 * @brief Configuration for IR learning and transmission.
 * 
 * This file contains definitions and configurations for IR signal learning,
 * transmission, and reception using ESP-IDF.
 */



// Maximum number of IR symbols that can be learned in a single command.
#define IR_TOLERANCE_US 350 // Tolerance in microseconds for IR signal duration matching

/**
 * @brief IR signal resolution in Hz.
 * 
 * 1 MHz resolution means each tick represents 1 microsecond.
 */
#define IR_RESOLUTION_HZ 1000000

/**
 * @brief GPIO number used for IR transmission.
 */
#define IR_TX_GPIO_NUM CONFIG_IR_TX_GPIO

/**
 * @brief GPIO number used for IR reception.
 */
#define IR_RX_GPIO_NUM CONFIG_IR_RX_GPIO

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

struct ir_learn_sub_list_head;

void ir_send_raw(struct ir_learn_sub_list_head *rmt_out);

#ifdef __cplusplus
}
#endif