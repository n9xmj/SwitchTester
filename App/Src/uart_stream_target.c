/******************************************************************************
 * uart_stream_target.c
 *
 * Application-owned description of this build's UART inventory for uart_stream.
 *
 * This is the ONLY per-MCU file the uart_stream module needs. Porting to a
 * different STM32 means editing the table below and nothing else -- the driver
 * itself carries no target conditionals.
 *
 * STM32G0B1 shares NVIC vectors between UARTs, so several entries legitimately
 * name the same IRQn:
 *
 *   USART1_IRQn                 USART1
 *   USART2_LPUART2_IRQn         USART2, LPUART2
 *   USART3_4_5_6_LPUART1_IRQn   USART3, USART4, USART5, USART6, LPUART1
 *
 * List every UART this build configures, whether or not uart_stream binds it.
 * An entry is what makes a UART *bindable*; unlisted UARTs simply stay with the
 * HAL. The table is const and can only reference handles that exist as symbols,
 * so it naturally tracks the CubeMX configuration.
 ******************************************************************************/

#include "uart_stream.h"
#include "usart.h"

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
