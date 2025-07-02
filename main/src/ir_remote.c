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

/* ESP32 includes */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_system.h"
#include "esp_spiffs.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_encoder.h"
#include "ir_config.h"
#include "device.h"
#include "ir_storage.h"

static const char *TAG = "IR_learn";
static ir_learn_handle_t handle = NULL;
rmt_channel_handle_t tx_channel = NULL;
rmt_encoder_handle_t raw_encoder = NULL;                /**< IR learn handle */
static struct ir_learn_sub_list_head ir_data; /**< IR learn test result */
QueueHandle_t ir_trans_queue = NULL;
QueueHandle_t ir_learn_queue = NULL;
extern bool light_flag; // Flag to control light state

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

    ir_trans_queue = xQueueCreate(5, sizeof(ir_event_cmd_t));
    ir_learn_queue = xQueueCreate(5, sizeof(ir_event_cmd_t));

    ir_event_cmd_t ir_event;

    while (1)
    {
        if (xQueueReceive(ir_trans_queue, &ir_event, portMAX_DELAY) == pdPASS)
        {
            switch (ir_event.event)
            {
            case IR_EVENT_TRANSMIT:
                ESP_LOGI(TAG, "IR transmit command for key: %s", ir_event.key);
                ir_learn_load(&ir_data, ir_event.key);
                ir_send_raw(&ir_data);
                ir_learn_clean_sub_data(&ir_data);
                break;
            case IR_EVENT_LEARN_DONE:
                ir_learn_save(&ir_data, ir_event.data, ir_event.key);
                if (strcmp(ir_event.key, "unknow") != 0)
                {
                    ESP_LOGI(TAG, "IR learn done for key: %s", ir_event.key);
                }
                else
                {
                    ESP_LOGI(TAG, "Key IR is unknow, set name for IR learn command:");
                }

                break;
            case IR_EVENT_SET_NAME:
                rename_ir_key_in_spiffs("unknow", ir_event.key);
                break;
            default:
                ESP_LOGW(TAG, "Unknown IR event: %d", ir_event.event);
                break;
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
        ir_learn_print_raw(data);
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
        .task_stack = 4096 * 3,
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
    // Initialize SPIFFS
    esp_err_t ret = spiffs_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPIFFS initialized successfully");

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
