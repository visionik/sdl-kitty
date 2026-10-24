# Terminal features & fixes

Per-host capability matrix for the Kitty graphics / keyboard stack.

**Sources of truth:**

- **Canonical table:** [`config/terminals.toml`](../config/terminals.toml) — see [`terminal-profiles.md`](terminal-profiles.md)
- **Runtime:** TOML loader (`SDL_kittyprofile_load.c` + tomlc17) with compiled builtins as fallback; `SDL_KITTY_PROFILES` / pref / base / in-tree default path

Env vars (`SDL_KITTY_TRANSMIT`, `SDL_KITTY_FB_MAX`, …) always override.

<!-- BEGIN GENERATED MATRIX -->

|   | Rex | Kitty.app | Ghostty ≤1.3.1 | Ghostty tip+ | WezTerm | Unknown |
|---|---|---|---|---|---|---|
| **Detect** | `TERM_PROGRAM=rex` | `kitty` or empty + `TERM=xterm-kitty` | `ghostty` ≤`1.3.1` | `ghostty` | `WezTerm` / `wezterm` | else |
| **Graphics `a=q` direct** | OK (often) | OK | OK | OK | TBD | probe |
| **SHM `t=s`** | `a=T` works when forced; `a=q` often silent/fail → still prefer direct | OK | OK | OK | TBD | probe |
| **zlib `o=z`** | reject | varies | varies | varies | TBD | probe |
| **`a=f` frame-edit** | OK / silent≈OK | OK | unimplemented 1.3.1 | OK (tip #13943) | TBD | probe |
| **Keyboard CSI-u** | OK | OK | OK | OK | TBD | probe |
| **Prefer transmit** | **direct** | **shm** if ok | **shm** if ok | **shm** if ok | **direct** | shm if ok |
| **Default FB max** | 1280×720 | 1920×1080 | 1920×1080 | 1920×1080 | 960×540 | 1280×720 |
| **Refuse direct above** | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB |
| **`a=f` max coverage** | **1.01** | 0.40 | 0.40 | 0.40 | 0.40 | 0.40 |
| **`a=f` max pixels** | **2073k** | 160k | 160k | 160k | 160k | 160k |
| **Keys on stdin** | yes (required) | yes | yes | yes | yes | yes |
| **ignore ioctl pixels** | yes | no | no | no | no | no |
| **direct a=f when SHM** | yes | yes | no | yes | no | no |
| **Notes** | prefer direct; full-frame a=f (no a=T flash); t=s often unsupported medium | Kitty 0.49+: SHM + a=f; prefer SHM; direct_af_when_shm; APC ≤256KiB; af_max_*… | Ghostty ≤1.3.1: SHM on; a=f unimplemented → dirty_bw off (full a=T); APC ≤65MiB | Ghostty tip/≥1.3.2: SHM + a=f (#13943); direct_af_when_shm; APC ≤65MiB | WezTerm: prefer direct (macOS SHM #8063); a=f no display invalidate → dirty_b… | Fallback when TERM_PROGRAM / TERM match nothing |

*Generated from `config/terminals.toml` by `scripts/terminals_toml_to_md.py`. Do not edit by hand — re-run the script.*

<!-- END GENERATED MATRIX -->

## Policy

1. **Probe** what the host answers (`a=q`, SHM, `a=f`).
2. **Profile** supplies defaults / quirks for that host family.
3. **Never** apply one host’s emergency fix as a silent global (document in this table + profile).
4. Soak results update this table; then tighten profile numbers.

## Env overrides (all hosts)

| Env | Effect |
|---|---|
| `SDL_KITTY_TRANSMIT=direct\|shm\|zlib` | Force medium |
| `SDL_KITTY_FB_MAX=WxH` / `0` | Cap / uncapped |
| `SDL_KITTY_FB_FIXED=WxH` | Exact FB |
| `SDL_KITTY_DIRTY_BW=0\|1` | Force sparse `a=f` off/on |
| `SDL_KITTY_DEBUG=1` | Init / present logs (stderr + SDL_Log) |
| `SDL_KITTY_PROFILES` | Path or `:`-list of TOML profile files |

## Soak log

| Date | Host | Caps / key_probe / soft demos | Notes |
|---|---|---|---|
| 2026-10-23 | Rex | a=q/a=f OK; keys OK; MAME soft OK | prefer direct; stdin keys |
| 2026-10-23 | Rex | `SDL_KITTY_TRANSMIT=shm` solid_present looked OK | leaked `Gi=1,p=1;OK` into next prompt → drain-on-exit |
| 2026-10-23 | Rex | retest: drain clean; probe `shm=fail` but forced `a=T` t=s presents OK | honor force (clear shm_failed); keep prefer_direct default |
| 2026-10-23 | Rex | soak suite: solid direct/SHM, dirty_bw, dvd_sprite, fireworks bursts, keys | see soak notes below |

### Rex soak suite (2026-10-23, post profile + drain)

| Demo | Log result |
|---|---|
| `solid_present` default | `term=rex prefer_direct=1 shm=skip → direct`; presents ok; auto-exits ~90 frames; no `Gi=` leak |
| `solid_present` `TRANSMIT=shm` | probe `shm=fail` but forced t=s; no fallback warn; presents ok; drain clean when exited |
| `dirty_rects_demo` | `dirty_bw=on`; #0 full then sparse `dirty_bw` ~53×39 |
| `dvd_sprite` | full then `dirty_bw` ~197×100; no base64 |
| `fireworks` + Space bursts | sparse rockets; high cov → `dirty_cpu` / `mode=full` (a=f gate); clean exit |
| `key_probe` (rex send-key) | arrows/z/x/c/q OK; digits often miss under injection |
| `key_probe` (physical) | **5/1 + all arrows** DOWN/UP/repeat via CSI; chords OK; clean quit — visuals OK on all demos |
| `alpha_blit` Kitty | SHM a=f blanked; multi-rect direct a=f → base64 wall; **no direct a=f on SHM sessions** (full SHM a=T) |
| 2026-10-23 | Ghostty | key_probe OK | — |
| 2026-10-24 | Ghostty.app 1.3.1 | caps: direct+SHM OK; solid/dirty(`DIRTY_BW=0`)/alpha/fireworks/galaga OK; SHM full a=T smooth | **a=f unimplemented** in 1.3.1. Profile `ghostty-1.3` (version ≤1.3.1): dirty_bw off. |
| 2026-10-24 | Ghostty Tip `1.3.2-main+22f100c63` | dirty_rects/alpha/fireworks/galaga OK; `dirty_bw=on`; sparse a=f when coverage low | Real tip from tip.files.ghostty.org (GH tip DMG was stale=1.3.1). Parser fix: `Gi=,r=1;OK`. Galaga mixes dirty_bw + full SHM. |
| 2026-10-24 | Kitty.app 0.49.2 | dirty_rects/alpha/fireworks/galaga OK; SHM + dirty_bw; alpha no base64 spew | Source-aligned: `direct_af_when_shm`; multi-rect `af_max_*` totals; `apc_chunk_bytes=261888` (Kitty 256KiB APC max). Ghostty APC=65MiB. |
| 2026-10-24 | WezTerm 20240203 | dirty/alpha/fw OK at 960×540; galaga playable but slow; arrows die after ~1–2s | Prefer direct (macOS SHM #8063); a=f no GPU invalidate → dirty_bw off; PTY flooded by full direct a=T. |
| 2026-10-23 | Kitty.app | a=q/a=f OK; shm; key_probe OK; fireworks needs a=f→SHM on big dirties | default FB was uncapped → base64 |
| — | WezTerm | TBD | — |
