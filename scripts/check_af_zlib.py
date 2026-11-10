#!/usr/bin/env python3
"""Does a compressed Kitty a=f edit (o=z) draw? Run in the terminal under test, look, Ctrl-C to quit.

Evidence for terminal-facts' kitty.frame_edit_zlib: the patches below must look as described.

A gray 320x160 image, then four in-place edits of its root frame (r=1), as sdl-kitty sends them:
  A  zlib a=f, one chunk          blue    at (10,10)  80x60
  B  raw a=f (control)            green   at (110,10) 80x60
  C  zlib a=f, many chunks        red noise at (210,10) 100x140
  D  zlib a=f over A              yellow  at (50,40)  40x40, its bottom 10 px below A
Errors come back (q=1: OK replies suppressed, errors kept) and are printed under the picture.
"""
import base64, os, random, select, sys, termios, time, tty, zlib

W, H, ID = 320, 160, 4242
fd = os.open("/dev/tty", os.O_RDWR)
chunks_sent = {}


def apc(ctrl, data, name):
    b = base64.b64encode(data)
    parts = [b[i:i + 4096] for i in range(0, len(b), 4096)] or [b""]
    chunks_sent[name] = len(parts)
    for k, c in enumerate(parts):
        m = 1 if k < len(parts) - 1 else 0
        head = f"{ctrl},m={m}" if k == 0 else f"m={m}"
        os.write(fd, b"\x1b_G" + head.encode() + b";" + c + b"\x1b\\")


def solid(w, h, rgb):
    return bytes(rgb) * (w * h)


def edit(name, x, y, w, h, pixels, z):
    body = zlib.compress(pixels, 1) if z else pixels
    oz = ",o=z" if z else ""
    apc(f"a=f,f=24,t=d,i={ID},r=1,X=1,x={x},y={y},s={w},v={h},q=1{oz}", body, name)


old = termios.tcgetattr(fd)
tty.setraw(fd)
try:
    os.write(fd, b"\x1b[2J\x1b[H")
    apc(f"a=T,f=24,t=d,i={ID},s={W},v={H},c=64,r=16,C=1,q=1,o=z", zlib.compress(solid(W, H, (128, 128, 128))), "base")
    time.sleep(0.3)
    edit("A", 10, 10, 80, 60, solid(80, 60, (0, 0, 255)), True)
    edit("B", 110, 10, 80, 60, solid(80, 60, (0, 200, 0)), False)
    rnd = random.Random(1)
    noise = bytes(v for _ in range(100 * 140) for v in (255, rnd.randrange(80), rnd.randrange(80)))
    edit("C", 210, 10, 100, 140, noise, True)
    time.sleep(0.2)
    edit("D", 50, 40, 40, 40, solid(40, 40, (255, 220, 0)), True)
    replies = b""
    end = time.time() + 2
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            replies += os.read(fd, 4096)
finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old)

out = "\x1b[18;1H"
out += "A blue (zlib) | B green (raw) | C red noise (zlib, %d chunks) | D yellow over A, 10 px past its bottom (zlib)\r\n" % chunks_sent["C"]
out += "chunks: %r\r\n" % chunks_sent
out += "replies: %r\r\n" % replies
out += "AFZLIB_TEST_DONE\r\n"
os.write(fd, out.encode())
try:
    time.sleep(3600)
except KeyboardInterrupt:
    os.write(fd, b"\x1b_Ga=d,d=I,i=%d,q=2\x1b\\" % ID)
