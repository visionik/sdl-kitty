# Terminal features & fixes

Per-host capability matrix for the Kitty graphics / keyboard stack.

**Sources of truth:**

- **Canonical table:** [`config/terminals.toml`](../config/terminals.toml) — see [`terminal-profiles.md`](terminal-profiles.md)
- **Runtime:** TOML loader (`SDL_kittyprofile_load.c` + tomlc17) with compiled builtins as fallback; `SDL_KITTY_PROFILES` / pref / base / in-tree default path

Env vars (`SDL_KITTY_TRANSMIT`, `SDL_KITTY_FB_MAX`, …) always override.

<!-- BEGIN GENERATED MATRIX -->

|   | Rex | Kitty.app | Ghostty | WezTerm | Unknown |
|---|---|---|---|---|---|
| **Detect** | `TERM_PROGRAM=rex` | `kitty` or empty + `TERM=xterm-kitty` | `ghostty` | `WezTerm` / `wezterm` | else |
| **Graphics `a=q` direct** | OK (often) | OK | OK (soak) | TBD | probe |
| **SHM `t=s`** | `a=T` works when forced; `a=q` often silent/fail → still prefer direct | OK | OK (typical) | TBD | probe |
| **zlib `o=z`** | reject | varies | varies | TBD | probe |
| **`a=f` frame-edit** | OK / silent≈OK | OK | OK (soak) | TBD | probe |
| **Keyboard CSI-u** | OK | OK | OK | TBD | probe |
| **Prefer transmit** | **direct** | **shm** if ok | **shm** if ok | shm if ok | shm if ok |
| **Default FB max** | 1280×720 | 1920×1080 | 1920×1080 | 1280×720 | 1280×720 |
| **Refuse direct above** | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB |
| **`a=f` max coverage** | **0.95** | 0.40 | 0.40 | 0.40 | 0.40 |
| **`a=f` max pixels** | **~921k (1280×720)** | 160k | 160k | 160k | 160k |
| **Keys on stdin** | yes (required) | yes | yes | yes | yes |
| **ignore ioctl pixels** | yes | no | no | no | no |
| **direct a=f when SHM** | yes | no | no | no | no |
| **Notes** | libghostty default disables t=s (a=q → EINVAL unsupported medium); prefer direct | Prefer SHM; skip direct a=f on SHM sessions (base64 wall); SHM a=f blanked hi… | Ghostty.app enables SHM via allWithTempDir; same a=f policy as Kitty for now | Conservative until soaked | Fallback when TERM_PROGRAM / TERM match nothing |

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
| 2026-10-23 | Kitty.app | a=q/a=f OK; shm; key_probe OK; fireworks needs a=f→SHM on big dirties | default FB was uncapped → base64 |
| — | WezTerm | TBD | — |
