/**
 * @file automation_console_config.h
 * @brief SwitchTester's settings for the App/automation_console module.
 *
 * Copied from App/automation_console/automation_console_config_template.h and
 * edited. The template carries the full commentary on every knob; this file
 * keeps only what a reader of THIS project needs, plus a note wherever the
 * value differs from the template default.
 */

#ifndef AUTOMATION_CONSOLE_CONFIG_H
#define AUTOMATION_CONSOLE_CONFIG_H

#include "platform.h"           // SYSTEM_TICK(), PUMP_POLLING_TASK()
#include "device_config.h"      // PRODUCT_NAME, PLATFORM_NAME, FIRMWARE_VERSION

//------------------------------------------------------------------------------
// Build switch
//------------------------------------------------------------------------------
// The console is how the HIL suite drives this instrument, so compiling it out
// disables the whole test rig. Kept at 1.

#define ACON_ENABLE                     1

//------------------------------------------------------------------------------
// Platform hooks
//------------------------------------------------------------------------------

#define ACON_TICK_MS()                  SYSTEM_TICK()
#define ACON_PUMP()                     PUMP_POLLING_TASK()

//------------------------------------------------------------------------------
// Identity, reported by the V builtin
//------------------------------------------------------------------------------

#define ACON_ID_PRODUCT                 PRODUCT_NAME
#define ACON_ID_PLATFORM                PLATFORM_NAME
#define ACON_ID_FIRMWARE                FIRMWARE_VERSION
#define ACON_ID_BUILD                   BUILD_CONFIG

//------------------------------------------------------------------------------
// Parsing limits
//------------------------------------------------------------------------------
// ABOVE THE TEMPLATE DEFAULT OF 6. The baud sweep (B) takes an index plus a
// host-supplied rate list, and UART_STRESS_MAX_RUNGS is 16 -- but the parser
// caps silently, so a request for more rungs than this quietly runs fewer.
// Raise both together if the ladder ever grows.
//
// Was -DACON_MAX_ARGS=14 on the compiler command line in .cproject until
// 2026-08-12; it belongs here, where the reason for the number can travel with
// it and both build configurations pick it up automatically.

#define ACON_MAX_ARGS                   14u

//------------------------------------------------------------------------------
// Buffer sizes, bytes
//------------------------------------------------------------------------------
// ACON_LINE_MAX is sized well above today's commands so that a future bulk push
// -- an edge-time sequence for a DMA-driven waveform, say -- is a constant
// change rather than a redesign. RAM is not tight here.
//
// Keep DEV_CONFIG_CONSOLE_RX_BUF_SIZE (1024 in device_config.h) comfortably
// above it. A line longer than the RX ring cannot be received at all, however
// the console handles it.
//
// ACON_EMIT_MAX: raised from 128 (2026-08-27) for the event-queue test op's
// F,G reply, which echoes up to 200 payload bytes as 400 hex characters plus
// framing. 512 keeps it symmetric with ACON_LINE_MAX; the TX path drains at
// ~92 chars/ms at 921600, so a full frame is ~5 ms of line time.

#define ACON_LINE_MAX                   512
#define ACON_EMIT_MAX                   512

//------------------------------------------------------------------------------
// Timeouts, milliseconds
//------------------------------------------------------------------------------
// ACON_TX_TIMEOUT_MS mirrors the stdio path's figure and is sized well above
// the ~11 ms to drain a full 1 kB ring at 921600 baud; a 53-byte frame into
// that ring never blocks in practice.

#define ACON_IDLE_TIMEOUT_MS            15000
#define ACON_TX_TIMEOUT_MS              100

#endif // AUTOMATION_CONSOLE_CONFIG_H
