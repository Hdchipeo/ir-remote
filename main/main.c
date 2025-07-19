#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "ir_config.h"
#include "driver_config.h"
#include "espnow_config.h"
#include "web_server.h"
#include "console.h"

#if CONFIG_OTA_ENABLED
#include "ota.h"
#endif

static const char *TAG = "Ir_Remote";

void app_main(void)
{
#if CONFIG_OTA_ENABLED
    app_ota_start(); // Start the OTA application
#else

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    app_wifi_init(); // Initialize Wi-Fi for ESPNOW communication
#endif
    app_espnow_start();     // Start the ESPNOW application
    app_web_server_start(); // Start the web server for remote control
    ir_task_start();        // Start the IR task
    app_driver_init();      // Initialize device components
    app_console_start();    // Start the console for user interaction
    ESP_LOGI(TAG, "IR Remote Control Application Started");
}
