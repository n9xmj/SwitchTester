/**
 * @file    device_config.h
 * @brief   Product options and constant parameter settings
 */
#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging_config.h"
#include "main.h"
#include "platform.h"
#include "globals.h"

//------------------------------------------------------------------------------
// Build options
//------------------------------------------------------------------------------
//
// Turn off build-time options if DEBUG is not defined (via the -DDEBUG
// compiler command line option). Logging answers DEBUG for itself, in
// logging_config.h.

#ifndef DEBUG
#undef DEBUG_MENU
#define DEBUG_MENU                      0
#endif

// Debug menu system enable.
// Set this to allow the debug menu system to be included. Independent of the
// logging configuration in logging_config.h.

#ifndef DEBUG_MENU
#define DEBUG_MENU                      1
#endif

//------------------------------------------------------------------------------

#define FIRMWARE_VERSION                "0.1.0.0.0"
#define PRODUCT_NAME                    "SwitchTester"
#define PLATFORM_NAME                   "NUCLEO-G0B1RE"

//------------------------------------------------------------------------------
// NVM pool ownership label -- written into the pool header and checked at init.
//
// Derived from PRODUCT_NAME on purpose: a project forked from this one changes
// PRODUCT_NAME as a matter of course and inherits a distinct pool label for
// free, which is exactly the property the check depends on. The .nvmdata
// section is NOLOAD and survives reflashing, so two projects sharing a board
// and a label will read each other's pools.
//
// Must be at most NVM_LABEL_MAX_LENGTH (16) characters; asserted in app_main.c.
//------------------------------------------------------------------------------

#define NVM_POOL_LABEL                  PRODUCT_NAME

#if defined(DEBUG)
#define BUILD_CONFIG                    "DEBUG"
#else 
#define BUILD_CONFIG                    "RELEASE"
#endif

//------------------------------------------------------------------------------
// Console UART (uart_stream) ring buffer sizes, bytes
//------------------------------------------------------------------------------
//
// TX is sized to absorb the largest single burst the application emits without
// stalling the main loop -- _write() only blocks once the ring is full, so the
// ring converts blocking time into queued time. The biggest burst here is the
// switch-cycling menu redraw at roughly 1.2 kB; at 921600 baud a 256-byte ring
// would stall ~10 ms on that, a 1 kB ring ~2 ms.
//
// RX has to cover what arrives while the application is not calling _read().
//
// It MUST also be comfortably larger than ACON_LINE_MAX (defined in
// automation_console_config.h): the automation
// console reads one byte per main-loop pass through newlib's getchar(), which
// cannot keep up with a sustained 921600-baud stream, so a whole command line
// has to be able to sit in the ring while the console drains it. Raised from
// 256 to 1024 on 2026-08-03 after the HIL suite measured ~19% byte loss on a
// 402-byte burst -- at 256 the ring was SMALLER than the longest legal line,
// so a maximal frame could not be received at all. The dropped bytes included
// the terminating CR, which is the ugly part: the line never completed, and
// the error surfaced against the NEXT command instead.
//
// Note the queue uses a leave-one-slot-empty scheme, so usable capacity is one
// byte less than the size given here -- 1023 against a 512-byte line limit.

#define DEV_CONFIG_CONSOLE_TX_BUF_SIZE                                      1024
#define DEV_CONFIG_CONSOLE_RX_BUF_SIZE                                      1024

//------------------------------------------------------------------------------
// Automation console
//------------------------------------------------------------------------------
//
// Minimum cycle period accepted from the automation console: on + off must be
// at least this. It applies ONLY to host-commanded cycling. The debug menu is
// deliberately exempt and keeps switch_out.h's much lower floors, because
// feeding it absurd values to find where the system breaks is a wanted
// experiment on a bench instrument. See automation-console-plan.md (S10).

#define ACON_MIN_CYCLE_PERIOD_US                                           50000

// The console core's own settings -- build switch, ACON_MAX_ARGS, buffer sizes,
// timeouts -- are NOT here. It is a vendored module and owns a settings file:
// App/Inc/automation_console_config.h, copied from the template in
// App/automation_console/. ACON_MIN_CYCLE_PERIOD_US above is different in kind:
// it is an application command's limit, not the module's, so it stays here.

//------------------------------------------------------------------------------
// Misc
//------------------------------------------------------------------------------

#define DEV_CONFIG_NVM_COMMIT_DELAY_MS                                      5000

#endif //DEVICE_CONFIG_H
