/******************************************************************************
 * logging_port_template.c
 *
 * USAGE TEMPLATE for the logging module's application bridge.
 *
 * **********************************************************************
 * IMPORTANT: THIS FILE IS EXCLUDED FROM THE BUILD.
 * **********************************************************************
 *
 * Copy it into your application's source directory (e.g. App/Src/), rename it
 * to "logging_port.c", and adjust the tick source for your platform.
 *
 * The logging module declares the function below and the application defines
 * it -- the same arrangement FatFs uses for disk_read()/get_fattime() and lwIP
 * for sys_now(). logging.c carries a WEAK default returning 0, so this file is
 * optional: without it the module still links and runs, and timestamped
 * messages simply read (0.000).
 ******************************************************************************/

#include <stdint.h>

/* Replace with whatever header provides your millisecond tick.
 * STM32 HAL example: */
#include "stm32g0xx_hal.h"

#include "logging.h"

/*******************************************************************************
 * Free-running millisecond counter used to prefix timestamped log messages.
 *
 * It need not start at zero or relate to wall-clock time -- only the
 * difference between successive calls is meaningful to a reader. It must not
 * block, and it must be safe to call from anywhere logging is used, including
 * interrupt context if the application logs from an ISR.
 *******************************************************************************/

uint32_t u32_log_timestamp_ms(void)
{
    return HAL_GetTick();
}
