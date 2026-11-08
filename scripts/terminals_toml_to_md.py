#!/usr/bin/env python3
"""Regenerate the matrix in docs/terminals.md from config/terminals.toml and config/sdl-kitty.toml.

    python3 scripts/terminals_toml_to_md.py          # rewrite the matrix
    python3 scripts/terminals_toml_to_md.py --check  # fail if docs/terminals.md is stale

One column per terminals.toml row, resolved as the driver does it (SDL_kittyprofile_load.c):
[defaults], then [terminal.<id>] for the row and each row it extends, then the facts that
decide a setting. Preserves everything outside the GENERATED MATRIX markers. Python 3.11+.
"""

from __future__ import annotations

import argparse
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = "<!-- BEGIN GENERATED MATRIX -->"
END = "<!-- END GENERATED MATRIX -->"
OS_NAMES = {"macos": "macOS", "linux": "Linux", "windows": "Windows", "bsd": "BSD"}
APC_HEADROOM = 256


def flatten(table: dict, prefix: str = "") -> dict:
    out = {}
    for key, value in table.items():
        if not prefix and key in ("id", "name", "extends", "match", "evidence", "notes"):
            continue
        if isinstance(value, dict):
            out.update(flatten(value, f"{prefix}{key}."))
        else:
            out[prefix + key] = value
    return out


def chain(rows: dict, row_id: str) -> list[dict]:
    """The row and the rows it extends, base first."""
    out = []
    row = rows.get(row_id)
    while row is not None:
        out.append(row)
        row = rows.get(row.get("extends"))
    return out[::-1]


def resolve(rows: dict, tuning: dict, row_id: str | None) -> tuple[dict, dict]:
    """(facts, settings) as the driver resolves them for this row's conditions."""
    settings = dict(tuning.get("defaults", {}))
    facts: dict = {}
    for row in chain(rows, row_id) if row_id else []:
        facts.update(flatten(row))
        settings.update(tuning.get("terminal", {}).get(row["id"], {}))
    if row_id is None:
        settings.update(tuning.get("terminal", {}).get("unknown", {}))
    if "kitty.frame_edit_reply" in facts:
        settings["silent_af_ok"] = not facts["kitty.frame_edit_reply"]
    if "window.ioctl_pixels" in facts:
        settings["ignore_ioctl_pixels"] = not facts["window.ioctl_pixels"]
    if facts.get("kitty.max_escape_bytes", 0) > APC_HEADROOM:
        settings["apc_chunk_bytes"] = facts["kitty.max_escape_bytes"] - APC_HEADROOM
    if facts.get("kitty.shm") is False:
        settings["prefer_direct"] = True
    settings["skip_af_probe"] = facts.get("kitty.frame_edit") is False
    if settings["skip_af_probe"]:
        settings["direct_af_when_shm"] = False
    return facts, settings


def heading(rows: dict, row: dict | None) -> str:
    if row is None:
        return "Unknown"
    if "extends" not in row:
        return row["name"]
    base = chain(rows, row["id"])[0]
    match = row.get("match", {})
    parts = []
    if "version_min" in match:
        parts.append(f"≥ {match['version_min']}")
    if "version_max" in match:
        parts.append(f"≤ {match['version_max']}")
    if "os" in match:
        parts.append("(" + ", ".join(OS_NAMES[each] for each in match["os"]) + ")")
    return f"{base['name']} {' '.join(parts)}"


def recognised(rows: dict, row: dict | None) -> str:
    if row is None:
        return "no row matches"
    if "extends" in row:
        return f"as {chain(rows, row['id'])[0]['name']}"
    match = row.get("match", {})
    parts = [f"XTVERSION `{name}`" for name in match.get("xtversion", [])]
    parts += [f"`{name}`" for name in match.get("env", [])]
    parts += [f"`TERM_PROGRAM={name}`" for name in match.get("term_program", [])]
    return "; ".join(parts)


def fact(value) -> str:
    if value is None:
        return "?"
    if value is True:
        return "yes"
    if value is False:
        return "**no**"
    return size(value) if isinstance(value, int) else f"`{value}`"


def size(n: int) -> str:
    for unit, scale in (("MiB", 1048576), ("MB", 1000000), ("KiB", 1024), ("kB", 1000)):
        if n % scale == 0:
            return f"{n // scale} {unit}"
    return f"{n:,} B"


def fb(s: dict) -> str:
    def norm(text: str) -> str:
        return (text or s.get("default_fb_max", "")).replace("x", "×")

    return f"shm {norm(s.get('default_fb_max_shm'))}; dir {norm(s.get('default_fb_max_direct'))}"


def transmit(s: dict) -> str:
    if s.get("prefer_direct"):
        return "**zlib**" if s.get("prefer_zlib") else "**direct**"
    return "**shm** if ok"


def yes_no(value) -> str:
    return "yes" if value else "no"


def build_matrix(facts_data: dict, tuning: dict) -> str:
    rows = {row["id"]: row for row in facts_data.get("terminal", [])}
    columns: list[dict | None] = [*rows.values(), None]
    resolved = [resolve(rows, tuning, row["id"] if row else None) for row in columns]

    lines = [BEGIN, ""]
    lines.append("| | " + " | ".join(heading(rows, row) for row in columns) + " |")
    lines.append("|---|" + "---|" * len(columns))

    def row(label: str, cells) -> None:
        lines.append(f"| **{label}** | " + " | ".join(cells) + " |")

    row("Recognised by", [recognised(rows, each) for each in columns])
    row("*Facts* (terminals.toml)", ["" for _ in columns])
    for key, label in (
        ("kitty.frame_edit", "`a=f` shows"),
        ("kitty.frame_edit_reply", "`a=f` replies"),
        ("kitty.shm", "SHM `t=s`"),
        ("kitty.max_escape_bytes", "Longest escape code"),
        ("kitty.storage_bytes", "Image storage"),
        ("window.ioctl_pixels", "ioctl pixel sizes"),
        ("keyboard.kitty_protocol_config", "Kitty keyboard needs"),
    ):
        row(label, [fact(f.get(key)) for f, _ in resolved])
    row("*sdl-kitty* (sdl-kitty.toml + facts)", ["" for _ in columns])
    row("Prefer transmit", [transmit(s) for _, s in resolved])
    row("Default FB max", [fb(s) for _, s in resolved])
    row("`a=f` probe", ["skipped" if s["skip_af_probe"] else ("silent = ok" if s.get("silent_af_ok") else "needs OK")
                        for _, s in resolved])
    row("`a=f` max coverage / pixels",
        [f"{float(s.get('af_max_coverage', 0.4)):.2f} / {int(s.get('af_max_pixels', 0)):,}" for _, s in resolved])
    row("Direct `a=f` when SHM", [yes_no(s.get("direct_af_when_shm")) for _, s in resolved])
    row("Coalesce gap", [f"{s.get('coalesce_gap_px', 0)} px" for _, s in resolved])
    row("APC chunk", [size(int(s.get("apc_chunk_bytes", 0))) for _, s in resolved])
    row("Ignore ioctl pixels", [yes_no(s.get("ignore_ioctl_pixels")) for _, s in resolved])
    lines.append("")
    lines.append("`?` unknown: the driver probes, or uses its tuning. Evidence for each fact is in "
                 "[`config/terminals.toml`](../config/terminals.toml).")
    lines.append("*Generated by `scripts/terminals_toml_to_md.py`; don't edit by hand.*")
    lines.append("")
    lines.append(END)
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--facts", type=Path, default=ROOT / "config" / "terminals.toml")
    ap.add_argument("--tuning", type=Path, default=ROOT / "config" / "sdl-kitty.toml")
    ap.add_argument("--md", type=Path, default=ROOT / "docs" / "terminals.md")
    ap.add_argument("--check", action="store_true", help="exit 1 if the md would change")
    args = ap.parse_args()

    matrix = build_matrix(tomllib.loads(args.facts.read_text()), tomllib.loads(args.tuning.read_text()))
    old = args.md.read_text(encoding="utf-8")
    start, end = old.index(BEGIN), old.index(END) + len(END)
    new = old[:start] + matrix + old[end:]
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
