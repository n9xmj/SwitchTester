# SwitchTester — Design Notes

> Status: **manual switch control + timer-driven cycling implemented**
> (debug-menu driven, NVM-persisted). Cycling builds clean but is **not yet
> bench-verified**. The sense front-end and the automation backdoor are designed
> but not built — each is banked for its own planning pass below.
> Last touched 2026-08-01.

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
| Sense B | SENSE_B | PB4 | COMP2 (+) via IO1 | threshold = DAC1_CH2 (adjustable) |
| Sense C | SENSE_C | PB0 | COMP3 (+) via IO1 | threshold = **½·VREFINT** (fixed, ≈0.6 V) — see note |
| Sense D | SENSE_D | PA0 | ADC1_IN0 | analog read; ADC also samples VREFINT |
| DAC ref 1 | — | PA4 | DAC1_CH1 | buffered, ext (PA4) + internal → COMP1 (−) |
| DAC ref 2 | — | — | DAC1_CH2 | unbuffered, internal only → COMP2 (−) |
| LED | NUCLEO_LED | PA5 | GPIO out | |
| Button | NUCLEO_BUTTON | PC13 | EXTI falling | |
| Console | DEBUG_TX/RX | PA2/PA3 | USART2 | 921600 |

COMP input assignments verified against `stm32g0xx_hal_comp.h`: COMP1_INP IO3 =
PA1, COMP2_INP IO1 = PB4, COMP3_INP IO1 = PB0. All three match.

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

## NVM-persisted parameters

Thirteen `uint32_t` objects at IDs `0x100`–`0x10C`: the manual pulse width (ms),
then repeat/on/off per channel. IDs are contiguous so
`NVM_PARAM_CYCLE_A_REPEAT + (channel * 3) + parameter` resolves them, guarded by
`_Static_assert`. Created and loaded by `v_switch_out_nvm_init()`, called from
`v_param_init()` **after** `x_nvm_pool_init()` and **before** `x_nvm_commit()`,
so a virgin pool creates all thirteen in one flash write. Defaults: on 500000 µs,
off 500000 µs, repeat 0, pulse width 100 ms. About 130 of the 512-byte pool.

## Banked for later — each gets its own planning pass

Deliberately out of scope until planned properly, in intended order:

### 0. Bench-test and debug the cycler

Immediate next step. Code is written and builds clean; nothing has been run on
hardware yet.

### 1. Automation / HIL backdoor

A hook in `v_debug_menu_service()` (`debug_menu.c`) that **bypasses the menu
system**, letting a host-side automation runner — e.g. a Python script driving a
simple REPL — control the switch tester over the *same* UART the menu uses. The
goal is a **deterministic script interface** for automating test runs.

Deliberately sequenced **before** the sense work: once the cycler is trustworthy,
scripted control is what makes long soak campaigns and phase sweeps practical,
and the sense work will want that automation already in place to drive it.

Being computer-driven rather than human-driven, this may need a proper
interrupt-driven circular-buffer UART manager; the user's existing `uart_stream`
API is the candidate to import. Reference implementation of both `uart_stream`
and the HIL-runner-to-debug-menu hook:
`C:\STM32\CubeSource\LED_Strip_Controller_G474`. **Not to be followed slavishly**
— it is one possible approach among several.

### 2. Sense-input operation

Polled vs interrupt/EXTI, DAC threshold setup and start-up sequencing, ADC
channel handling, bounce/latency measurement. `TIM2->CNT` is available as a
common 32-bit timestamp source shared with the drive side (1 µs resolution since
the PSC 63 change).

**Last, and open-ended by choice** — the best use of the sense inputs is not yet
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
