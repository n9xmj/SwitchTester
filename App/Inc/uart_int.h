/******************************************************************************
 * uart.h
 *
 * Interrupt-driven U(S)ART using ST HAL infrastructure
 ******************************************************************************/

#ifndef UART_H
#define UART_H

#include "main.h"

//------------------------------------------------------------------------------

// UART1 - Debug console
// Transmits a lot of text, so large Tx buffer is desirable.
// Data reception is usually just single bytes (human generated keystrokes),
// so Rx buffer can be small.
#define UART1_TX_BUFSIZE        0x200
#define UART1_RX_BUFSIZE        0x10

// UART2 - MCU <-> WIFI (ESP32C3)
// Not used at present
// Optimal buffer sizes TBD
#define UART2_TX_BUFSIZE        0x100
#define UART2_RX_BUFSIZE        0x100

// UART3 - Not used
#define UART3_TX_BUFSIZE        0
#define UART3_RX_BUFSIZE        0

// UART4 - File transfer to/from MCU
// Needs a large receive buffer to handle incoming packet buffering during times
// that reception processing is delayed due to SPIFLASH sector erase operations.
// Does not require a large transmit buffer.
#define UART4_TX_BUFSIZE        0x20
#define UART4_RX_BUFSIZE        0x880   // 2K + 128

// UART5 - Not used
#define UART5_TX_BUFSIZE        0
#define UART5_RX_BUFSIZE        0

// UART6 - Not used
#define UART6_TX_BUFSIZE        0
#define UART6_RX_BUFSIZE        0

//------------------------------------------------------------------------------

extern void v_uart_interrupt_init(void);

extern uint8_t u8_uart_tx(UART_HandleTypeDef *huart, uint16_t u16_data, uint32_t u32_timeout);
extern uint8_t u8_uart_tx_multi(UART_HandleTypeDef *huart, const void *v_data, uint16_t *p_u16_size, uint32_t u32_timeout);
extern uint8_t u8_uart_rx(UART_HandleTypeDef *huart, uint16_t *u16_data, uint32_t u32_timeout);
extern uint8_t u8_uart_rx_multi(UART_HandleTypeDef *huart, void *v_data, uint16_t *p_u16_size, uint32_t u32_timeout);

extern uint8_t u8_uart_tx_ready(UART_HandleTypeDef *huart);
extern uint8_t u8_uart_rx_ready(UART_HandleTypeDef *huart);
extern uint8_t u8_uart_wait_tx_done(UART_HandleTypeDef *huart, uint32_t u32_timeout);
extern uint8_t u8_uart_wait_rx_idle(UART_HandleTypeDef *huart, uint32_t u32_min_idle_time, uint32_t u32_timeout);
extern void v_uart_tx_clear(UART_HandleTypeDef *huart);
extern void v_uart_rx_clear(UART_HandleTypeDef *huart);

extern void v_uart_rx_isr(UART_HandleTypeDef *huart);
extern void v_uart_tx_isr(UART_HandleTypeDef *huart);

#endif
