"""
acon.py -- host-side driver for the SwitchTester automation console.

Speaks the SCRIPT-mode protocol described in
Docs/planning/automation-console-plan.md: enter on 0xDA, leave on 0xA5, send
one command per CR-terminated line, read one framed response.

The point of this module is that a test (or any host tool) never touches the
wire format directly. It calls `command("W,0,7A120,7A120,0")` and gets back a
Frame it can assert on.

Two protocol rules are honoured here because getting them wrong is how a host
desynchronises:

  * Dispatch is by SIGIL, never by position. Any line may be an async event
    ('*') or ignorable noise ('#'); those are routed to callbacks and the read
    continues, so an event arriving in the window between "host commits to
    send" and "device answers" cannot be mistaken for the response.

  * A response is ONE line unless its header carries K<n>, in which case
    exactly n '+' payload lines follow. Reading a declared count -- rather than
    scanning for a terminator -- is what makes a truncated frame detectable.
"""

import time

try:
    import serial
except ImportError:                                     # pragma: no cover
    raise SystemExit("pyserial is required:  python -m pip install pyserial")


ENTER_SENTINEL = 0xDA
EXIT_SENTINEL  = 0xA5
CANCEL         = 0x03           # Ctrl-C, quit alias

# Monitor-mode flow control. Only meaningful inside M; anywhere else they are
# ordinary bytes and would be read as an opcode.
XON            = 0x11
XOFF           = 0x13

SIG_OK      = '='
SIG_ERR     = '!'
SIG_PAYLOAD = '+'
SIG_EVENT   = '*'
SIG_NOISE   = '#'

DEFAULT_BAUD = 921600


class ProtocolError(Exception):
    """The device said something the grammar does not allow."""


class Frame:
    """One parsed device->host response.

    Attributes
      sigil    '=' or '!'
      op       the echoed opcode, in caret notation for control characters
      ok       True for '=', False for '!'
      code     error mnemonic (RNG / ARG / UNK / BUSY / NVM / OVF / DUP / TMO),
               None on success frames
      tokens   {KEY: int} for fields matching <KEY><hex>
      fields   every field after the opcode, unparsed, in order. V's identity
               response uses free text rather than tokens, so this is where
               that lands.
      payload  the '+' lines, without their sigil, when the header declared K<n>
      raw      the header line as received
    """

    def __init__(self, line, payload=None):
        self.raw = line
        self.payload = payload or []

        if not line or line[0] not in (SIG_OK, SIG_ERR):
            raise ProtocolError("not a response frame: %r" % line)

        self.sigil = line[0]
        self.ok = (self.sigil == SIG_OK)

        parts = line[1:].split(',')
        self.op = parts[0]
        rest = parts[1:]

        # Grammar: '!<op>,<CODE>[,<tok>]...' -- on an error frame the first
        # field is always the code, never a token.
        self.code = None
        if not self.ok and rest:
            self.code = rest[0]
            rest = rest[1:]

        self.fields = list(rest)
        self.tokens = {}
        for field in rest:
            if len(field) >= 2 and field[0].isalpha() and field[0].isupper():
                try:
                    self.tokens[field[0]] = int(field[1:], 16)
                except ValueError:
                    pass                # free text (V's identity strings)

    def __repr__(self):
        return "<Frame %s>" % self.raw


class AutomationConsole:
    """Session against one SwitchTester over a serial port."""

    def __init__(self, port, baud=DEFAULT_BAUD, timeout=1.0, trace=False):
        self.port_name = port
        self.baud = baud
        self.timeout = timeout
        self.trace = trace
        self.ser = None
        self.events = []            # '*' frames seen, in arrival order
        self.noise = []             # '#' lines seen

    # ---------------------------------------------------------------- port --

    def open(self):
        self.ser = serial.Serial(self.port_name, self.baud, timeout=self.timeout)
        time.sleep(0.15)
        self.drain()
        return self

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    def drain(self):
        """Discard anything already buffered. Used between test phases so one
        test's leftovers cannot be read as the next one's response."""
        self.ser.reset_input_buffer()
        time.sleep(0.05)
        while self.ser.in_waiting:
            self.ser.read(self.ser.in_waiting)
            time.sleep(0.05)

    # ------------------------------------------------------------- raw i/o --

    def write_raw(self, data):
        if isinstance(data, str):
            data = data.encode('ascii')
        if self.trace:
            print("    >> %r" % data)
        self.ser.write(data)
        self.ser.flush()

    def read_line(self):
        """One line, CR/LF stripped. Returns None on timeout."""
        raw = self.ser.readline()
        if not raw:
            return None
        line = raw.decode('ascii', errors='replace').rstrip('\r\n')
        if self.trace and line:
            print("    << %r" % line)
        return line

    # ------------------------------------------------------------- framing --

    def read_frame(self, timeout=None):
        """Read until a response frame arrives, routing events and noise aside.

        Returns None if nothing arrives before the deadline -- which is a
        legitimate result, since 'the device must stay silent' is something
        several tests assert.
        """
        deadline = time.time() + (timeout if timeout is not None else self.timeout * 3)

        while time.time() < deadline:
            line = self.read_line()
            if line is None or line == '':
                continue

            sigil = line[0]

            if sigil == SIG_EVENT:
                self.events.append(line)
                continue
            if sigil == SIG_NOISE:
                self.noise.append(line)
                continue
            if sigil == SIG_PAYLOAD:
                # A payload line with no header preceding it: the host lost
                # sync, or the device emitted a frame it did not declare.
                raise ProtocolError("orphan payload line: %r" % line)
            if sigil not in (SIG_OK, SIG_ERR):
                raise ProtocolError("unknown sigil in %r" % line)

            frame = Frame(line)

            # K<n> in the header means exactly n '+' lines follow.
            count = frame.tokens.get('K')
            if count:
                payload = []
                for _ in range(count):
                    nxt = self.read_line()
                    if nxt is None:
                        raise ProtocolError(
                            "declared K%d but only %d payload lines arrived"
                            % (count, len(payload)))
                    if not nxt.startswith(SIG_PAYLOAD):
                        raise ProtocolError("expected '+' payload, got %r" % nxt)
                    payload.append(nxt[1:])
                frame.payload = payload

            return frame

        return None

    def monitor(self, timeout_ms, cancel_after=None, slack=1.0):
        """Run monitor mode (M) and collect what it streams.

        Returns (ack, events, terminator). `events` are the '*' lines emitted
        during THIS call, not the session-long self.events list.

        cancel_after: seconds to wait before sending a cancel byte, or None to
        let the device's own timeout end it. slack: extra seconds to wait for
        the terminator beyond the requested timeout.

        Note the two-frame shape. M answers immediately with an ack, streams,
        then answers again with a terminator -- so it is the one op that cannot
        be driven with command(), which reads exactly one frame.
        """
        mark = len(self.events)
        self.write_raw('M,%X\r' % timeout_ms)

        ack = self.read_frame()
        if ack is None:
            raise ProtocolError("no ack from M,%X" % timeout_ms)
        if not ack.ok:
            return ack, [], None            # refused; nothing streamed

        if cancel_after is not None:
            time.sleep(cancel_after)
            self.write_raw('x')             # 'any byte' -- consumed as the cancel

        term = self.read_frame(timeout=(timeout_ms / 1000.0) + slack)
        return ack, self.events[mark:], term

    def suspend(self):
        """XOFF -- pause monitor-mode emission without leaving it."""
        self.write_raw(bytes([XOFF]))

    def resume(self):
        """XON -- resume emission."""
        self.write_raw(bytes([XON]))

    def expect_silence(self, seconds=0.5):
        """Assert the device says nothing for a while. Returns what it said
        anyway, so a failure can report the offending line."""
        end = time.time() + seconds
        heard = []
        while time.time() < end:
            line = self.read_line()
            if line:
                heard.append(line)
        return heard

    # ------------------------------------------------------------ protocol --

    def command(self, text, timeout=None):
        """Send one command line and return its response frame."""
        self.write_raw(text + '\r')
        return self.read_frame(timeout=timeout)

    def enter(self):
        """Enter the console in SCRIPT mode. Returns the ready frame."""
        self.write_raw(bytes([ENTER_SENTINEL]))
        frame = self.read_frame()
        if frame is None or frame.op != '~':
            raise ProtocolError("no session banner after ENTER, got %r" % frame)
        return frame

    def leave(self, how='sentinel'):
        """Leave the console. `how` is 'sentinel', 'quit' or 'cancel' -- all
        three are supposed to produce the same BYE frame."""
        if how == 'sentinel':
            self.write_raw(bytes([EXIT_SENTINEL]))
        elif how == 'quit':
            self.write_raw('Q\r')
        elif how == 'cancel':
            self.write_raw(bytes([CANCEL]) + b'\r')
        else:
            raise ValueError(how)
        return self.read_frame()

    # ------------------------------------------------------------ niceties --

    def state(self):
        """(level, mode, run) bitmaps via R."""
        f = self.command('R')
        return f.tokens.get('L'), f.tokens.get('M'), f.tokens.get('R')

    def quiesce(self):
        """Put the switches in a known state: nothing cycling, all forced low."""
        self.command('X,F')
        self.command('S,F,0,F')
