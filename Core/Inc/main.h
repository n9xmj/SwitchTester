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
#include "stm32g0xx_hal.h"

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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

extern RTC_HandleTypeDef hrtc;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim7;
extern TIM_HandleTypeDef htim14;

extern UART_HandleTypeDef huart2;

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define NUCLEO_BUTTON_Pin GPIO_PIN_13
#define NUCLEO_BUTTON_GPIO_Port GPIOC
#define NUCLEO_BUTTON_EXTI_IRQn EXTI4_15_IRQn
#define RTC_OSC32_IN_Pin GPIO_PIN_14
#define RTC_OSC32_IN_GPIO_Port GPIOC
#define RTC_OSC32_OUT_Pin GPIO_PIN_15
#define RTC_OSC32_OUT_GPIO_Port GPIOC
#define SENSE_D_Pin GPIO_PIN_0
#define SENSE_D_GPIO_Port GPIOA
#define SENSE_A_Pin GPIO_PIN_1
#define SENSE_A_GPIO_Port GPIOA
#define DEBUG_TX_Pin GPIO_PIN_2
#define DEBUG_TX_GPIO_Port GPIOA
#define DEBUG_RX_Pin GPIO_PIN_3
#define DEBUG_RX_GPIO_Port GPIOA
#define NUCLEO_LED_Pin GPIO_PIN_5
#define NUCLEO_LED_GPIO_Port GPIOA
#define SWITCH_A_Pin GPIO_PIN_4
#define SWITCH_A_GPIO_Port GPIOC
#define SWITCH_B_Pin GPIO_PIN_5
#define SWITCH_B_GPIO_Port GPIOC
#define SENSE_C_Pin GPIO_PIN_0
#define SENSE_C_GPIO_Port GPIOB
#define SWITCH_C_Pin GPIO_PIN_10
#define SWITCH_C_GPIO_Port GPIOB
#define SWITCH_D_Pin GPIO_PIN_11
#define SWITCH_D_GPIO_Port GPIOB
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define SENSE_B_Pin GPIO_PIN_4
#define SENSE_B_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
