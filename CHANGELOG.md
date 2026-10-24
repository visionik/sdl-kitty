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
- `LICENSE.txt` (zlib, same terms as SDL 3) and this changelog

### Changed

- Docs: document IFDEF debug/PERF_ACK build options in `docs/optimizations.md` and `docs/terminal-profiles.md`
- TODO: WezTerm MAME arrow keys still die after ~1–2s (PTY flood); tracked as open item

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

- **WezTerm:** no usable sparse `a=f` until ImageData.hash invalidate; macOS SHM pending [#8063](https://github.com/wezterm/wezterm/pull/8063); Galaga arrows can stall under PTY flood
- **Rex:** SHM medium often unsupported (`prefer_direct` / `prefer_zlib`)
- **Ghostty 1.3.1:** `a=f` unimplemented; tip+ works with sparse dirty_bw
