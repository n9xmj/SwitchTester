/**
 * @file    queue.c
 * @brief   Interrupt-safe circular byte queue implementation.
 *
 * @details
 * See queue.h for the concurrency model and locking policy. In short: strict
 * single-producer / single-consumer, indices are naturally aligned volatile
 * 16-bit words, and the @c _isr variants deliberately omit the PRIMASK
 * critical section because an ISR cannot be preempted by foreground code.
 */

#include "queue.h"

#include <string.h>

/* For the CMSIS core intrinsics only -- __get_PRIMASK, __set_PRIMASK,
 * __disable_irq -- which arrive with the family header this module's config
 * header names. queue.{c,h} are otherwise pure C and know nothing about UARTs;
 * they share uart_stream's config header because they ship inside the module,
 * not because they need anything else from it. */
#include "uart_stream_config.h"

/*==============================================================================
 * Critical section helpers
 *============================================================================*/

uint32_t u32_queue_enter_critical(void)
{
    uint32_t u32_primask = __get_PRIMASK();
    __disable_irq();
    return u32_primask;
}

void v_queue_exit_critical(uint32_t u32_primask)
{
    __set_PRIMASK(u32_primask);
}

/*==============================================================================
 * Private helpers
 *
 * These operate on an index snapshot supplied by the caller, so a caller that
 * already holds a critical section (or is an ISR, and needs none) pays for the
 * index reads exactly once.
 *============================================================================*/

/**
 * @brief  Advance a ring index, wrapping at the buffer size.
 * @param  u16_index Index to advance.
 * @param  u16_size  Ring size in bytes.
 * @return The next index.
 */
static inline uint16_t u16_queue_next(uint16_t u16_index, uint16_t u16_size)
{
    uint16_t u16_next = (uint16_t) (u16_index + 1U);

    if (u16_next >= u16_size)
    {
        u16_next = 0U;
    }
    return u16_next;
}

/**
 * @brief  Bytes pending, from a head/tail snapshot.
 * @param  u16_head Producer index.
 * @param  u16_tail Consumer index.
 * @param  u16_size Ring size in bytes.
 * @return Bytes currently stored.
 */
static inline uint16_t u16_queue_used_from(uint16_t u16_head,
                                           uint16_t u16_tail,
                                           uint16_t u16_size)
{
    if (u16_head >= u16_tail)
    {
        return (uint16_t) (u16_head - u16_tail);
    }
    return (uint16_t) (u16_size - u16_tail + u16_head);
}

/**
 * @brief  Store one byte without locking. Shared by the locked and ISR forms.
 * @param  p_x_queue Queue to append to.
 * @param  u8_data   Byte to store.
 * @retval true   Byte stored.
 * @retval false  Queue full.
 */
static bool b_queue_put(queue_t *p_x_queue, uint8_t u8_data)
{
    uint16_t u16_head = p_x_queue->u16_head;
    uint16_t u16_next = u16_queue_next(u16_head, p_x_queue->u16_size);

    if (u16_next == p_x_queue->u16_tail)
    {
        return false;                       /* full */
    }

    p_x_queue->p_u8_buffer[u16_head] = u8_data;
    p_x_queue->u16_head = u16_next;         /* publish last */
    return true;
}

/**
 * @brief  Fetch one byte without locking. Shared by the locked and ISR forms.
 * @param  p_x_queue Queue to read from.
 * @return Byte value 0..255, or -1 when empty.
 */
static int16_t i16_queue_get(queue_t *p_x_queue)
{
    uint16_t u16_tail = p_x_queue->u16_tail;
    uint8_t  u8_data;

    if (p_x_queue->u16_head == u16_tail)
    {
        return -1;                          /* empty */
    }

    u8_data = p_x_queue->p_u8_buffer[u16_tail];
    p_x_queue->u16_tail = u16_queue_next(u16_tail, p_x_queue->u16_size);
    return (int16_t) u8_data;
}

/*==============================================================================
 * Lifecycle
 *============================================================================*/

bool b_queue_init(queue_t *p_x_queue, uint16_t u16_size, uint8_t *p_u8_buffer)
{
    bool b_own_buffer;

    if ((p_x_queue == NULL) || (u16_size < 2U))
    {
        return false;
    }

    b_own_buffer = (p_u8_buffer == NULL);
    if (b_own_buffer)
    {
        p_u8_buffer = (uint8_t *) malloc(u16_size);
        if (p_u8_buffer == NULL)
        {
            return false;
        }
    }

    p_x_queue->p_u8_buffer    = p_u8_buffer;
    p_x_queue->u16_size       = u16_size;
    p_x_queue->u16_head       = 0U;
    p_x_queue->u16_tail       = 0U;
    p_x_queue->b_buffer_owned = b_own_buffer;
    return true;
}

void v_queue_release(queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return;
    }

    if (p_x_queue->b_buffer_owned && (p_x_queue->p_u8_buffer != NULL))
    {
        free(p_x_queue->p_u8_buffer);
    }

    p_x_queue->p_u8_buffer    = NULL;
    p_x_queue->u16_size       = 0U;
    p_x_queue->u16_head       = 0U;
    p_x_queue->u16_tail       = 0U;
    p_x_queue->b_buffer_owned = false;
}

void v_queue_reset(queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return;
    }

    p_x_queue->u16_head = 0U;
    p_x_queue->u16_tail = 0U;
}

/*==============================================================================
 * Status
 *============================================================================*/

bool b_queue_is_empty(const queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return true;
    }
    return (p_x_queue->u16_head == p_x_queue->u16_tail);
}

bool b_queue_is_full(const queue_t *p_x_queue)
{
    if ((p_x_queue == NULL) || (p_x_queue->u16_size == 0U))
    {
        return true;
    }
    return (u16_queue_next(p_x_queue->u16_head, p_x_queue->u16_size)
            == p_x_queue->u16_tail);
}

uint16_t u16_queue_used(const queue_t *p_x_queue)
{
    if ((p_x_queue == NULL) || (p_x_queue->u16_size == 0U))
    {
        return 0U;
    }
    return u16_queue_used_from(p_x_queue->u16_head,
                               p_x_queue->u16_tail,
                               p_x_queue->u16_size);
}

uint16_t u16_queue_available(const queue_t *p_x_queue)
{
    if ((p_x_queue == NULL) || (p_x_queue->u16_size == 0U))
    {
        return 0U;
    }
    return (uint16_t) ((p_x_queue->u16_size - 1U) - u16_queue_used(p_x_queue));
}

/*==============================================================================
 * Single-byte operations
 *
 * The index reads are individually atomic on M0+, so the critical sections in
 * the foreground forms are belt-and-braces against a second producer or
 * consumer appearing later rather than a correctness requirement today. The ISR
 * forms omit them: an ISR cannot be preempted by foreground code, and masking
 * PRIMASK per byte would stall higher-priority interrupts.
 *============================================================================*/

bool b_queue_enqueue(queue_t *p_x_queue, uint8_t u8_data)
{
    uint32_t u32_state;
    bool     b_stored;

    if (p_x_queue == NULL)
    {
        return false;
    }

    u32_state = u32_queue_enter_critical();
    b_stored  = b_queue_put(p_x_queue, u8_data);
    v_queue_exit_critical(u32_state);
    return b_stored;
}

int16_t i16_queue_dequeue(queue_t *p_x_queue)
{
    uint32_t u32_state;
    int16_t  i16_data;

    if (p_x_queue == NULL)
    {
        return -1;
    }

    u32_state = u32_queue_enter_critical();
    i16_data  = i16_queue_get(p_x_queue);
    v_queue_exit_critical(u32_state);
    return i16_data;
}

bool b_queue_enqueue_isr(queue_t *p_x_queue, uint8_t u8_data)
{
    if (p_x_queue == NULL)
    {
        return false;
    }
    return b_queue_put(p_x_queue, u8_data);
}

int16_t i16_queue_dequeue_isr(queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return -1;
    }
    return i16_queue_get(p_x_queue);
}

/*==============================================================================
 * Multi-byte block operations
 *
 * Indices are snapshotted once, the copy runs outside any lock, then the owned
 * index is published. Safe under SPSC because the opposite side only ever moves
 * its own index in the direction that makes this side's snapshot conservative.
 *============================================================================*/

uint16_t u16_queue_enqueue_multi(queue_t *p_x_queue,
                                 const uint8_t *p_u8_src,
                                 uint16_t u16_len)
{
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_available;
    uint16_t u16_to_write;
    uint16_t u16_part1;
    uint16_t u16_new_head;

    if ((p_x_queue == NULL) || (p_u8_src == NULL) || (u16_len == 0U)
        || (p_x_queue->u16_size == 0U))
    {
        return 0U;
    }

    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;

    u16_available = (uint16_t) ((p_x_queue->u16_size - 1U)
                    - u16_queue_used_from(u16_head, u16_tail, p_x_queue->u16_size));

    u16_to_write = (u16_len > u16_available) ? u16_available : u16_len;
    if (u16_to_write == 0U)
    {
        return 0U;
    }

    u16_part1 = (uint16_t) (p_x_queue->u16_size - u16_head);
    if (u16_part1 > u16_to_write)
    {
        u16_part1 = u16_to_write;
    }

    memcpy(&p_x_queue->p_u8_buffer[u16_head], p_u8_src, u16_part1);

    if (u16_to_write > u16_part1)
    {
        memcpy(p_x_queue->p_u8_buffer,
               p_u8_src + u16_part1,
               (size_t) (u16_to_write - u16_part1));
    }

    u16_new_head = (uint16_t) (u16_head + u16_to_write);
    if (u16_new_head >= p_x_queue->u16_size)
    {
        u16_new_head = (uint16_t) (u16_new_head - p_x_queue->u16_size);
    }

    p_x_queue->u16_head = u16_new_head;      /* publish last */
    return u16_to_write;
}

uint16_t u16_queue_dequeue_multi(queue_t *p_x_queue,
                                 uint8_t *p_u8_dest,
                                 uint16_t u16_max_len)
{
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_used;
    uint16_t u16_to_read;
    uint16_t u16_part1;
    uint16_t u16_new_tail;

    if ((p_x_queue == NULL) || (p_u8_dest == NULL) || (u16_max_len == 0U)
        || (p_x_queue->u16_size == 0U))
    {
        return 0U;
    }

    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;
    u16_used = u16_queue_used_from(u16_head, u16_tail, p_x_queue->u16_size);

    u16_to_read = (u16_used > u16_max_len) ? u16_max_len : u16_used;
    if (u16_to_read == 0U)
    {
        return 0U;
    }

    u16_part1 = (uint16_t) (p_x_queue->u16_size - u16_tail);
    if (u16_part1 > u16_to_read)
    {
        u16_part1 = u16_to_read;
    }

    memcpy(p_u8_dest, &p_x_queue->p_u8_buffer[u16_tail], u16_part1);

    if (u16_to_read > u16_part1)
    {
        memcpy(p_u8_dest + u16_part1,
               p_x_queue->p_u8_buffer,
               (size_t) (u16_to_read - u16_part1));
    }

    u16_new_tail = (uint16_t) (u16_tail + u16_to_read);
    if (u16_new_tail >= p_x_queue->u16_size)
    {
        u16_new_tail = (uint16_t) (u16_new_tail - p_x_queue->u16_size);
    }

    p_x_queue->u16_tail = u16_new_tail;      /* publish last */
    return u16_to_read;
}
