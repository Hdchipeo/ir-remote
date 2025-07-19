
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

#define IR_STEP_COUNT_MAX 3

/**
 * @brief Starts the IR learning task and initializes NVS and RMT peripherals.
 * 
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t ir_task_start(void);

struct ir_learn_sub_list_head;

/**
 * @brief Sends the learned IR command.
 * 
 * This function transmits the IR command using the RMT peripheral.
 * It retrieves the IR data from storage and sends it through the configured GPIO.
 * 
 * @param rmt_out Pointer to the list of IR symbols to be transmitted.
 */
void ir_send_raw(struct ir_learn_sub_list_head *rmt_out);

/**
 * @brief Sends a step of the IR command.
 * 
 * This function sends a single step of an IR command based on the key name.
 * It loads the timing data from storage and transmits the corresponding IR signal.
 * 
 * @param key_name The name of the IR command to send.
 */
void ir_send_step(const char *key_name);

/**
 * @brief Matches an IR command from the SPIFFS storage.
 * 
 * This function compares the learned IR symbols with those stored in SPIFFS.
 * If a match is found, it returns true and fills the matched_key_out with the key name.
 * 
 * @param data_learn Pointer to the list of learned IR symbols.
 * @param matched_key_out Pointer to a buffer to store the matched key name.
 * @return true if a match is found, false otherwise.
 */
void ir_white_screen(void);

/**
 * @brief Resets the IR screen.
 * 
 * This function sends a command to reset the IR screen.
 */
void ir_reset_screen(void);

/**
 * @brief Sends an IR command based on the key name.
 * 
 * This function sends the IR command associated with the given key name.
 * It can be used to trigger specific actions or devices that respond to IR signals.
 * 
 * @param key_name The name of the IR command to send.
 */
void ir_send_command(const char *command);

bool ir_learn_command(const char *key_name);

bool ir_save_command(const char *key_name);

#ifdef __cplusplus
}
#endif