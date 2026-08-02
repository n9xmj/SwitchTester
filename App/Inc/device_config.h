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
// Misc
//------------------------------------------------------------------------------

#define DEV_CONFIG_NVM_COMMIT_DELAY_MS                                      5000

#endif //DEVICE_CONFIG_H
