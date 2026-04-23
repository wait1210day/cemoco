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

#include "FreeRTOS.h"
#include "task.h"

#include "periodic_dispatcher.h"

void perdis_job_init(struct perdis_job *job, const struct perdis_job_def *def, void *userdata)
{
    job->def = def;
    job->last_called_tick = 0;
    job->userdata = userdata;
}

void perdis_job_run(struct perdis_job *job)
{
    const struct perdis_job_def *def = job->def;
    TickType_t current_tick = xTaskGetTickCount();

    if (current_tick - job->last_called_tick >= def->period_ticks)
    {
        def->handler(job->userdata);
        job->last_called_tick = current_tick;
    }
}

void perdis_run_all(struct perdis_job *jobs, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
        perdis_job_run(&jobs[i]);
}
