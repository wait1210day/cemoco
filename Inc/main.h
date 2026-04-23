/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

#include "stm32g4xx_ll_adc.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_cortex.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_system.h"
#include "stm32g4xx_ll_utils.h"
#include "stm32g4xx_ll_pwr.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_dma.h"

#include "stm32g4xx_ll_exti.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_HRTIM_MspPostInit(HRTIM_HandleTypeDef *hhrtim);

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define KEY_B_Pin GPIO_PIN_13
#define KEY_B_GPIO_Port GPIOC
#define KEY_C_Pin GPIO_PIN_14
#define KEY_C_GPIO_Port GPIOC
#define KEY_D_Pin GPIO_PIN_15
#define KEY_D_GPIO_Port GPIOC
#define ADC1_IN1_VOUT_Pin GPIO_PIN_0
#define ADC1_IN1_VOUT_GPIO_Port GPIOA
#define ADC1_IN2_IOUT_Pin GPIO_PIN_1
#define ADC1_IN2_IOUT_GPIO_Port GPIOA
#define EXTCONN_DAC1_OUT1_Pin GPIO_PIN_4
#define EXTCONN_DAC1_OUT1_GPIO_Port GPIOA
#define ADC2_IN13_IIN_Pin GPIO_PIN_5
#define ADC2_IN13_IIN_GPIO_Port GPIOA
#define ADC2_IN3_VIN_Pin GPIO_PIN_6
#define ADC2_IN3_VIN_GPIO_Port GPIOA
#define COMP2_INP_IL_Pin GPIO_PIN_7
#define COMP2_INP_IL_GPIO_Port GPIOA
#define ADC2_IN5_TEMP_Pin GPIO_PIN_4
#define ADC2_IN5_TEMP_GPIO_Port GPIOC
#define CTRLOOP_DBG_PULSE_Pin GPIO_PIN_12
#define CTRLOOP_DBG_PULSE_GPIO_Port GPIOB
#define EEPROM_SCL_Pin GPIO_PIN_6
#define EEPROM_SCL_GPIO_Port GPIOC
#define EEPROM_SDA_Pin GPIO_PIN_10
#define EEPROM_SDA_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define LED_RED_Pin GPIO_PIN_15
#define LED_RED_GPIO_Port GPIOA
#define LED_GREEN_PWM_Pin GPIO_PIN_10
#define LED_GREEN_PWM_GPIO_Port GPIOC
#define LED_BLUE_PWM_Pin GPIO_PIN_11
#define LED_BLUE_PWM_GPIO_Port GPIOC
#define SW1_Pin GPIO_PIN_3
#define SW1_GPIO_Port GPIOB
#define SW2_Pin GPIO_PIN_4
#define SW2_GPIO_Port GPIOB
#define FAN_PWM_Pin GPIO_PIN_5
#define FAN_PWM_GPIO_Port GPIOB
#define KEY_A_Pin GPIO_PIN_9
#define KEY_A_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
