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

v1 scope is the drive side only: force/pulse/toggle the four switch outputs,
start/stop/configure cycling, read back state, and assert on the transport error
counters. Sense-channel ops come later, when the sense design exists. The
protocol is deliberately ASCII and line-oriented — a binary framed transport is
wish-list material (**W1**), not v1.

---

## The Big Board

| ID | Status | Subject (one line) |
|----|--------|--------------------|
| **D1** | 🟢 | Entry / exit protocol — 0xDA in, 0xA5 / `Q` / idle timeout out |
| **D2** | 🟡 | Command namespace — full `0x20..0x7E`, case-sensitive (drop `toupper()`) |
| **D3** | 🔴 | Response framing — uniform terminated envelope vs per-op free-form |
| **D4** | 🟡 | Line grammar — first char is the command, remainder is the argument string |
| **D5** | 🟡 | Prompt / echo policy while in harness mode |
| **D6** | 🔴 | v1 op set — which commands actually ship |
| **D7** | 🟡 | Module name and file home |
| **S1** | 🟢 | Reject-on-error, structured machine-readable error response |
| **S2** | 🟡 | Cooperative pumping and re-entrancy vs `v_debug_menu_service()`'s lock |
| **S3** | 🟡 | Idle-timeout value and what state the tester is left in |
| **S4** | 🔴 | Behaviour while channels are cycling — entry, NVM commit deferral, exit |
| **S5** | 🟡 | Transport error counters as an assertable builtin |
| **I1** | 🟡 | Hook point — where the 0xDA intercept lives |
| **I2** | 🟡 | Op-table home + registration-time collision checking |
| **I3** | 🟡 | Build gate for release images |
| **I4** | 🟡 | Line-buffer sizing and static (not stack) allocation |
| **Q1** | 🔴 | **What must the host runner actually do?** — drives **D6** and most of the rest |
| **T1** | 🔴 | Host-side Python runner |
| **T2** | 🔴 | Sync decisions back into `SwitchTester-Design.md` |
| **T3** | 🔵 | Promote the REPL to `G0B1_Skeleton` alongside `uart_stream` |

### Wish list (v2+)

| ID | Subject |
|----|---------|
| **W1** | Binary framed transport — length + COBS + CRC, for bulk or timing-critical traffic |
| **W2** | Asynchronous event push — tester emits unsolicited records (edge timestamps, cycle completions) |
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
- **Identity macros available** for a version/ping builtin: `PRODUCT_NAME`,
  `PLATFORM_NAME`, `FIRMWARE_VERSION`, `BUILD_CONFIG` (in `device_config.h`).
  There is **no** `BUILD_NUMBER` here, unlike the reference project.
- **The cycler is still unverified on hardware.** Nothing in this plan should be
  taken as evidence it works.

---

## Detail sections

### Q1 — What must the host runner actually do?

**Status:** 🔴 · **Needs user:** yes

**Question:** What campaign is the host script running? The op set (**D6**), the
response format (**D3**) and whether asynchronous push (**W2**) is v1 or v2 all
fall out of this, and guessing wrong means designing the protocol twice.

Three plausible shapes, which pull in different directions:

- **Soak driver** — host configures the cycle parameters, starts channels, comes
  back hours later and asks "are you still cycling, how many transitions, any
  transport errors?" Needs counters and a status query; needs almost no
  throughput; wants **W2** never.
- **Step sequencer** — host drives the tester step by step, asserting the DUT's
  reaction after each step. Needs low latency, a strict command→response
  handshake, and every command to return a parseable completion.
- **Measurement harness** — host wants timestamped edges back, which means the
  tester must emit records the host did not individually ask for. That is **W2**,
  and if it is v1 the framing decision (**D3**) has to accommodate unsolicited
  output from the start.

**Leaning / recommendation:** design for the **step sequencer**, which is the
strict superset of the soak driver, and keep **W2** out of v1 — but with **D3**
choosing a frame shape that can carry an unsolicited record later without
breaking existing host parsers.

**Resolution:** _pending_

---

### D3 — Response framing

**Status:** 🔴 · **Needs user:** yes

**Question:** Does every command return a uniform, terminated envelope, or does
each op print whatever suits it, the way the reference implementation does?

**Options considered:**

- **Per-op free-form** (reference behaviour). Each op prints its own thing;
  `<HRN OPS>` … `<HRN OPS END>` brackets the one multi-line case. Cheapest to
  write; the host needs a bespoke parser per command and has no general way to
  know a response is complete.
- **Uniform single-line envelope** — every command answers with exactly one line,
  `<OK …>` or `<ERR code=… …>`. Host reads one line, done. Multi-line data has to
  be squeezed into key=value pairs or split across several commands.
- **Uniform bracketed envelope** — `<OK cmd=X>` … payload lines … `<END>`. Host
  always reads until `<END>`; payload can be any number of lines; an unsolicited
  **W2** record is just a frame with a different opening tag, so existing parsers
  can skip what they don't recognise instead of desynchronising.

**Leaning / recommendation:** the **bracketed envelope**. It costs two extra
`printf`s per command and it is the only one of the three that survives contact
with multi-line output and future async push. The single-line form is tempting
now and painful the first time an op wants to dump four channels of state.

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

### D4 — Line grammar

**Status:** 🟡 · **Needs user:** no

First non-space character is the command; the remainder, leading whitespace
trimmed, is handed to the op as a single `const char *`. Each op parses its own
arguments. CR or LF terminates; empty lines are ignored.

This is the reference's grammar and it is fine. The one thing worth adding is a
shared numeric-argument helper so `<ch>` parsing and range-checking is not
reimplemented (and mis-implemented) in six ops.

**Leaning:** adopt as described, plus a `b_harness_arg_u32()` helper that reports
"missing" and "out of range" distinctly, so **S1**'s error responses can say which.

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
- **Implementation phase sketch** (once **D3**/**D6**/**Q1** are green):
  1. `hil_repl.{c,h}` — executive, line reader, builtins, framing (**D3**)
  2. `debug_menu.c` intercept (**I1**) + `debug_config.h` gate (**I3**)
  3. Op table + collision scan (**I2**), ops in **D6** order
  4. Bench: enter, `V`, `L`, one op of each shape, exit by all three routes
  5. `scripts/` host runner (**T1**)
  6. Design-doc sync (**T2**)

- **Plan status:** 🟢 2 · 🟡 11 · 🔴 6 · 🔵 1 (20 rows) + 5 wish rows.
  **Next ID: Q1** — it gates **D6**, and **D3** should be taken with its answer
  in hand.

**End of hil-repl-plan.md**
