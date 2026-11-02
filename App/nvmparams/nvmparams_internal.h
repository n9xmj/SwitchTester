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
 * Reads every block, validates it (signature, then CRC), and leaves the live
 * block's contents in the RAM pool. One pass answers which block is live and
 * which blocks need repair.
 *
 * Rules (one write count for the whole pool, +1 per commit that writes):
 *   live   = highest u32_write_count among blocks that validate; on a tie the
 *            lowest index (tied blocks are exact copies). The count lives in
 *            the header, which the CRC does not cover, so validation is what
 *            makes the selector safe.
 *   write  = live + 1, modulo the block count. Commits go round in index
 *            order, so the block after the live one is always the oldest.
 *   repair = init rewrites every block that does not validate (blank or
 *            corrupt) as an exact copy of the live block, header included.
 *   none   = no block validates: the caller's policy applies (format all).
 *
 * The reported check, CRC stored and CRC computed are the live block's, or,
 * when none validates, the first non-blank block's (block 0's if all blank).
 *--------------------------------------------------------------------------*/

/** @brief Most wear-level blocks a pool may have (one bit each in the scan mask). */
#define NVM_MAX_WEAR_BLOCKS     32u

typedef struct
{
    uint32_t u32_valid_mask;    /* Bit n set: block n validates */
    uint32_t u32_crc_stored;    /* Reported block's stored CRC */
    uint32_t u32_crc_computed;  /* Reported block's computed CRC; 0 if not computed */
    uint8_t  u8_live_block;     /* Live block (valid only if b_any_valid) */
    uint8_t  u8_check;          /* nvm_pool_check_t of the reported block */
    bool     b_any_valid;       /* False: no block holds a valid pool */
    bool     b_all_blank;       /* True: every block is uniformly erased */
}
nvm_block_scan_t;

extern nvm_error_t x_nvm_scan_blocks(nvm_pool_t *p_x_pool, nvm_block_scan_t *p_x_scan);

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
