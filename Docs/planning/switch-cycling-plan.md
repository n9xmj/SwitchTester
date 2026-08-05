# Switch Cycling — implementation plan (decision log)

**Feature:** timer-driven automatic cycling of the SWITCH_A–D outputs. Each channel
runs an independent ON/OFF square wave with programmable on-time, off-time and
repeat count, driven fire-and-forget from TIM2 output-compare interrupts with no
foreground or job-queue involvement while running.

**Home:** `App/Src/switch_out.c` / `App/Inc/switch_out.h` (extends the existing
manual-drive module) · menu in `App/Src/debug_menu.c`.

**Parent spec:** [`../SwitchTester-Design.md`](../SwitchTester-Design.md) — the
contract. This plan is the negotiation log; sync the spec from it once decisions
land (**T2**).

**Status:** DONE — board fully green; **bench-verified 2026-08-03**. Runs at the
programmed rate, the completed-cycle count advances, and the GPIO `IDR` readback
confirms the pad really toggles. Exercised from the automation console (`W`/`C`/
`G`/`X`) and covered by the HIL suite in `scripts/hil/`.

**Planning model:** [`decision-log-model.md`](decision-log-model.md). No Must-Ship-Gap
fence — just a Big Board + wish list.

**Working mode:** resolve OPEN items in chat **one at a time**; update the table and
the matching detail section as each decision lands. Agent does not silently green a
🔴/🟡 — it records a leaning and waits for confirmation.

---

## Brief

The SwitchTester drives four CD4066 analog switches (via 330 Ω + indicator LEDs) that
sit across the pushbutton of a DUT. The DUT is an STM32 + PIC board whose button-sense
line is a 62 kΩ/240 kΩ divider with a 10 nF filter cap; the STM32 periodically flips
that GPIO to an output and pulses it low for 30 ms every 500 ms to stop the PIC's
power-off timer during a long press. A rare race leaves the button "locked out" — the
STM32 stops seeing transitions. This tester exists to reproduce and instrument that
failure by driving repeatable, precisely-timed button presses at variable rates and
duty cycles.

Manual drive already ships and is HIL-verified. This plan covers **v1 of automatic
cycling**: per-channel on-time, off-time and repeat count (0 = run until stopped), all
in integer microseconds, persisted in NVM, edited from a dedicated console submenu, and
executed by reprogramming `TIM2->CCRx` from the compare ISR. Sense-input feedback,
phase-walk automation and the HIL backdoor are explicitly out of scope — see the wish
list.

---

## The Big Board

| ID | Status | Subject |
|----|--------|---------|
| **D1** | 🟢 | Menu invocation — main menu `[c]` → cycling submenu via `MENU_ITEM_CALL_MENU` |
| **D2** | 🟢 | Key map — `a`–`l` parameters, `1`–`4` start/stop, `0` stop-all, `?`, `Esc` |
| **D3** | 🟢 | Display — variable help text, µs plus integer-derived mS gloss, `infinite`, live status column |
| **D4** | 🟢 | Menu table shape — 2-entry key-list collapse |
| **D5** | 🟢 | Input discipline — prompt shows current; empty keeps; ESC aborts; invalid re-prompts |
| **D6** | 🟢 | `[Esc]` is the sole submenu exit; main-menu `[S]` → `[s]` |
| **D7** | 🟢 | Parameter units — integer microseconds; no floating point anywhere |
| **D8** | 🟢 | `[!]` soft reset — quiesce outputs first, then `HAL_Delay(250)` + `NVIC_SystemReset()` |
| **S1** | 🟢 | OC mode during a run — set-level-on-match (`ACTIVE`/`INACTIVE`), not `TOGGLE` |
| **S2** | 🟢 | Cycle counting — count **elapsed**, not remaining; repeat 0 = infinite |
| **S3** | 🟢 | Run end — always `FORCED_INACTIVE`, output LOW, `CCxIE` cleared |
| **S4** | 🟢 | Missed-compare guard — signed 32-bit difference, clamp to `CNT + min_lead` |
| **S5** | 🟢 | Minimum on/off time — 10 µs |
| **S6** | 🟢 | Live parameter edits take effect at the next phase boundary |
| **S7** | 🟢 | A manual level command on a cycling channel stops the cycle first |
| **S8** | 🟢 | Start begins with the ON phase; stop is an immediate abort |
| **S9** | 🟢 | Run completion reported via `JOB_CYCLE_COMPLETE` on the job queue |
| **S10** | 🟢 | Sleep test `[W]` is **not** guarded against an active cycle |
| **S11** | 🟢 | `JOB_NVM_COMMIT` deferred while any channel is cycling |
| **I1** | 🟢 | Cycle engine lives in `switch_out.c`, not a separate module |
| **I2** | 🟢 | `switch_cycle_t` struct + `g_x_switch_cycle[SWITCH_OUT_COUNT]` |
| **I3** | 🟢 | ISR hand-rolled in `USER CODE BEGIN TIM2_IRQn 0`; one `SR` read, all four flags |
| **I4** | 🟢 | Concurrency — settings are ISR-read-only; atomic word stores; start/stop disarms first |
| **I5** | 🟢 | NVM persistence — 13 IDs at `0x100`–`0x10C`, created in `v_switch_out_nvm_init()` |
| **I6** | 🟢 | TIM2 config — PSC 63, ARR 2³²−1, NVIC prio 0, SysTick prio 1, init `FORCED_INACTIVE` |
| **I7** | 🟢 | Delete the two dead constants in `platform.h` |
| **T1** | 🟢 | Port a SwitchTester-adapted `decision-log-model.md` into `Docs/planning/` |
| **T2** | 🟢 | Sync `SwitchTester-Design.md` from this plan once implementation lands |

---

## Wish list (v2+)

| ID | Subject | Notes |
|----|---------|-------|
| **W1** | Fold the manual-menu pulse (`1`–`4` in the `[s]` submenu) into the cycle engine as repeat = 1 | Pulse currently runs off the 1 ms TIM14 tick; becomes redundant once **I2/I3** land |
| **W2** | Crystal/HSE timebase for repeatable phase-walk experiments | SYSCLK is HSI16→PLL today (±1%); free phase sweep is good for *discovery*, bad for *repeatability* |
| **W3** | Phase-walk helper — set cycle period to DUT period + Δ automatically | Depends on **W2** for meaningful control |
| **W4** | Sense-triggered / sense-instrumented cycling | Blocked on the SENSE-inputs feature; gets its own D-log |
| **W5** | HIL automation backdoor in `v_debug_menu_service()` | May need the user's `uart_stream` API; reference impl in `LED_Strip_Controller_G474` |
| **W6** | Multi-sector wear levelling for `nvmparams` | Noted by the user as a known gap in the API |
| **W7** | **Arbitrary square-wave generator** — DMA feeds CCR from a preloaded edge buffer, so a cycle need not be evenly spaced | Long-term; sketch below. Interrupts stay enabled alongside DMA for event recording |

### W7 — arbitrary-waveform cycling via DMA *(sketch, not a design)*

Recorded 2026-08-02 while fresh; no design work has started.

**The idea.** Today's cycle engine is PWM-shaped: `on_time + off_time` *is* the
period, and every cycle is identical. Instead, preload a buffer of CCR values and
let DMA transfer the next one into the channel's CCR on each compare match. The
sequence becomes arbitrary — 10 ms on, 50 ms off, 100 ms on, 20 ms off, … up to
the buffer length — and on buffer exhaustion the DMA re-arms and replays from the
start. The CPU is out of the edge path entirely.

**Why it fits what is already here.** The ISR's current job is exactly "write the
next compare value into CCR"; DMA does that same store without waking the core.
`CCxIE` and `CCxDE` can both be set, so the compare still raises its interrupt for
event recording (the REPL's **S6**/**S8** event queue) while DMA handles the
rearm. G0B1 has twelve DMA channels across DMA1/DMA2 with DMAMUX, so four
independent switch channels are affordable.

**The gotcha to remember, because it is not obvious.** DMA stores a value; it
cannot add. The buffer therefore holds *absolute* CCR values, and on the second
pass those absolute times are all in the past. Two ways out, with very different
costs:

- **Re-base in software** on the DMA transfer-complete interrupt: add the total
  sequence duration to every entry, once per pass. O(N) per pass, N small, at
  roughly 1 Hz — genuinely cheap, and it preserves everything below.
- **ARR-modulated burst DMA** (the classic STM32 arbitrary-waveform trick, writing
  ARR+CCR pairs through `DCR`/`DMAR`) is relative by construction and needs no
  re-basing — **but it would destroy the free-running 32-bit timebase.** `ARR` is
  `0xFFFFFFFF` today, and that is precisely what makes the cycler's modulo-2³²
  compare arithmetic coherent *and* what makes `TIM2->CNT` usable as the shared
  1 µs timestamp source (see **S8** in
  [`automation-console-plan.md`](automation-console-plan.md)). Taking this route means moving the
  timestamp base to another timer first.

So the re-basing approach is almost certainly the one to take, and the reason is
worth writing down now: TIM2 is not just the switch driver, it is the project's
clock.

---

## LOCKED CONTEXT

Settled; do not re-litigate unless reopened.

**External hardware.** Active HIGH. Each SWITCH_x drives a 330 Ω + indicator LED
(~4.5 mA) and, ahead of the resistor, a CD4066 control input needing the full
0↔3.3 V swing. The 4066 switch outputs go across the DUT's N.O. pushbutton.

**DUT button-sense network.** 62 kΩ to 3.3 V → button → 240 kΩ to GND, with 10 nF to
GND at the junction; that node (`PUSH_BUTTON_INT`) feeds both an STM32 GPIO and a PIC
power-controller input. Measured in-circuit: **29 mV** with the tester's channel off,
**2.66–2.97 V** with it on. The 29 mV implies ~121 nA leakage → **off-resistance
≈ 27 MΩ**, consistent with the CD4066 datasheet; the earlier 400 kΩ bench reading was
a measurement artifact. VIH/VIL margins confirmed good.

**DUT response floor.** The 10 nF sets τ ≈ 495 µs rising / 2.40 ms falling, so the DUT
needs ~1.06 ms of ON to register a press and ~2.34 ms of OFF to read a valid release —
a minimum meaningful cycle around 4–5 ms. The tester's 10 µs resolution (**S5**) exists
for *phase* control, not for short pulses.

**Reset behaviour.** SWITCH pins go Hi-Z for ~1–3 ms through any reset (power-on, NRST,
debugger, `[!]`), floating the 4066 control inputs. Consequences understood and
**accepted**; no external pulldowns will be added.

**Manual drive (already shipped, HIL-verified 2026-07-31).** Pins stay permanently
AF2/TIM2; level is the channel's `OCxM` field (`FORCED_ACTIVE`/`FORCED_INACTIVE`).
Polarity lives in `CCER.CCxP`, so there are no polarity `#define`s. Drive state is read
back from `OCxM` — `x_switch_out_get()` returns `SWITCH_OUT_TIMED` during a cycle run
for free.

**Peripheral config (verified in tree).** TIM2 PSC 63 → 1 µs tick, 71.6 min wrap;
ARR `0xFFFFFFFF`; TIM2 NVIC enabled at priority 0; SysTick demoted to priority 1;
`MX_TIM2_Init()` sets all four channels to `FORCED_INACTIVE`. COMP3 minus moved to
DAC1_CH2 so SENSE_B and SENSE_C share a DAC channel.

**Unconfirmed observation (not a decision).** The 2.66→2.97 V fluctuation on the sense
node fits an STM32 internal pull-up (~47 kΩ) being enabled part-time far better than it
fits IRLED supply transients — the 10 nF filters a tens-of-µs disturbance to a few mV.
If real, the pin's *mode* changes during a press, which is a third state variable in
the race. Worth a DC-coupled scope check before attributing cycling results to the
tester.

---

## Resolved items

### D1 — Menu invocation and submenu structure
**Status:** 🟢 — Main menu `[c]` opens the cycling submenu via `MENU_ITEM_CALL_MENU`. No
key clash: the main menu holds `!`, `?`, Enter, `s`, `W`, `q`, `Q`. Menu stack depth 4 is
ample.

### D2 — Key map
**Status:** 🟢

| Keys | Action |
|---|---|
| `a` `b` `c` / `d` `e` `f` / `g` `h` `i` / `j` `k` `l` | repeat / on-time / off-time for A / B / C / D |
| `1` `2` `3` `4` | start-stop toggle, A–D |
| `0` | stop all cyclers, force all outputs low |
| `?` `Esc` | help / return |

### D3 — Display rendering
**Status:** 🟢 — Parameter lines drawn by a `MENU_ITEM_HELP_TEXT_VARIABLE` handler that
emits its own `[key] label value` text. Times shown as raw µs plus an integer-derived
gloss, e.g. `500000  (500.000 mS)` — computed with `/ 1000UL` and `% 1000UL`, the
fractional field zero-padded `%03lu` (without the pad, 500001 µs renders as `500.1`).
Repeat 0 displays as `infinite`. Start/stop lines carry live status —
`-- idle --` or `RUNNING, 4231 of 10000` / `RUNNING, 4231 cycles`.

### D4 — Menu table shape
**Status:** 🟢

**Options considered:** twelve `MENU_ITEM_HELP_TEXT_VARIABLE` + twelve null-text
`MENU_ITEM_FUNCTION` pairs (24 entries, explicit and greppable, each key visibly bound
beside its label) versus a two-entry collapse. Console output is byte-identical either way.

**Resolution:** the **2-entry collapse** — one variable-help function looping all four
blocks, plus one `MENU_ITEM_KEY_LIST_FUNCTION` over `"abcdefghijkl"` decoding
`channel = i / 3`, `parameter = i % 3`. `"1234"` collapses the same way. Fewer helper
functions, and the decode mirrors the NVM ID arithmetic in **I5** so one mental model
covers both.

### D5 — Input discipline
**Status:** 🟢 — Applies to every prompt including the existing `[w]` pulse width.
`i_getline()` is the only input path, so `v_app_polling_task()` stays pumped.

| Return | Meaning | Action |
|---|---|---|
| `-1` | ESC | abort, setting unchanged |
| `0` | empty entry | keep present setting |
| `>0` | text | parse; on failure print "invalid" and re-prompt |

Re-prompt loop is unbounded with ESC as the exit, so a bad entry can't trap the user.
The two ESC roles don't collide: inside a prompt `i_getline()` consumes it as
cancel-entry, at menu level it exits the submenu.

### D6 — ESC as canonical exit
**Status:** 🟢 — `MENU_ITEM_RETURN_TO_PREVIOUS_MENU` with `.key = 0x1B`.
`p_c_char_to_str()` already maps `0x1B` → `"ESC"`, so it renders as
`[ESC] Return to previous menu` with no special-casing, and `str_key[4]` fits it exactly.
Applies to **all** submenus — `x` is removed from the switch-output submenu, not aliased.
Main menu `[S]` becomes `[s]`.

### D7 — Units and floating point
**Status:** 🟢 — All times are integer microseconds in `uint32_t` (max 4294 s). The
project is verified FP-clean and stays that way; `--specs=nano.specs` without
`-u _printf_float` means `%f` wouldn't link usefully anyway. No `ldiv()` — it takes
signed `long` and a µs value can exceed 2³¹. Plain `/` and `%` on the unsigned value;
GCC folds the pair into one `__aeabi_uidivmod`.

### D8 — `[!]` soft reset behaviour
**Status:** 🟢 — Emergency kill switch at the top of the main menu; a fixture the user
keeps in most projects.

**Resolution:** stop all cycling and force all outputs off, then `HAL_Delay(250)`, then
`NVIC_SystemReset()`. Quiescing first matters for two reasons: TIM2 and its interrupt stay
live through the whole `HAL_Delay(250)`, so a running cycle would otherwise keep driving
the DUT for a further quarter second; and once the pins go Hi-Z the node holds its last
level on line capacitance — roughly 20–50 pF of trace, 4066 input and LED junction against
only pA-scale leakage (the CD4066 control input, and the LED branch which is effectively
open below its ~1.8 V forward drop), giving a decay constant in the tens of milliseconds
versus a 1–3 ms reset-to-init window. That capacitance holds the node **low** only because
the outputs were forced off first; a channel left high would be held **on** through the
window by the same mechanism.

### S1 — Output-compare mode during a run
**Status:** 🟢 — `TIM_OCMODE_ACTIVE` / `TIM_OCMODE_INACTIVE` on match, alternating per
phase. Rejected `TOGGLE`: it makes the level sequence implicit, can't resynchronise, and
needs intervention to stop at a known level. The extra `CCMR` write costs a few cycles on
M0+; determinism is worth more in a rig built to instrument a race. Side effect: the
question of whether `CCxIF` fires in *forced* mode never arises, since a channel is never
both forced and interrupt-armed.

### S2 — Cycle counting
**Status:** 🟢 — `u32_cycles_done` counts **up**; the configured `u32_repeat_count` is
never consumed. Stop test is `if (u32_repeat_count && (u32_cycles_done >= u32_repeat_count))`.
Two reasons over a decrementing counter: the default config is infinite, where "remaining"
has nothing to display but elapsed reads naturally; and it leaves *all three* settings
ISR-read-only, which is what makes **I4** lock-free. Increment once per full ON+OFF cycle,
at the end of the OFF phase.

### S3 — Run end state
**Status:** 🟢 — Every exit path (count exhausted, `1`–`4` stop, `0` stop-all, manual
override per **S7**, `[!]` reset per **D8**) sets `OCxM = FORCED_INACTIVE` and clears
`CCxIE`, leaving the output **LOW**. That is bit-identical to the manual idle state, so
`x_switch_out_get()` reports `SWITCH_OUT_OFF` and every manual key works with no special
case. Rejected "return to frozen": `OCxM` is a single field so frozen-and-forced-low isn't
reachable, frozen merely *holds* the last level rather than asserting low, and it would
decode as `SWITCH_OUT_TIMED` forever.

### S4 — Missed-compare guard
**Status:** 🟢 — After computing the next CCR, test `(int32_t)(u32_next - TIM2->CNT) <= 0`
and clamp to `CNT + min_lead` if the counter has already passed it. Signed-difference-on-
unsigned is the correct comparison across a wrapping counter; a plain `<` is wrong. Without
it a late write costs a full **71.6-minute** wrap of silence. Two instructions. Justified
despite the generous margin in **S5** by the one thing that can blow past it by orders of
magnitude — a flash erase in `x_nvm_commit()` (see **S11**).

### S5 — Minimum on/off time
**Status:** 🟢 — 10 µs. At 1 µs/tick that is 640 CPU cycles at 64 MHz for a handful of
register writes at priority 0 — ample. Modulo-2³² CCR arithmetic works because ARR is
exactly 2³²−1; that dependency gets a comment at the point of use so nobody "tidies" ARR.

### S6 — Live parameter edits
**Status:** 🟢 — The ISR re-reads on-time and off-time from the struct each time it
reprograms, so a foreground write takes effect at the next phase boundary with no
handshake.

### S7 — Manual override
**Status:** 🟢 — A manual level command (`s` submenu force on/off/toggle) on a cycling
channel stops the cycle first. Both `[0]` keys therefore converge on the same end state,
which keeps them consistent.

### S8 — Start/stop semantics
**Status:** 🟢 — `1`–`4` are toggles. Start begins with the ON phase; pressing the key
again is an immediate abort, not a graceful finish — the end state is released either way
per **S3**.

### S9 — Completion reporting
**Status:** 🟢 — `printf` can't run in a priority-0 ISR, so on the final OFF phase the ISR
calls `v_job_add_with_params(NULL, JOB_CYCLE_COMPLETE, u8_channel, 0)` and the foreground
dispatcher prints. Verified ISR-safe: `v_job_add*()` wraps the queue update in
`SAVE_AND_DISABLE_INTERRUPTS()`/`RESTORE_INTERRUPTS()`, which save and restore PRIMASK
rather than unconditionally re-enabling, so they nest correctly. `u16_param2` is spare for
a completion reason. Manual stops are already in foreground context and print directly.

### S10 — Sleep test not guarded
**Status:** 🟢 — STOP1 gates TIM2's clock, so running `[W]` during a cycle injects a
multi-second gap into the run. Accepted without a guard: the sleep test is a leftover kept
as a reminder, and this is a bench tool, not a mission-critical system.

### S11 — NVM commit deferred while cycling
**Status:** 🟢 — `x_nvm_commit()` does a flash page erase and write, tens of milliseconds.
Whether that stalls the CPU here is uncertain (the G0B1's 512K flash is dual-bank, the NVM
page at `0x807F800` is in bank 2, code runs from bank 1, so read-while-write *may* apply) —
and the natural workflow puts the 5 s auto-commit right in the middle of a freshly-started
run. `JOB_CYCLE_COMPLETE`-style deferral: if any channel is cycling, the commit job leaves
`u8_need_commit` set and retries later. Parameters reach flash as soon as the run ends, and
the bank question stops mattering.

### I1 — Module home
**Status:** 🟢 — Cycle engine goes in `switch_out.c`. It needs the same channel map and
`OCxM` ownership as the manual layer; splitting those across two files invites them to
disagree. Adds roughly 150 lines to a ~200-line file.

### I2 — Per-channel state
**Status:** 🟢

```c
typedef struct
{
    /* Settings — NVM-backed, menu-edited, ISR reads only */
    uint32_t u32_repeat_count;      /* 0 = run until stopped */
    uint32_t u32_on_time_us;
    uint32_t u32_off_time_us;

    /* Runtime — ISR-owned */
    uint32_t u32_cycles_done;       /* completed ON+OFF cycles this run */
    uint8_t  u8_running;
    uint8_t  u8_phase;              /* 0 = ON phase, 1 = OFF phase */
}
switch_cycle_t;

switch_cycle_t g_x_switch_cycle[SWITCH_OUT_COUNT];
```

No CCR shadow field — the ISR reads `TIM2->CCRx` back directly, matching the
derive-from-hardware principle already used for `OCxM`. `u8_phase` and `u8_running` are
also derivable (from `OCxM` and `DIER.CCxIE`), but in a ≤10-line hot path an explicit byte
is clearer and cheaper than a register read and mask; both are ISR-owned so they can't
desync. `SWITCH_OUT_COUNT` is reused rather than introducing `NUM_SWITCH_CHANNELS`.

### I3 — ISR shape
**Status:** 🟢 — Body goes in `USER CODE BEGIN TIM2_IRQn 0`: read `TIM2->SR` once, service
all four `CCxIF` in one pass, clear the flags there. `HAL_TIM_IRQHandler()` then finds
nothing and no-ops (~0.4 µs of wasted flag tests; early-return if that's unwelcome).
Nothing else uses TIM2 interrupts, so bypassing HAL is clean and regeneration-safe. TIM2 has
a single IRQ for all four channels, so near-simultaneous matches across independent periods
are inevitable and must all be serviced in the one pass.

### I4 — Concurrency model
**Status:** 🟢 — Settings are ISR-read-only (a consequence of **S2**), so every menu edit is
a plain aligned 32-bit store — atomic on M0+, no PRIMASK anywhere. Runtime fields are
ISR-written and foreground-read; a 32-bit aligned read can't tear. Start/stop is always
foreground-side and always disarms before touching runtime state: **stop** clears `CCxIE`,
then clears `CCxIF`, then forces `OCxM` inactive; **start** initialises the struct while
`CCxIE` is still clear, then arms last. The ISR skips any channel whose `u8_running` is
clear, covering a compare already pending when the channel was stopped.

### I5 — NVM persistence
**Status:** 🟢 — Thirteen `uint32_t` objects, IDs `0x100`–`0x10C`, appended after
`NVM_PARAM_BASE_ID`: `NVM_PARAM_SWITCH_PULSE_MS`, then repeat/on/off per channel A–D.
Contiguity lets the ID be derived as `NVM_PARAM_CYCLE_A_REPEAT + (channel * 3) + parameter`,
guarded by a `_Static_assert` rather than a comment. Defaults: on 500000 µs, off 500000 µs,
repeat 0; pulse width stays 100 **ms** (its own menu's unit).

Init pattern per the API contract — `x_nvm_create()` does not modify existing objects:

```c
u32_value = DEFAULT;
x_nvm_create(&g_x_nvm_param, KEY, sizeof(u32_value), &u32_value);
x_nvm_get(&g_x_nvm_param, KEY, &u32_value);
```

Lives in `v_switch_out_nvm_init()` in `switch_out.c`, called from `v_param_init()` —
**after** `x_nvm_pool_init()`, **before** `x_nvm_commit()`, so first-boot creation of all
thirteen lands in one flash write. `u32_switch_pulse_width_ms` moves from a `debug_menu.c`
static into `switch_out.c` with accessors so one function owns all thirteen. Edits call
`x_nvm_set()`; the existing 5 s auto-commit batches the write. Space: 13 × 8 bytes = 104,
plus existing objects and pool header ≈ 130 of 512.

Prerequisites verified: pool size `0x200` (three-way consistent — `NVM_POOL_SIZE_DEFAULT`,
the zero-size default path, and the `nvm_mcu_flash[]` buffer), and linker reservation
`NVM_FLASH (r) : ORIGIN = 0x807F800, LENGTH = 2K` with `FLASH` truncated to 510K and a
`.nvmdata (NOLOAD)` section — `NOLOAD` means reflashing firmware doesn't wipe parameters.

### I6 — TIM2 and NVIC configuration
**Status:** 🟢 — Verified in tree: PSC 63, ARR 4294967295, TIM2 NVIC enabled at priority 0,
`TIM2_IRQHandler` generated, SysTick demoted to priority 1 (`TICK_INT_PRIORITY = 1U`) so
TIM2 genuinely preempts the tick, and `MX_TIM2_Init()` sets `FORCED_INACTIVE` on all four
channels as the init state. Foreground PRIMASK critical sections still block priority 0, but
all of them are a handful of instructions.

### I7 — Delete dead constants in `platform.h`
**Status:** 🟢

**Resolution:** delete both. Neither is referenced anywhere in the tree, and both carry
stale assumptions from the retired 10 ms tick.

| Constant | Why it's dead |
|---|---|
| `NVM_AUTO_COMMIT_DELAY 500 // 5 seconds (500 * 10mS)` | Superseded by `DEV_CONFIG_NVM_COMMIT_DELAY_MS = 5000` in `device_config.h`, which is what `v_timer_update()` actually uses |
| `NVM_PARAM_RAM_SIZE 512` | Pool size comes from `NVM_POOL_SIZE_DEFAULT` via the zero-size path in `x_nvm_pool_init()` |

They agree with reality today, which is the hazard — the same hand-synced-constant drift
as the `htim6` / 10 ms comments already corrected.

### T1 — Port the decision-log model into this repo
**Status:** 🟢

**Resolution:** ported to [`decision-log-model.md`](decision-log-model.md) — a
SwitchTester-adapted copy. Retains the Brief-first layout, Big Board, ID prefixes, status
colours and detail-section format. Drops the machinery that doesn't exist here: PLAY
references, the Must-Ship-Gap fence, `/wrapup` and `/cleanup-docs` skills, the
`.grok/` paths and the Python cleanup automation. Canonical upstream remains
`C:\STM32\CubeSource\LED_Strip_Controller_G474\Docs\planning\decision-log-model.md`.

### T2 — Spec sync
**Status:** 🟢 — Per the model, the plan is the negotiation log and
`Docs/SwitchTester-Design.md` is the contract. Sync the design doc from this plan once
implementation lands; don't maintain the detail in both.

---

## Global notes

**Implementation phase sketch:**

1. `switch_cycle_t` + `g_x_switch_cycle[]`, NVM init, dead-constant cleanup (**I2**, **I5**, **I7**)
2. Start/stop/arming plumbing and the run-end path (**S3**, **S8**, **I4**)
3. TIM2 compare ISR + missed-compare guard (**S1**, **S4**, **I3**)
4. `JOB_CYCLE_COMPLETE` + commit deferral (**S9**, **S11**)
5. Cycling submenu, and the `[Esc]`/`[s]`/`[!]` changes to existing menus (**D1**–**D8**)
6. Bench verification, then **T2** spec sync

**Plan status:** 🟢 28 · 🟡 0 · 🔴 0 · 🔵 0 (+6 wish-list rows). **Board fully green
2026-08-01** — all design decisions locked, ready to implement.
**Next suggested ID:** none open. Next action is phase 1 of the sketch above.

**End of switch-cycling-plan.md**
