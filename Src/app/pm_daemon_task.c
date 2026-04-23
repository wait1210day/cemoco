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
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"

#include "pm_daemon_task.h"
#include "hermes_messages.h"
#include "main.h"

#define PMD_TEMP_DERATING_EFF_APPROX 0.85f

static void compute_temp_derating(int32_t temp, const struct sensors_measures *measures,
                                  float *Vout, float *Iout)
{
    if (temp < CEMOCO_SOFTPROT_TEMP_DERATE_BEGIN)
        return;
    float rating_ratio = 1 - (float)(temp - CEMOCO_SOFTPROT_TEMP_DERATE_BEGIN)
                       / (CEMOCO_FAULT_OTP - CEMOCO_SOFTPROT_TEMP_DERATE_BEGIN);

    float max_Iin = rating_ratio * CEMOCO_SYSCAP_MAX_IN_CURR;
    float max_Iout1 = rating_ratio * CEMOCO_SYSCAP_MAX_OUT_CURR;
    if (measures->Vout <= 1.0f)
    {
        *Iout = max_Iout1;
        return;
    }

    float max_Iout2 = PMD_TEMP_DERATING_EFF_APPROX * max_Iin * measures->Vin / measures->Vout;
    float max_Iout = fminf(max_Iout1, max_Iout2);

    *Iout = fminf(*Iout, max_Iout);
}

static uint8_t softprot_need_shutdown_output(uint32_t softprot_flags)
{
    return (softprot_flags & (uint32_t) CONVSTAT_SOFTPROT_IN_OCP) ||
           (softprot_flags & (uint32_t) CONVSTAT_SOFTPROT_IN_UVP) ||
           (softprot_flags & (uint32_t) CONVSTAT_SOFTPROT_OUT_OCP) ||
           (softprot_flags & (uint32_t) CONVSTAT_SOFTPROT_OUT_OVP);
}

CEMOCO_NORETURN static void pmd_task(void *param)
{
    struct pmd_context *ctx = param;

    struct ctrloop_context *lpctx = &ctx->ctrloop;
    struct sensors_context *sensors = &ctx->sensors;

    QueueHandle_t recvqueue = HERMES_CREATE_QUEUE(10);
    struct hermes_message recvmsg;
    hermes_subscribe(HERMES_TOPIC_OUTPUT_SET, recvqueue);
    hermes_subscribe(HERMES_TOPIC_OUTPUT_ENABLE, recvqueue);
    hermes_subscribe(HERMES_TOPIC_SOFTPROT_SET, recvqueue);
    hermes_subscribe(HERMES_TOPIC_SOFTPROT_CLEAR, recvqueue);

    uint8_t output_enable = 0;
    enum convstat_softprot softprot_flags = CONVSTAT_SOFTPROT_NONE;
    enum convstat_fault fault_flags = CONVSTAT_FAULT_NONE;

    struct hermes_msg_softprot_conf softprot_conf = {
        // Default softprot presets
        .output_ovp = CEMOCO_SOFTPROT_OUT_OVP_MAX,
        .output_ocp = CEMOCO_SOFTPROT_OUT_OCP_MAX,
        .input_uvp = CEMOCO_SOFTPROT_IN_UVP_MIN,
        .input_ocp = CEMOCO_SOFTPROT_IN_OCP_MAX
    };

    float target_Vout = 5.0f, target_Iout = 3.0f;
    float sp_Vout = target_Vout, sp_Iout = target_Iout;

    uint16_t uvp_trig_count = 0;

    ctrloop_start(lpctx);
    while (1)
    {
        struct ctrloop_measures ctrloop_measures = ctrloop_get_measures(lpctx);
        sensors_update(sensors, &ctrloop_measures);
        struct sensors_measures measures = sensors_get_measures(sensors);

        sp_Vout = target_Vout;
        sp_Iout = target_Iout;

        if (sp_Vout > measures.Vin - 2.0f)
            sp_Vout = measures.Vin - 2.0f;

        // Hard-fault protections: cycle-by-cycle OCP, OTP, temp derating
        if (ctrloop_get_clear_cbcocp_flag(lpctx))
        {
            // Cycle-by-cycle overcurrent protection was triggered!
            // CBCOCP should be treated as a system fault that should never occur under
            // normal operations. When this flag (`CTRLOOP_STATE_FLAG_OCP`) is raised,
            // the output has been shut down directly by the hardware signal path
            // (analog comparator => HRTIM fault line). We just notify others of this
            // event and enter protection locked state here.
            fault_flags |= (uint32_t) CONVSTAT_FAULT_CBCOCP;
        }

        if (measures.temp >= CEMOCO_FAULT_OTP)
            fault_flags |= (uint32_t) CONVSTAT_FAULT_OTP;
        else if (measures.temp >= CEMOCO_SOFTPROT_TEMP_DERATE_BEGIN)
        {
            compute_temp_derating(measures.temp, &measures, &sp_Vout, &sp_Iout);
            softprot_flags |= (uint32_t) CONVSTAT_SOFTPROT_TEMP_DERATE;
        }
        else
        {
            softprot_flags &= ~(uint32_t) CONVSTAT_SOFTPROT_TEMP_DERATE;
        }

        // Soft protections
        if (measures.Vin <= softprot_conf.input_uvp)
        {
            uvp_trig_count++;
            if (uvp_trig_count >= 200)
                softprot_flags |= (uint32_t) CONVSTAT_SOFTPROT_IN_UVP;
        }
        else
        {
            uvp_trig_count = 0;
        }

        if (measures.Iin >= softprot_conf.input_ocp)
            softprot_flags |= (uint32_t) CONVSTAT_SOFTPROT_IN_OCP;
        if (measures.Vout >= softprot_conf.output_ovp)
            softprot_flags |= (uint32_t) CONVSTAT_SOFTPROT_OUT_OVP;
        if (measures.Iout >= softprot_conf.output_ocp)
            softprot_flags |= (uint32_t) CONVSTAT_SOFTPROT_OUT_OCP;

        if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_RESET)
            ctrloop_set_force_ccm(lpctx, 0);
        else
            ctrloop_set_force_ccm(lpctx, 1);

        // Enable or disable output / set output parameters
        if (output_enable && !fault_flags && !softprot_need_shutdown_output(softprot_flags))
        {
            ctrloop_set_setpoints(lpctx, sp_Vout, sp_Iout);
            ctrloop_enable_output(lpctx, 1);
        }
        else
        {
            ctrloop_enable_output(lpctx, 0);
        }


        // Bus messaging handling
        struct hermes_msg_converter_stat msg_stat = {
            .active_loop = ctrloop_measures.active_loop,
            .burst_ratio = ctrloop_measures.burst_ratio,
            .soft_prot_flags = softprot_flags,
            .fault_flags = fault_flags
        };
        HERMES_PUBLISH(HERMES_TOPIC_CONVERTER_STAT, &msg_stat);

        struct hermes_msg_elec_measures msg_elec_measures = {
            .iL = ctrloop_measures.iL,
            .Vin = measures.Vin,
            .Iin = measures.Iin,
            .Pin = measures.Pin,
            .Vout = measures.Vout,
            .Iout = measures.Iout,
            .Pout = measures.Pout,
            .efficiency = measures.efficiency,
            .temp = measures.temp,
            .Iin_raw = measures.Iin_raw
        };
        HERMES_PUBLISH(HERMES_TOPIC_ELEC_MEASURES, &msg_elec_measures);

        if (HERMES_QUEUE_RECEIVE(recvqueue, &recvmsg, pdMS_TO_TICKS(5)) != pdPASS)
            continue;

        if (recvmsg.topic == HERMES_TOPIC_OUTPUT_SET)
        {
            struct hermes_msg_output_set *data =
                HERMES_PAYLOAD_CAST(&recvmsg, struct hermes_msg_output_set);
            target_Vout = data->Vout;
            target_Iout = data->Iout;
        }
        else if (recvmsg.topic == HERMES_TOPIC_OUTPUT_ENABLE)
        {
            struct hermes_msg_output_enable *data =
                HERMES_PAYLOAD_CAST(&recvmsg, struct hermes_msg_output_enable);
            output_enable = data->enable;
        }
        else if (recvmsg.topic == HERMES_TOPIC_SOFTPROT_SET)
        {
            struct hermes_msg_softprot_set *data =
                HERMES_PAYLOAD_CAST(&recvmsg, struct hermes_msg_softprot_set);
            memcpy(&softprot_conf, data, sizeof(softprot_conf));
        }
        else if (recvmsg.topic == HERMES_TOPIC_SOFTPROT_CLEAR)
        {
            struct hermes_msg_softprot_clear *data =
                HERMES_PAYLOAD_CAST(&recvmsg, struct hermes_msg_softprot_clear);
            softprot_flags &= ~data->flags_to_clear;
        }
    }
}


err_t pmd_start_task(struct pmd_context *ctx, const struct pmd_config *config)
{
    err_t err = ctrloop_init(&ctx->ctrloop, &config->ctrloop_config);
    if (err != ERR_STATUS_SUCCESS)
        return err;

    err = sensors_init(&ctx->sensors, &config->sensors_config);
    if (err != ERR_STATUS_SUCCESS)
        return err;

    BaseType_t result = xTaskCreate(
        pmd_task, "pmd_task", 1024, ctx, config->task_priority, NULL);
    if (result != pdPASS)
        return ERR_STATUS_FAIL;

    return ERR_STATUS_SUCCESS;
}
