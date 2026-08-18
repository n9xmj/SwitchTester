# nvmparams — adoption guide

A lean non-volatile parameter store for resource-constrained MCUs. Named objects, held in a RAM
pool, written to your storage device in one operation when you ask. Architecturally inspired by
Espressif's NVS API, stripped down to run on low-end parts — it first shipped on an STM32G030.

**It is not a filesystem substitute.** Object IDs are 16-bit and object sizes are 16-bit, on
purpose. If you need large objects, add a filesystem.

The module is hardware-independent: it contains **no storage drivers**. You supply read and
write functions as pointers, so the same core runs against internal flash, SPI or I²C flash,
a file, or nothing at all.

See [`portable-apis-strategy.md`](../../../G0B1_Skeleton/Docs/planning/portable-apis-strategy.md)
for the cross-cutting vendoring model this module follows.

---

## Files

| File | Who owns it |
|---|---|
| `nvmparams.c` / `nvmparams.h` | **Vendored.** Never edit when adopting. |
| `nvmparams_internal.h` | **Vendored.** Test-only internals; `nvmparams.h` does not include it. |
| `nvmparams_config.h.example` | **Template.** Rename to `nvmparams_config.h` and edit. |
| `nvm_driver_*.c.example` | **Examples.** Not part of the module. Use, modify, or delete. |
| `nvm_list.c.example` / `nvm_list.h` | **Example.** Pool dump diagnostic; the only user of `<stdio.h>`. |

`*.c.example` and `*.h.example` are not compiled until renamed — no build system's source glob
matches a double extension, so the exclusion is structural rather than something you must
remember to configure.

> **If you edit an active copy of an example, edit the `.example` too.** The `.example` is
> canonical. Nothing enforces the match, and the usual failure is fixing a bug in your copy and
> leaving the example stale for whoever adopts it next.

---

## Adopting it, in five steps

1. **Copy `App/nvmparams/` into your project** and add it to your include path.
2. **Rename one `nvm_driver_*.c.example`** to `.c` — in place, or after copying it into your own
   source directory — and add it to your build. Or write your own; see *Writing a driver*.
3. **Rename `nvmparams_config.h.example` to `nvmparams_config.h`** and fill it in.
4. **`#include "nvmparams.h"`** — the only header your application needs. It pulls in your
   config for you.
5. **Declare a pool handle, write a config literal, call `x_nvm_pool_init()`.**

No other file is edited, nothing else is copied, and no build-exclusion setting is required.

---

## The config header

Your `nvmparams_config.h` is the module's control panel. It is included **from the middle of
`nvmparams.h`** — never include it directly; a guard will stop you.

| Knob | Purpose |
|---|---|
| `NVM_POOL_SIZE_DEFAULT` | Your usual pool size. Static-asserted against `NVM_POOL_SIZE_MIN`. Not a fallback: `u32_size` must always be set explicitly. |
| `NVM_LABEL_MAX_LENGTH` | Label length. **Set once, never change** — see *Gotchas*. Must be a multiple of 4. |
| `NVM_ENABLE_INTERNAL_MALLOC` | `0` compiles out `malloc`/`free` entirely, for projects with no heap. Then a NULL RAM buffer is an error rather than a request to allocate. |
| `NVM_LOG_ERROR(fmt, ...)` | Optional. Undefined means the module logs nothing and names no logging library. |
| Your parameter-ID enum | See *Parameter IDs*. |
| Your driver externs | Declared here, not in the module — so a driver you modify never requires editing a vendored file. |

**Anything the config references must exist in Part A of `nvmparams.h`** — above the include
point. In practice everything does except `nvm_header_t`, which embeds `NVM_LABEL_MAX_LENGTH`
and therefore cannot.

**Any `#if` on a config symbol must appear after `#include "nvmparams.h"`.** Test one earlier
and it silently reads as 0.

---

## Parameter IDs

Declare an ordinary enum, anchored at `NVM_ID_APP_FIRST`, ending with a marker you assert on:

```c
typedef enum
{
    NVM_PARAM_FIRST_THING = NVM_ID_APP_FIRST,
    NVM_PARAM_SECOND_THING,
    /* ... */
    NVM_PARAM_APP_LAST              /* marker, not a parameter */
}
app_nvm_param_t;

_Static_assert(NVM_PARAM_APP_LAST <= NVM_ID_APP_MAX, "too many NVM parameter IDs");
```

Pass the labels straight to the API — they convert implicitly, no cast needed. If you store an
ID in a *variable*, type it `nvm_param_id_t`, not your enum type.

| Range | Owner |
|---|---|
| `0x0000` | `NVM_PARAM_UNUSED` — marks a deleted object |
| `0x0001` … `0xFEFF` | **Yours** |
| `0xFF00` … `0xFFFF` | Reserved to nvmparams |

`x_nvm_create()`, `x_nvm_set()` and `x_nvm_delete()` **reject** a reserved ID at runtime with
`NVM_ERROR_ID_RESERVED`. `x_nvm_get()` deliberately does **not** — reads of module-owned objects
are harmless and useful for diagnostics. So `get` will succeed on an ID that `set` refuses.

The runtime check exists because a static assert only sees IDs you *declared*; it is blind to
computed ones, and IDs are routinely computed:

```c
x_id = NVM_PARAM_CYCLE_A_REPEAT + (channel * PARAMS_PER_CHANNEL) + parameter;
```

**Add new parameters at the end.** Inserting in the middle renumbers everything after it and
orphans the matching objects in any pool already written.

If you are adopting into a project whose legacy IDs already sit in `0xFF00`+, define
`NVM_ID_APP_MAX` higher before including the header. That forfeits headroom for future module
features, but avoids renumbering a deployed pool.

---

## Writing a driver

A driver moves `u32_size` bytes between `p_v_data` and `ux_address`. That is the whole contract.

```c
nvm_error_t my_read (const nvm_media_t *p_x_media);
nvm_error_t my_write(const nvm_media_t *p_x_media);
```

**It owes:** parameter validation, and reporting **physical device access errors**.

**It does not owe:** integrity checking — deciding whether data is valid is the module's job —
or any awareness of wear levelling. `ux_address` arrives already resolved.

**Returns:** `NVM_ERROR_NONE`, or a negative module code. **Positive values are yours** to define
as device-specific errors; they reach the caller unchanged, so a bus status survives to
somewhere useful.

`ux_address` is an integer rather than a pointer because it means different things per backend:

| Backend | `ux_address` is |
|---|---|
| MCU internal flash | an address in the memory map |
| RAM emulation | literally a pointer to the store |
| SPI / I²C flash | a byte offset in the device |
| File | an `lseek`/`fseek` offset |

Start from `nvm_driver_ram.c.example` — it is the smallest driver that can exist, so the
contract is what is left showing.

**Leaving both pointers NULL is valid**, not an error: the RAM pool behaves normally and nothing
persists. Useful for bring-up before storage works.

**A driver must create its own backing store if that is meaningful.** A file driver meeting a
nonexistent file should create it and return blank data, rather than reporting an error — see
*Init policy*.

---

## Minimal integration

```c
#include "nvmparams.h"

extern uint32_t _nvm_start;                 /* from the linker script */

nvm_pool_t g_x_pool;                        /* export via your own header */

static const nvm_pool_config_t x_config =   /* const: the module never writes to it */
{
    .p_c_label       = "PARAMS",
    .pfn_read        = my_read,
    .pfn_write       = my_write,
    .ux_base_address = (uintptr_t) &_nvm_start,
    .u32_size        = NVM_POOL_SIZE_DEFAULT,
    .u32_alloc_unit  = FLASH_PAGE_SIZE,     /* device erase granularity */

    /* Every unset field is a deliberate default:
     * .pfn_crc        = NULL   signature-only validation
     * .p_v_ram_buffer = NULL   allocate internally
     * .p_v_context    = NULL   nothing to pass the driver
     * .u8_wear_blocks = 0      no wear levelling
     * .x_init_policy  = 0      NVM_INIT_FORMAT_IF_BLANK
     */
};

void v_param_init(void)
{
    nvm_error_t x_status = x_nvm_pool_init(&g_x_pool, &x_config);
    if (x_status != NVM_ERROR_NONE) { /* see Init policy */ }

    /* Create every parameter with its default, then read back what is stored.
     * On a virgin pool this writes defaults; on later boots create is a no-op
     * and the get supplies the saved value. */
    u32_my_param = MY_DEFAULT;
    x_nvm_create(&g_x_pool, NVM_PARAM_MY_PARAM, sizeof(u32_my_param), &u32_my_param);
    x_nvm_get   (&g_x_pool, NVM_PARAM_MY_PARAM, &u32_my_param);

    x_nvm_commit(&g_x_pool);   /* one write for all of it */
}
```

The config may be a ROM constant or a stack temporary — the module copies what it needs and does
not retain the pointer.

**Test results against `NVM_ERROR_NONE`.** Never `< 0`, and never against a list of known codes:
a driver may return a positive value you have never heard of.

---

## Init policy

`x_nvm_pool_init()` classifies the media four ways and applies the policy you chose.

| Outcome | How it is known |
|---|---|
| **Valid** | signature (and CRC, if supplied) check out |
| **Blank** | uniformly `0xFF` or `0x00` — both erase polarities |
| **Corrupt** | neither blank nor valid |
| **Unreadable** | the driver returned an error |

| Policy | Blank | Corrupt | Unreadable |
|---|---|---|---|
| `NVM_INIT_FORMAT_IF_BLANK` *(default)* | format | **abort** | abort |
| `NVM_INIT_FORMAT_IF_INVALID` | format | reformat | abort |
| `NVM_INIT_REQUIRE_VALID` | **abort** | abort | abort |

**An unreadable device never triggers a format, under any policy.** Writing to a device you
could not read is how a transient fault — a loose bus line, a device not yet powered — becomes
permanent data loss.

`NVM_INIT_REQUIRE_VALID` is for a pool provisioned **out of band**, e.g. programmed at
manufacture. Do not use it for a pool your product provisions itself on first boot: it aborts on
blank media, so a virgin unit would never come up. For that case use `FORMAT_IF_BLANK` and add
your own "provisioned" marker parameter, so you can tell *virgin* from *provisioned and since
lost* — the pool alone cannot distinguish them.

Two returns report a format rather than a failure: `NVM_ERROR_POOL_FORMATTED` (media was blank,
nothing lost) and `NVM_ERROR_POOL_REFORMATTED` (**data was destroyed**).

**A failed init leaves the pool unusable**, with its data pointer NULL, so every later call
returns `NVM_ERROR_PARAMETER` rather than operating on a pool that was never loaded.

---

## Committing

Changes live in RAM until `x_nvm_commit()`. That is deliberate: writes are slow and consume
device endurance, so you choose when they happen.

- `x_nvm_set()` compares first — setting a parameter to its existing value does not dirty
  the pool.
- `x_nvm_commit()` returns `NVM_ERROR_NO_CHANGE` when there was nothing to write. Not an error.
- `x_nvm_create()` returns `NVM_ERROR_OBJECT_EXISTS` when the object was already there. **Also
  not an error** — it is the expected result on every boot after the first, and it is how you
  detect a first boot.

### Auto-commit timer

The module keeps a counter; the policy is yours.

```c
v_nvm_commit_timer_tick(&g_x_pool, PERIODIC_INTERVAL_MS);      /* from a periodic timer */

if (b_nvm_commit_time_elapsed(&g_x_pool, COMMIT_DELAY_MS))     /* in your main loop */
{
    x_nvm_commit(&g_x_pool);
}
```

Both calls are no-ops while nothing needs committing, so call them unconditionally. The counter
saturates rather than wrapping.

**It measures time since the LAST change, not the first**, so a pool under continuous
modification defers indefinitely. That is intended — it minimises writes. Call `x_nvm_commit()`
directly wherever a commit must happen regardless, such as before power-down.

`b_nvm_commit_time_elapsed()` is **level-triggered**: it stays true until something resets the
counter, which a successful commit does. If a commit fails it stays true and you will retry;
call `v_nvm_commit_timer_reset()` if you want back-off.

---

## Reserving flash in your linker script

Only for an MCU-internal-flash pool. This is SwitchTester's `STM32G0B1RETX_FLASH.ld`:

```ld
MEMORY
{
  RAM       (xrw) : ORIGIN = 0x20000000, LENGTH = 144K
  FLASH     (rx)  : ORIGIN = 0x8000000,  LENGTH = 510K   /* <- 512K MINUS the NVM sector */
  /* NVM_FLASH at the END of flash, aligned to a sector start, sized to the
     sector size (2K on STM32G0) */
  NVM_FLASH (r)   : ORIGIN = 0x807F800,  LENGTH = 2K
}

  .nvmdata (NOLOAD) :
  {
    . = ALIGN(8);
    _nvm_start = .;
    *(.nvmdata)
    *(.nvmdata*)
    . = ALIGN(8);
    _nvm_end = .;
  } >NVM_FLASH
```

Three things to get right:

- **Reduce `FLASH`'s LENGTH** by the reserved size. Forget this and the regions overlap: the
  linker will happily place code where the pool is about to be erased.
- **`(NOLOAD)`** is what makes parameters survive a firmware update — and is also why a foreign
  pool can outlive the code that wrote it. See *Gotchas*.
- **`_nvm_start`** is how you supply the address without the module knowing anything about
  linker scripts. No `__attribute__((section))` is needed anywhere.

---

## Gotchas

**A `(NOLOAD)` sector survives reflashing.** A previous project's pool, at the same address,
with a valid signature and self-consistent contents, will be read as yours. A CRC does not help
— the pool is intact, just foreign. If parameters come up as plausible nonsense on a shared
development board, suspect this first.

**`NVM_LABEL_MAX_LENGTH` is part of the on-media layout.** Change it after a pool exists and
every object shifts; the data is not stale but *misread*, while still carrying a valid
signature. Set it once at adoption.

**Enabling a CRC invalidates every existing pool.** Pools written without one carry a
placeholder in the CRC field, so the first boot afterwards sees a valid signature and a failing
check — which counts as *corrupt*, and the default policy aborts on corrupt. Make that
transition deliberately: one boot under `FORMAT_IF_INVALID`, or erase the pool.

**`x_nvm_pool_release()` commits before releasing.** If you are deliberately corrupting a pool
for a test, clear `u8_need_commit` first or the release will write over your setup.

**A reformat restarts `u32_write_count`.** Formatting zero-wipes the pool. This is deliberate —
carrying a possibly-corrupt count forward is the dangerous direction — but it means the count is
not a lifetime write total across reformats.

**Renumbering IDs orphans stored objects.** Anchoring your enum at `NVM_ID_APP_FIRST` renumbers
everything if you were previously using different base values; expect one reformat.
