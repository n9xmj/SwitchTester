# uart_stream Integration — implementation plan (decision log)

**Feature:** port the user's `uart_stream` interrupt-driven UART manager into
SwitchTester and run it in **HAL-coexistence mode** — `uart_stream` owns
interrupt processing for the UARTs it binds, HAL keeps everything else, and the
two share the vector table without fighting.

**Home:** `App/uart-stream/{uart_stream.c,uart_stream.h,queue.c,queue.h}` —
sources and headers together, queue API alongside. `App` is a whole-folder
`sourcePath` in `.cproject`, so the module is **in the build** as of 2026-08-01.

**Upstream:** `C:\STM32\CubeSource\LED_Strip_Controller_G474\App\uart-stream\`
(shipped there as G11 on USART2 @ 921600). Design reference:
that repo's `Docs/planning/uart_stream-port-notes.md` — **not** copied here.

**Parent spec:** [`../SwitchTester-Design.md`](../SwitchTester-Design.md).
**Planning model:** [`decision-log-model.md`](decision-log-model.md).

**Status:** **v1 WORKING on the bench (2026-08-02)** — console (USART2) bound and
serving stdio in single-instance mode, verified on hardware. All 20 design rows
green. Remaining work is the deferred rows only: skeleton promotion (**T1**),
host-side REPL (**T2**), loopback stress rig (**T3**).

**Working mode:** resolve OPEN items in chat **one at a time**; update the table
and the matching detail section as each lands. The agent records a leaning as 🟡
and locks only on confirmation.

---

## Brief

SwitchTester's console is currently a polled, blocking stdio retarget:
`_write` calls `HAL_UART_Transmit`, `_read` calls `HAL_UART_Receive(…, 1, 0)`.
That is fine for a human at a terminal and unfit for a scripted host. The
planned HIL/automation backdoor drives the *same* USART2 the menu uses, so a
deterministic script interface needs interrupt-driven RX and TX rings underneath
it — which is what `uart_stream` provides.

The larger goal is not project-specific: `uart_stream` should become a permanent
part of the user's reusable skeleton alongside `jobs` and `nvmparams`, which
means the HAL-coexistence problem has to be solved properly rather than by
convention. HAL's UART IRQ handler actively disables reception on error, so any
UART that `uart_stream` owns must keep HAL out of its interrupt path entirely,
while UARTs HAL owns must continue to work — including when both sit on the
**same NVIC vector**, which STM32G0B1 forces.

---

## The Big Board

| ID | Status | Subject |
|----|--------|---------|
| **D1** | 🟢 | Vector hooking — one `b_uart_stream_service_uart()` call per UART instance in USER CODE block 0, then `return` |
| **D2** | 🟢 | Service function takes a `UART_HandleTypeDef *`; app-provided target table carries handle + IRQn |
| **D3** | 🟢 | `stdio_retarget.c` routes through `uart_stream`; `_write` blocks with timeout |
| **D4** | 🟢 | Console rings: TX 1024, RX 256, as `DEV_CONFIG_CONSOLE_*_BUF_SIZE` |
| **D5** | 🟢 | Style — Doxygen on every function and internal var, Hungarian naming, consistent within the module |
| **D6** | 🟢 | Port base is **ee_fw's version** |
| **S1** | 🟢 | Error policy: count and clear ORE/FE/NE/PE, never disable the UART |
| **S2** | 🟢 | Shared-vector delegation — subsumed by **D1**/**D2**; no if-chain in the vector body |
| **S3** | 🟢 | Bounded-timeout blocking, **no** `v_app_polling_task()` pumping (re-entrancy) |
| **S4** | 🟢 | Bind poisons `gState`/`RxState` so HAL APIs return `HAL_BUSY` |
| **S5** | 🟢 | Reject on error; send a structured error response to the host |
| **S6** | 🟢 | TX blocking-path deadlock — fixed by re-arming `TXEIE` inside the fill loop |
| **I1** | 🟢 | Module lives at `App/uart-stream/` (sources + headers together, queue API alongside) |
| **I2** | 🟢 | `queue_t` control blocks embedded in the instance; byte buffers keep the dual malloc/caller-owned pattern |
| **I3** | 🟢 | Register macros verified present on G0B1 — ISR body ports unchanged |
| **I4** | 🟢 | `e_uart_stream_get_irqn()` deleted — the target table carries `e_irqn`, dissolving the G0B1 mismatch |
| **I5** | 🟢 | UART vectors at NVIC priority 2 — below TIM2 (0) and SysTick (1), above background (3) |
| **I6** | 🟢 | FIFOs enabled in CubeMX for every {LP}USART that has them |
| **I7** | 🟢 | Common ISR optimisation — lock-free on the ISR side, scoped masking in the foreground |
| **I8** | 🟢 | NVIC enable timing — `uart_stream` enables its own vector at bind; deinit must not disable a shared one |
| **T1** | 🔵 | Promote to `G0B1_Skeleton` — desired, deferred until fully tested |
| **T2** | 🔵 | Host-side script/REPL — deferred until single-instance console is proven |
| **T3** | 🔵 | Loopback stress rig — deferred until single-instance console works |

---

## Wish list (v2+)

| ID | Subject | Notes |
|----|---------|-------|
| ~~**W1**~~ | ~~DMA-backed TX for large bursts~~ | 🔵 **Do not pursue.** HAL's bounded UART-DMA already does this well and is in production use on the LED_Strip WS2812B UARTs — which `uart_stream` does not own. The coexistence design (**D1**/**D2**) is what makes that split possible: HAL keeps DMA-driven UARTs, `uart_stream` takes streaming ones. Duplicating DMA inside `uart_stream` would compete with a working solution for no gain. |
| **W2** | Second bound instance | No second UART is pinned out on this board today |
| **W3** | Cooperative (non-spinning) blocking variants | See **S3** — may become a v1 requirement rather than a wish |
| **W4** | Line-oriented RX helper for the parser | Overlaps the HIL backdoor's own design |
| **W5** | **Built-in per-target tables** — ship a table for each commonly-used MCU so an adopter need not hand-build one | See the detail note below; four design points already worked out |

---

### W5 — Built-in per-target tables (wish-list detail)

Ship a ready-made @ref uart_stream_target_t table for each MCU the author
commonly uses — STM32G0B0, STM32G0B1, STM32G474 — selected by the part macro
CubeMX already passes on every compile (`-DSTM32G0B1xx` here; the CMSIS device
headers carry the same names). An adopter on a supported part then wires only
the `*_it.c` vectors and skips the table entirely.

Four things have to be right, and three of them are non-obvious:

1. **Handles must be weak references.** A built-in table naming all eight G0B1
   UARTs will not *link* in a project that enables only USART2, because
   `&huart3` has no definition. Declaring them
   `extern UART_HandleTypeDef huart3 __attribute__((weak));` makes an undefined
   weak symbol resolve to address 0, so the entry is simply `NULL` and gets
   skipped at run time — one table then serves every subset of enabled UARTs.
   Verify the behaviour on the actual toolchain before relying on it.

2. **The `#error` must live somewhere always compiled.** If every target file is
   wholly wrapped in `#ifdef STM32G0B1xx`, an unrecognised part yields empty
   translation units and the diagnostic degrades to an undefined-symbol *link*
   error on `g_x_uart_stream_target`. Either one file with an
   `#if / #elif / #else` chain (the `#else` comes free, and is the right choice
   at three targets), or separate files plus a guard header kept in sync.

3. **Built-ins must be optional.** An adopter on an unsupported part has to be
   able to supply their own table without editing the module. Gate the built-ins
   on something like `UART_STREAM_TARGET_BUILTIN` (default on), and have the
   `#error` say so explicitly: *no built-in table for this MCU — add one, or
   define `UART_STREAM_TARGET_USER` and provide `g_x_uart_stream_target[]`
   yourself.*

4. **Carry the `*_it.c` snippet in each table file's comments.** Vector names and
   their per-UART call lists are the part the adopter still hand-writes; having
   the exact text beside the table means never deriving it from the reference
   manual.

Note the vector layouts genuinely differ across these parts — G0B0 and G0B1 do
not share the same groupings, and G474 gives every UART its own vector — so the
tables cannot be generated from a family rule.

---

## LOCKED CONTEXT

Verified during planning on 2026-08-01; not up for re-derivation.

**`uart_stream` does not hook vectors today.** There is no weak-symbol override
anywhere in it. It exposes two callable entry points — `v_uart_stream_isr()`
(scans all bound instances) and `v_uart_stream_isr_for(USART_TypeDef *)` (targets
one) — and the vector is hand-wired in the generated `*_it.c` USER CODE block.
"One vector, one owner" is enforced by documentation, not code. The interception
mechanism has to be designed; there is nothing to recover.

**HAL's hostile behaviour, precisely.** On ORE/FE/NE/PE, `HAL_UART_IRQHandler`
calls `UART_EndRxTransfer()`, which clears `RXNEIE`/`PEIE`/`EIE` and sets
`RxState = READY`. Coherent for a transactional API; fatal for an always-on
console, where one overrun from a terminal reconnect kills RX permanently. This
is also why "let HAL run, then hook the tail" cannot work — the shutdown happens
inside HAL, before any tail hook is reached.

**`USE_HAL_UART_REGISTER_CALLBACKS` is not a solution.** It replaces the weak
callback *functions* with runtime-registerable pointers. `HAL_UART_IRQHandler`
still runs the same state machine and still calls `UART_EndRxTransfer()`. HAL
offers no build option that granularises UART interrupt handling. Ruled out.

**STM32G0B1 shares UART vectors** (`stm32g0b1xx.h`):

| IRQn | Peripherals |
|---|---|
| `USART1_IRQn` (27) | USART1 only |
| `USART2_LPUART2_IRQn` (28) | USART2 + LPUART2 |
| `USART3_4_5_6_LPUART1_IRQn` (29) | USART3, 4, 5, 6, LPUART1 |

So runtime ownership arbitration is a genuine requirement on this part, not a
nicety — the moment `uart_stream` owns one peripheral on a shared vector and HAL
owns another. SwitchTester itself uses only USART2, whose vector-mate LPUART2 is
unused, so the simple case applies *here* even though the general mechanism must
handle the shared case.

**Two upstream variants exist, and they differ substantially.** Compared
2026-08-01:

| Aspect | LED_Strip (G474) | ee_fw ST3074 (G0B0) |
|---|---|---|
| Vector ownership | Exports `v_uart_stream_isr()` / `_isr_for()`; vector hand-wired in `*_it.c` USER CODE | **Defines strong `USART1/2/3_4_5_6/LPUART1_IRQHandler` in the .c** — claims all four unconditionally |
| Dispatch | `_isr_for()` matches one instance by register | `v_uart_stream_common_isr()` scans **all** instances on **every** UART interrupt |
| Queue storage | Embedded `queue_t`, caller-owned buffers via `b_queue_init()` — **no malloc** | `queue_t *` via `p_x_queue_create()` — **mallocs** |
| TX flush | Unbounded spin | **Two-tier bounded timeout**, best-effort, "must never hang the watchdog'd main loop" |
| Style | `#pragma once`, doxygen, self-contained folder | Include guards, banner comments, `App/Inc` + `App/Src` split |
| Instances | 4 | 6 |
| Device include | `main.h` | `stm32g0xx.h` |
| `u16_uart_stream_rx_multi_blocking` | absent | present but `// FIXME` — ignores its timeout |
| `b_tx_queue_load` | absent | field exists, **guard commented out at its only use site** |

**The ee_fw variant does not solve coexistence — it abolishes it.** It does not
take hard ownership of *one* vector; it takes all four UART vectors at link time,
and the common ISR walks every bound instance on every UART interrupt. HAL can
therefore never own a UART on that target, and there is no delegation path even
in principle. Cost scales with `MAX_INSTANCES`, not with what actually fired.

It is still useful evidence: it **proves the strong-symbol approach links and
works** on a G0. The mechanism is validated; it is simply applied too broadly.

**Current console path.** `App/Src/stdio_retarget.c`: `_write` → blocking
`HAL_UART_Transmit`; `_read` → `HAL_UART_Receive(…, 1, 0)`, non-blocking single
byte, returning −1 when idle so `i_getline()` can pump `v_app_polling_task()`.
At 921600 a character lands every **10.9 µs** while each blocking `printf`
character costs ~11 µs, so a scripted host overruns almost immediately — and
`HAL_UART_Receive`'s polling path tests only `RXNE`, never `ORE`, so overruns
drop bytes **silently with no error surfaced**. Nondeterministic parser
misbehaviour is exactly what a test interface cannot tolerate.

---

## Detail sections

One per Big Board row. **The Big Board above is authoritative for status** — each
section repeats its own status line, and sections are grouped by ID family rather
than by open/resolved, since most rows are now green. Resolved rows keep their
options and rationale so decisions stay auditable if one needs reopening.

### D3 — stdio retarget routes through uart_stream

**Status:** 🟢 — `_write` blocks with a bounded timeout (**S3**); `_read` returns
−1 when idle so `i_getline()` keeps pumping `v_app_polling_task()`.

`_write` → `u16_uart_stream_tx_multi()` / blocking variant; `_read` →
`i16_uart_stream_rx_byte()`, still returning −1 when idle so `i_getline()` keeps
pumping `v_app_polling_task()`. Note this is **required**, not optional: leaving
`HAL_UART_Transmit` in place alongside a bound instance means HAL and
`uart_stream` both drive `TDR`.

Open sub-point folded into **S3**: which TX path `printf` uses when the ring is
full.

**Resolution:** _pending confirmation_

---

### D4 — Console ring buffer sizes

**Status:** 🟢 — **TX 1024, RX 256**, declared as
`DEV_CONFIG_CONSOLE_TX_BUF_SIZE` / `DEV_CONFIG_CONSOLE_RX_BUF_SIZE` in
`App/Inc/device_config.h` alongside `DEV_CONFIG_NVM_COMMIT_DELAY_MS`.

**TX dominates, and it is about blocking time, not transmission time.**
Transmission is baud-bound regardless of ring size; the ring's job is to stop the
main loop *waiting* for it, since `_write()` only blocks once the ring is full.
Against this project's largest burst — the switch-cycling menu redraw, ~1.2 kB —
a 256-byte ring stalls ~10.4 ms while 1 kB stalls ~2 ms. The gap widens with
back-to-back logging in larger applications.

**RX is coupled to TX through that blocking time.** A bigger TX ring means far
less time stuck inside `_write()`, so `_read()` is called again much sooner and
the not-reading window that RX must cover shrinks. 256 bytes covers several
command lines comfortably. It is deliberately *not* sized for a host streaming
continuously through a full console dump; `u32_error_count` is the tell if that
ever occurs (see **S5**).

Footnotes: the leave-one-slot-empty scheme means 1024 yields 1023 usable bytes;
RAM cost is 1280 B against 3664 currently used of 147456, and even the **T3**
loopback rig binding all eight UARTs at these sizes is ~10 kB.

---

### I6 — FIFO mode
**Status:** 🟢 — FIFOs enabled in CubeMX for every {LP}USART that provides them.

The register macros the ISR uses (`USART_ISR_RXNE_RXFNE`, `USART_ISR_TXE_TXFNF`,
`USART_CR1_RXNEIE_RXFNEIE`, `USART_CR1_TXEIE_TXFNFIE`) are the dual-purpose
names — RXNE/TXE with FIFOs off, RXFNE/TXFNF with them on — so the code is
correct either way, and the existing `do/while` drain loops already consume and
produce multi-byte FIFO bursts without modification. Enabling FIFOs is therefore
close to free on the software side while cutting interrupt rate by up to 8×,
which is what makes the **T3** throughput targets reachable at all.

Trade-off to watch during **T3**: deeper thresholds mean fewer but *longer*
ISRs, which pulls against the 10 µs TIM2 cycling floor (**I5**).

---

### I7 — Common ISR optimisation

**Status:** 🟢 — all five items below adopted.

The drain/fill loops already handle FIFO bursts correctly — `do/while` on
`RXNE_RXFNE` and `TXE_TXFNF` naturally consumes/produces as many bytes as the
FIFO holds. What follows are throughput items, ranked by value.

**1. Lock-free SPSC on the ISR side — the headline.** `b_queue_enqueue()` and
`i16_queue_dequeue()` each take a **PRIMASK critical section per byte**. Inside
the ISR that is both wasted work and actively harmful: the ISR is the sole
producer of the RX ring and sole consumer of the TX ring, so under strict SPSC no
lock is needed at all. The producer reads `tail` (which only the consumer
advances — a stale read is conservative, merely making the ring look fuller) and
publishes `head` last as a single aligned 16-bit `volatile` store, atomic on M0+.

Worse than the ~6–8 cycles: masking PRIMASK blocks the **priority-0 TIM2 cycling
ISR**, once per byte. An 8-byte FIFO drain does it eight times.

**Approach (asymmetric — locking belongs on one side only):**

- **ISR side: no masking at all.** Add `b_queue_enqueue_isr()` /
  `i16_queue_dequeue_isr()` with no critical section. Unconditionally safe: the
  foreground cannot preempt an ISR, and only one ISR touches each ring.
- **Foreground side: mask that UART's own interrupt, not PRIMASK.** Clearing
  `RXNEIE`/`TXEIE` in `CR1` for the duration is comparable in cost (~4–6 cycles)
  but **scoped** — it cannot delay the priority-0 TIM2 cycling ISR the way
  PRIMASK does. This is the user's selective-masking idea, applied to the half
  where locking actually earns its keep.

Strictly, SPSC needs no locks on *either* side given atomic aligned index stores,
which M0+ provides for `volatile uint16_t`. Keeping the scoped foreground lock is
cheap insurance against a second foreground producer appearing later; the
per-byte ISR lock is the one that must go.

Document the invariant this rests on: **one producer per ring, one consumer per
ring.** Calling a TX function from a second ISR would break it.

**2. The TX loop defeats `&&` short-circuiting.**

```c
bool b_txe_active     = (u32_isr & USART_ISR_TXE_TXFNF) != 0U;
bool b_queue_not_empty = !b_queue_is_empty(p_inst->p_x_tx_queue);   /* critical section! */
if (b_txe_active && b_queue_not_empty)
```

Both operands are computed into locals *before* the `if`, so `b_queue_is_empty()`
— and its critical section — runs on every iteration including the one that
exits, even when `TXE` is already clear. Inlining the tests into the `if` restores
short-circuit evaluation and drops a critical section per iteration.

**3. Embedded `queue_t` rather than `queue_t *`.** ee_fw's pointer members cost an
extra load per ring access in the hot loop. LED_Strip's embedded structs remove
it — and this project is taking LED_Strip's caller-owned buffer model anyway
(**D6**), so it comes for free.

**4. Move the `ISR` re-read inside the `if`.** ee_fw re-reads `p_reg->ISR` at the
bottom of the RX loop unconditionally, so the exit iteration pays a wasted
volatile read. LED_Strip already has it inside the conditional; adopt that.

**5. The instance scan is already gone.** ee_fw's common ISR walked all
`UART_STREAM_MAX_INSTANCES` on every interrupt, touching each active instance's
`ISR` register. **D1**/**D2** replace that with one call per instance, so this
dissolves. The remaining handle→instance lookup is a short pointer scan;
memoising the last match would make it one compare in a single-UART build.

Rough per-byte estimate: ~28 cycles today versus ~14–16 with items 1–4, so
roughly **2×** on the dominant cost, or ~100 cycles per 8-byte FIFO interrupt.

**Resolution:** _pending_

---

### T3 — Loopback stress rig

**Status:** 🔵 — deferred until the single-instance console build is working.
Methodology recorded below; execution comes after that.

All eight G0B1 UARTs configured 921600 8N1 with TX strapped to RX, `uart_stream`
owning all or nearly all of them, pushing a long test pattern out every port and
verifying it returns intact. Avoids hooking up eight USB-serial bridges and lets
the rig be driven entirely from firmware.

**Expect to find the ceiling, and treat that as the result.** Rough arithmetic:
921600 8N1 is 92,160 bytes/s per direction; loopback makes each UART full-duplex,
so ~184,320 byte-events/s per UART, ~1.47M/s across eight. At 64 MHz that is
**~43 cycles per byte-event** — below the cost of an M0+ interrupt entry alone, so
eight-way saturation is not reachable by design, not by defect.

With 8-byte FIFOs and a mid threshold the interrupt rate drops roughly 4–8×,
giving ~175–350 cycles per ISR servicing several bytes — plausible but tight. So
the test should **ramp** (1 → 2 → 4 → 8 UARTs, and by baud) and record where
throughput departs from linear. That knee is the useful number, and **I6** is the
main knob that moves it.

Worth measuring alongside: `u32_error_count` per instance (RX ring overflow vs
hardware ORE), and the effect on TIM2 cycling jitter, since the cycler is the
other real-time consumer.

---

### S6 — TX blocking-path deadlock

**Status:** 🟢 — fixed by re-arming `TXEIE` inside the fill loop (below).

Present in **both** variants. `v_uart_stream_tx_multi_blocking()` calls
`v_queue_enqueue_multi_blocking()` — which loops until every byte is queued —
and only *then* sets `TXEIE`:

```c
v_queue_enqueue_multi_blocking(&p_x_inst->x_tx_queue, p_u8_src, u16_len);
p_x_inst->p_x_huart->Instance->CR1 |= USART_CR1_TXEIE_TXFNFIE;
```

If the payload is larger than the TX ring and `TXEIE` is clear on entry (the ISR
clears it whenever it drains the queue empty), the ring fills, the loop spins
waiting for space, and nothing can ever drain it — the TX ISR never fires. Hard
hang, no watchdog kick.

The single-byte blocking path is safe by luck: one byte cannot fill the ring, so
`TXEIE` is always set before a spin can matter.

**Resolution: re-arm `TXEIE` inside the fill loop — ON only, never OFF.**

```c
while (u16_remaining > 0U)
{
    u16_written = u16_queue_enqueue_multi(q, p_u8_ptr, u16_remaining);
    u16_remaining -= u16_written;
    p_u8_ptr      += u16_written;
    p_reg->CR1 |= USART_CR1_TXEIE_TXFNFIE;   /* unconditional re-arm */
}
```

**Why not simply set it once before the loop.** With an empty ring, `TXE` is
already asserted, so the ISR fires the instant `TXEIE` goes on, finds nothing to
send and clears it again — then the fill proceeds into the same hang. The re-arm
must happen *after* bytes are placed, every iteration, including the iteration
where `u16_written == 0` (that is precisely when the ISR may have just cleared
it).

**Keeping `TXEIE` on while filling is safe** — the concern that motivated the
late write does not apply. Under SPSC the producer writes `head` and reads
`tail`; the consumer writes `tail` and reads `head`. Disjoint variables. A drain
running during the `memcpy` only advances `tail`, which *frees* space, so the
producer's snapshot is stale-conservative: it writes fewer bytes than it could
have, never more. No desync is possible.

**No new function is required.** `u16_queue_enqueue_multi()` already returns the
count written, and `v_queue_enqueue_multi_blocking()` is already a loop over it —
the fix is one line inside that loop. It also makes the invariant uniform: *every*
path that enqueues TX bytes arms `TXEIE` afterwards, which the non-blocking paths
already did.

**Rejected alternative — ee_fw's `b_tx_queue_load` flag.** That field is not
vestigial cruft as first assessed: it was a fix for exactly this defect (tell the
ISR not to disarm while foreground is mid-fill) that was commented out before it
shipped. It works, but it taxes the **hot path** — an extra test in every TX
interrupt — to fix a rare cold-path case, whereas re-arming puts the cost where
the problem is.

### Severity and reachability

**The existing timeout does not cover it.** The two-tier bound lives in
`v_uart_stream_tx_flush_blocking_timeout()`, a different function.
`v_uart_stream_tx_multi_blocking()` → `v_queue_enqueue_multi_blocking()` is an
unbounded `while (u16_remaining > 0U)` with no deadline anywhere in the path.

**It is deterministic, not a race.** Once the preconditions hold it hangs every
time, which makes it easy to reproduce and easy to test:

1. The ISR drains the TX ring empty and clears `TXEIE` — normal, expected.
2. Foreground calls `v_uart_stream_tx_multi_blocking()` with a payload larger
   than the ring.
3. `u16_queue_enqueue_multi()` fills the ring, returns short.
4. Loop repeats; every subsequent call returns 0.
5. `TXEIE` is still clear, so the TX ISR never fires and nothing drains.

Hard hang, no watchdog kick, reset required.

**The interrupt management elsewhere is solid — this is one gap.** Every other TX
path sets `TXEIE` immediately after enqueuing: `b_uart_stream_tx_byte()`,
`u16_uart_stream_tx_multi()`, `v_uart_stream_tx_byte_blocking()`. The single-byte
blocking path is safe for a structural reason worth stating — one byte cannot fill
an empty ring, and a *full* ring implies something already filled it and therefore
already set `TXEIE`. The defect is specifically that the multi-byte blocking path
hoists its `TXEIE` write out of the loop.

**Likelihood here: low, but not negligible.** It needs a single write larger than
`DEV_CONFIG_CONSOLE_TX_BUF_SIZE` (1023 usable). `printf` fragments into many small
`_write()` calls, so the console will not trigger it. The exposure is the public
API: a HIL response path or a bulk dump handing `v_uart_stream_tx_multi_blocking()`
a >1 kB buffer in one call. That is a plausible shape for **T2**.

**Verdict:** low probability, maximum severity, trivial fix. Worth doing on import
rather than leaving as a latent trap in a module headed for the skeleton (**T1**).
Recommend both the root fix (set `TXEIE` first) *and* a bounded deadline on the
multi-blocking path, for parity with the flush and to cover an undrainable ring
for any other reason.

---

### S2 — Shared-vector delegation semantics

**Status:** 🟢 — **subsumed by D1/D2.** This row predates the final vector shape
and its three rules have each found a home; kept for traceability.

The vector body is a flat list of `b_uart_stream_service_uart(&huartN)` calls,
one per instance on that vector, **no `if` chain, return value ignored**,
followed by an early `return` in the top USER CODE block that bypasses CubeMX's
generated `HAL_UART_IRQHandler()` list entirely. HAL is invoked on an as-needed
basis *inside* `b_uart_stream_service_uart()`, on the not-owned path only.

Where each original rule went:

| Original rule | Disposition |
|---|---|
| Service **all** peripherals on the vector, don't stop at the first | Structural — the body calls the service function once per instance, so this holds by construction rather than by discipline |
| Delegate to HAL **only** for unowned peripherals | **D2** — the return value means *ownership*, and the delegation happens internally on the not-owned path. An owned-but-idle peripheral returns `true` and is never handed to HAL |
| Test **flag AND enable**, not the flag alone | Survives only as an internal micro-optimisation on the *owned* path, skipping the service routine when a sibling on the shared vector was the one that fired. Deliberately **not** applied before delegating to HAL, which performs the same `ISR`/`CR1`/`CR3` test in its own opening lines (**I7**) |

---

### S3 — Blocking calls vs the cooperative polling model

**Status:** 🟢 — **bounded-timeout blocking, no `v_app_polling_task()` pumping.**
`_write` blocks only when the ring is full; the two-tier timeout (caller-sized
outer drain bound plus a short hardware-`TC` bound) gives up best-effort rather
than hanging. Strictly better than the status quo, where `printf` blocks for the
*entire* transmission every time.

`v_queue_enqueue_blocking()` and `v_queue_enqueue_multi_blocking()` spin hard;
`v_uart_stream_tx_flush_blocking()` spins on queue-empty and only then sets
`TXEIE`. None of them pump `v_app_polling_task()`, so a full TX ring stalls job
dispatch, the menu and the NVM commit timer. Any of them called with interrupts
masked deadlocks outright, since only the TX ISR can drain the ring.

Options: cooperative variants that pump the polling task; drop-on-full for the
console; or keep the spin and simply never call the blocking forms from this
project. Interacts with **D3** (what `printf` does when the ring is full) and
**S6** (the blocking-path deadlock).

**Pumping `v_app_polling_task()` from a blocking TX is unsafe as structured.**
The user raised this and it holds up. `v_app_polling_task()` calls
`v_process_next_job()`, a job handler such as `JOB_CYCLE_COMPLETE` calls
`printf`, `_write` re-enters the TX path on the *same* full ring, and that path
pumps again — unbounded recursion to stack overflow. `v_debug_menu_service()` has
a `u8_reentry_lock`, but `v_process_next_job()` has no equivalent, and `printf`
from a job handler is exactly the realistic case.

So pumping would need a TX re-entrancy guard degrading inner calls to
drop-on-full — extra state and a subtle second behaviour mode. Not pumping avoids
the whole class by construction.

**ee_fw already answers half of this.** Its
`v_uart_stream_tx_flush_blocking_timeout()` uses a two-tier bound — a
caller-supplied outer drain timeout sized to the expected transmission, plus a
short internal 2 ms bound on hardware `TC` — and gives up best-effort rather than
hanging, with a `HAL_GetTick()`-based deadline. The no-arg wrapper keeps
signature compatibility with a conservative 50 ms default. That is the right
shape and it is field-proven.

It still does not *pump* anything, so for this project the remaining half is
whether bounded-and-give-up is sufficient or whether the wait should also call
`v_app_polling_task()` the way `i_getline()` and `v_delay_pump()` already do.

**Resolution:** _pending_

---

### S4 — HAL `huart` state after bind

**Status:** 🟢 — **bind poisons `gState` and `RxState` to `HAL_UART_STATE_BUSY`;
deinit restores `READY`.** Every HAL UART API then returns `HAL_BUSY` immediately
instead of touching a peripheral `uart_stream` owns, converting silent corruption
into a visible, self-explaining failure.

**The problem.** `UART_HandleTypeDef` is not just a config block — it carries live
transfer state: `gState` (TX/global), `RxState`, `Lock`, the `pTxBuffPtr` /
`TxXferCount` / `pRxBuffPtr` / `RxXferCount` transfer cursors, the `RxISR`/`TxISR`
function pointers, and `ErrorCode`. `HAL_UART_Init()` leaves `gState` and
`RxState` at `HAL_UART_STATE_READY`. `uart_stream` then drives `CR1`, `RDR` and
`TDR` directly and never updates any of it, so HAL's view says "idle and
available" forever while the peripheral is in fact being actively driven.

Three concrete failure modes follow:

1. **`HAL_UART_Transmit()` corrupts output.** It gates on
   `if (huart->gState == HAL_UART_STATE_READY)` — which is always true — then
   polls `TXE` and writes `TDR` itself. `uart_stream`'s ISR is *also* writing
   `TDR` whenever `TXE` fires. Two writers, interleaved bytes, silent loss. This
   is live in this project right now: `stdio_retarget.c`'s `_write` calls exactly
   that function.
2. **`HAL_UART_Receive()` steals bytes.** It polls `RXNE` and reads `RDR`; every
   byte it takes is one the ISR never sees. Silent RX loss with no error.
3. **`HAL_UART_IRQHandler()` on a bound instance kills RX.** Its error path calls
   `UART_EndRxTransfer()`, clearing `RXNEIE|PEIE|EIE`. **D1**/**D2** close this by
   construction, but it is why that invariant is not negotiable.

**Options.**

- *Documented rule only* — "never call `HAL_UART_*` on a bound instance." Zero
  cost, zero enforcement, silent corruption when violated.
- *Poison the handle at bind* — set `gState` and `RxState` to
  `HAL_UART_STATE_BUSY` (0x24, "internal process ongoing"). Every HAL API then
  returns `HAL_BUSY` immediately instead of touching the peripheral. Two stores at
  bind; `v_uart_stream_deinit()` restores `READY`. Converts silent corruption into
  a visible, harmless, self-explaining failure.
- *Also clear `RxISR`/`TxISR`* — belt-and-braces for failure mode 3, largely
  redundant given **D1**/**D2**.

**Leaning:** poison `gState`/`RxState` at bind, restore at deinit. The cost is two
stores, and it turns "why is my output garbled?" into "why does
`HAL_UART_Transmit` return `HAL_BUSY`?" — a question whose answer teaches the
invariant.

**Resolution:** _pending_

---

### S5 — RX overrun policy for the scripted parser

**Status:** 🟢 — **reject on error and send a structured error response to the
host.** A command whose reception was compromised is never partially executed:
the parser fails it and the host receives an explicit, machine-readable error
rather than silence or a plausible-looking wrong result.

This is the right trade for a test interface, where a silently mis-parsed command
could invalidate a whole run without anyone noticing. `uart_stream` still counts
and clears ORE/FE/NE/PE per instance (**S1**) and drops bytes when the RX ring is
full; the error counter becomes queryable so the host can assert it has not moved
across a run. Response format belongs to the REPL's own design (**T2**).

---

### T1 — Promote to `G0B1_Skeleton`

**Status:** 🔵 — desired, deferred until the new `uart_stream` is fully tested.

Once `uart_stream` is proven on this bench, port it back to the parent
`G0B1_Skeleton` alongside `jobs` and `nvmparams`, so it becomes part of the
reusable baseline rather than a SwitchTester-only asset. Carries the adopter
documentation (**D5**) and the target-table pattern (**D2**) with it — those are
what make it retargetable.

Gate: the loopback rig (**T3**) green, and the console running on it for a
sustained bench session without an error-count movement.

---

### T2 — Host-side script / REPL

**Status:** 🔵 — deferred until `uart_stream` is tested and debugged on the
single-instance console case. No value in layering the REPL on transport that has
not yet been shown to work as advertised. Stays a row on this board rather than
getting its own; spin it out only if the design grows past a few rows.

The Python-side driver for the automation backdoor. Distinct from `uart_stream`,
which is only the transport underneath it.

Decisions already taken in discussion but currently homeless — they need a home
before this row can start: drop `toupper()` from command dispatch, open the
command namespace to the full printable range `0x20..0x7E`, add registration-time
collision checking against builtins and already-registered ops, expose the
`uart_stream` error count as a builtin so the host can assert zero, and retain
`HARNESS_ENTER` / `HARNESS_EXIT` plus the idle timeout from the LED_Strip pattern.

See the pending question on whether this becomes its own D-log.

---



### D1 — Vector hooking pattern
**Status:** 🟢 — **One `b_uart_stream_service_uart()` call per UART instance**, in
USER CODE block 0, followed by `return`. Deliberately mirrors the body CubeMX
generates, so the replacement reads like what it replaced:

```c
/* USER CODE BEGIN USART2_LPUART2_IRQn 0 */
b_uart_stream_service_uart(&huart2);
b_uart_stream_service_uart(&hlpuart2);
return;
/* USER CODE END USART2_LPUART2_IRQn 0 */
```

**Why the CubeMX experiment settled it.** Enabling all eight G0B1 UARTs revealed
that HAL's own answer to a shared vector is an unconditional *list* of
`HAL_UART_IRQHandler()` calls, one per peripheral, each deciding internally
whether it has work. Two consequences killed the alternatives:

- Any construct spanning USER CODE blocks (`#if 0` … `#endif`, `if (…) {` … `}`)
  gates **every** call on that vector, but ownership is **per peripheral**. On a
  vector where `uart_stream` owns USART2 and HAL owns LPUART2 there is no way to
  suppress only the first.
- An or-aggregated "did I handle anything" gate is worse than useless: with both
  peripherals pending it suppresses HAL entirely, LPUART2's flag stays set, and
  the NVIC re-enters forever — an interrupt storm.

`return` rather than `#if 0` keeps braces and preprocessor balanced within a
single block, and matches the pattern `TIM2_IRQHandler` already uses in this repo.
The generated call list below becomes unreachable and is dropped by the compiler.

Rejected: the ee_fw strong-symbol approach (claims all vectors, no delegation
possible) and the one-vector-one-owner rule (on G0B1, owning USART2 would force
owning LPUART2).

### D2 — Service function signature and target table
**Status:** 🟢 — Takes a **`UART_HandleTypeDef *`**, matching HAL's own call site.

```c
/* True  = ours (serviced if anything was pending) -- caller must NOT delegate.
 * False = not ours; the call has already been forwarded to HAL internally. */
bool b_uart_stream_service_uart(UART_HandleTypeDef *p_x_huart);
```

Three points of shape, each load-bearing:

1. **The return value means *ownership*, not "did work".** Collapsing "not owned"
   and "owned but idle" into one `false` would let a sibling's interrupt on a
   shared vector route `HAL_UART_IRQHandler()` onto a `uart_stream`-owned
   peripheral — the exact `UART_EndRxTransfer()` shutdown being avoided.
2. **HAL is invoked internally** on the not-owned path, which is what lets the
   vector body collapse to a flat list with no external `if`.
3. **No pre-filter before delegating to HAL.** `HAL_UART_IRQHandler()` opens by
   reading `ISR`/`CR1`/`CR3` and testing exactly that; duplicating it pays the
   same register reads twice. The status pre-check is worth keeping only on the
   *owned* path, where it skips the service routine when a sibling fired.

**Target table**, application-provided so `uart_stream` carries no per-MCU
knowledge:

```c
typedef struct
{
    UART_HandleTypeDef *p_x_huart;   /* &huart2 -- ->Instance gives the register base */
    IRQn_Type           e_irqn;      /* USART2_LPUART2_IRQn                            */
}
uart_stream_target_t;
```

`p_x_reg` is redundant — `UART_HandleTypeDef` carries `USART_TypeDef *Instance`
as its first member. Because the table is `const` and can only reference handles
that exist as symbols, it naturally lists exactly the UARTs a build configures.

**Pointer over index, decided on robustness.** An index is O(1) against a ~6-cycle
scan (1–3 entries in a realistic build), but requires a second artifact — an enum
whose ordering must be hand-kept in sync with the table. Drift there services the
*wrong* UART: silent and subtle. The pointer form degrades gracefully instead — an
unlisted handle simply returns false and HAL takes it. It also matches the user's
standing rule against hand-synced compile-time constants, the same class of drift
that produced the `NVM_AUTO_COMMIT_DELAY` and `htim6` staleness removed earlier.

If the scan ever profiles hot, the fix is local and does not touch call sites:
sort the table by `e_irqn` with a cached first/last index per vector, or memoise
the last match.

### I4 — IRQn mapping
**Status:** 🟢 — **Dissolved by D2 rather than fixed.** `e_uart_stream_get_irqn()`
was a hardcoded reverse lookup from register base to vector, and five of its six
IRQn symbols do not exist on G0B1. With `e_irqn` carried per entry in the
application-provided target table, the function is deleted outright and the
per-MCU knowledge moves to the one file that should own it.

### D5 — Style and naming
**Status:** 🟢 — **Doxygen throughout.** Every function and every internal
variable gets at least a minimal Doxygen comment; public API functions get
comprehensive treatment (`@brief`, `@param`, `@retval`/`@return`, `@warning`,
`@note` where they earn their place). Naming stays the user's Hungarian variant
already used across `jobs`, `nvmparams` and `utils` — `p_x_`, `u32_`, `b_`,
`e_`, `h_`. Consistency within the module is the priority; keep the overall
shape, clean up and extend as needed.

ee_fw already documents the right *things* — its banner blocks carry
`Parameters:` / `Returns:` sections — so this is a format conversion that
preserves content and gains tooling support, not a rewrite.

Note the deliberate divergence: sibling App modules (`jobs.h`, `nvmparams.h`,
`utils.h`) stay banner-commented. `uart_stream` is a reusable library module
destined for the shared skeleton (**T1**), so richer docs are justified; the
consistency being enforced is *within* the module, not against its neighbours.

### D6 — Port base
**Status:** 🟢 — **ee_fw's version.** Keeps its `App/Inc` + `App/Src` layout,
include guards, `stm32g0xx.h` include, the bounded two-tier flush (**S3**) and
the G0-family shape.

Explicitly **dropped** on import: the blanket strong-symbol vector claim
(superseded by **D1**/**D2**), `e_uart_stream_get_irqn()` (deleted per **I4**),
the vestigial `b_tx_queue_load` field whose guard is commented out at its only use
site, and the `// FIXME` `u16_uart_stream_rx_multi_blocking()` stub that ignores
its timeout.

Still to be **taken from LED_Strip**: the caller-owned buffer model
(`b_queue_init()` with embedded `queue_t`, no malloc on the bind path) — a bench
tool should not depend on the heap for its console.

### I5 — Interrupt priority landscape
**Status:** 🟢 — Verified in the tree after the 2026-08-01 regen. M0+ has four
priority levels and all four are used distinctly:

| IRQ | Priority | Rationale |
|---|---|---|
| `TIM2_IRQn` | **0** | Cycling ISR: 10 µs floor, a missed compare costs a 71.6 min wrap |
| SysTick (`TICK_INT_PRIORITY`) | **1** | See below — must outrank UART |
| `USART1`, `USART2_LPUART2`, `USART3_4_5_6_LPUART1` | **2** | Below the cycler, above background |
| `TIM14`, `EXTI4_15`, `RTC_TAMP` | **3** | 1 ms app tick, button, RTC wake — all latency-tolerant |

**SysTick above UART is the load-bearing choice.** `uart_stream`'s bounded flush
measures its timeout with `HAL_GetTick()`. Were the tick below UART, saturated
UART traffic could starve the very counter the timeout depends on — the flush
would stretch exactly when it most needs to expire. The same argument protects
`v_delay_pump()` and the NVM commit timer.

Checked that UART saturation cannot make TIM14 *miss* ticks rather than merely
delay them: a FIFO-drain ISR is ~5 µs, and even eight saturated UARTs at 921600
give ~92k interrupts/s against TIM14's 1000 µs period. Delayed by microseconds,
never lost — pulse timing stays honest.

Caveat retained from analysis: NVIC priority alone is not sufficient, because
`queue.c`'s PRIMASK critical sections block priority 0 regardless of tier. That
is what **I7** removes from the ISR path.

Cosmetic: `HAL_NVIC_SetPriority(USART3_4_5_6_LPUART1_IRQn, 2, 0)` appears five
times, since USART3/4/5/6/LPUART1 each have their own MspInit setting the shared
vector. Idempotent.

### I8 — NVIC enable timing and ownership
**Status:** 🟢 — `uart_stream` enables its own vector inside
`x_uart_stream_init()`, after the queues are up, so it already follows the
user's preferred policy of arming interrupts when the application is ready
rather than at HAL-init time. **An adopter may therefore leave the NVIC box
unticked in CubeMX entirely** — which sidesteps CubeMX's buggy and inconsistent
per-channel enable option rather than fighting it. This belongs in the adopter
instructions (**D5**).

**Why the general CubeMX risk is narrower than it looks.** CubeMX enables the
*vector* in each MspInit, but HAL peripheral init does not arm the *source* — a
vector enabled with no source armed cannot fire. Verified for this project:
TIM2 (no `CCxIE`/`UIE` until a cycle starts), TIM14 (`UIE` only at
`HAL_TIM_Base_Start_IT()` in `v_hardware_init()`), RTC_TAMP (wakeup timer armed
on demand), and every UART (`HAL_UART_Init()` writes no `RXNEIE`/`TXEIE`; all
interrupt-enable writes in `stm32g0xx_hal_uart.c` live in the `_IT`/`_DMA` start
paths).

**The one genuinely live case is the button.** `NUCLEO_BUTTON_Pin` is
`GPIO_MODE_IT_FALLING`, and `HAL_GPIO_Init()` unmasks `EXTI->IMR1` as part of
configuring it — so that vector is armed from `MX_GPIO_Init()`, the first
peripheral init, long before `v_job_queue_init()`. Benign today (unimplemented
weak callback, and `v_job_add()` returns immediately while `u8_size == 0`), but
it stops being benign the moment that callback is implemented.

**Deinit must not disable a shared vector.** `v_uart_stream_deinit()`
deliberately clears only `RXNEIE`/`TXEIE`, never the NVIC. On G0B1 that is
required, not conservative: disabling `USART2_LPUART2_IRQn` on unbind would also
kill HAL's service of LPUART2.

### S1 — Error policy
**Status:** 🟢 — Clear `ORE`/`FE`/`NE`/`PE` via `ICR`, increment a per-instance
error counter, **never** disable the UART or its interrupts. This is the
inherited upstream behaviour and the entire reason for owning the ISR rather than
delegating to HAL. Not up for revision.

### I1 — Module location
**Status:** 🟢 — `App/uart-stream/`, sources and headers together with the queue
API alongside, rather than ee_fw's split `App/Inc` + `App/Src` layout. Keeping the
module self-contained in one directory suits something destined to be dropped into
other projects (**T1**). The `""` includes resolve within the directory; `main.h`
and `platform.h` come from the existing `-I` paths.

### I2 — Queue storage model
**Status:** 🟢 — **`queue_t` control blocks embedded as static members of the
`uart_stream` instance struct; byte buffers keep ee_fw's dual pattern** (caller
passes a pointer, or passes NULL and the buffer is malloc'd).

`g_x_uart_stream_instances[]` is static regardless, so embedding the control
blocks costs no extra RAM while removing a pointer indirection from the ISR hot
path and two mallocs per bind. `p_x_queue_create()` needs an in-place sibling
that initialises a caller-supplied `queue_t` and allocates only the buffer.

**Allocation philosophy (user's, 2026-08-01):** malloc is acceptable for
allocations that persist over the application lifetime or a long real-time
period; avoid it for temporaries that are repeatedly allocated and freed, where
stack or static storage belongs instead. The goal is efficient RAM use *without*
heap fragmentation or repeated allocator overhead. `uart_stream`'s buffers are
bind-time and effectively never freed, so they qualify. The project already
relies on this — `x_nvm_pool_init()` mallocs its 512-byte pool at startup and
never releases it.

The blanket no-malloc rule in the ee_fw project's `CLAUDE.md` is **not binding
here**; this repo is the user's own and deliberately takes the variant policy
above.

### I2a — `queue.c` code review (retained for reference)
**Reviewed:** The core ring is sound: leave-one-slot-empty, strict
single-producer/single-consumer, PRIMASK critical sections around head/tail
snapshots. The multi-byte paths read indices under lock, `memcpy` outside it, then
re-lock to publish — correct for SPSC because only the consumer moves `tail` and
only the producer moves `head`, so a stale read is conservative.

Three caveats, none fatal: the blocking helpers spin without cooperating (see
**S3**); `p_x_queue_create()`/`v_queue_destroy()` are malloc-based and unused by
the `uart_stream` bind path, so they are dead weight worth `#if`-ing out; and
`b_queue_is_full()`/`u16_queue_used()` compute outside the lock on a snapshot
taken inside it, which is fine but worth a comment.

### I3 — Register macros on G0B1
**Status:** 🟢 — All ten register macros the ISR uses are present in
`stm32g0b1xx.h`: `USART_ISR_RXNE_RXFNE`, `USART_ISR_TXE_TXFNF`,
`USART_CR1_RXNEIE_RXFNEIE`, `USART_CR1_TXEIE_TXFNFIE`, `USART_ICR_{ORECF,FECF,NECF,PECF}`,
`USART_ISR_TC`, `USART_ICR_TCCF`. G0B1 has UART FIFOs, so the FIFO-aware names
the G4 version uses carry over unchanged — **the register-level ISR body ports
with no edits.** Only the IRQn mapping (**I4**) needs work.

---

## Global notes

**Implementation phase sketch** (once the board is mostly green):

1. Fix **I4**, normalise style per **D5**, move `lib/` → `App/` (**I1**)
2. Vector hooking per **D1**, priority per **I5**
3. Rewrite `stdio_retarget.c` onto `uart_stream` (**D3**), buffers per **D4**
4. Blocking-call policy (**S3**), HAL state hygiene (**S4**)
5. Shared-vector dispatcher (**D2**, **S2**) — design now, implement when a
   second UART needs it
6. Bench-verify the console, then the HIL backdoor on top; **T1** afterwards

**Plan status:** 🟢 20 · 🟡 0 · 🔴 0 · 🔵 3 (+4 wish-list rows, W1 retired).
**Design complete 2026-08-01** — no open rows; remainder is implementation and test.
Verified against the table 2026-08-01.
Interrupt interception (**D1**, **D2**, **I4**) and the port base + style
(**D5**, **D6**) locked 2026-08-01; **I6** and **T3** added the same day.
**Next suggested ID:** **S3** — the blocking/cooperative policy. It gates **D3**
(what `printf` does when the ring is full) and shares call sites with **S6**, so
the two want settling together.

**End of uart-stream-integration-plan.md**
