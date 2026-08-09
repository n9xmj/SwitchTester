/******************************************************************************
 * debug_menu.c
 *
 * Bare-bones console debug menu built on the menusystem framework.
 *
 * Skeleton content only: [?] help plus two no-op quick-test stubs. Build the
 * menu up by adding menu_item_t entries to x_debug_top_menu (and sub-menus)
 * and pointing them at your own command handlers.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include <stdlib.h>                  /* strtoul() for the pulse-width entry */
#include <string.h>                  /* memset() for the NVM pool erase */

#include "device_config.h"          /* stdint/stdio, platform.h (SYSTEM_TICK), main.h */
#include "menusystem.h"
#include "debug_menu.h"
#include "utils.h"                   /* RTC wakeup + hour-time helpers under test */
#include "rtc.h"                     /* hrtc, for post-STOP HAL_RTC_WaitForSynchro */
#include "switch_out.h"              /* SWITCH_A..D drive control */
#include "nvmparams.h"               /* NVM pool dump / erase diagnostics */
#include "automation_console.h"      /* Host/script command interface */

/*============================================================================
 * PRIVATE PROTOTYPES
 *==========================================================================*/

static void v_debug_wakeup_sleep_test(void);
static void v_debug_automation_console(void);
static void v_debug_quick_test_1(void);
static void v_debug_quick_test_2(void);
static void v_debug_at_main_menu(void);
static void v_debug_menu_exec(char c_key);

static void v_switch_key_off(char c_key, uint8_t u8_index);
static void v_switch_key_on(char c_key, uint8_t u8_index);
static void v_switch_key_pulse(char c_key, uint8_t u8_index);
static void v_switch_key_toggle(char c_key, uint8_t u8_index);
static void v_switch_set_pulse_width(void);
static void v_switch_show_state(void);
static void v_switch_all_off(void);

static void v_cycle_help_text(void);
static void v_cycle_key_param(char c_key, uint8_t u8_index);
static void v_cycle_key_startstop(char c_key, uint8_t u8_index);
static void v_cycle_stop_all(void);
static void v_debug_nvm_dump(void);
static void v_debug_nvm_erase(void);
static void v_debug_soft_reset(void);

/*============================================================================
 * PRIVATE FUNCTIONS (menu command handlers)
 *==========================================================================*/

/* ---------------------------------------------------------------------------
 * TEMPORARY test scaffolding: live "unit test" for the two RTC helpers,
 * u32_set_rtc_wakeup_timer() and u32_get_rtc_hour_time(). Arms the RTC wakeup
 * timer, drops into STOP1, and reports how long we were actually asleep
 * (measured via the RTC, which keeps running in STOP). Strip before porting the
 * clean helpers back to G0B1_Skeleton.
 * ------------------------------------------------------------------------- */

#define DEBUG_SLEEP_TEST_TIME   2000    /* Milliseconds, approx */

extern void SystemClock_Config(void);   /* defined in main.c; STOP reverts to HSI16 */

static void v_debug_wakeup_sleep_test(void)
{
    uint32_t u32_enter_sleep_hour_time;
    uint32_t u32_exit_sleep_hour_time;
    uint32_t u32_in_sleep_time;
    uint32_t u32_actual_wakeup_ms;

    printf("Sleep mode test - going to sleep for %u mS\r\n"
           "HAL-reported RTC clock frequency: %u Hz\r\n"
          ,(unsigned) DEBUG_SLEEP_TEST_TIME
          ,(unsigned) HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_RTC));

    u32_enter_sleep_hour_time = u32_get_rtc_hour_time();
    u32_actual_wakeup_ms = u32_set_rtc_wakeup_timer(DEBUG_SLEEP_TEST_TIME);
    printf("Sleep entry hour timestamp: %lu\r\n"
           "Wakeup timer armed for ~%lu mS\r\n"
          ,u32_enter_sleep_hour_time
          ,u32_actual_wakeup_ms);

    /* printf() here is blocking + unbuffered, so the console TX has fully
     * drained before we sleep. Mask every wake source except the RTC so only
     * the wakeup timer can bring us out of STOP: SysTick (HAL_SuspendTick), the
     * app's TIM6 10 ms tick (EXTI4_15's button is externally pulled up, so it
     * shouldn't fire, but mask it too to leave the RTC as the sole waker). */
    HAL_SuspendTick();
    HAL_NVIC_DisableIRQ(TIM14_IRQn);
//    HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);

    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERMODE_STOP1, PWR_STOPENTRY_WFI);

    v_stop_rtc_wakeup_timer();                          /* disarm the wakeup timer after the test */

    /* After STOP the RTC calendar shadow (TR/DR/SSR) is de-synchronised: the APB
     * read interface was clocked off during STOP. Wait for RSF before reading,
     * or the calendar read can return a torn/stale value -- which shows up as a
     * bogus small "time in sleep" even when the core slept the full interval. */
    HAL_RTC_WaitForSynchro(&hrtc);
    u32_exit_sleep_hour_time = u32_get_rtc_hour_time(); /* record sleep-exit hour time */
    SystemClock_Config();                               /* restore 64 MHz PLL before console use */
    /* Calculate time-in-sleep and add to HAL tick */
    u32_in_sleep_time = (uint32_t) (u32_exit_sleep_hour_time - u32_enter_sleep_hour_time);
    v_system_tick_add(u32_in_sleep_time);
    HAL_ResumeTick();

    /* --- Turn on masked interrupts if needed --- */
    HAL_NVIC_EnableIRQ(TIM14_IRQn);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

    printf("Time in sleep: ~%lu mS, exit hour time:%lu\r\n",
           u32_in_sleep_time, u32_exit_sleep_hour_time);
}

/*
 * Hand-driven entry to the automation console. HUMAN mode, because only a
 * person picks a menu key -- the entry path carries the intent, so neither of
 * the common cases needs a mode command at all.
 *
 * Ctrl-C returns here, and so does 'Q'. There is no idle timeout in this mode:
 * that guard exists so a dead HOST cannot wedge the board, and here there is an
 * operator sitting at the terminal instead.
 */
static void v_debug_automation_console(void)
{
    printf("Automation console - Ctrl-C or 'Q' to return, 'L' lists ops\r\n");
    v_automation_console_run(ACON_MODE_HUMAN);
    printf("\r\nReturned from automation console\r\n");
}

static void v_debug_quick_test_1(void)
{
    // Logging API integration test -- kept identical to the one in
    // G0B1_Skeleton so the two trees can be compared directly.
    //
    // Exercises every macro form in log_helpers.h against the vendored module
    // in App/logging/, and confirms the application-supplied timestamp bridge
    // (u32_log_timestamp_ms in logging_port.c) is the one being called -- a
    // weak-default fallback would show (0.000) on every line.

    printf("\r\n--- logging API test ---\r\n");

    // Timestamped + [TAG] forms. LOGCT takes its color from the tag.
    LOGCT(LOG_SYSTEM, "LOGCT: tag color, value = %d", 42);
    LOG(LOG_SYSTEM, "LOG: no color, string = %s", "abc");
    LOGC(LOG_SYSTEM, LOGC_WARNING, "LOGC: explicit color (warning)");
    LOGC(LOG_SYSTEM, LOGC_ERROR, "LOGC: explicit color (error)");

    // Plain forms: no timestamp, no [TAG] prefix.
    LOG_PLAIN(LOG_SYSTEM, "LOG_PLAIN: bare text, no prefix\r\n");
    LOGC_PLAIN(LOG_SYSTEM, LOGC_CYAN, "LOGC_PLAIN: colored, no prefix");
    LOGCT_PLAIN(LOG_SYSTEM, "LOGCT_PLAIN: tag color, no prefix");

    // Build-gated forms.
    DPRINTF("DPRINTF: DEBUG-build only, no newline added\r\n");
    DPRINTF_TS("DPRINTF_TS: DEBUG-build only, timestamped");
    RPRINTF("RPRINTF: unconditional, survives a release build\r\n");

    // A class set to LOG_LEVEL_QUIET compiles out entirely -- this line should
    // produce no output at all.
    LOGCT(LOG_JOBS, "LOG_JOBS is QUIET; you should NOT see this");

    // Verbosity ladder. Every value below is a compile-time constant, so these
    // are the decisions the compiler actually made, not a runtime re-check.
    printf("\r\n  LOG_LEVEL = %d (0=QUIET 1=ALWAYS 2=ERROR 3=WARNING 4=INFO 5=DEBUG)\r\n",
           LOG_LEVEL);
    printf("  %-12s tier %d  emit=%d\r\n", LOG_SYSTEM_TAG, LOG_SYSTEM, LOG_EMIT(LOG_SYSTEM));
    printf("  %-12s tier %d  emit=%d\r\n", LOG_JOBS_TAG,   LOG_JOBS,   LOG_EMIT(LOG_JOBS));
    printf("  %-12s tier %d  emit=%d\r\n", LOG_EXTI_TAG,   LOG_EXTI,   LOG_EMIT(LOG_EXTI));

    // The edges that the ladder ordering exists to get right.
    printf("  a QUIET class under a DEBUG ceiling  -> emit=%d (want 0)\r\n",
           (LOG_LEVEL_QUIET != LOG_LEVEL_QUIET && LOG_LEVEL_QUIET <= LOG_LEVEL_DEBUG));
    printf("  an ALWAYS class under a QUIET ceiling -> emit=%d (want 0)\r\n",
           (LOG_LEVEL_ALWAYS != LOG_LEVEL_QUIET && LOG_LEVEL_ALWAYS <= LOG_LEVEL_QUIET));
    printf("  an ERROR class under a WARNING ceiling-> emit=%d (want 1)\r\n",
           (LOG_LEVEL_ERROR != LOG_LEVEL_QUIET && LOG_LEVEL_ERROR <= LOG_LEVEL_WARNING));
    printf("  a DEBUG class under a WARNING ceiling -> emit=%d (want 0)\r\n",
           (LOG_LEVEL_DEBUG != LOG_LEVEL_QUIET && LOG_LEVEL_DEBUG <= LOG_LEVEL_WARNING));

    // Single-statement behaviour: with the do{}while(0) wrapper this compiles
    // and takes the else. A bare braced macro body would not compile at all.
    if (LOG_SYSTEM == LOG_LEVEL_QUIET)
        LOGCT(LOG_SYSTEM, "dangling-else check: taken the wrong way");
    else
        printf("  dangling-else check: compiled and took the else\r\n");

    // Two timestamps a known interval apart. The delta proves the tick is
    // real and advancing rather than a stuck constant.
    LOGCT(LOG_SYSTEM, "timestamp check: t0");
    v_delay_ms(250);
    LOGCT(LOG_SYSTEM, "timestamp check: t0 + 250 mS");

    printf("--- end logging API test ---\r\n");
}

static void v_debug_quick_test_2(void)
{
    printf("Quick test function 2 (stub)\r\n");
}

static void v_debug_at_main_menu(void)
{
    printf("(at main menu level)\r\n");
}

/* ---------------------------------------------------------------------------
 * SWITCH_A..D manual drive.
 *
 * The four key-list handlers below are reached via MENU_ITEM_KEY_LIST_FUNCTION,
 * which hands the handler the index of the key within its key list -- that
 * index IS the switch channel number, so no key-to-channel decoding is needed.
 * ------------------------------------------------------------------------- */

#define SWITCH_PULSE_WIDTH_MIN_MS       1
#define SWITCH_PULSE_WIDTH_MAX_MS       60000

/*
 * Standard numeric entry. The prompt shows the present setting; an empty entry
 * keeps it, ESC abandons it unchanged, and anything unparseable or out of range
 * warns and re-prompts. The re-prompt loop is unbounded with ESC as the way out,
 * so a bad entry can never trap the user.
 *
 * i_getline() is the only input path used anywhere in the menu -- it keeps
 * v_app_polling_task() pumped while it blocks.
 *
 * Returns nonzero if *p_u32_value was updated.
 */
static uint8_t u8_debug_entry_u32(const char *pc_prompt, uint32_t u32_min,
                                  uint32_t u32_max, uint32_t *p_u32_value)
{
    char str_entry[16];
    int i_length;
    unsigned long ul_value;
    char *pc_end;

    do
    {
        printf("%s [now %lu]: ", pc_prompt, (unsigned long) *p_u32_value);

        i_length = i_getline(str_entry, sizeof(str_entry) - 1);

        if (i_length < 0)
        {
            printf("Cancelled - unchanged\r\n");
            return 0;
        }
        if (i_length == 0)
        {
            printf("Unchanged\r\n");
            return 0;
        }

        ul_value = strtoul(str_entry, &pc_end, 10);
        if ((*pc_end == 0) && (ul_value >= u32_min) && (ul_value <= u32_max))
        {
            *p_u32_value = (uint32_t) ul_value;
            return 1;
        }

        printf("Invalid entry [%s] - range is %lu..%lu\r\n",
               str_entry, (unsigned long) u32_min, (unsigned long) u32_max);
    }
    while (1);
}

static const char * pc_switch_state_text(uint8_t u8_channel)
{
    switch (x_switch_out_get(u8_channel))
    {
        case SWITCH_OUT_ON:     return "ON ";
        case SWITCH_OUT_TIMED:  return "TMR";
        case SWITCH_OUT_OFF:
        default:                return "off";
    }
}

static void v_switch_report(uint8_t u8_channel)
{
    printf("SWITCH_%s (%-4s) : %s\r\n",
           pc_switch_out_name(u8_channel),
           pc_switch_out_pin_name(u8_channel),
           pc_switch_state_text(u8_channel));
}

static void v_switch_key_off(char c_key, uint8_t u8_index)
{
    (void) c_key;
    v_switch_out_set(u8_index, 0);
    v_switch_report(u8_index);
}

static void v_switch_key_on(char c_key, uint8_t u8_index)
{
    (void) c_key;
    v_switch_out_set(u8_index, 1);
    v_switch_report(u8_index);
}

static void v_switch_key_toggle(char c_key, uint8_t u8_index)
{
    (void) c_key;
    v_switch_out_toggle(u8_index);
    v_switch_report(u8_index);
}

static void v_switch_key_pulse(char c_key, uint8_t u8_index)
{
    (void) c_key;
    v_switch_out_pulse(u8_index, u32_switch_out_get_pulse_width());
    printf("SWITCH_%s (%-4s) : pulse %lu mS\r\n",
           pc_switch_out_name(u8_index),
           pc_switch_out_pin_name(u8_index),
           (unsigned long) u32_switch_out_get_pulse_width());
}

static void v_switch_set_pulse_width(void)
{
    uint32_t u32_width = u32_switch_out_get_pulse_width();

    if (u8_debug_entry_u32("Pulse width, mS",
                           SWITCH_PULSE_WIDTH_MIN_MS, SWITCH_PULSE_WIDTH_MAX_MS,
                           &u32_width))
    {
        v_switch_out_set_pulse_width(u32_width);
        printf("Pulse width now %lu mS\r\n", (unsigned long) u32_width);
    }
}

static void v_switch_show_state(void)
{
    uint8_t u8_channel;
    uint32_t u32_remaining;

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        printf("SWITCH_%s (%-4s) : %s",
               pc_switch_out_name(u8_channel),
               pc_switch_out_pin_name(u8_channel),
               pc_switch_state_text(u8_channel));

        u32_remaining = u32_switch_out_pulse_remaining(u8_channel);
        if (u32_remaining)
        {
            printf("  [pulse, %lu mS left]", (unsigned long) u32_remaining);
        }
        v_newline();
    }
    printf("Pulse width: %lu mS\r\n", (unsigned long) u32_switch_out_get_pulse_width());
}

static void v_switch_all_off(void)
{
    v_switch_out_all_off();
    printf("All switch outputs off\r\n");
}

/* ---------------------------------------------------------------------------
 * SWITCH_A..D automatic cycling.
 *
 * Twelve editable parameters -- repeat / on-time / off-time for each of four
 * channels -- are bound by a single MENU_ITEM_KEY_LIST_FUNCTION over the key
 * list below. The framework hands the handler the key's index within that list,
 * which decodes straight to a (channel, parameter) pair with the same
 * arithmetic the NVM IDs use.
 *
 * Key-list entries print nothing in the menu help, so the whole table is drawn
 * by v_cycle_help_text() via MENU_ITEM_HELP_TEXT_VARIABLE, which lets each line
 * carry its live value.
 * ------------------------------------------------------------------------- */

#define CYCLE_PARAM_KEYS        "abcdefghijkl"
#define CYCLE_RUN_KEYS          "1234"

static const char * apc_cycle_param_label[SWITCH_CYCLE_PARAM_COUNT] =
{
    "repeat count",
    "on time, uS ",
    "off time, uS"
};

/* Microseconds as a pseudo-decimal millisecond value. Integer maths only: the
 * project is floating-point clean and stays that way. The %03lu zero-pad on the
 * fraction is load-bearing -- without it 500001 uS renders as "500.1 mS". */
static void v_cycle_print_us(uint32_t u32_us)
{
    printf("%10lu  (%lu.%03lu mS)",
           (unsigned long) u32_us,
           (unsigned long) (u32_us / 1000UL),
           (unsigned long) (u32_us % 1000UL));
}

static void v_cycle_help_text(void)
{
    uint8_t u8_channel;

    v_newline();

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        const switch_cycle_t *p_x_cycle = &g_x_switch_cycle[u8_channel];
        const char *pc_name = pc_switch_out_name(u8_channel);
        uint8_t u8_key_base = u8_channel * SWITCH_CYCLE_PARAM_COUNT;

        printf("[%c] Switch %s %s ",
               CYCLE_PARAM_KEYS[u8_key_base + SWITCH_CYCLE_PARAM_REPEAT],
               pc_name, apc_cycle_param_label[SWITCH_CYCLE_PARAM_REPEAT]);
        if (p_x_cycle->u32_repeat_count == 0)
        {
            printf("%10s\r\n", "infinite");
        }
        else
        {
            printf("%10lu\r\n", (unsigned long) p_x_cycle->u32_repeat_count);
        }

        printf("[%c] Switch %s %s ",
               CYCLE_PARAM_KEYS[u8_key_base + SWITCH_CYCLE_PARAM_ON],
               pc_name, apc_cycle_param_label[SWITCH_CYCLE_PARAM_ON]);
        v_cycle_print_us(p_x_cycle->u32_on_time_us);
        v_newline();

        printf("[%c] Switch %s %s ",
               CYCLE_PARAM_KEYS[u8_key_base + SWITCH_CYCLE_PARAM_OFF],
               pc_name, apc_cycle_param_label[SWITCH_CYCLE_PARAM_OFF]);
        v_cycle_print_us(p_x_cycle->u32_off_time_us);
        v_newline();

        v_newline();
    }

    for (u8_channel = 0; u8_channel < SWITCH_OUT_COUNT; u8_channel++)
    {
        const switch_cycle_t *p_x_cycle = &g_x_switch_cycle[u8_channel];

        printf("[%c] Start/stop Switch %s cycling    ",
               CYCLE_RUN_KEYS[u8_channel], pc_switch_out_name(u8_channel));

        if (! p_x_cycle->u8_running)
        {
            printf("-- idle --\r\n");
        }
        else if (p_x_cycle->u32_repeat_count)
        {
            printf("RUNNING, %lu of %lu\r\n",
                   (unsigned long) p_x_cycle->u32_cycles_done,
                   (unsigned long) p_x_cycle->u32_repeat_count);
        }
        else
        {
            printf("RUNNING, %lu cycles\r\n",
                   (unsigned long) p_x_cycle->u32_cycles_done);
        }
    }

    v_newline();
}

static void v_cycle_key_param(char c_key, uint8_t u8_index)
{
    uint8_t u8_channel   = u8_index / SWITCH_CYCLE_PARAM_COUNT;
    uint8_t u8_parameter = u8_index % SWITCH_CYCLE_PARAM_COUNT;
    switch_cycle_t *p_x_cycle = &g_x_switch_cycle[u8_channel];
    uint32_t u32_value;
    uint32_t u32_min;
    uint32_t u32_max;
    char str_prompt[48];

    (void) c_key;

    if (u8_parameter == SWITCH_CYCLE_PARAM_REPEAT)
    {
        u32_value = p_x_cycle->u32_repeat_count;
        u32_min = 0;
        u32_max = 0xFFFFFFFFUL;
        snprintf(str_prompt, sizeof(str_prompt),
                 "Switch %s repeat count, 0 = infinite",
                 pc_switch_out_name(u8_channel));
    }
    else
    {
        u32_value = (u8_parameter == SWITCH_CYCLE_PARAM_ON)
                    ? p_x_cycle->u32_on_time_us
                    : p_x_cycle->u32_off_time_us;
        u32_min = SWITCH_CYCLE_TIME_MIN_US;
        u32_max = SWITCH_CYCLE_TIME_MAX_US;
        snprintf(str_prompt, sizeof(str_prompt), "Switch %s %s time, uS",
                 pc_switch_out_name(u8_channel),
                 (u8_parameter == SWITCH_CYCLE_PARAM_ON) ? "on" : "off");
    }

    if (! u8_debug_entry_u32(str_prompt, u32_min, u32_max, &u32_value))
    {
        return;
    }

    /* Settings are read-only to the ISR, so this plain aligned 32-bit store is
     * atomic on M0+ and needs no masking; a running cycle picks the new value
     * up at its next phase boundary. */
    switch (u8_parameter)
    {
        case SWITCH_CYCLE_PARAM_REPEAT: p_x_cycle->u32_repeat_count = u32_value; break;
        case SWITCH_CYCLE_PARAM_ON:     p_x_cycle->u32_on_time_us   = u32_value; break;
        default:                        p_x_cycle->u32_off_time_us  = u32_value; break;
    }

    v_switch_cycle_nvm_save(u8_channel, u8_parameter);

    printf("Switch %s %s now %lu\r\n",
           pc_switch_out_name(u8_channel),
           apc_cycle_param_label[u8_parameter],
           (unsigned long) u32_value);

    if ((u8_parameter != SWITCH_CYCLE_PARAM_REPEAT)
        && (u32_value < 3000UL))
    {
        /* Not an error -- the tester may be pointed at other hardware -- but the
         * DUT's 10 nF filter needs roughly 1.1 mS to register a press and 2.3 mS
         * to read a valid release. */
        printf("  Note: below the DUT's RC response floor (~1100 uS on, ~2340 uS off)\r\n");
    }
}

static void v_cycle_key_startstop(char c_key, uint8_t u8_index)
{
    (void) c_key;

    if (u8_switch_cycle_running(u8_index))
    {
        printf("Switch %s cycling stopped after %lu cycles\r\n",
               pc_switch_out_name(u8_index),
               (unsigned long) g_x_switch_cycle[u8_index].u32_cycles_done);
        v_switch_cycle_stop(u8_index);
        return;
    }

    v_switch_cycle_start(u8_index);

    if (! u8_switch_cycle_running(u8_index))
    {
        printf("Switch %s not started - check on/off times (%lu..%lu uS)\r\n",
               pc_switch_out_name(u8_index),
               (unsigned long) SWITCH_CYCLE_TIME_MIN_US,
               (unsigned long) SWITCH_CYCLE_TIME_MAX_US);
        return;
    }

    printf("Switch %s cycling started\r\n", pc_switch_out_name(u8_index));
}

static void v_cycle_stop_all(void)
{
    v_switch_cycle_stop_all();
    printf("All switch cycling stopped, outputs off\r\n");
}

/* ---------------------------------------------------------------------------
 * System
 * ------------------------------------------------------------------------- */

/*
 * Dump the NVM pool: header (signature, CRC, write count) followed by every
 * object with its ID, size and raw bytes. This is the tool for telling a
 * corrupt pool apart from a mis-used API -- the stored IDs and sizes say
 * directly whether objects landed where the enum says they should.
 */
static void v_debug_nvm_dump(void)
{
    x_nvm_list(&g_x_nvm_param);
}

/*
 * Erase the NVM pool and restart, so every parameter is recreated from its
 * compiled-in default. This is the documented recovery from a corrupt pool
 * (see the notes at the top of nvmparams.h) and is destructive, hence the
 * confirmation.
 */
static void v_debug_nvm_erase(void)
{
    int i_key;

    printf("Erase NVM pool and reset? All saved parameters revert to defaults.\r\n"
           "Press 'Y' to confirm, any other key to cancel: ");

    i_key = i_getchar_blocking();
    v_newline();

    if ((i_key != 'Y') && (i_key != 'y'))
    {
        printf("Cancelled - NVM pool untouched\r\n");
        return;
    }

    printf("Erasing NVM pool...\r\n");

    v_switch_cycle_stop_all();
    v_switch_out_all_off();

    memset(g_x_nvm_param.p_v_data, 0xFF, g_x_nvm_param.u32_size);
    x_nvm_write(&g_x_nvm_param);

    HAL_Delay(250);
    NVIC_SystemReset();
}

static void v_debug_soft_reset(void)
{
    printf("Soft reset in 250 mS...\r\n");

    /* Quiesce first. TIM2 and its interrupt stay live through HAL_Delay(), so a
     * running cycle would otherwise keep driving the DUT right up to the reset;
     * and once the pins go high-Z at reset the node holds its last level on line
     * capacitance for far longer than the reset-to-init window -- which only
     * helps if that level is LOW. */
    v_switch_cycle_stop_all();
    v_switch_out_all_off();

    HAL_Delay(250);
    NVIC_SystemReset();
}

/*============================================================================
 * MENU DEFINITION
 *==========================================================================*/

/*
 * Switch-output submenu. The key-list entries are invisible to the menu help
 * printer (MENU_ITEM_KEY_LIST_FUNCTION never prints), so the key map has to be
 * spelled out in the fixed help text below.
 */
static const menu_item_t x_switch_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- Switch outputs (SWITCH_A..D, active high) ---\r\n"
                "[a b c d]  Force OFF          A..D\r\n"
                "[A B C D]  Force ON           A..D\r\n"
                "[1 2 3 4]  Pulse ON for [w]   A..D\r\n"
                "[! @ # $]  Toggle             A..D\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key_list = "abcd",
        .key_list_function = v_switch_key_off
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key_list = "ABCD",
        .key_list_function = v_switch_key_on
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key_list = "1234",
        .key_list_function = v_switch_key_pulse
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key_list = "!@#$",
        .key_list_function = v_switch_key_toggle
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'w',
        .text = "Set pulse width",
        .function = v_switch_set_pulse_width
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 's',
        .text = "Show switch output state",
        .function = v_switch_show_state
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '0',
        .text = "All switch outputs OFF",
        .function = v_switch_all_off
    },
    {
        /* ESC is the canonical return-from-submenu key throughout. The framework
         * already renders 0x1B as "ESC" via p_c_char_to_str(). */
        .item_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .key = 0x1B,
        .text = "Return to main menu"
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

/*
 * Cycling submenu. Every parameter line is drawn by v_cycle_help_text() so it
 * can show its live value; the keys themselves are bound by the two key-list
 * entries, which print nothing of their own.
 */
static const menu_item_t x_cycle_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- Switch cycling (SWITCH_A..D) ---"
    },
    {
        .item_type = MENU_ITEM_HELP_TEXT_VARIABLE,
        .key = 0,
        .text = NULL,
        .help_text_function = v_cycle_help_text
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key_list = CYCLE_PARAM_KEYS,
        .key_list_function = v_cycle_key_param
    },
    {
        .item_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .key_list = CYCLE_RUN_KEYS,
        .key_list_function = v_cycle_key_startstop
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '0',
        .text = "Stop all cycling",
        .function = v_cycle_stop_all
    },
    {
        .item_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .key = 0x1B,
        .text = "Return to main menu"
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

static const menu_item_t x_debug_top_menu[] =
{
    {
        .item_type = MENU_ITEM_HELP_TEXT_FIXED,
        .key = 0,
        .text = "\r\n--- " PRODUCT_NAME " v" FIRMWARE_VERSION " Main Menu ---\r\n"
    },
    {
        .item_type = MENU_ITEM_HELP,
        .key = '?',
        .text = NULL
    },
    {
        /* Bare <Enter> re-prints the menu without logging an unknown key. */
        .item_type = MENU_ITEM_HELP_HIDDEN,
        .key = '\r',
        .text = NULL
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = '!',
        .text = "Soft reset (system)",
        .function = v_debug_soft_reset
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'n',
        .text = "NVM pool dump",
        .function = v_debug_nvm_dump
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'N',
        .text = "NVM pool ERASE + reset (defaults)",
        .function = v_debug_nvm_erase
    },
    {
        .item_type = MENU_ITEM_CALL_MENU,
        .key = 's',
        .text = "Switch outputs (SWITCH_A..D)",
        .menu = x_switch_menu
    },
    {
        .item_type = MENU_ITEM_CALL_MENU,
        .key = 'c',
        .text = "Switch cycling (SWITCH_A..D)",
        .menu = x_cycle_menu
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'a',
        .text = "Automation console (human-driven)",
        .function = v_debug_automation_console
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'W',
        .text = "RTC wake-up timer sleep test",
        .function = v_debug_wakeup_sleep_test
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'q',
        .text = "Quick test function 1",
        .function = v_debug_quick_test_1
    },
    {
        .item_type = MENU_ITEM_FUNCTION,
        .key = 'Q',
        .text = "Quick test function 2",
        .function = v_debug_quick_test_2
    },
    {
        /* Hidden: ESC at the top level has nowhere to return to, so acknowledge
         * we are already here rather than logging it as an unknown key. */
        .item_type = MENU_ITEM_FUNCTION,
        .key = 0x1B,
        .text = NULL,
        .function = v_debug_at_main_menu
    },
    {
        .item_type = MENU_ITEM_END_OF_LIST,
    }
};

/*============================================================================
 * MENU CONTROL + SERVICE
 *==========================================================================*/

static void *x_debug_menu_stack[4];
static menu_control_t x_debug_menu_control;
#define DEBUG_MENU_STACK_DEPTH  (sizeof(x_debug_menu_stack) / sizeof(void *))

void v_debug_menu_init(void)
{
    v_menu_init(&x_debug_menu_control,
                x_debug_top_menu,
                &x_debug_menu_stack[0],
                DEBUG_MENU_STACK_DEPTH);

    /* key == 0xFF requests the initial help printout. */
    v_menu_exec(&x_debug_menu_control, 0xFF);
}

static void v_debug_menu_exec(char c_key)
{
    if (x_debug_menu_control.menu_stack == NULL)
    {
        v_debug_menu_init();
    }
    v_menu_exec(&x_debug_menu_control, c_key);
}

void v_debug_menu_service(void)
{
    static uint8_t u8_reentry_lock;
    int i_key;
    char str_key[4];

    if (u8_reentry_lock)
    {
        return;
    }
    u8_reentry_lock = 1;

    do
    {
        i_key = getchar();
        if (i_key < 0)
        {
            break;              /* no input pending */
        }

        /* Automation-console entry. Intercepted before the echo, so the
         * sentinel never appears on the wire and never reaches the menu
         * dispatcher. SCRIPT mode: only a machine sends a non-typeable 0xDA.
         *
         * Note this runs with u8_reentry_lock still held, which is what stops
         * the nested v_debug_menu_service() inside v_app_polling_task() from
         * stealing the console's input AND dispatching it as menu keys. That
         * guarantee is load-bearing, not tidiness. */
        if ((uint8_t) i_key == ACON_ENTER)
        {
            v_automation_console_run(ACON_MODE_SCRIPT);
            continue;
        }

        p_c_char_to_str((char) i_key, str_key);
        printf("Cmd [%s]\r\n", str_key);
        v_debug_menu_exec((char) i_key);
    }
    while (1);

    u8_reentry_lock = 0;
}

void v_debug_delay(uint32_t u32_delay)
{
    /* Cooperative delay: keep the console menu responsive while waiting. */
    uint32_t u32_start = SYSTEM_TICK();
    while (ELAPSED_TIME(u32_start) < u32_delay)
    {
        v_debug_menu_service();
    }
}
