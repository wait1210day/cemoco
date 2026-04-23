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

#include "leds.h"
#include "hermes.h"
#include "hermes_messages.h"
#include "fumi.h"


static const char *kTAG = "leds";

static void leds_hwops_tim_init(struct leds_context_per_led *led)
{
    if (!led->config.tim_channel_compl)
        HAL_TIM_PWM_Start(led->config.tim, led->config.tim_channel);
    else
        HAL_TIMEx_PWMN_Start(led->config.tim, led->config.tim_channel);

    __HAL_TIM_SetCompare(led->config.tim, led->config.tim_channel, 0);
}

static void leds_hwops_tim_set_brightness(struct leds_context_per_led *led, uint32_t brightness)
{
    uint32_t ARR = led->config.tim_channel_ARR;

    uint32_t cmp = (brightness - LEDS_BRIGHTNESS_MIN) * led->config.tim_channel_ARR
                 / (LEDS_BRIGHTNESS_MAX - LEDS_BRIGHTNESS_MIN);
    if (cmp >= ARR)
        cmp = ARR;

    __HAL_TIM_SetCompare(led->config.tim, led->config.tim_channel, cmp);
}

static void leds_hwops_gpio_init(struct leds_context_per_led *led)
{
    if (led->config.active_low)
        HAL_GPIO_WritePin(led->config.gpio_port, led->config.gpio_pin, GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(led->config.gpio_port, led->config.gpio_pin, GPIO_PIN_RESET);
}

static void leds_hwops_gpio_set_brightness(struct leds_context_per_led *led, uint32_t brightness)
{
    uint8_t led_on = (brightness - LEDS_BRIGHTNESS_MIN)
                    > ((LEDS_BRIGHTNESS_MAX - LEDS_BRIGHTNESS_MIN) / 2);
    if ((led_on && !led->config.active_low) || (!led_on && led->config.active_low))
        HAL_GPIO_WritePin(led->config.gpio_port, led->config.gpio_pin, GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(led->config.gpio_port, led->config.gpio_pin, GPIO_PIN_RESET);
}

struct leds_hw_ops
{
    void (*init)(struct leds_context_per_led *per_led);
    void (*set_brightness)(struct leds_context_per_led *per_led, uint32_t brightness);
};

static const struct leds_hw_ops g_tim_hw_ops = {
    .init = leds_hwops_tim_init,
    .set_brightness = leds_hwops_tim_set_brightness
};

static const struct leds_hw_ops g_gpio_hw_ops = {
    .init = leds_hwops_gpio_init,
    .set_brightness = leds_hwops_gpio_set_brightness
};


static void leds_internal_set_state(struct leds_context *ctx, enum leds_led led_selector,
                                    enum leds_mode mode)
{
    struct leds_context_per_led *led = &ctx->per_led[led_selector];
    led->mode = mode;
    if (led_selector == LEDS_LED_RED)
        led->brightness = LEDS_NORM_BR_R;
    else if (led_selector == LEDS_LED_GREEN)
        led->brightness = LEDS_NORM_BR_G;
    else if (led_selector == LEDS_LED_BLUE)
        led->brightness = LEDS_NORM_BR_B;
}

CEMOCO_NORETURN static void leds_task(void *param)
{
    struct leds_context *ctx = param;
    uint32_t prescaler = 0;

    uint8_t blink_state_slow = 0, blink_state_fast = 0;
    TickType_t last_wake_tick = xTaskGetTickCount();

    QueueHandle_t recvqueue = HERMES_CREATE_QUEUE(2);
    struct hermes_message recvmsg;
    hermes_subscribe(HERMES_TOPIC_CONVERTER_STAT, recvqueue);

    while (1)
    {
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(30));

        for (int i = 0; i < LEDS_LED_NB_ENUM; i++)
        {
            struct leds_context_per_led *per_led = &ctx->per_led[i];
            uint32_t brightness;

            if (per_led->mode == LEDS_MODE_ON)
                brightness = per_led->brightness;
            else if (per_led->mode == LEDS_MODE_BLINK_FAST)
                brightness = blink_state_fast ? per_led->brightness : LEDS_BRIGHTNESS_MIN;
            else if (per_led->mode == LEDS_MODE_BLINK_SLOW)
                brightness = blink_state_slow ? per_led->brightness : LEDS_BRIGHTNESS_MIN;
            else if (per_led->mode == LEDS_MODE_BREATH)
            {
                brightness = per_led->breath_brightness;

                if (per_led->breath_direction)
                    brightness++;
                else
                    brightness--;

                if (brightness >= per_led->brightness)
                {
                    brightness = per_led->brightness;
                    per_led->breath_direction ^= 1;
                }
                else if (brightness <= LEDS_BRIGHTNESS_MIN)
                {
                    brightness = LEDS_BRIGHTNESS_MIN;
                    per_led->breath_direction ^= 1;
                }

                per_led->breath_brightness = brightness;
            }
            else
                brightness = LEDS_BRIGHTNESS_MIN;

            per_led->hwops->set_brightness(per_led, brightness);
        }

        if (prescaler % 20 == 0)
            blink_state_slow ^= 1;
        if (prescaler % 10 == 0)
            blink_state_fast ^= 1;
        prescaler++;

        if (HERMES_QUEUE_RECEIVE(recvqueue, &recvmsg, pdMS_TO_TICKS(30)) != pdTRUE)
            continue;

        // LED allocations:
        //  BLUE: system and communication indicator
        //      FastBlink: initializing and self-testing
        //      Breath: standby, communication OK
        //      SlowBlink: running/standby, communication lost
        //      ConstantOn: running, communication OK
        //
        //  GREEN: output state
        //      ConstantOff: output inactive
        //      Breath: output active, burst mode
        //      ConstantOn: output active, CV mode
        //      SlowBlink: output active, CC mode
        //
        //  RED: system fault
        //      ConstantOff: normal
        //      ConstantOn: hard fault
        //      FastBlink: soft-protection triggered

        if (recvmsg.topic == HERMES_TOPIC_CONVERTER_STAT)
        {
            struct hermes_msg_converter_stat *stat = HERMES_PAYLOAD_CAST(
                &recvmsg, struct hermes_msg_converter_stat);

            // TODO(masshiroio): communication states

            if (stat->active_loop == CTRLOOP_ACTIVE_LOOP_NONE)
            {
                leds_internal_set_state(ctx, LEDS_LED_BLUE, LEDS_MODE_BREATH);
                leds_internal_set_state(ctx, LEDS_LED_GREEN, LEDS_MODE_OFF);
            }
            else if (stat->active_loop == CTRLOOP_ACTIVE_LOOP_BURST)
            {
                leds_internal_set_state(ctx, LEDS_LED_BLUE, LEDS_MODE_ON);
                leds_internal_set_state(ctx, LEDS_LED_GREEN, LEDS_MODE_BREATH);
            }
            else if (stat->active_loop == CTRLOOP_ACTIVE_LOOP_CV)
            {
                leds_internal_set_state(ctx, LEDS_LED_BLUE, LEDS_MODE_ON);
                leds_internal_set_state(ctx, LEDS_LED_GREEN, LEDS_MODE_ON);
            }
            else if (stat->active_loop == CTRLOOP_ACTIVE_LOOP_CC)
            {
                leds_internal_set_state(ctx, LEDS_LED_BLUE, LEDS_MODE_ON);
                leds_internal_set_state(ctx, LEDS_LED_GREEN, LEDS_MODE_BLINK_SLOW);
            }

            if (stat->fault_flags)
                leds_internal_set_state(ctx, LEDS_LED_RED, LEDS_MODE_ON);
            else if (stat->soft_prot_flags)
                leds_internal_set_state(ctx, LEDS_LED_RED, LEDS_MODE_BLINK_FAST);
            else
                leds_internal_set_state(ctx, LEDS_LED_RED, LEDS_MODE_OFF);
        }
    }
}

err_t leds_init(struct leds_context *ctx, const struct leds_config *config)
{
    for (int i = 0; i < LEDS_LED_NB_ENUM; i++)
    {
        const struct leds_led_config *led_config = &config->led_configs[i];
        struct leds_context_per_led *per_led = &ctx->per_led[i];
        memset(per_led, 0, sizeof(*per_led));
        memcpy(&per_led->config, led_config, sizeof(struct leds_led_config));

        if (led_config->tim)
            per_led->hwops = &g_tim_hw_ops;
        else
            per_led->hwops = &g_gpio_hw_ops;

        per_led->hwops->init(per_led);

        FUMI_LOGI(kTAG, "initialized led \"%s\", active %s, driver=%s", led_config->name,
                  led_config->active_low ? "low" : "high",
                  led_config->tim ? "tim" : "gpio");
    }

    BaseType_t task_creation_err = xTaskCreate(
        leds_task, "leds_task", 128, ctx, config->task_priority, NULL);
    if (task_creation_err != pdPASS)
        return ERR_STATUS_FAIL;

    return ERR_STATUS_SUCCESS;
}

err_t leds_set_state(struct leds_context *ctx, enum leds_led led_selector,
                     enum leds_mode mode, uint32_t brightness)
{
    if (led_selector >= LEDS_LED_NB_ENUM)
        return ERR_STATUS_FAIL;

    taskENTER_CRITICAL();
    struct leds_context_per_led *led = &ctx->per_led[led_selector];
    led->mode = mode;
    led->brightness = brightness;
    taskEXIT_CRITICAL();

    return ERR_STATUS_SUCCESS;
}
