# Event path — decision log

**Feature:** the *proving subset* of roadmap task 2 — switch-transition events produced
into the vendored `event_queue` and consumed end-to-end, through both sinks. Built first,
ahead of tasks 1/3/4, so the event path's unknowns are retired before the expensive tasks
commit to them.

**Code home:** application-side event production in `App/{Inc,Src}/switch_out.*` — all
low-level switch manipulation stays in that one module (I7); consumption at
[automation_console.c:517](../../App/automation_console/automation_console.c:517)
(`v_acon_flush_events()`) and [app_main.c:390](../../App/Src/app_main.c:390)
(`v_app_polling_task()`). The queue itself is the already-vendored `App/event_queue/` and
is **not** modified by this work.

**Parent spec:** [`../SwitchTester-Design.md`](../SwitchTester-Design.md) (sync after
decisions land). **Entry point / source of scope:**
[`switchtester-roadmap.md`](switchtester-roadmap.md).

**Status:** PLANNING — **the producer side is fully specified and buildable** (mask, record,
queue instance, both hooks); the two sinks await their shape. See *Implementation readiness*.
**Working mode:** the user relays design in chat; one question at a time; the board holds
everything else. Per [`decision-log-model.md`](decision-log-model.md).

---

## Brief

Switch outputs already change level at two distinct points: a software choke point
(`v_switch_out_force()`, every manual set/toggle/pulse/all-off) and a hardware compare
match observed after the fact (`v_switch_cycle_advance()` / `v_switch_cycle_isr()`). This
feature turns both into records in one `event_queue` instance, gated by an
application-level production mask, and drains that queue from whichever sink currently
owns the console — the automation console when acon is active, the polling task and the
human-readable log when the debug menu is.

Scope is deliberately the **proving slice**, not all of task 2: switch events only, no
sense events (task 1 gates those), no per-cycle duty lists (task 4). What it must retire
is the set of unknowns every later task inherits — ISR-context enqueue cost, the deferral
rule, the arming model, and mode-handover behaviour.

**Where the board stands.** The producer half is fully specified — the production mask, the
record struct, the queue instance and both hook sites are locked. The consumer half (the
acon command surface, the human log line, and the two drain bodies) is still open and is
what the next relay should cover. See *Implementation readiness* below the Big Board.

---

## Big Board

| ID | Status | Subject (one line) |
|----|--------|--------------------|
| S1 | 🟢 | Mask **NVM default** is 0 / all-disabled; boot mask is the restored value (see S4) |
| S2 | 🟢 | Flags persist across handover; ONE mask set shared by both interfaces |
| S3 | 🟢 | Both surfaces control it — acon in this slice, debug-menu tooling eventually |
| S4 | 🟢 | Mask register **is** stored in and restored from NVM |
| S5 | 🟢 | Pool-label ownership check; label is `"SwitchTester"`; mismatch → wipe and re-default |
| S6 | 🟢 | Defer the mask's `x_nvm_get()` until after switch/sense init; register starts 0 |
| I1 | 🟢 | `event_control_t` — 12 source bits, cycle-complete at 30, global enable at 31 |
| I2 | 🟢 | ISR reads the mask as one `u32_all` snapshot, not as successive bitfield reads |
| I3 | 🟢 | Per-pool label check; only the main params pool is renamed `"SwitchTester"` |
| S7 | 🟢 | Emit on every level **request**, even a redundant one — no change-filtering |
| S8 | 🟢 | Drop reporting is the side-channel counter only — no synthetic in-band record |
| D1 | 🔴 | acon command surface — mask read/write, dedicated flush, async event frame format |
| D2 | 🔴 | Human log line format and its verbosity tier/class |
| I4 | 🟢 | Record struct (12 B) and the 16-bit event-type/class ID |
| I5 | 🟢 | Queue instance: static 8 KB buffer, created at init, never destroyed; lock-fn pair |
| I6 | 🔴 | Drain implementations — `v_acon_flush_events()` body, and the polling-task drain |
| I7 | 🟢 | No wrapper layer: header for types, stack record, direct `x_event_queue_put()` |
| I8 | 🟢 | TIM2 ISR order: capture `CCRx` → reschedule → put. 6 µs worst-case ISR budget |
| T1 | 🟡 | Skeleton: label `"UNNAMED"` + notice **DONE**; check back-port + W1 still pending |
| T2 | 🔴 | HIL coverage — extend `test_acon.py` or add a `test_events.py` |

---

## Implementation status — producer side BUILT (2026-08-30)

Written, compiles clean (0 errors, 0 warnings), **not yet flashed or bench-tested.**

| File | What landed |
|---|---|
| `App/Inc/app_events.h` **(new)** | `event_control_t` + bit macros, `switch_event_data_t`, `event_class_t`, `EVENT_TICK_MS()` seam, `EVENT_QUEUE_BUFFER_SIZE`, the `b_event_armed()` inline gate, both static asserts |
| `App/Src/switch_out.c` | 8 KB static queue buffer + handle, PRIMASK lock pair, `v_event_queue_init()`, `v_event_emit()`, both production hooks, the mask's NVM create / restore / save |
| `App/Src/app_main.c` | pool label → `NVM_POOL_LABEL`, `u8_nvm_label_matches()`, `v_nvm_reclaim_foreign_pool()`, the provenance check in `v_param_init()`, and the init ordering (`v_event_queue_init()` → `v_switch_out_init()` → `v_event_control_restore()`) |
| `App/Inc/device_config.h` | `NVM_POOL_LABEL`, derived from `PRODUCT_NAME` |
| `App/Inc/nvmparams_config.h` | `NVM_PARAM_EVENT_CONTROL`, appended last — the cycling-ID contiguity asserts still hold |

**Cost:** flash 68,608 B (13.1%), RAM 21,912 B (14.9%) — the 8 KB ring is the bulk of the
RAM delta and leaves 122.6 KiB free.

**`v_event_emit()` and `b_event_armed()` are `always_inline` (user, 2026-08-30).** The
wrapper is acceptable *provided it costs no call frame* — optimise this one operation for
speed over size, without touching the project's optimisation level.

> **Build-config note, checked 2026-08-30.** Both Debug and Release compile C at **`-Og`**;
> an earlier claim in this session that Release used `-Os` was wrong. The `-Os` in
> `.cproject` was on the **C++** compiler option, inert in a C-only project. It has since
> been set to `-Og` as well, so no `-Os` remains anywhere and the two defined build types
> are consistent. Release differs from Debug only in `-g3`/`-DDEBUG`: flash 66,296 B vs
> 68,608 B, RAM identical at 21,912 B.

**It was not free by default, and the measurement is the reason the attribute is there.**
At **`-Og`** — which both Debug and Release use — the compiler emitted both as real
functions, `v_event_emit` 64 bytes calling `b_event_armed` 100 bytes, so a **masked**
source, the common case during a soak, paid **two nested call frames just to learn it was
masked**.
With `__attribute__((always_inline))` on both, the symbols disappear and the gate compiles
to roughly five instructions:

```
ldr r3, [pc, #60]    ; &g_x_event_control
ldr r3, [r3, #0]     ; ONE load -- the I2 snapshot
cmp r3, #0
bge  <return>        ; global enable is bit 31 = sign bit, so a sign test
adds r0, r4, #4      ; channel + MANUAL_SHIFT
movs r2, #1
lsls r2, r0
tst  r2, r3          ; same word, no second read
bne  <build record>
```

Three things worth recording:

- **The class switch constant-folds away entirely.** `u16_class` is a literal at every
  production site, so no comparison against class values survives — it goes straight to the
  shift. This is what makes the (class, channel) → bit mapping free to keep in one place.
- **Bit 31 for the global enable pays off in codegen**, not just in "all-zero is disarmed":
  GCC tests it as the sign bit (`cmp`/`bge`) rather than with a mask-and-test.
- **Flash cost: zero.** Text is 68,608 bytes both before and after. The call sites grew
  (`v_switch_out_force` 56 → 108, `v_switch_cycle_advance` 212 → 328) by almost exactly
  what deleting the two out-of-line functions saved.

**`NVM_POOL_LABEL` is derived from `PRODUCT_NAME`** rather than being its own string: a
project forked from this one changes `PRODUCT_NAME` as a matter of course and inherits a
distinct pool label for free, which is precisely the property S5's check relies on.

**Not yet done:** anything sink-side (D1, D2, I6), HIL (T2), and no way yet to set the mask
at runtime — so the path is currently unexercisable end to end. That is expected; the mask
defaults to all-disarmed and nothing produces until a command surface exists.

---

## Implementation readiness

**What this is:** the gating view of the Big Board above — which open rows block writing
code, and which can follow it. Every entry is a Big Board ID; nothing lives only here.

**Buildable today, gated on nothing:** the whole mask subsystem (**S1–S6, I1–I3**) plus the
producer side (**S7, I4, I5**) — record struct, queue instance, both hooks and the
production gate.

| Gates code? | Rows | Note |
|---|---|---|
| **Blocks the sinks** | **D1, D2, I6** | The acon command surface, the human log line, and the two drain bodies. Producers can be written and unit-tested before these land |
| **Blocks nothing; safe default exists** | **S8, T2** | Drop reporting can start side-channel and gain an in-band record later; HIL follows the feature |
| **Open but non-blocking** | **T1** | A different repo; blocked on the label check existing here |

---

## Wish list (v2+)

| ID | Status | Subject |
|----|--------|---------|
| W1 | 🔵 | **nvmparams:** public "peek a pool's label" without full pool init — LOW priority |
| W2 | 🔵 | Separate mask sets for the human vs automation interfaces — deliberately deferred |
| W3 | 🔵 | **nvmparams:** opt-in label-match check inside `x_nvm_pool_init()`, reported by return code |

---

## LOCKED CONTEXT

Recorded in [`switchtester-roadmap.md`](switchtester-roadmap.md) and **not to be
re-litigated here.** Each entry names where its rationale lives.

**Sink model**

- **One queue, one consumer, selected by console mode (R2).** acon active → acon is the
  only consumer, drained at `v_acon_flush_events()`. Debug menu active → the polling task
  is the only consumer, with optional log emission. The single-consumer contract is
  satisfied structurally, by console ownership already being exclusive. No fan-out, no
  second queue, no router.
- S12's "must not share S6's gate" requirement is honoured for free — the sinks are
  separated by mode, not by a shared flag.
- The contract is **per queue**: a future subsystem may have its own.

**Production gating**

- **The gate is at PRODUCTION, not emission.** Masked sources never enter the queue.
- **Masked means masked — nothing is counted, anywhere.** Not in `event_queue` (which
  cannot see masks), not at application level. The mask borrows the MCU `IER`'s *shape*,
  not its status-flag semantics.
- **Draining is unconditional.** Whichever sink owns the console always drains; the
  "optional log messages" choice governs emission only.
- **Mask shape (I1, locked 2026-08-30, toolchain-verified):** `event_control_t`, a union of
  `u32_all` and an anonymous bitfield struct. Bits 0–3 switch auto, 4–7 switch manual,
  8–11 sense, 12–29 filler, **bit 30 `b_global_switch_cycle_complete_event`** (global, not
  per channel), **bit 31 `b_global_event_enable`**. Flags are `bool : 1`; the filler is
  `uint32_t : 18`, since a `bool` bitfield cannot exceed width 1. `_Static_assert` on
  `sizeof == 4`. Complementary bit-mask defines/enum accompany the bitfields.
  Aligned 32-bit load/store is atomic on Cortex-M0+, so console-writes / ISR-reads need no
  lock.
- **Event ID = the class alone (I4, locked 2026-08-30).** The roadmap's
  `(class << 8) | instance` proposal is **superseded**: the channel travels in the record
  payload, so the 16-bit event type that `event_queue` already carries holds the class only.
  The mask is deliberately finer-grained than the ID — per-channel bits, per-class IDs.

**Manual vs automated**

- **Manual switch operations get their own event-mask bit**, independent of the automated
  path (B3). Arming manual events without cycling events, or the reverse, is first-class.
- Two hooks, matching the two sources: `v_switch_out_force()` (software choke point, the
  write *is* the edge) and `v_switch_cycle_advance()` / `v_switch_cycle_isr()` (hardware
  already changed the output). Both live inside `switch_out` — no restructuring needed.

**Flush**

- **Flush is host-commanded**, via a dedicated acon command, not automatic at mode
  transitions. `x_event_queue_flush()` makes the command thin. Flush-on-entry and
  flush-at-command-start stay available but are not required.

**Timestamps**

- `TIM2->CNT`, 1 us, captured **at the event**, not at print time (S8 of the console
  plan). `HAL_GetTick()`'s 1 ms cannot resolve a switch bounce.

**Deferral and overflow (console plan S7)**

- Async frames only *between* response frames, never inside one. Overflow policy is
  drop + dropped-count.

**Superseded (R1)**

- The console plan's hand-rolled 8-byte `repl_event_t` ring and its ISR-priority
  discussion are **dropped** — `event_queue` supersedes them. Carried over: the gating
  concept, reset-on-entry, the dropped count, and "no formatting in ISR context".
  `repl_event_t`'s *shape* may still serve as a record payload.

**Already built — use, do not rebuild**

- `event_queue` phase 1, bench-verified, `test_eventq.py` 20/20. Public API includes
  `x_event_queue_flush()`, `u32_event_queue_dropped()` / `v_event_queue_dropped_reset()`,
  `u32_event_queue_puts()` / `v_event_queue_puts_reset()`, `u16_event_queue_count()`,
  `b_event_queue_is_empty()`, `u32_event_queue_free_space()`.
- `v_acon_flush_events()` exists as a deliberately empty call site, called from the
  executive loop at
  [automation_console.c:616](../../App/automation_console/automation_console.c:616).
- `JOB_CYCLE_COMPLETE` ([jobs.h:53](../../App/Inc/jobs.h:53)) already carries the channel
  in `u8_param1` — use it rather than a parallel path.
- `i_getline()` keeps the polling task pumped while a menu prompt blocks, so the
  human-side drain does not stall.
- **`jobs.c`'s approximate overflow counter is accepted as-is and is not to be
  "fixed"** — the roadmap records the user's reasoning.

**Mask persistence and default (S1 + S4, locked 2026-08-30)**

- **The mask register is stored in and restored from NVM.** Boot mask = last written value.
- **Its NVM default is 0 / all-disabled**, so a virgin pool produces nothing until
  something is explicitly armed.
- With the global enable at the top bit (I1), the armed-nothing state is the all-zero
  register — "cleared" and "safe" are the same value, zero-init reaches it, and the
  mask-everything recovery command is a single store of 0.
- One `uint32_t` parameter object holds `u32_all`. Appending its ID must preserve the
  contiguity contract asserted in `switch_out.c`.

**Boot state — verified from the code 2026-08-30 (user observation, confirmed)**

- **All switch outputs start off and no cycling is running.** `v_switch_out_init()`
  ([switch_out.c:280](../../App/Src/switch_out.c:280)) zeroes `u8_running` and
  `u32_cycles_done` per channel, forces the output low, then starts output compare.
- **A cycling campaign never resumes itself across a reset.** The persisted per-channel
  repeat / on / off values are restored as *parameters* only; `u8_running` is explicitly
  zeroed, so nothing restarts without a command.
- **Therefore no ongoing event traffic exists at boot, whatever the restored mask says** —
  production requires activity, and there is none until the operator or a host starts
  something. This is the main reason a persisted arm-everything mask is not the hazard it
  first appears to be.
- **Init order is `v_param_init()` then `v_switch_out_init()`**
  ([app_main.c:256](../../App/Src/app_main.c:256) and 264), so a mask restored during
  `v_param_init()` would be live before the init-path forced-off calls run.

**Boot sequence for the mask (S6, locked 2026-08-30)**

- The mask's **readback is deferred**, not the pool init: pool init as normal → force the
  register to 0 → switch / sense init → `x_nvm_get()` the register. The init-path
  forced-off calls therefore see an all-zero mask and produce nothing.
- Only the readback moves. `x_nvm_create()` / default provisioning stays in
  `v_param_init()` with every other parameter.
- Failure of the deferred get leaves the register at 0 — disarmed, the safe direction.

**Mask lifetime and control surfaces (S2 + S3, locked 2026-08-30)**

- **One mask set, shared by both interfaces.** Not per-context — a per-context set would
  require the active context to be known and checked at every event-logging site.
- **Flags persist across handover.** A menu-set mask is visible to a host entering acon
  afterwards; an acon session's mask survives its exit. With S4 that makes arming sticky
  across mode changes *and* resets, so clearing the register is the one recovery path.
- **Both surfaces control it, acon first.** Debug-menu control of event logging is planned
  but deferred; the acon path is in this slice. Guard the acon path, relax the menu path.

**Pool ownership (S5, locked 2026-08-30)**

- **This project's pool label is `"SwitchTester"`.** Labels must be unique across every
  project loadable on this board.
- **A foreign pool is not preserved.** Label mismatch → overwrite with a fresh,
  freshly-defaulted pool. The check is application-side; `nvmparams` treats the label as
  informational and never decides on it.
- Both projects currently say `"PARAMS"`, so today the label discriminates nothing.
  Skeleton getting its own unique label is a **back-port**, not a prerequisite for
  SwitchTester's protection.

**Guard policy**

- Guard the REPL / host-commanded paths and reject; leave the menu / human paths relaxed.
  SwitchTester is bench tooling, not a product.

---

## Detail sections

### S1 — Mask reset default *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Question:** Are all event sources masked at boot, or is some default set armed?

**Rationale (roadmap, 2026-08-27):** all-masked agrees with S7's worry that unconditional
reporting would flood the link during a soak run and overflow the queue continuously; a
plain boot then produces nothing, the queue stays empty and the drop count stays 0 until
something is explicitly armed — the same "costs nothing unless asked" posture as the flash
test pool. The failure mode it chooses is *"I forgot to arm it"* (nothing happens,
trivially diagnosed) over *"why is my link flooded?"* (which looks like a fault).

**Resolution (user, 2026-08-30 — amended same day by S4):** **the mask's NVM default value
is 0 / all-disabled. Locked.** S4 then makes the *boot* mask the value restored from NVM,
so this row now fixes the **virgin-pool default**, not the boot state: a pool that has
never held a mask creates one reading `0x00000000`, and nothing is produced until
something is explicitly armed. Thereafter the boot mask is whatever was last written.

Two properties still fall out and are worth keeping in I1's layout: with the global enable
at the top bit, zero-initialised storage is already the safe state, and "clear the
register" and "disarm everything" are the same operation — which is also what makes the
one-key mask-everything command the roadmap wanted a single store of `0`.

**Consequence:** the roadmap's warning that persistence "largely replaces" this decision is
now the live situation. An arm-everything mask survives a reset and comes back producing,
so the mask-everything control is not a convenience — it is the recovery path from a
flooded link. See S5 for the other half of that consequence.

---

### S2 — Mask lifetime across mode handover *(seeded from the roadmap; not locked)*

**Status:** 🟡 · **Needs user:** yes

**Question:** Does a mask set from one control surface survive entry to / exit from the
other? Specifically: is a mask set from the debug menu visible to a host that enters acon
afterwards, and does an acon session's mask persist after it exits?

**Resolution (user, 2026-08-30):** **event flags stay in place across a handover, and there
is ONE mask set shared by both interfaces.** A mask set from the debug menu is visible to a
host that enters acon afterwards, and an acon session's mask survives its exit.

**Why one set rather than two** (user's reasoning): a per-context mask set would require
the active context to be known and checked at every event-logging site. One set is the
quickest, simplest and most efficient path. The possibility of splitting them later is
banked as **W2**, explicitly not to be worked now.

**Consequences worth having written down:**

- **Arming is fully sticky.** It survives mode changes (this row) *and* resets (S4). The
  clear-the-register command is therefore the single recovery path for both, which is
  another argument for it existing early rather than as a convenience.
- **No conflict with the console plan's "reset on entry" rule.** That governed the
  *queue*, not the mask, and the queue's flush is host-commanded now (LOCKED CONTEXT). The
  mask and the queue have deliberately different lifetimes.
- **A host that arms sources and exits without disarming leaves them armed** for the human
  console. Mild, and visible — the menu side emits only if its emit option is on — but it
  is the behaviour a script author should expect rather than be surprised by.

---

### S3 — Control surfaces for the mask *(seeded from the roadmap; not locked)*

**Status:** 🟡 · **Needs user:** yes

**Question:** Is production masking controlled from acon only, or from both the debug menu
and acon?

**Resolution (user, 2026-08-30):** **both surfaces, over the one shared register (S2) —
with acon first and the debug-menu side deferred.** In the user's words, *"debug menu
tooling will eventually provide means by which event logging can be controlled."*

So the sequencing is settled rather than left open: **acon control is in this slice; menu
control is planned but later.** Per the guard policy, validate the acon path and reject;
leave the menu path relaxed when it arrives.

Because S2 makes the register shared rather than per-context, the two surfaces are two
views of one piece of state — the menu control, when built, configures the same bits a host
would, with no reconciliation between them.

---

### S4 — NVM-persisting the mask register *(resolved)*

**Status:** 🟢 · **Needs user:** no

**Question:** Is the production mask persisted in NVM?

**Resolution (user, 2026-08-30):** **yes — store and restore the mask register in NVM.
Locked.** The NVM default value is 0 / all-disabled (S1), so a virgin pool boots
producing nothing; every boot thereafter restores the last-written mask.

**What this pulls in:**

- **One new parameter object**, one `uint32_t`, holding `u32_all`. The union's whole point
  is that persistence sees a plain scalar — no per-bit storage, no serialisation.
- **The parameter-ID enum carries a contiguity contract guarded by `_Static_assert` in
  `switch_out.c`** (IDs are computed arithmetically). Appending the new ID must preserve
  it; the assert will catch a violation at build time.
- **Restore ordering.** The mask must be restored before anything can produce — i.e. in
  the same init phase as the other `v_param_init()`-backed defaults, ahead of TIM2 cycling
  and ahead of console availability. A source armed in NVM starts producing as soon as its
  hook can run, so a late restore means a window of wrong behaviour rather than merely a
  late one.
- **Write policy is unaddressed** — whether a mask change commits immediately or is
  deferred/explicit. Left open deliberately; it belongs with the acon/menu command surface
  rows that follow the shape relay.
- **The foreign-pool hazard is now live rather than hypothetical** — see S5.

---

### S5 — Foreign or stale pool restoring an armed mask

**Status:** 🟡 · **Needs user:** yes

**Question:** What, if anything, protects the restored mask from a pool this firmware did
not write?

**Why it is open now:** S4 turned a documented hazard into a reachable one. `.nvmdata` is
`NOLOAD` and survives reflashing, and this bench is **shared with `G0B1_Skeleton`**, so a
pool written by other firmware can be read as ours — this has already happened once in
this project (the SWITCH_A/B cycling defaults came up as garbage in 2026-08-02 for exactly
this reason). A garbage mask decodes as "some or all sources armed", including the global
enable, so the failure presents as a flooded link at boot.

**It is the loud failure rather than the quiet one**, which is genuinely mitigating: it is
self-announcing and S1's clear-the-register recovery is one command. But it lands on a
host script that may be mid-campaign, and the drop counter would be recording real drops
against events nobody armed.

**Two mitigations are already on the table in the design doc**, and they are complementary
rather than alternatives:

1. **Per-project `.nvmdata` regions** — prevention, linker-script only, so the siblings
   never share an address in the first place.
2. **The pool-label ownership check** — detection, catches *any* foreign pool rather than
   only the sibling's.

**Resolution (user, 2026-08-30):** **the pool-label ownership check, with unique labels per
project. Locked.** In the user's framing: NVM pools carry a string label; check it during
or after pool init; keep labels unique across every project that can be loaded on the same
board. **A pool from a different project is explicitly not worth preserving** — on
mismatch, overwrite it with a fresh, freshly-defaulted pool.

**This project's label is `"SwitchTester"`** — 12 characters, zero-padded, inside the
16-character field.

**Verified against the code (2026-08-30), and one finding changes the picture:**

- `NVM_LABEL_MAX_LENGTH` is **16** ([nvmparams_config.h:34](../../App/Inc/nvmparams_config.h:34)),
  so 12 characters fit with room to spare. `x_nvm_format_block()` writes the label with
  `strncpy(header->c_label, label, 16)`
  ([nvmparams.c:356](../../App/nvmparams/nvmparams.c:356)), and `strncpy` zero-fills the
  remaining bytes — so "12 chars, 0-padded" is exactly what lands on media. At 12 < 16 the
  stored label is also NUL-terminated, which the module explicitly does *not* guarantee for
  a full-width label.
- The module treats the label as **informational only and never decides on it** — the
  header field is commented *"Informational only; not NUL-guaranteed"* and the app-side
  config repeats *"nvmparams never makes a decision on it."* So the check is
  application-side by design, exactly as the user proposed. No module change needed.
- **No init policy can substitute for it.** `NVM_INIT_FORMAT_IF_BLANK` (the default) and
  `NVM_INIT_FORMAT_IF_INVALID` both key on *validity*, and a foreign pool is perfectly
  valid — right signature, self-consistent contents. This is the same reason CRC cannot
  catch it. Provenance is unreachable except through the label.
- **The finding: SwitchTester and Skeleton currently use the identical label `"PARAMS"`**
  — [app_main.c:92](../../App/Src/app_main.c:92) here and `app_main.c:83` in
  `G0B1_Skeleton`. The label discriminates *nothing* today, between exactly the two
  projects that share this bench and this `.nvmdata` address. That is the 2026-08-02
  incident's collision, still fully open.
- **SwitchTester is protected by its own change alone.** Once this pool says
  `"SwitchTester"` and Skeleton's still says `"PARAMS"`, the labels differ and the check
  discriminates. Giving Skeleton a unique label of its own is what protects *Skeleton*, and
  is a natural back-port rather than a prerequisite here.
- **The wipe-and-re-default mechanism already exists.** `v_debug_nvm_erase()`
  ([debug_menu.c:657](../../App/Src/debug_menu.c:657), menu `[N]`) memsets the pool and
  resets so every parameter is recreated from its default. The mismatch handler should
  reuse that path rather than invent a second one.

---

### S6 — Boot-time event production from the init-path forced-off calls

**Status:** 🟢 · **Needs user:** no

**Context (user observation, 2026-08-30, verified against the code):** at startup all
switch outputs are off and no IRQ-driven pseudo-PWM is running, so nothing produces events
until the operator or a host starts something. **The operator will not see event spam.**
That is confirmed — see LOCKED CONTEXT's boot-state entry — and it substantially defuses
the worry S1/S4 raised about a persisted arm-everything mask flooding the link at boot.

**But there is a small, bounded exception.** `v_switch_out_init()` calls
`v_switch_out_force(u8_channel, 0)` once per channel
([switch_out.c:280](../../App/Src/switch_out.c:280)), and that function is exactly where
B3 places the manual-event hook. Since `v_param_init()` runs *before* `v_switch_out_init()`
([app_main.c:256](../../App/Src/app_main.c:256) vs 264), a mask restored with manual bits
armed is already live when those four calls happen. Result: **four "switch → off" manual
events at every boot**, before the operator has done anything.

Bounded and harmless in volume — but it is a puzzling preamble in a host script's first
read, and it reports transitions that are arguably not transitions at all (the outputs were
already off; init is asserting a known state, not changing one).

**Options considered:**

1. **Create the event queue after `v_switch_out_init()`.** The puts then hit an
   uninitialised queue, `x_event_queue_put()`'s init-magic guard rejects them with
   `EQ_ERROR_NOT_INIT`, and nothing is counted (only `EQ_ERROR_FULL` counts as a drop).
   Works, but it is correct *by accident* — someone reordering init later silently gets the
   four phantom events back.
2. **An explicit "event production live" flag**, set once at the end of application init and
   tested by the production layer. Note this **cannot be the mask's global enable bit**,
   which is persisted and may well come back set — it has to be a separate internal flag.
   Sturdier and self-documenting, and it covers any init-time force added later.
3. **Have the init path bypass the hook** via a lower-level force that does not emit. Clean
   at the call site, but it introduces a second way to change a switch level, which cuts
   against B3's whole point that `v_switch_out_force()` is the single choke point.

**Earlier leaning, superseded:** option 2 (an explicit "production live" flag), with
option 1's ordering as belt-and-braces.

**Resolution (user, 2026-08-30) — a fourth option, better than any of the three above:
DEFER THE MASK'S READBACK, not the pool init.** The boot sequence becomes:

1. NVM pool init as normal.
2. **Force the event control register to 0** (in RAM).
3. Do any needed switch / sense initialisation.
4. **`x_nvm_get()` the event control register.**

The four forced-off calls in step 3 therefore run against an all-zero mask and produce
nothing, after which the persisted arming takes effect.

**Why this beats option 2:** it reuses the mask's own disarmed state as the suppression
mechanism rather than adding a second, independent way to say "not yet". S1 already locks
all-zero as "nothing armed", so a separate flag would duplicate a meaning the register
already carries — and two gates mean two things to keep in agreement. This adds no new
state at all.

**Properties worth recording:**

- **B3's single choke point survives** — no bypass path, no lower-level force (option 3).
- **Fail-safe.** If step 4's `x_nvm_get()` fails for any reason the register stays 0, which
  is the disarmed state. The failure mode is "nothing is armed", not "everything is".
- **Covers sense for free.** Task 1's sense init lands in step 3 and inherits the same quiet
  window with no further work.
- **Creation and defaulting stay put.** Only the *readback into the RAM register* moves; the
  object's `x_nvm_create()` / default provisioning stays in `v_param_init()` with every
  other parameter.
- **Compatible with I3's ordering.** S5's label check still runs immediately after pool
  init, ahead of every `x_nvm_get()` including this deferred one — the two constraints nest
  rather than conflict.

**One implementation note, to protect the ordering from a later reorder:** put step 4 in a
named function (e.g. `v_event_control_restore()`) called explicitly from the init sequence
rather than inlining a bare `x_nvm_get()`. The dependency then reads as a deliberate step in
`app_main()`'s init list instead of a line whose position happens to matter, and the comment
explaining why it is late has an obvious home.

---

### S7 — `v_switch_out_force()` writes unconditionally: events for non-transitions

**Status:** 🟡 · **Needs user:** yes

**The finding:** `v_switch_out_force()` ([switch_out.c:89](../../App/Src/switch_out.c:89))
rewrites `OCxM` **without checking the current level**. It is the choke point for *level
writes*, not for *level changes* — so hooking it emits "transition" events for things that
are not transitions. Three concrete cases, only the first of which S6 covers:

| When | What happens | Genuine transition? |
|---|---|---|
| Boot | `v_switch_out_init()` forces all four low; they are already low | No — S6 handles it, but only at boot |
| **Campaign end** | `advance` → `v_switch_cycle_halt` → `force(ch, 0)` while the output is **already low** — the code says so: *"the output is already low, so the run ends released"* | **No** — yet it would emit a manual event alongside the genuine cycle-complete |
| **Manual set on a cycling channel** | `v_switch_out_set()` calls `v_switch_cycle_stop()` (→ halt → `force(0)`) and then `force(u8_on)` | **One user action, two manual events** |

The second and third recur in normal operation, so unlike S6 they cannot be handled by an
init-time quiet window.

**Proposed fix:** emit only when the level actually changes — compare the requested level
against the live pin before writing. `u8_switch_out_level_bitmap()`
([switch_out.c:400](../../App/Src/switch_out.c:400)) already reads GPIO `IDR` for exactly
this purpose, and the module documents that as *"the more honest measurement for a tester:
what the pin is doing, not what it was told to do"*. Reading `IDR` **before** the write
sidesteps any pin-update latency.

**What it buys:** "switch transition event" comes to mean what it says; the campaign-end
and manual-set duplicates disappear; and it **subsumes S6's boot case**, since the four
init-time forces stop being changes rather than merely being masked. S6's deferred restore
stays worth keeping regardless — it is one line and it protects anything added to the init
path later.

**Cost:** one register read and a comparison per level write, in paths that already do a
peripheral write. Negligible, and it is only on the hook path.

**Resolution (user, 2026-08-30):** **no filtering — emit an event whenever a level change
is REQUESTED, even if the output already sits at that level.** Simplest, and the record
carries the requested state as a member (I4), so a consumer that cares can see what was
asked for and dedupe against the previous record itself.

So the semantics are **"a level was commanded"**, not "the pin changed". Worth stating
plainly because it makes the following **expected behaviour, not defects**:

- **Campaign end emits two records** — a manual "off" from `halt`'s `force(ch, 0)` on an
  already-low output, plus the genuine cycle-complete.
- **A manual set on a cycling channel emits two manual records** — `force(0)` from the
  implicit `v_switch_cycle_stop()`, then `force(level)`.
- **Boot would emit four**, were it not for S6's zero mask during init — which remains
  worth keeping precisely because this row does not filter.

**Consequence for the drain and the log:** a host or reader may see consecutive records with
the same channel and the same state. That is information (the firmware asserted a level),
not noise to be suppressed downstream — and it is the behaviour a bench instrument arguably
wants, since "the firmware commanded low here" is a real event.

---

### I1 — Mask register type: bitmapped union-struct

**Status:** 🟡 · **Needs user:** yes (one residual — the cycle-complete bit)

**Question:** What C type holds the production mask, and which sources get bits?

**Locked in this row (user, 2026-08-30):**

- Typedef name is **`event_control_t`**, not `switch_event_control_t` — the register covers
  sense and future classes too.
- **`_Static_assert` on the typedef's size.**
- **Bit-mask defines / an enum complementing the bitfields**, usable against `u32_all`.
- **Four sense bits are allocated now** (the "anything I overlooked" slot was the sense
  masks), even though task 1 gates their producers.
- Layout is **stable for now** and expected to change only as later roadmap items land.

**User's proposed shape (2026-08-30, explicitly illustrative and incomplete):** a union of
a `uint32_t` alias and an anonymous bitfield struct — per-source `_auto` / `_manual` bits
from bit 0 up, an unused filler field, and the **global enable in the top bit**:

```c
typedef union
{
    uint32_t u32_all;
    struct
    {
        bool b_switch_a_auto_events   : 1;
        ...
        bool b_switch_a_manual_events : 1;
        ...
        // --- anything I overlooked ---
        uint32_t _u32_unused          : x;   // x = 32 - bits allocated
        bool b_global_event_enable    : 1;
    };
}
switch_event_control_t;
```

**This is the right shape and it matches house precedent.** `spiflash_status_reg_t` /
`spiflash_config_reg_t` in [MX25R80.h:50](../../App/Inc/MX25R80.h:50) are exactly this
pattern — scalar `all` alias, anonymous struct, `PACKED`, filler fields named `_fillN`.
The named-bit view gives readable production tests in the ISR; the `u32_all` view gives
the single aligned 32-bit access the lock-free story in LOCKED CONTEXT depends on. Global
enable at bit 31 also makes S1's locked all-off default the all-zero word.

**The filler field cannot be `bool` — measured, not assumed (2026-08-30).** The user asked
whether a multi-bit filler typed `bool` actually reserves the stated number of bits. It
does not, and it does not fail quietly either. Compiled against the project's own
toolchain (`arm-none-eabi-gcc` 14.3.1 from CubeIDE, `-mcpu=cortex-m0plus -mthumb
-std=gnu11 -O2 -Wall -Wextra`):

```
error: width of '_b_unused' exceeds its type
   13 |         bool _b_unused        : 19;
```

A `bool` bitfield is limited to width 1; anything wider is a **hard compile error**. So the
question can never be answered wrongly by accident — but the filler does have to carry a
different type.

**Mixing container types is fine here — also measured.** An earlier note in this row
warned that mixing `bool` and `uint32_t` bitfields risks AAPCS container-boundary padding.
That caution does not apply to this layout, and the record is corrected rather than
quietly dropped, because it changes which option is worth taking. Three variants were
compiled and their constants read out of `-O2` assembly:

| Variant | `sizeof` | bit 0 | bit 11 | bit 31 |
|---|---|---|---|---|
| 32 x `bool : 1` | 4 | `0x00000001` | — | `0x80000000` |
| uniform `uint32_t` (12 flags + `:19` filler + global) | 4 | `0x00000001` | `0x00000800` | `0x80000000` |
| **mixed** — 12 x `bool : 1` + `uint32_t : 19` filler + `bool : 1` global | 4 | `0x00000001` | `0x00000800` | `0x80000000` |

All three produce the intended linear LSB-first packing in one word.

**Recommendation, and it matches the user's own rule** (*"if there are any fields ... that
require non-bool representation ... they need to be typed appropriately"*): keep every
**flag** as `bool : 1` — which is what the sketch wanted and what reads best at the use
site — and type the **filler** `uint32_t`, since it is the one field that is not a flag
and cannot be `bool`. The type difference then marks the padding as padding.

**Two mechanical notes that stand unchanged:**

1. **Assert the size** — `_Static_assert(sizeof(event_control_t) == 4, ...)`, agreed by the
   user. It catches any future field that breaks packing at build time. The repo already
   leans on `_Static_assert` this way (`event_queue.h`, `switch_out.c`'s ID contiguity
   contract).
2. **Bit numbering is a host-visible contract.** If an acon command writes the mask as a
   raw 32-bit value, the Python side must agree with GCC's LSB-first allocation. The
   agreed bit-mask defines / enum are what the host driver and HIL suite should reference,
   so a later insertion is a one-place change rather than a hunt for literals.

**Bit allocation.** Twelve bits are settled: 4 switch x {auto, manual} = 8, plus 4 sense.
Global enable at bit 31. That leaves 19 filler bits.

**Resolution (user, 2026-08-30):** **cycle-complete gets a bit — but ONE GLOBAL bit, not
one per channel.** The proposal for four per-channel bits was declined: cycle-complete does
not need per-channel granularity, so `b_global_switch_cycle_complete_event` applies to all
channels and sits **immediately above** the global enable, at **bit 30**.

**The rationale (user, 2026-08-30), which generalises beyond this bit:** per-channel *mask*
granularity is unnecessary here because the *record* already carries the channel — see
**I4**, where the originating switch channel is one of the payload's minimum members. So
the mask answers "do I want this class of event at all" and the payload answers "which
channel was it". Splitting a class into per-channel bits is only worth doing when a host
would plausibly want some channels and not others, which is true of transitions during a
soak and not true of campaign completions.

That keeps the two "global" controls adjacent at the top of the register and leaves the
low bits as a clean per-source block, which is easier to read as a bitmap than a scheme
where a class is split across both ends. 12 source bits allocated, 18 filler.

**Final layout — verified against the toolchain**, not just reasoned about. Compiled with
`arm-none-eabi-gcc` 14.3.1, `-mcpu=cortex-m0plus -mthumb -std=gnu11 -O2 -Wall -Wextra`:
`_Static_assert(sizeof(event_control_t) == 4)` passes, no warnings, and each named bit
constant-folds to the intended mask.

| Bits | Field | Verified mask |
|---|---|---|
| 0–3 | `b_switch_{a,b,c,d}_auto_events` | bit 0 = `0x00000001` |
| 4–7 | `b_switch_{a,b,c,d}_manual_events` | bit 7 = `0x00000080` |
| 8–11 | `b_sense_{a,b,c,d}_events` | bit 8 = `0x00000100`, bit 11 = `0x00000800` |
| 12–29 | `_u32_unused : 18` | — |
| 30 | `b_global_switch_cycle_complete_event` | `0x40000000` |
| 31 | `b_global_event_enable` | `0x80000000` |

Flags are `bool : 1`; the filler is `uint32_t : 18` because a `bool` bitfield cannot exceed
width 1. All-zero remains the disarmed state (S1).

**Still unallocated, deliberately:** **queue-overflow / drop notification**, if the in-band
synthetic record the roadmap raises (the `jobs.c` `JOB_QUEUE_OVERFLOW` precedent) is
wanted. That is a produced record too, so it would need either a bit or a deliberate
exemption. Not decided; 18 filler bits are available for it.

---

### I2 — ISR reads the mask as one snapshot

**Status:** 🟡 · **Needs user:** no (flagging, not asking)

**Question:** How does the producer side read the mask, given the console writes it
concurrently?

**Why it matters:** LOCKED CONTEXT establishes that an aligned 32-bit load/store is atomic
on Cortex-M0+, so no lock is needed. That guarantees each *access* is clean — it does not
guarantee two accesses see the same register. A production test written as
`if (x_mask.b_global_event_enable && x_mask.b_switch_a_auto_events)` is two loads, and a
console write landing between them yields a combination that never existed.

**Resolution (2026-08-30) — taken as an implementation note, no user decision needed:** the
ISR-side test takes one `uint32_t u32_m = x_mask.u32_all;` snapshot and tests bits out of
the local. The register is `volatile` so the compiler cannot hoist or split the read, and
the console side writes whole words (read-modify-write of the named fields in main context,
single store of `u32_all`).

There is no real alternative here — it is what makes the atomicity claim hold at the use
site rather than only at the instruction — so it is recorded as settled rather than parked
as a question. Reopen if the production-layer design gives a reason to.

---

### I3 — Label check: placement, comparison, pool scope

**Status:** 🟡 · **Needs user:** yes (pool scope)

**Question:** Where does the check run, how does it compare, and which of this project's
pools carry a project-unique label?

**Placement.** After `x_nvm_pool_init()` — which is what fills the RAM pool from media —
and **before any `x_nvm_get()`**, including the mask's. S4 already requires the mask to be
restored ahead of anything that can produce; the label check has to sit ahead of *that*,
or a foreign mask gets used for the window between. So the init order is: pool init →
label check (wipe + re-default on mismatch) → `v_param_init()` → everything else.

Blank media needs no special case: init formats it and writes *our* label, so the check
passes trivially on a virgin board.

**Comparison.** `memcmp` across the full 16-byte field against a zero-padded reference,
rather than a prefix compare. `strncpy` guarantees the padding, so the full-field compare
is well-defined, and it rejects a foreign label that merely shares a prefix. A garbage or
truncated label fails it too, which is the wanted behaviour.

**Reading the label.** `nvm_list.c` gets at it as
`(const nvm_header_t *) p_x_pool->p_v_data`
([nvm_list.c:53](../../App/Src/nvm_list.c:53)), so the precedent for an app-side read
exists and no new API is strictly needed. A small accessor in the module would be tidier
than two adopters reaching into `p_v_data` — but that is a vendored-module change, so it
would land in Skeleton first and be re-vendored. Not proposed for this slice; noted so the
choice is deliberate.

**Shape: per-pool, not global.** Write the check as a helper taking the pool and its
expected label — `(p_x_pool, p_c_expected)` — called once per pool that warrants one.
`nvmparams` manages multiple pools across multiple devices (this project runs four over
three media), so a routine that implicitly means "the pool" would be wrong here and would
have to be rewritten on back-port. See T1.

**Residual — pool scope.** This project configures four pools, and the user's rule
("unique labels for each project that can be loaded on the same board") does not obviously
apply to all of them equally:

| Pool | Label today | Where | Exposure |
|---|---|---|---|
| main params | `"PARAMS"` | internal flash `.nvmdata` | **The collision.** Holds the mask. Becomes `"SwitchTester"` |
| `"PARAMS-RAM"` | `"PARAMS-RAM"` | RAM | Volatile — nothing survives to be mistaken for ours |
| `"TESTPOOL"` | `"TESTPOOL"` | test fixture ([nvm_test.c:44](../../App/Src/nvm_test.c:44)) | Test scaffolding |
| `"FLSHPOOL"` | `"FLSHPOOL"` | W25Q SPI flash ([nvm_test.c:67](../../App/Src/nvm_test.c:67)) | Persistent, but on a part Skeleton does not currently drive |

**Resolution (user, 2026-08-30 — "take your leanings"):** **rename only the main params
pool to `"SwitchTester"`; the other three keep their current labels and are not checked.**

It is the one pool holding persisted state on media a sibling project writes at the same
address, which is the entire hazard. The RAM pools cannot carry anything across a reflash;
the two test pools are fixtures whose contents are disposable by intent, and the SPI-flash
test pool is not auto-inited.

Because the check is a per-pool helper, this is a decision about **call sites, not
architecture** — adding `"TESTPOOL"` or `"FLSHPOOL"` to the checked set later is one extra
call each, with no redesign. That is the main reason taking the narrower option now costs
nothing.

---

### T1 — Skeleton back-port: placeholder label, and the check itself

**Status:** 🟡 · **Needs user:** yes (timing — nothing in `G0B1_Skeleton` has been touched)

**Decided in principle (user, 2026-08-30):** `G0B1_Skeleton`'s pool label becomes
**`"UNNAMED"`**, with a conspicuous notice that an adopter must change it to something
project-specific. The rationale is Skeleton's purpose: it is a starting point — a
minimalist framework for new projects on compatible hardware, with the user's preferred
APIs already wired in — so its defaults are meant to be filled in, not shipped.

**Why the placeholder beats the status quo.** `"PARAMS"` reads as a real answer, so nobody
changes it; `"UNNAMED"` reads as an unfilled blank. It also fails safe against S5's check:
an `"UNNAMED"` pool mismatches any named project and is therefore wiped rather than adopted.
The residual case — two *both-unnamed* projects sharing a board — is the one it cannot
discriminate, which is an argument for making the placeholder hard to leave in place rather
than an argument against it.

**Making "conspicuous" real, since a comment is a documentation control.**

**A config-header `NVM_POOL_LABEL` macro was proposed and is REJECTED (user,
2026-08-30).** It assumes a single pool per adoption. `nvmparams` manages **multiple pools
across multiple physical devices**, so the label is per-*pool* data, not a per-*adoption*
constant — and it already lives in the right place, `nvm_pool_config_t.p_c_label`,
supplied per pool at init. Promoting it to the adoption header would demote a per-pool
field to a project-wide one and lose expressiveness for nothing.

**This repo disproves the assumption by itself** — four pools over three distinct media:

| Pool | Driver | Media |
|---|---|---|
| main params | `x_nvm_drv_stm_flash_*` | internal flash `.nvmdata` |
| `"PARAMS-RAM"` | `x_nvm_drv_ram_*` | RAM |
| `"TESTPOOL"` | `x_nvm_drv_ram_*` | RAM (test fixture) |
| `"FLSHPOOL"` | `x_nvm_drv_spiflash_*` | W25Q128 SPI flash |

**What replaces it — the runtime check, which scales to N pools for free.** S5's label
check already visits each pool it is given, so it is the natural place to notice a
placeholder: any pool whose label is still `"UNNAMED"` gets a loud boot-time line. That
covers every pool on every device with no per-pool build machinery, and it degrades
correctly as pools are added.

**If a build-time nag is still wanted**, the honest form is a single adoption-level
*checklist* flag that makes no claim about pool count — e.g. `NVM_POOL_LABELS_REVIEWED 0`
driving a `#warning` until the adopter clears it. That asserts "the adopter did the label
pass", which is genuinely one fact per adoption, rather than pretending there is one label.
Secondary to the runtime notice, and optional.

**Consequence for the back-ported check.** It must be a **per-pool helper** — something of
the shape `x_nvm_check_label(p_x_pool, p_c_expected)` called once per pool the adopter
cares about — not a global "check the pool" routine. Same reasoning: multi-pool,
multi-device is the module's actual contract, and the SwitchTester-side implementation
(I3) should be written to that shape from the start so the back-port is a move rather than
a rewrite.

**The larger item: the label check itself probably belongs in Skeleton**, not only here.
It is generic application-side boilerplate that every clone wants, and under the
phased-migration rule the back-port is what actually tests whether the seam is right. If it
lands only in SwitchTester, every future project re-derives it — which is the failure mode
the sharing model exists to prevent.

**Sequencing note.** None of this is a prerequisite for SwitchTester: naming this pool
`"SwitchTester"` protects this project on its own (S5). T1 is what protects Skeleton and
everything cloned from it.

**Status of the work — partially DONE (2026-08-30).**

**Done, committed in `G0B1_Skeleton` as `094c89c`** (local; not pushed at time of writing):

- `x_nvm_param_config.p_c_label` in that repo's `app_main.c` is now **`"UNNAMED"`**, with
  the reasoning at the config site under a `>>> CHANGE THIS WHEN YOU ADOPT THE SKELETON <<<`
  banner.
- A new **"Adopting this skeleton — change these first"** section at the top of Skeleton's
  `README.md`, immediately after the feature list and ahead of everything procedural,
  carrying the same reasoning for an adopter who never opens `app_main.c`.
- The optional `NVM_POOL_LABELS_REVIEWED` build-time nag was **not** added — it was
  described as secondary and optional, and its natural partner is the runtime notice, which
  needs the check to exist first.

**Still pending, and deliberately so:**

- **The label check itself.** It does not exist in SwitchTester yet, so there is nothing to
  back-port. It follows once I3 is built, as a per-pool helper.
- **W1**, the label-peek enhancement, which travels with that same visit.

**Resolution:** _(partial — the placeholder label and notice are done; the check back-port
and W1 remain, both blocked on the SwitchTester-side implementation)_

---

### I4 — Event record payload

**Status:** 🟢 · **Needs user:** no

**RESOLVED (user, 2026-08-30) — the record struct and the ID scheme.**

```c
typedef struct
{
    uint8_t  u8_channel;    /* 0 = A .. 3 = D; switch or sense               */
    uint8_t  u8_pad;        /* reserved                                      */
    uint16_t u16_state;     /* normally boolean 0/1, widened to 16 bits so an
                             * ADC reading can go here (SENSE_D or any analog
                             * input taken through the ADC)                  */
    uint32_t u32_tim_count; /* TIM2: CCRx for automated, CNT for manual      */
    uint32_t u32_tick;      /* EVENT_TICK_MS()                               */
}
switch_event_data_t;
```

**Exactly 12 bytes, naturally aligned, no implicit padding** — the `uint8/uint8/uint16`
group fills one word and the two `uint32_t`s follow on their own boundaries. Worth an
accompanying `_Static_assert(sizeof(...) == 12)`; `event_queue` rounds a stored record up
to a multiple of 4, so 12 stays 12. With the module's 4-byte header that is **16 bytes on
the wire per event** — the figure I5 sizes against.

**Why `u16_state` is 16 bits** (user): so a sense channel can put an ADC reading in the same
field a switch uses for 0/1, rather than needing a second record shape. That is what lets
sense reuse this struct when task 1 lands.

**The event ID is the class, and the channel is NOT in it (user, 2026-08-30).**
`event_queue` already carries a fixed 16-bit **event type** ahead of the 16-bit length —
part of the queue's own record header, not of the payload. That field is the **class**:
manual switch change, auto-driven switch change, end-of-cycle, and so on, *"for the most
part aligning with the event enable classes"*.

**This supersedes the roadmap's `(class << 8) | instance` proposal.** The instance does not
belong in the ID because the channel already travels in the payload — the same reasoning
that made cycle-complete a single global mask bit (I1).

**Note the deliberate asymmetry:** the mask is *finer* grained than the event ID. Switch
transitions have per-channel mask bits (0–3 auto, 4–7 manual) but only two class IDs. The
production site knows both the class and the channel, so selecting the mask bit from
`(class, channel)` is trivial there — and it keeps host-side filtering by class cheap while
leaving per-channel arming available.

**User's stated minimum set (2026-08-30)** — expected to apply to all switch-related
events, with room to grow:

1. **The 1 µs free-running counter value.**
2. **A system or application tick count.**
3. **The switch channel that initiated the event.**

**Resolved on (1) — it is TIM2** (user, 2026-08-30: *"whatever TIM is presently wired up to
the switch outputs"*). Verified: the channel map holds `&TIM2->CCR1..CCR4`
([switch_out.c:43](../../App/Src/switch_out.c:43)), `v_switch_out_force()` rewrites TIM2's
`OCxM`, and `v_switch_cycle_schedule()` compares against `TIM2->CNT`. There is no TIM4 in
this project. This also agrees with S8, which chose `TIM2->CNT` independently.

**Refinement — for automated edges, `CCRx` is the exact stamp and `CNT` is not.** The
switch outputs are placed *by the hardware* at compare match, so by the time
`v_switch_cycle_advance()` runs, `TIM2->CNT` is already edge-time **plus ISR latency**. The
exact edge time is the compare value that caused it — and it is still readable, because
`v_switch_cycle_schedule()` overwrites `*(p_x_map->p_u32_ccr)` only at the *end* of
`advance`. Reading it at the top of the ISR costs nothing and is exact.

So the one record member has two correct sources, matched to the two hooks:

| Hook | Source | Why |
|---|---|---|
| `v_switch_out_force()` (manual) | `TIM2->CNT` | the software write **is** the edge, so now is exact |
| `v_switch_cycle_advance()` (automated) | `*(p_x_map->p_u32_ccr)`, read before rescheduling | the hardware placed the edge; `CNT` would carry ISR latency |

Both are the same counter domain — same units, same wrap — so this stays a single field, and
it delivers what S8 actually asked for ("captured at the event") rather than
"captured when the ISR got there".

**Resolved on (2) — `HAL_GetTick()` for now, behind a seam** (user, 2026-08-30). Roadmap
**B2** established that TIM14 already provides the 1 ms periodic interrupt but that
**nothing counts in it**, so the HAL's `uwTick` is the only tick that exists today.
Building B2's counter is explicitly *not* being done first; instead the member is wired to
`HAL_GetTick()` in a way that can be changed trivially later.

**Shape — a macro with a default, which is strictly more flexible than an inline
function.** This is the same seam roadmap **B1** identified as the better pattern for
`logging`'s timestamp, for the same reason: a macro lets an adopter point straight at an
existing function with no wrapper and no port file.

```c
/* Event timestamp seam. Retarget here -- e.g. to B2's application tick.
 * Must be cheap and ISR-safe: called from the TIM2 and TIM14 handlers. */
#ifndef EVENT_TICK_MS
#define EVENT_TICK_MS()     HAL_GetTick()
#endif
```

Redirecting to B2's counter later is then a one-line change in one header, with no producer
touched. `HAL_GetTick()` is a single aligned 32-bit read of `uwTick`, so it is safe from
either ISR.

**Two caveats worth recording, neither blocking:**

1. **The two timebases are independent, not one clock at two resolutions.** `uwTick` comes
   from SysTick; the other member comes from TIM2. They free-run separately and drift, so a
   host must not try to derive one from the other. B2 asked exactly this question and the
   answer, for now, is "two independent timebases". Each still does its own job — µs
   resolves edges, ms orders a long run.
2. **`uwTick` is writable, and this project writes it.**
   [debug_menu.c:113](../../App/Src/debug_menu.c:113) calls
   `v_system_tick_add(u32_in_sleep_time)` after the RTC/STOP wake-up self-test, to credit
   back the time spent asleep. So the tick member can **jump forward in one step** across
   that test, while TIM2 — stopped during STOP1 — does not. Unlikely to collide with a
   switch campaign in practice, but it is exactly the sort of discontinuity that would
   otherwise burn an hour later. B2's own objection to `uwTick` was that it is HAL-owned;
   reading it is fine, but this is what that ownership looks like in practice.

**Why two timebases is right, not redundant:** `TIM2->CNT` resolves edges (1 µs, and it
wraps every ~71.6 minutes at 1 MHz), while a millisecond tick gives unambiguous
wall-ordering across a long run. Neither alone does both jobs.

**Consequence already exploited:** because the channel travels in the payload, the mask does
not need per-channel granularity for every class — see I1's cycle-complete resolution.

**Sizing note, for the queue instance in I5.** Three members as `uint32_t`, `uint32_t`,
`uint8_t` is 9 bytes, which `event_queue` rounds to 12, plus its 4-byte header = **16 bytes
per event**. At a 1 ms half-period soak (2000 transitions/s) that is ~32 KB/s of records
before any formatting — far beyond both the queue and the link. That is S7's flood concern
in numbers, and it is what the production mask exists to control; it is not a reason to
shrink the record.

---

### I5 — Queue instance: producer contexts and locking

**Status:** 🟢 · **Needs user:** no

**The finding, from the code:** `v_switch_out_force()` — B3's manual-event hook and the
"only place a switch output level is changed directly" — is reached from **three different
contexts**:

| Call site | Context |
|---|---|
| `v_switch_out_set/toggle/pulse` ([switch_out.c:311](../../App/Src/switch_out.c:311), 348, 571) | main loop (menu / acon) |
| `v_switch_out_tick` ([switch_out.c:516](../../App/Src/switch_out.c:516)), pulse expiry | **TIM14 ISR** (1 ms tick) |
| `v_switch_cycle_halt` ([switch_out.c:120](../../App/Src/switch_out.c:120)) | **TIM2 ISR** (compare), and main |
| `v_switch_out_init` ([switch_out.c:290](../../App/Src/switch_out.c:290)) | init (quiet, per S6) |

Plus B3's second hook, `v_switch_cycle_advance()`, which is TIM2-ISR only.

**Therefore the queue is NOT single-producer.** Two different interrupts at different
priorities plus the main loop can all put. `event_queue`'s default SPSC lock-free mode is
only safe for one producer, so this instance **must supply the per-queue
`pfn_lock`/`pfn_unlock` pair** (its S5) — which is exactly the case R1 recorded the console
plan as having worried about, now solved by the module rather than by hand.

This is a design constraint, not a preference: without it, two producers can interleave
inside `put` and corrupt the ring. A PRIMASK save/restore pair is the natural
implementation, and it is what the console plan's original "short PRIMASK guard around the
head advance" anticipated.

**Instance configuration — RESOLVED (user, 2026-08-30):**

- **Static buffer, not the module's malloc path.** `EVENT_QUEUE_ENABLE_MALLOC` is not
  needed for this instance; the buffer is supplied via `p_v_ram_buffer`.
- **8 KB, generously sized, behind a `#define`** so it can be tuned without hunting for a
  literal.
- **Created during system init, alive for the device's lifetime, never destroyed.** It can
  be flushed/cleared on demand — which is what the host-commanded flush command (LOCKED
  CONTEXT) already provides via `x_event_queue_flush()`.

**What 8 KB buys:** at I4's 16 bytes on the wire per event, ~512 records. Against the worst
realistic burst — a 1 ms half-period soak at 2000 transitions/s — that is roughly a quarter
of a second of headroom before the consumer must have drained. Ample for the deferral rule
(drains happen every executive-loop pass), and the drop counter reports honestly if it is
ever not.

**Two implementation constraints carried from the module:**

1. **The buffer must be 4-byte aligned** — `x_event_queue_create()` guards on it (that
   module's I4). A plain `static uint8_t[]` is not guaranteed aligned; declare it as
   `uint32_t[]` or with an explicit alignment attribute.
2. **The lock-fn pair is mandatory** for this instance, per the finding above. Not a
   preference — three producer contexts, two of them interrupts at different priorities.

**Creation point:** during system init, before any producer can fire. S6 makes the ordering
forgiving rather than critical — the mask is 0 through switch init — but "queue exists
before the first armed put" is still the rule, and the deferred mask restore is the natural
line to put it before.

---

### I6 — Drain implementations

**Status:** 🔴 · **Needs user:** yes

**Question:** what do the two drain sites actually do?

Two bodies to write, both already sited by LOCKED CONTEXT's XOR sink model:

- **acon:** the body of `v_acon_flush_events()`
  ([automation_console.c:517](../../App/automation_console/automation_console.c:517)),
  honouring S7's deferral rule — async frames only *between* response frames, never inside
  one. Called from the executive loop at
  [:616](../../App/automation_console/automation_console.c:616).
- **menu/human:** a drain in `v_app_polling_task()`
  ([app_main.c:390](../../App/Src/app_main.c:390)), emitting log lines only when the
  human-side emit option is on, but **draining unconditionally** either way.

Depends on D1 (frame format) and D2 (log line). Open sub-questions: how many records to
drain per pass (all, or a bounded batch so one flush cannot monopolise the loop), and
whether the human side needs its own emit-on/off control separate from the mask.

**Resolution:** _(pending)_

---

### I7 — Module layout

**Status:** 🟢 · **Needs user:** no

**Resolution (user, 2026-08-30):** **all low-level switch manipulation stays in the existing
`switch_out` module** — *"I don't see any reason to fragment these operations into multiple
modules"* — and **there is no wrapper layer over `event_queue`.** Events are sometimes
produced inside time-critical ISRs, and a mostly-useless function on top of the queue's own
call is undesirable there. Concretely:

- The record struct is **stack-allocated at the production site and filled member by
  member**, then `x_event_queue_put()` is **called directly**.
- **A header** carries the types, enums and constants for the SwitchTester event queue —
  the record struct, the event-class enum, `event_control_t` and its bit macros, the
  `EVENT_TICK_MS()` seam, the queue-size `#define`. Header only; no new `.c` module.

**Agreed, with two observations:**

1. **There are only two production sites today** (`v_switch_out_force()`,
   `v_switch_cycle_advance()`), which is well under the count at which a helper starts to
   pay. `x_event_queue_put()` already takes `(handle, id, size, ptr)`; a wrapper would
   largely re-list its own arguments.
2. **Direct filling is better than a wrapper here, not merely equivalent.** The timestamp
   source differs per hook — `CCRx` for automated, `CNT` for manual (I4). A generic wrapper
   would have to be told which, or would hide the distinction behind a default. Filling at
   the site keeps it visible where it matters.

**One optional suggestion, not pressed:** if the `(class, channel) → mask bit` test ends up
duplicated at each site, make it a `static inline` in the header rather than copy-paste.
That is not an abstraction layer — it inlines to the same instructions — it just gives one
place to change when sense adds four more sites. Writing it out twice is defensible at two
sites.

**Sense, later:** because the plumbing is header-defined, sense producers include the same
header and call `put` directly too. The only thing needing a `.c` home is the mask register
variable itself (and the queue handle); `switch_out.c` is the natural place for now, with
an `extern` in the header. If that reads badly once sense arrives, moving one definition is
a small change — no interface moves with it.

---

### I8 — Where the `put` sits inside the TIM2 compare ISR

**Status:** 🟢 · **Needs user:** no

**The constraint:** `SWITCH_CYCLE_MIN_LEAD_US` is **4 µs**
([switch_out.c:71](../../App/Src/switch_out.c:71)) — at 64 MHz, **256 cycles**. That is the
budget `v_switch_cycle_schedule()` assumes it has between a compare match and writing the
next `CCRx`; miss it and the scheduler pushes the edge out rather than losing a wrap, so
the waveform takes a late edge.

**The cost being added:** `x_event_queue_put()`
([event_queue.c:183](../../App/event_queue/event_queue.c:183)) does three guard checks, a
record-space calculation, the lock/unlock pair, a 4-byte header write and a 12-byte payload
write — each potentially a split memcpy at the ring boundary — plus two counter updates.
Plausibly **60–120 cycles** including the PRIMASK save/restore: a quarter to a half of the
entire lead budget. **Estimated, not measured** — see below.

**Recommendation:** in `v_switch_cycle_advance()`, order the ISR as

1. capture the just-occurred `*(p_x_map->p_u32_ccr)` into a local (~2 cycles),
2. do the existing phase/count work and call `v_switch_cycle_schedule()`,
3. *then* build the record and call `x_event_queue_put()`.

The next edge is placed with the lead budget essentially intact, and the timestamp is still
exact because the compare value was taken before the reschedule overwrote it.

**Campaign end is exempt:** `v_switch_cycle_halt()` schedules nothing, so the two puts on
that path (S7's manual "off" plus the cycle-complete) have no lead time to protect.

**Measuring it, if wanted:** Cortex-M0+ has no DWT cycle counter, so the practical methods
are a loop of N puts timed against `TIM2->CNT` (1 µs resolution, so aggregate over
thousands) or a spare GPIO toggled around the call and read on a scope. Worth doing once,
since the number also tells us the real ceiling on cycling rate with events armed.

**Resolution (user, 2026-08-30):** **ordering confirmed** — capture `CCRx`, do the phase
work and reschedule, *then* build the record and put.

**The headroom, now quantified rather than guessed.** Three constants bound this, and they
are separate things:

| Constant | Value | What it actually limits | Enforced |
|---|---|---|---|
| `SWITCH_CYCLE_TIME_MIN_US` ([switch_out.h:45](../../App/Inc/switch_out.h:45)) | **10 µs** | hard floor on a single ON or OFF phase | `v_switch_cycle_start()` ([switch_out.c:557](../../App/Src/switch_out.c:557)) — all paths |
| `SWITCH_CYCLE_MIN_LEAD_US` | **4 µs** | how close to *now* the next compare may be scheduled | `v_switch_cycle_schedule()` |
| `ACON_MIN_CYCLE_PERIOD_US` ([device_config.h:91](../../App/Inc/device_config.h:91)) | **50 ms** | on+off, host-commanded cycling only | acon command |

Worst case is therefore a **10 µs** phase, in which the ISR must finish within 10 − 4 =
**6 µs (384 cycles at 64 MHz)** or the lead guard starts pushing edges out. The existing
ISR work plus an estimated 60–120-cycle `put` sits comfortably inside that. Noted also that
the user's own instinct — *"I rather doubt I'm going to need switch state-change times
shorter than 10 µs"* — is exactly the floor already in the code.

**On raising the lead later (user: could go to 100 µs or 1000 µs if needed) — one trap.**
`SWITCH_CYCLE_MIN_LEAD_US` and `SWITCH_CYCLE_TIME_MIN_US` are coupled, and the guard
**pushes rather than rejects**. Raising the lead above the phase floor would silently
stretch every short phase to the lead value instead of refusing it — the waveform would
quietly stop honouring commanded times, with no error anywhere. So the two must move
together, and the lead must stay meaningfully **below** the phase floor. Bumping the lead
to 100 µs would require raising `SWITCH_CYCLE_TIME_MIN_US` to something well above it.

Staying at 4 µs for now, per the user; the headroom above makes that safe.

---

### S8 — Drop-count reporting

**Status:** 🟢 · **Needs user:** no

**Question:** how does a consumer learn that events were dropped — the side-channel counter
alone, or also an in-band synthetic record at the point of loss?

**Resolution (user, 2026-08-30):** **side-channel count only. No synthetic event.**
`u32_event_queue_dropped()` / `v_event_queue_dropped_reset()` already exist and are
bench-verified, so this costs nothing to implement and the in-band question does not need
answering — including the follow-on about whether a synthetic record would need its own
mask bit.

**Framing that settles it** (user): *this is not a commercial test instrument.* It is a
hobby project, purpose-built to exercise one race in a commercial product, which has grown
into a general-purpose bench instrument but does not need product-grade hardening. Knowing
*how many* events were lost is enough; knowing *where* in the stream the gap fell is a
refinement this does not need to buy. Consistent with the standing guard policy — guard the
host-commanded paths, leave the human ones relaxed.

The original question, kept for the record:

`u32_event_queue_dropped()` / `v_event_queue_dropped_reset()` already exist and are
bench-verified, so the side-channel half is free. The open part is whether to add the
in-band form the roadmap raised, following `jobs.c`'s `JOB_QUEUE_OVERFLOW` precedent: a
synthetic "N events lost here" record injected into the stream so the *position* of the gap
survives, which a counter cannot express.

If in-band were taken, a second question would follow: does the synthetic record need its
own mask bit, or is it exempt from masking (it reports a fault, not a source)? Moot now.

Adding the in-band record later would change no existing behaviour, so nothing is closed off.

---

### D1 — acon command surface

**Status:** 🔴 · **Needs user:** yes

**Question:** what commands, and what does an async event frame look like on the wire?

Needed: op letters and syntax for reading and writing the mask (whole-word `u32_all` and/or
by named bit), the dedicated host-commanded flush (LOCKED CONTEXT), and probably reads of
the drop and put counters. Plus the async frame format itself — how a host distinguishes an
unsolicited event frame from a command response, given S7's deferral rule already keeps them
out of each other's way.

Constraint to remember: `'Q'` is the builtin quit and cannot be shadowed; the event_queue
test harness already took `'F'`.

**Resolution:** _(pending)_

---

### D2 — Human log line format

**Status:** 🔴 · **Needs user:** yes

**Question:** what does an event look like in the human-readable log, and at which verbosity
tier?

The record carries channel, state, a 1 µs TIM2 count and a millisecond tick (I4). A log
line need not show all four. Also open: which logging class/tier it belongs to, since the
migrated logging API gives message classes a compile-time verbosity tier — an event line
that cannot be compiled out would be a poor fit for a soak run.

**Resolution:** _(pending)_

---

### T2 — HIL coverage

**Status:** 🔴 · **Needs user:** no (follows the implementation)

Extend `scripts/hil/test_acon.py` or add a `test_events.py` alongside `test_eventq.py`.
Regression net today: acon 47, nvm 28, eventq 20. Natural cases: mask write/read-back, mask
persistence across reset, an armed source producing the expected count, a masked source
producing nothing, flush behaviour, and the drop counter under a deliberate overrun.

**Resolution:** _(pending)_

---

### W1 — nvmparams: fetch a pool's label without full init *(banked, low priority)*

**Status:** 🔵 · **Needs user:** no — **explicitly LOW priority (user, 2026-08-30): "I
don't want to get too sidetracked on this."** Recorded so it is not lost, not to be worked
now.

**The idea:** a public `nvmparams` function that reads a pool's label **without going
through the full initialisation process.**

**Why it is worth having, from this feature's own use case (S5/I3):** the label answers
*whose pool is this*, but today the only way to reach it is to `x_nvm_pool_init()` first
and then read the header out of `p_x_pool->p_v_data`. That means committing to the pool's
init policy — and to allocating and filling a whole RAM pool — before you are allowed to
ask a 16-byte question about provenance. A peek would let an adopter decide *policy* from
the answer rather than the other way round, and would make surveying several pools across
several devices cheap.

**Sketch, not a design:** something taking the pool config (which already carries
`pfn_read`, `ux_base_address` and `p_v_context`) plus a caller-supplied output buffer, so
it reads only `sizeof(nvm_header_t)` bytes, allocates nothing, formats nothing and leaves
no state behind. Must be per-pool, for the same multi-pool/multi-device reason as T1.

**Home:** this is a vendored-module enhancement, so its permanent place is Skeleton's
`Docs/planning/improvements-backlog.md` (or `nvmparams-plan.md`). Parked here because it
surfaced here; move it when T1's Skeleton visit happens, so the two cross-repo items travel
together rather than causing two excursions.

---

### W2 — Separate mask sets for the human and automation interfaces *(banked)*

**Status:** 🔵 · **Needs user:** no — **deliberately deferred (user, 2026-08-30):** *"another
thing I don't want to get bogged down on."* S2 locks a single shared set for now.

**The idea:** two mask registers rather than one, so the human console and the automation
console can arm different sources — e.g. an operator watching manual operations at the menu
while a host script has only cycling events armed.

**Why it was deferred** (user's reasoning): with two sets, the active context must be known
and checked against at every event-logging site. One set is quickest, simplest and most
efficient.

**One note for whoever revisits this, so the future decision starts better informed:** the
per-site cost is avoidable. Because consumption is already XOR by console mode (R2), the
active context changes only at a mode transition — so a single "effective mask" could be
selected once at handover and the production sites would test it exactly as they do now,
unchanged. The cost of the two-set model would then be a swap at handover rather than a
branch per event. That does not argue against the current decision, which is right on
simplicity grounds regardless; it just means efficiency need not be the reason to keep
rejecting it.

---

### W3 — nvmparams: label-match checking inside `x_nvm_pool_init()` *(banked)*

**Status:** 🔵 · **Needs user:** no — recorded as a design note, no implementation
requested (user, 2026-08-30).

**The question asked, and the answer:** does `x_nvm_pool_init()` do any label-match
checking today? **No — none.** `p_c_label` appears exactly three times in `nvmparams.c`,
all on the format path: the `x_nvm_format_block()` signature
([nvmparams.c:341](../../App/nvmparams/nvmparams.c:341)), the `strncpy` that writes it
([:356](../../App/nvmparams/nvmparams.c:356)), and the call from init that passes it in
([:664](../../App/nvmparams/nvmparams.c:664)). It is written, never read back or compared.
Structurally, init returns `NVM_ERROR_NONE` the moment the scan reports a valid pool —
**before the label is consulted at all** — so a foreign-but-valid pool takes the early
return. That early return is precisely the hole S5 exists to plug.

**The idea:** have init compare the caller's label against what it finds and report a
mismatch through the return code.

**Why it is attractive:** it puts the check where the knowledge already is. The adopter
gets provenance for free on the call it already makes, with no second read and no reaching
into `p_x_pool->p_v_data`. It would largely **supersede W1** for this project's use case —
W1's residual value is inspecting a pool *without* committing to init at all (surveying
several pools, or choosing a policy from the answer).

**Four design points for whoever builds it:**

1. **It must be opt-in.** Today `p_c_label != NULL` means "write this when formatting".
   Making it also mean "require this on a valid pool" would change behaviour for every
   existing adopter — including pools formatted by older firmware under a different label,
   which would start failing init. A new config field defaulting to off fits the struct's
   documented rule that *every field's zero value is a sane default*.
2. **A config flag, not a new `x_init_policy` value.** The policy enum answers "what to do
   when the media does **not** hold a valid pool". A label mismatch is a different axis
   entirely — the pool *is* valid, it just is not ours — so folding it into that enum would
   conflate two independent questions.
3. **Report, do not destroy.** Init should return the mismatch and leave the pool loaded so
   the adopter can log what it found and decide. Wiping is the application's call (S5), and
   a module that reformats on its own initiative removes the evidence. The existing
   `NVM_ERROR_POOL_FORMATTED` (-10) / `NVM_ERROR_POOL_REFORMATTED` (-11) pair is the
   precedent for reporting an *outcome* through the return value rather than a hard
   failure; a `NVM_ERROR_LABEL_MISMATCH` would sit naturally at -12.
4. **Do not release the buffer on mismatch.** If init tore the pool down, the adopter could
   not even report which label it found — which is the most useful thing to log.

**Home:** vendored-module enhancement, so it lands in Skeleton and re-vendors outward.
Travels with T1 and W1 rather than causing its own excursion.

---

## Global notes

- **Row set is partial by design.** Seeded only from what
  [`switchtester-roadmap.md`](switchtester-roadmap.md) records as still open. Rows for the
  event record format, the production-layer module shape, the acon command surface, the
  human log format, the HIL coverage and anything else follow the user's relay.
- **Out of scope for this slice:** sense events (task 1 gates them), per-cycle duty lists
  (task 4), any change to the `event_queue` module itself, and `event_queue` W4 (priority
  put, deferred with a promotion draft in [`event-queue-plan.md`](event-queue-plan.md)).
- **Regression net:** `test_acon.py` 47, `test_nvm.py` 28, `test_eventq.py` 20. Run after
  anything structural. Close Tera Term first — it holds COM3.

**Plan status summary:** 15 🟢 · 1 🟡 · 4 🔴 · 3 🔵.

**The producer side is designed AND built** (S1–S8, I1–I5, I7, I8) — compiles clean, not
yet flashed. See *Implementation status*.

**Remaining 🔴 are all sink-side or follow-on:** D1 (acon commands), D2 (human log line),
I6 (the two drains), T2 (HIL). D1/D2/I6 are the next relay. **🟡:** T1 (Skeleton
back-port of the label check, now that one exists here to port).

**The five 🔴 rows are all sink-side or follow-on:** D1 (acon commands), D2 (human log
line), I6 (the two drains), S8 (drop reporting), T2 (HIL). D1/D2/I6 are the next relay;
S8/T2 have safe defaults and can follow the code.

**Two 🟡:** I7's residual (where the shared plumbing lives once sense needs it) and T1
(Skeleton back-port, blocked on the label check existing here).
**No shape has been relayed yet for the event record format, the production-layer module,
the acon command surface, the human log format or HIL coverage** — those rows do not exist
because they have not been discussed, not because they are settled.

**The mask is now fully specified.** Everything needed to declare `event_control_t`, its
bit-mask defines, its NVM object and its label-protected restore path is locked.

**End of event-path-plan.md**
