#include "stdio.h"
#include "string.h"

#include "device.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

static const char *TAG = "device";

static const char *ir_mode_to_str(ac_mode_t mode)
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

    state->ac.light.power_on = false;
    // state->ac.light.brightness = 100; // Default brightness
    // state->ac.light.color_temperature = 3000; // Default color temperature in Kelvin
    // Initialize other device states as needed
    ESP_LOGI(TAG, "Device state initialized: AC power: %d, temp: %d, mode: %s, fan speed: %d",
             state->ac.power_on, state->ac.temperature, ir_mode_to_str(state->ac.mode), state->ac.speed);
    ESP_LOGI(TAG, "Fan state initialized: power: %d, speed: %d, oscillation: %d",
             state->fan.power_on, state->fan.speed, state->fan.oscillation);
    ESP_LOGI(TAG, "Light state initialized: power: %d",
             state->light.power_on);
}

void ir_state_handle_command(device_state_t *state, ir_command_packet_t packet)
{
    if (!state)
        return;

    switch (packet.command)
    {
    case AC_CMD_POWER_TOGGLE:
        state->ac.state.power = !state->ac.state.power;
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
