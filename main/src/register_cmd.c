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
#include "ir_config.h"
#include "ir_storage.h"

extern QueueHandle_t ir_learn_queue;
extern QueueHandle_t ir_trans_queue;
extern ir_learn_common_param_t *learn_param; // Pointer to the IR learn parameters
extern device_state_t g_device_state; // Global device state

static const char *TAG = "IR_CMD";

static struct
{
    struct arg_str *key;
    struct arg_end *end;
} ir_key_args;

typedef struct
{
    struct arg_str *old_key;
    struct arg_str *new_key;
    struct arg_end *end;
} rename_args_t;

static rename_args_t rename_args;

static int ir_learn_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ir_key_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ir_key_args.end, argv[0]);
        return 1;
    }

    ir_event_cmd_t IR_cmd = {
        .event = IR_EVENT_LEARN,
    };
    strncpy(IR_cmd.key, ir_key_args.key->sval[0], sizeof(IR_cmd.key));
    xQueueSend(ir_learn_queue, &IR_cmd, portMAX_DELAY);
    ESP_LOGI(TAG, "IR learn command for key: %s", ir_key_args.key->sval[0]);

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

    ir_event_cmd_t IR_cmd = {
        .event = IR_EVENT_TRANSMIT};
    strncpy(IR_cmd.key, ir_key_args.key->sval[0], sizeof(IR_cmd.key));
    xQueueSend(ir_trans_queue, &IR_cmd, portMAX_DELAY);
    ESP_LOGI(TAG, "IR transmit command send for key: %s", ir_key_args.key->sval[0]);

    return 0;
}
static int ir_input_name(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ir_key_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ir_key_args.end, argv[0]);
        return 1;
    }

    ir_event_cmd_t IR_cmd = {
        .event = IR_EVENT_SET_NAME,
    };
    strncpy(IR_cmd.key, ir_key_args.key->sval[0], sizeof(IR_cmd.key));
    xQueueSend(ir_trans_queue, &IR_cmd, portMAX_DELAY);
    ESP_LOGI(TAG, "Set name for IR learn command: %s", ir_key_args.key->sval[0]);
    return 0;
}
static int ir_list_cmd(int argc, char **argv)
{
    list_ir_keys_from_spiffs();
    ESP_LOGI(TAG, "Listing all IR keys is not implemented yet.");

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

    esp_err_t err = delete_ir_key_from_spiffs(ir_key_args.key->sval[0]);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "IR key '%s' deleted successfully from NVS.", ir_key_args.key->sval[0]);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to delete IR key '%s' from NVS: %s", ir_key_args.key->sval[0], esp_err_to_name(err));
    }

    return 0;
}
static int ir_reset_spiffs_cmd(int argc, char **argv)
{
    format_spiffs();
    ESP_LOGI(TAG, "SPIFFS storage formatted for IR keys.");

    return 0;
}

static int ir_rename_key_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&rename_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, rename_args.end, argv[0]);
        return 1;
    }

    const char *old_key = rename_args.old_key->sval[0];
    const char *new_key = rename_args.new_key->sval[0];

    rename_ir_key_in_spiffs(old_key, new_key);

    return 0;
}
static int ir_device_state_cmd(int argc, char **argv)
{
    load_device_state_from_nvs(&g_device_state);

    ESP_LOGI(TAG, "Current device state loaded from NVS:");
    ESP_LOGI(TAG, "AC power: %s, temp: %d, mode: %s, fan speed: %s",
             toggle_power_to_str(g_device_state.ac.power_on), g_device_state.ac.temperature,
             ir_mode_to_str(g_device_state.ac.mode), ir_fan_to_str(g_device_state.ac.speed));
    ESP_LOGI(TAG, "Fan power: %s, speed: %s, oscillation: %d",
        toggle_power_to_str(g_device_state.fan.power_on), ir_fan_to_str(g_device_state.fan.speed),
             g_device_state.fan.oscillation);
    ESP_LOGI(TAG, "Light power: %s",
        toggle_power_to_str(g_device_state.light.power_on));

    return 0;
}
void register_ir_device_state_commands(void)
{
    /* Register custom commands here */
    esp_console_cmd_t device_state_cmd = {
        .command = "device_state",
        .help = "Get current device state from NVS",
        .hint = NULL,
        .func = &ir_device_state_cmd,
        .argtable = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&device_state_cmd));
}

void register_ir_rename_commands(void)
{
    rename_args.old_key = arg_strn(NULL, NULL, "<old_key>", 1, 1, "Old IR key name");
    rename_args.new_key = arg_strn(NULL, NULL, "<new_key>", 1, 1, "New IR key name");
    rename_args.end = arg_end(2);
    /* Register custom commands here */
    esp_console_cmd_t rename_cmd = {
        .command = "rename",
        .help = "Rename IR key in storage",
        .hint = NULL,
        .func = &ir_rename_key_cmd,
        .argtable = (void **)&rename_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&rename_cmd));
}
void register_ir_input_name_commands(void)
{
    ir_key_args.key = arg_str1(NULL, NULL, "<Name for ir learn cmd>", "Input name for ir learn key command");
    ir_key_args.end = arg_end(1);
    /* Register custom commands here */
    esp_console_cmd_t input_name_cmd = {
        .command = "setname",
        .help = "Set name for IR learn command",
        .hint = NULL,
        .func = &ir_input_name,
        .argtable = &ir_key_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&input_name_cmd));
}
void register_ir_format_spiffs_commands(void)
{
    /* Register custom commands here */
    esp_console_cmd_t reset_cmd = {
        .command = "format",
        .help = "Format storage for IR keys",
        .hint = NULL,
        .func = &ir_reset_spiffs_cmd,
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
        .help = "Delete IR key from storage",
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
