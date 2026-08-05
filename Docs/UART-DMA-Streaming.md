# UART DMA streaming — banked technique note

**Status: NOT PLANNED. Nothing here is scheduled and `uart_stream` is not going
to be refactored for it in the near future.** This exists so the technique — and
specifically the one non-obvious register that makes it work — is not
rediscovered from scratch if the need ever arrives.

Written 2026-08-04, out of the UART performance investigation recorded in
[`SwitchTester-Design.md`](SwitchTester-Design.md) § "UART performance envelope".

---

## Why this might ever matter

`uart_stream` is interrupt-per-byte. That is fine on every instance with a
hardware FIFO — USART1/2/3 and LPUART1/2 all run 921600 losslessly at 90–96% of
line rate. It is *not* fine on **USART4/5/6, which have no FIFO on this part**:
the ISR must read `RDR` within one character time (10.85 µs at 921600), and
USART5 shares `USART3_4_5_6_LPUART1_IRQn` with four other UARTs, so every
received byte also costs four idle `HAL_UART_IRQHandler()` passes. Measured
ceiling on those instances: **230400 clean, 460800 marginal, 921600 hopeless.**

DMA is the only way past that, because it removes the per-byte interrupt
entirely rather than making it cheaper. It also sidesteps the shared vector —
DMA channels have their own.

## The part that is not obvious

The usual reason people conclude "STM32 DMA can't do variable-length streaming"
is that they go looking for a readable *memory pointer* and correctly find that
there isn't one. `CMAR` holds the base address and never moves; the internal
working pointer is not exposed.

**But `CNDTR` is.** It is the remaining-transfer count, it decrements as each
transfer completes, and it is readable at any instant while the channel is
running:

```c
typedef struct {                      /* stm32g0b1xx.h */
  __IO uint32_t CCR;                  /* configuration        */
  __IO uint32_t CNDTR;                /* <-- live, readable   */
  __IO uint32_t CPAR;                 /* peripheral address   */
  __IO uint32_t CMAR;                 /* memory base -- static */
} DMA_Channel_TypeDef;
```

So the write position in a circular buffer is simply:

```c
head = buffer_size - (DMAx_Channely->CNDTR);
```

`CNDTR` is safe to *read* while the channel is enabled; it must not be *written*
unless the channel is disabled. Same register on F0/F1/F3/L0/G0/G4; the
stream-based controllers on F2/F4/F7/H7 spell it `SxNDTR` and behave identically.

## The RX pattern

One circular DMA channel into one ring buffer, started once and never stopped.
No half-buffers, no double-buffering, no ping-pong.

- `head` comes from `CNDTR` as above; `tail` is software-owned.
- `available = (head - tail) mod buffer_size`.
- Three wake-up sources, none of which is per-byte:
  - **USART IDLE line interrupt** — fires when the line goes quiet, which is
    exactly "a variable-length burst just ended". This is what makes the
    variable-length case work without timeouts or padding.
  - **DMA half-transfer** and **transfer-complete** — service the ring before it
    laps the reader. These are throughput guards, *not* how length is measured.

That last distinction is the one worth remembering: HT/TC are not the mechanism,
they are backstops. Treating them as the mechanism is what forces the awkward
two-half-buffer design and makes variable-length feel impossible.

## The TX side

Easier. Hand DMA a length from the ring and take transfer-complete to start the
next chunk. The only wrinkle is a ring wrap, which becomes two transfers.

## Costs and constraints on this part

- **Channels.** G0B1 has 12 (DMA1 ×7, DMA2 ×5). Two per fully-streamed UART caps
  it at roughly five or six, less whatever else wants DMA. Note the arbitrary
  waveform generator idea (**W7** in
  [`planning/switch-cycling-plan.md`](planning/switch-cycling-plan.md)) also
  wants channels, up to four.
- **DMAMUX routing** must be configured per channel.
- **`uart_stream`'s public API would not need to change.** Callers already see a
  ring with byte-at-a-time and multi-byte accessors; whether the ring is filled
  by an ISR or by DMA is behind that boundary. The work is internal — an
  alternative fill path plus a per-instance flag for which one an instance uses.
- **Mixed mode is likely the right shape** if this is ever built: DMA for the
  instances that need the throughput, interrupt for the rest. Committing every
  instance to DMA would spend channels on UARTs that are already at line rate.

## Before building it, check it is needed

Nothing in this project requires a FIFO-less instance above 230400 today. The
loopback stress command (`U`) will say whether that is still true:

```bash
python scripts/hil/test_acon.py --port COM3 -k uart
```
