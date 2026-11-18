#!/usr/bin/env python3
"""Send the clock its settings over USB.

    provision.py [--real] [--ssid NAME]

Reads the Wi-Fi password (Keychain item reading-clock-wifi) and the
OpenRouter key (openrouter-api-key) from the macOS Keychain, the Readwise
token from READWISE_TEST_API_TOKEN (READWISE_API_TOKEN with --real; taken
from an interactive zsh when not in this environment), and the time zone
from /etc/localtime. Sends one "CFG <key> <value>" line per setting and
waits for "OK <key>". Prints key names and lengths only, never a value.

The clock takes settings while KEY stay-awake is on, or while it idles dark
with this Mac attached.
"""
import argparse
import os
import subprocess
import sys
import time

import serial
from serial.tools.list_ports import comports

ZONEINFO = "/usr/share/zoneinfo/"
REPLY_TIMEOUT_S = 5


def keychain(service):
    r = subprocess.run(["security", "find-generic-password", "-s", service, "-w"],
                       capture_output=True, text=True)
    if r.returncode != 0 or not r.stdout.strip():
        sys.exit(f"Keychain item {service!r} not found")
    return r.stdout.rstrip("\n")


def env_token(name):
    value = os.environ.get(name, "")
    if not value:
        # ~/.zshrc exports it from the Keychain in interactive shells only
        r = subprocess.run(["zsh", "-ic", f'printf %s "${name}"'], capture_output=True,
                           text=True, stdin=subprocess.DEVNULL)
        value = r.stdout.strip()
    if not value:
        sys.exit(f"{name} is not set")
    return value


def time_zone():
    # macOS links it to /var/db/timezone/zoneinfo/<zone>; Linux to
    # /usr/share/zoneinfo/<zone>
    link = os.readlink("/etc/localtime")
    if "/zoneinfo/" not in link:
        sys.exit(f"/etc/localtime ({link}) does not name a zone")
    iana = link.split("/zoneinfo/", 1)[1]
    # TZif version 2+ files end with the POSIX rule on the last line
    with open(ZONEINFO + iana, "rb") as f:
        lines = [l for l in f.read().split(b"\n") if l.strip()]
    posix = lines[-1].decode("ascii").strip()
    if not posix or len(posix) > 64:
        sys.exit(f"no POSIX TZ rule at the end of {ZONEINFO + iana}")
    return iana, posix


def clock_port():
    # Only the clock: other devices (a monitor, a dock) also show up as usbmodem
    port = next((c.device for c in comports() if c.vid == 0x303A), "")
    if not port:
        sys.exit("no Espressif USB serial port (is the clock plugged in and awake?)")
    return port


def send(port, key, value):
    """Sends one setting; True on "OK <key>"."""
    port.reset_input_buffer()
    port.write(f"CFG {key} {value}\n".encode("ascii"))
    port.flush()
    pending = b""
    end = time.monotonic() + REPLY_TIMEOUT_S
    while time.monotonic() < end:
        pending += port.read(256)
        *lines, pending = pending.split(b"\n")
        for line in lines:
            line = line.rstrip(b"\r").decode("utf-8", "replace")
            if line == f"OK {key}":
                return True
            if line in (f"ERR {key}", "ERR ?"):
                return False
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--real", action="store_true",
                    help="use the real Readwise token (READWISE_API_TOKEN), not the test one")
    ap.add_argument("--ssid", required=True, help="Wi-Fi network name (2.4 GHz)")
    args = ap.parse_args()

    iana, posix = time_zone()
    settings = [
        ("wifi_ssid", args.ssid),
        ("wifi_pass", keychain("reading-clock-wifi")),
        ("rw_token", env_token("READWISE_API_TOKEN" if args.real else "READWISE_TEST_API_TOKEN")),
        ("or_key", keychain("openrouter-api-key")),
        ("tz_posix", posix),
        ("tz_iana", iana),
    ]
    for key, value in settings:
        if not value.isascii() or "\n" in value or "\r" in value:
            sys.exit(f"{key}: value is not one line of ASCII")

    name = clock_port()
    port = serial.Serial()
    port.port = name
    port.baudrate = 115200
    port.timeout = 0.2
    # pyserial's defaults (DTR and RTS asserted) do not reset the chip
    port.open()
    print(f"[provision] {name}, Readwise token: {'REAL' if args.real else 'test'}", flush=True)
    time.sleep(0.2)
    # A bare newline first ends any partial line already in the clock's
    # buffer, so the first CFG line is never merged into it (and logged)
    port.write(b"\n")
    port.flush()
    time.sleep(0.1)

    failed = []
    for key, value in settings:
        ok = send(port, key, value)
        print(f"{'OK ' if ok else 'ERR'} {key} ({len(value)} chars)", flush=True)
        if not ok:
            failed.append(key)
    port.close()
    if failed:
        sys.exit("not stored: " + ", ".join(failed) +
                 " (turn KEY stay-awake on, or let the clock go dark on USB, and retry)")
    print("all six settings stored")


if __name__ == "__main__":
    main()
