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

#ifndef CEMOCO_FUMI_H
#define CEMOCO_FUMI_H

#include <stdio.h>
#include <stdarg.h>

#include "defs.h"
#include "stm32g4xx_hal.h"

enum fumi_level
{
    FUMI_LEVEL_DEBUG = 0,
    FUMI_LEVEL_INFO,
    FUMI_LEVEL_WARN,
    FUMI_LEVEL_ERROR
};

#define FUMI_DEFAULT_LEVEL  FUMI_LEVEL_DEBUG

#define FUMI_MSG_MAX_LEN    256
#define FUMI_NB_SLOTS       16
#define FUMI_TAG_MAXLEN     24

// FreeRTOS stack depth in StackType_t units, not bytes.
#define FUMI_TASK_STACK_DEPTH     256

#define FUMI_NO_SLOT_TIMEOUT      pdMS_TO_TICKS(0)

struct fumi_stats
{
    uint32_t sent;
    uint32_t uart_tx_error;
    uint32_t drop_no_free_slot;
    uint32_t drop_format_failure;
    uint32_t drop_in_isr;
};

err_t fumi_init(UART_HandleTypeDef *huart);

void fumi_set_level(enum fumi_level level);

// Note: approx. stats, since the data may be updated when copying
// the structure.
void fumi_get_stats_approx(struct fumi_stats *out);

void fumi_printf(enum fumi_level level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

void fumi_vprintf(enum fumi_level level, const char *tag, const char *fmt, va_list ap);


#if FUMI_LEVEL_DEBUG >= FUMI_DEFAULT_LEVEL
#define FUMI_LOGD(tag, fmt, ...) fumi_printf(FUMI_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define FUMI_LOGD(tag, fmt, ...) ((void)0)
#endif // FUMI_LEVEL_DEBUG

#if FUMI_LEVEL_INFO >= FUMI_DEFAULT_LEVEL
#define FUMI_LOGI(tag, fmt, ...) fumi_printf(FUMI_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#else
#define FUMI_LOGI(tag, fmt, ...) ((void)0)
#endif // FUMI_LEVEL_INFO

#if FUMI_LEVEL_WARN >= FUMI_DEFAULT_LEVEL
#define FUMI_LOGW(tag, fmt, ...) fumi_printf(FUMI_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#else
#define FUMI_LOGW(tag, fmt, ...) ((void)0)
#endif // FUMI_LEVEL_WARN

#if FUMI_LEVEL_ERROR >= FUMI_DEFAULT_LEVEL
#define FUMI_LOGE(tag, fmt, ...) fumi_printf(FUMI_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#define FUMI_LOGE(tag, fmt, ...) ((void)0)
#endif // FUMI_LEVEL_ERROR

#endif //CEMOCO_FUMI_H
