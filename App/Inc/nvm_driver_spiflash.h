/******************************************************************************
 * nvm_driver_spiflash.h
 *
 * MINIMAL nvmparams storage driver over the SPI-NOR flash (MX25R80.c), for the
 * SwitchTester testbed. NOT the vendorable version -- no p_v_context, single
 * fixed device, fixed pool region. Enough to bring a flash-backed pool up and
 * run the nvmparams HIL suite against real silicon.
 *
 * ux_address is read as a byte OFFSET into the flash (not a pointer / not an
 * MCU address). The pool lives at NVM_SPIFLASH_BASE.
 ******************************************************************************/

#ifndef NVM_DRIVER_SPIFLASH_H
#define NVM_DRIVER_SPIFLASH_H

#include "nvmparams.h"

/* Where the test pool lives on the flash, and the sanity bound for accesses.
 * Base is sector-aligned so the driver's erase-before-write covers the pool. */
#ifndef NVM_SPIFLASH_BASE
#define NVM_SPIFLASH_BASE       0x00000000u
#endif
#ifndef NVM_SPIFLASH_CAPACITY
#define NVM_SPIFLASH_CAPACITY   0x01000000u      /* 16 MB (W25Q128) upper bound  */
#endif

/*----------------------------------------------------------------------------
 * The nvmparams driver contract: two function pointers.
 *--------------------------------------------------------------------------*/

extern nvm_error_t x_nvm_drv_spiflash_read(const nvm_media_t *p_x_media);
extern nvm_error_t x_nvm_drv_spiflash_write(const nvm_media_t *p_x_media);

/*----------------------------------------------------------------------------
 * Test support -- not part of the contract. Mirrors the RAM driver so the same
 * HIL harness can drive either backend.
 *--------------------------------------------------------------------------*/

extern uintptr_t ux_nvm_drv_spiflash_base(void);       /* pool base offset      */
extern uint32_t  u32_nvm_drv_spiflash_size(void);      /* pool region size      */
extern void      v_nvm_drv_spiflash_fail_after(uint32_t u32_accesses, nvm_error_t x_error);
extern void      v_nvm_drv_spiflash_fail_clear(void);
extern void      v_nvm_drv_spiflash_counts(uint32_t *p_u32_reads, uint32_t *p_u32_writes);
extern void      v_nvm_drv_spiflash_counts_reset(void);
extern void      v_nvm_drv_spiflash_wipe(uint8_t u8_fill);  /* 0xFF = erase only */

#endif /* NVM_DRIVER_SPIFLASH_H */
