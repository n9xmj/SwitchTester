#!/usr/bin/env python3
"""test_nvm_persist.py -- SPI-flash persistence-across-reset test for nvmparams.

The 28-test suite (test_nvm.py --backend flash) proves the flash driver against
the nvmparams contract, but it never power-cycles: a re-init there still finds
the store powered. This test proves true non-volatility --

    commit a known value  ->  HARDWARE-RESET the MCU  ->  reconnect  ->
    re-init REQUIRE_VALID  ->  read the value back

If the value survives a reset (which discards the RAM pool buffer), the commit
really reached the flash and init really reloaded it.

The reset is an SWD reset via STM32_Programmer_CLI, pinned to the Nucleo's
ST-Link serial number so it can never disturb the second (DUT) probe on the bus.

    python scripts/hil/test_nvm_persist.py
    python scripts/hil/test_nvm_persist.py --port COM3
"""
import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acon import AutomationConsole, DEFAULT_BAUD

PROGRAMMER = os.environ.get(
    'STM32_PROGRAMMER_CLI',
    r'C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe')

POLICY_FORMAT_IF_INVALID = 1
POLICY_REQUIRE_VALID     = 2
NVM_ERROR_POOL_FORMATTED = -10

TEST_ID  = 0x0010
TEST_VAL = 0xCAFE1234


def bench_default(key, fallback=None):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        '..', 'bench.defaults.json')
    try:
        with open(path) as fh:
            return json.load(fh).get(key, fallback)
    except OSError:
        return fallback


def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v


def nvm(con, *args):
    return con.command('N,' + ','.join(str(a) for a in args))


def status_of(frame):
    assert frame is not None and frame.ok, "bad frame: %r" % (frame and frame.raw)
    return s32(frame.tokens['S'])


def reset_target(sn):
    cmd = [PROGRAMMER, '-c', 'port=SWD', 'sn=%s' % sn, '-rst']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("programmer reset failed:\n%s\n%s" % (r.stdout, r.stderr))


def phase_write(port, baud):
    con = AutomationConsole(port, baud)
    con.open()
    try:
        con.enter()
        assert nvm(con, 'P', '1').ok, "select flash backend"
        nvm(con, 'Z')
        nvm(con, 'W', 'FF')                                  # blank the sector
        st = status_of(nvm(con, 'R', '%X' % POLICY_FORMAT_IF_INVALID))
        assert st == NVM_ERROR_POOL_FORMATTED, "expected POOL_FORMATTED, got %d" % st
        status_of(nvm(con, 'C', '%X' % TEST_ID, '%X' % TEST_VAL))   # create w/ value
        status_of(nvm(con, 'K'))                             # commit to flash
        g = nvm(con, 'G', '%X' % TEST_ID)
        assert g.tokens.get('V') == TEST_VAL, "pre-reset readback wrong: %r" % g.raw
        print("  wrote id=0x%04X val=0x%08X, committed to flash" % (TEST_ID, TEST_VAL))
    finally:
        try: con.leave()
        except Exception: pass
        con.close()


def phase_verify(port, baud):
    con = AutomationConsole(port, baud)
    con.open()
    try:
        con.enter()
        assert nvm(con, 'P', '1').ok, "select flash backend"
        # REQUIRE_VALID: init succeeds ONLY if the flash holds a valid pool.
        st = status_of(nvm(con, 'R', '%X' % POLICY_REQUIRE_VALID))
        assert st >= 0, "REQUIRE_VALID re-init failed after reset: %d" % st
        g = nvm(con, 'G', '%X' % TEST_ID)
        val = g.tokens.get('V')
        assert val == TEST_VAL, \
            "value did NOT survive reset: got 0x%08X, want 0x%08X" % (val or 0, TEST_VAL)
        print("  after reset: REQUIRE_VALID loaded the pool, id=0x%04X reads 0x%08X"
              % (TEST_ID, val))
    finally:
        try: con.leave()
        except Exception: pass
        con.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', default=bench_default('com_port', 'COM3'))
    ap.add_argument('--baud', type=int, default=bench_default('baud', DEFAULT_BAUD))
    ap.add_argument('--sn',   default=bench_default('stlink_sn'))
    args = ap.parse_args()

    if not args.sn:
        print("no ST-Link SN (set stlink_sn in bench.defaults.json or pass --sn)")
        return 2

    print("nvmparams -- SPI-flash persistence across reset")
    print("port %s @ %d, ST-Link %s\n" % (args.port, args.baud, args.sn))

    try:
        print("phase 1: write + commit")
        phase_write(args.port, args.baud)

        print("resetting target (SWD)...")
        reset_target(args.sn)
        time.sleep(2.0)                     # let the app reboot + VCP re-enumerate

        print("phase 2: verify after reset")
        phase_verify(args.port, args.baud)
    except Exception as exc:
        print("\nFAIL: %s" % exc)
        return 1

    print("\nPASS -- committed value survived a hardware reset")
    return 0


if __name__ == '__main__':
    sys.exit(main())
