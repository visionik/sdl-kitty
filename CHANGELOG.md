# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Compile-time gates for runtime debug and PERF_ACK waits (default **OFF** for release perf):
  - `-DSDL_KITTY_RUNTIME_DEBUG=ON` + `SDL_KITTY_DEBUG=1` for present/probe/init/key/mouse logs
  - `-DSDL_KITTY_PERF_ACK=ON` + `SDL_KITTY_PERF_ACK=1` for optional `Gi=;OK` wait after full `a=T`
- Quiet Kitty APC on the hot path: `q=2` on `a=T` place keys, `a=f`, `a=p`, and deletes (no OK replies to drain)
- Skip `a=f` capability probe when `SDL_KITTY_DIRTY_BW` is forced on or off
- Kitty keyboard probe: after `CSI > 11 u`, query `CSI ? u` → `kitty_keyboard_ok`
  - WezTerm with no reply: one `SDL_LogWarn` + legacy arrow auto-up (~400 ms)
  - With reply: wait for real `:3` press/release
- `LICENSE.txt` (zlib, same terms as SDL 3) and this changelog

### Changed

- WezTerm `default_fb_max` **960×540 → 1280×720**
- Docs: IFDEF debug/PERF_ACK; WezTerm `enable_kitty_keyboard`; keyboard probe/fallback; capability matrix WezTerm columns filled from soaks

### Fixed

- WezTerm MAME arrows “dying”: root cause was **`enable_kitty_keyboard=false`** (Xterm encoding, no PTY key-ups), not PTY/graphics load. Set `enable_kitty_keyboard = true` in `wezterm.lua` for smooth holds.

## [0.1.0] — 2026-10-24

Initial public snapshot of the sdl-kitty worktree.

### Features

- SDL3 Kitty graphics protocol video driver (`SDL_VIDEO_DRIVER=kitty`)
- Window surface / software present (RGB24 or optional RGBA `f=32`)
- Transmit: SHM (`t=s`), zlib (`o=z`), direct base64 (`t=d`) with host probes
- Dirty presents: CPU convert, probe-gated `a=f` frame edit, tile-diff for full soft redraws
- Per-terminal profiles (`config/terminals.toml`): Rex, Kitty.app, Ghostty, WezTerm
- Keyboard: CSI-u + legacy CSI/SS3; mouse: SGR 1006 / 1016
- Experiments: `prefer_zlib`, `upload_skip`, `coalesce_gap_px` (Exp 3 overlays killed on WezTerm)
- Examples: `solid_present`, `mouse_paint`, `dvd_sprite`, `fireworks`, `alpha_blit`, `key_probe`, and more
- Unit tests: `ctest -R kitty_(probe|rect|profile)_utils_test`

### Known host limits

- **WezTerm:** no usable sparse `a=f` until ImageData.hash invalidate; macOS SHM pending [#8063](https://github.com/wezterm/wezterm/pull/8063); requires `enable_kitty_keyboard = true` for press/release (driver warns + OS-repeat fallback otherwise)
- **Rex:** SHM medium often unsupported (`prefer_direct` / `prefer_zlib`)
- **Ghostty 1.3.1:** `a=f` unimplemented; tip+ works with sparse dirty_bw
