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
| **Graphics `a=q` direct** | OK (often) | OK | OK | OK | OK | probe |
| **SHM `t=s`** | `a=T` works when forced; `a=q` often silent/fail → still prefer direct | OK | OK | OK | fail (macOS #8063) | probe |
| **zlib `o=z`** | OK (prefer) | varies | varies | varies | OK (prefer) | probe |
| **`a=f` frame-edit** | OK / silent≈OK | OK | unimplemented 1.3.1 | OK (tip #13943) | probe OK / display no-op (hash) | probe |
| **Keyboard CSI-u** | OK | OK | OK | OK | needs `enable_kitty_keyboard` | probe |
| **Prefer transmit** | **zlib** | **shm** if ok | **shm** if ok | **shm** if ok | **zlib** | shm if ok |
| **Default FB max** | shm 1280×720; dir 1280×720 | shm 1920×1080; dir 1280×720 | shm 1920×1080; dir 1280×720 | shm 1920×1080; dir 1280×720 | shm 1280×720; dir 1280×720 | shm 1280×720; dir 1280×720 |
| **Refuse direct above** | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB | ~8 MiB |
| **`a=f` max coverage** | 0.40 | 0.40 | 0.40 | 0.40 | 0.40 | 0.40 |
| **`a=f` max pixels** | 2073k | 160k | 160k | 160k | 160k | 160k |
| **Keys on stdin** | yes (required) | yes | yes | yes | yes | yes |
| **ignore ioctl pixels** | yes | no | no | no | no | no |
| **direct a=f when SHM** | yes | yes | no | yes | no | no |
| **Notes** | prefer_direct+zlib; FB≤720p (lib 10MB storage); t=s unsupported | Kitty: SHM FB 1080p / direct 720p; APC ≤256KiB; af_max_* on multi-rect totals | Ghostty ≤1.3.1: SHM 1080p / direct 720p; a=f unimplemented; APC ≤65MiB | Ghostty tip: SHM 1080p / direct 720p; a=f; coalesce=2; APC ≤65MiB | WezTerm: prefer_zlib; upload_skip; coalesce=2; no SHM; a=f off; dirty_union o… | Fallback when TERM_PROGRAM / TERM match nothing |

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
| 2026-10-24 | Rex | prefer_zlib + coalesce=2: fireworks / dvd_sprite / MAME pacman all look great | `-> zlib`; sparse `dirty_bw`; MAME soft often full-frame `a=f` (~2.8 MB) when `af_max_coverage=1.01` — visuals OK, wire heavy on attract/menu |
| 2026-10-24 | Rex | MAME pacman after af_max=0.40 + keep tile-diff bbox | Looks great. Large dirties → `dirty_cpu` zlib (~12–110 KB) vs old ~2.8 MB `a=f`; sparse still `dirty_bw` (e.g. 96×480). `tile-diff FULL bbox=… (kept bbox)` logged. |
| 2026-10-24 | Rex | galaga + mouse_paint under zlib/af_max=0.40 | Looks great. Galaga: sparse `dirty_bw` + zlib `dirty_cpu` on tile-diff FULL. Paint: tiny `dirty_bw` (~8–16 KB), avg_ms_tx~0.14. |
| 2026-10-24 | Ghostty tip | fireworks / dvd_sprite / alpha_blit re-soak | Looks great. `-> shm`, `dirty_bw=on`; dvd sparse a=f; fw bursts → full SHM (`wire=256`); alpha coalesce `6→3`. Quit abort still non-blocking. |
| 2026-10-24 | Kitty.app 0.49.2 | fireworks / dvd_sprite / alpha_blit re-soak | Looks great. `-> shm`, `dirty_bw=on` on fw/dvd. Alpha: full SHM every frame (`wire=256`, no coalesce) — multi-rect totals exceed `af_max_pixels=163840` → intentional SHM fallback (no base64 wall). |

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
| 2026-10-24 | WezTerm 20240203 | dirty/alpha/fw OK at then-960×540; galaga playable but slow; arrows die after ~1–2s | a=f no GPU invalidate → dirty_bw off; SHM #8063. Arrow cause revised next day. |
| 2026-10-25 | WezTerm 20240203 | Arrows fixed via **`enable_kitty_keyboard=true`**; probe `CSI ? u` + warn/fallback; FB cap **1280×720** (e.g. galaga `fb=1050x720`) | Not PTY/graphics load. |
| 2026-10-23 | Kitty.app | a=q/a=f OK; shm; key_probe OK; fireworks needs a=f→SHM on big dirties | default FB was uncapped → base64 |
