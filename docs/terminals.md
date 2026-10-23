# Terminal features & fixes

Per-host capability matrix for the Kitty graphics / keyboard stack.  
**Code source of truth:** `SDL_kittyprofile.c` (`KITTY_TerminalProfile`).  
Env vars (`SDL_KITTY_TRANSMIT`, `SDL_KITTY_FB_MAX`, …) always override.

Detection order: `TERM_PROGRAM` → `TERM=xterm-kitty` with empty `TERM_PROGRAM` → unknown.

| | Rex | Kitty.app | Ghostty | WezTerm | Unknown |
|---|---|---|---|---|---|
| **Detect** | `TERM_PROGRAM=rex` | `kitty` or empty + `TERM=xterm-kitty` | `ghostty` | `WezTerm` / `wezterm` | else |
| **Graphics `a=q` direct** | OK (often) | OK | OK (soak) | TBD | probe |
| **SHM `t=s`** | `a=T` works when forced; `a=q` often silent/fail → still prefer direct | OK | OK (typical) | TBD | probe |
| **zlib `o=z`** | reject | varies | varies | TBD | probe |
| **`a=f` frame-edit** | OK / silent≈OK | OK | OK (soak) | TBD | probe |
| **Prefer transmit** | **direct** | **shm** if ok | **shm** if ok | shm if ok | shm if ok |
| **Default FB max** | 1280×720 | 1920×1080 | 1920×1080 | 1280×720 | 1280×720 |
| **Refuse direct above** | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB |
| **`a=f` max coverage** | **0.95** | 0.40 | 0.40 | 0.40 | 0.40 |
| **`a=f` max pixels** | **~921k (1280×720)** | 160k | 160k | 160k | 160k |
| **Keys on stdin** | yes (required) | yes | yes | yes | yes if stdin tty |
| **Keyboard CSI-u** | OK | OK | OK | TBD | probe |
| **Notes** | Force direct; high a=f limits (burst flash); stdin keys | Strict a=f gate (base64 flood); prefer SHM | Metal; good protocol peer | Cross-check host | Conservative |

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
| 2026-10-23 | Ghostty | key_probe OK | — |
| 2026-10-23 | Kitty.app | a=q/a=f OK; shm; key_probe OK; fireworks needs a=f→SHM on big dirties | default FB was uncapped → base64 |
| — | WezTerm | TBD | — |
