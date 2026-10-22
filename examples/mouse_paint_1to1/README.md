# mouse_paint_1to1 (saved working snapshot)

Frozen copy of the `mouse_paint` demo that worked on Rex with:

- native 1:1 Kitty placement (`SDL_KITTY_PLACE_CELLS=0`, no `c`/`r` stretch)
- framebuffer cap (Rex default 1280×720) so huge panes do not blank
- SGR pixel mouse (`SDL_KITTY_MOUSE_PIXELS=1` → DECSET 1016) mapped **raw** into the FB (no pane→FB squash)
- direct RGB transmit

**Verified:** dark rectangle at capped native size (may not fill the pane), yellow crosshair locked under the pointer on that rectangle.

Saved: 2026-10-22. Source matches `examples/mouse_paint.c` at that time; binary includes the 1:1 mouse-mapping fix in the kitty driver.

Companion snapshot: `examples/mouse_paint_fullpane/` (stretch + scale mouse into FB).

## Run

Prefer an already-sized Rex/Ghostty pane:

```sh
./examples/mouse_paint_1to1/run.sh
```

`q` / Esc quits. Wheel cycles paint colors; right-click clears.

## Notes

- Binary rpath: `@executable_path/../../build/third_party/SDL` (needs a built `libSDL3` there).
- To rebuild this snapshot after driver changes:  
  `cmake --build build --target mouse_paint && cp build/mouse_paint examples/mouse_paint_1to1/mouse_paint`
- Live editable source remains `examples/mouse_paint.c` (CMake target `mouse_paint`).
