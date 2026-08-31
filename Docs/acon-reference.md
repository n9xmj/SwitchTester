# Automation console — reference manual

The **acon** is SwitchTester's machine-facing command console: a second personality on the
same USART2 the human debug menu uses, entered by a sentinel byte, speaking a line-oriented
request/response protocol instead of a redraw-heavy menu. It is not test-only — it is the
interface *for* a host-side harness, and equally the way the instrument is driven in normal
operation.

**This document is the operator/host reference: every command this firmware answers, its
syntax, its reply and its failure modes.**

Three other documents exist and are deliberately not duplicated here:

| Document | Covers |
|---|---|
| [`App/automation_console/README.md`](../App/automation_console/README.md) | The **portable module** — adopting it into another project, its config header, and the command-author API (`v_acon_emit()`, `u8_acon_args()`, …). Protocol fundamentals in brief. |
| [`planning/automation-console-plan.md`](planning/automation-console-plan.md) | The **decision log** — *why* the protocol is shaped this way. Rationale, not usage. |
| [`planning/event-path-plan.md`](planning/event-path-plan.md) | The **event path** — the production mask, the record format, and the design of the `A`/`D`/`H` ops. |

The module README stays project-neutral on purpose: the command set below is SwitchTester's,
not the module's, and must not leak into a vendored module's docs. If you port the console
somewhere else you take the README and write your own version of this file.

---

## 1. Getting a session

### Wire

| | |
|---|---|
| Port | USART2, the same one the debug menu uses — **COM3** on this bench |
| Baud | **921600** |
| Framing | 8-N-1, no flow control |
| Line ending | Host sends `CR`. `LF` is discarded wherever it appears, so a CRLF host still produces exactly one frame per command. Device replies `CRLF`. |

### Entering and leaving

| Byte | Meaning |
|---|---|
| `0xDA` | **ACON_ENTER** — enter SCRIPT mode. Intercepted before the menu's echo, so the sentinel never appears on the wire and never reaches the menu dispatcher. |
| `0xA5` | **ACON_EXIT** — leave. Recognised mid-line. |
| `Q` | Quit (a normal command; answers `=Q` then `=~,BYE`). |
| `0x03` | Ctrl-C — quit alias. |

Both sentinels have the MS bit set, so neither collides with a printable menu key nor can be
typed by accident from a terminal. They are a bit-complement pair (`0x5A|0x80` and `~0x5A`).

Entering emits a **ready banner** carrying the protocol version, and leaving emits a farewell:

```
0xDA  ->  =~,V1                 in, speaking protocol version 1
0xA5  ->  =~,BYE                out
```

`~` is the **session opcode** — used for frames the console itself originates rather than a
command. It sits in a normal opcode position, so a host parses session frames with the same
code it uses for everything else. The complete set:

| Frame | When |
|---|---|
| `=~,V<n>` | Session opened; `n` is the protocol version (currently 1) |
| `=~,BYE` | Session closed by sentinel, `Q` or Ctrl-C |
| `!~,TMO` | SCRIPT-mode idle timeout |
| `!~,OVF` | Input line exceeded `ACON_LINE_MAX` |
| `!~,DUP,E<byte>` | Two table entries claim the same opcode — reported once, at first dispatch |

**Wait for the banner before sending a command.** It is the only positive confirmation that
the sentinel was seen and the menu is no longer holding the wire.

### Two modes

Same dispatcher, same frames — only the *reader* differs, so a command tried by hand behaves
exactly as it will from a script.

- **SCRIPT** — entered by the `0xDA` sentinel. Raw byte-at-a-time, no echo, idle timeout
  armed, ordinary `printf` output suppressed for the session's duration. This is what a host
  driver uses.
- **HUMAN** — entered from the debug menu's `[a]` item. Full line editing and echo, because
  there is an operator present. **No idle timeout** — a person is allowed to think.

### Timeouts and limits

| Knob | Value | Meaning |
|---|---|---|
| `ACON_IDLE_TIMEOUT_MS` | 15000 | SCRIPT mode only. A host that goes quiet mid-line frees the console rather than wedging the board. |
| `ACON_TX_TIMEOUT_MS` | 100 | Emit-side backstop. |
| `ACON_LINE_MAX` | 512 | Longest accepted input line. |
| `ACON_EMIT_MAX` | 512 | Longest response frame. |
| `ACON_MAX_ARGS` | 14 | Comma fields `u8_acon_args()` will split out. |

An idle timeout answers with `!` rather than `=` — the host did not ask to leave:

```
!~,TMO
```

**Drive state is untouched by a timeout.** A soak run started earlier keeps running.

### While a session is open

The console pumps `v_app_polling_task()` on **every spin** of its reader, so jobs, cycling,
the periodic tick and the watchdog all keep running. Two consequences worth knowing:

- The board is never "frozen" inside a session. Cycling started with `C` continues while you
  type.
- The debug menu's re-entry lock is held for the session's lifetime, which is what stops the
  menu from stealing input — **and** what keeps the menu-side event log from draining the
  event queue out from under your `D` command. See *Sink selection* in the event-path plan.

---

## 2. Frame grammar

```
host   → device    <op>[,<arg>]...<CR>
device → host      <sigil><op>[,<token>]...<CRLF>
```

### Sigils — the first byte of every device→host line

| Sigil | Meaning |
|---|---|
| `=` | Command response, success |
| `!` | Command response, failure |
| `+` | Payload continuation line |
| `*` | Async event — emitted by monitor mode (`M`) only |
| `#` | Not protocol (stray output, banners) |

**Dispatch on the sigil, never on position.** Any line may turn out to be noise or an async
event; a host that assumes "the next line is my response" desynchronises the first time it is
wrong.

### Tokens

Fields after the opcode are `<KEY><hexvalue>` with **no separator inside the token** — `M8`
is key `M`, value `0x8`. Values are hex without a `0x` prefix. Some ops also emit bare
mnemonic fields (`PASS`, `BYE`, `TMO`, error codes), which carry no key.

### Multi-line replies

A response is **one line** unless its header carries `K<n>`, in which case exactly *n* `+`
payload lines follow.

```
=L,K19
+S set levels: select,set,clear
+R read state
...
```

Declaring the count up front — rather than scanning for a terminator — is what makes a
truncated response *detectable*. Ops that use this: `L`/`?`, `D`, `U`, `B`.

**`K` is reserved for exactly that meaning**, protocol-wide. An op must not use it for any
other count: a host reading a `K` will block waiting for payload lines. Monitor mode's
terminator carries `C` for its event count precisely because of this rule.

### Errors

```
!<op>,<CODE>[,<extra tokens>]
```

| Code | Meaning |
|---|---|
| `UNK` | No such opcode |
| `ARG` | Missing or unparseable field |
| `RNG` | Parsed, but outside limits |
| `OVF` | Line or frame too long |
| `BUSY` | Refused because cycling is in progress (carries `R<runmask>`) |
| `NVM` | An NVM store or commit failed |
| `DUP` | Op-table conflict, reported once at startup |
| `TMO` | Idle timeout (session frame) |

A control-character opcode is echoed in **caret notation** — Ctrl-C appears as `^C` — so a
host never has to handle a raw control byte in the opcode position.

---

## 3. Command reference

Opcodes are **case-sensitive**; there is no `toupper()` folding on this path (that is a debug
menu behaviour, and one of the reasons the acon exists).

### 3.1 Builtins — always present, provided by the module core

| Op | Syntax | Reply | Notes |
|---|---|---|---|
| `V` | `V` | `=V,<product>,<platform>,<firmware>,<build>` | Identity. Enough for a host to pin exactly what it is talking to. |
| `L` | `L` | `=L,K<n>` + *n* `+` lines | List every op with its one-line help. |
| `?` | `?` | as `L` | Alias. |
| `Z` | `Z` | `=Z` | No-op. |
| *(space)* | ` ` | `=Z` | No-op. |
| *(bare CR)* | | `=Z` | No-op that answers — useful as a liveness probe. |
| `Q` | `Q` | `=Q` then `=~,BYE` | Quit. |
| `^C` | `0x03` | as `Q` | Quit alias. |

```
=V,SwitchTester,NUCLEO-G0B1RE,0.1.0.0.0,DEBUG
```

### 3.2 Switch outputs

Four channels, **0 = SWITCH_A … 3 = SWITCH_D**, active high. Bitmaps are one bit per
channel, bit 0 = A.

> **Bench convention, not a restriction.** SWITCH_A drives the DUT on this bench, so scripts
> that have no reason to touch it conventionally stay off it — `test_events.py` works on
> SWITCH_D/SWITCH_C and uses `CH_SAFE_MASK = 0xE` even in its quiesce step. **Nothing in the
> firmware restricts channel 0, and nothing should:** HIL tests and ordinary acon scripts are
> entitled to drive all four channels. `test_acon.py` does drive SWITCH_A deliberately — its
> select-all/clear-all bitmap tests are precisely what a narrowed mask would stop testing.
> Know which convention a given script follows before you run it against live hardware.

#### `S` — set levels

```
S,<select>,<set>,<clear>
```

BSRR-style, so one command expresses set, clear, toggle and hold across any subset:

| select | set | clear | Effect on that channel |
|:---:|:---:|:---:|---|
| 0 | — | — | Untouched. Keeps cycling if it was. |
| 1 | 0 | 0 | Manual, **hold** the level it is at right now |
| 1 | 1 | 0 | Manual, **high** |
| 1 | 0 | 1 | Manual, **low** |
| 1 | 1 | 1 | Manual, **toggle** |

Selecting a channel takes it out of cycling. The level snapshot is taken **once**, before the
loop, so a multi-channel toggle or hold is coherent rather than sampling each channel at a
different instant.

Reply is the standard state triple (see `R`).

#### `R` — read state

```
R    ->    =R,L<levels>,M<modes>,R<running>
```

| Token | Meaning |
|---|---|
| `L` | Level bitmap — which outputs are currently high |
| `M` | Mode bitmap — which channels are timer-driven rather than forced |
| `R` | Run bitmap — which channels are cycling |

Every switch-affecting op (`S`, `W`, `C`, `X`) answers with this same triple, so a host never
needs a follow-up read to learn what its command did — **including on failure**:

```
W,9,1,1,1    ->  !W,ARG,L0,M0,R0        bad channel; state attached anyway
W,3,1,1,1    ->  !W,RNG,L0,M0,R0        below the period floor; state attached anyway
```

That is deliberate: a rejected command still leaves the host knowing exactly where the
hardware stands, so error recovery needs no extra round trip.

### 3.3 Cycling

#### `W` — write cycling parameters

```
W,<ch>,<on_us>,<off_us>,<repeat>
```

Times are **microseconds, hex**. `repeat` 0 means run until stopped.

Does **not** start anything — stage several channels, then release them together with `C`.
Does **not** persist — see `P`.

Limits, checked in this order:

| Check | Limit | Error |
|---|---|---|
| Channel | 0–3 | `ARG` |
| Each phase | `SWITCH_CYCLE_TIME_MIN_US` = 10 µs … `SWITCH_CYCLE_TIME_MAX_US` = 1 000 000 000 µs | `RNG` |
| **Period** (`on + off`) | ≥ `ACON_MIN_CYCLE_PERIOD_US` = **50 000 µs (50 ms)** | `RNG` |

> **The period floor is the single most common way to waste an afternoon.** It applies to the
> host-commanded path only — the engine itself will cycle far faster. A `W` that violates it
> is *refused*, and the channel silently keeps its previously stored parameters, so a test
> that does not check the reply goes on to measure something entirely different from what it
> asked for. **Always check `W`'s reply.**

#### `G` — get cycling parameters

```
G,<ch>    ->    =G,L<lv>,M<md>,R<run>,N<on_us>,F<off_us>,C<repeat>,D<done>
```

| Token | Meaning |
|---|---|
| `N` | ON time, µs |
| `F` | OFF time, µs |
| `C` | Repeat count (0 = infinite) |
| `D` | Cycles **done** |

Reports cycles *done* rather than remaining: with repeat 0 meaning "run until stopped",
"remaining" would encode three different situations as zero. The host computes remaining
itself when the repeat count is non-zero.

#### `C` — start cycling

```
C,<mask>
```

Starts every channel in the mask, beginning with the ON phase. **A channel already running is
restarted from its ON phase rather than ignored** — a host that says "start" wants a known
phase to measure against.

#### `X` — stop cycling

```
X,<mask>
```

Leaves each stopped output **forced LOW**. Use `S` if you want freeze-at-current-level
instead.

### 3.4 Persistence

#### `P` — commit parameters to NVM

```
P    ->    =P,W1      a change was written
     ->    =P,W0      NO_CHANGE, nothing was dirty
     ->    !P,BUSY,R<runmask>       refused: channels still cycling
     ->    !P,NVM,E<status>         the commit failed
```

`P` writes whatever the pool's **RAM shadow** currently holds. An op that changes a value
does not automatically dirty that shadow — it must call the parameter's own save. So `P`
alone cannot persist something nothing staged.

`W0` is a normal, assertable outcome, and it is the in-band way to tell whether a staged
change really reached the shadow.

Refused while cycling, with the run bitmap attached so the host knows which channels to stop
rather than having to issue a separate read.

### 3.5 Event path

The production mask, record format and design rationale live in
[`planning/event-path-plan.md`](planning/event-path-plan.md). What follows is the wire
contract.

#### `A` — event production mask

```
A                       read
A,<mask>                write the live register
A,<mask>,<persist>      write, and if persist != 0 also store to NVM
A,,<persist>            persist whatever the register currently holds

->    =A,M<mask>,W<0|1>
```

The value is the whole `event_control_t` as one hex word:

| Bits | Source |
|---|---|
| 0–3 | Switch A–D **auto** (cycling) transitions |
| 4–7 | Switch A–D **manual** transitions |
| 8–11 | Sense A–D *(bits exist; no producer yet)* |
| 12–29 | Reserved, read back as written |
| 30 | Switch cycle-complete (global, all channels) |
| 31 | **Global enable** |

So `A,0` is the disarm-everything shorthand, and nothing is produced at all unless bit 31 is
set. **Gating is at production** — a masked source never enters the queue and is counted
nowhere.

`W1` means the **NVM shadow was updated**, not that flash was written; the erase happens on
the pool's deferred auto-commit (5 s) or at the next `P`. Persisting is an explicit opt-in
because a HIL suite rewrites this register dozens of times per run, and dirtying the pool on
each would turn a test pass into a string of flash erases.

Both fields are fully parsed before either is applied, so a bad persist flag cannot leave the
mask half-written.

#### `D` — drain events

```
D[,<max>]    max 0 or absent = drain everything currently queued

->    =D,K<n>,N<remaining>,D<drops>
      +I<class>,C<ch>,S<state>,T<tim>,M<ms>      x n
```

| Token | Meaning |
|---|---|
| `K` | Number of `+` payload lines that follow — **exactly this many always arrive** |
| `N` | Records still queued after this batch |
| `D` | Cumulative drop count (queue-full) |

Payload line:

| Token | Meaning |
|---|---|
| `I` | Event class. Emitted with `%X`, so the leading zero is absent on the wire: `I101` manual (0x0101), `I102` auto, `I103` cycle-complete, `I201` sense. `I0` is the not-a-class sentinel |
| `C` | Channel, 0–3 |
| `S` | State — 0/1 for a switch, ADC counts for sense |
| `T` | TIM2 count, 1 µs resolution. `CCRx` for auto (the hardware placed the edge), `CNT` for manual (the write *is* the edge) |
| `M` | Millisecond tick |

`T` and `M` are **different timebases** (TIM2 and SysTick). They free-run separately and
drift; do not derive one from the other.

The depth is snapshotted before the first line is emitted, so `K` is exact — a producer
racing the drain can only *add*, never remove. If a `get` unexpectedly fails mid-batch the
device still emits a sentinel `I0` line, so the host never waits on payload that is not
coming.

Drops are reported by the side-channel counter only. There is **no synthetic in-band record**
for a drop.

#### `M` — monitor mode

```
M,<timeout_ms>

->    =M,T<timeout>                          ack: in, timeout accepted
      *I<cls>,C<ch>,S<st>,T<tim>,M<ms>       one per event, as it happens
      =M,<TMO|CAN>,C<emitted>,D<drops>,N<remaining>
```

Streams events live until the host stops it or the timeout expires. **Async in
behaviour, never in protocol** — the host asked to be here and is told when it ends, so an
unsolicited frame still never turns up in its buffer.

**This is the one op that answers twice**, so it cannot be driven with a plain
send-one-line-read-one-frame helper. Read the ack, collect `*` lines, then read the
terminator.

Event lines carry **exactly the tokens `D`'s `+` payload carries** — same stimulus, same
records either way, and one host-side parser serves both. Only the sigil differs.

| Terminator token | Meaning |
|---|---|
| `TMO` / `CAN` | Ended by its own timeout, or by a byte from the host |
| `C` | Events emitted during the session |
| `D` | Drop delta *over this session*, not the lifetime counter |
| `N` | Still queued at exit — non-zero means follow up with `D` |

> **`C`, not `K`, for the count.** `K` in a header is reserved protocol-wide for "exactly
> this many `+` payload lines follow". A terminator using `K` would block any conforming
> host — including `acon.py` — waiting for payload that is never coming. The streamed `*`
> lines were already delivered; they are not a declared payload block.

**The timeout is required and bounded.** `0` (unlimited) is refused with `RNG`, and so is
anything above `ACON_MONITOR_MAX_MS` (**1 hour**) — a ceiling matters as much as the floor,
since `0xFFFFFFFF` ms would obey the rule and defeat it. The session idle timeout is **not**
running while the device is inside a dispatch, so this timeout is the only thing between a
host that dies mid-monitor and a console that never comes back.

Note the reason code `TMO` here arrives on a `=` frame, not `!`: the host *asked* for this
timeout. Contrast `!~,TMO`, the session idle timeout, which nobody asked for.

**Input while monitoring:**

| Byte | Effect |
|---|---|
| `CR` (0x0D), `LF` (0x0A) | Consumed and ignored — **not** a cancel |
| XOFF (0x13) | Suspend emission, stay in the handler |
| XON (0x11) | Resume |
| anything else | Cancel. The byte is **consumed**, not executed |

The CR/LF carve-out is mandatory, not cosmetic: a CRLF-sending host leaves its LF in the ring
after the CR terminated the command line, and under a bare "any byte cancels" rule that
straggler would kill the monitor the instant it started.

`ACON_EXIT` (0xA5) cancels by being "any byte" and needs no special case — but note it does
**not** then leave the session, because it was consumed as the cancel. Send it again once the
terminator has arrived.

**While suspended the timeout keeps running.** If XOFF paused it, a host that suspends and
then crashes would strand the device — reintroducing exactly what the finite timeout fixes.
Nothing is drained while suspended, so the queue fills and may overflow; that is a better
trade than backpressuring a producer ISR, and it is why the terminator reports the drop
delta.

#### `H` — event queue housekeeping

```
H  or  H,S    status
H,F           flush (discard everything queued)
H,R           reset the drop and put counters

->    =H,N<queued>,D<drops>,P<puts>
```

Kept off the drain path so `D` stays purely a read.

### 3.6 Diagnostics

#### `E` — transport error count

```
E    ->    =E,E<count>
```

UART stream error counter — framing, overrun and parity errors seen by the transport.

#### `U` — UART loopback stress

```
U,<idx>[,<first>,<last>,<bursts>]

->    =U,K<steps>,I<idx>,B<baud>
      +S<size>,N<bursts>,T<sent>,R<received>,X<mismatches>,E<errors>,M<...>   x steps
```

Requires a loopback on the indexed UART. See `App/Src/uart_stress.c`.

#### `B` — baud sweep

```
B,<idx>[,<rate>...]        no rates = the default ladder

->    =B,K<rungs>,I<idx>,Z<...>
      +D<requested>,A<actual>,T<sent>,R<received>,X<mismatches>,E<errors>     x rungs
```

Failures answer `!B,<why>,I<idx>`.

> The recorded result of this sweep: **USART4/5/6 have no FIFO and cap at 230400**;
> everything else reaches 921600. Do not re-chase that.

#### `Y` — SPI flash probe *(temporary, removal-slated)*

```
Y,I    JEDEC id            ->  =Y,<mfr>,<type>,<density>,<verdict>
Y,S    status register     ->  =Y,S,<sr>
Y,T    DMA read/write test ->  =Y,T,PASS|FAIL,ID<id>,BL<...>,VF<...>
Y,L    loopback            ->  =Y,L,PASS|FAIL,<tx>,<rx>
Y,N    nCS loopback        ->  =Y,N,PASS|FAIL,L<lo>,H<hi>
Y,J    SCK loopback        ->  =Y,J,PASS|FAIL,L<lo>,H<hi>
Y,C[,0|1]  park nCS        ->  =Y,C,<level>
Y,K    clock burst         ->  =Y,K,<bursts>
```

> RDID is opcode **0x9F**. The legacy MX25R80 driver sent the Macronix-only 0x9E; W25Q and
> SST parts need 0x9F. Check the opcode before suspecting wiring.

### 3.7 Test-harness dispatchers

These two are sub-command dispatchers owned by their HIL suites rather than by an operator.
Each sub-letter has its own argument list; the tables below give the map, and the argument
detail lives with the code and the suite that drives it.

#### `N` — NVM test suite

`nvm_test.c` · driven by `scripts/hil/test_nvm.py`

| Sub | Purpose |
|---|---|
| `N,P[,0\|1]` | Select backend: 0 = RAM (default), 1 = SPI flash |
| `N,I` | Pool info — header fields plus derived geometry |
| `N,L` | List objects as `id:size` pairs, via the public API only |
| `N,C,<id>,<val>` | Create with a default value |
| `N,G,<id>` | Get; reports value alongside status |
| `N,S,<id>,<val>` | Set |
| `N,D,<id>` | Delete |
| `N,K` | Commit (`NO_CHANGE` is a normal, assertable outcome) |
| `N,R[,<policy>]` | Release and re-initialise — how init behaviour is tested |
| `N,W,<fill>` | Overwrite the emulated device (0xFF/0x00 = blank media, else garbage) |
| `N,F,<n>[,<err>]` | Arm a device fault on the n-th next access |
| `N,Z` | Disarm a pending fault |
| `N,A` | Device access counts — distinguishes "nothing to write" from "wrote nothing" |
| `N,B` | Reset the access counts |
| `N,T,<elapsed>,<limit>` | Commit-timer probe |

#### `F` — event queue (FIFO) test suite

`eventq_test.c` · driven by `scripts/hil/test_eventq.py`. Drives a **dedicated test queue**,
not the application's event queue — `A`/`D`/`H` are the application path.

| Sub | Purpose |
|---|---|
| `F,C[,<size>[,<mode>]]` | Create; reports the module's own status verbatim |
| `F,D` | Destroy |
| `F,I` | Info — the three helpers plus ring size |
| `F,R[,<sel>]` | Reset counters: 0/absent both, 1 drops, 2 puts |
| `F,P,<id>[,<hexbytes>]` | Put with an exact host-chosen payload |
| `F,G[,<cap>]` | Get, echoing copied payload as hex |
| `F,Z` | Flush |
| `F,S,<id>,<len>[,<seed>]` | Put a generated pattern, for sizes beyond the line limit |
| `F,V[,<cap>[,<seed>]]` | Get and verify the pattern on-target |
| `F,T[,<n>]` | ISR producer — arm a run of `n` puts from the periodic tick |

### 3.8 Examples

| Op | Purpose |
|---|---|
| `@,a,b,c` | Echo args back as CSV — a worked example for command authors |
| `$ <text>` | Echo raw text |

---

## 4. Worked sequences

### Arm the event path, run a cycle, collect the records

```
0xDA                =~,V1                   enter SCRIPT mode; wait for the banner
H,F                 =H,N0,D0,P0             start from an empty queue
A,C0000088,1        =A,MC0000088,W1         D auto+manual, cycle-complete, global; persisted
W,3,186A0,186A0,3   =W,L0,M0,R0             D: 100 ms on, 100 ms off, 3 repeats
C,8                 =C,L8,M8,R8             start
...                                         wait ~600 ms
D                   =D,K9,N0,D0             drain
                    +I101,C3,S1,T...,M...
                    +I102,C3,S0,T...,M...
                    ...
                    +I103,C3,S0,T...,M...   cycle complete
0xA5                =~,BYE
```

The production order inside one run is worth knowing: the cycle start's **manual ON**, then
the auto edges, then the halt's **manual OFF**, then the **completion** record.

### Park the board

```
X,E                 stop cycling on channels 1..3
S,E,0,E             force those channels low
A,0,1               disarm every source, persisted
P                   commit now rather than waiting out the 5 s auto-commit
H,F                 empty the queue
H,R                 zero the counters
```

### Liveness probe

Send a bare `CR`. A live console answers `=Z`; anything else means you are not in a session.

---

## 5. Driving it from a host

`scripts/hil/acon.py` is the host-side driver. A test never touches the wire format — it
calls `command("W,0,7A120,7A120,0")` and gets back a `Frame` it can assert on.

```python
from acon import AutomationConsole
with AutomationConsole('COM3', 921600) as con:      # the context manager opens the PORT
    con.enter()                                     # ...entering the SESSION is separate
    frame = con.command('A,C0000088,1')
    assert frame.ok and frame.tokens['M'] == 0xC0000088
    con.leave()
```

Opening the port and entering the session are deliberately two steps — a test that wants to
observe menu-mode behaviour, or to check what happens when the sentinel is *not* sent, needs
the port without the session. `enter()` returns the `=~,V1` banner and raises
`ProtocolError` if it does not arrive.

`Frame` carries `sigil`, `op`, `ok`, `code`, `tokens` (a `{KEY: int}` dict), `fields` and
`payload` (the `+` lines). Two protocol rules are honoured inside it because getting them
wrong is how a host desynchronises: **dispatch by sigil, never by position**, and **read a
declared `K` count rather than scanning for a terminator**. `read_frame()` returning `None`
on timeout is a legitimate result, not an error — several tests assert that the device stays
silent.

Convenience methods: `enter()`, `leave(how=...)`, `state()` → the `(L, M, R)` triple,
`quiesce()`, and for monitor mode `monitor(timeout_ms, cancel_after=None)` →
`(ack, events, terminator)` plus `suspend()` / `resume()` for XOFF/XON. Note that `acon.py`'s own `quiesce()` uses mask `0xF` — **all four channels**;
`test_events.py` deliberately shadows it with a `CH_SAFE_MASK = 0xE` version so SWITCH_A is
never driven on this bench.

The suites, all run against a live board:

| Suite | Tests | Covers |
|---|---:|---|
| `test_acon.py` | 47 | The protocol itself, switch ops, cycling, persistence |
| `test_events.py` | 30 | The application event path — mask, gating, records, drain, monitor mode, persistence |
| `test_eventq.py` | 20 | The vendored `event_queue` module, via `F` |
| `test_nvm.py` | 28 | The vendored `nvmparams` module, via `N` |

```bash
python scripts/hil/test_events.py --port COM3 --baud 921600
```

Add `--trace` to dump the wire traffic, `-k <substring>` to filter.

---

## 6. Gotchas

- **Close Tera Term first.** It holds COM3, and a HIL run will fail to open the port.
- **Check the reply on setup commands.** A refused `W` leaves the channel on its *stored*
  parameters and the run then measures something else entirely. The 50 ms period floor is the
  usual cause.
- **`A` does not persist unless you ask.** Add the persist flag, or the mask is gone at the
  next reset. Until 2026-08-30 it could not persist at all.
- **`P` returning `W0` is information, not a failure.** It means nothing was dirty — which is
  the assertion that catches a value that never reached the shadow.
- **Opcodes are case-sensitive.** `a` is not `A`.
- **`*` frames come only from `M`.** Route them by sigil regardless — a host that assumes
  "the next line is my response" desynchronises the first time it is wrong.
- **`M` answers twice.** Ack, then stream, then terminator. `command()` reads one frame; use
  `monitor()`.
- **Two ST-Link probes are on this bench.** Flashing pins the NUCLEO's serial number; the
  other probe belongs to the DUT and must never be targeted.

---

*Generated from the firmware as built 2026-08-30. The authoritative list is always the live
`L` command — if this file and the board disagree, the board is right.*
