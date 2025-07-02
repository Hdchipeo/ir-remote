#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "iot_button.h"
#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "device.h"
#include "ir_remote.h"
#include "ir_learn.h"

static const char *TAG = "device";
extern QueueHandle_t ir_event_queue; // Queue to handle IR transmit events
extern QueueHandle_t ir_learn_queue; // Queue to handle IR learn events

bool light_flag = false; // Flag to control light state

static void button_event_cb(void *arg, void *data)
{
    button_event_t button_event = iot_button_get_event(arg);
    ESP_LOGI(TAG, "%s", iot_button_get_event_str(button_event));
    ir_event_t event;

    if(BUTTON_SINGLE_CLICK == button_event)
    {
        event = IR_EVENT_TRANSMIT;
        xQueueSend(ir_event_queue, &event, portMAX_DELAY);
    }
    else if (BUTTON_LONG_PRESS_START == button_event)
    {
        event = IR_EVENT_LEARN;
        xQueueSend(ir_learn_queue, &event, portMAX_DELAY);
    }
}
void config_btn_gpio()
{
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = IR_TX_BUTTON,
        .active_level = 0,
    };

    button_handle_t btn = NULL;
    iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);

    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_event_cb, NULL);
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
void set_light_state(gpio_num_t gpio_num)
{
    static bool state = false;
    state = !state; // Toggle light state
    if (state)
    {
        gpio_set_level(gpio_num, 1); // Turn ON light
    }
    else
    {
        gpio_set_level(gpio_num, 0); // Turn OFF light
    }
}
static void set_light_state_with_delay(gpio_num_t gpio_num, uint32_t delay_ms)
{
    set_light_state(gpio_num);           // Turn on
    vTaskDelay(pdMS_TO_TICKS(delay_ms)); // Wait for a while
    set_light_state(gpio_num);           // Turn off
}

static void light_state_task(void *arg)
{
    while (1)
    {
        if (light_flag)
        {
            set_light_state_with_delay(LIGHT_STATE, 500); // Blink light for 500ms
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
void app_device_init()
{
    ESP_LOGI(TAG, "Initializing device...");
    config_btn_gpio();         // Configure button GPIO
    config_light_state_gpio(); // Configure light state GPIO
    ESP_LOGI(TAG, "Device initialized successfully.");
    xTaskCreate(light_state_task, "light_state_task", 2048, NULL, 5, NULL); // Create light state task
    ESP_LOGI(TAG, "Light state task created successfully.");
}