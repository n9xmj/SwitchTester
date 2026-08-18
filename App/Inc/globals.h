/******************************************************************************
 * globals.h
 *
 * Application-wide objects that more than one module needs to reach.
 ******************************************************************************/

#ifndef GLOBALS_H
#define GLOBALS_H

#include "nvmparams.h"

/*----------------------------------------------------------------------------
 * The application's parameter pool.
 *
 * OWNED BY THE APPLICATION, not by nvmparams. The module used to pre-declare a
 * default pool of its own; as a vendored module it no longer does, because a
 * library has no business deciding how many pools a project has or what they
 * are called.
 *
 * Instantiated in app_main.c and configured there by v_nvm_init(). Exported
 * here because the pool handle is the first argument to every nvmparams call,
 * so every consumer -- debug_menu, switch_out, the automation console -- needs
 * to see it.
 *
 * Note this declaration cannot live in nvmparams_config.h: that header is
 * included from the MIDDLE of nvmparams.h, before nvm_pool_t is defined.
 *--------------------------------------------------------------------------*/

extern nvm_pool_t g_x_nvm_param;

/*----------------------------------------------------------------------------
 * Meaning this project assigns to the pool's spare application byte.
 *
 * nvmparams reserves u8_user1 for the application and never touches it. Here
 * it records that the flash pool could not be brought up and v_param_init()
 * fell back to a volatile one.
 *
 * Worth flagging explicitly because the null device is transparent by design:
 * x_nvm_commit() reports success and does nothing, so nothing downstream --
 * the debug menu, the automation console, a HIL run -- could otherwise tell
 * that persistence had stopped working.
 *--------------------------------------------------------------------------*/

#define NVM_USER1_VOLATILE_FALLBACK     1u

#define B_NVM_IS_VOLATILE()             (g_x_nvm_param.u8_user1 == NVM_USER1_VOLATILE_FALLBACK)

#endif /* GLOBALS_H */
