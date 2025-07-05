#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

/* ESP32 includes */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_spiffs.h"
#include "ir_learn.h"
#include "ir_storage.h"

static const char *TAG = "IR_storage";

static esp_err_t save_ir_list_to_file(const char *key, struct ir_learn_sub_list_head *list)
{
    if (!key || !list)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s.ir", key);

    FILE *f = fopen(filepath, "wb");
    if (!f)
    {
        ESP_LOGE("IR", "Failed to open file %s for writing", filepath);
        return ESP_FAIL;
    }

    struct ir_learn_sub_list_t *sub_it;

    SLIST_FOREACH(sub_it, list, next)
    {
        uint32_t timediff = sub_it->timediff;
        uint32_t num_symbols = sub_it->symbols.num_symbols;

        fwrite(&timediff, sizeof(uint32_t), 1, f);
        fwrite(&num_symbols, sizeof(uint32_t), 1, f);
        fwrite(sub_it->symbols.received_symbols, sizeof(rmt_symbol_word_t), num_symbols, f);
    }

    fclose(f);
    ESP_LOGI("IR", "IR data saved to %s", filepath);
    return ESP_OK;
}
static esp_err_t load_ir_list_from_file(const char *key, struct ir_learn_sub_list_head *out_list)
{
    if (!key || !out_list)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s.ir", key);

    FILE *f = fopen(filepath, "rb");
    if (!f)
    {
        ESP_LOGE("IR", "Failed to open file %s for reading", filepath);
        return ESP_FAIL;
    }

    while (1)
    {
        uint32_t timediff = 0;
        uint32_t num_symbols = 0;

        size_t read_count = fread(&timediff, sizeof(uint32_t), 1, f);
        if (read_count != 1)
            break; // EOF

        fread(&num_symbols, sizeof(uint32_t), 1, f);

        size_t symbol_size = num_symbols * sizeof(rmt_symbol_word_t);
        rmt_symbol_word_t *symbols = malloc(symbol_size);
        if (!symbols)
        {
            ESP_LOGE("IR", "Out of memory");
            fclose(f);
            return ESP_ERR_NO_MEM;
        }

        fread(symbols, sizeof(rmt_symbol_word_t), num_symbols, f);

        rmt_rx_done_event_data_t symbol_data = {
            .received_symbols = symbols,
            .num_symbols = num_symbols,
        };

        ir_learn_add_sub_list_node(out_list, timediff, &symbol_data);
    }

    fclose(f);
    ESP_LOGI("IR", "IR data loaded from %s", filepath);
    return ESP_OK;
}
void ir_learn_save(struct ir_learn_sub_list_head *data_save, struct ir_learn_sub_list_head *data_src, const char *key)
{
    assert(data_src && "data_src is null");

    struct ir_learn_sub_list_t *sub_it;
    SLIST_FOREACH(sub_it, data_src, next)
    {
        ir_learn_add_sub_list_node(data_save, sub_it->timediff, &sub_it->symbols);
    }

    save_ir_list_to_file(key, data_save);
}
void ir_learn_load(struct ir_learn_sub_list_head *data_load, const char *key)
{
    esp_err_t ret = load_ir_list_from_file(key, data_load);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load IR symbols from file, ret: %s", esp_err_to_name(ret));
        return;
    }
}
void list_ir_keys_from_spiffs(void)
{
    const char *dir_path = "/spiffs";
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        ESP_LOGE("SPIFFS", "Failed to open %s", dir_path);
        return;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            const char *filename = entry->d_name;
            const char *ext = strrchr(filename, '.');

            if (ext && strcmp(ext, ".ir") == 0)
            {
                count++;
                char key[32] = {0};
                strncpy(key, filename, ext - filename);
                ESP_LOGI("SPIFFS", "IR Key: %s", key);
            }
        }
    }

    closedir(dir);
    ESP_LOGI("SPIFFS", "Total IR keys found: %d", count);
}
esp_err_t rename_ir_key_in_spiffs(const char *old_key, const char *new_key)
{
    if (!old_key || !new_key)
        return ESP_ERR_INVALID_ARG;

    char old_path[64];
    char new_path[64];

    snprintf(old_path, sizeof(old_path), "/spiffs/%s.ir", old_key);
    snprintf(new_path, sizeof(new_path), "/spiffs/%s.ir", new_key);

    FILE *fp = fopen(old_path, "rb");
    if (!fp)
    {
        ESP_LOGE("SPIFFS", "Old key file not found: %s", old_path);
        return ESP_ERR_NOT_FOUND;
    }
    fclose(fp);

    fp = fopen(new_path, "rb");
    if (fp)
    {
        fclose(fp);
        ESP_LOGE("SPIFFS", "New key already exists: %s", new_path);
        return ESP_ERR_INVALID_STATE;
    }
    int result = rename(old_path, new_path);
    if (result == 0)
    {
        ESP_LOGI("SPIFFS", "Renamed IR key from '%s' ➜ '%s'", old_key, new_key);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE("SPIFFS", "Rename failed: %s ➜ %s", old_path, new_path);
        return ESP_FAIL;
    }
}
esp_err_t delete_ir_key_from_spiffs(const char *key)
{
    if (!key)
        return ESP_ERR_INVALID_ARG;

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/spiffs/%s.ir", key);

    if (unlink(filepath) == 0)
    {
        ESP_LOGI("SPIFFS", "Deleted IR key file: %s", filepath);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE("SPIFFS", "Failed to delete: %s", filepath);
        return ESP_FAIL;
    }
}
void format_spiffs(void)
{
    const char *partition_label = NULL;

    ESP_LOGW(TAG, "Formatting SPIFFS partition...");

    esp_err_t err = esp_spiffs_format(partition_label);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to format SPIFFS (%s)", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS formatted successfully!");
    }
}
esp_err_t spiffs_init(void)
{
    esp_err_t ret = ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount or format SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total_bytes = 0, used_bytes = 0;
    ret = esp_spiffs_info(conf.partition_label, &total_bytes, &used_bytes);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS mounted successfully. Total: %d bytes, Used: %d bytes", total_bytes, used_bytes);

    return ESP_OK;
}
esp_err_t save_device_state_to_nvs(device_state_t *state)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_IR_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS (%s)", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, "device_state", state, sizeof(device_state_t));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save device state to NVS (%s)", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to commit NVS changes (%s)", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}
esp_err_t load_device_state_from_nvs(device_state_t *state)
{
    if (!state)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_IR_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS (%s)", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(device_state_t);
    err = nvs_get_blob(nvs_handle, "device_state", state, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Device state not found in NVS");
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read device state from NVS (%s)", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}