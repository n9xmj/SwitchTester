/******************************************************************************
 * nvm_driver_spiflash.c
 *
 * MINIMAL nvmparams storage driver over the SPI-NOR flash (MX25R80.c), for the
 * SwitchTester testbed. See nvm_driver_spiflash.h. This is the throwaway
 * bring-up driver, not the vendorable one:
 *
 *   - single fixed device (the global hspi3 bus handle the MX25R80 driver
 *     defaults to), no p_v_context
 *   - ux_address is a flash byte OFFSET
 *   - write() leans on u8_spiflash_write()'s erase-on-sector-boundary + 256-byte
 *     page splitting, so a whole-pool commit erases its sector then programs it
 *
 * The MX25R80 entry points return HAL status (0 == HAL_OK); anything non-zero
 * is surfaced to nvmparams as NVM_ERROR_DEVICE. The test-support hooks at the
 * bottom mirror the RAM driver so the same HIL harness drives either backend.
 ******************************************************************************/

#include <string.h>

#include "device_config.h"          /* main.h + platform.h: PACKED, HAL, pins --
                                     * MX25R80.h depends on these being in scope */
#include "nvmparams.h"
#include "nvm_driver_spiflash.h"
#include "MX25R80.h"

/*----------------------------------------------------------------------------
 * Fault injection + access counters (see nvm_driver_ram.c for the rationale;
 * identical mechanism, so the HIL suite's error-path tests run on flash too).
 *--------------------------------------------------------------------------*/

static uint32_t    u32_fail_countdown = 0;
static nvm_error_t x_fail_code        = NVM_ERROR_DEVICE;
static uint32_t    u32_read_count     = 0;
static uint32_t    u32_write_count    = 0;

/*----------------------------------------------------------------------------
 * Parameter + bounds validation. The driver checks its own arguments and the
 * physical device extent; it does no integrity checking and knows nothing of
 * wear levelling -- ux_address arrives already resolved to the target offset.
 *--------------------------------------------------------------------------*/

static nvm_error_t x_spiflash_check(const nvm_media_t *p_x_media)
{
    uint32_t u32_offset;

    if ((p_x_media == NULL) || (p_x_media->p_v_data == NULL) || (p_x_media->u32_size == 0u))
    {
        return NVM_ERROR_PARAMETER;
    }

    u32_offset = (uint32_t) p_x_media->ux_address;

    /* Reject an access that runs off the end of the device. */
    if (((uint64_t) u32_offset + (uint64_t) p_x_media->u32_size) > (uint64_t) NVM_SPIFLASH_CAPACITY)
    {
        return NVM_ERROR_PARAMETER;
    }

    return NVM_ERROR_NONE;
}

static nvm_error_t x_spiflash_fault_check(void)
{
    if (u32_fail_countdown == 0u)
    {
        return NVM_ERROR_NONE;
    }

    u32_fail_countdown--;
    if (u32_fail_countdown == 0u)
    {
        return x_fail_code;
    }

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_drv_spiflash_read(*p_x_media)
 ******************************************************************************/

nvm_error_t x_nvm_drv_spiflash_read(const nvm_media_t *p_x_media)
{
    nvm_error_t x_status;
    uint8_t     u8_hal;

    x_status = x_spiflash_check(p_x_media);
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    u32_read_count++;

    x_status = x_spiflash_fault_check();
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    u8_hal = u8_spiflash_read(p_x_media->p_v_data,
                             (uint32_t) p_x_media->ux_address,
                             (uint16_t) p_x_media->u32_size);

    return (u8_hal == 0u) ? NVM_ERROR_NONE : NVM_ERROR_DEVICE;
}

/******************************************************************************
 * nvm_error_t x_nvm_drv_spiflash_write(*p_x_media)
 *
 * u8_spiflash_write() with erase enabled: it erases each 4 KB sector as its
 * write pointer crosses the sector boundary, then page-programs. A whole-pool
 * commit that starts on a sector boundary therefore erases the pool's sector
 * once and programs the pool -- exactly the erase-before-write flash needs.
 ******************************************************************************/

nvm_error_t x_nvm_drv_spiflash_write(const nvm_media_t *p_x_media)
{
    nvm_error_t x_status;
    uint8_t     u8_hal;

    x_status = x_spiflash_check(p_x_media);
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    u32_write_count++;

    x_status = x_spiflash_fault_check();
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    u8_hal = u8_spiflash_write(p_x_media->p_v_data,
                              (uint32_t) p_x_media->ux_address,
                              (uint16_t) p_x_media->u32_size,
                              1u /* erase before write */);

    return (u8_hal == 0u) ? NVM_ERROR_NONE : NVM_ERROR_DEVICE;
}

/******************************************************************************
 * TEST SUPPORT  (mirrors nvm_driver_ram.c; not part of the driver contract)
 ******************************************************************************/

uintptr_t ux_nvm_drv_spiflash_base(void)
{
    return (uintptr_t) NVM_SPIFLASH_BASE;
}

uint32_t u32_nvm_drv_spiflash_size(void)
{
    return NVM_POOL_SIZE_DEFAULT;
}

void v_nvm_drv_spiflash_fail_after(uint32_t u32_accesses, nvm_error_t x_error)
{
    u32_fail_countdown = u32_accesses;
    x_fail_code        = x_error;
}

void v_nvm_drv_spiflash_fail_clear(void)
{
    u32_fail_countdown = 0;
}

void v_nvm_drv_spiflash_counts(uint32_t *p_u32_reads, uint32_t *p_u32_writes)
{
    if (p_u32_reads  != NULL) { *p_u32_reads  = u32_read_count;  }
    if (p_u32_writes != NULL) { *p_u32_writes = u32_write_count; }
}

void v_nvm_drv_spiflash_counts_reset(void)
{
    u32_read_count  = 0;
    u32_write_count = 0;
}

/*----------------------------------------------------------------------------
 * Overwrite the pool's flash region. 0xFF is a bare sector erase (blank media);
 * any other fill erases then programs the pool-sized region with that byte, to
 * forge a corrupt pool for the detection tests. Bypasses the fault/count hooks
 * on purpose -- it is bench scaffolding, not a device access under test.
 *--------------------------------------------------------------------------*/

void v_nvm_drv_spiflash_wipe(uint8_t u8_fill)
{
    if (u8_fill == 0xFFu)
    {
        (void) u8_spiflash_sector_erase(NVM_SPIFLASH_BASE);
        (void) u8_spiflash_write_wait(1000u);
    }
    else
    {
        static uint8_t au8_fill[NVM_POOL_SIZE_DEFAULT];

        memset(au8_fill, u8_fill, sizeof(au8_fill));
        (void) u8_spiflash_write(au8_fill, NVM_SPIFLASH_BASE,
                                 (uint16_t) sizeof(au8_fill), 1u);
    }
}
