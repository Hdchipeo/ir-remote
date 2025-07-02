#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"

#define LIGHT_STATE GPIO_NUM_18

void app_device_init();
void set_light_state(gpio_num_t gpio_num);
void config_btn_gpio();
