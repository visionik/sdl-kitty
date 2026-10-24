# sdl-kitty

SDL3 video driver that presents through the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/).

Real SDL apps can run inside Kitty, Ghostty, WezTerm, or Rex with:

```bash
export SDL_VIDEO_DRIVER=kitty
```

## Docs

- [Framebuffer placement, mouse, and Rex lessons](docs/framebuffer-placement-and-mouse.md) — CSI sizing, FB cap, stretch vs 1:1, mouse mapping, host quirks
- [Terminal features & fixes](docs/terminals.md) — per-host profile matrix (Rex / Kitty / Ghostty / WezTerm)
- [Terminal profile TOML](docs/terminal-profiles.md) — `config/terminals.toml` (loadable; regenerate matrix with `scripts/terminals_toml_to_md.py`)
- [Capability optimizations](docs/optimizations.md) — dirty rects and later protocol upgrades
- [Design notes](docs/design.md)

Working `mouse_paint` snapshots: `examples/mouse_paint_fullpane/`, `examples/mouse_paint_1to1/`.

## Status

Phase 2: window surface present, keyboard, mouse, zlib/SHM transmit, resize. Not upstreamed yet.

| Feature | Status |
|---|---|
| `SDL_VIDEO_DRIVER=kitty` | yes (forced; never auto-selected) |
| Window surface / software present | yes (RGB24 over APC) |
| Transmit: shared memory (`t=s`) | yes (default; falls back) |
| Transmit: zlib (`o=z`) | yes (fallback after SHM) |
| Transmit: direct base64 (`t=d`) | yes (final fallback) |
| Keyboard (raw + Kitty CSI u) | minimal (ASCII + mapped keys; q/Esc quit) |
| Mouse (SGR 1006) | yes (motion, buttons, wheel) |
| Resize (`SIGWINCH`) | yes |
| Double-buffer image ids | yes |
| OpenGL | no |

Override transmit mode with `SDL_KITTY_TRANSMIT=shm|zlib|direct`.

## Build

Requires CMake ≥ 3.16, a C toolchain, zlib, and a Kitty-capable terminal to run demos.

```bash
cmake -S . -B build
cmake --build build -j
```

This builds SDL3 (submodule at `third_party/SDL`, branch `sdl-kitty-v0`) with `SDL_KITTY=ON`, plus examples.

## Run examples

Must be run in a real terminal (not a dumb pipe):

```bash
./scripts/run-with-kitty-sdl.sh ./build/solid_present
./scripts/run-with-kitty-sdl.sh ./build/checker_keyboard
./scripts/run-with-kitty-sdl.sh ./build/mouse_paint
```

Or directly if the examples are linked to the built lib:

```bash
SDL_VIDEO_DRIVER=kitty ./build/solid_present
```

`mouse_paint`: left-drag paints, right-click clears, wheel cycles colors, q/Esc quits.

`dvd_bounce` draws the logo each frame; `dvd_sprite` blits a logo surface over `examples/assets/dvd_player_iso.png` (isometric DVD player backdrop; color-mod on bounce; dirty BW when the `a=f` probe succeeds).

`atlas_sprites` uploads a DVD+Blu-ray sprite sheet once (`a=t`) and bounces 10 cropped placements (5+5); pairwise collide pulses only the hitters for 10s.

`alpha_blit` sets `SDL_KITTY_FB_ALPHA=1` (ARGB window FB + Kitty `f=32`) and draws translucent discs over a checker grid.

`fireworks` launches colored rockets over `examples/assets/lake_night.png` (dirty-rect presents; click/space for extra bursts).

## DynAPI override

Point an existing SDL3 binary at this build:

```bash
export SDL3_DYNAMIC_API=/absolute/path/to/build/third_party/SDL/libSDL3.dylib  # or .so
export SDL_VIDEO_DRIVER=kitty
./some-sdl3-app
```

## Layout

- `third_party/SDL` — SDL3 + `src/video/kitty/` driver (eventual upstream PR surface)
- `examples/` — smoke demos
- `tests/` — unit tests for kitty helpers (`ctest -R kitty_`)
- `scripts/run-with-kitty-sdl.sh` — env helper
- `docs/` — design notes (see plan)

## License

Driver sources intended for SDL use the SDL zlib license header. Project scaffolding is zlib/MIT-compatible.
