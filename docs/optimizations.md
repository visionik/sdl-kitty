# Kitty capability optimizations

Phased work to use more of the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/) without regressing Rex stretch / 1:1 mouse baselines (`examples/mouse_paint_fullpane`, `examples/mouse_paint_1to1`).

Gates per step: automated tests **and** Rex visual confirmation.

## Step 1a — Dirty CPU convert (done)

**Verified:** 2026-10-22 — unit tests pass; Rex visual OK for `dirty_rects_demo`, `mouse_paint_fullpane`, and `mouse_paint_1to1`.

**Goal:** Honor `SDL_UpdateWindowSurfaceRects` dirty rects for **CPU** work. Wire upload stays a full-frame `a=T` (same bandwidth as before).

**Behavior:**

- Retained RGB24 scratch per window (`SDL_WindowData.rgb_scratch`)
- First present / resize / invalid scratch → full convert (`mode=full`)
- Later sparse updates → convert only the union of dirty rects (`mode=dirty_cpu`) when coverage &lt; 50%
- Still transmits the **entire** scratch via `a=T`
- Kill-switch: `SDL_KITTY_DIRTY=0`

**Code:** `SDL_kittyrect.c`, `SDL_kittyframebuffer.c`

**Tests:**

```bash
cmake --build build -j
ctest --test-dir build -R kitty_rect_utils_test --output-on-failure
```

**Visual:** `build/dirty_rects_demo` with `SDL_KITTY_DEBUG=1` — bouncing rect should log `dirty_cpu`; periodic/fullscreen flash logs `full`. Then re-check fullpane + 1to1 paint.

**Debug log** (`SDL_KITTY_DEBUG=1` → `/tmp/sdl_kitty_debug.log`):

```text
present #N WxH place=P mode=dirty_cpu|full rects=R union=Uw×Uh coverage=C convert_px=… upload_px=… -> ok
```

## Step 1b — Bandwidth partial (`a=f`) (done, default off)

**Verified:** 2026-10-22 — unit tests pass; Rex `dirty_rects_demo` with `DIRTY_BW=1` shows `mode=dirty_bw` and feels faster; fullpane + 1to1 still align with the flag on (those demos full-update every frame).

**Goal:** When `SDL_KITTY_DIRTY_BW=1` and a live image exists, sparse updates send only the dirty bbox via Kitty animation frame edit:

```text
a=f,f=24,t=d,i=<live>,r=1,X=1,x=…,y=…,s=…,v=…
```

(`r=1` edits the root frame; payload is packed RGB for that rectangle only.)

**Fallback:** any failure → full-frame `a=T` (same as 1a wire path).

**Default:** off (Rex animation support is unproven). Enable explicitly for experiments.

**Debug:** `mode=dirty_bw` with `upload_px == convert_px` (much smaller than full FB).

**Tests:** `KITTY_DirtyBWEnabled`, `KITTY_PackRGBRect` in `kitty_rect_utils_test`.

**Visual:** `SDL_KITTY_DIRTY_BW=1 SDL_KITTY_DEBUG=1 ./build/dirty_rects_demo` — expect `dirty_bw` in the log and a clean bounce; then fullpane/1to1 with the flag on.

## Step 2 — `a=q` transmit probe (done)

**Verified:** 2026-10-22 — unit tests pass; Rex `solid_present` log `direct=ok … term=rex -> direct`, colors cycling.

**Goal:** On init, probe Kitty graphics query (`a=q`) for direct and (when not Rex) SHM; select `transmit_mode`.

**Rules (`KITTY_SelectTransmitMode`):**

1. `SDL_KITTY_TRANSMIT=direct|shm|zlib` wins  
2. Else `TERM_PROGRAM=rex` → **direct** (skip SHM preference)  
3. Else SHM probe OK → **shm**  
4. Else → **direct**

**Debug:** `transmit_probe direct=ok|fail shm=ok|fail force=… term=… -> direct|shm|zlib`

**Tests:** `ctest -R kitty_probe_utils_test`

**Visual (Rex):** expect `-> direct` and `solid_present` / paint still work. On Kitty.app/Ghostty local without force, may select `shm`.

## Step 3 — `C=1` / `N=1` / `p=1` on place (done)

**Verified:** 2026-10-22 — header unit tests; Rex `solid_present` + fullpane paint aligned.

**Goal:** Every full `a=T` present includes:

| Key | Meaning |
|---|---|
| `C=1` | Do not advance the cursor after place |
| `N=1` | Transient image hint (evict sooner under quota) |
| `p=1` | Stable placement id per image id |

Still ping-pongs image ids 1↔2 and deletes the previous (flicker-safe). Same-`(i,p)` replace without delete is left for later if needed.

**Code:** `KITTY_FormatTransmitHeader` in `SDL_kittyprobe.c`; used by direct / zlib / SHM paths.

**Tests:** header must contain `C=1`, `N=1`, `p=1` (`kitty_probe_utils_test`).

**Visual:** `solid_present` + fullpane / 1to1 — no cursor junk, no new flicker/blank.

## Step 4 — Resize re-place (done)

**Verified:** 2026-10-22 — `KITTY_ClassifyResize` unit tests; Rex `solid_present` resize kept colors (log showed `action=full` because FB aspect changed under the 1280×720 cap — correct). `action=replace` is unit-covered for same-FB + new grid.

**Goal:** On WINCH with **stretch** (`PLACE_CELLS=1`), if the capped FB size is unchanged and only `cols`/`rows` changed, send `a=p` with new `c`/`r` (no pixel re-upload) and **skip** SDL surface recreate.

| Action | When |
|---|---|
| `none` | Grid and FB unchanged |
| `replace` | Stretch + live image + same FB + new grid → `a=p` |
| `full` | FB size changed, 1:1 mode, or no live image → existing resize path |

**Debug:** `resize action=replace|full|none fb … grid …`

**Kill-switch:** none yet; failure of `a=p` falls back to full resize.

**Tests:** `KITTY_ClassifyResize` in `kitty_probe_utils_test`.

**Visual:** Run `solid_present` or fullpane; resize the Rex pane while capped FB stays put — expect `action=replace`, no blank, mouse still maps after full resize when FB does change.

## Later steps (not done)

| Step | Summary |
|---|---|
| 5–6 | Stronger partial / atlas (optional) |
