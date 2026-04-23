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

#include <string.h>
#include <assert.h>
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"

#include "fumi.h"
#include "hermes.h"
#include "hermes_messages.h"
#include "fan_task.h"


// The 2-wire PWM fan works in open loop mode. 1s period is quite enough
// for temperature curve based control.
#define FAN_TASK_PERIOD_MS 1000

static const char *kTAG = "fan_task";

static float compute_duty_from_temp(const struct fan_curve_table *lut, int32_t temp)
{
    const struct fan_curve_table_entry *p1 = &lut->sorted[0];
    const struct fan_curve_table_entry *p2 = &lut->sorted[lut->nb_entries - 1];

    if (temp <= p1->temp)
        return p1->duty;
    if (temp >= p2->temp)
        return p2->duty;

    for (uint32_t i = 0; i < lut->nb_entries - 1; i++)
    {
        // Linear interpolation
        p1 = &lut->sorted[i];
        p2 = &lut->sorted[i + 1];
        if (temp >= p1->temp && temp <= p2->temp)
        {
            if (p1->temp == p2->temp)
                return (p1->duty + p2->duty) / 2.0f;
            float dt = (float)(p2->temp - p1->temp);

            float dduty = p2->duty - p1->duty;

            return ((float)(temp - p1->temp)) / dt * dduty + p1->duty;
        }
    }

    return -1.0f;
}

static void fan_task(void *param)
{
    struct fan_task_ctx *ctx = param;
    assert(ctx);

    FUMI_LOGI(kTAG, "fan task started");

    QueueHandle_t hq_sensors = HERMES_CREATE_QUEUE(1);
    hermes_subscribe_overwrite(HERMES_TOPIC_ELEC_MEASURES, hq_sensors);

    struct hermes_message msg;

    float duty = 0.0f;
    float last_reported_duty = 0.0f;

    while (1)
    {
        if (HERMES_QUEUE_RECEIVE(hq_sensors, &msg, 0))
        {
            assert(msg.payload_size == sizeof(struct hermes_msg_elec_measures));
            int32_t temp = ((struct hermes_msg_elec_measures *) msg.payload)->temp;

            duty = compute_duty_from_temp(ctx->config.lut, temp);

            if (duty < 0)
            {
                FUMI_LOGE(kTAG, "failed to compute FAN duty from temp %ld degC, setting to full-speed", temp);
                duty = 1.0f;
            }
        }

        uint32_t ccr = (uint32_t)(duty * (float) ctx->config.tim_period);
        if (ccr > 0xffff)
            ccr = 0xffff;
        __HAL_TIM_SET_COMPARE(ctx->config.tim, ctx->config.tim_channel, ccr);

        if (fabsf(duty - last_reported_duty) >= 0.01f)
        {
            FUMI_LOGI(kTAG, "FAN duty changed %lu%%", (uint32_t)(duty * 100.0f));
            last_reported_duty = duty;
        }

        vTaskDelay(pdMS_TO_TICKS(FAN_TASK_PERIOD_MS));
    }
}

err_t fan_task_init(struct fan_task_ctx *ctx, const struct fan_task_config *config)
{
    if (!ctx || !config)
        return ERR_STATUS_INVALID_ARG;
    if (!config->lut || !config->tim)
        return ERR_STATUS_INVALID_ARG;
    if (config->lut->nb_entries < 1)
        return ERR_STATUS_INVALID_ARG;

    memcpy(&ctx->config, config, sizeof(struct fan_task_config));

    // Initialize TIM pwm generation
    if (HAL_TIM_PWM_Init(config->tim) != HAL_OK)
    {
        FUMI_LOGE(kTAG, "failed to init TIM PWM mode");
        return ERR_STATUS_FAIL;
    }

    // Setting CCR to 0 before starting the PWM prevents unexpected PWM output
    // during system initialization.
    __HAL_TIM_SET_COMPARE(config->tim, config->tim_channel, 0);
    if (HAL_TIM_PWM_Start(config->tim, config->tim_channel) != HAL_OK)
    {
        FUMI_LOGE(kTAG, "failed to start TIM PWM generation");
        return ERR_STATUS_FAIL;
    }


    UBaseType_t task_creation_result = xTaskCreate(
         fan_task, "fan_task", 512, ctx, config->task_priority, &ctx->task_handle);
    if (task_creation_result != pdPASS)
    {
        FUMI_LOGE(kTAG, "failed to create task: out of memory");
        HAL_TIM_PWM_Stop(config->tim, config->tim_channel);
        HAL_TIM_PWM_DeInit(config->tim);
        return ERR_STATUS_NO_MEM;
    }

    return ERR_STATUS_SUCCESS;
}
