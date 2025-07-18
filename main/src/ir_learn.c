/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <sys/queue.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

#include "ir_learn.h"
#include "ir_encoder.h"
#include "ir_learn_err_check.h"
#include "ir_config.h"
#include "ir_storage.h"

static const char *TAG = "Ir-learn";

extern QueueHandle_t ir_learn_queue;
extern QueueHandle_t ir_trans_queue;

static TaskHandle_t ir_rx_task_handle = NULL;
static rmt_channel_handle_t rx_channel_handle = NULL;
ir_learn_common_param_t *learn_param = NULL;

const static rmt_receive_config_t ir_learn_rmt_rx_cfg = {
    .signal_range_min_ns = 1000,
    .signal_range_max_ns = RMT_MAX_RANGE_TIME * 1000,
};

static bool ir_learn_list_lock(ir_learn_t *ctx, uint32_t timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(ctx->rmt_mux, timeout_ticks) == pdTRUE;
}

static void ir_learn_list_unlock(ir_learn_t *ctx)
{
    xSemaphoreGiveRecursive(ctx->rmt_mux);
}

static bool ir_learn_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t task_woken = pdFALSE;
    ir_learn_t *ir_learn = (ir_learn_t *)user_data;

    xQueueSendFromISR(ir_learn->receive_queue, edata, &task_woken);
    return (task_woken == pdTRUE);
}

esp_err_t ir_learn_print_raw(struct ir_learn_sub_list_head *cmd_list)
{
    IR_LEARN_CHECK(cmd_list, "list pointer can't be NULL!", ESP_ERR_INVALID_ARG);

    uint8_t sub_num = 0;
    struct ir_learn_sub_list_t *sub_it;
    rmt_symbol_word_t *p_symbols;

    SLIST_FOREACH(sub_it, cmd_list, next)
    {
        ESP_LOGI(TAG, "sub_it:[%d], timediff:%03d ms, symbols:%03d",
                 sub_num++,
                 sub_it->timediff / 1000,
                 sub_it->symbols.num_symbols);

        p_symbols = sub_it->symbols.received_symbols;
        for (int i = 0; i < sub_it->symbols.num_symbols; i++)
        {
            printf("symbol:[%03d] %04d| %04d\r\n",
                   i, p_symbols->duration0, p_symbols->duration1);
            p_symbols++;
        }
    }
    return ESP_OK;
}

static esp_err_t ir_learn_remove_all_symbol(ir_learn_t *ctx)
{
    ir_learn_list_lock(ctx, 0);
    ir_learn_clean_data(&ctx->learn_list);
    ir_learn_clean_sub_data(&ctx->learn_result);
    ir_learn_list_unlock(ctx);

    return ESP_OK;
}

static esp_err_t ir_learn_destroy(ir_learn_t *ctx)
{
    ir_learn_remove_all_symbol(ctx);

    if (rx_channel_handle)
    {
        rmt_disable(rx_channel_handle);
        rmt_del_channel(rx_channel_handle);
    }

    if (ctx->receive_queue)
    {
        vQueueDelete(ctx->receive_queue);
    }

    if (ctx->rmt_mux)
    {
        vSemaphoreDelete(ctx->rmt_mux);
    }

    if (ctx->rmt_rx.received_symbols)
    {
        free(ctx->rmt_rx.received_symbols);
    }

    free(ctx);

    return ESP_OK;
}
static esp_err_t init_rmt_rx(ir_learn_t *ctx)
{
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_RX_GPIO_NUM,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = RMT_RX_MEM_BLOCK_SIZE,
        .flags.with_dma = false,
    };

    esp_err_t ret = rmt_new_rx_channel(&rx_channel_cfg, &rx_channel_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create RX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = ir_learn_rx_done_callback,
    };
    ret = rmt_rx_register_event_callbacks(rx_channel_handle, &cbs, ctx);
    return ESP_OK;
}
void ir_rx_stop(void)
{
    if (ir_rx_task_handle)
    {
        vTaskSuspend(ir_rx_task_handle);
        ESP_LOGI(TAG, "RX Task suspended");
    }

    if (rx_channel_handle)
    {
        rmt_disable(rx_channel_handle);
        rmt_del_channel(rx_channel_handle);
        rx_channel_handle = NULL;
        ESP_LOGI(TAG, "RX Channel deleted");
    }
}
void ir_rx_restart(ir_learn_common_param_t *learn_param)
{
    if (learn_param == NULL || learn_param->ctx == NULL)
    {
        ESP_LOGE(TAG, "Learn param is NULL");
        return;
    }

    ir_learn_t *ctx = (ir_learn_t *)learn_param->ctx;

    if (rx_channel_handle == NULL)
    {
        init_rmt_rx(ctx);
        rmt_enable(rx_channel_handle);
        ESP_LOGI(TAG, "RX Channel restarted");
    }
    else
    {
        ESP_LOGI(TAG, "RX Channel already exists");
    }

    if (ir_rx_task_handle)
    {
        vTaskResume(ir_rx_task_handle);
        ESP_LOGI(TAG, "RX Task resumed");
    }
    ir_learn_remove_all_symbol(learn_param->ctx);
}
esp_err_t ir_learn_restart(ir_learn_handle_t ir_learn_hdl)
{
    IR_LEARN_CHECK(ir_learn_hdl, "learn task not executed!", ESP_ERR_INVALID_ARG);
    ir_learn_t *ctx = (ir_learn_t *)ir_learn_hdl;

    ir_learn_remove_all_symbol(ctx);
    ctx->learned_count = 0;
    return ESP_OK;
}

esp_err_t ir_learn_stop(ir_learn_handle_t *ir_learn_hdl)
{
    IR_LEARN_CHECK(ir_learn_hdl && *ir_learn_hdl, "learn task not executed!", ESP_ERR_INVALID_ARG);
    ir_learn_t *handle = (ir_learn_t *)(*ir_learn_hdl);

    if (handle->running)
    {
        *ir_learn_hdl = NULL;
    }
    else
    {
        ESP_LOGI(TAG, "not running");
    }

    return ESP_OK;
}
static esp_err_t ir_learn_pause(ir_learn_t *ctx)
{
    IR_LEARN_CHECK(ctx, "learn task not executed!", ESP_ERR_INVALID_ARG);

    // if (ctx->running)
    // {
    //     ctx->running = false;
    // }
    if (rx_channel_handle)
    {
        rmt_disable(rx_channel_handle);
        rmt_del_channel(rx_channel_handle);
    }
    else
    {
        ESP_LOGI(TAG, "not running");
    }

    return ESP_OK;
}
static esp_err_t ir_learn_start(ir_learn_t *ctx)
{
    IR_LEARN_CHECK(ctx, "learn task not executed!", ESP_ERR_INVALID_ARG);

    // if (!ctx->running)
    // {
    // ctx->running = true;

    init_rmt_rx(ctx);
    rmt_enable(rx_channel_handle);
    ir_learn_remove_all_symbol(ctx);
    ESP_LOGI(TAG, "RX Channel initialized and enabled");
    // }
    // else
    // {
    //     ESP_LOGI(TAG, "already running");
    // }

    return ESP_OK;
}
esp_err_t ir_learn_clean_sub_data(struct ir_learn_sub_list_head *sub_head)
{
    IR_LEARN_CHECK(sub_head, "list pointer can't be NULL!", ESP_ERR_INVALID_ARG);

    struct ir_learn_sub_list_t *result_it;

    while (!SLIST_EMPTY(sub_head))
    {
        result_it = SLIST_FIRST(sub_head);
        if (result_it->symbols.received_symbols)
        {
            heap_caps_free(result_it->symbols.received_symbols);
        }
        SLIST_REMOVE_HEAD(sub_head, next);
        if (result_it)
        {
            heap_caps_free(result_it);
        }
    }
    SLIST_INIT(sub_head);

    return ESP_OK;
}

esp_err_t ir_learn_clean_data(struct ir_learn_list_head *learn_head)
{
    IR_LEARN_CHECK(learn_head, "list pointer can't be NULL!", ESP_ERR_INVALID_ARG);

    struct ir_learn_list_t *learn_list;

    while (!SLIST_EMPTY(learn_head))
    {
        learn_list = SLIST_FIRST(learn_head);

        ir_learn_clean_sub_data(&learn_list->cmd_sub_node);

        SLIST_REMOVE_HEAD(learn_head, next);
        if (learn_list)
        {
            heap_caps_free(learn_list);
        }
    }
    SLIST_INIT(learn_head);

    return ESP_OK;
}
void ir_learn_clone_sub_data(struct ir_learn_sub_list_head *dst, const struct ir_learn_sub_list_head *src)
{
    ir_learn_clean_sub_data(dst);
    struct ir_learn_sub_list_t *cur;
    SLIST_FOREACH(cur, src, next)
    {
        ir_learn_add_sub_list_node(dst, cur->timediff, &cur->symbols);
    }
}

static bool ir_learn_process_rx_data(ir_learn_common_param_t *learn_param, rmt_rx_done_event_data_t *rx_data)
{
    int64_t cur_time = esp_timer_get_time();
    size_t period = cur_time - learn_param->ctx->pre_time;
    learn_param->ctx->pre_time = cur_time;

    if (rx_data->num_symbols < 5)
    {
        ESP_LOGW(TAG, "Signal too short, received symbols: %d", rx_data->num_symbols);
        return false;
    }

    if (period < 500 * 1000)
    {
        learn_param->ctx->learned_sub++;
    }
    else
    {
        period = 0;
        learn_param->ctx->learned_sub = 1;
        learn_param->ctx->learned_count++;
    }

    ir_learn_list_lock(learn_param->ctx, 0);
    if (learn_param->ctx->learned_sub == 1)
    {
        ir_learn_add_list_node(&learn_param->ctx->learn_list);
    }

    struct ir_learn_list_t *last = SLIST_FIRST(&learn_param->ctx->learn_list);
    while (SLIST_NEXT(last, next))
    {
        last = SLIST_NEXT(last, next);
    }

    ir_learn_add_sub_list_node(&last->cmd_sub_node, period, rx_data);
    ir_learn_list_unlock(learn_param->ctx);

    if (learn_param->user_cb)
    {
        learn_param->user_cb(learn_param->ctx->learned_count, learn_param->ctx->learned_sub, &last->cmd_sub_node);
    }

    // ir_learn_clone_sub_data(&learn_param->ctx->learn_result, &last->cmd_sub_node);

    return true;
}

void ir_rx_pause()
{
    if (ir_rx_task_handle)
    {
        xTaskNotify(ir_rx_task_handle, 1, eSetValueWithOverwrite);
    }
}

static esp_err_t ir_learn_active_receive_loop(ir_learn_common_param_t *learn_param)
{
    if (!learn_param || !learn_param->ctx)
    {
        ESP_LOGE(TAG, "Invalid parameter in learn loop");
        return ESP_ERR_INVALID_ARG;
    }

    learn_param->ctx->learned_count = 0;
    learn_param->ctx->learned_sub = 0;

    rmt_rx_done_event_data_t learn_data;

    while (learn_param->ctx->learned_count < learn_param->ctx->learn_count)
    {
        if (xQueueReceive(learn_param->ctx->receive_queue, &learn_data, portMAX_DELAY) == pdTRUE)
        {
            bool success = ir_learn_process_rx_data(learn_param, &learn_data);
            if (!success)
            {
                ESP_LOGW(TAG, "Invalid RX data, waiting next...");
                if (rmt_receive(rx_channel_handle,
                                learn_param->ctx->rmt_rx.received_symbols,
                                learn_param->ctx->rmt_rx.num_symbols,
                                &ir_learn_rmt_rx_cfg) != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to restart RMT receive");
                    return ESP_FAIL;
                }
                continue;
            }

            if (rmt_receive(rx_channel_handle,
                            learn_param->ctx->rmt_rx.received_symbols,
                            learn_param->ctx->rmt_rx.num_symbols,
                            &ir_learn_rmt_rx_cfg) != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to restart RMT receive");
                return ESP_FAIL;
            }
        }
        else
        {
            if (learn_param->ctx->learned_sub > 0)
            {
                ESP_LOGI(TAG, "Timeout reached. Received partial IR. Proceeding.");
                break;
            }
            else
            {
                ESP_LOGW(TAG, "Timeout reached with no IR signal. Retrying...");
                break;
            }
        }
    }

    return ir_learn_check_valid(&learn_param->ctx->learn_list, &learn_param->ctx->learn_result);
}

static esp_err_t ir_clone_data(struct ir_learn_sub_list_head *dst, const struct ir_learn_sub_list_head *src)
{
    struct ir_learn_sub_list_t *node;

    SLIST_FOREACH(node, src, next)
    {
        struct ir_learn_sub_list_t *new_node = malloc(sizeof(*new_node));
        if (!new_node)
            return ESP_ERR_NO_MEM;

        memcpy(&new_node->symbols, &node->symbols, sizeof(node->symbols));
        new_node->timediff = node->timediff;

        SLIST_INSERT_HEAD(dst, new_node, next); // hoặc SLIST_INSERT_TAIL nếu muốn giữ thứ tự
    }

    return ESP_OK;
}
esp_err_t send_data_to_ir_app(ir_learn_common_param_t *learn_param, ir_event_cmd_t *ir_event)
{
    IR_LEARN_CHECK(learn_param && ir_event, "Invalid parameters", ESP_ERR_INVALID_ARG);

    if (xQueueSend(ir_trans_queue, ir_event, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to send IR event to queue");
        return ESP_FAIL;
    }

    return ESP_OK;
}
static void ir_learn_normal(ir_learn_common_param_t *learn_param, ir_event_cmd_t ir_event)
{
    ESP_LOGI(TAG, "Start learning IR cmd for key: %s", ir_event.key);

    if (learn_param->user_cb)
    {
        learn_param->user_cb(IR_LEARN_STATE_READY, 0, NULL);
    }

    ir_learn_start(learn_param->ctx);

    ESP_ERROR_CHECK(rmt_receive(rx_channel_handle,
                                learn_param->ctx->rmt_rx.received_symbols,
                                learn_param->ctx->rmt_rx.num_symbols,
                                &ir_learn_rmt_rx_cfg));

    esp_err_t ret = ir_learn_active_receive_loop(learn_param);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Learning completed successfully with %d commands",
                 learn_param->ctx->learned_count);
        if (learn_param->user_cb)
        {
            learn_param->user_cb(IR_LEARN_STATE_END, 0, &learn_param->ctx->learn_result);
        }
        ir_event.event = IR_EVENT_LEARN_DONE;
        ir_event.data = &learn_param->ctx->learn_result;
        send_data_to_ir_app(learn_param, &ir_event);
    }
    else
    {
        ESP_LOGE(TAG, "Learning failed, invalid data");
        if (learn_param->user_cb)
        {
            learn_param->user_cb(IR_LEARN_STATE_FAIL, 0, NULL);
        }
    }

    ir_learn_pause(learn_param->ctx);
}
static void ir_learn_step(ir_learn_common_param_t *learn_param, ir_event_cmd_t ir_event)
{
    char step[IR_KEY_MAX_LEN] = "step";
    float timediff_list[IR_STEP_COUNT_MAX] = {0};
    int step_index = 0;
    int64_t last_step_time = 0;

    if (learn_param->user_cb)
        learn_param->user_cb(IR_LEARN_STEP_READY, 0, NULL);

    ir_learn_start(learn_param->ctx);

    while (1)
    {
        ESP_LOGI(TAG, "Learning step %d for key: %s", step_index + 1, ir_event.key_name_step);

        int64_t start_time = esp_timer_get_time();

        ir_learn_start(learn_param->ctx);
        ESP_ERROR_CHECK(rmt_receive(rx_channel_handle,
                                    learn_param->ctx->rmt_rx.received_symbols,
                                    learn_param->ctx->rmt_rx.num_symbols,
                                    &ir_learn_rmt_rx_cfg));

        esp_err_t ret = ir_learn_active_receive_loop(learn_param);

        if (ret == ESP_OK)
        {
            if (step_index > 0)
            {
                int64_t diff_us = start_time - last_step_time;
                timediff_list[step_index - 1] = diff_us / 1000000.0f;
                ESP_LOGI(TAG, "Time since last step: %.2f seconds", timediff_list[step_index - 1]);
            }

            last_step_time = start_time;

            snprintf(step, IR_KEY_MAX_LEN, "step_%d", step_index + 1);
            snprintf(ir_event.key, IR_KEY_MAX_LEN, "%s/%s", ir_event.key_name_step, step);
            ir_event.event = IR_EVENT_LEARN_DONE;
            ir_event.data = &learn_param->ctx->learn_result;

            send_data_to_ir_app(learn_param, &ir_event);

            if (learn_param->user_cb)
                learn_param->user_cb(IR_LEARN_STEP_END, 0, NULL);

            step_index++;
        }
        else
        {
            ESP_LOGE(TAG, "Learning failed, invalid data");
            if (learn_param->user_cb)
                learn_param->user_cb(IR_LEARN_STEP_FAIL, 0, NULL);
        }

        if(step_index >= IR_STEP_COUNT_MAX)
        {
            ESP_LOGI(TAG, "Reached maximum step count: %d", IR_STEP_COUNT_MAX);
            break;
        }

        ir_learn_pause(learn_param->ctx);
    }

    ir_learn_pause(learn_param->ctx);

    if (step_index > 1)
    {
        esp_err_t ret = save_step_timediff_to_file(ir_event.key_name_step, timediff_list, step_index - 1);
        if (ret != ESP_OK)
            ESP_LOGE(TAG, "Failed to save timediff list for key: %s", ir_event.key_name_step);
        else
            ESP_LOGI(TAG, "Saved %d step delays", step_index - 1);
    }

    if (learn_param->user_cb)
        learn_param->user_cb(IR_LEARN_STEP_END, 0, &learn_param->ctx->learn_result);
}

static void ir_learn_task(void *arg)
{
    if (!arg)
    {
        ESP_LOGE(TAG, "ir_learn_task: NULL arg passed");
        vTaskDelete(NULL);
        return;
    }
    learn_param = (ir_learn_common_param_t *)arg;
    ir_event_cmd_t ir_event;

    while (1)
    {
        if (xQueueReceive(ir_learn_queue, &ir_event, portMAX_DELAY) == pdTRUE)
        {
            switch (ir_event.event)
            {
            case IR_EVENT_LEARN_NORMAL:
                ir_learn_normal(learn_param, ir_event);
                break;
            case IR_EVENT_LEARN_STEP:
                ir_learn_step(learn_param, ir_event);
                break;
            default:
                ESP_LOGW(TAG, "Unknown IR event: %d", ir_event.event);
                break;
            }
        }

        vTaskDelete(NULL);
    }
}

esp_err_t ir_learn_add_sub_list_node(struct ir_learn_sub_list_head *sub_head, uint32_t timediff, const rmt_rx_done_event_data_t *symbol)
{
    IR_LEARN_CHECK(sub_head, "list pointer can't be NULL!", ESP_ERR_INVALID_ARG);

    esp_err_t ret = ESP_OK;

    struct ir_learn_sub_list_t *item = (struct ir_learn_sub_list_t *)malloc(sizeof(struct ir_learn_sub_list_t));
    IR_LEARN_CHECK_GOTO(item, "no mem to store received RMT symbols", ESP_ERR_NO_MEM, err);

    item->timediff = timediff;
    item->symbols.num_symbols = symbol->num_symbols;
    item->symbols.received_symbols = malloc(symbol->num_symbols * sizeof(rmt_symbol_word_t));
    IR_LEARN_CHECK_GOTO(item->symbols.received_symbols, "no mem to store received RMT symbols", ESP_ERR_NO_MEM, err);

    memcpy(item->symbols.received_symbols, symbol->received_symbols, symbol->num_symbols * sizeof(rmt_symbol_word_t));
    item->next.sle_next = NULL;

    struct ir_learn_sub_list_t *last = SLIST_FIRST(sub_head);
    if (last == NULL)
    {
        SLIST_INSERT_HEAD(sub_head, item, next);
    }
    else
    {
        struct ir_learn_sub_list_t *sub_it;
        while ((sub_it = SLIST_NEXT(last, next)) != NULL)
        {
            last = sub_it;
        }
        SLIST_INSERT_AFTER(last, item, next);
    }
    return ret;

err:
    if (item)
    {
        free(item);
    }

    if (item->symbols.received_symbols)
    {
        free(item->symbols.received_symbols);
        item->symbols.received_symbols = NULL;
    }
    return ret;
}

esp_err_t ir_learn_add_list_node(struct ir_learn_list_head *learn_head)
{
    IR_LEARN_CHECK(learn_head, "list pointer can't be NULL!", ESP_ERR_INVALID_ARG);

    esp_err_t ret = ESP_OK;

    struct ir_learn_list_t *item = (struct ir_learn_list_t *)malloc(sizeof(struct ir_learn_list_t));
    IR_LEARN_CHECK_GOTO(item, "no mem to store received RMT symbols", ESP_ERR_NO_MEM, err);

    SLIST_INIT(&item->cmd_sub_node);
    item->next.sle_next = NULL;

    struct ir_learn_list_t *last = SLIST_FIRST(learn_head);
    if (last == NULL)
    {
        SLIST_INSERT_HEAD(learn_head, item, next);
    }
    else
    {
        struct ir_learn_list_t *it;
        while ((it = SLIST_NEXT(last, next)) != NULL)
        {
            last = it;
        }
        SLIST_INSERT_AFTER(last, item, next);
    }
    return ret;

err:
    if (item)
    {
        free(item);
    }
    return ret;
}

static esp_err_t ir_learn_check_duration(
    struct ir_learn_list_head *learn_head,
    struct ir_learn_sub_list_head *result_out,
    uint8_t sub_cmd_offset,
    uint32_t sub_num_symbols,
    uint32_t timediff)
{
    esp_err_t ret = ESP_OK;

    uint32_t duration_average0 = 0;
    uint32_t duration_average1 = 0;
    uint8_t learn_total_num = 0;

    struct ir_learn_list_t *main_it;
    rmt_symbol_word_t *p_symbols, *p_learn_symbols = NULL;
    rmt_rx_done_event_data_t add_symbols;

    add_symbols.num_symbols = sub_num_symbols;
    add_symbols.received_symbols = malloc(sub_num_symbols * sizeof(rmt_symbol_word_t));
    p_learn_symbols = add_symbols.received_symbols;
    IR_LEARN_CHECK_GOTO(p_learn_symbols, "no mem to store received RMT symbols", ESP_ERR_NO_MEM, err);

    for (int i = 0; i < sub_num_symbols; i++)
    {
        p_symbols = NULL;
        ret = ESP_OK;
        duration_average0 = 0;
        duration_average1 = 0;
        learn_total_num = 0;

        SLIST_FOREACH(main_it, learn_head, next)
        {

            struct ir_learn_sub_list_t *sub_it = SLIST_FIRST(&main_it->cmd_sub_node);
            for (int j = 0; j < sub_cmd_offset; j++)
            {
                sub_it = SLIST_NEXT(sub_it, next);
            }

            p_symbols = sub_it->symbols.received_symbols;
            p_symbols += i;

            if (duration_average0)
            {
                if ((p_symbols->duration0 > (duration_average0 / learn_total_num + RMT_DECODE_MARGIN)) ||
                    (p_symbols->duration0 < (duration_average0 / learn_total_num - RMT_DECODE_MARGIN)))
                {
                    ret = ESP_FAIL;
                }
            }
            if (duration_average1)
            {
                if ((p_symbols->duration1 > (duration_average1 / learn_total_num + RMT_DECODE_MARGIN)) ||
                    (p_symbols->duration1 < (duration_average1 / learn_total_num - RMT_DECODE_MARGIN)))
                {
                    ret = ESP_FAIL;
                }
            }
            IR_LEARN_CHECK_GOTO((ESP_OK == ret), "add cmd duration error", ESP_ERR_INVALID_ARG, err);

            duration_average0 += p_symbols->duration0;
            duration_average1 += p_symbols->duration1;
            learn_total_num++;
        }

        if (learn_total_num && p_symbols)
        {
            p_learn_symbols->duration0 = duration_average0 / learn_total_num;
            p_learn_symbols->duration1 = duration_average1 / learn_total_num;
            p_learn_symbols->level0 = p_symbols->level1;
            p_learn_symbols->level1 = p_symbols->level0;
            p_learn_symbols++;
        }
    }
    ir_learn_add_sub_list_node(result_out, timediff, &add_symbols);

    if (add_symbols.received_symbols)
    {
        free(add_symbols.received_symbols);
    }
    return ESP_OK;
err:
    if (add_symbols.received_symbols)
    {
        free(add_symbols.received_symbols);
    }
    return ESP_FAIL;
}

esp_err_t ir_learn_check_valid(struct ir_learn_list_head *learn_head, struct ir_learn_sub_list_head *result_out)
{
    IR_LEARN_CHECK(learn_head, "list pointer can't be NULL!", ESP_ERR_INVALID_ARG);

    esp_err_t ret = ESP_OK;
    struct ir_learn_list_t *learned_it;
    struct ir_learn_sub_list_t *sub_it;

    uint8_t expect_sub_cmd_num = 0xFF;
    uint8_t sub_cmd_num = 0;
    uint8_t learned_num = 0;

    SLIST_FOREACH(learned_it, learn_head, next)
    {
        sub_cmd_num = 0;
        learned_num++;
        SLIST_FOREACH(sub_it, &learned_it->cmd_sub_node, next)
        {
            sub_cmd_num++;
        }
        if (0xFF == expect_sub_cmd_num)
        {
            expect_sub_cmd_num = sub_cmd_num;
        }
        ESP_LOGI(TAG, "list:%d-%d", learned_num, sub_cmd_num);
        IR_LEARN_CHECK(expect_sub_cmd_num == sub_cmd_num, "cmd num mismatch", ESP_ERR_INVALID_SIZE);
    }

    uint16_t sub_num_symbols;
    uint32_t time_diff;

    for (int i = 0; i < sub_cmd_num; i++)
    {
        sub_num_symbols = 0xFFFF;
        time_diff = 0xFFFF;
        SLIST_FOREACH(learned_it, learn_head, next)
        {

            struct ir_learn_sub_list_t *sub_item = SLIST_FIRST(&learned_it->cmd_sub_node);
            for (int j = 0; j < i; j++)
            {
                sub_item = SLIST_NEXT(sub_item, next);
            }
            if (0xFFFF == sub_num_symbols)
            {
                sub_num_symbols = sub_item->symbols.num_symbols;
            }
            if (0xFFFF == time_diff)
            {
                time_diff = sub_item->timediff;
            }
            else
            {
                time_diff += sub_item->timediff;
            }
            IR_LEARN_CHECK(sub_num_symbols == sub_item->symbols.num_symbols, "sub symbol mismatch", ESP_ERR_INVALID_SIZE);
        }
        ret = ir_learn_check_duration(learn_head, result_out, i, sub_num_symbols, time_diff / learned_num);
        IR_LEARN_CHECK((ESP_OK == ret), "symbol add failed", ESP_ERR_INVALID_SIZE);
    }
    return ESP_OK;
}

esp_err_t ir_learn_new(const ir_learn_cfg_t *cfg, ir_learn_handle_t *handle_out)
{
    BaseType_t res;
    esp_err_t ret = ESP_OK;
    IR_LEARN_CHECK(cfg && handle_out, "invalid argument", ESP_ERR_INVALID_ARG);
    IR_LEARN_CHECK(cfg->learn_count < IR_LEARN_STATE_READY, "learn count too larger", ESP_ERR_INVALID_ARG);

    ir_learn_t *ir_learn_ctx = calloc(1, sizeof(ir_learn_t));
    IR_LEARN_CHECK(ir_learn_ctx, "no mem for ir_learn_ctx", ESP_ERR_NO_MEM);

    ir_learn_common_param_t *learn_param = calloc(1, sizeof(ir_learn_common_param_t));
    learn_param->ctx = ir_learn_ctx;
    learn_param->user_cb = cfg->callback;

    SLIST_INIT(&ir_learn_ctx->learn_list);
    ir_learn_ctx->learn_count = cfg->learn_count;
    ir_learn_ctx->rmt_rx.num_symbols = RMT_RX_MEM_BLOCK_SIZE * 8;
    ir_learn_ctx->rmt_rx.received_symbols = (rmt_symbol_word_t *)heap_caps_malloc(
        ir_learn_ctx->rmt_rx.num_symbols * sizeof(rmt_symbol_word_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    IR_LEARN_CHECK_GOTO(ir_learn_ctx->rmt_rx.received_symbols, "no mem to store received RMT symbols", ESP_ERR_NO_MEM, err);

    ir_learn_ctx->receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    IR_LEARN_CHECK_GOTO(ir_learn_ctx->receive_queue, "create rmt receive queue failed", ESP_FAIL, err);

    ir_learn_ctx->rmt_mux = xSemaphoreCreateRecursiveMutex();
    IR_LEARN_CHECK_GOTO(ir_learn_ctx->rmt_mux, "create rmt mux failed", ESP_FAIL, err);

    ir_learn_ctx->learn_event = xEventGroupCreate();
    IR_LEARN_CHECK_GOTO(ir_learn_ctx->learn_event, "create event group failed", ESP_FAIL, err);

    if (cfg->task_affinity < 0)
    {
        res = xTaskCreate(ir_learn_task, "ir learn task", cfg->task_stack, learn_param, cfg->task_priority, &ir_rx_task_handle);
    }
    else
    {
        res = xTaskCreatePinnedToCore(ir_learn_task, "ir learn task", cfg->task_stack, learn_param, cfg->task_priority, &ir_rx_task_handle, cfg->task_affinity);
    }
    IR_LEARN_CHECK_GOTO(res == pdPASS, "create ir_learn task fail!", ESP_FAIL, err);

    if (cfg->callback)
    {
        cfg->callback(IR_LEARN_STATE_INIT, 0, NULL);
    }

    *handle_out = ir_learn_ctx;
    return ret;
err:
    if (ir_learn_ctx)
    {
        ir_learn_destroy(ir_learn_ctx);
    }
    return ret;
}
