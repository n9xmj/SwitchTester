/******************************************************************************
 * nvmparams_config.h
 *
 * SwitchTester's configuration for the vendored nvmparams module.
 *
 * Derived from App/nvmparams/nvmparams_config.h.example -- see that file for
 * the full commentary on each setting. This copy carries only what a reader of
 * THIS project needs to know.
 *
 * DO NOT #include THIS FILE DIRECTLY. nvmparams.h includes it for you, and is
 * the only header the application needs.
 ******************************************************************************/

#ifndef NVMPARAMS_H_INSIDE
#error "Do not include nvmparams_config.h directly -- include nvmparams.h instead."
#endif

#ifndef NVMPARAMS_CONFIG_H
#define NVMPARAMS_CONFIG_H

#include "logging_config.h"     /* LOG_NVM class + LOGCT() */

/*=============================================================================
 * 1. POOL GEOMETRY
 *===========================================================================*/

/* Pool size. One STM32G0 flash page is 2 KB and the whole page is reserved
 * (see NVM_FLASH in STM32G0B1RETX_FLASH.ld), so there is room to grow. */
#define NVM_POOL_SIZE_DEFAULT               0x200

/* Pool header label length. SET ONCE, NEVER CHANGE -- it is part of the
 * on-media layout, and altering it silently misreads every existing pool.
 * Must be a multiple of 4. Keep the default. */
#define NVM_LABEL_MAX_LENGTH                16

/*=============================================================================
 * 2. OPTIONAL FEATURES
 *===========================================================================*/

/* This project lets nvmparams malloc() its pool buffer (the config below
 * passes p_v_ram_buffer = NULL). Set to 0 to compile the allocator out, in
 * which case a NULL buffer becomes a configuration error instead. */
#define NVM_ENABLE_INTERNAL_MALLOC          1

/* Route the module's error reports into this project's logging module. The
 * NVM class is defined in logging_config.h. Arguments must be side-effect
 * free -- they are discarded entirely in a build without logging. */
#define NVM_LOG_ERROR(fmt, ...)             LOGCT(LOG_NVM, fmt, ##__VA_ARGS__)

/*=============================================================================
 * 3. APPLICATION PARAMETER IDs
 *===========================================================================*/

/* Anchored at NVM_ID_APP_FIRST so the module can prove, at compile time, that
 * the list has not run into its reserved range. Add new parameters AT THE END
 * -- inserting in the middle renumbers everything after it and orphans the
 * matching objects in any pool already written. */

typedef enum
{
    /* Factory / manufacturing data. */
    NVM_CONFIG_SERIAL_NUMBER = NVM_ID_APP_FIRST,
    NVM_CONFIG_PRODUCT_ID,
    NVM_CONFIG_SKU,

    /* Switch outputs -- manual pulse width, milliseconds. */
    NVM_PARAM_SWITCH_PULSE_MS,

    /* Switch cycling parameters, three per channel.
     * These MUST remain contiguous and in this order: the ID for a given
     * (channel, parameter) pair is computed arithmetically as
     *     NVM_PARAM_CYCLE_A_REPEAT + (channel * SWITCH_CYCLE_PARAM_COUNT) + parameter
     * See x_switch_cycle_nvm_id() in switch_out.c, which guards the assumption
     * with its own _Static_assert. */
    NVM_PARAM_CYCLE_A_REPEAT,
    NVM_PARAM_CYCLE_A_ON_US,
    NVM_PARAM_CYCLE_A_OFF_US,
    NVM_PARAM_CYCLE_B_REPEAT,
    NVM_PARAM_CYCLE_B_ON_US,
    NVM_PARAM_CYCLE_B_OFF_US,
    NVM_PARAM_CYCLE_C_REPEAT,
    NVM_PARAM_CYCLE_C_ON_US,
    NVM_PARAM_CYCLE_C_OFF_US,
    NVM_PARAM_CYCLE_D_REPEAT,
    NVM_PARAM_CYCLE_D_ON_US,
    NVM_PARAM_CYCLE_D_OFF_US,

    /* Scratch values for test and debug. */
    NVM_PARAM_TEST_1,
    NVM_PARAM_TEST_2,
    NVM_PARAM_TEST_3,

    /* Event production mask -- one uint32_t holding event_control_t::u32_all.
     * Default is 0 (everything disarmed), so a virgin pool boots producing
     * nothing. Restored late in init on purpose; see v_event_control_restore()
     * in app_events.h. */
    NVM_PARAM_EVENT_CONTROL,

    /* Marker -- not a parameter. Keep last. */
    NVM_PARAM_APP_LAST
}
app_nvm_param_t;

_Static_assert(NVM_PARAM_APP_LAST <= NVM_ID_APP_MAX,
               "Too many application NVM parameter IDs -- the list has run into "
               "the range reserved by nvmparams (see NVM_ID_APP_MAX).");

/*=============================================================================
 * 4. STORAGE DRIVERS
 *===========================================================================*/

/* This project uses the STM32 internal-flash example driver, renamed from
 * nvm_driver_stm_flash.c.example. The pool lives in the NVM_FLASH region
 * reserved by the linker script; see globals.h for the pool handle and
 * app_main.c for the configuration literal. */

extern nvm_error_t x_nvm_drv_stm_flash_read (const nvm_media_t *p_x_media);
extern nvm_error_t x_nvm_drv_stm_flash_write(const nvm_media_t *p_x_media);

/* No CRC function is supplied yet, so pools are validated by signature alone
 * -- the legacy behaviour. Enabling one invalidates any pool already written;
 * see the CRC notes in the .example file before doing so. */

#endif /* NVMPARAMS_CONFIG_H */
