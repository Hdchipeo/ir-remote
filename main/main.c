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
#include "ir_storage.h"
#include "ota.h"

bool ota_enabled = false;

static const char *TAG = "Ir_Remote";

void app_main(void)
{

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    read_nvs(&ota_enabled);

    ir_task_start();        // Start the IR task
    app_driver_init();      // Initialize device components
    ESP_LOGI(TAG, "IR Remote Control Application Started");

#if CONFIG_CONSOLE_ENABLED
    app_console_start();    // Start the console for user interaction
#endif

    if(ota_enabled)
    {
        ota_enabled = false;
        write_nvs(ota_enabled);
        app_ota_start(); // Start the OTA application
    }
    else
    {
        app_wifi_init(); // Initialize Wi-Fi for ESPNOW communication
    }
    app_espnow_start();     // Start the ESPNOW application
    app_web_server_start(); // Start the web server for remote control
}
