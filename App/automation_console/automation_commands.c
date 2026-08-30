/******************************************************************************
 * automation_commands.c
 *
 * Application-specific command handlers for the automation console, and the
 * g_x_acon_command[] table the core dispatches into. This is the SwitchTester
 * command set -- switch levels, cycling, NVM persist, transport diagnostics.
 *
 * The core (automation_console.c) owns the executive, the framing and the
 * builtins (quit / list / version / no-op). A handler here receives the opcode
 * and the raw line and owns its own parsing: the switch/cycle commands call
 * u8_acon_args() for their comma-separated hex fields. See automation_console.h
 * for the command-author API.
 *
 * Every switch-oriented reply carries the L/M/R state payload, formatted once in
 * v_acon_reply_state() so the host has one thing to parse and document. That
 * payload is what makes these handlers application-specific; a project without
 * switch state writes its own replies with the bare v_acon_ok()/v_acon_emit().
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include "device_config.h"          /* stdint..., ACON_MIN_CYCLE_PERIOD_US */
#include "uart_stream.h"            /* transport error count (E) */
#include "stdio_retarget.h"         /* h_stdio_retarget_get_stream (E) */
#include "switch_out.h"
#include "app_events.h"        /* event mask, record type, queue handle */
#include "nvmparams.h"
#include "globals.h"          /* g_x_nvm_param */
#include "nvm_test.h"         /* v_acon_op_nvm_test -- SwitchTester-only NVM suite */
#include "eventq_test.h"      /* v_acon_op_eventq_test -- SwitchTester-only queue suite */
#include "MX25R80.h"          /* TEMP: SPI flash bring-up probe (Y) -- removal-slated */
#include "uart_stress.h"
#include "automation_console.h"

#if ACON_ENABLED

/*============================================================================
 * APPLICATION ERROR CODES  (generic UNK/ARG/RNG/OVF come from the core header)
 *==========================================================================*/

#define ACON_ERR_BUSY           "BUSY"      /* refused: cycling in progress    */
#define ACON_ERR_NVM            "NVM"       /* flash commit failed             */

/*============================================================================
 * STATE-REPLY HELPERS
 *==========================================================================*/

/* The state payload every switch-oriented command carries, success or failure.
 * One formatter, so the host has one thing to parse and one thing to document. */
static void v_acon_reply_state(acon_sigil_t x_sigil, char c_op, const char *pc_code)
{
    char ac_op[4];

    v_acon_emit(x_sigil, "%s%s%s,L%X,M%X,R%X",
                pc_acon_op_name(c_op, ac_op),
                (pc_code != NULL) ? "," : "",
                (pc_code != NULL) ? pc_code : "",
                u8_switch_out_level_bitmap(),
                u8_switch_out_mode_bitmap(),
                u8_switch_cycle_run_bitmap());
}

static void v_acon_ok_state(char c_op)
{
    v_acon_reply_state(ACON_SIG_OK, c_op, NULL);
}

static void v_acon_err_state(char c_op, const char *pc_code)
{
    v_acon_reply_state(ACON_SIG_ERR, c_op, pc_code);
}

/* Channel index, 0..3. */
static uint8_t b_acon_arg_channel(const char *pc_arg, uint8_t *p_u8_channel)
{
    uint32_t u32_value;

    if (!b_acon_arg_u32(pc_arg, &u32_value) || (u32_value >= SWITCH_OUT_COUNT))
    {
        return 0;
    }
    *p_u8_channel = (uint8_t) u32_value;
    return 1;
}

/*============================================================================
 * COMMAND HANDLERS
 *==========================================================================*/

/*
 * S -- set switch levels. Select / Set / Clear, BSRR-style.
 *
 *   Select 0            channel untouched, keeps cycling if it was
 *   Select 1, S=0 C=0   manual, hold the level it is at right now
 *   Select 1, S=1 C=0   manual, high
 *   Select 1, S=0 C=1   manual, low
 *   Select 1, S=1 C=1   manual, toggle
 *
 * The level snapshot is taken once, before the loop, so a multi-channel toggle
 * or hold is coherent rather than sampling each channel at a different instant.
 */
static void v_acon_op_set(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    uint32_t u32_select, u32_set, u32_clear;
    uint8_t u8_level_now;
    uint8_t u8_channel;

    if (u8_argc < 3u)
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }
    if (!b_acon_arg_u32(ap_c_arg[0], &u32_select)
        || !b_acon_arg_u32(ap_c_arg[1], &u32_set)
        || !b_acon_arg_u32(ap_c_arg[2], &u32_clear))
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }
    if ((u32_select | u32_set | u32_clear) > 0x0FuL)
    {
        v_acon_err_state(c_op, ACON_ERR_RANGE);
        return;
    }

    u8_level_now = u8_switch_out_level_bitmap();

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        uint32_t u32_bit = (1uL << u8_channel);
        uint8_t  u8_want_set   = ((u32_set   & u32_bit) != 0uL);
        uint8_t  u8_want_clear = ((u32_clear & u32_bit) != 0uL);

        if ((u32_select & u32_bit) == 0uL)
        {
            continue;
        }

        if (u8_want_set && u8_want_clear)
        {
            v_switch_out_toggle(u8_channel);
        }
        else if (u8_want_set)
        {
            v_switch_out_set(u8_channel, 1);
        }
        else if (u8_want_clear)
        {
            v_switch_out_set(u8_channel, 0);
        }
        else
        {
            /* Hold: force manual at whatever the pad is doing right now. This
             * is what freezes a cycling channel in place. */
            v_switch_out_set(u8_channel,
                             ((u8_level_now & (uint8_t) u32_bit) != 0u) ? 1 : 0);
        }
    }

    v_acon_ok_state(c_op);
}

/* R -- read switch state. The three bitmaps and nothing else. */
static void v_acon_op_read(char c_op, char *pc_line)
{
    (void) pc_line;
    v_acon_ok_state(c_op);
}

/*
 * W -- write cycling parameters for one channel. Does NOT start anything, so a
 * host can stage several channels and release them together with C.
 *
 * Deliberately does not persist: a script that configures before each of a
 * thousand iterations would otherwise commit a thousand flash writes to a part
 * with no wear levelling, and a run that silently mutates stored configuration
 * is not reproducible from a clean boot. P commits, explicitly.
 */
static void v_acon_op_write(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    uint8_t  u8_channel;
    uint32_t u32_on, u32_off, u32_repeat;

    if (u8_argc < 4u)
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }
    if (!b_acon_arg_channel(ap_c_arg[0], &u8_channel)
        || !b_acon_arg_u32(ap_c_arg[1], &u32_on)
        || !b_acon_arg_u32(ap_c_arg[2], &u32_off)
        || !b_acon_arg_u32(ap_c_arg[3], &u32_repeat))
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }

    /* The engine's own limits first, so a value this accepts is one that
     * v_switch_cycle_start() will not silently refuse later. */
    if ((u32_on  < SWITCH_CYCLE_TIME_MIN_US) || (u32_on  > SWITCH_CYCLE_TIME_MAX_US)
        || (u32_off < SWITCH_CYCLE_TIME_MIN_US) || (u32_off > SWITCH_CYCLE_TIME_MAX_US))
    {
        v_acon_err_state(c_op, ACON_ERR_RANGE);
        return;
    }

    /* Host-commanded cycling only. Summed in 64 bits because two values just
     * under the maximum would wrap a 32-bit add and read as tiny. */
    if (((uint64_t) u32_on + (uint64_t) u32_off) < (uint64_t) ACON_MIN_CYCLE_PERIOD_US)
    {
        v_acon_err_state(c_op, ACON_ERR_RANGE);
        return;
    }

    /* Settings are ISR-read-only, so plain aligned 32-bit stores are atomic on
     * M0+ and take effect at the next phase boundary with no handshake. */
    g_x_switch_cycle[u8_channel].u32_on_time_us   = u32_on;
    g_x_switch_cycle[u8_channel].u32_off_time_us  = u32_off;
    g_x_switch_cycle[u8_channel].u32_repeat_count = u32_repeat;

    v_acon_ok_state(c_op);
}

/*
 * G -- get cycling parameters for one channel, plus run progress.
 *
 * Reports cycles DONE rather than remaining: repeat 0 means "run until
 * stopped", so "remaining" would encode three different situations as zero.
 * The host computes remaining itself when the repeat count is non-zero.
 */
static void v_acon_op_get(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    const switch_cycle_t *p_x_cycle;
    uint8_t u8_channel;
    char ac_op[4];

    if (u8_argc < 1u)
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }
    if (!b_acon_arg_channel(ap_c_arg[0], &u8_channel))
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }

    p_x_cycle = &g_x_switch_cycle[u8_channel];

    /* u32_cycles_done is ISR-written and read here; 32-bit and aligned, so the
     * read is a single LDR on M0+ and cannot tear. */
    v_acon_emit(ACON_SIG_OK, "%s,L%X,M%X,R%X,N%lX,F%lX,C%lX,D%lX",
                pc_acon_op_name(c_op, ac_op),
                u8_switch_out_level_bitmap(),
                u8_switch_out_mode_bitmap(),
                u8_switch_cycle_run_bitmap(),
                (unsigned long) p_x_cycle->u32_on_time_us,
                (unsigned long) p_x_cycle->u32_off_time_us,
                (unsigned long) p_x_cycle->u32_repeat_count,
                (unsigned long) p_x_cycle->u32_cycles_done);
}

/*
 * C -- start cycling on every channel in the mask.
 *
 * A channel already running is restarted from its ON phase rather than ignored:
 * a host that says "start" wants a known phase to measure against, and silently
 * leaving it mid-cycle makes every subsequent timing measurement inherit an
 * unknown offset.
 */
static void v_acon_op_cycle_start(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    uint32_t u32_mask;
    uint8_t u8_channel;

    if ((u8_argc < 1u) || !b_acon_arg_u32(ap_c_arg[0], &u32_mask))
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }
    if (u32_mask > 0x0FuL)
    {
        v_acon_err_state(c_op, ACON_ERR_RANGE);
        return;
    }

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        if ((u32_mask & (1uL << u8_channel)) == 0uL)
        {
            continue;
        }
        if (u8_switch_cycle_running(u8_channel))
        {
            v_switch_cycle_stop(u8_channel);
        }
        v_switch_cycle_start(u8_channel);
    }

    /* v_switch_cycle_start() fails silently on out-of-range stored parameters,
     * so the run bitmap in this response is the host's only way to learn
     * whether the command actually took effect. */
    v_acon_ok_state(c_op);
}

/* X -- stop cycling. Leaves each stopped output forced LOW, which is the
 * engine's existing behaviour; S expresses freeze-at-current-level instead. */
static void v_acon_op_cycle_stop(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    uint32_t u32_mask;
    uint8_t u8_channel;

    if ((u8_argc < 1u) || !b_acon_arg_u32(ap_c_arg[0], &u32_mask))
    {
        v_acon_err_state(c_op, ACON_ERR_ARGS);
        return;
    }
    if (u32_mask > 0x0FuL)
    {
        v_acon_err_state(c_op, ACON_ERR_RANGE);
        return;
    }

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        if ((u32_mask & (1uL << u8_channel)) != 0uL)
        {
            v_switch_cycle_stop(u8_channel);
        }
    }

    v_acon_ok_state(c_op);
}

/*
 * P -- commit parameters to flash now, rather than waiting for the auto-commit
 * timer. Refused while any channel is cycling: a commit erases and rewrites a
 * page, tens of milliseconds against phase times that can be 10 uS, so the
 * deferral is something a host may pre-empt when the bench is idle but never
 * override.
 *
 * Calls x_nvm_commit() synchronously rather than posting JOB_NVM_COMMIT, so the
 * response carries the real outcome instead of "a job was queued".
 */
static void v_acon_op_persist(char c_op, char *pc_line)
{
    nvm_error_t x_status;
    char ac_op[4];

    (void) pc_line;

    if (u8_switch_cycle_any_running())
    {
        /* The run bitmap tells the host WHICH channels to stop, rather than
         * making it issue a separate read to find out. */
        v_acon_emit(ACON_SIG_ERR, "%s,%s,R%X",
                    pc_acon_op_name(c_op, ac_op), ACON_ERR_BUSY,
                    u8_switch_cycle_run_bitmap());
        return;
    }

    x_status = x_nvm_commit(&g_x_nvm_param);

    if (x_status == NVM_ERROR_NONE)
    {
        v_acon_emit(ACON_SIG_OK, "%s,W1", pc_acon_op_name(c_op, ac_op));
    }
    else if (x_status == NVM_ERROR_NO_CHANGE)
    {
        v_acon_emit(ACON_SIG_OK, "%s,W0", pc_acon_op_name(c_op, ac_op));
    }
    else
    {
        v_acon_emit(ACON_SIG_ERR, "%s,%s,E%lX", pc_acon_op_name(c_op, ac_op),
                    ACON_ERR_NVM, (unsigned long) (int32_t) x_status);
    }
}

/*
 * E -- transport error counters. Cumulative ORE/FE/NE/PE plus RX-ring drops
 * since the stream was bound; there is no reset, so a host takes a baseline
 * before a run and asserts the value has not moved after it.
 */
/*============================================================================
 * EVENT PATH -- production mask (A) and synchronous drain (D)
 *
 * Consumption is host-COMMANDED, never unsolicited: nothing is ever emitted
 * that the host did not ask for, so a plain write/read driver needs no frame
 * classification. See Docs/planning/event-path-plan.md (D1).
 *==========================================================================*/

/*
 * A[,mask] -- read or write the event production mask.
 *
 * With no argument this reads. With one it writes, and the reply echoes what
 * actually landed so a host never has to assume the write took. The value is
 * the whole event_control_t as one hex word: the global enable is bit 31, so
 * "0" is the disarm-everything shorthand.
 *
 * Deliberately NOT persisted here -- P is the existing persist-to-NVM op and
 * this stays consistent with the cycling parameters, which are also set live
 * and committed separately.
 */
static void v_acon_op_event_mask(char c_op, char *pc_line)
{
    char    *ap_c_arg[1];
    char     ac_op[4];
    uint8_t  u8_argc = u8_acon_args(pc_line, ap_c_arg, 1);
    uint32_t u32_mask;

    if ((u8_argc >= 1u) && (ap_c_arg[0][0] != '\0'))
    {
        if (!b_acon_arg_u32(ap_c_arg[0], &u32_mask))
        {
            v_acon_err(c_op, ACON_ERR_ARGS);
            return;
        }

        g_x_event_control.u32_all = u32_mask;
    }

    v_acon_emit(ACON_SIG_OK, "%s,M%X",
                pc_acon_op_name(c_op, ac_op),
                (unsigned) g_x_event_control.u32_all);
}

/*
 * D[,max] -- synchronous event drain.
 *
 * max 0 (or absent) drains until the queue reads empty; 1+ drains up to that
 * many. Never blocks for longer than emptying the queue takes: asking for 4
 * when 2 are queued emits 2 and returns.
 *
 * Uses the console's existing multi-line idiom rather than inventing one: a
 * "=D,K<n>,N<rem>,D<drops>" header followed by exactly n "+" payload lines, one
 * per event. The host reads ONE frame, already parsed, instead of looping until
 * a terminator.
 *
 * N is the number still queued FROM THE SNAPSHOT the header was built on. It is
 * what lets a host that asked for 4 and got 4 know to come back -- the loop is
 * "while N: drain(N)". Note a producer racing the drain can add more, so N == 0
 * means "nothing left of what I saw", not "the queue is provably empty"; H,S
 * answers that if it matters.
 *
 * A drain with nothing queued is just "=D,K0,N0,D<drops>" with no payload --
 * not a special case a host has to handle separately.
 */
static void v_acon_op_event_drain(char c_op, char *pc_line)
{
    char    *ap_c_arg[1];
    char     ac_op[4];
    uint8_t  u8_argc   = u8_acon_args(pc_line, ap_c_arg, 1);
    uint32_t u32_max   = 0UL;
    uint32_t u32_count;
    uint32_t u32_avail;
    uint32_t u32_i;

    if ((u8_argc >= 1u) && (ap_c_arg[0][0] != '\0'))
    {
        if (!b_acon_arg_u32(ap_c_arg[0], &u32_max))
        {
            v_acon_err(c_op, ACON_ERR_ARGS);
            return;
        }
    }

    /* Snapshot the depth, then emit exactly that many. Taking the count first
     * is what lets the K header be exact: a producer racing this drain can only
     * ADD, never remove, so the snapshot can never overpromise. */
    u32_avail = (uint32_t) u16_event_queue_count(&g_x_event_queue);
    u32_count = ((u32_max == 0UL) || (u32_max > u32_avail)) ? u32_avail : u32_max;

    v_acon_emit(ACON_SIG_OK, "%s,K%X,N%X,D%X",
                pc_acon_op_name(c_op, ac_op),
                (unsigned) u32_count,
                (unsigned) (u32_avail - u32_count),
                (unsigned) u32_event_queue_dropped(&g_x_event_queue));

    for (u32_i = 0UL; u32_i < u32_count; u32_i++)
    {
        switch_event_data_t  x_data;
        event_queue_record_t x_record =
        {
            .u16_buf_size = (uint16_t) sizeof(x_data),
            .pv_data      = &x_data
        };
        event_queue_status_t x_status =
            x_event_queue_get(&g_x_event_queue, &x_record);

        /* The header has already promised K lines, so ALWAYS emit K of them --
         * a short block would leave the host waiting on payload that is never
         * coming. A get that fails here (it should not: the count was snapped
         * above and only this context consumes) emits a sentinel I0 line, which
         * is a diagnosable value rather than a hung transaction. TRUNCATED is
         * not a failure: the record came out, only an oversized payload was
         * clipped, and ours are all one size. */
        if ((x_status != EQ_OK) && (x_status != EQ_STATUS_TRUNCATED))
        {
            v_acon_emit(ACON_SIG_PAYLOAD, "I%X,C%X,S%X,T%X,M%X",
                        (unsigned) EVENT_CLASS_NONE, 0u, 0u, 0u, 0u);
            continue;
        }

        v_acon_emit(ACON_SIG_PAYLOAD, "I%X,C%X,S%X,T%X,M%X",
                    (unsigned) x_record.u16_id,
                    (unsigned) x_data.u8_channel,
                    (unsigned) x_data.u16_state,
                    (unsigned) x_data.u32_tim_count,
                    (unsigned) x_data.u32_tick);
    }
}

/*
 * H[,sub] -- event queue housekeeping. Flush and counter resets, kept off the
 * drain path so D stays purely a read.
 *
 *   H       or H,S : status -- queued, drops, puts
 *   H,F           : flush the queue
 *   H,R           : reset the drop and put counters
 */
static void v_acon_op_event_house(char c_op, char *pc_line)
{
    char   *ap_c_arg[1];
    char    ac_op[4];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, 1);
    char    c_sub   = ((u8_argc >= 1u) && (ap_c_arg[0][0] != '\0'))
                      ? ap_c_arg[0][0] : 'S';

    switch (c_sub)
    {
        case 'F':
            (void) x_event_queue_flush(&g_x_event_queue);
            break;

        case 'R':
            v_event_queue_dropped_reset(&g_x_event_queue);
            v_event_queue_puts_reset(&g_x_event_queue);
            break;

        case 'S':
            break;

        default:
            v_acon_err(c_op, ACON_ERR_ARGS);
            return;
    }

    v_acon_emit(ACON_SIG_OK, "%s,N%X,D%X,P%X",
                pc_acon_op_name(c_op, ac_op),
                (unsigned) u16_event_queue_count(&g_x_event_queue),
                (unsigned) u32_event_queue_dropped(&g_x_event_queue),
                (unsigned) u32_event_queue_puts(&g_x_event_queue));
}

static void v_acon_op_errors(char c_op, char *pc_line)
{
    uart_stream_h_t h_stream = h_stdio_retarget_get_stream();
    char ac_op[4];

    (void) pc_line;

    v_acon_emit(ACON_SIG_OK, "%s,E%lX", pc_acon_op_name(c_op, ac_op),
                (unsigned long) u32_uart_stream_get_error_count(h_stream));
}

/*
 * U -- loopback stress test on one uart_stream-bindable UART.
 *
 *   U,<index>[,<first_size>[,<last_size>[,<bursts>]]]      all sizes hex
 *
 * Multi-line: the header declares K<n> and one payload line per size step
 * follows, so a host reads a known count rather than guessing where the run
 * ended. Steps completed before a mid-run failure are still reported.
 *
 * This blocks for the whole run -- seconds, at the lower baud rates -- and the
 * polling task is deliberately not pumped meanwhile. A host must allow for that;
 * the ordinary command timeout will not be enough.
 */
/*
 * B - baud sweep. B,<idx>[,<rate>...]
 *
 * With no rates, walks the module's built-in ladder; with rates, walks exactly
 * those, in the order given -- so a host can bisect a knee without a reflash.
 * Reports loss per rung rather than pass/fail: a marginal rate is not a
 * boolean, and the ceiling is the host's call from the figures.
 */
static void v_acon_op_baud_sweep(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    static uart_stress_rung_t ax_rung[UART_STRESS_MAX_RUNGS];
    static uint32_t au32_rates[UART_STRESS_MAX_RUNGS];
    uart_stress_result_t x_result;
    uint32_t u32_index;
    uint8_t u8_rates = 0U;
    uint8_t u8_rungs = 0U;
    uint8_t u8_i;
    char ac_op[4];

    if ((u8_argc < 1u) || !b_acon_arg_u32(ap_c_arg[0], &u32_index))
    {
        v_acon_err(c_op, ACON_ERR_ARGS);
        return;
    }
    if (u32_index > 0xFFuL)
    {
        v_acon_err(c_op, ACON_ERR_RANGE);
        return;
    }

    for (u8_i = 1U; (u8_i < u8_argc) && (u8_rates < UART_STRESS_MAX_RUNGS); u8_i++)
    {
        if (!b_acon_arg_u32(ap_c_arg[u8_i], &au32_rates[u8_rates]))
        {
            v_acon_err(c_op, ACON_ERR_ARGS);
            return;
        }
        u8_rates++;
    }

    x_result = x_uart_stress_sweep((uint8_t) u32_index,
                                   (u8_rates != 0U) ? au32_rates : NULL,
                                   u8_rates,
                                   ax_rung, (uint8_t) UART_STRESS_MAX_RUNGS,
                                   &u8_rungs);

    if (x_result != UART_STRESS_OK)
    {
        static const char *apc_why[] =
            { "OK", "ARG", "CONS", "BUSY", "MEM", "LOOP" };
        v_acon_emit(ACON_SIG_ERR, "%s,%s,I%lX",
                    pc_acon_op_name(c_op, ac_op),
                    apc_why[(unsigned) x_result],
                    (unsigned long) u32_index);
        return;
    }

    v_acon_emit(ACON_SIG_OK, "%s,K%X,I%lX,Z%X",
                pc_acon_op_name(c_op, ac_op),
                (unsigned) u8_rungs,
                (unsigned long) u32_index,
                (unsigned) UART_STRESS_SWEEP_BYTES);

    for (u8_i = 0U; u8_i < u8_rungs; u8_i++)
    {
        const uart_stress_rung_t *p_x = &ax_rung[u8_i];

        v_acon_emit(ACON_SIG_PAYLOAD, "D%lX,A%lX,T%lX,R%lX,X%lX,E%lX",
                    (unsigned long) p_x->u32_requested,
                    (unsigned long) p_x->u32_actual,
                    (unsigned long) p_x->u32_sent,
                    (unsigned long) p_x->u32_received,
                    (unsigned long) p_x->u32_mismatch,
                    (unsigned long) p_x->u32_errors);
    }
}

static void v_acon_op_uart_stress(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    static uart_stress_step_t ax_step[UART_STRESS_MAX_STEPS];
    uart_stress_result_t x_result;
    uint32_t u32_index, u32_first, u32_last, u32_bursts;
    uint8_t u8_steps = 0U;
    uint8_t u8_i;
    char ac_op[4];

    if ((u8_argc < 1u) || !b_acon_arg_u32(ap_c_arg[0], &u32_index))
    {
        v_acon_err(c_op, ACON_ERR_ARGS);
        return;
    }

    u32_first  = UART_STRESS_DEFAULT_FIRST;
    u32_last   = UART_STRESS_DEFAULT_LAST;
    u32_bursts = UART_STRESS_DEFAULT_BURSTS;

    if ((u8_argc >= 2u) && !b_acon_arg_u32(ap_c_arg[1], &u32_first))
    {
        v_acon_err(c_op, ACON_ERR_ARGS);
        return;
    }
    if ((u8_argc >= 3u) && !b_acon_arg_u32(ap_c_arg[2], &u32_last))
    {
        v_acon_err(c_op, ACON_ERR_ARGS);
        return;
    }
    if ((u8_argc >= 4u) && !b_acon_arg_u32(ap_c_arg[3], &u32_bursts))
    {
        v_acon_err(c_op, ACON_ERR_ARGS);
        return;
    }

    if ((u32_index > 0xFFuL) || (u32_first > 0xFFFFuL)
        || (u32_last > 0xFFFFuL) || (u32_bursts > 0xFFuL))
    {
        v_acon_err(c_op, ACON_ERR_RANGE);
        return;
    }

    x_result = x_uart_stress_run((uint8_t) u32_index,
                                 (uint16_t) u32_first, (uint16_t) u32_last,
                                 (uint8_t) u32_bursts,
                                 ax_step, (uint8_t) UART_STRESS_MAX_STEPS,
                                 &u8_steps);

    if (x_result != UART_STRESS_OK)
    {
        static const char *apc_why[] =
            { "OK", "ARG", "CONS", "BUSY", "MEM", "LOOP" };
        v_acon_emit(ACON_SIG_ERR, "%s,%s,I%lX",
                    pc_acon_op_name(c_op, ac_op),
                    apc_why[(unsigned) x_result],
                    (unsigned long) u32_index);
        return;
    }

    /* Header carries what the steps are relative to: which UART, at what baud.
     * Without the baud a byte count is uninterpretable. */
    v_acon_emit(ACON_SIG_OK, "%s,K%X,I%lX,B%lX",
                pc_acon_op_name(c_op, ac_op),
                (unsigned) u8_steps,
                (unsigned long) u32_index,
                (unsigned long) u32_uart_stress_baud((uint8_t) u32_index));

    for (u8_i = 0U; u8_i < u8_steps; u8_i++)
    {
        const uart_stress_step_t *p_x = &ax_step[u8_i];

        v_acon_emit(ACON_SIG_PAYLOAD, "S%X,N%X,T%lX,R%lX,X%lX,E%lX,M%lX",
                    (unsigned) p_x->u16_size,
                    (unsigned) p_x->u16_bursts,
                    (unsigned long) p_x->u32_sent,
                    (unsigned long) p_x->u32_received,
                    (unsigned long) p_x->u32_mismatch,
                    (unsigned long) p_x->u32_errors,
                    (unsigned long) p_x->u32_elapsed_ms);
    }
}

/*============================================================================
 * EXAMPLE COMMANDS  (how-to templates; also the whole of Skeleton's command set)
 *
 * Two idioms, side by side, both using the bare command-author API (no L/M/R
 * state payload -- that is what a project without switch state looks like):
 *   @  splits comma fields with u8_acon_args() and echoes them   (parsed args)
 *   $  reads the line directly and echoes it verbatim            (raw text)
 *==========================================================================*/

/*
 * @ -- echo the comma-separated fields back as CSV. The PARSED-ARGUMENT idiom:
 * split the line with u8_acon_args() and act on the fields. Almost every real
 * command is shaped like this; here the "action" is merely to echo them.
 *
 *   @,12,ab,text  ->  =@,12,ab,text        (@ with no args -> =@)
 */
static void v_acon_op_echo_args(char c_op, char *pc_line)
{
    static char s_ac_csv[ACON_LINE_MAX];
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    char ac_op[4];
    uint16_t u16_len = 0;
    uint8_t u8_i;

    if (u8_argc == 0u)
    {
        v_acon_ok(c_op);                    /* "=@" -- nothing to echo */
        return;
    }

    s_ac_csv[0] = '\0';
    for (u8_i = 0u; u8_i < u8_argc; u8_i++)
    {
        int i_n = snprintf(&s_ac_csv[u16_len], sizeof(s_ac_csv) - u16_len,
                           "%s%s", (u8_i > 0u) ? "," : "", ap_c_arg[u8_i]);
        if ((i_n < 0) || ((uint16_t) i_n >= (uint16_t) (sizeof(s_ac_csv) - u16_len)))
        {
            break;                          /* buffer full: emit what fits */
        }
        u16_len += (uint16_t) i_n;
    }

    v_acon_emit(ACON_SIG_OK, "%s,%s", pc_acon_op_name(c_op, ac_op), s_ac_csv);
}

/*
 * $ -- echo the whole line back verbatim, commas and all. The RAW-LINE idiom: a
 * command that does NOT want the comma splitter reads the line directly. Passing
 * the line as the argument to "%s" (never as the format) also means a '%' in the
 * data is data, not a conversion.
 *
 *   $hello,world  ->  =$hello,world
 *
 * The text is whatever survived the reader, which strips CR/LF but not other
 * control bytes; sanitise here if a downstream consumer needs printable-only.
 */
static void v_acon_op_echo_raw(char c_op, char *pc_line)
{
    (void) c_op;
    v_acon_emit(ACON_SIG_OK, "%s", pc_line);
}

/*
 * Y -- SPI flash wiring / presence probe. TEMPORARY, MX25R80 bring-up only;
 * removed when the vendorable spiflash module lands.
 *
 *   Y[,I]   read JEDEC ID    -> =Y,<mfg>,<type>,<density>,<verdict>
 *   Y,S     read status reg  -> =Y,S,<sr>
 *
 * The ID probe is pure blocking SPI (no DMA), so it is the right first check
 * that CS / SCK / MOSI / MISO are wired and the part answers. Verdict:
 *   OK     Macronix (0xC2) answered (expected MX25R80xx)
 *   OTHER  something answered, but not the expected manufacturer
 *   NONE   all 0x00 / all 0xFF -> MISO stuck: no part / no power / bad wiring
 * A failed SPI transaction returns "!Y,FAIL,<hal>" instead.
 */
static void v_acon_op_spiflash(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    char ac_op[4];
    char c_sub = (u8_argc >= 1u) ? ap_c_arg[0][0] : 'I';

    /* Y,L -- raw full-duplex loopback, bypassing the driver entirely. With MOSI
     * (PC12) shorted to MISO (PB4) and the Click disconnected, every byte clocked
     * out must come back in. PASS proves MOSI/MISO/SCK are real and correctly
     * mapped on the Nucleo; CS and the Click are out of the path. Reports
     * PASS/FAIL plus the tx and rx byte strings so a mismatch is visible. */
    if (c_sub == 'L')
    {
        static const uint8_t au8_tx[8] =
            { 0x9Fu, 0xA5u, 0x5Au, 0x00u, 0xFFu, 0x12u, 0x34u, 0xC2u };
        uint8_t au8_rx[8] = { 0 };
        char ac_tx[20];
        char ac_rx[20];
        uint8_t u8_hal;
        uint8_t u8_i;
        uint8_t u8_ok = 1u;

        u8_hal = (uint8_t) HAL_SPI_TransmitReceive(&hspi3, (uint8_t *) au8_tx,
                            au8_rx, (uint16_t) sizeof(au8_tx), SPIFLASH_TIMEOUT);
        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,L,FAIL,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }

        ac_tx[0] = '\0';
        ac_rx[0] = '\0';
        for (u8_i = 0u; u8_i < (uint8_t) sizeof(au8_tx); u8_i++)
        {
            char ac_b[4];
            (void) snprintf(ac_b, sizeof(ac_b), "%02X", (unsigned) au8_tx[u8_i]);
            (void) strcat(ac_tx, ac_b);
            (void) snprintf(ac_b, sizeof(ac_b), "%02X", (unsigned) au8_rx[u8_i]);
            (void) strcat(ac_rx, ac_b);
            if (au8_rx[u8_i] != au8_tx[u8_i]) { u8_ok = 0u; }
        }

        v_acon_emit(ACON_SIG_OK, "%s,L,%s,%s,%s", pc_acon_op_name(c_op, ac_op),
                    u8_ok ? "PASS" : "FAIL", ac_tx, ac_rx);
        return;
    }

    /* Y,N -- NCS loopback. CS (PA15) is jumpered to TEST_INPUT (PC3, input +
     * pull-up). Drive CS low then high, sense PC3 each time: it must track. PASS
     * proves PA15 actually drives and the jumper is good; reports the two sensed
     * levels as L<lo>,H<hi> so a stuck/broken line is visible. */
    if (c_sub == 'N')
    {
        GPIO_PinState x_lo;
        GPIO_PinState x_hi;
        uint8_t u8_ok;

        HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin, GPIO_PIN_RESET);
        HAL_Delay(1u);
        x_lo = HAL_GPIO_ReadPin(TEST_INPUT_GPIO_Port, TEST_INPUT_Pin);

        HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin, GPIO_PIN_SET);
        HAL_Delay(1u);
        x_hi = HAL_GPIO_ReadPin(TEST_INPUT_GPIO_Port, TEST_INPUT_Pin);

        u8_ok = ((x_lo == GPIO_PIN_RESET) && (x_hi == GPIO_PIN_SET)) ? 1u : 0u;

        v_acon_emit(ACON_SIG_OK, "%s,N,%s,L%u,H%u", pc_acon_op_name(c_op, ac_op),
                    u8_ok ? "PASS" : "FAIL",
                    (unsigned) x_lo, (unsigned) x_hi);
        return;
    }

    /* Y,J -- SCK loopback. Momentarily reconfigures SCK (PB3) from its SPI3
     * alternate function to a GPIO push-pull output, drives it low then high,
     * senses PC3, then restores AF9. Jumper the PC3 sense lead onto the SCK net
     * (its device-plug end, mirroring Y,N) first. PASS proves the SCK lead is on
     * the right Nucleo pin and continuous to the device -- the one lead never
     * yet verified. */
    if (c_sub == 'J')
    {
        GPIO_InitTypeDef x_gpio = {0};
        GPIO_PinState x_lo;
        GPIO_PinState x_hi;
        uint8_t u8_ok;

        x_gpio.Pin   = SPIFLASH_SCK_Pin;
        x_gpio.Mode  = GPIO_MODE_OUTPUT_PP;
        x_gpio.Pull  = GPIO_NOPULL;
        x_gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(SPIFLASH_SCK_GPIO_Port, &x_gpio);

        HAL_GPIO_WritePin(SPIFLASH_SCK_GPIO_Port, SPIFLASH_SCK_Pin, GPIO_PIN_RESET);
        HAL_Delay(1u);
        x_lo = HAL_GPIO_ReadPin(TEST_INPUT_GPIO_Port, TEST_INPUT_Pin);

        HAL_GPIO_WritePin(SPIFLASH_SCK_GPIO_Port, SPIFLASH_SCK_Pin, GPIO_PIN_SET);
        HAL_Delay(1u);
        x_hi = HAL_GPIO_ReadPin(TEST_INPUT_GPIO_Port, TEST_INPUT_Pin);

        /* Restore PB3 -> SPI3 SCK (AF9). */
        x_gpio.Pin       = SPIFLASH_SCK_Pin;
        x_gpio.Mode      = GPIO_MODE_AF_PP;
        x_gpio.Pull      = GPIO_NOPULL;
        x_gpio.Speed     = GPIO_SPEED_FREQ_LOW;
        x_gpio.Alternate = GPIO_AF9_SPI3;
        HAL_GPIO_Init(SPIFLASH_SCK_GPIO_Port, &x_gpio);

        u8_ok = ((x_lo == GPIO_PIN_RESET) && (x_hi == GPIO_PIN_SET)) ? 1u : 0u;
        v_acon_emit(ACON_SIG_OK, "%s,J,%s,L%u,H%u", pc_acon_op_name(c_op, ac_op),
                    u8_ok ? "PASS" : "FAIL",
                    (unsigned) x_lo, (unsigned) x_hi);
        return;
    }

    /* Y,C[,<0|1>] -- park the CS pin (PA15) static so it can be metered end to
     * end to the Click CS. Default 1 (idle/high). CS stays parked until the next
     * Y,I re-drives it. */
    if (c_sub == 'C')
    {
        uint32_t u32_lvl = 1u;
        if (u8_argc >= 2u) { (void) b_acon_arg_u32(ap_c_arg[1], &u32_lvl); }
        HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin,
                          (u32_lvl != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        v_acon_emit(ACON_SIG_OK, "%s,C,%u", pc_acon_op_name(c_op, ac_op),
                    (unsigned) ((u32_lvl != 0u) ? 1u : 0u));
        return;
    }

    /* Y,K -- clock-activity burst: hold CS low and stream 0xA5 for ~2 s so SCK
     * and MOSI carry traffic for a scope/DMM. Blocks for the duration; the host
     * must allow a >2 s read timeout. Reports the burst count in hex. */
    if (c_sub == 'K')
    {
        static const uint8_t au8_pat[32] = {
            0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,
            0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,
            0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,
            0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u,0xA5u };
        uint32_t u32_t0 = HAL_GetTick();
        uint32_t u32_bursts = 0u;

        SPI_FLASH_SELECT();
        while ((HAL_GetTick() - u32_t0) < 2000u)
        {
            (void) HAL_SPI_Transmit(&hspi3, (uint8_t *) au8_pat,
                                    (uint16_t) sizeof(au8_pat), SPIFLASH_TIMEOUT);
            u32_bursts++;
        }
        SPI_FLASH_DESELECT();

        v_acon_emit(ACON_SIG_OK, "%s,K,%lX", pc_acon_op_name(c_op, ac_op),
                    (unsigned long) u32_bursts);
        return;
    }

    /* Y,T -- DMA round-trip smoke test (DESTRUCTIVE to sector 0). Erase 4 KB,
     * blank-check via RX DMA, write a 256-byte incrementing pattern via TX DMA,
     * read back via RX DMA, verify. Exercises erase + both DMA directions -- the
     * exact ops the nvmparams glue will call. BL/VF report FFFFFFFF on success or
     * the first failing byte index. */
    if (c_sub == 'T')
    {
        static uint8_t au8_w[256];
        static uint8_t au8_r[256];
        const uint32_t u32_addr = 0x000000u;
        spiflash_id_t x_id;
        uint16_t u16_i;
        uint8_t  u8_hal;
        int32_t  i32_blank = -1;    /* first non-0xFF after erase; -1 = clean   */
        int32_t  i32_diff  = -1;    /* first readback mismatch;   -1 = matches  */

        (void) u8_spiflash_read_id(&x_id);

        /* 1) erase sector 0, then wait generously for WIP to clear (the driver's
         * own 100 ms wait can be short of a worst-case sector-erase time). */
        u8_hal = u8_spiflash_sector_erase(u32_addr);
        (void) u8_spiflash_write_wait(1000u);
        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,T,FAIL,ERASE,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }

        /* 2) blank-check via RX DMA -- expect all 0xFF */
        u8_hal = u8_spiflash_read(au8_r, u32_addr, 256u);
        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,T,FAIL,READ1,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }
        for (u16_i = 0u; u16_i < 256u; u16_i++)
        {
            if (au8_r[u16_i] != 0xFFu) { i32_blank = (int32_t) u16_i; break; }
        }

        /* 3) fill + program one page via TX DMA */
        for (u16_i = 0u; u16_i < 256u; u16_i++) { au8_w[u16_i] = (uint8_t) u16_i; }
        u8_hal = u8_spiflash_write_page(au8_w, u32_addr, 256u);
        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,T,FAIL,WRITE,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }

        /* 4) read back via RX DMA + verify */
        memset(au8_r, 0, sizeof(au8_r));
        u8_hal = u8_spiflash_read(au8_r, u32_addr, 256u);
        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,T,FAIL,READ2,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }
        for (u16_i = 0u; u16_i < 256u; u16_i++)
        {
            if (au8_r[u16_i] != au8_w[u16_i]) { i32_diff = (int32_t) u16_i; break; }
        }

        {
            uint8_t u8_ok = ((i32_blank < 0) && (i32_diff < 0)) ? 1u : 0u;
            v_acon_emit(ACON_SIG_OK, "%s,T,%s,ID%02X,BL%lX,VF%lX",
                        pc_acon_op_name(c_op, ac_op),
                        u8_ok ? "PASS" : "FAIL",
                        (unsigned) x_id.u8_manufacturer_id,
                        (unsigned long) ((i32_blank < 0) ? 0xFFFFFFFFuL : (uint32_t) i32_blank),
                        (unsigned long) ((i32_diff  < 0) ? 0xFFFFFFFFuL : (uint32_t) i32_diff));
        }
        return;
    }

    if (c_sub == 'S')
    {
        spiflash_status_reg_t x_sr;
        uint8_t u8_hal = u8_spiflash_read_status(&x_sr);

        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,S,FAIL,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }
        v_acon_emit(ACON_SIG_OK, "%s,S,%02X",
                    pc_acon_op_name(c_op, ac_op), (unsigned) x_sr.all);
        return;
    }

    /* Default sub-op: JEDEC ID probe. */
    {
        spiflash_id_t x_id;
        uint8_t u8_hal = u8_spiflash_read_id(&x_id);
        const char *pc_verdict;

        if (u8_hal != HAL_OK)
        {
            v_acon_emit(ACON_SIG_ERR, "%s,FAIL,%X",
                        pc_acon_op_name(c_op, ac_op), (unsigned) u8_hal);
            return;
        }

        if (((x_id.u8_manufacturer_id == 0x00u) && (x_id.u8_memory_type == 0x00u) &&
             (x_id.u8_memory_density == 0x00u)) ||
            ((x_id.u8_manufacturer_id == 0xFFu) && (x_id.u8_memory_type == 0xFFu) &&
             (x_id.u8_memory_density == 0xFFu)))
        {
            pc_verdict = "NONE";        /* MISO stuck low/high: part not answering */
        }
        else
        {
            /* Any coherent JEDEC id means a device answered. Part-agnostic:
             * 0xC2 Macronix, 0xEF Winbond (W25Q), 0xBF SST, ... the raw bytes
             * name it; the wiring check only cares that something replied. */
            pc_verdict = "OK";
        }

        v_acon_emit(ACON_SIG_OK, "%s,%02X,%02X,%02X,%s",
                    pc_acon_op_name(c_op, ac_op),
                    (unsigned) x_id.u8_manufacturer_id,
                    (unsigned) x_id.u8_memory_type,
                    (unsigned) x_id.u8_memory_density,
                    pc_verdict);
    }
}

/*============================================================================
 * COMMAND TABLE  (the application-owned port point; core dispatches into this)
 *==========================================================================*/

const acon_op_t g_x_acon_command[] =
{
    { 'S', v_acon_op_set,         "set levels: select,set,clear" },
    { 'R', v_acon_op_read,        "read state"                   },
    { 'W', v_acon_op_write,       "write cycle: ch,on,off,rpt"   },
    { 'G', v_acon_op_get,         "get cycle params: ch"         },
    { 'C', v_acon_op_cycle_start, "start cycling: mask"          },
    { 'X', v_acon_op_cycle_stop,  "stop cycling: mask"           },
    { 'P', v_acon_op_persist,     "persist params to NVM"        },
    { 'E', v_acon_op_errors,      "transport error count"        },
    { 'N', v_acon_op_nvm_test,    "nvm test: sub[,args]"         },
    { 'F', v_acon_op_eventq_test, "event queue (fifo) test: sub[,args]" },
    { 'A', v_acon_op_event_mask,  "event mask: [hex] (bit31=global enable)" },
    { 'D', v_acon_op_event_drain, "drain events: [max] (0=all)"           },
    { 'H', v_acon_op_event_house, "event queue: [S=status,F=flush,R=reset]" },
    { 'Y', v_acon_op_spiflash,    "spi flash probe: [I=id,S=status,T=dma rw test,L=loopback,N=ncs lb,J=sck lb,C[,0|1]=park cs,K=clock burst] (temp)" },
    { 'U', v_acon_op_uart_stress, "uart loopback stress: idx[,first,last,bursts]" },
    { 'B', v_acon_op_baud_sweep,  "baud sweep: idx[,rate...] (default ladder)" },
    { '@', v_acon_op_echo_args,   "echo args as CSV (example)"   },
    { '$', v_acon_op_echo_raw,    "echo raw text (example)"      },
};

const uint8_t g_u8_acon_command_count =
    (uint8_t) (sizeof(g_x_acon_command) / sizeof(g_x_acon_command[0]));

#endif /* ACON_ENABLED */
