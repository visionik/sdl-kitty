#!/usr/bin/env python3
"""Summarize sdl-kitty present perf lines from a debug log.

Usage:
  SDL_KITTY_DEBUG=1 SDL_KITTY_PERF_ACK=1 ./scripts/run-with-kitty-sdl.sh ./build/dirty_rects_demo
  python3 scripts/kitty_perf_summary.py /tmp/sdl_kitty_debug.log

Parses:
  kitty: present #N ... mode=M ... wire=W raw=R ms_tx=T ms_ack=A ...
  kitty: perf_summary ...
"""
from __future__ import annotations

import argparse
import re
import statistics
import sys
from pathlib import Path

PRESENT_RE = re.compile(
    r"kitty: present #(\d+).*?\bmode=(\S+).*?\bwire=(\d+)\s+raw=(\d+)\s+"
    r"ms_tx=([0-9.]+)\s+ms_ack=([0-9.-]+)"
)
SUMMARY_RE = re.compile(
    r"kitty: perf_summary presents=(\d+)\s+wire_total=(\d+)\s+avg_wire=(\d+)\s+"
    r"avg_ms_tx=([0-9.]+)\s+avg_ms_ack=([0-9.-]+)\s+ack_n=(\d+)\s+skips=(\d+)"
)
PROBE_RE = re.compile(r"kitty: transmit_probe .* -> (\S+)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", nargs="?", default="/tmp/sdl_kitty_debug.log")
    ap.add_argument("--min-frame", type=int, default=2, help="skip first N presents (startup)")
    args = ap.parse_args()
    path = Path(args.log)
    if not path.is_file():
        print(f"missing log: {path}", file=sys.stderr)
        return 1
    text = path.read_text(errors="replace")

    modes = {}
    wires = []
    raws = []
    ms_tx = []
    ms_ack = []
    probe_mode = None
    for m in PROBE_RE.finditer(text):
        probe_mode = m.group(1)
    for m in PRESENT_RE.finditer(text):
        frame = int(m.group(1))
        if frame < args.min_frame:
            continue
        mode = m.group(2)
        w = int(m.group(3))
        r = int(m.group(4))
        tx = float(m.group(5))
        ack = float(m.group(6))
        modes[mode] = modes.get(mode, 0) + 1
        if w > 0:
            wires.append(w)
            raws.append(r)
            ms_tx.append(tx)
            if ack >= 0:
                ms_ack.append(ack)

    print(f"log: {path}")
    if probe_mode:
        print(f"transmit_probe -> {probe_mode}")
    print(f"presents (frame>={args.min_frame} with wire): {len(wires)}")
    print(f"modes: {modes}")
    if not wires:
        print("no present lines with wire= found (need SDL_KITTY_DEBUG=1 rebuild)")
        return 2

    def pct(xs, p):
        s = sorted(xs)
        i = min(len(s) - 1, max(0, int(round((p / 100.0) * (len(s) - 1)))))
        return s[i]

    def block(name, xs, unit=""):
        if not xs:
            print(f"  {name}: (none)")
            return
        print(
            f"  {name}: n={len(xs)} mean={statistics.mean(xs):.2f}{unit} "
            f"p50={pct(xs, 50):.2f}{unit} p95={pct(xs, 95):.2f}{unit} "
            f"min={min(xs):.2f}{unit} max={max(xs):.2f}{unit}"
        )

    print("wire_bytes (APC on PTY):")
    block("wire", [float(x) for x in wires], "")
    print("raw_bytes (pre-b64 payload; zlib = compressed size):")
    block("raw", [float(x) for x in raws], "")
    if raws and sum(raws) > 0:
        ratios = [w / r for w, r in zip(wires, raws) if r > 0]
        print("wire/raw ratio (~4/3 for raw RGB b64; lower can mean zlib helped):")
        block("ratio", ratios, "")
    print("ms_tx (client compress+encode+write):")
    block("ms_tx", ms_tx, "ms")
    print("ms_ack (host Gi=OK wait; needs SDL_KITTY_PERF_ACK=1):")
    block("ms_ack", ms_ack, "ms")

    # Last driver-emitted summary if present
    last = None
    for m in SUMMARY_RE.finditer(text):
        last = m
    if last:
        print(
            f"driver perf_summary: presents={last.group(1)} wire_total={last.group(2)} "
            f"avg_wire={last.group(3)} avg_ms_tx={last.group(4)} "
            f"avg_ms_ack={last.group(5)} ack_n={last.group(6)} skips={last.group(7)}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
