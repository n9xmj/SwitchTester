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
#include "nvmparams.h"               /* NVM pool erase diagnostics */
#include "nvm_list.h"                /* x_nvm_list() pool dump -- not part of the module */
#include "globals.h"                 /* g_x_nvm_param */
#include "automation_console.h"      /* Host/script command interface */
#include "app_events.h"              /* Event queue + record layout (human sink) */

/*============================================================================
 * PRIVATE PROTOTYPES
 *==========================================================================*/

static void v_debug_wakeup_sleep_test(void);
static void v_debug_automation_console(void);
static void v_debug_quick_test_1(void);
static void v_debug_quick_test_2(void);
static void v_debug_spi_pin_probe(void);   /* TEMP: SPI flash wiring probe */
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
static void v_event_help_text(void);
static void v_event_key_toggle(char c_key, uint8_t u8_index);
static void v_event_dump(void);
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

/* ---------------------------------------------------------------------------
 * SPI pin probe monitor  (TEMPORARY -- MX25R80 bring-up, removal-slated).
 *
 * Reconfigures the SPI-flash lines to plain GPIO, toggles MOSI/SCK/NCS together
 * at ~1 Hz, and after each edge reads every line back via IDR -- the real pad
 * level, not what the STM commanded -- plus MISO (input, pull-down) and
 * TEST_INPUT (PC3). Lets you hold probe wires on the board and watch levels
 * track hands-free until ESC. Throw-away: not tidy, not re-entrant.
 * ------------------------------------------------------------------------- */
static void v_debug_spi_pin_probe(void)
{
    GPIO_InitTypeDef x_gpio = {0};
    uint8_t u8_level = 1u;

    printf("\r\n--- SPI pin probe: MOSI/SCK/NCS out, MISO in (pull-down), TEST in.\r\n");
    printf("    Toggling ~1 Hz; IDR readback each edge. ESC to quit.\r\n");

    /* MOSI (PC12) and SCK (PB3) -> GPIO push-pull output; NCS is already one. */
    x_gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    x_gpio.Pull  = GPIO_NOPULL;
    x_gpio.Speed = GPIO_SPEED_FREQ_LOW;
    x_gpio.Pin   = SPIFLASH_MOSI_Pin;
    HAL_GPIO_Init(SPIFLASH_MOSI_GPIO_Port, &x_gpio);
    x_gpio.Pin   = SPIFLASH_SCK_Pin;
    HAL_GPIO_Init(SPIFLASH_SCK_GPIO_Port, &x_gpio);

    /* MISO (PB4) -> GPIO input, pull-down so an undriven line reads 0. */
    x_gpio.Mode  = GPIO_MODE_INPUT;
    x_gpio.Pull  = GPIO_PULLDOWN;
    x_gpio.Pin   = SPIFLASH_MISO_Pin;
    HAL_GPIO_Init(SPIFLASH_MISO_GPIO_Port, &x_gpio);

    for (;;)
    {
        if (getchar() == 0x1B) { break; }       /* ESC -- getchar is non-blocking here */

        HAL_GPIO_WritePin(SPIFLASH_MOSI_GPIO_Port, SPIFLASH_MOSI_Pin,
                          (u8_level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(SPIFLASH_SCK_GPIO_Port, SPIFLASH_SCK_Pin,
                          (u8_level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin,
                          (u8_level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);

        HAL_Delay(10u);     /* let levels settle before the readback */

        printf("OutLvl:%u Test:%u SCK:%u MOSI:%u MISO:%u NCS:%u\r\n",
               (unsigned) u8_level,
               (unsigned) HAL_GPIO_ReadPin(TEST_INPUT_GPIO_Port,    TEST_INPUT_Pin),
               (unsigned) HAL_GPIO_ReadPin(SPIFLASH_SCK_GPIO_Port,  SPIFLASH_SCK_Pin),
               (unsigned) HAL_GPIO_ReadPin(SPIFLASH_MOSI_GPIO_Port, SPIFLASH_MOSI_Pin),
               (unsigned) HAL_GPIO_ReadPin(SPIFLASH_MISO_GPIO_Port, SPIFLASH_MISO_Pin),
               (unsigned) HAL_GPIO_ReadPin(SPIFLASH_NCS_GPIO_Port,  SPIFLASH_NCS_Pin));

        HAL_Delay(500u);    /* ~500 ms per level -> ~1 Hz square wave */
        u8_level ^= 1u;
    }

    /* Restore SPI alternate functions (MOSI=AF4, SCK/MISO=AF9); park NCS high. */
    x_gpio.Mode      = GPIO_MODE_AF_PP;
    x_gpio.Pull      = GPIO_NOPULL;
    x_gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    x_gpio.Alternate = GPIO_AF4_SPI3;
    x_gpio.Pin       = SPIFLASH_MOSI_Pin;
    HAL_GPIO_Init(SPIFLASH_MOSI_GPIO_Port, &x_gpio);
    x_gpio.Alternate = GPIO_AF9_SPI3;
    x_gpio.Pin       = SPIFLASH_SCK_Pin;
    HAL_GPIO_Init(SPIFLASH_SCK_GPIO_Port, &x_gpio);
    x_gpio.Pin       = SPIFLASH_MISO_Pin;
    HAL_GPIO_Init(SPIFLASH_MISO_GPIO_Port, &x_gpio);

    HAL_GPIO_WritePin(SPIFLASH_NCS_GPIO_Port, SPIFLASH_NCS_Pin, GPIO_PIN_SET);

    printf("--- SPI pin probe ended.\r\n");
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

/* ---------------------------------------------------------------------------
 * Event-logging configuration submenu.
 *
 * Every line is a toggle on one bit of g_x_event_control, the production mask
 * described in app_events.h. The mask gates events at PRODUCTION -- a bit that
 * is off means those records never enter the queue at all, so this menu is the
 * cheapest control in the system, not a display filter.
 *
 * ONE table drives three consumers -- the menu listing, the toggle handler and
 * the register dump -- so a bit cannot be renamed in one view and not another.
 * It is in ascending bit order, which groups the channels the way the register
 * does; the dump walks it backwards to get the descending order a datasheet
 * would print.
 *
 * EVENT_TOGGLE_KEYS is positionally tied to that table: the key-list handler
 * receives the index of the key that matched and uses it as the table index, so
 * the two must stay the same length and the same order. Keeping the keys in one
 * string rather than a per-row member is what makes that relationship checkable
 * at a glance -- and the _Static_assert below makes it checkable at build time.
 *
 * Key choice follows the switch-output submenu: lowercase/uppercase/shifted
 * splits one concept three ways across the same four channel positions.
 * ------------------------------------------------------------------------- */

#define EVENT_TOGGLE_KEYS       "ABCD" "abcd" "!@#$" "y" "g"

typedef struct
{
    uint8_t     u8_bit;
    const char *pc_label;
}
event_mask_row_t;

static const event_mask_row_t x_event_mask_row[] =
{
    {  0, "Switch A auto events"         },
    {  1, "Switch B auto events"         },
    {  2, "Switch C auto events"         },
    {  3, "Switch D auto events"         },
    {  4, "Switch A manual events"       },
    {  5, "Switch B manual events"       },
    {  6, "Switch C manual events"       },
    {  7, "Switch D manual events"       },
    {  8, "Sense A events"               },
    {  9, "Sense B events"               },
    { 10, "Sense C events"               },
    { 11, "Sense D events"               },
    { 30, "Switch cycle-complete events" },
    { 31, "GLOBAL event enable"          },
};

#define EVENT_MASK_ROW_COUNT    (sizeof(x_event_mask_row) / sizeof(x_event_mask_row[0]))

/* sizeof() on the string literal counts its NUL, hence the -1. */
_Static_assert(sizeof(EVENT_TOGGLE_KEYS) - 1u == EVENT_MASK_ROW_COUNT,
               "EVENT_TOGGLE_KEYS must have exactly one key per x_event_mask_row "
               "entry, in the same order -- the key-list index IS the row index");

/* Reserved field: bits 12..29, the _u32_unused member of event_control_t. */
#define EVENT_RESERVED_MASK     0x3FFFF000UL
#define EVENT_RESERVED_SHIFT    12U

/* Column width shared by the menu listing and the dump, so the two line up
 * with each other and not just internally. */
#define EVENT_LABEL_WIDTH       "32"

static const char * pc_event_state(uint32_t u32_mask, uint8_t u8_bit)
{
    return ((u32_mask >> u8_bit) & 1UL) ? "Enabled" : "Disabled";
}

static void v_event_help_text(void)
{
    uint32_t u32_mask = g_x_event_control.u32_all;
    uint8_t  u8_i;

    v_newline();

    for (u8_i = 0; u8_i < EVENT_MASK_ROW_COUNT; u8_i++)
    {
        const event_mask_row_t *p_x_row = &x_event_mask_row[u8_i];

        /* A blank line between the groups the register itself defines: the
         * four auto bits, the four manual bits, the four sense bits, then the
         * two globals. */
        if ((u8_i != 0) && ((u8_i % 4u) == 0u))
        {
            v_newline();
        }

        printf("[%c] %-" EVENT_LABEL_WIDTH "s: %s\r\n",
               EVENT_TOGGLE_KEYS[u8_i],
               p_x_row->pc_label,
               pc_event_state(u32_mask, p_x_row->u8_bit));
    }

    v_newline();
}

static void v_event_key_toggle(char c_key, uint8_t u8_index)
{
    const event_mask_row_t *p_x_row;
    bool b_saved;

    (void) c_key;

    if (u8_index >= EVENT_MASK_ROW_COUNT)
    {
        return;                     /* cannot happen; the assert above binds */
    }

    p_x_row = &x_event_mask_row[u8_index];

    /* Read-modify-write of a whole 32-bit word. ISRs only ever READ this
     * register, and an aligned 32-bit access is atomic on Cortex-M0+, so the
     * worst an interrupt landing mid-sequence sees is the before or the after
     * value -- never a half-applied one. No critical section needed. */
    g_x_event_control.u32_all ^= (1UL << p_x_row->u8_bit);

    /* Arming is sticky across reset (plan S4). This updates the pool's RAM
     * shadow; the deferred auto-commit in v_timer_update() does the flash
     * write, which is what stops a run of toggles becoming a run of erases.
     *
     * The menu persists unconditionally, unlike the acon path where it is an
     * explicit per-command opt-in: a human toggling a bit means it, and does so
     * a handful of times rather than dozens per test run. */
    b_saved = b_event_control_nvm_save();

    printf("%-" EVENT_LABEL_WIDTH "s: %s%s\r\n",
           p_x_row->pc_label,
           pc_event_state(g_x_event_control.u32_all, p_x_row->u8_bit),
           b_saved ? "" : "   *** NVM save FAILED, not sticky across reset ***");
}

/*
 * Register dump: one line per defined bit, highest first, then the raw value.
 *
 * The reserved field gets one row rather than eighteen -- printing 18 lines of
 * "(reserved) 0" would bury the 14 rows that carry information. Its value is
 * shown, not assumed, because a non-zero there means something wrote the
 * register with a stale or foreign layout and that is worth seeing.
 */
static void v_event_dump(void)
{
    uint32_t u32_mask = g_x_event_control.u32_all;
    uint8_t  u8_i;

    printf("\r\nEvent enable register bitmap\r\n\r\n"
           "  %5s  %-10s  %-" EVENT_LABEL_WIDTH "s %s\r\n"
           "  %5s  %-10s  %-" EVENT_LABEL_WIDTH "s %s\r\n",
           "Bit", "Mask", "Field", "State",
           "-----", "----------", "--------------------------------", "--------");

    for (u8_i = EVENT_MASK_ROW_COUNT; u8_i > 0u; u8_i--)
    {
        const event_mask_row_t *p_x_row = &x_event_mask_row[u8_i - 1u];

        if (p_x_row->u8_bit == (EVENT_RESERVED_SHIFT - 1u))
        {
            printf("  %5s  0x%08lX  %-" EVENT_LABEL_WIDTH "s %lu\r\n",
                   "29-12", (unsigned long) EVENT_RESERVED_MASK, "(reserved)",
                   (unsigned long) ((u32_mask & EVENT_RESERVED_MASK)
                                    >> EVENT_RESERVED_SHIFT));
        }

        printf("  %5u  0x%08lX  %-" EVENT_LABEL_WIDTH "s %s\r\n",
               (unsigned) p_x_row->u8_bit,
               (unsigned long) (1UL << p_x_row->u8_bit),
               p_x_row->pc_label,
               pc_event_state(u32_mask, p_x_row->u8_bit));
    }

    /* Two 16-bit halves rather than one 32-bit field: at a glance it separates
     * the two global bits in the high half from the per-source bits in the low
     * half, which is how the register is actually reasoned about. */
    printf("\r\nEvent enable register    : %04lX %04lX\r\n",
           (unsigned long) (u32_mask >> 16),
           (unsigned long) (u32_mask & 0xFFFFUL));
}

/*
 * Switch-output submenu. The key-list entries are invisible to the menu help
 * printer (MENU_ITEM_KEY_LIST_FUNCTION never prints), so the key map has to be
 * spelled out in the fixed help text below.
 */
static const menu_item_t x_switch_menu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- Switch outputs (SWITCH_A..D, active high) ---\r\n"
                "[a b c d]  Force OFF          A..D\r\n"
                "[A B C D]  Force ON           A..D\r\n"
                "[1 2 3 4]  Pulse ON for [w]   A..D\r\n"
                "[! @ # $]  Toggle             A..D\r\n"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = "abcd",
        .pfn_key_list_function = v_switch_key_off
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = "ABCD",
        .pfn_key_list_function = v_switch_key_on
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = "1234",
        .pfn_key_list_function = v_switch_key_pulse
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = "!@#$",
        .pfn_key_list_function = v_switch_key_toggle
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'w',
        .p_c_text = "Set pulse width",
        .pfn_function = v_switch_set_pulse_width
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 's',
        .p_c_text = "Show switch output state",
        .pfn_function = v_switch_show_state
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '0',
        .p_c_text = "All switch outputs OFF",
        .pfn_function = v_switch_all_off
    },
    {
        /* ESC is the canonical return-from-submenu key throughout. The framework
         * already renders 0x1B as "ESC" via pc_char_to_str(). */
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = "Return to previous menu"
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

/*
 * Event-logging submenu. Like the cycling menu, every toggle line is drawn by
 * v_event_help_text() so it can show its live state; the keys are bound by the
 * single key-list entry, which prints nothing of its own.
 */
static const menu_item_t x_event_menu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- Event logging configuration (production mask) ---"
    },
    {
        .x_type = MENU_ITEM_HELP_TEXT_VARIABLE,
        .c_key = 0,
        .p_c_text = NULL,
        .pfn_help_text_function = v_event_help_text
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = EVENT_TOGGLE_KEYS,
        .pfn_key_list_function = v_event_key_toggle
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'p',
        .p_c_text = "Dump the event enable register",
        .pfn_function = v_event_dump
    },
    {
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = "Return to previous menu"
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
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
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- Switch cycling (SWITCH_A..D) ---"
    },
    {
        .x_type = MENU_ITEM_HELP_TEXT_VARIABLE,
        .c_key = 0,
        .p_c_text = NULL,
        .pfn_help_text_function = v_cycle_help_text
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = CYCLE_PARAM_KEYS,
        .pfn_key_list_function = v_cycle_key_param
    },
    {
        .x_type = MENU_ITEM_KEY_LIST_FUNCTION,
        .p_c_key_list = CYCLE_RUN_KEYS,
        .pfn_key_list_function = v_cycle_key_startstop
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '0',
        .p_c_text = "Stop all cycling",
        .pfn_function = v_cycle_stop_all
    },
    {
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = "Return to previous menu"
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

static const menu_item_t x_debug_top_menu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- " PRODUCT_NAME " v" FIRMWARE_VERSION " Main Menu ---\r\n"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        /* Bare <Enter> re-prints the menu without logging an unknown key. */
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '!',
        .p_c_text = "Soft reset (system)",
        .pfn_function = v_debug_soft_reset
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'n',
        .p_c_text = "NVM pool dump",
        .pfn_function = v_debug_nvm_dump
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'N',
        .p_c_text = "NVM pool ERASE + reset (defaults)",
        .pfn_function = v_debug_nvm_erase
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 's',
        .p_c_text = "Switch outputs (SWITCH_A..D)",
        .p_x_menu = x_switch_menu
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'c',
        .p_c_text = "Switch cycling (SWITCH_A..D)",
        .p_x_menu = x_cycle_menu
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'e',
        .p_c_text = "Event logging configuration",
        .p_x_menu = x_event_menu
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'a',
        .p_c_text = "Automation console (human-driven)",
        .pfn_function = v_debug_automation_console
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'W',
        .p_c_text = "RTC wake-up timer sleep test",
        .pfn_function = v_debug_wakeup_sleep_test
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'q',
        .p_c_text = "Quick test function 1",
        .pfn_function = v_debug_quick_test_1
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'Q',
        .p_c_text = "Quick test function 2",
        .pfn_function = v_debug_quick_test_2
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'p',
        .p_c_text = "SPI pin probe monitor (temp)",
        .pfn_function = v_debug_spi_pin_probe
    },
    {
        /* Hidden: ESC at the top level has nowhere to pop. menusystem replies
         * "[At top-level menu]" on an empty-stack return, so a spammed ESC
         * confirms you are fully backed out -- no custom function needed. */
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
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
    if (x_debug_menu_control.pap_x_menu == NULL)
    {
        v_debug_menu_init();
    }
    v_menu_exec(&x_debug_menu_control, c_key);
}

/*============================================================================
 * EVENT LOG -- the human-side sink
 *
 * The other half of the XOR sink model: acon active -> the host drains with the
 * D command; menu active -> this drains. Which one runs is decided structurally
 * rather than by a mode flag, and the mechanism is subtler than it looks --
 * see the comment at the call site in v_debug_menu_service().
 *
 * Draining is UNCONDITIONAL and emission is separate. Whatever the logging tier
 * or the class table say, every record that comes out is consumed, because the
 * alternative is a ring that fills up and starts charging the drop counter for
 * events nobody asked to see.
 *==========================================================================*/

/* Records drained per service pass. An unbounded drain would hold the polling
 * loop for the length of whatever backlog it found -- and there is a real way
 * to accumulate one: an acon session that arms the mask, runs a soak and exits
 * without issuing D leaves up to ~512 records behind for the menu to inherit.
 * A small batch trickles that out over successive passes instead, and the pass
 * rate is orders of magnitude above any event rate the switch path can produce.
 */
#define EVENT_LOG_BATCH                 8

/*
 * Class -> display label. NULL for anything this table does not know, which is
 * the "no log emission for classes not defined yet" rule: an unrecognised
 * record is still consumed, just not printed.
 */
static const char * pc_event_class_name(uint16_t u16_class)
{
    switch ((event_class_t) u16_class)
    {
        case EVENT_CLASS_SWITCH_MANUAL:         return "SW-Man";
        case EVENT_CLASS_SWITCH_AUTO:           return "SW-Auto";
        case EVENT_CLASS_SWITCH_CYCLE_COMPLETE: return "SW-Done";
        case EVENT_CLASS_SENSE_LEVEL:           return "Sense";
        default:                                return NULL;
    }
}

static void v_event_log_drain(void)
{
    uint8_t u8_i;

    for (u8_i = 0; u8_i < EVENT_LOG_BATCH; u8_i++)
    {
        switch_event_data_t  x_data;
        event_queue_record_t x_record =
        {
            .u16_buf_size = (uint16_t) sizeof(x_data),
            .pv_data      = &x_data
        };
        event_queue_status_t x_status;
        const char          *pc_name;

        x_status = x_event_queue_get(&g_x_event_queue, &x_record);

        /* EQ_STATUS_EMPTY ends the batch; so does NOT_INIT, which is what a
         * v_debug_delay() spin sees if one ever runs before the queue exists.
         * TRUNCATED is not a failure -- the record came out, only an oversized
         * payload was clipped, and every payload here is one size. */
        if ((x_status != EQ_OK) && (x_status != EQ_STATUS_TRUNCATED))
        {
            break;
        }

        pc_name = pc_event_class_name(x_record.u16_id);
        if (pc_name == NULL)
        {
            continue;               /* consumed, deliberately not printed */
        }

        /* %lu + an explicit (unsigned long) cast on the 32-bit members: it is
         * correct whether the toolchain's uint32_t is unsigned int or unsigned
         * long, which %u is not. The 16-bit members promote to int, so %X with
         * (unsigned) is the right pair for those. */
        LOGCT(LOG_EVENT, "%04X %-8s ID:%c-%02X Tick:%-8lu TIM:%-8lu",
              (unsigned) x_record.u16_id,
              pc_name,
              pc_switch_out_name(x_data.u8_channel)[0],
              (unsigned) x_data.u16_state,
              (unsigned long) x_data.u32_tick,
              (unsigned long) x_data.u32_tim_count);
    }
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

        pc_char_to_str((char) i_key, str_key);
        printf("Cmd [%s]\r\n", str_key);
        v_debug_menu_exec((char) i_key);
    }
    while (1);

    /* Human-side event drain. Inside the re-entry lock ON PURPOSE, and this is
     * load-bearing: ACON_PUMP() calls v_app_polling_task() on every spin of the
     * console's line reader, so an acon session re-enters this function
     * thousands of times per second. The lock turns those calls around at the
     * top, which is the only thing keeping the menu sink from eating the very
     * records the host's D command came to collect.
     *
     * Placed after the input loop rather than before it so that a menu key that
     * drives a switch gets its events printed in the same pass that handled the
     * key, not the next one. */
    v_event_log_drain();

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
