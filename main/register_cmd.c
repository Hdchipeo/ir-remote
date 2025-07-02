#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "ir_learn.h"
#include "ir_remote.h"

extern QueueHandle_t ir_learn_queue;
extern QueueHandle_t ir_trans_queue;

static struct
{
    struct arg_str *key; 
    struct arg_end *end;
} ir_key_args;

static int ir_learn_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ir_key_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ir_key_args.end, argv[0]);
        return 1;
    }

    ir_event_cmd_t ir_cmd = {
        .event = IR_EVENT_LEARN,
        .key = ir_key_args.key->sval[0]};
    xQueueSend(ir_learn_queue, &ir_cmd, portMAX_DELAY);
    ESP_LOGI("IR_CMD", "IR learn command send for key: %s", ir_key_args.key->sval[0]);

    return 0;
}
static int ir_trans_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ir_key_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ir_key_args.end, argv[0]);
        return 1;
    }

    ir_event_cmd_t ir_cmd = {
        .event = IR_EVENT_TRANSMIT,
        .key = ir_key_args.key->sval[0]};
    xQueueSend(ir_trans_queue, &ir_cmd, portMAX_DELAY);
    ESP_LOGI("IR_CMD", "IR transmit command send for key: %s", ir_key_args.key->sval[0]);

    return 0;
}
static int ir_list_cmd(int argc, char **argv)
{
    // This function can be used to list all IR keys stored in NVS
    // For now, we will just print a message
    list_ir_keys_from_nvs();
    ESP_LOGI("IR_CMD", "Listing all IR keys is not implemented yet.");

    return 0;
}
static int ir_delete_key_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ir_key_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ir_key_args.end, argv[0]);
        return 1;
    }

    esp_err_t err = delete_ir_key_from_nvs(ir_key_args.key->sval[0]);
    if (err == ESP_OK)
    {
        ESP_LOGI("IR_CMD", "IR key '%s' deleted successfully from NVS.", ir_key_args.key->sval[0]);
    }
    else
    {
        ESP_LOGE("IR_CMD", "Failed to delete IR key '%s' from NVS: %s", ir_key_args.key->sval[0], esp_err_to_name(err));
    }

    return 0;
}
static int ir_reset_nvs_cmd(int argc, char **argv)
{
    // This function can be used to reset NVS storage for IR keys
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK)
    {
        ESP_LOGI("IR_CMD", "NVS storage reset successfully.");
    }
    else
    {
        ESP_LOGE("IR_CMD", "Failed to reset NVS storage: %s", esp_err_to_name(err));
    }

    return 0;
}
void register_ir_reset_nvs_commands(void)
{
    /* Register custom commands here */
    esp_console_cmd_t reset_cmd = {
        .command = "reset_nvs",
        .help = "Reset NVS storage for IR keys",
        .hint = NULL,
        .func = &ir_reset_nvs_cmd,
        .argtable = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&reset_cmd));
}
void resister_ir_delete_commands(void)
{
    ir_key_args.key = arg_str1(NULL, NULL, "<Name for ir key to delete>", "Input name for ir key to delete");
    ir_key_args.end = arg_end(1);
    /* Register custom commands here */
    esp_console_cmd_t delete_cmd = {
        .command = "delete",
        .help = "Delete IR key from NVS",
        .hint = NULL,
        .func = &ir_delete_key_cmd,
        .argtable = &ir_key_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&delete_cmd));
}
void register_ir_learn_commands(void)
{
    ir_key_args.key = arg_str1(NULL, NULL, "<Name for ir learn cmd>", "Input name for ir learn key command");
    ir_key_args.end = arg_end(1);
    /* Register custom commands here */
    esp_console_cmd_t learn_cmd = {
        .command = "learn",
        .help = "Set name for IR learn command",
        .hint = NULL,
        .func = &ir_learn_cmd,
        .argtable = &ir_key_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&learn_cmd));
}
void register_ir_trans_commands(void)
{
    ir_key_args.key = arg_str1(NULL, NULL, "<Name for ir cmd transmit>", "Input name for ir key command transmit");
    ir_key_args.end = arg_end(1);
    /* Register custom commands here */
    esp_console_cmd_t trans_cmd = {
        .command = "transmit",
        .help = "Transmit IR learn command",
        .hint = NULL,
        .func = &ir_trans_cmd,
        .argtable = &ir_key_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&trans_cmd));
}
void register_ir_list_commands(void)
{
    /* Register custom commands here */
    esp_console_cmd_t list_cmd = {
        .command = "list",
        .help = "List all IR keys stored in NVS",
        .hint = NULL,
        .func = &ir_list_cmd,
        .argtable = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&list_cmd));
}

