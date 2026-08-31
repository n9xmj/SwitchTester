/******************************************************************************
 * automation_console.c
 *
 * Machine-facing command console -- portable CORE. See automation_console.h for
 * the protocol and the core/commands contract, and
 * Docs/planning/automation-console-plan.md for why it is shaped this way.
 *
 * This file has NO application dependencies. Domain commands live in
 * automation_commands.c and are reached through g_x_acon_command[]; the builtins
 * (quit / list / version / no-op) are owned here and are always present.
 *
 * Two invariants hold this together and are easy to break by accident:
 *
 *  1. In SCRIPT mode the protocol path does not touch stdio in EITHER
 *     direction. Output goes through v_acon_emit() and input through
 *     i16_acon_rx_byte(), and both talk to uart_stream directly. That is what
 *     lets stdout be suppressed wholesale during a session without silencing the
 *     console itself, it is why the reader can keep pace with the wire, and it
 *     is why moving the console to a different UART is a matter of passing a
 *     different handle rather than a redesign.
 *
 *     HUMAN mode is the deliberate opposite: i_getline() reads through stdio and
 *     echoes through printf. The one printf in this file -- the op-table
 *     conflict report -- is on that path only, never on the protocol path.
 *
 *  2. The sigil is an argument to v_acon_emit(), not part of the format string,
 *     so "every device->host line carries a sigil" is a property of the
 *     signature rather than a convention every call site has to remember.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "automation_console.h"      /* pulls automation_console_config.h */
#include "utils.h"                   /* i_getline (human-mode reader) */
#include "uart_stream.h"
#include "stdio_retarget.h"          /* h_stdio_retarget_get_stream */

#if ACON_ENABLED

/*============================================================================
 * PLATFORM HOOKS -- and what happens when a config header omits one
 *
 * Both are normally defined in automation_console_config.h, which maps them
 * onto whatever the project already has (platform.h's SYSTEM_TICK() and
 * PUMP_POLLING_TASK() here). These fallbacks keep the module compiling where
 * they are absent -- lifted into another project, or built deliberately without
 * that dependency.
 *
 * The #warnings are the point: compiling either out is a legitimate choice but
 * a terrible accident.
 *
 *   No pump -- the SCRIPT reader services nothing while it waits, and the
 *              symptom is a board that appears to hang.
 *   No tick -- elapsed time is permanently zero, so the SCRIPT-mode idle
 *              timeout never fires and a host that dies mid-session wedges the
 *              console until someone power-cycles the board.
 *==========================================================================*/

#ifndef ACON_PUMP
#warning "ACON_PUMP() not configured: the console will not pump the main loop while it waits"
#define ACON_PUMP()             do { } while (0)
#endif

#ifndef ACON_TICK_MS
#warning "ACON_TICK_MS() not configured: the SCRIPT-mode idle timeout is disabled"
#define ACON_TICK_MS()          0u
#endif

/*
 * Session brackets. Optional, and silent when unset -- unlike the two above,
 * omitting these costs nothing, so there is no #warning.
 *
 * ACON_ON_EXIT() runs on EVERY way out: quit, exit sentinel, and the idle
 * timeout's early return. Anything it undoes must be undone on all three, or
 * the one path a dead host actually takes is the one that leaks.
 */
#ifndef ACON_ON_ENTER
#define ACON_ON_ENTER()         do { } while (0)
#endif

#ifndef ACON_ON_EXIT
#define ACON_ON_EXIT()          do { } while (0)
#endif

/* Unsigned wrap does the right thing at rollover, so no special case. */
#define ACON_ELAPSED_MS(ts)     (ACON_TICK_MS() - (ts))

/*============================================================================
 * PROTOCOL CONSTANTS (core-owned)
 *==========================================================================*/

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

#define ACON_ERR_DUPLICATE      "DUP"       /* op table conflict, reported once */

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
static uint8_t      s_u8_quit_requested;

typedef enum
{
    ACON_LINE_OK = 0,               /* a complete line is in s_ac_line         */
    ACON_LINE_QUIT,                 /* exit sentinel or Ctrl-C                 */
    ACON_LINE_TIMEOUT,              /* SCRIPT mode idle timeout                */
    ACON_LINE_TOOLONG               /* overran the buffer; never dispatched    */
}
acon_line_t;

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

void v_acon_emit(acon_sigil_t x_sigil, const char *pc_format, ...)
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

const char * pc_acon_op_name(char c_op, char ac_buf[4])
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

void v_acon_ok(char c_op)
{
    char ac_op[4];
    v_acon_emit(ACON_SIG_OK, "%s", pc_acon_op_name(c_op, ac_op));
}

void v_acon_err(char c_op, const char *pc_code)
{
    char ac_op[4];
    v_acon_emit(ACON_SIG_ERR, "%s,%s", pc_acon_op_name(c_op, ac_op), pc_code);
}

/*============================================================================
 * INPUT PARSING
 *==========================================================================*/

uint8_t u8_acon_args(char *pc_line, char *ap_c_arg[], uint8_t u8_max)
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

uint8_t b_acon_arg_u32(const char *pc_arg, uint32_t *p_u32_value)
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

/*============================================================================
 * BUILTINS  (always present; an application command table cannot shadow these)
 *==========================================================================*/

static void v_acon_builtin_nop(char c_op, char *pc_line);
static void v_acon_builtin_version(char c_op, char *pc_line);
static void v_acon_builtin_quit(char c_op, char *pc_line);
static void v_acon_builtin_list(char c_op, char *pc_line);

static const acon_op_t s_x_acon_builtin[] =
{
    { ACON_OP_VERSION,  v_acon_builtin_version, "identity" },
    { ACON_OP_LIST,     v_acon_builtin_list,    "list ops" },
    { ACON_OP_LIST_ALT, v_acon_builtin_list,    "list ops" },
    { ACON_OP_NOP,      v_acon_builtin_nop,     "no-op"    },
    { ' ',              v_acon_builtin_nop,     "no-op"    },
    { ACON_OP_QUIT,     v_acon_builtin_quit,    "quit"     },
    { ACON_OP_CANCEL,   v_acon_builtin_quit,    "quit"     },
};

#define ACON_BUILTIN_COUNT   (sizeof(s_x_acon_builtin) / sizeof(s_x_acon_builtin[0]))

/* Z / space / bare CR -- no-op. All three normalise to this one response, so a
 * host never handles a frame whose opcode field is a space or a control code. */
static void v_acon_builtin_nop(char c_op, char *pc_line)
{
    (void) c_op;
    (void) pc_line;
    v_acon_emit(ACON_SIG_OK, "%c", ACON_OP_NOP);
}

/* V -- identity. Enough for a host to pin exactly what it is talking to. */
static void v_acon_builtin_version(char c_op, char *pc_line)
{
    (void) pc_line;
    v_acon_emit(ACON_SIG_OK, "%c,%s,%s,%s,%s", c_op,
                ACON_ID_PRODUCT, ACON_ID_PLATFORM,
                ACON_ID_FIRMWARE, ACON_ID_BUILD);
}

/* Q / Ctrl-C -- leave the session. */
static void v_acon_builtin_quit(char c_op, char *pc_line)
{
    (void) c_op;
    (void) pc_line;
    s_u8_quit_requested = 1;
}

/* L / ? -- list every opcode, application commands first then builtins. */
static void v_acon_builtin_list(char c_op, char *pc_line)
{
    uint8_t u8_i;
    char ac_op[4];

    (void) pc_line;

    /* Header declares the payload count, so the host reads exactly that many
     * lines. A truncated frame is then detectable, which a terminator could not
     * manage. */
    v_acon_emit(ACON_SIG_OK, "%s,K%X", pc_acon_op_name(c_op, ac_op),
                (unsigned) (g_u8_acon_command_count + (uint8_t) ACON_BUILTIN_COUNT));

    for (u8_i = 0; u8_i < g_u8_acon_command_count; u8_i++)
    {
        v_acon_emit(ACON_SIG_PAYLOAD, "%s %s",
                    pc_acon_op_name(g_x_acon_command[u8_i].c_op, ac_op),
                    g_x_acon_command[u8_i].pc_help);
    }
    for (u8_i = 0; u8_i < (uint8_t) ACON_BUILTIN_COUNT; u8_i++)
    {
        v_acon_emit(ACON_SIG_PAYLOAD, "%s %s",
                    pc_acon_op_name(s_x_acon_builtin[u8_i].c_op, ac_op),
                    s_x_acon_builtin[u8_i].pc_help);
    }
}

/*
 * Registration-time collision check across BOTH tables. The tables are static
 * const, so the result cannot change within a run -- entry is simply the moment
 * where reporting it is useful.
 *
 * An application opcode that also names a builtin is unreachable (builtins are
 * matched first), and an application opcode claimed twice shadows itself. Both
 * are reported. Two entries pointing at the same handler are an intentional
 * alias (L and ?, Q and Ctrl-C), never a conflict.
 *
 * The report is mode-appropriate: a host wants to fail its run immediately
 * rather than discover a shadowed opcode by its symptoms, and a person wants a
 * sentence. This mirrors how menusystem reports key-code duplication.
 */
static void v_acon_report_dup(char c_op, uint8_t u8_i, uint8_t u8_j)
{
    char ac_op[4];

    if (s_x_mode == ACON_MODE_SCRIPT)
    {
        v_acon_emit(ACON_SIG_ERR, "%c,%s,E%X", ACON_OP_SESSION,
                    ACON_ERR_DUPLICATE, (unsigned) (uint8_t) c_op);
    }
    else
    {
        printf("Automation console: opcode [%s] is claimed twice "
               "(entries %u and %u) - the second is unreachable\r\n",
               pc_acon_op_name(c_op, ac_op), (unsigned) u8_i, (unsigned) u8_j);
    }
}

static void v_acon_check_op_table(void)
{
    uint8_t u8_i, u8_j;

    /* Application entry that collides with a builtin -- unreachable. */
    for (u8_i = 0; u8_i < g_u8_acon_command_count; u8_i++)
    {
        for (u8_j = 0; u8_j < (uint8_t) ACON_BUILTIN_COUNT; u8_j++)
        {
            if (g_x_acon_command[u8_i].c_op == s_x_acon_builtin[u8_j].c_op)
            {
                v_acon_report_dup(g_x_acon_command[u8_i].c_op, u8_i, u8_j);
            }
        }
    }

    /* Application entry claimed twice within its own table. */
    for (u8_i = 0; u8_i < g_u8_acon_command_count; u8_i++)
    {
        for (u8_j = (uint8_t) (u8_i + 1u); u8_j < g_u8_acon_command_count; u8_j++)
        {
            if (g_x_acon_command[u8_i].c_op != g_x_acon_command[u8_j].c_op)
            {
                continue;
            }
            if (g_x_acon_command[u8_i].pfn_handler == g_x_acon_command[u8_j].pfn_handler)
            {
                continue;               /* intentional alias */
            }
            v_acon_report_dup(g_x_acon_command[u8_i].c_op, u8_i, u8_j);
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
 * newlib's stdio costs roughly 2200 cycles per byte here -- enough that the old
 * path could only drain about a third of a sustained 921600-baud stream, losing
 * 69% of an 8 kB burst. Straight off the ring it keeps up.
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
 * Public non-blocking poll, for a long-running command that has to watch its
 * host while it streams. The core never uses this itself -- it reads through
 * i16_acon_rx_byte() with the handle already in hand -- so this exists purely
 * to give command authors the same capability without reaching around the
 * module for the stream handle and re-deriving the getchar() fallback.
 */
int16_t i16_acon_rx_poll(void)
{
    return i16_acon_rx_byte(h_stdio_retarget_get_stream());
}

/*
 * SCRIPT reader: raw, byte at a time, no echo.
 *
 * CR terminates. LF is discarded wherever it appears -- not "CR or LF", and not
 * "CR with a following LF swallowed". That one rule is what lets an empty line
 * be a no-op that answers without a CRLF host generating a spurious second frame
 * for every command it sends.
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
    uint32_t u32_t0 = ACON_TICK_MS();

    for (;;)
    {
        int16_t i16_ch;

        ACON_PUMP();

        /* Drain everything the ISR has already captured before pumping again.
         * The pump is the expensive part of this loop, so paying it once per
         * burst instead of once per byte is what lets the reader keep pace with
         * the wire. Responsiveness is unaffected: the inner loop only runs while
         * bytes are actually waiting, and it exits at the terminator. */
        while ((i16_ch = i16_acon_rx_byte(h_stream)) >= 0)
        {
            u32_t0 = ACON_TICK_MS();

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

        if (ACON_ELAPSED_MS(u32_t0) >= ACON_IDLE_TIMEOUT_MS)
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

static const acon_op_t * px_acon_find(const acon_op_t *px_table,
                                      uint8_t u8_count, char c_op)
{
    uint8_t u8_i;

    for (u8_i = 0; u8_i < u8_count; u8_i++)
    {
        if ((px_table[u8_i].c_op == c_op) && (px_table[u8_i].pfn_handler != NULL))
        {
            return &px_table[u8_i];
        }
    }
    return NULL;
}

static void v_acon_dispatch(void)
{
    const acon_op_t *px;
    char c_op;

    /* An empty line is the bare-CR no-op. It answers like everything else --
     * there is no silent path anywhere in the protocol. */
    if (s_ac_line[0] == '\0')
    {
        v_acon_builtin_nop(ACON_OP_NOP, s_ac_line);
        return;
    }

    c_op = s_ac_line[0];

    /* Builtins first, so the escape hatch and introspection can never be shadowed
     * by an application opcode. The handler owns its own parsing: it gets the raw
     * line and calls u8_acon_args() if it wants comma fields. */
    px = px_acon_find(s_x_acon_builtin, (uint8_t) ACON_BUILTIN_COUNT, c_op);
    if (px == NULL)
    {
        px = px_acon_find(g_x_acon_command, g_u8_acon_command_count, c_op);
    }

    if (px != NULL)
    {
        px->pfn_handler(c_op, s_ac_line);
        return;
    }

    v_acon_err(c_op, ACON_ERR_UNKNOWN);
}

void v_automation_console_run(acon_mode_t x_mode)
{
    s_x_mode = x_mode;
    s_u8_quit_requested = 0;

    /* Mute stdout for a SCRIPT session, before the banner, so nothing a job
     * prints on stdout can land between our frames. stderr stays through, but
     * nothing writes async output there. HUMAN mode leaves stdout on -- there is
     * an operator, and i_getline echoes via stderr either way. */
    if (x_mode == ACON_MODE_SCRIPT)
    {
        v_stdout_mute(1);
    }

    /* Before the banner: a host that sees =~,V1 is entitled to assume the
     * session's starting state is already established. */
    ACON_ON_ENTER();

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
            /* '!' rather than '=': the host did not ask to leave. Drive state is
             * untouched, so a soak run started earlier keeps running. */
            v_acon_emit(ACON_SIG_ERR, "%c,TMO", ACON_OP_SESSION);
            ACON_ON_EXIT();
            if (s_x_mode == ACON_MODE_SCRIPT)
            {
                v_stdout_mute(0);
            }
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
    ACON_ON_EXIT();
    if (s_x_mode == ACON_MODE_SCRIPT)
    {
        v_stdout_mute(0);
    }
}

#endif /* ACON_ENABLED */
