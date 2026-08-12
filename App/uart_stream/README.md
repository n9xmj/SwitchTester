# `uart_stream` — adoption guide

Interrupt-driven, non-blocking UART TX/RX on ring buffers, driven by a register-level ISR
rather than the HAL's. Built for an always-on streaming console: bursty ANSI redraws, log
floods, and a host that can talk faster than the main loop polls.

This file is **how to drop the module into a project**. For *why* it is shaped this way,
see [`portable-apis-strategy.md`](../../Docs/planning/portable-apis-strategy.md).

---

## Files

| File | Owner | Notes |
|---|---|---|
| `uart_stream.c` / `.h` | **vendored** | The driver. |
| `queue.c` / `.h` | **vendored** | The ring buffers. Pure C, ships inside the module. |
| `uart_stream_config_template.h` | **vendored** | Copy → adopter's `uart_stream_config.h`. |
| `App/Inc/uart_stream_config.h` | **adopter** | Family header + knobs. **Required.** |
| `App/Src/uart_stream_target_<part>.c` | **adopter** | Handle→vector table. **Required.** |

Copy `App/uart_stream/` verbatim; the four source files are byte-identical across every
project using them.

## Adopting it, in five steps

1. Copy `App/uart_stream/` in and put it on the include path.
2. Copy `uart_stream_config_template.h` → `App/Inc/uart_stream_config.h`. **Change the
   family header** — that is the one mandatory edit on a different part.
3. Write `App/Src/uart_stream_target_<part>.c` (see below). Name it for the MCU.
4. Hook the NVIC vectors in the CubeMX-generated `*_it.c`.
5. Bind at run time with `x_uart_stream_init()`, then route stdio through the TX/RX calls.

## Configuration

| Knob | Default | What it does |
|---|---|---|
| *family header* | `stm32g0xx_hal.h` | The HAL header for your series. Supplies `UART_HandleTypeDef`, `USART_TypeDef`, `IRQn_Type`, the `HAL_*` calls, and the CMSIS intrinsics `queue.c` uses. |
| `UART_STREAM_MAX_INSTANCES` | 6 | Simultaneously bound UARTs. |
| `UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS` | 50 | Outer bound on draining the TX ring. |
| `UART_STREAM_FLUSH_TC_TIMEOUT_MS` | 2 | **Floor**, not the bound — see below. |
| `UART_STREAM_TX_BLOCK_TIMEOUT_MS` | 100 | Default deadline for blocking TX. |

### What a real config looks like

```c
/* App/Inc/uart_stream_config.h */
#include "main.h"           /* -> stm32g0xx_hal.h; see the indexer gotcha below */

#define UART_STREAM_MAX_INSTANCES           6
#define UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS  50U
#define UART_STREAM_FLUSH_TC_TIMEOUT_MS     2U
#define UART_STREAM_TX_BLOCK_TIMEOUT_MS     100U
```

That is the entire file. On a G4 the include becomes `"stm32g4xx_hal.h"` (or that
project's `main.h`); on an F4, `"stm32f4xx_hal.h"`. Nothing else changes between families
*in this file* — but see the two-family-boundaries gotcha, because something else does
change elsewhere.

## The target table

The module does not know your part. You tell it which UARTs exist and which NVIC vector
each sits on:

```c
#include "uart_stream.h"        /* NOT "usart.h" — see gotchas */

extern UART_HandleTypeDef huart1   __attribute__((weak));
extern UART_HandleTypeDef huart2   __attribute__((weak));
extern UART_HandleTypeDef hlpuart2 __attribute__((weak));

const uart_stream_target_t g_x_uart_stream_target[] =
{
    { &huart1,   USART1_IRQn         },
    { &huart2,   USART2_LPUART2_IRQn },   /* vectors may be shared */
    { &hlpuart2, USART2_LPUART2_IRQn },
};
const uint8_t g_u8_uart_stream_target_count =
    (uint8_t) (sizeof(g_x_uart_stream_target) / sizeof(g_x_uart_stream_target[0]));
```

**Weak handles are what let one file serve every build of that part.** A UART CubeMX
provisioned resolves to the real object; one it did not resolves to NULL at link time with
no error, and a NULL entry can never match a validated handle, so it is inert without a
guard.

**Listing a UART is not claiming it.** The table is read from exactly one place — the
handle→vector lookup inside `x_uart_stream_init()`, for the single handle being bound. An
entry you never bind is never matched, never NVIC-enabled, never touched. That is what
makes it safe to list UARTs the HAL drives with DMA, which LED_Strip does deliberately.

## IRQ wiring

One `b_uart_stream_service_uart()` call per UART on that vector, in the **upper** USER CODE
block, then `return`:

```c
void USART2_LPUART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_LPUART2_IRQn 0 */
  b_uart_stream_service_uart(&huart2);
  b_uart_stream_service_uart(&hlpuart2);
  return;
  /* USER CODE END USART2_LPUART2_IRQn 0 */
  HAL_UART_IRQHandler(&huart2);
  ...
```

The `return` bypasses the generated `HAL_UART_IRQHandler()` list, which **must not** run for
a bound UART. Unbound UARTs still reach the HAL, because the service call forwards to it
internally.

On a part where every UART has its own vector (STM32G4), there is nothing to chain — one
call, no `return` subtlety beyond the same pattern.

## Using it

```c
/* Rings are caller-owned. 1 KB each is a reasonable console default: the RX ring
 * must hold a whole command line, and the TX ring converts blocking time into
 * queued time for the largest burst you emit (a menu redraw, an ANSI repaint). */
static uint8_t s_au8_rx[1024];
static uint8_t s_au8_tx[1024];
static uart_stream_h_t s_h_console;

void v_console_init(void)
{
    s_h_console = x_uart_stream_init(&huart2,
                                     (uint16_t) sizeof(s_au8_rx), s_au8_rx,
                                     (uint16_t) sizeof(s_au8_tx), s_au8_tx);
    if (s_h_console == UART_STREAM_HANDLE_INVALID)
    {
        /* Not in the target table, already bound, or the pool is full. */
        Error_Handler();
    }
}
```

Then: `b_uart_stream_tx_byte()`, `b_uart_stream_tx_byte_blocking()`,
`u16_uart_stream_tx_multi{,_blocking}()`, `i16_uart_stream_rx_byte()` (returns `-1` when
empty), `u16_uart_stream_rx_multi()`, `v_uart_stream_tx_flush{,_timeout}()`,
`u32_uart_stream_{get,set}_baud()`, plus queue-depth, error-count and ISR-service-count
accessors.

## Port

**No port source.** Everything the module needs from the application is either a macro in
`uart_stream_config.h` or the target table above.

## Gotchas worth knowing before they cost you time

**There are TWO family boundaries, and the config header is only the first.** The second is
`u32_uart_stream_kernel_clock()` in `uart_stream.c` — the list of which instances have an
independent clock mux. The baud getter/setter depend on it. A port to another series must
revisit that list as well as the include.

**Do NOT `#include "usart.h"` in the target file.** Its non-strong extern for a provisioned
handle collides with the weak declaration in that translation unit. The weak decls stand
alone; `usart.c`'s strong definitions satisfy them at link time.

**Never call `HAL_UART_*` on a bound UART.** Binding sets `gState`/`RxState` to
`HAL_UART_STATE_BUSY` precisely so such calls fail fast with `HAL_BUSY` rather than fight
this driver for the data register.

**One producer, one consumer, per queue.** TX may be written from exactly one context; RX
read from exactly one. The lock-free reasoning depends on it.

**`UART_STREAM_FLUSH_TC_TIMEOUT_MS` is a floor, not the bound.** Once the ring drains, the
wait for hardware TC is computed per flush from the rate actually in effect (12 bit-times +
2 ms). Do not raise it for slow instances — that is already handled, down to 1200 baud and
below. A fixed constant is wrong at both ends: 2 ms is ~184 character times at 921600, but
*shorter than one character* below about 4800.

**The baud setter is unguarded by design.** No TX drain, no RX flush. `BRR` must not change
mid-character, so call `v_uart_stream_tx_flush_timeout()` first if you care — and discard
the RX ring afterwards, since bytes received at the old rate are framing garbage. It returns
the rate actually achieved, which is not what you asked for: `BRR` is an integer divisor, so
at 64 MHz a requested 921600 lands on 927536 (+0.64%).

**CubeIDE indexer trap — measured, not theoretical.** Writing the family header directly
(`#include "stm32g0xx_hal.h"`) in a config header under `App/Inc` takes CDT from 3
unresolved inclusions / 0.13% unresolved names to 21 / 2.1% — thousands of phantom errors in
the Problems view against an image that compiles byte-identical. A `USE_HAL_DRIVER` guard
does **not** rescue it, because a real translation unit defines that macro. Write
`#include "main.h"` instead; it contains nothing but that same include and CDT has already
resolved it in `main.c`'s context. The *template* keeps the family header because it states
the real dependency, guarded, since as an orphan header it would otherwise poison the index
by itself.

**Not every UART has a FIFO.** On the STM32G0B1 only USART1/2/3 and LPUART1/2 do; USART4/5/6
do not, and they lose bytes above roughly 403200 baud when the main loop cannot drain fast
enough. That is a hardware property, not a driver bug — size your rings and your rate
accordingly.
