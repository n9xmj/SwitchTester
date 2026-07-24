/******************************************************************************
 * led.h
 *
 * Low level LED/indicator control using timer PWM
 ******************************************************************************/

#ifndef LED_H
#define LED_H

//------------------------------------------------------------------------------

//#define LED_PWM_MAX_DUTY    256

//------------------------------------------------------------------------------

void v_led_init(void);

void v_led_blue_on(void);
void v_led_blue_off(void);
void v_led_blue_toggle(void);
void v_led_blue_pwm(uint16_t u16_duty);

void v_led_red_on(void);
void v_led_red_off(void);
void v_led_red_toggle(void);
void v_led_red_pwm(uint16_t u16_duty);

void v_led_debug_on(void);
void v_led_debug_off(void);
void v_led_debug_toggle(void);
void v_led_debug_pwm(uint16_t u16_duty);

#endif /* LED_H */
