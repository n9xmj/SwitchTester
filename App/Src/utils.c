/******************************************************************************
 * utils.c
 *
 * Utility functions that do not fall under any major operational category.
 ******************************************************************************/

#include "device_config.h"              // Includes debug_config.h, main.h, platform.h
#include "stdio_retarget.h"
#include "utils.h"
//#include <time.h>                       // For v_get_rtc_time; uses mktime()

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
 * void v_app_polling_task(void)
 *
 * This task is defined here as a weak stub. It is intended to be overridden
 * in user code, using it to feed background processing tasks during blocking
 * operations.
 * It is used in this API with i_getchar_blocking
 ******************************************************************************/

void __attribute__((weak)) v_app_polling_task(void)
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

int i_getchar_blocking(void)
{
    int i_char;

    do
    {
        v_app_polling_task();
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
    GETLINE_CANCEL_EXIT,        /* Ctrl-C: silent abandon, returns -2 */
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

        else if (i_key == ('C' - 0x40)) // Ctrl-C (silent abandon)
        {
            /* Deliberately emits nothing -- no <Cancel>, no CRLF, no erase.
             * The automation console uses this as its exit from human mode and
             * any unframed output there would be noise on a machine-readable
             * stream. Callers that only distinguish "cancelled" see the same
             * negative return as ESC. */
            u8_done = GETLINE_CANCEL_EXIT;
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

    if (u8_done == GETLINE_ESCAPE_EXIT)
    {
        return -1;
    }
    if (u8_done == GETLINE_CANCEL_EXIT)
    {
        return -2;
    }
    return i_len;
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
 * void v_delay_pump(uint32_t u32_ticks)
 *
 * Blocking delay, like HAL_Delay(), but pumps v_app_polling_task() during
 * the delay interval. The tick interval is determined by the interval used
 * to feed the HAL uwTick counter, normally incremented on SysTick interrupts,
 * typically 1mS
 ******************************************************************************/

void v_delay_pump(uint32_t u32_ticks)
{
    uint32_t u32_timestamp = HAL_GetTick();
    do
    {
        v_app_polling_task();
    }
    while ((HAL_GetTick() - u32_timestamp) < u32_ticks);
}

/******************************************************************************
 *
 ******************************************************************************/

reset_type_t x_get_reset_source(void)
{
    uint8_t u8_reset_flags = (uint8_t) ((RCC->CSR & 0xFF000000) >> 24);
    if (u8_reset_flags == 0)
    {
        return x_reset_source.x_reset_type;
    }

    x_reset_source.u8_reset_flags = u8_reset_flags;
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

    return x_reset_source.x_reset_type;
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

// Arms the RTC wakeup timer for approximately the requested interval, deriving
// every timing parameter from the RTC's *current* hardware configuration rather
// than from compile-time constants. Nothing about the RTC is forced: whatever
// wakeup-clock prescaler MX_RTC_Init() (or a later runtime change) left in
// RTC_CR is honored, and only the autoreload value (WUTR) is written.
//
// Returns the actual number of milliseconds that will elapse for the value
// programmed, or 0 if the wakeup timer could not be armed (RTC has no running
// clock source, or is configured for the ck_spre (1 Hz) wakeup clock, which
// this millisecond-resolution helper does not serve).
//
// NOTE: If the RTC clock source is the internal RC (LSI), the elapsed time is
// subject to the LSI tolerance (nominally 32 kHz +/- 5%).

uint32_t u32_set_rtc_wakeup_timer(uint16_t u16_duration_ms)
{
    uint32_t u32_rtcclk_hz;
    uint32_t u32_wucksel;
    uint32_t u32_prescaler_div;
    uint32_t u32_wakeup_tick_hz;
    uint32_t u32_wakeup_ticks;

    // RTCCLK source frequency as actually configured (LSE / LSI / HSE/32);
    // returns 0 if no source is selected and running.
    u32_rtcclk_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_RTC);
    if (u32_rtcclk_hz == 0U)
    {
        return 0U;                          // RTC has no clock -> cannot arm
    }

    // Honor whatever wakeup-clock selection the RTC currently holds.
    u32_wucksel = READ_BIT(RTC->CR, RTC_CR_WUCKSEL);

    // This helper only serves the RTCCLK/N (sub-second) wakeup modes. The
    // ck_spre modes tick at 1 Hz, which cannot represent a millisecond interval.
    if ((u32_wucksel & RTC_CR_WUCKSEL_2) != 0U)
    {
        return 0U;                          // ck_spre (1 Hz) mode -> not a ms wakeup
    }

    // WUCKSEL[1:0] selects RTCCLK/16, /8, /4, /2 for codes 0, 1, 2, 3.
    u32_prescaler_div  = 16U >> (u32_wucksel & 0x3U);
    u32_wakeup_tick_hz = u32_rtcclk_hz / u32_prescaler_div;

    // The wakeup event fires every (WUTR + 1) wakeup-timer ticks, so the number
    // of ticks for the requested interval is programmed as WUTR = ticks - 1.
    u32_wakeup_ticks = ((uint32_t) u16_duration_ms * u32_wakeup_tick_hz) / 1000U;
    if (u32_wakeup_ticks == 0U)        u32_wakeup_ticks = 1U;
    if (u32_wakeup_ticks > 0x10000U)   u32_wakeup_ticks = 0x10000U;  // WUTR is 16-bit

// *** REMOVE AFTER DEBUG ***
printf("WUT set to %lu ticks\r\n", u32_wakeup_ticks);

    // Arm using the existing clock selection; SetWakeUpTimer_IT() handles the
    // disable / WUTWF-wait / WUTR write / WUCKSEL set / EXTI-IT / enable sequence.
    if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, u32_wakeup_ticks - 1U, u32_wucksel) != HAL_OK)
    {
        return 0U;
    }

    // SetWakeUpTimer_IT() clears the RTC WUTF flag, but any wakeup interrupt
    // that was already latched in the NVIC (e.g. from a previously-running
    // wakeup timer) stays pending on the shared RTC_TAMP vector. A pending IRQ
    // makes a subsequent WFI fall straight through STOP without ever sleeping,
    // so clear it here to guarantee an arm-then-sleep sequence actually sleeps.
    // NB: this vector is shared with the RTC alarm/tamper; clearing it drops a
    // coincidentally-pending alarm/tamper IRQ, which is acceptable for the
    // wakeup-before-STOP use case this function is built for.
    HAL_NVIC_ClearPendingIRQ(RTC_TAMP_IRQn);

    // Truthful elapsed time for the value actually programmed.
    return (u32_wakeup_ticks * 1000U) / u32_wakeup_tick_hz;
}

/******************************************************************************
 *
 ******************************************************************************/

// Disarm (stop) the RTC wakeup timer previously armed by
// u32_set_rtc_wakeup_timer(). This genuinely stops the timer: it clears WUTE
// (halting the wakeup down-counter) and WUTIE (its interrupt enable), and
// nothing else -- all other RTC functions (alarm, tamper, timestamp), the
// shared RTC EXTI line, and the shared RTC NVIC interrupt are left untouched.
//
// Fire-and-forget companion to u32_set_rtc_wakeup_timer(): arm it before
// entering a low-power/STOP state, disarm it on exit. Safe to call even when
// the wakeup timer is not currently armed.
//
// Assumes the RTC and its global interrupt are configured by the CubeMX init
// (HAL_RTC_MspInit enables the RTC clock and the RTC_TAMP NVIC line).

void v_stop_rtc_wakeup_timer(void)
{
    // With no running RTC clock source the wakeup timer cannot be running, so
    // there is nothing to stop. Bailing here also avoids HAL_RTCEx_Deactivate...
    // blocking on the WUTWF poll (which needs RTCCLK to ever assert).
    if (HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_RTC) == 0U)
    {
        return;
    }

    (void) HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
}

/******************************************************************************
 *
 ******************************************************************************/

uint32_t u32_get_rtc_hour_time(void)
{
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

    // Subsecond fraction of the current second, straight from the RTC's own
    // definition (RM / HAL): fraction = (PREDIV_S - SSR) / (PREDIV_S + 1).
    // HAL_RTC_GetTime() reads both live from hardware -- SubSeconds from RTC_SSR
    // and SecondFraction from RTC_PRER's PREDIV_S field -- so this is exact
    // regardless of clock source or prescaler tuning, with no RTCCLK /
    // AsynchPrediv / tick-rate assumptions. (SSR counts down 0..PREDIV_S, so the
    // numerator is always >= 0 and the +1 denominator is never zero.)

    u32_subsec_ms = (1000UL * (x_rtc_time.SecondFraction - x_rtc_time.SubSeconds))
                    / (x_rtc_time.SecondFraction + 1UL);

    // Milliseconds elapsed in the present hour (minutes + seconds + subseconds)

    u32_hour_time_ms = ((uint32_t) x_rtc_time.Minutes * 60UL * 1000UL)
                       + ((uint32_t) x_rtc_time.Seconds * 1000UL)
                       + u32_subsec_ms;

    return u32_hour_time_ms;
}
