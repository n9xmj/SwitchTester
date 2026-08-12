/**
 * @file automation_console_config_template.h
 *
 * USAGE TEMPLATE for the automation console module
 * (automation_console.h / automation_console.c).
 *
 * **********************************************************************
 * IMPORTANT: DO NOT #include THIS FILE DIRECTLY IN YOUR APPLICATION.
 * **********************************************************************
 *
 * This is a demonstration template only.
 *
 * Adopting the automation console in a new project:
 *   1. Copy this file into your application's include directory (e.g. App/Inc/).
 *   2. Rename the copy to "automation_console_config.h".
 *   3. Edit the two PLATFORM HOOKS below so they reach your tick source and
 *      your cooperative polling hook. Nothing else in the module needs the
 *      application, and these are the whole of it.
 *   4. Edit the identity strings, then the sizes and timeouts, to taste.
 *   5. Write your command handlers and the g_x_acon_command[] table in
 *      App/Src/automation_commands.c -- baseline that on the copy shipped
 *      alongside this file.
 *
 * The App/automation_console/ directory is a reusable component dropped into
 * multiple projects unchanged. Each project supplies its own
 * automation_console_config.h.
 *
 * NOTE: automation_console.h includes "automation_console_config.h" by name, so
 * this file must exist on the include path for the module to compile. That is
 * deliberate -- it is the same contract FatFs uses for ffconf.h and lwIP for
 * lwipopts.h.
 *
 * There is no port .c file. Everything the console needs from the application
 * is either a macro defined here or the g_x_acon_command[] table, so the module
 * has no port half.
 */

#ifndef AUTOMATION_CONSOLE_CONFIG_TEMPLATE_H
#define AUTOMATION_CONSOLE_CONFIG_TEMPLATE_H

//------------------------------------------------------------------------------
// Project headers this config draws on (EDIT THIS)
//------------------------------------------------------------------------------
// The module includes only this file, so anything the hooks and identity
// strings below are written in terms of has to be pulled in here.

#include "platform.h"           // SYSTEM_TICK(), PUMP_POLLING_TASK()
#include "device_config.h"      // PRODUCT_NAME, PLATFORM_NAME, FIRMWARE_VERSION

//------------------------------------------------------------------------------
// Build switch
//------------------------------------------------------------------------------
// 1 compiles the console in. 0 compiles it out entirely -- both module bodies
// drop to nothing and v_automation_console_run() becomes an inert inline stub,
// so no call site needs an #ifdef.

#define ACON_ENABLE                     1

//------------------------------------------------------------------------------
// PLATFORM HOOKS (EDIT THESE)
//------------------------------------------------------------------------------
// ACON_TICK_MS()
//   A free-running millisecond counter. Only ever used as a difference, and
//   unsigned wrap handles rollover, so any width up to 32 bits works.
//   Omitting it is legal: the module warns at compile time and the SCRIPT-mode
//   idle timeout is disabled, which means a host that dies mid-session wedges
//   the console until the board is reset.
//
// ACON_PUMP()
//   Cooperative polling hook, called every spin of the SCRIPT reader so jobs,
//   cycling and the watchdog keep running while the console waits for input.
//   Omitting it is legal and warns; the symptom is a board that appears to hang
//   for the duration of a session.

#define ACON_TICK_MS()                  SYSTEM_TICK()
#define ACON_PUMP()                     PUMP_POLLING_TASK()

//------------------------------------------------------------------------------
// Identity, reported verbatim by the V builtin (EDIT THESE)
//------------------------------------------------------------------------------
// Enough for a host to pin exactly what it is talking to. Any of them may be
// left undefined, in which case that field reports "?".

#define ACON_ID_PRODUCT                 PRODUCT_NAME
#define ACON_ID_PLATFORM                PLATFORM_NAME
#define ACON_ID_FIRMWARE                FIRMWARE_VERSION
#define ACON_ID_BUILD                   BUILD_CONFIG

//------------------------------------------------------------------------------
// Parsing limits (EDIT THESE)
//------------------------------------------------------------------------------
// Maximum comma-separated fields u8_acon_args() will split out of one line.
// This is a ceiling on the widest command in the project's own table -- the
// core's builtins take none. A caller asking for more than this silently gets
// this many, so size it against the widest handler and not against typical use.

#define ACON_MAX_ARGS                   6u

//------------------------------------------------------------------------------
// Buffer sizes, bytes (EDIT THESE)
//------------------------------------------------------------------------------
// Both are static buffers in the core, and together they are the module's whole
// RAM cost.
//
// ACON_LINE_MAX bounds one host command line. It MUST NOT exceed the console
// UART's RX ring: a line longer than the ring cannot be received at all,
// however the console handles it, and the loss takes the terminating CR with
// it, so the failure surfaces against the NEXT command.
//
// ACON_EMIT_MAX bounds one response frame. Responses are bounded by the frame
// grammar rather than by input size. A frame that would overflow is dropped and
// an "!~,OVF" sent in its place, never a truncated fragment that would parse as
// a shorter, wrong frame.

#define ACON_LINE_MAX                   512
#define ACON_EMIT_MAX                   128

//------------------------------------------------------------------------------
// Timeouts, milliseconds (EDIT THESE)
//------------------------------------------------------------------------------
// ACON_IDLE_TIMEOUT_MS applies in SCRIPT mode only -- human mode has an
// operator at the terminal rather than a host that can die. Reset by ANY
// received byte, so a keep-alive works even mid-line. Needs ACON_TICK_MS().
//
// ACON_TX_TIMEOUT_MS is the deadline for pushing one frame into the TX ring.

#define ACON_IDLE_TIMEOUT_MS            15000
#define ACON_TX_TIMEOUT_MS              100

#endif // AUTOMATION_CONSOLE_CONFIG_TEMPLATE_H
