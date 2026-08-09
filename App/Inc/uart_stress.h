/******************************************************************************
 * uart_stress.h
 *
 * Loopback throughput test for any uart_stream-bindable UART.
 *
 * Binds one UART from g_x_uart_stream_target[] with freshly allocated rings,
 * confirms it is wired Tx<->Rx, then pushes progressively larger bursts through
 * it and reports what came back. The UART is released and its rings returned to
 * the heap when the run ends, however it ends.
 *
 * THIS IS NOT A BACKGROUND-FRIENDLY TEST. It deliberately does not pump the
 * cooperative polling task: it is a raw measurement of what uart_stream and the
 * interrupt path can carry, and normal system behaviour is not expected while
 * it runs. Jobs, the debug menu and switch-pulse timing all stall for its
 * duration. Only run it on purpose.
 *
 * The console UART is refused outright -- testing it would tear down the link
 * carrying the request.
 ******************************************************************************/

#ifndef UART_STRESS_H
#define UART_STRESS_H

#include <stdint.h>

/* Ring sizes for the UART under test. These do NOT need to scale with the
 * burst size: the test interleaves pushing and draining, so a burst far larger
 * than the rings still flows. */
#ifndef UART_STRESS_RING_SIZE
#define UART_STRESS_RING_SIZE           1024u
#endif

/* Bounds on the size progression. Sizes double from first to last. */
#define UART_STRESS_SIZE_MIN            8u
#define UART_STRESS_SIZE_MAX            8192u
#define UART_STRESS_MAX_STEPS           12u

/* Default progression when the caller does not name one. */
/* Baud sweep. UART_STRESS_SWEEP_BYTES is one burst per rung -- enough to show
 * loss without making a 9600 rung take a second. */
#define UART_STRESS_MAX_RUNGS           16u
#define UART_STRESS_SWEEP_BYTES         512u

#define UART_STRESS_DEFAULT_FIRST       64u
#define UART_STRESS_DEFAULT_LAST        8192u
#define UART_STRESS_DEFAULT_BURSTS      4u

typedef enum
{
    UART_STRESS_OK = 0,
    UART_STRESS_BAD_INDEX,          /* no such entry in the target table      */
    UART_STRESS_IS_CONSOLE,         /* refused: that is the link we are on    */
    UART_STRESS_IN_USE,             /* already bound by something else        */
    UART_STRESS_BIND_FAILED,        /* ring allocation or bind failed         */
    UART_STRESS_NO_LOOPBACK         /* pre-check saw nothing, or saw garbage  */
}
uart_stress_result_t;

typedef struct
{
    uint16_t    u16_size;           /* burst size, bytes                      */
    uint16_t    u16_bursts;         /* bursts attempted at this size          */
    uint32_t    u32_sent;           /* bytes accepted by the TX ring          */
    uint32_t    u32_received;       /* bytes read back                        */
    uint32_t    u32_mismatch;       /* bytes that came back with wrong value  */
    uint32_t    u32_errors;         /* uart_stream error-count delta          */
    uint32_t    u32_elapsed_ms;     /* wall time for this step                */
}
uart_stress_step_t;

/*
 * One rung of a baud sweep. Statistics rather than pass/fail on purpose: a
 * marginal rate passes or fails run to run, so a boolean is both unstable and
 * uninformative exactly where the interesting break point is. The host decides
 * what counts as the ceiling from the loss figures.
 */
typedef struct
{
    uint32_t    u32_requested;      /* rate asked for                         */
    uint32_t    u32_actual;         /* rate the BRR divisor actually produced */
    uint32_t    u32_sent;           /* bytes accepted by the TX ring          */
    uint32_t    u32_received;       /* bytes read back                        */
    uint32_t    u32_mismatch;       /* bytes that came back with wrong value  */
    uint32_t    u32_errors;         /* uart_stream error-count delta          */
}
uart_stress_rung_t;

/**
 * @brief Run the loopback progression on one target UART.
 *
 * @param u8_index        Index into g_x_uart_stream_target[].
 * @param u16_first_size  First burst size; clamped to the SIZE_MIN/MAX bounds.
 * @param u16_last_size   Last burst size; sizes double from first to last.
 * @param u8_bursts       Bursts per size, at least 1.
 * @param p_x_steps       Caller's result array.
 * @param u8_max_steps    Capacity of @p p_x_steps.
 * @param p_u8_steps_done Set to the number of steps actually filled in.
 * @return UART_STRESS_OK, or why it stopped. Steps completed before a failure
 *         are still valid and still reported.
 */
extern uart_stress_result_t x_uart_stress_run(uint8_t u8_index,
                                              uint16_t u16_first_size,
                                              uint16_t u16_last_size,
                                              uint8_t u8_bursts,
                                              uart_stress_step_t *p_x_steps,
                                              uint8_t u8_max_steps,
                                              uint8_t *p_u8_steps_done);

/** @brief Configured baud of a target, or 0 if the index is out of range. */
extern uint32_t u32_uart_stress_baud(uint8_t u8_index);

/**
 * @brief Sweep a UART's loopback across a ladder of baud rates.
 *
 * One short burst per rate, recording loss at each. Unlike x_uart_stress_run()
 * this does NOT probe for a loopback first: the slowest rung *is* the wiring
 * test, since no FIFO effect exists down there. Loss at every rung including
 * the slowest means the jumper; loss only above some rate means you have found
 * the ceiling.
 *
 * The instance's original baud rate is restored on every exit path.
 *
 * @param p_u32_rates   Rates to test, or @c NULL to use the built-in ladder.
 * @param u8_rate_count Number of entries in @p p_u32_rates; ignored if NULL.
 * @param p_x_rungs     Caller storage, at least @p u8_max_rungs entries.
 * @param p_u8_rungs_done Rungs actually written.
 * @return UART_STRESS_OK, or the same refusals as x_uart_stress_run().
 */
extern uart_stress_result_t x_uart_stress_sweep(uint8_t u8_index,
                                               const uint32_t *p_u32_rates,
                                               uint8_t u8_rate_count,
                                               uart_stress_rung_t *p_x_rungs,
                                               uint8_t u8_max_rungs,
                                               uint8_t *p_u8_rungs_done);

/** @brief Short name of a target ("U1", "LPU2", ...), or "?" if out of range. */
extern const char * pc_uart_stress_name(uint8_t u8_index);

#endif // UART_STRESS_H
