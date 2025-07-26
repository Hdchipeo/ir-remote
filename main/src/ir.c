/* C includes */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_encoder.h"
#include "ir_config.h"
#include "driver_config.h"
#include "ir_storage.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_system.h"
#include "espnow_config.h"

static const char *TAG = "Driver_IR_learn";

rmt_channel_handle_t tx_channel = NULL;
rmt_encoder_handle_t raw_encoder = NULL; /**< IR learn handle */
extern QueueHandle_t ir_trans_queue;     /**< Queue to handle IR transmit events */
extern QueueHandle_t ir_learn_queue;     /**< Queue to handle IR learn events */

void listener_ir()
{
    ir_event_cmd_t ir_event = {
        .event = IR_EVENT_LEARN_NORMAL};
    xQueueSend(ir_learn_queue, &ir_event, portMAX_DELAY);
}

static bool compare_ir_symbols(const rmt_symbol_word_t *a, size_t a_num,
                               const rmt_symbol_word_t *b, size_t b_num)
{
    if (a_num != b_num)
    {
        return false;
    }

    for (size_t i = 0; i < a_num; ++i)
    {
        int diff0 = abs((int)a[i].duration0 - (int)b[i].duration0);
        int diff1 = abs((int)a[i].duration1 - (int)b[i].duration1);
        if (diff0 > IR_TOLERANCE_US || diff1 > IR_TOLERANCE_US)
        {
            return false;
        }
    }

    return true;
}

static int count_sub_nodes(const struct ir_learn_sub_list_head *head)
{
    int count = 0;
    struct ir_learn_sub_list_t *item;
    SLIST_FOREACH(item, head, next)
    {
        count++;
    }
    return count;
}

bool match_ir_with_key(const struct ir_learn_sub_list_head *data_learn, const char *key, char *matched_key_out)
{
    if (!key || strlen(key) == 0) {
        ESP_LOGE("IR_MATCH", "Invalid key");
        return false;
    }

    struct ir_learn_sub_list_head temp_list;
    SLIST_INIT(&temp_list);

    ir_learn_load(&temp_list, key);
    ESP_LOGI("IR_MATCH", "Checking key: %s", key);

    if (SLIST_EMPTY(&temp_list)) {
        ir_learn_clean_sub_data(&temp_list);
        return false;
    }

    struct ir_learn_sub_list_t *sub_a = SLIST_FIRST(data_learn);
    struct ir_learn_sub_list_t *sub_b = SLIST_FIRST(&temp_list);
    bool all_matched = true;

    while (sub_a && sub_b)
    {
        if (!compare_ir_symbols(sub_a->symbols.received_symbols, sub_a->symbols.num_symbols,
                                sub_b->symbols.received_symbols, sub_b->symbols.num_symbols))
        {
            ESP_LOGI("IR_MATCH", "Mismatch with key: %s", key);
            all_matched = false;
            break;
        }
        sub_a = SLIST_NEXT(sub_a, next);
        sub_b = SLIST_NEXT(sub_b, next);
    }

    if (sub_a != NULL || sub_b != NULL)
    {
        all_matched = false;
    }

    ir_learn_clean_sub_data(&temp_list);

    if (all_matched && matched_key_out)
    {
        strncpy(matched_key_out, key, 32);
    }

    return all_matched;
}

bool match_ir_from_spiffs(const struct ir_learn_sub_list_head *data_learn, char *matched_key_out)
{
    DIR *dir = opendir("/spiffs");
    if (!dir)
    {
        ESP_LOGE("IR_MATCH", "Failed to open /spiffs directory");
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strstr(entry->d_name, ".ir"))
        {
            char key[32];
            snprintf(key, sizeof(key), "%.*s", strcspn(entry->d_name, "."), entry->d_name);

            struct ir_learn_sub_list_head temp_list;
            SLIST_INIT(&temp_list);
            ir_learn_load(&temp_list, key);
            ESP_LOGI("IR_MATCH", "Checking key: %s", key);
            if (SLIST_EMPTY(&temp_list))
            {
                ir_learn_clean_sub_data(&temp_list);
                continue;
            }

            struct ir_learn_sub_list_t *sub_a, *sub_b;
            bool all_matched = true;

            sub_a = SLIST_FIRST(data_learn);
            sub_b = SLIST_FIRST(&temp_list);

            while (sub_a && sub_b)
            {
                if (!compare_ir_symbols(sub_a->symbols.received_symbols, sub_a->symbols.num_symbols,
                                        sub_b->symbols.received_symbols, sub_b->symbols.num_symbols))
                {
                    ESP_LOGI("IR_MATCH", "Mismatch in key: %s", key);
                    all_matched = false;
                    break;
                }
                sub_a = SLIST_NEXT(sub_a, next);
                sub_b = SLIST_NEXT(sub_b, next);
            }

            if (sub_a != NULL || sub_b != NULL)
            {
                all_matched = false;
            }

            ir_learn_clean_sub_data(&temp_list);

            if (all_matched)
            {
                if (matched_key_out)
                {
                    strncpy(matched_key_out, key, 32);
                }
                closedir(dir);
                return true;
            }
        }
    }

    closedir(dir);
    return false;
}

static esp_err_t ir_tx_init(void)
{
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 128,
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO_NUM,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &tx_channel));

    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000, // 38KHz
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));

    ir_encoder_config_t raw_encoder_cfg = {
        .resolution = IR_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(ir_encoder_new(&raw_encoder_cfg, &raw_encoder));

    return ESP_OK;
}
void rmt_tx_start(void)
{
    ir_tx_init();
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
}
void rmt_tx_stop(void)
{
    ESP_ERROR_CHECK(rmt_disable(tx_channel));
    rmt_del_channel(tx_channel);
    raw_encoder->del(raw_encoder);
}

void ir_send_raw(struct ir_learn_sub_list_head *rmt_out)
{
    struct ir_learn_sub_list_t *sub_it;

    rmt_transmit_config_t transmit_cfg = {
        .loop_count = 0, // no loop
    };
    ESP_LOGI(TAG, "Starting IR transmission...");

    SLIST_FOREACH(sub_it, rmt_out, next)
    {
        vTaskDelay(pdMS_TO_TICKS(sub_it->timediff / 1000));

        rmt_symbol_word_t *rmt_symbols = sub_it->symbols.received_symbols;
        size_t symbol_num = sub_it->symbols.num_symbols;

        if (!rmt_symbols || symbol_num == 0)
        {
            ESP_LOGW(TAG, "Empty IR data, skipping one sub command.");
            continue;
        }

        esp_err_t err = rmt_transmit(tx_channel, raw_encoder, rmt_symbols, symbol_num, &transmit_cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
            continue;
        }
        rmt_tx_wait_all_done(tx_channel, -1);
    }
    ESP_LOGI(TAG, "IR transmission completed");
}

void ir_send_step(const char *key_name)
{
    struct ir_learn_sub_list_head load_data;
    float loaded_list[IR_STEP_COUNT_MAX] = {0};
    char key_name_load[IR_KEY_MAX_LEN] = {0};
    size_t count = 0;
    load_step_timediff_from_file(key_name, loaded_list, &count);
    if (count == 0)
    {
        ESP_LOGW(TAG, "No delay data found for key: %s", key_name);
        return;
    }

    for (size_t i = 0; i < count; i++)
    {
        snprintf(key_name_load, IR_KEY_MAX_LEN, "%s_step%d", key_name, i + 1);
        esp_err_t ret = ir_learn_load(&load_data, key_name_load);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to load IR data from: %s", key_name_load);
            continue;
        }
        ir_send_raw(&load_data);
        ESP_LOGI(TAG, "Sent step %d for key: %s", i + 1, key_name_load);
        vTaskDelay(pdMS_TO_TICKS(loaded_list[i] * 1000));
    }
    ESP_LOGI(TAG, "All steps sent for key: %s", key_name);
}

void ir_send_command(const char *key_name)
{
    ESP_LOGI(TAG, "IR command sent for key: %s", key_name);
    ir_event_cmd_t IR_cmd = {
        .event = IR_EVENT_TRANSMIT};
    snprintf(IR_cmd.key, IR_KEY_MAX_LEN, "%s", key_name);
    xQueueSend(ir_trans_queue, &IR_cmd, portMAX_DELAY);

    send_data_to_screen(key_name, "normal");
}

void ir_white_screen(void)
{
    ESP_LOGI(TAG, "IR white screen command sent");
    ir_event_cmd_t IR_cmd = {
        .event = IR_EVENT_SEND_STEP};
    snprintf(IR_cmd.key_name_step, IR_KEY_MAX_LEN, "white");
    xQueueSend(ir_trans_queue, &IR_cmd, portMAX_DELAY);

    send_data_to_screen(WHITE_SCREEN_CMD, "step");
}

void ir_reset_screen(void)
{
    ESP_LOGI(TAG, "IR reset screen command sent");
    ir_event_cmd_t IR_cmd = {
        .event = IR_EVENT_SEND_STEP};
    snprintf(IR_cmd.key_name_step, IR_KEY_MAX_LEN, "reset");
    xQueueSend(ir_trans_queue, &IR_cmd, portMAX_DELAY);

    send_data_to_screen(RESET_SCREEN_CMD, "step");
}

void ir_learn_single(const char *key_name)
{
    ESP_LOGI(TAG, "IR learn for key: %s", key_name);
    ir_event_cmd_t ir_event = {
        .event = IR_EVENT_LEARN_NORMAL};
    snprintf(ir_event.key, IR_KEY_MAX_LEN, "%s", key_name);
    xQueueSend(ir_learn_queue, &ir_event, portMAX_DELAY);
}
void ir_learn_step(const char *key_name_step)
{
    ESP_LOGI(TAG, "IR learn step for key: %s", key_name_step);
    ir_event_cmd_t ir_event = {
        .event = IR_EVENT_LEARN_STEP};
    snprintf(ir_event.key_name_step, IR_KEY_MAX_LEN, "%s", key_name_step);
    xQueueSend(ir_learn_queue, &ir_event, portMAX_DELAY);
}

bool ir_learn_command(const char *mode, const char *name)
{
    ESP_LOGI(TAG, "IR learn command for mode: %s", mode);
    send_data_to_screen(name, mode);
    if (strcmp(mode, "normal") == 0)
    {
        ir_learn_single(name);
    }
    else if (strcmp(mode, "step") == 0)
    {
        ir_learn_step(name);
    }
    else
    {
        ESP_LOGE(TAG, "Unknown IR learn mode: %s", mode);
        return false;
    }
    return true;
}

bool ir_save_command(const char *key_name)
{
    ESP_LOGI(TAG, "IR save command for key : %s", key_name);
    return true;
}

bool ir_delete_command(const char *key_name)
{
    ESP_LOGI(TAG, "IR delete command for key: %s", key_name);
    if (!key_name || strlen(key_name) == 0)
    {
        ESP_LOGE(TAG, "Invalid key name for deletion");
        return false;
    }

    esp_err_t err = delete_ir_key_from_spiffs(key_name);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to delete IR key from SPIFFS: %s", esp_err_to_name(err));
        return false;
    }

    return true;

}
bool ir_rename_command(const char *old_key_name, const char *new_key_name)
{
    ESP_LOGI(TAG, "IR rename command from %s to %s", old_key_name, new_key_name);
    if (!old_key_name || !new_key_name || strlen(old_key_name) == 0 || strlen(new_key_name) == 0)
    {
        ESP_LOGE(TAG, "Invalid key names for renaming");
        return false;
    }

    esp_err_t err = rename_ir_key_in_spiffs(old_key_name, new_key_name);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to rename IR key in SPIFFS: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}