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
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key       = 0,
        .text      = "\r\n--- Example Menu ---\r\n"
    },
    {   /* '?' prints the menu (the help listing) */
        .item_type = MENU_ITEM_HELP,
        .key       = '?',
        .text      = NULL
    },
    {   /* Hidden alias: a bare <Enter> reprints the menu, not "unknown key" */
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key       = '\r',
        .text      = NULL
    },
    {   /* A command: press 'x' -> v_example_command() runs */
        .item_type = MENU_ITEM_FUNCTION,
        .key       = 'x',
        .text      = "Example command",
        .function  = v_example_command
    },
    {   /* ESC returns to the parent menu (submenus only -- see note) */
        .item_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .key       = 0x1B,
        .text      = NULL
    },
    {   /* Sentinel -- MUST be last */
        .item_type = MENU_ITEM_END_OF_LIST
    }
};
```

**Home menu vs submenu.** The `RETURN_TO_PREVIOUS_MENU` entry only makes sense in
a submenu. Your *home* menu omits it — there is nowhere to return to — and ends
straight at the sentinel.

**A submenu is just an item that points at another menu array:**

```c
    {
        .item_type = MENU_ITEM_CALL_MENU,   /* or MENU_ITEM_GOTO_MENU */
        .key       = 's',
        .text      = "Enter the example submenu",
        .menu      = x_example_menu
    },
```

**Item types** you will reach for (full list in `menusystem.h`):
`HELP_TEXT_FIXED` (title / section text), `HELP` and `HELP_HIDDEN` (the `?`
printer and its silent aliases), `FUNCTION` (a `void(void)` handler),
`KEY_FUNCTION` (handler receives the key), `KEY_LIST_FUNCTION` (one handler
bound to several keys via `.key_list`), and `GOTO_MENU` / `CALL_MENU` /
`RETURN_TO_PREVIOUS_MENU` / `RETURN_TO_HOME_MENU`.

**`no_newline`.** Set `.no_newline = 1` on an item to suppress the CR/LF after
its text, so the next item continues on the same line — handy for rendering a
row of related items. Default `0` behaves exactly as it always has.

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

        p_c_char_to_str((char) i_key, str_key);   /* renders 0x1B as "ESC", etc. */
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
