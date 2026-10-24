# Terminal profile config (TOML)

**Canonical file:** [`config/terminals.toml`](../config/terminals.toml)  
**Runtime:** `SDL_kittyprofile_load.c` parses TOML with vendored [tomlc17](https://github.com/cktan/tomlc17) (MIT) under `third_party/SDL/src/video/kitty/tomlc17/`. Compiled builtins remain the fallback when no file loads.

Profiles hold **defaults and quirks**, not live capability claims. Probes (`a=q`, SHM, `a=f`) decide availability. Env `SDL_KITTY_*` always wins last.

## Resolve order

1. Copy `[defaults]`
2. First matching `[[terminal]]` overlays **only keys it sets**
3. If nothing matches → `id = "unknown"` if present, else bare defaults
4. Apply env overrides

Across files: first successful file replaces the table; further files in `SDL_KITTY_PROFILES` (colon-separated) overlay by `id`. Search when `SDL_KITTY_PROFILES` is unset: user pref path → `SDL_GetBasePath()` → compile-time `KITTY_DEFAULT_PROFILES_PATH` (repo `config/terminals.toml` for in-tree builds).

## `[defaults]` / `[[terminal]]` keys

### Host policy (per terminal or default)

| Key | Type | Meaning |
|---|---|---|
| `prefer_direct` | bool | Prefer direct over SHM when both probe OK; skip SHM probe |
| `prefer_zlib` | bool | When not on SHM, prefer `o=z` zlib direct over raw base64 |
| `force_transmit` | string | Force `direct`/`zlib`/`shm` after env (Exp 2 Ghostty A/B) |
| `upload_skip` | bool | Skip wire when scratch FNV matches last upload (Exp 6) |
| `coalesce_gap_px` | int | Merge dirty rects within this pixel gap before multi a=f (0=off) |
| `default_fb_max` | `"WxH"` | Aspect-fit FB cap when `SDL_KITTY_FB_MAX` unset; empty/`0` = no default |
| `max_direct_bytes` | int | Refuse direct a=T / a=f above this raw size |
| `af_max_coverage` | float | Skip union a=f when dirty coverage ≥ this |
| `af_max_pixels` | int | Skip a=f when dirty px ≥ this (also per-rect multi-a=f cap) |
| `keys_on_stdin` | bool | Read keyboard from stdin when it is a tty |
| `silent_af_ok` | bool | a=f probe: silent (no APC reply) counts as success |
| `ignore_ioctl_pixels` | bool | Ignore `TIOCGWINSZ` xpixel/ypixel (Rex HiDPI stub) |
| `direct_af_when_shm` | bool | Allow direct a=f while session transmit mode is SHM |
| `dirty_at_overlays` | bool | Experiment: sparse dirty as stacked `a=T` overlays (WezTerm) |
| `direct_fallback_max_bytes` | int | If SHM fails, allow direct fallback only ≤ this size (unless `prefer_direct`) |
| `notes` | string | Docs / DEBUG only |

### Matchers (`[[terminal]]` only)

| Key | Type | Meaning |
|---|---|---|
| `id` | string | Profile name (`term=` in DEBUG) |
| `match.term_program` | string[] | Case-insensitive match on `TERM_PROGRAM` |
| `match.term_when_program_empty` | string[] | Exact `TERM` when `TERM_PROGRAM` empty (Kitty.app) |
| `match.term_substring` | string[] | `TERM` contains substring (driver auto-detect) |
| `match.kitty_window_id` | bool | Treat set `KITTY_WINDOW_ID` as a hit |
| `match.term_program_version_min` | string | Inclusive semver floor on `TERM_PROGRAM_VERSION` |
| `match.term_program_version_max` | string | Inclusive semver ceiling on `TERM_PROGRAM_VERSION` |

Identity matchers are OR’d; version min/max are AND’d with identity. A row with version bounds does **not** match when `TERM_PROGRAM_VERSION` is unset or unparsable. Put more specific version rows **above** a catch-all for the same host.

Version parse: leading `major[.minor[.patch]]` (skips a non-digit prefix); `-pre` sorts below the same numeric release (`1.3.2-main` < `1.3.2`, and `1.3.2-main` > `1.3.1`). Ghostty tip reports e.g. `1.3.2-main+…`.

`unknown` has no matchers (explicit fallback). Ids like `ghostty-1.3` map to the same `KITTY_TERM_*` kind as the unversioned host name.

### Tunable constants (usually only in `[defaults]`)

| Key | Default | Meaning |
|---|---|---|
| `apc_chunk_bytes` | 2097152 | Direct APC payload chunk size |
| `probe_timeout_ms` | 40 | Graphics query await |
| `drain_passes` | 8 | Exit tty drain loops |
| `drain_quiet_us` | 15000 | Quiet gap in drain |
| `stub_winch_iters` | 40 | Wait for non-stub grid at init |
| `stub_winch_delay_ms` | 25 | Delay between stub polls |
| `stub_cols` / `stub_rows` | 80 / 24 | Stub grid size |
| `key_hold_ms` | 80 | Legacy key min-hold |
| `fb_diff` | true | Tile-diff default |
| `fb_diff_tile` | 32 | Tile size |
| `fb_diff_cover` | 0.45 | Diff → full threshold |
| `tty_pending_max` | 4096 | Input reassembly buffer |
| `window_fallback` | `"640x480"` | Window size if unset |
| `place_cells` | true | Stretch FB to cell grid |
| `pingpong` | false | Delete+swap vs same-(i,p) replace |
| `shm_unlink_after_send` | false | Client unlink after SHM send (must stay false; terminal unlinks) |

## Env overrides

| Env | Effect |
|---|---|
| `SDL_KITTY_TRANSMIT` | Force medium |
| `SDL_KITTY_FB_MAX` / `FB_FIXED` | FB size |
| `SDL_KITTY_DIRTY_BW` | Sparse a=f on/off |
| `SDL_KITTY_DIRTY_AT` | Stacked a=T dirty overlays on/off (WezTerm experiment) |
| `SDL_KITTY_FB_DIFF*` / `PINGPONG` / `PLACE_CELLS` / `FB_ALPHA` | Present path |
| `SDL_KITTY_PROFILES` | Path or `:`-separated list of TOML file(s) |
| `SDL_KITTY_DEBUG=1` | Log `term=` / `profiles=` source |

## Phases

1. **Done** — schema + `config/terminals.toml`  
2. **Done** — tomlc17 loader + search paths + builtin fallback + `kitty_profile_utils_test`  
3. **Done** — `scripts/terminals_toml_to_md.py`; wrappers set `SDL_KITTY_PROFILES`  
4. **Done** — §B/§C wired into driver call sites  
5. **Documented** — user pref-path overlay (see below)

## User override (pref path)

Without setting `SDL_KITTY_PROFILES`, the driver also looks for:

`$SDL_GetPrefPath("sdl-kitty", "kitty")/terminals.toml`

On macOS that is typically:

`~/Library/Application Support/sdl-kitty/kitty/terminals.toml`

Example — keep Rex defaults but try SHM first once the host enables the medium:

```toml
[[terminal]]
id = "rex"
prefer_direct = false
```

Sparse rows overlay the shipped `[defaults]` + matching host. Confirm with `SDL_KITTY_DEBUG=1` and the init line `profiles=… term=rex prefer_direct=0`.

In-tree demos: `scripts/run-with-kitty-sdl.sh` sets `SDL_KITTY_PROFILES` to the repo `config/terminals.toml` so edits apply immediately.
