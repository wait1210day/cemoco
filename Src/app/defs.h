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

#ifndef CEMOCO_DEFS_H
#define CEMOCO_DEFS_H

#define arraysize_of(x) (sizeof(x) / sizeof(x[0]))

#define CEMOCO_ALWAYS_INLINE __attribute__((always_inline))

enum error_status
{
    ERR_STATUS_SUCCESS = 0,
    ERR_STATUS_FAIL,
    ERR_STATUS_INVALID_ARG,
    ERR_STATUS_NO_MEM
};

typedef enum error_status err_t;


#define CEMOCO_SYSCAP_MAX_OUT_CURR  25.0f
#define CEMOCO_SYSCAP_MAX_OUT_VOLT  36.0f
#define CEMOCO_SYSCAP_MAX_IN_VOLT   36.0f
#define CEMOCO_SYSCAP_MAX_IN_CURR   25.0f

#define CEMOCO_SYSCAP_MIN_CC        1.0f
#define CEMOCO_SYSCAP_MAX_CC        25.0f
#define CEMOCO_SYSCAP_MIN_CV        1.0f
#define CEMOCO_SYSCAP_MAX_CV        30.0f

// Protection thresholds
#define CEMOCO_SOFTPROT_IN_UVP_MIN  10.00f
#define CEMOCO_SOFTPROT_IN_UVP_MAX  40.00f
#define CEMOCO_SOFTPROT_IN_OCP_MIN   1.00f
#define CEMOCO_SOFTPROT_IN_OCP_MAX  25.00f

#define CEMOCO_SOFTPROT_OUT_OVP_MIN 10.00f
#define CEMOCO_SOFTPROT_OUT_OVP_MAX 40.00f
#define CEMOCO_SOFTPROT_OUT_OCP_MIN  1.00f
#define CEMOCO_SOFTPROT_OUT_OCP_MAX 25.00f

#define CEMOCO_SOFTPROT_TEMP_DERATE_BEGIN      75
#define CEMOCO_FAULT_OTP                       90

// ADC channel maps in DMA buffer
#define ADC1_BUFIDX_VOUT    0
#define ADC1_BUFIDX_IOUT    1
#define ADC2_BUFIDX_IIN     0
#define ADC2_BUFIDX_VIN     1
#define ADC2_BUFIDX_TEMP    2

// The smallest input power reading that we can compute efficiency
#define CEMOCO_EFF_MEASURE_P_IN_THRESH   5.0f

#define CEMOCO_CCMRAM_TEXT  __attribute__((__section__(".ccmram.text")))
#define CEMOCO_CCMRAM_DATA  __attribute__((__section__(".ccmram.data")))
#define CEMOCO_NORETURN     __attribute__((__noreturn__))
#define CEMOCO_MAYBE_UNUSED __attribute__((__unused__))

#define CEMOCO_TASK_PRIORITY_FUMI   5
#define CEMOCO_TASK_PRIORITY_SPAWN  18
#define CEMOCO_TASK_PRIORITY_PMD    17
#define CEMOCO_TASK_PRIORITY_LEDS   10
#define CEMOCO_TASK_PRIORITY_HOSTIF 15
#define CEMOCO_TASK_PRIORITY_FAN    16

#endif //CEMOCO_DEFS_H
