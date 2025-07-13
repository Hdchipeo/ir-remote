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

/* IR learn includes */
#include "ir_learn.h"
#include "ir_encoder.h"
#include "ir_config.h"
#include "driver_config.h"
#include "ir_storage.h"
#include "device.h"

static const char *TAG = "App_IR_learn";

static ir_learn_handle_t handle = NULL;
static struct ir_learn_sub_list_head ir_data;

QueueHandle_t ir_trans_queue = NULL;
QueueHandle_t ir_learn_queue = NULL;

extern ir_learn_common_param_t *learn_param; // Pointer to the IR learn parameters
extern device_state_t g_device_state;        // Global device state

extern bool light_flag; // Flag to control light state

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
    case IR_LEARN_STATE_RECEIVE:
        ESP_LOGI(TAG, "IR Learn receive step: %d", sub_step);
        break;
    case IR_LEARN_STATE_STEP:
    default:
        ESP_LOGI(TAG, "IR Learn step:[%d][%d]", state, sub_step);
        break;
    }
    return;
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
                ir_rx_stop(); // Stop IR RX before transmitting
                ESP_LOGI(TAG, "IR transmit command for key: %s", ir_event.key);
                ir_learn_load(&ir_data, ir_event.key);
                ir_send_raw(&ir_data);
                update_device_state_from_key(&g_device_state, ir_event.key);
                ir_learn_clean_sub_data(&ir_data);
                ir_rx_restart(learn_param);
                break;
            case IR_EVENT_LEARN_DONE:
                ir_learn_save(&ir_data, ir_event.data, ir_event.key);
                ir_learn_clean_sub_data(&ir_data);
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
            case IR_EVENT_RECEIVE:
                break;
            default:
                ESP_LOGW(TAG, "Unknown IR event: %d", ir_event.event);
                break;
            }
        }
    }
    vTaskDelete(NULL);
}
static esp_err_t ir_learn_init_task(ir_learn_result_cb cb)
{
    esp_err_t ret = ESP_OK;

    xTaskCreate(ir_learn_tx_task, "Tx task", 1024 * 6, NULL, 10, NULL);

    const ir_learn_cfg_t config = {
        .learn_count = IR_LEARN_COUNT,
        .task_stack = 4096 * 5,
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
