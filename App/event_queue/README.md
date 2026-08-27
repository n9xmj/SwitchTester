# event_queue — adoption guide

A small variable-length event FIFO for resource-constrained MCUs. Producers —
including ISRs — push records tagged with a 16-bit event ID and carrying 0 to
65535 payload bytes; one consumer pops them into its own storage. Copy-in /
copy-out: the queue never hands out interior pointers. Functionally it is a
scaled-down cousin of a FreeRTOS queue, minus the RTOS: in the single-producer /
single-consumer case it is lock-free and **masks no interrupts at all**.

The module is hardware-independent — C standard library only, no HAL, no
project headers. One `.c`, one `.h`, one config header.

See [`portable-apis-strategy.md`](../../../G0B1_Skeleton/Docs/planning/portable-apis-strategy.md)
for the cross-cutting vendoring model this module follows.

---

## Files

| File | Who owns it |
|---|---|
| `event_queue.c` / `event_queue.h` | **Vendored.** Never edit when adopting. |
| `event_queue_config.h.example` | **Template.** Rename to `event_queue_config.h` and edit. |

> **If you edit an active copy of the example, edit the `.example` too.** The
> `.example` is canonical. Nothing enforces the match, and the usual failure is
> fixing something in your copy and leaving the example stale for whoever
> adopts it next.

---

## Adopting it, in four steps

1. **Copy `App/event_queue/` into your project** and add it to your include path.
2. **Rename `event_queue_config.h.example` to `event_queue_config.h`** (in your
   own include directory) and set the two knobs.
3. **`#include "event_queue.h"`** — the only header your application needs. It
   pulls in your config for you.
4. **Declare a handle, call `x_event_queue_create()`, start putting.**

---

## Sixty seconds of usage

The whole common case — an ISR reporting events to the main loop:

```c
#include "event_queue.h"

/* The module deliberately does NOT type event IDs -- they are generic
 * uint16_t so the core stays application-agnostic. Declare your own. */
enum
{
    EVENT_SAMPLE_READY = 1,
    EVENT_BUTTON,
    EVENT_FAULT
};

static event_queue_handle_t x_events;       /* static => zeroed, see Gotchas */

void v_app_init(void)
{
    /* NULL config = all defaults: EVENT_QUEUE_DEFAULT_SIZE bytes, ring
     * allocated internally, no locking (the SPSC contract, below). */
    if (x_event_queue_create(&x_events, NULL) < 0)
    {
        /* out of memory, or the handle was already live */
    }
}

/* PRODUCER -- safe straight from an ISR. On EQ_ERROR_FULL nothing was
 * written; decide whether dropping this event matters to you. */
void v_adc_isr(void)
{
    uint32_t u32_reading = u32_read_sample();

    (void) x_event_queue_put(&x_events, EVENT_SAMPLE_READY,
                             sizeof(u32_reading), &u32_reading);
}

void v_button_isr(void)
{
    /* Zero-length events are legal -- the ID alone is the message. */
    (void) x_event_queue_put(&x_events, EVENT_BUTTON, 0u, NULL);
}

/* CONSUMER -- the main loop drains until empty. */
void v_poll_events(void)
{
    uint8_t au8_payload[32];
    event_queue_record_t x_record =
    {
        .pv_data      = au8_payload,        /* where the payload lands   */
        .u16_buf_size = sizeof(au8_payload) /* how much of it fits       */
    };

    for (;;)
    {
        event_queue_status_t x_status = x_event_queue_get(&x_events, &x_record);

        if (x_status != EQ_OK && x_status != EQ_STATUS_TRUNCATED)
        {
            break;                          /* EQ_STATUS_EMPTY, or an error */
        }

        /* x_record.u16_id has the event, x_record.u16_data_size the TRUE
         * stored payload size (even when more than u16_buf_size). */
        switch (x_record.u16_id)
        {
            case EVENT_SAMPLE_READY: /* au8_payload holds a uint32_t */ break;
            case EVENT_BUTTON:       /* no payload                   */ break;
            default:                                                   break;
        }
    }
}
```

That is the entire integration. Everything below is variations and contract.

---

## Variation: static ring, config in flash

No heap, ring in your own storage. The config struct is `const`-friendly — the
module copies what it needs and never writes through or retains the pointer:

```c
static uint32_t au32_ring[512u / 4u];   /* uint32_t => 4-byte aligned by type */
static event_queue_handle_t x_events;

static const event_queue_config_t x_config =    /* lives in flash */
{
    .u32_size  = sizeof(au32_ring),
    .pv_buffer = au32_ring,
    /* Every unset field is a deliberate default:
     * .pfn_lock/.pfn_unlock = NULL   no locking (SPSC contract) */
};

void v_app_init(void)
{
    (void) x_event_queue_create(&x_events, &x_config);
}
```

With `EVENT_QUEUE_ENABLE_MALLOC` set to 0 in the config header, the module
never references any allocator and this is the only style available.

---

## Variation: more than one producer

The lock-free guarantee covers exactly ONE producer context and ONE consumer
context. The moment two contexts can call `put` on the same queue — two ISRs,
or an ISR plus the main loop — supply a lock pair. Its only job is serializing
producers **against each other**; the consumer never takes it, so `get` in the
main loop still masks nothing.

```c
static uint32_t u32_saved_primask;

static void v_events_lock(void)             /* the classic single-MCU lock */
{
    uint32_t u32_primask = __get_PRIMASK();
    __disable_irq();
    u32_saved_primask = u32_primask;
}

static void v_events_unlock(void)
{
    __set_PRIMASK(u32_saved_primask);
}

static const event_queue_config_t x_config =
{
    .u32_size   = 512u,
    .pfn_lock   = v_events_lock,
    .pfn_unlock = v_events_unlock,
};
```

**The contract:** the lock must mask every producer context of this queue
except the caller's own — including an ISR producer preempting a main-loop
`put` in progress. On a Cortex-M0+ that means PRIMASK (shown) or `NVIC`
gating of the specific producer IRQs; there is no BASEPRI on that core. The
lock functions are per-queue, so different queues can carry different (or no)
policies. Both pointers must be supplied together or both left NULL — create
rejects one without the other.

---

## The config header

Your `event_queue_config.h` is included **from the middle of `event_queue.h`**
— never include it directly; a guard will stop you.

| Knob | Purpose |
|---|---|
| `EVENT_QUEUE_ENABLE_MALLOC` | `0` compiles the allocator path out entirely — no heap, no `malloc`/`free` reference. A NULL `pv_buffer` then becomes an error instead of a request to allocate. |
| `EVENT_QUEUE_DEFAULT_SIZE` | Ring size used when the config's `u32_size` is 0 — including the all-defaults `create(&h, NULL)`. Static-asserted ≥ minimum and a multiple of 4. |
| `EVENT_QUEUE_MALLOC(size)` / `EVENT_QUEUE_FREE(ptr)` | Optional. Route internal ring allocation through an alternative allocator (RTOS heap, pool). Add whatever `#include` it needs in the config header. Undefined = C library `malloc`/`free`. |

---

## Records and space

On the wire, every record is:

```c
typedef struct
{
    uint16_t u16_id;            /* your event identifier          */
    uint16_t u16_data_size;     /* TRUE payload size, not rounded */
    uint8_t  u8_data[];         /* payload follows                */
}
event_queue_header_t;           /* 4 bytes of overhead per record */
```

A record **occupies** `sizeof(header) + payload` rounded **up** to a multiple
of 4, while `u16_data_size` always reports the true size. So in a 256-byte
ring: a 4-byte payload costs 8 bytes (31 such events fit), a 5-byte payload
costs 12, a zero-length event costs 4.

`u32_event_queue_free_space()` reports payload bytes available to the **next**
put — record overhead already deducted, so a put with `u16_data_size` up to
that value is guaranteed to succeed.

Records may straddle the ring's wrap point internally; you never see that.

---

## Status codes

`event_queue_status_t` is a genuine signed 16-bit enum with one triage rule:

> **Negative = the operation failed in a way that loses data or indicates
> misuse. Positive = it did something defensible you may want to know about.
> Zero = unqualified success.** `if (x_status < 0)` is the designed idiom.

| Code | Meaning |
|---|---|
| `EQ_OK` (0) | Success |
| `EQ_STATUS_EMPTY` (+) | Get/peek: nothing queued — the idle result of every polling loop |
| `EQ_STATUS_TRUNCATED` (+) | Get/peek: payload larger than your buffer; what fits was copied. On a **get** the overflow is discarded with the record; on a **peek** nothing is lost |
| `EQ_STATUS_SIZE_ROUNDED` (+) | Create: size was not a multiple of 4, rounded **down**; the queue is live (net size in the handle's `u32_size`) |
| `EQ_ERROR_PARAMETER` (−) | NULL/invalid argument, half a lock pair, create over a live handle |
| `EQ_ERROR_NOT_INIT` (−) | Handle never created, or already destroyed |
| `EQ_ERROR_FULL` (−) | Put: no room. **Nothing was written** — the event is dropped, and the producer should know |
| `EQ_ERROR_MEMORY` (−) | Create: allocation failed |
| `EQ_ERROR_ALIGNMENT` (−) | Create: caller buffer not 4-byte aligned |
| `EQ_ERROR_SIZE` (−) | Create: net size below the two-header minimum |

Why put-on-full is an error but get-on-empty is not: a failed put **loses an
event** and the producer must be told; an empty get loses nothing — it is how
every drain loop ends.

*(If you also use `nvmparams`: its convention is different — there, positive
values are driver-defined device errors and you must test against
`NVM_ERROR_NONE`, never `< 0`. Here `< 0` is the designed triage.)*

---

## The rest of the API

**`x_event_queue_peek(&h, &record)`** — exactly `get`, but the record stays
queued; the next get or peek sees it again. A truncated peek discards nothing,
so peek-with-small-buffer then get-with-right-sized-buffer is a valid pattern
(as is peek to *decide*, get to *act*). Consumer-context only, like get.

**`x_event_queue_flush(&h)`** — discard everything queued. Drains through the
get path, so it is exactly as safe as get: consumer-context only, producers
may keep putting concurrently. Returns `EQ_OK` once empty — flushing an
already-empty queue is success.

**`x_event_queue_destroy(&h)`** — frees the ring if the module allocated it,
invalidates the handle; every later call on it returns `EQ_ERROR_NOT_INIT`.

**Helpers** — all tolerate NULL/uninitialised handles (empty/zero results):

```c
bool     b_event_queue_is_empty  (&h);      /* exact from the consumer side  */
uint16_t u16_event_queue_count   (&h);      /* records queued; consumer-exact */
uint32_t u32_event_queue_free_space(&h);    /* payload bytes; producer-exact */
```

"Exact from" one side means: called from the other side (or any third
context) the value is a snapshot that can only be conservatively stale —
never dangerously wrong.

**In the get/peek record, every zero is a meaningful default.** `pv_data =
NULL` discards the payload while still returning the ID and true size (an
all-zero record is a valid "consume it and tell me what it was");
`u16_buf_size = 0` copies nothing. The module never writes `pv_data` or
`u16_buf_size`, so a drain loop initialises the record once.

---

## Gotchas

**Give handles static or zeroed storage.** Create refuses a handle that is
already live (re-creating would leak an owned ring), and it detects "live" by
an internal magic word. A stack handle full of garbage has a 1-in-2³²
chance of impersonating a live queue; a static or zeroed one, none.

**Never destroy a queue a producer can still touch.** Destroy frees the ring;
an ISR putting into it afterwards is guarded (magic cleared → `NOT_INIT`),
but an ISR *already inside* `put` when the buffer is freed is not. Quiesce
producers first.

**One consumer means one consumer.** `get`, `peek` and `flush` all mutate or
read the consumer-side cursor; calling them from two contexts is exactly the
race the producer lock does **not** cover. If two contexts must consume,
that is your serialization to provide.

**`EQ_ERROR_FULL` is a dropped event, not back-pressure.** The queue cannot
make the producer wait. If an event must not be lost, size the ring for the
worst-case burst — `u32_event_queue_free_space()` from the producer side is
exact if you want to instrument headroom.

**The counters are internal; do not reach into the handle.** The handle is in
the public header so you can instantiate it, and `u32_size` is fair to read,
but everything else is module state — the helpers exist so you never need it.

**Lock functions come as a pair.** One without the other is a config error
(`EQ_ERROR_PARAMETER`) — a lock that is never released, or a release of a
lock never taken, is a bug the module refuses to help you write.
