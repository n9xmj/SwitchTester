/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    comp.c
  * @brief   This file provides code for the configuration
  *          of the COMP instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "comp.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

COMP_HandleTypeDef hcomp1;
COMP_HandleTypeDef hcomp2;
COMP_HandleTypeDef hcomp3;

/* COMP1 init function */
void MX_COMP1_Init(void)
{

  /* USER CODE BEGIN COMP1_Init 0 */

  /* USER CODE END COMP1_Init 0 */

  /* USER CODE BEGIN COMP1_Init 1 */

  /* USER CODE END COMP1_Init 1 */
  hcomp1.Instance = COMP1;
  hcomp1.Init.InputPlus = COMP_INPUT_PLUS_IO3;
  hcomp1.Init.InputMinus = COMP_INPUT_MINUS_DAC1_CH1;
  hcomp1.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp1.Init.WindowOutput = COMP_WINDOWOUTPUT_EACH_COMP;
  hcomp1.Init.Hysteresis = COMP_HYSTERESIS_LOW;
  hcomp1.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp1.Init.Mode = COMP_POWERMODE_HIGHSPEED;
  hcomp1.Init.WindowMode = COMP_WINDOWMODE_DISABLE;
  hcomp1.Init.TriggerMode = COMP_TRIGGERMODE_IT_RISING_FALLING;
  if (HAL_COMP_Init(&hcomp1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP1_Init 2 */

  /* USER CODE END COMP1_Init 2 */

}
/* COMP2 init function */
void MX_COMP2_Init(void)
{

  /* USER CODE BEGIN COMP2_Init 0 */

  /* USER CODE END COMP2_Init 0 */

  /* USER CODE BEGIN COMP2_Init 1 */

  /* USER CODE END COMP2_Init 1 */
  hcomp2.Instance = COMP2;
  hcomp2.Init.InputPlus = COMP_INPUT_PLUS_IO3;
  hcomp2.Init.InputMinus = COMP_INPUT_MINUS_DAC1_CH2;
  hcomp2.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp2.Init.WindowOutput = COMP_WINDOWOUTPUT_EACH_COMP;
  hcomp2.Init.Hysteresis = COMP_HYSTERESIS_LOW;
  hcomp2.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp2.Init.Mode = COMP_POWERMODE_HIGHSPEED;
  hcomp2.Init.WindowMode = COMP_WINDOWMODE_DISABLE;
  hcomp2.Init.TriggerMode = COMP_TRIGGERMODE_IT_RISING_FALLING;
  if (HAL_COMP_Init(&hcomp2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP2_Init 2 */

  /* USER CODE END COMP2_Init 2 */

}
/* COMP3 init function */
void MX_COMP3_Init(void)
{

  /* USER CODE BEGIN COMP3_Init 0 */

  /* USER CODE END COMP3_Init 0 */

  /* USER CODE BEGIN COMP3_Init 1 */

  /* USER CODE END COMP3_Init 1 */
  hcomp3.Instance = COMP3;
  hcomp3.Init.InputPlus = COMP_INPUT_PLUS_IO1;
  hcomp3.Init.InputMinus = COMP_INPUT_MINUS_DAC1_CH2;
  hcomp3.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp3.Init.WindowOutput = COMP_WINDOWOUTPUT_EACH_COMP;
  hcomp3.Init.Hysteresis = COMP_HYSTERESIS_LOW;
  hcomp3.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp3.Init.Mode = COMP_POWERMODE_HIGHSPEED;
  hcomp3.Init.WindowMode = COMP_WINDOWMODE_DISABLE;
  hcomp3.Init.TriggerMode = COMP_TRIGGERMODE_IT_RISING_FALLING;
  if (HAL_COMP_Init(&hcomp3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP3_Init 2 */

  /* USER CODE END COMP3_Init 2 */

}

void HAL_COMP_MspInit(COMP_HandleTypeDef* compHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(compHandle->Instance==COMP1)
  {
  /* USER CODE BEGIN COMP1_MspInit 0 */

  /* USER CODE END COMP1_MspInit 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**COMP1 GPIO Configuration
    PA1     ------> COMP1_INP
    PA11 [PA9]     ------> COMP1_OUT
    */
    GPIO_InitStruct.Pin = SENSE_A_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SENSE_A_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_COMP1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* COMP1 interrupt Init */
    HAL_NVIC_SetPriority(ADC1_COMP_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ADC1_COMP_IRQn);
  /* USER CODE BEGIN COMP1_MspInit 1 */

  /* USER CODE END COMP1_MspInit 1 */
  }
  else if(compHandle->Instance==COMP2)
  {
  /* USER CODE BEGIN COMP2_MspInit 0 */

  /* USER CODE END COMP2_MspInit 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**COMP2 GPIO Configuration
    PA3     ------> COMP2_INP
    PA12 [PA10]     ------> COMP2_OUT
    */
    GPIO_InitStruct.Pin = SENSE_B_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SENSE_B_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_COMP2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* COMP2 interrupt Init */
    HAL_NVIC_SetPriority(ADC1_COMP_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ADC1_COMP_IRQn);
  /* USER CODE BEGIN COMP2_MspInit 1 */

  /* USER CODE END COMP2_MspInit 1 */
  }
  else if(compHandle->Instance==COMP3)
  {
  /* USER CODE BEGIN COMP3_MspInit 0 */

  /* USER CODE END COMP3_MspInit 0 */

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**COMP3 GPIO Configuration
    PC2     ------> COMP3_OUT
    PB0     ------> COMP3_INP
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_COMP3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = SENSE_C_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SENSE_C_GPIO_Port, &GPIO_InitStruct);

    /* COMP3 interrupt Init */
    HAL_NVIC_SetPriority(ADC1_COMP_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ADC1_COMP_IRQn);
  /* USER CODE BEGIN COMP3_MspInit 1 */

  /* USER CODE END COMP3_MspInit 1 */
  }
}

void HAL_COMP_MspDeInit(COMP_HandleTypeDef* compHandle)
{

  if(compHandle->Instance==COMP1)
  {
  /* USER CODE BEGIN COMP1_MspDeInit 0 */

  /* USER CODE END COMP1_MspDeInit 0 */

    /**COMP1 GPIO Configuration
    PA1     ------> COMP1_INP
    PA11 [PA9]     ------> COMP1_OUT
    */
    HAL_GPIO_DeInit(GPIOA, SENSE_A_Pin|GPIO_PIN_11);

    /* COMP1 interrupt Deinit */
  /* USER CODE BEGIN COMP1:ADC1_COMP_IRQn disable */
    /**
    * Uncomment the line below to disable the "ADC1_COMP_IRQn" interrupt
    * Be aware, disabling shared interrupt may affect other IPs
    */
    /* HAL_NVIC_DisableIRQ(ADC1_COMP_IRQn); */
  /* USER CODE END COMP1:ADC1_COMP_IRQn disable */

  /* USER CODE BEGIN COMP1_MspDeInit 1 */

  /* USER CODE END COMP1_MspDeInit 1 */
  }
  else if(compHandle->Instance==COMP2)
  {
  /* USER CODE BEGIN COMP2_MspDeInit 0 */

  /* USER CODE END COMP2_MspDeInit 0 */

    /**COMP2 GPIO Configuration
    PA3     ------> COMP2_INP
    PA12 [PA10]     ------> COMP2_OUT
    */
    HAL_GPIO_DeInit(GPIOA, SENSE_B_Pin|GPIO_PIN_12);

    /* COMP2 interrupt Deinit */
  /* USER CODE BEGIN COMP2:ADC1_COMP_IRQn disable */
    /**
    * Uncomment the line below to disable the "ADC1_COMP_IRQn" interrupt
    * Be aware, disabling shared interrupt may affect other IPs
    */
    /* HAL_NVIC_DisableIRQ(ADC1_COMP_IRQn); */
  /* USER CODE END COMP2:ADC1_COMP_IRQn disable */

  /* USER CODE BEGIN COMP2_MspDeInit 1 */

  /* USER CODE END COMP2_MspDeInit 1 */
  }
  else if(compHandle->Instance==COMP3)
  {
  /* USER CODE BEGIN COMP3_MspDeInit 0 */

  /* USER CODE END COMP3_MspDeInit 0 */

    /**COMP3 GPIO Configuration
    PC2     ------> COMP3_OUT
    PB0     ------> COMP3_INP
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_2);

    HAL_GPIO_DeInit(SENSE_C_GPIO_Port, SENSE_C_Pin);

    /* COMP3 interrupt Deinit */
  /* USER CODE BEGIN COMP3:ADC1_COMP_IRQn disable */
    /**
    * Uncomment the line below to disable the "ADC1_COMP_IRQn" interrupt
    * Be aware, disabling shared interrupt may affect other IPs
    */
    /* HAL_NVIC_DisableIRQ(ADC1_COMP_IRQn); */
  /* USER CODE END COMP3:ADC1_COMP_IRQn disable */

  /* USER CODE BEGIN COMP3_MspDeInit 1 */

  /* USER CODE END COMP3_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

