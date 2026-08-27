/******************************************************************************
 * event_queue.c
 *
 * Variable-length event queue -- vendored module, implementation.
 *
 * Synchronization model (see event_queue.h for the caller-facing contract):
 * the ring is managed by monotonic byte/record counters rather than classic
 * wrapping head/tail indices. Each counter has exactly ONE writing side;
 * the other side only reads it. used = written - read is correct across
 * unsigned wrap, and written == read is unambiguously empty -- no reserved
 * byte, no full/empty flag, no power-of-2 size requirement.
 *
 * The producer commits by advancing u32_bytes_written AFTER the record bytes
 * are in place; the consumer releases space by advancing u32_bytes_read AFTER
 * the record bytes are copied out. The compiler barrier between the memcpy
 * and the counter store keeps those in order; the M0+/M4 cores this targets
 * are in-order single-core, so no hardware barrier is required.
 ******************************************************************************/

#include <string.h>
#include "event_queue.h"

#if EVENT_QUEUE_ENABLE_MALLOC
#include <stdlib.h>
#endif

/* Live handle marker: "EVQ1". Cleared by destroy. */
#define EQ_MAGIC            0x45565131u

/* Keep buffer stores and volatile counter stores in program order. */
#define EQ_COMPILER_BARRIER()   __asm volatile("" ::: "memory")

/* Space a record of u16_size payload bytes occupies in the ring. */
#define EQ_RECORD_SPACE(u16_size) \
    ((sizeof(event_queue_header_t) + (uint32_t)(u16_size) + 3u) & ~3u)

/*-----------------------------------------------------------------------------
 * Ring copy helpers. u32_idx is a wrapped index in [0, size); u32_len never
 * exceeds the ring size, so at most one wrap occurs (the two-stage memcpy).
 *---------------------------------------------------------------------------*/

static void v_eq_ring_write(event_queue_handle_t *px_handle, uint32_t u32_idx,
                            const void *pv_src, uint32_t u32_len)
{
    uint32_t u32_first = px_handle->u32_size - u32_idx;

    if (u32_first > u32_len)
    {
        u32_first = u32_len;
    }
    memcpy(&px_handle->pu8_buffer[u32_idx], pv_src, u32_first);
    memcpy(&px_handle->pu8_buffer[0],
           &((const uint8_t *)pv_src)[u32_first], u32_len - u32_first);
}

static void v_eq_ring_read(const event_queue_handle_t *px_handle, uint32_t u32_idx,
                           void *pv_dst, uint32_t u32_len)
{
    uint32_t u32_first = px_handle->u32_size - u32_idx;

    if (u32_first > u32_len)
    {
        u32_first = u32_len;
    }
    memcpy(pv_dst, &px_handle->pu8_buffer[u32_idx], u32_first);
    memcpy(&((uint8_t *)pv_dst)[u32_first],
           &px_handle->pu8_buffer[0], u32_len - u32_first);
}

static uint32_t u32_eq_ring_advance(const event_queue_handle_t *px_handle,
                                    uint32_t u32_idx, uint32_t u32_len)
{
    u32_idx += u32_len;
    if (u32_idx >= px_handle->u32_size)
    {
        u32_idx -= px_handle->u32_size;
    }
    return u32_idx;
}

/*-----------------------------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------------------------*/

event_queue_status_t x_event_queue_create(event_queue_handle_t *px_handle,
                                          const event_queue_config_t *px_config)
{
    static const event_queue_config_t x_defaults = { 0 };
    uint32_t u32_size;
    void    *pv_buffer;

    if (px_handle == NULL)
    {
        return EQ_ERROR_PARAMETER;
    }
    if (px_handle->u32_magic == EQ_MAGIC)
    {
        /* Already live: re-creating would leak an owned ring and yank the
         * buffer out from under any producer. Destroy first. */
        return EQ_ERROR_PARAMETER;
    }
    if (px_config == NULL)
    {
        px_config = &x_defaults;
    }

    /* Lock functions come as a pair or not at all. */
    if ((px_config->pfn_lock == NULL) != (px_config->pfn_unlock == NULL))
    {
        return EQ_ERROR_PARAMETER;
    }

    u32_size = px_config->u32_size;
    if (u32_size == 0u)
    {
        u32_size = EVENT_QUEUE_DEFAULT_SIZE;
    }
    if ((u32_size < EVENT_QUEUE_SIZE_MIN) || ((u32_size % 4u) != 0u))
    {
        return EQ_ERROR_SIZE;
    }

    pv_buffer = px_config->pv_buffer;
    if (pv_buffer != NULL)
    {
        if (((uintptr_t)pv_buffer & 3u) != 0u)
        {
            return EQ_ERROR_ALIGNMENT;
        }
    }
    else
    {
#if EVENT_QUEUE_ENABLE_MALLOC
        pv_buffer = malloc(u32_size);
        if (pv_buffer == NULL)
        {
            return EQ_ERROR_MEMORY;
        }
#else
        return EQ_ERROR_PARAMETER;  /* No buffer and no allocator */
#endif
    }

    memset(px_handle, 0, sizeof(*px_handle));
    px_handle->pu8_buffer     = (uint8_t *)pv_buffer;
    px_handle->u32_size       = u32_size;
    px_handle->pfn_lock       = px_config->pfn_lock;
    px_handle->pfn_unlock     = px_config->pfn_unlock;
    px_handle->u8_owns_buffer = (px_config->pv_buffer == NULL);
    px_handle->u32_magic      = EQ_MAGIC;

    return EQ_OK;
}

event_queue_status_t x_event_queue_destroy(event_queue_handle_t *px_handle)
{
    if (px_handle == NULL)
    {
        return EQ_ERROR_PARAMETER;
    }
    if (px_handle->u32_magic != EQ_MAGIC)
    {
        return EQ_ERROR_NOT_INIT;
    }

#if EVENT_QUEUE_ENABLE_MALLOC
    if (px_handle->u8_owns_buffer)
    {
        free(px_handle->pu8_buffer);
    }
#endif
    memset(px_handle, 0, sizeof(*px_handle));

    return EQ_OK;
}

/*-----------------------------------------------------------------------------
 * Put / get
 *---------------------------------------------------------------------------*/

event_queue_status_t x_event_queue_put(event_queue_handle_t *px_handle,
                                       uint16_t u16_id,
                                       uint16_t u16_data_size,
                                       const void *pv_data)
{
    event_queue_header_t x_header;
    uint32_t             u32_space;
    uint32_t             u32_free;
    uint32_t             u32_idx;

    if (px_handle == NULL)
    {
        return EQ_ERROR_PARAMETER;
    }
    if (px_handle->u32_magic != EQ_MAGIC)
    {
        return EQ_ERROR_NOT_INIT;
    }
    if ((pv_data == NULL) && (u16_data_size != 0u))
    {
        return EQ_ERROR_PARAMETER;
    }

    u32_space = EQ_RECORD_SPACE(u16_data_size);

    if (px_handle->pfn_lock != NULL)
    {
        px_handle->pfn_lock();
    }

    /* A stale read of the consumer's counter only underestimates free space --
     * conservative, never unsafe. */
    u32_free = px_handle->u32_size
             - (px_handle->u32_bytes_written - px_handle->u32_bytes_read);
    if (u32_space > u32_free)
    {
        if (px_handle->pfn_unlock != NULL)
        {
            px_handle->pfn_unlock();
        }
        return EQ_ERROR_FULL;
    }

    x_header.u16_id        = u16_id;
    x_header.u16_data_size = u16_data_size;

    u32_idx = px_handle->u32_wr_idx;
    v_eq_ring_write(px_handle, u32_idx, &x_header, sizeof(x_header));
    if (u16_data_size != 0u)
    {
        u32_idx = u32_eq_ring_advance(px_handle, u32_idx, sizeof(x_header));
        v_eq_ring_write(px_handle, u32_idx, pv_data, u16_data_size);
    }
    px_handle->u32_wr_idx = u32_eq_ring_advance(px_handle,
                                                px_handle->u32_wr_idx, u32_space);

    /* Commit: the record must be fully in place before it becomes visible. */
    EQ_COMPILER_BARRIER();
    px_handle->u32_bytes_written += u32_space;
    px_handle->u32_records_put   += 1u;

    if (px_handle->pfn_unlock != NULL)
    {
        px_handle->pfn_unlock();
    }
    return EQ_OK;
}

event_queue_status_t x_event_queue_get(event_queue_handle_t *px_handle,
                                       event_queue_record_t *px_record)
{
    event_queue_header_t x_header;
    uint32_t             u32_space;
    uint32_t             u32_copy;
    uint32_t             u32_idx;

    if ((px_handle == NULL) || (px_record == NULL))
    {
        return EQ_ERROR_PARAMETER;
    }
    if (px_handle->u32_magic != EQ_MAGIC)
    {
        return EQ_ERROR_NOT_INIT;
    }

    /* One volatile read: anything committed at or before this moment is
     * complete in the buffer (the producer's barrier guarantees it). */
    if (px_handle->u32_bytes_written == px_handle->u32_bytes_read)
    {
        return EQ_STATUS_EMPTY;
    }

    u32_idx = px_handle->u32_rd_idx;
    v_eq_ring_read(px_handle, u32_idx, &x_header, sizeof(x_header));
    u32_space = EQ_RECORD_SPACE(x_header.u16_data_size);

    u32_copy = x_header.u16_data_size;
    if (px_record->pv_data == NULL)
    {
        u32_copy = 0u;
    }
    else if (u32_copy > px_record->u16_buf_size)
    {
        u32_copy = px_record->u16_buf_size;
    }
    if (u32_copy != 0u)
    {
        u32_idx = u32_eq_ring_advance(px_handle, u32_idx, sizeof(x_header));
        v_eq_ring_read(px_handle, u32_idx, px_record->pv_data, u32_copy);
    }

    px_record->u16_id        = x_header.u16_id;
    px_record->u16_data_size = x_header.u16_data_size;

    px_handle->u32_rd_idx = u32_eq_ring_advance(px_handle,
                                                px_handle->u32_rd_idx, u32_space);

    /* Release: the bytes must be fully copied out before the producer may
     * reuse them. */
    EQ_COMPILER_BARRIER();
    px_handle->u32_bytes_read  += u32_space;
    px_handle->u32_records_got += 1u;

    return (u32_copy < x_header.u16_data_size) ? EQ_STATUS_TRUNCATED : EQ_OK;
}

/*-----------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

bool b_event_queue_is_empty(const event_queue_handle_t *px_handle)
{
    if ((px_handle == NULL) || (px_handle->u32_magic != EQ_MAGIC))
    {
        return true;
    }
    return (px_handle->u32_bytes_written == px_handle->u32_bytes_read);
}

uint16_t u16_event_queue_count(const event_queue_handle_t *px_handle)
{
    if ((px_handle == NULL) || (px_handle->u32_magic != EQ_MAGIC))
    {
        return 0u;
    }
    return (uint16_t)(px_handle->u32_records_put - px_handle->u32_records_got);
}

uint32_t u32_event_queue_free_space(const event_queue_handle_t *px_handle)
{
    uint32_t u32_free;

    if ((px_handle == NULL) || (px_handle->u32_magic != EQ_MAGIC))
    {
        return 0u;
    }
    u32_free = px_handle->u32_size
             - (px_handle->u32_bytes_written - px_handle->u32_bytes_read);

    /* Report payload capacity: deduct one record's header. u32_free is always
     * a multiple of 4, so free - header is exactly the largest payload that
     * fits (its rounding re-absorbs the deduction). */
    if (u32_free < sizeof(event_queue_header_t))
    {
        return 0u;
    }
    return u32_free - sizeof(event_queue_header_t);
}
