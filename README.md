# sdl-kitty

SDL3 video driver that presents through the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/).

Run real SDL3 apps inside **Kitty**, **Ghostty**, **WezTerm**, or **Rex**:

```bash
export SDL_VIDEO_DRIVER=kitty
```

This repo is a meta-project: examples, docs, terminal profiles, and an SDL3 submodule (`third_party/SDL` on branch `sdl-kitty-v0`) that contains the driver under `src/video/kitty/`.

## Status

Software-framebuffer path only (`GetWindowSurface` / `UpdateWindowSurface`). No OpenGL, Vulkan, Metal, or GPU backends yet.

| Feature | Status |
|---|---|
| `SDL_VIDEO_DRIVER=kitty` | yes (forced; never auto-selected) |
| Window surface / software present | yes |
| Transmit: SHM (`t=s`) / zlib (`o=z`) / direct (`t=d`) | yes (probed + profile defaults) |
| Sparse dirty BW (`a=f`) | yes when host probe OK |
| Tile-diff for full soft presents (e.g. MAME) | yes (`SDL_KITTY_FB_DIFF`, default on) |
| Keyboard (CSI-u + legacy) | yes (games + text via flag 16 / `SDL_StartTextInput`) |
| Mouse (SGR 1006 / 1016) | yes |
| Resize (`SIGWINCH`) | yes |
| Per-terminal TOML profiles | yes (`config/terminals.toml`) |
| OpenGL / GPU | no |

See [CHANGELOG.md](CHANGELOG.md) for recent changes.

## Docs

- [Framebuffer placement, mouse, and Rex lessons](docs/framebuffer-placement-and-mouse.md)
- [Terminal features & fixes](docs/terminals.md) — per-host matrix
- [Terminal profile TOML](docs/terminal-profiles.md) — `config/terminals.toml`
- [Capability optimizations](docs/optimizations.md) — dirty rects, probes, transmit
- [Keyboard](docs/keyboard.md)
- [Design notes](docs/design.md)
- [TODO](TODO.md) — living backlog

## Build

Requires CMake ≥ 3.16, a C toolchain, zlib, and a Kitty-capable terminal to run demos.

```bash
git clone --recurse-submodules https://github.com/visionik/sdl-kitty.git
cd sdl-kitty
cmake -S . -B build
cmake --build build -j
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

### CMake options

| Option | Default | Meaning |
|---|---|---|
| `SDL_KITTY_BUILD_EXAMPLES` | `ON` | Build in-tree demos |
| `SDL_KITTY_RUNTIME_DEBUG` | `OFF` | Compile present/probe/key/mouse debug logs (`SDL_KITTY_DEBUG=1` at run) |
| `SDL_KITTY_PERF_ACK` | `OFF` | Compile optional wait for `Gi=;OK` after full `a=T` (`SDL_KITTY_PERF_ACK=1`) |

Release builds leave both debug options **OFF** so env debug flags are no-ops and presents stay quiet (`q=2` on the APC hot path).

```bash
cmake -S . -B build -DSDL_KITTY_RUNTIME_DEBUG=ON -DSDL_KITTY_PERF_ACK=ON
```

### Tests

```bash
ctest --test-dir build -R 'kitty_(probe|rect|profile)_utils_test' --output-on-failure
```

## Run examples

Must run in a real terminal (stdout is a TTY):

```bash
./scripts/run-with-kitty-sdl.sh ./build/solid_present
./scripts/run-with-kitty-sdl.sh ./build/mouse_paint
./scripts/run-with-kitty-sdl.sh ./build/fireworks
./scripts/run-with-kitty-sdl.sh ./build/key_probe

# Interactive soak menu (any Kitty-capable host); quit each demo to return:
./scripts/kitty_soak.sh
./scripts/kitty_soak.sh --once dirty
./scripts/kitty_soak.sh --list
```

Or, when linked to the built lib:

```bash
SDL_VIDEO_DRIVER=kitty ./build/solid_present
```

| Demo | Notes |
|---|---|
| `solid_present` | Full-frame color smoke |
| `static_present` | Identical frames (upload_skip / tile-diff) |
| `dirty_rects_demo` | Sparse dirty rects + periodic full tint |
| `mouse_paint` | Left-drag paints; q/Esc quits |
| `dvd_sprite` / `dvd_bounce` | Motion + dirty presents |
| `fireworks` | Rockets over `examples/assets/lake_night.png` |
| `alpha_blit` | RGBA `f=32` translucent discs |
| `atlas_sprites` | Multi-placement APC demo (not the FB driver path) |
| `key_probe` | Physical keyboard CSI-u check |

Working paint snapshots: `examples/mouse_paint_fullpane/`, `examples/mouse_paint_1to1/`.

## DynAPI override

Point an existing SDL3 binary at this build:

```bash
export SDL3_DYNAMIC_API=/absolute/path/to/build/third_party/SDL/libSDL3.dylib  # or .so
export SDL_VIDEO_DRIVER=kitty
./some-sdl3-app
```

On macOS, `DYLD_LIBRARY_PATH=…/build/third_party/SDL` also works for apps that load `libSDL3` by name (e.g. a local MAME soft build).

## Useful env

| Env | Effect |
|---|---|
| `SDL_VIDEO_DRIVER=kitty` | Select this driver |
| `SDL_KITTY_TRANSMIT=shm\|zlib\|direct` | Force transmit medium |
| `SDL_KITTY_FB_MAX` / `FB_FIXED` | Cap or fix framebuffer size |
| `SDL_KITTY_DIRTY_BW=0\|1` | Force sparse `a=f` off/on |
| `SDL_KITTY_PROFILES` | Path to `terminals.toml` |
| `SDL_KITTY_DEBUG=1` | Logs (needs `-DSDL_KITTY_RUNTIME_DEBUG=ON`) |

Profiles and more overrides: [docs/terminal-profiles.md](docs/terminal-profiles.md).

## Layout

- `third_party/SDL` — SDL3 fork with `src/video/kitty/` (branch `sdl-kitty-v0`)
- `config/terminals.toml` — per-host defaults
- `examples/` — smoke demos
- `tests/` — unit tests for kitty helpers
- `scripts/` — runners and probes
- `docs/` — design and host notes

## Host notes (short)

- **Kitty.app** — correctness oracle; prefer SHM + sparse `a=f`
- **Ghostty tip+** — SHM + `a=f`; stable 1.3.1 needs full `a=T` (`a=f` unimplemented)
- **Rex** — prefer direct/zlib; SHM often unsupported by the host
- **WezTerm** — prefer zlib; FB cap 1280×720; sparse `a=f` not display-usable until ImageData.hash invalidate; set `enable_kitty_keyboard = true` in `wezterm.lua` (driver probes `CSI ? u` and warns otherwise)

## License

[zlib License](LICENSE.txt) — same terms as [SDL 3](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt).

SDL 3 itself remains Copyright (C) 1997-2026 Sam Lantinga; see `third_party/SDL/LICENSE.txt`.
