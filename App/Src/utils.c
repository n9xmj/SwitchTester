/******************************************************************************
 * utils.c
 *
 * Utility functions that do not fall under any major operational category.
 ******************************************************************************/

#include "device_config.h"              // Includes debug_config.h, main.h, macros.h
#include "stdio_retarget.h"
#include "utils.h"
//#include <time.h>                       // For v_get_rtc_time; uses mktime()

//------------------------------------------------------------------------------

// Tick rate set for the core SysTick timer and HAL_GetTick()
// This is typically 1000 Hz / 1 millisecond
#define SYSTICK_TIMEBASE_HZ     1000        // Typically 1000 (1 mS)

// Clock source rate for RTC
#define RTC_CLOCK_HZ            32768UL     // LSI clock

//------------------------------------------------------------------------------

static const char *reset_source_str[RESET_TYPE_MAX] =
{
    [RESET_TYPE_UNKNOWN]    = "UNKNOWN",
    [RESET_TYPE_POWER]      = "BOR/POR",
    [RESET_TYPE_OPTION_BYTE] = "OPTION LOAD",
    [RESET_TYPE_PIN]        = "NRST PIN",
    [RESET_TYPE_SOFTWARE]   = "SOFTWARE",
    [RESET_TYPE_IWDG]       = "IND WATCHDOG",
    [RESET_TYPE_WWDG]       = "WIN WATCHDOG",
    [RESET_TYPE_LOW_POWER]  = "LOW POWER",
};

reset_source_t x_reset_source;

/******************************************************************************
 *
 ******************************************************************************/

void __attribute__((weak)) app_polling_task(void)
{
    // Does nothing, intended to be overridden
}
    
/******************************************************************************
 * int i_getchar_blocking(void)
 *
 * Get a character from STDIN, blocking until something is received
 *
 * Returns:     Character received from STDIN
 ******************************************************************************/

// app_polling_task() is located in app_main.c
extern void app_polling_task(void);

int i_getchar_blocking(void)
{
    int i_char;

    do
    {
        app_polling_task();
        i_char = getchar();
    }
    while (i_char < 0);

    return i_char;
}

/******************************************************************************
 *
 ******************************************************************************/

enum
{
    GETLINE_IN_PROGRESS,
    GETLINE_NORMAL_EXIT,
    GETLINE_ESCAPE_EXIT,
};

int i_getline(char *p_c_entry, uint16_t u16_length_limit)
{
    int i_key;
    int i_len = 0;
    uint8_t u8_clear_line = 0;
    uint8_t u8_done = GETLINE_IN_PROGRESS;

    do
    {
        i_key = i_getchar_blocking();

        if (i_key == '\r')              // Return/Enter
        {
            v_newline();
            u8_done = GETLINE_NORMAL_EXIT;
        }

        else if (i_key == '\b')         // Backspace
        {
            if (i_len > 0)
            {
                printf("\b \b");
                i_len--;
            }
        }

        else if (i_key == 0x1B)         // ESCape
        {
            u8_clear_line = 1;
            u8_done = GETLINE_ESCAPE_EXIT;
        }

        else if (i_key == ('X' - 0x40)) // Ctrl-X (cancel/re-enter)
        {
            u8_clear_line = 1;
        }

        else if (i_key >= 0x20)         // Normal character
        {
            if (i_len < u16_length_limit)
            {
                printf("%c", i_key);
                p_c_entry[i_len] = (char) i_key;
                i_len++;
            }
        }

        if (u8_clear_line)
        {
            while (i_len > 0)
            {
                printf("\b \b");
                i_len--;
            }
            u8_clear_line = 0;
            if (u8_done == GETLINE_ESCAPE_EXIT)
            {
                printf("<Cancel>\r\n");
            }
        }
    }
    while (u8_done == GETLINE_IN_PROGRESS);

    p_c_entry[i_len] = 0;

    return (u8_done == GETLINE_ESCAPE_EXIT) ? -1 : i_len;
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_prompt_wait(uint8_t u8_delay_sec)
{
    int i_key;
    uint8_t u8_delay_100ms;
    uint8_t u8_return = 0xFF;

    for ( ; u8_delay_sec > 0; u8_delay_sec--)
    {
        DPRINTF("%u \r", u8_delay_sec);
        for (u8_delay_100ms = 0; u8_delay_100ms < 10; u8_delay_100ms++)
        {
            DEBUG_LED_TOGGLE;
            HAL_Delay(100);
            i_key = getchar();
            if (i_key == 0x1B)
            {
                DEBUG_LED_OFF;
                DPRINTF("ABORT\r\n\n");
                u8_return = 0;  // Return 0: abort
                break;
            }
            if (i_key == '\r')
            {
                u8_return = 2;  // Return 2: go go go!
                break;
            }
        }
        if (u8_return != 0xFF) break;
    }

    if (u8_return == 0xFF) u8_return = 1; // Return 1: Delay complete (go)

    return u8_return;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_newline(void)
{
    putchar('\r');
    putchar('\n');
}


void v_conditional_newline(void)
{
    if (ui_stdout_chars_after_crlf())
    {
        v_newline();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_repeat_char(char c_char, int16_t i16_repeat)
{
    uint16_t u16_count = (i16_repeat >= 0) ? i16_repeat : -i16_repeat;
    for (; u16_count > 0; u16_count--)
    {
        putchar(c_char);
    }
    if (i16_repeat < 0)
    {
        v_newline();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_hexchar_to_int(char c_digit)
{
    if ((c_digit >= '0') && (c_digit <= '9'))
    {
        return (uint8_t) (c_digit - '0');
    }
    if ((c_digit >= 'A') && (c_digit <= 'F'))
    {
        return (uint8_t) (c_digit - 'A' + 10);
    }
    if ((c_digit >= 'a') && (c_digit <= 'f'))
    {
        return (uint8_t) (c_digit - 'a' + 10);
    }

    return 0xFF;
}

/******************************************************************************
 *
 ******************************************************************************/

char u8_int_to_hexchar(uint8_t u8_digit)
{
    u8_digit &= 0x0F;
    if (u8_digit < 10)
    {
        return (char) (u8_digit + '0');
    }
    return (char) (u8_digit - 10 + 'A');
}

/******************************************************************************
 * v_delay_us(u16_microseconds)
 *
 * Generate delays with 1 microsecond precision
 * Uses hardware timer for delay timing
 *
 * WARNING: Using this routine will partially reconfigure the timer used to
 * generate the delay. It assumes it has a monopoly on the use of the timer
 * selected. The timer used by this routine should not be used for any other
 * purpose.
 ******************************************************************************/

// Notes:
// 1. Delay is configured by setting up the counter to be clocked at a 1uS rate,
//    with the desired delay in microseconds written to the autoreload register.
//    The counter counts down, and sets the "update generation" flag when it
//    underflows. This is the flag that is checked to determine when the delay
//    has completed.
// 2. The timer used for this function should not be used for any other purpose.
//    This function modifies the terminal count (reload), modifies the counter
//    (resets it), and stops/starts the timer counter as needed.
// 3. The delay-using-autoreload approach (the one being used here, not the
//    #if'd out version) allows the use of the basic timers (TIM6/TIM7) that do
//    not provide any capture/compare channels.
// 4. The timer used here is NOT -fully- initialized by the code here; it is
//    expected that basic timer initialization (such as setting the prescaler)
//    be done as part of the hardware init process following MCU reset
//    (e.g. by CubeMX autogenerated code).

void v_delay_us(uint16_t u16_microseconds)
{
    if (u16_microseconds == 0) return;
    if (u16_microseconds < 0xFFFF) u16_microseconds++;

    // Stop counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 &= ~TIM_CR1_CEN;
    // Reset counter
    __HAL_TIM_SET_COUNTER(&DELAY_US_TIMER_HANDLE, 0);
    __HAL_TIM_SET_AUTORELOAD(&DELAY_US_TIMER_HANDLE, u16_microseconds);
    // Force an update event, loads counter and autoreload with values set above
    DELAY_US_TIMER_HANDLE.Instance->EGR = TIM_EGR_UG;
    // Clear the update event flag; this is what is tested to determine if
    // the requested time period has elapsed
    __HAL_TIM_CLEAR_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_UPDATE);
    // Start counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 |= TIM_CR1_CEN;
    // Wait for delay completion
    while (! __HAL_TIM_GET_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_UPDATE))
    {
    }
}

#if 0
    // This implementation uses a compare channel to set the delay interval.
    // There may be some advantage to using this approach if one wanted to
    // provide multiple delay timers, each making use of a cap/com channel.
    // Keeping this code in place (but masked out) for reference.

void v_delay_us(uint16_t u16_microseconds)
{
    // Stop counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 &= ~TIM_CR1_CEN;
    // Reset counter
    __HAL_TIM_SET_COUNTER(&DELAY_US_TIMER_HANDLE, 0);
    __HAL_TIM_SET_AUTORELOAD(&DELAY_US_TIMER_HANDLE, 0xFFFF);
    // Set timeout period using compare 1
    __HAL_TIM_SET_COMPARE(&DELAY_US_TIMER_HANDLE, TIM_CHANNEL_1, u16_microseconds);
    // Force an update event, loads counter and autoreload with values set above
    DELAY_US_TIMER_HANDLE.Instance->EGR = TIM_EGR_UG;
    // Clear the compare event flag; this is what is tested to determine if
    // the requested time period has elapsed
    __HAL_TIM_CLEAR_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_CC1);
    // Start counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 |= TIM_CR1_CEN;
    // Wait for delay completion
    while (! __HAL_TIM_GET_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_CC1))
    {
    }
}
#endif

/******************************************************************************
 *
 ******************************************************************************/

void v_debounce_init(debounce_t *p_x_object,
                     level_read_function_t p_f_level_read_function,
                     uint8_t u8_threshold)
{
    if (p_x_object == NULL)
    {
        return;
    }

    memset(p_x_object, 0, sizeof(debounce_t));

    if (p_f_level_read_function == NULL)
    {
        return;
    }

    uint8_t u8_level;

    p_x_object->p_f_level_read_function = p_f_level_read_function;
    p_x_object->u8_debounce_count = u8_threshold;
    p_x_object->u8_debounce_count_threshold = u8_threshold;
    u8_level = p_f_level_read_function();
    p_x_object->u8_debounced_level = u8_level;
    p_x_object->u8_present_level = u8_level;
    p_x_object->u8_previous_level = u8_level;
    p_x_object->x_auto_repeat_mode = DEBOUNCE_NO_AUTO_REPEAT;
}

/******************************************************************************
 * debounced_level_t x_debounce(*p_x_object)
 *
 * Debounce an input pin or other signal returning a binary state
 *
 * p_x_object   : Pointer to a debounce_t struct; should be initialized using
 *                v_debounce_init() prior to use
 *
 * Returns:     Nonzero value if the debounced state of the signal being
 *              monitored has changed.
 *              0 = No change in debounced signal level
 *              1 = Debounced signal level changed from high/true to low/false
 *              2 = Debounced signal level changed from low/false to high/true
 *              3 = Signal at active level has been held for auto-repeat
 *                  interval
 *
 * Notes:
 * The debounced level of the pin or other object being monitored can be
 * checked by examining p_x_object->u8_debounced_level.
 * This function should be called at regular period intervals; e.g. from a
 * periodic timer interrupt.
 ******************************************************************************/

debounced_level_t x_debounce(debounce_t *p_x_object)
{
    debounced_level_t x_return = DEBOUNCED_LEVEL_NO_CHANGE;

    if (p_x_object == NULL)
    {
        return DEBOUNCED_LEVEL_NO_CHANGE;
    }

    if (p_x_object->p_f_level_read_function == NULL)
    {
        return DEBOUNCED_LEVEL_NO_CHANGE;
    }

    p_x_object->u8_previous_level = p_x_object->u8_present_level;
    p_x_object->u8_present_level = (p_x_object->p_f_level_read_function)();

    if (p_x_object->u8_previous_level == p_x_object->u8_present_level)
    {
        if (p_x_object->u8_debounce_count < p_x_object->u8_debounce_count_threshold)
        {
            // Previous and present signal level readings are the same, but not yet
            // verified as stable.
            // Increment debounce/stability count
            p_x_object->u8_debounce_count++;
            return DEBOUNCED_LEVEL_NO_CHANGE;
        }
        else if (p_x_object->u8_debounced_level != p_x_object->u8_present_level)
        {
            // Previous and present signal level readings are the same, and stability
            // count has reached terminal value.
            // Signal has been verified stable, report change in level
            x_return = p_x_object->u8_present_level
                       ? DEBOUNCED_LEVEL_CHANGED_HIGH
                       : DEBOUNCED_LEVEL_CHANGED_LOW;
            p_x_object->u8_debounced_level = p_x_object->u8_present_level;
        }
        else
        {
            // No change in signal level - signal is stable
            // If auto-repeat is enabled, and signal is at the active
            // auto-repeat level, then return a DEBOUNCED_ACTIVE_LEVEL_REPEAT
            // state periodically.
            if ( (
                   (p_x_object->x_auto_repeat_mode == DEBOUNCE_AUTO_REPEAT_LOW)
                   && (p_x_object->u8_debounced_level == 0)
                 )
                 ||
                 (
                   (p_x_object->x_auto_repeat_mode == DEBOUNCE_AUTO_REPEAT_HIGH)
                   && (p_x_object->u8_debounced_level != 0)
                 )
               )
            {
                p_x_object->u8_auto_repeat_count++;
                // Wait for initial delay (after signal stable) before issuing auto-repeats
                if (p_x_object->u8_auto_repeat_count >= p_x_object->u8_auto_repeat_delay)
                {
                    p_x_object->u8_auto_repeat_delay_done = 1;
                }
                // Once the initial delay has elapsed, auto-repeats can be issued at the
                // set repeat interval
                if ( p_x_object->u8_auto_repeat_delay_done
                     && (p_x_object->u8_auto_repeat_count >= p_x_object->u8_auto_repeat_interval) )
                {
                    p_x_object->u8_auto_repeat_count = 0;
                    x_return = DEBOUNCED_ACTIVE_LEVEL_REPEAT;
                }
                else
                {
                    // Waiting for auto-repeat interval to pass
                    // Do nothing
                }
            }
            else
            {
                // Signal is stable, auto-repeat disabled
                // Do nothing
            }
        }
    }
    else
    {
        // Previous and present signal level readings are different.
        // Signal state may be changing.
        // Reset debounce/stability count
        p_x_object->u8_debounce_count = 0;
        p_x_object->u8_auto_repeat_count = 0;
        p_x_object->u8_auto_repeat_delay_done = 0;
    }

    return x_return;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_add_and_calculate_avg(circular_buf_t *x_buf, int16_t i16_val)
{
    if (x_buf->b_full)
    {
        x_buf->i32_sum -= x_buf->i16_data[x_buf->u8_index];
        x_buf->i16_data[x_buf->u8_index] = i16_val;
        x_buf->i32_sum += i16_val;
        x_buf->i16_avg = x_buf->i32_sum / BUFF_SIZE;
    }
    else
    {
        x_buf->i16_data[x_buf->u8_index] = i16_val;
        x_buf->i32_sum += i16_val;
        x_buf->i16_avg = x_buf->i32_sum / (x_buf->u8_index + 1);
    }

    x_buf->u8_prev_index = x_buf->u8_index;
    x_buf->u8_index++;
    if (x_buf->u8_index >= BUFF_SIZE)
    {
        x_buf->u8_index = 0;
        x_buf->b_full = true;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_init_buffer(circular_buf_t *x_buf)
{
    memset(x_buf, 0, sizeof(circular_buf_t));
}

/******************************************************************************
 *
 ******************************************************************************/

void v_debug_config(void)
{
#ifdef DEBUG
    __HAL_RCC_DBGMCU_CLK_ENABLE();
    // Enable debug while in Stop mode
    HAL_DBGMCU_EnableDBGStopMode();

    // Disable peripheral clocks when core halted while debugging
    // - disable IWDG clock to prevent watchdog reset
    // - disable RTC clock to halt RTC time increment
    // - disable TIM14 clock to halt local time increment
    __HAL_DBGMCU_FREEZE_IWDG();
    __HAL_DBGMCU_FREEZE_RTC();
    __HAL_DBGMCU_FREEZE_TIM6();
    __HAL_DBGMCU_FREEZE_TIM7();
    __HAL_DBGMCU_FREEZE_TIM14();
#endif
}

/******************************************************************************
 *
 ******************************************************************************/

void v_get_reset_source(void)
{
    x_reset_source.u8_reset_flags = (RCC->CSR & 0xFF000000) >> 24;
    x_reset_source.x_reset_type = RESET_TYPE_UNKNOWN;

    // The order of tests below is important! The PIN reset flag is set
    // in many cases due to NRST pin being driven low by the MCU when a
    // WWDG, IWDG, etc. reset is triggered. Hence test PIN reset flag
    // last!

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST))
    {
        // Low power reset
        x_reset_source.x_reset_type = RESET_TYPE_LOW_POWER;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PWRRST))
    {
        // BOR or POR/PDR reset
        x_reset_source.x_reset_type = RESET_TYPE_POWER;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_OBLRST))
    {
        // Option byte load reset
        x_reset_source.x_reset_type = RESET_TYPE_OPTION_BYTE;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))
    {
        // Software reset
        x_reset_source.x_reset_type = RESET_TYPE_SOFTWARE;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
    {
        // Independent watchdog reset
        x_reset_source.x_reset_type = RESET_TYPE_IWDG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST))
    {
        // Window watchdog reset
        x_reset_source.x_reset_type = RESET_TYPE_WWDG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))
    {
        // Pin reset (external NRST assert)
        x_reset_source.x_reset_type = RESET_TYPE_PIN;
    }

    // Clear all reset flags
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/******************************************************************************
 *
 ******************************************************************************/

const char * pc_reset_source_description(reset_type_t  x_reset_type)
{
    if (x_reset_type >= RESET_TYPE_MAX)
    {
        x_reset_type  = RESET_TYPE_UNKNOWN;
    }
    return reset_source_str[x_reset_type];
}

/******************************************************************************
 *
 ******************************************************************************/

void v_flash_rdp_check(void)
{
    FLASH_OBProgramInitTypeDef flashConfig;
    HAL_StatusTypeDef flashStatus;

    // Unlock the Flash to enable the flash control register access
    HAL_FLASH_Unlock();

    // Unlock the Options Bytes
    HAL_FLASH_OB_Unlock();
    HAL_FLASHEx_OBGetConfig(&flashConfig);

    LOGCT(LOG_SYSTEM, "Flash RDP level: 0x%02lX", flashConfig.RDPLevel);

    if (flashConfig.RDPLevel == OB_RDP_LEVEL_0)
    {
        flashConfig.OptionType = OPTIONBYTE_RDP;
        flashConfig.RDPLevel = OB_RDP_LEVEL_1;

        flashStatus = HAL_FLASHEx_OBProgram(&flashConfig);
        LOGCT(LOG_SYSTEM, "OB programmed, status %d", flashStatus);

        if (flashStatus == HAL_OK)
        {
            LOGCT(LOG_SYSTEM, "OB programmed, loading...");
            HAL_FLASHEx_OBGetConfig(&flashConfig);
            // Load the new option byte values
            // Doing this resets the MCU
            flashStatus = HAL_FLASH_OB_Launch();
            LOGCT(LOG_SYSTEM, "OB_Launch() status: %d", flashStatus);
        }
    }

//    HAL_FLASHEx_OBGetConfig(&flashConfig);

    // Lock the Options Bytes
    HAL_FLASH_OB_Lock();

    // Lock the Flash to disable the flash control register access
    HAL_FLASH_Lock();
}

/******************************************************************************
 *
 ******************************************************************************/

// Note: These functions modify the STM HAL tick counter (uwTick)
// This is the same value that is returned when HAL_GetTick() is called

//extern volatile uint32_t uwTick;

void v_system_tick_set(uint32_t u32_tick_set)
{
  ATOMIC_BLOCK_BEGIN
    uwTick = u32_tick_set;
  ATOMIC_BLOCK_END
}

void v_system_tick_add(uint32_t u32_tick_add)
{
  ATOMIC_BLOCK_BEGIN
    uwTick += u32_tick_add;
  ATOMIC_BLOCK_END
}

/******************************************************************************
 *
 ******************************************************************************/
// Change this to match the RTC_WAKEUPCLOCK_RTCCLK_DIVxx selected when
// HAL_RTCEx_SetWakeUpTimer_IT() is called
#define WAKEUP_TIMER_PRESCALER_DIV      16
#define WAKEUP_TIMER_PRESCALER_SELECT   RTC_WAKEUPCLOCK_RTCCLK_DIV16

// WARNING: Maximum wakeup time with typical RTC configuration
// prescaler = /16 and RTC clock = 32000Hz is ~32767 mS

void v_set_rtc_wakeup_timer(uint16_t u16_duration_ms)
{
#if 0
    uint32_t u32_wakeup_time_set;
    HAL_StatusTypeDef x_status;

    // Deactivate existing wake up timer
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

    // Convert wakeup time in mS to wakeup timer units
    // Wakeup timer tick rate = RTC clock / RTC wakeup timer clock prescaler
    u32_wakeup_time_set = (uint32_t) u16_duration_ms * RTC_CLOCK_HZ / (1000 * WAKEUP_TIMER_PRESCALER_DIV);
    if (u32_wakeup_time_set >= 0x10000) u32_wakeup_time_set = 0xFFFF;

    // Start wakeup timer
    x_status = HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, u32_wakeup_time_set, WAKEUP_TIMER_PRESCALER_SELECT);
    if (x_status != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RTC_WAKEUPTIMER_EXTI_ENABLE_EVENT();
    __HAL_RTC_WAKEUPTIMER_EXTI_ENABLE_IT();
#endif
}

/******************************************************************************
 *
 ******************************************************************************/

// FIXME: <u64_get_rtc_time> Probably don't need most of this.
// Can save considerable code space if C <time> library calls such as mktime()
// could be removed.
// This routine is used primarily to figure out how long the MCU was in sleep
// mode, and as this time is limited to 20s or so, the time in sleep could
// be calculated using a much simpler approach than doing a complete
// epoch time calculation. It could be done using the RTC minutes, seconds,
// and subseconds values to calculate a "milliseconds into the hour", along
// with some modulo arithmetic.
// Sprayer code no longer uses -any- 64-bit tick counters/timers for delay and
// event timing; everything is now based on the HAL tick timer, which -does- get
// adjusted using the time-in-sleep calculations that make use of this routine.

uint64_t u64_get_rtc_time(void)
{
#if 0
    uint64_t            u64_timestamp_ms;
    uint32_t            u32_subsec_ms;
    time_t              x_time;
    struct tm           x_tm;
    RTC_TimeTypeDef     x_rtc_time = {0};
    RTC_DateTypeDef     x_rtc_date = {0};
    HAL_StatusTypeDef   x_status = HAL_OK;

    //Read Time from HW RTC
    x_status = HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN);
    if (x_status != HAL_OK)
    {
        LOGCT(LOG_PM,"HAL_RTC_GetTime() error: %u", x_status);
    }

    //Read Date from HW RTC
    x_status = HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN);
    if(x_status != HAL_OK)
    {
        LOGCT(LOG_PM,"HAL_RTC_GetDate() error: %u",x_status);
    }

    // Update calendar time
    x_tm.tm_year = (x_rtc_date.Year + 100);       // Year + 2000 - 1900
    x_tm.tm_mon = x_rtc_date.Month - 1;           // Month, where 0 = jan
    x_tm.tm_mday = x_rtc_date.Date;               // Day of the month
    x_tm.tm_hour = x_rtc_time.Hours;
    x_tm.tm_min = x_rtc_time.Minutes;
    x_tm.tm_sec = x_rtc_time.Seconds;
    x_tm.tm_isdst = 0;                            // Is DST on? 1 = yes, 0 = no, -1 = unknown

    //Convert the time to Epoch time
    x_time = mktime(&x_tm);
    u64_timestamp_ms = (uint64_t) x_time;
    u64_timestamp_ms *= 1000;

    // Convert RTC subseconds to milliseconds and add to epoch time
    // The STM32 RTC is typically configured to count subseconds in
    // 1/256-second units (~3.9 mS units)
    // However, this can vary depending on how the RTC is configured.
    // x_rtc_time.SubSeconds will be a value between 0..hrtc->Init.SynchPrediv
    // As this counter counts down from SynchPrediv to 0, the actual subsecond
    // value is calculated as hrtc->Init.SynchPrediv - x_rtc_time.SubSeconds
    // For these calculations, it is assumed that the RTC clock source is
    // LSI (Low Speed Internal), running at 32.000 KHz +/- 5%

    u32_subsec_ms = (hrtc.Init.SynchPrediv - x_rtc_time.SubSeconds)
                    * SYSTICK_TIMEBASE_HZ
                    * (hrtc.Init.AsynchPrediv + 1)
                    / RTC_CLOCK_HZ;
    u64_timestamp_ms += u32_subsec_ms;

    LOGCT(LOG_PM, "RTC TIME: 20%02u/%02u/%02u %02u:%02u:%02u.%03lu (%lu)",
          x_rtc_date.Year, x_rtc_date.Month, x_rtc_date.Date,
          x_rtc_time.Hours, x_rtc_time.Minutes, x_rtc_time.Seconds, u32_subsec_ms,
          (uint32_t) u64_timestamp_ms);

    return u64_timestamp_ms;
#else
    return 0;
#endif
}

//------------------------------------------------------------------------------

uint32_t u32_get_rtc_hour_time(void)
{
#if 0
    uint32_t            u32_hour_time_ms;
    uint32_t            u32_subsec_ms;
    RTC_TimeTypeDef     x_rtc_time;
    RTC_DateTypeDef     x_rtc_date;

    //Read Time from HW RTC

    memset(&x_rtc_time, 0, sizeof(x_rtc_time));
    memset(&x_rtc_time, 0, sizeof(x_rtc_date));
    HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN);
    // Date info is not needed, but needs to be read to prevent RTC from
    // latching the time/date counts
    HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN);

    // Convert RTC subseconds to milliseconds and add to epoch time
    // The STM32 RTC is typically configured to count subseconds in
    // 1/256-second units (~3.9 mS units)
    // However, this can vary depending on how the RTC is configured.
    // x_rtc_time.SubSeconds will be a value between 0..hrtc->Init.SynchPrediv
    // As this counter counts down from SynchPrediv to 0, the actual subsecond
    // value is calculated as hrtc->Init.SynchPrediv - x_rtc_time.SubSeconds
    // For these calculations, it is assumed that the RTC clock source is
    // LSI (Low Speed Internal), running at 32.000 KHz +/- 5%

    u32_subsec_ms = (hrtc.Init.SynchPrediv - x_rtc_time.SubSeconds)
                    * SYSTICK_TIMEBASE_HZ
                    * (hrtc.Init.AsynchPrediv + 1)
                    / RTC_CLOCK_HZ;

    // Time in mS for the present hour + subseconds

    u32_hour_time_ms = (uint32_t) x_rtc_time.Minutes * 60 * SYSTICK_TIMEBASE_HZ
                       + (uint32_t) x_rtc_time.Seconds * SYSTICK_TIMEBASE_HZ
                       + u32_subsec_ms;

    LOGCT(LOG_PM, "RTC hour time: %02u:%02u.%03lu (%lu mS)"
          ,x_rtc_time.Minutes
          ,x_rtc_time.Seconds
          ,u32_subsec_ms
          ,u32_hour_time_ms
         );

    return u32_hour_time_ms;
#else
    return 0;
#endif
}
