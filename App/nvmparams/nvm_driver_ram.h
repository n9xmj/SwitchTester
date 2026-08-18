/******************************************************************************
 * nvm_driver_ram.h
 *
 * Interface to the RAM-emulated nvmparams storage driver example.
 *
 * NOT PART OF THE nvmparams MODULE. Ships alongside it as
 * nvm_driver_ram.c.example; rename that to .c and add it to your build to use
 * it, or delete both files if you do not want it.
 *
 * The two driver entry points are all nvmparams needs -- point a pool's
 * pfn_read / pfn_write at them and set ux_base_address to
 * ux_nvm_drv_ram_base(). Everything below that is test support and exists only
 * so a host-driven test can make the emulated device misbehave on purpose.
 ******************************************************************************/

#ifndef NVM_DRIVER_RAM_H
#define NVM_DRIVER_RAM_H

#include "nvmparams.h"

/*----------------------------------------------------------------------------
 * The driver proper.
 *--------------------------------------------------------------------------*/

extern nvm_error_t x_nvm_drv_ram_read (const nvm_media_t *p_x_media);
extern nvm_error_t x_nvm_drv_ram_write(const nvm_media_t *p_x_media);

/*----------------------------------------------------------------------------
 * Where the emulated store lives, for a pool's ux_base_address, and how big
 * it is. Reported rather than #defined so a caller cannot get out of step
 * with the driver's own idea of its size.
 *--------------------------------------------------------------------------*/

extern uintptr_t ux_nvm_drv_ram_base(void);
extern uint32_t  u32_nvm_drv_ram_size(void);

/*----------------------------------------------------------------------------
 * Fault injection.
 *
 * v_nvm_drv_ram_fail_after(n, err) makes the n-th subsequent device access
 * fail with <err>; n == 1 fails the very next one. Reads and writes share the
 * countdown, because that is what a failing bus looks like. The fault is
 * one-shot: after it fires, accesses succeed again until re-armed.
 *
 * This is the only practical way to exercise nvmparams' error paths -- a real
 * flash part cannot be asked to fail on cue.
 *--------------------------------------------------------------------------*/

extern void v_nvm_drv_ram_fail_after(uint32_t u32_accesses, nvm_error_t x_error);
extern void v_nvm_drv_ram_fail_clear(void);

/*----------------------------------------------------------------------------
 * Access counters, for asserting that a commit did or did not touch the
 * device -- the difference between "nothing to write" and "wrote nothing".
 *--------------------------------------------------------------------------*/

extern void v_nvm_drv_ram_counts(uint32_t *p_u32_reads, uint32_t *p_u32_writes);
extern void v_nvm_drv_ram_counts_reset(void);

/*----------------------------------------------------------------------------
 * Overwrite the whole store with a fill byte. Use 0xFF or 0x00 to present
 * blank media, or anything else to present garbage that nvmparams should
 * classify as corrupt.
 *--------------------------------------------------------------------------*/

extern void v_nvm_drv_ram_wipe(uint8_t u8_fill);

#endif /* NVM_DRIVER_RAM_H */
