#!/usr/bin/env python3
"""molly-count serial console (USB-Serial-JTAG needs DTR asserted for
host->device writes; pyserial does not assert it for us reliably on all
platforms, so we set it explicitly).

Usage:
  serial_cmd.py                       # just listen for 20s
  serial_cmd.py --time 30 --send GET --send "SLEEP 60"
Opens the port, asserts DTR, prints everything the board sends; sends the
given command lines (CRLF) at 1s intervals after a 1s initial delay.
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--time", type=float, default=20.0, help="seconds to listen")
    ap.add_argument("--send", action="append", default=[], help="command to send")
    args = ap.parse_args()

    s = serial.Serial(args.port, args.baud, timeout=0.2)
    s.dtr = True  # USB-Serial-JTAG: DTR must be asserted to accept host data
    print(f"# {args.port} open, DTR asserted, listening {args.time}s", flush=True)

    deadline = time.monotonic() + args.time
    pending = list(args.send)
    next_send = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        if pending and time.monotonic() >= next_send:
            cmd = pending.pop(0)
            s.write(cmd.encode() + b"\r\n")
            print(f">>> {cmd}", flush=True)
            next_send = time.monotonic() + 1.0
        try:
            data = s.read(4096)
        except serial.SerialException:
            # board went back to sleep mid-listen: the port vanishing is the
            # normal true-deep-sleep signature, not an error
            print("\n# port vanished (board asleep)", flush=True)
            return
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
    s.close()


if __name__ == "__main__":
    main()
