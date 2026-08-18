/******************************************************************************
 * nvmparams_internal.h
 *
 * Module internals -- NOT part of the public API.
 *
 * nvmparams.h does NOT include this file. Reaching these functions is a
 * deliberate act: a test harness includes this header on purpose. Application
 * code should not.
 *
 * The unchecked accessors below are the real implementations. The public
 * x_nvm_create() / x_nvm_set() / x_nvm_delete() are thin wrappers that
 * range-check the object ID against the module's reserved range first and
 * reject it with NVM_ERROR_ID_RESERVED. Bench and HIL tests occasionally need
 * to reach past that guard -- for instance to plant a deliberately malformed
 * object -- which is why these are not static.
 *
 * They are declared here rather than left for a test file to extern by hand,
 * because a hand-written declaration that drifts from the definition is a
 * linkage bug no compiler catches: the call simply passes the wrong arguments.
 ******************************************************************************/

#ifndef NVMPARAMS_INTERNAL_H
#define NVMPARAMS_INTERNAL_H

#include "nvmparams.h"

/*----------------------------------------------------------------------------
 * Unchecked accessors. Identical to their public counterparts except that they
 * do not enforce the reserved-ID range.
 *--------------------------------------------------------------------------*/

extern nvm_error_t x_nvm_create_unchecked(nvm_pool_t *p_x_pool, nvm_param_id_t x_id,
                                          uint16_t u16_size, const void *p_v_default);
extern nvm_error_t x_nvm_delete_unchecked(nvm_pool_t *p_x_pool, nvm_param_id_t x_id);
extern nvm_error_t x_nvm_set_unchecked   (nvm_pool_t *p_x_pool, nvm_param_id_t x_id,
                                          const void *p_v_data);

/*----------------------------------------------------------------------------
 * Wear-level block scan.
 *
 * Reads and validates each block's header in ONE pass and answers both
 * questions at once -- which block is live, and which should be written next.
 * A flag-driven "find lowest / find highest" helper would have to scan twice,
 * and on SPI flash that is real time rather than a rounding error.
 *
 * PHASE 1: there is exactly one block, so this reports block 0 for both and
 * does no scanning at all. The wear-levelling implementation replaces the body
 * and nothing above it changes.
 *
 * Selection rules, once there is more than one block:
 *   live  = highest u32_write_count WHOSE DATA CRC ALSO VALIDATES, falling
 *           through to the next candidate on failure. The write count lives in
 *           the header, which the CRC does not cover, so validation is what
 *           makes an unprotected selector safe.
 *   write = lowest write count among valid blocks, ties broken by lowest index.
 *           Invalid blocks are a last resort, not a preference -- a damaged
 *           block has no readable count and would otherwise look like zero and
 *           attract every single commit.
 *--------------------------------------------------------------------------*/

typedef struct
{
    uint8_t  u8_live_block;     /* Highest valid write count; 0 if none valid */
    uint8_t  u8_write_block;    /* Next block to write */
    uint32_t u32_live_count;    /* Live block's write count; 0 if none valid */
    bool     b_any_valid;       /* False: no block holds a valid pool */
}
nvm_block_scan_t;

extern nvm_block_scan_t x_nvm_scan_blocks(nvm_pool_t *p_x_pool);

/*----------------------------------------------------------------------------
 * Point the pool's media descriptor at one wear-level block, resolving the
 * effective device address. Drivers never see a block index -- this is where
 * that translation happens, and it is the only place it happens.
 *--------------------------------------------------------------------------*/

extern void v_nvm_stamp_media(nvm_pool_t *p_x_pool, uint8_t u8_block);

/*----------------------------------------------------------------------------
 * Format one block: zero the RAM pool, plant the end-of-list record, apply the
 * label, and write it to the given block.
 *
 * Takes a block index because first-time initialisation writes EVERY block
 * with a copy of the empty pool -- so that after init no block is ever blank,
 * and "invalid" therefore means "damaged" rather than "not yet used". In phase
 * 1 the caller's loop runs exactly once.
 *--------------------------------------------------------------------------*/

extern nvm_error_t x_nvm_format_block(nvm_pool_t *p_x_pool, uint8_t u8_block,
                                      const char *p_c_label);

#endif /* NVMPARAMS_INTERNAL_H */
