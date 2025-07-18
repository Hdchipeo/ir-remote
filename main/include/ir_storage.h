#pragma once

#include "esp_err.h"
#include "ir_learn.h"  // Make sure this contains the definition of struct ir_learn_sub_list_head
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_IR_NAMESPACE "ir-nvs-storage"
/**
 * @brief Save IR learning data to SPIFFS storage.
 * 
 * @param data_save Pointer to the destination list to store processed data
 * @param data_src Pointer to the source list that contains learned IR data
 * @param key File name to save (without ".ir" extension; it will be added automatically)
 */
void ir_learn_save(struct ir_learn_sub_list_head *data_save, struct ir_learn_sub_list_head *data_src, const char *key);

/**
 * @brief Load IR data from SPIFFS storage.
 * 
 * @param data_load Pointer to the list to load data into
 * @param key File name to load (without ".ir" extension)
 */
esp_err_t ir_learn_load(struct ir_learn_sub_list_head *data_load, const char *key);

/**
 * @brief List all IR keys stored in SPIFFS.
 */
void list_ir_keys_from_spiffs(void);

/**
 * @brief Format the entire SPIFFS partition (delete all stored files).
 */
void format_spiffs(void);

/**
 * @brief Delete a specific IR key (file) from SPIFFS.
 * 
 * @param key File name to delete (without ".ir" extension)
 * @return ESP_OK on success, or appropriate error code
 */
esp_err_t delete_ir_key_from_spiffs(const char *key);

/**
 * @brief Rename an IR key (file) in SPIFFS.
 * 
 * @param old_key Existing file name (without ".ir")
 * @param new_key New file name (without ".ir")
 * @return ESP_OK on success
 */
esp_err_t rename_ir_key_in_spiffs(const char *old_key, const char *new_key);

/**
 * @brief Initialize SPIFFS file system. Must be called before using SPIFFS.
 * 
 * @return ESP_OK on success
 */
esp_err_t spiffs_init(void);

/**
 * @brief Save device state to NVS.
 * 
 * @param state Device state to save
 * @return ESP_OK on success
 */
esp_err_t save_device_state_to_nvs(device_state_t *state);

/**
 * @brief Load device state from NVS.
 * 
 * @param state Pointer to the device state structure to fill
 * @return ESP_OK on success
 */
esp_err_t load_device_state_from_nvs(device_state_t *state);
/**
 * @brief Match IR data from SPIFFS with received IR data.
 * 
 * @param data_learn Pointer to the list of learned IR data
 * @param matched_key_out Output buffer for the matched key (if found)
 * @return true if a match is found, false otherwise
 */
bool match_ir_from_spiffs(const struct ir_learn_sub_list_head *data_learn, char *matched_key_out);

/**
 * @brief Save step timediff data to a file.
 * 
 * @param key_name Key name for the file (without ".timediff" extension)
 * @param timediff_list Pointer to the list of timediffs
 * @param count Number of timediffs in the list
 * @return ESP_OK on success, or appropriate error code
 */
esp_err_t save_step_timediff_to_file(const char *key_name, const float *timediff_list, size_t count);

/**
 * @brief Load step timediff data from a file.
 * 
 * @param key_name Key name for the file (without ".timediff" extension)
 * @param timediff_list Pointer to the buffer to fill with loaded timediffs
 * @param count_out Pointer to store the number of timediffs loaded
 * @return ESP_OK on success, or appropriate error code
 */
esp_err_t load_step_timediff_from_file(const char *key_name, float *timediff_list, size_t *count_out);

/**
 * @brief List all IR step delay files in SPIFFS.
 * 
 * This function lists all files with the ".timediff" extension in the SPIFFS partition.
 */
void list_ir_step_delay_from_spiffs(void);

#ifdef __cplusplus
}
#endif