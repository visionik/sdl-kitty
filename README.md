# sdl-kitty

SDL3 video driver that presents through the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/).

Real SDL apps can run inside Kitty, Ghostty, WezTerm, or Rex with:

```bash
export SDL_VIDEO_DRIVER=kitty
```

## Status

Phase 0/1 spike: window surface present + basic keyboard. Not upstreamed yet.

| Feature | Status |
|---|---|
| `SDL_VIDEO_DRIVER=kitty` | yes (forced; never auto-selected) |
| Window surface / software present | yes (RGB24 over APC, chunked base64) |
| Keyboard (raw tty) | minimal (ASCII + q/Esc quit) |
| Mouse | no |
| Shared memory (`t=s`) | no |
| OpenGL | no |

## Build

Requires CMake ≥ 3.16, a C toolchain, and a Kitty-capable terminal to run demos.

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
```

Or directly if the examples are linked to the built lib:

```bash
SDL_VIDEO_DRIVER=kitty ./build/solid_present
```

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
- `scripts/run-with-kitty-sdl.sh` — env helper
- `docs/` — design notes (see plan)

## License

Driver sources intended for SDL use the SDL zlib license header. Project scaffolding is zlib/MIT-compatible.
