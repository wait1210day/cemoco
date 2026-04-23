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

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hermes.h"

struct hermes_sub_node
{
    QueueHandle_t queue;
    uint8_t queue_overwrite;
    struct hermes_sub_node *next;
    uint8_t used_in_pool;
};

struct hermes_topic_entry
{
    hermes_topic_t topic;
    struct hermes_sub_node *sub_head;
};


static struct hermes_sub_node g_sub_node_pool[HERMES_MAX_SUBSCRIPTIONS];
static struct hermes_topic_entry g_topic_entries[HERMES_MAX_TOPICS];

static struct hermes_sub_node *alloc_free_sub_node()
{
    for (int i = 0; i < arraysize_of(g_sub_node_pool); i++)
    {
        struct hermes_sub_node *node = &g_sub_node_pool[i];
        if (!node->used_in_pool)
        {
            node->used_in_pool = 1;
            return node;
        }
    }
    return NULL;
}

void hermes_init()
{
    memset(&g_sub_node_pool, 0, sizeof(g_sub_node_pool));
    for (int i = 0; i < arraysize_of(g_topic_entries); i++)
    {
        struct hermes_topic_entry *entry = &g_topic_entries[i];
        entry->topic = i;
        entry->sub_head = NULL;
    }
}

static err_t hermes_subscribe_internal(
    hermes_topic_t topic, QueueHandle_t queue, uint8_t queue_overwrite)
{
    if (topic >= HERMES_MAX_TOPICS || queue == NULL)
        return ERR_STATUS_INVALID_ARG;

    taskENTER_CRITICAL();
    struct hermes_topic_entry *topic_entry = &g_topic_entries[topic];

    // Check for duplicated subscriptions
    struct hermes_sub_node *curr = topic_entry->sub_head;
    while (curr != NULL)
    {
        if (curr->queue == queue)
        {
            taskEXIT_CRITICAL();
            return ERR_STATUS_SUCCESS;
        }
        curr = curr->next;
    }

    // Allocate node and linking
    struct hermes_sub_node *sub_node = alloc_free_sub_node();
    if (sub_node == NULL)
    {
        taskEXIT_CRITICAL();
        return ERR_STATUS_NO_MEM;
    }

    // Inserting at the head of list is the fastest way and the order
    // doesn't matter here.
    sub_node->queue = queue;
    sub_node->queue_overwrite = queue_overwrite;
    sub_node->next = topic_entry->sub_head;
    topic_entry->sub_head = sub_node;

    taskEXIT_CRITICAL();
    return ERR_STATUS_SUCCESS;
}

err_t hermes_subscribe_overwrite(hermes_topic_t topic, QueueHandle_t queue)
{
    if (uxQueueSpacesAvailable(queue) != 1)
        return ERR_STATUS_INVALID_ARG;
    return hermes_subscribe_internal(topic, queue, 1);
}

err_t hermes_subscribe(hermes_topic_t topic, QueueHandle_t queue)
{
    return hermes_subscribe_internal(topic, queue, 0);
}

err_t hermes_publish(hermes_topic_t topic, void *payload, size_t payload_size)
{
    if (topic >= HERMES_MAX_TOPICS || payload == NULL
        || payload_size == 0 || payload_size > HERMES_MAX_PAYLOAD_BYTES)
        return ERR_STATUS_INVALID_ARG;

    // Critical section is not needed here, since the list just monotonically
    // increases for most cases. Even though there may be race conditions,
    // it is safe since the operation is readonly.
    struct hermes_sub_node *curr = g_topic_entries[topic].sub_head;
    if (curr == NULL)
        return ERR_STATUS_SUCCESS;

    struct hermes_message message;
    message.topic = topic;
    message.from_task = xTaskGetCurrentTaskHandle();
    message.timestamp = xTaskGetTickCount();
    message.payload_size = payload_size;
    memcpy(message.payload, payload, payload_size);

    while (curr != NULL)
    {
        if (!curr->queue_overwrite)
        {
            // We should never block the publisher, so 0 timeout is used.
            xQueueSend(curr->queue, &message, 0);
        }
        else
        {
            xQueueOverwrite(curr->queue, &message);
        }
        curr = curr->next;
    }

    return ERR_STATUS_SUCCESS;
}
