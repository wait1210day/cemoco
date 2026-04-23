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

#ifndef CEMOCO_PERIODIC_DISPATCHER_H
#define CEMOCO_PERIODIC_DISPATCHER_H

#include "FreeRTOS.h"

typedef void(*perdis_handler_t)(void *userdata);

struct perdis_job_def
{
    const char *name;
    TickType_t period_ticks;
    perdis_handler_t handler;
};

struct perdis_job
{
    const struct perdis_job_def *def;
    TickType_t last_called_tick;
    void *userdata;
};

void perdis_job_init(struct perdis_job *job, const struct perdis_job_def *def, void *userdata);

void perdis_job_run(struct perdis_job *job);
void perdis_run_all(struct perdis_job *jobs, uint32_t size);

#endif //CEMOCO_PERIODIC_DISPATCHER_H
