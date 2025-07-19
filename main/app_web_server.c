#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "mdns.h"
#include "web_server.h"

static const char *TAG = "WebServer";
static httpd_handle_t s_server = NULL;

void mdns_start(void)
{
    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(ESP_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("IR Web Server"));
    
    // Add service to mDNS
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS initialized with hostname %s: ", ESP_HOSTNAME);
}

static const char *get_content_type(const char *filename)
{
    if (strstr(filename, ".html"))
        return "text/html";
    if (strstr(filename, ".css"))
        return "text/css";
    if (strstr(filename, ".js"))
        return "application/javascript";
    if (strstr(filename, ".ico"))
        return "image/x-icon";
    return "text/plain";
}

static esp_err_t static_file_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving URI: %s", req->uri);

    char filepath[64];
    if (strcmp(req->uri, "/") == 0)
    {
        snprintf(filepath, sizeof(filepath), "/spiffs/index.html"); // Default file
    }
    else
    {
        snprintf(filepath, sizeof(filepath), "/spiffs%s", req->uri); // Map trực tiếp
    }

    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(filepath));

    char line[256];
    while (fgets(line, sizeof(line), file))
    {
        httpd_resp_sendstr_chunk(req, line);
    }

    httpd_resp_sendstr_chunk(req, NULL);
    fclose(file);
    return ESP_OK;
}

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Hello GET request received");

    // Get the response string from user context
    const char *response_str = (const char *)req->user_ctx;
    if (response_str)
    {
        httpd_resp_send(req, response_str, HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        httpd_resp_send_404(req);
    }

    return ESP_OK;
}

static const httpd_uri_t hello = {
    .uri = "/hello",
    .method = HTTP_GET,
    .handler = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "Hello World!"};

static const httpd_uri_t static_file = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = static_file_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t uri_style = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = static_file_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t uri_script = {
    .uri = "/script.js",
    .method = HTTP_GET,
    .handler = static_file_get_handler,
    .user_ctx = NULL};

void app_web_server_start(void)
{
    mdns_start();

    httpd_handle_t server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return;
    }
    s_server = server;

    httpd_register_uri_handler(server, &static_file);
    httpd_register_uri_handler(server, &uri_style);
    httpd_register_uri_handler(server, &uri_script);
    httpd_register_uri_handler(server, &hello);

    ESP_LOGI(TAG, "Web server started successfully");
}
void app_web_server_stop(void)
{
    if (s_server)
    {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
