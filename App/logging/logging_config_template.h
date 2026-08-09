/**
 * @file logging_config_template.h
 *
 * USAGE TEMPLATE for the logging module (logging.h / log_helpers.h).
 *
 * **********************************************************************
 * IMPORTANT: DO NOT #include THIS FILE DIRECTLY IN YOUR APPLICATION.
 * **********************************************************************
 *
 * This is a demonstration template only.
 *
 * Adopting the logging module in a new project:
 *   1. Copy this file into your application's include directory (e.g. App/Inc/).
 *   2. Rename the copy to "logging_config.h".
 *   3. Edit the tag definitions below to match your project's needs.
 *   4. Copy logging_port_template.c to App/Src/logging_port.c and implement
 *      u32_log_timestamp_ms() there (optional -- a weak default returning 0
 *      lives in logging.c, so skipping this step just means timestamps read
 *      0.000).
 *   5. From modules that want convenient logging, do:
 *         #include "logging_config.h"
 *   6. Use the macros: LOGCT(LOG_YOURTAG, "message %d", value);
 *      RPRINTF("unconditional output\r\n");   // always available
 *
 * The App/logging/ directory is a reusable component dropped into multiple
 * projects unchanged. Each project supplies its own logging_config.h with its
 * own tags, colors and levels. The module also depends on App/common/ANSI.h,
 * which travels with it.
 *
 * NOTE: logging.c includes "logging_config.h" by name, so this file must exist
 * on the include path for the module to compile. That is deliberate -- it is
 * the same contract FatFs uses for ffconf.h and lwIP for lwipopts.h.
 */

#ifndef LOGGING_CONFIG_TEMPLATE_H
#define LOGGING_CONFIG_TEMPLATE_H

//------------------------------------------------------------------------------
// Knobs that must be set BEFORE the module headers are pulled in
//------------------------------------------------------------------------------
// logging.h supplies a fallback default for LOG_WITH_TIMESTAMP via #ifndef, so
// setting it here (ahead of the include below) is what makes the project's
// choice stick. Nonzero prefixes timestamped messages with (seconds.millis).

#define LOG_WITH_TIMESTAMP              1

#include "logging.h"   // brings in log_color_t and the LOGC_* constants used below

//------------------------------------------------------------------------------
// Global verbosity ceiling (EDIT THIS)
//------------------------------------------------------------------------------
// How verbose this build is willing to be. A message class is emitted when its
// tier is at or below this value. The ladder and what the tiers mean are
// documented in logging.h:
//
//   LOG_LEVEL_QUIET    0   nothing at all, including LOG_LEVEL_ALWAYS classes
//   LOG_LEVEL_ALWAYS   1   only classes marked ALWAYS
//   LOG_LEVEL_ERROR    2
//   LOG_LEVEL_WARNING  3
//   LOG_LEVEL_INFO     4
//   LOG_LEVEL_DEBUG    5   everything
//
// MIGRATING AN OLDER PROJECT that still writes `#define LOG_FOO 1` / `0`?
// Set LOG_LEVEL to LOG_LEVEL_DEBUG and leave the tags alone -- a tag of 0 stays
// quiet and a tag of 1 stays on, exactly as before. One line, no refactor.
//
// Turn logging off entirely if DEBUG is not defined (via -DDEBUG on the
// compiler command line, typically set in Debug build configurations).

#ifndef DEBUG
#define LOG_LEVEL                       LOG_LEVEL_QUIET
#else
#define LOG_LEVEL                       LOG_LEVEL_DEBUG
#endif

//------------------------------------------------------------------------------
// Project-specific tag definitions (EDIT THESE)
//------------------------------------------------------------------------------
// Each message class you want to use requires three coordinated defines:
//
//   #define LOG_FOO           LOG_LEVEL_INFO   // QUIET = compiled out entirely
//   #define LOG_FOO_TAG       "FOO"            // string used in the [TAG] prefix
//   #define LOG_FOO_COLOR     LOGC_xxx         // an LOGC_* value from logging.h
//
// The class value is the verbosity tier at which that class becomes visible --
// how chatty it is. Give terse, important classes a low tier (ERROR) and noisy
// ones a high tier (DEBUG), then tune LOG_LEVEL above to taste. The
// LOGCT(LOG_FOO, "fmt", args...) family folds the comparison at compile time,
// giving free per-subsystem filtering. You only pass the un-suffixed name to
// the macro -- it reaches _TAG and _COLOR itself using the ## token-pasting
// operator.
//
// Keep the names reasonably short. Colors are defined in logging.h (LOGC_RED,
// LOGC_WHITE, LOGC_YELLOW, LOGC_BRIGHT_* etc. plus the attribute bits).
//
// Do NOT wrap these in a conditional. The macros always compile, so the tag
// symbols must always exist; a logging-off build is expressed by LOG_LEVEL
// above, not by making the tags disappear.
//
// The example tags below are for illustration only.

// Generic / catch-all messages that don't fit another category.
#define LOG_SYSTEM                      LOG_LEVEL_INFO
#define LOG_SYSTEM_TAG                  "SYSTEM"
#define LOG_SYSTEM_COLOR                LOGC_BRIGHT_MAGENTA

// Example subsystem tag (replace or delete as needed).
#define LOG_EXAMPLE                     LOG_LEVEL_DEBUG
#define LOG_EXAMPLE_TAG                 "EX"
#define LOG_EXAMPLE_COLOR               LOGC_WHITE

//------------------------------------------------------------------------------
// Pull in the macro sugar (LOGCT, LOG, RPRINTF, DPRINTF, etc.)
// The tag defines above must precede this include.

#include "log_helpers.h"

#endif /* LOGGING_CONFIG_TEMPLATE_H */
