/******************************************************************************
 * log_helpers.h
 *
 * VENDORED MODULE -- App/logging/ (the macro "sugar" layer)
 *
 * Provides LOG(), LOGC(), LOGCT(), the _PLAIN variants, DPRINTF() and
 * RPRINTF(). This file is part of the reusable logging component and is NOT
 * project-specific -- do not edit it per-project. Everything that varies
 * between projects lives in logging_config.h.
 *
 * Usage in an adopting project:
 *   - Copy logging_config_template.h from this directory into your app's
 *     include directory, rename it to logging_config.h, and edit the tags.
 *   - Application modules then #include "logging_config.h" to get the full
 *     tagged/colored/timestamped logging experience. That header pulls this
 *     one in for you; do not include this file directly.
 *   - Portable modules that must stay independent of any one project's tags
 *     can include "logging.h" instead and call the v_log*() functions.
 *
 * The tag definitions (LOG_FOO, LOG_FOO_TAG, LOG_FOO_COLOR) must be provided
 * by the including file BEFORE this header is processed -- the ## token
 * pasting below resolves them at the point of use. See
 * logging_config_template.h for the expected pattern.
 ******************************************************************************/

#ifndef LOG_HELPERS_H
#define LOG_HELPERS_H

#include "logging.h"        /* log_color_t, PRINTF_ATTR, the v_log*() prototypes */

//------------------------------------------------------------------------------
// Unconditional / DEBUG-only direct printf wrappers (no tag, no color, no
// filtering by the per-class LOG_* settings).
//
// LOG_IN_DEBUG_BUILD is a constant rather than an #ifdef around the macro
// bodies, so the calls below are ALWAYS compiled -- and therefore always
// format-checked by PRINTF_ATTR -- then folded away when it is 0. A macro that
// expands to nothing instead lets typos and stale variable names rot in a
// non-DEBUG build until someone enables it months later.

#ifdef DEBUG
#define LOG_IN_DEBUG_BUILD              1
#else
#define LOG_IN_DEBUG_BUILD              0
#endif

// DPRINTF(...)
// Unconditional output when the DEBUG build option is enabled.
// Does not add or modify the output text in any way.
#define DPRINTF(...) \
    do { if (LOG_IN_DEBUG_BUILD) { v_log_printf(__VA_ARGS__); } } while (0)

// DPRINTF_TS(...)
// Unconditional output when the DEBUG build option is enabled, with timestamp
#define DPRINTF_TS(...) \
    do { if (LOG_IN_DEBUG_BUILD) { v_log_printf_time(__VA_ARGS__); } } while (0)

//------------------------------------------------------------------------------
// RPRINTF() is an unconditional printf() output; it is simply an alias
// for printf() implemented as a function macro.
// Use of this macro is intended for output that should be generated
// in the release build; i.e. is not conditioned on the <DEBUG> build flag.

#define RPRINTF(...) printf(__VA_ARGS__)

//------------------------------------------------------------------------------
// The emit predicate
//------------------------------------------------------------------------------
//
// LOG_EMIT(tag) decides, at compile time, whether one message class survives
// the configured verbosity ceiling. See the level ladder in logging.h for what
// the numbers mean.
//
//   (tag) <= (LOG_LEVEL)             the class is within the ceiling
//   (tag) != LOG_LEVEL_QUIET         ...and is not itself switched off
//
// The second clause is needed only because QUIET is 0 and everything is <= the
// ceiling when the class is 0. It is what makes 0 mean "quiet" at BOTH ends:
// a class set to QUIET never emits, and a global set to QUIET emits nothing.
//
// Both operands are compile-time constants, so this collapses to a literal
// 1 or 0 and the guarded call is eliminated. There is deliberately no
// runtime component -- see the ladder comment in logging.h.

#define LOG_EMIT(tag)   ( (tag) != LOG_LEVEL_QUIET && (tag) <= (LOG_LEVEL) )

//------------------------------------------------------------------------------
// Debug output macros (the main "sugar").
//
// The LOG_* / LOGC_* / LOGCT_* variants use the per-class verbosity tier (e.g.
// LOG_SYSTEM) plus the associated LOG_SYSTEM_TAG and LOG_SYSTEM_COLOR that must
// be defined by the caller (in logging_config.h) before including this file.
//
// LOGCT(tag, fmt, ...) is the most common: timestamp + [TAG] + color-from-tag.
//
// There is exactly ONE definition of each macro -- no #else branch expanding to
// nothing. Turning logging off is expressed as LOG_LEVEL = LOG_LEVEL_QUIET, so
// every call site is still compiled and format-checked in every configuration,
// and the fold removes the code. Each body is wrapped in do{}while(0) so a
// macro invocation behaves as a single statement:
//     if (x) LOGCT(LOG_FOO, "..."); else y();
// is correct here, where a bare braced block would break the else.

// LOG_PLAIN(tag, ...)
// Conditional output without [TAG] or timestamp prefix text.
#define LOG_PLAIN(tag, ...) \
    do { if (LOG_EMIT(tag)) { v_log_printf(__VA_ARGS__); } } while (0)

// LOGC_PLAIN(tag, color, fmt, ...)
// Same as LOG_PLAIN(), but provides option to change foreground color of all
// text printed by it on an ANSI terminal.
#define LOGC_PLAIN(tag, color, fmt, ...) \
    do { if (LOG_EMIT(tag)) { v_logc_printf(color, fmt, ##__VA_ARGS__); } } while (0)

// LOGCT_PLAIN(tag, fmt, ...)
// Same as LOG_PLAIN, but sets foreground color to the one associated with
// the <tag>
#define LOGCT_PLAIN(tag, fmt, ...) \
    do { if (LOG_EMIT(tag)) { v_logc_printf(tag ## _COLOR, fmt, ##__VA_ARGS__); } } while (0)

// LOG(tag, fmt, ...)
// Conditional output WITH [TAG] prefix text.
#define LOG(tag, fmt, ...) \
    do { if (LOG_EMIT(tag)) { v_log_printf_time_tag(tag ## _TAG, fmt, ##__VA_ARGS__); } } while (0)

// LOGC(tag, color, fmt, ...)
// Same as LOG(), but provides option to change foreground color of all text
// printed by it on an ANSI terminal.
#define LOGC(tag, color, fmt, ...) \
    do { if (LOG_EMIT(tag)) { v_logc_printf_time_tag(tag ## _TAG, color, fmt, ##__VA_ARGS__); } } while (0)

// LOGCT(tag, fmt, ...)
// Same as LOG(), but sets foreground color for output to the value associated
// with <tag>
#define LOGCT(tag, fmt, ...) \
    do { if (LOG_EMIT(tag)) { v_logc_printf_time_tag(tag ## _TAG, tag ## _COLOR, fmt, ##__VA_ARGS__); } } while (0)

#endif /* LOG_HELPERS_H */
