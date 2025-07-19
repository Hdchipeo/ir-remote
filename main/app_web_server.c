#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "mdns.h"
#include "web_server.h"
#include "ir_config.h"

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

esp_err_t ir_send_handler(httpd_req_t *req)
{
    char param[32] = {0};
    /* Lấy chuỗi query 'name' */
    if (httpd_req_get_url_query_str(req, param, sizeof(param)) == ESP_OK)
    {
        char value[32];
        if (httpd_query_key_value(param, "name", value, sizeof(value)) == ESP_OK)
        {
            ESP_LOGI("HTTP", "IR Send: %s", value);
            ir_send_command(value);
        }
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
esp_err_t ir_learn_handler(httpd_req_t *req) {
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char mode[16];
        if (httpd_query_key_value(query, "mode", mode, sizeof(mode)) == ESP_OK) {
            ESP_LOGI("LEARN", "Học lệnh chế độ: %s", mode);
            bool success = ir_learn_command(mode);  // Giả định bạn có hàm học
            if (success) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Learn failed");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
    return ESP_FAIL;
}

esp_err_t ir_save_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char name[32];
        if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK) {
            ESP_LOGI("SAVE", "Lưu lệnh: %s", name);
            bool ok = ir_save_command(name); // Giả định bạn có hàm save
            if (ok) {
                httpd_resp_sendstr(req, "Saved");
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
    return ESP_FAIL;
}

esp_err_t white_screen_handler(httpd_req_t *req)
{
    ir_white_screen();
    httpd_resp_sendstr(req, "White screen triggered");
    return ESP_OK;
}
esp_err_t reset_screen_handler(httpd_req_t *req)
{
    ir_reset_screen();
    httpd_resp_sendstr(req, "Screen reset");
    return ESP_OK;
}

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

httpd_uri_t send_uri = {
    .uri = "/ir/send",
    .method = HTTP_GET,
    .handler = ir_send_handler,
    .user_ctx = NULL};

httpd_uri_t white_uri = {
    .uri = "/ir/white-screen",
    .method = HTTP_GET,
    .handler = white_screen_handler,
    .user_ctx = NULL};

httpd_uri_t reset_uri = {
    .uri = "/ir/reset-screen",
    .method = HTTP_GET,
    .handler = reset_screen_handler,
    .user_ctx = NULL};

httpd_uri_t learn_uri = {
        .uri = "/ir/learn",
        .method = HTTP_GET, 
        .handler = ir_learn_handler,
        .user_ctx = NULL};

httpd_uri_t save_uri = {
    .uri = "/ir/save",
    .method = HTTP_GET,
    .handler = ir_save_handler,
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

    httpd_register_uri_handler(server, &send_uri);
    httpd_register_uri_handler(server, &white_uri);
    httpd_register_uri_handler(server, &reset_uri);
        
    httpd_register_uri_handler(server, &learn_uri);
    httpd_register_uri_handler(server, &save_uri);

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
