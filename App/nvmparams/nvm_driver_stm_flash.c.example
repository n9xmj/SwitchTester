/******************************************************************************
 * nvm_driver_stm_flash.c
 *
 * EXAMPLE -- nvmparams storage driver for STM32 internal flash.
 *
 * ****************************************************************************
 * THIS FILE SHIPS AS nvm_driver_stm_flash.c.example AND IS NOT COMPILED UNTIL
 * YOU RENAME IT. The double extension means no build system's source glob
 * picks it up by accident.
 * ****************************************************************************
 *
 * To use: rename to nvm_driver_stm_flash.c, either in place or (preferred)
 * after copying it into your own source directory, add it to your build, and
 * declare its two functions in your nvmparams_config.h. Then point a pool's
 * pfn_read / pfn_write at them.
 *
 * These example drivers are NOT part of the nvmparams module. Use one as-is,
 * modify it freely, or delete it and write your own anywhere you like. The
 * only thing nvmparams knows about a driver is the pointer you hand to
 * x_nvm_pool_init().
 *
 * ---------------------------------------------------------------------------
 * WHAT A DRIVER OWES, AND WHAT IT DOES NOT
 *
 * Owes:      move u32_size bytes between p_v_data and ux_address; validate its
 *            own parameters; report physical device access errors.
 * Does not:  check data integrity (nvmparams decides what is valid), and know
 *            anything about wear levelling (ux_address is already the final
 *            effective address).
 *
 * Return NVM_ERROR_NONE on success. Negative values are nvmparams' own codes.
 * POSITIVE values are yours to define as device-specific errors -- they reach
 * the caller unchanged, so a HAL status can survive to somewhere useful.
 * ---------------------------------------------------------------------------
 *
 * YOU MUST RESERVE THE FLASH REGION IN YOUR LINKER SCRIPT. See README.md; the
 * short version is a dedicated MEMORY region at the end of flash, a matching
 * (NOLOAD) section, and the main FLASH region shortened by the same amount.
 * Forgetting the last part is the classic error -- the regions overlap and the
 * linker happily places code where the pool will be erased.
 ******************************************************************************/

#include <string.h>

#include "stm32g0xx_hal.h"

#include "nvmparams.h"

/*----------------------------------------------------------------------------
 * STM32 flash writes a 64-BIT DOUBLEWORD at a time -- the hardware computes
 * ECC across all 8 bytes, so there is no narrower write. That is why a
 * uint64_t appears in a module where every other scalar is 32-bit: it is the
 * shape of the peripheral, not an arithmetic type.
 *
 * The granule is family-specific: G0/G4/L4/WB program doublewords, F1/F4
 * program half-words or words, H7 programs 256-bit flash words. Porting this
 * driver to another family means revisiting this constant and the program
 * loop, and nothing else.
 *--------------------------------------------------------------------------*/

#define STM_FLASH_WRITE_GRANULE     8u

/*----------------------------------------------------------------------------
 * Resolve a flash address to its bank and BANK-RELATIVE page number.
 *
 * HAL_FLASHEx_Erase() wants both: Page must be 0..(FLASH_PAGE_NB - 1) WITHIN
 * the selected bank, not an absolute page index. On a dual-bank part an
 * absolute index simply runs off the end of the valid range.
 *
 * Both values are derived from the address and the HAL's own bank size at
 * compile time, so a single-bank and a dual-bank configuration of the same
 * part are both handled without a #define to keep in sync.
 *--------------------------------------------------------------------------*/

static void v_flash_locate(uint32_t u32_address, uint32_t *p_u32_bank, uint32_t *p_u32_page)
{
    uint32_t u32_offset = u32_address - FLASH_BASE;

#if defined(FLASH_BANK_2)
    if (u32_offset >= FLASH_BANK_SIZE)
    {
        *p_u32_bank = FLASH_BANK_2;
        u32_offset -= FLASH_BANK_SIZE;
    }
    else
    {
        *p_u32_bank = FLASH_BANK_1;
    }
#else
    *p_u32_bank = FLASH_BANK_1;
#endif

    *p_u32_page = u32_offset / FLASH_PAGE_SIZE;
}

/******************************************************************************
 * nvm_error_t x_nvm_drv_stm_flash_read(*p_x_media)
 *
 * Internal flash is memory-mapped, so a read is a plain copy and cannot fail.
 ******************************************************************************/

nvm_error_t x_nvm_drv_stm_flash_read(const nvm_media_t *p_x_media)
{
    if ((p_x_media == NULL) || (p_x_media->p_v_data == NULL) || (p_x_media->u32_size == 0u))
    {
        return NVM_ERROR_PARAMETER;
    }

    memcpy(p_x_media->p_v_data, (const void *) p_x_media->ux_address, p_x_media->u32_size);

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_drv_stm_flash_write(*p_x_media)
 *
 * Erase the pages the pool occupies, then program it doubleword by doubleword.
 *
 * Notes:
 * The page count is DERIVED from the transfer size rather than assumed to be
 * one. A pool larger than a single page would otherwise be programmed into
 * flash that was never erased -- which fails silently in the sense that the
 * write "succeeds" and the data is wrong.
 *
 * The tail is padded with 0xFF (the erased value) when the pool size is not a
 * whole number of doublewords, and the source is copied into a local rather
 * than cast to a uint64_t *. Casting would both assume an 8-byte-aligned
 * buffer and read past the end of it on the final iteration.
 ******************************************************************************/

nvm_error_t x_nvm_drv_stm_flash_write(const nvm_media_t *p_x_media)
{
    FLASH_EraseInitTypeDef x_erase = {0};
    const uint8_t *p_u8_source;
    uint32_t u32_address;
    uint32_t u32_written;
    uint32_t u32_page_error = 0;
    uint32_t u32_bank;
    uint32_t u32_page;
    HAL_StatusTypeDef x_hal = HAL_OK;

    if ((p_x_media == NULL) || (p_x_media->p_v_data == NULL) || (p_x_media->u32_size == 0u))
    {
        return NVM_ERROR_PARAMETER;
    }

    p_u8_source = (const uint8_t *) p_x_media->p_v_data;
    u32_address = (uint32_t) p_x_media->ux_address;

    v_flash_locate(u32_address, &u32_bank, &u32_page);

    x_erase.TypeErase = FLASH_TYPEERASE_PAGES;
    x_erase.Banks     = u32_bank;
    x_erase.Page      = u32_page;

    // Round UP: a pool that spills a single byte into a second page still
    // requires that page to be erased.
    x_erase.NbPages   = (p_x_media->u32_size + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;

    do
    {
        x_hal = HAL_FLASH_Unlock();
        if (x_hal != HAL_OK) break;

        x_hal = HAL_FLASHEx_Erase(&x_erase, &u32_page_error);
        if (x_hal != HAL_OK) break;

        for (u32_written = 0; u32_written < p_x_media->u32_size;
             u32_written += STM_FLASH_WRITE_GRANULE)
        {
            uint64_t u64_word = 0xFFFFFFFFFFFFFFFFull;   /* pad with the erased value */
            uint32_t u32_remaining = p_x_media->u32_size - u32_written;
            uint32_t u32_chunk = (u32_remaining >= STM_FLASH_WRITE_GRANULE)
                                 ? STM_FLASH_WRITE_GRANULE : u32_remaining;

            memcpy(&u64_word, &p_u8_source[u32_written], u32_chunk);

            x_hal = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                      u32_address + u32_written, u64_word);
            if (x_hal != HAL_OK) break;
        }
    }
    while (0);

    HAL_FLASH_Lock();

    return (x_hal == HAL_OK) ? NVM_ERROR_NONE : NVM_ERROR_DEVICE;
}
