
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

#define IR_STEP_COUNT_MAX 30

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
 * @brief Starts the RMT transmission.
 * 
 * This function initializes and starts the RMT peripheral for IR transmission.
 * It prepares the RMT channel and sets the GPIO for output.
 */
void rmt_tx_start(void);

/**
 * @brief Stops the RMT transmission.
 * 
 * This function stops the RMT peripheral and releases the resources used for IR transmission.
 */
void rmt_tx_stop(void);

 /**
  * @brief Sends an IR command based on the provided command string.
  * This function is a wrapper for sending IR commands using the RMT peripheral.
  */
void ir_send_command(const char *command);

/**
 * @brief Learns an IR command and saves it with the specified mode and name.
 * 
 * This function initiates the IR learning process for a specific mode and command name.
 * It captures the IR signal and stores it in the SPIFFS storage.
 * 
 * @param mode The mode of the IR command (e.g., "normal", "step").
 * @param name The name of the IR command to be learned.
 * @return true if learning was successful, false otherwise.
 */

bool ir_learn_command(const char *mode, const char* name);

/**
 * @brief Saves the learned IR command to storage.
 * 
 * This function saves the IR command associated with the given key name to SPIFFS.
 * It is typically called after learning a new IR command.
 * 
 * @param key_name The name of the IR command to save.
 * @return true if saving was successful, false otherwise.
 */
bool ir_save_command(const char *key_name);

/**
 * @brief Matches the learned IR data with a key name.
 * 
 * This function checks if the learned IR data matches any key in the SPIFFS storage.
 * If a match is found, it fills the matched_key_out with the corresponding key name.
 * 
 * @param data_learn Pointer to the list of learned IR symbols.
 * @param key The key name to match against the learned data.
 * @param matched_key_out Pointer to a buffer to store the matched key name.
 * @return true if a match is found, false otherwise.
 */
bool match_ir_with_key(const struct ir_learn_sub_list_head *data_learn, const char *key, char *matched_key_out);

/**
 * @brief Deletes an IR command from storage.
 * 
 * This function removes the IR command associated with the given key name from SPIFFS.
 * It is typically called when an IR command is no longer needed.
 * 
 * @param key_name The name of the IR command to delete.
 * @return true if deletion was successful, false otherwise.
 */
bool ir_delete_command(const char *key_name);

/**
 * @brief Renames an IR command in storage.
 * 
 * This function renames the IR command from old_key_name to new_key_name in SPIFFS.
 * It is typically called when an IR command needs to be renamed.
 * 
 * @param old_key_name The current name of the IR command.
 * @param new_key_name The new name for the IR command.
 * @return true if renaming was successful, false otherwise.
 */
bool ir_rename_command(const char *old_key_name, const char *new_key_name);

#ifdef __cplusplus
}
#endif