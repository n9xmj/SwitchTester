/******************************************************************************
 * uart.c
 *
 * Interrupt-driven U(S)ART using ST HAL infrastructure
 ******************************************************************************/

#include "device_config.h"
#include "uart_int.h"

#if !defined(UART1_INTERRUPT_MODE) || !defined(USART1)
#undef UART1_INTERRUPT_MODE
#define UART1_INTERRUPT_MODE    0
#endif

#if !defined(UART2_INTERRUPT_MODE) || !defined(USART2)
#undef UART2_INTERRUPT_MODE
#define UART2_INTERRUPT_MODE    0
#endif

#if !defined(UART3_INTERRUPT_MODE) || !defined(USART3)
#undef UART3_INTERRUPT_MODE
#define UART3_INTERRUPT_MODE    0
#endif

#if !defined(UART4_INTERRUPT_MODE) || !defined(USART4)
#undef UART4_INTERRUPT_MODE
#define UART4_INTERRUPT_MODE    0
#endif

#if !defined(UART5_INTERRUPT_MODE) || !defined(USART5)
#undef UART5_INTERRUPT_MODE
#define UART5_INTERRUPT_MODE    0
#endif

#if !defined(UART6_INTERRUPT_MODE) || !defined(USART6)
#undef UART6_INTERRUPT_MODE
#define UART6_INTERRUPT_MODE    0
#endif

//------------------------------------------------------------------------------
// How to expand to support additional U(S)ARTS:
// - Support for U(S)ART1 and U(S)ART2 is available in base implementation.
// - Add a #if UARTx_INTERRUPT_MODE check and #define UARTx_INTERRUPT_MODE
//   macro for each UART to be supported in uart_int.h
// - Declare an instance of the fifo_t struct for both transmit and receive
//   FIFOs for each new UART to be supported. See existing declarations for
//   x_uart1_tx_fifo and x_uart1_rx_fifo to see how this is done.
//   Constant initializer structs should also be declared; see declarations
//   for x_uart1_tx_fifo_init and x_uart1_rx_fifo_init to see how this is done.
// - Declare buffers for the transmit and receive queues. These can be
//   statically or dynamically allocated; if dynamic allocation is preferred,
//   it would typically be done in v_uart_interrupt_init().
// - Add appropriate initialization code for each new UART in
//   x_uart1_tx_fifo_init()
// - Add an appropriate if () search term in p_x_uart_tx_fifo_select() and
//   p_x_uart_rx_fifo_select(). Use existing code for USART1 for guidance.
// - The remainder of the code is generic and should not require modification.
//------------------------------------------------------------------------------

// This check and definition are done to permit backwards compatibility with
// STM32 variants that do not have FIFO support.
// STM32's without FIFO support define register bitmasks (in the CMSIS register
// definition headers) using the names shown within the #if blocks.

#if defined(USART_ISR_RXNE_RXFNE) && !defined(USART_ISR_RXNE)
#define USART_ISR_RXNE USART_ISR_RXNE_RXFNE
#endif

#if defined(USART_ISR_TXE_TXFNF) && !defined(USART_ISR_TXE)
#define USART_ISR_TXE USART_ISR_TXE_TXFNF
#endif

#if defined(USART_CR1_RXNEIE_RXFNEIE) && !defined(USART_CR1_RXNEIE)
#define USART_CR1_RXNEIE USART_CR1_RXNEIE_RXFNEIE
#endif

#if defined(USART_CR1_TXEIE_TXFNFIE) && !defined(USART_CR1_TXEIE)
#define USART_CR1_TXEIE USART_CR1_TXEIE_TXFNFIE
#endif

//------------------------------------------------------------------------------

// UART/FIFO status register - bitmapped

typedef union PACKED
{
    uint8_t all;
    struct
    {
        uint8_t full        : 1;        // FIFO empty
        uint8_t empty       : 1;        // FIFO full
        uint8_t _reserved1  : 2;
        uint8_t element_u16 : 1;        // FIFO elements are uint16_t's
        uint8_t _reserved2  : 3;
    };
}
fifo_status_t;

// FIFO control block

typedef struct
{
    union
    {
        uint8_t         *p_u8_buffer;   // Pointer to buffer/queue
        uint16_t        *p_u16_buffer;
    };
    uint16_t            u16_bufsize;    // Size of FIFO buffer in ELEMENT units - if using uint16_t elements, this should be set to sizeof(buffer) / sizeof(uint16_t)
    volatile uint16_t   u16_head;       // FIFO head pointer - enqueued data stored at this index
    volatile uint16_t   u16_tail;       // FIFO tail pointer - dequeued data retrieved from this index

    volatile fifo_status_t x_status;    // FIFO status/control register
    uint8_t             u8_reserved;
}
fifo_t;

//------------------------------------------------------------------------------

// U(S)ART1 FIFO control blocks and buffers

#if UART1_INTERRUPT_MODE != 0
fifo_t x_uart1_tx_fifo;
fifo_t x_uart1_rx_fifo;
uint8_t u8_uart1_tx_buffer[UART1_TX_BUFSIZE];
uint8_t u8_uart1_rx_buffer[UART1_RX_BUFSIZE];
#endif

// U(S)ART2 FIFO control blocks and buffers

#if UART2_INTERRUPT_MODE != 0
fifo_t x_uart2_tx_fifo;
fifo_t x_uart2_rx_fifo;
uint8_t u8_uart2_tx_buffer[UART2_TX_BUFSIZE];
uint8_t u8_uart2_rx_buffer[UART2_RX_BUFSIZE];
#endif

// U(S)ART3 FIFO control blocks and buffers

#if UART3_INTERRUPT_MODE != 0
fifo_t x_uart3_tx_fifo;
fifo_t x_uart3_rx_fifo;
uint8_t u8_uart3_tx_buffer[UART3_TX_BUFSIZE];
uint8_t u8_uart3_rx_buffer[UART3_RX_BUFSIZE];
#endif

// U(S)ART4 FIFO control blocks and buffers

#if UART4_INTERRUPT_MODE != 0
fifo_t x_uart4_tx_fifo;
fifo_t x_uart4_rx_fifo;
uint8_t u8_uart4_tx_buffer[UART4_TX_BUFSIZE];
uint8_t u8_uart4_rx_buffer[UART4_RX_BUFSIZE];
#endif

// U(S)ART5 FIFO control blocks and buffers

#if UART5_INTERRUPT_MODE != 0
fifo_t x_uart5_tx_fifo;
fifo_t x_uart5_rx_fifo;
uint8_t u8_uart5_tx_buffer[UART5_TX_BUFSIZE];
uint8_t u8_uart5_rx_buffer[UART5_RX_BUFSIZE];
#endif

// U(S)ART6 FIFO control blocks and buffers

#if UART6_INTERRUPT_MODE != 0
fifo_t x_uart6_tx_fifo;
fifo_t x_uart6_rx_fifo;
uint8_t u8_uart6_tx_buffer[UART6_TX_BUFSIZE];
uint8_t u8_uart6_rx_buffer[UART6_RX_BUFSIZE];
#endif

// U(S)ART1 FIFO control block initialization data

#if UART1_INTERRUPT_MODE != 0
static const fifo_t x_uart1_tx_fifo_init =
{
    .p_u8_buffer = u8_uart1_tx_buffer,
    .u16_bufsize = UART1_TX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};

static const fifo_t x_uart1_rx_fifo_init =
{
    .p_u8_buffer = u8_uart1_rx_buffer,
    .u16_bufsize = UART1_RX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};
#endif

// U(S)ART2 FIFO control block initialization data

#if UART2_INTERRUPT_MODE != 0
static const fifo_t x_uart2_tx_fifo_init =
{
    .p_u8_buffer = u8_uart2_tx_buffer,
    .u16_bufsize = UART2_TX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};

static const fifo_t x_uart2_rx_fifo_init =
{
    .p_u8_buffer = u8_uart2_rx_buffer,
    .u16_bufsize = UART2_RX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};
#endif

// U(S)ART3 FIFO control block initialization data

#if UART3_INTERRUPT_MODE != 0
static const fifo_t x_uart3_tx_fifo_init =
{
    .p_u8_buffer = u8_uart3_tx_buffer,
    .u16_bufsize = UART3_TX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};

static const fifo_t x_uart3_rx_fifo_init =
{
    .p_u8_buffer = u8_uart3_rx_buffer,
    .u16_bufsize = UART3_RX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};
#endif

// U(S)ART4 FIFO control block initialization data

#if UART4_INTERRUPT_MODE != 0
static const fifo_t x_uart4_tx_fifo_init =
{
    .p_u8_buffer = u8_uart4_tx_buffer,
    .u16_bufsize = UART4_TX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};

static const fifo_t x_uart4_rx_fifo_init =
{
    .p_u8_buffer = u8_uart4_rx_buffer,
    .u16_bufsize = UART4_RX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};
#endif

// U(S)ART5 FIFO control block initialization data

#if UART5_INTERRUPT_MODE != 0
static const fifo_t x_uart5_tx_fifo_init =
{
    .p_u8_buffer = u8_uart5_tx_buffer,
    .u16_bufsize = UART5_TX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};

static const fifo_t x_uart5_rx_fifo_init =
{
    .p_u8_buffer = u8_uart5_rx_buffer,
    .u16_bufsize = UART5_RX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};
#endif

// U(S)ART6 FIFO control block initialization data

#if UART6_INTERRUPT_MODE != 0
static const fifo_t x_uart6_tx_fifo_init =
{
    .p_u8_buffer = u8_uart6_tx_buffer,
    .u16_bufsize = UART6_TX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};

static const fifo_t x_uart6_rx_fifo_init =
{
    .p_u8_buffer = u8_uart6_rx_buffer,
    .u16_bufsize = UART6_RX_BUFSIZE,
    .u16_head = 0,
    .u16_tail = 0,
    .x_status.full = 0,
    .x_status.empty = 1,
    .x_status.element_u16 = 0
};
#endif

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_interrupt_init(void)
{
#if UART1_INTERRUPT_MODE != 0
    // Turn off Tx interrupt; gets turned on when u8_uart_tx() is called
    huart1.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_UE);
    // Initialize FIFO queues for UART 1
    x_uart1_tx_fifo = x_uart1_tx_fifo_init;
    x_uart1_rx_fifo = x_uart1_rx_fifo_init;
    // Point HAL Rx/Tx interrupt callbacks to our handlers
    huart1.RxISR = v_uart_rx_isr;
    huart1.TxISR = v_uart_tx_isr;
    // Turn on Rx interrupt
    // Also need to turn on interrupt-on-error, to work around HAL ISR bug
    huart1.Instance->ICR = 0xFFFFFFFF;
    huart1.Instance->CR3 |= USART_CR3_EIE;
    huart1.Instance->CR1 |= USART_CR1_RXNEIE | USART_CR1_UE;
#endif
#if UART2_INTERRUPT_MODE != 0
    // Turn off Tx interrupt; gets turned on when u8_uart_tx() is called
    huart2.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_UE);
    // Initialize FIFO queues for UART 2
    x_uart2_tx_fifo = x_uart2_tx_fifo_init;
    x_uart2_rx_fifo = x_uart2_rx_fifo_init;
    // Point HAL Rx/Tx interrupt callbacks to our handlers
    huart2.RxISR = v_uart_rx_isr;
    huart2.TxISR = v_uart_tx_isr;
    // Turn on Rx interrupt
    // Also need to turn on interrupt-on-error, to work around HAL ISR bug
    huart2.Instance->ICR = 0xFFFFFFFF;
    huart2.Instance->CR3 |= USART_CR3_EIE;
    huart2.Instance->CR1 |= USART_CR1_RXNEIE | USART_CR1_UE;
#endif
#if UART3_INTERRUPT_MODE != 0
    // Turn off Tx interrupt; gets turned on when u8_uart_tx() is called
    huart3.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_UE);
    // Cycle the UART enable - clears any pending Tx/Rx activity
    huart3.Instance->CR1 &= ~USART_CR1_UE;
    huart3.Instance->CR1 |= USART_CR1_UE;
    // Initialize FIFO queues for UART 1
    x_uart3_tx_fifo = x_uart3_tx_fifo_init;
    x_uart3_rx_fifo = x_uart3_rx_fifo_init;
    // Point HAL Rx/Tx interrupt callbacks to our handlers
    huart3.RxISR = v_uart_rx_isr;
    huart3.TxISR = v_uart_tx_isr;
    // Turn on Rx interrupt
    huart3.Instance->CR1 |= USART_CR1_RXNEIE;
#endif
#if UART4_INTERRUPT_MODE != 0
    // Turn off Tx interrupt; gets turned on when u8_uart_tx() is called
    huart4.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_UE);
    // Initialize FIFO queues for UART 1
    x_uart4_tx_fifo = x_uart4_tx_fifo_init;
    x_uart4_rx_fifo = x_uart4_rx_fifo_init;
    // Point HAL Rx/Tx interrupt callbacks to our handlers
    huart4.RxISR = v_uart_rx_isr;
    huart4.TxISR = v_uart_tx_isr;
    // Turn on Rx interrupt
    // Also need to turn on interrupt-on-error, to work around HAL ISR bug
    huart4.Instance->ICR = 0xFFFFFFFF;
    huart4.Instance->CR3 |= USART_CR3_EIE;
    huart4.Instance->CR1 |= USART_CR1_RXNEIE | USART_CR1_UE;
#endif
#if UART5_INTERRUPT_MODE != 0
    // Turn off Tx interrupt; gets turned on when u8_uart_tx() is called
    huart5.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_UE);
    // Initialize FIFO queues for UART 1
    x_uart5_tx_fifo = x_uart4_tx_fifo_init;
    x_uart5_rx_fifo = x_uart4_rx_fifo_init;
    // Point HAL Rx/Tx interrupt callbacks to our handlers
    huart5.RxISR = v_uart_rx_isr;
    huart5.TxISR = v_uart_tx_isr;
    // Turn on Rx interrupt
    // Also need to turn on interrupt-on-error, to work around HAL ISR bug
    huart5.Instance->ICR = 0xFFFFFFFF;
    huart5.Instance->CR3 |= USART_CR3_EIE;
    huart5.Instance->CR1 |= USART_CR1_RXNEIE | USART_CR1_UE;
#endif
#if UART6_INTERRUPT_MODE != 0
    // Turn off Tx interrupt; gets turned on when u8_uart_tx() is called
    huart4.Instance->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_UE);
    // Initialize FIFO queues for UART 1
    x_uart6_tx_fifo = x_uart4_tx_fifo_init;
    x_uart6_rx_fifo = x_uart4_rx_fifo_init;
    // Point HAL Rx/Tx interrupt callbacks to our handlers
    huart6.RxISR = v_uart_rx_isr;
    huart6.TxISR = v_uart_tx_isr;
    // Turn on Rx interrupt
    // Also need to turn on interrupt-on-error, to work around HAL ISR bug
    huart6.Instance->ICR = 0xFFFFFFFF;
    huart6.Instance->CR3 |= USART_CR3_EIE;
    huart6.Instance->CR1 |= USART_CR1_RXNEIE | USART_CR1_UE;
#endif
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_set_baud_rate(UART_HandleTypeDef *huart, uint32_t u32_baudrate)
{
    // Get current UART oversampling, clock prescaler and base clock settings
    uint32_t u32_oversampling = LL_USART_GetOverSampling(huart->Instance);
    uint32_t u32_prescaler = LL_USART_GetPrescaler(huart->Instance);
    uint32_t u32_peripheral_clock = HAL_RCC_GetPCLK1Freq();


#define __LL_USART_DIV_SAMPLING8(__PERIPHCLK__, __PRESCALER__, __BAUDRATE__) \
  (((((__PERIPHCLK__)/(USART_PRESCALER_TAB[(__PRESCALER__)]))*2U)\
    + ((__BAUDRATE__)/2U))/(__BAUDRATE__))
#define __LL_USART_DIV_SAMPLING16(__PERIPHCLK__, __PRESCALER__, __BAUDRATE__) \
  ((((__PERIPHCLK__)/(USART_PRESCALER_TAB[(__PRESCALER__)]))\
    + ((__BAUDRATE__)/2U))/(__BAUDRATE__))

    uint32_t u32_brr;
    if (u32_oversampling == LL_USART_OVERSAMPLING_16)
    {
        u32_brr = u32_peripheral_clock >> u32_prescaler;
        u32_brr += (u32_baudrate >> 1);
        u32_brr /= u32_baudrate;
    }
    else
    {
        u32_brr = (u32_peripheral_clock << 1) >> u32_prescaler;
        u32_brr += (u32_baudrate >> 1);
        u32_brr /= u32_baudrate;

        u32_brr = __LL_USART_DIV_SAMPLING8(u32_pclk, (uint8_t) u32_prescaler, u32_baudrate);
        u32_brr =
    }

    if (PrescalerValue > LL_USART_PRESCALER_DIV256)
    {
      /* Do not overstep the size of USART_PRESCALER_TAB */
    }
    else if (BaudRate == 0U)
    {
      /* Can Not divide per 0 */
    }
    else if (OverSampling == LL_USART_OVERSAMPLING_8)
    {
      usartdiv = (uint16_t)(__LL_USART_DIV_SAMPLING8(PeriphClk, (uint8_t)PrescalerValue, BaudRate));
      brrtemp = usartdiv & 0xFFF0U;
      brrtemp |= (uint16_t)((usartdiv & (uint16_t)0x000FU) >> 1U);
      USARTx->BRR = brrtemp;
    }
    else
    {
      USARTx->BRR = (uint16_t)(__LL_USART_DIV_SAMPLING16(PeriphClk, (uint8_t)PrescalerValue, BaudRate));
    }

}
/******************************************************************************
 *
 ******************************************************************************/

static void v_fifo_enqueue(fifo_t *p_x_fifo, uint16_t u16_data)
{
    if (p_x_fifo->x_status.full)
    {
        return;
    }

    if (p_x_fifo->x_status.element_u16)
    {
        p_x_fifo->p_u16_buffer[p_x_fifo->u16_head] = u16_data;
    }
    else
    {
        p_x_fifo->p_u8_buffer[p_x_fifo->u16_head] = (uint8_t) u16_data;
    }
    p_x_fifo->u16_head++;
    if (p_x_fifo->u16_head >= p_x_fifo->u16_bufsize)
    {
        p_x_fifo->u16_head = 0;
    }
    if (p_x_fifo->u16_head == p_x_fifo->u16_tail)
    {
        p_x_fifo->x_status.full = 1;
    }
    p_x_fifo->x_status.empty = 0;
}

/******************************************************************************
 *
 ******************************************************************************/

static uint16_t u16_fifo_dequeue(fifo_t *p_x_fifo)
{
    uint16_t u16_data;

    if (p_x_fifo->x_status.empty)
    {
        return 0;
    }

    if (p_x_fifo->x_status.element_u16)
    {
        u16_data = p_x_fifo->p_u16_buffer[p_x_fifo->u16_tail];
    }
    else
    {
        u16_data = (uint16_t) (p_x_fifo->p_u8_buffer[p_x_fifo->u16_tail]);
    }

    p_x_fifo->u16_tail++;
    if (p_x_fifo->u16_tail >= p_x_fifo->u16_bufsize)
    {
        p_x_fifo->u16_tail = 0;
    }
    if (p_x_fifo->u16_head == p_x_fifo->u16_tail)
    {
        p_x_fifo->x_status.empty = 1;
    }
    p_x_fifo->x_status.full = 0;

    return u16_data;
}

/******************************************************************************
 *
 ******************************************************************************/

static fifo_t *p_x_uart_tx_fifo_select(UART_HandleTypeDef *huart)
{
#if UART1_INTERRUPT_MODE != 0
    if (huart->Instance == USART1)
    {
        return &x_uart1_tx_fifo;
    }
#endif
#if UART2_INTERRUPT_MODE != 0
    if (huart->Instance == USART2)
    {
        return &x_uart2_tx_fifo;
    }
#endif
#if UART3_INTERRUPT_MODE != 0
    if (huart->Instance == USART3)
    {
        return &x_uart3_tx_fifo;
    }
#endif
#if UART4_INTERRUPT_MODE != 0
    if (huart->Instance == USART4)
    {
        return &x_uart4_tx_fifo;
    }
#endif
#if UART5_INTERRUPT_MODE != 0
    if (huart->Instance == USART5)
    {
        return &x_uart5_tx_fifo;
    }
#endif
#if UART6_INTERRUPT_MODE != 0
    if (huart->Instance == USART6)
    {
        return &x_uart4_tx_fifo;
    }
#endif

    return NULL;
}

/******************************************************************************
 *
 ******************************************************************************/

static fifo_t *p_x_uart_rx_fifo_select(UART_HandleTypeDef *huart)
{
#if UART1_INTERRUPT_MODE != 0
    if (huart->Instance == USART1)
    {
        return &x_uart1_rx_fifo;
    }
#endif
#if UART2_INTERRUPT_MODE != 0
    if (huart->Instance == USART2)
    {
        return &x_uart2_rx_fifo;
    }
#endif
#if UART3_INTERRUPT_MODE != 0
    if (huart->Instance == USART3)
    {
        return &x_uart3_rx_fifo;
    }
#endif
#if UART4_INTERRUPT_MODE
    if (huart->Instance == USART4)
    {
        return &x_uart4_rx_fifo;
    }
#endif
#if UART5_INTERRUPT_MODE
    if (huart->Instance == USART5)
    {
        return &x_uart5_rx_fifo;
    }
#endif
#if UART6_INTERRUPT_MODE
    if (huart->Instance == USART6)
    {
        return &x_uart6_rx_fifo;
    }
#endif

    return NULL;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_tx_ready(UART_HandleTypeDef *huart)
{
    fifo_t *p_x_fifo;

    p_x_fifo = p_x_uart_tx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return 0;
    }

    return !p_x_fifo->x_status.full;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_rx_ready(UART_HandleTypeDef *huart)
{
    fifo_t *p_x_fifo;

    p_x_fifo = p_x_uart_rx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return 0;
    }

    return !p_x_fifo->x_status.empty;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_tx(UART_HandleTypeDef *huart, uint16_t u16_data, uint32_t u32_timeout)
{
    uint32_t u32_timestamp = HAL_GetTick();
    fifo_t *p_x_fifo;
    uint8_t u8_timeout = 0;

    p_x_fifo = p_x_uart_tx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return 1;
    }

    do
    {
        if (!p_x_fifo->x_status.full)
        {
            // Turn off Tx interrupt during queue update
            // Necessary since UART Tx ISR can modify Tx queue state
            huart->Instance->CR1 &= ~USART_CR1_TXEIE;
            // Put data to send into Tx FIFO
            v_fifo_enqueue(p_x_fifo, u16_data);
            // Turn Tx interrupt back on
            huart->Instance->CR1 |= USART_CR1_TXEIE;
            // Done sending - don't need to check for timeout
            break;
        }
        // Turn Tx interrupt back on
        huart->Instance->CR1 |= USART_CR1_TXEIE;
        // If blocked (Tx queue full), check for transmit timeout
        u8_timeout = (HAL_GetTick() - u32_timestamp) >= u32_timeout;
    }
    while (!u8_timeout);


    return u8_timeout;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_tx_multi(UART_HandleTypeDef *huart, const void *v_data, uint16_t *p_u16_size, uint32_t u32_timeout)
{
    uint32_t u32_timestamp = HAL_GetTick();
    fifo_t *p_x_fifo;
    uint16_t u16_index;
    uint16_t u16_data;
    uint8_t u8_timeout = 0;

    p_x_fifo = p_x_uart_tx_fifo_select(huart);
    if ((p_x_fifo == NULL) || (p_u16_size == NULL))
    {
        return 2;
    }

    u16_index = 0;
    while ((u16_index < *p_u16_size) && !u8_timeout)
    {
        if (!p_x_fifo->x_status.full)
        {
            // Perform 8- or 16-bit fetch from input data buffer
            // (fetch size based on queue element size configuration)
            if (p_x_fifo->x_status.element_u16)
            {
                u16_data = ((uint16_t *) v_data)[u16_index];
            }
            else
            {
                u16_data = (uint16_t) ((uint8_t *) v_data)[u16_index];
            }
            // Turn off Tx interrupt during queue update
            // Necessary since UART Tx ISR can modify Tx queue state
            huart->Instance->CR1 &= ~USART_CR1_TXEIE;
            // Put data to send into Tx queue
            v_fifo_enqueue(p_x_fifo, u16_data);
            // Turn Tx interrupt back on
            huart->Instance->CR1 |= USART_CR1_TXEIE;
            u16_index++;
        }
        else
        {
            // Turn Tx interrupt back on
            huart->Instance->CR1 |= USART_CR1_TXEIE;
            while (p_x_fifo->x_status.full && !u8_timeout)
            {
                u8_timeout = (HAL_GetTick() - u32_timestamp) >= u32_timeout;
            }
        }
    }

    *p_u16_size = u16_index;            // Return # elements queued for transmit
    return u8_timeout;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_rx(UART_HandleTypeDef *huart, uint16_t *u16_data, uint32_t u32_timeout)
{
    uint32_t u32_timestamp = HAL_GetTick();
    fifo_t *p_x_fifo;
    uint16_t u16_data_return = 0;
    uint8_t u8_timeout = 0;

    p_x_fifo = p_x_uart_rx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return 2;
    }

    do
    {
        if (!p_x_fifo->x_status.empty)
        {
            // Turn off Rx interrupt during queue fetch
            // Necessary since UART Rx ISR can modify Rx queue state
            ATOMIC_BLOCK_BEGIN
            huart->Instance->CR1 &= ~USART_CR1_RXNEIE;
            huart->Instance->CR3 &= ~USART_CR3_EIE;
            ATOMIC_BLOCK_END
            // Get received data from Rx FIFO
            u16_data_return = u16_fifo_dequeue(p_x_fifo);
            // Turn Rx interrupt back on
            ATOMIC_BLOCK_BEGIN
            huart->Instance->CR1 |= USART_CR1_RXNEIE;
            huart->Instance->CR3 |= USART_CR3_EIE;
            ATOMIC_BLOCK_END
            // Receive successful - don't need to check for timeout
            break;
        }
        // If blocked (Rx queue empty), check for receive timeout
        u8_timeout = (HAL_GetTick() - u32_timestamp) >= u32_timeout;
    }
    while (!u8_timeout);

    if (u16_data != NULL)
    {
        *u16_data = u16_data_return;
    }

    return u8_timeout;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_rx_multi(UART_HandleTypeDef *huart, void *v_data, uint16_t *p_u16_size, uint32_t u32_timeout)
{
    uint32_t u32_timestamp = HAL_GetTick();
    fifo_t *p_x_fifo;
    uint16_t u16_index;
    uint16_t u16_data;
    uint8_t u8_timeout = 0;

    p_x_fifo = p_x_uart_rx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return 2;
    }

    u16_index = 0;

    while ((u16_index < *p_u16_size) && !u8_timeout)
    {
        if (!p_x_fifo->x_status.empty)
        {
            // Turn off Rx interrupt during queue fetch
            // Necessary since UART Rx ISR can modifiy Rx queue state
            ATOMIC_BLOCK_BEGIN
            huart->Instance->CR1 &= ~USART_CR1_RXNEIE;
            huart->Instance->CR3 &= ~USART_CR3_EIE;
            ATOMIC_BLOCK_END
            // Fetch received data from Rx queue
            u16_data = u16_fifo_dequeue(p_x_fifo);
            // Turn Rx interrupt back on
            ATOMIC_BLOCK_BEGIN
            huart->Instance->CR1 |= USART_CR1_RXNEIE;
            huart->Instance->CR3 |= USART_CR3_EIE;
            ATOMIC_BLOCK_END
            // Place receive data into input data buffer
            if (p_x_fifo->x_status.element_u16)
            {
                ((uint16_t *) v_data)[u16_index] = u16_data;
            }
            else
            {
                ((uint8_t *) v_data)[u16_index] = (uint8_t) u16_data;
            }
            u16_index++;
        }
        else
        {
            u8_timeout = (HAL_GetTick() - u32_timestamp) >= u32_timeout;
        }
    }

    *p_u16_size = u16_index;            // Return #elements fetched from receive queue
    return u8_timeout;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_wait_tx_done(UART_HandleTypeDef *huart, uint32_t u32_timeout)
{
    uint32_t u32_timestamp = HAL_GetTick();
    fifo_t *p_x_fifo;
    uint8_t u8_timeout = 0;

    p_x_fifo = p_x_uart_tx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return 2;
    }

    while (!u8_timeout &&
           (!p_x_fifo->x_status.empty ||
            ((huart->Instance->ISR & USART_ISR_TC) == 0)) )
    {
        u8_timeout = (HAL_GetTick() - u32_timestamp) >= u32_timeout;
    }

    return u8_timeout;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_uart_wait_rx_idle(UART_HandleTypeDef *huart, uint32_t u32_min_idle_time, uint32_t u32_timeout)
{
    uint32_t u32_timestamp = HAL_GetTick();
    uint16_t u16_data;
    uint8_t u8_rx_idle;
    uint8_t u8_timeout;

    if (u32_timeout <= u32_min_idle_time)
    {
        u32_timeout = u32_min_idle_time + 1;
    }

    do
    {
        u8_rx_idle = u8_uart_rx(huart, &u16_data, u32_min_idle_time);
        u8_timeout = (HAL_GetTick() - u32_timestamp) >= u32_timeout;
    }
    while (!u8_rx_idle && !u8_timeout);

    return u8_rx_idle;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_tx_clear(UART_HandleTypeDef *huart)
{
    fifo_t *p_x_fifo;

    p_x_fifo = p_x_uart_tx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return;
    }

    // Turn off Tx interrupt during queue manipulation
    // Necessary since UART Tx ISR can modify Tx queue state
    huart->Instance->CR1 &= ~USART_CR1_TXEIE;
    // This clears the UART Tx hardware FIFO
    huart->Instance->RQR = USART_RQR_TXFRQ;

    p_x_fifo->u16_head = 0;
    p_x_fifo->u16_tail = 0;
    p_x_fifo->x_status.empty = 1;
    p_x_fifo->x_status.full = 0;

    // Note: USART_CR1_TXEIE is -not- turned back on here
    // It will get turned on when something is placed into the Tx queue; i.e.
    // when u8_uart_tx() is called.
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_rx_clear(UART_HandleTypeDef *huart)
{
    fifo_t *p_x_fifo;

    p_x_fifo = p_x_uart_rx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return;
    }

    // Turn off Rx interrupt during queue manipulation
    // Necessary since UART Rx ISR can modify Rx queue state
    huart->Instance->CR1 &= ~USART_CR1_RXNEIE;
    // This clears the UART Rx hardware FIFO
    huart->Instance->RQR = USART_RQR_RXFRQ;
    huart->Instance->RDR; // Dummy read

    p_x_fifo->u16_head = 0;
    p_x_fifo->u16_tail = 0;
    p_x_fifo->x_status.empty = 1;
    p_x_fifo->x_status.full = 0;

    huart->Instance->CR1 |= USART_CR1_RXNEIE;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_rx_handler(UART_HandleTypeDef *huart, fifo_t *p_x_fifo)
{
    uint16_t u16_data;

    while (huart->Instance->ISR & USART_ISR_RXNE)
    {
        if (p_x_fifo->x_status.full)
        {
            // Stop servicing Rx interrupts if Rx queue full.
            // Interrupt will get turned back on when a uart_rx() call is made.
            huart->Instance->CR1 &= ~USART_CR1_RXNEIE;
            break;
        }
        // Note:
        // Receive errors (e.g. parity, framing, overrrun) are not checked here.
        // To check for them, look for a non-zero value in huart->ErrorCode.
        u16_data = (uint16_t) huart->Instance->RDR;
        v_fifo_enqueue(p_x_fifo, u16_data);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_tx_handler(UART_HandleTypeDef *huart, fifo_t *p_x_fifo)
{
    uint16_t u16_data;

    while (huart->Instance->ISR & USART_ISR_TXE)
    {
        if (p_x_fifo->x_status.empty)
        {
            // Stop servicing transmit interrupts if Tx queue empty.
            // Interrupt will get turned back on when a uart_tx() call is made.
            huart->Instance->CR1 &= ~USART_CR1_TXEIE;
            break;
        }
        u16_data = u16_fifo_dequeue(p_x_fifo);
        huart->Instance->TDR = u16_data;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_rx_isr(UART_HandleTypeDef *huart)
{
    fifo_t *p_x_fifo;

    p_x_fifo = p_x_uart_rx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return;
    }
    v_uart_rx_handler(huart, p_x_fifo);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_uart_tx_isr(UART_HandleTypeDef *huart)
{
    fifo_t *p_x_fifo;

    p_x_fifo = p_x_uart_tx_fifo_select(huart);
    if (p_x_fifo == NULL)
    {
        return;
    }
    v_uart_tx_handler(huart, p_x_fifo);
}

/******************************************************************************
 *
 ******************************************************************************/

// Called by HAL UART ISR when a UART error occurs that it can't handle
// Overrides the weak definition in stm32*_hal_uart.c

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    // The HAL UART ISR shuts down receive interrupts on certain errors
    // (such as receive overrun).
    // Override this behavior and turn the interrupts back on.

    if (huart->ErrorCode & (HAL_UART_ERROR_RTO | HAL_UART_ERROR_ORE))
    {
        SAVE_AND_DISABLE_INTERRUPTS();
        huart->RxISR = &v_uart_rx_isr;
        huart->Instance->CR3 |= USART_CR3_EIE;
        huart->Instance->CR1 |= USART_CR1_RXNEIE;
        RESTORE_INTERRUPTS();
    }
}
