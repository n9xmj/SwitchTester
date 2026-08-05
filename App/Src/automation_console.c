/******************************************************************************
 * automation_console.c
 *
 * Machine-facing command console -- see automation_console.h for the protocol
 * and Docs/planning/automation-console-plan.md for why it is shaped this way.
 *
 * Two invariants hold this together and are easy to break by accident:
 *
 *  1. Nothing in here ever calls printf. Output goes through v_acon_emit(),
 *     which writes to uart_stream directly, bypassing stdio entirely. That is
 *     what lets stdout be suppressed wholesale in SCRIPT mode (I7), and it is
 *     also why the console could be moved to a different UART by passing a
 *     different handle.
 *
 *  2. The sigil is an argument to v_acon_emit(), not part of the format string.
 *     "every device->host line carries a sigil" is thereby a property of the
 *     signature rather than a convention every call site has to remember.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include <stdarg.h>

#include "device_config.h"          /* stdint/stdio/stdlib/string, main.h, platform.h */
#include "utils.h"                  /* i_getline (human-mode reader) */
#include "uart_stream.h"
#include "stdio_retarget.h"         /* h_stdio_retarget_get_stream */
#include "switch_out.h"
#include "nvmparams.h"
#include "automation_console.h"

/*============================================================================
 * STANDALONE FALLBACK
 *
 * platform.h normally supplies PUMP_POLLING_TASK(). This keeps the module
 * compiling where it is absent -- lifted into another project, or built
 * deliberately without that dependency.
 *
 * The #warning is the point: compiling the pump out is a legitimate choice but
 * a terrible accident. Without it a forgotten include silently turns the SCRIPT
 * reader into one that services nothing while it waits, and the symptom is a
 * board that appears to hang.
 *==========================================================================*/

#ifndef PUMP_POLLING_TASK
#warning "platform.h not included: PUMP_POLLING_TASK() compiled out, the console will not pump the main loop"
#define PUMP_POLLING_TASK()     do { } while (0)
#endif

/*============================================================================
 * PROTOCOL CONSTANTS
 *==========================================================================*/

typedef enum
{
    ACON_SIG_OK      = '=',         /* command response, success               */
    ACON_SIG_ERR     = '!',         /* command response, failure               */
    ACON_SIG_PAYLOAD = '+',         /* payload continuation line               */
    ACON_SIG_EVENT   = '*'          /* async event -- phase 2, unused today    */
}
acon_sigil_t;

/* Protocol-level frames use a reserved opcode, so a session event parses in the
 * same shape as a command response instead of being a special case. */
#define ACON_OP_SESSION         '~'

#define ACON_OP_NOP             'Z'
#define ACON_OP_QUIT            'Q'
#define ACON_OP_VERSION         'V'
#define ACON_OP_LIST            'L'
#define ACON_OP_LIST_ALT        '?'
#define ACON_OP_CANCEL          ('C' - 0x40)    /* Ctrl-C, 0x03: quit alias    */

#define ACON_PROTOCOL_VERSION   1

/* Error codes. Short, fixed mnemonics -- the host switches on these. */
#define ACON_ERR_UNKNOWN        "UNK"       /* no such opcode                  */
#define ACON_ERR_ARGS           "ARG"       /* missing or unparseable field    */
#define ACON_ERR_RANGE          "RNG"       /* parsed, but outside limits      */
#define ACON_ERR_BUSY           "BUSY"      /* refused: cycling in progress    */
#define ACON_ERR_NVM            "NVM"       /* flash commit failed             */
#define ACON_ERR_OVERFLOW       "OVF"       /* line or frame too long          */
#define ACON_ERR_DUPLICATE      "DUP"       /* op table conflict (I2)          */

#define ACON_MAX_ARGS           6u

/*============================================================================
 * MODULE STATE
 *==========================================================================*/

/* Static rather than stack: the console runs on an already-nested main-loop
 * stack, and these are large enough to matter there. Safe as single shared
 * buffers because everything here is main-loop only and never reentrant --
 * nothing in this file may be called from an ISR. */
static char         s_ac_line[ACON_LINE_MAX];
static char         s_ac_emit[ACON_EMIT_MAX];

static acon_mode_t  s_x_mode;
static uint8_t      s_u8_session_active;

typedef enum
{
    ACON_LINE_OK = 0,               /* a complete line is in s_ac_line         */
    ACON_LINE_QUIT,                 /* exit sentinel or Ctrl-C                 */
    ACON_LINE_TIMEOUT,              /* SCRIPT mode idle timeout                */
    ACON_LINE_TOOLONG               /* overran the buffer; never dispatched    */
}
acon_line_t;

typedef void (*acon_handler_t)(char c_op, char *ap_c_arg[], uint8_t u8_argc);

typedef struct
{
    char            c_op;
    acon_handler_t  pfn_handler;
    const char     *pc_help;
}
acon_op_t;

/*============================================================================
 * OUTPUT
 *==========================================================================*/

static void v_acon_write(const char *pc_data, uint16_t u16_len)
{
    uart_stream_h_t h_stream = h_stdio_retarget_get_stream();

    if (h_stream != UART_STREAM_HANDLE_INVALID)
    {
        (void) u16_uart_stream_tx_multi_blocking(h_stream,
                                                 (const uint8_t *) pc_data,
                                                 u16_len,
                                                 ACON_TX_TIMEOUT_MS);
    }
}

/*
 * Emit one frame. The terminator is appended here so no call site can forget
 * it and run two frames together -- which would look like a protocol bug rather
 * than a missing newline. Format strings therefore never contain a newline.
 *
 * A truncated frame is the dangerous case: it is still a well-formed SHORTER
 * frame to the host, so "=G,L9,M4,R4,N7A1" would parse cleanly and be wrong.
 * On truncation the partial line is discarded and an overflow frame sent in its
 * place -- never the fragment.
 */
static void v_acon_emit(acon_sigil_t x_sigil, const char *pc_format, ...)
{
    static const char ac_overflow[] = "!~,OVF\r\n";
    const uint16_t u16_room = (uint16_t) (sizeof(s_ac_emit) - 4u);
    va_list x_args;
    int i_len;

    s_ac_emit[0] = (char) x_sigil;

    va_start(x_args, pc_format);
    i_len = vsnprintf(&s_ac_emit[1], (size_t) u16_room + 1u, pc_format, x_args);
    va_end(x_args);

    if ((i_len < 0) || ((uint16_t) i_len > u16_room))
    {
        v_acon_write(ac_overflow, (uint16_t) (sizeof(ac_overflow) - 1u));
        return;
    }

    s_ac_emit[1 + i_len]     = '\r';
    s_ac_emit[1 + i_len + 1] = '\n';
    v_acon_write(s_ac_emit, (uint16_t) (i_len + 3));
}

/*
 * Opcode as it appears in a response. Control characters echo in caret notation
 * so a frame is printable end to end -- an ESC echoed raw would start an ANSI
 * sequence and eat the following characters off the operator's screen.
 */
static const char * pc_acon_op_name(char c_op, char ac_buf[4])
{
    uint8_t u8_op = (uint8_t) c_op;

    if (u8_op == 0x7Fu)
    {
        ac_buf[0] = '^'; ac_buf[1] = '?'; ac_buf[2] = '\0';
    }
    else if (u8_op < 0x20u)
    {
        ac_buf[0] = '^'; ac_buf[1] = (char) (u8_op + 0x40u); ac_buf[2] = '\0';
    }
    else
    {
        ac_buf[0] = c_op; ac_buf[1] = '\0';
    }
    return ac_buf;
}

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

static void v_acon_err(char c_op, const char *pc_code)
{
    char ac_op[4];
    v_acon_emit(ACON_SIG_ERR, "%s,%s", pc_acon_op_name(c_op, ac_op), pc_code);
}

/*============================================================================
 * INPUT PARSING
 *==========================================================================*/

/*
 * Split the argument text in place. Field 0 is whatever followed the opcode;
 * an optional comma directly after the opcode is skipped, so both "R" and "R,"
 * mean "no arguments".
 */
static uint8_t u8_acon_args(char *pc_line, char *ap_c_arg[], uint8_t u8_max)
{
    uint8_t u8_count = 0;
    char *pc = pc_line + 1;

    if (*pc == ',')
    {
        pc++;
    }
    if (*pc == '\0')
    {
        return 0;
    }

    ap_c_arg[u8_count++] = pc;

    while (*pc != '\0')
    {
        if ((*pc == ',') && (u8_count < u8_max))
        {
            *pc = '\0';
            ap_c_arg[u8_count++] = pc + 1;
        }
        pc++;
    }
    return u8_count;
}

/* Hex, and the whole field must parse. Empty or trailing junk is a failure, so
 * "S,3,1,x" is rejected rather than quietly read as zero. */
static uint8_t b_acon_arg_u32(const char *pc_arg, uint32_t *p_u32_value)
{
    char *pc_end;
    unsigned long ul_value;

    if ((pc_arg == NULL) || (*pc_arg == '\0'))
    {
        return 0;
    }

    ul_value = strtoul(pc_arg, &pc_end, 16);

    if (*pc_end != '\0')
    {
        return 0;
    }

    *p_u32_value = (uint32_t) ul_value;
    return 1;
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
static void v_acon_op_set(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
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
static void v_acon_op_read(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    (void) ap_c_arg;
    (void) u8_argc;
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
static void v_acon_op_write(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
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
static void v_acon_op_get(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
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
static void v_acon_op_cycle_start(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
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
static void v_acon_op_cycle_stop(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
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
static void v_acon_op_persist(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    nvm_error_t x_status;
    char ac_op[4];

    (void) ap_c_arg;
    (void) u8_argc;

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
static void v_acon_op_errors(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    uart_stream_h_t h_stream = h_stdio_retarget_get_stream();
    char ac_op[4];

    (void) ap_c_arg;
    (void) u8_argc;

    v_acon_emit(ACON_SIG_OK, "%s,E%lX", pc_acon_op_name(c_op, ac_op),
                (unsigned long) u32_uart_stream_get_error_count(h_stream));
}

/* V -- identity. Enough for a host to pin exactly what it is talking to. */
static void v_acon_op_version(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    (void) ap_c_arg;
    (void) u8_argc;

    v_acon_emit(ACON_SIG_OK, "%c,%s,%s,%s,%s", c_op,
                PRODUCT_NAME, PLATFORM_NAME, FIRMWARE_VERSION, BUILD_CONFIG);
}

/* Z / space / bare CR -- no-op. All three normalise to this one response, so a
 * host never handles a frame whose opcode field is a space or a control code. */
static void v_acon_op_nop(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    (void) c_op;
    (void) ap_c_arg;
    (void) u8_argc;

    v_acon_emit(ACON_SIG_OK, "%c", ACON_OP_NOP);
}

static void v_acon_op_list(char c_op, char *ap_c_arg[], uint8_t u8_argc);
static void v_acon_op_quit(char c_op, char *ap_c_arg[], uint8_t u8_argc);

/*============================================================================
 * OP TABLE
 *==========================================================================*/

static const acon_op_t s_ax_acon_op[] =
{
    { 'S',              v_acon_op_set,          "set levels: select,set,clear" },
    { 'R',              v_acon_op_read,         "read state"                   },
    { 'W',              v_acon_op_write,        "write cycle: ch,on,off,rpt"    },
    { 'G',              v_acon_op_get,          "get cycle params: ch"         },
    { 'C',              v_acon_op_cycle_start,  "start cycling: mask"          },
    { 'X',              v_acon_op_cycle_stop,   "stop cycling: mask"           },
    { 'P',              v_acon_op_persist,      "persist params to NVM"        },
    { 'E',              v_acon_op_errors,       "transport error count"        },
    { ACON_OP_VERSION,  v_acon_op_version,      "identity"                     },
    { ACON_OP_LIST,     v_acon_op_list,         "list ops"                     },
    { ACON_OP_LIST_ALT, v_acon_op_list,         "list ops"                     },
    { ACON_OP_NOP,      v_acon_op_nop,          "no-op"                        },
    { ' ',              v_acon_op_nop,          "no-op"                        },
    { ACON_OP_QUIT,     v_acon_op_quit,         "quit"                         },
    { ACON_OP_CANCEL,   v_acon_op_quit,         "quit"                         },
};

#define ACON_OP_COUNT   (sizeof(s_ax_acon_op) / sizeof(s_ax_acon_op[0]))

static uint8_t s_u8_quit_requested;

static void v_acon_op_quit(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    (void) c_op;
    (void) ap_c_arg;
    (void) u8_argc;

    s_u8_quit_requested = 1;
}

static void v_acon_op_list(char c_op, char *ap_c_arg[], uint8_t u8_argc)
{
    uint8_t u8_i;
    char ac_op[4];

    (void) ap_c_arg;
    (void) u8_argc;

    /* Header declares the payload count, so the host reads exactly that many
     * lines. A truncated frame is then detectable, which a terminator could not
     * manage. */
    v_acon_emit(ACON_SIG_OK, "%s,K%X", pc_acon_op_name(c_op, ac_op),
                (unsigned) ACON_OP_COUNT);

    for (u8_i = 0; u8_i < ACON_OP_COUNT; u8_i++)
    {
        v_acon_emit(ACON_SIG_PAYLOAD, "%s %s",
                    pc_acon_op_name(s_ax_acon_op[u8_i].c_op, ac_op),
                    s_ax_acon_op[u8_i].pc_help);
    }
}

/*
 * Registration-time collision check. The table is static const, so the result
 * cannot change within a run -- entry is simply the moment where reporting it
 * is useful. O(n^2) over a dozen entries is a few hundred cycles, once, and
 * needs no state to remember whether it has run.
 *
 * The report is mode-appropriate: a host wants to fail its run immediately
 * rather than discover a shadowed opcode by its symptoms, and a person wants a
 * sentence. This mirrors how menusystem already reports key-code duplication.
 */
static void v_acon_check_op_table(void)
{
    uint8_t u8_i, u8_j;

    for (u8_i = 0; u8_i < ACON_OP_COUNT; u8_i++)
    {
        for (u8_j = (uint8_t) (u8_i + 1u); u8_j < ACON_OP_COUNT; u8_j++)
        {
            char ac_op[4];

            if (s_ax_acon_op[u8_i].c_op != s_ax_acon_op[u8_j].c_op)
            {
                continue;
            }

            /* Two entries pointing at the same handler are an intentional
             * alias (L and ?, Q and Ctrl-C), not a conflict. */
            if (s_ax_acon_op[u8_i].pfn_handler == s_ax_acon_op[u8_j].pfn_handler)
            {
                continue;
            }

            if (s_x_mode == ACON_MODE_SCRIPT)
            {
                v_acon_emit(ACON_SIG_ERR, "%c,%s,E%X", ACON_OP_SESSION,
                            ACON_ERR_DUPLICATE,
                            (unsigned) (uint8_t) s_ax_acon_op[u8_i].c_op);
            }
            else
            {
                printf("Automation console: opcode [%s] is claimed twice "
                       "(entries %u and %u) - the second is unreachable\r\n",
                       pc_acon_op_name(s_ax_acon_op[u8_i].c_op, ac_op),
                       (unsigned) u8_i, (unsigned) u8_j);
            }
        }
    }
}

/*============================================================================
 * INPUT
 *==========================================================================*/

/*
 * One received byte, or -1 if nothing is waiting. Never blocks.
 *
 * Reads uart_stream's RX ring DIRECTLY rather than going through getchar().
 * newlib's stdio costs roughly 2200 cycles per byte here -- enough that the
 * old path could only drain about a third of a sustained 921600-baud stream,
 * losing 69% of an 8 kB burst. Straight off the ring it keeps up.
 *
 * Safe only because stdin is unbuffered (_IONBF, set in v_stdio_retarget): with
 * a buffered stdin, newlib could be holding a byte that this path would never
 * see, and the console would silently lose the first character of a command.
 *
 * Falls back to getchar() if the console was never bound to uart_stream -- the
 * bind can fail, and a degraded console beats a deaf one.
 */
static int16_t i16_acon_rx_byte(uart_stream_h_t h_stream)
{
    if (h_stream != UART_STREAM_HANDLE_INVALID)
    {
        return i16_uart_stream_rx_byte(h_stream);
    }
    return (int16_t) getchar();
}

/*
 * SCRIPT reader: raw, byte at a time, no echo.
 *
 * CR terminates. LF is discarded wherever it appears -- not "CR or LF", and not
 * "CR with a following LF swallowed". That one rule is what lets an empty line
 * be a no-op that answers without a CRLF host generating a spurious second
 * frame for every command it sends.
 *
 * The idle timer resets on ANY received byte, so a keep-alive works even
 * mid-line, and it is armed here rather than only at the top of the loop -- a
 * host that sends one byte and dies must not wedge the board.
 */
static acon_line_t x_acon_read_script(void)
{
    uart_stream_h_t h_stream = h_stdio_retarget_get_stream();
    uint16_t u16_len = 0;
    uint8_t u8_overflow = 0;
    uint32_t u32_t0 = SYSTEM_TICK();

    for (;;)
    {
        int16_t i16_ch;

        PUMP_POLLING_TASK();

        /* Drain everything the ISR has already captured before pumping again.
         * The pump is the expensive part of this loop, so paying it once per
         * burst instead of once per byte is what lets the reader keep pace with
         * the wire. Responsiveness is unaffected: the inner loop only runs
         * while bytes are actually waiting, and it exits at the terminator. */
        while ((i16_ch = i16_acon_rx_byte(h_stream)) >= 0)
        {
            u32_t0 = SYSTEM_TICK();

            if ((uint8_t) i16_ch == ACON_EXIT)
            {
                return ACON_LINE_QUIT;
            }
            if (i16_ch == '\n')
            {
                continue;
            }
            if (i16_ch == '\r')
            {
                s_ac_line[u16_len] = '\0';
                return u8_overflow ? ACON_LINE_TOOLONG : ACON_LINE_OK;
            }

            if (u16_len < (uint16_t) (sizeof(s_ac_line) - 1u))
            {
                s_ac_line[u16_len++] = (char) i16_ch;
            }
            else
            {
                /* Remember it and keep consuming to the terminator. A truncated
                 * command must be rejected, never executed. */
                u8_overflow = 1;
            }
        }

        if (ELAPSED_TIME(u32_t0) >= ACON_IDLE_TIMEOUT_MS)
        {
            return ACON_LINE_TIMEOUT;
        }
    }
}

/*
 * HUMAN reader: i_getline(), so echo, destructive backspace, ESC-cancel and
 * Ctrl-X clear all come from the same code the debug menu uses -- the console
 * does not grow its own editing feel that drifts from the rest of the app.
 *
 * No idle timeout, and the rationale does not transfer: the timeout guards
 * against a dead HOST, and this mode has an operator sitting at the terminal.
 */
static acon_line_t x_acon_read_human(void)
{
    int i_length = i_getline(s_ac_line, (uint16_t) (sizeof(s_ac_line) - 1u));

    if (i_length == -2)                 /* Ctrl-C: silent abandon */
    {
        return ACON_LINE_QUIT;
    }
    if (i_length < 0)                   /* ESC: cancel the line, stay in */
    {
        s_ac_line[0] = '\0';
    }
    return ACON_LINE_OK;
}

/*============================================================================
 * EXECUTIVE
 *==========================================================================*/

/*
 * Phase 2 hook. Async event frames may only appear between the end of one
 * response and the start of the next, so this is where they will be flushed.
 * The call site exists now, empty, because it fixes the one architectural point
 * -- WHERE async output is allowed -- while the loop is still small enough to
 * take in at a glance. See automation-console-plan.md (I5, S6, S7).
 */
static void v_acon_flush_events(void)
{
}

static void v_acon_dispatch(void)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc;
    char c_op;
    uint8_t u8_i;

    /* An empty line is the bare-CR no-op. It answers like everything else --
     * there is no silent path anywhere in the protocol. */
    if (s_ac_line[0] == '\0')
    {
        v_acon_op_nop(ACON_OP_NOP, NULL, 0);
        return;
    }

    c_op = s_ac_line[0];
    u8_argc = u8_acon_args(s_ac_line, ap_c_arg, (uint8_t) ACON_MAX_ARGS);

    for (u8_i = 0; u8_i < ACON_OP_COUNT; u8_i++)
    {
        /* Strict case sensitivity: no toupper() anywhere. Folding case would
         * permanently halve the namespace to tolerate a typo a machine does
         * not make, and a lowercase send gets a clean !x,UNK instead. */
        if (s_ax_acon_op[u8_i].c_op == c_op)
        {
            s_ax_acon_op[u8_i].pfn_handler(c_op, ap_c_arg, u8_argc);
            return;
        }
    }

    v_acon_err(c_op, ACON_ERR_UNKNOWN);
}

void v_automation_console_run(acon_mode_t x_mode)
{
    s_x_mode = x_mode;
    s_u8_quit_requested = 0;

    /* Set before the banner: from here on stdout is muted in SCRIPT mode, so
     * nothing a job prints can land between our frames. */
    s_u8_session_active = 1;

    v_acon_emit(ACON_SIG_OK, "%c,V%X", ACON_OP_SESSION, ACON_PROTOCOL_VERSION);
    v_acon_check_op_table();

    while (!s_u8_quit_requested)
    {
        acon_line_t x_line = (s_x_mode == ACON_MODE_SCRIPT)
                             ? x_acon_read_script()
                             : x_acon_read_human();

        if (x_line == ACON_LINE_QUIT)
        {
            break;
        }
        if (x_line == ACON_LINE_TIMEOUT)
        {
            /* '!' rather than '=': the host did not ask to leave. Drive state
             * is untouched, so a soak run started earlier keeps running. */
            v_acon_emit(ACON_SIG_ERR, "%c,TMO", ACON_OP_SESSION);
            s_u8_session_active = 0;
            return;
        }
        if (x_line == ACON_LINE_TOOLONG)
        {
            v_acon_err(ACON_OP_SESSION, ACON_ERR_OVERFLOW);
        }
        else
        {
            v_acon_dispatch();
        }

        v_acon_flush_events();
    }

    v_acon_emit(ACON_SIG_OK, "%c,BYE", ACON_OP_SESSION);
    s_u8_session_active = 0;
}

uint8_t u8_automation_console_mutes_stdout(void)
{
    /* HUMAN mode leaves stdout alone and must: i_getline() echoes through
     * printf, and there is no host parser to protect there. */
    return (uint8_t) (s_u8_session_active && (s_x_mode == ACON_MODE_SCRIPT));
}
