/**
 * @file    device_config.h
 * @brief   Product options and constant parameter settings
 */
#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug_config.h"
#include "main.h"
#include "macros.h"
#include "globals.h"

//------------------------------------------------------------------------------

#define FIRMWARE_VERSION                "0.0.0.2.0"
#define PRODUCT_NAME                    "GPS Demo"
#define PRODUCT_ID                      "0000"
#define RELEASE_REVISION                "00"
#define PRODUCT_SKU                     "xxxxxx"
#define MAIN_PCB_REVISION               "NUCLEO-G0B1RE"

// Uncomment this line to force continuous motor run on startup
//#define CONTINUOUS_MOTOR_RUN_TEST

#if defined(DEBUG)
#define BUILD_CONFIG                    "DEBUG"
#else 
#define BUILD_CONFIG                    "RELEASE"
#endif

//------------------------------------------------------------------------------
// Misc
//------------------------------------------------------------------------------

#define DEV_CONFIG_NVM_COMMIT_DELAY_MS                                      5000

#endif //DEVICE_CONFIG_H
