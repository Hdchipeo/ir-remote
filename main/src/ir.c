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

static const char *TAG = "Driver_IR_learn";

rmt_channel_handle_t tx_channel = NULL;
rmt_encoder_handle_t raw_encoder = NULL;      /**< IR learn handle */

static bool compare_ir_symbols(const rmt_symbol_word_t *a, size_t a_num,
                        const rmt_symbol_word_t *b, size_t b_num)
{
    if (a_num != b_num) {
        return false;
    }

    for (size_t i = 0; i < a_num; ++i) {
        int diff0 = abs((int)a[i].duration0 - (int)b[i].duration0);
        int diff1 = abs((int)a[i].duration1 - (int)b[i].duration1);
        if (diff0 > IR_TOLERANCE_US || diff1 > IR_TOLERANCE_US) {
            return false;
        }
    }

    return true;
}
static int compare_ir_symbols_percent(const rmt_symbol_word_t *a, size_t a_num,
                                      const rmt_symbol_word_t *b, size_t b_num,
                                      uint32_t tolerance)
{
    if (!a || !b || a_num == 0 || b_num == 0) return 0;

    size_t min_len = a_num < b_num ? a_num : b_num;
    size_t matched = 0;

    for (size_t i = 0; i < min_len; ++i)
    {
        int diff0 = abs((int)a[i].duration0 - (int)b[i].duration0);
        int diff1 = abs((int)a[i].duration1 - (int)b[i].duration1);
        if (diff0 <= tolerance && diff1 <= tolerance) {
            matched++;
        }
    }

    return (int)((matched * 100) / min_len);
}

static bool match_ir_from_spiffs(const struct ir_learn_sub_list_head *data_learn, char *matched_key_out)
{
    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE("IR_MATCH", "Failed to open /spiffs directory");
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".ir")) {
            char key[32];
            strncpy(key, entry->d_name, sizeof(key));
            key[strcspn(key, ".")] = '\0'; 

            struct ir_learn_sub_list_head temp_list;
            SLIST_INIT(&temp_list);

            esp_err_t ret = load_ir_list_from_file(key, &temp_list);
            if (ret != ESP_OK) {
                ir_learn_clear_list(&temp_list);
                continue;
            }

            struct ir_learn_sub_list_t *sub_a, *sub_b;
            bool all_matched = true;

            sub_a = SLIST_FIRST(data_learn);
            sub_b = SLIST_FIRST(&temp_list);

            while (sub_a && sub_b) {
                if (!compare_ir_symbols(sub_a->symbols.received_symbols, sub_a->symbols.num_symbols,
                                        sub_b->symbols.received_symbols, sub_b->symbols.num_symbols)) {
                    all_matched = false;
                    break;
                }
                sub_a = SLIST_NEXT(sub_a, next);
                sub_b = SLIST_NEXT(sub_b, next);
            }

            if (sub_a != NULL || sub_b != NULL) {
                all_matched = false;
            }

            ir_learn_clean_sub_data(&temp_list);

            if (all_matched) {
                if (matched_key_out) {
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
int match_ir_best_from_spiffs(struct ir_learn_sub_list_head *received_list,
                              char *best_key_out, size_t max_key_len)
{
    if (!received_list || !best_key_out) return 0;

    DIR *dir = opendir("/spiffs");
    if (!dir) {
        ESP_LOGE("IR", "Failed to open /spiffs");
        return 0;
    }

    struct dirent *entry;
    int best_percent = 0;
    char best_key[IR_FILENAME_MAX] = {0};

    struct ir_learn_sub_list_t *recv_it = SLIST_FIRST(received_list);
    if (!recv_it) {
        ESP_LOGW("IR", "Received list is empty");
        closedir(dir);
        return 0;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        const char *filename = entry->d_name;

        if (!strstr(filename, ".ir")) continue;

        char key[IR_FILENAME_MAX] = {0};
        strncpy(key, filename, strlen(filename) - 3); 

        struct ir_learn_sub_list_head stored_list;
        SLIST_INIT(&stored_list);
        ir_learn_load(&stored_list, key);

        struct ir_learn_sub_list_t *stored_it = SLIST_FIRST(&stored_list);
        if (!stored_it) {
            ir_learn_clean_sub_data(&stored_list);
            continue;
        }

        int percent = compare_ir_symbols_percent(
            recv_it->symbols.received_symbols, recv_it->symbols.num_symbols,
            stored_it->symbols.received_symbols, stored_it->symbols.num_symbols,
            IR_MATCH_TOLERANCE);

        ESP_LOGI("IR", "Compared with %s: %d%% match", key, percent);

        if (percent > best_percent)
        {
            best_percent = percent;
            strncpy(best_key, key, IR_FILENAME_MAX);
        }

        ir_learn_clean_sub_data(&stored_list);
    }

    closedir(dir);

    if (best_percent > 0) {
        strncpy(best_key_out, best_key, max_key_len);
        ESP_LOGI("IR", "Best match: %s (%d%%)", best_key, best_percent);
    } else {
        ESP_LOGW("IR", "No matching IR pattern found");
    }

    return best_percent;
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
static void rmt_tx_start(void)
{
    ir_tx_init();
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
}
static void rmt_tx_stop(void)
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

    rmt_tx_start();
    ESP_LOGI(TAG, "Starting IR transmission...");

    SLIST_FOREACH(sub_it, rmt_out, next)
    {
        vTaskDelay(pdMS_TO_TICKS(sub_it->timediff / 1000));

        rmt_symbol_word_t *rmt_symbols = sub_it->symbols.received_symbols;
        size_t symbol_num = sub_it->symbols.num_symbols;

        ESP_ERROR_CHECK(rmt_transmit(tx_channel, raw_encoder, rmt_symbols, symbol_num, &transmit_cfg));
        rmt_tx_wait_all_done(tx_channel, -1);
    }
    rmt_tx_stop();
    ESP_LOGI(TAG, "IR transmission completed");
}