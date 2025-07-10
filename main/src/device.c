#include "stdio.h"
#include "string.h"

#include "device.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "ir_storage.h"

static const char *TAG = "device";

const char *toggle_power_to_str(bool power_on)
{
    return power_on ? "on" : "off";
}

const char *ir_mode_to_str(ac_mode_t mode)
{
    switch (mode)
    {
    case AC_MODE_COOL:
        return "cool";
    case AC_MODE_HEAT:
        return "heat";
    case AC_MODE_FAN:
        return "fan";
    case AC_MODE_DRY:
        return "dry";
    case AC_MODE_AUTO:
        return "auto";
    default:
        return "unknown";
    }
}
const char *ir_fan_to_str(fan_speed_t speed)
{
    switch (speed)
    {
    case LOW:
        return "low";
    case MEDIUM:
        return "medium";
    case HIGH:
        return "high";
    case MAX:
        return "max";
    default:
        return "unknown";
    }
}
static ac_mode_t ir_mode_from_str(const char *mode_str)
{
    if (strcmp(mode_str, "cool") == 0)
        return AC_MODE_COOL;
    else if (strcmp(mode_str, "heat") == 0)
        return AC_MODE_HEAT;
    else if (strcmp(mode_str, "fan") == 0)
        return AC_MODE_FAN;
    else if (strcmp(mode_str, "dry") == 0)
        return AC_MODE_DRY;
    else if (strcmp(mode_str, "auto") == 0)
        return AC_MODE_AUTO;
    else
        return AC_MODE_MAX; // Unknown mode
}
static fan_speed_t ir_fan_from_int(int speed)
{
    switch (speed)
    {
    case 0:
        return LOW;
    case 1:
        return MEDIUM;
    case 2:
        return HIGH;
    default:
        return MAX; // Invalid speed
    }
}
void ir_state_init(device_state_t *state)
{
    if (!state)
        return;
    state->ac.power_on = false;
    state->ac.temperature = 25;    // Default temperature
    state->ac.speed = LOW;         // Default fan speed
    state->ac.mode = AC_MODE_COOL; // Default mode

    state->fan.power_on = false;
    state->fan.speed = LOW;         // Default fan speed
    state->fan.oscillation = false; // Default oscillation state

    state->light.power_on = false;
    // state->ac.light.brightness = 100; // Default brightness
    // state->ac.light.color_temperature = 3000; // Default color temperature in Kelvin
    // Initialize other device states as needed
    ESP_LOGI(TAG, "Device state initialized: AC power: %s, temp: %d, mode: %s, fan speed: %s",
             toggle_power_to_str(state->ac.power_on), state->ac.temperature, ir_mode_to_str(state->ac.mode), ir_fan_to_str(state->ac.speed));
    ESP_LOGI(TAG, "Fan state initialized: power: %s, speed: %s, oscillation: %d",
             toggle_power_to_str(state->fan.power_on), ir_fan_to_str(state->fan.speed), state->fan.oscillation);
    ESP_LOGI(TAG, "Light state initialized: power: %s",
             toggle_power_to_str(state->light.power_on));
}

void ir_state_handle_command(device_state_t *state, ir_command_packet_t packet)
{
    if (!state)
        return;

    switch (packet.command)
    {
    case AC_CMD_POWER_TOGGLE:
        state->ac.power_on = !state->ac.power_on;
        break;
    case AC_CMD_TEMP_UP:
        if (state->ac.temperature < 30)
            state->ac.temperature++;
        break;
    case AC_CMD_TEMP_DOWN:
        if (state->ac.temperature > 20)
            state->ac.temperature--;
        break;
    case AC_CMD_MODE_NEXT:
        state->ac.mode = (state->ac.mode + 1) % AC_MODE_MAX;
        break;
    case AC_CMD_FAN_SPEED_UP:
        state->ac.speed = (state->ac.speed + 1) % MAX;
        break;
    case AC_CMD_FAN_SPEED_DOWN:
        state->ac.speed = (state->ac.speed - 1 + MAX) % MAX;
        break;
    case AC_CMD_SET_TEMP:
        state->ac.temperature = packet.value;
        if (state->ac.temperature < 20)
            state->ac.temperature = 20;
        else if (state->ac.temperature > 30)
            state->ac.temperature = 30;
        break;
    case AC_CMD_SET_MODE:
        if (packet.value >= AC_MODE_COOL && packet.value < AC_MODE_MAX)
            state->ac.mode = packet.value;
        break;
    case Fan_CMD_POWER_TOGGLE:
        state->fan.power_on = !state->fan.power_on;
        break;
    case Fan_CMD_SPEED_UP:
        state->fan.speed = (state->fan.speed + 1) % MAX;
        break;
    case Fan_CMD_SPEED_DOWN:
        state->fan.speed = (state->fan.speed - 1 + MAX) % MAX;
        break;
    case Fan_CMD_OSCILLATION_TOGGLE:
        state->fan.oscillation = !state->fan.oscillation;
        break;
    case Light_CMD_POWER_TOGGLE:
        state->light.power_on = !state->light.power_on;
        break;
    default:
        ESP_LOGW(TAG, "Unknown command");
        break;
    }
}

void ir_state_get_key(device_state_t *state, char *out_key, size_t max_len)
{
    if (!state || !out_key)
        return;

    switch (state->type)
    {
    case DEVICE_TYPE_AC:
        snprintf(out_key, max_len, "ac_%s_%d_%s_%d",
                 state->ac.power_on ? "on" : "off",
                 state->ac.temperature,
                 ir_mode_to_str(state->ac.mode),
                 state->ac.speed);
        break;
    case DEVICE_TYPE_FAN:
        snprintf(out_key, max_len, "fan_%s_%d_%s",
                 state->fan.power_on ? "on" : "off",
                 state->fan.speed,
                 state->fan.oscillation ? "oscillating" : "static");
        break;
    case DEVICE_TYPE_LIGHT:
        snprintf(out_key, max_len, "light_%s",
                 state->light.power_on ? "on" : "off");
        break;
    default:
        ESP_LOGW(TAG, "Unknown device type: %d", state->type);
        snprintf(out_key, max_len, "unknown_device");
        return;
    }
}
static void ir_state_set_from_key(device_state_t *state, const char *key)
{
    if (!state || !key)
        return;

    if (strncmp(key, "ac_", 3) == 0)
    {
        char power_str[4] = {0}; // "on"/"off"
        char mode_str[8] = {0};  // "cool"/"auto"
        int temp = 0;
        int fan_speed = 0;

        int parsed = sscanf(key, "ac_%3[^_]_%d_%7[^_]_%d",
                            power_str, &temp, mode_str, &fan_speed);
        if (parsed != 4)
        {
            ESP_LOGW(TAG, "Invalid key format: %s", key);
            return;
        }

        state->type = DEVICE_TYPE_AC;
        state->ac.power_on = strcmp(power_str, "on") == 0;
        state->ac.temperature = temp;
        state->ac.mode = ir_mode_from_str(mode_str);
        state->ac.speed = ir_fan_from_int(fan_speed);

        ESP_LOGI(TAG, "Parsed AC state -> power: %s, temp: %d, mode: %s, fan: %d",
                 power_str, temp, mode_str, fan_speed);
    }
    else if (strncmp(key, "fan_", 4) == 0)
    {
        char power_str[4] = {0}; // "on"/"off"
        int speed = 0, swing = 0;

        int parsed = sscanf(key, "fan_%3[^_]_%d_%d", power_str, &speed, &swing);
        if (parsed >= 1)
        {
            state->type = DEVICE_TYPE_FAN;
            state->fan.power_on = strcmp(power_str, "on") == 0;

            if (parsed >= 2)
                state->fan.speed = speed;

            if (parsed == 3)
                state->fan.oscillation = swing;

            ESP_LOGI(TAG, "Parsed FAN state -> power: %s, speed: %d, swing: %d",
                     power_str, speed, swing);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid fan key format: %s", key);
        }
    }
    else if (strncmp(key, "light_", 6) == 0)
    {
        char power_str[4] = {0}; // "on"/"off"
        int brightness = 0;
        int parsed = sscanf(key, "light_%3[^_]_%d", power_str, &brightness);

        state->type = DEVICE_TYPE_LIGHT;
        state->light.power_on = strcmp(power_str, "on") == 0;

        if (parsed == 2)
            state->light.brightness = brightness;

        ESP_LOGI(TAG, "Parsed LIGHT state -> power: %s, brightness: %d",
                 power_str, brightness);
    }
    else
    {
        ESP_LOGW(TAG, "Unrecognized key format: %s", key);
    }
}
void update_device_state_from_key(device_state_t *state, const char *key)
{
    if (!state || !key)
        return;

    ir_state_set_from_key(state, key);
    save_device_state_to_nvs(state);
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "Updated device state from key: %s", key);
    ESP_LOGI(TAG, "Current state: AC power: %s, temp: %d, mode: %s, fan speed: %s",
             toggle_power_to_str(state->ac.power_on), state->ac.temperature, ir_mode_to_str(state->ac.mode), ir_fan_to_str(state->ac.speed));
    ESP_LOGI(TAG, "Current fan state: power: %s, speed: %s, oscillation: %d",
             toggle_power_to_str(state->fan.power_on), ir_fan_to_str(state->fan.speed), state->fan.oscillation);
    ESP_LOGI(TAG, "Current light state: power: %s",
             toggle_power_to_str(state->light.power_on));
    ESP_LOGI(TAG, "===============================================");
}
