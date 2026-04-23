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

#include <string.h>
#include <assert.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "fumi.h"

struct fumi_msg
{
    size_t len;
    char buf[FUMI_MSG_MAX_LEN];
};

struct fumi_context
{
    volatile uint8_t ready;
    volatile enum fumi_level level;
    UART_HandleTypeDef *huart;

    QueueHandle_t q_ready_slot;
    QueueHandle_t q_free_slot;

    TaskHandle_t task;

    struct fumi_stats stats;
    struct fumi_msg slots[FUMI_NB_SLOTS];
};

static struct fumi_context g_fumi;

CEMOCO_NORETURN static void fumi_task(CEMOCO_MAYBE_UNUSED void *arg)
{
    while (1)
    {
        uint8_t slot_idx;
        if (xQueueReceive(g_fumi.q_ready_slot, &slot_idx, portMAX_DELAY) != pdTRUE)
            continue;
        assert(slot_idx < FUMI_NB_SLOTS);

        struct fumi_msg *slot = &g_fumi.slots[slot_idx];
        if (HAL_UART_Transmit(g_fumi.huart, (const uint8_t*) slot->buf, slot->len, 100) != HAL_OK)
            g_fumi.stats.uart_tx_error++;
        else
            g_fumi.stats.sent++;

        slot->len = 0;
        xQueueSend(g_fumi.q_free_slot, &slot_idx, 0);
    }
}

err_t fumi_init(UART_HandleTypeDef *huart)
{
    if (g_fumi.ready || !huart)
        return ERR_STATUS_INVALID_ARG;

    memset(&g_fumi, 0, sizeof(g_fumi));

    err_t err = ERR_STATUS_SUCCESS;
    UBaseType_t result;

    g_fumi.level = FUMI_DEFAULT_LEVEL;
    g_fumi.huart = huart;

    g_fumi.q_ready_slot = xQueueCreate(FUMI_NB_SLOTS, sizeof(uint8_t));
    if (!g_fumi.q_ready_slot)
    {
        err = ERR_STATUS_NO_MEM;
        goto err_out;
    }

    g_fumi.q_free_slot = xQueueCreate(FUMI_NB_SLOTS, sizeof(uint8_t));
    if (!g_fumi.q_free_slot)
    {
        err = ERR_STATUS_NO_MEM;
        goto err_out;
    }

    // Initially, all the slots are available (free). Add them to the
    // FREE queue so that a log producer can get one free slot.
    for (uint8_t i = 0; i < FUMI_NB_SLOTS; i++)
        xQueueSendToBack(g_fumi.q_free_slot, &i, 0);

    result = xTaskCreate(fumi_task, "fumi", FUMI_TASK_STACK_DEPTH, NULL,
                         CEMOCO_TASK_PRIORITY_FUMI, &g_fumi.task);
    if (result != pdTRUE)
    {
        err = ERR_STATUS_NO_MEM;
        goto err_out;
    }

    g_fumi.ready = 1;

    return ERR_STATUS_SUCCESS;

err_out:
    if (g_fumi.q_ready_slot)
        vQueueDelete(g_fumi.q_ready_slot);
    if (g_fumi.q_free_slot)
        vQueueDelete(g_fumi.q_free_slot);
    return err;
}

void fumi_set_level(enum fumi_level level)
{
    g_fumi.level = level;
}

void fumi_printf(enum fumi_level level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fumi_vprintf(level, tag, fmt, ap);
    va_end(ap);
}


static char fumi_level_char(enum fumi_level level)
{
    switch (level)
    {
        case FUMI_LEVEL_DEBUG:
            return 'D';
        case FUMI_LEVEL_INFO:
            return 'I';
        case FUMI_LEVEL_WARN:
            return 'W';
        case FUMI_LEVEL_ERROR:
            return 'E';
        default:
            return '?';
    }
}


static int fumi_format_prefix(char *buf, size_t size, enum fumi_level level, const char *tag)
{
    uint32_t ts = xTaskGetTickCount();

    return snprintf(
        buf, size, "%c (%lu) %.*s: ",
        fumi_level_char(level),
        (unsigned long) ts,
        FUMI_TAG_MAXLEN,
        tag ? tag : "???"
    );
}

static size_t fumi_format_msg(struct fumi_msg *msg, enum fumi_level level,
                              const char *tag, const char *fmt, va_list ap)
{
    int n = fumi_format_prefix(msg->buf, sizeof(msg->buf), level, tag);
    if (n < 0)
        return 0;

    size_t used = (size_t) n;
    if (used >= sizeof(msg->buf))
        used = sizeof(msg->buf) - 1;

    size_t rem = sizeof(msg->buf) - used;

    if (vsnprintf(msg->buf + used, rem, fmt, ap) < 0)
        return 0;

    used = strnlen(msg->buf, sizeof(msg->buf));

    if (used + 2 < sizeof(msg->buf))
    {
        msg->buf[used++] = '\r';
        msg->buf[used++] = '\n';
        msg->buf[used] = '\0';
    }
    else if (used + 1 < sizeof(msg->buf))
    {
        msg->buf[used++] = '\n';
        msg->buf[used] = '\0';
    }
    else if (used > 0)
    {
        msg->buf[sizeof(msg->buf) - 2] = '\r';
        msg->buf[sizeof(msg->buf) - 1] = '\n';
        used = sizeof(msg->buf);
    }

    return used;
}

void fumi_vprintf(enum fumi_level level, const char *tag, const char *fmt, va_list ap)
{
    if (!g_fumi.ready)
        return;
    if (level < g_fumi.level)
        return;
    if (!fmt)
        return;

    if (__get_IPSR() != 0U)
    {
        g_fumi.stats.drop_in_isr++;
        return;
    }

    // Try to get a free slot
    uint8_t slot_idx;
    if (xQueueReceive(g_fumi.q_free_slot, &slot_idx, FUMI_NO_SLOT_TIMEOUT) != pdTRUE)
    {
        g_fumi.stats.drop_no_free_slot++;
        return;
    }
    assert(slot_idx < FUMI_NB_SLOTS);

    // Format
    struct fumi_msg *msg = &g_fumi.slots[slot_idx];
    va_list apdup;
    va_copy(apdup, ap);
    msg->len = fumi_format_msg(msg, level, tag, fmt, apdup);
    va_end(apdup);

    if (!msg->len)
    {
        msg->len = 0;
        g_fumi.stats.drop_format_failure++;
        xQueueSend(g_fumi.q_free_slot, &slot_idx, 0);
        return;
    }

    assert(xQueueSend(g_fumi.q_ready_slot, &slot_idx, 0) == pdTRUE);
}

void fumi_get_stats_approx(struct fumi_stats *out)
{
    if (!out)
        return;

    memcpy(out, &g_fumi.stats, sizeof(g_fumi.stats));
}
