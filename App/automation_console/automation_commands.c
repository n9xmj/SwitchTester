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
#include "nvmparams.h"
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

/*============================================================================
 * COMMAND TABLE  (the application-owned seam; core dispatches into this)
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
    { 'U', v_acon_op_uart_stress, "uart loopback stress: idx[,first,last,bursts]" },
    { 'B', v_acon_op_baud_sweep,  "baud sweep: idx[,rate...] (default ladder)" },
    { '@', v_acon_op_echo_args,   "echo args as CSV (example)"   },
    { '$', v_acon_op_echo_raw,    "echo raw text (example)"      },
};

const uint8_t g_u8_acon_command_count =
    (uint8_t) (sizeof(g_x_acon_command) / sizeof(g_x_acon_command[0]));

#endif /* ACON_ENABLED */
