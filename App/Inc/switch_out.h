/******************************************************************************
 * switch_out.h
 *
 * SWITCH_A..D output control (TIM2 CH1..CH4 on PC4/PC5/PB10/PB11).
 *
 * The switch pins stay permanently muxed to AF2/TIM2; the drive level is set
 * by the channel's output-compare mode field (OCxM):
 *
 *      ON  = TIM_OCMODE_FORCED_ACTIVE      (OCxREF forced active)
 *      OFF = TIM_OCMODE_FORCED_INACTIVE    (OCxREF forced inactive)
 *
 * Nothing here knows about pin polarity: "active" is whatever the channel's
 * CCxP bit in TIM2->CCER says it is (set from OCPolarity in CubeMX). An
 * active-low board is a CubeMX change, not a firmware change.
 *
 * Automatic cycling puts a channel into set-level-on-compare mode instead, and
 * reprograms CCRx from the TIM2 compare ISR. See Docs/planning/switch-cycling-plan.md.
 ******************************************************************************/

#ifndef SWITCH_OUT_H
#define SWITCH_OUT_H

#include <stdint.h>

#define SWITCH_OUT_COUNT        4       // SWITCH_A .. SWITCH_D

typedef enum
{
    SWITCH_OUT_OFF = 0,                 // OCxM == forced inactive
    SWITCH_OUT_ON,                      // OCxM == forced active
    SWITCH_OUT_TIMED                    // OCxM == anything else (timer-driven)
}
switch_out_state_t;

/*----------------------------------------------------------------------------
 * Cycling parameters
 *
 * Times are in whole microseconds (TIM2 runs at 1 tick/uS).
 *
 * The maximum is bounded well below 2^31 because the missed-compare guard in
 * switch_out.c compares (int32_t)(next_ccr - CNT); a delta at or above 2^31
 * would read as negative and defeat it.
 *--------------------------------------------------------------------------*/

#define SWITCH_CYCLE_TIME_MIN_US        10UL
#define SWITCH_CYCLE_TIME_MAX_US        1000000000UL    /* 1000 seconds */
#define SWITCH_CYCLE_ON_DEFAULT_US      500000UL        /* 500 mS */
#define SWITCH_CYCLE_OFF_DEFAULT_US     500000UL        /* 500 mS */
#define SWITCH_CYCLE_REPEAT_DEFAULT     0UL             /* run until stopped */

/* Parameter indices, matching both the menu key order (repeat / on / off) and
 * the NVM ID stride. Do not reorder without updating nvmparams.h. */
#define SWITCH_CYCLE_PARAM_REPEAT       0
#define SWITCH_CYCLE_PARAM_ON           1
#define SWITCH_CYCLE_PARAM_OFF          2
#define SWITCH_CYCLE_PARAM_COUNT        3

typedef struct
{
    /* Settings. NVM-backed, edited from the menu, read-only to the ISR -- so a
     * foreground update is a plain aligned 32-bit store (atomic on M0+) and
     * takes effect at the next phase boundary with no handshake. */
    uint32_t    u32_repeat_count;       /* ON pulses to run; 0 = until stopped */
    uint32_t    u32_on_time_us;
    uint32_t    u32_off_time_us;

    /* Runtime state, owned by the ISR. Foreground only touches these while the
     * channel's compare interrupt is disarmed. */
    uint32_t    u32_cycles_done;        /* completed ON pulses this run */
    uint8_t     u8_running;
    uint8_t     u8_phase;               /* see SWITCH_CYCLE_PHASE_* */
}
switch_cycle_t;

#define SWITCH_CYCLE_PHASE_ON           0
#define SWITCH_CYCLE_PHASE_OFF          1

extern switch_cycle_t g_x_switch_cycle[SWITCH_OUT_COUNT];

/*----------------------------------------------------------------------------
 * Manual drive
 *--------------------------------------------------------------------------*/

extern void v_switch_out_init(void);                                // Force all outputs off, then enable the TIM2 channel outputs
extern void v_switch_out_set(uint8_t u8_channel, uint8_t u8_on);    // Force one output on/off; stops cycling and cancels any pulse
extern void v_switch_out_toggle(uint8_t u8_channel);                // Invert one output; stops cycling and cancels any pulse
extern void v_switch_out_pulse(uint8_t u8_channel, uint32_t u32_ms);// Force one output on for <u32_ms>, then off again
extern void v_switch_out_all_off(void);                             // Force every output off, stop every cycle and pulse

extern switch_out_state_t x_switch_out_get(uint8_t u8_channel);     // Present drive state, read back from OCxM
extern uint32_t u32_switch_out_pulse_remaining(uint8_t u8_channel); // Milliseconds left in a pulse, 0 if none in progress
extern const char * pc_switch_out_name(uint8_t u8_channel);         // "A".."D", or "?" if <u8_channel> is out of range
extern const char * pc_switch_out_pin_name(uint8_t u8_channel);     // "PC4".."PB11" -- for bench/scope reference

extern uint32_t u32_switch_out_get_pulse_width(void);               // Manual-menu pulse width, milliseconds
extern void v_switch_out_set_pulse_width(uint32_t u32_ms);          // Set + persist the above

extern void v_switch_out_tick(void);                                // Pulse timebase; call every PERIODIC_TIMER_INTERVAL_MS

/*----------------------------------------------------------------------------
 * Automatic cycling
 *--------------------------------------------------------------------------*/

extern void v_switch_out_nvm_init(void);                            // Create/load all persisted switch parameters; call from v_param_init()
extern void v_switch_cycle_nvm_save(uint8_t u8_channel, uint8_t u8_parameter);

extern void v_switch_cycle_start(uint8_t u8_channel);               // Begin cycling; starts with the ON phase
extern void v_switch_cycle_stop(uint8_t u8_channel);                // Immediate abort; leaves the output forced LOW
extern void v_switch_cycle_stop_all(void);
extern uint8_t u8_switch_cycle_running(uint8_t u8_channel);
extern uint8_t u8_switch_cycle_any_running(void);                   // Nonzero if any channel is cycling

extern void v_switch_cycle_isr(void);                               // TIM2 compare service; call from TIM2_IRQHandler

#endif // SWITCH_OUT_H
