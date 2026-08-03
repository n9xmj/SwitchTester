# Automation console — decision log

A resident, machine-facing command console entered from the debug-menu shell by a
sentinel byte, giving an external host deterministic control of the instrument
over the **same** USART2 the human menu uses. Replaces "send ESC three times, send
a menu key, read until the timer runs out" with framed request/response.

It is **not** a test harness and not test-only (**D7**): it is the interface *for*
a host-side harness, and equally the way the instrument is driven in normal
operation.

- **Code home:** `App/Src/automation_console.c` + `App/Inc/automation_console.h` (**D7**)
- **Transport:** [`uart-stream-integration-plan.md`](uart-stream-integration-plan.md) — **done and bench-verified**
- **Parent spec:** [`../SwitchTester-Design.md`](../SwitchTester-Design.md) § "HIL / script REPL"
- **Reference implementation:** `C:\STM32\CubeSource\LED_Strip_Controller_G474\App\{Src,Inc}\test_harness.{c,h}` — *a* solution, not *the* solution
- **Status:** PLANNING
- **Working mode:** one question at a time in chat; everything else parks on the board

---

## Brief

The debug menu is a *human* interface: single-keystroke dispatch, `toupper()`
folding, free-form prose output, a redraw-heavy screen. None of that is safe to
parse from a script. The REPL is a second personality on the same wire — entered
on `ACON_ENTER` (0xDA), left on `ACON_EXIT` (0xA5), `Q`, or an idle
timeout — that bypasses the menu system entirely and speaks a line-oriented,
framed protocol instead.

The channel carries **two kinds of device→host traffic** (**Q1**, locked):
*command responses*, which answer exactly one host request, and *async events*,
which the device emits on its own — a cycled output changing state, a sense
comparator transitioning, captured ADC data. Async frames never interleave into a
response frame: they are queued while a transaction is in flight and flushed when
the REPL returns to wait-for-command. Both frame kinds carry a lead identifier so
the host can tell them apart, and async frames are timestamped at the moment of
the event (**S8**).

**Delivery is phased.** *Phase 1* is command/response only — no event queue, no
async frames, no capture. *Phase 2* adds the async machinery. The split is a
schedule decision, not a design one: phase 1 must be built so phase 2 drops in,
and the specific obligations that buys are enumerated in **I5**, which is the row
to check before writing any phase-1 code.

Phase-1 op scope is the drive side only: force/pulse/toggle the four switch
outputs, start/stop/configure cycling, read back state, and assert on the
transport error counters. Sense-channel ops come later, when the sense design
exists.

---

## The Big Board

| ID | Status | Subject (one line) |
|----|--------|--------------------|
| **Q1** | 🟢 | **What the host runner must do** — command/response *and* async events, both v1 |
| **D1** | 🟢 | Entry / exit protocol — 0xDA in, 0xA5 / `Q` / idle timeout out |
| **D2** | 🟢 | Command namespace — strict case sensitivity, full `0x20..0x7E` |
| **D3** | 🟢 | Frame identity and grammar — sigils carry status; 1-char keys, hex values |
| **D4** | 🟢 | Host→device grammar — freeform, comma-separated, **no CRC or length** |
| **D5** | 🟢 | No prompt, no echo; every command answers; `Z` / `' '` are the no-op |
| **D6** | 🟢 | Phase-1 op set — six commands, mask-addressed where simultaneity matters |
| **D9** | 🟢 | Level-command encoding — Select + Set + Clear, BSRR-style, both = toggle |
| **D7** | 🟢 | Module name — `automation_console.{c,h}`, not a "test harness" |
| **D8** | 🟢 | Wire encoding — ASCII lines, hex for binary data, both directions |
| **S1** | 🟢 | Reject-on-error, structured machine-readable error response |
| **S2** | 🟢 | Re-entry lock already covers it — verified, no code change needed |
| **S3** | 🟢 | 15 s `#define`, reset on any byte, announced exit `!~,TMO` |
| **S4** | 🟢 | Cycling: entry/exit non-disturbing, commits deferred, `P` forces one |
| **S5** | 🟡 | Transport error counters as an assertable builtin |
| **S6** | 🔵 | Async event queue — ISR-safe fixed records, formatted at dequeue *(phase 2)* |
| **S7** | 🔵 | Deferral rule and queue-overflow policy (drop + dropped-count) *(phase 2)* |
| **S8** | 🟡 | Timestamp source and capture point — `TIM2->CNT`, sampled at event time |
| **S9** | 🔵 | Event subscription / arming — which events report, and when *(phase 2)* |
| **S10** | 🟢 | Minimum cycle-period guard — 50 ms, REPL-commanded cycling only |
| **S11** | 🟡 | Host receive contract — dispatch by sigil, never by position |
| **S12** | 🟡 | Menu-mode human log — separate gate flag, emitted from the job runner |
| **S13** | 🟢 | Switch-op responses carry state; ok/error lives in the frame header |
| **S14** | 🟢 | µs on the wire; no auto-persist — commit is the explicit `P` command |
| **S15** | 🟡 | Start/stop edge cases — already cycling, level on stop, repeat exhaustion |
| **S16** | 🟢 | Repeat progress — report cycles *done*; host derives remaining |
| **I1** | 🟡 | Hook point — where the 0xDA intercept lives |
| **I2** | 🟡 | Op-table home + registration-time collision checking |
| **I3** | 🟡 | Build gate for release images |
| **I4** | 🟡 | Line-buffer sizing and static (not stack) allocation |
| **I5** | 🟡 | **Async-readiness contract — what phase 1 must do so phase 2 is a drop-in** |
| **I6** | 🟢 | State readback — three bitmaps: level (`IDR`), mode (`OCxM`), cycling-active |
| **I7** | 🟡 | Sigil-prefix stray output at `_write()` so logs cannot desync the host |
| **I8** | 🟢 | `v_acon_emit()` — the frame emitter; sigil is an argument, not a convention |
| **T1** | 🔴 | Host-side Python runner |
| **T2** | 🔴 | Sync decisions back into `SwitchTester-Design.md` |
| **T3** | 🔵 | Promote the REPL to `G0B1_Skeleton` alongside `uart_stream` |

### Wish list (v2+)

| ID | Subject |
|----|---------|
| **W1** | Binary bulk-payload variant — length + COBS + CRC for ADC capture streams, if **D8** goes ASCII |
| **W2** | ~~Async event push~~ — **promoted to v1** (**Q1**); machinery is **S6**–**S9** |
| **W3** | Sense-channel ops — blocked on the sense design, which does not exist yet |
| **W4** | Harness on a *second* UART so the console stays human while a script drives |
| **W5** | Scripted sequence upload — host pushes a small program the tester runs unattended |

---

## LOCKED CONTEXT

Established; do not re-litigate unless explicitly reopened.

- **Transport works.** `uart_stream` owns USART2's interrupt path outright; HAL
  never sees that vector. Console TX ring 1024 B, RX ring 256 B
  (`DEV_CONFIG_CONSOLE_TX_BUF_SIZE` / `_RX_BUF_SIZE`). Bench-verified 2026-08-02.
- **`u32_uart_stream_get_error_count(h)`** exists and counts ORE/FE/NE/PE per
  instance — the assertable quantity **S5** is about exposing.
- **The REPL bypasses the menu system.** It does not wrap, reuse or re-enter
  `v_menu_exec()`. Decided when the backdoor was first sketched.
- **Sentinels are `ACON_ENTER` 0xDA / `ACON_EXIT` 0xA5** — MS-bit set so
  they cannot collide with any printable menu key, and a bit-complement pair.
- **Existing input path** is `v_debug_menu_service()` → `getchar()` →
  `v_debug_menu_exec()`, guarded by a static re-entry lock, and it is called
  recursively from `v_debug_delay()`. **S2** is about not breaking that.
- **`switch_out` public API is already sufficient** for a drive-side op set:
  `v_switch_out_set/toggle/pulse/all_off`, `x_switch_out_get`,
  `u32_switch_out_pulse_remaining`, `v_switch_cycle_start/stop/stop_all`,
  `u8_switch_cycle_running`, `u8_switch_cycle_any_running`. No new plumbing
  needed to expose drive control — only argument parsing and response framing.
- **Two timebases exist, and they are not equivalent.** `SYSTEM_TICK()` is
  `HAL_GetTick()` — **1 ms** resolution (`PERIODIC_TIMER_INTERVAL_MS` is 1).
  `TIM2->CNT` is free-running, `Prescaler = 63` on a 64 MHz clock → **1 µs** per
  tick, `Period = 0xFFFFFFFF`, wrapping every **71.6 minutes**. TIM2 is already
  the cycling timebase and is never stopped. **S8** turns on this difference.
- **What this project is, and what follows from it (stated 2026-08-02).**
  SwitchTester is a **hobby project and bench instrument** — built to chase a
  specific pushbutton "lockup" race condition in a consumer product. It is *not*
  a commercial product. The consequence is a deliberate asymmetry that should not
  be argued away in a later session: **automated (REPL-commanded) runs are
  guarded; human-initiated ones get relaxed or no guards.** Being able to feed the
  menu deliberately insane values to see where the system breaks is a *wanted*
  capability, not an oversight. **S10** is the first instance of this rule.
- **Two floors already exist, at different layers** (verified 2026-08-02):
  `SWITCH_CYCLE_MIN_LEAD_US` is **4 µs** (`switch_out.c:69`) — the missed-compare
  guard, which clamps any schedule closer than that to `CNT + 4 µs`. Above it,
  `v_switch_cycle_start()` validates the stored parameters against
  `SWITCH_CYCLE_TIME_MIN_US` = **10 µs** and `SWITCH_CYCLE_TIME_MAX_US` =
  **1000 s** (`switch_out.h:45-46`). Both are correctness guards, not policy;
  **S10**'s 50 ms REPL floor sits on top of them.
- **`v_switch_cycle_start()` fails silently.** It returns without any indication
  if the channel is already running, or if the stored times are out of range.
  Nothing in the current API reports which happened — which is precisely why
  **I6** carries the `run` bitmap and **S13** returns post-state.
- **Repeat exhaustion already routes ISR → main loop through the job runner.**
  `v_switch_cycle_isr()` calls `v_switch_cycle_halt()` and then
  `v_job_add_with_params(NULL, JOB_CYCLE_COMPLETE, u8_channel, 0)`. The
  "record in the ISR, format in the main loop" discipline **S6** and **S12**
  need is therefore already established in this codebase, and the phase-2
  cycle-complete async event should hook this existing job rather than invent a
  parallel path.
- **While cycling, `OCxM` is `ACTIVE`/`INACTIVE`, never a *forced* value** — which
  is exactly what lets **I6**'s mode bitmap distinguish timer-driven from manual.
  `v_switch_cycle_halt()` forces LOW and clears `u8_running` together, so mode and
  run agree in normal operation.
- **Intended rate profile (2026-08-02).** A typical full switch cycle is around
  **1 second**, possibly longer. Edge timing and on/off time *resolution* need to
  be **better than 1 ms**; the cycle *period* is never in the milliseconds, let
  alone microseconds. This retires the throughput objection to ASCII framing
  (**D8**) — at ~2 events/second, frame size is irrelevant — and it is what
  **S10**'s minimum-period guard formalises.
- **The link is genuinely full duplex.** `uart_stream`'s ISR services `RXNE` and
  `TXE` in the same handler against two separate rings, with no coupling: the
  device transmitting does not impede reception, and vice versa. Verified in
  `uart_stream.c` (the RX drain loop and the TX fill loop are independent). This
  is what makes **S11** a host-parser question rather than a wire problem.
- **Async event sources will be ISR context.** Cycling transitions come from the
  TIM2 compare ISR (priority 0); sense comparator edges will come from EXTI
  (priority 3). Neither can call `printf`, so **S6** is not optional.
- **Identity macros available** for a version/ping builtin: `PRODUCT_NAME`,
  `PLATFORM_NAME`, `FIRMWARE_VERSION`, `BUILD_CONFIG` (in `device_config.h`).
  There is **no** `BUILD_NUMBER` here, unlike the reference project.
- **The cycler is still unverified on hardware.** Nothing in this plan should be
  taken as evidence it works.

---

## Detail sections

### I5 — Async-readiness contract

**Status:** 🟡 · **Needs user:** no — but it is the row phase 1 is judged against

Phase 1 ships no async machinery. The risk is that phase-1 code makes phase 2 a
refactor instead of an addition — and worse, that it invalidates host scripts
already written. Four obligations prevent that. All four are close to free in
phase 1; all four are expensive to retrofit.

1. **Emit sigils from the very first frame.** Every device→host line starts with
   `=`, `!` or `#` (**D3**) even though `*` is unused in phase 1, and the phase-1
   host runner (**T1**) must already skip lines whose sigil it does not
   recognise. This is the one that actually matters: if phase 1 emits bare lines,
   every host script written against it breaks the day the first `*` frame appears.
2. **Ops never `printf` directly.** They emit through a framing helper —
   `v_acon_emit(...)` — so that phase 2 can add the "not inside a response frame"
   interlock in exactly one place. Ops that write to stdout freely cannot be
   fenced later without touching every op.
3. **The flush call site exists in phase 1.** `v_acon_flush_events()` is called at
   the top of the wait-for-command state and is an empty function. Zero cost, and
   it fixes the one architectural point — *where* async is allowed to happen —
   while the loop is still small enough to see whole.
4. **Response frames are bounded in phase 1.** Every response is one line, or
   declares its `K<n>` payload count up front (**D3**). Emitting async "between
   frames" is only well defined if a frame has a knowable end; a response that
   just trails off gives phase 2 nowhere safe to insert.

Explicitly *not* required in phase 1: the event record struct, the ring, the
overflow counter, the subscription mask. Those are additive once the four above
hold.

**Leaning:** take all four. Combined they are perhaps thirty lines and one empty
function.

**Resolution:** _pending_

---

### S11 — Host receive contract

**Status:** 🟡 · **Needs user:** no

**The identified window is real, and it is harmless.** The host commits to
sending a command; in the same one-or-two-byte-time window the device dequeues an
event and emits it. The host then reads a `*` frame where it might have expected its
response.

Nothing is corrupted. The link is full duplex — two rings, two directions, no
coupling — so a device→host event frame and a host→device command frame in flight
simultaneously do not interfere on the wire. The device's own invariant also
survives: emission happens only in the main loop, one whole frame at a time, so
an event can precede or follow a response but can never land inside one.

What the window actually breaks is a *host parser that identifies a response by
position* — the common pyserial idiom of "write the command, read one line, that
line is the answer".

**Options considered:**

- **A `ready for events` / `stop events` token pair.** Host declares when async
  is welcome. This does not close the window: the `stop events` token races the
  device's in-flight emit exactly as before, so the host must *still* handle a
  straggler arriving after it asked for silence. It adds a two-state mode that
  can disagree between the ends, and while events are suppressed the device is
  queueing them anyway — so the deferral has moved, not disappeared.
- **Sigil dispatch as a contract.** The host reads lines and routes by first
  character: `*` and `#` go to handlers, `+` is payload, and the first `=` / `!`
  line is the
  response to the outstanding command. The window becomes a non-event because no
  code anywhere assumes positional ordering.

**Leaning / recommendation:** sigil dispatch, stated as a protocol contract
rather than left as an implementation detail of the runner — *the host must be
prepared for an async frame at any point at which it is reading device output.*
This is already **I5** obligation 1 seen from the host side, and it is why that
obligation applies to the phase-1 runner even though phase 1 emits no events: a
runner written to the contract needs no change when phase 2 lands, and one
written to positional reads has to be rewritten.

Note that the **S6** REPL-mode enqueue gate bounds this further — outside REPL
mode no event exists to race with anything.

**Resolution:** _pending_

---

### S12 — Menu-mode human log

**Status:** 🟡 · **Needs user:** no

Cycling transitions may be worth *watching* from the debug menu too, not just
reporting to a host. That is a different feature from the REPL event queue and it
must not reuse the same gate: **S6**'s flag means "a host is attached and events
may be enqueued", and overloading it would couple the human view to REPL mode.

Two constraints carry over unchanged, though:

- **No `printf` from an ISR** — same reason as **S6**. Emission goes through the
  job runner, with the ISR doing nothing but recording what happened.
- **Timestamps are captured at the event**, not at print time (**S8**), or the
  log shows when the job ran rather than when the switch moved.

**Leaning:** a second, independent gate flag; the transition ISR records into a
small structure and posts a job; the job formats and prints. Whether that shares
**S6**'s ring or gets its own is an implementation detail to settle when the row
is built — sharing is tempting but couples two features with different lifetimes,
and the ring is 512 bytes.

Low priority — this is a convenience, and the REPL path is the one that matters
for the campaign the tester exists to run.

**Resolution:** _pending_

---

### S10 — Minimum cycle-period guard *(resolved)*

**Status:** 🟢

**`on_time + off_time >= 50 ms`, enforced only on REPL-commanded cycling.** The
threshold is `ACON_MIN_CYCLE_PERIOD_US` in `device_config.h`. Violations are **rejected**
with a structured error naming the offending value (**S1**), never clamped —
silently running a different test than the host asked for is the failure mode this
whole protocol exists to prevent.

**The debug menu is deliberately exempt.** Its setters keep a much lower floor —
1000 µs on/off proposed — or none at all, above the 4 µs hardware guard that
`SWITCH_CYCLE_MIN_LEAD_US` already enforces for correctness. Feeding the menu
absurd values to find where the system breaks is a wanted experiment.

**Rationale, and why the earlier recommendation was declined.** The proposal on
the table was to *also* hard-check at `v_switch_cycle_start()`, so the floor held
regardless of path — menu edits, and NVM restores, which matters here because this
project has already been bitten by a pool carrying another project's contents. That
was rejected on the correct grounds: this is bench tooling, not a commercial
product, and the guard exists to protect *automated* runs, not to protect the
operator from themselves.

**The residual case, recorded for honesty rather than as an objection:** a cycle
configured below 50 ms from the menu, followed by REPL entry, will report events at
that rate. The numbers say it does not matter — at the proposed 1000 µs floor the
worst case is ~1000 edges/s, and at ~350 µs per ASCII frame that is roughly a third
of the link, which **S7**'s drain loop and overflow accounting already handle. With
no menu floor at all the cycler bottoms out at the 4 µs hardware guard, but the
**S6** REPL-mode enqueue gate means nothing is being queued in menu mode anyway.

---

### D3 — Frame identity and grammar *(resolved)*

**Status:** 🟢

**Question:** How does the host distinguish a command response from an async
event, how does it know a frame is complete, and **where does the ok/error status
live**?

Locked by **Q1**: both frame kinds carry a lead identifier, async is never
inserted into a response frame, and a response is a bounded thing the host reads
to completion.

**Status is structural, not a payload field.** It is carried by the sigil plus
the header token, so a host branches to its error path from the *first character*
of the line, before parsing anything. Encoding it as `st=OK` inside a uniform
frame would work equally well on the wire but forces every reader to parse before
it can tell success from failure.

**Every device→host line carries a sigil in column 0:**

| Sigil | Frame kind |
|-------|------------|
| `=` | command response, success — no `OK` token needed, the sigil *is* the status |
| `!` | command response, failure |
| `+` | payload continuation line of a multi-line response |
| `*` | async event (phase 2) |
| `#` | human/log noise — not part of the protocol; host ignores the line |

The `#` row is easy to forget and expensive to omit: something will eventually
`printf` a stray line from a job or an error path while the REPL is active, and
without a "not for you" sigil that line desynchronises the host mid-frame.
Prefixing it makes stray output harmless instead of fatal.

**Grammar.**

``
=<op>[,<tok>]...                 success, complete in one line
!<op>,<CODE>[,<tok>]...          failure, complete in one line
=<op>,K<n>[,<tok>]...            success, exactly <n> payload lines follow
+<text>                          payload line -- free text, NOT tokenised
*<src><ch>,T<hex>,V<hex>         async event (phase 2)
#<text>                          not protocol; host ignores the line
``

`<op>` is the single command character, echoed back so a desynchronised host
notices immediately. A `<tok>` is **one uppercase key letter immediately followed
by a hex value** — no `=`, no `0x`, no separator inside the token. **Tokens are
comma-separated**, so a host splits on `,` and reads each field as
`key = tok[0], value = int(tok[1:], 16)`. The comma is exact where a space is
not: nothing accidentally emits a double comma, and no field can be lost to
whitespace trimming.

| Key | Meaning | | Key | Meaning |
|:---:|---------|-|:---:|---------|
| `L` | level bitmap | | `C` | repeat count |
| `M` | mode bitmap | | `D` | cycles done |
| `R` | run bitmap | | `K` | payload line count |
| `N` | oN-time, µs | | `T` | timestamp, µs |
| `F` | oFf-time, µs | | `V` | value |

**Protocol-level frames use the reserved opcode `~`**, so session events are
machine-parseable in exactly the same shape as command responses rather than being
a special case the host must pattern-match: `=~,V1` on entry (ready, protocol
version 1), `=~,BYE` on a clean exit, `!~,TMO` on an idle timeout. `~` is reserved
at registration (**I2**) so no domain op can claim it.

A response is **one line** unless its header carries `K<n>`, in which case exactly
that many `+` lines follow. The host reads a line, checks for `K`, reads that many
more — no terminator scanning, and a truncated frame is *detectable* because fewer
lines arrive than were promised. Of the phase-1 commands only the `L` op-list
needs the multi-line form.

**Worked examples:**

``
=S,L9,M4,R4                          set levels -> ok
!W,RNG,L9,M4,R4                      write params -> rejected, out of range
=G,L9,M4,R4,N7A120,F7A120,C0,D4D2    get params for a channel
=L,K8                                op list, 8 payload lines follow
+V ping / version
``

**What was tightened, and what deliberately was not.** `=S,L9,M4,R4` is 13 bytes
against 37 for the earlier `=OK cmd=S level=0x9 mode=0x4 run=0x4` — a 65 %
reduction. Removed: the `OK`/`ERR` words (the sigil already says it), the `cmd=`
key (position after the sigil says it), the `0x` prefixes, the `=` inside every
token, and the echo of the offending value on an error (the host sent it and
already knows it).

**Not** taken all the way to pure positional (`=S,9,4,4`), which would save four
more bytes. Terminal readability is the entire reason **D8** chose ASCII over
binary; spending four bytes to keep a response self-describing is consistent with
that decision, whereas positional fields would undercut the rationale while still
paying ASCII's costs. One-character keys are the point at which those two pressures
balance.

**Also reversed here, on new information:** the bracketed `=END` form was
originally argued for because a single-line frame would have to "squeeze a
four-channel state dump into key=value pairs". Once **D6** landed, that dump is
three hex nibbles. A declared count is strictly better than a terminator anyway —
it detects truncation, which a terminator cannot.

---

### S9 — Event subscription / arming

**Status:** 🔵 — phase 2 · **Needs user:** yes, when phase 2 opens

**Question:** Which events are reported, and from when? Three sub-questions that
have to be answered together:

1. **Default off or default on?** If every cycling transition reports
   unconditionally, a soak run at a 1 ms half-period floods the link with data
   nobody asked for and overflows the queue continuously (**S7**).
2. **Granularity.** Per event *class* (switch transitions / sense edges / ADC),
   per channel, or the cross product? Per-class is one bitmask and is probably
   enough; per-channel matters if the host wants to watch one DUT input while
   three others cycle as background load.
3. **Lifetime.** Does a subscription survive REPL exit and re-entry? Survive
   reset? If events are enqueued while no host is attached, the queue fills and
   the dropped-count is meaningless by the time anyone reads it.

**Largely superseded by the S6 REPL-mode gate.** With enqueue disabled outside
REPL mode, "default off" and "cleared on exit" are both satisfied structurally —
the coarse subscription *is* REPL mode, and question 3 (lifetime) answers itself.
What remains of this row is only the fine-grained part: whether a host that wants
to watch one channel while three others cycle as background load can say so.

**Leaning / recommendation:** phase 2 ships with the REPL-mode gate alone — all
classes, all channels, reported whenever a host is attached. A per-class /
per-channel mask op is added only if a real campaign wants it; at the intended
~1 s cycle rate, filtering to save two frames a second is not worth an op, a
mask, and the chance of a host silently filtering out the thing it came to
measure. Never persisted to NVM — session state, not configuration.

**Resolution:** _pending_

---

### S6 — Async event queue

**Status:** 🔵 — phase 2, design below is the standing plan · **Needs user:** no

Events originate in ISR context — TIM2 compare for cycling transitions (priority
0), EXTI for sense edges later (priority 3). `printf` in an ISR is out of the
question: it is not reentrant, it would block on the TX ring, and it would run
formatting at priority 0.

So the queue holds **fixed-size binary records, not strings**, and formatting
happens in the main loop at flush time:

``c
typedef struct {
    uint32_t u32_timestamp;   /* TIM2->CNT at the event -- S8            */
    uint8_t  u8_class;        /* SW transition / sense edge / ADC / ...  */
    uint8_t  u8_channel;
    uint16_t u16_value;
} repl_event_t;               /* 8 bytes                                 */
``

An 8-byte record in a power-of-two ring is a single-producer/single-consumer
structure with the same discipline as `uart_stream`'s rings — producer (ISR)
writes `head`, consumer (main loop) writes `tail`, disjoint aligned `volatile`
indices, no critical section needed on M0+. Multiple ISR sources at *different*
priorities do break the single-producer assumption, though: TIM2 at priority 0
can preempt EXTI at priority 3 mid-enqueue. That needs either a short PRIMASK
guard around the head advance or all event sources at one priority — the guard is
a handful of cycles and does not constrain the priority map, so prefer it.

**Enqueue is gated on REPL mode.** While the debug menu owns the console, no
interrupt or background process may push into the queue at all; the gate flips to
allowed on REPL entry and back on exit. This is a single flag tested at the top of
the enqueue path, and it settles several things at once: nothing accumulates for
an absent host, the queue cannot overflow while nobody is listening, a dropped
count is only ever attributable to a live session, and the ISR cost outside REPL
mode is one predictable-branch test.

**Corollary — reset the queue on entry, not just enable it.** A previous REPL
session can exit with events still queued. Without a head/tail/dropped-counter
reset at entry, those stale events are emitted into the new session carrying
timestamps from the old one, which is exactly the sort of thing that produces an
unreproducible measurement nobody can explain. Reset costs three stores.

**Leaning:** 64 records (512 bytes), statically allocated, PRIMASK-guarded
enqueue, gated on REPL mode, reset at entry, formatted at dequeue.

**Resolution:** _pending_

---

### S7 — Deferral rule and overflow policy

**Status:** 🔵 — phase 2, design below is the standing plan · **Needs user:** no

Locked by **Q1**: async frames are emitted only between the end of one response
frame and the start of the next — never inside one.

**Drain structure.** The executive loop makes the deferral rule structural rather
than something that has to be remembered at each emit site:

``c
for (;;)
{
    if (<lead char available from host>)
    {
        <capture the rest of the command frame>
        <dispatch on opcode>
        <emit the response frame>
    }

    <service the event queue -- emit at most ONE event frame>
}
``

Two properties fall out of this ordering and both are wanted. Command service
comes first, so a pending command is never delayed behind a backlog. And exactly
one event per iteration — rather than draining the whole queue — bounds the
latency a command can suffer to a single frame time instead of up to 64 of them;
when the host is idle the loop spins freely and a backlog still drains as fast as
the link allows.

**One hazard in the capture step.** `<capture the rest of the command frame>` is
a blocking read. A host that sends a lead character and then stalls would wedge
the device there, so **S3**'s idle timeout must apply *inside* frame capture, not
only at the wait-for-command read — otherwise the anti-wedge guarantee has a hole
exactly one byte wide.

Two consequences that the rule creates and that need explicit answers:

- **Transmit time is not event time.** A queued event may be emitted milliseconds
  after it happened. This is exactly why **S8** insists the timestamp is captured
  at the event, in the ISR — if it were sampled at flush the queueing latency
  would be silently folded into the measurement, which defeats the purpose of
  timestamping at all.
- **The queue can overflow.** A long-running command plus a fast cycle rate fills
  64 records quickly. Silent loss is the failure mode **S1** exists to prevent, so
  overflow must be *reported*, not just survived.

**Options for overflow:** drop-newest (cheap, keeps the oldest history),
drop-oldest (keeps the most recent, needs a tail advance in the ISR), or block
(unacceptable — this is an ISR).

**Leaning:** **drop-newest** with a saturating dropped-counter, and emit a
`*O,D<count>` frame (source `O` for overflow, `D` for dropped) at the head of
the next flush whenever the counter is
non-zero, then clear it. The host then knows precisely that its event record has
a hole and how big, rather than quietly receiving an incomplete history. Combined
with **S9**'s default-off subscriptions, overflow should be rare in practice.

**Resolution:** _pending_

---

### S8 — Timestamp source and capture point

**Status:** 🟡 · **Needs user:** no — but it is a correction, not a detail

**Question:** What clock timestamps an async event, and when is it sampled?

`SYSTEM_TICK()` / `HAL_GetTick()` is the obvious choice and is the wrong one
here: **1 ms resolution**. A switch bounce is tens of microseconds, and the whole
point of this tester is measuring what the DUT does at switch edges. A 1 ms
timestamp cannot distinguish a bounce from a clean edge, and the drive side is
already programmed in microseconds (`u32_on_time_us` / `u32_off_time_us`).

`TIM2->CNT` is free-running at **1 µs**, 32-bit, never stopped, and is *already*
the timebase the cycling engine schedules against — so a switch-transition event
and the compare that caused it are expressed in the same units, on the same
clock, with no conversion.

**Two refinements worth taking:**

- For a **cycling transition**, do not read `CNT` in the ISR — the `CCR` value
  that fired *is* the exact edge time, unaffected by interrupt latency. Reading
  `CNT` instead folds in however long the ISR took to be entered.
- `TIM2->CNT` wraps every **71.6 minutes**, which is well inside a soak run. The
  device should not try to extend it; the host unwraps trivially by watching for
  a decrease in a monotonic event stream. Document the wrap rather than hiding it.

**Leaning:** `TIM2->CNT` (or the firing `CCR`) as a raw 32-bit microsecond
stamp, captured in the ISR at enqueue, wrap documented and handled host-side.

**Resolution:** _pending_

---

### D9 — Level-command encoding *(resolved)*

**Status:** 🟢

**Three 4-bit masks: `Select`, `Set`, `Clear`.** `Select` governs *mode* —
which channels are forced into manual control; `Set`/`Clear` govern *level*,
BSRR-style, with both bits set meaning toggle.

| `Select` | `Set` | `Clear` | Result for that channel |
|:---:|:---:|:---:|---|
| 0 | – | – | **Untouched.** Mode and level both preserved; a cycling channel keeps cycling. |
| 1 | 0 | 0 | Manual, **hold current level** — freeze wherever it is. |
| 1 | 1 | 0 | Manual, **high**. |
| 1 | 0 | 1 | Manual, **low**. |
| 1 | 1 | 1 | Manual, **toggle** — invert the present pad level. |

**Why all three masks and not two.** Mode and level are *orthogonal*, which is
what the original proposal's `Select` captured and what a bare two-mask
set/clear form loses. Dropping `Select` would make "force to manual without
changing the level" inexpressible — and that operation has a specific use here:
`Select` = all channels, `Set` = 0, `Clear` = 0 **freezes every switch
simultaneously, wherever it is**. For an instrument built to catch a DUT lockup,
capturing the switch state at the moment of the event is exactly the kind of
thing worth being able to do in one command.

Replacing AND/OR with Set/Clear is what buys **toggle**, which the AND/OR pair
could not express in any combination — and it removes AND/OR's redundant
encoding (`AND=0,OR=1` was a second spelling of "set") in the process.

**Toggle is well defined even from cycling mode:** `Select` forces manual first,
and the level inverted is the present *pad* level read from `IDR` (**I6**), which
is valid whether the channel was cycling or manual. So "freeze and invert" is a
single atomic command rather than a read-modify-write race.

The only remaining redundancy is benign and conventional — when `Select` is 0 the
`Set`/`Clear` bits are simply ignored, the same way masked-off bits are ignored
anywhere else.

**Note:** the `Select=1, Set=0, Clear=0` → *hold* cell is the natural completion
of the table rather than something explicitly specified; the alternative reading
is that it should be a no-op. Hold is the more useful of the two and leaves no
combination wasted, but it is a one-line change if the other reading is wanted.

**Ergonomics live in the host runner (T1), not on the wire.** A script author
calls `sw_set(on=['A'], off=['B'], toggle=['C'], freeze=['D'])` and never
assembles a mask by hand under any encoding.

---

### S13 — Switch-op responses carry state *(resolved)*

**Status:** 🟢

**Every switch-oriented command — success or failure — returns the resulting
state**, as the three bitmaps of **I6** — level from `IDR`, mode from `OCxM`, run from the cycle engine. There
is no bare-acknowledgement response.

The ok/error status is *not* part of that payload; it lives in the frame header
(**D3**), so the two concerns stay separate: the sigil and header say whether the
command succeeded, the payload says what the hardware is now doing.

**Why state rather than an ack.** A bare ack forces a script that wants to verify
into a second round trip, and that two-step is racy — between the ack and the
read, a cycling channel has moved on. Returning post-state makes every command
self-verifying in one exchange, and it costs nothing because the state is already
assembled to answer the read command at all. It also gives a host a free
assertion at every step of a sequence rather than only where it remembered to
ask.

**Errors carry state too.** A rejected command has not changed anything, but the
host still learns what the hardware is doing without a follow-up read — and the
value it wanted to check is usually exactly the one it was trying to set.

**Implementation:** one formatter emitting `L<n>,M<n>,R<n>` (**D3**), shared by
the read command, the getter and all four mutating commands. One parser on the host side, one
thing to document.

---

### S14 — Time units and NVM persistence *(resolved)*

**Status:** 🟢 — the persistence half is **entailed** by **S4**'s `P` command;
say so if the setter should auto-persist after all and this reopens.

Two ambiguities in the cycle-parameter command that would otherwise be settled by
accident at implementation time.

**Units.** The NVM parameters and the cycle engine are already in **microseconds**
(`u32_on_time_us` / `u32_off_time_us`), and `TIM2->CNT` is a 1 µs timebase
(**S8**). Milliseconds on the wire would mean a conversion at exactly one place
and a mismatch everywhere else.

**Resolved:** microseconds on the wire, hex-encoded like every other numeric
(**D3**), and **S10**'s constant is named
`ACON_MIN_CYCLE_PERIOD_US = 50000` so the floor is expressed in the same unit it
is checked in. A 32-bit µs field spans about 71 minutes per phase, which is far
past any plausible cycle.

**Persistence.** The menu setters call `v_switch_cycle_nvm_save()`. The REPL
shares the same parameter storage, so "does the REPL setter persist too?" has to
be answered.

**Resolved: no auto-persist from the automation console.** An explicit commit
command (`P`, **S4**) only makes sense if configuring does not already commit —
the two decisions are one decision. Two reasons, both concrete. A script
that configures before each of a thousand iterations would commit a thousand
flash writes for values it re-sends anyway — gratuitous wear on a part with no
wear levelling (`switch-cycling-plan.md` **W6**). And a run that silently mutates
stored configuration is not reproducible from a clean boot, which is the property
a test campaign most needs. If persistence is ever wanted it should be an
explicit op the host calls deliberately, not a side effect of configuring.

**Resolution:** _pending_

---

### S16 — Repeat progress reporting *(resolved)*

**Status:** 🟢

**Command 6 reports cycles _done_ (`u32_cycles_done`), not cycles remaining.**
The host derives `remaining = repeat − done` itself, since the getter returns the
configured repeat count in the same response. The one asymmetry the host must
carry: **`repeat == 0` means infinite**, so there is no remaining to compute in
that case — a host-side special case rather than a wire-format sentinel.

`u32_cycles_done` is ISR-written and main-loop-read, but it is 32-bit and
aligned, so the read is a single `LDR` on M0+ and cannot tear.

**Why not remaining, as originally proposed.** `repeat_count == 0` means "run until
stopped" — confirmed in
`switch_out.h:63` and in the ISR at `switch_out.c:167` —
`if (repeat_count && (cycles_done >= repeat_count))` — so a zero repeat count
never terminates. It is also `SWITCH_CYCLE_REPEAT_DEFAULT`, which makes the
infinite run the *common* case, not an edge case.

That makes "**# cycles remaining**, 0 if stopped or in manual mode" ambiguous in
three different ways, all of which encode as `0`:

| Situation | "remaining" | What the host should conclude |
|---|:---:|---|
| Stopped / manual | 0 | not running |
| Finite run, just completed | 0 | ran to completion |
| **Infinite run, cycling right now** | 0 (?) | *actively running* — the opposite |

**Options considered:**

- **Report `remaining` as specified**, with a sentinel for infinite (`0xFFFFFFFF`
  or `-1`). Works, but a sentinel is a thing to remember, and the field is still
  a derived quantity the device computes on the host's behalf.
- **Report `done` (`u32_cycles_done`) instead.** Always well defined — including
  during an infinite run, which is exactly the case a soak campaign cares about,
  since "how many cycles has it completed" *is* the question. The host computes
  `remaining = repeat - done` itself whenever `repeat != 0`, and knows there is no
  such thing as remaining when `repeat == 0`. No sentinel, no ambiguity.
- **Report both.** Redundant — `remaining` is derivable from the other two fields.

`done` also carries strictly more information: after a finite run completes it is
the final tally, whereas `remaining` collapses to 0 and says nothing about what
happened.

---

### S15 — Start/stop edge cases

**Status:** 🟡 · **Needs user:** no

Three cases the command set does not yet define, each of which will otherwise be
defined by whatever the code happens to do:

- **Start on a channel already cycling.** Today `v_switch_cycle_start()` returns
  silently — confirmed at `switch_out.c:485`. *Leaning: restart from ON.* A host
  that says "start" wants a known phase to measure against; silently ignoring
  leaves the channel at an arbitrary point in its cycle and every subsequent
  timing measurement inherits that unknown. Whichever way this goes, **I6**'s
  `run` bitmap in the **S13** response means the host is never left guessing.
- **Level after stop.** `v_switch_cycle_stop()` currently leaves the output
  forced LOW. For a race hunt, "stop and hold wherever you are" is also a
  plausible want. *Leaning: keep forced-LOW as the default* — it is the existing
  behaviour and it is deterministic — since the level command now expresses
  freeze-at-current-level directly (**D9**, `Select` set with `Set`/`Clear` both
  clear), which covers the other want without changing stop's semantics.
- **Repeat-count exhaustion.** A channel that finishes its repeats stops on its
  own, so the host's model of "is it running" goes stale with no notification.
  *Leaning:* the read command reports it accurately, and in phase 2 exhaustion
  emits an async event — this is one of the better arguments for **S6** existing
  at all, since polling for it is exactly what async is meant to replace.

**Resolution:** _pending_

---

### I6 — State readback *(resolved)*

**Status:** 🟢

**Three 4-bit bitmaps**, from three independent sources:

| Bitmap | Source | Meaning |
|--------|--------|---------|
| `level` | `GPIOx->IDR` | 0 = low, 1 = high — **valid in both modes** |
| `mode` | `OCxM` via `LL_TIM_OC_GetMode()` | 0 = manual/forced, 1 = under TIM control |
| `run` | `switch_cycle_t.u8_running` | 1 = cycling *and* repeat count not exhausted |

**Why `IDR` and not a shadow.** `x_switch_out_get()` reads `OCxM` and returns
`SWITCH_OUT_ON`/`OFF`/`TIMED` — deliberately hardware rather than a shadow, but
it **cannot report a level for a cycling channel at all**, because while cycling
`OCxM` holds `ACTIVE`/`INACTIVE` (act-on-match), never either *forced* value.
`IDR` captures the pad every AHB cycle regardless of TIM2 owning the pin through
its alternate function, so it reports the real driven level in either mode. For a
*tester*, reading the pad is also the more honest measurement — what the pin is
doing, not what the peripheral was told to do.

**Why `run` as well as `mode`, given they agree in normal operation.** Both go to
1 together at start and both go to 0 together on exhaustion, since
`v_switch_cycle_halt()` forces the output LOW *and* clears `u8_running` in one
place. So `run` is functionally redundant while everything works — and that is not
the reason to carry it:

- **It detects a silently refused start.** `v_switch_cycle_start()` returns
  without any indication if the channel is already running, or if the stored
  on/off times fall outside `SWITCH_CYCLE_TIME_MIN_US`/`MAX_US`. With **S13**'s
  post-state response, a host issuing "start" and reading back `run` learns
  immediately whether the command took effect — no separate query, no ambiguity.
- **It is a genuine cross-check.** `mode` is read from the timer, `run` from
  software state. The existing code comments already describe the `OCxM` read as
  "an independent cross-check on `switch_cycle_t.u8_running`". Disagreement means
  a bug, and on an instrument the ability to see that is worth four bits.

**What three bitmaps still cannot express: repeat progress.** "How many cycles
have completed" is a per-channel *count*, not a boolean, so it does not belong in
a bitmap. **Resolved by giving it its own command** — **D6** command 6, the getter
complement to the parameter setter — rather than by widening the read command.
The reporting semantics are **S16**.

**Implementation:** `u8_switch_out_level_bitmap()`, `u8_switch_out_mode_bitmap()`
and `u8_switch_cycle_run_bitmap()` in `switch_out.c`, so the REPL, the menu and
any later logging share one implementation.

---

### D6 — Phase-1 op set *(resolved)*

**Status:** 🟢

Seven commands, plus the executive builtins. Deliberately a starting set; more
are expected to follow.

| # | Command | Inputs | Returns |
|---|---------|--------|---------|
| 1 | Set switch output levels (manual mode) | `Select`, `Set`, `Clear` masks (**D9**) | level + mode + run bitmaps (**I6**) |
| 2 | Read switch state | none | level + mode + run bitmaps (**I6**) |
| 3 | Set cycling parameters | switch # (0–3), on-time, off-time, repeat count | level + mode + run bitmaps (**S13**) |
| 4 | Start auto-cycling | start bitmask | level + mode + run bitmaps (**S13**) |
| 5 | Stop auto-cycling | stop bitmask | level + mode + run bitmaps (**S13**) |
| 6 | **Get** cycling parameters | switch # (0–3) | on-time, off-time, repeat count, cycles **done** (**S16**) + the three bitmaps |
| 7 | Commit parameters to NVM now | none | written / no-change, or refused while cycling (**S4**) |

**Opcode assignment** — fixed here so collisions are designed out rather than
caught by **I2**'s registration scan at startup:

| Op | Command | | Op | Builtin |
|:--:|---------|-|:--:|---------|
| `S` | set switch levels (**D9**) | | `V` | version / identity ping |
| `R` | read switch state | | `L` `?` | list ops |
| `W` | write cycling parameters | | `Z` `' '` | no-op (**D5**) |
| `G` | get cycling parameters | | `Q` | quit |
| `C` | start cycling | | `~` | *reserved* — session frames (**D3**) |
| `X` | stop cycling | | | |
| `P` | persist to NVM (**S4**) | | | |

**Addressing is by mask where simultaneity matters, per-channel where it does
not.** Commands 1, 4 and 5 take bitmasks so that several channels change together
within a single command execution rather than across several commands separated
by host round-trip latency. For an instrument built to hunt a pushbutton race
condition, "these two switches changed at the same time" is a capability, not a
convenience — and it is not recoverable by a host issuing four separate commands.
Command 3 is per-channel because its parameters differ per channel and there is
nothing to synchronise.

Setting parameters (3) is deliberately separate from starting (4), so a host can
stage several channels' configurations and then start them together.

**Command 6 is the getter complement to command 3** — same per-channel
addressing, returning the configured on-time, off-time and repeat count plus
run progress. It is what carries the per-channel data the three bitmaps of
**I6** structurally cannot: a count is not a bit. It also lets a host verify
what it configured without inferring it, and read the final tally after a
finite run has completed. Progress is reported as cycles *done* (**S16**).
Response stays single-line, so it needs no `n=` payload (**D3**).

**Parameter storage is shared with the debug menu** — the REPL sets the same
values the menu does. Whether it also *persists* them is **S14**.

**Deferred from the earlier candidate list:** pulse, toggle, all-off, transport
error counters (**S5**) and NVM commit/status. All remain sensible additions;
none is needed to run a first campaign, and several are expressible with the five
above (all-off is a level command with every channel selected).

---

### S4 — Behaviour while channels are cycling *(resolved)*

**Status:** 🟢

**1. Entry and exit are non-disturbing.** The host may enter and leave the
automation console freely while channels are cycling — a soak driver attaching to
query status is the main use case. Neither the sentinel, `Q`, nor an idle timeout
touches drive state. Explicit `X` is the only thing that stops a run, so a
timeout firing mid-soak cannot silently kill it.

**2. NVM commits stay deferred while any channel cycles.** Existing behaviour,
kept: `app_main.c` holds the commit off and zeroes `u16_commit_timer` so the
auto-commit countdown re-arms, and the parameters reach flash once the run ends.
A commit erases and rewrites a flash page — tens of milliseconds against phase
times that can be as short as 10 µs — so letting it run mid-cycle would stall the
compare ISR.

**3. A new command requests an immediate commit** rather than waiting for the
auto-commit timer: `P` (persist), **D6** command 7. It **refuses while cycling**,
which is what keeps rule 2 intact — the deferral is not something the host can
override, only something it can pre-empt when the bench is idle.

`P` calls `x_nvm_commit()` **synchronously** rather than posting `JOB_NVM_COMMIT`,
so the response carries the real outcome instead of an acknowledgement that a job
was queued. The distinction matters: `x_nvm_commit()` returns `NVM_ERROR_NONE`
(a page was actually written), `NVM_ERROR_NO_CHANGE` (pool already clean, no
erase/write spent), or a genuine failure — and a host that just configured a soak
run wants to know which of those happened before it walks away.

| Response | Meaning |
|---|---|
| `=P,W1` | written — a flash page was erased and rewritten |
| `=P,W0` | no change — pool was already clean (`NVM_ERROR_NO_CHANGE`) |
| `!P,BUSY,R<n>` | refused, cycling active; `R` is the run bitmap |
| `!P,NVM,E<n>` | commit failed; `E` is the `nvm_error_t` |

The `R` bitmap on the BUSY refusal is deliberate — it tells the host *which*
channels it needs to stop, rather than making it issue a separate read to find
out.

**Note on the opcode.** The suggestion was to reuse `W`, but `W` is already the
cycle-parameter *writer* (**D6** command 3). `P` avoids a collision that **I2**'s
registration scan would have caught at startup anyway — better to not build it in.

---

### D2 — Command namespace and case sensitivity *(resolved)*

**Status:** 🟢

**Strict case sensitivity.** No `toupper()` anywhere in dispatch; the namespace is
the full printable range `0x20..0x7E`. The reference implementation folds case in
both the builtin `switch` and the op-table scan, halving an already small
namespace — this does not.

**I2's collision check reserves both cases of every builtin letter**, so `v` and
`q` cannot be claimed by a domain op even though they are technically free. A host
that sends lowercase by habit then gets a clean `!v,UNK` rather than silently
hitting some unrelated command that happened to take the letter.

**Rationale:** loud failure beats a namespace booby trap. Case folding makes
`s` and `S` the same command forever, which is a permanent halving of the
namespace to buy tolerance for a typo that a machine does not make.

---

### D4 — Host→device grammar *(resolved)*

**Status:** 🟢 — **no checksum, no CRC, no length field**

**The question was:** does host→device get the same rigour as device→host — a
fixed packet with length, opcode, sub-opcode, validation and an EOT marker — or a
freeform opcode plus arguments?

**The asymmetry is the point.** The two directions have different consumers and
should not be assumed to need the same format:

- **Device→host is parsed by a machine** and is where a mis-parse silently
  corrupts a test result. That direction earns strict framing (**D3**).
- **Host→device is parsed by a 500-line C parser on an MCU**, and is the
  direction a human types by hand when debugging the REPL from Tera Term. A
  length-prefixed CRC'd packet makes that impossible without a tool, and buys
  little: a corrupted command is *already* rejected rather than partially
  executed (**S1**), and the transport counts its own ORE/FE/NE/PE (**S5**).

**Resolved: freeform, comma-separated to match the response direction**
(**D3**). First character is the opcode; the remainder splits on `,`; CR or LF
terminates; empty lines are ignored. One splitter on each side of the link and
one rule to remember, rather than "commas that way, spaces this way".

``
S,3,1,2          select=3, set=1, clear=2
W,1,7A120,7A120,0   channel 1, on 500000 µs, off 500000 µs, repeat 0 (infinite)
G,1              get channel 1 parameters
``

**Numerics are hex in both directions**, uniformly — one parse routine, one format
routine, no per-field rule to look up. The honest cost is that hand-typing a time
means converting first (500 ms → `7A120`), which is mildly unpleasant at a
terminal; it is paid by the host runner (**T1**) in practice, and the debug menu
remains the human interface for anything typed by hand. Channel indices and
bitmaps are small enough that hex and decimal coincide.

A shared `b_acon_arg_u32()` helper does the field parsing and range-checking, so
it is not reimplemented — and mis-implemented — across six ops, and so **S1**'s
error frames can distinguish "missing" from "out of range".

**Why no CRC.** A corrupted command is already rejected rather than partially
executed (**S1**); the transport counts its own ORE/FE/NE/PE per instance
(**S5**), so line-level corruption is observable and assertable without a
per-frame check; and a truncated command line simply fails to parse. A CRC would
add a field to every command, a computation on both ends, and a hand-typing
barrier at the terminal, in exchange for detecting a class of error the layers
above and below already catch.

If a command ever needs to carry bulk data, it carries it as hex in a field, which
keeps the grammar unchanged.

---

### D5 — Prompt / echo policy *(resolved)*

**Status:** 🟢

**No prompt, no echo.** The menu echoes `Cmd [x]` per *keystroke* and prints
`{Ready}:`; a 30-character command line would produce 30 echo lines for a host to
filter. Automation-console mode emits neither. The response frame (**D3**) is the
only output, which is what makes the channel deterministic.

**Every non-empty command line produces exactly one response frame** — recognised
or not. An unknown opcode returns `!<op>,UNK`. There is no silent path, so a host
that gets nothing back knows the link or the device is at fault, never the
protocol.

**Empty lines remain silent, and that is deliberate — not an exception.** A host
sending CRLF line endings delivers a bare `
` after every `
`. If an empty line
produced a response, every single command would generate a spurious extra frame
and the host would run permanently one frame out of step. Empty input is discarded
before dispatch.

**A dedicated no-op is therefore needed. It is `Z`, with `' '` (0x20) as an
alias.** It takes no arguments, touches nothing, and returns `=Z` — *both*
spellings answer `=Z`, so the host never has to deal with a frame whose opcode
field is a space, which whitespace trimming anywhere in the chain could silently
eat. Space is a natural fit: it is literally the first character of **D2**'s
`0x20..0x7E` namespace. **I2** reserves both.

Three uses:

- *"Are you there"* — the minimal liveness check, 4 bytes out and 4 back.
- **Resynchronisation.** A host that suspects a partial line is sitting in the
  device's buffer sends `
` (flushes the line, silently) then `Z` (gets a known
  frame). Confirmation of sync without side effects.
- **Latency measurement**, with no work in the path to confound it.

`V` remains the identity ping and is the better call at session start — it pins
product, platform, firmware and build config — but it is not a no-op, and using an
error response as a liveness probe would be worse than either.

**Keep-alives.** A host that must block on some external process holds the session
open by sending a no-op inside the **S3** window: `" "`, two bytes. The idle
timer resets on *any* received byte, so a keep-alive works even if it lands
mid-line.

**Why 0x0A is *not* a no-op alias.** (Also worth noting the code: 0x0A is LF; CR
is 0x0D.) Neither can be the no-op, and it is the same reason empty lines stay
silent above — this is that decision seen from the other side. A bare `
` or ``
does not *reach* dispatch as a command character: the line reader consumes it as a
**terminator**, so what dispatch would see is an empty line. Making an empty line
answer is precisely the thing that puts a CRLF host permanently one frame out of
step, because every `...
` command would produce its response *plus* a spurious
no-op frame.

The alternative — swallowing an `
` that immediately follows a `` — works for
back-to-back CRLF but then silently eats a legitimate standalone `
` keep-alive
that happens to follow a command, and needs a time bound to distinguish the cases.
That is a lot of fragility to save one byte.

`' '` has none of that trouble: it is a *printable* character, so `" "` is a
genuine non-empty line whose first character is the no-op opcode, dispatched by
exactly the same path as every other command. No special case anywhere.

Entry and exit still announce themselves so a host knows the mode switch landed;
those banners get sigils like everything else (**I5** obligation 1).

---

### D7 — Module name and file home *(resolved)*

**Status:** 🟢

**`automation_console.{c,h}`** in `App/Src` / `App/Inc`.

**Why not `test_harness` or `hil_repl`.** Both names are wrong in the same
direction, and my earlier `hil_repl` leaning was wrong for the same reason
`test_harness` was: this module is **not** a test harness — it is the interface
*for* one, and the harness lives on the host. Nor is it test-only. It is how an
external host controls the instrument, which includes real-world operation and
not merely platform self-test. A name carrying `test` or `hil` would mislead
every future reader about what the module is allowed to be used for, and would
invite exactly the scope confusion the reference implementation already suffers
from — `test_harness.c` there is 955 lines, most of it *human*-interactive
routines (key echo, line editor, field entry) that have nothing to do with the
machine interface; the executive is about 120 of them.

Only the executive is carried across. If SwitchTester later wants HuIL test
routines, they get their own file.

**Naming inside the module:** public functions take the full module prefix in
keeping with `v_switch_cycle_*` / `u32_uart_stream_*` —
`v_automation_console_run()`, `v_automation_console_service()`. Constants and
statics use the short form `ACON_` / `acon_` to stay readable —
`ACON_ENTER` (0xDA), `ACON_EXIT` (0xA5), `ACON_ENABLED`, `ACON_IDLE_TIMEOUT_MS`,
`b_acon_arg_u32()`. The sentinel *values* are unchanged (**D1**); only the
spelling of their names moves.

---

### S2 — Cooperative pumping and re-entrancy *(resolved)*

**Status:** 🟢 — **no code change required; verified against the call graph**

The console reads with bare `getchar()` and pumps `v_app_polling_task()`
directly. It never calls `v_debug_menu_service()` or `v_debug_delay()`.

**The existing re-entry lock already provides the guarantee.** Traced 2026-08-02:

```
app_main()                                   app_main.c:257
  v_app_polling_task()                       app_main.c:244
    KICK_WATCHDOG()
    v_debug_menu_service()                   debug_menu.c:777
      u8_reentry_lock = 1                    <-- set here, cleared only on return
      getchar() -> 0xDA
        v_automation_console_run()           <-- I1 intercept, inside the lock
          loop:
            v_app_polling_task()
              KICK_WATCHDOG()                <-- still kicked. good
              v_debug_menu_service()         <-- returns immediately, lock held
              v_process_next_job()           <-- still runs. wanted
            getchar()                        <-- console gets every byte
    v_process_next_job()
```

Because the console is entered from *inside* `v_debug_menu_service()`, the lock
is held for the console's entire lifetime, and the nested
`v_debug_menu_service()` reached through `v_app_polling_task()` returns at its
first statement. No recursion, no call stacking, no second reader.

**The lock is load-bearing for correctness, not tidiness — and that deserves a
comment at both ends.** Without it the nested service call would `getchar()`
bytes destined for the console *and dispatch them as menu keys*, executing
arbitrary menu commands in the middle of an automation session. It is not merely
that the menu would be noisy; it would be actuating the instrument.

**What still runs nested, deliberately:** `KICK_WATCHDOG()` (so a long session
cannot trip the watchdog) and `v_process_next_job()` (so jobs, the pulse timebase
and deferred commits keep working). The job runner continuing to run is wanted —
and it is also why **I7** exists, since jobs log.

**Also verified:** `v_debug_menu_service()` has exactly one caller outside its own
file (`app_main.c:246`), and `v_debug_delay()` has none. `i_getchar_blocking()`
and `i_getline()` in `utils.c` pump `v_app_polling_task()` too, so they are safe
by the same argument if the console ever uses them.

---

### S3 — Idle timeout *(resolved)*

**Status:** 🟢

**15 seconds**, as `ACON_IDLE_TIMEOUT_MS` in `debug_config.h` alongside the other
build switches. An anti-wedge measure: without it, a host that dies mid-session
leaves the board unreachable from a terminal.

**The timer resets on any received byte**, not on a completed command — so a
keep-alive works even if it arrives mid-line, and a host streaming a long command
cannot time out partway through sending it.

**It applies inside frame capture, not only at wait-for-command** (**S7**).
Otherwise a host that sends a lead character and then dies wedges the device in
the capture loop, and the anti-wedge guarantee has a hole exactly one byte wide.

**Exit is announced, never silent:** `!~,TMO`. The `!` sigil is doing real work
here — a timeout is an exit the host did not ask for, so it is reported as a
failure rather than as the clean `=~,BYE`.

**The session-frame rule, stated once:** *any* `~` frame after entry means the
session has ended. `=~,BYE` if the host caused it (`Q` or the exit sentinel),
`!~,TMO` if it did not. A host needs to watch for one opcode, and the sigil tells
it whether to be surprised.

**Keeping the timeout short is a deliberate trade.** The earlier leaning here was
60 s plus a host-settable value, on the reasoning that a soak campaign might
connect, start a run and return in an hour. That was solving the wrong problem:
**a soak run does not need the session held open.** Cycling continues after exit
(**S4**), so the host can start a run, leave, and reconnect later. What actually
needs the session held open is a host blocked on some external process — and that
host can send a no-op keep-alive (**D5**), which is 2 bytes every few seconds. A
short fixed timeout plus keep-alives is more robust than a long one, because the
board recovers from a dead host in 15 seconds rather than a minute, and it removes
a host-settable knob that could be set to something absurd.

---

### S5 — Transport error counters as a builtin

**Status:** 🟡 · **Needs user:** no

Inherited decision from the transport plan (**S5** there): `uart_stream` counts
ORE/FE/NE/PE per instance and the count becomes queryable, so a host can assert
it has not moved across a run. `u32_uart_stream_get_error_count()` already
exists.

**Leaning:** an `E` op that returns the console instance's count, plus a way to
zero it at the start of a run. Once the loopback rig (**T3** in the transport
plan) is up, the same op should be able to report *any* bound instance, not just
the console.

**Resolution:** _pending_

---

### I7 — Stray output during a session

**Status:** 🟡 · **Needs user:** no

**The problem, which S2 creates by design.** `v_process_next_job()` keeps running
nested inside the console loop — that is wanted — and **jobs log**.
`JOB_NVM_COMMIT` prints `NVM commit: status %d (...)`; `JOB_CYCLE_COMPLETE` and
anything else added later will print too. Those lines land in the middle of the
host's stream. **D3** anticipated exactly this with the `#` sigil, but nothing
yet *applies* it — `logging.c` calls `printf`/`vprintf` directly.

**Prefixing inside `logging.c` does not work.** There is no single choke point: a
log entry is assembled from several calls (`v_print_color()`, `v_print_timestamp()`,
then `vprintf`), and the variants differ in which prefix helpers they use —
`v_log_printf()` uses none at all. Hooking each would mean touching every variant
and would still miss a bare `printf` from anywhere else.

**The choke point that does work is `_write()`.** Everything printed converges
there, so a line-oriented filter applied at that one place catches log output,
stray `printf`, and anything a future job adds, without those call sites knowing
the console exists:

- While a session is active, `_write()` emits `#` at the start of the buffer and
  after every `
` it passes through.
- **The console's own frames bypass `_write()` entirely** — `v_acon_emit()` writes
  to `uart_stream` directly, so it carries its own sigils and is unaffected. This
  falls out of **I5** obligation 2 rather than being an extra rule.

The result is that *nothing* can desync the host, including output from code
written years from now by someone who never read this document.

**Known constraint:** a log format string containing an embedded `
` produces a
correctly-prefixed second line, so multi-line entries are fine — but a format
string that emits a *partial* line and returns (no trailing newline) will have the
next entry's `#` appear mid-line. Existing logging always terminates its entries,
so this is a note for future call sites rather than a present defect.

**Leaning:** filter in `_write()`, gated on a session-active flag; console frames
go direct to `uart_stream`.

**Resolution:** _pending_

---

### I8 — The frame emitter *(resolved)*

**Status:** 🟢

**Yes — `printf` is unavailable to the console, by construction.** **I7** routes
everything that goes through `_write()` into the `#` filter, so the console's own
frames must reach the wire another way or they would be prefixed as noise. That
means `vsnprintf()` into a buffer, then a multibyte `uart_stream` write. Rather
than open-code that at a dozen call sites, it is one varargs function.

```c
typedef enum
{
    ACON_SIG_OK      = '=',     /* command response, success   */
    ACON_SIG_ERR     = '!',     /* command response, failure   */
    ACON_SIG_PAYLOAD = '+',     /* payload continuation line   */
    ACON_SIG_EVENT   = '*'      /* async event -- phase 2      */
}
acon_sigil_t;

static void v_acon_emit(acon_sigil_t x_sigil, const char *p_c_format, ...);
```

Call sites then read:

```c
v_acon_emit(ACON_SIG_OK,  "S,L%X,M%X,R%X", u8_level, u8_mode, u8_run);
v_acon_emit(ACON_SIG_ERR, "P,BUSY,R%X", u8_run);
```

**Why the sigil is a separate typed argument rather than part of the format
string.** It makes **I5** obligation 1 — *every device→host line carries a sigil* —
a property of the function signature instead of a convention every call site has
to remember. A call site cannot omit it, cannot typo it, and cannot invent one.
That obligation is the single most expensive thing to retrofit in this whole
design, so it is worth making structurally impossible to violate.

**The emitter appends `
` itself.** Every frame is exactly one line, so no call
site should be able to forget the terminator — forgetting it would run two frames
together and desync the host in a way that looks like a protocol bug rather than a
missing `
`. It follows that **format strings never contain a newline**, which is
a clean invariant to state and to assert on in debug builds.

**Truncation must be detected, and must not be emitted.** `vsnprintf()` returns the
length it *would* have written; if that meets or exceeds the buffer, the formatted
frame is short. Emitting it anyway is the dangerous case, because a truncated frame
is still a *well-formed shorter frame* to the host — `=G,L9,M4,R4,N7A1` parses
cleanly and is wrong. That is precisely the failure mode **S1** exists to prevent,
so on truncation the emitter discards the partial line and sends
`!<op>,OVF` instead.

**Sizing.** The longest phase-1 frame is the getter with every field at its maximum
hex width — `=G,LF,MF,RF,NFFFFFFFF,FFFFFFFF,CFFFFFFFF,DFFFFFFFF` — 53 bytes with
the terminator. `128` leaves comfortable room for `L` payload help strings.

**Buffer is static, not stack** — same reasoning as **I4**: the console runs on an
already-nested main-loop stack. Safe as a single shared buffer because
`v_acon_emit()` is main-loop-only and never reentrant; it must **never** be called
from an ISR, which is the same rule **S6** enforces for events.

**No new plumbing needed.** `u16_uart_stream_tx_multi_blocking()` is the write
call, and the console handle is already reachable through the existing
`h_stdio_retarget_get_stream()` accessor — the console does not need its own
binding. Timeout mirrors `STDIO_TX_TIMEOUT_MS` (100 ms), which `stdio_retarget.c`
already sizes well above the ~11 ms it takes to drain a full 1024-byte ring at
921600 baud. In practice a 53-byte frame into a 1 kB ring never blocks at all.

**Keep the format specifiers integer and string only.** No `%f` anywhere — the
cycling design is already integer-microseconds with no floating point, and keeping
it that way avoids linking newlib's float formatter into an M0+ image.

---

### I1 — Hook point

**Status:** 🟡 · **Needs user:** no

`v_debug_menu_service()` currently does `getchar()` → `printf("Cmd [%s]")` →
`v_debug_menu_exec()`. The intercept goes between the read and the echo: if the
byte is `ACON_ENTER`, call the REPL and `continue`, so the sentinel is never
echoed and never reaches the menu dispatcher.

**Leaning:** intercept in `v_debug_menu_service()`, one `if` before the echo,
guarded by the **I3** build gate. Keeps the REPL out of `debug_menu.c` proper —
`debug_menu.c` gains an include and three lines.

**Resolution:** _pending_

---

### I2 — Op table and collision checking

**Status:** 🟡 · **Needs user:** no

Decided earlier: add registration-time collision checking — the reference has
none, so two ops claiming the same letter means the second is unreachable and
nothing says so.

**Options:** compile-time (`_Static_assert` over a table — awkward for a
duplicate-scan in C) versus a runtime scan at init that complains loudly on the
console. Table home: single static table in the REPL module (reference) versus
per-module registration.

**Leaning:** single static table in the REPL module — SwitchTester has one
subsystem worth driving today — plus a one-time O(n²) runtime scan at first entry
that reports duplicates and builtin-letter squatting (**D2**). n is a dozen; the
scan costs nothing and it runs on a test build.

**Resolution:** _pending_

---

### I3 — Build gate

**Status:** 🟡 · **Needs user:** no

Reference gates the whole module on `TEST_HARNESS_ENABLED`, defaulting to 1.

**Leaning:** same pattern, `ACON_ENABLED`, defaulting to 1, defined in
`debug_config.h` alongside the other debug switches rather than in the module
header — SwitchTester keeps its build switches in one place. Note that
SwitchTester *is* a bench instrument, so there is no real release image to strip;
the gate is for tidiness and for when this is promoted to the skeleton (**T3**).

**Resolution:** _pending_

---

### I4 — Line-buffer sizing

**Status:** 🟡 · **Needs user:** no

Reference carries an 8 KiB-ish static line buffer because its `P` op takes a long
hex-encoded PLAY string. SwitchTester's candidate ops (**D6**) are a letter and
up to four small integers.

**Leaning:** 96 bytes, static (not stack — the REPL runs on the main-loop stack
inside an already-nested call). Overflow characters are dropped and the line
still terminates on CR, but overflow must produce an `<ERR>` rather than
silently executing a truncated command — the reference drops silently, which is
exactly the failure mode **S1** exists to prevent.

**Resolution:** _pending_

---

### T1 — Host-side Python runner

**Status:** 🔴 · **Needs user:** no — but blocked on **D3** and **D6**

A small `pyserial` driver: open COM3 at the console baud, send `ACON_ENTER`,
wait for `=~,V1`, then a `command(cmd, *args) -> parsed response` method
that reads until the frame terminator. Lives in `scripts/` alongside the existing
build/flash scripts.

Cannot be written until the framing is chosen.

**Resolution:** _pending_

---

### T2 — Sync to the design doc

**Status:** 🔴 · **Needs user:** no

`Docs/SwitchTester-Design.md` § "HIL / script REPL" currently carries the
pre-decision sketch, including a stale paragraph about possibly needing an
interrupt-driven UART manager (`uart_stream` is done). Once the board is mostly
green, replace that section with the settled contract and link here.

**Resolution:** _pending_

---

### T3 — Promote to `G0B1_Skeleton`

**Status:** 🔵 — deferred

Same gate as the transport's **T1**: prove it here first. The REPL executive is
generic; only the op table is application-specific, which is the split that makes
it portable — and the reason **D7** wants the HuIL routines kept out.

**Resolution:** _deferred_

---

### D8 — Wire encoding *(resolved)*

**Status:** 🟢

**ASCII in both directions**, with hex for any raw binary-coded data. Frames are
printable lines, self-delimiting on CR — no escaping layer, no framing layer, and
no CRC strictly required because a truncated line fails to parse rather than
silently decoding to something plausible.

**Options considered:** binary records (length-prefixed or COBS-framed with a
CRC) would halve the bytes and remove formatting cost from the emit path, but
need a real framing layer — a length prefix resynchronises badly after a lost
byte, which is why **W1** names COBS specifically — and make the shared console
unreadable to a human, on the very channel that also serves the debug menu.

**Rationale.** The throughput argument that would have favoured binary evaporated
once the rate profile was stated: a typical full cycle is ~1 second, so events
arrive at ~2/second and frame size is irrelevant. What remains is the ability to
watch an entire live host session — commands, responses and later events — in a
terminal window, which is worth a great deal while bringing up a protocol that
has to be trusted before it can be used to trust anything else.

**Left open deliberately:** oscilloscope-style analog capture on the sense inputs
would move real volumes of data and may justify revisiting this. **D3**'s frame
sigil means a binary frame kind can be added alongside the ASCII ones without
disturbing them — that is **W1**, and it is a bridge to cross when the sense
design exists, not now.

---

### Q1 — What the host runner must do *(resolved)*

**Status:** 🟢

The channel carries **two transaction kinds**, and the protocol is designed for
both from v1:

1. **Command / response.** Host sends one command, device answers with one
   bounded response frame. Responses follow a fixed, easily parseable format.
2. **Async events.** Device-originated transmissions the host did not request —
   a cycled output changing state, a sense comparator transitioning, captured ADC
   data, and any other system event the host wants to know about.

**Rules locked with it:**

- An async transmission is **never inserted into a response frame**. It occurs
  only between the end of one response frame and the start of the next.
- Both kinds carry a **lead disambiguation header**, with different identifiers,
  so the host always knows which it is reading (**D3**).
- Async events are **timestamped** (**S8**).
- Events arising during a command/response exchange are **queued** and flushed
  when the REPL returns to wait-for-command (**S6**, **S7**).

**Rationale:** the earlier leaning — build a step sequencer and defer async push
to v2 — was wrong for this instrument. A switch tester whose host cannot see
*when* an edge happened is measuring nothing; and unsolicited output cannot be
retrofitted into a strict request/response protocol without invalidating every
host parser written against v1. Designing the frame identity for it now costs one
sigil.

**Consequence:** wish row **W2** is promoted into v1 as **S6**–**S9**.

---

### D1 — Entry / exit protocol *(resolved)*

**Status:** 🟢

`ACON_ENTER` = 0xDA enters, printing `=~,V1`. `ACON_EXIT` = 0xA5 or
a `Q` line exits, printing `=~,BYE`. An idle timeout (**S3**) also exits,
printing `!~,TMO` first. Both sentinels have the MS bit set so they cannot
collide with a printable menu key and cannot be typed by accident from a
terminal; they are a bit-complement pair (0x5A | 0x80 and ~0x5A).

**Rationale:** carried from the reference implementation and confirmed when the
backdoor was first sketched. The alternative — a printable escape sequence —
costs namespace and can be produced accidentally by a human at a terminal.

---

### S1 — Reject on error, structured error response *(resolved)*

**Status:** 🟢

Inherited from the transport plan's **S5**. A command whose reception was
compromised is never partially executed: the parser fails it and the host gets an
explicit, machine-readable error rather than silence or a plausible-looking wrong
result. The exact error envelope is **D3**'s to specify.

**Rationale:** on a test interface, a silently mis-parsed command can invalidate
a whole run with nobody noticing.

---

## Global notes

- **Every ID here is drive-side or protocol.** Sense-channel ops are **W3** and
  stay off this board until the sense design exists.
- **The cycler is unverified on hardware.** If REPL bring-up and cycler
  bench-testing happen in the same session, a failure is ambiguous — prefer
  proving the cycler by hand at the menu first.
- **Phase 1 — command/response** — **unblocked; every gating row is green:**
  1. `automation_console.{c,h}` — executive, line reader, builtins, and
     `v_acon_emit()` (**I8**), honouring all four **I5** obligations from the
     first commit
  2. `debug_menu.c` intercept (**I1**) + `debug_config.h` gate (**I3**) +
     `_write()` sigil filter (**I7**)
  3. Op table + collision scan (**I2**), the seven **D6** commands in order
  4. Shared state-bitmap helpers (**I6**) + cycle-period floor (**S10**) at the
     REPL parameter setter only
  5. Bench: enter, `V`, `L`, one op of each shape, exit by all three routes
  6. `scripts/` host runner (**T1**) — sigil dispatch from day one (**S11**)
  7. Design-doc sync (**T2**)

- **Phase 2 — async events:**
  8. Event ring + REPL-mode enqueue gate + one-per-iteration drain (**S6**,
     **S7**, **S8**); the cycling ISR is the first producer
  9. Fine-grained subscription mask (**S9**) — only if a campaign asks for it
  10. Bench: events on a slow cycle, force an overflow deliberately and confirm
      `*O`, confirm no event ever lands inside a response frame, and confirm
      the runner survives an event arriving in the command-commit window (**S11**)

- **One producer shape, forever.** Every async source added later (sense EXTI,
  ADC completion) enqueues the same 8-byte record and inherits framing,
  timestamping and overflow accounting for free. Phase 2 is where that machinery
  gets built; **I5** is what keeps phase 1 from making it a refactor.

- **Plan status:** 🟢 20 · 🟡 11 · 🔴 2 · 🔵 4 (37 rows) + 5 wish rows (one
  promoted). **Every design row is settled.** The two remaining reds are
  **T1**/**T2**, which follow the code rather than precede it; the twelve yellows
  all carry leanings and read as implementation guidance, not open questions.

**End of automation-console-plan.md**
