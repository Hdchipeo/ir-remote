#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "esp_now.h"
#include "esp_crc.h"
#include "espnow_config.h"
#include "device.h"
#include "ir_config.h"
#include "ir_learn.h"
#include "ir_storage.h"

static const char *TAG = "Esp-now";

static QueueHandle_t s_espnow_queue = NULL;
extern QueueHandle_t ir_trans_queue;

static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

device_state_t g_device_state;

static esp_err_t espnow_add_peer(uint8_t *mac_addr, bool encrypt)
{
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        ESP_LOGE(TAG, "Malloc peer information fail");
        return ESP_ERR_NO_MEM;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = encrypt;
    memcpy(peer->peer_addr, mac_addr, ESP_NOW_ETH_ALEN);

    esp_err_t ret = esp_now_add_peer(peer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Add peer failed: %s", esp_err_to_name(ret));
    }

    free(peer);

    return ret;
}

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    espnow_event_t evt;
    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL)
    {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE)
    {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t *mac_addr = recv_info->src_addr;
    uint8_t *des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0)
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(des_addr))
    {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    }
    else
    {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL)
    {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(s_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE)
    {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}
static void update_and_send_device_state(device_state_t *state)
{
    if (state == NULL)
    {
        ESP_LOGE(TAG, "Device state is NULL");
        return;
    }
    ir_event_cmd_t ir_event;

    // Update the device state
    save_device_state_to_nvs(state);

    ir_state_get_key(state, ir_event.key, IR_KEY_MAX_LEN);
    if (ir_event.key == NULL)
    {
        ESP_LOGE(TAG, "Failed to get IR key");
        return;
    }

    ir_event.event = IR_EVENT_TRANSMIT;
    xQueueSend(ir_trans_queue, &ir_event, portMAX_DELAY);
    ESP_LOGI(TAG, "Send IR key %s to TX Task", ir_event.key);
}
static void espnow_task(void *pvParameter)
{
    espnow_event_t evt;

    while (xQueueReceive(s_espnow_queue, &evt, portMAX_DELAY) == pdTRUE)
    {
        switch (evt.id)
        {
        case ESPNOW_SEND_CB:
        {
            break;
        }
        case ESPNOW_RECV_CB:
        {
            espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
            ESP_LOGI(TAG, "Receive ESPNOW data from: " MACSTR ", len: %d",
                     MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

            ir_state_handle_command(&g_device_state, *(ir_command_packet_t *)recv_cb->data);
            update_and_send_device_state(&g_device_state);
            free(recv_cb->data);
            break;
        }
        default:
            ESP_LOGE(TAG, "Callback type error: %d", evt.id);
            break;
        }
    }
}
static esp_err_t espnow_init(void)
{
    s_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
    if (s_espnow_queue == NULL)
    {
        ESP_LOGE(TAG, "Create queue fail");
        return ESP_FAIL;
    }
    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK(esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW));
    ESP_ERROR_CHECK(esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL));
#endif
    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));

    /* Add broadcast peer information to peer list. */
    espnow_add_peer(broadcast_mac, false);

    xTaskCreate(espnow_task, "esp_now_task", 2048, NULL, 4, NULL);

    return ESP_OK;
}
void app_espnow_start(void)
{
    ESP_ERROR_CHECK(espnow_init());
}
static void espnow_deinit()
{
    vQueueDelete(s_espnow_queue);
    s_espnow_queue = NULL;
    esp_now_deinit();
}
void app_espnow_stop(void)
{
    espnow_deinit();
}