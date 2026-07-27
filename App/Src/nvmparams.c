/******************************************************************************
 * nvmparams.c
 *
 * Non-volatile memory parameter management
 ******************************************************************************/

#include "device_config.h"
#include "platform.h"
#include "nvmparams.h"

//------------------------------------------------------------------------------

// Round-up (n) to the nearest multiple of 4 that is greater than or equal to (n)
// NVM data objects are always allocated memory in 4-byte / 32-bit chunks
#define ROUNDUP4(n)     (((n) + 3) & 0xFFFC)

//------------------------------------------------------------------------------

// Pre-defined NVM pools and data
// These items are project-specific; it is OK to delete, rename or re-use them
// in a different project.

//nvm_pool_t g_x_nvm_config;
nvm_pool_t g_x_nvm_param;

static uint32_t nvm_mcu_flash[NVM_POOL_SIZE_DEFAULT/sizeof(uint32_t)] __attribute__((section(".nvmdata")));

/******************************************************************************
 * u32_crc32(void *p_v_data, uint32_t u32_size)
 * Calculate CRC-32 of data block using Ethernet polynomial
 *
 * p_v_data     Pointer to data block. Should be 32-bit aligned.
 * u32_size     Size of data block (bytes). Should be a multiple of 4.
 *
 * Returns:     CRC-32 of data block calculated using Ethernet polynomial and
 *              methodology.
 *
 * Notes:
 * For the STM32G series implementation of this routine, the hardware CRC
 * generator is used. The expected configuration is to use the default settings
 * that CubeMX provides: bytewise data input, 32-bit ethernet polynomial,
 * default init value, etc.
 * This function does not necessarily have to use the Ethernet CRC-32 or even
 * calculate a CRC (of any sort) - it just needs to perform some sort of hashing
 * function on the data that can be used as an integrity check. This could be
 * something as simple as a additive checksum, or even nothing at all (return 0)
 * if integrity checking is not needed.
 ******************************************************************************/

uint32_t u32_crc32(void *p_v_data, uint32_t u32_size)
{
//    return HAL_CRC_Calculate(&hcrc, (uint32_t *) p_v_data, u32_size);
    return 0xDEADC0DE;
}

/******************************************************************************
 *
 ******************************************************************************/

#define FLASH_PAGE(addr) ((uint32_t) (addr) - FLASH_BASE) / FLASH_PAGE_SIZE

nvm_error_t x_mcuflash_write(const void *p_v_data, void *p_v_flash, uint32_t u32_size)
{
    FLASH_EraseInitTypeDef EraseInitStruct = {0};
    uint64_t *p_u64_data = (uint64_t *) (((uint32_t) p_v_data) & 0xFFFFFFFC);
    uint32_t u32_flash_address = ((uint32_t) p_v_flash) & 0xFFFFFFFC;
    uint32_t u32_bytes_written = 0;
    uint32_t u32_page_error;
    HAL_StatusTypeDef x_status = HAL_OK;

    do
    {
        EraseInitStruct.Page = FLASH_PAGE(p_v_flash);
        EraseInitStruct.NbPages = 1;
        EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;

        x_status = HAL_FLASH_Unlock();
        if (x_status != HAL_OK) break;
        x_status = HAL_FLASHEx_Erase(&EraseInitStruct, &u32_page_error);
        if (x_status != HAL_OK) break;

        while (u32_bytes_written < u32_size)
        {
            x_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, u32_flash_address, *p_u64_data);
            if (x_status != HAL_OK) break;
            p_u64_data++;
            u32_flash_address += sizeof(uint64_t);
            u32_bytes_written += sizeof(uint64_t);
        }
    }
    while (0);

    HAL_FLASH_Lock();

    return (x_status == HAL_OK) ? NVM_ERROR_NONE : NVM_ERROR_DEVICE;
}

/******************************************************************************
 * i_nvm_read(*p_x_pool)
 *
 * Read data from NVM
 * This function should be customized by the user to access the storage device
 * to be used for storing non-volatile data.
 *
 * p_x_pool         Pointer to a nvm_pool_t struct that has been initialized
 *                  by i_nvm_pool_init()
 *
 * Returns:         0 if read was successful, nonzero on failure
 *
 * Note:
 * The API user is responsible for modifying this routine to support reading
 * from the non-volatile memory storage device(s) that will be used.
 * This routine does not need to perform any error checks other than those
 * related to physical device access; data integrity checks and validity of
 * the contents of p_x_pool and the NVM pool header data will be done by
 * API code.
 ******************************************************************************/

#define SPIFLASH_NVM_DATA_ADDRESS   0x0400

nvm_error_t x_nvm_read(nvm_pool_t *p_x_pool)
{
//    int i_status = 0;
    nvm_error_t x_status = NVM_ERROR_NONE;

    if (p_x_pool->p_v_data == NULL)
    {
        return NVM_ERROR_PARAMETER;
    }

    switch (p_x_pool->x_device)
    {
        case NVM_DEVICE_NONE:
            // Dummy device for testing
            // Does not read from a physical device, just zeroes out the RAM data pool
            memset(p_x_pool->p_v_data, 0, p_x_pool->u32_size);
            break;

        case NVM_DEVICE_MCUFLASH:
            memcpy(p_x_pool->p_v_data, nvm_mcu_flash, p_x_pool->u32_size);
            break;

#if 0
        // This demo case shows how one might use a file (using C stdio
        // filesystem calls) as a storage device.
        // The application code is expected to create the nvmdata.bin
        // file outside of this routine.
        case NVM_DEVICE_FILE:
            // Need to declare:
            // FILE *p_x_nvmfile;
            // size_t x_file_bytes_rw;
            // as module local vars
            p_x_nvmfile = fopen("nvmdata.bin", "r");
            if (p_x_nvmfile == NULL)
            {
                i_status = ERRNO;
                break;
            }
            fseek(p_x_nvmfile, 0, SEEK_SET);
            x_file_bytes_rw = fread(p_x_pool->p_v_data, p_x_pool->u32_size, p_x_nvmfile);
            if (x_file_bytes_rw == 0)
            {
                i_status = ERRNO;
            }
            fclose(p_x_nvmfile);
            if (i_status != 0)
            {
                x_status = NVM_ERROR_DEVICE;
            }
            break;
#endif
        default:
            x_status = NVM_ERROR_DEVICE;
            break;
    }

    return x_status;
}

/******************************************************************************
 *
 ******************************************************************************/

nvm_error_t x_nvm_write(nvm_pool_t *p_x_pool)
{
    nvm_error_t x_status = NVM_ERROR_NONE;

    if (p_x_pool->p_v_data == NULL)
    {
        return NVM_ERROR_PARAMETER;
    }

    switch (p_x_pool->x_device)
    {
        case NVM_DEVICE_NONE:
            // Dummy device for testing
            // Does not write data to anything
            break;

        case NVM_DEVICE_MCUFLASH:
            x_status = x_mcuflash_write(p_x_pool->p_v_data, nvm_mcu_flash, p_x_pool->u32_size);
            break;

#if 0
        // This demo case shows how one might use a file (using C stdio
        // filesystem calls) as a storage device.
        // The application code is expected to create the nvmdata.bin
        // file outside of this routine.
        case NVM_DEVICE_FILE:
            // Need to declare:
            // FILE *p_x_nvmfile;
            // size_t x_file_bytes_rw;
            // as module local vars
            p_x_nvmfile = fopen("nvmdata.bin", "w");
            if (p_x_nvmfile == NULL)
            {
                i_status = ERRNO;
                break;
            }
            x_file_bytes_rw = fread(p_x_pool->p_v_data, 1, p_x_pool->u32_size, p_x_nvmfile);
            if (x_file_bytes_rw == 0)
            {
                x_status = ERRNO;
            }
            fclose(p_x_nvmfile);
            break;
#endif
        default:
            x_status = NVM_ERROR_DEVICE;
            break;
    }

    return x_status;
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
        p_x_pool->u16_commit_timer = 0;
        return NVM_ERROR_NONE;
    }

    nvm_header_t *p_x_nvm_header = (nvm_header_t *) p_x_pool->p_v_data;
    uint8_t *p_u8_data_start = (uint8_t *) (p_x_pool->p_v_data) + sizeof(nvm_header_t);
    p_x_nvm_header->u32_signature = NVM_DATA_SIGNATURE;
    p_x_nvm_header->u32_write_count++;
    p_x_nvm_header->u32_crc = u32_crc32(p_u8_data_start, p_x_pool->u32_size - sizeof(nvm_header_t));

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
 * nvm_error_t x_nvm_pool_init(*p_x_pool, x_device, *p_v_ram_buffer, u32_size)
 *
 * Initializes a NVM data pool structure, and reads NVM-stored data content
 * of pool into the pool's RAM buffer
 *
 * *p_x_pool    Pointer to a nvm_pool_t instance; the pool's "handle"
 *              The caller must allocate/instantiate this (typically a static
 *              allocation)
 * x_device     The physical non-volatile storage device ID to associate with
 *              the pool.
 * *p_v_ram_buffer
 *              A pointer to a region of RAM memory that will serve as the
 *              target/buffer for all data operations on the pool.
 *              This buffer is filled with contents from the physical storage
 *              device when x_nvm_pool_init() is called, and written out to
 *              the device when x_nvm_commit() is called.
 *              If NULL, then x_nvm_pool_init() will malloc() a <u32_size>'d
 *              buffer for it.
 * u32_size     The size (bytes) of the RAM buffer provided or to allocate.
 *              If 0, size will be set to NVM_POOL_SIZE_DEFAULT
 *
 * Returns:     NVM_ERROR_NONE if the pool was initialized and its contents
 *              were read from the physical NVM device successfully.
 *              NVM_ERROR_POOL_CORRUPT will be returned when a new pool is
 *              created and the physical NVM storage for it has not been
 *              initialized yet. It can also be returned if a previously
 *              created pool's physical storage has been corrupted. In
 *              either case, the routine will (attempt to) reformat the
 *              storage to its default (empty) state.
 *
 * Notes:
 * A given x_device should be associated with only one p_x_pool. Bad Things
 * will happen if this rule is not adhered to.
 * It is permissible for a single physical storage device to contain multiple
 * NVM storage pools, but this can be done only if the physical device is
 * partitioned into multiple segments, and each segment associated with a
 * different device ID.
 * The SCN4 project uses this multi-segment approach; a single physical
 * SPI FLASH device is providing two 4K (sector-sized) segments, each of
 * which is associated with a single pool. Two device implementations exist
 * in x_nvm_read() and x_nvm_write(), both of which operate on the same
 * physical device, but at different device memory addresses.
 ******************************************************************************/

nvm_error_t x_nvm_pool_init(nvm_pool_t *p_x_pool, nvm_device_t x_device,
                            void *p_v_ram_buffer, uint32_t u32_size,
                            const char *p_c_label)
{
    nvm_error_t x_status = NVM_ERROR_NONE;
    nvm_header_t *p_x_header;

    if (p_x_pool == NULL)
    {
        return NVM_ERROR_PARAMETER;
    }

    memset(p_x_pool, 0, sizeof(nvm_pool_t));

    if (u32_size == 0)
    {
        u32_size = NVM_POOL_SIZE_DEFAULT;
    }

    if (u32_size < (sizeof(nvm_header_t) + sizeof(nvm_object_t)))
    {
        return NVM_ERROR_PARAMETER;
    }

    // Associate the caller-provided RAM buffer with the storage pool
    // If the caller did not provide one, malloc(u32_size) one here.

    u32_size = ROUNDUP4(u32_size);

    p_x_pool->u8_internal_malloc = 0;
    if (p_v_ram_buffer == NULL)
    {
        p_v_ram_buffer = malloc(u32_size);
        if (p_v_ram_buffer == NULL)
        {
            return NVM_ERROR_MEMORY;
        }
        p_x_pool->u8_internal_malloc = 1;
    }

    p_x_pool->p_v_data = p_v_ram_buffer;
    p_x_pool->x_device = x_device;
    p_x_pool->u32_size = u32_size;

    // Initialize the RAM data buffer with content read from the physical NVM
    // storage device.

    x_status = x_nvm_read(p_x_pool);
    if (x_status != NVM_ERROR_NONE)
    {
        if (p_x_pool->u8_internal_malloc)
        {
            free(p_v_ram_buffer);
        }
        // Setting the pool's data pointer to NULL will ensure that attempts
        // to use this pool on other nvmparam API calls will return
        // a NVM_ERROR_PARAMETER error.
        p_x_pool->p_v_data = NULL;
        return x_status;
    }

    // Check integrity of NVM data pool
    // Determine if it needs to be initialized/created

    p_x_header = p_x_pool->p_v_data;
    uint8_t *p_u8_data_start = (uint8_t *) (p_x_pool->p_v_data) + sizeof(nvm_header_t);
    uint32_t u32_data_size = p_x_pool->u32_size - sizeof(nvm_header_t);
    uint32_t u32_crc;
    if (p_x_header->u32_signature != NVM_DATA_SIGNATURE)
    {
        x_status = NVM_ERROR_POOL_CORRUPT;
    }
    else
    {
        // Verify data pool checksum/CRC here
        u32_crc = u32_crc32(p_u8_data_start, u32_data_size);
        if (u32_crc != p_x_header->u32_crc)
        {
            x_status = NVM_ERROR_POOL_CORRUPT;
        }
    }

    // Create/format new NVM memory pool if it is corrupt or has not been
    // created/formatted yet.

    if (x_status == NVM_ERROR_POOL_CORRUPT)
    {
        // Zero out the RAM memory pool
        memset(p_x_pool->p_v_data, 0, p_x_pool->u32_size);
        // Set the modified flag so x_nvm_commit() will write it out
        p_x_pool->u8_need_commit = 1;
        // Create the end record
        nvm_object_t *p_x_object = (nvm_object_t *) p_u8_data_start;
        p_x_object->x_id = NVM_PARAM_END_OF_DATA;
        p_x_object->u16_size = 0;
        // Set the pool label
        if (p_c_label != NULL)
        {
            strncpy(p_x_header->c_label, p_c_label, NVM_LABEL_MAX_LENGTH);
        }
        // Write out initialized RAM data
        // Note: x_nvm_commit() will update the header data
        // (signature, write count, and CRC)
        x_status = x_nvm_commit(p_x_pool);
        if (x_status == NVM_ERROR_NONE)
        {
            x_status = NVM_ERROR_POOL_CORRUPT;
        }
    }

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

    if (p_x_pool->u8_internal_malloc)
    {
        // NVM memory pool was malloc'd by x_nvm_pool_init
        // free() it here if so
        free(p_x_pool->p_v_data);
        p_x_pool->p_v_data = NULL;
        p_x_pool->u8_internal_malloc = 0;
    }

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

nvm_error_t x_nvm_create(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, uint16_t u16_size, const void *p_v_default)
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

nvm_error_t x_nvm_delete(nvm_pool_t *p_x_pool, nvm_param_id_t x_id)
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

nvm_error_t x_nvm_set(nvm_pool_t *p_x_pool, nvm_param_id_t x_id, const void *p_v_data)
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
 *
 ******************************************************************************/

nvm_error_t x_nvm_list(nvm_pool_t *p_x_pool)
{
#if DEBUG_MENU
    nvm_header_t *p_x_nvm_header = (nvm_header_t *) p_x_pool->p_v_data;
    nvm_object_t *p_x_nvm_object;
    uint32_t u32_object_index;
    uint16_t u16_data_index;
    uint16_t u16_object_count;

    if (p_x_pool == NULL)
    {
        return NVM_ERROR_PARAMETER;
    }

    printf("\r\n"
            "Label     : \"%.16s\"\r\n"
            "DeviceID  : %u\r\n"
            "Size      : %lu (malloc:%u)\r\n"
            "Signature : 0x%08lX\r\n"
            "CRC       : 0x%08lX\r\n"
            "WriteCnt  : %lu\r\n"
            "NeedCommit: %u\r\n\n",
            p_x_nvm_header->c_label,
            p_x_pool->x_device,
            p_x_pool->u32_size, p_x_pool->u8_internal_malloc,
            p_x_nvm_header->u32_signature,
            p_x_nvm_header->u32_crc,
            p_x_nvm_header->u32_write_count,
            p_x_pool->u8_need_commit
            );

    p_x_nvm_object = (nvm_object_t *) (((uint8_t *) p_x_pool->p_v_data) + sizeof(nvm_header_t));
    u32_object_index = sizeof(nvm_header_t);
    u16_object_count = 0;

    printf("ID      Offset  Size   Data\r\n");
    while ((p_x_nvm_object->x_id != NVM_PARAM_END_OF_DATA) && (u32_object_index < p_x_pool->u32_size))
    {
        printf("0x%04X  0x%04lX  %-5u ", p_x_nvm_object->x_id, u32_object_index, p_x_nvm_object->u16_size);
        for (u16_data_index = 0; u16_data_index < p_x_nvm_object->u16_size; u16_data_index++)
        {
            printf(" %02X", p_x_nvm_object->u8_data[u16_data_index]);
        }
        printf("\r\n");
        u32_object_index += sizeof(nvm_object_t) + ROUNDUP4(p_x_nvm_object->u16_size);
        p_x_nvm_object = (nvm_object_t *) (((uint8_t *) p_x_pool->p_v_data) + u32_object_index);
        u16_object_count++;
    }

    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        u32_object_index += sizeof(nvm_object_t);
    }
    printf("\r\n%u objects, %lu of %lu bytes used\r\n", u16_object_count, u32_object_index, p_x_pool->u32_size);
#endif

    return NVM_ERROR_NONE;
}
