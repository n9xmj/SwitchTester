# HIL / script REPL — decision log

A resident, machine-facing command REPL entered from the debug-menu shell by a
sentinel byte, giving a host-side script deterministic control of the tester over
the **same** USART2 the human menu uses. Replaces "send ESC three times, send a
menu key, read until the timer runs out" with framed request/response, so the
host reads until a terminator rather than guessing on a clock.

- **Code home:** `App/Src/` + `App/Inc/` (module name — see **D7**)
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
on `HARNESS_ENTER` (0xDA), left on `HARNESS_EXIT` (0xA5), `Q`, or an idle
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

v1 op scope is the drive side only: force/pulse/toggle the four switch outputs,
start/stop/configure cycling, read back state, and assert on the transport error
counters. Sense-channel *ops* come later, when the sense design exists — but the
async *machinery* is v1, because retrofitting unsolicited output into a strict
request/response protocol forces a v2 of every host parser already written.

---

## The Big Board

| ID | Status | Subject (one line) |
|----|--------|--------------------|
| **Q1** | 🟢 | **What the host runner must do** — command/response *and* async events, both v1 |
| **D1** | 🟢 | Entry / exit protocol — 0xDA in, 0xA5 / `Q` / idle timeout out |
| **D2** | 🟡 | Command namespace — full `0x20..0x7E`, case-sensitive (drop `toupper()`) |
| **D3** | 🟡 | Frame identity — how the host tells a response from an async event |
| **D4** | 🟡 | Host→device grammar — freeform opcode+args vs fixed packet with length/CRC |
| **D5** | 🟡 | Prompt / echo policy while in harness mode |
| **D6** | 🔴 | v1 op set — which commands actually ship |
| **D7** | 🟡 | Module name and file home |
| **D8** | 🔴 | **Wire encoding — ASCII/hex lines vs binary records** |
| **S1** | 🟢 | Reject-on-error, structured machine-readable error response |
| **S2** | 🟡 | Cooperative pumping and re-entrancy vs `v_debug_menu_service()`'s lock |
| **S3** | 🟡 | Idle-timeout value and what state the tester is left in |
| **S4** | 🔴 | Behaviour while channels are cycling — entry, NVM commit deferral, exit |
| **S5** | 🟡 | Transport error counters as an assertable builtin |
| **S6** | 🟡 | Async event queue — ISR-safe fixed records, formatted at dequeue |
| **S7** | 🟡 | Deferral rule and queue-overflow policy (drop + dropped-count) |
| **S8** | 🟡 | Timestamp source and capture point — `TIM2->CNT`, sampled at event time |
| **S9** | 🔴 | Event subscription / arming — which events report, and when |
| **I1** | 🟡 | Hook point — where the 0xDA intercept lives |
| **I2** | 🟡 | Op-table home + registration-time collision checking |
| **I3** | 🟡 | Build gate for release images |
| **I4** | 🟡 | Line-buffer sizing and static (not stack) allocation |
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
- **Sentinels are `HARNESS_ENTER` 0xDA / `HARNESS_EXIT` 0xA5** — MS-bit set so
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

### D8 — Wire encoding

**Status:** 🔴 · **Needs user:** yes — the largest remaining fork

**Question:** Are frames ASCII (printable lines, hex for any binary payload), or
binary records (length-prefixed or COBS-framed, with a CRC)?

**Options considered:**

- **ASCII lines, hex payloads.** Self-delimiting on CR — no escaping problem, no
  framing layer, no CRC strictly needed because a truncated line fails to parse.
  A whole session is readable in Tera Term, including the async events, which
  matters a lot while debugging the REPL itself. Costs 2× on the wire for binary
  data and needs `printf`/`strtoul` on both ends.
- **Binary records.** Half the bytes, no formatting cost in the flush path, exact
  field widths. But it needs a real framing layer (a length prefix resynchronises
  badly after a lost byte; COBS does not, which is why **W1** names it), needs a
  CRC because nothing else detects truncation, and makes the shared console
  unreadable to a human — you would be debugging the protocol through a hex dump
  on a channel you also use as the menu.
- **ASCII framing, binary payload.** The worst of both: still needs escaping or
  hex for the payload, still unreadable, still needs a CRC.

**Throughput check, so this is decided on numbers.** At 921600 baud 8N1 the link
carries ~92 kB/s. A 32-byte ASCII event frame is ~350 µs. A channel cycling at a
1 ms half-period emits 2000 events/s → ~70 % of the link, which is already
uncomfortable; binary would halve it to ~35 %. But that is a pathological cycle
rate for a switch tester, and **S9**'s subscription model is the real answer to
event floods — not saving bytes on frames nobody asked for.

**Leaning / recommendation:** **ASCII lines with hex payloads** for v1. The one
case that genuinely justifies binary is bulk ADC capture, which is sense-side
work that does not exist yet; **D3**'s frame identifier leaves room to add a
binary frame kind later without disturbing the ASCII ones (**W1**). Being able to
watch a live host session in a terminal window is worth a lot during bring-up,
and this protocol has to be debugged before it can be trusted.

**Resolution:** _pending_

---

### D3 — Frame identity

**Status:** 🟡 · **Needs user:** no

**Question:** How does the host distinguish a command response from an async
event, and how does it know a frame is complete?

Locked by **Q1**: both frame kinds carry a lead identifier, async is never
inserted into a response frame, and a response is a bounded thing the host reads
to completion.

**Options considered:**

- **Per-op free-form** (reference behaviour) — no general completion signal, a
  bespoke parser per command. Rejected: it cannot express "this is an event, not
  your answer" at all.
- **Uniform single-line frames** — one line per frame, sigil in column 0. Trivial
  to parse and to resynchronise. Multi-line data must be split across frames or
  packed into key=value pairs.
- **Bracketed multi-line frames** — opening tag, payload lines, `<END>`. Handles
  a four-channel state dump naturally; the host always reads to the terminator.

**Leaning / recommendation:** bracketed frames with a **sigil as the first
character of every device→host line**, so a host that loses sync recovers at the
next line rather than the next frame:

| Sigil | Frame kind |
|-------|------------|
| `=` | command response — `=OK cmd=R` … payload … `=END` |
| `!` | command error — `!ERR cmd=R code=RANGE arg=2` (always single-line) |
| `*` | async event — `*EVT t=<µs> src=SW ch=A val=1` |
| `#` | human/log noise — anything not part of the protocol; host ignores the line |

The `#` row is the one that is easy to forget and expensive to omit: something
will eventually `printf` a stray line from a job or an error path while the REPL
is active, and without a "not for you" sigil that line desynchronises the host
mid-frame. Prefixing it makes stray output harmless instead of fatal.

**Resolution:** _pending_

---

### S9 — Event subscription / arming

**Status:** 🔴 · **Needs user:** yes

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

**Leaning / recommendation:** default **off**; a subscription mask op that takes
a class and a channel set; subscriptions cleared on REPL exit, so the queue is
never accumulating for an absent host and re-entry always starts from a known
state. The host arms what it wants immediately after `<HRN v1 RDY>`. Not
persisted to NVM — this is session state, not configuration.

**Resolution:** _pending_

---

### S6 — Async event queue

**Status:** 🟡 · **Needs user:** no

Events originate in ISR context — TIM2 compare for cycling transitions (priority
0), EXTI for sense edges later (priority 3). `printf` in an ISR is out of the
question: it is not reentrant, it would block on the TX ring, and it would run
formatting at priority 0.

So the queue holds **fixed-size binary records, not strings**, and formatting
happens in the main loop at flush time:

```c
typedef struct {
    uint32_t u32_timestamp;   /* TIM2->CNT at the event -- S8            */
    uint8_t  u8_class;        /* SW transition / sense edge / ADC / ...  */
    uint8_t  u8_channel;
    uint16_t u16_value;
} repl_event_t;               /* 8 bytes                                 */
```

An 8-byte record in a power-of-two ring is a single-producer/single-consumer
structure with the same discipline as `uart_stream`'s rings — producer (ISR)
writes `head`, consumer (main loop) writes `tail`, disjoint aligned `volatile`
indices, no critical section needed on M0+. Multiple ISR sources at *different*
priorities do break the single-producer assumption, though: TIM2 at priority 0
can preempt EXTI at priority 3 mid-enqueue. That needs either a short PRIMASK
guard around the head advance or all event sources at one priority — the guard is
a handful of cycles and does not constrain the priority map, so prefer it.

**Leaning:** 64 records (512 bytes), statically allocated, PRIMASK-guarded
enqueue, formatted at dequeue.

**Resolution:** _pending_

---

### S7 — Deferral rule and overflow policy

**Status:** 🟡 · **Needs user:** no

Locked by **Q1**: async frames are emitted only between the end of one response
frame and the start of the next — never inside one. So the flush point is the top
of the REPL's wait-for-command state, before the read blocks.

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
`*OVF n=<count>` frame at the head of the next flush whenever the counter is
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

### D6 — v1 op set

**Status:** 🔴 · **Needs user:** yes — follows **Q1**

**Question:** Which commands ship in v1?

**Candidate set** (drive side only; every one is a thin wrapper over an existing
`switch_out` entry point, so cost is parsing and framing, not new logic):

| Cmd | Meaning |
|-----|---------|
| `V` | version / ping — product, platform, firmware, build config |
| `L` / `?` | list ops |
| `Q` | quit |
| `S <ch> <0\|1>` | force one output off/on |
| `T <ch>` | toggle one output |
| `P <ch> <ms>` | pulse one output |
| `X` | all off, stop everything |
| `C <ch> start\|stop` | start / stop cycling on a channel |
| `W <ch> <on_us> <off_us> <repeat>` | write cycle parameters |
| `R <ch>` | read back channel state — drive level, pulse remaining, cycling, params |
| `E` | transport error counters (**S5**) |
| `N` | NVM commit / status |

**Leaning / recommendation:** ship the whole table. It is a couple of hundred
lines of argument parsing over an API that already exists, and a half-populated
command set is the thing a host script will immediately need to work around.

**Resolution:** _pending_

---

### S4 — Behaviour while channels are cycling

**Status:** 🔴 · **Needs user:** no (leaning is straightforward, but it needs to
be written down before it becomes accidental behaviour)

**Question:** Three interactions need a stated rule:

1. Can the host enter the REPL while channels are cycling? (Yes — a soak driver
   entering to query status is the main use case.)
2. `JOB_NVM_COMMIT` is already deferred while any channel cycles. A host that
   writes cycle parameters (`W`) and immediately power-cycles the board would
   lose them silently. Does `W` report "queued, not yet committed"?
3. On exit — sentinel, `Q`, or timeout — does the REPL stop cycling? An idle
   timeout firing mid-soak and killing the run would be a nasty surprise.

**Leaning / recommendation:** entry and exit are both non-disturbing — the REPL
never changes drive state on its own. `W` returns the commit state explicitly so
the host can poll for durability rather than assume it. Explicit `X` is the only
thing that stops a run.

**Resolution:** _pending_

---

### D2 — Command namespace and case sensitivity

**Status:** 🟡 · **Needs user:** no

Decided earlier: drop `toupper()` from harness dispatch, open the namespace to
the full printable range `0x20..0x7E`. The reference folds case in both the
builtin `switch` and the op-table scan, which halves an already small namespace.

**Open sub-point:** the builtins are `V` / `L` / `?` / `Q` uppercase. Once
dispatch is case-sensitive, `v` and `q` become *available* — and a host that
sends lowercase by habit gets `<ERR>` instead of a version string. Either accept
that (strict, and the error is loud) or reserve both cases of the four builtin
letters at registration time (**I2**) so nobody can claim them.

**Leaning:** strict case-sensitivity, and **I2**'s collision check reserves both
cases of the builtin letters. Loud failure beats a namespace booby trap.

**Resolution:** _pending_

---

### D4 — Host→device grammar

**Status:** 🟡 · **Needs user:** no

**Question:** Does the host→device direction get the same rigour as device→host —
a fixed packet with length, opcode, sub-opcode, validation and an EOT marker — or
a freeform opcode plus argument text?

**The asymmetry is the point.** The two directions have different consumers and
should not be assumed to need the same format:

- **Device→host is parsed by a machine** and is where a mis-parse silently
  corrupts a test result. That direction earns strict framing (**D3**).
- **Host→device is parsed by a 500-line C parser on an MCU**, and is the
  direction a human types by hand when debugging the REPL from Tera Term. A
  length-prefixed CRC'd packet makes that impossible without a tool, and buys
  little: a corrupted command is *already* rejected rather than partially
  executed (**S1**), and the transport counts its own ORE/FE/NE/PE (**S5**).

**Leaning:** freeform — first non-space character is the opcode, the remainder
(leading whitespace trimmed) goes to the op as a single `const char *`, CR or LF
terminates, empty lines ignored. Sub-opcodes, where a command needs one, are just
the first argument token (`C 1 start`) rather than a protocol-level field. Add a
shared `b_repl_arg_u32()` helper so channel parsing and range-checking is not
reimplemented — and mis-implemented — in six ops, and so **S1**'s error frames
can distinguish "missing" from "out of range".

If a command ever needs to carry bulk data, it carries it as hex in the argument
text (the reference does exactly this), which keeps the grammar unchanged.

**Resolution:** _pending_

---

### D5 — Prompt / echo policy

**Status:** 🟡 · **Needs user:** no

The menu echoes `Cmd [x]` for every keystroke and prints `{Ready}:`. Both are
noise a host must filter, and the echo is per-*character*, so a 30-character
command line produces 30 echo lines.

**Leaning:** harness mode echoes nothing and prints no prompt. The response frame
(**D3**) is the only output, which is exactly what makes the channel
deterministic. `<HRN v1 RDY>` on entry and `<HRN BYE>` on exit stay — a host
needs to know the mode switch landed.

**Resolution:** _pending_

---

### D7 — Module name and file home

**Status:** 🟡 · **Needs user:** no

The reference calls it `test_harness.{c,h}`, but that file is 955 lines and most
of it is *human*-interactive test routines (`_huil` key echo, line editor, field
entry) that have nothing to do with the machine REPL — the REPL executive is
about 120 lines of it.

**Leaning:** name it for what it is — `hil_repl.{c,h}` in `App/Src` / `App/Inc` —
and carry across only the executive. If SwitchTester later wants HuIL test
routines they get their own file. Naming it `test_harness` here would import the
reference's scope confusion along with its code.

**Resolution:** _pending_

---

### S2 — Cooperative pumping and re-entrancy

**Status:** 🟡 · **Needs user:** no

The REPL loop blocks — it owns the console until it exits. Two hazards:

- It must pump `v_app_polling_task()` every spin or the rest of the application
  stops (jobs, the pulse timebase, NVM commit).
- `v_debug_menu_service()` has a static re-entry lock and is re-entered from
  `v_debug_delay()`. If the REPL is called *from* the service loop and its own
  spin calls anything that reaches `v_debug_menu_service()`, the lock silently
  swallows input — or worse, the menu consumes bytes meant for the REPL.

**Leaning:** the REPL reads with bare `getchar()` and pumps `v_app_polling_task()`
directly, never `v_debug_menu_service()`. Since it is invoked from inside the
service loop, the re-entry lock is already held for its whole duration, which is
the correct guarantee — but that is load-bearing and belongs in a comment.

**Resolution:** _pending_

---

### S3 — Idle timeout

**Status:** 🟡 · **Needs user:** no

Reference uses 15 s, reset on any received byte, and auto-exits to the menu. This
is an anti-wedge measure: without it, a host that dies mid-session leaves the
board unreachable from a terminal.

**Open sub-point:** 15 s is short for a soak campaign where the host connects,
starts a run, and comes back in an hour. Either the host must keep-alive, or the
timeout needs to be much longer, or it needs to be settable by the host.

**Leaning:** default 60 s, and a host-settable value via an op argument — the
host knows its own cadence better than a compile-time constant does. Timeout
prints `<HRN TIMEOUT>` and exits without disturbing drive state (**S4**).

**Resolution:** _pending_

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

### I1 — Hook point

**Status:** 🟡 · **Needs user:** no

`v_debug_menu_service()` currently does `getchar()` → `printf("Cmd [%s]")` →
`v_debug_menu_exec()`. The intercept goes between the read and the echo: if the
byte is `HARNESS_ENTER`, call the REPL and `continue`, so the sentinel is never
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

**Leaning:** same pattern, `HIL_REPL_ENABLED`, defaulting to 1, defined in
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

A small `pyserial` driver: open COM3 at the console baud, send `HARNESS_ENTER`,
wait for `<HRN v1 RDY>`, then a `command(cmd, *args) -> parsed response` method
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

`HARNESS_ENTER` = 0xDA enters, printing `<HRN v1 RDY>`. `HARNESS_EXIT` = 0xA5 or
a `Q` line exits, printing `<HRN BYE>`. An idle timeout (**S3**) also exits,
printing `<HRN TIMEOUT>` first. Both sentinels have the MS bit set so they cannot
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
- **Implementation phase sketch** (once **D8**/**D6**/**S9** are green):
  1. `hil_repl.{c,h}` — executive, line reader, builtins, frame emit (**D3**)
  2. `debug_menu.c` intercept (**I1**) + `debug_config.h` gate (**I3**)
  3. Op table + collision scan (**I2**), ops in **D6** order
  4. Event ring + ISR enqueue + flush point (**S6**, **S7**, **S8**) — the
     cycling ISR is the first producer, which doubles as cycler verification
  5. Subscription mask (**S9**)
  6. Bench: enter, `V`, `L`, one op of each shape, arm events on a slow cycle,
     force an overflow deliberately and confirm `*OVF`, exit by all three routes
  7. `scripts/` host runner (**T1**)
  8. Design-doc sync (**T2**)

- **Two producers, one consumer, forever.** Every async source added later (sense
  EXTI, ADC completion) enqueues the same 8-byte record and inherits framing,
  timestamping and overflow accounting for free. That is the reason to build
  **S6** now rather than when the sense work arrives.

- **Plan status:** 🟢 3 · 🟡 15 · 🔴 6 · 🔵 1 (25 rows) + 5 wish rows (one
  promoted). **Next ID: D8** — the encoding fork; **D6** and **S9** follow it.

**End of hil-repl-plan.md**
