#ifndef UART_STREAM_H_
#define UART_STREAM_H_

/**
 * @file    uart_stream.h
 * @brief   Interrupt-driven streaming UART driver that coexists with the HAL.
 *
 * @details
 * HAL is used for peripheral **initialisation only** - clocks, pins, baud,
 * word length, FIFO configuration. After a UART is bound here, this module owns
 * that peripheral's interrupt enables and services it at register level. HAL is
 * never allowed into the interrupt path for a bound UART, because
 * @c HAL_UART_IRQHandler() reacts to ORE/FE/NE/PE by calling
 * @c UART_EndRxTransfer(), which disables reception - correct for a
 * transactional API, fatal for an always-on console.
 *
 * UARTs this module does **not** bind continue to be serviced by HAL exactly as
 * before, including when they share an NVIC vector with a bound one.
 *
 * @section uart_stream_portability STM32-family portability
 *
 * Two of the three usual sources of family coupling are designed out: the NVIC
 * vector map lives entirely in the application-owned target table (below), and
 * everything else goes through the family-uniform HAL API
 * (@c UART_HandleTypeDef, @c HAL_UART_IRQHandler, @c HAL_GetTick). The one
 * remaining coupling is the register-level ISR path in @c v_uart_stream_service,
 * which assumes the **FIFO-capable USART IP**: the @c ISR / @c TDR / @c RDR /
 * @c ICR register model and the FIFO-era bit names
 * (@c USART_ISR_RXNE_RXFNE, @c USART_ISR_TXE_TXFNF,
 * @c USART_CR1_RXNEIE_RXFNEIE, the @c *CF clear flags).
 *
 * That IP is shared by G0/C0/G4/L4/L5/U5/H5/H7/WB/WL - so a move to, e.g., an
 * STM32H723 is a near-drop-in: new target table, register path unchanged. The
 * legacy USARTv1 families (F1/F2/F4/F7/L1) use @c SR / @c DR and unsuffixed bit
 * names; porting to one of those means remapping that one register surface,
 * which is fenced off and flagged in @c v_uart_stream_service.
 *
 * @section uart_stream_adopt Integrating into a new target
 *
 * Everything MCU-specific lives in one application-owned table, so this module
 * carries no per-target conditionals.
 *
 * -# **Initialise the UART with CubeMX/HAL as usual.** Baud, word length and
 *    parity are yours. Enabling the peripheral's NVIC channel in CubeMX is
 *    *optional* - see step 4.
 *
 * -# **Provide the target table.** Define @ref g_x_uart_stream_target and
 *    @ref g_u8_uart_stream_target_count in an application file, listing every
 *    UART this build configures together with the NVIC vector it sits on.
 *    Several peripherals legitimately map to one @c IRQn_Type on parts with
 *    shared vectors:
 *    @code
 *    const uart_stream_target_t g_x_uart_stream_target[] =
 *    {
 *        { &huart1,  USART1_IRQn               },
 *        { &huart2,  USART2_LPUART2_IRQn       },
 *        { &hlpuart2, USART2_LPUART2_IRQn      },
 *    };
 *    const uint8_t g_u8_uart_stream_target_count =
 *        (uint8_t) (sizeof(g_x_uart_stream_target) / sizeof(g_x_uart_stream_target[0]));
 *    @endcode
 *
 * -# **Hook the vectors.** In the CubeMX-generated @c *_it.c, put one
 *    @ref b_uart_stream_service_uart call per UART on that vector into the
 *    upper USER CODE block, then @c return. The @c return bypasses the
 *    generated @c HAL_UART_IRQHandler() list, which must not run for a bound
 *    UART; unbound UARTs still reach HAL, because this function forwards to it
 *    internally. The shape deliberately mirrors what CubeMX generates:
 *    @code
 *    void USART2_LPUART2_IRQHandler(void)
 *    {
 *      // USER CODE BEGIN USART2_LPUART2_IRQn 0
 *      b_uart_stream_service_uart(&huart2);
 *      b_uart_stream_service_uart(&hlpuart2);
 *      return;
 *      // USER CODE END USART2_LPUART2_IRQn 0
 *      HAL_UART_IRQHandler(&huart2);
 *      ...
 *    @endcode
 *
 * -# **Bind at run time** with @ref x_uart_stream_init, once the application is
 *    ready to service traffic. That call enables the interrupt sources *and*
 *    the NVIC channel itself, so CubeMX need not enable the channel at init
 *    time - which also sidesteps CubeMX's inconsistent handling of that option.
 *
 * -# **Route stdio** (or your own I/O layer) through the TX/RX calls below.
 *
 * @warning Never call any @c HAL_UART_* API on a bound UART. Binding sets the
 *          HAL handle's @c gState and @c RxState to
 *          @c HAL_UART_STATE_BUSY precisely so such calls fail fast with
 *          @c HAL_BUSY instead of fighting this driver for the data register.
 *
 * @note One producer and one consumer per queue. TX may be written from exactly
 *       one context; RX may be read from exactly one context. See queue.h.
 */

#include <stdint.h>
#include <stdbool.h>

#include "main.h"               /* CubeMX indirection to the right family header:
                                 * UART_HandleTypeDef, USART_TypeDef, IRQn_Type.
                                 * Do NOT include a family-specific header here --
                                 * that would tie the module to one STM32 series. */
#include "queue.h"

/*==============================================================================
 * Configuration
 *============================================================================*/

/** @brief Maximum number of simultaneously bound UARTs. */
#ifndef UART_STREAM_MAX_INSTANCES
#define UART_STREAM_MAX_INSTANCES           6
#endif

/** @brief Default outer drain timeout (ms) for the no-argument flush wrapper. */
#ifndef UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS
#define UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS  50U
#endif

/**
 * @brief FLOOR (ms) on waiting for hardware TC once the ring drains.
 *
 * The actual bound is computed per flush from the rate in effect
 * (12 bit-times + 2 ms; see v_uart_stream_tx_flush_timeout), so this is the
 * minimum and the fallback used when the rate cannot be read. There is no need
 * to raise it for slow instances -- the calculation already covers them, down
 * to 1200 baud and below.
 */
#ifndef UART_STREAM_FLUSH_TC_TIMEOUT_MS
#define UART_STREAM_FLUSH_TC_TIMEOUT_MS     2U
#endif

/** @brief Default deadline (ms) for the blocking TX calls. */
#ifndef UART_STREAM_TX_BLOCK_TIMEOUT_MS
#define UART_STREAM_TX_BLOCK_TIMEOUT_MS     100U
#endif

/** @brief Returned by @ref x_uart_stream_init when a UART cannot be bound. */
#define UART_STREAM_HANDLE_INVALID          ((uart_stream_h_t) 0xFFU)

/** @brief Opaque handle identifying a bound UART. */
typedef uint8_t uart_stream_h_t;

/*==============================================================================
 * Application-provided target description
 *============================================================================*/

/**
 * @brief One UART this build configures, and the NVIC vector it sits on.
 *
 * @note The register base is not stored - @c UART_HandleTypeDef carries it as
 *       its first member, so @c p_x_huart->Instance is always available.
 */
typedef struct
{
    UART_HandleTypeDef *p_x_huart;   /**< HAL handle, e.g. @c &huart2.        */
    IRQn_Type           e_irqn;      /**< Vector serving it; may be shared.   */
}
uart_stream_target_t;

/** @brief Application-provided table of every UART in this build. */
extern const uart_stream_target_t g_x_uart_stream_target[];

/** @brief Number of entries in @ref g_x_uart_stream_target. */
extern const uint8_t g_u8_uart_stream_target_count;

/*==============================================================================
 * Instance state
 *============================================================================*/

/**
 * @brief Per-instance state.
 *
 * @warning Exposed for test, debug and status dumps only. Application code
 *          should use the opaque handle and the public API.
 */
typedef struct uart_stream_instance_s
{
    UART_HandleTypeDef *p_x_huart;         /**< Bound HAL handle.                     */
    queue_t             x_rx_queue;        /**< ISR produces, application consumes.   */
    queue_t             x_tx_queue;        /**< Application produces, ISR consumes.   */
    IRQn_Type           e_irqn;            /**< Vector, from the target table.        */
    volatile uint32_t   u32_error_count;   /**< Cleared-and-counted comms errors.     */
    volatile uint32_t   u32_isr_service_count; /**< Times the ISR serviced this instance. */
    bool                b_active;          /**< Slot is bound.                        */
}
uart_stream_instance_t;

/** @brief Instance pool. Exposed for status dumps; treat as read-only. */
extern uart_stream_instance_t g_x_uart_stream_instances[UART_STREAM_MAX_INSTANCES];

/*==============================================================================
 * Bind / unbind
 *============================================================================*/

/**
 * @brief Bind a HAL-initialised UART for interrupt-driven streaming.
 *
 * Initialises both ring buffers, enables RX/TX interrupt sources, enables the
 * NVIC channel named by the target table, and marks the HAL handle busy so
 * stray @c HAL_UART_* calls fail fast.
 *
 * @param p_x_huart       HAL handle, already through @c HAL_UART_Init().
 * @param u16_rx_buf_size RX ring size in bytes (>= 2).
 * @param p_u8_rx_buf     RX storage, or @c NULL to allocate.
 * @param u16_tx_buf_size TX ring size in bytes (>= 2).
 * @param p_u8_tx_buf     TX storage, or @c NULL to allocate.
 *
 * @return A valid handle, or @ref UART_STREAM_HANDLE_INVALID if the arguments
 *         are bad, the UART is absent from the target table, it is already
 *         bound, no slot is free, or a buffer allocation failed.
 */
uart_stream_h_t x_uart_stream_init(UART_HandleTypeDef *p_x_huart,
                                   uint16_t u16_rx_buf_size, uint8_t *p_u8_rx_buf,
                                   uint16_t u16_tx_buf_size, uint8_t *p_u8_tx_buf);

/**
 * @brief Unbind a UART and hand it back to HAL.
 *
 * Disables this UART's interrupt sources, releases any allocated buffers and
 * restores the HAL handle to @c HAL_UART_STATE_READY.
 *
 * @param h_stream Handle from @ref x_uart_stream_init.
 *
 * @note The NVIC channel is deliberately **not** disabled: on parts with shared
 *       vectors that would also silence HAL's service of the other peripherals
 *       on the same vector.
 */
void v_uart_stream_deinit(uart_stream_h_t h_stream);

/*==============================================================================
 * Interrupt entry point
 *============================================================================*/

/**
 * @brief Service one UART from its vector, or hand it to HAL.
 *
 * Call once per UART sharing the vector, from the upper USER CODE block of the
 * generated handler, then @c return.
 *
 * @param p_x_huart HAL handle of the UART to consider.
 *
 * @retval true   This UART is bound here. It was serviced if it had anything
 *                pending; either way HAL must not see it.
 * @retval false  Not bound. It has already been forwarded to
 *                @c HAL_UART_IRQHandler() internally.
 *
 * @note The return value is diagnostic. Callers normally ignore it, because the
 *       delegation has already happened.
 */
bool b_uart_stream_service_uart(UART_HandleTypeDef *p_x_huart);

/*==============================================================================
 * Transmit
 *============================================================================*/

/**
 * @brief Queue one byte. Never blocks.
 * @param h_stream Bound handle.
 * @param u8_data  Byte to send.
 * @retval true   Queued.
 * @retval false  Ring full, or bad handle; byte dropped.
 */
bool b_uart_stream_tx_byte(uart_stream_h_t h_stream, uint8_t u8_data);

/**
 * @brief Queue one byte, waiting for space up to a deadline.
 * @param h_stream       Bound handle.
 * @param u8_data        Byte to send.
 * @param u32_timeout_ms Maximum wait.
 * @retval true   Queued.
 * @retval false  Timed out or bad handle.
 */
bool b_uart_stream_tx_byte_blocking(uart_stream_h_t h_stream,
                                    uint8_t u8_data,
                                    uint32_t u32_timeout_ms);

/**
 * @brief Queue as much of a block as fits. Never blocks.
 * @param h_stream Bound handle.
 * @param p_u8_src Source bytes.
 * @param u16_len  Bytes offered.
 * @return Bytes queued; may be fewer than @p u16_len, and may be 0.
 */
uint16_t u16_uart_stream_tx_multi(uart_stream_h_t h_stream,
                                  const uint8_t *p_u8_src, uint16_t u16_len);

/**
 * @brief Queue a whole block, waiting for space up to a deadline.
 *
 * Re-arms the transmit interrupt after **every** fill attempt. That is what
 * makes a payload larger than the ring safe: without it, a ring that the ISR
 * had already drained empty leaves TXEIE clear, so nothing can drain the ring
 * the loop is waiting on, and the call never returns.
 *
 * @param h_stream       Bound handle.
 * @param p_u8_src       Source bytes.
 * @param u16_len        Bytes to send.
 * @param u32_timeout_ms Maximum wait for the whole transfer.
 * @return Bytes queued; equals @p u16_len unless the deadline expired.
 */
uint16_t u16_uart_stream_tx_multi_blocking(uart_stream_h_t h_stream,
                                           const uint8_t *p_u8_src,
                                           uint16_t u16_len,
                                           uint32_t u32_timeout_ms);

/**
 * @brief Wait for the ring to drain and the shift register to empty.
 * @param h_stream             Bound handle.
 * @param u32_drain_timeout_ms Outer bound on the software ring draining; size it
 *                             to the expected transmission time.
 * @note Best effort. A flush must never hang a watchdogged main loop, so both
 *       waits are bounded and expiry is silent.
 */
void v_uart_stream_tx_flush_timeout(uart_stream_h_t h_stream,
                                    uint32_t u32_drain_timeout_ms);

/**
 * @brief Flush with the default drain timeout.
 * @param h_stream Bound handle.
 */
void v_uart_stream_tx_flush(uart_stream_h_t h_stream);

/*==============================================================================
 * Receive
 *============================================================================*/

/**
 * @brief Take one received byte. Never blocks.
 * @param h_stream Bound handle.
 * @return Byte value 0..255, or -1 when nothing is buffered.
 */
int16_t i16_uart_stream_rx_byte(uart_stream_h_t h_stream);

/**
 * @brief Take up to @p u16_max_len received bytes. Never blocks.
 * @param h_stream    Bound handle.
 * @param p_u8_dest   Destination buffer.
 * @param u16_max_len Capacity of @p p_u8_dest.
 * @return Bytes copied; may be 0.
 */
uint16_t u16_uart_stream_rx_multi(uart_stream_h_t h_stream,
                                  uint8_t *p_u8_dest, uint16_t u16_max_len);

/*==============================================================================
 * Status
 *============================================================================*/

/** @brief Bytes queued for transmission. @param h_stream Bound handle. @return Byte count. */
uint16_t u16_uart_stream_tx_queue_used(uart_stream_h_t h_stream);

/** @brief Free space in the TX ring. @param h_stream Bound handle. @return Byte count. */
uint16_t u16_uart_stream_tx_queue_free(uart_stream_h_t h_stream);

/** @brief Bytes waiting to be read. @param h_stream Bound handle. @return Byte count. */
uint16_t u16_uart_stream_rx_queue_used(uart_stream_h_t h_stream);

/** @brief Free space in the RX ring. @param h_stream Bound handle. @return Byte count. */
uint16_t u16_uart_stream_rx_queue_free(uart_stream_h_t h_stream);

/**
 * @brief Cumulative communication and overflow errors on this instance.
 *
 * Counts cleared ORE/FE/NE/PE events and bytes dropped because the RX ring was
 * full. A host driving an automated session can read this before and after a
 * run and assert it has not moved.
 *
 * @param h_stream Bound handle.
 * @return Error count since bind.
 */
uint32_t u32_uart_stream_get_error_count(uart_stream_h_t h_stream);

/**
 * @brief Reset the error count to zero.
 *
 * A single aligned store from the main loop; it cannot be lost against the
 * higher-priority UART ISR, though an error counted in the same instant may be
 * cleared with it. Call before a run to baseline it, then read it after.
 *
 * @param h_stream Bound handle.
 */
void v_uart_stream_clear_error_count(uart_stream_h_t h_stream);

/**
 * @brief Times this instance's registers were serviced in interrupt context.
 *
 * Incremented once per @ref b_uart_stream_service_uart call that actually
 * services this instance (owned, with a pending flag). Free-running; it wraps
 * at 2^32 and is never checked, because it is a diagnostic, not a control value.
 *
 * Primary use is a wiring tripwire: after binding a UART, if traffic is flowing
 * but this count never moves, the vector's USER CODE hook in the CubeMX-generated
 * @c *_it.c is missing (HAL is servicing the UART instead of this driver). A
 * bring-up check can read it before and after known traffic to confirm the ISR
 * path really reaches this module.
 *
 * @param h_stream Bound handle.
 * @return Service count since bind.
 */
uint32_t u32_uart_stream_get_isr_service_count(uart_stream_h_t h_stream);

/**
 * @brief Reset the ISR service count to zero.
 *
 * A single aligned store from the main loop; safe against the higher-priority
 * UART ISR as above. Use to re-arm the wiring tripwire: clear it, push known
 * traffic, and confirm it moved.
 *
 * @param h_stream Bound handle.
 */
void v_uart_stream_clear_isr_service_count(uart_stream_h_t h_stream);

/**
 * @brief Test whether transmission is still in progress.
 * @param h_stream Bound handle.
 * @retval true  Bytes are queued, or the shift register has not emptied.
 * @retval false Idle.
 */
bool b_uart_stream_is_tx_busy(uart_stream_h_t h_stream);

/**
 * @brief Look up an instance for test, debug or status dumps.
 * @param h_stream Handle to resolve.
 * @return Instance pointer, or @c NULL if @p h_stream is out of range.
 */
uart_stream_instance_t *p_x_uart_stream_get_instance(uart_stream_h_t h_stream);

/**
 * @brief Read the baud rate the hardware is ACTUALLY running.
 *
 * Derived from @c BRR and the peripheral's kernel clock, not from the HAL's
 * cached @c Init.BaudRate -- that only records what was last requested, and
 * @c BRR is an integer divisor, so the two differ. At 64 MHz a requested
 * 921600 lands on BRR=69, i.e. 927536 (+0.64%).
 *
 * Handles LPUART's 256x fixed-point divisor and USART oversampling-by-8 as
 * well as the ordinary case.
 *
 * @param h_stream Handle to query.
 * @return Achieved baud rate, or 0 if @p h_stream is invalid.
 */
uint32_t u32_uart_stream_get_baud(uart_stream_h_t h_stream);

/**
 * @brief Set the baud rate; returns what the hardware actually achieved.
 *
 * @warning UNGUARDED BY DESIGN. No TX drain, no RX flush. @c BRR must not
 *          change mid-character, so a caller that cares calls
 *          v_uart_stream_tx_flush_timeout() first -- and should discard the RX
 *          ring afterwards, since bytes received at the old rate are framing
 *          garbage once the divisor moves. Doing that here would force a
 *          policy on callers that may want an on-the-fly change.
 *
 * @c UE is dropped across the @c BRR write because the new divisor does not
 * take effect while the peripheral is enabled. @c CR1's interrupt arming is
 * preserved, so the ISR wiring is undisturbed.
 *
 * @param h_stream Handle to retune.
 * @param u32_baud Requested rate.
 * @return Achieved rate (see u32_uart_stream_get_baud), or 0 if the request
 *         is unreachable on this instance/clock or @p h_stream is invalid --
 *         in which case the rate is left untouched.
 */
uint32_t u32_uart_stream_set_baud(uart_stream_h_t h_stream, uint32_t u32_baud);

#endif /* UART_STREAM_H_ */
