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
// RX only has to cover what arrives while the application is not calling
// _read(). A large TX ring shortens that window considerably (less time stuck
// inside _write), so RX can stay small. It is NOT sized for a host that streams
// continuously through a full console dump -- uart_stream's per-instance error
// counter is the tell if that ever happens.
//
// Note the queue uses a leave-one-slot-empty scheme, so usable capacity is one
// byte less than the size given here.

#define DEV_CONFIG_CONSOLE_TX_BUF_SIZE                                      1024
#define DEV_CONFIG_CONSOLE_RX_BUF_SIZE                                       256

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

#define ACON_LINE_MAX                                                        256

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
