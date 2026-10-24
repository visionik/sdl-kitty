#!/usr/bin/env python3
"""Probe Kitty graphics / keyboard capabilities on the current tty.

No SDL — raw APC/CSI queries. Safe to run in Kitty, Ghostty, WezTerm, Rex.

Usage:
  python3 scripts/kitty_graphics_caps_probe.py
  python3 scripts/kitty_graphics_caps_probe.py 2>/tmp/caps.txt

Prints a short report to stderr (CRLF-safe for raw-ish hosts).
"""
from __future__ import annotations

import base64
import mmap
import os
import re
import select
import sys
import termios
import time
import tty


def eprint(msg: str) -> None:
    sys.stderr.write(msg.replace("\n", "\r\n"))
    if not msg.endswith("\n"):
        sys.stderr.write("\r\n")
    sys.stderr.flush()


def drain(fd: int, timeout: float = 0.05) -> bytes:
    out = b""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], max(0.0, end - time.monotonic()))
        if not r:
            break
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        out += chunk
        end = time.monotonic() + 0.02  # keep draining bursts
    return out


def await_reply(fd: int, pattern: re.Pattern[bytes], attempts: int = 40) -> bytes:
    buf = b""
    for _ in range(attempts):
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            chunk = os.read(fd, 4096)
            if chunk:
                buf += chunk
                if pattern.search(buf):
                    return buf
        else:
            if buf and pattern.search(buf):
                return buf
    return buf


def b64_pixel_rgb() -> str:
    # 1x1 red RGB24
    return base64.b64encode(bytes([255, 0, 0])).decode("ascii")


def _classify_gq(buf: bytes) -> str:
    if re.search(rb";OK\033\\|;OK\x1b\\|OK", buf):
        return "OK"
    if re.search(rb";\d+:", buf) or b"EINVAL" in buf or b"ENOENT" in buf or b"EBADF" in buf:
        return "ERROR"
    if b"unsupported medium" in buf.lower():
        return "ERROR"
    if not buf:
        return "SILENT/NONE"
    return "SILENT/NONE"


def _shm_wrap_lib() -> "ctypes.CDLL":
    """Non-variadic shm_open wrapper.

    Apple Silicon ctypes cannot pass the mode arg to variadic shm_open(3)
    correctly, which yields an unopenable object (EACCES / Ghostty
    EINVAL: invalid data). A tiny fixed-arity wrapper avoids that.
    """
    import ctypes
    import subprocess
    import tempfile

    lib_path = os.path.join(tempfile.gettempdir(), "sdl_kitty_shm_wrap.dylib")
    src = (
        "#include <sys/mman.h>\n"
        "#include <fcntl.h>\n"
        "int sk_shm_open(const char *name, int oflag, int mode) {\n"
        "  return shm_open(name, oflag, mode);\n"
        "}\n"
        "int sk_shm_unlink(const char *name) { return shm_unlink(name); }\n"
    )
    need_build = True
    if os.path.exists(lib_path):
        # Rebuild if missing symbols would fail load; cheap to rebuild when stale.
        try:
            age = time.time() - os.path.getmtime(lib_path)
            need_build = age > 86400
        except OSError:
            need_build = True
    if need_build:
        src_path = lib_path + ".c"
        with open(src_path, "w", encoding="utf-8") as f:
            f.write(src)
        subprocess.check_call(
            ["cc", "-dynamiclib", "-o", lib_path, src_path],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    lib = ctypes.CDLL(lib_path)
    lib.sk_shm_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    lib.sk_shm_open.restype = ctypes.c_int
    lib.sk_shm_unlink.argtypes = [ctypes.c_char_p]
    return lib


def _shm_open_probe(name: str, payload: bytes) -> int | None:
    """Create a POSIX shm object with payload. Returns fd, or None on failure."""
    wrap = _shm_wrap_lib()
    name_b = name.encode("ascii")
    wrap.sk_shm_unlink(name_b)
    fd = wrap.sk_shm_open(name_b, os.O_CREAT | os.O_RDWR | os.O_EXCL, 0o600)
    if fd < 0:
        return None
    try:
        os.ftruncate(fd, len(payload))
    except OSError:
        os.close(fd)
        wrap.sk_shm_unlink(name_b)
        return None
    mm = mmap.mmap(fd, len(payload))
    mm[:] = payload
    mm.close()
    return fd


def _shm_unlink(name: str) -> None:
    wrap = _shm_wrap_lib()
    wrap.sk_shm_unlink(name.encode("ascii"))


def main() -> int:
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        eprint("need a real tty on stdin+stdout")
        return 1

    fd_in = sys.stdin.fileno()
    fd_out = sys.stdout.fileno()
    old = termios.tcgetattr(fd_in)
    term = os.environ.get("TERM_PROGRAM", "")
    term_env = os.environ.get("TERM", "")

    eprint("=== kitty graphics caps probe ===")
    eprint(f"TERM_PROGRAM={term!r}  TERM={term_env!r}")
    eprint(f"isatty0={os.isatty(fd_in)} isatty1={os.isatty(fd_out)}")

    try:
        tty.setraw(fd_in)
        # Keep ISIG for Ctrl+C
        attrs = termios.tcgetattr(fd_in)
        attrs[3] |= termios.ISIG
        termios.tcsetattr(fd_in, termios.TCSANOW, attrs)

        drain(fd_in, 0.02)

        # --- size ---
        os.write(fd_out, b"\033[14t\033[16t\033[18t")
        size_buf = await_reply(fd_in, re.compile(rb"\033\[\d+;\d+(?:;\d+)*t"), 30)
        eprint(f"size replies: {size_buf!r}")

        # --- keyboard flags query ---
        os.write(fd_out, b"\033[?u\033[c")
        kb_buf = await_reply(fd_in, re.compile(rb"\033\[\?\d+u|\033\[\?.*c"), 30)
        eprint(f"keyboard/DA: {kb_buf!r}")

        # --- a=q direct RGB 1x1 ---
        pix = b64_pixel_rgb()
        qid = 900001
        cmd = f"\033_Ga=q,i={qid},f=24,s=1,v=1,t=d;{pix}\033\\\033[c".encode()
        drain(fd_in, 0.02)
        os.write(fd_out, cmd)
        qbuf = await_reply(fd_in, re.compile(rb"\033_G.*\033\\|\033\\\\\033\\"), 40)
        direct = _classify_gq(qbuf)
        eprint(f"a=q direct RGB (t=d): {direct}  raw={qbuf[:120]!r}")

        # --- a=q SHM RGB 1x1 (t=s) — Ghostty.app enables; Rex/libghostty often rejects ---
        shm = "SKIP"
        shm_name = f"/sdl-kitty-caps-{os.getpid()}"
        shm_fd = _shm_open_probe(shm_name, bytes([255, 0, 0]))
        qid_s = 900004
        if shm_fd is None:
            shm = "CREATE_FAIL"
            eprint(f"a=q SHM (t=s): {shm} (shm_open failed)")
        else:
            name_b64 = base64.b64encode(shm_name.encode("ascii")).decode("ascii")
            scmd = (
                f"\033_Ga=q,t=s,f=24,s=1,v=1,i={qid_s},S=3;{name_b64}\033\\\033[c"
            ).encode()
            drain(fd_in, 0.02)
            os.write(fd_out, scmd)
            sbuf = await_reply(fd_in, re.compile(rb"\033_G.*\033\\|\033\\\\\033\\"), 40)
            shm = _classify_gq(sbuf)
            eprint(f"a=q SHM (t=s): {shm}  name={shm_name} raw={sbuf[:140]!r}")
            os.close(shm_fd)
            # Protocol: terminal unlinks on success; client unlinks on failure.
            if shm != "OK":
                _shm_unlink(shm_name)

        # --- tiny visible place (a=T) then delete ---
        pid = 900002
        place = (
            f"\033_Ga=T,f=24,s=1,v=1,i={pid},q=2,C=1;"
            f"{pix}\033\\"
        ).encode()
        os.write(fd_out, place)
        time.sleep(0.05)
        # frame-edit probe: create quiet image then a=f
        fid = 900003
        create = f"\033_Ga=t,f=24,s=2,v=2,i={fid},q=2;{base64.b64encode(bytes([0, 255, 0] * 4)).decode()}\033\\".encode()
        os.write(fd_out, create)
        time.sleep(0.05)
        drain(fd_in, 0.05)
        edit = (
            f"\033_Ga=f,i={fid},r=1,X=1,x=0,y=0,s=1,v=1,f=24,t=d;{pix}\033\\\033[c"
        ).encode()
        os.write(fd_out, edit)
        ebuf = await_reply(fd_in, re.compile(rb"\033_G"), 40)
        frame_edit = _classify_gq(ebuf)
        if frame_edit == "SILENT/NONE":
            frame_edit = "SILENT/NONE (often OK on Rex)"
        eprint(f"a=f frame-edit: {frame_edit}  raw={ebuf[:120]!r}")

        # cleanup images
        for i in (pid, fid, qid, qid_s):
            os.write(fd_out, f"\033_Ga=d,d=I,i={i},q=2\033\\".encode())
        drain(fd_in, 0.05)

        eprint("--- summary ---")
        eprint(f"direct transmit a=q: {direct}")
        eprint(f"SHM transmit a=q:    {shm}")
        eprint(f"frame edit a=f:      {frame_edit}")
        eprint("Expect: Ghostty.app SHM OK; Rex SHM ERROR (unsupported medium); Kitty SHM OK.")
        eprint("For what *sdl-kitty* chose, also run:")
        eprint("  SDL_KITTY_DEBUG=1 scripts/run-with-kitty-sdl.sh build/solid_present 2>/tmp/sdl-caps-ghostty.err")
        eprint("  rg 'transmit_probe|frame_edit|init |term=' /tmp/sdl-caps-ghostty.err")
    finally:
        try:
            os.write(fd_out, b"\033_Ga=d,d=A,q=2\033\\")  # delete all
        except OSError:
            pass
        termios.tcsetattr(fd_in, termios.TCSADRAIN, old)
        eprint("done (tty restored)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
