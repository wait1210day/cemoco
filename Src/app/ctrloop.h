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

#ifndef CEMOCO_CTRLOOP_H
#define CEMOCO_CTRLOOP_H

#include "stm32g4xx_ll_adc.h"
#include "stm32g4xx_hal.h"

#include "FreeRTOS.h"
#include "event_groups.h"

#include "defs.h"
#include "cali.h"

enum ctrloop_active_loop
{
    CTRLOOP_ACTIVE_LOOP_NONE,
    CTRLOOP_ACTIVE_LOOP_BURST,
    CTRLOOP_ACTIVE_LOOP_CV,
    CTRLOOP_ACTIVE_LOOP_CC
};

enum ctrloop_control_method
{
    CTRLOOP_CONTROL_METHOD_CCM = 0,
    CTRLOOP_CONTROL_METHOD_BURST
};

enum ctrloop_state_flags
{
    CTRLOOP_STATE_FLAG_RAMPING = 0x01,
    CTRLOOP_STATE_FLAG_BURST_EXIT_BLANKING = 0x02
};

struct ctrloop_measures
{
    enum ctrloop_active_loop active_loop;
    uint32_t state_flags;

    float Vout;
    float Iout;
    float iL;
    float Vin;

    uint16_t Iin_raw;
    uint16_t temp_raw;

    float burst_ratio;
};

enum ctrloop_state
{
    CTRLOOP_STATE_STANDBY = 0,
    CTRLOOP_STATE_RUN
};

enum ctrloop_burst_mode_state
{
    CTRLOOP_BURST_MODE_STATE_IDLE = 0,
    CTRLOOP_BURST_MODE_STATE_ISSUING
};

#define CTRLOOP_EVGROUP_CBCOCP  0x01

struct ctrloop_config
{
    // Basic hardware
    HRTIM_HandleTypeDef *hhrtim;
    ADC_TypeDef *ll_adc1;
    ADC_TypeDef *ll_adc2;
    DMA_TypeDef *ll_adc2_dma;
    uint32_t adc2_dma_channel;

    COMP_HandleTypeDef *hcomp_pcmc;
    DAC_HandleTypeDef *hdac_pcmc;
    uint32_t dac_pcmc_channel;

    // ADC calibration data. Also used to calculate DAC values.
    const struct cali_params *cali_params;
};

struct ctrloop_context
{
    // By placing all the state variables that needed by loop ISR
    // in this structure and passing its pointer can actually
    // significantly decrease the jitter of ISR's execution time
    // with zero extra overhead, compared with placing them all in global.

    HRTIM_HandleTypeDef *hhrtim;
    HRTIM_TypeDef *ll_hrtim;
    ADC_TypeDef *ll_adc1;
    ADC_TypeDef *ll_adc2;
    DAC_HandleTypeDef *hdac_pcmc;
    uint32_t dac_pcmc_channel;
    uint32_t ll_dac_pcmc_channel;

    EventGroupHandle_t event_group;

    uint16_t adc2_rawbuf[3];
    uint32_t isr_count;

    float Iout_prev;

    // CV/CC loop setpoints
    volatile float Vout_ref;
    float Vout_ref_internal;
    volatile float Iout_ref;

    // CCM mode states
    float cv_integral;
    float cc_integral;
    uint32_t burst_enter_req_count;
    volatile uint8_t force_ccm;

    // burst mode states
    enum ctrloop_burst_mode_state burst_state;
    uint8_t burst_nb_issued_pulses;
    uint32_t burst_total_cycle_count;
    uint32_t burst_idle_cycle_count;
    float burst_ratio;
    uint32_t burst_exit_req_count;

    enum ctrloop_active_loop active_loop;
    enum ctrloop_control_method next_cycle_loop;

    volatile enum ctrloop_state loop_state;
    volatile uint32_t loop_state_flags;

    struct cali_params cali_params;
    volatile struct ctrloop_measures measures;
};

/**
 * This function performs the following initialization steps:
 * 1. calibrate and enable ADCs (LL driver)
 * 2. initialize HRTIM
 */
err_t ctrloop_init(struct ctrloop_context *lpctx, const struct ctrloop_config *config);

/**
 * Start the ADC interruption, which calls the high frequency control loop.
 */
void ctrloop_start(struct ctrloop_context *lpctx);

void ctrloop_set_setpoints(struct ctrloop_context *lpctx, float cv, float cc);

/**
 * Alternate between Force-CCM mode and Auto mode (Burst mode & CCM mode mixed).
 * It controls the current flow of the converter's inductor in light load condition.
 * It is only allowed to change the mode when the loop is in STANDBY state.
 *
 * @param lpctx         Context structure.
 * @param force_ccm     Force CCM mode (1) or Auto mode.
 * @return              `ERR_STATUS_FAIL` if in RUN state.
 */
err_t ctrloop_set_force_ccm(struct ctrloop_context *lpctx, uint8_t force_ccm);

void ctrloop_enable_output(struct ctrloop_context *lpctx, uint8_t enable);

static inline uint8_t ctrloop_get_clear_cbcocp_flag(struct ctrloop_context *lpctx)
{
    uint32_t test_bits = CTRLOOP_EVGROUP_CBCOCP;
    uint32_t bits = xEventGroupWaitBits(lpctx->event_group, test_bits, pdTRUE, pdTRUE, 0);
    return (bits & CTRLOOP_EVGROUP_CBCOCP);
}

struct ctrloop_measures ctrloop_get_measures(struct ctrloop_context *lpctx);

void ctrloop_isr_on_adc_fastpath(struct ctrloop_context *lpctx);
void ctrloop_isr_on_hrtim_fault(struct ctrloop_context *lpctx);

#endif //CEMOCO_CTRLOOP_H
