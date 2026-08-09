/******************************************************************************
 * logging_port.c
 *
 * Application bridge for the vendored logging module.
 *
 * APPLICATION-OWNED PORT FILE. Created by copying
 * App/logging/logging_port_template.c into App/Src/. Edit it freely; never
 * edit the files under App/logging/.
 *
 * The logging module declares u32_log_timestamp_ms() and this file defines it,
 * which is what keeps logging.c free of any HAL or platform dependency.
 ******************************************************************************/

#include <stdint.h>

#include "stm32g0xx_hal.h"

#include "logging.h"

/*******************************************************************************
 * Free-running millisecond counter used to prefix timestamped log messages.
 *
 * Overrides the weak default in logging.c (which returns 0).
 *******************************************************************************/

uint32_t u32_log_timestamp_ms(void)
{
    return HAL_GetTick();
}
