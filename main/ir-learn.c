#include <stdio.h>
#include <string.h>
#include <inttypes.h>

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
#include "driver/rmt_rx.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "unity.h"

#define IR_RESOLUTION_HZ 1000000 // 1MHz resolution, 1 tick = 1us
#define IR_RX_GPIO GPIO_NUM_15
#define IR_BTN_GPIO GPIO_NUM_17
#define LIGHT_GPIO GPIO_NUM_5
#define LIGHT_STATE GPIO_NUM_16

#define BUTTON_ACTIVE_LEVEL 0 // Button active level 0

#define NVS_NAMESPACE "ir_store"
#define NVS_KEY "samsung_power"
#define TAG "ir_learn"

#define IR_SYMBOL_MAX 100
#define IR_BUTTON_DEBOUNCE_MS 50

#define IR_BUFFER_SYMBOLS 128 // buffer size

rmt_channel_handle_t rx_chan = NULL;
QueueHandle_t rx_queue = NULL;
static rmt_symbol_word_t rx_buffer[IR_BUFFER_SYMBOLS];

bool light_flag = false; // Flag to control light state

typedef enum
{
    IR_EVENT_NONE,
    IR_EVENT_LEARN,
    IR_EVENT_LEARN_DONE,
    IR_EVENT_RECEIVE,
    IR_EVENT_RECEIVE_DONE,
    IR_EVENT_RESET
} ir_event_t;

QueueHandle_t ir_event_queue;

static void button_event_cb(void *arg, void *data)
{
    button_event_t event = iot_button_get_event(arg);
    ESP_LOGI(TAG, "%s", iot_button_get_event_str(event));

    if (BUTTON_LONG_PRESS_START == event)
    {
        ir_event_t event;
        event = IR_EVENT_RESET;
        xQueueSend(ir_event_queue, &event, portMAX_DELAY);
    }
}
static void config_btn_gpio()
{
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = IR_BTN_GPIO,
        .active_level = BUTTON_ACTIVE_LEVEL,
    };

    button_handle_t btn = NULL;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
    TEST_ASSERT(ret == ESP_OK);
    TEST_ASSERT_NOT_NULL(btn);

    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_event_cb, NULL);
}
static void config_light_gpio()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(LIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = false,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(LIGHT_GPIO, 0);
}
static void config_light_state_gpio()
{
    gpio_config_t light_conf = {
        .pin_bit_mask = BIT64(LIGHT_STATE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = false,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&light_conf));
    gpio_set_level(LIGHT_STATE, 0);
}
static void set_light_state(gpio_num_t gpio_num)
{
    static bool state = false;
    state = !state; // Toggle light state
    if (state)
    {
        gpio_set_level(gpio_num, 1); // Turn ON light
    }
    else
    {
        gpio_set_level(gpio_num, 0); //Turn OFF light
    }
}
static void set_light_state_with_delay(gpio_num_t gpio_num, uint32_t delay_ms)
{
    set_light_state(gpio_num);           // Turn on
    vTaskDelay(pdMS_TO_TICKS(delay_ms)); // Wait for a while
    set_light_state(gpio_num);           // Turn off
}

esp_err_t save_ir_to_nvs(const char *key, rmt_symbol_word_t *symbols, size_t symbol_num)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(handle, "ir_len", symbol_num);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(handle, key, symbols, symbol_num * sizeof(rmt_symbol_word_t));
    if (err == ESP_OK)
    {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Saved %d symbols to NVS", symbol_num);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save symbols: %s", esp_err_to_name(err));
    }
    nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t load_ir_from_nvs(const char *key, rmt_symbol_word_t **symbols_out, size_t *num_symbols_out)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    uint32_t num_symbols;
    err = nvs_get_u32(handle, "ir_len", &num_symbols);
    if (err != ESP_OK)
        return err;

    size_t size = num_symbols * sizeof(rmt_symbol_word_t);
    *symbols_out = malloc(size);
    if (!*symbols_out)
        return ESP_ERR_NO_MEM;

    err = nvs_get_blob(handle, key, *symbols_out, &size);
    *num_symbols_out = num_symbols;

    nvs_close(handle);
    return err;
}

bool compare_ir_symbols(const rmt_symbol_word_t *a, size_t a_num, const rmt_symbol_word_t *b, size_t b_num)
{
    if (a_num != b_num)
        return false;
    for (size_t i = 0; i < a_num; ++i)
    {
        int diff0 = abs((int)a[i].duration0 - (int)b[i].duration0);
        int diff1 = abs((int)a[i].duration1 - (int)b[i].duration1);
        if (diff0 > 300 || diff1 > 300)
        {
            return false;
        }
    }
    return true;
}
void reset_ir_nvs()
{
    ESP_LOGW(TAG, "Đang xóa dữ liệu IR trong NVS...");
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_erase_key(handle, NVS_KEY);  // Delete signal blob
        nvs_erase_key(handle, "ir_len"); // Delete signal length
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "🧹 Đã xóa IR khỏi NVS.");
    }
    else
    {
        ESP_LOGE(TAG, "Không thể mở NVS để xóa.");
    }
}
static bool rmt_rx_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

void rmt_rx_init()
{
    esp_err_t err;

    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_BUFFER_SYMBOLS,
        .flags.with_dma = false,
    };

    // Create RX channel
    err = rmt_new_rx_channel(&rx_cfg, &rx_chan);
    if (err != ESP_OK)
    {
        ESP_LOGE("RMT", "Failed to create RX channel: %s", esp_err_to_name(err));
        return;
    }
    else
    {
        ESP_LOGI("RMT", "RX channel created successfully.");
    }

    // Create queue for callback
    rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (rx_queue == NULL)
    {
        ESP_LOGE("RMT", "Failed to create RX queue");
        return;
    }
    else
    {
        ESP_LOGI("RMT", "RX queue created successfully.");
    }

    // Regist callback
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_callback,
    };
    err = rmt_rx_register_event_callbacks(rx_chan, &cbs, rx_queue);
    if (err != ESP_OK)
    {
        ESP_LOGE("RMT", "Failed to register RX callbacks: %s", esp_err_to_name(err));
        return;
    }
    else
    {
        ESP_LOGI("RMT", "RX callbacks registered successfully.");
    }

    // Turn on RX
    err = rmt_enable(rx_chan);
    if (err != ESP_OK)
    {
        ESP_LOGE("RMT", "Failed to enable RX channel: %s", esp_err_to_name(err));
        return;
    }
    else
    {
        ESP_LOGI("RMT", "RX channel enabled successfully.");
    }

    ESP_LOGI("RMT", "rmt_rx_init() completed successfully.");

    rmt_receive_config_t recv_cfg = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 10000000,
    };
    rmt_receive(rx_chan, rx_buffer, sizeof(rx_buffer), &recv_cfg);
}
esp_err_t start_ir_receive()
{
    rmt_receive_config_t recv_cfg = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 10000000,
    };

    rmt_disable(rx_chan);

    esp_err_t err = rmt_enable(rx_chan); 
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to re-enable RMT channel: %s", esp_err_to_name(err));
        return err;
    }
    return rmt_receive(rx_chan, rx_buffer, sizeof(rx_buffer), &recv_cfg);
}

esp_err_t ir_learn_receive(rmt_symbol_word_t **symbols_out, size_t *num_symbols_out)
{
    rmt_rx_done_event_data_t rx_data;
    if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(10000)))
    {
        *symbols_out = malloc(rx_data.num_symbols * sizeof(rmt_symbol_word_t));
        if (!*symbols_out)
            return ESP_ERR_NO_MEM;
        memcpy(*symbols_out, rx_data.received_symbols,
               rx_data.num_symbols * sizeof(rmt_symbol_word_t));
        *num_symbols_out = rx_data.num_symbols;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t ir_receive(rmt_symbol_word_t **symbols_out, size_t *num_symbols_out)
{
    rmt_rx_done_event_data_t rx_data;
    if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(3000)))
    {
        *symbols_out = malloc(rx_data.num_symbols * sizeof(rmt_symbol_word_t));
        if (!*symbols_out)
            return ESP_ERR_NO_MEM;
        memcpy(*symbols_out, rx_data.received_symbols,
               rx_data.num_symbols * sizeof(rmt_symbol_word_t));
        *num_symbols_out = rx_data.num_symbols;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}


static void ir_learn_task(void *arg)
{
    config_btn_gpio();
    config_light_gpio(); 
    ESP_LOGI(TAG, "Initializing RMT RX...");
    rmt_rx_init();

    rmt_symbol_word_t *learned_symbols = NULL;
    size_t learned_symbol_num = 0;

    ir_event_t event;
    bool ir_flag = true;

    rmt_symbol_word_t *received_symbols = NULL;
    size_t received_num = 0;
    rmt_symbol_word_t *saved_symbols = NULL;
    size_t saved_num = 0;

    while (1)
    {
        if (xQueueReceive(ir_event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            switch (event)
            {
            case IR_EVENT_RESET:
                ESP_LOGW(TAG, "Reset NVS...");
                reset_ir_nvs();
                if (learned_symbols)
                {
                    free(learned_symbols);
                    learned_symbols = NULL;
                    learned_symbol_num = 0;
                }
            case IR_EVENT_LEARN:
                ESP_LOGI(TAG, "Ir learning mode started...");
                rmt_symbol_word_t *symbols = NULL;
                size_t num_symbols = 0;
                light_flag = true;

                if (ir_learn_receive(&symbols, &num_symbols) == ESP_OK)
                {
                    ESP_LOGI(TAG, "Successfully learned %d symbols", num_symbols);
                    save_ir_to_nvs(NVS_KEY, symbols, num_symbols);
                    if (learned_symbols)
                        free(learned_symbols);
                    learned_symbols = symbols;
                    learned_symbol_num = num_symbols;
                    ir_flag = true; // Set flag to enable light
                  
                }
                else
                {
                    ESP_LOGW(TAG, "Không học được tín hiệu IR");
                    if (symbols)
                        free(symbols);
                }
                light_flag = false;
                ESP_LOGI(TAG, "Chế độ nhận tín hiệu IR bắt đầu...");
                vTaskDelay(pdMS_TO_TICKS(500)); 
                start_ir_receive(); //Start receiving IR again
                break;
            default:
                break;
            }
        }

        if (ir_flag)
        {
            if (ir_receive(&received_symbols, &received_num) == ESP_OK)
            {
                event = IR_EVENT_RECEIVE;
                xQueueSend(ir_event_queue, &event, portMAX_DELAY);
                ESP_LOGI(TAG, "Nhận tín hiệu IR: %d symbols", received_num);
                esp_err_t err = load_ir_from_nvs(NVS_KEY, &saved_symbols, &saved_num);

                if (compare_ir_symbols(saved_symbols, saved_num, received_symbols, received_num))
                {
                    ESP_LOGI(TAG, "🎯 Tín hiệu nhận khớp với NVS!");
                    set_light_state(LIGHT_GPIO);
                }
                else
                {
                    ESP_LOGW(TAG, "⚠️ Tín hiệu không khớp!");
                }
                free(received_symbols);
                vTaskDelay(pdMS_TO_TICKS(100)); 
                start_ir_receive();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

static void light_state_task(void *arg)
{
    ir_event_t event;
    config_light_state_gpio(); //Configure light state GPIO

    while (1)
    {
        if (light_flag)
        {
            set_light_state_with_delay(LIGHT_STATE, 500); // Blink light for 500ms
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ir_event_queue = xQueueCreate(10, sizeof(ir_event_t));
    xTaskCreate(ir_learn_task, "ir_learn_task", 4096, NULL, 5, NULL);
    xTaskCreate(light_state_task, "light_state_task", 4048, NULL, 5, NULL);
}
