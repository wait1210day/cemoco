/**
 * This file is part of cemoco.
 *
 * cemoco is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * cemoco is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cemoco. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CEMOCO_HOSTIF_H
#define CEMOCO_HOSTIF_H

#include "stm32g4xx_hal.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "defs.h"
#include "hermes_messages.h"
#include "periodic_dispatcher.h"


#define NB_PERIOD_SEND_JOBS 4

enum hostif_link_state
{
    HOSTIF_LINK_UP = 0,
    HOSTIF_LINK_DEGRADED,
    HOSTIF_LINK_BUS_OFF,
    HOSTIF_LINK_RECOVER_WAIT,
    HOSTIF_LINK_RECOVER_PROBE
};

struct hostif_link
{
#define HOSTIF_LINK_FLAGS_BUS_OFF      (1 << 1)
#define HOSTIF_LINK_FLAGS_ERR_PASSIVE  (1 << 2)
#define HOSTIF_LINK_FLAGS_ERR_WARN     (1 << 3)
#define HOSTIF_LINK_FLAGS_RX_OK        (1 << 4)

    volatile uint32_t isr_flags;

    enum hostif_link_state state;
    enum hostif_link_state prev_state;

    TickType_t last_rx_tick;
    TickType_t last_tx_tick;
    TickType_t last_error_tick;
    TickType_t next_recover_tick;

    uint32_t bus_off_count;
    uint32_t err_passive_count;

    uint8_t recover_backoff_step;

    uint8_t tx_enabled;

    // Statistics
    uint32_t drop_tx_fifo_full;
    uint32_t drop_tx_error;
};

struct hostif_context
{
    FDCAN_HandleTypeDef *hfdcan;
    QueueHandle_t rx_queue;
    uint32_t node_id;

    struct hostif_link link;

    struct perdis_job period_send_jobs[NB_PERIOD_SEND_JOBS];

    struct hermes_msg_elec_measures cached_elec_measures;
    struct hermes_msg_converter_stat cached_converter_stat;
};

struct hostif_config
{
    FDCAN_HandleTypeDef *hfdcan;
    uint32_t task_priority;
};

err_t hostif_init(struct hostif_context *ctx, const struct hostif_config *config);

void hostif_on_isr_fdcan_rxfifo0(struct hostif_context *ctx, uint32_t rxfifo0_its);
void hostif_on_isr_fdcan_error(struct hostif_context *ctx, uint32_t error_status_its);

#endif //CEMOCO_HOSTIF_H
