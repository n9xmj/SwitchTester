/******************************************************************************
 * nvm_test.h
 *
 * Host-driven test harness for the nvmparams module.
 *
 * ****************************************************************************
 * SwitchTester ONLY. This is NOT part of the vendored nvmparams module and is
 * not back-ported anywhere.
 * ****************************************************************************
 *
 * Skeleton is the minimal base for new projects and must not carry test dead
 * weight; LED_Strip has only a precursor automation console; the mirror has a
 * completely different HIL interface. SwitchTester is the only project with
 * the console, the bench and the host scripts, so the suite lives here and
 * stays here. The upside is that tests cost nothing to add -- there is no
 * three-project migration to pay for each one.
 *
 * Everything here operates on a SEPARATE pool backed by the RAM driver, never
 * on the application's flash pool. That buys three things:
 *
 *   - tests can wipe, corrupt and fault-inject freely with no risk to the
 *     real parameters, and no flash wear from a test run;
 *   - the error paths become reachable at all, since a real flash part cannot
 *     be asked to fail on cue;
 *   - it exercises multi-pool operation, which the module claims to support
 *     and which nothing else in this project uses.
 ******************************************************************************/

#ifndef NVM_TEST_H
#define NVM_TEST_H

#include "nvmparams.h"

/*----------------------------------------------------------------------------
 * The test pool. Separate from g_x_nvm_param in every respect: its own handle,
 * its own RAM-backed store, its own configuration.
 *--------------------------------------------------------------------------*/

extern nvm_pool_t g_x_nvm_test;

/*----------------------------------------------------------------------------
 * Bring the test pool up. Called once during application init. Safe to call
 * when the RAM driver is not present in the build -- it simply does nothing.
 *--------------------------------------------------------------------------*/

extern void v_nvm_test_init(void);

/*----------------------------------------------------------------------------
 * Automation-console handler for the 'N' op. Registered in the command table
 * in automation_commands.c.
 *
 * The op is sub-commanded rather than consuming a dozen single-character op
 * codes:  N,<sub>[,args]
 *
 *   N,I               pool info: signature, write count, dirty flag, geometry
 *   N,L               list objects: id/size pairs
 *   N,C,<id>,<val>    create a uint32 object with default <val>
 *   N,G,<id>          get
 *   N,S,<id>,<val>    set
 *   N,D,<id>          delete
 *   N,K               commit
 *   N,R[,<policy>]    re-init the pool, optionally with a policy 0..2
 *   N,W,<fill>        overwrite the emulated device with <fill>
 *   N,F,<n>[,<err>]   fail the n-th subsequent device access
 *   N,Z               clear a pending fault
 *   N,A               report device read/write counts
 *   N,B               reset device read/write counts
 *   N,T               commit-timer probe: tick and report elapsed state
 *
 * All numeric arguments are hexadecimal, matching the rest of the console.
 *--------------------------------------------------------------------------*/

extern void v_acon_op_nvm_test(char c_op, char *pc_line);

#endif /* NVM_TEST_H */
