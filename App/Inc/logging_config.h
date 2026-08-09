/**
 * @file    logging_config.h
 * @brief   Project-specific logging configuration and message-class tags.
 *
 * This is the header application modules include when they want the logging
 * "sugar" (LOGCT, LOG, RPRINTF, ...). It defines this project's message
 * classes -- enable/level, tag string and color -- and then pulls in the macro
 * helpers from the vendored module.
 *
 * APPLICATION-OWNED PORT FILE. Created by copying App/logging/logging_config_template.h
 * into App/Inc/ and customizing the tag list. Edit it freely; never edit the
 * files under App/logging/.
 *
 * Portable modules should NOT include this file -- they can include
 * "logging.h" directly if they need the low-level output functions without
 * inheriting this project's tags.
 *
 * Replaces the former debug_config.h, whose name predated the logging API.
 * Non-logging build options moved to device_config.h.
 */

#ifndef LOGGING_CONFIG_H
#define LOGGING_CONFIG_H

//------------------------------------------------------------------------------
// Knobs that must be set before the module headers are pulled in
//------------------------------------------------------------------------------
// logging.h defaults this via #ifndef, so setting it here (ahead of the include
// below) is what makes this project's choice stick.

#define LOG_WITH_TIMESTAMP              1

#include "logging.h"   // for log_color_t etc. (needed for the _COLOR values below)

//------------------------------------------------------------------------------
// Global verbosity ceiling
//------------------------------------------------------------------------------
// How verbose this build is willing to be. A message class is emitted when its
// tier is at or below this. The ladder (LOG_LEVEL_QUIET .. LOG_LEVEL_DEBUG) and
// what the tiers mean are documented in logging.h.
//
//   LOG_LEVEL_QUIET    nothing at all, including LOG_LEVEL_ALWAYS classes
//   LOG_LEVEL_ERROR    only ALWAYS + ERROR classes
//   LOG_LEVEL_DEBUG    everything
//
// Turn logging off entirely if DEBUG is not defined (via the -DDEBUG compiler
// command line option); otherwise use the project setting below.

#ifndef DEBUG
#define LOG_LEVEL                       LOG_LEVEL_QUIET
#else
// Change this to raise or lower debug logging output for the whole build.
#define LOG_LEVEL                       LOG_LEVEL_DEBUG
#endif

//------------------------------------------------------------------------------
// Message classes and associated output tags/colors for this project
//------------------------------------------------------------------------------
// Each class needs three coordinated defines. Only the un-suffixed name is
// passed to a logging macro; it reaches _TAG and _COLOR itself via the ##
// token-pasting operator:
//
//   LOGCT(LOG_SYSTEM, "value = %d", n);   ->  uses LOG_SYSTEM_TAG / _COLOR
//
// The class value is the verbosity tier at which that class becomes visible.
// LOG_LEVEL_QUIET switches a class off outright, whatever LOG_LEVEL is.
//
// These are deliberately NOT wrapped in a conditional: the macros always
// compile, so the tag symbols must always exist. A logging-off build is
// expressed by LOG_LEVEL above, not by making these disappear.

// Misc/system
#define LOG_SYSTEM                      LOG_LEVEL_DEBUG
#define LOG_SYSTEM_TAG                  "SYSTEM"
#define LOG_SYSTEM_COLOR                LOGC_BRIGHT_MAGENTA

// Job queue activty
#define LOG_JOBS                        LOG_LEVEL_QUIET
#define LOG_JOBS_TAG                    "JOB"
#define LOG_JOBS_COLOR                  LOGC_WHITE

// EXTI interrupt reporting
#define LOG_EXTI                        LOG_LEVEL_QUIET
#define LOG_EXTI_TAG                    "EXTI"
#define LOG_EXTI_COLOR                  LOGC_WHITE

//------------------------------------------------------------------------------
// Pull in the macro sugar (LOGCT, LOG, LOGC, RPRINTF, DPRINTF, ...).
// The class defines above must precede this include.

#include "log_helpers.h"

#endif // LOGGING_CONFIG_H
