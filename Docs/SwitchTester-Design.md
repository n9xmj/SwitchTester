# SwitchTester — Design Notes

> Status: **manual switch control, timer-driven cycling, the `uart_stream` console
> transport, the automation console (phase 1), the vendored `logging`, `menusystem`,
> `nvmparams` (phase 1, incl. a SPI-flash backend) and `event_queue` modules are all
> implemented and bench-verified.** Three HIL suites totalling 95 tests drive the real
> link and pass: `test_acon.py` 47, `test_nvm.py` 28, `test_eventq.py` 20.
>
> **Remaining work — see [`planning/switchtester-roadmap.md`](planning/switchtester-roadmap.md),
> which is the entry point for anyone picking this project up:** sense inputs (largest,
> and open-ended until the user decides what each channel measures), tying `event_queue`
> into switch and sense events, consuming those events via the automation console and
> the human-readable log, and interrupt-driven "switching lists" (per-cycle variable
> duty). Last touched 2026-08-27.

## Concept

Drive 4 "switch" outputs and sense their response on 4 "sense" inputs — a bench
tool for exercising/validating external switching hardware.

## Pin map

| Function | Net label | Pin | Peripheral | Notes |
|---|---|---|---|---|
| Switch out A | SWITCH_A | PC4  | TIM2_CH1 | 32-bit timer, OC |
| Switch out B | SWITCH_B | PC5  | TIM2_CH2 | |
| Switch out C | SWITCH_C | PB10 | TIM2_CH3 | |
| Switch out D | SWITCH_D | PB11 | TIM2_CH4 | |
| Sense A | SENSE_A | PA1 | COMP1 (+) via IO3 | threshold = DAC1_CH1 (dedicated, adjustable) |
| Sense B | SENSE_B | PB6 | COMP2 (+) via IO2 | threshold = DAC1_CH2 (adjustable); moved off PB4 (now SPI3_MISO) |
| Sense C | SENSE_C | PB0 | COMP3 (+) via IO1 | threshold = **½·VREFINT** (fixed, ≈0.6 V) — see note |
| Sense D | SENSE_D | PA0 | ADC1_IN0 | analog read; ADC also samples VREFINT |
| DAC ref 1 | — | PA4 | DAC1_CH1 | buffered, ext (PA4) + internal → COMP1 (−) |
| DAC ref 2 | — | — | DAC1_CH2 | unbuffered, internal only → COMP2 (−) |
| LED | NUCLEO_LED | PA5 | GPIO out | |
| Button | NUCLEO_BUTTON | PC13 | EXTI falling | |
| Console | DEBUG_TX/RX | PA2/PA3 | USART2 | 921600 |

COMP input assignments verified against `stm32g0xx_hal_comp.h`: COMP1_INP IO3 =
PA1, COMP2_INP IO2 = PB6, COMP3_INP IO1 = PB0. All three match. (Sense B was on
PB4/IO1 until the SPI-flash testbed took PB4 for SPI3_MISO; PB6/IO2 is the
equivalent COMP2 input.)

## External hardware

Simple and **active HIGH**. Each SWITCH_x output drives two things in parallel:

- an indicator LED through a 330 Ω series resistor (LED Vf ≈ 1.8 V, colour
  dependent) — roughly **4.5 mA**, well inside the pin's drive capability;
- ahead of the 330 Ω, the control input of a **CD4066** bidirectional analog
  switch, which needs to see the near-full 0 ↔ 3.3 V swing. The CD4066's switch
  outputs go to the DUT.

Push-pull AF output satisfies both with no special handling; the existing
`GPIO_SPEED_FREQ_LOW` setting is appropriate for manual level changes.

## Switch outputs — TIM2 (32-bit)

TIM2 is used for its **32-bit** counter (long + precise timing). Config: PSC 0,
ARR 0xFFFFFFFF, internal clock, all four channels output-compare.

**Drive scheme (decided, implemented).** The switch pins stay **permanently
muxed to AF2/TIM2** — there is no GPIO↔AF remuxing. The drive level is the
channel's output-compare mode field:

| Level | `OCxM` |
|---|---|
| ON  | `TIM_OCMODE_FORCED_ACTIVE` |
| OFF | `TIM_OCMODE_FORCED_INACTIVE` |

Setting a level is a single `OCxM` field write via `LL_TIM_OC_SetMode()`. When
the cycling mode arrives, moving a channel between manual and timer-driven is
the same single field write — no mux change, so no glitch window where pin
ownership is ambiguous.

**Polarity is not a firmware concern.** "ON" is always `FORCED_ACTIVE`; the
`CCxP` bit in `TIM2->CCER` (CubeMX's `OCPolarity`) decides what active means
electrically. The board is active-high, so `OCPOLARITY_HIGH` stands. An
active-low board would be a CubeMX change with no code edit.

**Start-up is edge-free.** `HAL_TIM_MspPostInit()` muxes the pins to AF2 with
`CCxE` clear, which leaves them driven low. `v_switch_out_init()` sets `OCxM` to
forced-inactive *before* enabling `CCxE`, so with active-high polarity the pins
never move — the external hardware sees no start-up twitch.

`HAL_TIM_OC_Start()` also sets `CEN`, leaving `TIM2->CNT` free-running as a
32-bit timebase. Nothing uses it yet; it is there for the cycling mode and for
sense timestamping.

**Drive state is read back from `OCxM`**, not from a shadow variable, so it
cannot drift out of step with the peripheral. `x_switch_out_get()` returns
`SWITCH_OUT_OFF` / `SWITCH_OUT_ON` / `SWITCH_OUT_TIMED`; the third value is
reserved for a channel under timer control, so the cycling mode will not force
an API change.

**Pulse timing** currently runs off the existing 1 ms periodic tick (per-channel
countdown in `v_switch_out_tick()`). 1 ms resolution, overlapping pulses on
multiple channels work. This is deliberately a placeholder: pulse is "cycle with
repeat = 1" and folds into the cycle engine when that lands.

### Code

- `App/Inc/switch_out.h`, `App/Src/switch_out.c` — the whole switch-output layer.
- `v_switch_out_init()` called from `v_hardware_init()` in `app_main.c`.
- `v_switch_out_tick()` called from `HAL_TIM_PeriodElapsedCallback()`.

## Debug menu — switch outputs

Reached from the main menu with **`s`**. Key-list entries are dispatched by
`MENU_ITEM_KEY_LIST_FUNCTION`, which hands the handler the index of the key
within its list — that index *is* the channel number.

| Keys | Action |
|---|---|
| `a` `b` `c` `d` | Force **OFF**, A–D |
| `A` `B` `C` `D` | Force **ON**, A–D |
| `1` `2` `3` `4` | **Pulse** ON for `[w]` ms, A–D |
| `!` `@` `#` `$` | **Toggle**, A–D |
| `w` | Set pulse width (1–60000 ms, default 100) |
| `s` | Show state of all four + pulse width |
| `0` | All outputs OFF |
| `Esc` | Return to main menu |

`[Esc]` is the canonical return-from-submenu key throughout — the framework
already renders `0x1B` as `"ESC"` in `p_c_char_to_str()`. All numeric prompts
follow one entry discipline: the prompt shows the present setting, an empty entry
keeps it, `Esc` abandons it unchanged, and an invalid entry warns and re-prompts
(with `Esc` as the way out, so a bad entry can't trap you). `i_getline()` is the
only input path, which keeps `v_app_polling_task()` pumped while blocked.

The main menu also carries **`[!]` soft reset** — it stops all cycling and forces
every output off, waits 250 ms, then `NVIC_SystemReset()`. Quiescing first matters:
TIM2 stays live through the delay, and once the pins go Hi-Z at reset the node
holds its last level on line capacitance for far longer than the reset-to-init
window — which only helps if that level is LOW.

Note: `MENU_ITEM_KEY_LIST_FUNCTION` entries are invisible to the menu help
printer, so the key map is spelled out in a `MENU_ITEM_HELP_TEXT_FIXED` block.

## Sense front-end — DAC-referenced comparators + ADC

The four sense channels are **intentionally asymmetric** — each has different
capabilities:

- **SENSE_A** — COMP1, with its **own dedicated DAC channel** (DAC1_CH1) setting
  the compare point.
- **SENSE_B** — COMP2, threshold from DAC1_CH2.
- **SENSE_C** — COMP3. *Intent* is to share DAC1_CH2 with SENSE_B; **as
  currently configured it does not** — it is on the fixed `1_2VREFINT` (½·VREFINT
  ≈ 0.6 V) reference. `COMP_INPUT_MINUS_DAC1_CH2` carries no per-instance
  restriction in the HAL asserts, so wiring COMP3 to DAC1_CH2 is a one-line
  CubeMX change whenever the sense work starts.
- **SENSE_D** — ADC1_IN0. The analog channel: best suited to oscilloscope-style
  logging, possibly combined with the ADC analog watchdog for threshold events.
  ADC also samples VREFINT for VDDA calibration (absolute voltage).

Comparators are currently `TriggerMode NONE` and **not started**.

**ADC sampling on a comparator input** (should it prove desirable on SENSE_A/B/C)
is possible despite CubeMX refusing the dual assignment: the pin is in analog
mode either way, and the ADC channel mux and comparator input mux are
independent. PA1 is the concrete candidate — `COMP1_INP` (IO3) *and* `ADC1_IN1`.
The known cost is the ADC sample-and-hold loading the pin with ~20 pF during the
sampling phase, which shifts the level appreciably if the source impedance is
high. Complications to be worked out only if this turns out to be wanted.

## Switch cycling — TIM2 compare driven

Full decision log with rationale: [`planning/switch-cycling-plan.md`](planning/switch-cycling-plan.md).

Each channel runs an independent ON/OFF square wave. Per-channel parameters —
**repeat count** (0 = run until stopped), **on-time** and **off-time** in whole
microseconds — are NVM-persisted and edited from the `[c]` submenu.

**TIM2 is now PSC 63 → 1 µs tick**, ARR `0xFFFFFFFF` (71.6 min wrap), NVIC
enabled at **priority 0**, SysTick demoted to priority 1.

**Fire-and-forget.** Starting a run drives the output high, then hands every
subsequent edge to the hardware: the channel switches to set-level-on-compare
(`TIM_OCMODE_ACTIVE` / `TIM_OCMODE_INACTIVE`, alternating per phase) and the ISR
only reprograms `CCRx`. The hardware places the edge, so ISR lateness costs
nothing until it exceeds the phase time. No foreground or job-queue involvement
while running.

- **Scheduling chains edge-to-edge** — the next compare is computed from the
  *previous CCR*, not from "now", so ISR latency never accumulates into the
  waveform. Arithmetic is modulo 2³², coherent only because ARR is exactly
  2³²−1. **Do not change ARR.**
- **Missed-compare guard:** `(int32_t)(next - CNT) < MIN_LEAD` → clamp to
  `CNT + MIN_LEAD`. Signed-difference-on-unsigned is the correct comparison
  across a wrapping counter. Without it, a late write costs a full 71.6-minute
  wrap of silence.
- **Counting is elapsed, not remaining.** `u32_cycles_done` counts completed ON
  pulses; the configured repeat count is never consumed. This displays sensibly
  during an infinite run and leaves all three settings ISR-read-only, which is
  what makes the design lock-free.
- **Every exit path parks the channel in `FORCED_INACTIVE`** — count exhausted,
  manual stop, stop-all, manual level override, or `[!]` reset. A run ends right
  after the final ON pulse's falling edge: output LOW, no runt pulse, no trailing
  dead time. That state is bit-identical to manual idle, so the `[s]` menu keys
  work with no special case.
- **Concurrency:** settings are ISR-read-only, so menu edits are plain aligned
  32-bit stores (atomic on M0+) that take effect at the next phase boundary.
  Only `TIM2->DIER` needs masking, being shared across channels. Start arms
  `CCxIE` last; stop disarms it first.
- **Completion** is reported via `JOB_CYCLE_COMPLETE` on the job queue —
  `v_job_add*()` is ISR-safe (PRIMASK save/restore), and `printf` can't run at
  priority 0.
- **Minimum on/off is 10 µs**, maximum 1000 s (bounded well below 2³¹ so the
  missed-compare guard stays valid).
- **`JOB_NVM_COMMIT` is deferred while any channel is cycling** — a flash page
  erase is tens of milliseconds against phase times as short as 10 µs. The job
  re-arms the auto-commit timer and retries; parameters reach flash once the run
  ends.

### Timing reality at the DUT

The DUT's 10 nF filter dominates: τ ≈ 495 µs rising, 2.40 ms falling, so it needs
~1.06 ms of ON to register a press and ~2.34 ms of OFF to read a valid release —
a minimum meaningful cycle of 4–5 ms. The µs resolution exists for **phase**
control, not short pulses: setting a cycle period to the DUT's 500 ms plus a
small Δ walks the button edge through the DUT's pulse window at Δ per cycle. The
menu warns (without blocking) below 3 ms.

Note SYSCLK is HSI16→PLL, ±1%, so phase also drifts on its own — good for
discovery, poor for repeatability. A crystal timebase is wish-list **W2**.

### Cycling menu — `[c]` from the main menu

| Keys | Action |
|---|---|
| `a` `b` `c` / `d` `e` `f` / `g` `h` `i` / `j` `k` `l` | repeat / on / off for A / B / C / D |
| `1` `2` `3` `4` | start-stop toggle, A–D |
| `0` | stop all cycling, outputs off |
| `?` `Esc` | help / return |

Parameter lines are drawn by a `MENU_ITEM_HELP_TEXT_VARIABLE` handler so each
shows its live value; the keys are bound by two `MENU_ITEM_KEY_LIST_FUNCTION`
entries, with `channel = index / 3` and `parameter = index % 3` — the same
arithmetic the NVM IDs use. Times display as raw µs plus an integer-derived
`(500.000 mS)` gloss; repeat 0 shows as `infinite`; start/stop lines carry live
run status.

## NVM pool — inherited-data hazard

The `.nvmdata` sector at `0x0807F800` is `NOLOAD`, so **reflashing never erases
it** — deliberate, so parameters survive firmware updates. The cost is that a
pool outlives the `nvm_param_id_t` enum that wrote it.

That bit on 2026-08-02: SWITCH_A and SWITCH_B came up with nonsense cycling
defaults while C and D were correct. This bench Nucleo is shared with
`G0B1_Skeleton` (and SwitchTester is a fork of it, itself forked from a GPS
baseline), so a previous project's firmware had written its own pool to the same
address with its own enum. When the cycling IDs `0x101`–`0x10C` were added they
landed on ID space that older pool already occupied; `x_nvm_create()` correctly
declined to overwrite existing objects and `x_nvm_get()` faithfully returned
their contents. Both behaved exactly as documented — the stale data was the
input, not a fault.

Cleared with the `[N]` pool-erase command; defaults now correct.

**CRC would not have caught it.** The inherited pool was not corrupt, it was
intact-but-foreign: valid signature, self-consistent contents. CRC detects
damage, not provenance. (Note `u32_crc32()` is stubbed to a constant in this
project anyway — the HAL CRC peripheral is not wired in, so validation is
signature-only.)

**Proposed guard, not yet implemented — pool ownership validation.**

`nvm_header_t` already carries `c_label[16]`, set from `x_nvm_pool_init()`'s
`p_c_label` argument (`"PARAMS"` here). It is **only ever written**, in the
format path; the restore path checks signature and CRC and never compares it.
So a caller-supplied pool-identity string already exists in the header,
unvalidated.

Adding a `strncmp` on restore turns it into an ownership check:

- **No layout change** — decisive, since altering `nvm_header_t` would invalidate
  every pool in every project already deployed with this API.
- No object or config-zone ID consumed.
- Checked in the header **before any object parsing**, so it cannot be confused
  by a damaged object chain — unlike an object-based version tag.
- 16 bytes of identity rather than 32 bits.
- Feeds the existing `NVM_ERROR_POOL_CORRUPT` → reformat path.

**Must be opt-in**, gated on `p_c_label != NULL` exactly as the format path
already is. Enabling it unconditionally would make every deployed device across
every project using this API reformat its pool on the next firmware update,
wiping field settings. Opt-in keeps the change purely additive.

**Alternative, and complementary — separate pool regions per project** (user idea,
2026-08-27). Give Skeleton and SwitchTester their own `.nvmdata` sectors in their own
linker scripts, rather than both landing on `0x0807F800`. Neither ever reads the
other's pool: each finds blank media the first time it runs after the other, formats
it, and creates its own defaults. It is a **port-tier change** — linker script only,
nothing in the vendored module, no runtime cost, no format change.

The two guards do different jobs and are worth having together:

| | Separate regions | Label check |
|---|---|---|
| Kind | **Prevention** | **Detection** |
| Covers | the known siblings that share this bench | *any* foreign pool, including a third project nobody remembered |
| Cost | 2 KB of address planning per project | one `strncmp` on restore |

Regions alone do not close the hole that actually bit here: the doc above records the
culprit as *"a previous project's firmware"* traceable to a GPS baseline, not to
Skeleton — and a third image can land on either address whatever these two agree
between themselves. The label check catches that case; regions stop the two projects
in daily rotation from ever reaching it.

**One decision if regions are taken:** whether each project reserves only its own
sector, or both.

- **Own sector only** — simpler. The other project's sector is ordinary flash, so
  code may be placed there and flashing one image can wipe the other's parameters.
  Harmless (the next run reformats), but settings do not survive an image swap.
- **Both sectors reserved in both projects** — 4 KB out of 510 KB in each, and both
  pools survive alternating flashes, which is the nicer bench behaviour when swapping
  images repeatedly.

Add a distinct `NVM_ERROR_POOL_FOREIGN` rather than reusing `POOL_CORRUPT`, so
the log reports *wrong owner* rather than *damaged* — precisely the distinction
that was missing when this bit.

Caveat: `"PARAMS"` is generic enough that `G0B1_Skeleton` likely passes the same
string, so it would **not** have caught the 2026-08-02 case. Project-unique
labels (`"SWTEST-PARAMS"`) are the caller discipline the scheme depends on.

Keep it application-facing in intent but implemented inside `nvmparams`, since
the check has to happen during pool init where the application has no hook.
The API is mission-critical with a decade of production use, so the additive,
opt-in shape matters more than the feature itself.

## NVM-persisted parameters

Thirteen `uint32_t` objects at IDs `0x100`–`0x10C`: the manual pulse width (ms),
then repeat/on/off per channel. IDs are contiguous so
`NVM_PARAM_CYCLE_A_REPEAT + (channel * 3) + parameter` resolves them, guarded by
`_Static_assert`. Created and loaded by `v_switch_out_nvm_init()`, called from
`v_param_init()` **after** `x_nvm_pool_init()` and **before** `x_nvm_commit()`,
so a virgin pool creates all thirteen in one flash write. Defaults: on 500000 µs,
off 500000 µs, repeat 0, pulse width 100 ms. About 130 of the 512-byte pool.

## Event queue — vendored variable-length FIFO, bench-verified 2026-08-27

`App/event_queue/` — a vendorable ring FIFO of variable-length records: 16-bit
event ID + 16-bit true payload size + 0..65535 payload bytes, copy-in/copy-out,
each record occupying header+payload rounded up to a multiple of 4. C stdlib
only; config seam at `App/Inc/event_queue_config.h`. Full decision log:
[`planning/event-queue-plan.md`](planning/event-queue-plan.md) (19 green, 1
deferred, no open rows).

Contract highlights: SPSC (one producer context, one consumer context) is
lock-free with **zero interrupt masking** — monotonic per-side counters, single
writer each, aligned 32-bit atomicity. Multiple producers supply a per-queue
lock/unlock function-pointer pair that wraps the put path only; the consumer
never masks. Status enum is a packed int16: negative = real error (put-on-full
drops the event and says so), positive = information (get-on-empty, truncated
get), 0 = OK. `create` takes a `const` (ROM-able, C99-designated) config where
every zero member is a sane default, and refuses an already-live handle.

Same-day post-verification additions (user-directed): a non-multiple-of-4
create size **rounds down** and reports `EQ_STATUS_SIZE_ROUNDED`;
`EVENT_QUEUE_MALLOC/FREE` macros in the adoption header route internal
allocation to an alternative (RTOS) allocator; `x_event_queue_peek`
(non-consumptive get — a truncated peek discards nothing) and
`x_event_queue_flush` (consumer-side drain-all, idempotent).

Bench validation is `test_eventq.py` (17/17) over acon op `F` — including a
true-ISR producer soak: the 1 ms tick callback (`v_eventq_test_tick()` in
`app_main.c`) puts sequence-stamped events in interrupt context while the host
drains concurrently, asserting a contiguous sequence and exact put+drop
accounting. `ACON_EMIT_MAX` was raised 128 → 512 for the byte-echo replies.
The intended first customer is automation-console phase 2 (async events via
`v_acon_flush_events()` / `JOB_CYCLE_COMPLETE`).

## Done — cycler and automation console

### Timer-driven cycling — bench-verified 2026-08-03

Runs at the programmed rate, the completed-cycle count advances, and the level
bitmap read back from GPIO `IDR` confirms the pad really toggles rather than the
software merely claiming it does. Decision log:
[`planning/switch-cycling-plan.md`](planning/switch-cycling-plan.md).

### Automation console, phase 1 — bench-verified 2026-08-03

`App/{Inc,Src}/automation_console.*`. Named for what it is: **not** a test
harness but the interface *for* one, and not test-only — it is how an external
host drives the instrument.

> **Using it: [`acon-reference.md`](acon-reference.md)** — the reference manual. Every op with
> its syntax, reply tokens and failure modes, plus session entry/exit, frame grammar, worked
> sequences and the host driver. **Keep it current in the same change that alters an op, the
> protocol or a documented limit.**

Full decision log:
[`planning/automation-console-plan.md`](planning/automation-console-plan.md)
(30 green, 9 deferred to phase 2, no open rows) — that is the *why*; the reference above is
the *how*.

Entered from the debug menu — the `ACON_ENTER` (0xDA) sentinel selects SCRIPT
mode, the `[a]` menu key selects HUMAN mode — so the entry path carries the
intent and neither common case needs a mode command. Both modes share one
dispatcher and one response format, so a command tried by hand behaves exactly
as it will from a script.

Wire format: ASCII, comma-separated, hex numerics, one line per response unless
the header declares `K<n>` payload lines. Sigil in column 0 — `=` ok, `!` error,
`+` payload, `*` reserved for phase-2 events, `#` ignorable. CR terminates and
**LF is discarded everywhere**, which is what lets a bare CR be a no-op that
answers without a CRLF host getting a spurious second frame per command.

Nine commands: `S` set levels (Select/Set/Clear, BSRR-style), `R` read state,
`W`/`G` write and get cycle parameters, `C`/`X` start and stop cycling, `P`
persist to NVM, `E` transport error counters, `U` UART loopback stress test.
Plus `V` `L` `?` `Z` `Q` `^C` builtins.

In SCRIPT mode the protocol path does not touch stdio in either direction —
`v_acon_emit()` out and `i16_uart_stream_rx_byte()` in, both straight to
`uart_stream`. That is what lets stdout be suppressed wholesale during a session,
and it makes moving the console to a different UART a matter of passing a
different handle.

### HIL test suite — `scripts/hil/`

`acon.py` is the host driver; `test_acon.py` is 47 tests over the SCRIPT-facing
side, all passing against the board:

```
python scripts/hil/test_acon.py --port COM3          # add --slow for the 15 s timeout test
```

It has already earned its keep twice: it found the RX ring being *smaller* than
the longest legal command line, and it characterised the UART performance
envelope below.

### Baud sweep -- measured 2026-08-09

The `B` command walks a UART's loopback up a ladder of baud rates, reporting loss at each
rung rather than pass/fail, and returns the rate the `BRR` divisor *actually* produced
alongside the one requested. With no rate list it uses a built-in ladder whose steps bunch
up above 230400, where the FIFO-less instances give out; a host can pass its own list to
bisect a knee without a reflash.

512 bytes per rung, one burst:

| requested | actual | USART5 (no FIFO) | USART1 (FIFO) |
|---|---|---|---|
| 9600 .. 403200 | -- | lossless | lossless |
| 460800 | 460431 | **448/512, 64 errors** | lossless |
| 691200 | 688172 | **341/512, 171 errors** | lossless |
| 921600 | 927536 | **256/512, 256 errors** | lossless |

**This refines the 2026-08-04 figure.** The coarse doubling ladder put USART5's ceiling at
230400 with 460800 "marginal"; the finer rungs show it is in fact **lossless through
403200**, with a hard break at 460800. Nothing changed on the bench -- the earlier ladder
simply had no rungs between 230400 and 460800.

Two things fall out of the numbers. The loss is a clean gradient, not a cliff, and at
921600 USART5 receives **exactly half** the bytes -- the signature of servicing one byte
per two character times. And `actual` diverges from `requested` because `BRR` is an integer
divisor: at 64 MHz, 921600 lands on BRR=69 for a true 927536, **+0.64%**. Harmless in
self-loopback, where both ends share the divisor, but it eats into the budget when a UART
talks to an external device.

A sweep needs no loopback probe: the slowest rung *is* the wiring test, since no FIFO
effect exists at 9600. Loss at every rung including the slowest means the jumper; loss only
above some rate means the ceiling. That distinction cost real bench time on 2026-08-09 --
the `U` command's 8-byte probe drops bytes at 921600 on a FIFO-less instance and reports
`LOOP` (no loopback), pointing at wiring that was never at fault.

### UART performance envelope — measured 2026-08-04

The `U` command loopback-tests any bindable UART. Results on this bench, 64 B to
8 kB bursts, 4 per size, all channels jumpered Tx↔Rx with identical 10 cm wires:

| UART | FIFO | baud | result |
|---|:--:|---|---|
| USART1 | yes | 921600 | lossless, 88 kB/s — 96% of line rate |
| LPUART1 | yes | 921600 | lossless, 83 kB/s — 90% |
| LPUART2 | yes | 921600 | lossless, 88 kB/s — 96% |
| USART3 | yes | 115200 | lossless — 99% |
| USART4 | **no** | 115200 | lossless — 99% |
| USART5 | **no** | 921600 | **fails**; see the baud sweep above |
| USART6 | **no** | 921600 | **fails**; jumpered since 2026-08-09 |

**USART4/5/6 have no hardware FIFO** on this part — only USART1/2/3 and
LPUART1/2 do, which is why CubeMX emits `HAL_UARTEx_Set*FifoThreshold()` for
exactly those five. Without a FIFO the ISR must read `RDR` within **one
character time** — 10.85 µs at 921600 — and USART5 shares
`USART3_4_5_6_LPUART1_IRQn` with four other UARTs, so every received byte also
costs four idle `HAL_UART_IRQHandler()` passes. The measured ceiling tracks the
character-time budget exactly: 43 µs/char clean, 21.7 µs marginal, 10.85 µs
hopeless.

Ruled out by experiment, so nobody re-chases them: the wire (replaced, identical
result), the pins (PC12→PB1 verified by direct GPIO drive with a pull-down), the
drive strength (`VERY_HIGH` changed nothing), which end transmits (`CR2.SWAP`
changed nothing), a board-level second driver (PB1 ignores PB8/PB9, holds its
pull-up), and the divider (`BRR` identical to USART1, and in *self*-loopback a
divider error cancels anyway).

**Practical rule: FIFO-less instances are good to 230400; the rest to 921600.**
Going faster on a FIFO-less instance means DMA rather than tuning — the technique
is banked in [`UART-DMA-Streaming.md`](UART-DMA-Streaming.md). **Not planned**;
nothing needs it today.

### SPI flash testbed — bench-verified 2026-08-18

Testbed for the nvmparams SPI-flash driver (the mirror project is migrating NVM
from STM32 internal flash to SPI NOR). **Temporary** — the driver graft and the
`Y`/`p` console hooks are slated for removal once the vendorable `spiflash`
module lands. See `G0B1_Skeleton/Docs/planning/nvmparams-plan.md` phase 2.

**Pins — SPI3 + CS + a sense lead:**

| Function | Net label | Pin | Peripheral | Notes |
|---|---|---|---|---|
| Flash SCK | SPIFLASH_SCK | PB3 | SPI3_SCK (AF9) | mode 0, 8 MHz |
| Flash MISO | SPIFLASH_MISO | PB4 | SPI3_MISO (AF9) | PB4 freed for this by moving Sense B to PB6 (COMP2 IO2) |
| Flash MOSI | SPIFLASH_MOSI | PC12 | SPI3_MOSI (AF4) | |
| Flash CS | SPIFLASH_NCS | PA15 | GPIO out, push-pull | moved off hardware NSS so CS holds low across a multi-phase command |
| Test sense | TEST_INPUT | PC3 | GPIO in, pull-up | temporary probe lead for the loopback checks below |

SPI3 has **TX/RX DMA** (DMA1 Ch1 RX, Ch2 TX, linear mode); the driver's
read/write use `HAL_SPI_*_DMA` and depend on the channel IRQ handlers.

**Driver.** The legacy `MX25R80.c` (the user's own code) was grafted in with its
pseudo-filesystem/directory support stripped — not the `ee_fw-lib` template,
which is a non-functional stub. `App/Src/nvm_driver_spiflash.c` is the minimal
nvmparams storage driver over it; `nvm_test.c` gains a flash-backed pool and an
`N,P` backend selector so the existing HIL suite runs on flash.

**Console hooks (temporary).** `Y` sub-ops for bring-up: `I` JEDEC id, `S`
status, `T` DMA erase/write/read round-trip, `L` MOSI↔MISO loopback, `N` CS
loopback (via PC3), `J` SCK loopback, `C` park CS, `K` clock burst. Debug-menu
`p` is a hands-free pin monitor (IDR readback of all lines while toggling).

**Verification.** nvmparams HIL **28/28 on real SPI flash** (`test_nvm.py
--backend flash` — fault injection, corrupt/blank, full lifecycle) plus
`test_nvm_persist.py`, which proves a committed value survives a **hardware
reset**. Bench part: Winbond **W25Q128** (`EF 40 18`).

**Gotcha banked — RDID opcode.** The legacy `read_id` sent `0x9E`, a
Macronix-specific alias; JEDEC-standard Read-ID is **`0x9F`**. `0x9E` reads
`00 00 00` on W25Q/SST parts, looking exactly like dead wiring — it cost a long
bench session before the transmitted opcode was checked. The vendorable module
must use `0x9F`.

## Banked for later — each gets its own planning pass

> **Forward plan:** [`planning/switchtester-roadmap.md`](planning/switchtester-roadmap.md)
> now carries the user's stated intent for finishing the project (2026-08-27) — the
> four remaining tasks, their order, the open questions, and how the items below fit
> into them. Read it before starting any of this work. The `event_queue` module that
> the async-event and sense-event work will build on is **built and bench-verified**.

### 1. Automation console, phase 2 — async events

Designed and fully decided; not built. Device-originated frames (cycle
transitions, sense edges, captured data), never interleaved into a response,
queued while a transaction is in flight and flushed between frames, timestamped
from `TIM2->CNT` at 1 µs. Rows **S6**–**S9**, **S12**, **I5** in the
automation-console plan carry the design.

### 2. Sense-input operation

Polled vs interrupt/EXTI, DAC threshold setup and start-up sequencing, ADC
channel handling, bounce/latency measurement. `TIM2->CNT` is available as a
common 32-bit timestamp source shared with the drive side (1 µs resolution since
the PSC 63 change).

**Open-ended by choice, and now the largest remaining task** (roadmap task 1) — the best use of the sense inputs is not yet
decided, and that question needs answering before any design work. The four
channels are intentionally asymmetric (see the sense front-end section above),
so "what should each one measure" is the real first question, not "polled or
interrupt".

## Resolved

- Periodic tick is **1 ms** — `PERIODIC_TIMER_INTERVAL_MS = 1`, TIM14 PSC 63 /
  ARR 999 = 64 MHz/64/1000. (The old "verify the tick rate" TODO; stale comments
  in `app_main.c` referring to 10 ms and htim6 have been corrected.)
- ADC channel set is **IN0 + VREFINT**.
- Switch drive scheme — forced-OC, above.
