# Terminal profiles: facts and tuning

Two TOML files decide how the driver treats a terminal:

| File | Holds | Owned by |
|---|---|---|
| [`config/terminals.toml`](../config/terminals.toml) | **Facts**: how to recognise each terminal, and what it really does, with evidence. | [terminal-facts](https://github.com/visionik/terminal-facts) (shared with pixcell). Vendored unchanged; `config/terminals.source` records the commit. Format: its [SPEC.md](https://github.com/visionik/terminal-facts/blob/main/SPEC.md). |
| [`config/sdl-kitty.toml`](../config/sdl-kitty.toml) | **Tuning**: what sdl-kitty chooses given the facts (FB caps, coalescing, drain timing...). | This repo. |

Both are compiled into the driver: `scripts/update_facts.py` writes
`third_party/SDL/src/video/kitty/SDL_kittyprofile_data.h`, and ctest
(`kitty_profile_data_fresh`) fails if it's stale. No per-terminal values live in C;
`SDL_kittyprofile.c` holds only the seed defaults. The loader is
`SDL_kittyprofile_load.c` (vendored [tomlc17](https://github.com/cktan/tomlc17), MIT).

Probes (`a=q`, SHM, `a=f`) still decide what's available. `SDL_KITTY_*` environment
variables win over everything.

## Recognising the terminal

At start-up, from the environment (`match.env`, `TERM_PROGRAM`, `TERM`). Then, straight
after entering raw mode, the driver asks **XTVERSION** (`CSI > 0 q`, with DA1 behind it so
the wait ends at once) and recognises the terminal again from the answer. The answer
can't be inherited: WezTerm started from a Rex pane carries `TERM_PROGRAM=rex`, but
answers `WezTerm 20240203-…`. It also carries the version, for version rows.
`SDL_KITTY_XTVERSION=0` skips the question.

Extending rows then stack, in file order: Ghostty 1.3.1 is `ghostty`, then `ghostty-1.3`;
WezTerm on macOS is `wezterm`, then `wezterm-macos`. A version row never applies when
the version is unknown. `SDL_KITTY_DEBUG=1` logs
`terminal=ghostty rows=ghostty,ghostty-1.3 version=1.3.1 by=xtversion`.

## Resolving

1. The seed (`SDL_kittyprofile.c`).
2. `[defaults]` from each tuning file, in load order.
3. `[terminal.<id>]` for each row applied (base first), from each tuning file; or
   `[terminal.unknown]` when no row matches.
4. Facts that decide a setting, over the tuning:

   | Fact | Setting |
   |---|---|
   | `kitty.frame_edit_reply = false` | `silent_af_ok = true` (and `true` → `false`) |
   | `window.ioctl_pixels = false` | `ignore_ioctl_pixels = true` (and `true` → `false`) |
   | `kitty.max_escape_bytes` | `apc_chunk_bytes` = it minus 256 (header and ST) |
   | `kitty.shm = false` | `prefer_direct = true` |
   | `kitty.frame_edit = false` | skip the `a=f` probe; `direct_af_when_shm = false` |
   | `kitty.frame_edit_zlib` | `af_zlib` |
   | `keyboard.kitty_protocol_config` | the warning when `CSI ? u` goes unanswered |

5. `SDL_KITTY_*` environment variables, at their call sites.

An unknown fact leaves the tuning value. A new terminal needs only a row in
terminal-facts; it gets `[defaults]` plus whatever its facts decide.

## Files at run time

| What | Where (first match) |
|---|---|
| Facts | `SDL_KITTY_FACTS`; else `terminals.toml` in the pref path (`SDL_GetPrefPath("sdl-kitty", "kitty")`), then `SDL_GetBasePath()`/`kitty/`, then the base path; else `KITTY_DEFAULT_FACTS_PATH`. A file **replaces** the compiled-in facts, and one that doesn't follow schema 1 is refused (the compiled-in facts stay). |
| Tuning | `SDL_KITTY_PROFILES` (`:`-separated list); else **every** `sdl-kitty.toml` found in the same places, then `KITTY_DEFAULT_PROFILES_PATH`. Each **overlays** the compiled-in tuning. |

In-tree builds set `KITTY_DEFAULT_FACTS_PATH` and `KITTY_DEFAULT_PROFILES_PATH` to the
repo's `config/` files, so edits show without regenerating the header. The `scripts/run-*.sh`
wrappers set `SDL_KITTY_PROFILES` to `config/sdl-kitty.toml`.

On macOS the pref path is `~/Library/Application Support/sdl-kitty/kitty/`. Example: keep
Rex's tuning, but try zlib off:

```toml
# ~/Library/Application Support/sdl-kitty/kitty/sdl-kitty.toml
[terminal.rex]
prefer_zlib = false
```

## Changing things

- **A finding about a terminal** ("WezTerm's `a=f` never redraws") is a fact: add it to
  terminal-facts with its evidence, then `python3 scripts/update_facts.py` (reads
  `../terminal-facts` by default) and commit `config/terminals.*` and the header.
- **A choice of sdl-kitty's** is tuning: edit `config/sdl-kitty.toml`, then
  `python3 scripts/update_facts.py --header`.
- `python3 scripts/terminals_toml_to_md.py` regenerates the matrix in
  [terminals.md](terminals.md).

## `sdl-kitty.toml` keys

Under `[defaults]` and `[terminal.<id>]` (`<id>` from terminals.toml, or `unknown`;
quote ids with dots: `[terminal."ghostty-1.3"]`). `update_facts.py` refuses ids that
aren't in terminals.toml.

### Policy

| Key | Type | Meaning |
|---|---|---|
| `prefer_direct` | bool | Prefer direct over SHM when both probe OK; skip the SHM probe. Forced on by `kitty.shm = false`. |
| `prefer_zlib` | bool | When not on SHM, prefer `o=z` zlib direct over raw base64 |
| `force_transmit` | string | Force `direct`/`zlib`/`shm` after env (Exp 2 Ghostty A/B) |
| `upload_skip` | bool | Skip wire when scratch FNV matches last upload (Exp 6) |
| `coalesce_gap_px` | int | Merge dirty rects within this pixel gap before multi a=f (0=off) |
| `default_fb_max` | `"WxH"` | Fallback aspect-fit FB cap when `SDL_KITTY_FB_MAX` unset; empty/`0` = no default |
| `default_fb_max_shm` | `"WxH"` | Soft-max when transmit is SHM; empty/`0` → `default_fb_max` |
| `default_fb_max_direct` | `"WxH"` | Soft-max for direct/zlib (and SHM-failed); empty/`0` → `default_fb_max` |
| `max_direct_bytes` | int | Refuse direct a=T / a=f above this raw size |
| `af_max_coverage` | float | Skip union a=f when dirty coverage ≥ this (env: `SDL_KITTY_AF_MAX_COVERAGE`) |
| `af_max_pixels` | int | Skip a=f when dirty px ≥ this (also per-rect multi-a=f cap) |
| `af_zlib` | bool | Compress `a=f` edits (`o=z`). Used when `kitty.frame_edit_zlib` is unknown (env: `SDL_KITTY_AF_ZLIB=0`/`1`). The window property `SDL.window.kitty.af_zlib` says what was chosen. |
| `keys_on_stdin` | bool | Read keyboard from stdin when it is a tty |
| `silent_af_ok` | bool | a=f probe: silence counts as success. Used when `kitty.frame_edit_reply` is unknown. |
| `ignore_ioctl_pixels` | bool | Ignore `TIOCGWINSZ` xpixel/ypixel. Used when `window.ioctl_pixels` is unknown. |
| `direct_af_when_shm` | bool | Allow direct a=f while session transmit mode is SHM |
| `dirty_at_overlays` | bool | Experiment: sparse dirty as stacked `a=T` overlays (WezTerm; default off) |
| `dirty_union_at` | bool | Exp 3: single union `a=T` overlay + DECSC/DECRC + edge skip |
| `direct_fallback_max_bytes` | int | If SHM fails, allow direct fallback only ≤ this size (unless `prefer_direct`) |
| `notes` | string | Docs / DEBUG only |

### Constants (usually only in `[defaults]`)

| Key | Default | Meaning |
|---|---|---|
| `apc_chunk_bytes` | 2097152 | Direct APC payload chunk size. Used when `kitty.max_escape_bytes` is unknown. |
| `probe_timeout_ms` | 40 | Probe wait budget (25 ms polls) |
| `drain_passes` | 8 | Exit tty drain loops |
| `drain_quiet_us` | 15000 | Quiet gap in drain |
| `stub_winch_iters` | 40 | Wait for non-stub grid at init |
| `stub_winch_delay_ms` | 25 | Delay between stub polls |
| `stub_cols` / `stub_rows` | 80 / 24 | Stub grid size |
| `key_hold_ms` | 80 | Legacy key min-hold (arrows use ≥400 ms when Kitty keyboard probe fails) |
| `fb_diff` | true | Tile-diff default |
| `fb_diff_tile` | 32 | Tile size |
| `fb_diff_cover` | 0.45 | Diff → full threshold |
| `tty_pending_max` | 4096 | Input reassembly buffer |
| `window_fallback` | `"640x480"` | Window size if unset |
| `place_cells` | true | Stretch FB to cell grid |
| `pingpong` | false | Delete+swap vs same-(i,p) replace |
| `shm_unlink_after_send` | false | Client unlink after SHM send (must stay false; terminal unlinks) |

## Keyboard

WezTerm ignores `CSI > 11 u` unless `enable_kitty_keyboard = true` in `wezterm.lua`. After
the push the driver queries `CSI ? u`; with no reply, a terminal whose facts carry
`keyboard.kitty_protocol_config` gets one `SDL_LogWarn` naming that setting, and every
terminal gets the press + OS-repeat fallback. See [keyboard.md](keyboard.md).

## Env overrides

| Env | Effect |
|---|---|
| `SDL_KITTY_TRANSMIT` | Force medium |
| `SDL_KITTY_FB_MAX` / `FB_FIXED` | FB size |
| `SDL_KITTY_DIRTY_BW` | Sparse a=f on/off (skips the a=f probe) |
| `SDL_KITTY_DIRTY_AT` | Stacked a=T dirty overlays on/off (WezTerm experiment) |
| `SDL_KITTY_FB_DIFF*` / `PINGPONG` / `PLACE_CELLS` / `FB_ALPHA` | Present path |
| `SDL_KITTY_PROFILES` | Tuning file(s), `:`-separated |
| `SDL_KITTY_FACTS` | A `terminals.toml` instead of the compiled-in facts |
| `SDL_KITTY_XTVERSION=0` | Recognise the terminal from the environment only |
| `SDL_KITTY_DEBUG=1` | Log `terminal=` / presents (needs `-DSDL_KITTY_RUNTIME_DEBUG=ON` at cmake) |
| `SDL_KITTY_PERF_ACK=1` | Wait for graphics OK after `a=T` (needs `-DSDL_KITTY_PERF_ACK=ON`; leave off for interactive) |
| `SDL_KITTY_KEY_DEBUG` / `MOUSE_DEBUG` | Key / mouse dumps (also need `-DSDL_KITTY_RUNTIME_DEBUG=ON`) |
