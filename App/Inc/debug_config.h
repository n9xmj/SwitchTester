/*******************************************************************************
 * debug_config.h
 *******************************************************************************/

#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// Turn off all debugging options if DEBUG is not defined (via -DDEBUG compiler
// command line option)

#ifndef DEBUG
#undef DEBUG_LOGGING
#define DEBUG_LOGGING                   0
#undef DEBUG_MENU
#define DEBUG_MENU                      0
#undef INCLUDE_TESTS
#define INCLUDE_TESTS                   0
#endif

#include "logging.h"

// Global debug logging enable
// Setting this to 0 disables most application-generated outputs
// However, debug menu system inclusion is independent of this
// setting.

#if !defined(DEBUG_LOGGING)
// Change this to enable or disable all debug logging output
#define DEBUG_LOGGING                   1
#endif

// Debug menu system enable
// Set this to allow the debug menu system to be included.
// This may be enabled or disabled independent of the DEBUG_LOGGING
// setting.

#ifndef DEBUG_MENU
#define DEBUG_MENU                      1
#endif

//------------------------------------------------------------------------------
// Debug enables and associated output tags
//------------------------------------------------------------------------------

// The logging macros such as LOG(ID, format, arg) make use of two macros
// internally - one which determines whether the log statement is placed in the
// code and output, and another, with its symbol name ending in _TAG,
// which provides the text string describing the message classification.
// When invoking a logging macro which uses these filters, only the un-suffixed
// class macro name needs to be provided.
//
// Example:
// If you want to create a new message classification (filter) named
// LOG_FUBAR, you also need to define a macro named LOG_FUBAR_TAG and associate
// it with a string constant such as "FUBAR"
//
// To generate a conditionally-compiled log message using the LOG_FUBAR filter,
// you only need to provide the un-suffixed classification macro name to the
// macro function, like this:
// LOG(LOG_FUBAR, "format string", arg, arg ...)
// Note that you don't have to provide LOG_FUBAR_TAG in the call - the LOG()
// macro will reference it during expansion by concatenating LOG_FUBAR and _TAG
// using the ## macro concatenation operator.

#if DEBUG_LOGGING

// Misc/system
#define LOG_SYSTEM                      1
#define LOG_SYSTEM_TAG                  "SYSTEM"
#define LOG_SYSTEM_COLOR                LOGC_BRIGHT_MAGENTA

// Job queue activty
#define LOG_JOBS                        0
#define LOG_JOBS_TAG                    "JOB"
#define LOG_JOBS_COLOR                  LOGC_WHITE

// EXTI interrupt reporting
#define LOG_EXTI                        0
#define LOG_EXTI_TAG                    "EXTI"
#define LOG_EXTI_COLOR                  LOGC_WHITE

#endif  // DEBUG_LOGGING

//------------------------------------------------------------------------------

// Macros used for debug output
// Use these instead of direct calls to the v_log_*() functions,
// allowing filtering of debug outputs at build/compile time.

#ifdef DEBUG
  // DPRINTF(...)
  // Unconditional output when the DEBUG build option is enabled.
  // Does not add or modify the output text in any way.
  #define DPRINTF(...) \
          { v_log_printf(__VA_ARGS__); }
  // DPRINTF_TS(...)
  // Unconditional output when the DEBUG build option is enabled, with timestamp
  #define DPRINTF_TS(...) \
          { v_log_printf_ts(__VA_ARGS__); }
#else
  #define DPRINTF(...)
  #define DPRINTF_TS(...)
#endif

#if DEBUG_LOGGING

// LOG_PLAIN(tag, ...)
// Conditional output (tag != 0) without [TAG] or timestamp prefix text.
#define LOG_PLAIN(tag, ...) \
    { if (tag) { v_log_printf(__VA_ARGS__); } }
// LOGC_PLAIN(tag, color, ...)
// Same as LOG_PLAIN(), but provides option to change foreground color of all
// text printed by it on an ANSI terminal.
#define LOGC_PLAIN(tag, color, fmt, ...) \
    { if (tag) { v_logc_printf(color, fmt, ##__VA_ARGS__); } }
// LOGCT_PLAIN(tag, ...)
// Same as LOG_PLAIN, but sets foreground color to the one associated with
// the <tag>
#define LOGCT_PLAIN(tag, fmt, ...) \
    { if (tag) { v_logc_printf(tag ## _COLOR, fmt, ##__VA_ARGS__); } }
// LOG(tag, fmt, ...)
// Conditional output (tag != 0) WITH [TAG] prefix text.
#define LOG(tag, fmt, ...) \
    { if (tag) { v_log_printf_time_tag(tag ## _TAG, fmt, ##__VA_ARGS__); } }
// LOGC(tag, color, fmt, ...)
// Same as LOG(), but provides option to change foreground color of all text
// printed by it on an ANSI terminal.
#define LOGC(tag, color, fmt, ...) \
    { if (tag) { v_logc_printf_time_tag(tag ## _TAG, color, fmt, ##__VA_ARGS__); } }
// LOGCT(tag, fmt, ...)
// Same as LOG(), but sets foreground color for output to the value associated
// with <tag>
#define LOGCT(tag, fmt, ...) \
    { if (tag) { v_logc_printf_time_tag(tag ## _TAG, tag ## _COLOR, fmt, ##__VA_ARGS__); } }

#else // DEBUG_LOGGING

#define LOG_PLAIN(tag, ...)
#define LOGC_PLAIN(tag, color, ...)
#define LOGCT_PLAIN(tag, fmt, ...)
#define LOG(tag, fmt, ...)
#define LOGC(tag, color, fmt, ...)
#define LOGCT(tag, fmt, ...)

#endif // DEBUG_LOGGING

// RPRINTF() is an unconditional printf() output; it is simply an alias
// for printf() implemented as a function macro
// Use of this macro is intended for output that should be generated
// in the release build; i.e. is not conditioned on the <DEBUG> build flag.

#define RPRINTF(...) printf(__VA_ARGS__)

#endif
