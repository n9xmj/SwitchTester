#ifndef QUEUE_H_
#define QUEUE_H_

/**
 * @file    queue.h
 * @brief   Interrupt-safe circular byte queues (ring buffers) for
 *          single-producer / single-consumer use.
 *
 * @details
 * Leave-one-slot-empty strategy: @c head == @c tail means empty,
 * <tt>(head + 1) % size == tail</tt> means full. One byte is intentionally
 * unused so that full and empty are distinguishable without a separate count.
 *
 * @par Concurrency model
 * Strictly **single producer, single consumer** per queue. The producer writes
 * @c u16_head and reads @c u16_tail; the consumer writes @c u16_tail and reads
 * @c u16_head. Because the two sides touch disjoint index variables, and each
 * index is a naturally aligned @c volatile @c uint16_t (an atomic single-word
 * access on Cortex-M0+), no lock is required for correctness. A stale read of
 * the opposite index is always *conservative* - the producer may believe there
 * is less free space than there really is, the consumer may believe fewer bytes
 * are pending than there really are. Neither can corrupt the ring.
 *
 * @par Locking policy
 * Two flavours of the single-byte operations are provided:
 * - The plain forms (@ref b_queue_enqueue, @ref i16_queue_dequeue) take a
 *   PRIMASK critical section. Use these from **foreground** code, as cheap
 *   insurance should a second producer or consumer ever appear.
 * - The @c _isr forms (@ref b_queue_enqueue_isr, @ref i16_queue_dequeue_isr)
 *   take no critical section and must be used from **interrupt** context.
 *   Masking PRIMASK inside an ISR is both unnecessary - foreground cannot
 *   preempt an ISR - and harmful, since it would stall higher priority
 *   interrupts once per byte transferred.
 *
 * @warning Violating the one-producer / one-consumer rule, for example
 *          enqueuing to the same queue from two different interrupts, breaks
 *          the lock-free guarantee and will corrupt the ring.
 *
 * @note There are deliberately **no blocking helpers here**. A blocking fill
 *       has to re-arm the transmitter between attempts, which is knowledge this
 *       UART-agnostic layer does not have; that loop belongs to the caller.
 *       See @c v_uart_stream_tx_multi_blocking().
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief Control structure for a circular byte buffer.
 *
 * @note Allocate this in place - typically as a member of the owning driver's
 *       instance struct - and bind it with @ref b_queue_init. Only the data
 *       buffer is ever a candidate for the heap.
 */
typedef struct queue_s
{
    uint8_t          *p_u8_buffer;      /**< Circular data buffer.                        */
    uint16_t          u16_size;         /**< Buffer size in bytes (>= 2).                 */
    volatile uint16_t u16_head;         /**< Next write index; owned by the producer.     */
    volatile uint16_t u16_tail;         /**< Next read index; owned by the consumer.      */
    bool              b_buffer_owned;   /**< Set when @ref b_queue_init allocated buffer. */
}
queue_t;

/*------------------------------------------------------------------------------
 * Critical section helpers (PRIMASK based, nesting safe)
 *----------------------------------------------------------------------------*/

/**
 * @brief  Save PRIMASK and disable interrupts.
 * @return Prior PRIMASK value, to be handed to @ref v_queue_exit_critical.
 */
uint32_t u32_queue_enter_critical(void);

/**
 * @brief Restore PRIMASK saved by @ref u32_queue_enter_critical.
 * @param u32_primask Value previously returned by that function.
 */
void v_queue_exit_critical(uint32_t u32_primask);

/*------------------------------------------------------------------------------
 * Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief Bind an in-place @ref queue_t to a data buffer, allocating on demand.
 *
 * @param p_x_queue    Caller-owned control structure to initialise.
 * @param u16_size     Buffer size in bytes; must be >= 2. Usable capacity is one
 *                     byte less, per the leave-one-slot-empty scheme.
 * @param p_u8_buffer  Caller-supplied storage, or @c NULL to allocate
 *                     @p u16_size bytes. An allocated buffer is released by
 *                     @ref v_queue_release.
 *
 * @retval true   Queue is ready for use.
 * @retval false  Bad argument, or the allocation failed.
 *
 * @note Allocation here is a bind-time, application-lifetime event, not a
 *       repeated alloc/free cycle that would fragment the heap.
 */
bool b_queue_init(queue_t *p_x_queue, uint16_t u16_size, uint8_t *p_u8_buffer);

/**
 * @brief Release any buffer this queue allocated and mark it unusable.
 * @param p_x_queue Queue to release. A caller-supplied buffer is left alone.
 */
void v_queue_release(queue_t *p_x_queue);

/**
 * @brief Discard all contents, keeping the buffer binding.
 * @param p_x_queue Queue to empty.
 */
void v_queue_reset(queue_t *p_x_queue);

/*------------------------------------------------------------------------------
 * Status (atomic snapshots)
 *----------------------------------------------------------------------------*/

/**
 * @brief  Test for empty.
 * @param  p_x_queue Queue to inspect.
 * @return true when no bytes are pending.
 */
bool b_queue_is_empty(const queue_t *p_x_queue);

/**
 * @brief  Test for full.
 * @param  p_x_queue Queue to inspect.
 * @return true when no space remains.
 */
bool b_queue_is_full(const queue_t *p_x_queue);

/**
 * @brief  Bytes currently pending.
 * @param  p_x_queue Queue to inspect.
 * @return Number of bytes available to dequeue.
 */
uint16_t u16_queue_used(const queue_t *p_x_queue);

/**
 * @brief  Free space.
 * @param  p_x_queue Queue to inspect.
 * @return Number of bytes that can still be enqueued.
 */
uint16_t u16_queue_available(const queue_t *p_x_queue);

/*------------------------------------------------------------------------------
 * Single-byte operations - foreground (locked)
 *----------------------------------------------------------------------------*/

/**
 * @brief  Append one byte. Foreground use; takes a critical section.
 * @param  p_x_queue Queue to append to.
 * @param  u8_data   Byte to store.
 * @retval true   Byte stored.
 * @retval false  Queue full; byte discarded.
 */
bool b_queue_enqueue(queue_t *p_x_queue, uint8_t u8_data);

/**
 * @brief  Remove one byte. Foreground use; takes a critical section.
 * @param  p_x_queue Queue to read from.
 * @return Byte value 0..255, or -1 when the queue is empty.
 */
int16_t i16_queue_dequeue(queue_t *p_x_queue);

/*------------------------------------------------------------------------------
 * Single-byte operations - interrupt context (lock free)
 *----------------------------------------------------------------------------*/

/**
 * @brief  Append one byte from interrupt context. Takes no critical section.
 * @param  p_x_queue Queue whose sole producer is this interrupt.
 * @param  u8_data   Byte to store.
 * @retval true   Byte stored.
 * @retval false  Queue full; byte discarded.
 */
bool b_queue_enqueue_isr(queue_t *p_x_queue, uint8_t u8_data);

/**
 * @brief  Remove one byte from interrupt context. Takes no critical section.
 * @param  p_x_queue Queue whose sole consumer is this interrupt.
 * @return Byte value 0..255, or -1 when the queue is empty.
 */
int16_t i16_queue_dequeue_isr(queue_t *p_x_queue);

/*------------------------------------------------------------------------------
 * Multi-byte block operations
 *----------------------------------------------------------------------------*/

/**
 * @brief  Append as much of a block as will fit. Never blocks.
 * @param  p_x_queue Queue to append to.
 * @param  p_u8_src  Source bytes.
 * @param  u16_len   Number of bytes offered.
 * @return Bytes actually stored; may be 0, and may be fewer than @p u16_len.
 */
uint16_t u16_queue_enqueue_multi(queue_t *p_x_queue,
                                 const uint8_t *p_u8_src,
                                 uint16_t u16_len);

/**
 * @brief  Remove up to @p u16_max_len bytes. Never blocks.
 * @param  p_x_queue   Queue to read from.
 * @param  p_u8_dest   Destination buffer.
 * @param  u16_max_len Capacity of @p p_u8_dest.
 * @return Bytes actually copied; may be 0.
 */
uint16_t u16_queue_dequeue_multi(queue_t *p_x_queue,
                                 uint8_t *p_u8_dest,
                                 uint16_t u16_max_len);

#endif /* QUEUE_H_ */
