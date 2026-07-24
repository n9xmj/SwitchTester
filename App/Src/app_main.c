/******************************************************************************
 * Application MAIN
 ******************************************************************************/

#include "device_config.h"              // Includes debug_config.h, main.h, macros.h, C stdlib
#include "utils.h"
#include "jobs.h"
#include "nvmparams.h"
#include "debug_menu.h"
#include "led.h"
#include "button.h"
#include "ui.h"
#include "ssd1306.h"
#include "uart_int.h"
#include "gps.h"

/******************************************************************************
 *
 ******************************************************************************/

#if LOG_EXTI
const char * p_c_exti_pin_name[] =
{
/* GPIO pin # */
    /*  0 */ "TMR_INT",
    /*  1 */ NULL,
    /*  2 */ NULL,
    /*  3 */ "MTR_INDEX",
    /*  4 */ NULL,
    /*  5 */ "MTR_FAULT",
    /*  6 */ NULL,
    /*  7 */ NULL,
    /*  8 */ "SWITCH4",
    /*  9 */ "SWITCH3",
    /* 10 */ "SWITCH2",
    /* 11 */ "SWITCH1",
    /* 12 */ NULL,
    /* 13 */ "NUCLEO_BUTTON",
    /* 14 */ NULL,
    /* 15 */ NULL,
};
#endif

/******************************************************************************
 *
 ******************************************************************************/

void v_print_startup_banner(void)
{
    if (x_reset_source.x_reset_type == RESET_TYPE_UNKNOWN)
    {
        v_get_reset_source();
    }

    v_newline();
    v_repeat_char('*', -64);
    RPRINTF("Product             : " PRODUCT_NAME "\r\n"
            "Product ID          : " PRODUCT_ID "\r\n"
            "SKU                 : " PRODUCT_SKU "\r\n"
            "Main PCB revision   : " MAIN_PCB_REVISION "\r\n"
            "Firmware version    : " FIRMWARE_VERSION "\r\n"
            "Release #           : " RELEASE_REVISION "\r\n"
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

/******************************************************************************
 *
 ******************************************************************************/

uint32_t u32_test_param_1;

void v_param_init(void)
{

    x_nvm_pool_init(&g_x_nvm_param, NVM_DEVICE_MCUFLASH, NULL, 0, "PARAMS");

    u32_test_param_1 = 0xDEAD;
    x_nvm_create(&g_x_nvm_param, NVM_PARAM_TEST_1,
                 sizeof(u32_test_param_1),
                 &u32_test_param_1);
    x_nvm_get(&g_x_nvm_param, NVM_PARAM_TEST_1, &u32_test_param_1);

    x_nvm_commit(&g_x_nvm_param);
}

/******************************************************************************
 *
 ******************************************************************************/

void v_hardware_init(void)
{
    v_led_init();
    v_param_init();
    v_button_init();
    ssd1306_Init();
    v_ui_handler(NO_BUTTON);

//    x_nvm_commit(&g_x_nvm_param);

    HAL_TIM_Base_Start_IT(&PERIODIC_INT_TIMER_HANDLE);
}

/******************************************************************************
 *
 ******************************************************************************/

#define PERIODC_INT_TEST_INTERVAL_MS        1000

void v_periodic_int_test(void)
{
    static uint16_t u16_count;

    u16_count += PERIODIC_TIMER_INTERVAL_MS;
    if (u16_count >= PERIODC_INT_TEST_INTERVAL_MS)
    {
        u16_count -= PERIODC_INT_TEST_INTERVAL_MS;
        v_job_add(NULL, JOB_PERIODIC);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_timer_update(void)
{
#if 0
    // Indication update service (job) timing

    if ( (g_x_LED_ind_info.x_indication_status == LED_INDICATION_IN_PROGRESS)
         && (g_x_LED_ind_info.u32_update_timestamp != 0)
         && (SYSTEM_TICK() > g_x_LED_ind_info.u32_update_timestamp) )
    {
        g_x_LED_ind_info.u32_update_timestamp = 0;
        v_job_add(NULL, JOB_INDICATION_UPDATE);
    }
#endif

    // NVM auto-commit check

    if (g_x_nvm_param.u8_need_commit
        && (g_x_nvm_param.u16_commit_timer < DEV_CONFIG_NVM_COMMIT_DELAY_MS))
    {
        g_x_nvm_param.u16_commit_timer += PERIODIC_TIMER_INTERVAL_MS;
        if (g_x_nvm_param.u16_commit_timer >= DEV_CONFIG_NVM_COMMIT_DELAY_MS)
        {
            v_job_add(NULL, JOB_NVM_COMMIT);
        }
    }

    // Button debounce

    v_button_debounce_service();
}

/******************************************************************************
 *
 ******************************************************************************/

// This function definition overrides the weak stub definition in the HAL.
// It is called by the HAL when a timer's counter is reset (update event)
// and its corresponding interrupt is enabled.
//
// At present, only one timer (TIM16, the periodic 10mS tick timer) is being
// configured to generate update event interrupts. Servicing of that interrupt
// is done here.

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &PERIODIC_INT_TIMER_HANDLE)
    {
        v_periodic_int_test();
        v_timer_update();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_exti_report(uint8_t u8_level, uint16_t u16_pin_mask)
{
#if LOG_EXTI
    const char *p_c_pinname;
    uint8_t u8_pin_number;
    uint8_t u8_onebits;

    u8_onebits = __builtin_popcount(u16_pin_mask);
    u8_pin_number = __builtin_ctz(u16_pin_mask);
    if ( (u8_onebits != 1)
         || (u8_pin_number > 15)
         || (p_c_exti_pin_name[u8_pin_number] == NULL) )
    {
        p_c_pinname = "?UNKNOWN?";
    }
    else
    {
        p_c_pinname = p_c_exti_pin_name[u8_pin_number];
    }

    LOGCT(LOG_EXTI, "%s INT on %s (Pin %u/0x%04X)",
          (u8_level ? "+Rising" : "-Falling"),
          p_c_pinname, u8_pin_number, u16_pin_mask);
#endif
}

/******************************************************************************
 *
 ******************************************************************************/

void HAL_GPIO_EXTI_Rising_Callback(uint16_t u16_pin)
{
#if LOG_EXTI
    v_job_add_with_params(NULL, JOB_EXTI_REPORT, 1, u16_pin);
#endif

    if (u16_pin == SWITCH4_INT_Pin)
    {

    }
    else if (u16_pin == SWITCH3_INT_Pin)
    {

    }
    else if (u16_pin == SWITCH2_INT_Pin)
    {

    }
    else if (u16_pin == SWITCH1_INT_Pin)
    {

    }
    else if (u16_pin == NUCLEO_BUTTON_Pin)
    {

    }
    else
    {

    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t u16_pin)
{
#if LOG_EXTI
    v_job_add_with_params(NULL, JOB_EXTI_REPORT, 0, u16_pin);
#endif

    if (u16_pin == SWITCH4_INT_Pin)
    {
//        v_job_add_with_params(NULL, JOB_BUTTON, 4, 0);
    }
    else if (u16_pin == SWITCH3_INT_Pin)
    {
//        v_job_add_with_params(NULL, JOB_BUTTON, 3, 0);
    }
    else if (u16_pin == SWITCH2_INT_Pin)
    {
//        v_job_add_with_params(NULL, JOB_BUTTON, 2, 0);
    }
    else if (u16_pin == SWITCH1_INT_Pin)
    {
//        v_job_add_with_params(NULL, JOB_BUTTON, 1, 0);
    }
    else if (u16_pin == NUCLEO_BUTTON_Pin)
    {
//        v_job_add_with_params(NULL, JOB_BUTTON, 0, 0);
    }
    else
    {

    }
}

/******************************************************************************
 *
 ******************************************************************************/

extern void v_debug_mtr_test_start();
extern void v_debug_mtr_test_stop();

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

        case JOB_BUTTON:
            v_ui_handler(x_job.u8_param1);
            break;

#if 0
        case JOB_INDICATION_UPDATE:
            g_u32_sleep_mode_timer = 0;
            v_job_indication_update();
            break;
#endif

        case JOB_NVM_COMMIT:
            x_nvm_commit(&g_x_nvm_param);
            break;

        case JOB_PERIODIC:
//            LOGCT(LOG_SYSTEM, "1s periodic int");
            break;

        case JOB_TEST:
            break;

        case JOB_FAULT:
            LOGCT(LOG_SYSTEM,  "*** DRIVER FAULT ***");
            break;

        case JOB_EXTI_REPORT:
            v_exti_report(x_job.u8_param1, x_job.u16_param2);
            break;

        case JOB_QUEUE_OVERFLOW:
            // Log queue overflow event using LOG_SYSTEM flag instead of
            // LOG_JOBS since the latter is normally turned off.
            LOGC(LOG_SYSTEM, LOGC_WARNING, "Job queue overflow, %u job(s) lost", x_job.u8_param1);
            break;

        default:
            break;
    }
}

/******************************************************************************
 * app_polling_task()
 *
 * This function contains all of the things that need to be done repeatedly
 * (constantly polled) by app_main().
 *
 * app_main() will call this function from within a never-terminating
 * while() loop. Other processes that block the system may call this
 * function to keep the system active and responsive while blocking.
 *
 * i_getline() in utils.c makes use of this.
 ******************************************************************************/

volatile uint32_t u32_idle_count;

void app_polling_task(void)
{
    bool b_gps_data_valid;

    KICK_WATCHDOG();
    u32_idle_count++;
    v_debug_menu_service();
    v_process_next_job();
    b_gps_data_valid = b_gps_get_data();
    if (b_gps_data_valid)
    {
        b_gps_data_valid = b_gps_parse_data(c_gps_data, &g_x_gps_info);
        if (b_gps_data_valid)
        {
            v_ui_display_gps_info(&g_x_gps_info);
        }
    }
}

/******************************************************************************
 *
 ******************************************************************************/

NEVER_RETURNS void app_main(void)
{
//  uwTick = 0;

    v_uart_interrupt_init();
    v_job_queue_init(NULL, NULL, 0);

    v_print_startup_banner();

    v_hardware_init();

    v_debug_menu_init();

//    v_indicate(LED_INDICATION_DEVICE_ON);

    while (1)
    {
        app_polling_task();
    }
}
