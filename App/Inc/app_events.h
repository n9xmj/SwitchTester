/******************************************************************************
 * app_events.h
 *
 * SwitchTester event-path types, IDs and production mask.
 *
 * Header only -- there is no "event module". Producers stack-allocate a record,
 * fill it member by member and call x_event_queue_put() directly. Events are
 * produced inside time-critical ISRs, so a wrapper layer over the queue's own
 * call would buy nothing and cost cycles where they are scarce.
 * See Docs/planning/event-path-plan.md (I7).
 *
 * The parts:
 *
 *   event_control_t       production mask -- a global enable plus one bit per
 *                         source, modelled on an MCU IER. Gating is at
 *                         PRODUCTION: a masked source never enters the queue
 *                         and is counted nowhere.
 *   switch_event_data_t   the record payload. 12 bytes, shared by switch and
 *                         (later) sense sources.
 *   event_class_t         the 16-bit event ID carried in event_queue's own
 *                         record header. It is the event CLASS only -- the
 *                         channel travels in the payload.
 *
 * The mask is deliberately finer-grained than the class ID: switch transitions
 * have a bit per channel per direction, but only two class IDs. The production
 * site knows both, so selecting the bit is free there, while a host filtering
 * by class stays cheap.
 ******************************************************************************/

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

#include "event_queue.h"
#include "switch_out.h"

/*============================================================================
 * TUNABLES
 *==========================================================================*/

/* Ring size in bytes. Generous on purpose: at 16 bytes on the wire per event
 * (12-byte payload + event_queue's 4-byte header) this holds ~512 records,
 * roughly a quarter-second of a 1 mS half-period soak before the consumer must
 * have drained. Must be a multiple of 4. */
#define EVENT_QUEUE_BUFFER_SIZE         8192U

/*----------------------------------------------------------------------------
 * Timestamp seam -- the millisecond member of every record.
 *
 * Retarget by defining EVENT_TICK_MS() before this header, or by editing here.
 * The obvious future target is an application-owned tick incremented in the
 * existing 1 mS TIM14 callback (roadmap B2), which would replace the HAL's.
 *
 * Two things to know about the default:
 *
 *   - It is NOT the same timebase as the record's TIM2 count. uwTick comes from
 *     SysTick, the TIM2 count from TIM2; they free-run separately and drift. Do
 *     not derive one from the other.
 *   - uwTick is writable and this project writes it: the RTC/STOP wake-up
 *     self-test credits slept time back with v_system_tick_add(). The tick
 *     member can therefore jump forward in one step across that test, while
 *     TIM2 -- stopped during STOP1 -- does not.
 *
 * Must be cheap and ISR-safe. HAL_GetTick() is a single aligned 32-bit read.
 *--------------------------------------------------------------------------*/

#ifndef EVENT_TICK_MS
#define EVENT_TICK_MS()                 HAL_GetTick()
#endif

/*============================================================================
 * EVENT CLASS IDs
 *
 * The 16-bit value stored in event_queue's record header. High byte groups a
 * family so a host can filter coarsely; the low byte distinguishes members of
 * it. The CHANNEL is not encoded here -- it is a payload member.
 *==========================================================================*/

typedef enum
{
    EVENT_CLASS_NONE                  = 0x0000U,

    /* Switch family. */
    EVENT_CLASS_SWITCH_MANUAL         = 0x0101U,  /* software-commanded level  */
    EVENT_CLASS_SWITCH_AUTO           = 0x0102U,  /* compare-match transition  */
    EVENT_CLASS_SWITCH_CYCLE_COMPLETE = 0x0103U,  /* campaign finished         */

    /* Sense family -- reserved, producers arrive with roadmap task 1. */
    EVENT_CLASS_SENSE_LEVEL           = 0x0201U
}
event_class_t;

/*============================================================================
 * EVENT RECORD PAYLOAD
 *==========================================================================*/

/*
 * 12 bytes, naturally aligned, no implicit padding. event_queue rounds a stored
 * record up to a multiple of 4, so 12 stays 12.
 *
 * u16_state is 16 bits wide although a switch only ever puts 0 or 1 in it, so a
 * sense channel can place an ADC reading in the same field rather than needing a
 * second record shape.
 *
 * u32_tim_count is the TIM2 (1 uS) counter domain in both cases, but the source
 * differs by hook and the difference matters:
 *
 *   manual     TIM2->CNT   -- the software write IS the edge, so now is exact
 *   automated  CCRx        -- the hardware placed the edge at the compare
 *                            value; CNT read in the ISR would carry ISR latency
 */
typedef struct
{
    uint8_t  u8_channel;        /* 0 = A .. 3 = D                             */
    uint8_t  u8_pad;            /* reserved                                   */
    uint16_t u16_state;         /* 0/1 for a switch; ADC counts for sense     */
    uint32_t u32_tim_count;     /* TIM2 domain -- see above                   */
    uint32_t u32_tick;          /* EVENT_TICK_MS()                            */
}
switch_event_data_t;

_Static_assert(sizeof(switch_event_data_t) == 12,
               "switch_event_data_t must be 12 bytes -- the wire format is "
               "shared with the host tooling");

/*============================================================================
 * PRODUCTION MASK
 *==========================================================================*/

/*
 * Bit assignments. Written whole by the console (main context), read by ISRs;
 * an aligned 32-bit load/store is atomic on Cortex-M0+, so neither side locks.
 *
 * All-zero is the disarmed state, which is also the NVM default and the value
 * the register holds through switch init (see the plan, S1/S4/S6). "Clear the
 * register" and "disarm everything" are therefore the same operation.
 */
#define EVENT_MASK_SWITCH_A_AUTO        (1UL <<  0)
#define EVENT_MASK_SWITCH_B_AUTO        (1UL <<  1)
#define EVENT_MASK_SWITCH_C_AUTO        (1UL <<  2)
#define EVENT_MASK_SWITCH_D_AUTO        (1UL <<  3)
#define EVENT_MASK_SWITCH_A_MANUAL      (1UL <<  4)
#define EVENT_MASK_SWITCH_B_MANUAL      (1UL <<  5)
#define EVENT_MASK_SWITCH_C_MANUAL      (1UL <<  6)
#define EVENT_MASK_SWITCH_D_MANUAL      (1UL <<  7)
#define EVENT_MASK_SENSE_A              (1UL <<  8)
#define EVENT_MASK_SENSE_B              (1UL <<  9)
#define EVENT_MASK_SENSE_C              (1UL << 10)
#define EVENT_MASK_SENSE_D              (1UL << 11)
#define EVENT_MASK_CYCLE_COMPLETE       (1UL << 30)
#define EVENT_MASK_GLOBAL_ENABLE        (1UL << 31)

/* Group masks -- the whole point of the register model is that per-class and
 * per-channel arming are the same mechanism at different granularities. */
#define EVENT_MASK_SWITCH_AUTO_ALL      0x0000000FUL
#define EVENT_MASK_SWITCH_MANUAL_ALL    0x000000F0UL
#define EVENT_MASK_SWITCH_ALL           0x000000FFUL
#define EVENT_MASK_SENSE_ALL            0x00000F00UL
#define EVENT_MASK_ALL_SOURCES          0x40000FFFUL

/* The base bit of each per-channel group; the channel index shifts it. */
#define EVENT_MASK_SWITCH_AUTO_SHIFT    0U
#define EVENT_MASK_SWITCH_MANUAL_SHIFT  4U
#define EVENT_MASK_SENSE_SHIFT          8U

/*
 * Every flag is bool:1 and the filler is uint32_t:18 -- a bool bitfield cannot
 * exceed width 1, which is a hard compile error rather than a silent short
 * fill. Verified on arm-none-eabi-gcc 14.3.1 for cortex-m0plus: 4 bytes, bit 0
 * = 0x00000001, bit 30 = 0x40000000, bit 31 = 0x80000000.
 */
typedef union
{
    uint32_t u32_all;

    struct
    {
        bool     b_switch_a_auto_events   : 1;      /* 0  */
        bool     b_switch_b_auto_events   : 1;      /* 1  */
        bool     b_switch_c_auto_events   : 1;      /* 2  */
        bool     b_switch_d_auto_events   : 1;      /* 3  */
        bool     b_switch_a_manual_events : 1;      /* 4  */
        bool     b_switch_b_manual_events : 1;      /* 5  */
        bool     b_switch_c_manual_events : 1;      /* 6  */
        bool     b_switch_d_manual_events : 1;      /* 7  */
        bool     b_sense_a_events         : 1;      /* 8  */
        bool     b_sense_b_events         : 1;      /* 9  */
        bool     b_sense_c_events         : 1;      /* 10 */
        bool     b_sense_d_events         : 1;      /* 11 */

        uint32_t _u32_unused              : 18;     /* 12..29 */

        bool     b_global_switch_cycle_complete_event : 1;  /* 30 */
        bool     b_global_event_enable    : 1;      /* 31 */
    };
}
event_control_t;

_Static_assert(sizeof(event_control_t) == 4,
               "event_control_t must be exactly 4 bytes -- it is stored as a "
               "single NVM object and written as one atomic word");

/*============================================================================
 * SHARED STATE
 *
 * Defined in switch_out.c. All low-level switch manipulation lives in that one
 * module, and the event plumbing rides along with it rather than fragmenting
 * into another; sense producers will include this header and put directly too.
 *==========================================================================*/

extern event_queue_handle_t    g_x_event_queue;
extern volatile event_control_t g_x_event_control;

/*============================================================================
 * PRODUCTION GATE
 *==========================================================================*/

/*
 * Is this (class, channel) armed?
 *
 * ALWAYS_INLINE, not merely `inline`. This sits in the TIM2 compare ISR inside a
 * 4 uS edge-scheduling budget, and the builds that matter are -Og (Debug) and
 * -Os (Release) -- neither of which inlines a multi-call-site function on its
 * own. Left to itself the compiler emitted this as a real 100-byte function, so
 * a MASKED source -- the common case during a soak -- paid a call frame just to
 * be told "no". Optimising this one function for speed rather than size, without
 * changing the project's optimisation level.
 *
 * Inlining also constant-folds the switch away: u16_class is a literal at every
 * production site, so what survives is one load, one test and one bit test.
 *
 * ONE snapshot of the whole register, then bits are tested out of the local.
 * Testing the register twice would let a console write land between the global
 * check and the source check and yield a combination that never existed.
 */
__attribute__((always_inline))
static inline bool b_event_armed(uint16_t u16_class, uint8_t u8_channel)
{
    const uint32_t u32_mask = g_x_event_control.u32_all;
    uint32_t       u32_bit;

    if ((u32_mask & EVENT_MASK_GLOBAL_ENABLE) == 0UL)
    {
        return false;
    }

    switch (u16_class)
    {
        case EVENT_CLASS_SWITCH_MANUAL:
            u32_bit = 1UL << (EVENT_MASK_SWITCH_MANUAL_SHIFT + u8_channel);
            break;

        case EVENT_CLASS_SWITCH_AUTO:
            u32_bit = 1UL << (EVENT_MASK_SWITCH_AUTO_SHIFT + u8_channel);
            break;

        case EVENT_CLASS_SWITCH_CYCLE_COMPLETE:
            /* One global bit, not one per channel: the record already carries
             * the channel, and a campaign completion is rare enough that
             * per-channel arming would not buy anything. */
            u32_bit = EVENT_MASK_CYCLE_COMPLETE;
            break;

        case EVENT_CLASS_SENSE_LEVEL:
            u32_bit = 1UL << (EVENT_MASK_SENSE_SHIFT + u8_channel);
            break;

        default:
            return false;
    }

    return ((u32_mask & u32_bit) != 0UL);
}

/*============================================================================
 * LIFECYCLE
 *==========================================================================*/

/* Create the queue. Call during system init, before anything can produce. The
 * queue lives for the life of the device and is never destroyed; it can be
 * flushed on demand with x_event_queue_flush(). */
extern void v_event_queue_init(void);

/* Create the mask's NVM object with its all-disarmed default. Called from
 * v_switch_out_nvm_init(), with the other persisted parameters, so a virgin
 * pool writes every object in one flash write. Does NOT read the value back. */
extern void v_event_control_nvm_init(void);

/* Park the live mask without touching the persisted copy: every source goes
 * quiet, and v_event_control_restore() puts back whatever NVM holds.
 *
 * The pair brackets an automation-console session (plan S2b). A host therefore
 * always starts from a known all-disabled state and cannot leave the human
 * console's arming disturbed on the way out -- while an acon command that asks
 * to persist writes the NVM copy, so the restore hands that same value back and
 * a deliberate change still sticks. One register, no per-production-site
 * context test; the cost is two calls per session rather than one per event. */
extern void v_event_control_suspend(void);

/* Read the persisted mask into the live register. Also the acon session-exit
 * half of the pair above. Deliberately deferred at boot until
 * AFTER switch/sense init: the register is all-zero until this runs, so the
 * forced-off writes in v_switch_out_init() produce nothing. Keep this call
 * where it is in the init order -- moving it earlier resurrects those events. */
extern void v_event_control_restore(void);

/* Persist the current mask: copies the live register into the NVM pool's RAM
 * shadow. The flash write itself is the pool's deferred auto-commit (or the
 * next explicit commit), so a burst of calls costs one erase, not one each.
 * Returns false if the pool rejected the store. */
extern bool b_event_control_nvm_save(void);

#endif /* APP_EVENTS_H */
