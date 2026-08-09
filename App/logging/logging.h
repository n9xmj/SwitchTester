/******************************************************************************
 * logging.h
 *
 * VENDORED MODULE -- App/logging/ (the engine layer)
 *
 * Declares the log_color_t attribute set and the v_log*() output functions.
 * This header and logging.c depend on nothing but the C library and the
 * vendored ANSI.h; there is no HAL, no platform.h and no application header
 * in the dependency graph.
 *
 * The two things this module needs FROM the application it declares here and
 * the application defines:
 *
 *   1. uint32_t u32_log_timestamp_ms(void)  -- see the prototype below.
 *      A weak default returning 0 lives in logging.c, so a project links and
 *      runs before it has written a port source (timestamps read 0.000).
 *      Copy logging_port_template.c to your App/Src/ to override it.
 *
 *   2. logging_config.h -- the per-project config header naming the message
 *      classes. Copy logging_config_template.h to your App/Inc/ and edit.
 *
 * It is suggested that the macros in log_helpers.h be used to generate log
 * messages rather than calling these functions directly. The functions apply
 * no compile-time filtering, so direct calls are always emitted even in a
 * build intended to produce no debug output.
 ******************************************************************************/

#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>

#include "ANSI.h"

// Set LOG_WITH_TIMESTAMP to a nonzero value to enable log outputs generated
// using v_log_* functions that include '_time' in the name (such as
// v_log_printf_time()) to prefix message outputs with a timestamp obtained
// from u32_log_timestamp_ms().
//
// This is the fallback default. To change it, define LOG_WITH_TIMESTAMP in
// logging_config.h BEFORE that file includes this one -- the #ifndef below
// then leaves the project's choice alone.

#ifndef LOG_WITH_TIMESTAMP
#define LOG_WITH_TIMESTAMP              1
#endif

//------------------------------------------------------------------------------
// Verbosity levels
//------------------------------------------------------------------------------
//
// These measure VERBOSITY, not severity, and they ascend from terse to chatty.
// Read a value as "the verbosity tier at which this message class becomes
// visible" -- equivalently, how much noise you must be willing to accept in
// order to see it. An error is cheap to show; debug chatter is expensive.
//
// Two things are expressed on this one scale:
//
//   - Each message class in logging_config.h is assigned a tier:
//         #define LOG_SYSTEM      LOG_LEVEL_ERROR
//   - LOG_LEVEL, also in logging_config.h, is the global ceiling -- how
//         verbose this build is willing to be.
//
// A class is emitted when its tier is within the ceiling (tag <= LOG_LEVEL),
// so LOG_LEVEL_WARNING passes ALWAYS/ERROR/WARNING and drops INFO/DEBUG. See
// LOG_EMIT() in log_helpers.h for the exact predicate.
//
// LOG_LEVEL_QUIET is 0 at BOTH ends of that comparison: a class set to QUIET
// never emits, and a global set to QUIET emits nothing at all -- including
// LOG_LEVEL_ALWAYS, which means "never filtered out by verbosity tuning", not
// "outranks the master switch". Use RPRINTF() for output that must survive
// any configuration.
//
// Keeping 0 == quiet is also what preserves the legacy 0/1 enable scheme: a
// project that still writes `#define LOG_FOO 1` need only set
// LOG_LEVEL to LOG_LEVEL_DEBUG and everything behaves as it did before.
//
// All of these are compile-time constants so the test in each LOGxx() macro
// folds and the guarded call is eliminated at any -O above -O0. Do not make
// LOG_LEVEL a runtime variable: an application may carry hundreds of LOGxx()
// invocations and the elimination is the whole point on a small part.

#define LOG_LEVEL_QUIET                 0   // never emitted (class or global)
#define LOG_LEVEL_ALWAYS                1   // shown whenever logging is on at all
#define LOG_LEVEL_ERROR                 2
#define LOG_LEVEL_WARNING               3
#define LOG_LEVEL_INFO                  4
#define LOG_LEVEL_DEBUG                 5   // chattiest

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
//
// Defined here only; log_helpers.h picks it up by including this header.

#if defined(__GNUC__) || defined(__CC_ARM)
#define PRINTF_ATTR(fmtpos,va_argpos)   __attribute__((format (printf, fmtpos, va_argpos)))
#else
#define PRINTF_ATTR(fmtpos,va_argpos)
#endif

//------------------------------------------------------------------------------
// Application-supplied bridge function (see the file header)
//------------------------------------------------------------------------------

// Return a free-running millisecond counter, used to prefix timestamped log
// messages. On STM32 the application normally returns HAL_GetTick().
//
// logging.c provides a weak definition returning 0, so this need not be
// supplied for the module to link. Define it in your own source file (see
// logging_port_template.c) to override that default.

extern uint32_t u32_log_timestamp_ms(void);

//------------------------------------------------------------------------------
// Output functions
//------------------------------------------------------------------------------

extern void v_log_printf(char *p_c_format, ...) PRINTF_ATTR(1, 2);
extern void v_logc_printf(log_color_t x_color, char *p_c_format, ...) PRINTF_ATTR(2, 3);
extern void v_log_printf_time(char *p_c_format, ...) PRINTF_ATTR(1, 2);
extern void v_logc_printf_time(log_color_t x_color, char *p_c_format, ...) PRINTF_ATTR(2, 3);
extern void v_log_printf_time_tag(char *p_c_tag, char *p_c_format, ...) PRINTF_ATTR(2, 3);
extern void v_logc_printf_time_tag(char *p_c_tag, log_color_t x_color, char *p_c_format, ...) PRINTF_ATTR(3, 4);

#endif
