/******************************************************************************
 * nvm_list.c
 *
 * EXAMPLE -- pool dump diagnostic for nvmparams.
 *
 * ****************************************************************************
 * THIS FILE SHIPS AS nvm_list.c.example AND IS NOT COMPILED UNTIL YOU RENAME
 * IT. The double extension means no build system's source glob will pick it
 * up by accident.
 * ****************************************************************************
 *
 * To use: rename to nvm_list.c, add it to your build, and include nvm_list.h
 * where you call it. To skip it: delete this file and nvm_list.h. Nothing in
 * the nvmparams module refers to either.
 *
 * This lived inside nvmparams.c until the module was vendored. It moved out
 * because it is the module's only user of <stdio.h>, and a debugging aid has
 * no business dragging printf() into a headless project's link.
 ******************************************************************************/

#include <stdio.h>

#include "nvmparams.h"
#include "nvm_list.h"

// Object data is allocated in 4-byte units, so a record's footprint is its
// header plus its size rounded up. The module has its own copy of this; it is
// repeated here rather than exported, to keep the example standalone.
#define NVM_LIST_ROUNDUP4(n)    (((n) + 3) & 0xFFFC)

/******************************************************************************
 * nvm_error_t x_nvm_list(*p_x_pool)
 *
 * Dump the pool header and every object it holds.
 ******************************************************************************/

nvm_error_t x_nvm_list(nvm_pool_t *p_x_pool)
{
    const nvm_header_t *p_x_nvm_header;
    const nvm_object_t *p_x_nvm_object;
    uint32_t u32_object_index;
    uint16_t u16_data_index;
    uint16_t u16_object_count;

    // Checked BEFORE dereferencing anything. The original had this test after
    // the header pointer was taken from p_x_pool->p_v_data, so a NULL pool
    // faulted on the way to the guard meant to prevent it.
    if ((p_x_pool == NULL) || (p_x_pool->p_v_data == NULL))
    {
        return NVM_ERROR_PARAMETER;
    }

    p_x_nvm_header = (const nvm_header_t *) p_x_pool->p_v_data;

    // The label is written with strncpy() bounded by the field width, so a
    // label of exactly NVM_LABEL_MAX_LENGTH characters has no terminator.
    // Bound the print by the field width rather than trusting it, and derive
    // that width instead of hardcoding it, so this still prints correctly if
    // the adopter changes NVM_LABEL_MAX_LENGTH.
    printf("\r\n"
           "Label     : \"%.*s\"\r\n"
           "Size      : %lu (malloc:%u)\r\n"
           "Signature : 0x%08lX\r\n"
           "CRC       : 0x%08lX\r\n"
           "WriteCnt  : %lu\r\n"
           "NeedCommit: %u\r\n"
           "Blocks    : %u (stride %lu, alloc unit %lu)\r\n\n",
           (int) NVM_LABEL_MAX_LENGTH, p_x_nvm_header->c_label,
           (unsigned long) p_x_pool->u32_size, p_x_pool->u8_internal_malloc,
           (unsigned long) p_x_nvm_header->u32_signature,
           (unsigned long) p_x_nvm_header->u32_crc,
           (unsigned long) p_x_nvm_header->u32_write_count,
           p_x_pool->u8_need_commit,
           p_x_pool->u8_wear_blocks,
           (unsigned long) p_x_pool->u32_block_stride,
           (unsigned long) p_x_pool->u32_alloc_unit);

    u32_object_index = sizeof(nvm_header_t);
    p_x_nvm_object = (const nvm_object_t *) ((const uint8_t *) p_x_pool->p_v_data
                                            + u32_object_index);
    u16_object_count = 0;

    printf("ID      Offset  Size   Data\r\n");

    while ((p_x_nvm_object->x_id != NVM_PARAM_END_OF_DATA) &&
           (u32_object_index < p_x_pool->u32_size))
    {
        printf("0x%04X  0x%04lX  %-5u ",
               (unsigned) p_x_nvm_object->x_id,
               (unsigned long) u32_object_index,
               p_x_nvm_object->u16_size);

        for (u16_data_index = 0; u16_data_index < p_x_nvm_object->u16_size; u16_data_index++)
        {
            printf(" %02X", p_x_nvm_object->u8_data[u16_data_index]);
        }
        printf("\r\n");

        u32_object_index += sizeof(nvm_object_t)
                          + NVM_LIST_ROUNDUP4(p_x_nvm_object->u16_size);
        p_x_nvm_object = (const nvm_object_t *) ((const uint8_t *) p_x_pool->p_v_data
                                                + u32_object_index);
        u16_object_count++;
    }

    if (p_x_nvm_object->x_id == NVM_PARAM_END_OF_DATA)
    {
        u32_object_index += sizeof(nvm_object_t);
    }

    printf("\r\n%u objects, %lu of %lu bytes used\r\n",
           u16_object_count,
           (unsigned long) u32_object_index,
           (unsigned long) p_x_pool->u32_size);

    return NVM_ERROR_NONE;
}
