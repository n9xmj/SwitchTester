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
#include "nvm_test.h"               /* RAM-backed test pool, SwitchTester only */

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

/* The application's parameter pool. Owned here, exported via globals.h.
 * nvmparams no longer pre-declares a pool of its own -- see D5/I4. */
nvm_pool_t g_x_nvm_param;

/*----------------------------------------------------------------------------
 * Pool configuration.
 *
 * const, so it lives in flash: nvmparams never writes to it, and does not
 * retain the pointer once init returns -- it copies what it needs.
 *
 * The base address comes from the linker script rather than from a buffer
 * declared with __attribute__((section(".nvmdata"))). See the NVM_FLASH region
 * and the .nvmdata section in STM32G0B1RETX_FLASH.ld.
 *
 * Every field left unset is a deliberate default: no CRC (signature-only
 * validation, the legacy behaviour), no wear levelling, internally allocated
 * RAM buffer, and NVM_INIT_FORMAT_IF_BLANK -- format virgin media, refuse to
 * silently destroy a corrupt pool.
 *--------------------------------------------------------------------------*/

extern uint32_t _nvm_start;         /* provided by the linker script */

static const nvm_pool_config_t x_nvm_param_config =
{
    /* Stored in the pool header. Informational only -- nvmparams never makes a
     * decision on it. Shows up in the debug-menu pool dump. */
    .p_c_label       = "PARAMS",

    /* Storage driver, read half: fills the RAM pool from the device at init. */
    .pfn_read        = x_nvm_drv_stm_flash_read,

    /* Storage driver, write half: writes the RAM pool out on commit. */
    .pfn_write       = x_nvm_drv_stm_flash_write,

    /* Where the pool lives on the device. Supplied by the linker script rather
     * than by a __attribute__((section)) buffer -- see NVM_FLASH / .nvmdata in
     * STM32G0B1RETX_FLASH.ld. For a SPI or file backend this same field would
     * be a byte offset instead of an address. */
    .ux_base_address = (uintptr_t) &_nvm_start,

    /* Pool size in bytes. Must be a multiple of 4 and at least
     * NVM_POOL_SIZE_MIN; init rejects anything smaller. */
    .u32_size        = NVM_POOL_SIZE_DEFAULT,

    /* The device's erase granularity -- one 2 KB page on the STM32G0. Only
     * used to space wear-level blocks, so it is inert until wear levelling
     * exists, but supplying it now costs nothing and documents the hardware. */
    .u32_alloc_unit  = FLASH_PAGE_SIZE,

    /* Reformat a corrupt pool rather than refusing to start. SwitchTester is
     * bench tooling and its parameters are not precious -- losing them costs a
     * re-entry in the debug menu, whereas refusing to boot costs the
     * instrument. A product holding calibration or a serial number should NOT
     * choose this; the default (FORMAT_IF_BLANK) refuses instead, and
     * NVM_INIT_REQUIRE_VALID refuses even to format blank media. */
    .x_init_policy   = NVM_INIT_FORMAT_IF_INVALID,

    /* Not used in this project -- shown for reference. Each defaults to 0/NULL,
     * and each zero value means the feature is simply off. */
//  .pfn_crc         = NULL,                        /* signature-only validation */
//  .p_v_ram_buffer  = NULL,                        /* allocate the pool internally */
//  .p_v_context     = NULL,                        /* nothing to pass to the driver */
//  .u8_wear_blocks  = 0,                           /* no wear levelling */
};

/*----------------------------------------------------------------------------
 * Fallback configuration -- the null device.
 *
 * Identical to the real pool except that it has no storage driver, so the RAM
 * buffer works normally and nothing is ever persisted. Used only if the flash
 * pool cannot be brought up at all.
 *--------------------------------------------------------------------------*/

static const nvm_pool_config_t x_nvm_param_config_volatile =
{
    .p_c_label       = "PARAMS-RAM",
    .u32_size        = NVM_POOL_SIZE_DEFAULT,
    /* pfn_read / pfn_write deliberately NULL -- see nvmparams.h on the null
     * device. Reads zero the buffer, writes report success and do nothing. */
};

void v_param_init(void)
{
    nvm_error_t x_status = x_nvm_pool_init(&g_x_nvm_param, &x_nvm_param_config);

    /* Test against NVM_ERROR_NONE, never against a list of known codes and
     * never "< 0": a driver may return a positive device-specific value we
     * have never heard of. */
    switch (x_status)
    {
        case NVM_ERROR_NONE:
            break;

        case NVM_ERROR_POOL_FORMATTED:
            /* Blank media. The normal first-boot path on a virgin board, and
             * on any board whose NVM sector has been erased. Nothing lost. */
            LOGCT(LOG_NVM, "pool formatted -- media was blank");
            break;

        case NVM_ERROR_POOL_REFORMATTED:
            /* Media held something that was not a valid pool of ours. Logged
             * at a higher volume than the blank case because parameters WERE
             * destroyed -- most likely a foreign pool left in the NOLOAD
             * sector by another project's firmware, which reflashing does not
             * erase. */
            LOGCT(LOG_NVM, "pool REFORMATTED -- previous contents were destroyed");
            break;

        default:
            /* The flash pool could not be brought up even with reformatting
             * allowed. Degrade to a volatile pool rather than refusing to run:
             * this is a bench instrument, and one that boots with default
             * parameters and a loud warning is still useful, whereas one that
             * sits and spins is not.
             *
             * This is an APPLICATION policy decision, not the module's. A
             * product holding calibration data would rightly choose otherwise. */
            LOGCT(LOG_NVM, "pool init FAILED, status %d -- falling back to volatile",
                  (int) x_status);

            x_status = x_nvm_pool_init(&g_x_nvm_param, &x_nvm_param_config_volatile);
            if (x_status != NVM_ERROR_NONE)
            {
                /* Nothing left to try. The volatile pool has no device to
                 * fail on, so reaching here means a bad configuration or a
                 * failed allocation -- a software fault, not a hardware one. */
                LOGCT(LOG_NVM, "volatile fallback ALSO failed, status %d", (int) x_status);
                break;
            }

            /* Flag the degradation where the rest of the system can see it.
             * Necessary because the null device is deliberately transparent:
             * x_nvm_commit() reports success and does nothing, so without this
             * neither the operator nor a HIL run could tell that persistence
             * had stopped working. */
            g_x_nvm_param.u8_user1 = NVM_USER1_VOLATILE_FALLBACK;
            LOGCT(LOG_NVM, "running on a VOLATILE pool -- settings will not persist");
            break;
    }

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

    /* Second pool, RAM-backed, for the host-driven nvmparams suite. Kept
     * entirely separate from the flash pool above so tests can corrupt and
     * fault-inject freely -- and it is the only thing in this project that
     * exercises multi-pool operation. SwitchTester only; see nvm_test.h. */
    v_nvm_test_init();
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
                LOGCT(LOG_SYSTEM, "NVM commit deferred - switch cycling active");
                break;
            }
            {
                nvm_error_t x_nvm_status = x_nvm_commit(&g_x_nvm_param);

                /* NVM_ERROR_NO_CHANGE means the pool was already clean, so no
                 * erase/write cycle was spent. Anything negative below that is
                 * a real failure. */
                LOGCT(LOG_SYSTEM, "NVM commit: status %d (%s)",
                      (int) x_nvm_status,
                      (x_nvm_status == NVM_ERROR_NONE)      ? "written" :
                      (x_nvm_status == NVM_ERROR_NO_CHANGE) ? "no change" : "FAILED");
            }
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

/*
 * Boot indicator: a 250 ms blip on the Nucleo LED as the last thing before the
 * main loop. Visible proof the board reset and got all the way through init --
 * useful when the console is not attached, or when the console is exactly what
 * you are trying to diagnose.
 *
 * Blocking on purpose. Nothing needs servicing between init finishing and the
 * loop starting, and a plain HAL_Delay keeps it obvious.
 */
static void v_boot_indicator(void)
{
    HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, GPIO_PIN_SET);
    HAL_Delay(250);
    HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, GPIO_PIN_RESET);
}

NEVER_RETURNS void app_main(void)
{
    v_job_queue_init(NULL, NULL, 0);

    v_print_startup_banner();
    v_hardware_init();
    v_debug_menu_init();
    v_boot_indicator();

    while (1)
    {
        v_app_polling_task();
    }
}
