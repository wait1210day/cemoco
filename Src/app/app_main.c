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

#define CEMOCO_APP_MAIN_IMPL

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "stm32g4xx_hal.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_hrtim.h"
#include "stm32g4xx_ll_dac.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_main.h"
#include "main.h"
#include "ll_helper.h"
#include "fumi.h"
#include "hermes.h"
#include "hermes_messages.h"
#include "leds.h"
#include "pm_daemon_task.h"
#include "hostif.h"
#include "fan_task.h"


static const char *kTAG = "app_main";

static const struct cali_params g_cali_params = {
    .adc_Vin_coeffs = {0.020168f, 0.015891f},
    .adc_Iin_coeffs = {0.017196f, -34.661624f},
    // .adc_Vout_coeffs = {0.020161f, 0.029110f},
    .adc_Vout_coeffs = {0.020201f, 0.029168f},
    // .adc_Iout_coeffs = {0.020111f, -40.738247f},
    .adc_Iout_coeffs = {0.019813f, -39.920985f},
    .adc_iL_coeffs = {0.0201416f, -40.746456f},
    .adc_temp_coeffs = {0.080586f, -50.0f},
    .vout_drop_compensation_k = 0.012000f
};

static const struct fan_curve_table_entry g_fan_curve_entries[] = {
    { 39, 0.00f },
    { 40, 0.25f },
    { 50, 0.30f },
    { 65, 0.45f },
    { 75, 1.00f }
};

static const struct fan_curve_table g_fan_curve = {
    .nb_entries = arraysize_of(g_fan_curve_entries),
    .sorted = g_fan_curve_entries
};

static struct app_context g_appctx;
static struct leds_context g_leds;
struct pmd_context g_pmd;
static struct hostif_context g_hostif;
static struct fan_task_ctx g_fan;

void HAL_HRTIM_Fault1Callback(HRTIM_HandleTypeDef *hhrtim)
{
    pmd_rtisr_on_hrtim_fault(&g_pmd);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t rxfifo0_it)
{
    hostif_on_isr_fdcan_rxfifo0(&g_hostif, rxfifo0_it);
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t error_status_its)
{
    hostif_on_isr_fdcan_error(&g_hostif, error_status_its);
}

// NOLINTNEXTLINE
int _write(int file, char *ptr, int len)
{
    HAL_StatusTypeDef status;
    if (file == 1 || file == 2)
    {
        status = HAL_UART_Transmit(g_appctx.huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
        if (status == HAL_OK)
            return len;

        errno = EIO;
        return -1;
    }
    errno = EBADF;
    return -1;
}

static void debug_task(void *param)
{
    struct hermes_message recvmsg;
    QueueHandle_t recvqueue = HERMES_CREATE_QUEUE(4);
    hermes_subscribe(HERMES_TOPIC_CONVERTER_STAT, recvqueue);
    hermes_subscribe(HERMES_TOPIC_ELEC_MEASURES, recvqueue);

    struct hermes_msg_elec_measures msg_elec_measures;
    struct hermes_msg_converter_stat msg_converter_stat;

    float setpoint = 12.0f;
    int loop_count = 0;


    int en_state = 0;
    int btn_en_blanking = 0;

    while (1)
    {
        /*
        if (HAL_GPIO_ReadPin(KEY_A_GPIO_Port, KEY_A_Pin) == GPIO_PIN_RESET)
        {
            if (!btn_en_blanking)
            {
                en_state ^= 1;
                btn_en_blanking = 1;
            }
        }
        else
        {
            btn_en_blanking = 0;
        }

        {
            struct hermes_msg_output_enable msg = {
                .enable = en_state
            };
            HERMES_PUBLISH(HERMES_TOPIC_OUTPUT_ENABLE, &msg);
        }


        if (HAL_GPIO_ReadPin(KEY_B_GPIO_Port, KEY_B_Pin) == GPIO_PIN_RESET)
        {
            setpoint += 0.05f;
        }

        if (HAL_GPIO_ReadPin(KEY_C_GPIO_Port, KEY_C_Pin) == GPIO_PIN_RESET)
        {
            setpoint -= 0.05f;
        }

        if (HAL_GPIO_ReadPin(KEY_D_GPIO_Port, KEY_D_Pin) == GPIO_PIN_RESET)
        {
            struct hermes_msg_softprot_clear msg = {
                .flags_to_clear = 0xfffffffful
            };
            HERMES_PUBLISH(HERMES_TOPIC_SOFTPROT_CLEAR, &msg);
        }

        if (setpoint > 23.0f)
            setpoint = 23.0f;
        else if (setpoint < 2.0f)
            setpoint = 2.0f;

        struct hermes_msg_output_set msg = {
            .Vout = setpoint,
            .Iout = 10.0f
        };
        HERMES_PUBLISH(HERMES_TOPIC_OUTPUT_SET, &msg);
        */

        while (HERMES_QUEUE_RECEIVE(recvqueue, &recvmsg, 0) == pdPASS)
        {
            if (recvmsg.topic == HERMES_TOPIC_CONVERTER_STAT)
                memcpy(&msg_converter_stat, recvmsg.payload, recvmsg.payload_size);
            else if (recvmsg.topic == HERMES_TOPIC_ELEC_MEASURES)
                memcpy(&msg_elec_measures, recvmsg.payload, recvmsg.payload_size);
        }

        // TODO(masshiroio): experimental code, just for testing
        loop_count++;
        if (loop_count >= 3)
        {
            size_t free_heap = xPortGetFreeHeapSize();
            size_t free_heap_min = xPortGetMinimumEverFreeHeapSize();

            const char *active_loop;
            switch (msg_converter_stat.active_loop)
            {
                case CTRLOOP_ACTIVE_LOOP_CC: active_loop = "CC"; break;
                case CTRLOOP_ACTIVE_LOOP_CV: active_loop = "CV"; break;
                case CTRLOOP_ACTIVE_LOOP_BURST: active_loop = "BURST"; break;
                default: active_loop = "NONE"; break;
            }

            printf("%.02fV %.02fA [%u] %.02fW / %.02fV %.02fA %.02fW [%.01f%%] iL=%.02f softprot=%lu "
                "fault=%lu / T=%ludegC S=%.02fV act=%s BR=%.02f / free_heap=%u (%u ever min)\n\r",
                msg_elec_measures.Vin, msg_elec_measures.Iin, msg_elec_measures.Iin_raw, msg_elec_measures.Pin,
                msg_elec_measures.Vout, msg_elec_measures.Iout, msg_elec_measures.Pout,
                msg_elec_measures.efficiency * 100,
                msg_elec_measures.iL,
                msg_converter_stat.soft_prot_flags,
                msg_converter_stat.fault_flags,
                msg_elec_measures.temp,
                setpoint,
                active_loop,
                msg_converter_stat.burst_ratio,
                free_heap,
                free_heap_min
            );

            // HAL_UART_Transmit(g_appctx.huart2, (const uint8_t*) buf, len, HAL_MAX_DELAY);

            loop_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void init_and_spawn_task(void *param)
{
    // System basic components
    fumi_init(g_appctx.huart2);
    hermes_init();

    FUMI_LOGI(kTAG, "FreeRTOS scheduler started");
    FUMI_LOGI(kTAG, "initializing and spawning");

    // Peripherals initialization and spawn other tasks
    struct leds_config leds_config = {
        .led_configs = {
            {
                .name = "blue",
                .tim = g_appctx.htim8,
                .tim_channel = TIM_CHANNEL_2,
                .tim_channel_compl = 1,
                .tim_channel_ARR = 99,
                .gpio_port = NULL,
                .gpio_pin = 0,
                .active_low = 1
            },
            {
                .name = "green",
                .tim = g_appctx.htim8,
                .tim_channel = TIM_CHANNEL_1,
                .tim_channel_compl = 1,
                .tim_channel_ARR = 99,
                .gpio_port = NULL,
                .gpio_pin = 0,
                .active_low = 1
            },
            {
                .name = "red",
                .tim = NULL,
                .tim_channel = 0,
                .tim_channel_compl = 0,
                .tim_channel_ARR = 0,
                .gpio_port = LED_RED_GPIO_Port,
                .gpio_pin = LED_RED_Pin,
                .active_low = 1
            }
        },
        .task_priority = 8
    };
    leds_init(&g_leds, &leds_config);

    ll_helper_adc_start_calibration(ADC1, LL_ADC_SINGLE_ENDED);
    ll_helper_adc_start_calibration(ADC2, LL_ADC_SINGLE_ENDED);

    HAL_OPAMP_Start(g_appctx.hopamp3);

    struct pmd_config pmd_config = {
        .task_priority = CEMOCO_TASK_PRIORITY_PMD,
        .ctrloop_config = {
            .hhrtim = g_appctx.hhrtim1,
            .ll_adc1 = ADC1,
            .ll_adc2 = ADC2,
            .ll_adc2_dma = DMA1,
            .adc2_dma_channel = LL_DMA_CHANNEL_2,

            .hcomp_pcmc = g_appctx.hcomp2,
            .hdac_pcmc = g_appctx.hdac3,
            .dac_pcmc_channel = DAC_CHANNEL_2,

            .cali_params = &g_cali_params
        },
        .sensors_config = {
            .cali_params = &g_cali_params
        }
    };
    pmd_start_task(&g_pmd, &pmd_config);


    struct hostif_config hostif_config = {
        .hfdcan = g_appctx.hfdcan1,
        .task_priority = CEMOCO_TASK_PRIORITY_HOSTIF
    };
    hostif_init(&g_hostif, &hostif_config);

    struct fan_task_config fan_config = {
        .lut = &g_fan_curve,
        .tim = g_appctx.htim17,
        .tim_channel = TIM_CHANNEL_1,
        .tim_period = 6800,
        .task_priority = CEMOCO_TASK_PRIORITY_FAN
    };
    fan_task_init(&g_fan, &fan_config);

    // xTaskCreate(debug_task, "debug_task", 1024, NULL, 12, NULL);

    vTaskDelete(NULL);
}


CEMOCO_NORETURN void app_early_fault_error_handler()
{
    // TODO(masshiroio): Debug print
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
    while (1) {}
}

CEMOCO_NORETURN void app_main(struct app_context *appctx_origin)
{
    // Copy `appctx` into global memory immediately, since FreeRTOS will
    // reset stack pointer and overwrite the whole stack space in
    // `vTaskStartScheduler()`.
    memcpy(&g_appctx, appctx_origin, sizeof(struct app_context));

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    BaseType_t result = xTaskCreate(
        init_and_spawn_task, "init_spwan_task", 1024, NULL, CEMOCO_TASK_PRIORITY_SPAWN, NULL);
    if (result != pdPASS)
        app_early_fault_error_handler();

    vTaskStartScheduler();

    // `vTaskStartScheduler()` should never return.
    // Unreachable code.
    app_early_fault_error_handler();
}
