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

#ifndef CEMOCO_LL_HELPER_H
#define CEMOCO_LL_HELPER_H

#include "defs.h"
#include "stm32g4xx_ll_adc.h"
#include "stm32g4xx_ll_dma.h"


// Very thing wrapper for LL library, a stateless version of HAL.

err_t ll_helper_adc_disable(ADC_TypeDef *adc);
err_t ll_helper_adc_enable(ADC_TypeDef *adc);

err_t ll_helper_adc_start_calibration(ADC_TypeDef *adc, uint32_t single_diff);

err_t ll_helper_adc_start_dma_no_irq(ADC_TypeDef *adc,
                                     DMA_TypeDef *dma,
                                     uint32_t dma_channel,
                                     uint32_t dma_transfer_mode,
                                     uint8_t *addr,
                                     uint32_t count);

err_t ll_helper_adc_start_injected_irq(ADC_TypeDef *adc, uint8_t jeos);

#endif //CEMOCO_LL_HELPER_H
