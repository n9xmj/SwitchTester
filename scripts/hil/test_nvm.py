"""
test_nvm.py -- HIL test suite for the vendored nvmparams module.

Drives the 'N' automation-console op, which operates on a SEPARATE RAM-backed
pool (g_x_nvm_test) rather than the application's flash pool. That is what
makes this suite safe and fast: it can corrupt the device, inject bus faults
and re-initialise repeatedly without touching the real parameters and without
consuming a single flash erase cycle.

It is also the only practical way to reach nvmparams' error paths. A real
flash part cannot be asked to fail on cue; the RAM driver can.

SwitchTester only. The test harness is deliberately not part of the vendored
module and is not back-ported -- Skeleton must stay minimal, LED_Strip has an
older console, and the mirror has an entirely different HIL interface.

    python scripts/hil/test_nvm.py --port COM3
    python scripts/hil/test_nvm.py --port COM3 --trace
    python scripts/hil/test_nvm.py --port COM3 -k reserved

Exit code is 0 only if every test passed.
"""

import argparse
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])

from acon import AutomationConsole, DEFAULT_BAUD


# ---------------------------------------------------------------------------
# nvmparams result codes. Must track nvmparams.h -- the whole point of having
# distinct codes is that a test can assert on which one came back.
# ---------------------------------------------------------------------------

NVM_ERROR_NONE             = 0
NVM_ERROR_PARAMETER        = -1
NVM_ERROR_MEMORY           = -2
NVM_ERROR_DEVICE           = -3
NVM_ERROR_ID_NOT_FOUND     = -4
NVM_ERROR_OBJECT_EXISTS    = -5
NVM_ERROR_OBJECT_NOT_FOUND = -6
NVM_ERROR_POOL_CORRUPT     = -7
NVM_ERROR_NO_CHANGE        = -8
NVM_ERROR_ID_RESERVED      = -9
NVM_ERROR_POOL_FORMATTED   = -10
NVM_ERROR_POOL_REFORMATTED = -11

ERR_NAME = {v: k for k, v in list(globals().items())
            if k.startswith('NVM_ERROR_')}

# Init policies, from nvm_init_policy_t.
POLICY_FORMAT_IF_BLANK   = 0
POLICY_FORMAT_IF_INVALID = 1
POLICY_REQUIRE_VALID     = 2

NVM_DATA_SIGNATURE  = 0x5AA5A55A
NVM_CRC_PLACEHOLDER = 0xDEADC0DE

# A parameter ID inside the module's reserved range (0xFF00..0xFFFF).
RESERVED_ID = 0xFF01
# Ordinary application IDs, well clear of anything the firmware itself uses.
ID_A, ID_B, ID_C = 0x0500, 0x0501, 0x0502


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
    """Firmware emits nvm_error_t as (unsigned long)(int32_t), i.e. already
    sign-extended to 32 bits, so undo that rather than a 16-bit extension."""
    return value - 0x100000000 if value >= 0x80000000 else value


def name_of(code):
    return ERR_NAME.get(code, str(code))


# ---------------------------------------------------------------------------
# Command wrappers
# ---------------------------------------------------------------------------

def nvm(con, *args):
    """Issue N,<args...> and return the reply frame."""
    text = 'N,' + ','.join(str(a) for a in args)
    return con.command(text)


def nvm_status(con, *args):
    """Issue a command whose reply carries S<status>, return it as a signed int."""
    frame = nvm(con, *args)
    check(frame is not None, "no response to N,%s" % ','.join(str(a) for a in args))
    check(frame.ok, "expected success frame, got %r" % frame.raw)
    check('S' in frame.tokens, "reply %r has no S status token" % frame.raw)
    return s32(frame.tokens['S'])


def info(con):
    """N,I -- returns the pool info as a dict of ints."""
    frame = nvm(con, 'I')
    check(frame is not None and frame.ok, "N,I failed: %r" % (frame.raw if frame else None))
    return dict(frame.tokens)


def counts(con):
    frame = nvm(con, 'A')
    check(frame is not None and frame.ok, "N,A failed")
    return frame.tokens['R'], frame.tokens['W']


def reinit(con, policy=POLICY_FORMAT_IF_INVALID):
    return nvm_status(con, 'R', '%X' % policy)


def fresh(con):
    """Blank the device and re-initialise -- the standard test starting point."""
    nvm(con, 'Z')                       # clear any armed fault
    nvm(con, 'W', 'FF')                 # blank media
    status = reinit(con, POLICY_FORMAT_IF_INVALID)
    check(status == NVM_ERROR_POOL_FORMATTED,
          "expected POOL_FORMATTED on blank media, got %s" % name_of(status))
    nvm(con, 'B')                       # zero the access counters
    return status


# ---------------------------------------------------------------------------
# Pool lifecycle
# ---------------------------------------------------------------------------

@test("init on blank media formats and says so")
def t_init_blank(con):
    nvm(con, 'W', 'FF')
    status = reinit(con, POLICY_FORMAT_IF_BLANK)
    check(status == NVM_ERROR_POOL_FORMATTED,
          "expected POOL_FORMATTED, got %s" % name_of(status))


@test("blank detection accepts 0x00 as well as 0xFF")
def t_init_blank_zeroes(con):
    # Both erase polarities count as blank: NOR flash erases to 0xFF, some
    # EEPROMs and a freshly created file to 0x00.
    nvm(con, 'W', '0')
    status = reinit(con, POLICY_FORMAT_IF_BLANK)
    check(status == NVM_ERROR_POOL_FORMATTED,
          "0x00 fill should read as blank, got %s" % name_of(status))


@test("init on a valid pool loads it without reformatting")
def t_init_valid(con):
    fresh(con)
    status = reinit(con, POLICY_FORMAT_IF_BLANK)
    check(status == NVM_ERROR_NONE,
          "a valid pool should load cleanly, got %s" % name_of(status))


@test("corrupt media: FORMAT_IF_BLANK refuses, FORMAT_IF_INVALID reformats")
def t_init_corrupt_policies(con):
    # Garbage is neither blank nor a valid pool. The two policies must differ
    # here: refusing is the safe default, reformatting is opt-in.
    nvm(con, 'W', '5A')
    status = reinit(con, POLICY_FORMAT_IF_BLANK)
    check(status == NVM_ERROR_POOL_CORRUPT,
          "FORMAT_IF_BLANK should refuse corrupt media, got %s" % name_of(status))

    nvm(con, 'W', '5A')
    status = reinit(con, POLICY_FORMAT_IF_INVALID)
    check(status == NVM_ERROR_POOL_REFORMATTED,
          "FORMAT_IF_INVALID should reformat, got %s" % name_of(status))


@test("REQUIRE_VALID refuses even blank media")
def t_init_require_valid(con):
    # For a pool that must have been provisioned beforehand: manufacturing
    # defaults would hide a production fault, so it never writes.
    nvm(con, 'W', 'FF')
    status = reinit(con, POLICY_REQUIRE_VALID)
    check(status == NVM_ERROR_POOL_CORRUPT,
          "REQUIRE_VALID should refuse blank media, got %s" % name_of(status))
    fresh(con)


@test("a failed init leaves the pool unusable, not half-built")
def t_init_failure_unusable(con):
    nvm(con, 'W', 'FF')
    reinit(con, POLICY_REQUIRE_VALID)
    frame = nvm(con, 'I')
    check(frame is not None and not frame.ok,
          "info should report the pool unusable after a failed init, got %r"
          % (frame.raw if frame else None))
    fresh(con)


@test("pool header carries the signature and the CRC placeholder")
def t_header_fields(con):
    fresh(con)
    d = info(con)
    check(d['G'] == NVM_DATA_SIGNATURE,
          "signature is 0x%08X, expected 0x%08X" % (d['G'], NVM_DATA_SIGNATURE))
    check(d['C'] == NVM_CRC_PLACEHOLDER,
          "with no CRC function the field should hold the placeholder, got 0x%08X" % d['C'])


@test("geometry: one block, stride equals pool size when no alloc unit is set")
def t_geometry(con):
    fresh(con)
    d = info(con)
    check(d['B'] == 1, "expected 1 wear block, got %d" % d['B'])
    check(d['T'] == d['Z'],
          "with no allocation unit the stride should equal the pool size, "
          "got stride %d vs size %d" % (d['T'], d['Z']))


# ---------------------------------------------------------------------------
# Object round trip
# ---------------------------------------------------------------------------

@test("create, get, set, get round trip")
def t_round_trip(con):
    fresh(con)
    check(nvm_status(con, 'C', '%X' % ID_A, '1234') == NVM_ERROR_NONE, "create failed")

    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 0x1234,
          "created object should hold its default, got 0x%X" % frame.tokens['V'])

    check(nvm_status(con, 'S', '%X' % ID_A, 'BEEF') == NVM_ERROR_NONE, "set failed")
    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 0xBEEF,
          "set value did not read back, got 0x%X" % frame.tokens['V'])


@test("create on an existing object reports OBJECT_EXISTS and preserves the value")
def t_create_existing(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '1111')
    status = nvm_status(con, 'C', '%X' % ID_A, '2222')
    check(status == NVM_ERROR_OBJECT_EXISTS,
          "expected OBJECT_EXISTS, got %s" % name_of(status))

    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 0x1111,
          "create must not overwrite an existing value, got 0x%X" % frame.tokens['V'])


@test("get on a nonexistent object reports OBJECT_NOT_FOUND")
def t_get_missing(con):
    fresh(con)
    frame = nvm(con, 'G', '%X' % ID_C)
    check(s32(frame.tokens['S']) == NVM_ERROR_OBJECT_NOT_FOUND,
          "expected OBJECT_NOT_FOUND, got %s" % name_of(s32(frame.tokens['S'])))


@test("delete removes the object and reclaims its space")
def t_delete(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, 'AAAA')
    nvm_status(con, 'C', '%X' % ID_B, 'BBBB')

    check(nvm_status(con, 'D', '%X' % ID_A) == NVM_ERROR_NONE, "delete failed")

    frame = nvm(con, 'G', '%X' % ID_A)
    check(s32(frame.tokens['S']) == NVM_ERROR_OBJECT_NOT_FOUND,
          "deleted object should be gone")

    # The surviving object must still be intact -- delete does a garbage
    # collect, which moves everything after the hole.
    frame = nvm(con, 'G', '%X' % ID_B)
    check(frame.tokens['V'] == 0xBBBB,
          "delete corrupted the following object, got 0x%X" % frame.tokens['V'])


# ---------------------------------------------------------------------------
# Reserved ID enforcement
# ---------------------------------------------------------------------------

@test("create/set/delete reject a module-reserved ID")
def t_reserved_rejected(con):
    fresh(con)
    for sub in ('C', 'S', 'D'):
        status = nvm_status(con, sub, '%X' % RESERVED_ID, '1')
        check(status == NVM_ERROR_ID_RESERVED,
              "N,%s on a reserved ID should give ID_RESERVED, got %s"
              % (sub, name_of(status)))


@test("get is deliberately NOT reserved-checked")
def t_reserved_get_allowed(con):
    # The asymmetry is intentional: reads of module-owned objects are harmless
    # and useful for diagnostics, so get() succeeds on an ID that set() refuses.
    fresh(con)
    frame = nvm(con, 'G', '%X' % RESERVED_ID)
    status = s32(frame.tokens['S'])
    check(status != NVM_ERROR_ID_RESERVED,
          "get must not apply the reserved-ID guard, got %s" % name_of(status))


# ---------------------------------------------------------------------------
# Commit behaviour
# ---------------------------------------------------------------------------

@test("commit writes once, then reports NO_CHANGE")
def t_commit_no_change(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '77')

    _, writes_before = counts(con)
    check(nvm_status(con, 'K') == NVM_ERROR_NONE, "first commit should write")
    _, writes_after = counts(con)
    check(writes_after == writes_before + 1,
          "expected exactly one device write, saw %d" % (writes_after - writes_before))

    status = nvm_status(con, 'K')
    check(status == NVM_ERROR_NO_CHANGE,
          "second commit should report NO_CHANGE, got %s" % name_of(status))

    _, writes_final = counts(con)
    check(writes_final == writes_after,
          "NO_CHANGE must not touch the device, saw %d extra write(s)"
          % (writes_final - writes_after))


@test("setting a parameter to its existing value does not dirty the pool")
def t_set_same_value(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '4242')
    nvm_status(con, 'K')

    nvm_status(con, 'S', '%X' % ID_A, '4242')
    status = nvm_status(con, 'K')
    check(status == NVM_ERROR_NO_CHANGE,
          "re-setting the same value should leave the pool clean, got %s" % name_of(status))


@test("write count increments per commit; a reformat restarts it")
def t_write_count_monotonic(con):
    # Wear-level block selection will pick the live block by highest write
    # count, so this must never reset -- including across a reformat.
    fresh(con)
    before = info(con)['W']

    nvm_status(con, 'C', '%X' % ID_A, '1')
    nvm_status(con, 'K')
    after = info(con)['W']
    check(after == before + 1,
          "write count should step by one per commit: %d -> %d" % (before, after))

    # A reformat DOES reset the count, by design: formatting zero-wipes the
    # pool so it is created in a pristine state, and carrying a possibly
    # corrupt count forward is the dangerous direction -- a spuriously high
    # value would win block selection forever. What matters for wear levelling
    # is that every block ends up at the SAME count, which a full wipe makes
    # true by construction. The cost is that the count is not a lifetime write
    # total across reformats.
    nvm(con, 'W', '5A')
    reinit(con, POLICY_FORMAT_IF_INVALID)
    after_reformat = info(con)['W']
    check(after_reformat == 1,
          "a reformatted pool should start its count again at 1, got %d" % after_reformat)


@test("committed data survives a re-init")
def t_persistence(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, 'C0DE')
    nvm_status(con, 'K')

    status = reinit(con, POLICY_FORMAT_IF_BLANK)
    check(status == NVM_ERROR_NONE, "reload should be clean, got %s" % name_of(status))

    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 0xC0DE,
          "value did not survive re-init, got 0x%X" % frame.tokens['V'])


@test("uncommitted changes are lost on re-init")
def t_uncommitted_lost(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '1')
    nvm_status(con, 'K')
    nvm_status(con, 'S', '%X' % ID_A, '9999')      # deliberately not committed

    reinit(con, POLICY_FORMAT_IF_BLANK)
    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 1,
          "uncommitted change should not have persisted, got 0x%X" % frame.tokens['V'])


# ---------------------------------------------------------------------------
# Fault injection -- the paths a real flash part cannot be asked to exercise
# ---------------------------------------------------------------------------

@test("a device write error is reported and the pool stays dirty")
def t_write_fault(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '5')

    nvm(con, 'F', '1', '%X' % (NVM_ERROR_DEVICE & 0xFFFF))
    status = nvm_status(con, 'K')
    check(status == NVM_ERROR_DEVICE,
          "commit should surface the driver's error, got %s" % name_of(status))

    # A failed write must leave the commit-needed flag set: the data is still
    # only in RAM, and a caller that retries has to be able to.
    d = info(con)
    check(d['D'] == 1, "pool should still be dirty after a failed commit")

    nvm(con, 'Z')
    check(nvm_status(con, 'K') == NVM_ERROR_NONE, "retry after clearing the fault should work")


@test("a failed commit does not advance the write count")
def t_write_fault_no_count(con):
    # The count is rolled back, because nothing reached the media -- otherwise
    # block selection would later prefer a block that was never written.
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '5')
    before = info(con)['W']

    nvm(con, 'F', '1', '%X' % (NVM_ERROR_DEVICE & 0xFFFF))
    nvm_status(con, 'K')
    after = info(con)['W']
    check(after == before,
          "write count moved despite a failed write: %d -> %d" % (before, after))
    nvm(con, 'Z')


@test("a device read error fails init without formatting")
def t_read_fault_no_format(con):
    # Writing to a device that could not be read is how a transient fault --
    # a loose bus line, a device not yet powered -- becomes permanent data
    # loss. Init must abort under every policy.
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, 'ABCD')
    nvm_status(con, 'K')

    _, writes_before = counts(con)
    nvm(con, 'F', '1', '%X' % (NVM_ERROR_DEVICE & 0xFFFF))
    status = reinit(con, POLICY_FORMAT_IF_INVALID)
    check(status == NVM_ERROR_DEVICE,
          "init should surface the read error, got %s" % name_of(status))

    _, writes_after = counts(con)
    check(writes_after == writes_before,
          "init must not write after a failed read, saw %d write(s)"
          % (writes_after - writes_before))

    # And the data must still be there once the fault clears.
    nvm(con, 'Z')
    check(reinit(con, POLICY_FORMAT_IF_BLANK) == NVM_ERROR_NONE, "pool should reload")
    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 0xABCD,
          "data was destroyed by a transient read fault, got 0x%X" % frame.tokens['V'])


@test("a driver's positive device code reaches the caller unchanged")
def t_positive_error_passthrough(con):
    # Positive values are reserved for device-specific errors and the core
    # must not translate or swallow them -- an SPI status has to survive to
    # somewhere it can be read.
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '1')

    nvm(con, 'F', '1', '2A')            # arbitrary positive code
    status = nvm_status(con, 'K')
    check(status == 0x2A,
          "expected the driver's own code 0x2A back, got %s" % name_of(status))
    nvm(con, 'Z')


# ---------------------------------------------------------------------------
# Commit timer
# ---------------------------------------------------------------------------

@test("commit timer does not run while the pool is clean")
def t_timer_gated(con):
    fresh(con)
    nvm_status(con, 'K')                # ensure clean
    frame = nvm(con, 'T', '64', '1')    # tick 100, limit 1
    check(frame.tokens['C'] == 0,
          "timer should not advance on a clean pool, got 0x%X" % frame.tokens['C'])
    check(frame.tokens['E'] == 0,
          "elapsed must be false on a clean pool")


@test("commit timer accumulates and reports elapsed once past the limit")
def t_timer_elapsed(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '1')      # dirties the pool

    frame = nvm(con, 'T', '10', '100')          # +16, limit 256
    check(frame.tokens['E'] == 0, "should not have elapsed yet")
    frame = nvm(con, 'T', 'F0', '100')          # +240 -> 256
    check(frame.tokens['C'] == 0x100,
          "timer should read 0x100, got 0x%X" % frame.tokens['C'])
    check(frame.tokens['E'] == 1, "should have elapsed at the limit")


@test("commit timer saturates instead of wrapping")
def t_timer_saturates(con):
    # A uint16 of milliseconds rolls over in 65.5 s. A wrapped counter would
    # make an already-elapsed interval quietly read as not-yet-elapsed.
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '1')
    for _ in range(5):
        frame = nvm(con, 'T', 'FFFF', 'FFFF')
    check(frame.tokens['C'] == 0xFFFF,
          "timer should saturate at 0xFFFF, got 0x%X" % frame.tokens['C'])
    check(frame.tokens['E'] == 1, "saturated timer should read as elapsed")


@test("a successful commit resets the timer")
def t_timer_reset_on_commit(con):
    fresh(con)
    nvm_status(con, 'C', '%X' % ID_A, '1')
    nvm(con, 'T', '100', 'FFFF')
    nvm_status(con, 'K')
    frame = nvm(con, 'T', '0', 'FFFF')
    check(frame.tokens['C'] == 0,
          "commit should have zeroed the timer, got 0x%X" % frame.tokens['C'])


# ---------------------------------------------------------------------------
# Pool capacity
# ---------------------------------------------------------------------------

@test("filling the pool fails cleanly rather than overrunning it")
def t_pool_full(con):
    fresh(con)
    # Each object costs 4 bytes of record plus 4 of data, against a 512-byte
    # pool less a 28-byte header and a 4-byte end record: about 60 objects.
    last = NVM_ERROR_NONE
    created = 0
    for i in range(200):
        last = nvm_status(con, 'C', '%X' % (ID_A + i), '%X' % i)
        if last != NVM_ERROR_NONE:
            break
        created += 1

    check(last != NVM_ERROR_NONE, "pool never reported full after 200 objects")
    check(created > 20, "pool filled suspiciously early, after %d objects" % created)

    # Whatever it returns, the pool must still be walkable and its earlier
    # contents intact -- a full pool is not a corrupt one.
    frame = nvm(con, 'G', '%X' % ID_A)
    check(frame.tokens['V'] == 0,
          "first object damaged by filling the pool, got 0x%X" % frame.tokens['V'])
    fresh(con)


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
    ap.add_argument('--backend', choices=['ram', 'flash'], default='ram',
                    help='RAM-backed pool (default) or the SPI-flash-backed pool')
    args = ap.parse_args()

    global ARGS
    ARGS = args

    selected = [(n, f) for (n, f) in TESTS
                if args.filter is None or args.filter.lower() in n.lower()]
    if not selected:
        print("no tests selected")
        return 1

    print("nvmparams -- HIL suite (%s-backed test pool)" % args.backend.upper())
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
        if args.backend == 'flash':
            f = nvm(con, 'P', '1')      # select the SPI-flash-backed pool
            if f is None or not f.ok or f.tokens.get('B') != 1:
                print("could not select flash backend: %r" % (f.raw if f else None))
                return 2
        for name, fn in selected:
            sys.stdout.write("  %-62s " % name)
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
                    con.drain()
                except Exception:
                    pass
    finally:
        # Leave the test pool in a sane state and the console closed out,
        # whatever happened. The flash pool is never touched by this suite.
        try:
            nvm(con, 'Z')
            fresh(con)
            con.leave()
        except Exception:
            pass
        con.close()

    print("\n%d passed, %d failed" % (passed, failed))
    if failures:
        print("\nfailures:")
        for name, why in failures:
            print("  %-60s %s" % (name, why))

    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
