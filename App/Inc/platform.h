/******************************************************************************
 * platform.h
 *
 * Generic hardware-interface macros and definitions (skeleton).
 ******************************************************************************/

#ifndef MACROS_H
#define MACROS_H

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

#define ATOMIC_BLOCK_BEGIN \
do { \
    uint32_t u32_primask = __get_PRIMASK(); \
    __set_PRIMASK(1);

#define ATOMIC_BLOCK_END \
    __set_PRIMASK(u32_primask); \
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

//------------------------------------------------------------------------------
// MCU peripheral / IP block assignments
//------------------------------------------------------------------------------

// Console (debug) UART -- retargeted for stdio in main.c via v_stdio_retarget()
#define DEBUG_UART_HANDLE               huart2

// Periodic interrupt timer configuration

#define PERIODIC_INT_TIMER_HANDLE       htim14
#define PERIODIC_TIMER_INTERVAL_MS      1

// HAL timer handle to use for short delay generation
// Used by v_delay_us() function in utils.c
// This timer must not be in use by any other purpose or task, as the
// v_delay_us() function will stop/start the timer and reset its counter.

#define DELAY_US_TIMER_HANDLE           htim7

//------------------------------------------------------------------------------
// System / core control
//------------------------------------------------------------------------------

//#define KICK_WATCHDOG()         HAL_IWDG_Refresh(&hiwdg)
#define KICK_WATCHDOG()

#endif // MACROS_H
