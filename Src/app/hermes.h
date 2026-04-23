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

#ifndef CEMOCO_HERMES_H
#define CEMOCO_HERMES_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "defs.h"

#define HERMES_MAX_PAYLOAD_BYTES    64
#define HERMES_MAX_TOPICS           16
#define HERMES_MAX_SUBSCRIPTIONS    16

typedef uint16_t hermes_topic_t;

struct hermes_message
{
    hermes_topic_t topic;
    TaskHandle_t from_task;
    uint32_t timestamp;

    // 32bits aligned buffer
    uint32_t payload[(HERMES_MAX_PAYLOAD_BYTES + 3) / 4];
    size_t payload_size;
};

void hermes_init();
err_t hermes_subscribe(hermes_topic_t topic, QueueHandle_t queue);

// Only for `size(queue) == 1`
err_t hermes_subscribe_overwrite(hermes_topic_t topic, QueueHandle_t queue);

err_t hermes_publish(hermes_topic_t topic, void *payload, size_t payload_size);

// Helper macros to simplify Hermes usage

/**
 * Creates a FreeRTOS queue suitable for Hermes messages.
 *
 * @param length Depth of the queue.
 */
#define HERMES_CREATE_QUEUE(length) \
    xQueueCreate(length, sizeof(struct hermes_message))

/**
 * Publishes a typed message to a topic.
 *
 * @param topic     The target topic.
 * @param ptr       Pointer to the message structure.
 */
#define HERMES_PUBLISH(topic, ptr) \
    hermes_publish(topic, ptr, sizeof(*(ptr)))

/**
 * Receives a message from the queue with a timeout.
 *
 * @param queue The queue to receive from.
 * @param msg_ptr       Pointer to the hermes_message struct to fill.
 * @param timeout_ms    Timeout in milliseconds.
 */
#define HERMES_QUEUE_RECEIVE(queue, msg_ptr, timeout_ticks) \
    xQueueReceive(queue, msg_ptr, timeout_ticks)

/**
 * Accesses the payload of a message as a specific type.
 *
 * @param msg_ptr   Pointer to the hermes_message struct.
 * @param type      The type to cast the payload to.
 */
#define HERMES_PAYLOAD_CAST(msg_ptr, type) ((type *)((msg_ptr)->payload))

#endif //CEMOCO_HERMES_H
