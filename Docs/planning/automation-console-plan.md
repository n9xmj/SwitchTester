# Automation console — decision log

A resident, machine-facing command console entered from the debug-menu shell by a
sentinel byte, giving an external host deterministic control of the instrument
over the **same** USART2 the human menu uses. Replaces "send ESC three times, send
a menu key, read until the timer runs out" with framed request/response.

It is **not** a test harness and not test-only (**D7**): it is the interface *for*
a host-side harness, and equally the way the instrument is driven in normal
operation.

- **Reference manual:** [`../acon-reference.md`](../acon-reference.md) — **this file is the
  *why*; that one is the *how*.** Every op's syntax and reply lives there, and it is the file
  to update when the command set or the protocol changes. Nothing here is a usage guide.
- **Code home:** `App/Src/automation_console.c` + `App/Inc/automation_console.h` (**D7**)
- **Transport:** [`uart-stream-integration-plan.md`](uart-stream-integration-plan.md) — **done and bench-verified**
- **Parent spec:** [`../SwitchTester-Design.md`](../SwitchTester-Design.md) § "Automation console, phase 1"
- **Code:** `App/{Inc,Src}/automation_console.*`, `App/{Inc,Src}/uart_stress.*`
- **Tests:** `scripts/hil/test_acon.py` — 47 tests, all passing
- **Reference implementation:** `C:\STM32\CubeSource\LED_Strip_Controller_G474\App\{Src,Inc}\test_harness.{c,h}` — *a* solution, not *the* solution
- **Status:** **PHASE 1 BUILT AND BENCH-VERIFIED (2026-08-03/04)**; phase 2 designed, not built
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
| **D2** | 🟢 | Command namespace — strict case, `0x01..0x7F`; MSB-set stays out-of-band |
| **D3** | 🟢 | Frame identity and grammar — sigils carry status; 1-char keys, hex values |
| **D4** | 🟢 | Host→device grammar — freeform, comma-separated, **no CRC or length** |
| **D5** | 🟢 | No prompt, no echo; everything answers; no-op is `Z` / `' '` / bare CR |
| **D6** | 🟢 | Phase-1 op set — six commands, mask-addressed where simultaneity matters |
| **D7** | 🟢 | Module name — `automation_console.{c,h}`, not a "test harness" |
| **D8** | 🟢 | Wire encoding — ASCII lines, hex for binary data, both directions |
| **D9** | 🟢 | Level-command encoding — Select + Set + Clear, BSRR-style, both = toggle |
| **D10** | 🟢 | Human mode — mode is an entry parameter; no in-band switch |
| **S1** | 🟢 | Reject-on-error, structured machine-readable error response |
| **S2** | 🟢 | Re-entry lock already covers it — verified, no code change needed |
| **S3** | 🟢 | 15 s `#define`, reset on any byte, announced exit `!~,TMO` |
| **S4** | 🟢 | Cycling: entry/exit non-disturbing, commits deferred, `P` forces one |
| **S5** | 🟢 | Transport error counters as an assertable builtin — the `E` op |
| **S6** | 🔵 | Async event queue — ISR-safe fixed records, formatted at dequeue *(phase 2)* |
| **S7** | 🔵 | Deferral rule and queue-overflow policy (drop + dropped-count) *(phase 2)* |
| **S8** | 🔵 | Timestamp source — `TIM2->CNT` tentative; wide-counter idea *(phase 2)* |
| **S9** | 🔵 | Event subscription / arming — which events report, and when *(phase 2)* |
| **S10** | 🟢 | Minimum cycle-period guard — 50 ms, REPL-commanded cycling only |
| **S11** | 🟢 | Host receive contract — dispatch by sigil, never by position |
| **S12** | 🔵 | Menu-mode human log — folded into phase-2 event-queue planning |
| **S13** | 🟢 | Switch-op responses carry state; ok/error lives in the frame header |
| **S14** | 🟢 | µs on the wire; no auto-persist — commit is the explicit `P` command |
| **S15** | 🟢 | Start/stop edge cases — restart from ON, stop stays LOW, exhaustion reported |
| **S16** | 🟢 | Repeat progress — report cycles *done*; host derives remaining |
| **I1** | 🟢 | Hook point — one `if` in `v_debug_menu_service()`, before the echo |
| **I2** | 🟢 | Op-table collision scan on entry — mode-appropriate conflict report |
| **I3** | 🟢 | **No build gate** — the console ships in every build configuration |
| **I4** | 🟢 | Input line buffer 256 B, `#define`-settable, static |
| **I5** | 🔵 | Async-readiness contract — absorbed into D3/I7/I8 *(phase 2)* |
| **I6** | 🟢 | State readback — three bitmaps: level (`IDR`), mode (`OCxM`), cycling-active |
| **I7** | 🟢 | stdout suppressed in SCRIPT mode; console I/O bypasses stdio entirely |
| **I8** | 🟢 | `v_acon_emit()` — the frame emitter; sigil is an argument, not a convention |
| **I9** | 🟢 | `i_getline()` gains a silent `^C` exit returning −2; no consumer edits |
| **T1** | 🔵 | Host-side Python runner — deferred until there is code to drive |
| **T2** | 🟢 | Design doc synced 2026-08-04 |
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
- **stdio is raw, unbuffered, and does no EOL translation** (verified
  2026-08-02). `v_stdio_retarget()` sets `_IONBF` on `stdout`, `stdin` *and*
  `stderr` (`stdio_retarget.c:72-74` — not `Core/`), and `_write()` hands the
  buffer straight to `u16_uart_stream_tx_multi_blocking()` with no `\n` → `\r\n`
  expansion. `_read()` is non-blocking and equally untranslated.
  Three consequences this design depends on: **(a)** the console's line reader
  sees `\r` and `\n` exactly as the host sent them, so the CRLF reasoning in
  **D5**/**S3** rests on real behaviour rather than an assumption; **(b)**
  `v_acon_emit()`'s explicit `\r\n` reaches the wire verbatim (**I8**); **(c)**
  unbuffered output means one log entry arrives as *several* `_write()` calls,
  which is what shapes **I7**.
- **Hand-drivability is a design goal, not a side effect (stated 2026-08-02).**
  The console is meant to be usable *directly from a terminal* — to try a command
  by hand before committing it to a script, and to diagnose the instrument with
  nothing but Tera Term. That is why every device→host byte stays in printable
  ASCII (**D8**, and **D3**'s caret notation), why a bare CR answers (**D5**), and
  why **D10** exists. A protocol decision that would make the stream unreadable to
  a human needs to justify itself against this, not only against machine
  parseability.
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
a `Q` line exits, printing `=~,BYE`; `^C` (0x03) is an alias for `Q`
and exits identically in either mode (**D10**). An idle timeout (**S3**) also exits,
printing `!~,TMO` first. Both sentinels have the MS bit set so they cannot
collide with a printable menu key and cannot be typed by accident from a
terminal; they are a bit-complement pair (0x5A | 0x80 and ~0x5A).

**Rationale:** carried from the reference implementation and confirmed when the
backdoor was first sketched. The alternative — a printable escape sequence —
costs namespace and can be produced accidentally by a human at a terminal.

---

### D2 — Command namespace and case sensitivity *(resolved)*

**Status:** 🟢

**Strict case sensitivity.** No `toupper()` anywhere in dispatch. The reference
implementation folds case in both the builtin `switch` and the op-table scan,
halving an already small namespace — this does not.

**The namespace is `0x01..0x7F`, not just the printable range.** There is no
reason to exclude `0x01..0x1F` merely because those codes are unprintable: an
opcode is a byte a machine sends, and a control character is as good a byte as any.

| Range | Status |
|---|---|
| `0x01..0x7F` | **available as opcodes**, minus the reservations below |
| `0x00` | reserved — out-of-band |
| `0x80..0xFF` | reserved — entry/exit sentinels (`ACON_ENTER` 0xDA, `ACON_EXIT` 0xA5) and any future out-of-band signalling |

**Reserved inside the usable range** (**I2** enforces all of these at
registration, so a domain op cannot claim one):

| Code | Why |
|---|---|
| `0x0A` LF | ignored everywhere by the line reader (**D4**) |
| `0x0D` CR | line terminator; a bare CR is the no-op (**D5**) |
| `0x20` space | no-op alias (**D5**) |
| `0x5E` `^` | caret-notation introducer for control-char echoes (**D3**) |
| `0x03` `^C` | quit alias / escape hatch (**D10**) |
| `V` `L` `?` `Q` `Z` | builtins — **both cases** reserved |
| `~` | protocol/session frames (**D3**) |

**Guidance, not restriction: prefer printable opcodes for ordinary commands.**
A printable opcode can be typed at a terminal while debugging; a control-character
one cannot — and its response comes back in caret notation (**D3**) rather than as
the byte itself, so the two directions no longer look alike in a log. The control
range is there for commands that *should* be hard to send by accident, not as the
default place to put the next feature.

**I2's collision check reserves both cases of every builtin letter**, so `v` and
`q` cannot be claimed by a domain op even though they are technically free. A host
that sends lowercase by habit then gets a clean `!v,UNK` rather than silently
hitting some unrelated command that happened to take the letter.

**Rationale:** loud failure beats a namespace booby trap. Case folding makes
`s` and `S` the same command forever, which is a permanent halving of the
namespace to buy tolerance for a typo that a machine does not make.

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

```
=<op>[,<tok>]...                 success, complete in one line
!<op>,<CODE>[,<tok>]...          failure, complete in one line
=<op>,K<n>[,<tok>]...            success, exactly <n> payload lines follow
+<text>                          payload line -- free text, NOT tokenised
*<src><ch>,T<hex>,V<hex>         async event (phase 2)
#<text>                          not protocol; host ignores the line
```

`<op>` is the command character, echoed back so a desynchronised host notices
immediately. A `<tok>` is **one uppercase key letter immediately followed
by a hex value** — no `=`, no `0x`, no separator inside the token. **Tokens are
comma-separated**, so a host splits on `,` and reads each field as
`key = tok[0], value = int(tok[1:], 16)`. The comma is exact where a space is
not: nothing accidentally emits a double comma, and no field can be lost to
whitespace trimming.

**Control-character opcodes are echoed in caret notation**, so that a response
frame is *always* printable ASCII end to end:

| Opcode | Echoed as | |
|---|---|---|
| `0x01..0x1F` | `^` + (opcode + `0x40`) | → `0x41..0x5F`, i.e. `^A`..`^_` |
| `0x7F` | `^?` | the conventional spelling; `0x7F + 0x40` is not printable |
| `0x20..0x7E` | itself | unchanged |

So a command sent as byte `0x03` answers `=^C,...`. **`^` (0x5E) is therefore
reserved** (**D2**, **I2**) — a domain op may not claim it, which is simpler than
escaping it as `^^`.

**This applies to the response direction only.** The host still sends the raw
control byte; it writes bytes deliberately and has no parsing problem. Making the
command direction accept `^C` as two characters would mean the reader could not
tell an opcode from a caret-escaped one without lookahead, for no gain.

**Why, precisely — because the obvious reason is not the real one.** A raw control
byte in a response cannot in fact break the host's end-of-line detection: CR and LF
are reserved out of the opcode space entirely (**D2**), CR being the terminator and
LF discarded, so neither can ever reach the echo field. The reasons that do hold:

- **ESC (`0x1B`) echoed to a terminal starts an ANSI escape sequence.** `=`, ESC,
  `,`, `L9`… is not merely unreadable — the terminal *consumes* following
  characters as an escape sequence, corrupting the display of a session that
  **D8** chose ASCII specifically to keep watchable. `0x07` BEL, `0x08` BS and
  `0x0C` FF misbehave similarly.
- **It makes "responses are printable" a structural invariant** rather than a
  consequence of today's reservation list. A future change to what is reserved
  cannot silently put a raw control byte on the wire.
- **Caret notation is self-documenting** — `^C` is instantly recognisable, where a
  bare `0x03` in a log is a mystery.

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

```
=S,L9,M4,R4                          set levels -> ok
!W,RNG,L9,M4,R4                      write params -> rejected, out of range
=G,L9,M4,R4,N7A120,F7A120,C0,D4D2    get params for a channel
=L,K8                                op list, 8 payload lines follow
+V ping / version
```

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
(**D3**). First character is the opcode; the remainder splits on `,`. One splitter
on each side of the link and one rule to remember, rather than "commas that way,
spaces this way".

**Line termination: CR terminates, LF is ignored — everywhere, unconditionally.**
Not "CR or LF", and not "CR, with LF swallowed if it follows one". `0x0A` is
consumed and discarded wherever it appears, so it can never terminate a line,
never become an opcode, and never produce a frame.

That single rule is what makes a **bare CR a response-generating no-op** (**D5**)
without reintroducing the CRLF hazard:

| Host sends | Result |
|---|---|
| `S,3,1,2\r\n` | `\r` terminates a non-empty line → command runs; `\n` discarded. **One** frame. |
| `\r` | empty line → no-op → `=Z` |
| `\r\n\r\n` | two no-ops — what a human pressing Enter twice expects |
| `\n\r` | `\n` discarded, `\r` terminates → identical to bare CR |

**The cost, stated plainly:** a host that sends LF-only line endings never
terminates a line, and its session ends on the **S3** idle timeout. That is a
discoverable failure rather than a silent corruption, it is documented, and the
host runner (**T1**) is ours and sends CR. Terminals send CR. The trade buys an
unambiguous reader with no lookahead, no timing window and no special case.

```
S,3,1,2          select=3, set=1, clear=2
W,1,7A120,7A120,0   channel 1, on 500000 µs, off 500000 µs, repeat 0 (infinite)
G,1              get channel 1 parameters
```

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

**Every command line produces exactly one response frame** — recognised or not,
empty or not. An unknown opcode returns `!<op>,UNK`. There is no silent path at
all, so a host that gets nothing back knows the link or the device is at fault,
never the protocol.

**Empty lines answer too**, and that is only safe because of **D4**'s termination
rule: LF is discarded unconditionally, so a CRLF host's trailing `\n` never
reaches dispatch and cannot produce the spurious second frame that would otherwise
put it permanently one frame out of step. Get that rule wrong and
empty-lines-answer becomes a desynchronisation bug; with it, the exception
disappears entirely.

**The no-op is `Z`, with `' '` (0x20) and a bare CR as aliases.** It takes no
arguments, touches nothing, and returns `=Z` — *all three* spellings normalise to
that one response, so the host never handles a frame whose opcode field is a space
or a control character, which whitespace trimming anywhere in the chain could
silently eat. **I2** reserves all three.

**Bare CR is the one that matters operationally, and the reason is human.**
Checking a console's responsiveness by tapping Enter or space is ingrained muscle
memory — it is why the debug menus bind `.key = '\r'` to reprint help. If the
device is somehow stuck in the automation console, tapping Enter returns `=Z`
instead of the expected menu help, and the state is diagnosed in one keystroke
with no host, no script and no guessing. The symmetry is deliberate: **CR always
produces a response in either mode, and *which* response tells you which mode you
are in.**

Four uses:

- *"Are you there"* — the minimal liveness check; a bare CR is one byte out.
- **Mode diagnosis by hand**, as above.
- **Resynchronisation.** A host that suspects a partial line is sitting in the
  device's buffer sends CR: that terminates and discards whatever was accumulating
  and returns a known frame, in one byte.
- **Latency measurement**, with no work in the path to confound it.

`V` remains the identity ping and is the better call at session start — it pins
product, platform, firmware and build config — but it is not a no-op, and using an
error response as a liveness probe would be worse than either.

**Keep-alives.** A host that must block on some external process holds the session
open by sending a no-op inside the **S3** window — a bare CR is one byte. The idle
timer resets on *any* received byte, so a keep-alive works even if it lands
mid-line.

**LF (0x0A) is still not an alias, and no longer needs to be.** It is discarded
unconditionally (**D4**), which is precisely what lets CR *be* one. An earlier
version of this row kept empty lines silent because both CR and LF terminated,
which made a CRLF host's trailing LF indistinguishable from a deliberate blank
line. Demoting LF from "terminator" to "ignored" removes that ambiguity at its
source: the constraint was in the reader, not in the protocol, and fixing the
reader turned a hazard into a feature.

(Codes, since the reasoning turns on them: CR is 0x0D, LF is 0x0A.)

Entry and exit still announce themselves so a host knows the mode switch landed;
those banners get sigils like everything else (**I5** obligation 1).

---

### D6 — Phase-1 op set *(resolved)*

**Status:** 🟢

Eight commands, plus the executive builtins. Deliberately a starting set; more
are expected to follow. Command 8 arrived with **S5**, after the original seven.

| # | Command | Inputs | Returns |
|---|---------|--------|---------|
| 1 | Set switch output levels (manual mode) | `Select`, `Set`, `Clear` masks (**D9**) | level + mode + run bitmaps (**I6**) |
| 2 | Read switch state | none | level + mode + run bitmaps (**I6**) |
| 3 | Set cycling parameters | switch # (0–3), on-time, off-time, repeat count | level + mode + run bitmaps (**S13**) |
| 4 | Start auto-cycling | start bitmask | level + mode + run bitmaps (**S13**) |
| 5 | Stop auto-cycling | stop bitmask | level + mode + run bitmaps (**S13**) |
| 6 | **Get** cycling parameters | switch # (0–3) | on-time, off-time, repeat count, cycles **done** (**S16**) + the three bitmaps |
| 7 | Commit parameters to NVM now | none | written / no-change, or refused while cycling (**S4**) |
| 8 | Transport error counters | none, or a reset flag | ORE/FE/NE/PE count (**S5**) |

**Opcode assignment** — fixed here so collisions are designed out rather than
caught by **I2**'s registration scan at startup:

| Op | Command | | Op | Builtin |
|:--:|---------|-|:--:|---------|
| `S` | set switch levels (**D9**) | | `V` | version / identity ping |
| `R` | read switch state | | `L` `?` | list ops |
| `W` | write cycling parameters | | `Z` `' '` CR | no-op (**D5**) |
| `^C` | quit — alias of `Q` (**D10**) |
| `G` | get cycling parameters | | `Q` | quit |
| `C` | start cycling | | `~` | *reserved* — session frames (**D3**) |
| `X` | stop cycling | | | |
| `P` | persist to NVM (**S4**) | | | |
| `E` | error counters (**S5**) | | | |

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

**Deferred from the earlier candidate list:** pulse, toggle and all-off. All remain sensible additions;
none is needed to run a first campaign, and several are expressible with the five
above (all-off is a level command with every channel selected).

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

### D10 — Human mode *(resolved)*

**Status:** 🟢

**The mode is chosen at entry, by the caller**, and switched in-band with `^X`
(0x18, CAN). Two readers, one dispatcher:

| | **SCRIPT** (default) | **HUMAN** |
|---|---|---|
| Reader | raw, byte-at-a-time | `i_getline()` |
| Echo | none | per character |
| Editing | none | backspace, `Ctrl-X` clear, `ESC` cancel |
| **S3** idle timeout | applies | does not (see below) |
| **I7** `#` filter | on | off |
| Dispatcher | *identical* | *identical* |
| Response frames | *identical* | *identical* |

**The dispatcher and `v_acon_emit()` are shared, and that is the point.** A
command tried by hand must behave exactly as it will from a script, or the mode
is a liar. Only the *reader* differs.

**Entry mode is a parameter, because the entry path already encodes the intent:**

```c
void v_automation_console_run(acon_mode_t x_mode);
```

| Called from | Mode | Why |
|---|---|---|
| `v_debug_menu_service()` on the `ACON_ENTER` sentinel (**I1**) | `ACON_MODE_SCRIPT` | only a machine sends a non-typeable 0xDA |
| a debug-menu entry | `ACON_MODE_HUMAN` | only a person picks a menu key |

This is better than a flag defaulted at entry, and it subsumes the earlier
"default off, reset on entry" rule rather than merely satisfying it: there is no
persistent mode state to leak between sessions, because the mode *is* the
argument. The common cases also need no mode command at all — a script never asks
for script mode, and a human never asks for human mode.

It also gives hand-driving a **discoverable** entry point. Sending 0xDA from a
terminal is awkward; picking a menu key is not.

**There is no in-band mode switch at all.** Mode comes from the entry argument and
nothing changes it for the life of the session. `^E` and `^X` are both dropped;
0x05 and 0x18 go back to the free pool. What a stuck operator actually needs is
not a way to change mode — it is a way *out*, and that is `^C` (below).

**`^C` (0x03) is the escape hatch, and it is an alias for quit — not a mode
switch.** It is a **dispatcher-level opcode**, which matters: the most likely way
to find the board "stuck" in the console is a *script* session whose host died, so
the hatch has to work in SCRIPT mode, not only in the human reader. One quit
implementation, two delivery paths:

| Mode | How `^C` arrives | Result |
|---|---|---|
| SCRIPT | raw reader reads byte 0x03, dispatches it | `=~,BYE`, exit |
| HUMAN | `i_getline()` returns −2 (**I9**), console synthesises the same quit | `=~,BYE`, exit |

**Why this is bulletproof rather than merely adequate:** the idle timeout and `^C`
cover exactly each other's gaps. **S3**'s timeout applies in SCRIPT mode, where
there is a host but possibly no operator — a dead host releases the console in
15 s unattended. `^C` serves HUMAN mode, where there is no timeout but there *is*
an operator sitting at the terminal. Neither mode is left without a recovery path,
and neither needs the other's.

**And "stuck in automation mode" is not a hang.** The console pumps
`v_app_polling_task()` every spin (**S2**), so jobs, cycling, the pulse timebase
and the watchdog all keep running the entire time. The board is responsive and
doing its work; only the console's input interpretation differs. Combined with
**D5** — tap Enter, get `=Z`, and you know exactly where you are — diagnosis and
recovery are two keystrokes with no equipment.

**`i_getline()` is the right reader, and the pumping concern does not apply.** It
blocks through `i_getchar_blocking()`, which loops `v_app_polling_task()` +
`getchar()` — the *same* call the console loop already makes, reached through the
same held re-entry lock (**S2**). No new hazard: the lock is what makes the pump
safe, and it is held for the console's entire lifetime regardless of which reader
is running.

What it brings for free, already written and already familiar from the debug menu:
per-character echo, destructive backspace (`\b \b`), `ESC` to cancel a line with a
`<Cancel>` notice, `Ctrl-X` to clear and re-enter, and length limiting. That
settles the previous open sub-point on this row — **backspace comes with the
reader** rather than needing its own implementation.

**Three real limitations, recorded rather than discovered:**

1. **`i_getline()` ignores every byte below 0x20** (`else if (i_key >= 0x20)`), so
   a control-character opcode cannot be typed in human mode. **Accepted, not
   worked around:** the control range is semi-reserved for operations only an
   automation interface needs to drive (**D2**), so a human has no reason to send
   one. Both candidate switch keys are unaffected — `i_getline()` tests for `^X`
   and ESC *before* the printable check, so whichever **I9** picks stays
   reachable.
2. **No idle timeout.** `i_getchar_blocking()` has no deadline, so **S3** cannot
   fire while a line is being entered. This is acceptable *because the rationale
   does not transfer*: **S3** exists so a dead **host** cannot wedge the board, and
   human mode has no host — it has an operator sitting at the terminal. It also
   matches how every other line entry in this application already behaves. Worth
   naming as the reason a script must never enable human mode: it would silently
   forfeit its anti-wedge guarantee.
3. **`i_getline()` handles `\b` (0x08) but not `0x7F` (DEL)**, and terminals differ
   on which one Backspace sends. Tera Term is configurable. A one-line widening
   would fix it for the menu too, but it edits a shared utility, so it is noted
   here rather than folded in silently.

**Caret notation on *input* is no longer needed** and is dropped. It existed to
solve limitation 1 — a trap that only existed while `^E` was the switch and was
itself filtered. With `^X` handled ahead of the printable check, the switch is
reachable, and the control range is script-only by design rather than by accident.
Should hand-typed control opcodes ever be wanted, the reasoning is preserved here:
human mode has the whole line before dispatch, so resolving a leading `^X` needs no
lookahead, and **D3**'s objection to caret-on-input applies only to script mode's
byte-at-a-time reader.

**There is no persistent mode state to leak between sessions** — the mode is set
from the entry argument every time, so **S6**'s reset-at-entry discipline is
satisfied structurally rather than by remembering to clear a flag.

**The switch is a plain toggle, with no explicit-set form.** A toggle would
normally be awkward for a script — it has to know the current state — but no
script needs one here: it *enters* in `ACON_MODE_SCRIPT` by construction and has
no reason to leave it. The switch exists for the operator taking over a session or
stepping down to watch raw bytes, and a person can see which mode they are in.

The response reports the mode arrived at, so it is never ambiguous — `M0` for
SCRIPT, `M1` for HUMAN.

**Echoed control characters use caret notation** (**D3**), for exactly the reason
that rule exists: echoing a raw ESC back to the terminal would start an ANSI
escape sequence and corrupt the display. In practice `i_getline()` discards them
before echo anyway (limitation 1), so this matters mainly if that filter is ever
widened.

**Human mode forfeits machine-parseability, and that is the honest trade.**
Echo is per-character, so it cannot be sigil-framed — there is no line to frame.
Prefixing every echoed character with `#` would technically preserve the invariant
and would be intolerable to type against. So: **echo output is not part of the
protocol**, and a session with echo on is a human session by definition. That is
the whole point of the command, but it needs stating rather than discovering.

**Not included:** a prompt. **D5** rules one out, and human mode does not change
that argument — `i_getline()` does not print one either, so both modes stay
consistent with each other and with the row above. If hand-driving turns out to
want one, it belongs on this row later.

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

### S5 — Transport error counters as a builtin *(resolved)*

**Status:** 🟢

Inherited decision from the transport plan (**S5** there): `uart_stream` counts
ORE/FE/NE/PE per instance and the count becomes queryable, so a host can assert
it has not moved across a run. `u32_uart_stream_get_error_count()` already
exists.

**Resolved:** an `E` op that returns the console instance's count, plus a way to
zero it at the start of a run. Once the loopback rig (**T3** in the transport
plan) is up, the same op should be able to report *any* bound instance, not just
the console.

---

### S6 — Async event queue

**Status:** 🔵 — phase 2, design below is the standing plan · **Needs user:** no

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

**Resolution:** _deferred to phase 2_

---

### S7 — Deferral rule and overflow policy

**Status:** 🔵 — phase 2, design below is the standing plan · **Needs user:** no

Locked by **Q1**: async frames are emitted only between the end of one response
frame and the start of the next — never inside one.

**Drain structure.** The executive loop makes the deferral rule structural rather
than something that has to be remembered at each emit site:

```c
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
```

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

**Resolution:** _deferred to phase 2_

---

### S8 — Timestamp source and capture point *(deferred)*

**Status:** 🔵 — phase 2; direction below is tentative

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

**Tentative direction:** `TIM2->CNT` (or the firing `CCR`) as the source, captured
in the ISR at enqueue. Locked far enough to build phase 1 against; the width
question below is phase-2 work.

**Open for phase 2 — widening past 32 bits.** A TIM2 update (overflow) interrupt
incrementing a high word would give a 40-bit or 64-bit stamp. Notes for when this
is picked up:

- **The cost is nothing.** One interrupt every 71.6 minutes. Keep the high word a
  plain `uint32_t` incremented in the ISR and compose to `uint64_t` at *format*
  time, so no 64-bit arithmetic ever runs in interrupt context.
- **The read is not atomic, and this is the classic bug.** Reading `high` then
  `CNT` can straddle a wrap and yield a stamp wrong by 2³² µs — 71 minutes, in the
  direction that looks plausible. The standard fix is a double read: read `high`,
  read `CNT`, read `high` again, and retry if it moved. Inside the TIM2 ISR itself
  the hazard is absent, since update and compare flags are serviced in one handler
  invocation; it bites for *other* ISRs, which is exactly where sense-edge
  timestamps (EXTI, priority 3) will be taken.
- **Check whether it is needed first.** A 32-bit stamp plus host-side unwrapping
  already gives unlimited range while consecutive events are under 71 minutes
  apart — trivially true at the intended rates. The case it does *not* cover is a
  host attaching mid-campaign, which cannot know how many wraps have passed. That
  may be answerable far more cheaply by having a status response report the
  device's current wide tick once, letting the host anchor, rather than widening
  every event frame.
- Wire cost is not an objection either way: 16 hex characters against 8, at a
  couple of events per second.

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

**Resolution:** _deferred to phase 2_

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

### S11 — Host receive contract *(resolved)*

**Status:** 🟢

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

**Resolved:** sigil dispatch, stated as a protocol contract
rather than left as an implementation detail of the runner — *the host must be
prepared for an async frame at any point at which it is reading device output.*
This is already **I5** obligation 1 seen from the host side, and it is why that
obligation applies to the phase-1 runner even though phase 1 emits no events: a
runner written to the contract needs no change when phase 2 lands, and one
written to positional reads has to be rewritten.

Note that the **S6** REPL-mode enqueue gate bounds this further — outside REPL
mode no event exists to race with anything.

---

### S12 — Menu-mode human log *(deferred)*

**Status:** 🔵 — phase 2, with the event-queue design

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

---

### S15 — Start/stop edge cases *(resolved)*

**Status:** 🟢

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

**Resolved: all three leanings above are adopted as written.** Flagged as
provisional by the decision — how the tester actually behaves in use may argue
for changing the stop level or the already-cycling case, and this row is where
that would be reopened.

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

### I1 — Hook point *(resolved)*

**Status:** 🟢

`v_debug_menu_service()` currently does `getchar()` → `printf("Cmd [%s]")` →
`v_debug_menu_exec()`. The intercept goes between the read and the echo: if the
byte is `ACON_ENTER`, call the REPL and `continue`, so the sentinel is never
echoed and never reaches the menu dispatcher.

**Resolved:** intercept in `v_debug_menu_service()`, one `if` before the echo,
guarded by the **I3** build gate. Keeps the REPL out of `debug_menu.c` proper —
`debug_menu.c` gains an include and three lines.

---

### I2 — Op table and collision checking *(resolved)*

**Status:** 🟢

Decided earlier: add registration-time collision checking — the reference has
none, so two ops claiming the same letter means the second is unreachable and
nothing says so.

**Options:** compile-time (`_Static_assert` over a table — awkward for a
duplicate-scan in C) versus a runtime scan at init that complains loudly on the
console. Table home: single static table in the REPL module (reference) versus
per-module registration.

**Resolved:** single static table in the module — SwitchTester has one
subsystem worth driving today — plus a one-time O(n²) runtime scan at first entry
that reports duplicates and builtin-letter squatting (**D2**). n is a dozen; the
scan costs nothing and it runs on a test build.

**The scan runs once on every console entry**, not at boot and not per
command. The table is `static const`, so the result cannot change within a run;
entry is simply the moment where reporting it is useful. Cost is n² byte
comparisons with n around a dozen — a few hundred cycles, once, on a bench tool.
No state to keep, no once-per-boot flag to get wrong.

**The conflict report is mode-appropriate** (**D10**), which is the first place
outside echo where the two modes diverge in output:

| Mode | Report |
|---|---|
| SCRIPT | a standard error frame, `!~,DUP,...`, so a host can fail the run
  immediately rather than discovering a shadowed op by its symptoms |
| HUMAN | plain readable text, in the style `menusystem` already uses for the
  same class of problem |

This mirrors how key-code duplication is already handled in `menusystem`, so the
console is not inventing a second convention for the same failure.

---

### I3 — Build gate *(resolved)*

**Status:** 🟢

Reference gates the whole module on `TEST_HARNESS_ENABLED`, defaulting to 1.

**Superseded leaning:** same pattern, `ACON_ENABLED`, defaulting to 1, in
`debug_config.h` alongside the other debug switches rather than in the module
header — SwitchTester keeps its build switches in one place. Note that
SwitchTester *is* a bench instrument, so there is no real release image to strip;
the gate is for tidiness and for when this is promoted to the skeleton (**T3**).

**Resolved: there is no build gate.** The automation console is compiled into
**every** build configuration; `ACON_ENABLED` is dropped before it exists.

The reference project gates its harness because it ships a product image. This
does not ship anything — SwitchTester *is* the bench instrument, so there is no
release build to strip and nothing the gate would protect. What a gate would
reliably do is make the console absent from precisely the build someone reaches
for when they need it, which is a worse failure than the few kilobytes it saves.

---

### I4 — Line-buffer sizing *(resolved)*

**Status:** 🟢

Reference carries an 8 KiB-ish static line buffer because its `P` op takes a long
hex-encoded PLAY string. SwitchTester's candidate ops (**D6**) are a letter and
up to four small integers.

**Superseded leaning:** 96 bytes, static (not stack — it runs on the main-loop stack
inside an already-nested call). Overflow characters are dropped and the line
still terminates on CR, but overflow must produce an `<ERR>` rather than
silently executing a truncated command — the reference drops silently, which is
exactly the failure mode **S1** exists to prevent.

**Resolved: 256 bytes, `#define`-settable as `ACON_LINE_MAX`, static.**

RAM is not tight on this part, and the 96-byte figure was sized to today's
commands — which is exactly the kind of assumption that becomes a limit later. A
plausible later use is the host pushing a bulk data frame: an edge-time sequence
for the DMA-driven arbitrary-waveform generator (**W7** in
[`switch-cycling-plan.md`](switch-cycling-plan.md)) is hex on the wire and would
not fit in 96 bytes. Making it a `#define` means that case is a constant change
rather than a redesign.

The emit buffer (**I8**) stays at 128 and gets its own `#define` for symmetry —
responses are bounded by the frame grammar and do not grow with input size.

---

### I5 — Async-readiness contract *(deferred)*

**Status:** 🔵 — phase 2; see the note below on what already binds

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

### I7 — Stray output during a session *(resolved)*

**Status:** 🟢

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

- **The filter must be stateful — "prefix the start of each buffer" is wrong.**
  stdio is unbuffered (`_IONBF`), so a single log entry arrives as *several*
  `_write()` calls: `v_logc_printf_time_tag()` alone does `v_print_color()` →
  `v_print_timestamp()` → `printf("[%s] ")` → `vprintf(fmt)`. Prefixing each call
  would produce `#(1.234) #[SYS] #NVM commit: ...`. The `#` must go in only at
  **column 0**.
- So: keep an at-line-start flag, set at session entry and whenever a `\r` or `\n`
  is passed through. When the flag is set and the next byte is not itself a
  newline, inject `#` and clear it. `_write()` then emits in segments rather than
  one block, which costs a short scan per call and only while a session is active.
- **Do not repurpose `ui_stdout_after_crlf_char_count` for this.** It is nearly the
  right state and is tempting, but it resets on `\r` only and ignores `\n`, and
  `utils.c` reads it through `ui_stdout_chars_after_crlf()` for output formatting.
  Widening its reset condition to fix this filter would change behaviour for an
  unrelated caller. A private flag is two bytes.
- **The console's own frames bypass `_write()` entirely** — `v_acon_emit()` writes
  to `uart_stream` directly, so it carries its own sigils and is unaffected. This
  falls out of **I5** obligation 2 rather than being an extra rule.

The result is that *nothing* can desync the host, including output from code
written years from now by someone who never read this document.

**With the flag approach, embedded newlines are handled correctly by
construction** — a multi-line log entry gets a `#` on each of its lines, and a
call site that emits a partial line simply continues that line on the next call,
which is the right answer rather than a hazard. This is strictly more robust than
the per-buffer scheme it replaces.

**Note on line endings:** `logging.c`'s `v_newline()` emits `\r` *then* `\n`
(`logging.c:17-21`), so log lines are CRLF-terminated on the wire. The flag resets
on either character, so a bare `\n` from anywhere else is handled too.

**Superseded leaning:** a `#`-injecting filter in `_write()`, driven by a private
at-line-start flag.

**Resolved: do not filter stdout — suppress it.** In SCRIPT mode `_write()`
discards, and the console's own output never goes near stdio: `v_acon_emit()`
(**I8**) writes to `uart_stream` directly. Nothing stray can reach the wire, so
there is nothing to prefix, and the column-tracking machinery above is not needed
at all.

**stdout stays enabled in HUMAN mode**, and must — `i_getline()` echoes through
`printf`, so suppressing it there would blank the very feature human mode exists
for. There is also no host parser to protect, so interleaved log output is
readable context rather than corruption. The mode already carries this
distinction (**D10**); no new flag is needed.

**D3**'s `#` sigil stays defined as belt-and-braces — it costs nothing and gives
anything that ever writes directly to the port a harmless way to be ignored.

**Consequence worth recording: in SCRIPT mode the console does not depend on
stdio in either direction.** Output goes through `v_acon_emit()` and, since
2026-08-04, input through `i16_uart_stream_rx_byte()` — both straight to
`uart_stream`. (An earlier version of this paragraph claimed independence while
input still went through `getchar()`; that was wrong until the read path
followed.) HUMAN mode is deliberately the opposite and uses stdio both ways, as
`i_getline()` echoes through `printf`.

Pointing the console at a *different* UART is therefore a matter of passing a
different handle. Not wanted for this pass, but it turns wish row **W4** — the
console on a second port while the first stays human — from a redesign into a
parameter.

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

**The emitter appends `\r\n` itself.** Every frame is exactly one line, so no call
site should be able to forget the terminator — forgetting it would run two frames
together and desync the host in a way that looks like a protocol bug rather than a
missing `\n`. It follows that **format strings never contain a newline**, which is
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

### I9 — `i_getline()` gains a silent `^C` exit *(resolved)*

**Status:** 🟢

**`i_getline()` is modified in place**: `^C` (0x03) becomes a third exit state,
`GETLINE_CANCEL_EXIT`, returning **−2**. It emits **nothing** — no `<Cancel>`, no
CRLF, no line-clearing erase. Silent by design, because in the automation console
any unframed output would be noise on a machine-readable stream.

```c
if (u8_done == GETLINE_ESCAPE_EXIT) { return -1; }
if (u8_done == GETLINE_CANCEL_EXIT) { return -2; }
return i_len;
```

**In-place modification is safe here, and it was not for `^X` — that is the whole
reason `^C` is the right key.** `^C` is currently **unhandled**: it falls into the
`< 0x20` silent-discard bucket, so nothing anywhere responds to it today. Adding a
branch introduces *new* behaviour where there was none, rather than *changing*
existing behaviour. `^X` already means "clear the line and keep going", and
repurposing it would have been a regression — which is what the wrapper in the
earlier option C existed to avoid. With `^C` there is nothing to avoid.

**Consumers need no edit.** Exactly one call site exists in the repository —
`u8_debug_entry_u32()` at `debug_menu.c:163` — and it already tests
`if (i_length < 0)`. `^C` therefore cancels a menu entry exactly as ESC does,
which is both universal convention and previously a no-op, so nothing regresses.
`< 0` rather than `== GETLINE_ESCAPE_EXIT` is the house style for this check and
is what keeps future exit states from breaking callers.

**Also settled by this:** ESC keeps meaning "cancel this line" inside the console,
so an arrow key (ESC `[` `A`) discards the line and nothing worse — no mode
change, no spurious frames. The hazard that option A carried is gone without
paying for option C's wrapper or option D's duplicated reader.

**Optional, still separate:** `i_getline()` handles `\b` but not `0x7F`, and
terminals disagree on which Backspace sends. Unlike the `^C` addition, that one
*does* change existing behaviour, so it stays a separate decision.

---

#### Options considered *(kept for audit)*

**The starting facts** (`utils.c`, verified 2026-08-02). Before this change,
`i_getline()` had exactly **two** exits:

| Key | Handling | Return |
|---|---|---|
| **CR** `0x0D` | `GETLINE_NORMAL_EXIT` | `i_len` (0…limit) |
| **ESC** `0x1B` | `GETLINE_ESCAPE_EXIT`, clears line, prints `<Cancel>` | **−1** |

Consumed without exiting: **BS** `0x08` (destructive backspace), **^X** `0x18`
(clears the whole line, keeps reading), every other byte `< 0x20` including
**^C** `0x03` (silently discarded), and `≥ 0x20` (appended, echoed).

So **ESC is the only special-case return**. `^X` is recognised but does not exit;
`^C` is not recognised at all.

**Consumers:** exactly one outside `utils.c` — `u8_debug_entry_u32()` at
`debug_menu.c:163`, which treats *any* negative as "Cancelled - unchanged".

**Options.**

- **A — use ESC, change nothing.** ESC already returns −1 distinguishably, so
  human→script needs no edit to shared code at all. `^X` keeps its present job
  (clear the line, stay in the mode), which is a tidy split: `^X` cancels a line,
  ESC leaves the mode. Symmetric too — 0x1B switches script→human, ESC switches
  back.
  *Cost:* ESC is the lead byte of every ANSI sequence a terminal sends. An arrow
  key becomes ESC `[` `A` → mode switch, then `[` and `A` dispatch as commands.
  The debug menu already has the milder form of this (an arrow key cancels an
  entry), so it is a known wart — but here it also changes mode and emits two
  spurious frames.
- **B — no in-band switch at all.** Mode comes only from the entry argument
  (**D10**); to change it, leave the console and re-enter. Zero code, zero risk,
  no symmetry.
- **C — `i_getline_ex(buf, limit, flags)`, `i_getline()` becomes a `flags = 0`
  wrapper.** With `GETLINE_RETURN_ON_CANCEL`, `^X` becomes a third exit returning
  −2. The existing caller is **not touched and not refactored** — it keeps
  clear-and-retype byte-for-byte, because its call compiles to the same wrapper.
  ESC keeps meaning "cancel this line", so arrow keys stay annoying rather than
  becoming mode changes. Cost: one enumerator, one branch, one wrapper in a shared
  file.
- **D — private line reader in `automation_console.c`.** Total independence, and
  freedom to handle `0x7F`, control opcodes and ANSI sequences properly. Cost:
  ~40 duplicated lines that will drift from the menu's editing feel.

**Note on modifying `i_getline()` in place:** it must not simply be changed to
return on `^X`. The one existing caller reads any negative as cancel, so that
would silently convert the menu's clear-and-retype into abandon-entry — a
regression in a path nobody asked to change. Options C and D both avoid this; A
avoids it by not touching anything.

**Chosen: none of the four.** Switching the key from `^X` to `^C` removed the
constraint they were all working around — `^C` had no existing behaviour to
preserve, so the in-place edit that was unsafe for `^X` is safe for `^C`, and the
wrapper (C) and the duplicated reader (D) both become unnecessary.

The `0x7F`/DEL point raised alongside those options is unaffected by the choice and
carries forward — see the resolution above.

---

### T1 — Host-side Python runner

**Status:** 🔵 — deferred until there is code to drive

A small `pyserial` driver: open COM3 at the console baud, send `ACON_ENTER`,
wait for `=~,V1`, then a `command(cmd, *args) -> parsed response` method
that reads until the frame terminator. Lives in `scripts/` alongside the existing
build/flash scripts.

Cannot be written until the framing is chosen.

---

### T2 — Sync to the design doc *(resolved)*

**Status:** 🟢

Done 2026-08-04. `Docs/SwitchTester-Design.md` now describes the cycler and the
automation console as built and bench-verified rather than banked, carries the
phase-1 command set and wire format, points at the HIL suite, and records the
measured UART performance envelope. `switch-cycling-plan.md` moved from
IMPLEMENTING to DONE.

The stale wording it replaced had survived several sessions and was the single
most misleading thing in the repo — it described work as unbuilt that had been
running on the bench for a day.
---

### T3 — Promote to `G0B1_Skeleton`

**Status:** 🔵 — deferred

Same gate as the transport's **T1**: prove it here first. The REPL executive is
generic; only the op table is application-specific, which is the split that makes
it portable — and the reason **D7** wants the HuIL routines kept out.

**Resolution:** _deferred_

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
  2. `debug_menu.c` intercept (**I1**, SCRIPT) + a menu entry (HUMAN, **D10**) +
     `debug_config.h` gate (**I3**) + `_write()` sigil filter (**I7**) +
     `i_getline_ex()` (**I9**)
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

- **Plan status:** 🟢 30 · 🔵 9 (39 rows) + 5 wish rows. **No row is open.**
  Every phase-1 decision is locked; the nine deferred rows are phase-2 async work
  (**S6**–**S9**, **S12**, **I5**), tooling that follows the code (**T1**, **T2**)
  and skeleton promotion (**T3**). **Next step is implementation, not planning.**

**End of automation-console-plan.md**
