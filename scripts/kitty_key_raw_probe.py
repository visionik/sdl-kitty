#!/usr/bin/env python3
"""Raw Kitty keyboard protocol probe (no SDL).

Enables CSI > flags u on the tty, then prints hex dumps of whatever arrives
on stdin (and optionally stdout) until Esc Esc or timeout.

Usage:
  python3 scripts/kitty_key_raw_probe.py              # flags=11 (1|2|8)
  python3 scripts/kitty_key_raw_probe.py --flags 31   # all progressive bits
  python3 scripts/kitty_key_raw_probe.py --seconds 20

Press physical keys on Rex and Ghostty; compare dumps. Esc Esc to quit early.
"""
from __future__ import annotations

import argparse
import os
import select
import sys
import termios
import time
import tty


def hexdump(data: bytes) -> str:
    parts = []
    for b in data:
        if 32 <= b < 127 and b not in (0x5B,):  # printable-ish
            parts.append(f"{b:02x}('{chr(b)})")
        elif b == 0x1B:
            parts.append("1b ESC")
        else:
            parts.append(f"{b:02x}")
    return " ".join(parts)


def summarize(data: bytes) -> str:
    """Short human hint for common sequences."""
    if data == b"\x1b":
        return "bare ESC"
    if data.startswith(b"\x1b[") and data.endswith(b"u"):
        return f"CSI-u {data[2:-1].decode('ascii', 'replace')}u"
    if data.startswith(b"\x1b[") and data[-1:] in b"ABCDHFPQRS~":
        return f"legacy CSI …{chr(data[-1])}"
    if data.startswith(b"\x1bO") and len(data) >= 3:
        return f"SS3 {chr(data[2])}"
    if len(data) == 1 and 32 <= data[0] < 127:
        return f"ASCII '{chr(data[0])}'"
    return "bytes"


def eprint(msg: str) -> None:
    """Write to stderr with CRLF — raw mode does not imply CR on LF."""
    sys.stderr.write(msg.replace("\n", "\r\n"))
    if not msg.endswith("\n"):
        sys.stderr.write("\r\n")
    sys.stderr.flush()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--flags", type=int, default=11, help="Kitty progressive flags (default 11=1|2|8)")
    ap.add_argument("--seconds", type=float, default=30.0, help="Listen duration")
    ap.add_argument("--also-stdout", action="store_true", help="Also select/read stdout")
    args = ap.parse_args()

    if not sys.stdin.isatty():
        print("stdin is not a tty", file=sys.stderr)
        return 1

    fd_in = sys.stdin.fileno()
    fd_out = sys.stdout.fileno()
    old = termios.tcgetattr(fd_in)
    esc_esc = 0

    try:
        tty.setraw(fd_in)
        # Non-blocking-ish reads via select; VMIN/VTIME already raw.
        enable = f"\033[>{args.flags}u".encode()
        # Query current flags then DA1 for support detection.
        query = b"\033[?u\033[c"
        os.write(fd_out, enable + query)
        eprint(
            f"kitty_key_raw_probe: flags={args.flags} "
            f"isatty0={os.isatty(fd_in)} isatty1={os.isatty(fd_out)} "
            f"TERM_PROGRAM={os.environ.get('TERM_PROGRAM', '')!r}\n"
            f"Press keys for {args.seconds:.0f}s (Esc Esc to quit).\n"
            f"Expect CSI-u for digits/letters under flag 8."
        )

        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            fds = [fd_in]
            if args.also_stdout and fd_out != fd_in:
                fds.append(fd_out)
            r, _, _ = select.select(fds, [], [], 0.25)
            if not r:
                continue
            for fd in r:
                try:
                    data = os.read(fd, 256)
                except OSError as e:
                    eprint(f"read fd{fd}: {e}")
                    continue
                if not data:
                    continue
                which = "fd0" if fd == fd_in else f"fd{fd}"
                eprint(f"{which} {summarize(data):40s} | {hexdump(data)}")
                # Esc Esc quit
                for b in data:
                    if b == 0x1B:
                        esc_esc += 1
                        if esc_esc >= 2:
                            deadline = 0
                            break
                    else:
                        esc_esc = 0
    finally:
        try:
            os.write(fd_out, b"\033[<u")  # pop keyboard mode
        except OSError:
            pass
        termios.tcsetattr(fd_in, termios.TCSADRAIN, old)
        # Cooked mode again — plain \n is fine, but CRLF stays safe on Rex.
        eprint("restored tty / popped keyboard mode")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
