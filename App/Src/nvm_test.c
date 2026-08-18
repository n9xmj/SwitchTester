/******************************************************************************
 * nvm_test.c
 *
 * Host-driven test harness for nvmparams. SwitchTester only -- see nvm_test.h
 * for why this is not, and will not become, part of the vendored module.
 *
 * Everything here runs against g_x_nvm_test, a RAM-backed pool entirely
 * separate from the application's flash pool. Nothing in this file can damage
 * the real parameters, and a test run costs no flash wear.
 ******************************************************************************/

#include "device_config.h"
#include "nvmparams.h"
#include "nvm_driver_ram.h"
#include "nvm_driver_spiflash.h"
#include "nvm_test.h"
#include "automation_console.h"

/*============================================================================
 * THE TEST POOL
 *==========================================================================*/

nvm_pool_t g_x_nvm_test;

/* Its own RAM buffer, so the pool does not depend on the heap and a test run
 * cannot fail for reasons unrelated to what it is testing. */
static uint8_t au8_nvm_test_buffer[NVM_POOL_SIZE_DEFAULT];

/*----------------------------------------------------------------------------
 * Test-pool configuration.
 *
 * Note ux_base_address: for the RAM driver this is literally a pointer to the
 * emulated store, cast to uintptr_t and back. The same field is a memory
 * address for the STM32 flash driver and a byte offset for a SPI or file
 * backend -- three readings of one member, which is why it is a uintptr_t.
 *
 * The policy is FORMAT_IF_INVALID so a test can deliberately corrupt the
 * device and still get a usable pool back on re-init. Tests that want the
 * other policies pass them to N,R explicitly.
 *--------------------------------------------------------------------------*/

static nvm_pool_config_t x_nvm_test_config =
{
    .p_c_label       = "TESTPOOL",
    .pfn_read        = x_nvm_drv_ram_read,
    .pfn_write       = x_nvm_drv_ram_write,
    .u32_size        = NVM_POOL_SIZE_DEFAULT,
    .p_v_ram_buffer  = au8_nvm_test_buffer,
    .x_init_policy   = NVM_INIT_FORMAT_IF_INVALID,
    /* .ux_base_address filled in at run time -- see v_nvm_test_init() */
};

/*----------------------------------------------------------------------------
 * The SPI-flash-backed test pool. Same shape as the RAM pool, but its store is
 * the real SPI-NOR part (via nvm_driver_spiflash.c), so a commit persists and
 * survives a reset. ux_base_address is the flash byte offset, filled in at
 * init from the driver. Not brought up at boot -- the flash test inits it
 * explicitly (N,P,1 then N,R) so a plain boot costs no flash wear.
 *--------------------------------------------------------------------------*/

nvm_pool_t g_x_nvm_flash;

static uint8_t au8_nvm_flash_buffer[NVM_POOL_SIZE_DEFAULT];

static nvm_pool_config_t x_nvm_flash_config =
{
    .p_c_label       = "FLSHPOOL",
    .pfn_read        = x_nvm_drv_spiflash_read,
    .pfn_write       = x_nvm_drv_spiflash_write,
    .u32_size        = NVM_POOL_SIZE_DEFAULT,
    .p_v_ram_buffer  = au8_nvm_flash_buffer,
    .x_init_policy   = NVM_INIT_FORMAT_IF_INVALID,
    /* .ux_base_address filled in at run time from ux_nvm_drv_spiflash_base() */
};

void v_nvm_test_init(void)
{
    /* The store's address is reported by the driver rather than #defined, so
     * this cannot drift out of step with it. */
    x_nvm_test_config.ux_base_address = ux_nvm_drv_ram_base();

    (void) x_nvm_pool_init(&g_x_nvm_test, &x_nvm_test_config);
}

/*============================================================================
 * CONSOLE HANDLER
 *==========================================================================*/

#if ACON_ENABLED

/* Application-defined error code, local to this file. ACON_ERR_* codes are not
 * a shared namespace -- automation_commands.c defines its own BUSY and NVM the
 * same way. The host asserts on the string, so it only has to be consistent
 * between here and the test script. */
#define ACON_ERR_POOL           "POOL"      /* pool unusable, e.g. failed init */

/* Objects created by the test suite are uint32_t. Fixed width keeps the host
 * protocol simple; nvmparams itself is indifferent to object size. */
typedef uint32_t nvm_test_value_t;

static void v_nvm_reply_status(char c_op, char c_sub, nvm_error_t x_status)
{
    char ac_op[4];

    /* Every nvmparams result is reported, including the "not really an error"
     * ones -- NO_CHANGE, OBJECT_EXISTS, POOL_FORMATTED. The host asserts on
     * the exact value, which is the whole point of having distinct codes. */
    v_acon_emit(ACON_SIG_OK, "%s,%c,S%lX",
                pc_acon_op_name(c_op, ac_op), c_sub,
                (unsigned long) (int32_t) x_status);
}

/*----------------------------------------------------------------------------
 * Backend selector. Every 'N' op runs against whichever pool N,P last selected:
 * the RAM store (default) or the SPI-flash store. One descriptor per backend
 * binds the pool handle, its config, and the driver-specific test-support hooks
 * (wipe / fault / counts), so the whole 'N' suite runs on either backend.
 *--------------------------------------------------------------------------*/

typedef struct
{
    nvm_pool_t        *p_x_pool;
    nvm_pool_config_t *p_x_cfg;
    uintptr_t        (*pfn_base)(void);
    void             (*pfn_wipe)(uint8_t);
    void             (*pfn_fail_after)(uint32_t, nvm_error_t);
    void             (*pfn_fail_clear)(void);
    void             (*pfn_counts)(uint32_t *, uint32_t *);
    void             (*pfn_counts_reset)(void);
}
nvm_test_backend_t;

static nvm_test_backend_t s_x_backend_ram =
{
    &g_x_nvm_test, &x_nvm_test_config,
    ux_nvm_drv_ram_base, v_nvm_drv_ram_wipe,
    v_nvm_drv_ram_fail_after, v_nvm_drv_ram_fail_clear,
    v_nvm_drv_ram_counts, v_nvm_drv_ram_counts_reset
};

static nvm_test_backend_t s_x_backend_flash =
{
    &g_x_nvm_flash, &x_nvm_flash_config,
    ux_nvm_drv_spiflash_base, v_nvm_drv_spiflash_wipe,
    v_nvm_drv_spiflash_fail_after, v_nvm_drv_spiflash_fail_clear,
    v_nvm_drv_spiflash_counts, v_nvm_drv_spiflash_counts_reset
};

static nvm_test_backend_t *p_x_be = &s_x_backend_ram;

void v_acon_op_nvm_test(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    char ac_op[4];
    char c_sub;
    uint32_t u32_a = 0;
    uint32_t u32_b = 0;
    nvm_error_t x_status;
    nvm_pool_t *p_x = p_x_be->p_x_pool;      /* the active pool for this call */

    if (u8_argc < 1u)
    {
        v_acon_emit(ACON_SIG_ERR, "%s,%s",
                    pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
        return;
    }

    c_sub = ap_c_arg[0][0];

    /* Optional numeric arguments, hex like the rest of the console. */
    if (u8_argc >= 2u) { (void) b_acon_arg_u32(ap_c_arg[1], &u32_a); }
    if (u8_argc >= 3u) { (void) b_acon_arg_u32(ap_c_arg[2], &u32_b); }

    switch (c_sub)
    {
        /*------------------------------------------------------------------
         * N,P[,<0|1>] -- select the backend the other ops act on: 0 = RAM
         * (default), 1 = SPI flash. Takes effect from the NEXT command. The
         * flash pool is not auto-inited; follow with N,R to bring it up.
         *----------------------------------------------------------------*/
        case 'P':
            p_x_be = (u32_a == 1u) ? &s_x_backend_flash : &s_x_backend_ram;
            v_acon_emit(ACON_SIG_OK, "%s,P,B%X", pc_acon_op_name(c_op, ac_op),
                        (unsigned) ((p_x_be == &s_x_backend_flash) ? 1u : 0u));
            return;

        /*------------------------------------------------------------------
         * N,I -- pool info.
         *
         * Reports the header fields plus the geometry the module derived, so
         * a host can assert on stride and block count without a second call.
         *----------------------------------------------------------------*/
        case 'I':
        {
            const nvm_header_t *p_x_hdr = (const nvm_header_t *) p_x->p_v_data;

            if (p_x->p_v_data == NULL)
            {
                /* A failed init NULLs the buffer, so this is how the host sees
                 * "the pool is unusable" rather than reading stale fields. */
                v_acon_emit(ACON_SIG_ERR, "%s,I,%s",
                            pc_acon_op_name(c_op, ac_op), ACON_ERR_POOL);
                return;
            }

            v_acon_emit(ACON_SIG_OK, "%s,I,G%lX,C%lX,W%lX,D%X,Z%lX,B%X,T%lX,U%lX",
                        pc_acon_op_name(c_op, ac_op),
                        (unsigned long) p_x_hdr->u32_signature,
                        (unsigned long) p_x_hdr->u32_crc,
                        (unsigned long) p_x_hdr->u32_write_count,
                        p_x->u8_need_commit,
                        (unsigned long) p_x->u32_size,
                        p_x->u8_wear_blocks,
                        (unsigned long) p_x->u32_block_stride,
                        (unsigned long) p_x->u32_alloc_unit);
            return;
        }

        /*------------------------------------------------------------------
         * N,L -- list objects as id:size pairs.
         *
         * Walks the pool through the public API only (p_x_nvm_search and
         * p_x_next_nvm_object), which is also a check that those are enough
         * to traverse a pool from outside the module.
         *----------------------------------------------------------------*/
        case 'L':
        {
            const nvm_object_t *p_x_obj;
            uint32_t u32_offset = sizeof(nvm_header_t);
            uint16_t u16_count = 0;

            if (p_x->p_v_data == NULL)
            {
                v_acon_emit(ACON_SIG_ERR, "%s,L,%s",
                            pc_acon_op_name(c_op, ac_op), ACON_ERR_POOL);
                return;
            }

            p_x_obj = (const nvm_object_t *)
                      ((const uint8_t *) p_x->p_v_data + u32_offset);

            while ((p_x_obj->x_id != NVM_PARAM_END_OF_DATA) &&
                   (u32_offset < p_x->u32_size))
            {
                v_acon_emit(ACON_SIG_OK, "%s,L,I%X,Z%X",
                            pc_acon_op_name(c_op, ac_op),
                            (unsigned) p_x_obj->x_id, p_x_obj->u16_size);
                u16_count++;
                u32_offset += sizeof(nvm_object_t)
                            + (uint32_t) ((p_x_obj->u16_size + 3u) & 0xFFFCu);
                p_x_obj = (const nvm_object_t *)
                          ((const uint8_t *) p_x->p_v_data + u32_offset);
            }

            v_acon_emit(ACON_SIG_OK, "%s,L,N%X", pc_acon_op_name(c_op, ac_op), u16_count);
            return;
        }

        /* N,C,<id>,<val> -- create with a default value. */
        case 'C':
        {
            nvm_test_value_t x_value = (nvm_test_value_t) u32_b;
            x_status = x_nvm_create(p_x, (nvm_param_id_t) u32_a,
                                    sizeof(x_value), &x_value);
            v_nvm_reply_status(c_op, c_sub, x_status);
            return;
        }

        /* N,G,<id> -- get. Reports the value alongside the status. */
        case 'G':
        {
            nvm_test_value_t x_value = 0;
            x_status = x_nvm_get(p_x, (nvm_param_id_t) u32_a, &x_value);
            v_acon_emit(ACON_SIG_OK, "%s,G,S%lX,V%lX",
                        pc_acon_op_name(c_op, ac_op),
                        (unsigned long) (int32_t) x_status,
                        (unsigned long) x_value);
            return;
        }

        /* N,S,<id>,<val> -- set. */
        case 'S':
        {
            nvm_test_value_t x_value = (nvm_test_value_t) u32_b;
            x_status = x_nvm_set(p_x, (nvm_param_id_t) u32_a, &x_value);
            v_nvm_reply_status(c_op, c_sub, x_status);
            return;
        }

        /* N,D,<id> -- delete. */
        case 'D':
            x_status = x_nvm_delete(p_x, (nvm_param_id_t) u32_a);
            v_nvm_reply_status(c_op, c_sub, x_status);
            return;

        /* N,K -- commit. NO_CHANGE is a normal, assertable outcome. */
        case 'K':
            x_status = x_nvm_commit(p_x);
            v_nvm_reply_status(c_op, c_sub, x_status);
            return;

        /*------------------------------------------------------------------
         * N,R[,<policy>] -- release and re-initialise.
         *
         * This is how a host tests init behaviour: wipe or corrupt the device
         * with N,W, then re-init under a chosen policy and assert on the
         * status. Policy 0 FORMAT_IF_BLANK, 1 FORMAT_IF_INVALID,
         * 2 REQUIRE_VALID.
         *----------------------------------------------------------------*/
        case 'R':
            if (p_x->p_v_data != NULL)
            {
                /* Release commits any pending change, which would defeat a
                 * test that just corrupted the device. Drop the dirty flag
                 * first so re-init sees exactly what the test set up. */
                p_x->u8_need_commit = 0;
                (void) x_nvm_pool_release(p_x);
            }
            p_x_be->p_x_cfg->x_init_policy   = (nvm_init_policy_t) u32_a;
            p_x_be->p_x_cfg->ux_base_address = p_x_be->pfn_base();
            x_status = x_nvm_pool_init(p_x, p_x_be->p_x_cfg);
            v_nvm_reply_status(c_op, c_sub, x_status);
            return;

        /* N,W,<fill> -- overwrite the emulated device. 0xFF or 0x00 present
         * blank media; anything else presents garbage, i.e. corrupt. */
        case 'W':
            p_x_be->pfn_wipe((uint8_t) u32_a);
            v_acon_emit(ACON_SIG_OK, "%s", pc_acon_op_name(c_op, ac_op));
            return;

        /* N,F,<n>[,<err>] -- arm a device fault on the n-th next access. */
        case 'F':
            p_x_be->pfn_fail_after(u32_a,
                (u8_argc >= 3u) ? (nvm_error_t) (int32_t) u32_b : NVM_ERROR_DEVICE);
            v_acon_emit(ACON_SIG_OK, "%s", pc_acon_op_name(c_op, ac_op));
            return;

        /* N,Z -- disarm a pending fault. */
        case 'Z':
            p_x_be->pfn_fail_clear();
            v_acon_emit(ACON_SIG_OK, "%s", pc_acon_op_name(c_op, ac_op));
            return;

        /* N,A -- device access counts. Distinguishes "nothing to write" from
         * "wrote nothing", which no status code can. */
        case 'A':
        {
            uint32_t u32_reads = 0;
            uint32_t u32_writes = 0;
            p_x_be->pfn_counts(&u32_reads, &u32_writes);
            v_acon_emit(ACON_SIG_OK, "%s,A,R%lX,W%lX",
                        pc_acon_op_name(c_op, ac_op),
                        (unsigned long) u32_reads, (unsigned long) u32_writes);
            return;
        }

        /* N,B -- reset the access counts. */
        case 'B':
            p_x_be->pfn_counts_reset();
            v_acon_emit(ACON_SIG_OK, "%s", pc_acon_op_name(c_op, ac_op));
            return;

        /*------------------------------------------------------------------
         * N,T,<elapsed>,<limit> -- commit-timer probe.
         *
         * Ticks the timer by <elapsed> and reports the counter along with
         * whether it has reached <limit>. Lets a host verify the saturation
         * and the need-commit gating without waiting in real time.
         *----------------------------------------------------------------*/
        case 'T':
            v_nvm_commit_timer_tick(p_x, (uint16_t) u32_a);
            v_acon_emit(ACON_SIG_OK, "%s,T,C%X,E%X",
                        pc_acon_op_name(c_op, ac_op),
                        p_x->u16_commit_timer,
                        b_nvm_commit_time_elapsed(p_x, (uint16_t) u32_b) ? 1 : 0);
            return;

        default:
            v_acon_emit(ACON_SIG_ERR, "%s,%s",
                        pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
            return;
    }
}

#endif /* ACON_ENABLED */
