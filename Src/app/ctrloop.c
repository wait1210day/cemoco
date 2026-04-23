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

#include <math.h>
#include <string.h>

#include "stm32g4xx_ll_adc.h"
#include "stm32g4xx_ll_hrtim.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_dac.h"

#include "defs.h"
#include "ctrloop.h"
#include "main.h"
#include "ll_helper.h"

#define CTRLOOP_TIMER_PERIOD        27200 // 43520

// Timer A compare 3: about 100ns after the timer PERIOD event to set PWM HIGH.
//
// According to ST's application note AN5497: This is the event that is used to
// set the PWM output HIGH. A small delay is required from the beginning of the
// switching period as if the comparator trip event must be cleared before the output
// can be set HIGH by one of the set sources.
#define CTRLOOP_TIM_A_CMP3          543

// Timer A compare 4: comparator event blanking time (~150ns) after the PWM rising
// egde settled by CMP3 (CMP4 - CMP3 = (1359 - 543) * 184ps = ~150ns).
#define CTRLOOP_TIM_A_CMP4          1359

#define CTRLOOP_OUTPUTS             (LL_HRTIM_OUTPUT_TA1 | LL_HRTIM_OUTPUT_TA2)

#define CTRLOOP_BURST_ENTER_COUNT   500
#define CTRLOOP_BURST_EXIT_COUNT    80

#define CTRLOOP_BURST_DUTY_K        1.02f
#define CTRLOOP_BURST_NB_PULSES     1
#define CTRLOOP_BURST_RARIO_SAMPLE_CYCLES 200

// Inductance in Henry, used to estimate burst mode threshold current
#define CTRLOOP_L_NOMINAL            0.000010f
#define CTRLOOP_BURST_K_MARGIN_ENTER 0.2f
#define CTRLOOP_BURST_K_MARGIN_EXIT  0.3f
#define CTRLOOP_BURST_K_MARGIN_EXIT_CC 0.6f

#define CTRLOOP_ISR_COUNT_PERIOD    100
#define CTRLOOP_IREF_MAX            40.0f
#define CTRLOOP_IREF_MIN            (-1.0f)

#define CTRLOOP_CV_RAMPING_STEP     0.005f

static const float k_fsw = 200.0f * 1000.0f;
static const float k_t_loop = 1.0f / k_fsw;

// Outer CV loop is relatively fast to acquire better step response.
// For the voltage error of 1V, it requires `k_cv_kp` amps of inductor current,
// including slope compensation.
static const float k_cv_kp = 50.0f;
static const float k_cv_ki_discrete = 380000.0f * k_t_loop;

// Outer CC loop is relatively slow to be stable.
// For the current error of 1A, it requires `k_cc_kp` amps of inductor current.
static const float k_cc_kp = 1.0f;
static const float k_cc_ki_discrete = 20000.0f * k_t_loop;


#define INISR                               CEMOCO_ALWAYS_INLINE static inline
#define SET_LOOP_STATE_FLAG(var, flag)      var |= (uint32_t)(flag)
#define CLEAR_LOOP_STATE_FLAG(var, flag)    var &= ~((uint32_t)(flag))


INISR float clamp(float x, float min_, float max_)
{
    if (x < min_)
        return min_;
    if (x > max_)
        return max_;
    return x;
}

INISR void ctrloop_reset_loop_internal_states(struct ctrloop_context *lpctx)
{
    lpctx->Vout_ref_internal = 0;
    lpctx->cv_integral = 0;
    lpctx->cc_integral = 0;
    lpctx->burst_enter_req_count = 0;

    lpctx->burst_nb_issued_pulses = 0;
    lpctx->burst_state = CTRLOOP_BURST_MODE_STATE_IDLE;
    lpctx->burst_total_cycle_count = 0;
    lpctx->burst_idle_cycle_count = 0;
    lpctx->burst_ratio = 0;
    lpctx->burst_exit_req_count = 0;
}

static void set_burst_mode_conf(struct ctrloop_context *lpctx)
{
    HRTIM_TypeDef *hrtim = lpctx->ll_hrtim;

    // Disable output first, to prevent unexpected glitches or shoot-through
    LL_HRTIM_DisableOutput(hrtim, CTRLOOP_OUTPUTS);

    // Compare 1: Reset source of TA1, controls the duty cycle in burst mode
    LL_HRTIM_TIM_SetCompare1(hrtim, LL_HRTIM_TIMER_A, (uint32_t)(0.05f * CTRLOOP_TIMER_PERIOD));
    // Compare 3: Set source of TA1
    LL_HRTIM_TIM_SetCompare3(hrtim, LL_HRTIM_TIMER_A, 0);

    // Disable the peak-current control. Current comparator will play the role of
    // cycle-by-cycle current limitation comparator in burst mode.
    float k1 = lpctx->cali_params.adc_iL_coeffs[0];
    float k2 = lpctx->cali_params.adc_iL_coeffs[1];

    float reset_val_f = (CTRLOOP_IREF_MAX - k2) / k1;
    if (reset_val_f >= 4095)
        reset_val_f = 4095;
    if (reset_val_f <= 0)
        reset_val_f = 0;

    DAC_TypeDef *dac = lpctx->hdac_pcmc->Instance;
    LL_DAC_SetWaveSawtoothResetData(dac, lpctx->ll_dac_pcmc_channel, (uint32_t) reset_val_f);
    LL_DAC_SetWaveSawtoothStepData(dac, lpctx->ll_dac_pcmc_channel, 0);

    // Fire!
    LL_HRTIM_BM_Start(hrtim);
    LL_HRTIM_EnableOutput(hrtim, CTRLOOP_OUTPUTS);
}

static void set_fpwm_mode_conf(struct ctrloop_context *lpctx)
{
    HRTIM_TypeDef *hrtim = lpctx->ll_hrtim;
    LL_HRTIM_DisableOutput(hrtim, CTRLOOP_OUTPUTS);
    LL_HRTIM_BM_Stop(hrtim);

    // Compare 1: Reset source of TA1, limits the maximum duty cycle in PCMC mode
    LL_HRTIM_TIM_SetCompare1(hrtim, LL_HRTIM_TIMER_A, (uint32_t)(0.96f * CTRLOOP_TIMER_PERIOD));
    // Compare 3: Set source of TA1, clears the COMP event and sets the PWM
    LL_HRTIM_TIM_SetCompare3(hrtim, LL_HRTIM_TIMER_A, CTRLOOP_TIM_A_CMP3);

    LL_HRTIM_EnableOutput(hrtim, CTRLOOP_OUTPUTS);
}

INISR float update_outer_cv_loop(struct ctrloop_context *lpctx, float Vout, float Iout)
{
    // A ramping-up preprocess
    if (lpctx->Vout_ref > lpctx->Vout_ref_internal)
    {
        lpctx->Vout_ref_internal += CTRLOOP_CV_RAMPING_STEP;
        if (lpctx->Vout_ref_internal > lpctx->Vout_ref)
            lpctx->Vout_ref_internal = lpctx->Vout_ref;
    }
    else if (lpctx->Vout_ref < lpctx->Vout_ref_internal)
    {
        lpctx->Vout_ref_internal -= CTRLOOP_CV_RAMPING_STEP;
        if (lpctx->Vout_ref_internal < lpctx->Vout_ref)
            lpctx->Vout_ref_internal = lpctx->Vout_ref;
    }

    if (lpctx->Vout_ref_internal == lpctx->Vout_ref)
        CLEAR_LOOP_STATE_FLAG(lpctx->loop_state_flags, CTRLOOP_STATE_FLAG_RAMPING);
    else
        SET_LOOP_STATE_FLAG(lpctx->loop_state_flags, CTRLOOP_STATE_FLAG_RAMPING);

    float Vout_err = lpctx->Vout_ref_internal - Vout;
    lpctx->cv_integral += Vout_err * k_cv_ki_discrete;
    lpctx->cv_integral = clamp(lpctx->cv_integral, CTRLOOP_IREF_MIN, CTRLOOP_IREF_MAX);

    return clamp(k_cv_kp * Vout_err + lpctx->cv_integral + 0.2f * Iout, CTRLOOP_IREF_MIN, CTRLOOP_IREF_MAX);
}

INISR float update_outer_cc_loop(struct ctrloop_context *lpctx, float Iout)
{
    float Iout_err = lpctx->Iout_ref - Iout;
    lpctx->cc_integral += Iout_err * k_cc_ki_discrete;
    lpctx->cc_integral = clamp(lpctx->cc_integral, CTRLOOP_IREF_MIN, CTRLOOP_IREF_MAX);

    return clamp(k_cc_kp * Iout_err + lpctx->cc_integral, CTRLOOP_IREF_MIN, CTRLOOP_IREF_MAX);
}

INISR void update_peak_current_sawtooth(struct ctrloop_context *lpctx, float peak_iref, float Vout)
{
    float k1 = lpctx->cali_params.adc_iL_coeffs[0];
    float k2 = lpctx->cali_params.adc_iL_coeffs[1];

    float reset_val_f = (peak_iref - k2) / k1;
    if (reset_val_f >= 4095)
        reset_val_f = 4095;
    if (reset_val_f <= 0)
        reset_val_f = 0;

    float slope_amps_per_usec = 1.6f * Vout / (CTRLOOP_L_NOMINAL * 1000000.0f);
    float slope_lsb_per_usec = slope_amps_per_usec / k1;

    // HRTIM triggers DAC step in every 46ns (0.046us), and LSB/us * us/step = LSB/step.
    float step_val_f = slope_lsb_per_usec * 0.046f;

    // `LL_DAC_SetWaveSawtoothStepData()` actually uses Q12.5 format
    // representation for `StepData` parameter. So we must x16 to get the
    // correct value to be written into register.
    uint32_t step_val_raw = (uint32_t)(step_val_f * 16.0f);

    if (step_val_raw >= 0xffff)
        step_val_raw = 0xffff;
    if (step_val_raw <= 16)
        step_val_raw = 16;

    DAC_TypeDef *dac = lpctx->hdac_pcmc->Instance;
    LL_DAC_SetWaveSawtoothResetData(dac, lpctx->ll_dac_pcmc_channel, (uint32_t) reset_val_f);
    LL_DAC_SetWaveSawtoothStepData(dac, lpctx->ll_dac_pcmc_channel, step_val_raw);
}

// Continuous Conduction PWM mode
INISR void run_fpwm_mode(struct ctrloop_context *lpctx, float Vin, float Vout, float Iout)
{
    float cv_Iref_result = update_outer_cv_loop(lpctx, Vout, Iout);
    float cc_Iref_result = update_outer_cc_loop(lpctx, Iout);

    // Clamping of integrator has been done in `update_outer_xxx()` function.
    // It is also guaranteed that both `cv_Iref_result` and `cc_Iref_result`
    // will never exceed the `CTRLOOP_IREF_MAX`.

    float peak_iref = 0;
    if (cv_Iref_result <= cc_Iref_result)
    {
        peak_iref = cv_Iref_result;
        lpctx->active_loop = CTRLOOP_ACTIVE_LOOP_CV;
    }
    else
    {
        peak_iref = cc_Iref_result;
        lpctx->active_loop = CTRLOOP_ACTIVE_LOOP_CC;
    }

    // Update slope compensation
    update_peak_current_sawtooth(lpctx, peak_iref, Vout);

    // Burst mode is allowed only when:
    //   - Force CCM is disabled
    //   - Ramping is not in the process
    //   - CV loop is active
    if (!lpctx->force_ccm && !(lpctx->loop_state_flags & CTRLOOP_STATE_FLAG_RAMPING)
        && lpctx->active_loop == CTRLOOP_ACTIVE_LOOP_CV)
    {
        // An estimated load current threshold that makes the valley (minimum) inductor
        // current < 0. See comments in function `ctrloop_run_burst_mode()` for more details.
        const float burst_enter_I_thresh =
            (Vin - Vout) * Vout / (2.0f * k_fsw * CTRLOOP_L_NOMINAL * Vin)
            * CTRLOOP_BURST_K_MARGIN_ENTER;

        if (Iout < burst_enter_I_thresh)
            lpctx->burst_enter_req_count++;
        else
            lpctx->burst_enter_req_count = 0;

        // Exiting CCM and entering BURST checkpoint
        if (lpctx->burst_enter_req_count >= CTRLOOP_BURST_ENTER_COUNT)
        {
            ctrloop_reset_loop_internal_states(lpctx);
            set_burst_mode_conf(lpctx);
            lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_BURST;
        }
        else
            lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_CCM;
    }
    else
        lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_CCM;
}

INISR void run_burst_mode(struct ctrloop_context *lpctx, float Vin, float Vout, float Iout)
{
    float basic_duty = lpctx->Vout_ref / Vin * CTRLOOP_BURST_DUTY_K;
    basic_duty = clamp(basic_duty, 0.03f, 0.95f);

    LL_HRTIM_TIM_SetCompare1(
        lpctx->ll_hrtim, LL_HRTIM_TIMER_A, (uint32_t)(basic_duty * CTRLOOP_TIMER_PERIOD));

    LL_HRTIM_BM_SetPeriod(lpctx->ll_hrtim, 1);

    if (Vout < lpctx->Vout_ref)
    {
        LL_HRTIM_BM_SetCompare(lpctx->ll_hrtim, 0);
    }
    else
    {
        LL_HRTIM_BM_SetCompare(lpctx->ll_hrtim, 1);
    }

    // Burst-mode exiting checkpoint

    // Calculate the theoretical threshold current for the CCM/DCM boundary.
    // 1. Inductor peak-to-peak ripple current:
    //    delta_Ipp = (Vin - Vout) * D / (L * fsw)
    //
    // 2. Inductor valley current relates to average current (I_avg) as:
    //    I_valley = I_avg - (delta_Ipp / 2)
    //
    // 3. The critical boundary between DCM and CCM occurs when I_valley = 0.
    //    Setting I_valley = 0 and solving for I_avg yields the threshold:
    //    I_avg_thresh = delta_Ipp / 2
    //                 = (Vin - Vout) * D / (2 * L * fsw)
    //
    // 4. Substituting D = Vout / Vin (ideal Buck approximation):
    //    I_avg_thresh = (Vin - Vout) * Vout / (2 * L * fsw * Vin)
    //
    // -----------------------------------------------------------------------------
    // Hysteresis
    // -----------------------------------------------------------------------------
    // Hysteresis is applied to prevent mode oscillation (chattering):
    //   - Enter Burst Mode: Iout < I_avg_thresh * K_ENTER
    //   - Exit Burst Mode:  Iout > I_avg_thresh * K_EXIT
    //
    // Where K_EXIT (CTRLOOP_BURST_K_MARGIN_EXIT) sets the exit margin.

    const float burst_exit_I_thresh = (Vin - Vout) * Vout
                                      / (2.0f * k_fsw * CTRLOOP_L_NOMINAL * Vin)
                                      * CTRLOOP_BURST_K_MARGIN_EXIT;
    if (Iout > burst_exit_I_thresh)
        lpctx->burst_exit_req_count++;
    else
        lpctx->burst_exit_req_count = 0;


    if (lpctx->burst_exit_req_count >= CTRLOOP_BURST_EXIT_COUNT
        || Vout < (lpctx->Vout_ref - 1.0f)
        || Iout > lpctx->Iout_ref * CTRLOOP_BURST_K_MARGIN_EXIT_CC
        || lpctx->force_ccm)
    {
        // Switch from BURST to CCM mode
        ctrloop_reset_loop_internal_states(lpctx);

        lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_CCM;
        // Forcing CV loop to start regulation from real load current improves
        // transient performance (decrease voltage drop).
        lpctx->cv_integral = Iout;
        lpctx->cc_integral = lpctx->Iout_ref;
        lpctx->Vout_ref_internal = lpctx->Vout_ref;

        set_fpwm_mode_conf(lpctx);
    }
    else
    {
        lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_BURST;
    }


    lpctx->active_loop = CTRLOOP_ACTIVE_LOOP_BURST;
}

CEMOCO_CCMRAM_TEXT void ctrloop_isr_on_adc_fastpath(struct ctrloop_context *lpctx)
{
    if (!LL_ADC_IsActiveFlag_JEOS(lpctx->ll_adc1))
        return;

    LL_GPIO_SetOutputPin(CTRLOOP_DBG_PULSE_GPIO_Port, CTRLOOP_DBG_PULSE_Pin);

    LL_ADC_ClearFlag_JEOS(lpctx->ll_adc1);
    const uint32_t Vout_raw = LL_ADC_INJ_ReadConversionData12(lpctx->ll_adc1, LL_ADC_INJ_RANK_1);
    const uint32_t Iout_raw = LL_ADC_INJ_ReadConversionData12(lpctx->ll_adc1, LL_ADC_INJ_RANK_2);

    const float Vin = (float) lpctx->adc2_rawbuf[ADC2_BUFIDX_VIN] * lpctx->cali_params.adc_Vin_coeffs[0]
                    + lpctx->cali_params.adc_Vin_coeffs[1];
    float Iout = (float) Iout_raw * lpctx->cali_params.adc_Iout_coeffs[0] + lpctx->cali_params.adc_Iout_coeffs[1];

    // Simple low-pass filter.
    Iout = lpctx->Iout_prev * 0.7f + Iout * 0.3f;
    lpctx->Iout_prev = Iout;

    const float Vout = (float) Vout_raw * lpctx->cali_params.adc_Vout_coeffs[0]
                     + lpctx->cali_params.adc_Vout_coeffs[1]
                     - Iout * lpctx->cali_params.vout_drop_compensation_k;


    lpctx->active_loop = CTRLOOP_ACTIVE_LOOP_NONE;
    if (lpctx->loop_state != CTRLOOP_STATE_RUN)
        ctrloop_reset_loop_internal_states(lpctx);

    if (lpctx->loop_state != CTRLOOP_STATE_RUN)
    {
        // Make the internal CV reference (after ramping) follow the
        // realtime output voltage to avoid a "valley" when starting up
        // with output capacitor partially charged (pre-biased).
        //
        // This variable is only used by CCM PWM operational mode.
        lpctx->Vout_ref_internal = Vout;
    }

    if (lpctx->loop_state == CTRLOOP_STATE_RUN)
    {
        if (lpctx->next_cycle_loop == CTRLOOP_CONTROL_METHOD_BURST)
            run_burst_mode(lpctx, Vin, Vout, Iout);
        else if (lpctx->next_cycle_loop == CTRLOOP_CONTROL_METHOD_CCM)
            run_fpwm_mode(lpctx, Vin, Vout, Iout);
    }

    // Update shared measurements
    lpctx->isr_count++;
    if (lpctx->isr_count >= CTRLOOP_ISR_COUNT_PERIOD)
    {
        lpctx->measures.Iout = Iout;
        lpctx->measures.Vout = Vout;
        lpctx->measures.Vin = Vin;
        lpctx->measures.Iin_raw = lpctx->adc2_rawbuf[ADC2_BUFIDX_IIN];
        lpctx->measures.temp_raw = lpctx->adc2_rawbuf[ADC2_BUFIDX_TEMP];
        lpctx->measures.active_loop = lpctx->active_loop;
        lpctx->measures.state_flags = lpctx->loop_state_flags;
        lpctx->measures.burst_ratio = lpctx->burst_ratio;
        lpctx->isr_count = 0;
    }

    LL_GPIO_ResetOutputPin(CTRLOOP_DBG_PULSE_GPIO_Port, CTRLOOP_DBG_PULSE_Pin);
}

err_t ctrloop_init(struct ctrloop_context *lpctx, const struct ctrloop_config *config)
{
    memset(lpctx, 0, sizeof(struct ctrloop_context));

    lpctx->hhrtim = config->hhrtim;
    lpctx->ll_hrtim = config->hhrtim->Instance;
    lpctx->ll_adc1 = config->ll_adc1;
    lpctx->ll_adc2 = config->ll_adc2;
    lpctx->hdac_pcmc = config->hdac_pcmc;
    lpctx->dac_pcmc_channel = config->dac_pcmc_channel;
    switch (lpctx->dac_pcmc_channel)
    {
        case DAC_CHANNEL_1: lpctx->ll_dac_pcmc_channel = LL_DAC_CHANNEL_1; break;
        case DAC_CHANNEL_2: lpctx->ll_dac_pcmc_channel = LL_DAC_CHANNEL_2; break;
        default:
            return ERR_STATUS_INVALID_ARG;
    }
    memcpy(&lpctx->cali_params, config->cali_params, sizeof(lpctx->cali_params));

    lpctx->event_group = xEventGroupCreate();
    if (!lpctx->event_group)
        return ERR_STATUS_NO_MEM;

    err_t result = ll_helper_adc_start_dma_no_irq(
        lpctx->ll_adc2,
        config->ll_adc2_dma,
        config->adc2_dma_channel,
        LL_ADC_REG_DMA_TRANSFER_UNLIMITED,
        (uint8_t *) lpctx->adc2_rawbuf,
        arraysize_of(lpctx->adc2_rawbuf)
    );
    if (result != ERR_STATUS_SUCCESS)
        return result;

    if (HAL_DAC_Start(config->hdac_pcmc, config->dac_pcmc_channel) != HAL_OK)
        return ERR_STATUS_FAIL;

    if (HAL_COMP_Start(config->hcomp_pcmc) != HAL_OK)
        return ERR_STATUS_FAIL;

    // Initialize setpoints (CV 2.5V, CC 5.0A)
    lpctx->Vout_ref = 2.5f;
    lpctx->Iout_ref = 5.0f;
    lpctx->loop_state = CTRLOOP_STATE_STANDBY;
    lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_CCM;

    // Initialize internal states.
    // This function manipulates non-volatile variables out of the
    // ISR context, but the ISR has not been running yet, so it can be
    // considered safe.
    ctrloop_reset_loop_internal_states(lpctx);

    return ERR_STATUS_SUCCESS;
}

void ctrloop_start(struct ctrloop_context *lpctx)
{
    // Master timer compare 1: ADC trigger
    // TODO(masshiroio): trigger ADC at the central point of PWM
    LL_HRTIM_TIM_SetCompare1(lpctx->ll_hrtim, LL_HRTIM_TIMER_MASTER, 2100);

    LL_HRTIM_ForceUpdate(lpctx->ll_hrtim, LL_HRTIM_TIMER_MASTER);

    ll_helper_adc_enable(lpctx->ll_adc1);
    ll_helper_adc_start_injected_irq(lpctx->ll_adc1, 1);

    LL_HRTIM_TIM_CounterEnable(lpctx->ll_hrtim, LL_HRTIM_TIMER_A | LL_HRTIM_TIMER_MASTER);
    LL_HRTIM_BM_Enable(lpctx->ll_hrtim);
}

void ctrloop_enable_output(struct ctrloop_context *lpctx, uint8_t enable)
{
    if (enable && lpctx->loop_state != CTRLOOP_STATE_RUN)
    {
        lpctx->loop_state = CTRLOOP_STATE_RUN;

        // We start from force-pwm mode to get a smooth voltage ramp and
        // establish stable output. Then the converter will switch to burst mode
        // automatically if light load is detected.
        lpctx->next_cycle_loop = CTRLOOP_CONTROL_METHOD_CCM;
        set_fpwm_mode_conf(lpctx);

        LL_HRTIM_EnableOutput(lpctx->ll_hrtim, LL_HRTIM_OUTPUT_TA1 | LL_HRTIM_OUTPUT_TA2);
    }
    else if (!enable && lpctx->loop_state == CTRLOOP_STATE_RUN)
    {
        LL_HRTIM_DisableOutput(lpctx->ll_hrtim, LL_HRTIM_OUTPUT_TA1 | LL_HRTIM_OUTPUT_TA2);
        lpctx->loop_state = CTRLOOP_STATE_STANDBY;
    }
}

void ctrloop_set_setpoints(struct ctrloop_context *lpctx, float cv, float cc)
{
    lpctx->Vout_ref = clamp(cv, CEMOCO_SYSCAP_MIN_CV, CEMOCO_SYSCAP_MAX_CV);
    lpctx->Iout_ref = clamp(cc, CEMOCO_SYSCAP_MIN_CC, CEMOCO_SYSCAP_MAX_CC);
}

err_t ctrloop_set_force_ccm(struct ctrloop_context *lpctx, uint8_t force_ccm)
{
    if (lpctx->loop_state != CTRLOOP_STATE_STANDBY)
        return ERR_STATUS_FAIL;

    lpctx->force_ccm = force_ccm;
    return ERR_STATUS_SUCCESS;
}

struct ctrloop_measures ctrloop_get_measures(struct ctrloop_context *lpctx)
{
    return lpctx->measures;
}

void ctrloop_isr_on_hrtim_fault(struct ctrloop_context *lpctx)
{
}
