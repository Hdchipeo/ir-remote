#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "ir_remote.h" 
#include "device.h"

static const char* TAG = "Ir_Remote";
  

void app_main(void)
{
    ir_task_start(); // Start the IR task
    app_device_init(); // Initialize device components
    ESP_LOGI(TAG, "IR Remote Control Application Started");
}
