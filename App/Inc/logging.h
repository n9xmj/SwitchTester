/******************************************************************************
 * logging.h
 ******************************************************************************/

#ifndef LOGGING_H
#define LOGGING_H

#include "ANSI.h"

// Set LOG_WITH_TIMESTAMP to a nonzero value to enable log outputs generated
// using v_log_* functions that include '_time' in the name (such as
// v_log_printf_time()) to prefix message outputs with a timestamp derived
// from the system RTC (HAL_GetTick() for STM32)

#ifndef LOG_WITH_TIMESTAMP
#define LOG_WITH_TIMESTAMP              1
#endif

//------------------------------------------------------------------------------

// Color values used with color logging functions
// v_logc_xxx(), LOGC(), LOGC_PLAIN()
// Do not change the order of these enums! The ordinal value of each of these
// must match up with ANSI standard color numbers. See ANSI.h and the
// ANSI_FG_xxxx macro definitions in it to see how this works.

typedef enum __attribute__((packed))
{
    // Don't change the ordering or assigned value for these
    LOGC_BLACK,
    LOGC_RED,
    LOGC_GREEN,
    LOGC_YELLOW,
    LOGC_BLUE,
    LOGC_MAGENTA,
    LOGC_CYAN,
    LOGC_WHITE,
    LOGC_GRAY,
    LOGC_BRIGHT_RED,
    LOGC_BRIGHT_GREEN,
    LOGC_BRIGHT_YELLOW,
    LOGC_BRIGHT_BLUE,
    LOGC_BRIGHT_MAGENTA,
    LOGC_BRIGHT_CYAN,
    LOGC_BRIGHT_WHITE,
    // ANSI attribute bitmasks - these have no enum definition ordering
    // requirement but must be powers of 2 that are >= 0x10
    // (e.g. 0x10, 0x20, 0x40, etc.)
    // These can be bitwise-OR'ed with the color numbers to add text
    // effects. e.g. LOGC_YELLOW | LOGC_BOLD | LOGC_UNDERLINE will produce
    // boldfaced and underlined yellow colored text.
    LOGC_BOLD = 0x10,
    LOGC_UNDERLINE = 0x20,
    LOGC_REVERSE = 0x40,
    LOGC_BLINK = 0x80,
    // Standardized colors for different classes of messages
    // (warning, error, highlight)
    // OK to change these to any color/attribute combination desired.
    LOGC_WARNING = LOGC_YELLOW,
    LOGC_ERROR = LOGC_BRIGHT_RED,
    LOGC_HIGHLIGHT = LOGC_BRIGHT_MAGENTA,
    // LOGC_NORMAL should have the highest ordinal value +1 of any of the
    // color selections listed here + all attribute bits set.
    // LOGC_BRIGHT_WHITE | LOGC_BOLD | LOGC_UNDERLINE | LOGC_REVERSE |
    // LOGC_BLINK = 0xFF, so LOGC_NORMAL should be set to 0x100.
    // LOGC_NORMAL will have to be changed if new attribute bitmasks are added.
    LOGC_NORMAL = 0x100,
    // LOGC_NONEWLINE is a special case; it's not a color or display attribute
    // but it is a display option that can be ORed with any other attribute
    // including LOGC_NORMAL. This needs its own bit slot just like LOGC_NORMAL.
    LOGC_NONEWLINE = 0x200,
    // LOGC_NEWLINE_BEFORE can be OR'd into the color specification to request
    // that a newline sequence be issued BEFORE the tag/timestamp and message
    // are output.
    LOGC_NEWLINE_BEFORE = 0x400,
    // Use LOGC_NONE to completely suppress ANSI escape sequence transmissions
    // at the start and end of a message. This differs from the behavior of
    // LOGC_NORMAL, which will send out ANSI attribute-off sequences at the
    // start and end of a log message.
    LOGC_NONE = 0x8000
}
log_color_t;

//------------------------------------------------------------------------------

// GCC supports some limited format string sanity checking vs. variable arguments
// provided when a printf-workalike function is tagged with
// _attribute__((format (printf, x, y)))
// The Keil (MDK-ARM) toolchain supports this too, using the same syntax

#if defined(__GNUC__) || defined(__CC_ARM)
#define PRINTF_ATTR(fmtpos,va_argpos)   __attribute__((format (printf, fmtpos, va_argpos)))
#else
#define PRINTF_ATTR(fmtpos,va_argpos)
#endif

// It is suggested that the logging macros defined in debug_config.h used to
// generate log messages rather than using these functions directly.
// These functions do not provide any sort of compile-time inclusion filtering,
// so direct calls to these will always be included in the application code,
// even if a release version intended to have no debug outputs is built.

extern void v_log_printf(char *p_c_format, ...) PRINTF_ATTR(1, 2);
extern void v_logc_printf(log_color_t x_color, char *p_c_format, ...) PRINTF_ATTR(2, 3);
extern void v_log_printf_time(char *p_c_format, ...) PRINTF_ATTR(1, 2);
extern void v_logc_printf_time(log_color_t x_color, char *p_c_format, ...) PRINTF_ATTR(2, 3);
extern void v_log_printf_time_tag(char *p_c_tag, char *p_c_format, ...) PRINTF_ATTR(2, 3);
extern void v_logc_printf_time_tag(char *p_c_tag, log_color_t x_color, char *p_c_format, ...) PRINTF_ATTR(3, 4);

#endif
