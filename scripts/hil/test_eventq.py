"""
test_eventq.py -- HIL test suite for the vendored event_queue module.

Drives the 'F' automation-console op (eventq_test.c), which operates on a
dedicated test queue instance. Two payload transports: F,P / F,G carry exact
bytes as hex text so the host asserts on content; F,S / F,V generate and
verify an arithmetic pattern on-target for payloads beyond the line budget.

The centrepiece is the ISR-producer test: F,T arms the 1 ms periodic timer
callback to put sequence-stamped events in true interrupt context while this
script concurrently drains over the console -- a genuine SPSC race on real
hardware, which is the one thing a host-side build could never test.

SwitchTester only. The harness is not part of the vendored module.

    python scripts/hil/test_eventq.py --port COM3
    python scripts/hil/test_eventq.py --port COM3 --trace
    python scripts/hil/test_eventq.py --port COM3 -k wrap
"""

import argparse
import re
import sys
import time

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])

from acon import AutomationConsole, DEFAULT_BAUD


# ---------------------------------------------------------------------------
# event_queue status codes. Must track event_queue.h.
# ---------------------------------------------------------------------------

EQ_OK               = 0
EQ_ERROR_PARAMETER  = -1
EQ_ERROR_NOT_INIT   = -2
EQ_ERROR_FULL       = -3
EQ_ERROR_MEMORY     = -4
EQ_ERROR_ALIGNMENT  = -5
EQ_ERROR_SIZE       = -6
EQ_STATUS_EMPTY     = 1
EQ_STATUS_TRUNCATED = 2
EQ_STATUS_SIZE_ROUNDED = 3

CODE_NAME = {v: k for k, v in list(globals().items())
             if k.startswith(('EQ_ERROR_', 'EQ_STATUS_', 'EQ_OK'))}

# The header is 4 bytes; every record occupies header+payload rounded up to 4.
HDR = 4

# ID the ISR producer stamps (EQ_TEST_TICK_ID in eventq_test.c).
TICK_ID = 0x7E57

TESTS = []
ARGS = None


def test(name):
    def wrap(fn):
        TESTS.append((name, fn))
        return fn
    return wrap


class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)


def s32(value):
    """Statuses are emitted as (unsigned long)(int32_t) -- sign-extended."""
    return value - 0x100000000 if value >= 0x80000000 else value


def name_of(code):
    return CODE_NAME.get(code, str(code))


def space(payload):
    """Ring bytes one record of `payload` bytes consumes."""
    return (HDR + payload + 3) & ~3


# ---------------------------------------------------------------------------
# Command wrappers
# ---------------------------------------------------------------------------

def eq(con, *args):
    """Issue F,<args...> and return the reply frame."""
    text = 'F,' + ','.join(str(a) for a in args)
    return con.command(text)


def eq_status(con, *args):
    """Issue a command whose reply carries S<status>, return it signed."""
    frame = eq(con, *args)
    check(frame is not None, "no response to F,%s" % ','.join(str(a) for a in args))
    check(frame.ok, "expected success frame, got %r" % frame.raw)
    check('S' in frame.tokens, "reply %r has no S status token" % frame.raw)
    return s32(frame.tokens['S'])


def expect(con, want, *args):
    got = eq_status(con, *args)
    check(got == want, "F,%s: expected %s, got %s"
          % (','.join(str(a) for a in args), name_of(want), name_of(got)))


def info(con):
    """F,I -- N count, F free payload space, E empty flag, Z ring size."""
    frame = eq(con, 'I')
    check(frame is not None and frame.ok,
          "F,I failed: %r" % (frame.raw if frame else None))
    return dict(frame.tokens)


def extract_frame(con, sub, cap=None):
    """F,G / F,K -- returns (status, id, true_size, data_bytes).

    A negative status comes back as a bare S-only reply (no I/Z/D fields)."""
    frame = eq(con, sub) if cap is None else eq(con, sub, '%X' % cap)
    check(frame is not None and frame.ok,
          "F,%s failed: %r" % (sub, frame.raw if frame else None))
    status = s32(frame.tokens['S'])
    if status < 0:
        return (status, 0, 0, b'')
    m = re.search(r',D([0-9A-Fa-f]*)$', frame.raw)
    check(m is not None, "F,%s reply %r has no D field" % (sub, frame.raw))
    return (status, frame.tokens['I'], frame.tokens['Z'],
            bytes.fromhex(m.group(1)))


def get_frame(con, cap=None):
    return extract_frame(con, 'G', cap)


def peek_frame(con, cap=None):
    return extract_frame(con, 'K', cap)


def put(con, ident, payload=b''):
    """F,P -- put exact bytes; returns the status."""
    if payload:
        return eq_status(con, 'P', '%X' % ident, payload.hex().upper())
    return eq_status(con, 'P', '%X' % ident)


def tick_state(con):
    """F,T -- returns (remaining, put, drops)."""
    frame = eq(con, 'T')
    check(frame is not None and frame.ok, "F,T query failed")
    return frame.tokens['R'], frame.tokens['P'], frame.tokens['D']


def fresh(con, size=0, mode=None):
    """Destroy whatever queue exists (result ignored) and create anew."""
    eq(con, 'D')
    args = ['C']
    if size or mode is not None:
        args.append('%X' % size)
    if mode is not None:
        args.append('%X' % mode)
    status = eq_status(con, *args)
    check(status == EQ_OK, "create failed: %s" % name_of(status))


def drain_all(con, limit=200):
    """Get until EMPTY; returns the list of (id, size, data) received."""
    out = []
    for _ in range(limit):
        status, ident, size, data = get_frame(con)
        if status == EQ_STATUS_EMPTY:
            return out
        check(status in (EQ_OK, EQ_STATUS_TRUNCATED),
              "drain hit %s" % name_of(status))
        out.append((ident, size, data))
    raise Failure("queue did not drain within %d gets" % limit)


# ---------------------------------------------------------------------------
# Lifecycle and guards
# ---------------------------------------------------------------------------

@test("create defaults, info reports empty")
def t_create_defaults(con):
    fresh(con)
    i = info(con)
    check(i['E'] == 1, "fresh queue not empty: %r" % i)
    check(i['N'] == 0, "fresh queue count %d" % i['N'])
    check(i['Z'] == 0x100, "default ring size 0x%X, expected 0x100" % i['Z'])
    check(i['F'] == 0x100 - HDR, "free space 0x%X, expected 0x%X"
          % (i['F'], 0x100 - HDR))


@test("create rejects undersize; odd sizes round down with a status")
def t_create_size(con):
    eq(con, 'D')
    expect(con, EQ_ERROR_SIZE, 'C', '4')        # below two-header minimum
    expect(con, EQ_ERROR_SIZE, 'C', '7')        # rounds down to 4, below min
    expect(con, EQ_STATUS_SIZE_ROUNDED, 'C', '102')     # 258 -> 256, live
    i = info(con)
    check(i['Z'] == 0x100, "rounded ring size 0x%X, expected 0x100" % i['Z'])
    check(put(con, 0x77, b'\x5A') == EQ_OK, "rounded queue not usable")
    status, ident, size, data = get_frame(con)
    check((status, ident, data) == (EQ_OK, 0x77, b'\x5A'),
          "rounded queue round-trip failed")
    expect(con, EQ_OK, 'D')


@test("create rejects a misaligned caller buffer")
def t_create_misaligned(con):
    eq(con, 'D')
    expect(con, EQ_ERROR_ALIGNMENT, 'C', '100', '2')


@test("create over a live queue is refused, does not disturb it")
def t_create_over_live(con):
    fresh(con)
    check(put(con, 0x11, b'\xAA') == EQ_OK, "seed put failed")
    expect(con, EQ_ERROR_PARAMETER, 'C')
    status, ident, size, data = get_frame(con)
    check((status, ident, data) == (EQ_OK, 0x11, b'\xAA'),
          "queue disturbed by refused create: %s id=%X data=%s"
          % (name_of(status), ident, data.hex()))


@test("static caller buffer works; destroyed handle rejects ops")
def t_static_and_destroyed(con):
    fresh(con, size=0x200, mode=1)
    check(put(con, 0x22, b'\x01\x02') == EQ_OK, "put on static-ring queue failed")
    expect(con, EQ_OK, 'D')
    expect(con, EQ_ERROR_NOT_INIT, 'D')                 # double destroy
    check(put(con, 0x22, b'\x01') == EQ_ERROR_NOT_INIT, "put after destroy")
    status = get_frame(con)[0]
    check(status == EQ_ERROR_NOT_INIT, "get after destroy: %s" % name_of(status))
    i = info(con)
    check((i['N'], i['F'], i['E']) == (0, 0, 1),
          "helpers not inert on destroyed handle: %r" % i)


# ---------------------------------------------------------------------------
# Data integrity
# ---------------------------------------------------------------------------

@test("put/get round-trips exact bytes, FIFO order")
def t_roundtrip(con):
    fresh(con)
    payloads = [(0x101, b'\x00'), (0x102, b'\xDE\xAD\xBE\xEF'),
                (0x103, bytes(range(31))), (0x7FFF, b'\xFF' * 64)]
    for ident, data in payloads:
        check(put(con, ident, data) == EQ_OK, "put %X failed" % ident)
    for ident, data in payloads:
        status, got_id, got_size, got = get_frame(con)
        check(status == EQ_OK, "get: %s" % name_of(status))
        check(got_id == ident, "id 0x%X, expected 0x%X" % (got_id, ident))
        check(got_size == len(data), "size %d, expected %d" % (got_size, len(data)))
        check(got == data, "data %s != %s" % (got.hex(), data.hex()))
    status = get_frame(con)[0]
    check(status == EQ_STATUS_EMPTY, "after drain: %s" % name_of(status))


@test("zero-length events carry ID only")
def t_zero_length(con):
    fresh(con)
    check(put(con, 0x42) == EQ_OK, "zero-length put failed")
    status, ident, size, data = get_frame(con)
    check((status, ident, size, data) == (EQ_OK, 0x42, 0, b''),
          "zero-length event came back %s,%X,%d,%s"
          % (name_of(status), ident, size, data.hex()))


@test("oversize payload truncates: info status, true size, overflow discarded")
def t_truncation(con):
    fresh(con)
    data = bytes(range(16))
    check(put(con, 0x55, data) == EQ_OK, "put failed")
    status, ident, size, got = get_frame(con, cap=8)
    check(status == EQ_STATUS_TRUNCATED, "expected TRUNCATED, got %s" % name_of(status))
    check(size == 16, "true size %d, expected 16" % size)
    check(got == data[:8], "truncated data %s" % got.hex())
    # The record was consumed whole -- the queue is empty, not holding a tail.
    check(get_frame(con)[0] == EQ_STATUS_EMPTY, "truncated record not consumed")


@test("cap-0 get consumes and reports without copying")
def t_cap_zero(con):
    fresh(con)
    check(put(con, 0x66, b'\x01\x02\x03') == EQ_OK, "put failed")
    status, ident, size, got = get_frame(con, cap=0)
    check((status, ident, size, got) == (EQ_STATUS_TRUNCATED, 0x66, 3, b''),
          "cap-0 get: %s,%X,%d,%s" % (name_of(status), ident, size, got.hex()))
    check(get_frame(con)[0] == EQ_STATUS_EMPTY, "record not consumed")


@test("peek is non-consumptive; truncated peek discards nothing")
def t_peek(con):
    fresh(con)
    check(peek_frame(con)[0] == EQ_STATUS_EMPTY, "peek on empty")
    data = bytes(range(8))
    check(put(con, 0x21, data) == EQ_OK, "put 1 failed")
    check(put(con, 0x22, b'\x99') == EQ_OK, "put 2 failed")
    status, ident, size, got = peek_frame(con)
    check((status, ident, size, got) == (EQ_OK, 0x21, 8, data),
          "peek mismatch: %s,%X,%d,%s" % (name_of(status), ident, size, got.hex()))
    i = info(con)
    check(i['N'] == 2, "peek consumed a record: count %d" % i['N'])
    status, ident, size, got = peek_frame(con, cap=4)
    check((status, size, got) == (EQ_STATUS_TRUNCATED, 8, data[:4]),
          "truncated peek: %s,%d,%s" % (name_of(status), size, got.hex()))
    status, ident, size, got = get_frame(con)
    check((status, ident, got) == (EQ_OK, 0x21, data),
          "get after truncated peek lost data: %s" % got.hex())
    status, ident, size, got = get_frame(con)
    check((ident, got) == (0x22, b'\x99'), "second record damaged")
    check(get_frame(con)[0] == EQ_STATUS_EMPTY, "queue not empty")


@test("flush empties the queue; idempotent; guarded when destroyed")
def t_flush(con):
    fresh(con)
    for n in range(5):
        check(put(con, 0x30 + n, bytes([n]) * (n * 3)) == EQ_OK,
              "put %d failed" % n)
    expect(con, EQ_OK, 'Z')
    i = info(con)
    check((i['N'], i['E'], i['F']) == (0, 1, 0x100 - HDR),
          "flush left state %r" % i)
    check(get_frame(con)[0] == EQ_STATUS_EMPTY, "flush left a record")
    expect(con, EQ_OK, 'Z')                     # flushing empty is success
    expect(con, EQ_OK, 'D')
    expect(con, EQ_ERROR_NOT_INIT, 'Z')         # flush after destroy


# ---------------------------------------------------------------------------
# Space accounting, full, wrap
# ---------------------------------------------------------------------------

@test("free space and count track puts and gets exactly")
def t_accounting(con):
    fresh(con, size=0x40)
    total = 0x40 - HDR
    i = info(con)
    check(i['F'] == total, "fresh free 0x%X" % i['F'])
    check(put(con, 1, b'\x00' * 5) == EQ_OK, "put failed")     # space 12
    i = info(con)
    check(i['F'] == 0x40 - space(5) - HDR, "free 0x%X after put" % i['F'])
    check((i['N'], i['E']) == (1, 0), "count/empty %r" % i)
    check(get_frame(con)[0] == EQ_OK, "get failed")
    i = info(con)
    check((i['F'], i['N'], i['E']) == (total, 0, 1), "not restored: %r" % i)


@test("put on a full queue fails whole; exact fit succeeds")
def t_full(con):
    fresh(con, size=0x20)                       # 32 bytes
    check(put(con, 1, b'\xAA' * 8) == EQ_OK, "put 1 failed")    # space 12
    check(put(con, 2, b'\xBB' * 8) == EQ_OK, "put 2 failed")    # space 12, used 24
    st = put(con, 3, b'\xCC' * 8)               # needs 12, only 8 free
    check(st == EQ_ERROR_FULL, "expected FULL, got %s" % name_of(st))
    check(put(con, 4, b'\xDD' * 4) == EQ_OK, "exact-fit put failed")  # space 8
    i = info(con)
    check((i['F'], i['N']) == (0, 3), "full queue reports %r" % i)
    got = drain_all(con)
    check([g[0] for g in got] == [1, 2, 4],
          "drain order %r -- the FULL put must have written nothing"
          % [g[0] for g in got])
    check(got[0][2] == b'\xAA' * 8 and got[1][2] == b'\xBB' * 8
          and got[2][2] == b'\xDD' * 4, "post-full data damaged")


@test("records split across the wrap survive many laps")
def t_wrap(con):
    fresh(con, size=0x30)                       # 48 bytes; records land at
    seed = 0                                    # shifting offsets each lap
    sizes = [9, 3, 0, 17, 5, 12, 1, 30, 7, 22, 4, 15, 11, 2, 19, 8]
    pending = []
    for n, size in enumerate(sizes * 4):        # 64 records, dozens of wraps
        while True:
            st = eq_status(con, 'S', '%X' % (0x600 + n), '%X' % size,
                           '%X' % seed)
            if st == EQ_OK:
                pending.append((0x600 + n, size, seed))
                seed = (seed + 13) & 0xFF
                break
            check(st == EQ_ERROR_FULL, "pattern put: %s" % name_of(st))
            ident, size_exp, seed_exp = pending.pop(0)
            frame = eq(con, 'V', '800', '%X' % seed_exp)
            check(frame.ok and s32(frame.tokens['S']) == EQ_OK,
                  "pattern get: %r" % frame.raw)
            check(frame.tokens['I'] == ident and frame.tokens['Z'] == size_exp
                  and frame.tokens['V'] == 1,
                  "lap verify failed: %r (expected id %X size %d)"
                  % (frame.raw, ident, size_exp))
    for ident, size_exp, seed_exp in pending:
        frame = eq(con, 'V', '800', '%X' % seed_exp)
        check(frame.ok and s32(frame.tokens['S']) == EQ_OK
              and frame.tokens['I'] == ident and frame.tokens['Z'] == size_exp
              and frame.tokens['V'] == 1, "final drain verify: %r" % frame.raw)
    check(get_frame(con)[0] == EQ_STATUS_EMPTY, "queue not empty after laps")


@test("large pattern payloads round-trip and truncate correctly")
def t_large(con):
    fresh(con, size=0x800)
    expect(con, EQ_OK, 'S', '7A', '400', '5A')          # 1024-byte payload
    frame = eq(con, 'V', '800', '5A')
    check(frame.ok and s32(frame.tokens['S']) == EQ_OK
          and frame.tokens['Z'] == 0x400 and frame.tokens['V'] == 1,
          "large verify: %r" % frame.raw)
    expect(con, EQ_OK, 'S', '7B', '500', '11')          # 1280-byte payload
    frame = eq(con, 'V', '100', '11')                   # 256-byte buffer
    check(frame.ok and s32(frame.tokens['S']) == EQ_STATUS_TRUNCATED
          and frame.tokens['Z'] == 0x500 and frame.tokens['C'] == 0x100
          and frame.tokens['V'] == 1, "large truncated verify: %r" % frame.raw)


# ---------------------------------------------------------------------------
# Concurrency: the ISR producer
# ---------------------------------------------------------------------------

def run_isr_soak(con, mode, host_puts):
    """Arm the 1 ms ISR producer and concurrently drain/put from here."""
    fresh(con, size=0x100, mode=mode)
    armed = 0x200                               # 512 events, ~0.5 s of ticks
    frame = eq(con, 'T', '%X' % armed)
    check(frame is not None and frame.ok, "arm failed")

    received = []                               # tick-event sequence numbers
    host_ids = []
    hp = 0
    deadline = time.time() + 15.0
    while time.time() < deadline:
        status, ident, size, data = get_frame(con)
        if status == EQ_STATUS_EMPTY:
            remaining, put_n, drops = tick_state(con)
            if remaining == 0:
                break
            continue
        check(status == EQ_OK, "soak get: %s" % name_of(status))
        if ident == TICK_ID:
            check(size == 4, "tick event size %d" % size)
            received.append(int.from_bytes(data, 'little'))
        else:
            host_ids.append(ident)
        if hp < host_puts:                      # interleave a main-context put
            st = put(con, 0x1000 + hp, bytes([hp & 0xFF]) * 3)
            check(st in (EQ_OK, EQ_ERROR_FULL), "host put: %s" % name_of(st))
            if st == EQ_OK:
                hp += 1
    else:
        raise Failure("ISR run did not finish within the deadline")

    def collect(records):
        for ident, size, data in records:
            if ident == TICK_ID:
                received.append(int.from_bytes(data, 'little'))
            else:
                host_ids.append(ident)

    # The ISR is quiet now; top up any host puts the busy window squeezed out.
    while hp < host_puts:
        st = put(con, 0x1000 + hp, bytes([hp & 0xFF]) * 3)
        if st == EQ_OK:
            hp += 1
        else:
            check(st == EQ_ERROR_FULL, "top-up put: %s" % name_of(st))
            collect(drain_all(con))

    collect(drain_all(con))
    remaining, put_n, drops = tick_state(con)
    check(remaining == 0, "run over but R=%d" % remaining)
    check(put_n + drops == armed,
          "put %d + drops %d != armed %d" % (put_n, drops, armed))
    check(len(received) == put_n,
          "received %d tick events, ISR reports %d put" % (len(received), put_n))
    check(received == list(range(len(received))),
          "tick sequence not contiguous/ordered (first anomaly near %r)"
          % received[:20])
    return put_n, drops, hp


@test("ISR-context producer: 512 events, contiguous sequence, no loss unaccounted")
def t_isr_producer(con):
    put_n, drops, _ = run_isr_soak(con, mode=None, host_puts=0)
    check(put_n > 0, "ISR put nothing -- is the tick hook wired?")


@test("locked multi-producer: ISR + main-context puts interleave safely")
def t_isr_locked(con):
    put_n, drops, hp = run_isr_soak(con, mode=3, host_puts=32)
    check(hp == 32, "only %d host puts landed" % hp)


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', required=True, help='e.g. COM3')
    ap.add_argument('--baud', type=int, default=DEFAULT_BAUD)
    ap.add_argument('--trace', action='store_true', help='dump the wire traffic')
    ap.add_argument('-k', dest='filter', default=None, help='substring filter')
    args = ap.parse_args()

    global ARGS
    ARGS = args

    selected = [(n, f) for (n, f) in TESTS
                if args.filter is None or args.filter.lower() in n.lower()]
    if not selected:
        print("no tests selected")
        return 1

    print("event_queue -- HIL suite")
    print("port %s @ %d, %d test(s)\n" % (args.port, args.baud, len(selected)))

    passed = failed = 0
    failures = []

    con = AutomationConsole(args.port, args.baud, trace=args.trace)
    try:
        con.open()
    except Exception as exc:
        print("could not open %s: %s" % (args.port, exc))
        print("(is a terminal still attached? the port is opened exclusively)")
        return 2

    try:
        con.enter()
        for name, fn in selected:
            sys.stdout.write("  %-70s " % name)
            sys.stdout.flush()
            try:
                con.drain()
                fn(con)
                print("PASS")
                passed += 1
            except Exception as exc:
                print("FAIL")
                failed += 1
                failures.append((name, str(exc)))
                try:
                    eq(con, 'T', '0')       # quiesce any ISR run
                    con.drain()
                except Exception:
                    pass
    finally:
        try:
            eq(con, 'T', '0')
            eq(con, 'D')                    # leave no queue behind
            con.leave()
        except Exception:
            pass
        con.close()

    print("\n%d passed, %d failed" % (passed, failed))
    if failures:
        print("\nfailures:")
        for name, why in failures:
            print("  %-68s %s" % (name, why))

    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
