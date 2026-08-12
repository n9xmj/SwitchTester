/******************************************************************************
 * uart_stream_target_g0b1.c
 *
 * Application-owned UART inventory for uart_stream, specific to the STM32G0B1.
 *
 * FILE-NAMING CONVENTION: target-specific sources carry a _<part> suffix. This
 * file is the ONLY per-MCU piece uart_stream needs -- porting to another STM32
 * means dropping in uart_stream_target_<newpart>.c (a different vector map) and
 * deleting this one. The driver core carries no target conditionals.
 *
 * WEAK HANDLES -- how one file serves every G0B1 build:
 *
 *   The eight handle symbols below are declared __attribute__((weak)). For a
 *   UART this build actually provisions in CubeMX, usart.c defines the handle
 *   strongly and &huartN resolves to the real object. For a UART this build
 *   does NOT provision, the weak reference resolves to NULL at link time -- no
 *   link error. So this same table drops into any G0B1 project unchanged;
 *   entries for absent UARTs are simply NULL and inert.
 *
 *   Why NULL entries need no guard: the table is walked only by
 *   b_uart_stream_lookup_irqn(), which compares each entry against a
 *   caller-validated, non-NULL handle -- a NULL entry can never match, so it is
 *   skipped without a check. x_uart_stream_init() also rejects a NULL handle up
 *   front, so a NULL entry can never be bound.
 *
 *   __attribute__((weak)) is a GCC idiom (arm-none-eabi-gcc / CubeIDE). A Keil
 *   or IAR port swaps it for that toolchain's __weak.
 *
 *   Do NOT #include "usart.h" here: its non-weak extern for a provisioned
 *   handle would collide with the weak declaration in this translation unit.
 *   The weak decls stand alone; the strong definitions in usart.c satisfy them
 *   at link time.
 *
 * STM32G0B1 shares NVIC vectors between UARTs, so several entries legitimately
 * name the same IRQn:
 *
 *   USART1_IRQn                 USART1
 *   USART2_LPUART2_IRQn         USART2, LPUART2
 *   USART3_4_5_6_LPUART1_IRQn   USART3, USART4, USART5, USART6, LPUART1
 ******************************************************************************/

#include "uart_stream.h"   /* -> uart_stream_config.h -> the family header:
                            * UART_HandleTypeDef, IRQn_Type */

/* Weak handle references -- see header comment. A UART this build does not
 * provision resolves to NULL at link time rather than failing the link. */
extern UART_HandleTypeDef huart1   __attribute__((weak));
extern UART_HandleTypeDef huart2   __attribute__((weak));
extern UART_HandleTypeDef huart3   __attribute__((weak));
extern UART_HandleTypeDef huart4   __attribute__((weak));
extern UART_HandleTypeDef huart5   __attribute__((weak));
extern UART_HandleTypeDef huart6   __attribute__((weak));
extern UART_HandleTypeDef hlpuart1 __attribute__((weak));
extern UART_HandleTypeDef hlpuart2 __attribute__((weak));

const uart_stream_target_t g_x_uart_stream_target[] =
{
    { &huart1,   USART1_IRQn               },
    { &huart2,   USART2_LPUART2_IRQn       },   /* console / HIL backdoor */
    { &hlpuart2, USART2_LPUART2_IRQn       },
    { &huart3,   USART3_4_5_6_LPUART1_IRQn },
    { &huart4,   USART3_4_5_6_LPUART1_IRQn },
    { &huart5,   USART3_4_5_6_LPUART1_IRQn },
    { &huart6,   USART3_4_5_6_LPUART1_IRQn },
    { &hlpuart1, USART3_4_5_6_LPUART1_IRQn }
};

const uint8_t g_u8_uart_stream_target_count =
    (uint8_t) (sizeof(g_x_uart_stream_target) / sizeof(g_x_uart_stream_target[0]));
