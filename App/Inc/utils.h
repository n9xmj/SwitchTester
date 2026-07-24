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

#define BUFF_SIZE 12

typedef struct circular_buf
{
    bool b_full;
    uint8_t u8_index;
    uint8_t u8_prev_index;
    uint8_t _u8_fill1;
    int16_t i16_avg;
    int32_t i32_sum;
    int16_t i16_data[BUFF_SIZE];
}
circular_buf_t;

typedef enum __attribute__((packed))
{
    DEBOUNCED_LEVEL_NO_CHANGE,
    DEBOUNCED_LEVEL_CHANGED_LOW,
    DEBOUNCED_LEVEL_CHANGED_HIGH,
    DEBOUNCED_ACTIVE_LEVEL_REPEAT,
}
debounced_level_t;

typedef enum __attribute__((packed))
{
    DEBOUNCE_NO_AUTO_REPEAT,
    DEBOUNCE_AUTO_REPEAT_LOW,
    DEBOUNCE_AUTO_REPEAT_HIGH
}
debounce_auto_repeat_mode_t;

// Structure used by v_debounce() function to debounce the state of input pins
// or other signals returning a binary state

typedef uint8_t (*level_read_function_t)(void);

typedef struct
{
    level_read_function_t p_f_level_read_function; // Pointer to function that returns the present state of the pin/signal to be debounced
    uint8_t u8_debounce_count;              // (private) Debounce count - increments when previous & present state match, reset on mismatch
    uint8_t u8_debounce_count_threshold;    // Debounce count threshold - when count >= threshold, signal state is considered stable
    uint8_t u8_debounced_level;             // Last debounced state - this is the qualified level of the object being checked
    uint8_t u8_present_level;               // Last level read from object; u8_present_level = level_read_function()
    uint8_t u8_previous_level;              // (private)
    // Auto-repeat settings
    debounce_auto_repeat_mode_t x_auto_repeat_mode; // Auto-repeat mode; 0=None, 1=Low, 2=High
    uint8_t u8_auto_repeat_delay;           // Initial delay after signal stable before starting auto-repeats
    uint8_t u8_auto_repeat_interval;        // Interval between auto-repeats
    uint8_t u8_auto_repeat_count;           // (private) Counter for auto repeat interval timing
    uint8_t u8_auto_repeat_delay_done;      // (private) Set after initial auto-repeat delay has elapsed
}
debounce_t;

extern reset_source_t x_reset_source;       // Cause (source) of reset saved here; see v_get_reset_source()

//------------------------------------------------------------------------------

extern int i_getchar_blocking(void);                            // Get character from STDIN, blocking until char received
extern int i_getline(char *p_c_entry, uint16_t u16_length_limit); // Get 1-line text entry from STDIN (blocking)
extern uint8_t u8_prompt_wait(uint8_t u8_delay_sec);            // Wait for delay or go/cancel signal from STDIN
extern void v_newline(void);                                    // Send CR/LF sequence to STDOUT
extern void v_conditional_newline(void);                        // Send CR/LF if not at end of line
extern void v_repeat_char(char c_char, int16_t i16_repeat);     // Output <c_char> for <i16_repeat> times, CR/LF at end if <i16_repeat> negative
extern uint8_t u8_hexchar_to_int(char c_digit);                 // Convert single ASCII hexadecimal character to integer (0-15)
extern char u8_int_to_hexchar(uint8_t u8_digit);                // Convert single digit hexadecimal integer to character ('0'..'9','A'..'F')
extern void v_delay_us(uint16_t u16_microseconds);              // Delay in 1 microsecond units, uses HW timer
// Wait for background SPI transaction to finish
extern void v_debounce_init(debounce_t *p_x_object,
                            level_read_function_t p_f_level_read_function,
                            uint8_t u8_threshold);
extern debounced_level_t x_debounce(debounce_t *p_x_object);   // Debounce a input pin or other binary state object

void v_add_and_calculate_avg(circular_buf_t *x_buf, int16_t i16_val);
void v_init_buffer(circular_buf_t *x_buf);

extern void v_debug_config(void);                               // Setup suspend options when running under debugger
extern void v_get_reset_source(void);                           // Determines source of MCU reset, saved in x_reset_source
extern const char * pc_reset_source_description(reset_type_t  x_reset_type);

extern void v_flash_rdp_check(void);

extern void v_system_tick_set(uint32_t u32_tick_set);
extern void v_system_tick_add(uint32_t u32_tick_add);
extern void v_set_rtc_wakeup_timer(uint16_t u16_duration_ms);
//extern uint64_t u64_get_rtc_time(void);
extern uint32_t u32_get_rtc_hour_time(void);

#endif
