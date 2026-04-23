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

#ifndef CEMOCO_LEDS_H
#define CEMOCO_LEDS_H

#include "stm32g4xx_hal.h"

#include "defs.h"


#define LEDS_BRIGHTNESS_MIN  0
#define LEDS_BRIGHTNESS_MAX  128

#define LEDS_NORM_BR_G       60
#define LEDS_NORM_BR_R       128
#define LEDS_NORM_BR_B       128


enum leds_mode
{
    LEDS_MODE_OFF = 0,
    LEDS_MODE_ON,
    LEDS_MODE_BLINK_SLOW,
    LEDS_MODE_BLINK_FAST,
    LEDS_MODE_BREATH
};

enum leds_led
{
    // Portability Note:
    // Define LEDS on the board here.

    LEDS_LED_BLUE = 0,
    LEDS_LED_GREEN,
    LEDS_LED_RED,

    LEDS_LED_NB_ENUM
};

struct leds_led_config
{
    const char *name;

    // Set these TIM-related fields if the LED pin enables TIM alternate function.
    TIM_HandleTypeDef *tim;
    uint32_t tim_channel;
    uint8_t tim_channel_compl;
    uint32_t tim_channel_ARR;

    // Set these GPIO-related fields if the LED only driven by GPIO peripheral.
    GPIO_TypeDef *gpio_port;
    uint32_t gpio_pin;

    // Whether the LED is configured as active LOW.
    // This field only applies when GPIO underlying driver (instead of TIM) is used.
    uint8_t active_low;
};

struct leds_config
{
    const struct leds_led_config led_configs[LEDS_LED_NB_ENUM];
    uint8_t task_priority;
};

struct leds_hw_ops;

struct leds_context_per_led
{
    struct leds_led_config config;
    const struct leds_hw_ops *hwops;

    enum leds_mode mode;
    uint32_t brightness;

    uint32_t breath_brightness;
    uint8_t breath_direction;
};

struct leds_context
{
    struct leds_context_per_led per_led[LEDS_LED_NB_ENUM];
};

/**
 * Initialize LEDS context and start the related peripherals.
 * If TIM peripheral is used for PWM generation, `HAL_TIM_PWM_Start()` or
 * `HAL_TIMEx_PWMN_Start()` will be called.
 *
 * It starts a task for LED state management.
 *
 * @param ctx       Context structure.
 * @param config    LEDS configuration.
 */
err_t leds_init(struct leds_context *ctx, const struct leds_config *config);

/**
 * Set state for a single LED.
 *
 * @param ctx           Context structure
 * @param led_selector  LED selection
 * @param mode          LED mode to set
 * @param brightness    LED brightness to set
 */
err_t leds_set_state(struct leds_context *ctx, enum leds_led led_selector,
                     enum leds_mode mode, uint32_t brightness);

#endif //CEMOCO_LEDS_H
