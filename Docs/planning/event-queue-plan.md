# Event queue — decision log

**Feature:** vendorable variable-length-message ring FIFO (`event_queue`) — 16-bit
generic event ID + 16-bit length header, 0..N data bytes, copy-in/copy-out, ISR-safe
producers. Distinct from `uart_stream`'s byte-oriented `queue.c`.

**Code home (planned):** `App/event_queue/` (vendored module per the portable-APIs
conventions). **Parent spec:** [`../SwitchTester-Design.md`](../SwitchTester-Design.md)
(sync after decisions land).

**Status:** PHASE 1 COMPLETE (2026-08-27) — designed, implemented,
bench-verified (`test_eventq.py` 20/20; acon 47/47, nvm 28/28) and documented
(`README.md`, T2) in one day. Same-day additions: size round-down
(EQ_STATUS_SIZE_ROUNDED), allocator macro seam, peek + flush (S7), dropped-event
counter (S8), successful-put counter (S9). Only W4
(priority put) remains banked, with a promotion draft. **Working mode:** user
relays design in chat; one question at a time; board holds everything else.

---

## Brief

A general-purpose event queue: producers (possibly ISRs) push small records — a 16-bit
ID, a 16-bit size, and 0..N payload bytes — into a ring buffer; a consumer (typically
the main loop) pops them into caller-provided storage. The core is fully
hardware-independent (C stdlib only) so it can be vendored into any project. Thread
safety comes from a single-producer/single-consumer lock-free design by default, with
optional per-queue adopter-supplied lock/unlock function pointers when multiple
producers exist. Phase 1 is the four core functions plus a small helper set; peek and
flush are banked.

First expected customer: automation-console phase-2 async events
(`v_acon_flush_events()` / `JOB_CYCLE_COMPLETE`), already designed in
[`automation-console-plan.md`](automation-console-plan.md).

---

## Big Board

| ID | Status | Subject (one line) |
|----|--------|--------------------|
| D1 | 🟢 | API surface: create / destroy / put / get, config-struct create |
| D2 | 🟢 | `get` takes a caller-filled `event_queue_record_t` (2 params total) |
| D3 | 🟢 | `put` keeps direct params `(handle, id, size, pv_data)` — mixed style |
| D4 | 🟢 | Phase-1 helper set: `b_is_empty`, `u32_free_space`, `u16_count` |
| D5 | 🟢 | Status enum: packed int16-equivalent, negative=error / positive=info |
| D6 | 🟢 | Naming: `EQ_ERROR_*` / `EQ_STATUS_*` constants, `event_queue_` types/functions |
| S1 | 🟢 | Record format: `{u16_id, u16_data_size, u8_data[]}`; stored size rounded to 4 |
| S2 | 🟢 | Wrap: records may split at the ring boundary; two-stage memcpy |
| S3 | 🟢 | Sign principle: get-on-empty = info (+), put-on-full = error (−) |
| S4 | 🟢 | Get truncation: copy what fits, discard rest, info code, true size always out |
| S5 | 🟢 | Locking: per-queue fn pointers, put-path only, NULL = SPSC lock-free |
| S6 | 🟢 | Destroy/guard detail: free owned buffer, reset state, init-magic on all ops |
| S7 | 🟢 | Peek + flush shipped (promoted from W1/W2); consumer-context ops |
| S8 | 🟢 | Dropped-event counter: producer-owned monotonic + consumer-owned ack |
| S9 | 🟢 | Successful-put counter, same ack pattern; lifetime total, not a depth |
| I1 | 🟢 | Monotonic internal byte counters — no head/tail ambiguity, no shared writes |
| I2 | 🟢 | Buffer ownership flag; malloc behind `EVENT_QUEUE_ENABLE_MALLOC` (default on) |
| I3 | 🟢 | `create` takes `const` config, copies into handle; config can live in flash |
| I4 | 🟢 | Create guards: min size = 2×sizeof(header); require 4-byte-aligned buffer |
| I5 | 🟢 | Struct contents: agent's call, callers treat handle as read-only |
| T1 | 🟢 | Vendoring shape: `App/event_queue/`, stdlib-only deps, config example header |
| T2 | 🟢 | Adoption README — written post-verification, example-first |
| T3 | 🟢 | Testing via the automation console (HIL-style); no dedicated TEST build |

## Wish list (v2+)

| ID | Subject |
|----|---------|
| W1 | ~~Non-consumptive get (peek)~~ — **IMPLEMENTED 2026-08-27**, see S7 |
| W2 | ~~Consumer-side flush/drain-all~~ — **IMPLEMENTED 2026-08-27**, see S7 |
| W3 | ~~C23 `enum : int16_t` spelling~~ — **DROPPED 2026-08-27** (user: the packed idiom is long-proven, and the sizeof static assert already trips if enum sizing is ever forced) |
| W4 | Priority put — event jumps the line, consumed by the next get *(user, DEFERRED 2026-08-27; promotion draft below — see "W4 — promotion draft")* |

---

## LOCKED CONTEXT

- **Vendorable:** no dependencies beyond the C standard library
  (`stdint/stdbool/string/stdlib`). No HAL, no project headers.
- **Producers may be ISRs; multiple producers are possible. A single consumer (get
  point) is probable** (user, 2026-08-27). The SPSC lock-free guarantee covers the
  producers-vs-consumer edge; producer-vs-producer serialization is the adopter's
  lock's job (S5).
- **Cortex-M0+ constraints that shaped the design:** no unaligned access (hence S1's
  rounding + no casting into the ring), no BASEPRI (adopter locks are PRIMASK or
  per-IRQ NVIC gating), aligned 32-bit load/store is atomic (basis of I1).
- **Ease of use is a stated near-top priority.** All synchronization bookkeeping is
  internal; call sites manage nothing (user, 2026-08-27).
- **Zero-means-default principle (user, 2026-08-27):** any caller-filled struct
  member left at 0/NULL — as C99 designated initializers produce for omitted
  members — must behave as a meaningful default, never as a trap. Applies to every
  caller-visible struct in the API (config, get record, future additions).
- **Parameter-count rule of thumb (user):** more than 3–4 function parameters →
  collapse into a struct. `put`'s four scalars just qualify for direct treatment.
- Hungarian prefixes per house style; enum-returning functions use `x_`.

---

## Detail sections

### D1 — API surface *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** Four core functions:

```c
event_queue_status_t x_event_queue_create (event_queue_handle_t *px_handle,
                                           const event_queue_config_t *px_config);
event_queue_status_t x_event_queue_destroy(event_queue_handle_t *px_handle);
event_queue_status_t x_event_queue_put    (event_queue_handle_t *px_handle,
                                           uint16_t u16_id,
                                           uint16_t u16_data_size,
                                           const void *pv_data);
event_queue_status_t x_event_queue_get    (event_queue_handle_t *px_handle,
                                           event_queue_record_t *px_record);
```

`create` moved from a parameter list to a caller-filled **config struct** (user,
2026-08-27): C99 designated initializers make the call self-documenting (same idiom as
the debug-menu menu-builder inits), omitted members zero → defaults by omission
(`NULL` buffer → malloc, `NULL` locks → SPSC), and future optional members never
change the signature. Config members: `u32_size`, `pv_buffer`, `v_lock`, `v_unlock`.
All functions return the status type, including `destroy`.

### D2 — `get` record struct *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Options considered:** (a) user's original in/out `pu16_data_size` pointer — fewest
params but an uninitialized-in/out footgun (forgotten pre-set → garbage copy limit →
`memcpy` overrun); (b) explicit five-parameter form — safe but parameter-heavy;
(c) record struct — safe *and* two params, named members at call sites.

**Leaning:** (c):

```c
typedef struct
{
  uint16_t  u16_id;         /* out: event ID                       */
  uint16_t  u16_data_size;  /* out: true stored size, always set   */
  uint16_t  u16_buf_size;   /* in:  capacity of pv_data            */
  void     *pv_data;        /* in:  destination; NULL = discard    */
} event_queue_record_t;
```

Drain loops fill it once and re-call: the library never writes `u16_buf_size` /
`pv_data`. `pv_data == NULL` discards the payload (drain-unwanted-event case) while
still returning ID and true size.

**Resolution:** option (c) confirmed (user, 2026-08-27), with the added
**zero-means-default requirement** (now in LOCKED CONTEXT): members a designated
initializer leaves at 0/NULL must behave sensibly, not as traps. For this struct the
zeros already compose safely — `pv_data = NULL` → discard payload;
`u16_buf_size = 0` → nothing copied, truncation info code if payload was non-empty;
ID and true size always returned. An all-zero record is therefore a valid
"consume-and-tell-me-what-it-was" call.

### D3 — `put` keeps direct parameters *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Rationale kept:** `(handle, id, size, pv_data)` is four natural inputs with no
in/out ambiguity, and `put` is the ISR-side hot path — forcing producers through a
struct fill adds stores exactly where overhead matters most.

**Resolution:** confirmed (user, 2026-08-27), citing their rule of thumb — >3–4
params collapse into a struct; `put` "(barely) qualifies" for scalar treatment.

### D4 — Phase-1 helper set *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** all three confirmed (user, 2026-08-27) — each falls out of the I1
counters for free:

- `b_event_queue_is_empty(px)` — consumer-side safe.
- `u32_event_queue_free_space(px)` — bytes available for the next record's *payload*
  (net of one header); stable only from the producer context (doc note).
- `u16_event_queue_count(px)` — queued-record count via a second pair of monotonic
  *record* counters, same single-writer trick.

**Considered and DECLINED 2026-08-27:** a `bytes_used` / `bytes_free` / `capacity`
trio, proposed on the observation that `u32_event_queue_free_space()` returns
payload capacity for the next record (free bytes minus one header) rather than raw
free bytes, and that nothing reports fullness directly. **User: not wanted --
`u32_event_queue_free_space()` provides the information needed.** Do not re-propose;
the header deduction is deliberate and documented, not an oversight.

Phase-2 ideas surfaced during resolution, banked on the wish list: non-consumptive
get (W1) and priority put (W4). Flush remains W2.

### D5 — Status enum shape *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** `typedef enum __attribute__((packed)) { EQ_ERROR_MIN = -32768, …,
EQ_INFO_MAX = 32767 } event_queue_status_t;` — bounding values force a genuine signed
16-bit type. GCC extension (Clang-compatible), acceptable for the arm-none-eabi-gcc
target family; config header is the escape hatch if ever needed, C23 fixed underlying
type is the eventual standard spelling (W3). Sign semantics per S3: caller's
`if (x < 0)` cleanly separates real errors from information codes; `0` = OK.

### D6 — Naming conventions *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Options considered:** full `EVENT_QUEUE_ERROR_*` (rejected by user as overly wordy
once the actual code name is appended); bare `Q_*` (user considered dropping "EVENT"
entirely but flagged possible ambiguity with uart_stream's byte queue).

**Resolution:** blessed (user, 2026-08-27). Constants `EQ_ERROR_*` / `EQ_STATUS_*`,
plus `EQ_OK = 0`. Types `event_queue_status_t` / `event_queue_handle_t` /
`event_queue_config_t` / `event_queue_record_t`, functions `x_event_queue_*`, files
`App/event_queue/event_queue.{c,h}` + `event_queue_config.h`. Collision check
(2026-08-27): uart_stream's `queue.{c,h}` uses only `queue_t` / `queue_s` /
`QUEUE_H_`, and nothing in `App/` uses an `EQ_` prefix — unambiguous. Exception: the
config macro `EVENT_QUEUE_ENABLE_MALLOC` stays long-form — config-header defines
benefit from greppable uniqueness.

### S1 — Record format and alignment *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** wire format:

```c
typedef struct
{
  uint16_t u16_id;
  uint16_t u16_data_size;   /* TRUE payload size — not rounded */
  uint8_t  u8_data[];
} event_queue_header_t;
```

Space consumed/freed per record is `sizeof(header) + u16_data_size` **rounded up to a
multiple of 4** — same scheme as nvmparams pool management. The header always stores
the true size. Because records can still start at a rotating offset after wraps, the
core additionally **never casts into the ring**: headers are memcpy'd to/from an
aligned local, so the struct describes the wire format rather than being a live
pointer target (M0+ hard-faults on unaligned access). Max payload 65535 bytes.

### S2 — Wrap handling *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** records may split across the ring boundary; `put`/`get` do a
two-stage memcpy. Chosen over pad-and-skip (never-split) because it wastes zero
space and reserves no marker ID, and costs only a few lines given S1's
copy-through-locals rule. (User, 2026-08-27: "go with your lean.")

### S3 — Error/info sign principle *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** **negative = the operation failed in a way that loses data or
indicates misuse; positive = it did something defensible the caller may want to know
about; 0 = OK.** Consequences: put-on-full is negative (the event was dropped — the
producer must know); get-on-empty is positive (nothing lost; it is the idle result of
every polling loop); truncated get is positive (S4). NULL handle / bad params /
uninitialized handle are negative.

### S4 — Get truncation semantics *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** when the stored payload exceeds the caller's buffer capacity, copy
what fits, discard the remainder, return a positive info code, and always set the
out-size to the **true stored size**. Not a failure — a notification. (Per the user's
opening spec.)

### S5 — Locking model *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** the config (and handle) carry `void (*v_lock)(void)` /
`void (*v_unlock)(void)`. `NULL` = no locking — valid for the SPSC case, which is
lock-free by construction (I1). Non-NULL locks wrap **the put path only**: their sole
job is serializing producers among themselves; once puts are serialized the consumer
sees a single committing producer, so `get` never masks anything. Per-queue function
pointers beat a global config macro because lock policy is genuinely per-instance
(one queue fed by a single ISR, another by three contexts).

**Adopter contract (README material):** the lock must mask every producer context of
that queue except the caller's own, including ISR-preempts-mainloop-mid-put. On M0+
that means PRIMASK or per-IRQ NVIC gating (no BASEPRI). Bare `void(void)` signatures
(no context arg) — per-queue policies get dedicated two-line functions anyway.

### S6 — Destroy and handle-validity guards *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** confirmed (user, 2026-08-27). `destroy` returns the status type;
frees the buffer only if library-owned (I2); resets queue state. The handle carries a
magic/init field set by `create`, cleared by `destroy`; `put`/`get`/helpers fail with
a negative code on a never-created or destroyed handle instead of chasing wild
pointers. Cheap, and this API is automated-path in spirit (guard policy: guard
automated paths).

### I1 — Monotonic internal counters *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** the handle holds `u32_bytes_written` (written only by `put`) and
`u32_bytes_read` (written only by `get`), free-running, never reset. Derived:
used = `written − read` (unsigned wrap-safe, same idiom as SysTick timestamp
deltas — correct modulo 2³² because the true difference never exceeds the buffer
size); free = size − used; empty ⇔ equal. Kills the head==tail full/empty ambiguity
with no reserved byte and no power-of-2 size requirement; single-writer-per-counter +
atomic aligned 32-bit access on M0+ is the SPSC lock-free guarantee — stale reads of
the other side's counter err conservatively. Each side also keeps a private wrapped
index in the handle so the hot path does no division. Commit order in `put`: copy
record, *then* advance the counter. A second pair of record counters serves
`u16_event_queue_count` (D4). **Entirely internal — call sites manage nothing**
(user-confirmed requirement, 2026-08-27).

### I2 — Buffer ownership and malloc guard *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** `pv_buffer == NULL` in the config → `malloc(u32_size)`; an ownership
flag in the handle tells `destroy` whether `free()` is safe. The malloc path compiles
behind `EVENT_QUEUE_ENABLE_MALLOC`, **defined/on in the example adoption header** —
heapless adopters undefine it and supply static buffers, and the module then never
links malloc/free.

### I3 — Const config *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** `create` takes `const event_queue_config_t *`, never writes through
it, and copies what it needs into the handle. Callers may declare configs
`static const` so they live in flash. A stack-scoped config is equally legal since
nothing retains the pointer. The config stays a distinct type (not a view of the
handle) so callers cannot poke live queue state.

### I4 — Create-time guards *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** confirmed (user, 2026-08-27). Minimum `u32_size` =
`2 × sizeof(event_queue_header_t)` = 8 bytes (user's opening spec; two zero-payload
records). A **caller-supplied buffer must be 4-byte aligned**, checked in `create`
and rejected with a negative code otherwise — S1's rounding keeps records 4-aligned
*relative to the buffer start*, so a misaligned start would silently defeat it. The
user judged the restriction undemanding: sizeable static or malloc'd buffers are
typically 4-aligned anyway.

**Agent note under the zero-means-default principle (flag if unwanted):**
`u32_size == 0` in the config will follow the same principle — default to an
`EVENT_QUEUE_DEFAULT_SIZE` from the config header rather than erroring, so a
designated initializer that omits the size still yields a working queue.

### I5 — Handle struct contents *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** struct membership for **all** API structs is the agent's call (user,
2026-08-27). Public-header handle struct (caller instantiates), documented
**read-only for callers**. Sketch: buffer pointer + size; ownership flag; init magic
(S6); lock/unlock pointers (S5); `u32_bytes_written/read`, `u32_wr_idx/rd_idx`,
record counters (I1). Exact layout settles during implementation.

### S7 — Peek and flush *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** promoted from W1/W2 and implemented 2026-08-27 (user: "the
other ones should be easy" — they were, both falling out of one refactor of
get into a shared extract path with a consume flag).

- `x_event_queue_peek(px_handle, px_record)` — identical contract to get
  (record struct, truncation, EMPTY) but non-consumptive. A truncated peek
  discards nothing: a later get with a big enough buffer still retrieves the
  whole payload. Consumer-context only, like get.
- `x_event_queue_flush(px_handle)` — discards every queued record by draining
  through the get path, so the SPSC contract is untouched and concurrent puts
  are safe (records committed after the flush passes them are kept). EQ_OK
  once empty — flushing an already-empty queue is success. Consumer-context
  only. A one-jump implementation (read-counter teleport to a write-counter
  snapshot) was considered and rejected: snapshotting the byte and record
  counters atomically against a live producer needs a retry loop, and the
  drain loop is equally correct with none of that.

Console: `F,K` (peek, shares the `F,G` handler) and `F,Z` (flush). HIL: two
new tests (peek non-consumption incl. truncated-peek-keeps-data; flush
idempotence and NOT_INIT guard) — suite now 17.

### S8 — Dropped-event accounting *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** implemented 2026-08-27 at the user's direction, following this
project's own job-queue precedent (`u8_full` doubling as an overflow count, reported
in band as a synthetic `JOB_QUEUE_OVERFLOW` job). `event_queue` has no full flag to
overload, so the counter is its own handle field with an accessor and a reset-er:

```c
uint32_t u32_event_queue_dropped     (const event_queue_handle_t *px_handle);
void     v_event_queue_dropped_reset (event_queue_handle_t *px_handle);
```

**Only `EQ_ERROR_FULL` is counted.** A parameter or not-initialised return is a
caller bug, not a dropped event; folding those in would make the number mean two
things. Drops from every producer land in the same counter, so on a multi-producer
queue the figure is the total across all of them.

**The design constraint, and why it is not a naive counter.** The count is
incremented by `put` (producer, typically an ISR) and reset by the consumer. The
Cortex-M0+ has no LDREX/STREX, so a read-modify-write cannot be made atomic without
masking -- which this module exists to avoid. An obvious "increment in put, zero in
reset" therefore races: a consumer store of 0 landing inside the producer's RMW is
silently clobbered by the writeback. The fix is the module's own idiom, one writer
per field: `u32_records_dropped` is producer-owned and monotonic,
`u32_dropped_ack` is consumer-owned and written only by the reset-er, and the
accessor returns the unsigned-wrap-safe delta. No shared RMW, no lock, correct in
both SPSC and locked multi-producer.

Console: `F,I` reports `O<dropped>`, `F,R` resets. HIL: two new tests (drops counted
only for FULL, survive drains, reset and resume; inert on a destroyed handle), plus
the ISR soak now reconciles the module's counter against the harness's own tally --
`dropped == ISR drops + host drops` exactly. Suite 19.

### S9 — Successful-put counter *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** added 2026-08-27 at the user's suggestion as a diagnostic companion
to S8 -- *"counted successful put attempts ... would not have to be decremented on a
get ... dead-simple to implement."*

```c
uint32_t u32_event_queue_puts     (const event_queue_handle_t *px_handle);
void     v_event_queue_puts_reset (event_queue_handle_t *px_handle);
```

It is a **lifetime total, not a queue depth** -- `u16_event_queue_count()` remains
the depth. `puts + dropped` is every put attempt a producer made.

**Why it is not simply the raw counter exposed.** The underlying total
(`u32_records_put`) already existed -- it is half of what
`u16_event_queue_count()` computes as `records_put - records_got`. So a reset-er
that *zeroed* it, which is the obvious reading of "just a uint32_t", would make
that subtraction underflow and report a nonsense queue depth. Using S8's
consumer-owned-ack pattern avoids that as well as the cross-context
read-modify-write race, and costs one extra handle word: the producer's total stays
monotonic and untouched, `u32_puts_ack` is consumer-owned, and the accessor returns
the delta.

Console: `F,I` gains `P<puts>`; `F,R` takes an optional selector (0/absent both,
1 drops, 2 puts) so the host can prove the two reset-ers are independent. HIL: one
new test covering successes-only counting, get-does-not-decrement, independent
resets, and that a reset leaves the live queue depth intact; the ISR soak also
reconciles `puts == ISR puts + host puts`. Suite 20.

### T1 — Vendoring shape *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** `App/event_queue/event_queue.{c,h}` + `event_queue_config.h.example`
per the portable-APIs conventions (Skeleton `Docs/planning/portable-apis-strategy.md`).
Dependencies: C stdlib only. Adopter seam = config header (malloc switch; future
knobs).

### T2 — Adoption README *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Resolution:** written 2026-08-27, after implementation/test/debug as
scheduled. `App/event_queue/README.md`, following the nvmparams README pattern
with the user's directive applied: **document-by-example first** — a "sixty
seconds of usage" full integration leads, followed by static-ring and
multi-producer variations, then the formal reference (config knobs, record
format/space math, status-code table with the sign principle, API rest,
gotchas). Carries the S5 adopter lock contract and an explicit note that the
`< 0` triage idiom is the OPPOSITE of nvmparams' never-test-`< 0` convention,
for adopters using both.

### T3 — Test approach *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Options considered:** a PC-side native unit-test build (module is pure C stdlib,
would run on the host) — **rejected by the user**: no dedicated TEST build wanted.

**Resolution:** test through the **existing automation-console interface** with
HIL-style host tests (user, 2026-08-27) — the `test_nvm.py` pattern: a
`scripts/hil/test_eventq.py` driving acon commands that exercise the queue on-target.
Implies a small set of acon test commands (or debug hooks) to put/get/inspect from
the host; their shape settles during implementation. The full matrix — wrap splits,
fill/drain patterns, truncation, full/empty edges, ISR-context puts, lock behavior —
runs on real hardware through this path.

---

## W4 — promotion draft (priority put / put-to-front)

Drafted 2026-08-27 at the user's request; not yet promoted to numbered rows.
**Stated goal (user):** emulate FreeRTOS queue features at smaller scale --
`xQueueSendToFront` / `SendToBack` are the model. Context worth pinning:
FreeRTOS affords SendToFront cheaply because EVERY queue op runs in a critical
section and items are fixed-size, so front-insert is one slot-pointer
decrement under a mask already being paid. Phase 1's lock-free SPSC design is
exactly what makes it non-trivial here: a front-put writes the READ end,
breaking the single-writer-per-end property everything rests on.

Issues a phase-2 design must resolve, in dependency order:

1. **The architecture fork (Q -- needs user).** Two viable shapes:
   - *(a) Full front-put, FreeRTOS-style:* real ring-insert before the read
     position. Requires a lock covering BOTH ends -- see items 2-4. Per-queue
     opt-in confines the cost to queues that want it.
   - *(b) Front mailbox:* a single reserved priority slot (bounded payload)
     checked by get before the ring. Producers fill it under the existing
     producer lock; the consumer stays lock-free. Only one (or N, small)
     pending priority event, but the "urgent event jumps the line" use case
     is fully covered, and no phase-1 invariant is touched. Much lower risk.
2. **Lock contract escalation (S).** For shape (a): put_front must exclude
   producers AND the consumer. An ISR put_front preempting a main-loop get
   mid-extraction corrupts read-side state (get holds a local rd_idx and
   commits it after copying), so on a front-capable queue **get must take the
   lock too** -- the consumer's zero-masking guarantee dies on those queues
   (and only those). Likely rule: put_front is refused on a queue created
   without a lock pair.
3. **Free-space race from the other end (S).** A front-put allocates free
   bytes from the opposite end to a normal put; both check the same free
   count. Safe only if every space-consuming op on that queue runs under the
   lock -- another reason shape (a) is a per-queue mode, not a universal add.
4. **Counter-model bend (I).** Front-put moves the read side BACKWARD
   (`u32_bytes_read -= space`, `u32_rd_idx -= space`, both under the lock).
   The arithmetic (unsigned deltas) still holds, but "monotonic" stops being
   literally true on the read side, and an UNLOCKED reader of free space could
   momentarily OVERESTIMATE it -- fine only because item 2 forces all mutators
   under the lock; helpers degrade to snapshots on front-capable queues.
5. **Ordering among priority events (S).** FreeRTOS semantics: successive
   SendToFront are LIFO among themselves (each lands in front of the last).
   Adopt as-is, or document-and-accept? (Mailbox shape: define overwrite vs
   refuse when the slot is full instead.)
6. **Mechanics (I, low drama).** Insert address is (rd_idx - space) mod size;
   the record may split backward across the wrap -- the same two-stage memcpy
   computed from the other end. S1's rounding keeps alignment. Commit order
   inside the lock still wants the compiler barrier before release.
7. **HIL coverage (T).** New tests: front vs back ordering; front-put from
   the tick ISR while the host drains (the get-preemption case item 2
   exists for); LIFO stacking of multiple front-puts; front-put on a
   lock-less queue refused.

**Leaning:** none recorded -- the fork in item 1 is the user's call when
phase 2 starts, and everything downstream reshapes around it.

---

## Global notes

**Phase sketch:** phase 1 = core four functions + D4 helpers, SPSC + fn-pointer
locking, acon test commands + `test_eventq.py` HIL suite (T3) — **DONE,
bench-verified 2026-08-27**; phase 2 = W1/W4 non-consumptive get and priority
put as needed, W2 flush, automation-console phase-2 async events as first
customer.

**Code anchors (phase 1, 2026-08-27):**

- Module: `App/event_queue/event_queue.{c,h}` + `event_queue_config.h.example`;
  live config `App/Inc/event_queue_config.h`; include path added to both
  `.cproject` configs.
- Test harness: `App/{Inc,Src}/eventq_test.{h,c}` under acon op **`F`** (fifo);
  ISR producer hook `v_eventq_test_tick()` in `app_main.c`'s 1 ms timer
  callback. Host suite `scripts/hil/test_eventq.py`, 15 tests.

**Implementation deltas vs. the board** (all within granted freedom, recorded
for audit):

- The test op letter is `F`, not the `Q` sketched in chat — `Q` is the
  console's builtin QUIT and builtins cannot be shadowed.
- `create` on an already-live handle (magic present) is refused with
  `EQ_ERROR_PARAMETER`: re-creating would leak an owned malloc'd ring.
- A `u32_size` that is not a multiple of 4 is **rounded down** and reported
  with the new positive `EQ_STATUS_SIZE_ROUNDED` (queue live, handle's
  `u32_size` holds the net size); `EQ_ERROR_SIZE` only when the net size falls
  below the minimum. (Initially implemented as a hard reject; changed to
  round-down on user direction 2026-08-27, consistent with zero-means-default
  KISS. Preserves S1's records-stay-4-aligned property across wraps.)
- Allocator seam (user, 2026-08-27): the internal-buffer path allocates via
  `EVENT_QUEUE_MALLOC(size)` / `EVENT_QUEUE_FREE(ptr)`, overridable in the
  adoption header (adopter supplies any #includes); defaults to C-library
  malloc/free.
- The commit/release ordering (record bytes before counter advance) is held by
  a GCC compiler barrier (`asm volatile "" ::: "memory"`) — same GCC-extension
  precedent as D5's packed enum.
- `ACON_EMIT_MAX` raised 128 → 512 (project config, user-approved) so `F,G`
  can echo up to 200 payload bytes as hex.

**Plan status (2026-08-27):** 🟢 23 · 🟡 0 · 🔵 0 · 🔴 0 · W: W4 deferred
(W1/W2 implemented → S7, W3 dropped). Next IDs: D7, S10, I6, T4, W5. **Every
board row closed. Phase 1 complete: 17/17 HIL, regression nets green (acon
47/47, nvm 28/28), README written, design doc synced
(`../SwitchTester-Design.md` § "Event queue").**

**End of event-queue-plan.md**
