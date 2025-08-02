#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <dirent.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "mdns.h"
#include "web_server.h"
#include "ir_config.h"
#include "ir_storage.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "index_html.h" // Include your HTML index file
#include "style_css.h"
#include "script_js.h"
#include "cJSON.h"

static const char *TAG = "WebServer";
static httpd_handle_t s_server = NULL;
extern bool ota_enabled; // Flag to control OTA updates

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

void start_dns_server(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE("DNS", "Socket creation failed");
        vTaskDelete(NULL);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    uint8_t buf[512];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    while (1)
    {
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < 0)
            continue;

        // Trả lời DNS redirect về 192.168.4.1
        buf[2] |= 0x80; // response flag
        buf[3] |= 0x80; // recursion available
        buf[7] = 1;     // ANCOUNT = 1

        int i = len;
        buf[i++] = 0xC0;
        buf[i++] = 0x0C;
        buf[i++] = 0x00;
        buf[i++] = 0x01; // Type A
        buf[i++] = 0x00;
        buf[i++] = 0x01; // Class IN
        buf[i++] = 0x00;
        buf[i++] = 0x00;
        buf[i++] = 0x00;
        buf[i++] = 0x3C; // TTL
        buf[i++] = 0x00;
        buf[i++] = 0x04; // RDLENGTH
        buf[i++] = 192;
        buf[i++] = 168;
        buf[i++] = 4;
        buf[i++] = 1; // IP 192.168.4.1

        sendto(sock, buf, i, 0, (struct sockaddr *)&source_addr, socklen);
    }

    close(sock);
    vTaskDelete(NULL);
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

esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html, index_html_len);
    return ESP_OK;
}

esp_err_t css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css, style_css_len);
    return ESP_OK;
}

esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)script_js, script_js_len);
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
esp_err_t ir_learn_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char mode[16], name[32];
        if (httpd_query_key_value(query, "mode", mode, sizeof(mode)) == ESP_OK &&
            httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK)
        {

            ESP_LOGI("LEARN", "Học lệnh chế độ: %s, tên: %s", mode, name);

            bool success = ir_learn_command(mode, name); // 🧠 Giả định Bro có hàm học lệnh
            if (success)
            {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
            }
            else
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Learn failed");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode or name");
    return ESP_FAIL;
}

esp_err_t ir_save_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char name[32];
        if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK)
        {
            ESP_LOGI("SAVE", "Lưu lệnh: %s", name);
            bool ok = ir_save_command(name); // Giả định bạn có hàm save
            if (ok)
            {
                httpd_resp_sendstr(req, "Saved");
            }
            else
            {
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
esp_err_t ir_update_handler(httpd_req_t *req)
{
    ota_enabled = true;
    write_nvs(ota_enabled);

    ESP_LOGI(TAG, "Firmware update requested");
    httpd_resp_sendstr(req, "Firmware update started");

    esp_restart();
    return ESP_OK;
}
esp_err_t ir_list_handler(httpd_req_t *req)
{
    const char *dir_path = "/spiffs";
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        ESP_LOGE("SPIFFS", "Failed to open %s", dir_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SPIFFS open failed");
        return ESP_FAIL;
    }

    typedef struct
    {
        char name[32];
        bool step_exist[64];
    } key_info_t;

    key_info_t keys[32] = {0};
    int key_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_REG)
            continue;

        char key[32];
        int step;

        if (sscanf(entry->d_name, "%[^_]_step%d.ir", key, &step) == 2)
        {
            bool found = false;
            for (int i = 0; i < key_count; i++)
            {
                if (strcmp(keys[i].name, key) == 0)
                {
                    keys[i].step_exist[step] = true;
                    found = true;
                    break;
                }
            }
            if (!found && key_count < 32)
            {
                strcpy(keys[key_count].name, key);
                keys[key_count].step_exist[step] = true;
                key_count++;
            }
        }
    }
    closedir(dir);

    // Tạo JSON
    char json[2048];
    size_t offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, "[");

    for (int i = 0; i < key_count; i++)
    {
        if (i > 0)
            offset += snprintf(json + offset, sizeof(json) - offset, ",");

        offset += snprintf(json + offset, sizeof(json) - offset, "{\"name\":\"%s\",\"delays\":[", keys[i].name);

        // Đọc delay file
        char delay_path[64];
        snprintf(delay_path, sizeof(delay_path), "/spiffs/%s.delay", keys[i].name);
        int delays[64] = {0};
        FILE *f = fopen(delay_path, "r");
        int delay_count = 0;
        if (f)
        {
            while (fscanf(f, "%d", &delays[delay_count]) == 1 && delay_count < 64)
            {
                delay_count++;
            }
            fclose(f);
        }

        // Mảng lưu các step tồn tại
        int step_index[64];
        int step_count = 0;

        for (int j = 0; j < 64; j++)
        {
            if (keys[i].step_exist[j])
            {
                step_index[step_count++] = j;
            }
        }

        for (int j = 0; j < step_count - 1; j++)
        {
            if (j != 0)
                offset += snprintf(json + offset, sizeof(json) - offset, ",");

            // delay giữa step_index[j] → step_index[j+1] ứng với delays[step_index[j] - 1]
            int step_start = step_index[j];
            int delay_value = (step_start > 0) ? delays[step_start - 1] : 0;

            offset += snprintf(json + offset, sizeof(json) - offset, "%d", delay_value);
        }
        offset += snprintf(json + offset, sizeof(json) - offset, "]}");
    }

    offset += snprintf(json + offset, sizeof(json) - offset, "]");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}
esp_err_t ir_update_delay_handler(httpd_req_t *req)
{
    // B1: Lấy key từ query string
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query string");
    }

    char key[IR_KEY_MAX_LEN];
    if (httpd_query_key_value(query, "key", key, sizeof(key)) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing key param");
    }

    // B2: Đọc body (delays)
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive body");
    }
    buf[len] = '\0';

    // B3: Parse delay
    int delays[IR_STEP_COUNT_MAX] = {0};
    size_t count = 0;
    char *token = strtok(buf, ",");
    while (token && count < IR_STEP_COUNT_MAX)
    {
        delays[count++] = atoi(token);
        token = strtok(NULL, ",");
    }

    // B4: Ghi vào file
    int result = save_step_timediff_to_file(key, delays, count);
    if (result != 0)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
    }

    return httpd_resp_sendstr(req, "Delay updated");
}
esp_err_t ir_delete_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char name[32];
        if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK)
        {
            ESP_LOGI("DELETE", "Xoá lệnh: %s", name);
            bool ok = ir_delete_command(name);
            if (ok)
            {
                httpd_resp_sendstr(req, "Deleted");
            }
            else
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete failed");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name");
    return ESP_FAIL;
}
esp_err_t ir_delete_step_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query string");
    }

    char key[IR_KEY_MAX_LEN];
    char idx_str[8];
    if (httpd_query_key_value(query, "key", key, sizeof(key)) != ESP_OK ||
        httpd_query_key_value(query, "index", idx_str, sizeof(idx_str)) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing key or index");
    }

    int index = atoi(idx_str);
    if (index < 0 || index >= IR_STEP_COUNT_MAX)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
    }

    // Gọi hàm xoá step khỏi file (tuỳ Bro triển khai)
    bool ok = ir_delete_step_from_file(key, index);
    if (!ok)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete step failed");
    }

    ESP_LOGI("IR", "Đã xoá step %d trong lệnh %s", index, key);
    return httpd_resp_sendstr(req, "Step deleted");
}

esp_err_t ir_rename_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
    {
        char old_name[32], new_name[32];
        if (httpd_query_key_value(query, "old", old_name, sizeof(old_name)) == ESP_OK &&
            httpd_query_key_value(query, "new", new_name, sizeof(new_name)) == ESP_OK)
        {
            ESP_LOGI("RENAME", "Đổi tên lệnh từ: %s sang %s", old_name, new_name);
            bool ok = ir_rename_command(old_name, new_name);
            if (ok)
            {
                httpd_resp_sendstr(req, "Renamed");
            }
            else
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rename failed");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing old or new name");
    return ESP_FAIL;
}
esp_err_t ir_assign_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");

    buf[len] = '\0';

    char source[32], target[32];
    cJSON *json = cJSON_Parse(buf);
    if (!json)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    cJSON *src = cJSON_GetObjectItem(json, "source");
    cJSON *dst = cJSON_GetObjectItem(json, "target");

    if (!cJSON_IsString(src) || !cJSON_IsString(dst))
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid params");
    }

    strncpy(source, src->valuestring, sizeof(source));
    strncpy(target, dst->valuestring, sizeof(target));
    cJSON_Delete(json);

    cJSON *aliases;
    if (ir_load_aliases(&aliases) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Load failed");

    cJSON_ReplaceItemInObject(aliases, source, cJSON_CreateString(target));
    ir_save_aliases(aliases);
    cJSON_Delete(aliases);

    return httpd_resp_sendstr(req, "Alias updated");
}

esp_err_t ir_alias_list_handler(httpd_req_t *req)
{
    cJSON *aliases;
    if (ir_load_aliases(&aliases) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Load failed");

    char *json_str = cJSON_PrintUnformatted(aliases);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    cJSON_Delete(aliases);
    free(json_str);
    return ESP_OK;
}

esp_err_t ir_alias_delete_handler(httpd_req_t *req)
{
    char query[64], source[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "source", source, sizeof(source)) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing source param");
    }

    cJSON *aliases;
    if (ir_load_aliases(&aliases) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Load failed");

    cJSON_DeleteItemFromObject(aliases, source);
    ir_save_aliases(aliases);
    cJSON_Delete(aliases);

    return httpd_resp_sendstr(req, "Alias deleted");
}
esp_err_t ir_assign_bulk_handler(httpd_req_t *req)
{
    char content[1024];
    int received = httpd_req_recv(req, content, sizeof(content) - 1); // Trừ 1 để chừa '\0'
    if (received <= 0)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive");

    content[received] = '\0';

    cJSON *arr = cJSON_Parse(content);
    if (!arr || !cJSON_IsArray(arr))
    {
        if (arr)
            cJSON_Delete(arr);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON array");
    }

    // Load aliases từ file nếu có
    cJSON *aliases = NULL;
    if (ir_load_aliases(&aliases) != ESP_OK || !aliases)
    {
        aliases = cJSON_CreateObject(); // Tạo mới nếu không tồn tại
    }

    // Lặp qua từng phần tử trong mảng
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        cJSON *from = cJSON_GetObjectItem(item, "from");
        cJSON *to = cJSON_GetObjectItem(item, "to");

        if (!cJSON_IsString(from) || !cJSON_IsString(to))
        {
            ESP_LOGW("IR_ASSIGN", "Bỏ qua item không hợp lệ");
            continue;
        }

        // Gán: từ source → target
        cJSON_ReplaceItemInObject(aliases, from->valuestring, cJSON_CreateString(to->valuestring));
        ESP_LOGI("IR_ASSIGN", "Gán %s → %s", from->valuestring, to->valuestring);
    }

    // Ghi lại file alias
    esp_err_t save_res = ir_save_aliases(aliases);
    cJSON_Delete(aliases);
    cJSON_Delete(arr);

    if (save_res != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save alias file");

    return httpd_resp_sendstr(req, "✅ Bulk IR assignments saved!");
}

esp_err_t ir_simple_list_handler(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();

    DIR *dir = opendir("/spiffs");
    if (!dir)
        return httpd_resp_send_err(req, 500, "Cannot open SPIFFS");

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_REG)
            continue;
        if (strstr(entry->d_name, ".ir"))
            cJSON_AddItemToArray(arr, cJSON_CreateString(entry->d_name));
    }
    closedir(dir);

    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    cJSON_Delete(arr);
    return ESP_OK;
}

httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL};
httpd_uri_t css_uri = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = css_handler,
    .user_ctx = NULL};
httpd_uri_t js_uri = {
    .uri = "/script.js",
    .method = HTTP_GET,
    .handler = js_handler,
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

httpd_uri_t update_uri = {
    .uri = "/fw/check",
    .method = HTTP_GET,
    .handler = ir_update_handler,
    .user_ctx = NULL};

httpd_uri_t uri_list = {
    .uri = "/ir/list",
    .method = HTTP_GET,
    .handler = ir_list_handler,
    .user_ctx = NULL};

httpd_uri_t uri_delete = {
    .uri = "/ir/delete",
    .method = HTTP_GET,
    .handler = ir_delete_handler,
    .user_ctx = NULL};

httpd_uri_t uri_rename = {
    .uri = "/ir/rename",
    .method = HTTP_GET,
    .handler = ir_rename_handler,
    .user_ctx = NULL};

httpd_uri_t uri_update_delay = {
    .uri = "/ir/update_delay",
    .method = HTTP_POST,
    .handler = ir_update_delay_handler,
    .user_ctx = NULL};

httpd_uri_t delete_delay_uri = {
    .uri = "/ir/delete_delay",
    .method = HTTP_POST,
    .handler = ir_delete_step_handler};

httpd_uri_t assign_post = {
    .uri = "/ir/assign",
    .method = HTTP_POST,
    .handler = ir_assign_handler};

httpd_uri_t assign_get = {
    .uri = "/ir/aliases",
    .method = HTTP_GET,
    .handler = ir_alias_list_handler};

httpd_uri_t assign_delete = {
    .uri = "/ir/assign",
    .method = HTTP_DELETE,
    .handler = ir_alias_delete_handler};

httpd_uri_t assign_list = {
    .uri = "/ir/simple_list",
    .method = HTTP_GET,
    .handler = ir_simple_list_handler};

    httpd_uri_t assign_bulk = {
        .uri = "/ir/assign/bulk",
        .method = HTTP_POST,
        .handler = ir_assign_bulk_handler};

void app_web_server_start(void)
{
    mdns_start();

    httpd_handle_t server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 30;
    config.stack_size = 8192 * 2;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return;
    }
    s_server = server;

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &css_uri);
    httpd_register_uri_handler(server, &js_uri);

    httpd_register_uri_handler(server, &send_uri);
    httpd_register_uri_handler(server, &white_uri);
    httpd_register_uri_handler(server, &reset_uri);

    httpd_register_uri_handler(server, &learn_uri);
    httpd_register_uri_handler(server, &save_uri);
    httpd_register_uri_handler(server, &update_uri);
    httpd_register_uri_handler(server, &uri_list);
    httpd_register_uri_handler(server, &uri_delete);
    httpd_register_uri_handler(server, &uri_rename);
    httpd_register_uri_handler(server, &uri_update_delay);
    httpd_register_uri_handler(server, &delete_delay_uri);

    httpd_register_uri_handler(server, &assign_post);
    httpd_register_uri_handler(server, &assign_get);
    httpd_register_uri_handler(server, &assign_delete);
    httpd_register_uri_handler(server, &assign_list);
    httpd_register_uri_handler(server, &assign_bulk);

    // xTaskCreate(start_dns_server, "dns_server", 4096, NULL, 5, NULL);

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
