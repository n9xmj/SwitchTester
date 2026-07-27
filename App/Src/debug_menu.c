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

#include "device_config.h"          /* stdint/stdio, platform.h (SYSTEM_TICK), main.h */
#include "menusystem.h"
#include "debug_menu.h"
#include "utils.h"                   /* RTC wakeup + hour-time helpers under test */
#include "rtc.h"                     /* hrtc, for post-STOP HAL_RTC_WaitForSynchro */

/*============================================================================
 * PRIVATE PROTOTYPES
 *==========================================================================*/

static void v_debug_wakeup_sleep_test(void);
static void v_debug_quick_test_1(void);
static void v_debug_quick_test_2(void);
static void v_debug_menu_exec(char c_key);

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

static void v_debug_quick_test_1(void)
{
    printf("Quick test function 1 (stub)\r\n");
}

static void v_debug_quick_test_2(void)
{
    printf("Quick test function 2 (stub)\r\n");
}

/*============================================================================
 * MENU DEFINITION
 *==========================================================================*/

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
