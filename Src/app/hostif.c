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

#include <stdio.h>
#include <string.h>

#include "hostif_protocol.h"
#include "hostif.h"
#include "hermes.h"
#include "hermes_messages.h"
#include "fumi.h"


#define LOOP_INTERVAL_MS          10

#define SEND_INTERVAL_STAT_OUT    pdMS_TO_TICKS(100)
#define SEND_INTERVAL_STAT_IN     pdMS_TO_TICKS(100)
#define SEND_INTERVAL_PROT_EVENT  pdMS_TO_TICKS(500)
#define SEND_INTERVAL_STAT_MISC1  pdMS_TO_TICKS(1000)


static const char *kTAG = "hostif";

struct can_message
{
    uint32_t timestamp;
    uint32_t id;
    uint8_t data[8];
};

void hostif_on_isr_fdcan_rxfifo0(struct hostif_context *ctx, uint32_t rxfifo0_its)
{
    FDCAN_RxHeaderTypeDef rxhdr;
    struct can_message frame;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((rxfifo0_its & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == RESET)
        goto out;

    if (HAL_FDCAN_GetRxMessage(ctx->hfdcan, FDCAN_RX_FIFO0, &rxhdr, frame.data) != HAL_OK)
        goto out;

    if (rxhdr.DataLength != FDCAN_DLC_BYTES_8)
        goto out;

    frame.id = rxhdr.Identifier;
    frame.timestamp = xTaskGetTickCountFromISR();
    xQueueSendFromISR(ctx->rx_queue, &frame, &higher_priority_task_woken);

    ctx->link.last_rx_tick = xTaskGetTickCountFromISR();
    ctx->link.isr_flags |= HOSTIF_LINK_FLAGS_RX_OK;

out:
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void hostif_on_isr_fdcan_error(struct hostif_context *ctx, uint32_t error_status_its)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (error_status_its & FDCAN_IT_BUS_OFF)
        ctx->link.isr_flags |= HOSTIF_LINK_FLAGS_BUS_OFF;
    if (error_status_its & FDCAN_IT_ERROR_PASSIVE)
        ctx->link.isr_flags |= HOSTIF_LINK_FLAGS_ERR_PASSIVE;
    if (error_status_its & FDCAN_IT_ERROR_WARNING)
        ctx->link.isr_flags |= HOSTIF_LINK_FLAGS_ERR_WARN;

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void can_send(struct hostif_context *ctx, const struct can_message *frame)
{
    // The link management layer say we should NOT transmit packets
    if (!ctx->link.tx_enabled)
        return;

    if (ctx->hfdcan->Instance->CCCR & FDCAN_CCCR_INIT)
    {
        // Peripheral is not initialized or in error state, don't even try
        return;
    }

    FDCAN_TxHeaderTypeDef txhdr = {
        .Identifier = frame->id,
        .IdType = FDCAN_EXTENDED_ID,
        .TxFrameType = FDCAN_DATA_FRAME,
        .DataLength = FDCAN_DLC_BYTES_8,
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch = FDCAN_BRS_OFF,
        .FDFormat = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl = FDCAN_NO_TX_EVENTS,
        .MessageMarker = 0
    };

    uint32_t tick_start = HAL_GetTick();
    while (HAL_FDCAN_GetTxFifoFreeLevel(ctx->hfdcan) == 0)
    {
        // At 500kbps, a frame takes ~0.25ms. If it's full for 2ms, the bus
        // is likely dead.
        if ((HAL_GetTick() - tick_start) > 2)
        {
            ctx->link.drop_tx_fifo_full++;
            return;
        }
    }

    if (HAL_FDCAN_AddMessageToTxFifoQ(ctx->hfdcan, &txhdr, frame->data) != HAL_OK)
    {
        ctx->link.drop_tx_error++;
        return;
    }

    ctx->link.last_tx_tick = xTaskGetTickCount();
}

// Store a word (u16), long word (u32)
#define STW(dst, start_idx, data)                   \
    dst[start_idx + 0] = ((data) & 0xff00) >> 8;    \
    dst[start_idx + 1] = ((data) & 0x00ff) >> 0;

#define STL(dst, start_idx, data)                       \
    dst[start_idx + 0] = ((data) & 0xff000000) >> 24;   \
    dst[start_idx + 1] = ((data) & 0x00ff0000) >> 16;   \
    dst[start_idx + 2] = ((data) & 0x0000ff00) >> 8;    \
    dst[start_idx + 3] = ((data) & 0x000000ff) >> 0;

static void send_prot_faults(void *userdata)
{
    struct hostif_context *ctx = userdata;
    struct can_message frame = {0};
    frame.id = CAN_MAKE_ID(ID_PRI_EMER, ID_TYPE_PROT_EVENT, ID_DSTADDR_BROADCAST, ctx->node_id);

    uint32_t prots = ctx->cached_converter_stat.soft_prot_flags;
    uint32_t faults = ctx->cached_converter_stat.fault_flags;

    uint16_t flt = 0, swp = 0;
    if (prots & CONVSTAT_SOFTPROT_IN_UVP)
        swp |= HOSTIF_CAN_PROT_SWP_IN_UVP;
    if (prots & CONVSTAT_SOFTPROT_IN_OCP)
        swp |= HOSTIF_CAN_PROT_SWP_IN_OCP;
    if (prots & CONVSTAT_SOFTPROT_OUT_OVP)
        swp |= HOSTIF_CAN_PROT_SWP_OUT_OVP;
    if (prots & CONVSTAT_SOFTPROT_OUT_OCP)
        swp |= HOSTIF_CAN_PROT_SWP_OUT_OCP;
    if (prots & CONVSTAT_SOFTPROT_TEMP_DERATE)
        swp |= HOSTIF_CAN_PROT_SWP_DERATE;

    if (faults & CONVSTAT_FAULT_IN_OVP)
        flt |= HOSTIF_CAN_PROT_FLT_IN_OVP;
    if (faults & CONVSTAT_FAULT_OTP)
        flt |= HOSTIF_CAN_PROT_FLT_OTP;

    STW(frame.data, 2, swp)
    STW(frame.data, 6, flt)

    can_send(ctx, &frame);
}

static void send_telm_stat_out(void *userdata)
{
    struct hostif_context *ctx = userdata;
    const struct hermes_msg_converter_stat *converter_stat = &ctx->cached_converter_stat;
    const struct hermes_msg_elec_measures *elec_measures = &ctx->cached_elec_measures;

    struct can_message frame = {0};
    frame.id = CAN_MAKE_ID(ID_PRI_NORMAL, ID_TYPE_STAT_OUT, ID_DSTADDR_BROADCAST, ctx->node_id);

    if (converter_stat->active_loop == CTRLOOP_ACTIVE_LOOP_NONE)
        frame.data[1] = 0;
    else if (converter_stat->active_loop == CTRLOOP_ACTIVE_LOOP_BURST)
        frame.data[1] = 1;
    else if (converter_stat->active_loop == CTRLOOP_ACTIVE_LOOP_CV)
        frame.data[1] = 2;
    else if (converter_stat->active_loop == CTRLOOP_ACTIVE_LOOP_CC)
        frame.data[1] = 3;

    uint16_t volt = (uint16_t)(elec_measures->Vout * 1000.0f);
    uint16_t curr = (uint16_t)(elec_measures->Iout * 1000.0f);
    uint16_t power = (uint16_t)(elec_measures->Pout * 10.0f + 0.5f);
    STW(frame.data, 2, volt)
    STW(frame.data, 4, curr)
    STW(frame.data, 6, power)

    can_send(ctx, &frame);
}

static void send_telm_stat_in(void *userdata)
{
    struct hostif_context *ctx = userdata;
    const struct hermes_msg_elec_measures *elec_measures = &ctx->cached_elec_measures;

    struct can_message frame = {0};
    frame.id = CAN_MAKE_ID(ID_PRI_NORMAL, ID_TYPE_STAT_IN, ID_DSTADDR_BROADCAST, ctx->node_id);

    uint16_t volt = (uint16_t)(elec_measures->Vin * 1000.0f);
    uint16_t curr = (uint16_t)(elec_measures->Iin * 1000.0f);
    uint16_t power = (uint16_t)(elec_measures->Pin * 10.0f + 0.5f);
    STW(frame.data, 2, volt)
    STW(frame.data, 4, curr)
    STW(frame.data, 6, power)

    can_send(ctx, &frame);
}

static void send_telm_stat_misc1(void *userdata)
{
    struct hostif_context *ctx = userdata;
    const struct hermes_msg_converter_stat *converter_stat = &ctx->cached_converter_stat;
    const struct hermes_msg_elec_measures *elec_measures = &ctx->cached_elec_measures;

    struct can_message frame = {0};
    frame.id = CAN_MAKE_ID(ID_PRI_NORMAL, ID_TYPE_STAT_MISC1, ID_DSTADDR_BROADCAST, ctx->node_id);

    uint16_t eff = (uint16_t)(elec_measures->efficiency * 1000.0f + 0.5f);
    STW(frame.data, 0, eff)

    int8_t temp = (int8_t)elec_measures->temp;
    memcpy(&frame.data[2], &temp, 1);

    frame.data[3] = (uint8_t) (converter_stat->burst_ratio * 100.0f + 0.5f);

    can_send(ctx, &frame);
}

static const struct perdis_job_def g_period_send_jobs_def[NB_PERIOD_SEND_JOBS] = {
    { "send_prot_faults", SEND_INTERVAL_PROT_EVENT, send_prot_faults },
    { "send_telm_stat_out", SEND_INTERVAL_STAT_OUT, send_telm_stat_out },
    { "send_telm_stat_in", SEND_INTERVAL_STAT_IN, send_telm_stat_in },
    { "send_telm_stat_misc1", SEND_INTERVAL_STAT_MISC1, send_telm_stat_misc1 }
};

static void handle_rx_message(struct hostif_context *ctx, const struct can_message *frame)
{
    // printf("Got message: %lx\n\r", frame->id);

    uint32_t frame_type = CAN_ID_GET_TYPE(frame->id);
    const uint8_t *data = frame->data;

    switch (frame_type)
    {
#define DB(idx)        data[idx]
#define DW(idx, shift) ((uint16_t)data[idx] << shift)
#define DL(idx, shift) ((uint32_t)data[idx] << shift)

        case ID_TYPE_OUT_ENABLE:
        {
            struct hermes_msg_output_enable msg = {
                .enable = DB(7) ? 1 : 0
            };
            HERMES_PUBLISH(HERMES_TOPIC_OUTPUT_ENABLE, &msg);
            break;
        }
        case ID_TYPE_OUT_SET:
        {
            uint16_t field_cc_u16 = DW(2, 8) | DW(3, 0);
            uint16_t field_cv_u16 = DW(6, 8) | DW(7, 0);

            float cc = (float) field_cc_u16 / 1000.0f;
            float cv = (float) field_cv_u16 / 1000.0f;
            if (cc > CEMOCO_SYSCAP_MAX_CC || cv > CEMOCO_SYSCAP_MAX_CV ||
                cc < CEMOCO_SYSCAP_MIN_CC || cv < CEMOCO_SYSCAP_MIN_CV)
            {
                break;
            }

            struct hermes_msg_output_set msg = {.Vout = cv, .Iout = cc};
            HERMES_PUBLISH(HERMES_TOPIC_OUTPUT_SET, &msg);
            break;
        }

        default:
            break;
#undef DB
#undef DW
#undef DL
    }
}

static TickType_t can_backoff_ticks_by_step(int step)
{
    switch (step)
    {
        case 0: return pdMS_TO_TICKS(25);
        case 1: return pdMS_TO_TICKS(50);
        case 2: return pdMS_TO_TICKS(100);
        case 3: return pdMS_TO_TICKS(200);
        case 4: return pdMS_TO_TICKS(500);
        default:
            return pdMS_TO_TICKS(1000);
    }
}

static const char *get_link_state_name(enum hostif_link_state state)
{
    switch (state)
    {
        case HOSTIF_LINK_UP:            return "UP";
        case HOSTIF_LINK_DEGRADED:      return "DEGRADED";
        case HOSTIF_LINK_BUS_OFF:       return "BUS_OFF";
        case HOSTIF_LINK_RECOVER_WAIT:  return "RECOVER_WAIT";
        case HOSTIF_LINK_RECOVER_PROBE: return "RECOVER_PROBE";
    }
    return "UNKNOWN";
}

static const char *get_isr_flags_name(uint32_t isr_flags)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "[%s|%s|%s|%s]",
             isr_flags & HOSTIF_LINK_FLAGS_BUS_OFF ? "BO" : "__",
             isr_flags & HOSTIF_LINK_FLAGS_ERR_PASSIVE ? "EP" : "__",
             isr_flags & HOSTIF_LINK_FLAGS_ERR_WARN ? "EW" : "__",
             isr_flags & HOSTIF_LINK_FLAGS_RX_OK ? "OK" : "__");

    return buf;
}

static void can_link_poll(struct hostif_context *ctx)
{
    struct hostif_link *link = &ctx->link;
    TickType_t now = xTaskGetTickCount();

    // ISR flags is cleared here and the next ISR call will set them again.
    uint32_t isr_flags = link->isr_flags;
    link->isr_flags = 0;

    if (isr_flags & HOSTIF_LINK_FLAGS_BUS_OFF)
    {
        link->bus_off_count++;
        link->last_error_tick = now;
        link->tx_enabled = 0;
        link->state = HOSTIF_LINK_BUS_OFF;
    }
    else if (isr_flags & HOSTIF_LINK_FLAGS_ERR_PASSIVE)
    {
        link->err_passive_count++;
        link->last_error_tick = now;
        if (link->state == HOSTIF_LINK_UP)
            link->state = HOSTIF_LINK_DEGRADED;
    }

    switch (link->state)
    {
        case HOSTIF_LINK_UP:
            link->tx_enabled = 1;
            break;

        case HOSTIF_LINK_DEGRADED:
            link->tx_enabled = 1;
            // If no error occurred and a valid packet was received during the past 500ms,
            // the bus likely has become normal.
            if ((now - link->last_error_tick) > pdMS_TO_TICKS(500) &&
                (now - link->last_rx_tick) < pdMS_TO_TICKS(500))
            {
                link->state = HOSTIF_LINK_UP;
            }
            break;

        case HOSTIF_LINK_BUS_OFF:
            link->next_recover_tick =
                now + can_backoff_ticks_by_step(link->recover_backoff_step);

            link->recover_backoff_step++;
            link->state = HOSTIF_LINK_RECOVER_WAIT;
            break;

        case HOSTIF_LINK_RECOVER_WAIT:
            link->tx_enabled = 0;
            if (now >= link->next_recover_tick)
            {
                // According to G474 ref manual: "In initialization mode, the CAN does not monitor
                // the FDCAN_RX signal, and therefore cannot complete the recovery sequence.
                // To recover from an error state, the CAN must operate in normal mode."
                //
                // And there is no need to stop/start or reinitialize the controller.
                // See ST doc RM0440, page 1966, section 44.3.5 "Error management" for more details.
                CLEAR_BIT(ctx->hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);

                link->state = HOSTIF_LINK_RECOVER_PROBE;
                link->last_error_tick = now;
            }
            break;

        case HOSTIF_LINK_RECOVER_PROBE:
            // In PROBE state, transmit is still suppressed and we continuously observe the
            // bus activity. If we receive some valid packets or no errors occur for a while,
            // we bring the link up again.
            link->tx_enabled = 0;

            if ((now - link->last_rx_tick) < pdMS_TO_TICKS(50) ||
                (now - link->last_error_tick) > pdMS_TO_TICKS(100))
            {
                link->state = HOSTIF_LINK_UP;
                link->tx_enabled = 1;
                link->recover_backoff_step = 0;
            }
            break;
    }

    if (link->state != link->prev_state)
    {
        FUMI_LOGD(kTAG, "link state changed: %s -> %s, isr_flags=%s",
                  get_link_state_name(link->prev_state),
                  get_link_state_name(link->state),
                  get_isr_flags_name(isr_flags));
    }
    link->prev_state = link->state;
}

CEMOCO_NORETURN static void hostif_task(void *param)
{
    struct hostif_context *ctx = param;

    QueueHandle_t rq1 = HERMES_CREATE_QUEUE(1);
    QueueHandle_t rq2 = HERMES_CREATE_QUEUE(1);
    hermes_subscribe_overwrite(HERMES_TOPIC_CONVERTER_STAT, rq1);
    hermes_subscribe_overwrite(HERMES_TOPIC_ELEC_MEASURES, rq2);

    uint32_t last_softprot_flags = 0, last_fault_flags = 0;

    struct hermes_message hermes_msg;
    struct can_message frame;
    struct hermes_msg_elec_measures *msg_elec_measures = &ctx->cached_elec_measures;
    struct hermes_msg_converter_stat *msg_converter_stat = &ctx->cached_converter_stat;
    while (1)
    {
        if (HERMES_QUEUE_RECEIVE(rq1, &hermes_msg, 0))
        {
            if (hermes_msg.topic == HERMES_TOPIC_CONVERTER_STAT)
                memcpy(msg_converter_stat, hermes_msg.payload, hermes_msg.payload_size);
        }

        if (HERMES_QUEUE_RECEIVE(rq2, &hermes_msg, 0))
        {
            if (hermes_msg.topic == HERMES_TOPIC_ELEC_MEASURES)
                memcpy(msg_elec_measures, hermes_msg.payload, hermes_msg.payload_size);
        }

        // If protection and fault states change, we must notify others immediately.
        if (msg_converter_stat->soft_prot_flags != last_softprot_flags ||
            msg_converter_stat->fault_flags != last_fault_flags)
        {
            send_prot_faults(ctx);
        }

        perdis_run_all(ctx->period_send_jobs, NB_PERIOD_SEND_JOBS);

        can_link_poll(ctx);

        last_softprot_flags = msg_converter_stat->soft_prot_flags;
        last_fault_flags = msg_converter_stat->fault_flags;

        while (xQueueReceive(ctx->rx_queue, &frame, 0) == pdTRUE)
            handle_rx_message(ctx, &frame);

        vTaskDelay(pdMS_TO_TICKS(LOOP_INTERVAL_MS));
    }
}

err_t hostif_init(struct hostif_context *ctx, const struct hostif_config *config)
{
    uint32_t halerr = HAL_OK;
    UBaseType_t task_creation_res = pdTRUE;
    err_t err = ERR_STATUS_SUCCESS;

    memset(ctx, 0, sizeof(struct hostif_context));

    // TODO(masshiroio): the Node ID should be determined by hardware strapping
    //                   or read from EEPROM, and 0x20 is just for debugging now.
    ctx->node_id = 0x20;

    for (int i = 0; i < NB_PERIOD_SEND_JOBS; i++)
        perdis_job_init(&ctx->period_send_jobs[i], &g_period_send_jobs_def[i], ctx);

    ctx->hfdcan = config->hfdcan;
    ctx->rx_queue = xQueueCreate(8, sizeof(struct can_message));
    if (ctx->rx_queue == NULL)
        return ERR_STATUS_NO_MEM;

    // Config FDCAN filter
    FDCAN_FilterTypeDef can_flt_config = {0};
    can_flt_config.IdType = FDCAN_EXTENDED_ID;
    can_flt_config.FilterType = FDCAN_FILTER_MASK;
    can_flt_config.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;

    can_flt_config.FilterIndex = 0;
    can_flt_config.FilterID1 = CAN_MAKE_ID(0, 0, ctx->node_id, 0);
    can_flt_config.FilterID2 = CAN_MAKE_ID(0, 0, 0xff, 0);
    if (HAL_FDCAN_ConfigFilter(ctx->hfdcan, &can_flt_config) != HAL_OK)
    {
        err = ERR_STATUS_FAIL;
        goto fail;
    }

    can_flt_config.FilterIndex = 1;
    can_flt_config.FilterID1 = CAN_MAKE_ID(0, 0, ID_DSTADDR_BROADCAST, 0);
    can_flt_config.FilterID2 = CAN_MAKE_ID(0, 0, 0xff, 0);
    if (HAL_FDCAN_ConfigFilter(ctx->hfdcan, &can_flt_config) != HAL_OK)
    {
        err = ERR_STATUS_FAIL;
        goto fail;
    }

    halerr = HAL_FDCAN_ConfigGlobalFilter(
        ctx->hfdcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);
    if (halerr != HAL_OK)
    {
        err = ERR_STATUS_FAIL;
        goto fail;
    }

    // Config interrupts and enable FDCAN
    halerr = HAL_FDCAN_ActivateNotification(
        ctx->hfdcan,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF
        | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING,
        0
    );
    if (halerr != HAL_OK)
    {
        err = ERR_STATUS_FAIL;
        goto fail;
    }

    halerr = HAL_FDCAN_Start(ctx->hfdcan);
    if (halerr != HAL_OK)
    {
        err = ERR_STATUS_FAIL;
        goto fail;
    }

    task_creation_res = xTaskCreate(
        hostif_task, "hostif_task", 1024, ctx, config->task_priority, NULL);
    if (task_creation_res != pdPASS)
    {
        err = ERR_STATUS_NO_MEM;
        goto fail;
    }

    return ERR_STATUS_SUCCESS;

fail:
    vQueueDelete(ctx->rx_queue);
    return err;
}
