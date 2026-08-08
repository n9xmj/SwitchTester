/******************************************************************************
 * automation_console.h
 *
 * Machine-facing command console: the portable CORE (executive, framing,
 * dispatch, builtins) that every project shares, plus the contract by which an
 * application plugs in its own commands.
 *
 * SOURCE LAYOUT
 *   automation_console.c   -- this core. Portable; no application dependencies.
 *   automation_commands.c  -- the application's command handlers and the
 *                             g_x_acon_command[] table the core dispatches into.
 *
 *   The seam is g_x_acon_command[] (below), exactly as uart_stream takes its
 *   per-target UART table from the application. The core owns the builtins
 *   (quit / list / version / no-op) and guarantees they are always present, so
 *   a command module carries only its own domain ops.
 *
 * It is NOT a test harness -- it is the interface *for* one, and the harness
 * lives on the host. Nor is it test-only: this is how an external host drives
 * the instrument, which includes ordinary operation and not merely self-test.
 *
 * PROTOCOL (full rationale in Docs/planning/automation-console-plan.md)
 *
 *   Host -> device: <op>[,<arg>]...<CR>
 *      <op> is one character. Arguments are comma-separated and every numeric
 *      is HEX, in both directions. CR terminates; LF is discarded wherever it
 *      appears, so a CRLF host produces exactly one frame per command and a
 *      bare CR is a no-op that answers.
 *
 *   Device -> host: one line, first character a sigil:
 *      '='  command succeeded          =S,L9,M4,R4
 *      '!'  command failed             !W,RNG,L9,M4,R4
 *      '+'  payload continuation line, when the header declared K<n>
 *      '*'  asynchronous event         (phase 2; nothing emits these yet)
 *      '#'  not protocol -- ignore     (belt-and-braces; stdout is suppressed)
 *
 *      Tokens are <KEY><hexvalue> with no separator inside the token. The key
 *      set is the application's to define; the core reserves only the session
 *      opcode '~' and the sigils above. Control-character opcodes echo in caret
 *      notation (0x03 -> "^C"), so a response frame is always printable ASCII.
 *
 * MODES
 *   ACON_MODE_SCRIPT   raw byte-at-a-time reader, no echo, idle timeout armed,
 *                      stdout suppressed. Entered from the 0xDA sentinel.
 *   ACON_MODE_HUMAN    i_getline() reader, so echo and line editing come from
 *                      the same code the debug menu uses. No idle timeout --
 *                      there is an operator present, not a host that can die.
 *                      Entered from a debug-menu key.
 *
 *   The dispatcher and the response frames are identical in both modes. Only
 *   the reader differs, so a command tried by hand behaves exactly as it will
 *   from a script.
 *
 * ESCAPE HATCH
 *   Ctrl-C quits from either mode, and a bare CR always answers -- so an
 *   operator who finds the board unexpectedly in the console can identify that
 *   ("=Z" instead of the menu's help) and leave it, in two keystrokes.
 ******************************************************************************/

#ifndef AUTOMATION_CONSOLE_H
#define AUTOMATION_CONSOLE_H

#include <stdint.h>

/*============================================================================
 * PUBLIC API -- session entry (debug_menu) and stdout gating (stdio_retarget)
 *==========================================================================*/

/*----------------------------------------------------------------------------
 * Session sentinels
 *
 * MS bit set, so neither can collide with a printable menu key nor be typed by
 * accident from a terminal. They are a bit-complement pair.
 *--------------------------------------------------------------------------*/

#define ACON_ENTER              0xDAu       /* 0x5A | 0x80 */
#define ACON_EXIT               0xA5u       /* ~0x5A       */

typedef enum
{
    ACON_MODE_SCRIPT = 0,               /* Host-driven: raw, framed, timed out */
    ACON_MODE_HUMAN                     /* Operator-driven: echo and editing   */
}
acon_mode_t;

/**
 * @brief Run the automation console until the host quits, an operator presses
 *        Ctrl-C, or (SCRIPT mode only) the idle timeout expires.
 *
 * Blocking: it owns console input for its lifetime and pumps
 * v_app_polling_task() every spin, so jobs, cycling and the watchdog all keep
 * running. Must be called from inside v_debug_menu_service(), whose re-entry
 * lock is what stops the menu from stealing input meanwhile.
 *
 * @param x_mode  Which reader to use. The caller knows: a sentinel byte came
 *                from a machine, a menu key came from a person.
 */
extern void v_automation_console_run(acon_mode_t x_mode);

/**
 * @brief True while a session is in progress and stdout must stay suppressed.
 *
 * SCRIPT mode only. Human mode leaves stdout enabled -- i_getline() echoes
 * through printf, and there is no host parser to protect.
 */
extern uint8_t u8_automation_console_mutes_stdout(void);

/*============================================================================
 * COMMAND-AUTHOR API -- everything below is for automation_commands.c
 *==========================================================================*/

/* Response sigils: the first byte of every device->host line. Passed as an
 * argument to v_acon_emit(), never embedded in a format string, so "every line
 * carries a sigil" is a property of the signature, not a convention. */
typedef enum
{
    ACON_SIG_OK      = '=',         /* command response, success               */
    ACON_SIG_ERR     = '!',         /* command response, failure               */
    ACON_SIG_PAYLOAD = '+',         /* payload continuation line               */
    ACON_SIG_EVENT   = '*'          /* async event -- phase 2, unused today    */
}
acon_sigil_t;

/* Generic error mnemonics, shared vocabulary a host can switch on. Domain
 * modules may define their own additional codes locally. */
#define ACON_ERR_UNKNOWN        "UNK"       /* no such opcode                  */
#define ACON_ERR_ARGS           "ARG"       /* missing or unparseable field    */
#define ACON_ERR_RANGE          "RNG"       /* parsed, but outside limits      */
#define ACON_ERR_OVERFLOW       "OVF"       /* line or frame too long          */

/* Maximum comma-separated fields u8_acon_args() will split out. */
#ifndef ACON_MAX_ARGS
#define ACON_MAX_ARGS           6u
#endif

/*----------------------------------------------------------------------------
 * Buffer and timing tunables.
 *
 * Fallbacks so the module builds standalone; a project overrides any of them in
 * device_config.h (included ahead of this header in the core .c, so its value
 * wins). ACON_EMIT_MAX bounds one response frame; ACON_LINE_MAX bounds one
 * input line and must not exceed the console RX ring.
 *--------------------------------------------------------------------------*/

#ifndef ACON_LINE_MAX
#define ACON_LINE_MAX           512
#endif
#ifndef ACON_EMIT_MAX
#define ACON_EMIT_MAX           128
#endif
#ifndef ACON_IDLE_TIMEOUT_MS
#define ACON_IDLE_TIMEOUT_MS    15000
#endif
#ifndef ACON_TX_TIMEOUT_MS
#define ACON_TX_TIMEOUT_MS      100
#endif

/*----------------------------------------------------------------------------
 * Command table -- the application-owned seam.
 *
 * A handler receives the opcode and the whole raw line (opcode at [0]). It owns
 * its own parsing: call u8_acon_args() to split comma fields, or read the line
 * directly for a raw-text command. See automation_commands.c for both idioms.
 *--------------------------------------------------------------------------*/

typedef void (*acon_handler_t)(char c_op, char *pc_line);

typedef struct
{
    char            c_op;               /* opcode that selects this handler    */
    acon_handler_t  pfn_handler;        /* handler, or NULL for a help-only row */
    const char     *pc_help;            /* one-line help, shown by the L/? op   */
}
acon_op_t;

/** @brief Application's command table. Defined in automation_commands.c. */
extern const acon_op_t g_x_acon_command[];

/** @brief Number of entries in @ref g_x_acon_command. */
extern const uint8_t   g_u8_acon_command_count;

/*----------------------------------------------------------------------------
 * Author helpers, provided by the core.
 *--------------------------------------------------------------------------*/

/**
 * @brief Emit one response frame. The sigil is the first byte; the terminator
 *        is appended here, so a format string never contains a newline. A frame
 *        that would overflow ACON_EMIT_MAX is dropped and an "!~,OVF" sent in
 *        its place -- never a truncated fragment that would parse as a shorter,
 *        wrong frame.
 */
extern void v_acon_emit(acon_sigil_t x_sigil, const char *pc_format, ...);

/** @brief Bare success frame "=<op>", no payload. */
extern void v_acon_ok(char c_op);

/** @brief Bare failure frame "!<op>,<code>". */
extern void v_acon_err(char c_op, const char *pc_code);

/**
 * @brief Opcode as it appears in a frame: control characters in caret notation
 *        (ESC -> "^["), everything else literal. @p ac_buf is caller storage of
 *        at least 4 bytes; the return value points into it.
 */
extern const char * pc_acon_op_name(char c_op, char ac_buf[4]);

/**
 * @brief Split a line's arguments in place. Field 0 is whatever followed the
 *        opcode; one optional comma directly after the opcode is skipped, so
 *        both "R" and "R," mean no arguments. Commas become NUL terminators, so
 *        the line is consumed -- a raw-text command must read it before calling.
 * @return Field count, capped at @p u8_max.
 */
extern uint8_t u8_acon_args(char *pc_line, char *ap_c_arg[], uint8_t u8_max);

/**
 * @brief Parse one hex field in full. Empty or trailing junk is a failure, so
 *        "3x" is rejected rather than read as 3.
 * @retval 1 parsed; 0 rejected.
 */
extern uint8_t b_acon_arg_u32(const char *pc_arg, uint32_t *p_u32_value);

#endif // AUTOMATION_CONSOLE_H
