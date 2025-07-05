/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <sys/queue.h>
#include "driver/rmt_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "ir_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define RMT_RX_MEM_BLOCK_SIZE CONFIG_RMT_MEM_BLOCK_SYMBOLS
#define RMT_DECODE_MARGIN CONFIG_RMT_DECODE_MARGIN_US
#define RMT_MAX_RANGE_TIME CONFIG_RMT_SINGLE_RANGE_MAX_US

    /**
     * @brief Type of IR learn handle
     */
    typedef void *ir_learn_handle_t;

    /**
     * @brief Type of IR learn step
     */
    typedef enum
    {
        IR_LEARN_STATE_INIT = 0,   /**< IR learn initialization */
        IR_LEARN_STATE_STEP,       /**< IR learn step, start from 1 */
        IR_LEARN_STATE_READY = 20, /**< IR learn ready, after successful initialization */
        IR_LEARN_STATE_END,        /**< IR learn successfully */
        IR_LEARN_STATE_FAIL,       /**< IR learn failure */
        IR_LEARN_STATE_EXIT,       /**< IR learn exit */
    } ir_learn_state_t;

    typedef enum
    {
        IR_EVENT_NONE,
        IR_EVENT_LEARN,
        IR_EVENT_LEARN_DONE,
        IR_EVENT_RECEIVE,
        IR_EVENT_RECEIVE_DONE,
        IR_EVENT_TRANSMIT,
        IR_EVENT_SET_NAME,
        IR_EVENT_RESET,
        IR_EVENT_EXIT
    } ir_event_t;

    // Maximum length of the IR key used for identifying learned signals.
#define IR_KEY_MAX_LEN 64

    typedef struct
    {
        ir_event_t event;
        char key[IR_KEY_MAX_LEN]; /*!< Key name for IR command */
        struct ir_learn_sub_list_head *data;
    } ir_event_cmd_t;

    /**
     * @brief An element in the list of infrared (IR) learn data packets.
     *
     */
    struct ir_learn_sub_list_t
    {
        uint32_t timediff;                /*!< The interval time from the previous packet (ms) */
        rmt_rx_done_event_data_t symbols; /*!< Received RMT symbols */
        SLIST_ENTRY(ir_learn_sub_list_t)
        next; /*!< Pointer to the next packet */
    };

    /**
     * @cond Doxy command to hide preprocessor definitions from docs
     *
     * @brief The head of a singly-linked list for IR learn cmd packets.
     *
     */
    SLIST_HEAD(ir_learn_sub_list_head, ir_learn_sub_list_t);
    /**
     * @endcond
     */

    /**
     * @brief The head of a list of infrared (IR) learn data packets.
     *
     */
    struct ir_learn_list_t
    {
        struct ir_learn_sub_list_head cmd_sub_node; /*!< Package head of every cmd */
        SLIST_ENTRY(ir_learn_list_t)
        next; /*!< Pointer to the next packet */
    };

    /**
     * @cond Doxy command to hide preprocessor definitions from docs
     *
     * @brief The head of a singly-linked list for IR learn data packets.
     *
     */
    SLIST_HEAD(ir_learn_list_head, ir_learn_list_t);
    /**
     * @endcond
     */

    /**
     * @brief IR learn result user callback.
     *
     * @param[out] state IR learn step
     * @param[out] sub_step Interval less than 500 ms, we think it's the same command
     * @param[out] data Command list of this step
     *
     */
    typedef void (*ir_learn_result_cb)(ir_learn_state_t state, uint8_t sub_step, struct ir_learn_sub_list_head *data);

    /**
     * @brief IR learn configuration
     */
    typedef struct
    {
        rmt_clock_source_t clk_src; /*!< RMT clock source */
        uint32_t resolution;        /*!< RMT resolution, in Hz */

        int learn_count;             /*!< IR learn count needed */
        int learn_gpio;              /*!< IR learn io that consumed by the sensor */
        ir_learn_result_cb callback; /*!< IR learn result callback for user */

        int task_priority; /*!< IR learn task priority */
        int task_stack;    /*!< IR learn task stack size */
        int task_affinity; /*!< IR learn task pinned to core (-1 is no affinity) */
    } ir_learn_cfg_t;

    typedef struct
    {
        rmt_channel_handle_t channel_rx; /*!< rmt rx channel handler */
        rmt_rx_done_event_data_t rmt_rx; /*!< received RMT symbols */

        struct ir_learn_list_head learn_list;
        struct ir_learn_sub_list_head learn_result;

        EventGroupHandle_t learn_event;
        SemaphoreHandle_t rmt_mux;
        QueueHandle_t receive_queue; /*!< A queue used to send the raw data to the task from the ISR */
        bool running;
        uint32_t pre_time;

        uint8_t learn_count;
        uint8_t learned_count;
        uint8_t learned_sub;

    } ir_learn_t;

    typedef struct
    {
        ir_learn_result_cb user_cb;
        ir_learn_t *ctx;
    } ir_learn_common_param_t;

    /**
     * @brief Create new IR learn handle.
     *
     * @param[in]  cfg Config for IR learn
     * @param[out] handle_out New IR learn handle
     * @return
     *          - ESP_OK                  Device handle creation success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     *          - ESP_ERR_NO_MEM          Memory allocation failed.
     *
     */
    esp_err_t ir_learn_new(const ir_learn_cfg_t *cfg, ir_learn_handle_t *handle_out);

    /**
     * @brief Restart IR learn process.
     *
     * @param[in] ir_learn_hdl IR learn handle
     * @return
     *          - ESP_OK                  Restart process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     *
     */
    esp_err_t ir_learn_restart(ir_learn_handle_t ir_learn_hdl);

    /**
     * @brief Stop IR learn process.
     * @note Delete all
     *
     * @param[in] ir_learn_hdl IR learn handle
     * @return
     *          - ESP_OK                  Stop process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     *
     */
    esp_err_t ir_learn_stop(ir_learn_handle_t *ir_learn_hdl);

    /**
     * @brief Add IR learn list node, every new learn list will create it.
     *
     * @param[in] learn_head IR learn list head
     * @return
     *          - ESP_OK                  Create learn list success.
     *          - ESP_ERR_NO_MEM          Memory allocation failed.
     *
     */
    esp_err_t ir_learn_add_list_node(struct ir_learn_list_head *learn_head);

    /**
     * @brief Add IR learn sub step list node, every sub step should be added.
     *
     * @param[in] sub_head IR learn sub step list head
     * @param[in] timediff Time diff between each sub step
     * @param[in] symbol symbols of each sub step
     * @return
     *          - ESP_OK                  Create learn list success.
     *          - ESP_ERR_NO_MEM          Memory allocation failed.
     *
     */
    esp_err_t ir_learn_add_sub_list_node(struct ir_learn_sub_list_head *sub_head,
                                         uint32_t timediff, const rmt_rx_done_event_data_t *symbol);

    /**
     * @brief Delete IR learn list node, will recursively delete sub steps.
     *
     * @param[in] learn_head IR learn list head
     *          - ESP_OK                  Stop process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     *
     */
    esp_err_t ir_learn_clean_data(struct ir_learn_list_head *learn_head);

    /**
     * @brief Delete sub steps.
     *
     * @param[in] sub_head IR learn sub list head
     *          - ESP_OK                  Stop process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     *
     */
    esp_err_t ir_learn_clean_sub_data(struct ir_learn_sub_list_head *sub_head);

    /**
     * @brief Add IR learn list node, every new learn list will create it.
     *
     * @param[in] learn_head IR learn list head
     * @param[out] result_out IR learn result
     * @return
     *          - ESP_OK                  Get learn result process.
     *          - ESP_ERR_INVALID_SIZE    Size error.
     *
     */
    esp_err_t ir_learn_check_valid(struct ir_learn_list_head *learn_head,
                                   struct ir_learn_sub_list_head *result_out);

    /**
     * @brief Print the RMT symbols.
     *
     * @param[in] cmd_list IR learn list head
     *          - ESP_OK                  Stop process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     *
     */
    esp_err_t ir_learn_print_raw(struct ir_learn_sub_list_head *cmd_list);

    /**
     * @brief Stop IR RX.
     * @param
     *          - ESP_OK                  Stop process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     */
    void ir_rx_stop(void);

    /**
     * @brief Restart IR RX.
     * @param [in] learn_param IR learn common parameters
     *          - ESP_OK                  Restart process success.
     *          - ESP_ERR_INVALID_ARG     Invalid device handle or argument.
     */
    void ir_rx_restart(ir_learn_common_param_t *learn_param);

#ifdef __cplusplus
}
#endif
