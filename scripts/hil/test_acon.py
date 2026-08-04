"""
test_acon.py -- HIL test suite for the SwitchTester automation console.

Exercises the SCRIPT-facing (host) side of the interface only: entry and exit
sequences, every command with sane inputs, and the parsing / error-checking
edge cases. Human mode is deliberately out of scope -- it has an operator, not
an assertion.

This is a correctness suite, not a performance one. Nothing here measures
throughput or latency; where it waits on time it waits generously.

    python scripts/hil/test_acon.py --port COM3
    python scripts/hil/test_acon.py --port COM3 --trace       # show the wire
    python scripts/hil/test_acon.py --port COM3 --slow        # + 15 s timeout test
    python scripts/hil/test_acon.py --port COM3 -k overflow   # filter by name

Exit code is 0 only if every test passed.
"""

import argparse
import sys
import time

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])

from acon import (AutomationConsole, ProtocolError, Frame,
                  ENTER_SENTINEL, EXIT_SENTINEL, CANCEL,
                  SIG_OK, SIG_ERR, DEFAULT_BAUD)


# ---------------------------------------------------------------------------
# Tiny test harness. Deliberately dependency-free: this has to run on a bench
# machine with nothing installed but pyserial.
# ---------------------------------------------------------------------------

TESTS = []


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


def expect_ok(frame, op, message=""):
    check(frame is not None, "no response at all %s" % message)
    check(frame.ok, "expected success, got %r %s" % (frame.raw, message))
    check(frame.op == op, "expected op %r, got %r" % (op, frame.op))
    return frame


def expect_err(frame, op, code, message=""):
    check(frame is not None, "no response at all %s" % message)
    check(not frame.ok, "expected an error frame, got %r %s" % (frame.raw, message))
    check(frame.op == op, "expected op %r, got %r in %r" % (op, frame.op, frame.raw))
    check(frame.code == code,
          "expected code %r, got %r in %r" % (code, frame.code, frame.raw))
    return frame


def expect_state(frame):
    """Every switch-oriented response carries the three bitmaps."""
    for key in ('L', 'M', 'R'):
        check(key in frame.tokens,
              "response %r is missing the %s bitmap" % (frame.raw, key))
    return frame


# ---------------------------------------------------------------------------
# Entry and exit sequences
# ---------------------------------------------------------------------------

@test("enter: 0xDA gives the session banner")
def t_enter(con):
    con.write_raw(bytes([ENTER_SENTINEL]))
    f = con.read_frame()
    check(f is not None, "no banner after ENTER")
    check(f.ok and f.op == '~', "expected '=~,V1', got %r" % f.raw)
    check(f.tokens.get('V') == 1, "expected protocol V1, got %r" % f.raw)
    con.leave()


@test("enter: sentinel is not echoed and does not reach the menu")
def t_enter_not_echoed(con):
    con.write_raw(bytes([ENTER_SENTINEL]))
    f = con.read_frame()
    check(f.raw == '=~,V1', "banner should be exactly '=~,V1', got %r" % f.raw)
    con.leave()


@test("exit: 0xA5 sentinel gives BYE")
def t_exit_sentinel(con):
    con.enter()
    f = con.leave('sentinel')
    check(f is not None and f.ok and f.op == '~', "expected BYE, got %r" % f)
    check('BYE' in f.raw, "expected BYE in %r" % f.raw)


@test("exit: Q gives BYE")
def t_exit_quit(con):
    con.enter()
    f = con.leave('quit')
    check(f is not None and 'BYE' in f.raw, "expected BYE, got %r" % f)


@test("exit: Ctrl-C gives BYE")
def t_exit_cancel(con):
    con.enter()
    f = con.leave('cancel')
    check(f is not None and 'BYE' in f.raw, "expected BYE, got %r" % f)


@test("exit: console is really gone -- menu answers afterwards")
def t_exit_returns_to_menu(con):
    con.enter()
    con.leave()
    con.drain()
    # '?' at the menu reprints help; the console would have answered '=?,K..'
    con.write_raw('?')
    time.sleep(0.4)
    blob = con.ser.read(con.ser.in_waiting or 1).decode('ascii', 'replace')
    check('Main Menu' in blob or 'Help' in blob,
          "expected menu help after exit, got %r" % blob[:120])
    con.drain()


@test("re-entry: enter/exit/enter works")
def t_reentry(con):
    con.enter()
    con.leave()
    f = con.enter()
    check(f.raw == '=~,V1', "second entry banner wrong: %r" % f.raw)
    con.leave()


# ---------------------------------------------------------------------------
# No-op, liveness and the CRLF rule
# ---------------------------------------------------------------------------

@test("no-op: bare CR answers =Z")
def t_nop_cr(con):
    con.enter()
    f = con.command('')
    expect_ok(f, 'Z')
    con.leave()


@test("no-op: space answers =Z")
def t_nop_space(con):
    con.enter()
    f = con.command(' ')
    expect_ok(f, 'Z')
    con.leave()


@test("no-op: Z answers =Z")
def t_nop_z(con):
    con.enter()
    f = con.command('Z')
    expect_ok(f, 'Z')
    con.leave()


@test("LF alone is discarded -- produces no frame")
def t_lone_lf(con):
    con.enter()
    con.write_raw('\n')
    heard = con.expect_silence(0.5)
    check(not heard, "LF should be ignored entirely, device said %r" % heard)
    # and the link still works
    expect_ok(con.command('Z'), 'Z')
    con.leave()


@test("CRLF host: one command yields exactly ONE frame")
def t_crlf_single_frame(con):
    """The desync test. If the trailing LF produced a second frame, a CRLF
    host would run permanently one response behind."""
    con.enter()
    con.write_raw('R\r\n')
    first = con.read_frame()
    expect_ok(first, 'R')
    heard = con.expect_silence(0.6)
    check(not heard,
          "trailing LF produced a spurious second frame: %r" % heard)
    con.leave()


# ---------------------------------------------------------------------------
# Builtins
# ---------------------------------------------------------------------------

@test("V: identity has product, platform, version, build")
def t_version(con):
    con.enter()
    f = expect_ok(con.command('V'), 'V')
    check(len(f.fields) == 4, "expected 4 identity fields, got %r" % f.fields)
    check(f.fields[0] == 'SwitchTester', "unexpected product %r" % f.fields[0])
    con.leave()


@test("L: declares K<n> and sends exactly that many payload lines")
def t_list(con):
    con.enter()
    f = expect_ok(con.command('L'), 'L')
    n = f.tokens.get('K')
    check(n is not None and n > 0, "L must declare K<n>, got %r" % f.raw)
    check(len(f.payload) == n,
          "declared K%d but %d payload lines arrived" % (n, len(f.payload)))
    con.leave()


@test("?: is an alias for L")
def t_list_alias(con):
    con.enter()
    a = expect_ok(con.command('L'), 'L')
    b = expect_ok(con.command('?'), '?')
    check(a.tokens.get('K') == b.tokens.get('K'),
          "L and ? disagree on op count")
    con.leave()


# ---------------------------------------------------------------------------
# Commands with sane inputs
# ---------------------------------------------------------------------------

@test("R: returns all three bitmaps")
def t_read(con):
    con.enter()
    expect_state(expect_ok(con.command('R'), 'R'))
    con.leave()


@test("S: set and clear drive the level bitmap")
def t_set_levels(con):
    con.enter()
    con.quiesce()
    expect_ok(con.command('S,F,F,0'), 'S')      # select all, set all
    lvl, _, _ = con.state()
    check(lvl == 0x0F, "expected all four high, level=0x%X" % lvl)
    expect_ok(con.command('S,F,0,F'), 'S')      # select all, clear all
    lvl, _, _ = con.state()
    check(lvl == 0x00, "expected all four low, level=0x%X" % lvl)
    con.leave()


@test("S: select mask leaves unselected channels alone")
def t_set_select_mask(con):
    con.enter()
    con.quiesce()
    con.command('S,F,F,0')                      # all high
    con.command('S,1,0,1')                      # clear channel 0 only
    lvl, _, _ = con.state()
    check(lvl == 0x0E, "expected only ch0 cleared, level=0x%X" % lvl)
    con.quiesce()
    con.leave()


@test("S: set|clear on the same bit is toggle")
def t_set_toggle(con):
    con.enter()
    con.quiesce()
    before, _, _ = con.state()
    con.command('S,1,1,1')                      # toggle ch0
    after, _, _ = con.state()
    check((before ^ after) == 0x01,
          "toggle should flip exactly ch0: 0x%X -> 0x%X" % (before, after))
    con.command('S,1,1,1')                      # toggle back
    back, _, _ = con.state()
    check(back == before, "second toggle should restore: 0x%X" % back)
    con.quiesce()
    con.leave()


@test("W then G: parameters round-trip")
def t_write_get(con):
    con.enter()
    expect_ok(con.command('W,0,7A120,7A120,0'), 'W')
    f = expect_ok(con.command('G,0'), 'G')
    check(f.tokens.get('N') == 0x7A120, "on-time wrong: %r" % f.raw)
    check(f.tokens.get('F') == 0x7A120, "off-time wrong: %r" % f.raw)
    check(f.tokens.get('C') == 0, "repeat wrong: %r" % f.raw)
    check('D' in f.tokens, "G must report cycles done: %r" % f.raw)
    con.leave()


@test("C/X: start sets the run bit, stop clears it and leaves the output low")
def t_cycle_start_stop(con):
    con.enter()
    con.quiesce()
    con.command('W,0,7A120,7A120,0')
    f = expect_ok(con.command('C,1'), 'C')
    check(f.tokens['R'] & 0x01, "run bit not set after start: %r" % f.raw)
    check(f.tokens['M'] & 0x01, "mode bit not set after start: %r" % f.raw)
    f = expect_ok(con.command('X,1'), 'X')
    check(not (f.tokens['R'] & 0x01), "run bit still set after stop: %r" % f.raw)
    check(not (f.tokens['L'] & 0x01), "stop should leave the output low: %r" % f.raw)
    con.leave()


@test("cycling actually advances the completed-cycle count")
def t_cycle_counts(con):
    con.enter()
    con.quiesce()
    con.command('W,0,186A0,186A0,0')            # 100 ms / 100 ms = 5 cycles/s
    con.command('C,1')
    first = con.command('G,0').tokens['D']
    time.sleep(1.5)
    second = con.command('G,0').tokens['D']
    con.command('X,1')
    check(second > first,
          "cycle count did not advance: %d -> %d" % (first, second))
    con.quiesce()
    con.leave()


@test("S hold (select, set=0, clear=0) freezes a cycling channel to manual")
def t_hold_freezes(con):
    con.enter()
    con.quiesce()
    con.command('W,0,7A120,7A120,0')
    con.command('C,1')
    f = expect_ok(con.command('S,1,0,0'), 'S')  # hold ch0
    check(not (f.tokens['M'] & 0x01),
          "hold should take the channel out of timer mode: %r" % f.raw)
    check(not (f.tokens['R'] & 0x01),
          "hold should stop the cycle: %r" % f.raw)
    con.quiesce()
    con.leave()


@test("P: commits, and reports written or no-change")
def t_persist(con):
    con.enter()
    con.quiesce()
    f = expect_ok(con.command('P'), 'P')
    check(f.tokens.get('W') in (0, 1), "P must report W0 or W1: %r" % f.raw)
    con.leave()


@test("P: refused while cycling, and names the offending channels")
def t_persist_busy(con):
    con.enter()
    con.quiesce()
    con.command('W,0,7A120,7A120,0')
    con.command('C,1')
    f = expect_err(con.command('P'), 'P', 'BUSY')
    check(f.tokens.get('R', 0) & 0x01,
          "BUSY frame should carry the run bitmap: %r" % f.raw)
    con.command('X,1')
    con.quiesce()
    con.leave()


@test("W does not auto-persist -- a fresh commit reports no change")
def t_no_autopersist(con):
    con.enter()
    con.quiesce()
    con.command('P')                            # flush anything outstanding
    con.command('W,0,7A120,7A120,0')            # should NOT dirty the pool
    f = expect_ok(con.command('P'), 'P')
    check(f.tokens.get('W') == 0,
          "W appears to have auto-persisted -- P reported a write: %r" % f.raw)
    con.leave()


@test("E: reports a transport error count")
def t_errors(con):
    con.enter()
    f = expect_ok(con.command('E'), 'E')
    check('E' in f.tokens, "E must report a count: %r" % f.raw)
    con.leave()


# ---------------------------------------------------------------------------
# Edge cases: parsing and error checking
# ---------------------------------------------------------------------------

@test("unknown opcode is rejected")
def t_unknown_op(con):
    con.enter()
    expect_err(con.command('y'), 'y', 'UNK')
    con.leave()


@test("dispatch is case sensitive -- lowercase is not the same command")
def t_case_sensitive(con):
    con.enter()
    expect_ok(con.command('R'), 'R')
    expect_err(con.command('r'), 'r', 'UNK')
    con.leave()


@test("missing arguments are rejected")
def t_missing_args(con):
    con.enter()
    expect_err(con.command('S'), 'S', 'ARG')
    expect_err(con.command('G'), 'G', 'ARG')
    expect_err(con.command('C'), 'C', 'ARG')
    expect_err(con.command('W'), 'W', 'ARG')
    con.leave()


@test("too few arguments are rejected")
def t_too_few_args(con):
    con.enter()
    expect_err(con.command('S,3,1'), 'S', 'ARG')
    expect_err(con.command('W,0,7A120,7A120'), 'W', 'ARG')
    con.leave()


@test("non-hex arguments are rejected, not silently read as zero")
def t_non_hex(con):
    con.enter()
    expect_err(con.command('S,3,1,z'), 'S', 'ARG')
    expect_err(con.command('G,x'), 'G', 'ARG')
    con.leave()


@test("trailing junk on an otherwise valid number is rejected")
def t_trailing_junk(con):
    con.enter()
    expect_err(con.command('G,0x'), 'G', 'ARG')
    expect_err(con.command('C,1junk'), 'C', 'ARG')
    con.leave()


@test("empty field is rejected")
def t_empty_field(con):
    con.enter()
    expect_err(con.command('S,3,,1'), 'S', 'ARG')
    con.leave()


@test("out-of-range channel is rejected")
def t_bad_channel(con):
    con.enter()
    expect_err(con.command('G,4'), 'G', 'ARG')
    expect_err(con.command('W,9,7A120,7A120,0'), 'W', 'ARG')
    con.leave()


@test("mask wider than four channels is rejected")
def t_bad_mask(con):
    con.enter()
    expect_err(con.command('S,1F,0,0'), 'S', 'RNG')
    expect_err(con.command('C,10'), 'C', 'RNG')
    expect_err(con.command('X,FF'), 'X', 'RNG')
    con.leave()


@test("cycle period below the automation floor is rejected")
def t_period_floor(con):
    con.enter()
    expect_err(con.command('W,0,100,100,0'), 'W', 'RNG')     # 512 us total
    con.leave()


@test("on/off time outside the engine's own limits is rejected")
def t_engine_limits(con):
    con.enter()
    expect_err(con.command('W,0,1,7A120,0'), 'W', 'RNG')     # below 10 us
    expect_err(con.command('W,0,FFFFFFFF,7A120,0'), 'W', 'RNG')
    con.leave()


@test("an over-long line is rejected, never executed truncated")
def t_line_overflow(con):
    """The line must exceed ACON_LINE_MAX (256) to trigger the guard.

    It is fed in chunks with gaps rather than as one blast, and that pacing is
    NOT test tidiness -- it is working around a real device limit found by this
    very test on 2026-08-03. The console's RX ring is 256 bytes (255 usable),
    which is SMALLER than ACON_LINE_MAX, and the per-byte getchar() path cannot
    drain a sustained 921600-baud stream. Blasted in one go, ~19% of the bytes
    are lost to RX overrun -- including, sometimes, the terminating CR, so the
    line never completes and no frame comes back at all.

    Paced, the device keeps up and the overflow path is exercised honestly.
    See t_burst_rx_limit below, which pins the limitation itself.
    """
    con.enter()
    line = 'R' + ('0123456789' * 40)                          # 401 chars
    for i in range(0, len(line), 32):
        con.write_raw(line[i:i + 32])
        time.sleep(0.02)
    con.write_raw('\r')
    f = con.read_frame()
    check(f is not None, "no response to an over-long line")
    check(not f.ok and f.code == 'OVF',
          "expected '!~,OVF', got %r" % f.raw)
    expect_ok(con.command('Z'), 'Z')                          # still in sync
    con.leave()


@test("known limit: a full-length line blasted at line rate overruns RX")
def t_burst_rx_limit(con):
    """Documents the defect rather than asserting the bug is absent.

    Fails once the RX ring is enlarged past ACON_LINE_MAX and/or the console
    reads through i16_uart_stream_rx_byte() instead of getchar() -- at which
    point delete this test and drop the pacing from t_line_overflow.
    """
    con.enter()
    before = con.command('E').tokens['E']
    con.drain()
    con.write_raw('R' + ('0123456789' * 40) + '\r')           # one blast
    con.read_frame(timeout=2.0)                               # usually nothing
    time.sleep(0.5)
    con.drain()
    # Re-sync: the device may still be chewing through the backlog.
    for _ in range(4):
        con.command('')
    con.drain()
    after = con.command('E').tokens['E']
    check(after > before,
          "expected RX overrun errors from a line-rate burst "
          "(%d -> %d) -- if this now passes cleanly, the limit is fixed "
          "and this test should be deleted" % (before, after))
    con.leave()


@test("error frames still carry the state payload")
def t_error_carries_state(con):
    con.enter()
    f = expect_err(con.command('W,0,100,100,0'), 'W', 'RNG')
    expect_state(f)
    con.leave()


@test("extra arguments to an argument-less command are ignored")
def t_extra_args_ignored(con):
    con.enter()
    expect_ok(con.command('R,1,2,3'), 'R')
    con.leave()


@test("every command answers -- no silent path")
def t_everything_answers(con):
    con.enter()
    for line in ('R', 'Z', ' ', '', 'V', 'E', 'y', 'S', 'S,1F,0,0', 'G,9'):
        f = con.command(line)
        check(f is not None, "no response to %r" % line)
    con.leave()


# ---------------------------------------------------------------------------
# Slow tests (opt in with --slow)
# ---------------------------------------------------------------------------

@test("slow: idle timeout exits the session with TMO")
def t_idle_timeout(con):
    con.enter()
    f = con.read_frame(timeout=25.0)            # ACON_IDLE_TIMEOUT_MS is 15 s
    check(f is not None, "no timeout frame after the idle period")
    check(not f.ok and f.op == '~' and 'TMO' in f.raw,
          "expected '!~,TMO', got %r" % f.raw)
    con.drain()


t_idle_timeout.slow = True


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', required=True, help='e.g. COM3')
    ap.add_argument('--baud', type=int, default=DEFAULT_BAUD)
    ap.add_argument('--trace', action='store_true', help='dump the wire traffic')
    ap.add_argument('--slow', action='store_true', help='include timing tests')
    ap.add_argument('-k', dest='filter', default=None, help='substring filter')
    args = ap.parse_args()

    selected = [(n, f) for (n, f) in TESTS
                if (args.slow or not getattr(f, 'slow', False))
                and (args.filter is None or args.filter.lower() in n.lower())]

    if not selected:
        print("no tests selected")
        return 1

    print("SwitchTester automation console -- HIL suite")
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
        for name, fn in selected:
            sys.stdout.write("  %-62s " % name)
            sys.stdout.flush()
            try:
                con.drain()
                fn(con)
                print("PASS")
                passed += 1
            except (Failure, ProtocolError) as exc:
                print("FAIL")
                failures.append((name, str(exc)))
                failed += 1
                # Leave the console so the next test starts from the menu.
                try:
                    con.write_raw(bytes([EXIT_SENTINEL]))
                    time.sleep(0.2)
                    con.drain()
                except Exception:
                    pass
            except Exception as exc:              # noqa: BLE001
                print("ERROR")
                failures.append((name, "%s: %s" % (type(exc).__name__, exc)))
                failed += 1
                try:
                    con.write_raw(bytes([EXIT_SENTINEL]))
                    time.sleep(0.2)
                    con.drain()
                except Exception:
                    pass
    finally:
        # Best effort: never leave the bench cycling.
        try:
            con.write_raw(bytes([ENTER_SENTINEL]))
            time.sleep(0.2)
            con.quiesce()
            con.write_raw(bytes([EXIT_SENTINEL]))
            time.sleep(0.2)
        except Exception:
            pass
        con.close()

    print("\n%d passed, %d failed" % (passed, failed))
    if failures:
        print("\nfailures:")
        for name, why in failures:
            print("  %-60s %s" % (name, why))
    if con.events:
        print("\nasync event frames seen (phase 1 emits none): %d" % len(con.events))
        for line in con.events[:10]:
            print("  %s" % line)

    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
