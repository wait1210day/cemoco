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


#include "stm32g4xx_ll_adc.h"
#include "ll_helper.h"

// Reference: ST HAL library

#define LL_HELPER_ADC_DISABLE_TIMEOUT   2ul

// Fixed timeout value for ADC calibration.
// Values defined to be higher than the worst case: low clock frequency
// and maximum prescalers.
// See also: `ADC_CALIBRATION_TIMEOUT` in `stm32g4xx_hal_adc_ex.c`.
#define LL_HELPER_ADC_CALI_TIMEOUT      318978ul

err_t ll_helper_adc_disable(ADC_TypeDef *adc)
{
    if (!LL_ADC_IsEnabled(adc) || LL_ADC_IsDisableOngoing(adc))
        return ERR_STATUS_SUCCESS;

    LL_ADC_Disable(adc);

    // Wait for ADC effectively disabled with a given timeout
    uint32_t tickstart = HAL_GetTick();
    while ((adc->CR & ADC_CR_ADEN) != 0ul)
    {
        if ((HAL_GetTick() - tickstart) > LL_HELPER_ADC_DISABLE_TIMEOUT)
        {
            // Check again to avoid false timeout detection in case of preemption
            if ((adc->CR & ADC_CR_ADEN) != 0ul)
                return ERR_STATUS_FAIL;
        }
    }

    return ERR_STATUS_SUCCESS;
}

err_t ll_helper_adc_enable(ADC_TypeDef *adc)
{
    if (LL_ADC_IsEnabled(adc) == 0)
    {
        LL_ADC_ClearFlag_ADRDY(adc);
        LL_ADC_Enable(adc);
        while (LL_ADC_IsActiveFlag_ADRDY(adc) == 0)
            ;
    }
    return ERR_STATUS_SUCCESS;
}

err_t ll_helper_adc_start_calibration(ADC_TypeDef *adc, uint32_t single_diff)
{
    if (ll_helper_adc_disable(adc) != ERR_STATUS_SUCCESS)
        return ERR_STATUS_FAIL;

    // Enable the ADC voltage regulator and wait for it becoming stable.
    LL_ADC_EnableInternalRegulator(adc);
    HAL_Delay(10);

    LL_ADC_StartCalibration(adc, single_diff);

    // Wait for calibration completion
    uint32_t loop_count = 0;
    while (LL_ADC_IsCalibrationOnGoing(adc) != 0ul)
    {
        loop_count++;
        if (loop_count >= LL_HELPER_ADC_CALI_TIMEOUT)
            return ERR_STATUS_FAIL;
    }
    return ERR_STATUS_SUCCESS;
}


err_t ll_helper_adc_start_dma_no_irq(ADC_TypeDef *adc,
                                     DMA_TypeDef *dma,
                                     uint32_t dma_channel,
                                     uint32_t dma_transfer_mode,
                                     uint8_t *addr,
                                     uint32_t count)
{
    LL_DMA_DisableChannel(dma, dma_channel);
    // Clear flags: transfer completion (TC), half transfer (HT),
    //              transfer error (TE)
    switch (dma_channel)
    {
#define CASE_FOR_DMA_CH_X(x)             \
        case LL_DMA_CHANNEL_##x:         \
            LL_DMA_ClearFlag_TC##x(dma); \
            LL_DMA_ClearFlag_HT##x(dma); \
            LL_DMA_ClearFlag_TE##x(dma); \
            break;

        CASE_FOR_DMA_CH_X(1)
        CASE_FOR_DMA_CH_X(2)
        CASE_FOR_DMA_CH_X(3)
        CASE_FOR_DMA_CH_X(4)
        CASE_FOR_DMA_CH_X(5)
        CASE_FOR_DMA_CH_X(6)
        CASE_FOR_DMA_CH_X(7)
        CASE_FOR_DMA_CH_X(8)

        default:
            return ERR_STATUS_FAIL;
#undef CASE_FOR_DMA_CH_X
    }

    LL_DMA_SetMemoryAddress(dma, dma_channel, (uint32_t) addr);
    LL_DMA_SetPeriphAddress(
        dma, dma_channel, LL_ADC_DMA_GetRegAddr(adc, LL_ADC_DMA_REG_REGULAR_DATA));
    LL_DMA_SetDataLength(dma, dma_channel, count);

    LL_DMA_EnableChannel(dma, dma_channel);

    LL_ADC_ClearFlag_ADRDY(adc);
    LL_ADC_REG_SetDMATransfer(adc, dma_transfer_mode);
    LL_ADC_Enable(adc);

    while (LL_ADC_IsActiveFlag_ADRDY(adc) == 0ul)
    {
        // Just wait for ADC ready
    }
    LL_ADC_REG_StartConversion(adc);

    return ERR_STATUS_SUCCESS;
}

err_t ll_helper_adc_start_injected_irq(ADC_TypeDef *adc, uint8_t jeos)
{
    LL_ADC_ClearFlag_JEOC(adc);
    LL_ADC_ClearFlag_JEOS(adc);

    // Enable the interruption of injected END of conversion
    if (jeos)
        LL_ADC_EnableIT_JEOS(adc);
    else
        LL_ADC_EnableIT_JEOC(adc);

    LL_ADC_INJ_StartConversion(adc);
    return ERR_STATUS_SUCCESS;
}
