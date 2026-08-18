/******************************************************************************
 * nvmparams.c
 *
 * Non-volatile memory parameter management
 ******************************************************************************/

#include <string.h>

#include "nvmparams.h"
#include "nvmparams_internal.h"

// AFTER nvmparams.h, deliberately: NVM_ENABLE_INTERNAL_MALLOC comes from the
// adopter's config header, which nvmparams.h pulls in. Testing it any earlier
// silently reads as 0 and drops the allocator declarations.
#if NVM_ENABLE_INTERNAL_MALLOC
#include <stdlib.h>
#endif

//------------------------------------------------------------------------------

// Round-up (n) to the nearest multiple of 4 that is greater than or equal to (n)
// NVM data objects are always allocated memory in 4-byte / 32-bit chunks
#define ROUNDUP4(n)     (((n) + 3) & 0xFFFC)

// Optional logging. The application defines NVM_LOG_ERROR in its config header
// if it wants the module to report problems; otherwise every log site here
// compiles away. There is deliberately no reference to any logging module.
// NOTE: arguments must be free of side effects -- they are discarded entirely
// when this expands to nothing.
#ifndef NVM_LOG_ERROR
#define NVM_LOG_ERROR(...)      ((void) 0)
#endif

/******************************************************************************
 * v_nvm_stamp_media(*p_x_pool, u8_block)
 *
 * Point the pool's media descriptor at wear-level block <u8_block>, resolving
 * the effective device address.
 *
 * This is the ONLY place a block index becomes an address. Storage drivers are
 * deliberately blind to wear levelling: they receive one address and move
 * bytes. Keeping the arithmetic here means adding wear levelling later changes
 * no driver.
 ******************************************************************************/

void v_nvm_stamp_media(nvm_pool_t *p_x_pool, uint8_t u8_block)
{
    p_x_pool->x_media.ux_address  = p_x_pool->ux_base_address
                                  + ((uintptr_t) p_x_pool->u32_block_stride * u8_block);
    p_x_pool->x_media.u32_size    = p_x_pool->u32_size;
    p_x_pool->x_media.p_v_data    = p_x_pool->p_v_data;
    /* p_v_context is set once at init and never changes. */
}

/******************************************************************************
 * nvm_error_t x_nvm_read(*p_x_pool)
 *
 * Fill the pool's RAM buffer from the storage device, via the application's
 * read driver.
 *
 * Returns:     NVM_ERROR_NONE, or whatever the driver reported. Positive
 *              values are device-specific driver codes and are passed through
 *              unchanged.
 *
 * Notes:
 * A NULL read driver is a VALID configuration, not an error -- it is the null
 * device: the RAM pool works normally and nothing is ever persisted. In that
 * case the buffer is zeroed so the pool reads as blank and formats to defaults.
 * That matters: leaving the buffer untouched would make the outcome depend on
 * where it came from, since a static buffer arrives zeroed from .bss while a
 * malloc()ed one holds indeterminate bytes and would usually classify as
 * CORRUPT instead.
 ******************************************************************************/

nvm_error_t x_nvm_read(nvm_pool_t *p_x_pool)
{
    if ((p_x_pool == NULL) || (p_x_pool->p_v_data == NULL))
    {
        return NVM_ERROR_PARAMETER;
    }

    if (p_x_pool->pfn_read == NULL)
    {
        memset(p_x_pool->p_v_data, 0, p_x_pool->u32_size);
        return NVM_ERROR_NONE;
    }

    return p_x_pool->pfn_read(&p_x_pool->x_media);
}

/******************************************************************************
 * nvm_error_t x_nvm_write(*p_x_pool)
 *
 * Write the pool's RAM buffer out to the storage device, via the application's
 * write driver.
 *
 * Returns:     NVM_ERROR_NONE, or whatever the driver reported.
 *
 * Notes:
 * A NULL write driver reports success without doing anything -- the null
 * device again. Callers cannot distinguish that from a real write, which is
 * the intent: a pool configured not to persist behaves normally in every other
 * respect.
 ******************************************************************************/

nvm_error_t x_nvm_write(nvm_pool_t *p_x_pool)
{
    if ((p_x_pool == NULL) || (p_x_pool->p_v_data == NULL))
    {
        return NVM_ERROR_PARAMETER;
    }

    if (p_x_pool->pfn_write == NULL)
    {
        return NVM_ERROR_NONE;
    }

    return p_x_pool->pfn_write(&p_x_pool->x_media);
}

/******************************************************************************
 * nvm_object_t * p_x_next_nvm_object(p_x_nvm_object)
 *
 * Returns a pointer to the object in the NVM data pool that follows the one
 * pointed to by <p_x_nvm_object>.
 *
 * Returns:
 *   Pointer to next object in the data pool that follows the one pointed to
 *   by p_x_nvm_object
 *
 * p_x_nvm_object   Pointer to the start of an object entry in the NVM data
 *                  pool. This must point to the object metadata/header
 *                  record (nvm_object_t), NOT the data field that follows it.
 *
 * Notes:
 * The NVM data pool is essentially a unidirectional linked list that is
 * organized sequentially. This function assists in the traversal of the list.
 * Each object occupies a variable amount of space, and new objects that are
 * added are always placed in the free space following the last one. Because of
 * this, there is no need to maintain next-object pointers in the list; the
 * pointer to the next object is determined by adding the
 * sizeof(object metadata) + size of object data to the current object pointer.
 ******************************************************************************/

nvm_object_t * p_x_next_nvm_object(nvm_object_t *p_x_nvm_object)
{
    uint8_t *p_u8_temp_ptr = (uint8_t *) p_x_nvm_object;

    p_u8_temp_ptr += sizeof(nvm_object_t);
    p_u8_temp_ptr += ROUNDUP4(p_x_nvm_object->u16_size);

    return (nvm_object_t *) p_u8_temp_ptr;
}

/******************************************************************************
 * nvm_object_t * nvm_search(*p_x_pool, x_id)
 *
 * Search NVM RAM memory pool for object ID <x_id>
 *
 * p_x_pool     Pointer to nvm_pool_t handle; NVM data pool to search
 * x_id         Object ID to locate
 *
 * Returns:     Pointer to the <x_id> object requested, or
 *              Pointer to the end record if the <x_id> object was not found.
 *              Will return NULL if the search reaches past the end of the
 *              memory pool without having found either the <x_id> object or
 *              end record (e.g. if memory pool is corrupted or improperly
 *              formatted).
 *
 * Notes:
 * This function is available to use by application code for testing and
 * debugging purposes. Generally speaking, this function is intended for use
 * mainly by API code.
 * It not recommended to directly access objects in the NVM pool using pointers
 * returned by this routine, as the storage location for a given object can
 * change as objects are created, set or deleted.
 ******************************************************************************/

nvm_object_t * p_x_nvm_search(nvm_pool_t *p_x_pool, nvm_param_id_t x_id)
{
    nvm_object_t *p_x_object;
    uint8_t *p_u8_end_of_pool;

    // Calculate end address of memory pool (for overflow check)
    p_u8_end_of_pool = p_x_pool->p_v_data;
    p_u8_end_of_pool += p_x_pool->u32_size;

    // Point to first object in NVM RAM data pool
    p_x_object = (nvm_object_t *) ((uint8_t *) p_x_pool->p_v_data + sizeof(nvm_header_t));

    // Find location of data object <x_id>
    do
    {
        // Overflow/runaway protection
        // Stop search if next object pointer is outside the designated
        // RAM pool
        if ((uint8_t *) p_x_object >= p_u8_end_of_pool)
        {
            return NULL;
        }

        // Stop search if requested object or end record found
        if ( (p_x_object->x_id == x_id) ||
             (p_x_object->x_id == NVM_PARAM_END_OF_DATA) )
        {
            break;
        }

        // Point to next object in data pool
        p_x_object = p_x_next_nvm_object(p_x_object);
    }
    while (1);

    return p_x_object;
}

/******************************************************************************
 * nvm_error_t x_nvm_commit(p_x_pool)
 *
 * Write out the contents of the NVM pool RAM buffer to its associated
 * non-volatile storage device.
 *
 * p_x_pool     NVM pool to commit (write out) to the physical non-volatile
 *              storage device.
 *
 * Returns:     Error code or NVM_ERROR_NONE if successful
 *
 * Notes:
 * The physical commit/write is done only if the pool's content-modified flag
 * is set. Committing an unmodified pool will NOT result in an error return;
 * in this case, the data pool will not be written out but the error code
 * returned will still be NVM_ERROR_NONE.
 ******************************************************************************/

nvm_error_t x_nvm_commit(nvm_pool_t *p_x_pool)
{
    nvm_error_t x_status;

    if ( (p_x_pool == NULL) ||
         (p_x_pool->p_v_data == NULL) ||
         (p_x_pool->u32_size < (sizeof(nvm_header_t) + sizeof(nvm_object_t))) )
    {
        return NVM_ERROR_PARAMETER;
    }

    if (p_x_pool->u8_need_commit == 0)
    {
        // Nothing was modified since the last commit, so no flash write is
        // performed. Reported distinctly from NVM_ERROR_NONE so callers can
        // tell "wrote it" from "nothing to write" -- the pool's whole purpose
        // is minimising erase/write cycles, and that is only visible if the
        // two outcomes are distinguishable.
        p_x_pool->u16_commit_timer = 0;
        return NVM_ERROR_NO_CHANGE;
    }

    nvm_header_t *p_x_nvm_header = (nvm_header_t *) p_x_pool->p_v_data;
    uint8_t *p_u8_data_start = (uint8_t *) (p_x_pool->p_v_data) + sizeof(nvm_header_t);
    p_x_nvm_header->u32_signature = NVM_DATA_SIGNATURE;

    // Monotonic, and never reset. Wear levelling selects the live block by
    // highest write count, so this must keep counting across reformats.
    p_x_nvm_header->u32_write_count++;

    // The CRC function is the application's to supply. With none, the field
    // carries a fixed placeholder and validation is by signature alone --
    // the legacy behaviour, bit-identical to pools already in the field.
    if (p_x_pool->pfn_crc != NULL)
    {
        p_x_nvm_header->u32_crc = p_x_pool->pfn_crc(p_u8_data_start,
                                        p_x_pool->u32_size - sizeof(nvm_header_t));
    }
    else
    {
        p_x_nvm_header->u32_crc = NVM_CRC_PLACEHOLDER;
    }

    x_status = x_nvm_write(p_x_pool);

    if (x_status == NVM_ERROR_NONE)
    {
        // Write successful, clear the modified (commit-needed) flag
        p_x_pool->u8_need_commit = 0;
        p_x_pool->u16_commit_timer = 0;
    }
    else
    {
        // If write failed, un-do increment of write count
        p_x_nvm_header->u32_write_count--;
    }

    return x_status;
}

/******************************************************************************
 * v_nvm_release_buffer(*p_x_pool)
 *
 * Give back the pool's RAM buffer if this module allocated it, and mark the
 * pool unusable.
 *
 * Notes:
 * Setting p_v_data to NULL is what makes a failed init safe: every other API
 * call checks it and returns NVM_ERROR_PARAMETER, so a caller that ignores
 * init's result gets errors rather than operating on a pool that was never
 * loaded.
 ******************************************************************************/

static void v_nvm_release_buffer(nvm_pool_t *p_x_pool)
{
#if NVM_ENABLE_INTERNAL_MALLOC
    if (p_x_pool->u8_internal_malloc)
    {
        free(p_x_pool->p_v_data);
        p_x_pool->u8_internal_malloc = 0;
    }
#endif
    p_x_pool->p_v_data = NULL;
}

/******************************************************************************
 * nvm_error_t x_nvm_format_block(*p_x_pool, u8_block, *p_c_label)
 *
 * Reset the RAM pool to an empty, well-formed state and write it to one
 * wear-level block.
 *
 * p_x_pool     Pool handle, already configured by x_nvm_pool_init()
 * u8_block     Wear-level block index to write
 * p_c_label    Pool label, or NULL to leave it blank
 *
 * Returns:     NVM_ERROR_NONE, or the driver's error
 *
 * Notes:
 * Takes a block index because first-time initialisation writes EVERY block
 * with a copy of the empty pool. That way no block is ever blank after init,
 * so a block that later fails to validate means "damaged" rather than "never
 * used" -- and those two want opposite responses when choosing where to write.
 * With wear levelling disabled the caller's loop simply runs once.
 ******************************************************************************/

nvm_error_t x_nvm_format_block(nvm_pool_t *p_x_pool, uint8_t u8_block,
                               const char *p_c_label)
{
    nvm_header_t *p_x_header = (nvm_header_t *) p_x_pool->p_v_data;
    uint8_t      *p_u8_data_start = (uint8_t *) p_x_pool->p_v_data + sizeof(nvm_header_t);
    nvm_object_t *p_x_object;

    // Empty pool: zeroed, with an end-of-list record at the first object slot
    memset(p_x_pool->p_v_data, 0, p_x_pool->u32_size);

    p_x_object = (nvm_object_t *) p_u8_data_start;
    p_x_object->x_id = NVM_PARAM_END_OF_DATA;
    p_x_object->u16_size = 0;

    if (p_c_label != NULL)
    {
        strncpy(p_x_header->c_label, p_c_label, NVM_LABEL_MAX_LENGTH);
    }

    // x_nvm_commit() fills in the signature, write count and CRC, so mark the
    // pool dirty and target the requested block.
    p_x_pool->u8_need_commit = 1;
    v_nvm_stamp_media(p_x_pool, u8_block);

    return x_nvm_commit(p_x_pool);
}

/******************************************************************************
 * nvm_block_scan_t x_nvm_scan_blocks(*p_x_pool)
 *
 * Decide which wear-level block holds the live pool, and which should be
 * written next.
 *
 * PHASE 1 IMPLEMENTATION: there is exactly one block, so this reports block 0
 * for both and performs no device access at all. The pool's RAM buffer has
 * already been filled by x_nvm_read(), so validity is judged from that.
 *
 * The wear-levelling implementation replaces this body and nothing that calls
 * it changes. See nvmparams_internal.h for the selection rules it must follow.
 ******************************************************************************/

nvm_block_scan_t x_nvm_scan_blocks(nvm_pool_t *p_x_pool)
{
    nvm_block_scan_t x_scan = { 0, 0, 0, false };
    const nvm_header_t *p_x_header = (const nvm_header_t *) p_x_pool->p_v_data;

    if (p_x_header->u32_signature == NVM_DATA_SIGNATURE)
    {
        x_scan.b_any_valid   = true;
        x_scan.u32_live_count = p_x_header->u32_write_count;
    }

    return x_scan;
}

/******************************************************************************
 * b_nvm_pool_is_blank(*p_x_pool)
 *
 * Report whether the RAM pool is uniformly erased, i.e. the media has never
 * been written.
 *
 * Notes:
 * Both erase polarities in use are accepted -- 0xFF for NOR flash, 0x00 for
 * some EEPROMs and for a freshly created file. No configuration is needed and
 * there is no harmful false positive available: NVM_DATA_SIGNATURE is
 * 0x5AA5A55A, so "uniformly blank" and "carries a valid signature" cannot both
 * be true. The only pool this could misjudge is a corrupt one that happens to
 * be uniformly blank, which is indistinguishable from never-written by
 * definition -- so calling it blank is the correct answer anyway.
 ******************************************************************************/

static bool b_nvm_pool_is_blank(const nvm_pool_t *p_x_pool)
{
    const uint8_t *p_u8 = (const uint8_t *) p_x_pool->p_v_data;
    uint8_t u8_first = p_u8[0];
    uint32_t u32_index;

    if ((u8_first != 0x00u) && (u8_first != 0xFFu))
    {
        return false;
    }

    for (u32_index = 1; u32_index < p_x_pool->u32_size; u32_index++)
    {
        if (p_u8[u32_index] != u8_first)
        {
            return false;
        }
    }

    return true;
}

/******************************************************************************
 * nvm_error_t x_nvm_pool_init(*p_x_pool, *p_x_config)
 *
 * Initialise a NVM data pool and load its contents from the storage device.
 *
 * p_x_pool     Pointer to an (uninitialised) nvm_pool_t; the pool's handle.
 *              The caller allocates it, typically statically, but does not
 *              populate its members.
 * p_x_config   Pool configuration. May be a ROM constant: this function never
 *              writes to it, and does not retain the pointer -- it copies what
 *              the pool needs.
 *
 * Returns:
 *   NVM_ERROR_NONE                 pool loaded from valid media
 *   NVM_ERROR_POOL_FORMATTED       media was blank; pool formatted, NOTHING LOST
 *   NVM_ERROR_POOL_REFORMATTED     media was corrupt; pool reformatted, DATA DESTROYED
 *   NVM_ERROR_POOL_CORRUPT         media was corrupt and the policy forbids reformatting
 *   NVM_ERROR_PARAMETER            malformed configuration
 *   NVM_ERROR_MEMORY               internal allocation failed
 *   anything else                  reported by the read driver, passed through
 *
 * Test the result against NVM_ERROR_NONE. Never test "< 0", and never test
 * against a list of known codes: a driver may return a positive
 * device-specific value this module has never heard of.
 *
 * Notes:
 * A pool with no read/write driver is a VALID configuration -- the RAM pool
 * behaves normally and nothing is persisted.
 *
 * If the read driver reports an error, this function ALWAYS fails, whatever
 * the policy says. Writing to a device that could not be read is how a
 * transient fault -- a loose bus line, a device not yet powered -- becomes
 * permanent data loss.
 ******************************************************************************/

nvm_error_t x_nvm_pool_init(nvm_pool_t *p_x_pool, const nvm_pool_config_t *p_x_config)
{
    nvm_error_t x_status;
    nvm_block_scan_t x_scan;
    uint8_t u8_blocks;
    uint8_t u8_block;

    if ((p_x_pool == NULL) || (p_x_config == NULL))
    {
        return NVM_ERROR_PARAMETER;
    }

    memset(p_x_pool, 0, sizeof(nvm_pool_t));

    //--------------------------------------------------------------------------
    // Validate the configuration.
    //
    // Pool geometry is now the adopter's to supply rather than compiled in, so
    // init is the only place these can be caught. Per the project's guard
    // policy this path is host/automated-reachable, so it REJECTS rather than
    // clamping.
    //--------------------------------------------------------------------------

    if ((p_x_config->u32_size < NVM_POOL_SIZE_MIN) ||
        ((p_x_config->u32_size % 4u) != 0u))
    {
        NVM_LOG_ERROR("pool size %lu invalid (min %lu, multiple of 4)",
                      (unsigned long) p_x_config->u32_size,
                      (unsigned long) NVM_POOL_SIZE_MIN);
        return NVM_ERROR_PARAMETER;
    }

    u8_blocks = (p_x_config->u8_wear_blocks == 0u) ? 1u : p_x_config->u8_wear_blocks;

    // Wear levelling with no allocation unit would have the module pack blocks
    // with no knowledge of the device's erase granularity, so that adjacent
    // blocks could share an erase unit and writing one would destroy another.
    // The VALUE cannot be validated against a device the module knows nothing
    // about, but this COMBINATION is unambiguously wrong. With a single block
    // the stride is never used, so a zero allocation unit is harmless there.
    if ((u8_blocks > 1u) && (p_x_config->u32_alloc_unit == 0u))
    {
        NVM_LOG_ERROR("wear levelling needs a non-zero allocation unit");
        return NVM_ERROR_PARAMETER;
    }

#if !NVM_ENABLE_INTERNAL_MALLOC
    // Internal allocation is compiled out, so a NULL buffer is a configuration
    // error rather than a request to allocate one.
    if (p_x_config->p_v_ram_buffer == NULL)
    {
        NVM_LOG_ERROR("no RAM buffer supplied and internal malloc is disabled");
        return NVM_ERROR_PARAMETER;
    }
#endif

    //--------------------------------------------------------------------------
    // Copy the configuration into the pool.
    //
    // Copied rather than referenced: the media descriptor is restamped before
    // every driver call, so this state has to be writable regardless. Copying
    // the rest costs a few bytes and means the caller's config may equally be
    // a ROM constant or a stack temporary.
    //--------------------------------------------------------------------------

    p_x_pool->u32_size        = p_x_config->u32_size;
    p_x_pool->ux_base_address = p_x_config->ux_base_address;
    p_x_pool->u32_alloc_unit  = p_x_config->u32_alloc_unit;
    p_x_pool->u8_wear_blocks  = u8_blocks;
    p_x_pool->pfn_read        = p_x_config->pfn_read;
    p_x_pool->pfn_write       = p_x_config->pfn_write;
    p_x_pool->pfn_crc         = p_x_config->pfn_crc;
    p_x_pool->x_init_policy   = p_x_config->x_init_policy;
    p_x_pool->x_media.p_v_context = p_x_config->p_v_context;

    // Blocks are spaced by the pool size rounded up to a whole number of
    // allocation units, so two blocks can never share an erase unit. With a
    // zero allocation unit the stride is just the pool size -- correct for the
    // single-block case, which the check above is what makes safe.
    if (p_x_config->u32_alloc_unit > 1u)
    {
        p_x_pool->u32_block_stride =
            ((p_x_pool->u32_size + p_x_config->u32_alloc_unit - 1u) /
              p_x_config->u32_alloc_unit) * p_x_config->u32_alloc_unit;
    }
    else
    {
        p_x_pool->u32_block_stride = p_x_pool->u32_size;
    }

    //--------------------------------------------------------------------------
    // Attach the RAM buffer.
    //--------------------------------------------------------------------------

    if (p_x_config->p_v_ram_buffer != NULL)
    {
        p_x_pool->p_v_data = p_x_config->p_v_ram_buffer;
    }
#if NVM_ENABLE_INTERNAL_MALLOC
    else
    {
        p_x_pool->p_v_data = malloc(p_x_pool->u32_size);
        if (p_x_pool->p_v_data == NULL)
        {
            NVM_LOG_ERROR("could not allocate %lu byte pool",
                          (unsigned long) p_x_pool->u32_size);
            return NVM_ERROR_MEMORY;
        }
        p_x_pool->u8_internal_malloc = 1;
    }
#endif

    //--------------------------------------------------------------------------
    // Load the pool.
    //--------------------------------------------------------------------------

    v_nvm_stamp_media(p_x_pool, 0);

    x_status = x_nvm_read(p_x_pool);
    if (x_status != NVM_ERROR_NONE)
    {
        // The device could not be read, so nothing is known about its contents.
        // Do NOT format: that would destroy data over a fault that may well be
        // transient. Fail, and leave the pool unusable.
        NVM_LOG_ERROR("read driver failed, status %d", (int) x_status);
        v_nvm_release_buffer(p_x_pool);
        return x_status;
    }

    x_scan = x_nvm_scan_blocks(p_x_pool);

    if (x_scan.b_any_valid)
    {
        // Valid signature. Verify the CRC too, if the application supplied a
        // CRC function; with none, validation is by signature alone and the
        // stored field carries NVM_CRC_PLACEHOLDER. Do NOT compare against the
        // placeholder here -- a pool written by a CRC-enabled build and read by
        // one without would fail for no good reason, breaking rollback.
        if (p_x_pool->pfn_crc != NULL)
        {
            const nvm_header_t *p_x_header = (const nvm_header_t *) p_x_pool->p_v_data;
            uint8_t *p_u8_data_start = (uint8_t *) p_x_pool->p_v_data + sizeof(nvm_header_t);
            uint32_t u32_crc = p_x_pool->pfn_crc(p_u8_data_start,
                                        p_x_pool->u32_size - sizeof(nvm_header_t));

            if (u32_crc != p_x_header->u32_crc)
            {
                x_scan.b_any_valid = false;
            }
        }
    }

    if (x_scan.b_any_valid)
    {
        return NVM_ERROR_NONE;
    }

    //--------------------------------------------------------------------------
    // No valid pool. Classify the media and apply the caller's policy.
    //--------------------------------------------------------------------------

    if (p_x_pool->x_init_policy == NVM_INIT_REQUIRE_VALID)
    {
        // This pool was supposed to have been provisioned already -- factory
        // calibration, a serial number written on the line. Manufacturing
        // defaults here would hide a production fault, so never write.
        NVM_LOG_ERROR("pool not valid and policy requires a provisioned pool");
        v_nvm_release_buffer(p_x_pool);
        return NVM_ERROR_POOL_CORRUPT;
    }

    if (!b_nvm_pool_is_blank(p_x_pool))
    {
        // Corrupt, not blank: there is something here and it is not ours.
        if (p_x_pool->x_init_policy != NVM_INIT_FORMAT_IF_INVALID)
        {
            NVM_LOG_ERROR("pool corrupt; policy forbids reformatting");
            v_nvm_release_buffer(p_x_pool);
            return NVM_ERROR_POOL_CORRUPT;
        }
        x_status = NVM_ERROR_POOL_REFORMATTED;
    }
    else
    {
        x_status = NVM_ERROR_POOL_FORMATTED;
    }

    //--------------------------------------------------------------------------
    // Format. EVERY block is written, so that no block is left blank and a
    // block that later fails to validate can be read as damaged rather than
    // merely unused. With wear levelling off this loop runs once.
    //--------------------------------------------------------------------------

    for (u8_block = 0; u8_block < p_x_pool->u8_wear_blocks; u8_block++)
    {
        nvm_error_t x_format_status = x_nvm_format_block(p_x_pool, u8_block,
                                                         p_x_config->p_c_label);
        if (x_format_status != NVM_ERROR_NONE)
        {
            NVM_LOG_ERROR("format of block %u failed, status %d",
                          (unsigned) u8_block, (int) x_format_status);
            v_nvm_release_buffer(p_x_pool);
            return x_format_status;
        }
    }

    // Leave the media descriptor pointing at the live block.
    v_nvm_stamp_media(p_x_pool, 0);

    return x_status;
}

/******************************************************************************
 *
 ******************************************************************************/

nvm_error_t x_nvm_pool_release(nvm_pool_t *p_x_pool)
{
    nvm_error_t x_status;

    if ( (p_x_pool == NULL) || (p_x_pool->p_v_data == NULL) )
    {
        return NVM_ERROR_PARAMETER;
    }

    // Check for pending commit before closing out pool
    x_status = x_nvm_commit(p_x_pool);

    // Hand back the buffer if x_nvm_pool_init() allocated it. Compiled out
    // entirely when internal allocation is disabled, so no-heap projects link
    // no allocator at all.
    v_nvm_release_buffer(p_x_pool);

    // Clear the pool data, marking this pool as uninitialized
    memset(p_x_pool, 0, sizeof(nvm_pool_t));

    return x_status;
}

/******************************************************************************
 * nvm_error_t x_nvm_create(*p_x_pool, x_id, u16_size, *p_v_default)
 *
 * Add a new parameter (object) to the NVM data pool if it does not
 * already exist.
 *
 * Parameters:
 * *p_x_pool    Pointer to NVM pool handle
 * x_id         ID to assign to created object
 * u16_size     Size of data pointed to by <p_v_default> in bytes
 * p_v_default  Points to value/data to be stored
 *              This data will only be stored IF the <x_id> object is not
 *              already present in the memory pool.
 *              Use x_nvm_set() to modify the value of an existing object.
 *
 * Returns:     NVM_ERROR_NONE if new object added successfully
 *              NVM_ERROR_OBJECT_EXISTS if the object already exists in the
 *                memory pool. This is not really an error, just an indication
 *                that a new object was not created and the existing object
 *                value was not changed.
 *              NVM_ERROR_POOL_CORRUPT if the p_x_pool is corrupt or has not
 *                been initialized
 *              NVM_ERROR_MEMORY if the new object could not be created due to
 *                lack of free memory pool space
 ******************************************************************************/

nvm_error_t x_nvm_create_unchecked(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, uint16_t u16_size, const void *p_v_default)
{
    nvm_object_t *p_x_nvm_object;
    nvm_object_t *p_x_end_object;
    uint16_t u16_allocated_size;

    if ( (p_x_pool == NULL) || (p_x_pool->p_v_data == NULL) )
    {
        return NVM_ERROR_PARAMETER;
    }

    p_x_nvm_object = p_x_nvm_search(p_x_pool, x_id);
    if (p_x_nvm_object == NULL)
    {
        return NVM_ERROR_POOL_CORRUPT;
    }

    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        // Ensure that new object will fit in available memory
        u16_allocated_size = sizeof(nvm_object_t) + ROUNDUP4(u16_size);
        p_x_end_object = (nvm_object_t *) ((uint8_t *) p_x_nvm_object + u16_allocated_size);
        if ( ((uint8_t *) p_x_end_object + sizeof(nvm_object_t)) >
             ((uint8_t *) p_x_pool->p_v_data + p_x_pool->u32_size) )
        {
            return NVM_ERROR_MEMORY;
        }

        // Add new parameter/object to RAM data pool
        memset(p_x_nvm_object, 0, u16_allocated_size);
        p_x_nvm_object->x_id = x_id;
        p_x_nvm_object->u16_size = u16_size;
        memcpy(p_x_nvm_object->u8_data, p_v_default, u16_size);

        // Add end record
        p_x_end_object->x_id = NVM_PARAM_END_OF_DATA;
        p_x_end_object->u16_size = 0;

        p_x_pool->u8_need_commit = 1;
        p_x_pool->u16_commit_timer = 0;
    }

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_delete(*p_x_pool, x_id)
 *
 * Remove (delete) an object from the NVM pool
 *
 * Parameters:
 * *p_x_pool    Pointer to NVM pool handle
 * x_id         ID of object to remove from the NVM pool
 *
 * Returns:     NVM_ERROR_OBJECT_NOT_FOUND if the object ID was not found
 *              NVM_ERROR_POOL_CORRUPT if the NVM pool is corrupted or not init'd
 *              NVM_ERROR_NONE if the object was deleted successfully
 *
 * Note:
 * Deleting a NVM object can, and most likely will, cause the position
 * (address of) of the remaining objects stored in the memory pool to change.
 * Application code should always work with copies of objects that are retrieved
 * using x_nvm_get() and updated using x_nvm_set(). Direct access of data stored
 * in the NVM RAM memory pool is not recommended.
 ******************************************************************************/

nvm_error_t x_nvm_delete_unchecked(nvm_pool_t *p_x_pool, nvm_param_id_t x_id)
{
    nvm_object_t *p_x_nvm_object;

    if ( (p_x_pool == NULL) || (p_x_pool->p_v_data == NULL) )
    {
        return NVM_ERROR_PARAMETER;
    }

    p_x_nvm_object = p_x_nvm_search(p_x_pool, x_id);
    if (p_x_nvm_object == NULL)
    {
        return NVM_ERROR_POOL_CORRUPT;
    }
    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        return NVM_ERROR_OBJECT_NOT_FOUND;
    }

    nvm_object_t *p_x_end_object = p_x_nvm_search(p_x_pool, NVM_PARAM_END_OF_DATA);
    if (p_x_nvm_object == NULL)
    {
        return NVM_ERROR_POOL_CORRUPT;
    }

    // Remove object from memory pool
    // Move all objects below it up to fill in space freed

    nvm_object_t *p_x_move_start = p_x_next_nvm_object(p_x_nvm_object);
    uint32_t u32_move_size = (uint32_t) p_x_end_object - (uint32_t) p_x_move_start + sizeof(nvm_object_t);
    memmove(p_x_nvm_object, p_x_move_start, u32_move_size);

    p_x_pool->u8_need_commit = 1;
    p_x_pool->u16_commit_timer = 0;

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_get(*p_x_pool, x_id, *p_v_data)
 *
 * GET the value of a NVM parameter/object
 * Search the NVM RAM memory pool for the object identified by <x_id> and copy
 * its data into the buffer (variable) pointed to by p_v_data
 *
 * Parameters:
 * *p_x_pool    Pointer to NVM pool handle
 * x_id         ID of object to fetch from the NVM pool
 * *p_v_data    The data content of the object will be copied to the buffer
 *              (variable) pointed to by this.
 *              The existing content of this buffer will not be affected if
 *              the <x_id> object was not found.
 *
 * Returns:     NVM_ERROR_OBJECT_NOT_FOUND if the object ID was not found
 *              NVM_ERROR_POOL_CORRUPT if the NVM pool is corrupted or not init'd
 *              NVM_ERROR_NONE if the object was found and copied successfully
 ******************************************************************************/

nvm_error_t x_nvm_get(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, void *p_v_data)
{
    nvm_object_t *p_x_nvm_object;

    if ( (p_x_pool == NULL) || (p_x_pool->p_v_data == NULL) )
    {
        return NVM_ERROR_PARAMETER;
    }

    p_x_nvm_object = p_x_nvm_search(p_x_pool, x_id);
    if (p_x_nvm_object == NULL)
    {
        return NVM_ERROR_POOL_CORRUPT;
    }
    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        return NVM_ERROR_OBJECT_NOT_FOUND;
    }

    memcpy(p_v_data, p_x_nvm_object->u8_data, p_x_nvm_object->u16_size);

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_get_size(*p_x_pool, x_id, *p_v_data, *p_u16_size)
 *
 * Check for the existence of a NVM object, and get its stored size
 *
 * Parameters:
 * *p_x_pool    Pointer to NVM pool handle
 * x_id         ID of object to fetch from the NVM pool
 * *p_u16_size  Pointer to a uint16_t variable where the object's stored size
 *              will be saved. Can be NULL if size information is not needed.
 *
 * Returns:     NVM_ERROR_OBJECT_NOT_FOUND if the object ID was not found
 *              NVM_ERROR_POOL_CORRUPT if the NVM pool is corrupted or not init'd
 *              NVM_ERROR_NONE if the object was found
 ******************************************************************************/

nvm_error_t x_nvm_get_size(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, uint16_t *p_u16_size)
{
    nvm_object_t *p_x_nvm_object;

    if ( (p_x_pool == NULL) || (p_x_pool->p_v_data == NULL) )
    {
        return NVM_ERROR_PARAMETER;
    }

    p_x_nvm_object = p_x_nvm_search(p_x_pool, x_id);
    if (p_x_nvm_object == NULL)
    {
        return NVM_ERROR_POOL_CORRUPT;
    }
    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        return NVM_ERROR_OBJECT_NOT_FOUND;
    }

    if (p_u16_size != NULL)
    {
        *p_u16_size = p_x_nvm_object->u16_size;
    }

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * nvm_error_t x_nvm_set(*p_x_pool, x_id, *p_v_data)
 *
 * SET the value of a NVM parameter/object
 * Search the NVM RAM memory pool for the object identified by <x_id> and copy
 * the data pointed to by <p_v_data> into the RAM buffer object's data space;
 * i.e. change the object's value.
 *
 * Parameters:
 * *p_x_pool    Pointer to NVM pool handle
 * x_id         ID of object to set the value of
 * *p_v_data    The data pointed to by this will be copied into the RAM
 *              buffer, overwriting the prior value. The amount/size of
 *              the copy is determined by the object's previously set size
 *              which is set when the object is created.
 *
 * Returns:     NVM_ERROR_OBJECT_NOT_FOUND if the object ID was not found
 *              NVM_ERROR_POOL_CORRUPT if the NVM pool is corrupted or not init'd
 *              NVM_ERROR_NONE if the object was found and set successfully
 ******************************************************************************/

nvm_error_t x_nvm_set_unchecked(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, const void *p_v_data)
{
    nvm_object_t *p_x_nvm_object;

    if ( (p_x_pool == NULL) || (p_x_pool->p_v_data == NULL) )
    {
        return NVM_ERROR_PARAMETER;
    }

    p_x_nvm_object = p_x_nvm_search(p_x_pool, x_id);
    if (p_x_nvm_object == NULL)
    {
        return NVM_ERROR_POOL_CORRUPT;
    }
    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        return NVM_ERROR_OBJECT_NOT_FOUND;
    }

    // Check if value in NVM data pool is different from the value passed
    // If it is not, there is no need to copy the new data in or set the
    // need-commit flag

    int i_cmp = memcmp(p_x_nvm_object->u8_data, p_v_data, p_x_nvm_object->u16_size);
    if (i_cmp != 0)
    {
        memcpy(p_x_nvm_object->u8_data, p_v_data, p_x_nvm_object->u16_size);
        p_x_pool->u8_need_commit = 1;
        p_x_pool->u16_commit_timer = 0;
    }

    return NVM_ERROR_NONE;
}

/******************************************************************************
 * Public write-side accessors.
 *
 * These are thin wrappers over the unchecked implementations above. Their only
 * job is to keep application code out of the ID range this module reserves for
 * itself.
 *
 * The check is at RUNTIME rather than compile time on purpose. A static assert
 * can only see IDs the adopter DECLARED; it is blind to computed ones -- and
 * IDs are routinely computed, e.g.
 *     NVM_PARAM_CYCLE_A_REPEAT + (channel * COUNT) + parameter
 * where an out-of-range channel lands wherever it lands. The wrapper catches
 * every actual access, including that one. The anchored-enum static assert in
 * the adopter's config header covers the declaration side; neither mechanism
 * does the other's job.
 *
 * x_nvm_get() and x_nvm_get_size() are deliberately NOT wrapped: reads of
 * module-owned objects are harmless and occasionally useful, e.g. for a
 * diagnostic pool dump. That asymmetry is intentional -- get() will succeed on
 * an ID that set() rejects.
 *
 * Rejection is reported distinctly (NVM_ERROR_ID_RESERVED) and logged, because
 * the failure is otherwise silent: most x_nvm_create() call sites ignore the
 * result, since NVM_ERROR_OBJECT_EXISTS is a normal non-error return. A
 * rejected create would then leave a parameter that simply never exists and a
 * default that never appears.
 ******************************************************************************/

nvm_error_t x_nvm_create(nvm_pool_t *p_x_pool, nvm_param_id_t x_id,
                         uint16_t u16_size, const void *p_v_default)
{
    if (NVM_ID_IS_RESERVED(x_id))
    {
        NVM_LOG_ERROR("create refused: ID 0x%04X is reserved", (unsigned) x_id);
        return NVM_ERROR_ID_RESERVED;
    }

    return x_nvm_create_unchecked(p_x_pool, x_id, u16_size, p_v_default);
}

nvm_error_t x_nvm_delete(nvm_pool_t *p_x_pool, nvm_param_id_t x_id)
{
    if (NVM_ID_IS_RESERVED(x_id))
    {
        NVM_LOG_ERROR("delete refused: ID 0x%04X is reserved", (unsigned) x_id);
        return NVM_ERROR_ID_RESERVED;
    }

    return x_nvm_delete_unchecked(p_x_pool, x_id);
}

nvm_error_t x_nvm_set(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, const void *p_v_data)
{
    if (NVM_ID_IS_RESERVED(x_id))
    {
        NVM_LOG_ERROR("set refused: ID 0x%04X is reserved", (unsigned) x_id);
        return NVM_ERROR_ID_RESERVED;
    }

    return x_nvm_set_unchecked(p_x_pool, x_id, p_v_data);
}

/******************************************************************************
 * Auto-commit timer.
 *
 * The module keeps the counter; the POLICY is the application's. Drive the
 * tick from a periodic timer and compare against whatever threshold suits.
 *
 * The counter measures time since the LAST change, not the first, so a pool
 * under continuous modification defers indefinitely. That is deliberate -- it
 * minimises write cycles, which is the pool's whole purpose. Call x_nvm_commit()
 * directly wherever a commit must happen regardless, such as before power-down.
 ******************************************************************************/

void v_nvm_commit_timer_tick(nvm_pool_t *p_x_pool, uint16_t u16_elapsed)
{
    if ((p_x_pool == NULL) || (p_x_pool->u8_need_commit == 0))
    {
        // Nothing pending, so nothing to time. Gating here rather than at the
        // call site means this is safe to call unconditionally from an ISR.
        return;
    }

    // Saturate rather than wrap. A uint16_t of milliseconds rolls over in 65.5
    // seconds, and a wrapped counter would make an already-elapsed interval
    // quietly read as not-yet-elapsed again.
    if (p_x_pool->u16_commit_timer > (0xFFFFu - u16_elapsed))
    {
        p_x_pool->u16_commit_timer = 0xFFFFu;
    }
    else
    {
        p_x_pool->u16_commit_timer += u16_elapsed;
    }
}

bool b_nvm_commit_time_elapsed(const nvm_pool_t *p_x_pool, uint16_t u16_limit)
{
    if ((p_x_pool == NULL) || (p_x_pool->u8_need_commit == 0))
    {
        return false;
    }

    // Level-triggered, not edge-triggered: this stays true until something
    // resets the counter, which a successful commit does. If a commit fails,
    // it stays true and the caller retries -- reset the timer explicitly if
    // back-off is wanted.
    return (p_x_pool->u16_commit_timer >= u16_limit);
}

void v_nvm_commit_timer_reset(nvm_pool_t *p_x_pool)
{
    // Resets the inactivity timer specifically. Named that way because a
    // second, longer "time since first write" timer is a planned addition.
    if (p_x_pool != NULL)
    {
        p_x_pool->u16_commit_timer = 0;
    }
}
