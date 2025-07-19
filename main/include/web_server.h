#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"

#define ESP_HOSTNAME CONFIG_HOST_NAME_WEB_SERVER

void app_web_server_start(void);
void app_web_server_stop(void);
void mdns_start(void);