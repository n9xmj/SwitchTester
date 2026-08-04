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

#include "debug_config.h"
#include "main.h"
#include "platform.h"
#include "globals.h"

//------------------------------------------------------------------------------

#define FIRMWARE_VERSION                "0.1.0.0.0"
#define PRODUCT_NAME                    "SwitchTester"
#define PLATFORM_NAME                   "NUCLEO-G0B1RE"

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
// It MUST also be comfortably larger than ACON_LINE_MAX: the automation
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

// Host command line, bytes. Sized well above today's commands so that a future
// bulk push -- an edge-time sequence for a DMA-driven waveform, say -- is a
// constant change rather than a redesign. RAM is not tight here.
//
// Keep DEV_CONFIG_CONSOLE_RX_BUF_SIZE comfortably above this. A line longer
// than the RX ring cannot be received at all, however the console handles it.

#define ACON_LINE_MAX                                                        512

// Response frame assembly buffer, bytes. Responses are bounded by the frame
// grammar rather than by input size; the longest is the parameter getter with
// every field at full hex width, at 53 bytes.

#define ACON_EMIT_MAX                                                        128

// Idle timeout. Applies in SCRIPT mode only -- human mode has an operator at
// the terminal rather than a host that can die. Reset by ANY received byte, so
// a keep-alive works even mid-line.

#define ACON_IDLE_TIMEOUT_MS                                               15000

// Deadline for pushing one frame into the TX ring. Mirrors the stdio path's
// figure and is sized well above the ~11 ms to drain a full 1 kB ring at
// 921600 baud; a 53-byte frame into that ring never blocks in practice.

#define ACON_TX_TIMEOUT_MS                                                   100

//------------------------------------------------------------------------------
// Misc
//------------------------------------------------------------------------------

#define DEV_CONFIG_NVM_COMMIT_DELAY_MS                                      5000

#endif //DEVICE_CONFIG_H
