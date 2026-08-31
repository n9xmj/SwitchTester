/******************************************************************************
 * switch_out.c
 *
 * SWITCH_A..D output control -- see switch_out.h for the drive scheme.
 *
 * External hardware (active HIGH): each SWITCH_x output feeds a 330R + LED
 * indicator (~4.5 mA) and, ahead of the 330R, a CD4066 analog switch control
 * input which needs the full 0 .. 3.3 V swing. Push-pull AF output delivers
 * both without any special handling.
 *
 * Design decisions and their rationale: Docs/planning/switch-cycling-plan.md
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include "device_config.h"          /* stdint/stdio, main.h, platform.h, globals.h */
#include "tim.h"                     /* htim2 */
#include "stm32g0xx_ll_tim.h"        /* LL_TIM_OC_SetMode / LL_TIM_OC_GetMode */
#include "jobs.h"
#include "nvmparams.h"
#include "switch_out.h"
#include "app_events.h"

/*============================================================================
 * CHANNEL MAP
 *==========================================================================*/

typedef struct
{
    uint32_t    u32_ll_channel;     /* LL_TIM_CHANNEL_CHx -- OCxM access      */
    uint32_t    u32_hal_channel;    /* TIM_CHANNEL_x      -- HAL start/stop   */
    uint32_t    u32_ccif;           /* TIM_SR_CCxIF       -- compare flag     */
    uint32_t    u32_ccie;           /* TIM_DIER_CCxIE     -- compare enable   */
    volatile uint32_t *p_u32_ccr;   /* &TIM2->CCRx                            */
    GPIO_TypeDef *p_x_gpio_port;    /* For reading the pad back through IDR   */
    uint16_t    u16_gpio_pin;
    const char *pc_name;            /* "A".."D"                               */
    const char *pc_pin_name;        /* Where to put the scope probe           */
}
switch_out_map_t;

static const switch_out_map_t x_switch_map[SWITCH_OUT_COUNT] =
{
    { LL_TIM_CHANNEL_CH1, TIM_CHANNEL_1, TIM_SR_CC1IF, TIM_DIER_CC1IE, &TIM2->CCR1, SWITCH_A_GPIO_Port, SWITCH_A_Pin, "A", "PC4"  },
    { LL_TIM_CHANNEL_CH2, TIM_CHANNEL_2, TIM_SR_CC2IF, TIM_DIER_CC2IE, &TIM2->CCR2, SWITCH_B_GPIO_Port, SWITCH_B_Pin, "B", "PC5"  },
    { LL_TIM_CHANNEL_CH3, TIM_CHANNEL_3, TIM_SR_CC3IF, TIM_DIER_CC3IE, &TIM2->CCR3, SWITCH_C_GPIO_Port, SWITCH_C_Pin, "C", "PB10" },
    { LL_TIM_CHANNEL_CH4, TIM_CHANNEL_4, TIM_SR_CC4IF, TIM_DIER_CC4IE, &TIM2->CCR4, SWITCH_D_GPIO_Port, SWITCH_D_Pin, "D", "PB11" }
};

/*============================================================================
 * MODULE STATE
 *==========================================================================*/

switch_cycle_t g_x_switch_cycle[SWITCH_OUT_COUNT];

/* Manual-menu pulse width, milliseconds. NVM-backed. */
static uint32_t u32_switch_pulse_width_ms;

#define SWITCH_PULSE_WIDTH_DEFAULT_MS   100UL

/* Pulse countdowns, in PERIODIC_TIMER_INTERVAL_MS units. Written by both the
 * foreground (starting/cancelling a pulse) and the periodic ISR (counting it
 * down), so foreground updates are made with interrupts masked -- the ISR does
 * a read-modify-write and would otherwise be able to resurrect a stale count. */
static volatile uint32_t u32_pulse_remaining_ms[SWITCH_OUT_COUNT];

/* Minimum gap between "now" and a freshly programmed compare value. If the
 * counter has already reached the computed match, the compare will not fire
 * again until CNT wraps -- 2^32 ticks, 71.6 minutes at 1 uS/tick. */
#define SWITCH_CYCLE_MIN_LEAD_US        4UL

/* The NVM ID arithmetic in x_switch_cycle_nvm_id() assumes the cycling IDs are
 * contiguous, grouped by channel, in repeat/on/off order. */
_Static_assert(NVM_PARAM_CYCLE_A_ON_US  == NVM_PARAM_CYCLE_A_REPEAT + 1,
               "cycling NVM IDs must be contiguous in repeat/on/off order");
_Static_assert(NVM_PARAM_CYCLE_B_REPEAT == NVM_PARAM_CYCLE_A_REPEAT + SWITCH_CYCLE_PARAM_COUNT,
               "cycling NVM IDs must be grouped by channel with a 3-ID stride");
_Static_assert(NVM_PARAM_CYCLE_D_OFF_US == NVM_PARAM_CYCLE_A_REPEAT
                                           + (SWITCH_OUT_COUNT * SWITCH_CYCLE_PARAM_COUNT) - 1,
               "cycling NVM ID block must cover exactly SWITCH_OUT_COUNT channels");

/*============================================================================
 * EVENT PATH -- queue instance, production mask, NVM
 *
 * These are not switch-specific, but they live here rather than in a module of
 * their own: the plumbing is header-defined (app_events.h) and only the storage
 * needs a .c home. Sense producers will include the same header and put
 * directly. See Docs/planning/event-path-plan.md (I7).
 *==========================================================================*/

/* Declared uint32_t rather than uint8_t so the buffer is 4-byte aligned --
 * x_event_queue_create() requires it and rejects anything else. */
static uint32_t u32_event_queue_buffer[EVENT_QUEUE_BUFFER_SIZE / sizeof(uint32_t)];

event_queue_handle_t     g_x_event_queue;

/* .bss, so it starts all-zero = every source disarmed. That is both the NVM
 * default and what makes v_switch_out_init()'s forced-off writes silent: the
 * persisted value is not read back until v_event_control_restore(), long after
 * the outputs are up. */
volatile event_control_t g_x_event_control;

/*
 * Producer serialization. This queue has THREE producer contexts -- the main
 * loop, the TIM14 tick ISR (pulse expiry) and the TIM2 compare ISR -- so the
 * module's default single-producer lock-free mode is not safe here and the
 * lock pair is mandatory, not optional.
 *
 * Save/restore rather than unconditional enable, so a put from inside an
 * already-masked region does not re-enable interrupts on the way out.
 */
static uint32_t u32_event_lock_primask;

static void v_event_queue_lock(void)
{
    u32_event_lock_primask = __get_PRIMASK();
    __disable_irq();
}

static void v_event_queue_unlock(void)
{
    __set_PRIMASK(u32_event_lock_primask);
}

void v_event_queue_init(void)
{
    const event_queue_config_t x_config =
    {
        .u32_size   = sizeof(u32_event_queue_buffer),
        .pv_buffer  = u32_event_queue_buffer,
        .pfn_lock   = v_event_queue_lock,
        .pfn_unlock = v_event_queue_unlock
    };

    (void) x_event_queue_create(&g_x_event_queue, &x_config);
}

/*
 * Emit one event, if its source is armed.
 *
 * The record is a stack temporary filled member by member; x_event_queue_put()
 * copies it into the ring. A drop (queue full) is counted by the queue itself
 * and reported side-channel; there is no synthetic in-band record.
 *
 * ALWAYS_INLINE. This exists only so the same six lines are not written at three
 * production sites -- it must not cost a call frame for the privilege, and at
 * -Og / -Os the compiler will not inline a three-call-site function by itself
 * (measured: it emitted a real 64-byte function calling a real 100-byte
 * b_event_armed(), so a masked source paid two nested frames to learn it was
 * masked). Forced here rather than by changing the project's optimisation level,
 * because this is the only place that wants speed over size.
 *
 * With both inlined, an unarmed source costs a load, a test and a branch.
 */
__attribute__((always_inline))
static inline void v_event_emit(uint16_t u16_class,
                                uint8_t  u8_channel,
                                uint16_t u16_state,
                                uint32_t u32_tim_count)
{
    switch_event_data_t x_data;

    if (!b_event_armed(u16_class, u8_channel))
    {
        return;
    }

    x_data.u8_channel    = u8_channel;
    x_data.u8_pad        = 0U;
    x_data.u16_state     = u16_state;
    x_data.u32_tim_count = u32_tim_count;
    x_data.u32_tick      = EVENT_TICK_MS();

    (void) x_event_queue_put(&g_x_event_queue, u16_class,
                             (uint16_t) sizeof(x_data), &x_data);
}

/*============================================================================
 * PRIVATE - LEVEL CONTROL
 *==========================================================================*/

/* Set the drive level by rewriting the channel's OCxM field. This is the only
 * place a switch output level is changed directly.
 *
 * Also the manual-event production site. Note it emits on every level REQUEST,
 * including a redundant one -- the output may already be at the asked-for
 * level. That is deliberate: the semantics are "a level was commanded", not
 * "the pin changed", and the record carries the requested state so a consumer
 * can tell. Two consequences worth expecting rather than debugging: a campaign
 * ending emits a manual OFF on an already-low output alongside the
 * cycle-complete, and a manual set on a cycling channel emits two records (the
 * implicit stop's OFF, then the requested level). See the plan, S7.
 *
 * TIM2->CNT is exact here: this write IS the edge. */
static void v_switch_out_force(uint8_t u8_channel, uint8_t u8_on)
{
    LL_TIM_OC_SetMode(TIM2,
                      x_switch_map[u8_channel].u32_ll_channel,
                      u8_on ? LL_TIM_OCMODE_FORCED_ACTIVE
                            : LL_TIM_OCMODE_FORCED_INACTIVE);

    v_event_emit(EVENT_CLASS_SWITCH_MANUAL, u8_channel, u8_on, TIM2->CNT);
}

static void v_switch_out_cancel_pulse(uint8_t u8_channel)
{
    SAVE_AND_DISABLE_INTERRUPTS();
    u32_pulse_remaining_ms[u8_channel] = 0;
    RESTORE_INTERRUPTS();
}

/*============================================================================
 * PRIVATE - CYCLING
 *==========================================================================*/

/*
 * Stop a channel and park it in the manual idle state. Safe to call from the
 * TIM2 ISR: the DIER read-modify-write cannot be preempted there (TIM2 is the
 * highest-priority interrupt and cannot preempt itself). Foreground callers go
 * through v_switch_cycle_stop(), which adds the interrupt mask.
 */
static void v_switch_cycle_halt(uint8_t u8_channel)
{
    const switch_out_map_t *p_x_map = &x_switch_map[u8_channel];

    TIM2->DIER &= ~p_x_map->u32_ccie;       /* disarm before anything else */
    TIM2->SR = ~p_x_map->u32_ccif;          /* drop a match already latched */
    v_switch_out_force(u8_channel, 0);      /* assert LOW, not merely "stop" */

    g_x_switch_cycle[u8_channel].u8_running = 0;
}

/*
 * Program the next compare, <u32_delta> microseconds after the *previous*
 * compare rather than after "now". Chaining edge-to-edge keeps ISR latency from
 * accumulating into the waveform -- the hardware places every edge, and a late
 * ISR costs nothing at all until it exceeds the phase time.
 *
 * Arithmetic is modulo 2^32 throughout, which is only coherent because TIM2's
 * ARR is 0xFFFFFFFF and the counter therefore shares the wrap point of
 * uint32_t. Do not "tidy" ARR.
 */
static void v_switch_cycle_schedule(uint8_t u8_channel, uint32_t u32_delta)
{
    const switch_out_map_t *p_x_map = &x_switch_map[u8_channel];
    uint32_t u32_next = *(p_x_map->p_u32_ccr) + u32_delta;

    /* Signed difference is the correct comparison across a wrapping counter; a
     * plain < is wrong. If the counter has already passed (or is about to pass)
     * the computed match, push it out rather than lose a full wrap. */
    if ((int32_t) (u32_next - TIM2->CNT) < (int32_t) SWITCH_CYCLE_MIN_LEAD_US)
    {
        u32_next = TIM2->CNT + SWITCH_CYCLE_MIN_LEAD_US;
    }

    *(p_x_map->p_u32_ccr) = u32_next;
}

/*
 * One compare match on a cycling channel. The output has *already* changed --
 * the hardware did it -- so this only decides what happens next.
 */
static void v_switch_cycle_advance(uint8_t u8_channel)
{
    switch_cycle_t *p_x_cycle = &g_x_switch_cycle[u8_channel];
    uint32_t u32_ll_channel = x_switch_map[u8_channel].u32_ll_channel;
    uint32_t u32_delta;

    /* Capture the compare value that PRODUCED the edge, before
     * v_switch_cycle_schedule() overwrites it with the next one. This -- not
     * TIM2->CNT -- is the exact edge time: the hardware placed the edge at the
     * match, so CNT read here would carry ISR entry latency instead.
     *
     * The level after the edge follows from the phase we are leaving: an ON
     * phase ends with the output driven low, an OFF phase with it driven high. */
    const uint32_t u32_edge_count = *(x_switch_map[u8_channel].p_u32_ccr);
    const uint16_t u16_new_state  =
        (p_x_cycle->u8_phase == SWITCH_CYCLE_PHASE_ON) ? 0U : 1U;

    if (p_x_cycle->u8_phase == SWITCH_CYCLE_PHASE_ON)
    {
        /* The match just drove the output low, completing an ON pulse. Count it
         * here, and stop here if that was the last one -- the output is already
         * low, so the run ends released with no runt pulse and no trailing
         * dead time. */
        p_x_cycle->u32_cycles_done++;

        if (p_x_cycle->u32_repeat_count
            && (p_x_cycle->u32_cycles_done >= p_x_cycle->u32_repeat_count))
        {
            /* No reschedule on this path, so there is no lead budget to
             * protect and the puts can sit wherever reads best. The final
             * edge is reported first, then the campaign completion; the halt
             * between them emits its own manual OFF (S7). */
            v_event_emit(EVENT_CLASS_SWITCH_AUTO, u8_channel,
                         u16_new_state, u32_edge_count);

            v_switch_cycle_halt(u8_channel);
            v_job_add_with_params(NULL, JOB_CYCLE_COMPLETE, u8_channel, 0);

            v_event_emit(EVENT_CLASS_SWITCH_CYCLE_COMPLETE, u8_channel,
                         u16_new_state, u32_edge_count);
            return;
        }

        p_x_cycle->u8_phase = SWITCH_CYCLE_PHASE_OFF;
        u32_delta = p_x_cycle->u32_off_time_us;
        LL_TIM_OC_SetMode(TIM2, u32_ll_channel, LL_TIM_OCMODE_ACTIVE);
    }
    else
    {
        /* The match just drove the output high, starting an ON pulse. */
        p_x_cycle->u8_phase = SWITCH_CYCLE_PHASE_ON;
        u32_delta = p_x_cycle->u32_on_time_us;
        LL_TIM_OC_SetMode(TIM2, u32_ll_channel, LL_TIM_OCMODE_INACTIVE);
    }

    /* Next edge FIRST, event second. SWITCH_CYCLE_MIN_LEAD_US is 4 uS -- 256
     * cycles at 64 MHz -- and x_event_queue_put() is a meaningful fraction of
     * that, so producing before rescheduling would eat the lead budget and
     * start pushing edges out. See the plan, I8. */
    v_switch_cycle_schedule(u8_channel, u32_delta);

    v_event_emit(EVENT_CLASS_SWITCH_AUTO, u8_channel,
                 u16_new_state, u32_edge_count);
}

/*============================================================================
 * PRIVATE - NVM
 *==========================================================================*/

static nvm_param_id_t x_switch_cycle_nvm_id(uint8_t u8_channel, uint8_t u8_parameter)
{
    return (nvm_param_id_t) (NVM_PARAM_CYCLE_A_REPEAT
                             + (u8_channel * SWITCH_CYCLE_PARAM_COUNT)
                             + u8_parameter);
}

static uint32_t * p_u32_switch_cycle_param(uint8_t u8_channel, uint8_t u8_parameter)
{
    switch_cycle_t *p_x_cycle = &g_x_switch_cycle[u8_channel];

    switch (u8_parameter)
    {
        case SWITCH_CYCLE_PARAM_REPEAT: return &p_x_cycle->u32_repeat_count;
        case SWITCH_CYCLE_PARAM_ON:     return &p_x_cycle->u32_on_time_us;
        case SWITCH_CYCLE_PARAM_OFF:    return &p_x_cycle->u32_off_time_us;
        default:                        return NULL;
    }
}

/*============================================================================
 * PUBLIC - INIT
 *==========================================================================*/

/*
 * Create each persisted parameter with its default, then read back whatever is
 * already stored. x_nvm_create() does not disturb an object that already
 * exists, so on a virgin pool this writes defaults and on every later boot it
 * is a no-op and the get() supplies the saved value.
 *
 * Called from v_param_init() -- after x_nvm_pool_init(), before x_nvm_commit(),
 * so first-boot creation of all thirteen objects lands in one flash write.
 */
void v_switch_out_nvm_init(void)
{
    uint8_t u8_channel;
    uint8_t u8_parameter;

    u32_switch_pulse_width_ms = SWITCH_PULSE_WIDTH_DEFAULT_MS;
    x_nvm_create(&g_x_nvm_param, NVM_PARAM_SWITCH_PULSE_MS,
                 sizeof(u32_switch_pulse_width_ms), &u32_switch_pulse_width_ms);
    x_nvm_get(&g_x_nvm_param, NVM_PARAM_SWITCH_PULSE_MS, &u32_switch_pulse_width_ms);

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        switch_cycle_t *p_x_cycle = &g_x_switch_cycle[u8_channel];

        p_x_cycle->u32_repeat_count = SWITCH_CYCLE_REPEAT_DEFAULT;
        p_x_cycle->u32_on_time_us   = SWITCH_CYCLE_ON_DEFAULT_US;
        p_x_cycle->u32_off_time_us  = SWITCH_CYCLE_OFF_DEFAULT_US;

        for (u8_parameter = 0; u8_parameter < SWITCH_CYCLE_PARAM_COUNT; u8_parameter++)
        {
            nvm_param_id_t x_id = x_switch_cycle_nvm_id(u8_channel, u8_parameter);
            uint32_t *p_u32_value = p_u32_switch_cycle_param(u8_channel, u8_parameter);

            x_nvm_create(&g_x_nvm_param, x_id, sizeof(uint32_t), p_u32_value);
            x_nvm_get(&g_x_nvm_param, x_id, p_u32_value);
        }
    }

    /* Create only -- the read-back is deferred to v_event_control_restore().
     * Creating it here keeps first-boot provisioning of every object inside the
     * single flash write this function exists to batch. */
    v_event_control_nvm_init();
}

/*
 * Create the mask's NVM object with the all-disarmed default, WITHOUT reading
 * it back. The read-back is v_event_control_restore(), deliberately deferred
 * until after the outputs are up.
 *
 * A virgin pool therefore gets a 0 here and boots producing nothing; every
 * later boot leaves the stored value alone for the restore to pick up.
 */
void v_event_control_nvm_init(void)
{
    uint32_t u32_default = 0UL;

    x_nvm_create(&g_x_nvm_param, NVM_PARAM_EVENT_CONTROL,
                 sizeof(u32_default), &u32_default);
}

/*
 * Read the persisted mask into the live register.
 *
 * ORDERING IS LOAD-BEARING: this must run AFTER v_switch_out_init(). Until it
 * does, g_x_event_control is all-zero, which is what keeps the four forced-off
 * writes in switch init silent. Moving this call earlier resurrects them.
 *
 * Fails safe: if the get() does not succeed the register keeps its zero, so the
 * failure mode is "nothing is armed" rather than "everything is".
 */
/*
 * Park the live mask, leaving the persisted copy alone (plan S2b).
 *
 * RAM only, deliberately: no x_nvm_set() of the cleared value. That asymmetry
 * is the whole mechanism -- the NVM copy stays the human console's arming while
 * the live register is whatever the current acon session has asked for, and
 * v_event_control_restore() on the way out puts the former back.
 *
 * An acon command that DOES persist writes the NVM copy, so the restore hands
 * that same value back and a script's deliberate change survives its session.
 * Both cases fall out of one register with no context test at any production
 * site -- see S2b for why that beat keeping two register sets.
 */
void v_event_control_suspend(void)
{
    g_x_event_control.u32_all = 0UL;
}

void v_event_control_restore(void)
{
    uint32_t u32_mask = 0UL;

    if (x_nvm_get(&g_x_nvm_param, NVM_PARAM_EVENT_CONTROL, &u32_mask)
        == NVM_ERROR_NONE)
    {
        g_x_event_control.u32_all = u32_mask;
    }
}

bool b_event_control_nvm_save(void)
{
    uint32_t u32_mask = g_x_event_control.u32_all;

    return (x_nvm_set(&g_x_nvm_param, NVM_PARAM_EVENT_CONTROL, &u32_mask)
            == NVM_ERROR_NONE);
}

void v_switch_cycle_nvm_save(uint8_t u8_channel, uint8_t u8_parameter)
{
    if ((u8_channel >= SWITCH_OUT_COUNT) || (u8_parameter >= SWITCH_CYCLE_PARAM_COUNT))
    {
        return;
    }

    x_nvm_set(&g_x_nvm_param,
              x_switch_cycle_nvm_id(u8_channel, u8_parameter),
              p_u32_switch_cycle_param(u8_channel, u8_parameter));
}

/*
 * Order matters: OCxM is set to "forced inactive" BEFORE the channel output is
 * enabled, so the pin is already defined-off when TIM2 takes it over. The pins
 * are muxed to AF2 by HAL_TIM_MspPostInit() with CCxE clear, which leaves them
 * driven low, so with the default CCxP (active high) there is no edge at all
 * here -- the external hardware never sees a start-up twitch.
 *
 * HAL_TIM_OC_Start() also sets CEN, leaving TIM2->CNT free-running as the
 * 32-bit timebase that the cycling engine schedules against.
 */
void v_switch_out_init(void)
{
    uint8_t u8_channel;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        u32_pulse_remaining_ms[u8_channel] = 0;
        g_x_switch_cycle[u8_channel].u8_running = 0;
        g_x_switch_cycle[u8_channel].u32_cycles_done = 0;

        v_switch_out_force(u8_channel, 0);
        HAL_TIM_OC_Start(&htim2, x_switch_map[u8_channel].u32_hal_channel);
    }
}

/*============================================================================
 * PUBLIC - MANUAL DRIVE
 *==========================================================================*/

void v_switch_out_set(uint8_t u8_channel, uint8_t u8_on)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return;
    }

    /* A manual level request wins over anything already driving the channel --
     * otherwise a cycle or a pulse countdown would later move the output behind
     * the user's back. */
    v_switch_cycle_stop(u8_channel);
    v_switch_out_cancel_pulse(u8_channel);
    v_switch_out_force(u8_channel, u8_on);
}

void v_switch_out_toggle(uint8_t u8_channel)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return;
    }

    /* Toggling a cycling channel means "stop it", not "stop it and turn it on" --
     * x_switch_out_get() would report SWITCH_OUT_TIMED, which is neither. */
    if (g_x_switch_cycle[u8_channel].u8_running)
    {
        v_switch_out_set(u8_channel, 0);
        return;
    }

    v_switch_out_set(u8_channel, (x_switch_out_get(u8_channel) != SWITCH_OUT_ON));
}

void v_switch_out_pulse(uint8_t u8_channel, uint32_t u32_ms)
{
    if ((u8_channel >= SWITCH_OUT_COUNT) || (u32_ms == 0))
    {
        return;
    }

    v_switch_cycle_stop(u8_channel);

    /* Arm the countdown before driving the output: if the periodic ISR lands
     * between the two, the worst case is one tick of extra on-time, versus an
     * output left on indefinitely if the ordering were reversed. */
    SAVE_AND_DISABLE_INTERRUPTS();
    u32_pulse_remaining_ms[u8_channel] = u32_ms;
    RESTORE_INTERRUPTS();

    v_switch_out_force(u8_channel, 1);
}

void v_switch_out_all_off(void)
{
    uint8_t u8_channel;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        v_switch_out_set(u8_channel, 0);
    }
}

/*
 * Drive state is read back from the hardware rather than from a shadow copy,
 * so it cannot drift out of step with the peripheral. While a channel is
 * cycling its OCxM matches neither forced value, and SWITCH_OUT_TIMED is
 * reported -- an independent cross-check on switch_cycle_t.u8_running.
 */
switch_out_state_t x_switch_out_get(uint8_t u8_channel)
{
    uint32_t u32_mode;

    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return SWITCH_OUT_OFF;
    }

    u32_mode = LL_TIM_OC_GetMode(TIM2, x_switch_map[u8_channel].u32_ll_channel);

    if (u32_mode == LL_TIM_OCMODE_FORCED_ACTIVE)
    {
        return SWITCH_OUT_ON;
    }
    if (u32_mode == LL_TIM_OCMODE_FORCED_INACTIVE)
    {
        return SWITCH_OUT_OFF;
    }
    return SWITCH_OUT_TIMED;
}

/*
 * Bitmaps, bit 0 = SWITCH_A .. bit 3 = SWITCH_D. Three independent views of the
 * same four channels, from three different sources -- see Docs/planning/
 * automation-console-plan.md (I6).
 *
 * Level comes from the GPIO input register, not from OCxM: IDR captures the pad
 * every AHB cycle regardless of the pin being owned by TIM2's alternate
 * function, so it is valid while a channel is cycling -- which is exactly when
 * x_switch_out_get() cannot report a level at all. It is also the more honest
 * measurement for a tester: what the pin is doing, not what it was told to do.
 */
uint8_t u8_switch_out_level_bitmap(void)
{
    uint8_t u8_channel;
    uint8_t u8_bitmap = 0;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        const switch_out_map_t *p_x_map = &x_switch_map[u8_channel];

        if ((p_x_map->p_x_gpio_port->IDR & p_x_map->u16_gpio_pin) != 0U)
        {
            u8_bitmap |= (uint8_t) (1U << u8_channel);
        }
    }
    return u8_bitmap;
}

/* 1 = under timer control (cycling), 0 = manual/forced. Read from OCxM. */
uint8_t u8_switch_out_mode_bitmap(void)
{
    uint8_t u8_channel;
    uint8_t u8_bitmap = 0;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        if (x_switch_out_get(u8_channel) == SWITCH_OUT_TIMED)
        {
            u8_bitmap |= (uint8_t) (1U << u8_channel);
        }
    }
    return u8_bitmap;
}

/*
 * 1 = cycling and the repeat count is not yet exhausted. Software state, so it
 * is an independent cross-check on the OCxM-derived mode bitmap above: the two
 * agree in normal operation because v_switch_cycle_halt() forces the output LOW
 * and clears u8_running together. It is also how a host learns whether a start
 * command took effect at all, since v_switch_cycle_start() fails silently.
 */
uint8_t u8_switch_cycle_run_bitmap(void)
{
    uint8_t u8_channel;
    uint8_t u8_bitmap = 0;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        if (g_x_switch_cycle[u8_channel].u8_running)
        {
            u8_bitmap |= (uint8_t) (1U << u8_channel);
        }
    }
    return u8_bitmap;
}

uint32_t u32_switch_out_pulse_remaining(uint8_t u8_channel)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return 0;
    }
    return u32_pulse_remaining_ms[u8_channel];
}

const char * pc_switch_out_name(uint8_t u8_channel)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return "?";
    }
    return x_switch_map[u8_channel].pc_name;
}

const char * pc_switch_out_pin_name(uint8_t u8_channel)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return "?";
    }
    return x_switch_map[u8_channel].pc_pin_name;
}

uint32_t u32_switch_out_get_pulse_width(void)
{
    return u32_switch_pulse_width_ms;
}

void v_switch_out_set_pulse_width(uint32_t u32_ms)
{
    u32_switch_pulse_width_ms = u32_ms;
    x_nvm_set(&g_x_nvm_param, NVM_PARAM_SWITCH_PULSE_MS, &u32_switch_pulse_width_ms);
}

/*
 * Pulse timebase. Called from the periodic (1 ms) interrupt, so it must stay
 * short: at most four saturating subtractions and, on expiry, a single CCMR
 * field write per channel.
 */
void v_switch_out_tick(void)
{
    uint8_t u8_channel;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        if (u32_pulse_remaining_ms[u8_channel] == 0)
        {
            continue;
        }

        if (u32_pulse_remaining_ms[u8_channel] > PERIODIC_TIMER_INTERVAL_MS)
        {
            u32_pulse_remaining_ms[u8_channel] -= PERIODIC_TIMER_INTERVAL_MS;
        }
        else
        {
            u32_pulse_remaining_ms[u8_channel] = 0;
            v_switch_out_force(u8_channel, 0);
        }
    }
}

/*============================================================================
 * PUBLIC - CYCLING
 *==========================================================================*/

/*
 * Start cycling. The output goes high immediately and the hardware is left to
 * place every edge from there.
 *
 * Setting OCxM does not by itself move the pin -- only a compare match does --
 * so forcing the level active and then switching to "inactive on match" leaves
 * the output high until the on-time elapses.
 *
 * Arming order is deliberate: the struct is initialised and CCRx written while
 * the channel's interrupt is still disabled, any stale match is dropped, and
 * CCxIE is enabled last. Nothing here can race the ISR because the ISR cannot
 * run for this channel yet.
 */
void v_switch_cycle_start(uint8_t u8_channel)
{
    switch_cycle_t *p_x_cycle;
    const switch_out_map_t *p_x_map;
    uint32_t u32_next;

    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return;
    }

    p_x_cycle = &g_x_switch_cycle[u8_channel];
    p_x_map   = &x_switch_map[u8_channel];

    if (p_x_cycle->u8_running)
    {
        return;
    }

    if ((p_x_cycle->u32_on_time_us  < SWITCH_CYCLE_TIME_MIN_US)
        || (p_x_cycle->u32_off_time_us < SWITCH_CYCLE_TIME_MIN_US)
        || (p_x_cycle->u32_on_time_us  > SWITCH_CYCLE_TIME_MAX_US)
        || (p_x_cycle->u32_off_time_us > SWITCH_CYCLE_TIME_MAX_US))
    {
        return;
    }

    v_switch_out_cancel_pulse(u8_channel);

    p_x_cycle->u32_cycles_done = 0;
    p_x_cycle->u8_phase        = SWITCH_CYCLE_PHASE_ON;
    p_x_cycle->u8_running      = 1;

    v_switch_out_force(u8_channel, 1);
    u32_next = TIM2->CNT + p_x_cycle->u32_on_time_us;
    LL_TIM_OC_SetMode(TIM2, p_x_map->u32_ll_channel, LL_TIM_OCMODE_INACTIVE);
    *(p_x_map->p_u32_ccr) = u32_next;

    SAVE_AND_DISABLE_INTERRUPTS();
    TIM2->SR = ~p_x_map->u32_ccif;
    TIM2->DIER |= p_x_map->u32_ccie;        /* DIER is shared across channels */
    RESTORE_INTERRUPTS();
}

void v_switch_cycle_stop(uint8_t u8_channel)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return;
    }

    if (! g_x_switch_cycle[u8_channel].u8_running)
    {
        return;
    }

    /* DIER is shared by all four channels, so the read-modify-write inside
     * v_switch_cycle_halt() has to be protected from the ISR here. */
    SAVE_AND_DISABLE_INTERRUPTS();
    v_switch_cycle_halt(u8_channel);
    RESTORE_INTERRUPTS();
}

void v_switch_cycle_stop_all(void)
{
    uint8_t u8_channel;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        v_switch_cycle_stop(u8_channel);
    }
}

uint8_t u8_switch_cycle_running(uint8_t u8_channel)
{
    if (u8_channel >= SWITCH_OUT_COUNT)
    {
        return 0;
    }
    return g_x_switch_cycle[u8_channel].u8_running;
}

uint8_t u8_switch_cycle_any_running(void)
{
    uint8_t u8_channel;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        if (g_x_switch_cycle[u8_channel].u8_running)
        {
            return 1;
        }
    }
    return 0;
}

/*
 * TIM2 compare service, called directly from TIM2_IRQHandler at priority 0.
 *
 * One SR read covers all four channels; every set flag is serviced in this
 * pass, because independent per-channel periods make near-simultaneous matches
 * inevitable. Flags are rc_w0, so writing the complement clears just that one.
 */
void v_switch_cycle_isr(void)
{
    uint32_t u32_status = TIM2->SR;
    uint8_t  u8_channel;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        const switch_out_map_t *p_x_map = &x_switch_map[u8_channel];

        if ((u32_status & p_x_map->u32_ccif) == 0)
        {
            continue;
        }

        TIM2->SR = ~p_x_map->u32_ccif;

        /* A match can already have been latched when the channel was stopped. */
        if (g_x_switch_cycle[u8_channel].u8_running)
        {
            v_switch_cycle_advance(u8_channel);
        }
    }
}
