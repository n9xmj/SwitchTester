# `logging` — adoption guide

Compile-time-filtered, coloured, optionally timestamped console logging. Messages below
the build's verbosity ceiling cost **nothing** — not the call, not the arguments, not the
format string in `.rodata`.

This file is **how to drop the module into a project**. For *why* it is shaped this way —
the three-tier model, the dependency rule, the optional port — see
[`portable-apis-strategy.md`](../../Docs/planning/portable-apis-strategy.md). Conventions
live there and are deliberately not repeated here.

---

## Files

| File | Owner | Notes |
|---|---|---|
| `logging.c` | **vendored** | The engine. Includes the C library and `logging_config.h`; **no HAL**. |
| `logging.h` | **vendored** | Verbosity ladder, `log_color_t`, the `v_log*_printf*()` back ends. |
| `log_helpers.h` | **vendored** | The `LOG*` macros and `LOG_EMIT()`. Where the dead-code elimination happens. |
| `logging_config_template.h` | **vendored** | Copy → adopter's `logging_config.h`. Never included directly. |
| `logging_port_template.c` | **vendored** | Copy → adopter's `logging_port.c`. **Optional** — see *Port*. |
| `../common/ANSI.h` | **vendored leaf** | Escape sequences. Travels with this module; `logging.h`'s colours map onto it. |
| `App/Inc/logging_config.h` | **adopter** | Your tags, colours and ceiling. Required. |
| `App/Src/logging_port.c` | **adopter** | Your millisecond tick. Optional. |

Copy `App/logging/` and `App/common/ANSI.h` verbatim. Both are byte-identical across all
three projects that use them — `diff` them rather than assuming.

## Adopting it, in five steps

1. Copy `App/logging/` and `App/common/ANSI.h` into the new project; put both directories
   on the include path.
2. Copy `logging_config_template.h` → `App/Inc/logging_config.h`. It **must** exist under
   that name: `logging.c` includes it by name, the same contract FatFs uses for `ffconf.h`.
3. Edit the tags (below) and set `LOG_LEVEL`.
4. *Optional:* copy `logging_port_template.c` → `App/Src/logging_port.c` and point
   `u32_log_timestamp_ms()` at your tick.
5. From any module that wants to log: `#include "logging_config.h"` — not `logging.h`.

## Configuration

Everything lives in your `logging_config.h`.

| Knob | What it does |
|---|---|
| `LOG_WITH_TIMESTAMP` | Nonzero prefixes timestamped messages with `(seconds.millis)`. Must be set **before** `#include "logging.h"` in that file. |
| `LOG_LEVEL` | The build's verbosity ceiling. Usually `LOG_LEVEL_QUIET` unless `DEBUG`. |
| `LOG_<CLASS>` | That class's tier. |
| `LOG_<CLASS>_TAG` | Bracketed prefix string, e.g. `"SYSTEM"`. |
| `LOG_<CLASS>_COLOR` | A `log_color_t`. |

A class needs all three defines, and the macros build the latter two by token-pasting
`_TAG` / `_COLOR` onto the name you pass — so they must match exactly:

```c
#define LOG_SYSTEM              LOG_LEVEL_DEBUG
#define LOG_SYSTEM_TAG          "SYSTEM"
#define LOG_SYSTEM_COLOR        LOGC_BRIGHT_MAGENTA
```

### The ladder is VERBOSITY, not severity

This trips people, and it is the single most important thing on this page:

```
LOG_LEVEL_QUIET     0   never emitted, at either end
LOG_LEVEL_ALWAYS    1   shown whenever logging is on at all
LOG_LEVEL_ERROR     2
LOG_LEVEL_WARNING   3
LOG_LEVEL_INFO      4
LOG_LEVEL_DEBUG     5   chattiest
```

A message is emitted when `tag != QUIET && tag <= LOG_LEVEL`. So a **higher** number is
**more** verbose, and `0` means silence at both ends — a class set to `QUIET` never prints,
and a build set to `QUIET` prints nothing at all. An ascending *severity* scale was tried
first and abandoned: it made a global of `0` maximally permissive, which is exactly
backwards from what anyone types when they want quiet.

The names are severity-flavoured because that is how you think about a message class. The
comparison is verbosity. Both are true and they do not conflict.

### Colour is independent of tier

`LOG_<CLASS>_COLOR` is the **only** source of truth for colour. Nothing inspects a class's
tier to override it — a class at `LOG_LEVEL_ERROR` prints in whatever its `_COLOR` says.
Keeping the two aligned is a convention you follow when editing the config, not a rule the
code enforces.

Colours are `log_color_t` (`LOGC_RED`, `LOGC_BRIGHT_CYAN`, …) and may be OR-ed with
attribute bits: `LOGC_BOLD`, `LOGC_UNDERLINE`, `LOGC_REVERSE`, `LOGC_BLINK`. Three
non-colour options ride in the same word:

- `LOGC_NONEWLINE` — suppress the trailing newline.
- `LOGC_NEWLINE_BEFORE` — emit a newline *before* the tag/timestamp.
- `LOGC_NONE` — send no ANSI sequences at all, unlike `LOGC_NORMAL` which sends
  attributes-off at both ends.

## Using it

| Macro | Prefix | Colour |
|---|---|---|
| `LOG(tag, fmt, …)` | `[TAG]` + timestamp | none |
| `LOGC(tag, color, fmt, …)` | `[TAG]` + timestamp | explicit |
| `LOGCT(tag, fmt, …)` | `[TAG]` + timestamp | the tag's own |
| `LOG_PLAIN` / `LOGC_PLAIN` / `LOGCT_PLAIN` | none | as above |
| `RPRINTF(…)` | none | unconditional `printf` — always compiled in |
| `DPRINTF` / `DPRINTF_TS` | none | debug-build only, independent of `LOG_LEVEL` |

```c
#include "logging_config.h"

LOGCT(LOG_SYSTEM, "boot complete, %u ms", u32_elapsed);
LOGC(LOG_SYSTEM, LOGC_ERROR, "sensor %u did not answer", u8_id);
RPRINTF("always printed\r\n");
```

`LOGCT` is the one to reach for by default — it uses the class's own tag and colour, so
call sites stay short and the appearance of a whole class is retargetable from one line of
config.

Every macro is a `do { … } while (0)`, so this is safe:

```c
if (x) LOGCT(LOG_SYSTEM, "..."); else y();
```

## Port

**One function, and it is optional.**

```c
uint32_t u32_log_timestamp_ms(void);
```

**On an STM32 this is almost always one line.** Copy `logging_port_template.c` to
`App/Src/logging_port.c` and write:

```c
#include "stm32g0xx_hal.h"      /* your family header */
#include "logging.h"

uint32_t u32_log_timestamp_ms(void)
{
    return HAL_GetTick();       /* the typical answer, on any STM32 */
}
```

That is the whole port. On a non-HAL target, substitute whatever free-running millisecond
counter you have — a SysTick handler's own counter, an RTOS tick, a hardware timer.

`logging.c` carries a **weak default returning 0**, so the module links and runs without a
port at all — timestamps simply read `(0.000)`. The file is genuinely optional; skipping it
costs you timestamps and nothing else.

The counter need not start at zero or track wall-clock time; only differences are
meaningful to a reader. It must not block, and it must be safe to call from anywhere you
log — including an ISR, if you log from one.

## Gotchas worth knowing before they cost you time

**Include `logging_config.h`, never `logging.h`.** The config sets `LOG_WITH_TIMESTAMP`
ahead of pulling in `logging.h`, so including the engine header directly gets you the
fallback rather than the project's choice.

**Disabled classes really do vanish.** `LOG_EMIT()` is a compile-time constant expression,
so the whole `do{}while(0)` folds away — arguments unevaluated, and the **format-string
literal leaves `.rodata` too**, which is the part that usually survives naive dead-code
elimination. If you are checking this after a change, compare `.text` *and* `.rodata` in
the map file; a class you thought you disabled but whose strings remain means the guard
was defeated somewhere.

**Migrating a project that still writes `#define LOG_FOO 1` / `0`?** Set `LOG_LEVEL` to
`LOG_LEVEL_DEBUG` and leave the tags alone. A tag of `0` is `QUIET` and stays off; a tag of
`1` is `ALWAYS` and stays on. One line, no refactor, and you can convert classes to real
tiers at your leisure.

**`ANSI.h` lives in `App/common/`, not here.** It is a vendored *leaf* — no dependencies of
its own, shared by more than logging. It still has to travel with this module. Note the
lowercase `#include "ansi.h"` seen in some older trees breaks on a case-sensitive
filesystem; the file is `ANSI.h`.

**The engine includes no HAL.** If you find yourself adding one to `logging.c`, the thing
you want belongs in `logging_port.c` instead.
