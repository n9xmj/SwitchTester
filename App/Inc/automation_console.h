/******************************************************************************
 * automation_console.h
 *
 * Machine-facing command console for the SwitchTester, entered from the debug
 * menu and speaking a framed, line-oriented protocol over the same USART2 the
 * human menu uses.
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
 *      Tokens are <KEY><hexvalue> with no separator inside the token:
 *         L level bitmap    M mode bitmap     R run bitmap
 *         N on-time us      F off-time us     C repeat count    D cycles done
 *         K payload lines   E error/aux value
 *      Bit 0 of any bitmap is SWITCH_A, bit 3 is SWITCH_D.
 *
 *      Control-character opcodes echo in caret notation (0x03 -> "^C"), so a
 *      response frame is always printable ASCII end to end.
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

#endif // AUTOMATION_CONSOLE_H
