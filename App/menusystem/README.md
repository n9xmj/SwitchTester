# `menusystem` — adoption guide

A tiny, allocation-free console-menu framework. You describe a menu as a flat
array of `menu_item_t` entries — title text, key-dispatched command functions,
submenus — and the module prints the help listing, checks for duplicate keys,
and routes a keypress to the right handler. Submenus form a stack (goto / call /
return).

It is the simplest kind of vendored module: **C standard library only — no
config header, no port source, nothing to satisfy.** Copy the directory, put a
menu in front of it, and pump one service function from your main loop.

## Files

| File | What it is |
|---|---|
| `menusystem.c` | the module — dispatch, help printer, key-conflict checker |
| `menusystem.h` | the public API and the `menu_item_t` definition |

There is no `*_config.h` and no port template, because the module needs nothing
from the application beyond the C library. Its only outputs are `printf` /
`putchar`, which reach the console through whatever stdio retargeting you
already have.

## Adopting it, in three steps

1. **Copy** `App/menusystem/` into your project. It is byte-identical across all
   adopting projects — copy it wholesale, never fork it.
2. **Add** `../App/menusystem` to your compiler include paths.
3. **Define a menu and wire the service loop** — the rest of this guide.

## Defining a menu

A menu is a `const` array of `menu_item_t` terminated by an END_OF_LIST
sentinel. This is the canonical boilerplate — a title, a `?` help key with a
hidden `<Enter>` alias, one command, an ESC "return", and the sentinel:

```c
static void v_example_command(void);   /* your handler, defined elsewhere */

static const menu_item_t x_example_menu[] =
{
    {   /* Title -- printed at the top of the help listing */
        .x_type   = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key    = 0,
        .p_c_text = "\r\n--- Example Menu ---\r\n"
    },
    {   /* '?' prints the menu (the help listing) */
        .x_type   = MENU_ITEM_HELP,
        .c_key    = '?',
        .p_c_text = NULL
    },
    {   /* Hidden alias: a bare <Enter> reprints the menu, not "unknown key" */
        .x_type   = MENU_ITEM_HELP_HIDDEN,
        .c_key    = '\r',
        .p_c_text = NULL
    },
    {   /* A command: press 'x' -> v_example_command() runs */
        .x_type       = MENU_ITEM_FUNCTION,
        .c_key        = 'x',
        .p_c_text     = "Example command",
        .pfn_function = v_example_command
    },
    {   /* ESC returns to the parent. Canonical text so the copies fold in
           .rodata (see note); use .p_c_text = NULL instead to hide the entry. */
        .x_type   = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key    = 0x1B,
        .p_c_text = "Return to previous menu"
    },
    {   /* Sentinel -- MUST be last */
        .x_type = MENU_ITEM_END_OF_LIST
    }
};
```

**Home menu vs submenu.** A submenu's `RETURN_TO_PREVIOUS_MENU` pops back up. The
home menu has nowhere to pop — but you can still include a return there: on an
empty stack the framework prints `[At top-level menu]` (handy for confirming
you've backed all the way out) and reprints the menu. Give that home-menu entry
`.p_c_text = NULL` so it stays **off** the listing while still working — a
NULL-text return is hidden, exactly as a NULL-text `FUNCTION` is.

**Return-text convention.** A *visible* return supplies its own text, and every
one should use the identical canonical string **`"Return to previous menu"`**
verbatim. Identical `const` strings fold to a single `.rodata` copy at link time,
so repeating the phrase across every submenu costs nothing.

**A submenu is just an item that points at another menu array:**

```c
    {
        .x_type   = MENU_ITEM_CALL_MENU,   /* or MENU_ITEM_GOTO_MENU */
        .c_key    = 's',
        .p_c_text = "Enter the example submenu",
        .p_x_menu = x_example_menu
    },
```

### Item type reference

Every entry's `.x_type` is one of the following (all declared in
`menusystem.h`). Most menus use only `HELP_TEXT_FIXED`, `HELP`, `HELP_HIDDEN`,
`FUNCTION`, `CALL_MENU` and `RETURN_TO_PREVIOUS_MENU`; the rest are there when
you need them.

- **`MENU_ITEM_HELP_TEXT_FIXED`** — a static line or block printed in the
  listing; carries no key. Use it for the menu title and for section headers or
  a key legend. `.p_c_text` is the text.
- **`MENU_ITEM_HELP_TEXT_VARIABLE`** — as above, but also calls
  `.pfn_help_text_function` every time the menu is printed, so the line can show
  a *live* value. Any `.p_c_text` prints first as a fixed lead-in. See *Showing
  live parameter values* below.
- **`MENU_ITEM_HELP`** — reprints the whole menu; the conventional `?` key. With
  `.p_c_text = NULL` it lists itself as "Help - show this menu".
- **`MENU_ITEM_HELP_HIDDEN`** — the same reprint action, but kept off the
  listing. Bind a bare `<Enter>` (`'\r'`) to it so pressing return re-shows the
  menu instead of printing "not recognized".
- **`MENU_ITEM_FUNCTION`** — runs a `void(void)` handler (`.pfn_function`) when
  `.c_key` is pressed; the workhorse command entry. NULL `.p_c_text` hides it; a
  NULL handler (or `.b_not_implemented`) makes it report "Not implemented yet".
- **`MENU_ITEM_KEY_FUNCTION`** — like `FUNCTION`, but the handler is `void(char)`
  and receives the key that selected it (`.pfn_key_function`) — for when a
  single-key entry still wants to know which key ran it.
- **`MENU_ITEM_KEY_LIST_FUNCTION`** — binds several keys to one handler
  `void(char, uint8_t)`. `.p_c_key_list` is the set of keys; the handler gets the
  pressed key and its zero-based index in that list. Prints nothing of its own —
  pair it with a `HELP_TEXT_*` legend. See *One handler for a family of keys*.
- **`MENU_ITEM_GOTO_MENU`** — replaces the current menu with `.p_x_menu` **in
  place**, without pushing the stack. A lateral jump: a later
  `RETURN_TO_PREVIOUS_MENU` pops to whatever was beneath, not back to here.
- **`MENU_ITEM_CALL_MENU`** — enters `.p_x_menu` as a submenu, **pushing** it on
  the stack so `RETURN_TO_PREVIOUS_MENU` comes back. Ordinary submenu nesting.
- **`MENU_ITEM_RETURN_TO_PREVIOUS_MENU`** — pops one level. On an empty stack it
  prints `[At top-level menu]` and reprints. NULL `.p_c_text` hides it (see
  *Home menu vs submenu*). An optional `.pfn_function` runs as an on-exit
  callback (e.g. stop an active process on leaving the submenu).
- **`MENU_ITEM_RETURN_TO_HOME_MENU`** — pops all the way to the home menu; same
  optional on-exit `.pfn_function`.
- **`MENU_ITEM_IGNORE`** — never printed, never dispatched; a harmless
  placeholder you can leave in an array (a reserved slot, a structurally-kept
  but disabled entry).
- **`MENU_ITEM_END_OF_LIST`** — the mandatory terminator, and it must be last:
  both the dispatcher and the help printer stop at it.

(`MENU_ITEM_MAX_VALUE_FOR_SIZEOF_1` is not a usable entry — it only pins the
enum to a single byte.)

### Showing live parameter values (`HELP_TEXT_VARIABLE`)

`HELP_TEXT_VARIABLE` lets a menu *display* a setting without the user having to
run its editor. Its `.pfn_help_text_function` runs on every menu print, so
whatever it emits is refreshed each time (on `?`, and on the auto-reprint after
a command). Point it at a function that dumps your current values:

```c
static uint32_t u32_test_values[4];

/* Redraws the live values (and their keys) on every menu print. */
static void v_test_values_help(void)
{
    for (uint8_t u8_i = 0; u8_i < 4; u8_i++)
    {
        printf("[%c] Edit test value %u = %lu\r\n",
               "1234"[u8_i], (unsigned) (u8_i + 1),
               (unsigned long) u32_test_values[u8_i]);
    }
}

    {   /* optional fixed lead-in, then v_test_values_help() each print */
        .x_type                 = MENU_ITEM_HELP_TEXT_VARIABLE,
        .c_key                  = 0,
        .p_c_text               = "\r\n--- Test values ---\r\n",
        .pfn_help_text_function = v_test_values_help
    },
```

### One handler for a family of keys (`KEY_LIST_FUNCTION`)

When several keys drive the *same* operation on different targets, bind them all
to one handler instead of writing an entry per key. `.p_c_key_list` is the set of
keys; when the user presses one, the handler receives that key **and its index
in the list** — so the list position doubles as an array index:

```c
/* Keys '1'..'4' all land here. The framework passes the matched key's index in
   "1234", so u8_index is already the array index -- no lookup needed. (The key
   is passed too, if you would rather derive the index from it yourself.) */
static void v_edit_test_value(char c_key, uint8_t u8_index)
{
    char str_prompt[32];
    (void) c_key;

    snprintf(str_prompt, sizeof(str_prompt), "Test value %u", (unsigned) (u8_index + 1));
    (void) u8_entry_u32(str_prompt, 0, 0xFFFFFFFFUL, &u32_test_values[u8_index]); /* your line editor */
}

    {   /* one entry, one handler, four keys */
        .x_type                = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list          = "1234",
        .pfn_key_list_function = v_edit_test_value
    },
```

A `KEY_LIST_FUNCTION` prints nothing itself, so pair it with the
`HELP_TEXT_VARIABLE` block above: that block is both the key legend **and** the
live readout, and the two together give a compact "press *N* to edit value *N*,
here are their current values" panel. This is exactly how SwitchTester's *Switch
cycling* submenu presents its per-channel timing parameters.

**Option flags.** Each item carries a byte of option bits, settable two ways —
by named bitfield or by OR-mask:

- `.b_no_newline = 1` (mask `MOPT_NO_NEWLINE`) suppresses the CR/LF after the
  item's text, so the next item continues on the same line — handy for rendering
  a row of related items.
- `.b_not_implemented = 1` (mask `MOPT_NOT_IMPLEMENTED`) makes a command report
  "Not implemented yet" instead of dispatching — a stub you can leave in a menu.

The two forms are the same byte overlaid (`.u8_options = MOPT_NO_NEWLINE |
MOPT_NOT_IMPLEMENTED` sets both at once). Default `0` behaves exactly as it
always has.

## Wiring it in

You need a control block, a small stack (its depth bounds how deeply submenus
can nest), an init call, and a service function pumped from your main loop.

```c
#include "menusystem.h"

static void          *x_menu_stack[4];           /* max submenu nesting depth */
static menu_control_t  x_menu_control;
#define MENU_STACK_DEPTH  (sizeof(x_menu_stack) / sizeof(void *))

void v_menu_service_init(void)
{
    v_menu_init(&x_menu_control, x_home_menu,
                &x_menu_stack[0], MENU_STACK_DEPTH);

    v_menu_exec(&x_menu_control, 0xFF);   /* 0xFF -> print the initial help */
}
```

Then, once per main-loop pass, drain pending console input and dispatch it:

```c
void v_menu_service(void)
{
    static uint8_t u8_reentry_lock;
    int  i_key;
    char str_key[4];

    if (u8_reentry_lock) { return; }      /* re-entry guard -- see Gotchas */
    u8_reentry_lock = 1;

    for (;;)
    {
        i_key = getchar();                /* NON-blocking: returns < 0 when idle */
        if (i_key < 0) { break; }

        /* --- OPTIONAL: hand a machine off to the automation console ---------
         * Only if you have adopted the automation_console module. The 0xDA
         * sentinel is intercepted BEFORE the echo below, so it never reaches
         * the menu dispatcher. Delete this block if you do not use acon. */
        if ((uint8_t) i_key == ACON_ENTER)
        {
            v_automation_console_run(ACON_MODE_SCRIPT);
            continue;
        }
        /* ------------------------------------------------------------------- */

        pc_char_to_str((char) i_key, str_key);   /* renders 0x1B as "ESC", etc. */
        printf("Cmd [%s]\r\n", str_key);
        v_menu_exec(&x_menu_control, (char) i_key);
    }

    u8_reentry_lock = 0;
}
```

That is the whole integration: `v_menu_service_init()` once at start-up, then
`v_menu_service()` every pass through your main loop.

## Gotchas worth knowing before they cost you time

- **The re-entry lock is not tidiness.** If a blocking handler pumps the main
  loop while it waits (e.g. a cooperative delay that calls the polling task,
  which calls `v_menu_service()` again), the lock is what stops the nested call
  from stealing input and dispatching it as stray menu keys. Keep it.
- **`getchar()` must be non-blocking** for the drain-loop above — it must return
  a negative value when no key is waiting. That is how the console stdio is
  retargeted in these projects; if yours blocks, read one key per pass instead.
- **The module prints through `printf` / `putchar`.** Your stdio must already be
  retargeted to the console UART; menusystem neither knows nor cares how.
- **Duplicate keys are caught at run time, not compile time.** The help printer
  runs a conflict check and prints a `WARNING: Menu items share the same key`
  line when two items in one menu share a key. Watch for it when bringing a new
  menu up.
- **The sentinel is mandatory and must be last.** `MENU_ITEM_END_OF_LIST`
  terminates the array; without it the dispatcher walks off the end.
