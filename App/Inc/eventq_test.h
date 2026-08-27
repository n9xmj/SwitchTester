/******************************************************************************
 * eventq_test.h
 *
 * Host-driven test harness for the event_queue module.
 *
 * ****************************************************************************
 * SwitchTester ONLY. This is NOT part of the vendored event_queue module and
 * is not back-ported anywhere -- same reasoning as nvm_test.h: SwitchTester is
 * the project with the console, the bench and the host scripts, so the suite
 * lives here and tests cost nothing to add.
 * ****************************************************************************
 *
 * Everything here operates on a dedicated test queue instance, created and
 * destroyed on host command. Two payload transports:
 *
 *   - F,P / F,G move payload bytes as hex text on the console line, so the
 *     host asserts on exact bytes (payloads up to the line limit);
 *   - F,S / F,V generate and verify an arithmetic byte pattern on-target, for
 *     payloads far beyond what a console line can carry.
 *
 * The ISR-producer test (F,T) is the reason this suite exists: the 1 ms
 * periodic timer callback puts sequence-stamped events in true interrupt
 * context while the host concurrently drains with F,G -- a genuine SPSC
 * producer/consumer race on real hardware. NOTE: do not F,D while a F,T run
 * has events remaining; destroy frees the ring under the ISR producer.
 *
 * The op letter is 'F' (fifo): 'Q' is the console's builtin QUIT and 'E'/'V'
 * are taken by the application/builtin sets.
 ******************************************************************************/

#ifndef EVENTQ_TEST_H
#define EVENTQ_TEST_H

#include "event_queue.h"

/*----------------------------------------------------------------------------
 * ISR-context producer hook. Called from the 1 ms periodic timer callback in
 * app_main.c; a no-op unless a Q,T run is armed.
 *--------------------------------------------------------------------------*/

extern void v_eventq_test_tick(void);

/*----------------------------------------------------------------------------
 * Automation-console handler for the 'F' op. Registered in the command table
 * in automation_commands.c. Sub-commanded:  F,<sub>[,args]
 *
 *   F,C[,<size>[,<mode>]]  create the test queue. size hex bytes, 0/absent =
 *                          module default. mode: 0 malloc'd buffer (default),
 *                          1 static buffer (size <= its capacity),
 *                          2 deliberately misaligned static buffer (expects
 *                            create to fail EQ_ERROR_ALIGNMENT),
 *                          3 malloc'd buffer + PRIMASK lock pair (exercises
 *                            the locked put path)
 *   F,D                    destroy the test queue
 *   F,I                    info: record count, free payload space, empty flag,
 *                          ring size
 *   F,P,<id>[,<hexbytes>]  put one record; payload from the hex string
 *                          (absent/empty = zero-length payload)
 *   F,G[,<cap>]            get one record into a <cap>-byte buffer (default
 *                          and maximum 0xC8, the hex-dump line budget);
 *                          reply carries the copied bytes as hex
 *   F,S,<id>,<len>[,<seed>] put one record with <len> pattern bytes generated
 *                          on-target: byte[i] = (seed + i) & 0xFF
 *   F,V[,<cap>[,<seed>]]   get one record into a <cap>-byte buffer (default
 *                          the full scratch) and verify the pattern on-target
 *   F,T[,<n>]              with <n>: arm the ISR producer for n events (one
 *                          per ms, 4-byte sequence payload). Without: report
 *                          remaining / put / dropped counters.
 *
 * All numeric arguments are hexadecimal, matching the rest of the console.
 * Every module status is reported as S<hex of (int32_t)status>, so the host
 * asserts exact codes -- including the "not really an error" positive ones.
 *--------------------------------------------------------------------------*/

extern void v_acon_op_eventq_test(char c_op, char *pc_line);

#endif /* EVENTQ_TEST_H */
