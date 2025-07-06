#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        AC_MODE_COOL = 0,
        AC_MODE_HEAT,
        AC_MODE_DRY,
        AC_MODE_FAN,
        AC_MODE_AUTO,
        AC_MODE_MAX
    } ac_mode_t;

    typedef enum
    {
        LOW = 0,
        MEDIUM,
        HIGH,
        MAX
    } fan_speed_t;

    typedef struct
    {
        bool power_on;
        uint8_t temperature;
        fan_speed_t speed;
        ac_mode_t mode;

        char power_str[4]; // "on"/"off"
        uint8_t temperature_; // 20 - 30 degrees Celsius
        char mode_str[8];  // "auto"/"cool"/...
        int fan_speed;

    } ac_state_t;

    typedef struct
    {
        bool power_on;
        fan_speed_t speed;
        bool oscillation; // true = on, false = off
    } fan_state_t;

    typedef struct
    {
        bool power_on;
        int brightness;        // 0 - 100%
        int color_temperature; // 2700K - 6500K
    } light_state_t;

    typedef enum
    {
        DEVICE_TYPE_AC,
        DEVICE_TYPE_FAN,
        DEVICE_TYPE_LIGHT,
        // Add more device types as needed
    } device_type_t;

    typedef struct
    {
        device_type_t type;  // Type of device (e.g., AC, Fan, Light)
        ac_state_t ac;       // Air Conditioner state
        fan_state_t fan;     // Fan state
        light_state_t light; // Light state
        // Add more device states as needed
    } device_state_t;

    typedef enum
    {
        AC_CMD_POWER_TOGGLE,
        AC_CMD_TEMP_UP,
        AC_CMD_TEMP_DOWN,
        AC_CMD_MODE_NEXT,
        AC_CMD_FAN_SPEED_UP,
        AC_CMD_FAN_SPEED_DOWN,
        AC_CMD_SET_TEMP,
        AC_CMD_SET_MODE,

        Fan_CMD_POWER_TOGGLE,
        Fan_CMD_SPEED_UP,
        Fan_CMD_SPEED_DOWN,
        Fan_CMD_OSCILLATION_TOGGLE,

        Light_CMD_POWER_TOGGLE

    } ir_command_t;

    typedef struct
    {
        ir_command_t command;
        int value;
    } ir_command_packet_t;

    /**
     * @brief Initialize AC state with default values
     */
    void ir_state_init(device_state_t *state);

    /**
     * @brief Handle an IR command and update the AC state
     *
     * @param state The current AC state
     * @param command The IR command to handle
     */
    void ir_state_handle_command(device_state_t *state, ir_command_packet_t packet);

    /**
     * @brief Get the IR key for the current state
     *
     * @param state The current state
     * @param out_key Output buffer for the IR key
     * @param max_len Maximum length of the output buffer
     */
    void ir_state_get_key(device_state_t *state, char *out_key, size_t max_len);

    /**
     * @brief Update the device state from an IR key
     * @param state The device state to update
     * @param key The IR key to parse and update the state
     */
    void update_device_state_from_key(device_state_t *state, const char *key);

    
    const char *ir_fan_to_str(fan_speed_t speed);
    const char* toggle_power_to_str(bool power_on);
    const char *ir_mode_to_str(ac_mode_t mode);

#ifdef __cplusplus
}
#endif
