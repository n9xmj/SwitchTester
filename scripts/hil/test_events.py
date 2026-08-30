"""
test_events.py -- HIL test suite for the SwitchTester event PATH.

Distinct from test_eventq.py, which exercises the vendored event_queue module
against a dedicated test queue. This suite exercises the real thing: switch
transitions produced into the application's event queue, gated by the
production mask, and drained over the console.

    A[,mask]        read / write the production mask (hex, bit31 = global enable)
    D[,max]         drain: "=D,K<n>,N<rem>,D<drops>" + n "+" payload lines
    H[,S|F|R]       queue status / flush / reset counters

Consumption is host-COMMANDED. Nothing is ever emitted that was not asked for,
so this script never has to classify unsolicited frames.

    python scripts/hil/test_events.py --port COM3
    python scripts/hil/test_events.py --port COM3 --trace
    python scripts/hil/test_events.py --port COM3 -k mask
"""

import argparse
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])

from acon import AutomationConsole, DEFAULT_BAUD


# ---------------------------------------------------------------------------
# Mask bits. Must track App/Inc/app_events.h.
# ---------------------------------------------------------------------------

M_SW_A_AUTO       = 1 << 0
M_SW_B_AUTO       = 1 << 1
M_SW_C_AUTO       = 1 << 2
M_SW_D_AUTO       = 1 << 3
M_SW_A_MANUAL     = 1 << 4
M_SW_B_MANUAL     = 1 << 5
M_SW_C_MANUAL     = 1 << 6
M_SW_D_MANUAL     = 1 << 7
M_SENSE_A         = 1 << 8
M_CYCLE_COMPLETE  = 1 << 30
M_GLOBAL          = 1 << 31

M_SW_AUTO_ALL     = 0x0000000F
M_SW_MANUAL_ALL   = 0x000000F0

# Event class IDs. Must track event_class_t in app_events.h.
CLASS_NONE           = 0x0000
CLASS_SW_MANUAL      = 0x0101
CLASS_SW_AUTO        = 0x0102
CLASS_SW_CYCLE_DONE  = 0x0103

CLASS_NAME = {
    CLASS_NONE:          'NONE',
    CLASS_SW_MANUAL:     'SW_MANUAL',
    CLASS_SW_AUTO:       'SW_AUTO',
    CLASS_SW_CYCLE_DONE: 'CYCLE_DONE',
}

# ---------------------------------------------------------------------------
# Test channels.
#
# SWITCH_A (channel 0) is DELIBERATELY NOT USED. It drives the user's DUT on
# this bench, so a test run must not move it -- not even to force it low during
# quiesce. Everything here works on SWITCH_D, with SWITCH_C as the second
# channel where a test needs to contrast an armed source with a masked one.
# ---------------------------------------------------------------------------

CH_PRI = 3                      # SWITCH_D -- the workhorse
CH_SEC = 2                      # SWITCH_C -- second channel, DELIBERATE

# SWITCH_C is driven on purpose, in three places, and is to be KEPT (user,
# 2026-08-30: "better test coverage"). Do not propose narrowing these to
# SWITCH_D alone:
#
#   gate: armed vs masked      C high/low -- proving a MASKED channel stays
#                              silent needs a second channel that really moves
#   drain: order is FIFO       C is one of the three channels walked in order
#   auto: complete-only        C cycles 2x25 ms -- a channel to cycle while its
#                              transition bits are masked
#
# Expect ~2 single blinks plus one brief flicker on SWITCH_C per full run. Note
# each output also drives a CD4066 control input, not just an indicator LED, so
# a blink briefly closes an analog switch -- harmless here, but the reason
# SWITCH_A (the DUT channel) is never touched at all.

CH_SAFE_MASK = 0xE              # channels 1..3: everything except SWITCH_A

M_PRI_MANUAL = M_SW_D_MANUAL
M_SEC_MANUAL = M_SW_C_MANUAL

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


# ---------------------------------------------------------------------------
# Command wrappers
# ---------------------------------------------------------------------------

class Event(object):
    """One drained event: the '+' payload line, parsed."""

    __slots__ = ('cls', 'chan', 'state', 'tim', 'ms', 'raw')

    def __init__(self, line):
        self.raw = line
        tok = {}
        for field in line.split(','):
            if len(field) >= 2 and field[0].isalpha():
                try:
                    tok[field[0]] = int(field[1:], 16)
                except ValueError:
                    pass
        for key in 'ICSTM':
            if key not in tok:
                raise Failure("event line %r is missing %s" % (line, key))
        self.cls   = tok['I']
        self.chan  = tok['C']
        self.state = tok['S']
        self.tim   = tok['T']
        self.ms    = tok['M']

    def __repr__(self):
        return "<%s ch%d %s tim=%#x ms=%d>" % (
            CLASS_NAME.get(self.cls, "%#06x" % self.cls),
            self.chan, 'HI' if self.state else 'LO', self.tim, self.ms)


def mask_read(con):
    frame = con.command('A')
    check(frame is not None and frame.ok, "A failed: %r" % (frame.raw if frame else None))
    check('M' in frame.tokens, "A reply %r has no M token" % frame.raw)
    return frame.tokens['M']


def mask_write(con, value):
    """Write the mask; returns what the device echoes back."""
    frame = con.command('A,%X' % value)
    check(frame is not None and frame.ok, "A,%X failed: %r"
          % (value, frame.raw if frame else None))
    check('M' in frame.tokens, "A reply %r has no M token" % frame.raw)
    return frame.tokens['M']


def arm(con, bits):
    """Arm exactly `bits`, plus the global enable."""
    want = bits | M_GLOBAL
    got = mask_write(con, want)
    check(got == want, "mask write %#010x read back %#010x" % (want, got))


def disarm(con):
    got = mask_write(con, 0)
    check(got == 0, "mask disarm read back %#010x" % got)


def house(con, sub=None):
    """H[,sub] -- returns dict with N (queued), D (drops), P (puts)."""
    frame = con.command('H' if sub is None else 'H,%s' % sub)
    check(frame is not None and frame.ok, "H failed: %r" % (frame.raw if frame else None))
    for key in 'NDP':
        check(key in frame.tokens, "H reply %r has no %s token" % (frame.raw, key))
    return dict(frame.tokens)


def flush(con):
    house(con, 'F')


def drain(con, max_events=None):
    """D[,max] -- returns (events, remaining, drops)."""
    text = 'D' if max_events is None else 'D,%X' % max_events
    frame = con.command(text)
    check(frame is not None, "no response to %s" % text)
    check(frame.ok, "%s failed: %r" % (text, frame.raw))
    for key in 'KND':
        check(key in frame.tokens, "%s reply %r has no %s token" % (text, frame.raw, key))

    count = frame.tokens['K']
    check(len(frame.payload) == count,
          "%s declared K%d but %d payload lines arrived"
          % (text, count, len(frame.payload)))

    return [Event(line) for line in frame.payload], frame.tokens['N'], frame.tokens['D']


def drain_all(con):
    """Drain until the device reports nothing left from its snapshot."""
    events = []
    for _ in range(64):
        got, remaining, _ = drain(con)
        events.extend(got)
        if remaining == 0:
            return events
    raise Failure("drain_all did not converge")


def ok(con, text):
    """Issue a command and insist it succeeded. Setup steps must be checked --
    a silently rejected W leaves the channel on its STORED parameters, and the
    test then measures something entirely different from what it asked for."""
    frame = con.command(text)
    check(frame is not None, "no response to %s" % text)
    check(frame.ok, "%s was rejected: %r" % (text, frame.raw))
    return frame


def toggle(con, channel, level):
    """Manual set of one channel, via S (select/set/clear)."""
    bit = 1 << channel
    con.command('S,%X,%X,%X' % (bit, bit if level else 0, 0 if level else bit))


def quiesce(con):
    """Known state: disarmed, test channels idle, queue and counters clear.

    Note the CH_SAFE_MASK: this stops and clears channels 1..3 only. SWITCH_A is
    the DUT channel on this bench and is never driven by this suite, not even to
    force it low."""
    disarm(con)
    con.command('X,%X' % CH_SAFE_MASK)
    con.command('S,%X,0,%X' % (CH_SAFE_MASK, CH_SAFE_MASK))
    flush(con)
    house(con, 'R')


# ---------------------------------------------------------------------------
# Tests -- mask
# ---------------------------------------------------------------------------

@test("mask: reads back what was written")
def t_mask_roundtrip(con):
    for value in (0, M_GLOBAL, M_GLOBAL | M_PRI_MANUAL, 0x400000FF | M_GLOBAL):
        got = mask_write(con, value)
        check(got == value, "wrote %#010x, read %#010x" % (value, got))
    disarm(con)


@test("mask: reserved filler bits do not persist")
def t_mask_filler(con):
    """Bits 12..29 have no source behind them. They are storage, not function --
    this pins whether the device keeps them, so a later change is a visible
    diff rather than a surprise."""
    got = mask_write(con, 0xFFFFFFFF)
    check(got & M_GLOBAL, "global enable did not survive an all-ones write")
    check(got & M_CYCLE_COMPLETE, "cycle-complete bit did not survive")
    check(got & 0xFFF == 0xFFF, "source bits did not survive: %#010x" % got)
    disarm(con)


@test("mask: A with no argument does not disturb the value")
def t_mask_read_is_pure(con):
    arm(con, M_PRI_MANUAL)
    first = mask_read(con)
    second = mask_read(con)
    check(first == second, "two reads differed: %#010x then %#010x" % (first, second))
    check(second == (M_PRI_MANUAL | M_GLOBAL),
          "read %#010x, expected %#010x" % (second, M_PRI_MANUAL | M_GLOBAL))
    disarm(con)


# ---------------------------------------------------------------------------
# Tests -- production gating
# ---------------------------------------------------------------------------

@test("gate: disarmed produces nothing")
def t_gate_disarmed(con):
    quiesce(con)
    for _ in range(4):
        toggle(con, CH_PRI, 1)
        toggle(con, CH_PRI, 0)
    events, remaining, _ = drain(con)
    check(events == [] and remaining == 0,
          "disarmed run produced %d events" % len(events))


@test("gate: global enable clear masks an armed source")
def t_gate_global(con):
    quiesce(con)
    mask_write(con, M_PRI_MANUAL)          # source armed, GLOBAL deliberately not
    toggle(con, CH_PRI, 1)
    toggle(con, CH_PRI, 0)
    events, _, _ = drain(con)
    check(events == [], "global disabled but %d events were produced" % len(events))
    disarm(con)


@test("gate: an armed source produces, a masked one does not")
def t_gate_per_channel(con):
    quiesce(con)
    arm(con, M_PRI_MANUAL)                  # SWITCH_D only

    toggle(con, CH_PRI, 1)                  # D -- armed
    toggle(con, CH_SEC, 1)                  # C -- masked
    toggle(con, CH_PRI, 0)
    toggle(con, CH_SEC, 0)

    events = drain_all(con)
    check(len(events) == 2, "expected 2 events from the armed channel, got %d: %r"
          % (len(events), events))
    for ev in events:
        check(ev.chan == CH_PRI, "event from masked channel %d: %r" % (ev.chan, ev))
        check(ev.cls == CLASS_SW_MANUAL, "expected SW_MANUAL, got %r" % ev)
    check([e.state for e in events] == [1, 0],
          "expected HI then LO, got %r" % [e.state for e in events])
    quiesce(con)


@test("gate: masked sources are not counted anywhere")
def t_gate_not_counted(con):
    """Masking means masked -- no record, no counter, no trace."""
    quiesce(con)
    before = house(con)
    for _ in range(8):
        toggle(con, CH_PRI, 1)
        toggle(con, CH_PRI, 0)
    after = house(con)
    check(after['P'] == before['P'],
          "put counter moved %d -> %d for masked sources" % (before['P'], after['P']))
    check(after['D'] == before['D'],
          "drop counter moved %d -> %d for masked sources" % (before['D'], after['D']))


# ---------------------------------------------------------------------------
# Tests -- record content
# ---------------------------------------------------------------------------

@test("record: manual event carries channel, state and both timestamps")
def t_record_content(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)

    toggle(con, CH_PRI, 1)
    events = drain_all(con)

    check(len(events) == 1, "expected 1 event, got %d: %r" % (len(events), events))
    ev = events[0]
    check(ev.cls == CLASS_SW_MANUAL, "class %r" % ev)
    check(ev.chan == CH_PRI, "channel %d, expected %d" % (ev.chan, CH_PRI))
    check(ev.state == 1, "state %d, expected 1" % ev.state)
    check(ev.tim != 0, "TIM2 count is zero -- timestamp not captured")
    check(ev.ms != 0, "tick is zero -- EVENT_TICK_MS() not wired")
    quiesce(con)


@test("record: TIM2 count advances between two events")
def t_record_timestamps_advance(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)

    toggle(con, CH_PRI, 1)
    toggle(con, CH_PRI, 0)
    events = drain_all(con)

    check(len(events) == 2, "expected 2 events, got %d" % len(events))
    delta = (events[1].tim - events[0].tim) & 0xFFFFFFFF
    check(delta != 0, "two events share a TIM2 count -- not per-event capture")
    check(delta < 1000000, "implausible 1 uS delta between two console commands: %d" % delta)
    quiesce(con)


@test("record: a redundant level request still emits (S7)")
def t_record_redundant(con):
    """Semantics are 'a level was commanded', not 'the pin changed'. Driving a
    channel low twice must produce two records, not one."""
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)

    toggle(con, CH_PRI, 0)
    toggle(con, CH_PRI, 0)
    events = drain_all(con)

    check(len(events) == 2,
          "redundant request produced %d events, expected 2" % len(events))
    check(all(e.state == 0 for e in events), "expected both LO: %r" % events)
    quiesce(con)


# ---------------------------------------------------------------------------
# Tests -- drain protocol
# ---------------------------------------------------------------------------

@test("drain: empty queue is K0 with no payload")
def t_drain_empty(con):
    quiesce(con)
    events, remaining, _ = drain(con)
    check(events == [], "empty drain returned %d events" % len(events))
    check(remaining == 0, "empty drain reported N%d" % remaining)


@test("drain: max bounds the batch and remaining reports the rest")
def t_drain_max(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    for _ in range(3):
        toggle(con, CH_PRI, 1)
        toggle(con, CH_PRI, 0)
    # 6 events queued.

    events, remaining, _ = drain(con, 4)
    check(len(events) == 4, "asked for 4, got %d" % len(events))
    check(remaining == 2, "expected N2 after draining 4 of 6, got N%d" % remaining)

    events, remaining, _ = drain(con, 4)
    check(len(events) == 2, "expected the remaining 2, got %d" % len(events))
    check(remaining == 0, "expected N0, got N%d" % remaining)
    quiesce(con)


@test("drain: max larger than the queue drains what there is")
def t_drain_max_over(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    toggle(con, CH_PRI, 1)

    events, remaining, _ = drain(con, 0x20)
    check(len(events) == 1, "expected 1 event, got %d" % len(events))
    check(remaining == 0, "expected N0, got N%d" % remaining)
    quiesce(con)


@test("drain: order is FIFO")
def t_drain_order(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    order = (1, CH_SEC, CH_PRI)             # never SWITCH_A
    for channel in order:
        toggle(con, channel, 1)

    events = drain_all(con)
    check(len(events) == len(order),
          "expected %d events, got %d" % (len(order), len(events)))
    check([e.chan for e in events] == list(order),
          "out of order: %r" % [e.chan for e in events])
    quiesce(con)


@test("drain: consumes -- a second drain is empty")
def t_drain_consumes(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    toggle(con, CH_PRI, 1)

    first = drain_all(con)
    check(len(first) == 1, "expected 1 event, got %d" % len(first))

    events, remaining, _ = drain(con)
    check(events == [], "second drain returned %d events" % len(events))
    check(remaining == 0, "second drain reported N%d" % remaining)
    quiesce(con)


# ---------------------------------------------------------------------------
# Tests -- housekeeping
# ---------------------------------------------------------------------------

@test("house: status reports queue depth without consuming")
def t_house_depth(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    toggle(con, CH_PRI, 1)
    toggle(con, CH_PRI, 0)

    first = house(con)
    check(first['N'] == 2, "expected N2, got N%d" % first['N'])
    second = house(con)
    check(second['N'] == 2, "status consumed events: N%d then N%d"
          % (first['N'], second['N']))

    events = drain_all(con)
    check(len(events) == 2, "expected the 2 events to still be there, got %d" % len(events))
    quiesce(con)


@test("house: flush discards without emitting")
def t_house_flush(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    for _ in range(3):
        toggle(con, CH_PRI, 1)
        toggle(con, CH_PRI, 0)

    check(house(con)['N'] == 6, "expected 6 queued before flush")
    flush(con)
    check(house(con)['N'] == 0, "flush left events queued")

    events, remaining, _ = drain(con)
    check(events == [] and remaining == 0, "drain after flush returned events")
    quiesce(con)


@test("house: put counter tracks produced events")
def t_house_puts(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)

    before = house(con)['P']
    for _ in range(5):
        toggle(con, CH_PRI, 1)
        toggle(con, CH_PRI, 0)
    after = house(con)['P']

    check(after - before == 10,
          "expected 10 puts, counter moved %d -> %d" % (before, after))
    quiesce(con)


@test("house: counter reset zeroes drops and puts")
def t_house_reset(con):
    quiesce(con)
    arm(con, M_SW_MANUAL_ALL)
    toggle(con, CH_PRI, 1)
    check(house(con)['P'] > 0, "expected a non-zero put count to reset")

    after = house(con, 'R')
    check(after['P'] == 0, "put counter did not reset: P%d" % after['P'])
    check(after['D'] == 0, "drop counter did not reset: D%d" % after['D'])
    quiesce(con)


# ---------------------------------------------------------------------------
# Tests -- the automated path
# ---------------------------------------------------------------------------

@test("auto: a cycling run produces AUTO events and a CYCLE_COMPLETE")
def t_auto_cycle(con):
    """The other half of the producer: transitions the HARDWARE makes at compare
    match, reported from the TIM2 ISR."""
    quiesce(con)
    arm(con, M_SW_AUTO_ALL | M_SW_MANUAL_ALL | M_CYCLE_COMPLETE)

    # 25 ms on + 25 ms off = a 50 ms period, which is exactly
    # ACON_MIN_CYCLE_PERIOD_US -- the host-commanded floor. Anything shorter is
    # correctly REFUSED by the W handler, which would leave the channel on its
    # stored parameters and make this test measure the wrong waveform.
    # 3 repeats therefore takes ~150 ms; wait comfortably past that.
    ok(con, 'W,%X,61A8,61A8,3' % CH_PRI)
    ok(con, 'C,%X' % (1 << CH_PRI))
    con.expect_silence(0.5)

    events = drain_all(con)
    classes = [e.cls for e in events]

    check(CLASS_SW_AUTO in classes,
          "no SW_AUTO events from a cycling run: %r" % events)
    check(CLASS_SW_CYCLE_DONE in classes,
          "no CYCLE_COMPLETE event at the end of a run: %r" % events)

    done = [e for e in events if e.cls == CLASS_SW_CYCLE_DONE]
    check(len(done) == 1, "expected exactly 1 CYCLE_COMPLETE, got %d" % len(done))
    check(done[0].chan == CH_PRI,
          "CYCLE_COMPLETE on channel %d, expected %d" % (done[0].chan, CH_PRI))

    # S7: the halt inside the completing ISR forces the output low through the
    # manual choke point, so a manual record rides along with the completion.
    check(CLASS_SW_MANUAL in classes,
          "expected the halt's manual OFF alongside CYCLE_COMPLETE: %r" % events)
    quiesce(con)


@test("auto: cycle-complete can be armed while transitions are masked")
def t_auto_complete_only(con):
    """The reason cycle-complete has its own bit: watch campaign ends during a
    soak without drowning in per-edge transitions."""
    quiesce(con)
    arm(con, M_CYCLE_COMPLETE)              # no AUTO, no MANUAL

    ok(con, 'W,%X,61A8,61A8,2' % CH_SEC)
    ok(con, 'C,%X' % (1 << CH_SEC))
    con.expect_silence(0.5)

    events = drain_all(con)
    check(len(events) >= 1, "expected a CYCLE_COMPLETE, got nothing")
    for ev in events:
        check(ev.cls == CLASS_SW_CYCLE_DONE,
              "transitions were masked but %r came through" % ev)
    quiesce(con)


# ---------------------------------------------------------------------------
# Tests -- persistence
# ---------------------------------------------------------------------------

@test("nvm: mask survives a persist and is readable back")
def t_nvm_persist(con):
    quiesce(con)
    want = M_PRI_MANUAL | M_SW_C_AUTO | M_CYCLE_COMPLETE | M_GLOBAL
    mask_write(con, want)
    con.command('P')
    check(mask_read(con) == want, "mask changed across a persist")
    disarm(con)
    con.command('P')                        # leave the board disarmed on disk


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

    print("event path -- HIL suite")
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
                    quiesce(con)
                    con.drain()
                except Exception:
                    pass
    finally:
        try:
            quiesce(con)
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
