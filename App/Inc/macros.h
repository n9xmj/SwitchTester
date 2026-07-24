/******************************************************************************
 * macros.h
 *
 * Hardware interface macros and definitions
 ******************************************************************************/

#ifndef MACROS_H
#define MACROS_H

//#include "exti.h"
#include "stm32g0xx_ll_gpio.h"
#include "stm32g0xx_ll_exti.h"

//------------------------------------------------------------------------------
// Stringification
//------------------------------------------------------------------------------

// This is a "helper" macro for VSTR, it is not much use when used directly
// If you use STR(MACRONAME) in your code, you'll get the macro NAME in
// string form: "MACRONAME"

#define STR(s) #s

/// Use VSTR(macroname) to get the -value- of <macroname> in quoted-string form
// e.g. if you created this #define:
// #define FOO 1234
// and then use VSTR(FOO), you get the expanded value of FOO in string form
// in your code:
// "1234"

#define VSTR(s) STR(s)

//------------------------------------------------------------------------------
// Compiler/toolchain specific
//------------------------------------------------------------------------------

#define PACKED          __attribute__((packed))
#define MAYBE_UNUSED    __attribute__((unused))
#define NEVER_RETURNS   __attribute__((noreturn))

// Global interrupt control

#define SAVE_AND_DISABLE_INTERRUPTS() \
    uint32_t u32_primask = __get_PRIMASK(); \
    __disable_irq()

#define RESTORE_INTERRUPTS() \
    __set_PRIMASK(u32_primask)

#define ATOMIC_BLOCK_BEGIN          \
do {                                \
    uint32_t u32_primask;           \
    u32_primask = __get_PRIMASK();  \
    __set_PRIMASK(1);

#define ATOMIC_BLOCK_END            \
    __set_PRIMASK(u32_primask);     \
} while(0);

// BM2N(mask) : Bitmask-to-number
// Returns number of trailing binary zeroes in <mask>
// 0x0010 0000 -> 20 : the least significant 20 bits are 0
// 0xFFF0 0000 -> 20 : bits 'above' the least significant 1-bit are don't-care
// 0x0000 0001 -> 0
// 0xF000 8000 -> 15
// 0x8000 0000 -> 31
#define BM2N(mask)     (__builtin_ctzl((uint32_t) (mask)))

//------------------------------------------------------------------------------
// Misc. constants
//------------------------------------------------------------------------------

#define US_IN_1S    1000000             // # microseconds in 1 second
#define MS_IN_1S    1000                // # milliseconds in 1 second

#define SYSTEM_TICK()       HAL_GetTick()

#define ELAPSED_TIME(ts)    (SYSTEM_TICK() - (ts))

// Memory allocation sizes for NVM configuration and parameter storage
#define NVM_PARAM_RAM_SIZE              512

// NVM automatic commit delay - 10mS units
// See v_nvm_commit_check() in app_main.c
#define NVM_AUTO_COMMIT_DELAY           500     // 5 seconds (500 * 10mS)

//------------------------------------------------------------------------------
// MCU peripheral / IP block assignments
//------------------------------------------------------------------------------

// Communication interface handles and addresses
// I2C addresses should be expressed in 8-bit form

#define DEBUG_UART_HANDLE               huart2
#define UART2_INTERRUPT_MODE            1
#define GPS_UART_HANDLE                 huart1
#define UART1_INTERRUPT_MODE            1

#define DISPLAY_I2C_HANDLE              hi2c1
#define DISPLAY_I2C_ADDRESS             (0x3C << 1)

#define GPS_I2C_HANDLE                  hi2c1
#define GPS_I2C_ADDRESS                 (0x42 << 1)

// Periodic interrupt timer configuration

#define PERIODIC_INT_TIMER_HANDLE       htim6
#define PERIODIC_TIMER_INTERVAL_MS      10
// Time base (count clock rate) to use for the periodic interrupt timer.
// The typical value set here (10 uS) should be good for most applications.
// If it is desired to set the periodic interval to something greater
// than 655 mS, then this value needs to be increased.
#define PERIODIC_TIMER_CLOCK_PERIOD_US  10      // MICROSECOND units

// HAL timer handle to use for short delay generation
// Used by v_delay_us() function in utils.c
// This timer must not be in use by any other purpose or task, as the
// v_delay_us() function will stop/start the timer and reset its counter.

#define DELAY_US_TIMER_HANDLE           htim7

// Timers and timer channels used for motor drive
//
// _TIMER defines should reference a STM HAL timer handle
// These should reference STM HAL timer handles (type TIM_HandleTypeDef) as
// declared in either main.c or tim.c; e.g. htim2
//
// _CHANNEL defines should reference a STM HAL timer channel identifier
// e.g. TIM_CHANNEL_1

// Timer used for indicator LED PWM
// TIM3 CH2 is associated with the BLUE_LED pin
// TIM3 CH4 is associated with the RED_LED pin
// TIM3 CH1 is associated with the DEBUG_LED pin

#define LED_TIMER_HANDLE        htim3
#define LED_PWM_MAX_DUTY        256
#define LED_PWM_NOM_FREQ        1000

#define BLUE_LED_CHANNEL        TIM_CHANNEL_2
#define BLUE_LED_IS_GPIO_OUTPUT LL_GPIO_SetPinMode(BLUE_LED_GPIO_Port, BLUE_LED_Pin, LL_GPIO_MODE_OUTPUT)
#define BLUE_LED_IS_ALTFUNC     LL_GPIO_SetPinMode(BLUE_LED_GPIO_Port, BLUE_LED_Pin, LL_GPIO_MODE_ALTERNATE)
#define BLUE_LED_ON             HAL_GPIO_WritePin(BLUE_LED_GPIO_Port, BLUE_LED_Pin, 1)
#define BLUE_LED_OFF            HAL_GPIO_WritePin(BLUE_LED_GPIO_Port, BLUE_LED_Pin, 0)
#define BLUE_LED_TOGGLE         HAL_GPIO_TogglePin(BLUE_LED_GPIO_Port, BLUE_LED_Pin)

#define RED_LED_CHANNEL         TIM_CHANNEL_4
#define RED_LED_IS_GPIO_OUTPUT  LL_GPIO_SetPinMode(RED_LED_GPIO_Port, RED_LED_Pin, LL_GPIO_MODE_OUTPUT)
#define RED_LED_IS_ALTFUNC      LL_GPIO_SetPinMode(RED_LED_GPIO_Port, RED_LED_Pin, LL_GPIO_MODE_ALTERNATE)
#define RED_LED_ON              HAL_GPIO_WritePin(RED_LED_GPIO_Port, RED_LED_Pin, 1)
#define RED_LED_OFF             HAL_GPIO_WritePin(RED_LED_GPIO_Port, RED_LED_Pin, 0)
#define RED_LED_TOGGLE          HAL_GPIO_TogglePin(RED_LED_GPIO_Port, RED_LED_Pin)

#define DEBUG_LED_CHANNEL       TIM_CHANNEL_1
#define DEBUG_LED_IS_GPIO_OUTPUT LL_GPIO_SetPinMode(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin, LL_GPIO_MODE_OUTPUT)
#define DEBUG_LED_IS_ALTFUNC    LL_GPIO_SetPinMode(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin, LL_GPIO_MODE_ALTERNATE)
#define DEBUG_LED_ON            HAL_GPIO_WritePin(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin, 0)
#define DEBUG_LED_OFF           HAL_GPIO_WritePin(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin, 1)
#define DEBUG_LED_TOGGLE        HAL_GPIO_TogglePin(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin)

#define NUCLEO_LED_CLEAR()      HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, 0)
#define NUCLEO_LED_SET()        HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, 1)
#define NUCLEO_LED_IN_LEVEL()   HAL_GPIO_ReadPin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin)
#define NUCLEO_LED_OUT_LEVEL()  LL_GPIO_IsOutputPinSet(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin)

//------------------------------------------------------------------------------
// GPIO controls
//------------------------------------------------------------------------------

#define SWITCH1_INT_LEVEL()     HAL_GPIO_ReadPin(SWITCH1_INT_GPIO_Port, SWITCH1_INT_Pin)
#define SWITCH1_PRESSED()       (! SWITCH1_INT_LEVEL())
#define SWITCH2_INT_LEVEL()     HAL_GPIO_ReadPin(SWITCH2_INT_GPIO_Port, SWITCH2_INT_Pin)
#define SWITCH2_PRESSED()       (! SWITCH2_INT_LEVEL())
#define SWITCH3_INT_LEVEL()     HAL_GPIO_ReadPin(SWITCH3_INT_GPIO_Port, SWITCH3_INT_Pin)
#define SWITCH3_PRESSED()       (! SWITCH3_INT_LEVEL())
#define SWITCH4_INT_LEVEL()     HAL_GPIO_ReadPin(SWITCH4_INT_GPIO_Port, SWITCH4_INT_Pin)
#define SWITCH4_PRESSED()       (! SWITCH4_INT_LEVEL())

#define NUCLEO_BUTTON_LEVEL()   HAL_GPIO_ReadPin(NUCLEO_BUTTON_GPIO_Port, NUCLEO_BUTTON_Pin)
#define NUCLEO_BUTTON_PRESSED() (! NUCLEO_BUTTON_LEVEL())

//------------------------------------------------------------------------------
// System / core control
//------------------------------------------------------------------------------

//#define KICK_WATCHDOG()         HAL_IWDG_Refresh(&hiwdg)
#define KICK_WATCHDOG()

#endif // MACROS_H
