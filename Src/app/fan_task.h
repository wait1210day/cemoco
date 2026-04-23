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

#ifndef CEMOCO_FAN_TASK_H
#define CEMOCO_FAN_TASK_H

#include <stdint.h>

#include "stm32g4xx_hal_tim.h"
#include "FreeRTOS.h"
#include "task.h"

#include "defs.h"

struct fan_curve_table_entry
{
    int32_t temp;
    float duty;
};

struct fan_curve_table
{
    uint32_t nb_entries;
    const struct fan_curve_table_entry *sorted;
};

struct fan_task_config
{
    const struct fan_curve_table *lut;
    TIM_HandleTypeDef *tim;
    uint32_t tim_channel;
    // TIM counting period, should be `ARR + 1`
    uint32_t tim_period;
    uint32_t task_priority;
};

struct fan_task_ctx
{
    struct fan_task_config config;
    TaskHandle_t task_handle;
};

err_t fan_task_init(struct fan_task_ctx *ctx, const struct fan_task_config *config);

#endif //CEMOCO_FAN_TASK_H
