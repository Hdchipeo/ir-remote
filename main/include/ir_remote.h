#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_err.h"


#define IR_RESOLUTION_HZ 1000000 // 1MHz resolution, 1 tick = 1us
#define IR_TX_GPIO_NUM GPIO_NUM_5
#define IR_RX_GPIO_NUM GPIO_NUM_4
#define IR_TX_BUTTON GPIO_NUM_15
#define IR_LEARN_COUNT 1


esp_err_t ir_task_start(void);


