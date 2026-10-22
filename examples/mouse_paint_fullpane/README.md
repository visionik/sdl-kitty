# mouse_paint_fullpane (saved working snapshot)

Frozen copy of the `mouse_paint` demo that worked on Rex with:

- full-pane Kitty `c`/`r` stretch (`SDL_KITTY_PLACE_CELLS=1`)
- framebuffer cap (Rex default 1280×720 via `KITTY_ApplyFramebufferCap`)
- SGR pixel mouse (`SDL_KITTY_MOUSE_PIXELS=1` → DECSET 1016)
- direct RGB transmit

**Verified:** dark filled pane, yellow crosshair locked under the pointer, left-drag paints.

Saved: 2026-10-22. Source matches `examples/mouse_paint.c` at that time; binary is the matching build.

## Run

Prefer an already-sized Rex/Ghostty pane (not a fresh stub 80×24):

```sh
./examples/mouse_paint_fullpane/run.sh
```

Or from this directory:

```sh
./run.sh
```

`q` / Esc quits. Wheel cycles paint colors; right-click clears.

## Notes

- Binary rpath: `@executable_path/../../build/third_party/SDL` (needs a built `libSDL3` there).
- To rebuild this snapshot after driver changes:  
  `cmake --build build --target mouse_paint && cp build/mouse_paint examples/mouse_paint_fullpane/mouse_paint`
- Live editable source remains `examples/mouse_paint.c` (CMake target `mouse_paint`).
