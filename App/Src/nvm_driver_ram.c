/******************************************************************************
 * nvm_driver_ram.c
 *
 * EXAMPLE -- nvmparams storage driver emulating non-volatile memory in RAM.
 *
 * ****************************************************************************
 * THIS FILE SHIPS AS nvm_driver_ram.c.example AND IS NOT COMPILED UNTIL YOU
 * RENAME IT. The double extension means no build system's source glob picks
 * it up by accident.
 * ****************************************************************************
 *
 * This is the DEGENERATE CASE -- the smallest driver that can exist, and the
 * one to read first if you are writing your own. Both entry points are a
 * bounds check and a memcpy. Everything else a real driver does -- erase
 * granularity, page programming, busy polling, bus arbitration -- is absent
 * because RAM needs none of it, which leaves the nvmparams contract itself
 * showing plainly.
 *
 * It has two practical uses beyond teaching:
 *
 *   1. Bring-up on hardware whose real storage is not working yet, or is not
 *      fitted. Parameters behave normally; they simply do not survive a reset.
 *   2. TESTING. The fault-injection hooks at the bottom make a storage device
 *      fail on demand, which is the only practical way to exercise the error
 *      paths in nvmparams without a flash part that genuinely misbehaves.
 *
 * Note what ux_address means here. For the STM32 flash driver it is an address
 * in the MCU's memory map; for a SPI or file backend it is a byte offset with
 * no meaning as a pointer. For THIS driver it is literally a pointer to the
 * emulated store, cast back to one -- which is the case that demonstrates why
 * the field is a uintptr_t rather than a uint32_t: it round-trips a real
 * pointer losslessly, on a 32-bit target and on a 64-bit host build alike.
 *
 * There is deliberately no use of p_v_context. This driver needs nothing that
 * ux_address does not already carry.
 ******************************************************************************/

#include <string.h>

#include "nvmparams.h"
#include "nvm_driver_ram.h"

/*----------------------------------------------------------------------------
 * The emulated device.
 *
 * Sized for one pool by default. If you enable wear levelling, this must be at
 * least u8_wear_blocks * the block stride, or the driver's bounds check will
 * reject the higher blocks -- which is the correct failure, but an opaque one
 * if you have forgotten this buffer exists.
 *
 * Marked volatile-free and plainly static: it is ordinary RAM, and pretending
 * otherwise would obscure the point of the example.
 *--------------------------------------------------------------------------*/

#ifndef NVM_RAM_STORE_SIZE
#define NVM_RAM_STORE_SIZE      NVM_POOL_SIZE_DEFAULT
#endif

static uint8_t nvm_ram_store[NVM_RAM_STORE_SIZE];

/*----------------------------------------------------------------------------
 * Fault injection.
 *
 * A file-scope static rather than something reached through p_v_context: there
 * is exactly one emulated device, so a static is simpler and the test harness
 * can drive it directly through the functions at the bottom of this file.
 *
 * u32_fail_countdown == 0 means "no fault pending". Any other value counts
 * down on each access and fails when it reaches zero, so 1 fails the very next
 * access.
 *--------------------------------------------------------------------------*/

static uint32_t    u32_fail_countdown = 0;
static nvm_error_t x_fail_code        = NVM_ERROR_DEVICE;
static uint32_t    u32_read_count     = 0;
static uint32_t    u32_write_count    = 0;

/*----------------------------------------------------------------------------
 * Shared parameter and bounds validation.
 *
 * A driver's job is to check its own parameters and report device access
 * errors -- nothing more. It performs no integrity checking on the data, and
 * knows nothing about wear levelling: ux_address arrives already resolved to
 * the block being accessed.
 *--------------------------------------------------------------------------*/

static nvm_error_t x_ram_check(const nvm_media_t *p_x_media, uint8_t **pp_u8_target)
{
    uint8_t *p_u8_target;

    if ((p_x_media == NULL) || (p_x_media->p_v_data == NULL) || (p_x_media->u32_size == 0u))
    {
        return NVM_ERROR_PARAMETER;
    }

    // ux_address is a pointer to somewhere in the emulated store.
    p_u8_target = (uint8_t *) p_x_media->ux_address;

    if ((p_u8_target < nvm_ram_store) ||
        ((p_u8_target + p_x_media->u32_size) > (nvm_ram_store + NVM_RAM_STORE_SIZE)))
    {
        // Reaching here usually means the pool's base address or size does not
        // match this store, or wear levelling was enabled without enlarging it.
        return NVM_ERROR_PARAMETER;
    }

    *pp_u8_target = p_u8_target;

    return NVM_ERROR_NONE;
}

/*----------------------------------------------------------------------------
 * Consume one fault-injection tick. Returns the injected error, or
 * NVM_ERROR_NONE to proceed.
 *--------------------------------------------------------------------------*/

static nvm_error_t x_ram_fault_check(void)
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
 * nvm_error_t x_nvm_drv_ram_read(*p_x_media)
 ******************************************************************************/

nvm_error_t x_nvm_drv_ram_read(const nvm_media_t *p_x_media)
{
    uint8_t *p_u8_target;
    nvm_error_t x_status;

    x_status = x_ram_check(p_x_media, &p_u8_target);
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    u32_read_count++;

    x_status = x_ram_fault_check();
    if (x_status != NVM_ERROR_NONE)
    {
        // Fail WITHOUT copying, as a real device would on a bus error. The
        // caller must not receive half-transferred data alongside an error.
        return x_status;
    }

    memcpy(p_x_media->p_v_data, p_u8_target, p_x_media->u32_size);

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_drv_ram_write(*p_x_media)
 ******************************************************************************/

nvm_error_t x_nvm_drv_ram_write(const nvm_media_t *p_x_media)
{
    uint8_t *p_u8_target;
    nvm_error_t x_status;

    x_status = x_ram_check(p_x_media, &p_u8_target);
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    u32_write_count++;

    x_status = x_ram_fault_check();
    if (x_status != NVM_ERROR_NONE)
    {
        return x_status;
    }

    memcpy(p_u8_target, p_x_media->p_v_data, p_x_media->u32_size);

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * TEST SUPPORT
 *
 * Not part of the driver contract -- nvmparams neither knows nor cares that
 * these exist. They are here so a host-driven test can make the storage device
 * misbehave deliberately.
 ******************************************************************************/

uintptr_t ux_nvm_drv_ram_base(void)
{
    return (uintptr_t) nvm_ram_store;
}

uint32_t u32_nvm_drv_ram_size(void)
{
    return NVM_RAM_STORE_SIZE;
}

void v_nvm_drv_ram_fail_after(uint32_t u32_accesses, nvm_error_t x_error)
{
    u32_fail_countdown = u32_accesses;
    x_fail_code        = x_error;
}

void v_nvm_drv_ram_fail_clear(void)
{
    u32_fail_countdown = 0;
}

void v_nvm_drv_ram_counts(uint32_t *p_u32_reads, uint32_t *p_u32_writes)
{
    if (p_u32_reads  != NULL) { *p_u32_reads  = u32_read_count;  }
    if (p_u32_writes != NULL) { *p_u32_writes = u32_write_count; }
}

void v_nvm_drv_ram_counts_reset(void)
{
    u32_read_count  = 0;
    u32_write_count = 0;
}

void v_nvm_drv_ram_wipe(uint8_t u8_fill)
{
    memset(nvm_ram_store, u8_fill, NVM_RAM_STORE_SIZE);
}
