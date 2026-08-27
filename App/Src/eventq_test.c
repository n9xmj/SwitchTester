/******************************************************************************
 * eventq_test.c
 *
 * Host-driven test harness for event_queue. SwitchTester only -- see
 * eventq_test.h for why this is not, and will not become, part of the
 * vendored module.
 ******************************************************************************/

#include "device_config.h"
#include "event_queue.h"
#include "eventq_test.h"
#include "automation_console.h"

#if ACON_ENABLED

#include <string.h>

/* Event ID the ISR producer stamps on its records. */
#define EQ_TEST_TICK_ID         0x7E57u

/* On-target pattern/scratch capacity -- bounds F,S / F,V payloads. */
#define EQ_TEST_SCRATCH_SIZE    2048u

/* Hex-dump budget for F,G replies: 200 bytes = 400 characters, which fits the
 * console line alongside the reply fields. */
#define EQ_TEST_HEX_MAX         200u

/*============================================================================
 * THE TEST QUEUE
 *==========================================================================*/

static event_queue_handle_t s_x_eq_test;

/* Static ring for the caller-supplied-buffer modes. u32_aligned so mode 1
 * genuinely tests an aligned caller buffer; mode 2 offsets into it by one
 * byte to provoke EQ_ERROR_ALIGNMENT on purpose. */
static uint32_t s_au32_eq_buffer[512u / 4u];

/* Payload scratch, shared by the pattern put/get/verify sub-commands. */
static uint8_t s_au8_scratch[EQ_TEST_SCRATCH_SIZE];

/*----------------------------------------------------------------------------
 * PRIMASK lock pair for create mode 3 -- the realistic single-MCU lock an
 * adopter with multiple producer contexts would supply.
 *--------------------------------------------------------------------------*/

static uint32_t s_u32_primask;

static void v_eq_test_lock(void)
{
    uint32_t u32_primask = __get_PRIMASK();
    __disable_irq();
    s_u32_primask = u32_primask;
}

static void v_eq_test_unlock(void)
{
    __set_PRIMASK(s_u32_primask);
}

/*============================================================================
 * ISR PRODUCER (F,T)
 *
 * v_eventq_test_tick() runs in the 1 ms periodic timer interrupt. While a run
 * is armed it puts one 4-byte sequence-stamped event per tick. The sequence
 * advances only on a successful put, so the host sees a contiguous stream and
 * reads the drop count separately: put + dropped == armed count when done.
 *==========================================================================*/

static volatile uint32_t s_u32_tick_remaining;      /* ISR decrements */
static volatile uint32_t s_u32_tick_put;
static volatile uint32_t s_u32_tick_drops;
static uint32_t          s_u32_tick_seq;            /* ISR-private */

void v_eventq_test_tick(void)
{
    event_queue_status_t x_status;

    if (s_u32_tick_remaining == 0u)
    {
        return;
    }

    x_status = x_event_queue_put(&s_x_eq_test, EQ_TEST_TICK_ID,
                                 sizeof(s_u32_tick_seq), &s_u32_tick_seq);
    if (x_status == EQ_OK)
    {
        s_u32_tick_seq++;
        s_u32_tick_put++;
    }
    else
    {
        s_u32_tick_drops++;
    }
    s_u32_tick_remaining--;
}

/*============================================================================
 * CONSOLE HANDLER
 *==========================================================================*/

/*----------------------------------------------------------------------------
 * Hex text <-> bytes. Returns the byte count, or 0xFFFF on an odd-length or
 * non-hex string.
 *--------------------------------------------------------------------------*/

static int8_t i8_eq_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) { return (int8_t) (c - '0'); }
    if ((c >= 'a') && (c <= 'f')) { return (int8_t) (c - 'a' + 10); }
    if ((c >= 'A') && (c <= 'F')) { return (int8_t) (c - 'A' + 10); }
    return -1;
}

static uint16_t u16_eq_hex_to_bytes(const char *pc_hex, uint8_t *pu8_out,
                                    uint16_t u16_max)
{
    uint16_t u16_count = 0u;

    while (*pc_hex != '\0')
    {
        int8_t i8_hi = i8_eq_nibble(pc_hex[0]);
        int8_t i8_lo = (pc_hex[1] != '\0') ? i8_eq_nibble(pc_hex[1]) : -1;

        if ((i8_hi < 0) || (i8_lo < 0) || (u16_count >= u16_max))
        {
            return 0xFFFFu;
        }
        pu8_out[u16_count++] = (uint8_t) (((uint8_t) i8_hi << 4) | (uint8_t) i8_lo);
        pc_hex += 2;
    }
    return u16_count;
}

static void v_eq_bytes_to_hex(const uint8_t *pu8_in, uint16_t u16_count,
                              char *pc_out)
{
    static const char ac_digit[] = "0123456789ABCDEF";
    uint16_t u16_i;

    for (u16_i = 0u; u16_i < u16_count; u16_i++)
    {
        *pc_out++ = ac_digit[pu8_in[u16_i] >> 4];
        *pc_out++ = ac_digit[pu8_in[u16_i] & 0x0Fu];
    }
    *pc_out = '\0';
}

static void v_eq_reply_status(char c_op, char c_sub, event_queue_status_t x_status)
{
    char ac_op[4];

    v_acon_emit(ACON_SIG_OK, "%s,%c,S%lX",
                pc_acon_op_name(c_op, ac_op), c_sub,
                (unsigned long) (int32_t) x_status);
}

void v_acon_op_eventq_test(char c_op, char *pc_line)
{
    char *ap_c_arg[ACON_MAX_ARGS];
    uint8_t u8_argc = u8_acon_args(pc_line, ap_c_arg, ACON_MAX_ARGS);
    char ac_op[4];
    char c_sub;
    uint32_t u32_a = 0;
    uint32_t u32_b = 0;
    uint32_t u32_c = 0;
    event_queue_status_t x_status;

    if (u8_argc < 1u)
    {
        v_acon_emit(ACON_SIG_ERR, "%s,%s",
                    pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
        return;
    }

    c_sub = ap_c_arg[0][0];

    /* Optional numeric arguments, hex like the rest of the console. The
     * second argument of F,P is a hex byte STRING, parsed in its case. */
    if (u8_argc >= 2u) { (void) b_acon_arg_u32(ap_c_arg[1], &u32_a); }
    if (u8_argc >= 3u) { (void) b_acon_arg_u32(ap_c_arg[2], &u32_b); }
    if (u8_argc >= 4u) { (void) b_acon_arg_u32(ap_c_arg[3], &u32_c); }

    switch (c_sub)
    {
        /*------------------------------------------------------------------
         * F,C[,<size>[,<mode>]] -- create. Every mode reports the module's
         * own status verbatim, so the negative tests (mode 2, undersize,
         * misconfiguration) assert on exact codes host-side.
         *----------------------------------------------------------------*/
        case 'C':
        {
            event_queue_config_t x_config = { .u32_size = u32_a };

            switch (u32_b)
            {
                case 0u:                            /* malloc'd ring */
                    break;

                case 1u:                            /* static caller ring */
                    if ((u32_a == 0u) || (u32_a > sizeof(s_au32_eq_buffer)))
                    {
                        v_acon_emit(ACON_SIG_ERR, "%s,C,%s",
                                    pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                        return;
                    }
                    x_config.pv_buffer = s_au32_eq_buffer;
                    break;

                case 2u:                            /* misaligned on purpose */
                    x_config.pv_buffer = (uint8_t *) s_au32_eq_buffer + 1;
                    break;

                case 3u:                            /* malloc'd + PRIMASK lock */
                    x_config.pfn_lock   = v_eq_test_lock;
                    x_config.pfn_unlock = v_eq_test_unlock;
                    break;

                default:
                    v_acon_emit(ACON_SIG_ERR, "%s,C,%s",
                                pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                    return;
            }

            x_status = x_event_queue_create(&s_x_eq_test, &x_config);
            v_eq_reply_status(c_op, 'C', x_status);
            return;
        }

        /*------------------------------------------------------------------
         * F,D -- destroy. The host must not destroy while a F,T run has
         * events remaining (the ISR producer would put into a freed ring).
         *----------------------------------------------------------------*/
        case 'D':
            s_u32_tick_remaining = 0u;
            x_status = x_event_queue_destroy(&s_x_eq_test);
            v_eq_reply_status(c_op, 'D', x_status);
            return;

        /*------------------------------------------------------------------
         * F,I -- info from the three helpers plus the ring size.
         *----------------------------------------------------------------*/
        case 'I':
            v_acon_emit(ACON_SIG_OK, "%s,I,N%X,F%lX,E%X,Z%lX",
                        pc_acon_op_name(c_op, ac_op),
                        u16_event_queue_count(&s_x_eq_test),
                        (unsigned long) u32_event_queue_free_space(&s_x_eq_test),
                        (unsigned) (b_event_queue_is_empty(&s_x_eq_test) ? 1u : 0u),
                        (unsigned long) s_x_eq_test.u32_size);
            return;

        /*------------------------------------------------------------------
         * F,P,<id>[,<hexbytes>] -- put with exact host-chosen payload.
         *----------------------------------------------------------------*/
        case 'P':
        {
            uint16_t u16_len = 0u;

            if (u8_argc < 2u)
            {
                v_acon_emit(ACON_SIG_ERR, "%s,P,%s",
                            pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                return;
            }
            if (u8_argc >= 3u)
            {
                u16_len = u16_eq_hex_to_bytes(ap_c_arg[2], s_au8_scratch,
                                              (uint16_t) sizeof(s_au8_scratch));
                if (u16_len == 0xFFFFu)
                {
                    v_acon_emit(ACON_SIG_ERR, "%s,P,%s",
                                pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                    return;
                }
            }

            x_status = x_event_queue_put(&s_x_eq_test, (uint16_t) u32_a, u16_len,
                                         (u16_len != 0u) ? s_au8_scratch : NULL);
            v_eq_reply_status(c_op, 'P', x_status);
            return;
        }

        /*------------------------------------------------------------------
         * F,G[,<cap>] -- get, echoing the copied payload as hex so the host
         * verifies exact bytes. Cap defaults to (and may not exceed) the
         * hex-dump line budget; larger-payload tests use F,V instead.
         *----------------------------------------------------------------*/
        case 'G':
        {
            static char s_ac_hex[(EQ_TEST_HEX_MAX * 2u) + 1u];
            event_queue_record_t x_record =
            {
                .pv_data      = s_au8_scratch,
                .u16_buf_size = EQ_TEST_HEX_MAX,
            };

            if (u8_argc >= 2u)
            {
                if (u32_a > EQ_TEST_HEX_MAX)
                {
                    v_acon_emit(ACON_SIG_ERR, "%s,G,%s",
                                pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                    return;
                }
                x_record.u16_buf_size = (uint16_t) u32_a;
            }

            x_status = x_event_queue_get(&s_x_eq_test, &x_record);
            if (x_status < 0)
            {
                v_eq_reply_status(c_op, 'G', x_status);
                return;
            }

            if (x_status == EQ_STATUS_EMPTY)
            {
                x_record.u16_id        = 0u;    /* Not written by the module */
                x_record.u16_data_size = 0u;
                s_ac_hex[0] = '\0';
            }
            else
            {
                uint16_t u16_copied = x_record.u16_data_size;

                if (u16_copied > x_record.u16_buf_size)
                {
                    u16_copied = x_record.u16_buf_size;     /* Truncated get */
                }
                v_eq_bytes_to_hex(s_au8_scratch, u16_copied, s_ac_hex);
            }

            v_acon_emit(ACON_SIG_OK, "%s,G,S%lX,I%X,Z%X,D%s",
                        pc_acon_op_name(c_op, ac_op),
                        (unsigned long) (int32_t) x_status,
                        x_record.u16_id, x_record.u16_data_size, s_ac_hex);
            return;
        }

        /*------------------------------------------------------------------
         * F,S,<id>,<len>[,<seed>] -- put a pattern payload generated here,
         * for sizes beyond the console line.
         *----------------------------------------------------------------*/
        case 'S':
        {
            uint16_t u16_i;

            if ((u8_argc < 3u) || (u32_b > sizeof(s_au8_scratch)))
            {
                v_acon_emit(ACON_SIG_ERR, "%s,S,%s",
                            pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                return;
            }
            for (u16_i = 0u; u16_i < (uint16_t) u32_b; u16_i++)
            {
                s_au8_scratch[u16_i] = (uint8_t) (u32_c + u16_i);
            }

            x_status = x_event_queue_put(&s_x_eq_test, (uint16_t) u32_a,
                                         (uint16_t) u32_b,
                                         (u32_b != 0u) ? s_au8_scratch : NULL);
            v_eq_reply_status(c_op, 'S', x_status);
            return;
        }

        /*------------------------------------------------------------------
         * F,V[,<cap>[,<seed>]] -- get into a <cap>-byte buffer and verify
         * the pattern on-target. V1 = every copied byte matched.
         *----------------------------------------------------------------*/
        case 'V':
        {
            uint16_t u16_copied;
            uint16_t u16_i;
            uint8_t  u8_ok = 1u;
            event_queue_record_t x_record =
            {
                .pv_data      = s_au8_scratch,
                .u16_buf_size = (uint16_t) sizeof(s_au8_scratch),
            };

            if (u8_argc >= 2u)
            {
                if (u32_a > sizeof(s_au8_scratch))
                {
                    v_acon_emit(ACON_SIG_ERR, "%s,V,%s",
                                pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
                    return;
                }
                x_record.u16_buf_size = (uint16_t) u32_a;
            }

            x_status = x_event_queue_get(&s_x_eq_test, &x_record);
            if ((x_status < 0) || (x_status == EQ_STATUS_EMPTY))
            {
                v_eq_reply_status(c_op, 'V', x_status);
                return;
            }

            u16_copied = x_record.u16_data_size;
            if (u16_copied > x_record.u16_buf_size)
            {
                u16_copied = x_record.u16_buf_size;
            }
            for (u16_i = 0u; u16_i < u16_copied; u16_i++)
            {
                if (s_au8_scratch[u16_i] != (uint8_t) (u32_b + u16_i))
                {
                    u8_ok = 0u;
                    break;
                }
            }

            v_acon_emit(ACON_SIG_OK, "%s,V,S%lX,I%X,Z%X,C%X,V%X",
                        pc_acon_op_name(c_op, ac_op),
                        (unsigned long) (int32_t) x_status,
                        x_record.u16_id, x_record.u16_data_size,
                        u16_copied, (unsigned) u8_ok);
            return;
        }

        /*------------------------------------------------------------------
         * F,T[,<n>] -- ISR producer. With a count: reset the run counters,
         * then arm (the volatile store to remaining publishes the run to the
         * ISR). Without: report progress.
         *----------------------------------------------------------------*/
        case 'T':
            if (u8_argc >= 2u)
            {
                s_u32_tick_remaining = 0u;      /* Quiesce before rearming */
                s_u32_tick_put       = 0u;
                s_u32_tick_drops     = 0u;
                s_u32_tick_seq       = 0u;
                s_u32_tick_remaining = u32_a;
                v_acon_emit(ACON_SIG_OK, "%s,T,N%lX",
                            pc_acon_op_name(c_op, ac_op), (unsigned long) u32_a);
            }
            else
            {
                v_acon_emit(ACON_SIG_OK, "%s,T,R%lX,P%lX,D%lX",
                            pc_acon_op_name(c_op, ac_op),
                            (unsigned long) s_u32_tick_remaining,
                            (unsigned long) s_u32_tick_put,
                            (unsigned long) s_u32_tick_drops);
            }
            return;

        default:
            v_acon_emit(ACON_SIG_ERR, "%s,%s",
                        pc_acon_op_name(c_op, ac_op), ACON_ERR_ARGS);
            return;
    }
}

#else  /* !ACON_ENABLED */

void v_eventq_test_tick(void)
{
}

#endif /* ACON_ENABLED */
