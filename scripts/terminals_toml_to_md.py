#!/usr/bin/env python3
"""Regenerate the capability matrix in docs/terminals.md from config/terminals.toml.

Preserves everything outside the GENERATED MATRIX markers.
Requires Python 3.11+ (tomllib) or pip install tomli.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib  # type: ignore
    except ImportError:
        print("Need tomllib (3.11+) or tomli", file=sys.stderr)
        sys.exit(1)

BEGIN = "<!-- BEGIN GENERATED MATRIX -->"
END = "<!-- END GENERATED MATRIX -->"

# Narrative soak / probe rows that are not in TOML (kept beside profile numbers).
PROBE_ROWS = [
    # columns: rex, kitty, ghostty-1.3, ghostty (tip+), wezterm, unknown
    ("Graphics `a=q` direct", "OK (often)", "OK", "OK", "OK", "OK", "probe"),
    ("SHM `t=s`", "`a=T` works when forced; `a=q` often silent/fail → still prefer direct", "OK", "OK", "OK", "fail (macOS #8063)", "probe"),
    ("zlib `o=z`", "OK (prefer)", "varies", "varies", "varies", "OK (prefer)", "probe"),
    ("`a=f` frame-edit", "OK / silent≈OK", "OK", "unimplemented 1.3.1", "OK (tip #13943)", "probe OK / display no-op (hash)", "probe"),
    ("Keyboard CSI-u", "OK", "OK", "OK", "OK", "needs `enable_kitty_keyboard`", "probe"),
]

HOST_ORDER = ["rex", "kitty", "ghostty-1.3", "ghostty", "wezterm", "unknown"]
HOST_LABEL = {
    "rex": "Rex",
    "kitty": "Kitty.app",
    "ghostty-1.3": "Ghostty ≤1.3.1",
    "ghostty": "Ghostty tip+",
    "wezterm": "WezTerm",
    "unknown": "Unknown",
}


def resolve(defaults: dict, term: dict) -> dict:
    out = dict(defaults)
    out.update({k: v for k, v in term.items() if k != "match"})
    return out


def detect_cell(term: dict) -> str:
    m = term.get("match") or {}
    parts = []
    tp = m.get("term_program") or []
    if tp:
        if len(tp) == 1:
            parts.append(f"`TERM_PROGRAM={tp[0]}`" if term.get("id") == "rex" else "`" + "` / `".join(tp) + "`")
        else:
            parts.append(" / ".join(f"`{x}`" for x in tp))
    empty = m.get("term_when_program_empty") or []
    if empty:
        parts.append("empty + " + " / ".join(f"`TERM={x}`" for x in empty))
    if term.get("id") == "unknown":
        return "else"
    if not parts:
        return "—"
    if term.get("id") == "kitty" and empty:
        return "`kitty` or empty + `TERM=xterm-kitty`"
    return " / ".join(parts) if term.get("id") != "rex" else parts[0]


def fmt_bool(v: bool) -> str:
    return "yes" if v else "no"


def fmt_fb(p: dict) -> str:
    s = p.get("default_fb_max", "1280x720")
    if isinstance(s, str):
        return s.replace("x", "×")
    return str(s)


def fmt_bytes(n: int) -> str:
    if abs(n - 1920 * 1080 * 4) < 64:
        return "~8 MiB"
    if n >= 1024 * 1024 and n % (1024 * 1024) == 0:
        return f"~{n // (1024 * 1024)} MiB"
    if n >= 1024 * 1024:
        return f"~{n / (1024 * 1024):.1f} MiB"
    if n >= 1024 and n % 1024 == 0:
        return f"~{n // 1024} KiB"
    return str(n)


def fmt_pixels(n: int) -> str:
    if n == 1280 * 720:
        return f"**~{n // 1000}k (1280×720)**" if n > 200000 else f"**~{n} (1280×720)**"
    if n == 160 * 1024:
        return "160k"
    if n >= 1000:
        return f"{n // 1000}k"
    return str(n)


def prefer_cell(p: dict) -> str:
    # Matches ProbeTransmitModes: prefer_zlib wins over raw direct when set.
    if p.get("prefer_zlib"):
        return "**zlib**"
    if p.get("prefer_direct"):
        return "**direct**"
    tid = p.get("id") or ""
    if tid == "kitty" or tid == "ghostty" or tid.startswith("ghostty-"):
        return "**shm** if ok"
    return "shm if ok"


def detect_version_suffix(term: dict) -> str:
    m = term.get("match") or {}
    parts = []
    vmin = m.get("term_program_version_min")
    vmax = m.get("term_program_version_max")
    if vmin:
        parts.append(f"≥`{vmin}`")
    if vmax:
        parts.append(f"≤`{vmax}`")
    return (" " + " ".join(parts)) if parts else ""


def notes_cell(p: dict) -> str:
    n = (p.get("notes") or "").strip()
    if not n:
        if p.get("id") == "unknown":
            return "Conservative"
        return "—"
    # Shorten for table
    if len(n) > 80:
        n = n[:77] + "…"
    return n


def build_matrix(data: dict) -> str:
    defaults = data.get("defaults") or {}
    terminals = data.get("terminal") or []
    by_id = {}
    for t in terminals:
        tid = t.get("id")
        if not tid:
            continue
        p = resolve(defaults, t)
        p["id"] = tid
        by_id[tid] = (t, p)

    cols = []
    for hid in HOST_ORDER:
        if hid in by_id:
            cols.append(by_id[hid])
        else:
            cols.append(({"id": hid, "match": {}}, resolve(defaults, {"id": hid})))

    headers = [" "] + [HOST_LABEL.get(c[0].get("id", ""), c[0].get("id", "?")) for c in cols]
    lines = [
        BEGIN,
        "",
        "| " + " | ".join(headers) + " |",
        "|" + "|".join(["---"] * len(headers)) + "|",
    ]

    def row(label: str, cells: list[str]) -> None:
        lines.append("| " + " | ".join([f"**{label}**"] + cells) + " |")

    row("Detect", [detect_cell(c[0]) + detect_version_suffix(c[0]) for c in cols])

    for label, *probe in PROBE_ROWS:
        # probe tuple aligned to HOST_ORDER
        row(label, list(probe))

    row("Prefer transmit", [prefer_cell(c[1]) for c in cols])
    row("Default FB max", [fmt_fb(c[1]) for c in cols])
    row("Refuse direct above", [fmt_bytes(int(c[1].get("max_direct_bytes", 0))) for c in cols])

    cov_cells = []
    for _, p in cols:
        cov = float(p.get("af_max_coverage", 0.4))
        s = f"{cov:.2f}"
        if cov >= 0.9:
            s = f"**{s}**"
        cov_cells.append(s)
    row("`a=f` max coverage", cov_cells)

    px_cells = []
    for _, p in cols:
        n = int(p.get("af_max_pixels", 160 * 1024))
        s = fmt_pixels(n)
        if float(p.get("af_max_coverage", 0)) >= 0.9 and "**" not in s:
            s = f"**{s}**"
        px_cells.append(s)
    row("`a=f` max pixels", px_cells)

    row(
        "Keys on stdin",
        [
            "yes (required)"
            if c[1].get("id") == "rex" and c[1].get("keys_on_stdin")
            else ("yes" if c[1].get("keys_on_stdin") else "no")
            for c in cols
        ],
    )
    row(
        "ignore ioctl pixels",
        [fmt_bool(bool(c[1].get("ignore_ioctl_pixels"))) for c in cols],
    )
    row(
        "direct a=f when SHM",
        [fmt_bool(bool(c[1].get("direct_af_when_shm"))) for c in cols],
    )
    row("Notes", [notes_cell(c[1]) for c in cols])

    lines.append("")
    lines.append(f"*Generated from `config/terminals.toml` by `scripts/terminals_toml_to_md.py`. Do not edit by hand — re-run the script.*")
    lines.append("")
    lines.append(END)
    return "\n".join(lines)


def patch_markdown(md: str, matrix: str) -> str:
    if BEGIN in md and END in md:
        pattern = re.compile(
            re.escape(BEGIN) + r".*?" + re.escape(END),
            re.DOTALL,
        )
        return pattern.sub(matrix, md)
    # First run: replace from "Detection order:" (or first table) through just before ## Policy
    policy = md.find("\n## Policy")
    if policy < 0:
        return md.rstrip() + "\n\n" + matrix + "\n"
    start = md.find("\nDetection order:")
    if start < 0:
        start = md.find("\n| ")
    if start < 0:
        start = policy
    return md[:start] + "\n" + matrix + "\n" + md[policy:]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    ap.add_argument("--toml", type=Path, default=root / "config" / "terminals.toml")
    ap.add_argument("--md", type=Path, default=root / "docs" / "terminals.md")
    ap.add_argument("--check", action="store_true", help="exit 1 if md would change")
    args = ap.parse_args()

    data = tomllib.loads(args.toml.read_text(encoding="utf-8"))
    matrix = build_matrix(data)
    old = args.md.read_text(encoding="utf-8") if args.md.exists() else ""
    new = patch_markdown(old, matrix)
    if args.check:
        if new != old:
            print(f"{args.md} is stale; run scripts/terminals_toml_to_md.py", file=sys.stderr)
            return 1
        print("ok: matrix up to date")
        return 0
    args.md.write_text(new, encoding="utf-8")
    print(f"wrote {args.md}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
