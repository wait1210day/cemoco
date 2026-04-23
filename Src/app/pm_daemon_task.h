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

#ifndef CEMOCO_PM_DAEMON_TASK_H
#define CEMOCO_PM_DAEMON_TASK_H

#include "defs.h"
#include "ctrloop.h"
#include "sensors.h"

struct pmd_config
{
    uint32_t task_priority;
    struct ctrloop_config ctrloop_config;
    struct sensors_config sensors_config;
};

struct pmd_context
{
    struct ctrloop_context ctrloop;
    struct sensors_context sensors;
};


/**
 * Initializes `ctrloop` and `sensors` instances, and start the
 * Power Management Daemon (PMD) task. PMD manages the core power
 * control algorithm `ctrloop` and handles the filtering/postprocess
 * of critical measurements like input/output voltage and current.
 *
 * This task employs Hermes messaging bus.
 *
 * @hermes[publishes]
 *   HERMES_TOPIC_CONVERTER_ALERT every ~5ms
 *   HERMES_TOPIC_ELEC_MEASURES every ~5ms
 *   HERMES_TOPIC_CONVERTER_STAT every ~5ms
 *
 * @hermes[subscribes]
 *   HERMES_TOPIC_OUTPUT_ENABLE
 *   HERMES_TOPIC_OUTPUT_SET
 */
err_t pmd_start_task(struct pmd_context *ctx, const struct pmd_config *config);


/**
 * Hard realtime ISR bypasses FreeRTOS and will be dispatched to ctrloop module
 * directly as soon as possible. These interruptions have the highest hardware (NVIC)
 * interruption priority and will never be preempted by the OS scheduler and runs
 * in its full speed.
 * See `ctrloop` module for more details.
 */

static inline void pmd_rtisr_on_adc_fastpath(struct pmd_context *ctx)
{
    ctrloop_isr_on_adc_fastpath(&ctx->ctrloop);
}

static inline void pmd_rtisr_on_hrtim_fault(struct pmd_context *ctx)
{
    ctrloop_isr_on_hrtim_fault(&ctx->ctrloop);
}

#endif //CEMOCO_PM_DAEMON_TASK_H
