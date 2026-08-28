# SwitchTester — forward roadmap

**Purpose:** the user's stated intent for finishing SwitchTester, written down so a
new session can pick the work up cold. Recorded 2026-08-27.

**This is a roadmap, not a decision-log board.** It says *what* is wanted, in what
order, and *what is already designed* — it does not resolve design questions. Each
task gets its own `Docs/planning/<topic>-plan.md` D-log when its session starts, per
[`decision-log-model.md`](decision-log-model.md); creating that board is the first
act of the session, **after** the user relays their high-level shape. Do not
pre-empt those decisions.

---

## Where the project stands

Everything below is built, bench-verified on the NUCLEO-G0B1RE, and on `main`:

| Subsystem | State |
|---|---|
| Switch outputs, manual | Done. Levels set by rewriting each TIM2 channel's `OCxM` (`FORCED_ACTIVE`/`FORCED_INACTIVE`) — pins stay permanently AF2/TIM2, **no GPIO remuxing, no PWM output mode** |
| Switch cycling | Done. TIM2 compare ISR, per-channel `u32_on_time_us` / `u32_off_time_us` / `u32_repeat_count`, NVM-persisted |
| Automation console, phase 1 | Done. 47/47 HIL |
| logging, menusystem, uart_stream | Done, vendored |
| nvmparams | Phase 1 done, 28/28 HIL. Wear levelling unbuilt (phase 2) |
| **event_queue** | **Phase 1 done, 17/17 HIL. Vendored and adopted here; also vendored into Skeleton wired-but-unused.** The enabler for tasks 2–4 below |
| Sense front-end | **Configured but inert.** Comparators are `TriggerMode NONE` and not started; DAC channels not driven |

The four sense channels are deliberately asymmetric — see
[`../SwitchTester-Design.md`](../SwitchTester-Design.md) § "Sense front-end".

---

## The four tasks (user, 2026-08-27)

Quoted intent first, then what it implies.

### Task 1 — Meaningful support for the SENSE inputs
> *"Biggest task."*

Open-ended **by the user's own choice**. The standing note in the design doc still
governs: **"what should each channel measure" is the real first question**, and it
precedes any polled-vs-interrupt or threshold-setup design work. The channels are
not interchangeable (SENSE_A has its own DAC channel, B and C share/should share
DAC1_CH2, D is the ADC channel), so the answer shapes everything downstream.

Known technical groundwork already captured in the design doc: COMP3 → DAC1_CH2 is
a one-line CubeMX change; ADC-on-a-comparator-input is possible despite CubeMX
refusing the dual assignment (PA1 is the candidate, ~20 pF S&H loading is the cost);
`TIM2->CNT` is the shared 1 µs timestamp source.

### Task 2 — Tie event_queue into SWITCH and SENSE events
> *"Tie in event queue support for SWITCH and SENSE events."*

The switch half can be built **today** — the events already exist (TIM2 compare
transitions, `JOB_CYCLE_COMPLETE`). The sense half is gated on task 1.

### Task 3 — Consume those events, two ways
> *"Consume events generated in SWITCH and SENSE ISRs via the automation console,
> and (alternatively) in the human-readable log output."*

Depends on task 2. **Largely designed already** — see *Existing design to reuse*.

### Task 4 — Interrupt-driven "switching lists"
> *"At present, interrupt-driven switching is essentially a low-frequency
> ISR-assisted PWM generator — fixed ON and OFF times with a variable repeat count.
> I want the ability to vary the PWM 'duty' on a cycle-by-cycle basis. This could be
> done in an autonomous fashion using TIM DMA, I think, but would probably retain
> some software interrupt generation support so the switch state changes could be
> logged via the event queue."*

Independent of sense; wants task 2's event plumbing for the logging half.

---

## Suggested order, and the proving subset

The user's own proposal, which the dependency graph supports:

> *"A subset of (2) might be done first to prove the event queue design and the
> automation and human-logging paths, to lay the groundwork for the other tasks."*

**Do that first.** Tasks 2, 3 and 4 all consume the event path and task 1 produces
into it, so the path is the shared risk. Proving it on **switch** events costs
little — they already exist, need no new hardware bring-up, and no open user
decision blocks them. Concretely, a proving slice is: switch-transition events
enqueued from the TIM2 compare ISR, drained in the main loop, emitted as async acon
frames **and** as human log lines, with arming and a dropped-count.

That slice retires the real unknowns — ISR-context enqueue cost, the deferral rule,
the arming model, the two-sink question below — before either of the expensive tasks
commits to them.

After that the two remaining tracks are independent and can be taken in either
order:

- **Task 1 → rest of task 2/3** (sense events), blocked on the user's "what should
  each channel measure" answer.
- **Task 4**, blocked on a drive-architecture decision (below).

---

## Existing design to reuse — do not redesign these

Task 3 is mostly *already decided*. The rows live in
[`automation-console-plan.md`](automation-console-plan.md) and are marked 🔵 phase 2:

| Row | What it settles |
|---|---|
| **S6** | Async event queue — ISR-safe records, formatted at dequeue, enqueue gated on an "is anyone listening" flag, queue reset on session entry |
| **S7** | Deferral rule (async frames only *between* response frames, never inside one) and overflow policy (drop + dropped-count) |
| **S8** | Timestamp source — `TIM2->CNT`, 1 µs, captured **at the event**, not at print time. Rationale: `HAL_GetTick()`'s 1 ms cannot resolve a switch bounce |
| **S9** | Event subscription / arming — default on/off, granularity (per class vs per channel), lifetime across REPL entry/exit. **Needs the user when phase 2 opens** |
| **S12** | Menu-mode human log — the second sink. Must **not** share S6's REPL gate |
| **I5** | Async-readiness contract |

Two hooks already exist in code for exactly this:

- `v_acon_flush_events()` — [automation_console.c:517](../../App/automation_console/automation_console.c:517), deliberately empty. It fixes *where* async output is allowed (called from the executive loop at line 616).
- `JOB_CYCLE_COMPLETE` — [jobs.h:53](../../App/Inc/jobs.h:53), already carries the channel in `u8_param1`. Use it rather than building a parallel path.

---

## Reconciliations the next session must handle

Two places where the phase-2 design predates the vendored `event_queue` module and
needs updating rather than following literally.

### R1 — S6's bespoke ring is superseded

S6 specifies an 8-byte fixed-size `repl_event_t` in a power-of-two ring, hand-rolled,
with a note that multiple ISR priorities break single-producer and need "a short
PRIMASK guard around the head advance."

**`event_queue` now provides all of that, generalised:** variable-length records
(so an event's payload need not be squeezed into 8 fixed bytes), single-producer
lock-free operation with no masking, and — for the multi-ISR case S6 worried about —
the per-queue `pfn_lock`/`pfn_unlock` pair, which *is* S6's PRIMASK guard made
optional and per-instance.

**Carry over from S6:** the gating concept, the reset-on-entry rule, the
dropped-count, and "no formatting in ISR context — records in, strings out at
flush time." **Drop:** the hand-rolled ring and its priority discussion. The
`repl_event_t` shape may still be a fine *payload*, now as the record's data rather
than as the queue's fixed cell.

### R2 — one queue, two sinks, and the single-consumer contract

Task 3 wants the same events to reach **two** sinks: the automation console (S6) and
the human-readable log (S12). But `event_queue` is **single-consumer by contract** —
`get`, `peek` and `flush` all move the consumer-side cursor, and the README's
"one consumer means one consumer" gotcha applies directly. Two contexts calling
`get` on one queue is precisely the race the producer lock does *not* cover.

So this needs a deliberate choice, and it is a **real open question for the user**:

- **(a) One queue, one consumer that routes.** A single drain point (the job runner)
  reads each record once and emits it to whichever sink is currently armed. Honours
  the contract, one copy of each event, cheapest in ISR cost and RAM. Cost: an event
  can only go to one sink per drain unless the router deliberately emits to both.
- **(b) Two queues, producer enqueues to each armed sink.** Fully decouples the two
  gates — which S12 explicitly requires ("must not reuse the same gate"). Cost:
  double the ISR work and double the RAM, and the two can overflow independently.

Leaning: **(a)**, with the router emitting to both sinks when both are armed, and
S6's gate generalised from "REPL mode is active" to "any sink is armed." That keeps
S12's requirement that the *human* view is not coupled to REPL mode, while keeping
one producer path and one consumer. Not decided — the user's call.

---

## Task 4 — the design fork worth knowing before that session starts

The current engine does **not** use PWM output mode. It sets a level by rewriting
`OCxM` to forced-active/inactive and schedules the next edge by writing `CCRx` and
taking a compare interrupt. That was a deliberate locked decision (no GPIO↔AF
remuxing, polarity lives in `CCER.CCxP`).

A DMA-fed duty list points the other way — toward real PWM output with DMA'd `CCRx`,
and, because varying *duty per cycle* at a varying *period* also means varying `ARR`,
most likely TIM DMA **burst** mode (`DCR`/`DMAR`) writing several registers per
update event. That is a different drive mechanism from the one currently locked, so
task 4 should open by deciding whether to:

- extend the existing compare-ISR engine to pull each cycle's on/off pair from a
  list (simplest, keeps every existing property and the event hooks, costs one ISR
  per edge — which is what it already costs), or
- move to DMA-fed PWM for autonomous operation (highest fidelity at high rates, but
  changes the drive scheme and complicates the "log every state change" requirement
  the user explicitly wants to keep).

Resource facts to check against when that is decided: all four TIM2 channels
(CH1–CH4) are already in use, one per switch, each with its own `CCRx`; **DMA1
Channel 1 and Channel 2 are already claimed by SPI3 RX/TX** for the SPI-flash work.
A per-channel DMA scheme needs channels beyond those.

Also note: per-cycle-varying duty multiplies event volume, which makes **S9**
(subscription/arming) and **S7** (overflow policy) load-bearing rather than
nice-to-have. Decide those in the proving subset, not after.

---

## Open questions that need the user

One per task, to be asked **one at a time** when its session opens
([`one-question-at-a-time`](decision-log-model.md) — put the rest on the board):

| # | Question | Blocks |
|---|---|---|
| 1 | **What should each SENSE channel measure?** The channels are asymmetric; this precedes all sense design work | Task 1, and the sense half of 2/3 |
| 2 | **One queue with a routing consumer, or two queues?** (R2 above) | Task 3 |
| 3 | **S9 — arming:** default on or off, per-class or per-channel, does a subscription survive REPL exit? | The proving subset |
| 4 | **Task 4 — extend the compare-ISR engine, or move to DMA-fed PWM?** | Task 4 |

---

## Explicitly not in scope

- **LED_Strip_Controller_G474 vendoring is ON HOLD** (user, 2026-08-27). That project
  is paused pending a migration to a higher-end MCU platform, which itself waits on
  new hardware being wired up. Do not plan `event_queue` (or other module) adoption
  into LED_Strip; the "not yet" cells in Skeleton's
  `Docs/planning/portable-apis-strategy.md` adoption table are deliberate.
- **nvmparams wear levelling** — phase 2, tracked in Skeleton's `nvmparams-plan.md`,
  not part of this roadmap.
- **event_queue W4 (priority put)** — deferred with a full promotion draft in
  [`event-queue-plan.md`](event-queue-plan.md). Revisit only if a task here actually
  needs an event to jump the queue.
- **uart_stream DMA refactor** — banked in `../UART-DMA-Streaming.md`, still not
  wanted.

---

## Working conventions for these sessions

- **A D-log board per task**, created at the start of that task's session once the
  user relays their shape — not pre-created here.
- **One open question per response.** Everything else lives on the board as 🔴/🟡.
- **The user surfaces design ideas mid-task**, often while a build or test runs.
  These are welcome refinements, not scope creep — fold them in.
- **Guard policy:** guard REPL/host-commanded paths and reject; leave menu/human
  paths relaxed. SwitchTester is bench tooling, not a product.
- **Bench:** two ST-Links on-bus — `flash.ps1` pins the SwitchTester SN; COM3 is its
  console at 921600. COM1/COM2 are the DUT. The HIL suites
  (`test_acon.py` 47, `test_nvm.py` 28, `test_eventq.py` 17) are the regression net —
  run them after anything structural.

**End of switchtester-roadmap.md**
