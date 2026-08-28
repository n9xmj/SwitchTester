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
enqueued from the TIM2 compare ISR, drained by whichever sink owns the console (R2), emitted as async acon
frames in acon mode **or** as human log lines in menu mode, with arming and a dropped-count.

That slice retires the real unknowns — ISR-context enqueue cost, the deferral rule,
the arming model, the mode-handover behaviour (R2) — before either of the expensive tasks
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
| **S7** | Deferral rule (async frames only *between* response frames, never inside one) and overflow policy (drop + dropped-count). **Counter design settled 2026-08-27** — see "Event accounting" |
| **S8** | Timestamp source — `TIM2->CNT`, 1 µs, captured **at the event**, not at print time. Rationale: `HAL_GetTick()`'s 1 ms cannot resolve a switch bounce |
| **S9** | Event subscription / arming — **largely answered 2026-08-27:** MCU-style mask register, gating at production, masked sources counted nowhere. See "Event control" below. Residuals: reset default, mask lifetime |
| **S12** | Menu-mode human log — the second sink. Must **not** share S6's REPL gate |
| **I5** | Async-readiness contract |

Two hooks already exist in code for exactly this:

- `v_acon_flush_events()` — [automation_console.c:517](../../App/automation_console/automation_console.c:517), deliberately empty. It fixes *where* async output is allowed (called from the executive loop at line 616).
- `JOB_CYCLE_COMPLETE` — [jobs.h:53](../../App/Inc/jobs.h:53), already carries the channel in `u8_param1`. Use it rather than building a parallel path.

---

## Reconciliations the next session must handle

Two places where the phase-2 design predates the vendored `event_queue` module. **R1 is
still to handle; R2 is now resolved** — both are recorded so neither is re-litigated.

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

### R2 — one queue, two sinks — RESOLVED (user, 2026-08-27)

Task 3 wants switch/sense events to reach two sinks: the automation console (S6)
and the human-readable log (S12). But `event_queue` is **single-consumer by
contract** — `get`, `peek` and `flush` all move the consumer-side cursor, and the
README's "one consumer means one consumer" gotcha applies directly.

**Resolution — the two sinks are mutually exclusive, so there is only ever one
consumer.** In the user's words:

> *"the consumption sink for the event queue will probably be a XOR — when acon is
> active, it will be the only consumer. When acon is inactive (debug menu system
> controls console), event queue will be consumed by the app polling task and
> (optional) log messages with event emits will take place. Either way, there will
> only be one consumption point for a given event queue — either acon-exclusive, or
> human console-exclusive."*

This is better than the "one queue, one router that fans out to both" leaning this
doc previously carried: **no fan-out logic is needed at all.** Console ownership is
already exclusive — the debug menu and the automation console cannot both own the
link — so the single-consumer contract is satisfied *structurally*, by a property
the system already has, rather than by a rule a future maintainer has to remember.
Two queues (the other option) are not needed either.

**What this settles:**

- One `event_queue` instance for switch/sense events. One producer path (the ISRs),
  one consumer, selected by which mode owns the console.
- acon mode: drained at `v_acon_flush_events()`, honouring S7's deferral rule
  (between response frames, never inside one).
- menu/human mode: drained by `v_app_polling_task()`, emitting log lines only if
  the human-side emit option is on. `i_getline()` keeps the polling task pumped
  while blocked, so the drain does not stall during a menu prompt.
- S12's "must not share S6's gate" requirement is honoured for free — the two sinks
  are separated by mode, not by a shared flag.

**Two consequences — both since answered by the user; see "Event control" below. Kept
here because the reasoning explains WHY the answers are what they are:**

1. **Handover behaviour at the mode transition.** *(ANSWERED: a dedicated
   host-commanded acon flush, so no firmware handover policy is needed.)* The console switches modes at
   runtime (`ACON_ENTER` 0xDA in, `Q`/Ctrl-C/`ACON_EXIT` out), and a session can end
   with events still queued. S6 already decided *reset the queue on entry, not just
   enable it*, for the REPL side. Under the XOR model the equivalent question is
   whether a mode change **flushes** the queue or lets the incoming sink drain what
   the outgoing one left. `x_event_queue_flush()` exists and makes the flush option
   a single call — an unplanned payoff from building W2. **Open: flush on
   transition, or carry events across?** Leaning: flush, matching S6's
   reset-on-entry intent and keeping a host from being handed events that predate
   its session.

2. **The producer gate may simplify to an emit gate.** *(ANSWERED: no — the user
   wants gating at PRODUCTION. The reasoning below is why the question was live.)* S6 gated *enqueue* on REPL
   mode, reasoning that "nothing accumulates for an absent host" and "the queue
   cannot overflow while nobody is listening." Under the XOR model **some** consumer
   is always draining, so that overflow argument weakens: the human-side consumer
   can drain-and-discard when its emit option is off, which keeps the queue empty
   without gating the ISR at all. That trades a few ISR cycles for a uniform drain
   path and the option of counting events even when not emitting them. Fold this
   into **S9** (arming) when that opens rather than deciding it separately — it is
   the same question wearing a different hat.

Note the user's phrasing, *"one consumption point for a given event queue"*: the
contract is per-queue, so a future subsystem wanting its own queue is unaffected by
any of this.

---

## Event control — production masking and flush (user, 2026-08-27)

This substantially answers **S9** and settles both residuals left by **R2**. The
user's direction, in their words:

> *"Presumably the event queue can be flushed 'on demand' — perhaps when the acon is
> entered, or even at the start of specific acon commands, or even a dedicated acon
> command that does the flush. The dedicated acon flush command is probably the route
> I'd choose — it gives the host script control of the event queue."*
>
> *"I'd probably also add a acon command that can switch event -production- on and
> off. I'd probably set this up so event production can be somewhat granular — e.g.
> one production switch for switch events (or even one for each individual switch),
> another set of switches for the sense inputs, and more switches for other types of
> events that I've not yet conceived. Will probably also add a global event production
> on/off switch. Think of this using the same/similar model as an interrupt mask
> register on a MCU IP."*

### What this settles

**Flush is host-commanded, not automatic.** A dedicated acon command flushes the
queue, giving the host script explicit control rather than having the firmware decide
at mode transitions. R2's "flush on handover or carry across?" therefore does not need
a firmware policy: a host that wants a clean slate issues the flush as the first act
of its session. `x_event_queue_flush()` is a single call, so the command is thin.
(Flush-on-acon-entry and flush-at-the-start-of-specific-commands remain available if
they later prove convenient; they are not exclusive with a dedicated command.)

**The gate is at PRODUCTION, not emission.** The user's emphasis on event *production*
resolves R2's second residual in the opposite direction from the leaning this doc
previously carried: masked sources never enter the queue at all, rather than being
enqueued and filtered at the sink. That keeps the ISR cost proportional to what is
actually wanted and the queue holding only records someone asked for.

Consequently the consumer's job is unconditional: **whichever sink owns the console
always drains** (R2's XOR), regardless of whether the human side is currently
*printing* anything. Draining is not optional — it is what keeps the queue from
filling. "Optional log messages" governs emission only.

### The mask-register model

Following the user's MCU-IP analogy, the natural shape is a **global enable plus a
per-source enable set** — an `IER` with a master switch:

- Today's sources: 4 switch channels + 4 sense channels = **8**. A single `uint32_t`
  mask holds 32, which is ample headroom for "other types of events I've not yet
  conceived", so one mask word plus one global bool covers the whole model with no
  structure to redesign later.
- Granularity comes for free at the bit level: the host can enable "all switch
  events" by writing a nibble/byte of bits, or one individual switch by writing one
  bit. Per-class and per-channel are then the same mechanism at different
  granularities, which is what makes the register model worth borrowing — S9 asked
  "per class or per channel?" and the answer is *both, it is one mask*.
- **Proposed ID encoding** (proposal, not decided): event ID = `(class << 8) |
  instance`, so the class is the high byte and the instance the low byte. The
  enqueue-side check is then a class lookup plus a bit test, and the 16-bit generic
  ID that `event_queue` already carries needs no extra field.
- **Free property:** the mask is written by the console (main context) and read by
  ISRs, and an aligned 32-bit load/store is atomic on Cortex-M0+ — so no lock is
  needed on either side, consistent with `event_queue`'s own lock-free discipline.

### Leanings on the remaining S9 residuals (user, 2026-08-27 — NOT yet locked)

Recorded as leanings, in the user's framing: *"I suspect that I'll opt for..."*,
*"I might also opt to..."*. Do not treat these as decided; they are where the
thinking currently sits.

- **All events masked by default.** This answers S9's default-on-or-off directly and
  agrees with S7's original worry, that unconditional reporting would flood the link
  during a soak run and overflow the queue continuously. It also makes a plain boot
  free: nothing is produced, the queue stays empty, the drop count stays 0 until a
  host or operator explicitly arms something — the same "costs nothing unless asked"
  posture as the flash test pool, which is not auto-inited either. Worth noting the
  failure mode it chooses: *"I forgot to arm it"* (nothing happens, trivially
  diagnosed) in place of *"why is my link flooded?"* (which looks like a fault).

- **Control from BOTH the debug menu and acon.** Consistent with the XOR sink model:
  each surface configures production for whoever currently owns the console. Note
  this sharpens the *mask lifetime* residual rather than settling it — with two
  control surfaces over one register, a mask set from the menu is visible to a host
  that enters acon afterwards, so "does a mask survive acon entry/exit" becomes a
  question about handover, not just about reset. Per the guard policy, validate the
  acon path and leave the menu path relaxed.

- **NVM-persisting the mask register — TBD.** Two things to weigh when it is decided:

  1. **It largely replaces the masked-by-default decision rather than complementing
     it.** If the mask persists, the boot state is whatever was last set; "all
     masked" then only describes a virgin pool. The real question becomes *what is
     the boot mask — always all-off, or last-set?* If last-set, a one-key
     mask-everything command is worth having, because a persisted arm-everything
     mask would otherwise come back after every reset and flood the link again.
  2. **A stale or foreign pool could arm sources unexpectedly.** `.nvmdata` is
     `NOLOAD` and survives reflashing, and this bench is shared with Skeleton, so a
     pool written by other firmware can be read as ours (see the NVM inherited-data
     hazard in the design doc). A garbage mask reads as "some or all sources armed",
     which is the loud failure rather than the quiet one. **Two mitigations are on
     the table** (see the design doc's inherited-data section): per-project `.nvmdata`
     regions so the siblings never share an address (user idea, 2026-08-27 —
     prevention, linker-script only), and the already-documented pool-label ownership
     check (detection, catches any foreign pool). Cheap in itself — one
     `uint32_t` object appended at the end of the ID enum — but it inherits that
     hazard.

### Where the MCU analogy deliberately stops — RESOLVED (user, 2026-08-27)

On real hardware the analogy has a wrinkle: **a peripheral's status flag sets whether
or not the interrupt is enabled.** The `IER` bit gates whether the request
*propagates*, not whether the event is *recorded*, which is why polling a masked flag
still works. That raised the question of whether a masked source here should still
bump a counter a host could read.

**It should not. Masked means masked — nothing is counted, anywhere.** In the user's
words: *"The whole point of masking is to, well, mask event sources that are
not-of-interest. I see no use in tracking in any way events that are masked from
production."* Not at `event_queue` level (which cannot see masks at all) and not at
application level either.

So the mask borrows the MCU register's *shape* — global enable plus per-source bits,
one write, atomic — without borrowing its status-flag semantics. A masked source
costs one predictable branch in the ISR and produces nothing: no record, no counter,
no trace. That is the simpler behaviour and the one that matches what "production
off" plainly means.

---

## Event accounting — the drop count (user, 2026-08-27)

> **BUILT and bench-verified** (`event_queue` S8, suite 20/20): handle fields
> `u32_records_dropped` / `u32_dropped_ack`, public
> `u32_event_queue_dropped()` / `v_event_queue_dropped_reset()`, counting
> `EQ_ERROR_FULL` only, across all producers, reconciled on the bench against an
> independent ISR tally. A companion `u32_event_queue_puts()` / reset pair (S9)
> counts successes, so `puts + dropped` is every put attempt a producer made.

**There is exactly one thing worth counting: drops.** The queue was full, so the
event was discarded. A *masked* source is not counted at all — see the resolution
above; masking means the event was never of interest, so there is nothing to
account for.

That also keeps the accounting honest about what it means. "Events that happened but
did not reach you" is a fault signal — the ring was undersized or the consumer fell
behind. Folding in deliberately-masked events would mix a fault indication with a
configuration choice, and a host would have to subtract one from the other to learn
anything.

### The job-queue precedent (this project's own pattern)

`App/{Inc,Src}/jobs.*` already solves this, and elegantly. `u8_full` is tri-state
rather than a boolean:

| Value | Meaning |
|---|---|
| `0` | not full |
| `1` | full, nothing lost |
| `>= 2` | overflowed; **`u8_full - 1`** jobs were lost |

It saturates at `0xFF` rather than wrapping, and — the part worth borrowing
conceptually — the loss is reported **in band**: `u8_job_get()` synthesises a
`JOB_QUEUE_OVERFLOW` job carrying the lost count in `u8_param1`, then knocks
`u8_full` back to `1` (still full, overflow now reported). The consumer learns about
the loss through the same ordered channel as normal jobs, so a report cannot be
missed by a consumer that is only reading the queue.

Worth considering whether the event path wants the same in-band treatment — a
synthetic "N events lost here" record injected at the point of loss preserves
*where* in the stream the gap occurred, which a side-channel counter cannot express.
A counter alone tells the host how many it lost, not when.

### For event_queue — the shape, and the one hazard

The user's direction: **a new member in the handle/control struct counting overflows,
with an accessor and a reset-er added to the public API.** `event_queue` has no full
flag to overload, so the counter is its own field.

**The hazard that shapes the design:** the counter is incremented by `put`
(producer, typically an ISR) and a reset-er would be called by the consumer (main
loop). Cortex-M0+ has **no LDREX/STREX**, so a read-modify-write cannot be made
atomic without masking interrupts — which this module exists to avoid. A naive
"increment in `put`, zero it in the reset-er" therefore races: if the consumer's
store of `0` lands inside the producer's read-modify-write, the producer writes back
`old + 1` and the reset is silently lost.

**The module's own idiom already solves it** — every counter has exactly one writer,
and deltas are taken with unsigned subtraction:

- `u32_records_dropped` — **producer-owned**, monotonic, only ever incremented by
  `put`, never reset internally.
- `u32_dropped_ack` — **consumer-owned**, only ever written by the reset-er.
- Accessor returns `u32_records_dropped - u32_dropped_ack`, wrap-safe exactly like
  the existing byte counters.
- The reset-er stores `u32_dropped_ack = u32_records_dropped` — a single aligned
  32-bit store to a field the producer never touches.

That keeps the one-writer-per-field invariant the whole module rests on, needs no
lock, and is correct for both SPSC and the multi-producer (locked) case. Proposed
signatures, matching house style (`v_nvm_commit_timer_reset()` is the precedent for a
void reset-er):

```c
uint32_t u32_event_queue_dropped     (const event_queue_handle_t *px_handle);
void     v_event_queue_dropped_reset (event_queue_handle_t *px_handle);
```

Count **only** `EQ_ERROR_FULL`. A `EQ_ERROR_PARAMETER` or `EQ_ERROR_NOT_INIT` return
is a caller bug, not a dropped event, and folding those in would make the number mean
two different things.

### Layer boundary — why the module could not have counted masked events anyway

The drop counter belongs in the vendored module: fullness is `event_queue`'s own
business, and it is the only thing that knows a put was refused for space.

**Masking is purely an application-level behaviour** (user, 2026-08-27), and the
module could not participate even if counting were wanted. `event_queue` knows
nothing of event classes, channels or masks — the mask is an application concept
built on top of the generic 16-bit ID (see *Event control*), and pushing it into the
module would break the dependency rule by giving it knowledge of the adopter's
taxonomy. The mask is tested in the application's event-production layer, before
`x_event_queue_put()` is ever called.

Both are reported to the host together even though they live in different layers.

### Aside — the job queue's overflow counter is deliberately approximate

Noted while reading the precedent, and **explicitly accepted by the user
(2026-08-27). This is not a defect and is not to be "fixed".**

`u8_full` is read-modify-written by both `v_job_add*()` and `u8_job_get()` with no
critical section, and `v_job_add_with_params(NULL, JOB_CYCLE_COMPLETE, ...)` is
reached from `v_switch_cycle_advance()`, which runs in `v_switch_cycle_isr()` at
priority 0. So an ISR-side `u8_full++` can straddle the main-loop-side write.

**Why it does not matter, in the user's words:** the overflow count is *"used mostly
as a logging diagnostic"* across the several projects this job queue has served, and
*"the fact that an overflow occurred — not the exact count of overflows — is the
important thing to have tracked."* A count that is occasionally off by one under a
rare interleaving still carries the signal the diagnostic exists to carry. No queued
job is ever lost or corrupted by this; only the tally can drift.

**Do not refactor `jobs.c` for this.** It is long-standing, widely reused code whose
counter is fit for its stated purpose.

The reason the new `event_queue` drop counter uses the monotonic + ack pattern anyway
is not that this standard is stricter — it is that the pattern costs *nothing extra*
in code being written from scratch, and `event_queue`'s whole design already runs on
one-writer-per-field. Getting it exact for free in new code and leaving proven code
alone are the same judgement, not two different ones.

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
| 2 | ~~One queue or two?~~ ~~Flush on handover?~~ **BOTH RESOLVED** — XOR by console mode (R2); flush is a dedicated host-commanded acon command | — |
| 3 | **S9 — arming:** mask model, production-side gating and no masked-source counting all resolved. **Residuals, with leanings recorded (not locked): all-masked-by-default, menu + acon control, and whether the mask is NVM-persisted (TBD — note it largely supersedes the default)** | The proving subset |
| 4 | **Task 4 — extend the compare-ISR engine, or move to DMA-fed PWM?** | Task 4 |

---

## Smaller banked ideas (user, 2026-08-27) — not among the four tasks

Recorded to be worked later, not fleshed out. Both were checked against the code
first; the findings are below so a later session need not re-derive them.

### B1 — Logging: make the timestamp source a CONFIG-HEADER seam

**Current state: a seam already exists, but not the kind proposed.** The module does
**not** hardcode `HAL_GetTick()` — `logging.c` calls `u32_log_timestamp_ms()`, which
has a **weak default returning 0** so a freshly vendored copy links and runs before
any port is written. The application overrides it with a strong definition. In this
project that is `App/Src/logging_port.c`, whose entire content is:

```c
uint32_t u32_log_timestamp_ms(void) { return HAL_GetTick(); }
```

So it is a **link seam** (weak-symbol override), not a **config-header seam**. The
proposal — a definition in the adoption header naming the function to call — is a
real increase in versatility, for two reasons:

1. **It would let an adopter point straight at an existing function** with no wrapper
   and no port file: `#define LOG_TIMESTAMP_MS() u32_my_tick()`.
2. **`logging` would stop needing a port source at all.** That one function is the
   *whole* reason `App/Src/logging_port.c` exists, and it is the only entry in the
   strategy doc's port-inventory table that mandates a port source for a single
   function. Removing it would let `logging` join `menusystem` under the
   optional-port rule.

**Keep the weak default as a fallback** if this is done — undefined macro falls
through to the existing weak symbol, so nothing already vendored breaks and the
"links before you write a port" property survives. This is a change to the vendored
module, so it lands in Skeleton and is re-vendored to adopters.

### B2 — SwitchTester: its own interrupt-driven tick, settable/readable over acon

**Current state: does not exist.** Nothing maintains a software tick. The 1 ms TIM14
callback runs `v_periodic_int_test()`, `v_timer_update()`, `v_switch_out_tick()` and
the event-queue test hook, but counts nothing. Two things sit nearby and are worth
knowing about, neither of which is what was asked for:

- **`v_system_tick_set()` / `v_system_tick_add()`** (`utils.c`) exist and are
  atomic — but they write **`uwTick`, the HAL's own tick**, the same value
  `HAL_GetTick()` returns. Setting it therefore perturbs every HAL timeout in the
  system, which is exactly why an *independent* counter is the better idea.
- **`TIM2->CNT`** is already a free-running 32-bit 1 µs counter, and **S8 has already
  chosen it as the async-event timestamp source**. It is hardware, not
  interrupt-driven, and is not a settable tick count.

**The two ideas compose, though they were raised as unrelated.** If B2 gives
SwitchTester its own tick, B1 is precisely the mechanism by which `logging` would
consume it — the config-header seam would point at the new tick instead of
`HAL_GetTick()`, and log lines would share a timebase with the instrument rather than
with the HAL. Worth deciding together: whether that tick and `TIM2->CNT` are one
timebase at two resolutions or two independent ones.

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
