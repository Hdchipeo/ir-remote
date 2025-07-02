/* C includes */
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

/* ESP32 includes */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_encoder.h"
#include "ir_remote.h"
#include "device.h"

//static const int DELETE_END = BIT1;

#define NVS_IR_NAMESPACE "ir-storage"

static const char *TAG = "ir_learn";
static ir_learn_handle_t handle = NULL;
rmt_channel_handle_t tx_channel = NULL;
rmt_encoder_handle_t raw_encoder = NULL;
nvs_handle_t nvs_ir_handle;              /**< IR learn handle */
static struct ir_learn_sub_list_head ir_data; /**< IR learn test result */
QueueHandle_t ir_event_queue = NULL;
QueueHandle_t ir_learn_queue = NULL;
extern bool light_flag; // Flag to control light state

static esp_err_t save_ir_list_to_nvs(const char *key, struct ir_learn_sub_list_head *list)
{
    if (!key || !list)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_size = 0;
    struct ir_learn_sub_list_t *sub_it;

    // Tính tổng kích thước cần lưu
    SLIST_FOREACH(sub_it, list, next)
    {
        size_t symbol_data_size = sizeof(rmt_symbol_word_t) * sub_it->symbols.num_symbols;
        total_size += sizeof(uint32_t) * 2 + symbol_data_size;
    }

    if (total_size == 0)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    // Cấp phát bộ nhớ đệm
    uint8_t *buffer = malloc(total_size);
    if (!buffer)
    {
        return ESP_ERR_NO_MEM;
    }

    uint8_t *p = buffer;
    SLIST_FOREACH(sub_it, list, next)
    {
        // Ghi timediff
        memcpy(p, &sub_it->timediff, sizeof(uint32_t));
        p += sizeof(uint32_t);

        // Ghi num_symbols
        memcpy(p, &sub_it->symbols.num_symbols, sizeof(uint32_t));
        p += sizeof(uint32_t);

        // Ghi symbol data
        size_t symbol_data_size = sizeof(rmt_symbol_word_t) * sub_it->symbols.num_symbols;
        memcpy(p, sub_it->symbols.received_symbols, symbol_data_size);
        p += symbol_data_size;
    }

    // Lưu vào NVS
    
    esp_err_t err = nvs_open(NVS_IR_NAMESPACE, NVS_READWRITE, &nvs_ir_handle);
    if (err != ESP_OK)
    {
        free(buffer);
        return err;
    }

    err = nvs_set_blob(nvs_ir_handle, key, buffer, total_size);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_ir_handle);
    }

    nvs_close(nvs_ir_handle);
    free(buffer);

    return err;
}
static esp_err_t load_ir_symbols_from_nvs(const char *key, struct ir_learn_sub_list_head *out_list)
{
    esp_err_t err = nvs_open(NVS_IR_NAMESPACE, NVS_READONLY, &nvs_ir_handle);
    if (err != ESP_OK)
        return err;

    size_t total_size = 0;
    err = nvs_get_blob(nvs_ir_handle, key, NULL, &total_size);
    if (err != ESP_OK)
    {
        nvs_close(nvs_ir_handle);
        return err;
    }

    uint8_t *buffer = malloc(total_size);
    if (!buffer)
    {
        nvs_close(nvs_ir_handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(nvs_ir_handle, key, buffer, &total_size);
    nvs_close(nvs_ir_handle);
    if (err != ESP_OK)
    {
        free(buffer);
        return err;
    }

    // Deserialize into list
    uint8_t *ptr = buffer;
    while (ptr < buffer + total_size)
    {
        // Read header
        uint32_t timediff = *(uint32_t *)ptr;
        ptr += sizeof(uint32_t);

        size_t symbol_num = *(size_t *)ptr;
        ptr += sizeof(size_t);

        size_t symbol_size = symbol_num * sizeof(rmt_symbol_word_t);
        rmt_symbol_word_t *symbols = malloc(symbol_size);
        if (!symbols)
        {
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
        memcpy(symbols, ptr, symbol_size);
        ptr += symbol_size;

        rmt_rx_done_event_data_t symbol_data = {
            .received_symbols = symbols,
            .num_symbols = symbol_num,
        };

        ir_learn_add_sub_list_node(out_list, timediff, &symbol_data);
    }

    free(buffer);
    return ESP_OK;
}
int ir_learn_sub_list_len(struct ir_learn_sub_list_head *list) {
    int count = 0;
    struct ir_learn_sub_list_t *it;
    SLIST_FOREACH(it, list, next) {
        count++;
    }
    return count;
}
static void ir_learn_save(struct ir_learn_sub_list_head *data_save, struct ir_learn_sub_list_head *data_src)
{
   assert(data_src && "data_src is null");

    struct ir_learn_sub_list_t *sub_it;
    SLIST_FOREACH(sub_it, data_src, next) {
        ir_learn_add_sub_list_node(data_save, sub_it->timediff, &sub_it->symbols);
    }

    save_ir_list_to_nvs("on_ac", data_save);
    ESP_LOGI(TAG, "✅ IR learn result saved to NVS (with %d entries)", ir_learn_sub_list_len(data_save));
}
static void ir_learn_load(struct ir_learn_sub_list_head *data_load)
{
    esp_err_t ret = load_ir_symbols_from_nvs("on_ac", data_load);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load IR symbols from NVS, ret: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "IR symbols loaded from NVS");
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


static void ir_send_raw(struct ir_learn_sub_list_head *rmt_out)
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
static void ir_learn_tx_task(void *arg)
{
    ir_event_t ir_event = IR_EVENT_NONE;
    ir_event_queue = xQueueCreate(5, sizeof(ir_event_t));
    ir_learn_queue = xQueueCreate(5, sizeof(ir_event_t));

    while (1)
    {
        if (xQueueReceive(ir_event_queue, &ir_event, portMAX_DELAY) == pdPASS)
        {
            if(ir_event == IR_EVENT_TRANSMIT)
            {
                ESP_LOGI(TAG, "IR transmit detected");
                ir_learn_load(&ir_data);
                ir_send_raw(&ir_data);
                ir_learn_clean_sub_data(&ir_data);
            }
            else
            {
                ESP_LOGW(TAG, "Unexpected event: %d", ir_event);
            }
        }
    }
    vTaskDelete(NULL);
}
static void ir_send_cb(ir_learn_state_t state, uint8_t sub_step, struct ir_learn_sub_list_head *data)
{
    switch (state)
    {
    case IR_LEARN_STATE_READY:
        ESP_LOGI(TAG, "IR Learn ready");
        light_flag = true; // Turn on light when ready
        break;
    case IR_LEARN_STATE_EXIT:
        ESP_LOGI(TAG, "IR Learn exit");
        break;
    case IR_LEARN_STATE_END:
        ESP_LOGI(TAG, "IR Learn end");
        ir_learn_save(&ir_data, data);
        ir_learn_print_raw(&ir_data);
        light_flag = false; // Turn off light when exiting
        break;
    case IR_LEARN_STATE_FAIL:
        ESP_LOGE(TAG, "IR Learn failed, retry");
        light_flag = false;
        break;
    case IR_LEARN_STATE_STEP:
    default:
        ESP_LOGI(TAG, "IR Learn step:[%d][%d]", state, sub_step);
        break;
    }
    return;
}
static esp_err_t ir_learn_init_task(ir_learn_result_cb cb)
{
    esp_err_t ret = ESP_OK;

    xTaskCreate(ir_learn_tx_task, "Tx task", 1024 * 6, NULL, 10, NULL);

    const ir_learn_cfg_t config = {
        .learn_count = IR_LEARN_COUNT,
        .task_stack = 4096*3,
        .task_priority = 5,
        .task_affinity = 1,
        .callback = cb,
    };
    ESP_ERROR_CHECK(ir_learn_new(&config, &handle));
    ESP_LOGI(TAG, "IR learn new task created");

    return ret;
}
esp_err_t ir_task_start(void)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "IR learn task start");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NO_FREE_PAGES && ret != ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize IR learn task
    ret = ir_learn_init_task(ir_send_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "IR learn task initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "IR learn task started successfully");

    return ret;
}
