/******************************************************************************
 * logging.c
 *
 * VENDORED MODULE -- App/logging/ (the engine layer)
 *
 * Dependencies: the C library, the vendored ANSI.h, and logging_config.h
 * (the per-project config header this module names; copy
 * logging_config_template.h into your app's include directory). No HAL, no
 * platform.h, no other application header -- the millisecond tick arrives
 * through u32_log_timestamp_ms(), which the application defines.
 ******************************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "logging_config.h"
#include "logging.h"

/*******************************************************************************
 * Weak default for the application-supplied millisecond tick.
 *
 * Returning 0 means timestamps read (0.000) until the application provides a
 * real implementation, which lets a freshly-vendored copy link and run before
 * a port source has been written. Override it by defining a non-weak
 * u32_log_timestamp_ms() in your own source file -- see
 * logging_port_template.c.
 *******************************************************************************/

__attribute__((weak)) uint32_t u32_log_timestamp_ms(void)
{
    return 0;
}

/*******************************************************************************
 *
 *******************************************************************************/

static void v_newline(void)
{
    putchar('\r');
    putchar('\n');
}

/*******************************************************************************
 *
 *******************************************************************************/

static void v_print_color(log_color_t x_color)
{
    if (x_color & LOGC_NEWLINE_BEFORE)
    {
        v_newline();
    }
    if (x_color & LOGC_NORMAL)
    {
        printf(ANSI_NORMAL);
    }
    else if ((x_color & LOGC_NONE) == 0)
    {
        printf(ANSI_FG_FMT "%s%s%s%s",
               x_color & 0x0F,
               ((x_color & LOGC_BOLD) ? ANSI_BOLD : ""),
               ((x_color & LOGC_UNDERLINE) ? ANSI_UNDERLINE : ""),
               ((x_color & LOGC_REVERSE) ? ANSI_REVERSE : ""),
               ((x_color & LOGC_BLINK) ? ANSI_BLINK : "") );
    }
}

/*******************************************************************************
 *
 *******************************************************************************/

#if LOG_WITH_TIMESTAMP
static void v_print_timestamp(void)
{
    uint32_t u32_tick = u32_log_timestamp_ms();
    uint32_t u32_tick_ms = u32_tick % 1000;
    uint32_t u32_tick_s = u32_tick / 1000;
    printf("(%lu.%03lu) ", u32_tick_s, u32_tick_ms);
}
#endif

/*******************************************************************************
 *
 *******************************************************************************/

void v_log_printf(char *p_c_format, ...)
{
    va_list args;

    va_start(args, p_c_format);
    vprintf(p_c_format, args);
    va_end(args);
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_logc_printf(log_color_t x_color, char *p_c_format, ...)
{
    va_list args;

    v_print_color(x_color);

    va_start(args, p_c_format);
    vprintf(p_c_format, args);
    va_end(args);

    if ( (x_color & (LOGC_NONE | LOGC_NORMAL)) == 0 )
    {
        v_print_color(LOGC_NORMAL);
    }
    if ((x_color & LOGC_NONEWLINE) == 0)
    {
        v_newline();
    }
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_log_printf_time(char *p_c_format, ...)
{
    va_list args;

    #if LOG_WITH_TIMESTAMP
    v_print_timestamp();
    #endif

    va_start(args, p_c_format);
    vprintf(p_c_format, args);
    va_end(args);

    v_newline();
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_logc_printf_time(log_color_t x_color, char *p_c_format, ...)
{
    va_list args;

    v_print_color(x_color);

    #if LOG_WITH_TIMESTAMP
    v_print_timestamp();
    #endif

    va_start(args, p_c_format);
    vprintf(p_c_format, args);
    va_end(args);

    if ( (x_color & (LOGC_NORMAL | LOGC_NONE)) == 0 )
    {
        v_print_color(LOGC_NORMAL);
    }
    if ((x_color & LOGC_NONEWLINE) == 0)
    {
        v_newline();
    }
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_log_printf_time_tag(char *p_c_tag, char *p_c_format, ...)
{
    va_list args;

    #if LOG_WITH_TIMESTAMP
    v_print_timestamp();
    #endif
    printf("[%s] ", p_c_tag);

    va_start(args, p_c_format);
    vprintf(p_c_format, args);
    va_end(args);

    v_newline();
}

/*******************************************************************************
 *
 *******************************************************************************/

void v_logc_printf_time_tag(char *p_c_tag, log_color_t x_color, char *p_c_format, ...)
{
    va_list args;

    v_print_color(x_color);

    #if LOG_WITH_TIMESTAMP
    v_print_timestamp();
    #endif
    printf("[%s] ", p_c_tag);

    va_start(args, p_c_format);
    vprintf(p_c_format, args);
    va_end(args);

    if ( (x_color & (LOGC_NORMAL | LOGC_NONE)) == 0 )
    {
        v_print_color(LOGC_NORMAL);
    }
    if ((x_color & LOGC_NONEWLINE) == 0)
    {
        v_newline();
    }
}
