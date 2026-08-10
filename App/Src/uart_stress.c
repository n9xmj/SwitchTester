/******************************************************************************
 * uart_stress.c
 *
 * Loopback throughput test -- see uart_stress.h for what it is and is not.
 *
 * The shape that matters is the burst loop: pushing and draining are
 * INTERLEAVED. In loopback every transmitted byte comes straight back, so a
 * test that sent a whole burst before reading would overrun the RX ring on any
 * burst larger than the ring. Interleaving decouples burst size from ring size
 * entirely -- 8 kB flows through 1 kB rings without loss.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include "device_config.h"          /* stdint/string, main.h, platform.h */
#include "uart_stream.h"
#include "usart.h"                  /* huartN handles, for the baud lookup */
#include "uart_stress.h"

/*============================================================================
 * TUNABLES
 *==========================================================================*/

/* Chunk handed to / taken from uart_stream per pass. Small enough that the
 * loop alternates briskly between filling and draining. */
#define STRESS_CHUNK                    64u

/* Per-burst deadline. Generous: 8 kB at 115200 is about 710 ms of wire time
 * on its own, and a stalled burst must still terminate rather than wedge the
 * board while the polling task is not running. */
#define STRESS_BURST_TIMEOUT_MS         3000u

/* Gap between bursts, so each starts from a quiet line rather than running
 * into the tail of the last one. */
#define STRESS_BURST_GAP_MS             5u

/* Loopback pre-check. */
#define STRESS_PROBE_LEN                8u
#define STRESS_PROBE_TIMEOUT_MS         100u

/* Byte at position <n> of the stream. Incrementing, so a value error and a
 * position error look different: a corrupted byte mismatches alone, a dropped
 * byte leaves the received count short. */
#define STRESS_PATTERN(n)               ((uint8_t) (n))

/*============================================================================
 * PRIVATE
 *==========================================================================*/

static uint8_t s_au8_tx_chunk[STRESS_CHUNK];
static uint8_t s_au8_rx_chunk[STRESS_CHUNK];

/*
 * One burst. Fills from the pattern and verifies against it, interleaving so
 * neither ring can overrun regardless of u16_len.
 */
static void v_stress_burst(uart_stream_h_t h_stream, uint16_t u16_len,
                           uint32_t u32_timeout_ms,
                           uart_stress_step_t *p_x_step)
{
    uint32_t u32_tx_done = 0U;
    uint32_t u32_rx_done = 0U;
    uint32_t u32_t0 = SYSTEM_TICK();

    while (u32_rx_done < (uint32_t) u16_len)
    {
        uint16_t u16_want;
        uint16_t u16_got;
        uint16_t u16_i;

        /* --- push --- */
        if (u32_tx_done < (uint32_t) u16_len)
        {
            u16_want = (uint16_t) ((uint32_t) u16_len - u32_tx_done);
            if (u16_want > STRESS_CHUNK)
            {
                u16_want = STRESS_CHUNK;
            }
            for (u16_i = 0U; u16_i < u16_want; u16_i++)
            {
                s_au8_tx_chunk[u16_i] = STRESS_PATTERN(u32_tx_done + u16_i);
            }
            u32_tx_done += u16_uart_stream_tx_multi(h_stream,
                                                    s_au8_tx_chunk, u16_want);
        }

        /* --- drain --- */
        u16_got = u16_uart_stream_rx_multi(h_stream, s_au8_rx_chunk,
                                           (uint16_t) sizeof(s_au8_rx_chunk));
        for (u16_i = 0U; u16_i < u16_got; u16_i++)
        {
            if (s_au8_rx_chunk[u16_i] != STRESS_PATTERN(u32_rx_done))
            {
                p_x_step->u32_mismatch++;
            }
            u32_rx_done++;
        }

        if (ELAPSED_TIME(u32_t0) >= u32_timeout_ms)
        {
            break;      /* short receive is the finding; record and move on */
        }
    }

    p_x_step->u32_sent     += u32_tx_done;
    p_x_step->u32_received += u32_rx_done;
}

/*
 * Confirm the UART works and is wired Tx->Rx before spending time on the
 * progression. A missing loopback jumper is the overwhelmingly likely reason
 * for a run that otherwise reports total loss at every size, and saying so
 * plainly beats eight steps of zeroes.
 */
static uint8_t b_stress_probe(uart_stream_h_t h_stream)
{
    static const uint8_t au8_probe[STRESS_PROBE_LEN] =
        { 0xA5u, 0x5Au, 0x00u, 0xFFu, 0x55u, 0xAAu, 0x0Fu, 0xF0u };
    uint8_t au8_back[STRESS_PROBE_LEN];
    uint16_t u16_have = 0U;
    uint32_t u32_t0;

    if (u16_uart_stream_tx_multi(h_stream, au8_probe, STRESS_PROBE_LEN)
        != STRESS_PROBE_LEN)
    {
        return 0U;
    }

    u32_t0 = SYSTEM_TICK();
    while ((u16_have < STRESS_PROBE_LEN)
           && (ELAPSED_TIME(u32_t0) < STRESS_PROBE_TIMEOUT_MS))
    {
        u16_have += u16_uart_stream_rx_multi(h_stream, &au8_back[u16_have],
                                             (uint16_t) (STRESS_PROBE_LEN - u16_have));
    }

    if (u16_have != STRESS_PROBE_LEN)
    {
        return 0U;
    }
    return (memcmp(au8_probe, au8_back, STRESS_PROBE_LEN) == 0) ? 1U : 0U;
}

/*============================================================================
 * PUBLIC
 *==========================================================================*/

uint32_t u32_uart_stress_baud(uint8_t u8_index)
{
    if (u8_index >= g_u8_uart_stream_target_count)
    {
        return 0U;
    }
    return g_x_uart_stream_target[u8_index].p_x_huart->Init.BaudRate;
}

const char * pc_uart_stress_name(uint8_t u8_index)
{
    static const char *apc_name[] =
        { "U1", "U2", "LPU2", "U3", "U4", "U5", "U6", "LPU1" };

    if ((u8_index >= g_u8_uart_stream_target_count)
        || (u8_index >= (sizeof(apc_name) / sizeof(apc_name[0]))))
    {
        return "?";
    }
    return apc_name[u8_index];
}

uart_stress_result_t x_uart_stress_run(uint8_t u8_index,
                                       uint16_t u16_first_size,
                                       uint16_t u16_last_size,
                                       uint8_t u8_bursts,
                                       uart_stress_step_t *p_x_steps,
                                       uint8_t u8_max_steps,
                                       uint8_t *p_u8_steps_done)
{
    UART_HandleTypeDef *p_x_huart;
    uart_stream_h_t h_stream;
    uint32_t u32_size;
    uint8_t u8_step = 0U;

    *p_u8_steps_done = 0U;

    if (u8_index >= g_u8_uart_stream_target_count)
    {
        return UART_STRESS_BAD_INDEX;
    }

    p_x_huart = g_x_uart_stream_target[u8_index].p_x_huart;

    /* Refuse the console outright. Binding it would tear down the very link
     * the request arrived on, and the caller would never hear the result. */
    if (p_x_huart == &DEBUG_UART_HANDLE)
    {
        return UART_STRESS_IS_CONSOLE;
    }

    /* Already bound means something else owns it -- almost certainly a
     * previous run that did not release, which is worth reporting rather than
     * quietly stealing. */
    if (p_x_huart->gState == HAL_UART_STATE_BUSY)
    {
        return UART_STRESS_IN_USE;
    }

    /* Clamp the progression into sane bounds rather than rejecting: the caller
     * asked for a sweep, and a silly endpoint should not cost them the run. */
    if (u16_first_size < UART_STRESS_SIZE_MIN) { u16_first_size = UART_STRESS_SIZE_MIN; }
    if (u16_last_size  > UART_STRESS_SIZE_MAX) { u16_last_size  = UART_STRESS_SIZE_MAX; }
    if (u16_last_size  < u16_first_size)       { u16_last_size  = u16_first_size; }
    if (u8_bursts == 0U)                       { u8_bursts = 1U; }

    /* NULL storage: uart_stream allocates, and v_uart_stream_deinit() gives it
     * back. Nothing here leaks on any exit path below. */
    h_stream = x_uart_stream_init(p_x_huart,
                                  UART_STRESS_RING_SIZE, NULL,
                                  UART_STRESS_RING_SIZE, NULL);
    if (h_stream == UART_STREAM_HANDLE_INVALID)
    {
        return UART_STRESS_BIND_FAILED;
    }

    if (!b_stress_probe(h_stream))
    {
        v_uart_stream_deinit(h_stream);
        return UART_STRESS_NO_LOOPBACK;
    }

    for (u32_size = u16_first_size;
         (u32_size <= (uint32_t) u16_last_size) && (u8_step < u8_max_steps);
         u32_size *= 2U)
    {
        uart_stress_step_t *p_x_step = &p_x_steps[u8_step];
        uint32_t u32_before = u32_uart_stream_get_error_count(h_stream);
        uint32_t u32_t0 = SYSTEM_TICK();
        uint8_t u8_burst;

        memset(p_x_step, 0, sizeof(*p_x_step));
        p_x_step->u16_size   = (uint16_t) u32_size;
        p_x_step->u16_bursts = u8_bursts;

        for (u8_burst = 0U; u8_burst < u8_bursts; u8_burst++)
        {
            v_stress_burst(h_stream, (uint16_t) u32_size,
                           STRESS_BURST_TIMEOUT_MS, p_x_step);

            /* Spaced, so each burst starts from a quiet line. */
            uint32_t u32_gap = SYSTEM_TICK();
            while (ELAPSED_TIME(u32_gap) < STRESS_BURST_GAP_MS)
            {
                /* deliberately idle -- see the header on not pumping */
            }
        }

        p_x_step->u32_errors     = u32_uart_stream_get_error_count(h_stream)
                                   - u32_before;
        p_x_step->u32_elapsed_ms = ELAPSED_TIME(u32_t0);
        u8_step++;
    }

    *p_u8_steps_done = u8_step;
    v_uart_stream_deinit(h_stream);
    return UART_STRESS_OK;
}

/*==============================================================================
 * BAUD SWEEP
 *============================================================================*/

/*
 * Default ladder. Deliberately not a strict doubling: the steps bunch up above
 * 230400 because that is where the FIFO-less instances on this bench give out,
 * and a knee is only locatable if there are rungs either side of it.
 */
static const uint32_t s_au32_sweep_default[] =
{
     9600U,  19200U,  38400U,  57600U, 115200U,
   230400U, 288000U, 345600U, 403200U, 460800U,
   691200U, 921600U
};

/* Worst-case time to shift a full TX ring out at u32_baud, plus slack. Used as
 * the drain bound so the bottom of the ladder is not truncated: a 1024-byte
 * ring at 9600 takes over a second. */
static uint32_t u32_sweep_drain_ms(uint32_t u32_baud)
{
    if (u32_baud == 0U)
    {
        return 100U;
    }
    return ((UART_STRESS_RING_SIZE * 10U * 1000U) / u32_baud) + 20U;
}

/* Time to shift UART_STRESS_SWEEP_BYTES at u32_baud, doubled for the
 * interleaved read-back, plus slack. */
static uint32_t u32_sweep_burst_ms(uint32_t u32_baud)
{
    if (u32_baud == 0U)
    {
        return STRESS_BURST_TIMEOUT_MS;
    }
    return (((UART_STRESS_SWEEP_BYTES * 10U * 1000U) / u32_baud) * 2U) + 200U;
}

static void v_sweep_rx_discard(uart_stream_h_t h_stream)
{
    /* Bytes received at the previous rate are framing garbage once the divisor
     * moves; left in the ring they would be charged against the next rung. */
    while (u16_uart_stream_rx_multi(h_stream, s_au8_rx_chunk,
                                    (uint16_t) sizeof(s_au8_rx_chunk)) != 0U)
    {
        /* discard */
    }
}

uart_stress_result_t x_uart_stress_sweep(uint8_t u8_index,
                                        const uint32_t *p_u32_rates,
                                        uint8_t u8_rate_count,
                                        uart_stress_rung_t *p_x_rungs,
                                        uint8_t u8_max_rungs,
                                        uint8_t *p_u8_rungs_done)
{
    UART_HandleTypeDef *p_x_huart;
    uart_stream_h_t h_stream;
    uint32_t u32_entry_baud;
    uint8_t u8_rung = 0U;
    uint8_t u8_i;

    *p_u8_rungs_done = 0U;

    if (u8_index >= g_u8_uart_stream_target_count)
    {
        return UART_STRESS_BAD_INDEX;
    }
    p_x_huart = g_x_uart_stream_target[u8_index].p_x_huart;

    if (p_x_huart == &DEBUG_UART_HANDLE)
    {
        return UART_STRESS_IS_CONSOLE;      /* retuning the console cuts the reply off */
    }
    if (p_x_huart->gState == HAL_UART_STATE_BUSY)
    {
        return UART_STRESS_IN_USE;
    }

    if ((p_u32_rates == NULL) || (u8_rate_count == 0U))
    {
        p_u32_rates   = s_au32_sweep_default;
        u8_rate_count = (uint8_t) (sizeof(s_au32_sweep_default)
                                   / sizeof(s_au32_sweep_default[0]));
    }
    if (u8_rate_count > u8_max_rungs)
    {
        u8_rate_count = u8_max_rungs;
    }

    h_stream = x_uart_stream_init(p_x_huart,
                                  UART_STRESS_RING_SIZE, NULL,
                                  UART_STRESS_RING_SIZE, NULL);
    if (h_stream == UART_STREAM_HANDLE_INVALID)
    {
        return UART_STRESS_BIND_FAILED;
    }

    u32_entry_baud = u32_uart_stream_get_baud(h_stream);

    for (u8_i = 0U; u8_i < u8_rate_count; u8_i++)
    {
        uart_stress_rung_t *p_x = &p_x_rungs[u8_rung];
        uart_stress_step_t  x_step;
        uint32_t u32_err_before;

        /* Drain at the CURRENT rate before retuning -- the setter is unguarded
         * and a character still shifting would finish at the wrong divisor. */
        v_uart_stream_tx_flush_timeout(h_stream,
                                       u32_sweep_drain_ms(u32_uart_stream_get_baud(h_stream)));

        p_x->u32_requested = p_u32_rates[u8_i];
        p_x->u32_actual    = u32_uart_stream_set_baud(h_stream, p_u32_rates[u8_i]);
        if (p_x->u32_actual == 0U)
        {
            /* Unreachable on this clock: record it and move on rather than
             * abandoning the rest of the ladder. */
            p_x->u32_sent = 0U;
            p_x->u32_received = 0U;
            p_x->u32_mismatch = 0U;
            p_x->u32_errors = 0U;
            u8_rung++;
            continue;
        }

        v_sweep_rx_discard(h_stream);
        u32_err_before = u32_uart_stream_get_error_count(h_stream);

        x_step.u16_size     = (uint16_t) UART_STRESS_SWEEP_BYTES;
        x_step.u16_bursts   = 1U;
        x_step.u32_sent     = 0U;
        x_step.u32_received = 0U;
        x_step.u32_mismatch = 0U;
        x_step.u32_errors   = 0U;
        x_step.u32_elapsed_ms = 0U;

        /* Derive the burst bound from the rate: 512 bytes at 1200 baud needs
         * 4.3 s to shift, so a fixed 3 s cap would truncate the transfer and
         * report LOSS on a link that is actually perfect -- a lying rung. */
        v_stress_burst(h_stream, (uint16_t) UART_STRESS_SWEEP_BYTES,
                       u32_sweep_burst_ms(p_x->u32_actual), &x_step);

        p_x->u32_sent     = x_step.u32_sent;
        p_x->u32_received = x_step.u32_received;
        p_x->u32_mismatch = x_step.u32_mismatch;
        p_x->u32_errors   = u32_uart_stream_get_error_count(h_stream) - u32_err_before;
        u8_rung++;
    }

    /* Put the instance back the way it was found, on every path. */
    v_uart_stream_tx_flush_timeout(h_stream,
                                   u32_sweep_drain_ms(u32_uart_stream_get_baud(h_stream)));
    (void) u32_uart_stream_set_baud(h_stream, u32_entry_baud);
    v_uart_stream_deinit(h_stream);

    *p_u8_rungs_done = u8_rung;
    return UART_STRESS_OK;
}
