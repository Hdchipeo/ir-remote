#pragma once

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the application console interface.
 */
void app_console_start(void);

/**
 * @brief Register IR learning related commands in the console.
 */
void register_ir_learn_commands(void);

/**
 * @brief Register IR transmission related commands in the console.
 */
void register_ir_trans_commands(void);

/**
 * @brief Register command to list saved IR keys.
 */
void register_ir_list_commands(void);

/**
 * @brief Register command to delete an IR key.
 */
void resister_ir_delete_commands(void);  

/**
 * @brief Register command to format SPIFFS storage.
 */
void register_ir_format_spiffs_commands(void);

/**
 * @brief Register command to input custom IR key name.
 */
void register_ir_input_name_commands(void);

/**
 * @brief Register command to rename an existing IR key.
 */
void register_ir_rename_commands(void);

/**
 * @brief Register commands to manage IR device states.
 */
void register_ir_device_state_commands(void);

#ifdef __cplusplus
}
#endif