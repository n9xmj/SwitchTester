# Decision-log planning model (SwitchTester)

**Purpose:** living plan documents for multi-session design work — feature specs moving
toward implementation (switch cycling, SENSE inputs, HIL automation backdoor).

Adapted for this repo from the canonical copy at
`C:\STM32\CubeSource\LED_Strip_Controller_G474\Docs\planning\decision-log-model.md`,
which itself came from the Simplehuman workflow (sidebar decision log + one-at-a-time
resolution). The PLAY-specific machinery, Must-Ship-Gap fence, `/wrapup` and
`/cleanup-docs` skills and `.grok/` paths from those repos are **not** carried over —
SwitchTester has none of them.

---

## Why this exists

Chat is a bad place to store open questions. When several unrelated decisions arrive in
one message, some get answered and the rest silently vanish into scrollback — which is
exactly what happened during switch-cycling planning, where three questions went
unanswered across three consecutive exchanges.

The board fixes that structurally: every open item has a visible row and a colour, so
nothing depends on remembering. **Ask one question at a time in chat; park everything
else on the board.**

---

## When to use

Create or extend a plan when:

- A feature has many open design points.
- Work will span multiple sessions.
- You want to resolve items **one at a time** without re-reading chat history.

For small one-shot fixes, skip the plan and just do the work.

---

## Document layout

Plans live at `Docs/planning/<topic>-plan.md` — e.g. `switch-cycling-plan.md`.

### 1. Header block

Title, one-paragraph feature description, code home, link to the parent spec
(`Docs/SwitchTester-Design.md`), status (PLANNING · IN PROGRESS · IMPLEMENTING · DONE),
and a **working mode** one-liner.

### 2. Brief — first thing in the doc

One to two paragraphs of plain language: what the feature provides and the scope of the
current version. Orientation before any table. Detail belongs in the detail sections at
the bottom, not here.

### 3. The Big Board

The at-a-glance table, immediately after the Brief with minimal prose in between. In chat
you can reference rows by ID — *"what's still red?"*, *"green D4"*, *"reopen S2"*.

| ID | Status | Subject (one line) |
|----|--------|--------------------|

**ID prefixes**, numbered sequentially within each prefix (`D1`, `D2`, … `S1`, …):

| Prefix | Meaning |
|--------|---------|
| **D** | Design — UI, menus, key maps, units, formats |
| **S** | Semantics — behaviour, timing, error policy |
| **I** | Implementation — modules, structs, ISRs, scope, limits |
| **T** | Tooling / docs |
| **Q** | Question — explicitly needs user input to proceed |
| **W** | Wish — v2+ backlog (wish-list table only) |

**Status:**

| Indicator | Meaning |
|-----------|---------|
| 🔴 | Unaddressed — no leaning recorded yet |
| 🟡 | In progress — discussion open, or a leaning is documented |
| 🟢 | Resolved — decision locked, detail section updated |
| 🔵 | Deferred / explicitly out of scope for this version |

When a status changes, update **both** the table **and** the matching detail section.

### 4. Wish list (v2+) — optional companion table

Directly under the Big Board. Deferred features and ideas not in the current version. Use
**W** IDs. Add a one-line row when an idea surfaces in chat; promote it to a full
**D**/**S**/**I** row with a detail section when design work actually starts.

### 5. LOCKED CONTEXT

Decisions and established facts — **do not re-litigate** unless reopened. Bench
measurements, hardware constraints, prior shipped behaviour. Keeps the board focused on
what still moves.

Mark inferences as inferences. An unconfirmed hypothesis parked here should say so
explicitly, or it hardens into assumed fact.

### 6. Detail sections — one per ID

**Keep them sorted by ID, in the same order as the Big Board** — all the **D**
rows in numeric order, then all the **S** rows, then **I**, then **T**. Never
group by status, and never append a new row at the end of the file: insert it in
its numeric place.

The reason is navigation. The board is the index; if the detail sections run in
the same order, finding a row means scrolling or paging to roughly the right
place, with no search. Sorting by status instead means the file reshuffles every
time a row goes green, and a reader who knows where **S7** was last week finds
something else there.

Resolved rows stay in place — a `*(resolved)*` suffix on the heading marks them
without moving them.

Section skeleton:

```markdown
### D1 — <short title>

**Status:** 🔴 · **Needs user:** yes/no

**Question:** …

**Options considered:** …

**Leaning / recommendation:** …

**Resolution:** _(empty until 🟢; then the locked decision)_
```

Resolved rows **keep their options and rationale** so a later session can audit why a
decision was made and decide whether to reverse it.

### 7. Global notes (footer)

- Cross-cutting decisions that aren't one ID.
- Implementation phase sketch, once enough rows are 🟢.
- Code anchors as they appear.
- **Plan status summary** — counts by colour, next suggested ID.

---

## How to work the plan

1. **User** references IDs in chat: *"D4 → green, take the collapse"*, *"what's yellow?"*.
2. **Agent** updates the doc in the same session — table, detail section, LOCKED CONTEXT
   if the decision changes standing assumptions.
3. **Agent never silently resolves a 🔴 or 🟡.** Record a leaning, then wait for
   confirmation — or an explicit *"your call on D4"*.
4. **Agent asks one question at a time** in chat. Multiple open items live on the board,
   not in a numbered list in a message.
5. When implementation starts, add code anchors / a phase checklist to the footer.
6. After decisions land, **sync `Docs/SwitchTester-Design.md` from the plan** — the plan
   is the negotiation log, the design doc is the contract. Don't maintain detail in both.

---

## Related docs

| Doc | Role |
|-----|------|
| [`../SwitchTester-Design.md`](../SwitchTester-Design.md) | Project design spec — the contract once decisions land |
| [`switch-cycling-plan.md`](switch-cycling-plan.md) | First plan in this repo; worked example of the model |

**End of decision-log-model.md**
