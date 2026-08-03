/******************************************************************************
 * utils.h
 *
 * Utility functions that do not fall under any major operational category.
 ******************************************************************************/

#ifndef UTILS_H
#define UTILS_H

typedef enum
{
  RESET_TYPE_UNKNOWN = 0,
  RESET_TYPE_POWER,
  RESET_TYPE_OPTION_BYTE,
  RESET_TYPE_PIN,
  RESET_TYPE_SOFTWARE,
  RESET_TYPE_IWDG,
  RESET_TYPE_WWDG,
  RESET_TYPE_LOW_POWER,
  RESET_TYPE_MAX
}
reset_type_t;

// Structure used to save cause-of-reset state
// See v_get_reset_cause()

typedef struct
{
    uint8_t u8_reset_flags;
    reset_type_t x_reset_type;
}
reset_source_t;

extern reset_source_t x_reset_source;       // Cause (source) of reset saved here; see v_get_reset_source()

//------------------------------------------------------------------------------

// Note: the blocking calls below (i_getchar_blocking, i_getline, v_delay_pump)
// all pump v_app_polling_task() while they spin. That hook is the application's
// to provide and is declared in platform.h, not here -- see the comment there.

extern int i_getchar_blocking(void);                            // Get character from STDIN, blocking until char received
// Get 1-line text entry from STDIN (blocking). Returns the entry length, -1 if
// cancelled with ESC (prints "<Cancel>"), or -2 if abandoned with Ctrl-C (emits
// nothing at all). Callers that only care whether the entry survived should
// test for < 0, so a future exit state cannot break them.
extern int i_getline(char *p_c_entry, uint16_t u16_length_limit);
extern void v_newline(void);                                    // Send CR/LF sequence to STDOUT
extern void v_conditional_newline(void);                        // Send CR/LF if not at end of line
extern void v_repeat_char(char c_char, int16_t i16_repeat);     // Output <c_char> for <i16_repeat> times, CR/LF at end if <i16_repeat> negative
extern uint8_t u8_hexchar_to_int(char c_digit);                 // Convert single ASCII hexadecimal character to integer (0-15)
extern char u8_int_to_hexchar(uint8_t u8_digit);                // Convert single digit hexadecimal integer to character ('0'..'9','A'..'F')

extern void v_delay_us(uint16_t u16_microseconds);              // Delay in 1 microsecond units, uses HW timer
extern void v_delay_pump(uint32_t u32_ticks);                   // Delay in (typically) 1mS uints, uses HAL system tick, pumps v_app_polling_task()
#define v_delay_ms(u32_ticks) v_delay_pump(u32_ticks);

extern reset_type_t x_get_reset_source(void);                   // Determines source of MCU reset, saved in x_reset_source
extern const char * pc_reset_source_description(reset_type_t  x_reset_type);

extern void v_flash_rdp_check(void);                            // Sets FLASH RDP level (reversible readout protect) if presently RDP 0

extern void v_system_tick_set(uint32_t u32_tick_set);           // Directly sets the HAL system tick counter; i.e. value returned by HAL_GetTick()
extern void v_system_tick_add(uint32_t u32_tick_add);           // Adds a value to the HAL system tick counter
extern uint32_t u32_set_rtc_wakeup_timer(uint16_t u16_duration_ms); // Arms RTC wakeup timer for ~the interval specified, honoring the current RTC clock config; returns actual ms that will elapse, 0 if not armed
extern void v_stop_rtc_wakeup_timer(void);                      // Disarms (stops) the RTC wakeup timer armed above; leaves all other RTC functions untouched
extern uint32_t u32_get_rtc_hour_time(void);                    // Reads number of seconds elapsed in the present hour from RTC

#endif
