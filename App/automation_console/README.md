# `automation_console` — adoption guide

A machine-facing command console: single-character opcodes in, sigil-prefixed frames out,
every numeric in hex both ways. It is **not** a test harness — it is the interface *for*
one, and the harness lives on the host. Nor is it test-only: this is how an external host
drives the instrument, which includes ordinary operation.

This file is **how to drop the module into a project**. For *why* it is shaped this way,
see [`portable-apis-strategy.md`](../../Docs/planning/portable-apis-strategy.md); the
protocol's full rationale is in `automation-console-plan.md`.

---

## Files

| File | Owner | Notes |
|---|---|---|
| `automation_console.c` | **vendored** | Executive, framing, dispatch, builtins. No application dependencies. |
| `automation_console.h` | **vendored** | Protocol contract and the command-author API. |
| `automation_console_config_template.h` | **vendored** | Copy → adopter's `automation_console_config.h`. |
| `automation_commands.c` | **adopter** | Your handlers and `g_x_acon_command[]`. Ships here as a worked example. |
| `App/Inc/automation_console_config.h` | **adopter** | Knobs, hooks, identity. **Required.** |

## Adopting it, in four steps

1. Copy `App/automation_console/` in and put it on the include path.
2. Copy the template → `App/Inc/automation_console_config.h`; set the two platform hooks
   and the identity strings.
3. Replace `automation_commands.c`'s example ops with your own.
4. Call `v_automation_console_run()` from your debug menu — once for the `0xDA` sentinel
   (SCRIPT mode), once for a menu key (HUMAN mode).

## Configuration

| Knob | Default | What it does |
|---|---|---|
| `ACON_ENABLE` | 1 | 0 compiles the module out entirely — both `.c` bodies empty, entry points become inert inline stubs, so **no call site needs an `#ifdef`**. |
| `ACON_TICK_MS()` | **`HAL_GetTick()`** | Free-running millisecond counter. On an STM32 this is essentially always `HAL_GetTick()`, directly or via a project macro — `SYSTEM_TICK()` in these trees. Omitting it is legal and `#warning`s: the SCRIPT idle timeout is disabled, so a host that dies mid-session wedges the console. |
| `ACON_PUMP()` | **`v_app_polling_task()`** | Cooperative polling hook, called every spin of the SCRIPT reader so jobs, cycling and the watchdog keep running. Almost always the app's main-loop service function — `v_app_polling_task()` by convention here, reached through the NULL-safe `PUMP_POLLING_TASK()` macro. Omitting it `#warning`s: the board appears to hang for the duration of a session. |
| `ACON_ID_PRODUCT` / `_PLATFORM` / `_FIRMWARE` / `_BUILD` | `"?"` | Strings the `V` builtin reports. |
| `ACON_MAX_ARGS` | 6 | Widest comma-split `u8_acon_args()` will do. |
| `ACON_LINE_MAX` | 512 | Longest input line. **Must not exceed the console RX ring** — see gotchas. |
| `ACON_EMIT_MAX` | 128 | Longest response frame. |
| `ACON_IDLE_TIMEOUT_MS` | 15000 | SCRIPT-mode dead-host timeout. |
| `ACON_TX_TIMEOUT_MS` | 100 | Per-frame transmit bound. |

### What a real config looks like

Both platform hooks are one line each. This is the whole of what these projects write:

```c
#include "platform.h"       /* SYSTEM_TICK(), PUMP_POLLING_TASK() */
#include "device_config.h"  /* PRODUCT_NAME, PLATFORM_NAME, FIRMWARE_VERSION */

#define ACON_ENABLE             1

#define ACON_TICK_MS()          SYSTEM_TICK()        /* -> HAL_GetTick()          */
#define ACON_PUMP()             PUMP_POLLING_TASK()  /* -> v_app_polling_task()   */

#define ACON_ID_PRODUCT         PRODUCT_NAME
#define ACON_ID_PLATFORM        PLATFORM_NAME
#define ACON_ID_FIRMWARE        FIRMWARE_VERSION
#define ACON_ID_BUILD           BUILD_CONFIG

#define ACON_MAX_ARGS           6u
#define ACON_LINE_MAX           512
#define ACON_EMIT_MAX           128
#define ACON_IDLE_TIMEOUT_MS    15000
#define ACON_TX_TIMEOUT_MS      100
```

With no project macros to hand, `#define ACON_TICK_MS() HAL_GetTick()` and
`#define ACON_PUMP() v_app_polling_task()` are the direct equivalents. Prefer a guarded
macro like `PUMP_POLLING_TASK()` if your pump is a weak symbol — an unguarded call to an
unresolved weak symbol is a branch to address 0.

`ACON_LINE_MAX` and `ACON_EMIT_MAX` are static buffers in the core — together, the module's
whole RAM cost.

## Protocol, in brief

```
host → device   <op>[,<arg>]...<CR>
device → host   <sigil><op>[,<token>]...<CRLF>
```

Sigils: `=` success, `!` failure, `+` payload continuation, `*` async event (unused today),
`#` not-protocol. Tokens are `<KEY><hexvalue>` with no separator inside the token. CR
terminates; LF is discarded wherever it appears, so a CRLF host produces exactly one frame
per command and a bare CR is a no-op that answers.

Two modes, **same dispatcher and same frames** — only the reader differs, so a command tried
by hand behaves exactly as it will from a script:

- **SCRIPT** — raw byte-at-a-time, no echo, idle timeout armed, stdout suppressed. Entered
  from the `0xDA` sentinel.
- **HUMAN** — `i_getline()`, so echo and line editing come from the same code the debug menu
  uses. No idle timeout: there is an operator present, not a host that can die.

## Writing commands

The port point is `g_x_acon_command[]`. The core owns the builtins — quit, list, version,
no-op — and guarantees they are always present, so your table carries only domain ops.

```c
static void v_op_read(char c_op, char *pc_line)
{
    char    *ap_c_arg[ACON_MAX_ARGS];
    uint32_t u32_addr;

    if (u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS) < 1u
        || !b_acon_arg_u32(ap_c_arg[0], &u32_addr))
    {
        v_acon_err(c_op, ACON_ERR_ARGS);
        return;
    }
    v_acon_emit(ACON_SIG_OK, "%c,V%X", c_op, (unsigned) u32_read(u32_addr));
}

const acon_op_t g_x_acon_command[] = {
    { 'R', v_op_read, "read: addr" },
};
const uint8_t g_u8_acon_command_count =
    (uint8_t) (sizeof(g_x_acon_command) / sizeof(g_x_acon_command[0]));
```

Helpers: `v_acon_emit()`, `v_acon_ok()`, `v_acon_err()`, `u8_acon_args()`,
`b_acon_arg_u32()`, `pc_acon_op_name()`. Generic error mnemonics are `ACON_ERR_UNKNOWN`,
`_ARGS`, `_RANGE`, `_OVERFLOW`; define your own domain codes locally.

For a multi-line reply, declare the count in the header (`K<n>`) and follow with that many
`ACON_SIG_PAYLOAD` lines. Declaring the count is what makes a truncated response
*detectable*, which a terminator could not manage.

**Document your op set in the adopting project's own docs, not here.** This README covers the
module — protocol, config, command-author API — and stays project-neutral so it can be
vendored unchanged. Your command reference belongs alongside your other project
documentation, and `L` gives you the current list to generate it from.

## Port

**No port source.** Everything crossing the boundary is either a macro in the config header
or `g_x_acon_command[]`, which is your own command module rather than a copied template.
That is the optional-port rule in practice, not a gap.

## Gotchas worth knowing before they cost you time

**`ACON_LINE_MAX` must stay well below the console UART's RX ring.** A line longer than the
ring cannot be received at all, however the console handles it — and the loss takes the
terminating CR with it, so the failure surfaces against the *next* command instead of the
one that overran. This has bitten before: at a 256-byte ring against a 512-byte line limit,
a maximal frame measured ~19% byte loss.

**SCRIPT mode never touches stdio, in either direction.** Output goes through
`v_acon_emit()` and input through the raw reader, both talking to `uart_stream` directly.
That is what lets stdout be suppressed wholesale during a session without silencing the
console itself, and why the reader can keep pace with the wire. Do not "helpfully" route a
response through `printf`.

**The sigil is an argument, not part of the format string.** `v_acon_emit(ACON_SIG_OK, …)`
— so "every device→host line carries a sigil" is a property of the signature rather than a
convention every call site must remember. Keep it that way.

**A frame that would overflow is dropped whole**, replaced by `!~,OVF` — never a truncated
fragment, which would parse as a shorter, *wrong* frame.

**The parser caps silently.** `u8_acon_args()` returns at most `ACON_MAX_ARGS` fields; a
host that sends more gets the first N with no error. If a command takes a variable-length
list, size `ACON_MAX_ARGS` against the longest one you will accept and validate the count
yourself.

**Dispatch is case-sensitive**, and control-character opcodes echo in caret notation
(`0x03` → `"^C"`), so a response frame is always printable ASCII.

**Ctrl-C quits from either mode, and a bare CR always answers.** An operator who finds the
board unexpectedly in the console can identify that — `=Z` instead of the menu's help — and
leave, in two keystrokes.
