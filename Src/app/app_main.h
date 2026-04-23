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

#ifndef CEMOCO_APP_MAIN_H
#define CEMOCO_APP_MAIN_H

#include "stm32g4xx_hal.h"

struct app_context
{
    UART_HandleTypeDef *huart2;
    HRTIM_HandleTypeDef *hhrtim1;
    TIM_HandleTypeDef *htim8;
    DAC_HandleTypeDef *hdac1;
    DAC_HandleTypeDef *hdac3;
    COMP_HandleTypeDef *hcomp2;
    OPAMP_HandleTypeDef *hopamp3;
    FDCAN_HandleTypeDef *hfdcan1;
    TIM_HandleTypeDef *htim17;
};

#ifndef CEMOCO_APP_MAIN_IMPL
// Only required global objects are exported.

// Required by IRQ handler `stm32g44_it.c`
extern struct pmd_context g_pmd;
#endif

void app_main(struct app_context *appctx);

#endif //CEMOCO_APP_MAIN_H
