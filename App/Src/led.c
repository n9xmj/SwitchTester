/******************************************************************************
 * led.c
 *
 * Low level LED/indicator control using timer PWM
 ******************************************************************************/

#include "device_config.h"
#include "led.h"

/******************************************************************************
 *
 ******************************************************************************/

void v_led_init(void)
{
    HAL_TIM_PWM_Init(&LED_TIMER_HANDLE);
    HAL_TIM_PWM_Start(&LED_TIMER_HANDLE, BLUE_LED_CHANNEL);
    v_led_blue_off();
    HAL_TIM_PWM_Start(&LED_TIMER_HANDLE, RED_LED_CHANNEL);
    v_led_red_off();
    HAL_TIM_PWM_Start(&LED_TIMER_HANDLE, DEBUG_LED_CHANNEL);
    v_led_debug_off();
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_blue_on(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, BLUE_LED_CHANNEL, LED_PWM_MAX_DUTY);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_blue_off(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, BLUE_LED_CHANNEL, 0);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_blue_toggle(void)
{
    if (__HAL_TIM_GET_COMPARE(&LED_TIMER_HANDLE, BLUE_LED_CHANNEL) == 0)
    {
        v_led_blue_on();
    }
    else
    {
        v_led_blue_off();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_blue_pwm(uint16_t u16_duty)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, BLUE_LED_CHANNEL, u16_duty);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_red_on(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, RED_LED_CHANNEL, LED_PWM_MAX_DUTY);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_red_off(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, RED_LED_CHANNEL, 0);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_red_toggle(void)
{
    if (__HAL_TIM_GET_COMPARE(&LED_TIMER_HANDLE, RED_LED_CHANNEL) == 0)
    {
        v_led_red_on();
    }
    else
    {
        v_led_red_off();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_red_pwm(uint16_t u16_duty)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, RED_LED_CHANNEL, u16_duty);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_debug_on(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, DEBUG_LED_CHANNEL, LED_PWM_MAX_DUTY);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_debug_off(void)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, DEBUG_LED_CHANNEL, 0);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_debug_toggle(void)
{
    if (__HAL_TIM_GET_COMPARE(&LED_TIMER_HANDLE, DEBUG_LED_CHANNEL) == 0)
    {
        v_led_red_on();
    }
    else
    {
        v_led_red_off();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_led_debug_pwm(uint16_t u16_duty)
{
    __HAL_TIM_SET_COMPARE(&LED_TIMER_HANDLE, DEBUG_LED_CHANNEL, u16_duty);
}
