/**
 * @file    uart_stream.c
 * @brief   Register-level UART streaming driver, coexisting with the HAL.
 *
 * @details
 * See uart_stream.h for the integration contract. Design rationale and the
 * decision history behind the choices here live in
 * @c Docs/planning/uart-stream-integration-plan.md.
 */

#include "uart_stream.h"

/*==============================================================================
 * Module state
 *============================================================================*/

/** @brief Instance pool; a slot is in use when @c b_active is set. */
uart_stream_instance_t g_x_uart_stream_instances[UART_STREAM_MAX_INSTANCES] = {0};

/*==============================================================================
 * Private helpers
 *============================================================================*/

/**
 * @brief  Resolve a HAL handle to the NVIC vector serving it.
 * @param  p_x_huart Handle to look up.
 * @param  p_e_irqn  Receives the vector on success.
 * @retval true   Found in the application's target table.
 * @retval false  Not listed; the UART cannot be bound.
 *
 * @note This is the whole of the module's per-target knowledge, and it is
 *       supplied by the application rather than compiled in.
 */
static bool b_uart_stream_lookup_irqn(const UART_HandleTypeDef *p_x_huart,
                                      IRQn_Type *p_e_irqn)
{
    uint8_t u8_idx;

    for (u8_idx = 0U; u8_idx < g_u8_uart_stream_target_count; u8_idx++)
    {
        if (g_x_uart_stream_target[u8_idx].p_x_huart == p_x_huart)
        {
            *p_e_irqn = g_x_uart_stream_target[u8_idx].e_irqn;
            return true;
        }
    }
    return false;
}

/**
 * @brief  Find the bound instance owning a HAL handle.
 * @param  p_x_huart Handle to match.
 * @return Instance pointer, or @c NULL when this UART is not bound.
 */
static uart_stream_instance_t *p_x_uart_stream_find(const UART_HandleTypeDef *p_x_huart)
{
    uint8_t u8_idx;

    for (u8_idx = 0U; u8_idx < UART_STREAM_MAX_INSTANCES; u8_idx++)
    {
        uart_stream_instance_t *p_x_inst = &g_x_uart_stream_instances[u8_idx];

        if (p_x_inst->b_active && (p_x_inst->p_x_huart == p_x_huart))
        {
            return p_x_inst;
        }
    }
    return NULL;
}

/**
 * @brief  Validate a handle and return its live instance.
 * @param  h_stream Handle to check.
 * @return Instance pointer, or @c NULL if out of range or not bound.
 */
static uart_stream_instance_t *p_x_uart_stream_valid(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst;

    if (h_stream >= UART_STREAM_MAX_INSTANCES)
    {
        return NULL;
    }

    p_x_inst = &g_x_uart_stream_instances[h_stream];
    if (!p_x_inst->b_active || (p_x_inst->p_x_huart == NULL))
    {
        return NULL;
    }
    return p_x_inst;
}

/**
 * @brief Arm the transmit-empty interrupt for an instance.
 * @param p_x_inst Bound instance.
 */
static inline void v_uart_stream_tx_arm(uart_stream_instance_t *p_x_inst)
{
    p_x_inst->p_x_huart->Instance->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

/**
 * @brief Service one bound instance at register level.
 *
 * Drains the RX FIFO into the ring, fills the TX FIFO from the ring, clears any
 * communication errors without disturbing reception, and disarms the
 * transmit-empty interrupt once the ring runs dry.
 *
 * @param p_x_inst Bound instance with a pending interrupt.
 *
 * @note Uses the lock-free queue variants throughout: this runs in interrupt
 *       context, where foreground cannot preempt it, and masking PRIMASK per
 *       byte would stall higher-priority interrupts.
 */
/*
 * PORT BOUNDARY -- family-specific register surface.
 *
 * Everything below assumes the FIFO-capable USART IP (see uart_stream.h,
 * "STM32-family portability"). A port to a legacy USARTv1 family (F1/F2/F4/F7/
 * L1) remaps ONLY these tokens, all confined to this function:
 *   registers   : ISR->SR, TDR->DR, RDR->DR   (CR1/ICR differ too)
 *   RX ready    : USART_ISR_RXNE_RXFNE   -> USART_SR_RXNE
 *   TX ready    : USART_ISR_TXE_TXFNF    -> USART_SR_TXE
 *   errors      : USART_ISR_ORE/FE/NE/PE -> USART_SR_ORE/FE/NE/PE (SR read-clears)
 *   TX arm bit  : USART_CR1_TXEIE_TXFNFIE (see v_uart_stream_tx_arm)
 * The logic itself does not change.
 */
static void v_uart_stream_service(uart_stream_instance_t *p_x_inst)
{
    USART_TypeDef *p_x_reg = p_x_inst->p_x_huart->Instance;
    uint32_t       u32_isr = p_x_reg->ISR;

    /* Diagnostic: one unconditional bump per real servicing of this instance.
     * Free-running, wraps at 2^32, never checked -- it is the wiring tripwire
     * described in uart_stream.h (u32_uart_stream_get_isr_service_count). */
    p_x_inst->u32_isr_service_count++;

    /* RX: drain while the FIFO has data. Status is re-read only after a byte is
     * actually taken, so the exit iteration costs no extra register access. */
    while ((u32_isr & USART_ISR_RXNE_RXFNE) != 0U)
    {
        uint8_t u8_byte = (uint8_t) p_x_reg->RDR;

        if (!b_queue_enqueue_isr(&p_x_inst->x_rx_queue, u8_byte))
        {
            p_x_inst->u32_error_count++;    /* ring full - byte lost */
        }
        u32_isr = p_x_reg->ISR;
    }

    /* TX: fill while the FIFO has room and the ring has data. The register test
     * comes first so a clear TXE short-circuits before touching the ring. */
    while (((u32_isr & USART_ISR_TXE_TXFNF) != 0U)
           && !b_queue_is_empty(&p_x_inst->x_tx_queue))
    {
        int16_t i16_byte = i16_queue_dequeue_isr(&p_x_inst->x_tx_queue);

        if (i16_byte < 0)
        {
            break;                          /* raced empty; nothing to send */
        }
        p_x_reg->TDR = (uint8_t) i16_byte;
        u32_isr = p_x_reg->ISR;
    }

    /* Errors: clear and count. Never disable the UART - that is the entire
     * reason this driver owns the vector instead of HAL. */
    if ((u32_isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U)
    {
        p_x_reg->ICR = (USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF);
        p_x_inst->u32_error_count++;
    }

    if (b_queue_is_empty(&p_x_inst->x_tx_queue))
    {
        p_x_reg->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
}

/*==============================================================================
 * Bind / unbind
 *============================================================================*/

uart_stream_h_t x_uart_stream_init(UART_HandleTypeDef *p_x_huart,
                                   uint16_t u16_rx_buf_size, uint8_t *p_u8_rx_buf,
                                   uint16_t u16_tx_buf_size, uint8_t *p_u8_tx_buf)
{
    uart_stream_instance_t *p_x_inst = NULL;
    uart_stream_h_t         h_stream = UART_STREAM_HANDLE_INVALID;
    IRQn_Type               e_irqn;
    uint8_t                 u8_idx;

    if ((p_x_huart == NULL) || (p_x_huart->Instance == NULL))
    {
        return UART_STREAM_HANDLE_INVALID;
    }
    if ((u16_rx_buf_size < 2U) || (u16_tx_buf_size < 2U))
    {
        return UART_STREAM_HANDLE_INVALID;
    }
    if (!b_uart_stream_lookup_irqn(p_x_huart, &e_irqn))
    {
        return UART_STREAM_HANDLE_INVALID;  /* absent from the target table */
    }
    if (p_x_uart_stream_find(p_x_huart) != NULL)
    {
        return UART_STREAM_HANDLE_INVALID;  /* already bound */
    }

    for (u8_idx = 0U; u8_idx < UART_STREAM_MAX_INSTANCES; u8_idx++)
    {
        if (!g_x_uart_stream_instances[u8_idx].b_active)
        {
            h_stream = (uart_stream_h_t) u8_idx;
            p_x_inst = &g_x_uart_stream_instances[u8_idx];
            break;
        }
    }
    if (p_x_inst == NULL)
    {
        return UART_STREAM_HANDLE_INVALID;  /* pool exhausted */
    }

    if (!b_queue_init(&p_x_inst->x_rx_queue, u16_rx_buf_size, p_u8_rx_buf))
    {
        return UART_STREAM_HANDLE_INVALID;
    }
    if (!b_queue_init(&p_x_inst->x_tx_queue, u16_tx_buf_size, p_u8_tx_buf))
    {
        v_queue_release(&p_x_inst->x_rx_queue);
        return UART_STREAM_HANDLE_INVALID;
    }

    p_x_inst->p_x_huart            = p_x_huart;
    p_x_inst->e_irqn               = e_irqn;
    p_x_inst->u32_error_count      = 0U;
    p_x_inst->u32_isr_service_count = 0U;
    p_x_inst->b_active             = true;

    /* Mark the HAL handle busy so a stray HAL_UART_* call fails fast with
     * HAL_BUSY rather than racing this driver for TDR/RDR. */
    p_x_huart->gState  = HAL_UART_STATE_BUSY;
    p_x_huart->RxState = HAL_UART_STATE_BUSY;

    /* Interrupt sources first, then the vector: nothing can fire until the
     * instance above is fully populated. */
    p_x_huart->Instance->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    p_x_huart->Instance->CR1 |= USART_CR1_UE;
    NVIC_EnableIRQ(e_irqn);

    return h_stream;
}

void v_uart_stream_deinit(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if (p_x_inst == NULL)
    {
        return;
    }

    p_x_inst->p_x_huart->Instance->CR1 &=
        ~(USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TXEIE_TXFNFIE);

    /* The NVIC channel stays enabled on purpose: on shared vectors, disabling
     * it here would also silence HAL's service of the other peripherals. */

    p_x_inst->p_x_huart->gState  = HAL_UART_STATE_READY;
    p_x_inst->p_x_huart->RxState = HAL_UART_STATE_READY;

    v_queue_release(&p_x_inst->x_rx_queue);
    v_queue_release(&p_x_inst->x_tx_queue);

    p_x_inst->p_x_huart            = NULL;
    p_x_inst->u32_error_count      = 0U;
    p_x_inst->u32_isr_service_count = 0U;
    p_x_inst->b_active             = false;
}

/*==============================================================================
 * Interrupt entry point
 *============================================================================*/

bool b_uart_stream_service_uart(UART_HandleTypeDef *p_x_huart)
{
    uart_stream_instance_t *p_x_inst;

    if (p_x_huart == NULL)
    {
        return false;
    }

    p_x_inst = p_x_uart_stream_find(p_x_huart);
    if (p_x_inst == NULL)
    {
        /* Not ours. Hand it straight to HAL - no pre-filter, because
         * HAL_UART_IRQHandler() opens by reading ISR/CR1/CR3 and testing
         * exactly that; duplicating it would pay for the same reads twice. */
        HAL_UART_IRQHandler(p_x_huart);
        return false;
    }

    /* Ours. Skip the service call when a sibling on this shared vector was the
     * one that fired - but return true regardless, so the caller never hands an
     * owned UART to HAL. */
    if ((p_x_huart->Instance->ISR
         & (USART_ISR_RXNE_RXFNE | USART_ISR_TXE_TXFNF
            | USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U)
    {
        v_uart_stream_service(p_x_inst);
    }
    return true;
}

/*==============================================================================
 * Transmit
 *============================================================================*/

bool b_uart_stream_tx_byte(uart_stream_h_t h_stream, uint8_t u8_data)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if (p_x_inst == NULL)
    {
        return false;
    }

    if (!b_queue_enqueue(&p_x_inst->x_tx_queue, u8_data))
    {
        return false;
    }
    v_uart_stream_tx_arm(p_x_inst);
    return true;
}

bool b_uart_stream_tx_byte_blocking(uart_stream_h_t h_stream,
                                    uint8_t u8_data,
                                    uint32_t u32_timeout_ms)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);
    uint32_t u32_start;

    if (p_x_inst == NULL)
    {
        return false;
    }

    u32_start = HAL_GetTick();
    do
    {
        if (b_queue_enqueue(&p_x_inst->x_tx_queue, u8_data))
        {
            v_uart_stream_tx_arm(p_x_inst);
            return true;
        }
        /* Re-arm even on a failed attempt: the ISR may have just drained the
         * ring empty and cleared TXEIE, and nothing else would turn it back on. */
        v_uart_stream_tx_arm(p_x_inst);
    }
    while ((HAL_GetTick() - u32_start) < u32_timeout_ms);

    return false;
}

uint16_t u16_uart_stream_tx_multi(uart_stream_h_t h_stream,
                                  const uint8_t *p_u8_src, uint16_t u16_len)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);
    uint16_t u16_queued;

    if ((p_x_inst == NULL) || (p_u8_src == NULL) || (u16_len == 0U))
    {
        return 0U;
    }

    u16_queued = u16_queue_enqueue_multi(&p_x_inst->x_tx_queue, p_u8_src, u16_len);
    if (u16_queued > 0U)
    {
        v_uart_stream_tx_arm(p_x_inst);
    }
    return u16_queued;
}

uint16_t u16_uart_stream_tx_multi_blocking(uart_stream_h_t h_stream,
                                           const uint8_t *p_u8_src,
                                           uint16_t u16_len,
                                           uint32_t u32_timeout_ms)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);
    const uint8_t *p_u8_ptr;
    uint16_t       u16_remaining;
    uint32_t       u32_start;

    if ((p_x_inst == NULL) || (p_u8_src == NULL) || (u16_len == 0U))
    {
        return 0U;
    }

    p_u8_ptr      = p_u8_src;
    u16_remaining = u16_len;
    u32_start     = HAL_GetTick();

    do
    {
        uint16_t u16_written = u16_queue_enqueue_multi(&p_x_inst->x_tx_queue,
                                                       p_u8_ptr, u16_remaining);
        u16_remaining = (uint16_t) (u16_remaining - u16_written);
        p_u8_ptr     += u16_written;

        /* Unconditional re-arm, every iteration - including one that placed no
         * bytes. This is what makes a payload larger than the ring safe: the
         * ISR clears TXEIE whenever it drains the ring empty, and without this
         * the loop would wait forever on a ring nothing can drain. */
        v_uart_stream_tx_arm(p_x_inst);

        if (u16_remaining == 0U)
        {
            break;
        }
    }
    while ((HAL_GetTick() - u32_start) < u32_timeout_ms);

    return (uint16_t) (u16_len - u16_remaining);
}

void v_uart_stream_tx_flush_timeout(uart_stream_h_t h_stream,
                                    uint32_t u32_drain_timeout_ms)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);
    USART_TypeDef *p_x_reg;
    uint32_t       u32_start;
    uint32_t       u32_baud;
    uint32_t       u32_tc_timeout_ms;

    if (p_x_inst == NULL)
    {
        return;
    }
    p_x_reg = p_x_inst->p_x_huart->Instance;

    /* Ring drain, bounded by the caller's estimate of the transmission time. */
    v_uart_stream_tx_arm(p_x_inst);
    u32_start = HAL_GetTick();
    while (!b_queue_is_empty(&p_x_inst->x_tx_queue)
           && ((HAL_GetTick() - u32_start) < u32_drain_timeout_ms))
    {
        v_uart_stream_tx_arm(p_x_inst);
    }

    /* Shift register drain - at most one character time away once the ring is
     * empty, so DERIVE the bound from the rate actually in effect rather than
     * trusting a fixed constant. 12 bit-times covers 8N1 plus parity/2-stop;
     * +2 ms absorbs tick granularity. UART_STREAM_FLUSH_TC_TIMEOUT_MS remains
     * the floor, and the fallback when the rate cannot be read.
     *
     * A fixed constant is wrong at both ends: 2 ms is ~184 character times at
     * 921600 (harmlessly generous) but SHORTER than one character below about
     * 4800 baud, where it would return with a character still shifting and let
     * a following BRR change clip it. */
    u32_tc_timeout_ms = UART_STREAM_FLUSH_TC_TIMEOUT_MS;
    u32_baud = u32_uart_stream_get_baud(h_stream);
    if (u32_baud != 0U)
    {
        uint32_t u32_calc = ((12U * 1000U) / u32_baud) + 2U;

        if (u32_calc > u32_tc_timeout_ms)
        {
            u32_tc_timeout_ms = u32_calc;
        }
    }

    u32_start = HAL_GetTick();
    while (((p_x_reg->ISR & USART_ISR_TC) == 0U)
           && ((HAL_GetTick() - u32_start) < u32_tc_timeout_ms))
    {
        /* spin */
    }

    p_x_reg->ICR = USART_ICR_TCCF;
}

void v_uart_stream_tx_flush(uart_stream_h_t h_stream)
{
    v_uart_stream_tx_flush_timeout(h_stream, UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS);
}

/*
 * Which kernel clock feeds this instance. This is a FAMILY PORT BOUNDARY of the same
 * class as the register surface in v_uart_stream_service: instances with an
 * independent clock mux are asked via HAL_RCCEx_GetPeriphCLKFreq, and the rest
 * run from PCLK1. A port to another family revisits the selector list only.
 */
static uint32_t u32_uart_stream_kernel_clock(USART_TypeDef *p_x_reg)
{
    uint32_t u32_sel = 0U;

    if (p_x_reg == USART1)              { u32_sel = RCC_PERIPHCLK_USART1;  }
    else if (p_x_reg == USART2)         { u32_sel = RCC_PERIPHCLK_USART2;  }
#if defined(USART3)
    else if (p_x_reg == USART3)         { u32_sel = RCC_PERIPHCLK_USART3;  }
#endif
#if defined(LPUART1)
    else if (p_x_reg == LPUART1)        { u32_sel = RCC_PERIPHCLK_LPUART1; }
#endif
#if defined(LPUART2)
    else if (p_x_reg == LPUART2)        { u32_sel = RCC_PERIPHCLK_LPUART2; }
#endif
    else                                { u32_sel = 0U;                   }

    return (u32_sel != 0U) ? HAL_RCCEx_GetPeriphCLKFreq(u32_sel)
                           : HAL_RCC_GetPCLK1Freq();
}

uint32_t u32_uart_stream_get_baud(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);
    USART_TypeDef *p_x_reg;
    uint32_t u32_fck;
    uint32_t u32_brr;
    uint32_t u32_div;

    if (p_x_inst == NULL)
    {
        return 0U;
    }
    p_x_reg = p_x_inst->p_x_huart->Instance;
    u32_brr = p_x_reg->BRR;
    if (u32_brr == 0U)
    {
        return 0U;
    }
    u32_fck = u32_uart_stream_kernel_clock(p_x_reg);

    if (IS_LPUART_INSTANCE(p_x_reg))
    {
        /* LPUARTDIV is fixed-point: baud = 256 * fck / BRR. */
        return (uint32_t) (((uint64_t) u32_fck * 256U) / u32_brr);
    }
    if ((p_x_reg->CR1 & USART_CR1_OVER8) != 0U)
    {
        /* Oversampling by 8 parks USARTDIV[3:1] in BRR[2:0]. */
        u32_div = (u32_brr & 0xFFF0U) | ((u32_brr & 0x0007U) << 1U);
        return (u32_div != 0U) ? ((2U * u32_fck) / u32_div) : 0U;
    }
    return u32_fck / u32_brr;
}

uint32_t u32_uart_stream_set_baud(uart_stream_h_t h_stream, uint32_t u32_baud)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);
    USART_TypeDef *p_x_reg;
    uint32_t u32_fck;
    uint32_t u32_div;
    uint32_t u32_brr;
    uint32_t u32_cr1;

    if ((p_x_inst == NULL) || (u32_baud == 0U))
    {
        return 0U;
    }
    p_x_reg = p_x_inst->p_x_huart->Instance;
    u32_fck = u32_uart_stream_kernel_clock(p_x_reg);

    if (IS_LPUART_INSTANCE(p_x_reg))
    {
        u32_brr = (uint32_t) ((((uint64_t) u32_fck * 256U) + (u32_baud / 2U))
                              / u32_baud);
        if (u32_brr < 0x300U)       /* LPUART floor, per the reference manual */
        {
            return 0U;
        }
    }
    else if ((p_x_reg->CR1 & USART_CR1_OVER8) != 0U)
    {
        u32_div = ((2U * u32_fck) + (u32_baud / 2U)) / u32_baud;
        u32_brr = (u32_div & 0xFFF0U) | ((u32_div & 0x000FU) >> 1U);
    }
    else
    {
        u32_brr = (u32_fck + (u32_baud / 2U)) / u32_baud;
        if (u32_brr < 16U)          /* USARTDIV must be >= 16 */
        {
            return 0U;
        }
    }
    if (u32_brr > 0xFFFFU)
    {
        return 0U;                  /* rate unreachable on this clock */
    }

    /* Unguarded: see the header. UE drops only because BRR does not take
     * effect while enabled; CR1's interrupt arming is restored with it. */
    u32_cr1 = p_x_reg->CR1;
    p_x_reg->CR1 = u32_cr1 & ~USART_CR1_UE;
    p_x_reg->BRR = u32_brr;
    p_x_reg->CR1 = u32_cr1;

    /* Keep the HAL's cached view honest for anything that still reads it. */
    p_x_inst->p_x_huart->Init.BaudRate = u32_baud;

    return u32_uart_stream_get_baud(h_stream);
}

/*==============================================================================
 * Receive
 *============================================================================*/

int16_t i16_uart_stream_rx_byte(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if (p_x_inst == NULL)
    {
        return -1;
    }
    return i16_queue_dequeue(&p_x_inst->x_rx_queue);
}

uint16_t u16_uart_stream_rx_multi(uart_stream_h_t h_stream,
                                  uint8_t *p_u8_dest, uint16_t u16_max_len)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if ((p_x_inst == NULL) || (p_u8_dest == NULL) || (u16_max_len == 0U))
    {
        return 0U;
    }
    return u16_queue_dequeue_multi(&p_x_inst->x_rx_queue, p_u8_dest, u16_max_len);
}

/*==============================================================================
 * Status
 *============================================================================*/

uint16_t u16_uart_stream_tx_queue_used(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    return (p_x_inst == NULL) ? 0U : u16_queue_used(&p_x_inst->x_tx_queue);
}

uint16_t u16_uart_stream_tx_queue_free(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    return (p_x_inst == NULL) ? 0U : u16_queue_available(&p_x_inst->x_tx_queue);
}

uint16_t u16_uart_stream_rx_queue_used(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    return (p_x_inst == NULL) ? 0U : u16_queue_used(&p_x_inst->x_rx_queue);
}

uint16_t u16_uart_stream_rx_queue_free(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    return (p_x_inst == NULL) ? 0U : u16_queue_available(&p_x_inst->x_rx_queue);
}

uint32_t u32_uart_stream_get_error_count(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    return (p_x_inst == NULL) ? 0U : p_x_inst->u32_error_count;
}

void v_uart_stream_clear_error_count(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if (p_x_inst != NULL)
    {
        p_x_inst->u32_error_count = 0U;
    }
}

uint32_t u32_uart_stream_get_isr_service_count(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    return (p_x_inst == NULL) ? 0U : p_x_inst->u32_isr_service_count;
}

void v_uart_stream_clear_isr_service_count(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if (p_x_inst != NULL)
    {
        p_x_inst->u32_isr_service_count = 0U;
    }
}

bool b_uart_stream_is_tx_busy(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_valid(h_stream);

    if (p_x_inst == NULL)
    {
        return false;
    }
    if (!b_queue_is_empty(&p_x_inst->x_tx_queue))
    {
        return true;
    }
    return ((p_x_inst->p_x_huart->Instance->ISR & USART_ISR_TC) == 0U);
}

uart_stream_instance_t *p_x_uart_stream_get_instance(uart_stream_h_t h_stream)
{
    if (h_stream >= UART_STREAM_MAX_INSTANCES)
    {
        return NULL;
    }
    return &g_x_uart_stream_instances[h_stream];
}
