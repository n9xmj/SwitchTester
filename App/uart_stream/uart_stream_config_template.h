/**
 * @file uart_stream_config_template.h
 *
 * USAGE TEMPLATE for the uart_stream module (uart_stream.h / uart_stream.c,
 * plus the queue.{c,h} ring buffers that ship inside it).
 *
 * **********************************************************************
 * IMPORTANT: DO NOT #include THIS FILE DIRECTLY IN YOUR APPLICATION.
 * **********************************************************************
 *
 * This is a demonstration template only.
 *
 * Adopting uart_stream in a new project:
 *   1. Copy this file into your application's include directory (e.g. App/Inc/).
 *   2. Rename the copy to "uart_stream_config.h".
 *   3. Change the FAMILY HEADER below if your MCU is not an STM32G0. This is
 *      the one edit that is mandatory on a different part.
 *   4. Copy uart_stream_target_<part>.c to App/Src/ and edit its table to list
 *      the UARTs this build provisions and the NVIC vector each sits on. That
 *      file is the only other per-MCU piece.
 *   5. Adjust the sizes and timeouts below to taste.
 *   6. Wire each bound UART's IRQ handler to b_uart_stream_service_uart() --
 *      see the "IRQ WIRING" section of uart_stream.h.
 *
 * The App/uart_stream/ directory is a reusable component dropped into multiple
 * projects unchanged. Each project supplies its own uart_stream_config.h.
 *
 * NOTE: uart_stream.h and queue.c include "uart_stream_config.h" by name, so
 * this file must exist on the include path for the module to compile. That is
 * deliberate -- it is the same contract FatFs uses for ffconf.h and lwIP for
 * lwipopts.h.
 */

#ifndef UART_STREAM_CONFIG_TEMPLATE_H
#define UART_STREAM_CONFIG_TEMPLATE_H

//------------------------------------------------------------------------------
// FAMILY HEADER (EDIT THIS ON A DIFFERENT STM32 SERIES)
//------------------------------------------------------------------------------
// uart_stream is a HAL-based driver, so it needs the ST header for the series
// it is built against. This include is the ONLY place the module names it.
//
// What it supplies, and who wants it:
//
//   UART_HandleTypeDef, USART_TypeDef, IRQn_Type   uart_stream.h's public API
//   HAL_GetTick, HAL_NVIC_*, HAL_RCCEx_*           uart_stream.c
//   __get_PRIMASK / __set_PRIMASK / __disable_irq  queue.c's critical sections
//                                                  (CMSIS core, pulled in by
//                                                  the family header)
//
// CHANGE THIS LINE FOR A DIFFERENT FAMILY. The default is the STM32G0:
//
//      STM32G0   ->  "stm32g0xx_hal.h"      <-- shipped default
//      STM32G4   ->  "stm32g4xx_hal.h"
//      STM32F4   ->  "stm32f4xx_hal.h"
//      STM32H7   ->  "stm32h7xx_hal.h"          ...and so on.
//
// A wrong family header does not fail quietly -- the types and the register
// definitions both come from here, so it will not compile.
//
// WHY NOT "main.h"? CubeMX's main.h reaches the same family header, and this
// module used to include it for exactly that reason. But main.h is an
// APPLICATION file: it also drags in the project's pin macros and whatever else
// CubeMX put there, and it means "copy this directory into a new project" only
// works if that project has a main.h. Naming the HAL header directly states the
// real dependency and puts the family boundary in one visible place.
//
// STM32CubeIDE CAVEAT -- measured, not theoretical, and it is why YOUR COPY OF
// THIS FILE SHOULD SAY "main.h" WHERE THIS ONE SAYS "stm32g0xx_hal.h".
//
// Naming the family header from a config header that lives in App/Inc wrecks
// CDT's indexer: 3 unresolved inclusions / 0.13% unresolved names becomes
// 21 / 2.1% -- thousands of phantom errors in the Problems view against an
// image that compiles byte-identical. The compiler never cares; CDT cannot
// resolve the chain from that context, and the USE_HAL_DRIVER guard below does
// NOT rescue it, because a real translation unit defines that macro, so the
// include fires and fails anyway. Only routing through main.h works, because
// CDT has already resolved main.h in main.c's context.
//
// main.h contains nothing but #include "stm32g0xx_hal.h", so the two are
// identical to the compiler, and it costs nothing in portability: this file is
// YOURS, not part of the vendored module, and uart_stream.{c,h} and queue.c
// name only "uart_stream_config.h" either way. Both G0B1 projects in this
// family of repos use main.h for exactly this reason.
//
// This TEMPLATE keeps the family header anyway, because it is the honest
// statement of what the module depends on, and because a template is read far
// more often than it is compiled.
//
// There is a SECOND family boundary in the module, and it is not here:
// u32_uart_stream_kernel_clock() in uart_stream.c lists which instances have an
// independent clock mux. A port to another series revisits that selector list
// as well as this include.

// The guard is not decoration. This template is an ORPHAN header -- nothing
// includes it, by design -- so CubeIDE's CDT indexer parses it with no
// translation-unit context and therefore none of the -D flags the build passes.
// Unguarded, stm32g0xx.h then hits its "select first the target STM32xx device"
// #error and the ENTIRE HAL chain fails to index: measured at 17 extra
// unresolved inclusions and 0.17% -> 2.2% unresolved names on this project,
// i.e. a Problems view full of phantoms against an image that compiles clean.
// USE_HAL_DRIVER is on the command line of every CubeIDE project and absent
// from an orphan index pass, so it distinguishes the two exactly.
//
// The guard is NOT what makes the include safe in a real config header -- see
// the CubeIDE caveat above. It only stops THIS orphan file from poisoning the
// index. A copy that uses main.h does not need it.

#if defined(USE_HAL_DRIVER)
#include "stm32g0xx_hal.h"
#else
#error "uart_stream requires the STM32 HAL -- build with -DUSE_HAL_DRIVER"
#endif

//------------------------------------------------------------------------------
// Instance table size (EDIT THIS)
//------------------------------------------------------------------------------
// How many UARTs may be bound with x_uart_stream_init() at once. Each costs one
// instance slot plus whatever ring buffers that call passes in; the slot itself
// is small, so size this to the number of UARTs the build actually uses rather
// than trimming it hard.

#define UART_STREAM_MAX_INSTANCES           6

//------------------------------------------------------------------------------
// Flush timeouts, milliseconds (EDIT THESE)
//------------------------------------------------------------------------------
// UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS bounds the outer wait for the TX ring to
// empty, used by the no-argument v_uart_stream_tx_flush() wrapper. Size it
// against the time to drain a full ring at the slowest rate you run.
//
// UART_STREAM_FLUSH_TC_TIMEOUT_MS is a FLOOR, not the bound. Once the ring is
// empty the flush still waits for the hardware TC flag, and that wait is
// computed per flush from the rate actually in effect -- 12 bit-times plus 2 ms
// for tick granularity. This value is the minimum, and the fallback used when
// the rate cannot be read. There is no need to raise it for slow instances; the
// calculation already covers them, down to 1200 baud and below.

#define UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS  50U
#define UART_STREAM_FLUSH_TC_TIMEOUT_MS     2U

//------------------------------------------------------------------------------
// Blocking-write deadline, milliseconds (EDIT THIS)
//------------------------------------------------------------------------------
// Default deadline for the blocking TX calls that do not take one explicitly.
// These block only while the ring is FULL, so this is a backstop against a
// wedged peripheral rather than a figure normal traffic ever approaches.

#define UART_STREAM_TX_BLOCK_TIMEOUT_MS     100U

#endif // UART_STREAM_CONFIG_TEMPLATE_H
