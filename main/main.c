#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "ir_config.h" 
#include "device.h"
#include "console.h"
#include "ota.h"

static const char* TAG = "Ir_Remote";
  

void app_main(void)
{
    app_ota_start(); // Start the OTA application
    ir_task_start(); // Start the IR task
    app_device_init(); // Initialize device components
    app_console_start(); // Start the console for user interaction
    ESP_LOGI(TAG, "IR Remote Control Application Started");
}
