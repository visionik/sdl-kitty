# Framebuffer placement, mouse, and Rex lessons

How the sdl-kitty driver sizes the software framebuffer, places Kitty images, and maps SGR mouse into SDL coordinates — plus what broke on Rex and what to do about it.

Verified on Rex (libghostty client) with `mouse_paint`, 2026-10-22. Snapshots:

- `examples/mouse_paint_fullpane/` — stretch + capped FB + scaled 1016
- `examples/mouse_paint_1to1/` — native 1:1 + capped FB + raw 1016

---

## Mental model

Three spaces matter:

| Space | What it is | Typical source |
|---|---|---|
| **Cell grid** | `cols × rows` | `TIOCGWINSZ` / CSI |
| **Pane pixels (CSI 14t)** | Drawable text-area size in pixels | `CSI 14 t` reply |
| **SDL framebuffer** | `pixel_w × pixel_h` the app draws into | Driver (may be capped) |

Kitty placement then decides how the FB lands in the pane:

| Mode | Env | Kitty placement | Visual |
|---|---|---|---|
| **Stretch (default)** | `SDL_KITTY_PLACE_CELLS=1` | `c`/`r` = full cell grid | Image fills the pane |
| **1:1** | `SDL_KITTY_PLACE_CELLS=0` | omit `c`/`r`; native `s×v` | Image is FB-sized (often top-left; may not fill pane) |

SGR mouse (prefer **1016** pixel reports) lives in pane/CSI space. Mapping into the FB depends on placement mode (below).

```
┌──────────────── pane (CSI 14 t) ────────────────┐
│  stretch: FB stretched to fill                  │
│  1:1+cap: ┌──────────┐                          │
│           │ dark FB  │  ← only this is drawable │
│           └──────────┘                          │
└─────────────────────────────────────────────────┘
```

---

## How the driver works

### Size query

1. Prefer **CSI 14t** for FB / drawable pixels.
2. Prefer **CSI 16t** (or cols×cell) for cell metrics.
3. On Rex (`TERM_PROGRAM=rex`), **ignore** `TIOCGWINSZ` xpixel/ypixel — they can disagree with CSI and produce oversized FBs that go blank.
4. Fresh `rex run` panes often start at stub **80×24** then WINCH to the real size; the driver waits / re-queries so you are not stuck at 640×384 forever.

### Framebuffer cap

Hosts (observed: Rex) can **drop** huge multi-chunk direct RGB while still acknowledging the transmit. Symptom: present logs `ok`, pane stays blank / only stderr HUD text.

`KITTY_ApplyFramebufferCap` (`SDL_kittygraphics.c`):

- Env `SDL_KITTY_FB_MAX=W`, `WxH`, or `off` / `none` / `0`
- If unset and `TERM_PROGRAM=rex` → default **1280×720** (aspect preserved)
- Applies for **both** stretch and 1:1

With stretch, Kitty `c`/`r` still fills the pane from the smaller texture. With 1:1, you see a smaller native rectangle.

### Mouse modes

- Probe DECRQM for **1016**; enable if recognized, else **1006** cells.
- Also enable 1000 / 1002 / 1003 as needed for buttons and motion.
- Override: `SDL_KITTY_MOUSE_PIXELS=0` forces cell SGR (1006).
- `SDL_KITTY_MOUSE=0` skips mouse enable entirely (useful for transmit-only probes).

### Mouse → SDL mapping (`SDL_kittyevents.c`)

1016 reports are 1-based pixels in the CSI 14t text area. After converting to 0-based `mouse_raw_*`:

| Placement | When FB size ≠ pane size | Mapping |
|---|---|---|
| **Stretch** (`place_cells`) | Cap made FB smaller than pane | **Scale** pane → FB: `px = raw * pixel_w / mouse_coord_w` |
| **1:1** | Cap made FB smaller than pane | **Raw** 1016 coords (image at native size, typically origin-aligned) |

Stretch *must* scale: the image fills the pane, so pointer position in the pane should map proportionally into the (possibly capped) texture.

1:1 *must not* scale the whole pane into the FB: that made the crosshair track **100% of the pane** instead of the dark rectangle. Use raw coords; motion outside the rectangle leaves the drawable.

`mouse_coord_w/h` tracks the full CSI/cell product for the stretch scale path.

### Transmit dialect (Rex-safe)

Prefer a shape Rex actually paints:

- Direct RGB (`t=d`), large enough single chunk when possible
- Include `c`/`r` only when stretching
- **Omit** explicit `m=0` on one-shot `a=T` (Rex has blanked when `m=0` is present)
- **Omit** quiet `q=2` when diagnosing; drain APC replies so they are not typed as keys
- Avoid `o=z` on Rex until proven; zlib path has gone blank after capability OK

Override: `SDL_KITTY_TRANSMIT=direct|zlib|shm`.

---

## Recommended configs

### Full-pane interactive (default demo path)

```bash
export SDL_VIDEO_DRIVER=kitty
export SDL_KITTY_TRANSMIT=direct
export SDL_KITTY_PLACE_CELLS=1          # stretch
export SDL_KITTY_MOUSE_PIXELS=1         # 1016 when recognized
# optional: SDL_KITTY_FB_MAX=1280x720   # Rex already defaults this
./build/mouse_paint
# or: ./examples/mouse_paint_fullpane/run.sh
```

**Expect:** filled dark pane, pointer-aligned crosshair, paint on drag.

### Native 1:1 (capped)

```bash
export SDL_KITTY_PLACE_CELLS=0
# same transmit / mouse as above
./examples/mouse_paint_1to1/run.sh
```

**Expect:** capped rectangle (may not fill pane); crosshair aligned **on the rectangle**.

### A/B helper

```bash
./scripts/mouse_paint_compare.sh 1   # stretch
./scripts/mouse_paint_compare.sh 0   # 1:1
```

Run in an already-sized pane when possible. Debug log: `/tmp/sdl_kitty_debug.log` (`SDL_KITTY_DEBUG=1`).

---

## Lessons learned

1. **Present `ok` ≠ pixels on screen.** Always trust a visual check (or a tiny known-good APC probe). Rex can ack and still drop the frame.

2. **Huge FBs fail quietly.** Cap the texture; stretch with `c`/`r` if you need a full pane. Uncapped ~2800×2000 direct RGB was the blank-graphics root cause on Rex.

3. **Placement and mouse mapping are coupled.**  
   - Stretch + capped FB → scale mouse pane→FB.  
   - 1:1 + capped FB → raw 1016 on the image.  
   Scaling in 1:1 mode looks like “mouse is 100% of the pane, not the dark rectangle.”

4. **CSI 14t is the drawable ruler.** Do not invent a fixed HiDPI scale from one bad probe. Treat ~1.28× style ratios as unstable until re-measured; Rex chrome (row count) can look like a second ruler.

5. **Ignore Rex ioctl pixels for FB size.** Prefer CSI; ioctl xpixel/ypixel have disagreed and oversize the buffer.

6. **Stub 80×24 is real.** Automated / fresh `rex run` panes start tiny. Size before enabling mouse where possible; handle WINCH; re-get the SDL surface after `WINDOW_RESIZED`.

7. **Mouse modes poison naïve APC tests.** Enabling 1016 without raw mode / PumpEvents can dump APC as text and leak `^[<…M` into the pane. Test transmit and mouse separately first, then together under the SDL driver.

8. **`rex capture` is text-only.** It will not show Kitty graphics. Visual confirmation or a driver present log is required.

9. **Classify host quirks.** Against the Kitty graphics spec: client misunderstanding, ambiguous spec, or host bug (Rex around libghostty vs Ghostty’s own parser). Ghostty.app often stays aligned when Rex blanks — point fixes at the right layer.

10. **Ask before launching interactive panes; one test at a time.** State expectations and fail looks up front so A/B results stay interpretable.

---

## Best practices

### For app / demo authors

- Force `SDL_VIDEO_DRIVER=kitty` (driver never auto-selects).
- Prefer software surfaces / `SDL_RENDER_DRIVER=software`.
- After resize events, call `SDL_GetWindowSurface()` again before presenting.
- On Rex, prefer `SDL_KITTY_TRANSMIT=direct` until zlib/SHM are proven for your size.
- Leave `PLACE_CELLS` at default (stretch) for “game fills the terminal” UX.
- Use 1:1 when you need pixel-identical FB↔screen without resampling (accept letterboxing when capped).

### For driver work

- Keep **one** geometry story: CSI 14t pane, optional cap → FB, placement mode gates mouse math.
- Cap before transmit; log `init` and early `present` lines when `SDL_KITTY_DEBUG=1`.
- Probe 1016 with DECRQM; fall back to 1006 cleanly; restore all modes on quit.
- Do not scale mouse for 1:1; do scale for stretch when `mouse_coord ≠ pixel_*`.
- Prefer single-chunk direct when under host APC limits; omit fragile `m=` / `q=` on one-shot places when targeting Rex.

### For debugging a blank or drifted pane

| Symptom | Check |
|---|---|
| Blank / only “Video driver: kitty” text | FB size huge? Cap on? Multi-chunk? `m=0`/`o=z`? |
| Base64 / APC garbage on screen | Mouse enabled without consuming tty; or writes mixed with cooked output |
| Crosshair tracks pane, not 1:1 rectangle | Stretch-style scale applied with `PLACE_CELLS=0` |
| Crosshair cell-steppy | Stuck on 1006; or window still 1×1 |
| Stuck tiny / wrong aspect | Stub size; WINCH not applied; ioctl pixels used on Rex |
| Works in Ghostty, blanks in Rex | Transmit dialect / size / chunking — compare APC bytes |

Useful env:

- `SDL_KITTY_DEBUG=1` → `/tmp/sdl_kitty_debug.log`
- `SDL_KITTY_MOUSE_DEBUG=1` → occasional raw/SDL mouse lines on the tty
- `SDL_KITTY_FB_MAX=800x600` or `off`

---

## Code map

| File | Responsibility |
|---|---|
| `SDL_kittygraphics.c` | Size query, FB cap, setup/restore, transmit, mouse enable |
| `SDL_kittyevents.c` | SGR parse, pane↔FB mouse mapping, resize events |
| `SDL_kittyframebuffer.c` | Surface present → RGB → transmit |
| `SDL_kittyvideo.c` | Init, `PLACE_CELLS` / env, debug init line |
| `SDL_kittywindow.c` | Create window sized to terminal pixels |

---

## What we fixed (2026-10 session)

1. **Blank full-pane graphics on Rex** — default FB cap + stretch so the host gets a smaller single-chunk texture that still fills the pane.
2. **Cap only ran with stretch** — extended so 1:1 also caps (otherwise 1:1 at full CSI size blanks again).
3. **1:1 crosshair drift** — stop squashing full-pane 1016 into the capped FB when `place_cells` is off; use raw coordinates on the dark rectangle.
4. **Regression check** — stretch path still scales and stays aligned after the 1:1 mouse change.

Saved working binaries + `run.sh` under `examples/mouse_paint_fullpane/` and `examples/mouse_paint_1to1/`.
