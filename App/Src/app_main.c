/******************************************************************************
 * Application MAIN
 *
 * Generic, non-application-specific skeleton:
 *   - startup banner
 *   - NVM parameter pool (nvmparams)
 *   - job queue + dispatcher (v_process_next_job)
 *   - 10 ms periodic-interrupt service
 *   - console debug menu
 *
 * Add application code on top of this base; nothing here is tied to a
 * particular product.
 ******************************************************************************/

/*============================================================================
 * INCLUDES
 *==========================================================================*/

#include "device_config.h"          /* stdint/stdio, main.h, platform.h, globals.h */
#include "tim.h"                     /* PERIODIC_INT_TIMER_HANDLE (htim14) */
#include "utils.h"
#include "jobs.h"
#include "nvmparams.h"
#include "debug_menu.h"
#include "switch_out.h"
#include "uart_stream.h"
#include "stdio_retarget.h"

/*============================================================================
 * STARTUP BANNER
 *==========================================================================*/

void v_print_startup_banner(void)
{
    if (x_reset_source.x_reset_type == RESET_TYPE_UNKNOWN)
    {
        x_get_reset_source();
    }

    v_newline();
    v_repeat_char('*', -64);
    RPRINTF("Product             : " PRODUCT_NAME "\r\n"
            "Firmware version    : " FIRMWARE_VERSION "\r\n"
            "Platform/board rev  : " PLATFORM_NAME "\r\n"
            "Build config        : " BUILD_CONFIG "\r\n"
            "Build date          : " __DATE__ "\r\n"
            "Build time          : " __TIME__ "\r\n"
            "\r\n"
            "Reset source        : [%02X] #%u-%s\r\n",
            x_reset_source.u8_reset_flags,
            x_reset_source.x_reset_type,
            pc_reset_source_description(x_reset_source.x_reset_type)
           );
    v_repeat_char('*', -64);
    v_newline();
}

/*============================================================================
 * NVM PARAMETER POOL
 *==========================================================================*/

uint32_t u32_test_param_1;

void v_param_init(void)
{
    x_nvm_pool_init(&g_x_nvm_param, NVM_DEVICE_MCUFLASH, NULL, 0, "PARAMS");

    /* Example persistent parameter. Replace / extend for real use. */
    u32_test_param_1 = 0xDEAD;
    x_nvm_create(&g_x_nvm_param, NVM_PARAM_TEST_1,
                 sizeof(u32_test_param_1),
                 &u32_test_param_1);
    x_nvm_get(&g_x_nvm_param, NVM_PARAM_TEST_1, &u32_test_param_1);

    /* Switch-output parameters. Must sit between the pool init above and the
     * commit below, so a virgin pool creates every object in one flash write. */
    v_switch_out_nvm_init();

    x_nvm_commit(&g_x_nvm_param);
}

/*============================================================================
 * HARDWARE / SUBSYSTEM INIT
 *==========================================================================*/

/*
 * Bind the console UART to uart_stream and move stdio onto it.
 *
 * Until this runs, stdio uses a blocking HAL fallback, so anything printed
 * earlier (the start-up banner) still reaches the terminal. After it, HAL is
 * locked out of USART2 entirely -- the handle is marked busy, and the vector in
 * stm32g0xx_it.c routes to uart_stream.
 *
 * Ring buffers are allocated here (NULL storage pointers) rather than declared
 * statically: this is a bind-time, application-lifetime allocation, not the
 * repeated alloc/free pattern that fragments a heap.
 */
static void v_console_stream_init(void)
{
    uart_stream_h_t h_console;

    h_console = x_uart_stream_init(&DEBUG_UART_HANDLE,
                                   DEV_CONFIG_CONSOLE_RX_BUF_SIZE, NULL,
                                   DEV_CONFIG_CONSOLE_TX_BUF_SIZE, NULL);

    if (h_console == UART_STREAM_HANDLE_INVALID)
    {
        /* Stay on the HAL fallback; the console keeps working, just polled. */
        printf("WARNING: console uart_stream bind failed - stdio stays on HAL\r\n");
        return;
    }

    v_stdio_retarget_attach_stream(h_console);
}

void v_hardware_init(void)
{
    v_param_init();
    v_console_stream_init();
    v_switch_out_init();
    HAL_TIM_Base_Start_IT(&PERIODIC_INT_TIMER_HANDLE);
}

/*============================================================================
 * PERIODIC (1 ms) INTERRUPT SERVICE
 *==========================================================================*/

#define PERIODIC_TEST_INTERVAL_MS       1000

static void v_periodic_int_test(void)
{
    static uint16_t u16_count;

    u16_count += PERIODIC_TIMER_INTERVAL_MS;
    if (u16_count >= PERIODIC_TEST_INTERVAL_MS)
    {
        u16_count -= PERIODIC_TEST_INTERVAL_MS;
        v_job_add(NULL, JOB_PERIODIC);
    }
}

static void v_timer_update(void)
{
    /* NVM auto-commit check */
    if (g_x_nvm_param.u8_need_commit
        && (g_x_nvm_param.u16_commit_timer < DEV_CONFIG_NVM_COMMIT_DELAY_MS))
    {
        g_x_nvm_param.u16_commit_timer += PERIODIC_TIMER_INTERVAL_MS;
        if (g_x_nvm_param.u16_commit_timer >= DEV_CONFIG_NVM_COMMIT_DELAY_MS)
        {
            v_job_add(NULL, JOB_NVM_COMMIT);
        }
    }
}

/* Overrides the weak HAL stub; called on the periodic timer update event. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &PERIODIC_INT_TIMER_HANDLE)
    {
        v_periodic_int_test();
        v_timer_update();
        v_switch_out_tick();
    }
}

/*============================================================================
 * JOB DISPATCHER
 *==========================================================================*/

void v_process_next_job(void)
{
    uint8_t u8_job_available;
    job_t x_job;

    u8_job_available = u8_job_get(NULL, &x_job);
    if (! u8_job_available)
    {
        return;
    }

    LOGCT(LOG_JOBS, "Next job: #%u", x_job.u8_id);

    switch (x_job.u8_id)
    {
        case JOB_NONE:
            break;

        case JOB_NVM_COMMIT:
            /* A commit erases and rewrites a flash page -- tens of milliseconds,
             * against cycling phase times that can be as short as 10 uS. Rather
             * than risk stalling the compare ISR mid-run, hold the commit off
             * and let the auto-commit timer re-offer it: u8_need_commit is still
             * set, so zeroing the timer re-arms the countdown in v_timer_update()
             * and the parameters reach flash once the bench run finishes. */
            if (u8_switch_cycle_any_running())
            {
                g_x_nvm_param.u16_commit_timer = 0;
                break;
            }
            x_nvm_commit(&g_x_nvm_param);
            break;

        case JOB_CYCLE_COMPLETE:
            /* Queued from the TIM2 ISR, which cannot printf. */
            printf("\r\nSwitch %s cycling complete, %lu cycles\r\n",
                   pc_switch_out_name(x_job.u8_param1),
                   (unsigned long) g_x_switch_cycle[x_job.u8_param1].u32_cycles_done);
            break;

        case JOB_PERIODIC:
//            LOGCT(LOG_SYSTEM, "Periodic: %u mS", PERIODIC_TEST_INTERVAL_MS);
            break;

        case JOB_QUEUE_OVERFLOW:
            LOGC(LOG_SYSTEM, LOGC_WARNING,
                 "Job queue overflow, %u job(s) lost", x_job.u8_param1);
            break;

        default:
            break;
    }
}

/*============================================================================
 * MAIN POLLING TASK / ENTRY
 *==========================================================================*/

/*
 * Everything that must be polled continuously runs here. app_main() calls it
 * from a never-terminating loop; blocking operations may also call it to keep
 * the system responsive while they wait (see i_getline() in utils.c).
 */
void v_app_polling_task(void)
{
    KICK_WATCHDOG();
    v_debug_menu_service();
    v_process_next_job();
}

NEVER_RETURNS void app_main(void)
{
    v_job_queue_init(NULL, NULL, 0);

    v_print_startup_banner();
    v_hardware_init();
    v_debug_menu_init();

    while (1)
    {
        v_app_polling_task();
    }
}
