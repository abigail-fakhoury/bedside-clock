#!/usr/bin/env python3
"""Print the clock's USB log line by line.

    monitor.py [SECONDS] [--reset] [--send LINE]

Opens the Espressif USB serial port (vendor 0x303A), prints every line as it arrives, and stops
after SECONDS (default: run until Ctrl-C). --reset pulses RTS to restart the
chip first. --send writes LINE (plus a newline) once the port is open.
"""
import argparse
import glob
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seconds", nargs="?", type=float, help="stop after this many seconds")
    ap.add_argument("--reset", action="store_true", help="restart the chip via RTS")
    ap.add_argument("--send", metavar="LINE", help="send this line to the device")
    args = ap.parse_args()

    # Only the clock: other devices (a monitor, a dock) also show up as usbmodem
    from serial.tools.list_ports import comports
    ports = sorted(p.device for p in comports() if p.vid == 0x303A)
    if not ports:
        sys.exit("no Espressif USB serial port (is the clock plugged in and awake?)")
    port = serial.Serial()
    port.port = ports[0]
    port.baudrate = 115200
    port.timeout = 0.2
    # Leave DTR and RTS at pyserial's defaults (both asserted): the USB
    # bridge resets the chip only when RTS is asserted with DTR released
    port.open()
    print(f"[monitor] {ports[0]}", flush=True)

    if args.reset:
        port.dtr = False
        port.rts = True
        time.sleep(0.1)
        port.rts = False

    if args.send:
        time.sleep(0.5 if args.reset else 0.1)
        port.write(args.send.encode() + b"\n")
        port.flush()

    end = time.monotonic() + args.seconds if args.seconds else None
    pending = b""
    try:
        while end is None or time.monotonic() < end:
            chunk = port.read(256)
            if not chunk:
                continue
            pending += chunk
            *lines, pending = pending.split(b"\n")
            for line in lines:
                print(line.rstrip(b"\r").decode("utf-8", "replace"), flush=True)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as e:
        # USB disappears when the chip resets or sleeps
        print(f"[monitor] port lost: {e}", flush=True)
    if pending:
        print(pending.decode("utf-8", "replace"), flush=True)


if __name__ == "__main__":
    main()
