# Kitty capability optimizations

Phased work to use more of the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/) without regressing Rex stretch / 1:1 mouse baselines (`examples/mouse_paint_fullpane`, `examples/mouse_paint_1to1`).

Gates per step: automated tests **and** Rex visual confirmation.

## Step 1a — Dirty CPU convert (done)

**Verified:** 2026-10-22 — unit tests pass; Rex visual OK for `dirty_rects_demo`, `mouse_paint_fullpane`, and `mouse_paint_1to1`.

**Goal:** Honor `SDL_UpdateWindowSurfaceRects` dirty rects for **CPU** work. Wire upload stays a full-frame `a=T` (same bandwidth as before).

**Behavior:**

- Retained RGB24/RGBA scratch per window (`SDL_WindowData.rgb_scratch`)
- First present / resize / invalid scratch → full convert (`mode=full`) then full `a=T`
- Later updates → convert the dirty union (`mode=dirty_cpu`) at **any** coverage (no 50% cutoff)
- With dirty BW on + live image → `a=f` the union; full `a=T` only if edit fails
- Kill-switch: `SDL_KITTY_DIRTY=0`

**Code:** `SDL_kittyrect.c`, `SDL_kittyframebuffer.c`

**Tests:**

```bash
cmake --build build -j
ctest --test-dir build -R kitty_rect_utils_test --output-on-failure
```

**Visual:** `build/dirty_rects_demo` with `SDL_KITTY_DEBUG=1` — bouncing rect should log `dirty_cpu` / `dirty_bw`; `full` only on first frame or failed edit.

**Debug log** (`SDL_KITTY_DEBUG=1` → `/tmp/sdl_kitty_debug.log`):

```text
present #N WxH place=P mode=dirty_cpu|full rects=R union=Uw×Uh coverage=C convert_px=… upload_px=… -> ok
```

## Step 1e — Coalesce dirty rects (Exp 5)

**Goal:** Before multi-rect `a=f`, merge overlapping or near rects (expanded by `coalesce_gap_px`) so fewer APC edits.

**TOML:** `coalesce_gap_px = 2` (0 = off). Env: `SDL_KITTY_COALESCE_GAP`.

**Notes:** Helps Ghostty/Kitty/Rex multi-`a=f` (e.g. `alpha_blit`). On WezTerm without working `a=f`, little wire effect. Log: `coalesce N -> M rects (gap=G)`.

**Ghostty tip soak (2026-10-24):** `alpha_blit` coalesce ON logs `6 -> 3` / `6 -> 2` (gap=2); OFF (`COALESCE_GAP=0`) keeps `rects=6`. Wire/ms_tx flat (~256 / 0.56) on SHM — coalesce cuts APC count, not SHM payload size. Kept on Ghostty tip + WezTerm profiles.

## Step 1f — Single union `a=T` (Exp 3)

**Goal:** Safer WezTerm sparse path than multi `dirty_at`: one overlay per frame covering the dirty **union**, with DECSC/DECRC around CUP, place_cells `c`/`r` stretch, and skip when the place-cells box touches the screen edge.

**TOML:** `dirty_union_at = false` (killed after soak). Env: `SDL_KITTY_DIRTY_UNION_AT=0|1` still forces.

**Behavior (code retained):**

- Same `af_max_coverage` / `af_max_pixels` gates as `a=f`
- Clears previous overlays, places one `a=T` at z=10
- Debug: `mode=dirty_union` with `upload_px == convert_px`
- Falls through to full `a=T` on edge / pack / transmit failure

**WezTerm soak (2026-10-24) — killed:**

- Engaged (`mode=dirty_union`), no hang with single overlay + edge skip + DECSC/DECRC.
- Visuals wrong: dual yellow boxes / trails. Root cause in WezTerm cell model: Kitty style uses `attach_image` (z-stack), so the base full `a=T` keeps old yellow pixels while the overlay paints the new union on top. Overlay delete only removes the overlay id; it cannot mutate the base.
- Natural size (`c`/`r`=0): tiny jumpy overlay + trails.
- With `c`/`r`: better scale, still dual-box (stale base + overlay).
- Wire ~same as full zlib blue on `dirty_rects_demo` (~8.3 vs 8.7 KB).
- Real fix remains upstream **a=f `ImageData.hash` invalidate** (hash is creation-time only; GPU/dedup key never refreshes after `kitty_frame_transmit`).

## Step 1d — Upload skip (Exp 6)

**Goal:** If the full RGB scratch FNV-1a matches the last successful upload, skip the wire present (`mode=upload_skip`).

**TOML:** `upload_skip = true` (default via profile tunables). Kill: `SDL_KITTY_UPLOAD_SKIP=0`.

**Notes:** Complements tile-diff (soft full flips). Helps WezTerm when apps pass dirty rects but pixels are unchanged. Hash invalidated on resize / scratch rebuild.

## Step 1c — Dirty `a=T` overlays (experiment, WezTerm)

**Goal:** When `a=f` cannot refresh the display (WezTerm `ImageData.hash`), still shrink PTY traffic by transmitting each dirty rect as its own stacked `a=T` (z=10) over the live base image.

**Enable:** `SDL_KITTY_DIRTY_AT=1` only (profile default **off** after WezTerm crash).

**Behavior:**

- Same `af_max_coverage` / `af_max_pixels` gates as `a=f`; max 16 rects/frame
- `place_cells`: map FB rect → cell span + CUP, stretch overlay with `c`/`r`
- 1:1: CUP + optional `X`/`Y` cell offsets, natural pixel size
- Full `a=T` / successful `a=f` clears overlay ids first
- Debug: `mode=dirty_at` with `upload_px` ≪ full FB

**WezTerm 20240203 failure:** paints a few overlay frames then panics
(`assign_image_to_cells` overflow / `NotNan` add) **or hangs** (~1GB RSS:
PTY thread stuck placing cells while UI waits on the pane mutex). Likely
small-image + `c`/`r` stretch + CUP + `C=1` near edges with ungarded
cell-delta / `physical_rows - cursor.y` math (tip #6344 guards help zeros;
20240203 still vulnerable). Tracked under Upstream → WezTerm in `TODO.md`.

**If reviving before upstream:** DECSC/DECRC around overlay CUP; omit overlay
`c`/`r` (1:1 pixels); skip placements whose cell box touches the screen edge;
or wait for a=f hash invalidate (preferred) / newer WezTerm.

**Limits:** host crash/hang risk; cell seams; overlay churn; not a substitute for `a=f` invalidate.

## Step 1b — Bandwidth partial (`a=f`) (done, default off)

**Verified:** 2026-10-22 — unit tests; Rex `dirty_bw` with forced env; later same day: init `a=f` probe (`frame_edit_probe ok -> dirty_bw=on`) enables BW without env; `dvd_sprite` uses `mode=dirty_bw`.

**Goal:** When `SDL_KITTY_DIRTY_BW=1` and a live image exists, sparse updates send only the dirty bbox via Kitty animation frame edit:

```text
a=f,f=24,t=d,i=<live>,r=1,X=1,x=…,y=…,s=…,v=…
```

(`r=1` edits the root frame; payload is packed RGB for that rectangle only.)

**Fallback:** any failure → full-frame `a=T` (same as 1a wire path).

**Default:** follows an init **`a=f` frame-edit probe** (`frame_edit_ok`). Explicit APC `OK` or a silent reply (no error — Rex) counts as success; APC error disables BW. `SDL_KITTY_DIRTY_BW=0|1` still forces off/on.

Debug: `frame_edit_probe ok|fail -> dirty_bw=on|off`

**Debug:** `mode=dirty_bw` with `upload_px == convert_px` (much smaller than full FB).

**Tests:** `KITTY_DirtyBWEnabled`, `KITTY_PackRGBRect` in `kitty_rect_utils_test`.

**Visual:** `SDL_KITTY_DIRTY_BW=1 SDL_KITTY_DEBUG=1 ./build/dirty_rects_demo` — expect `dirty_bw` in the log and a clean bounce; then fullpane/1to1 with the flag on.

## Step 2 — `a=q` transmit probe (done)

**Verified:** 2026-10-22 — unit tests pass; Rex `solid_present` log `direct=ok … term=rex -> direct`, colors cycling.

**Goal:** On init, probe Kitty graphics query (`a=q`) for direct and (when not Rex) SHM; select `transmit_mode`.

**Rules (`KITTY_SelectTransmitMode`):**

1. `SDL_KITTY_TRANSMIT=direct|shm|zlib` wins  
2. Else profile `force_transmit`  
3. Else `prefer_direct` → **zlib** if `prefer_zlib` else **direct**  
4. Else SHM probe OK → **shm**  
5. Else → **zlib** if `prefer_zlib` else **direct**

**Debug:** `transmit_probe … prefer_zlib=… profile_force=… -> direct|shm|zlib`

## Perf measurement (wire + time)

Present debug lines include:

| Field | Meaning |
|---|---|
| `wire=` | APC bytes written (headers + base64 + ST) |
| `raw=` | Pre-b64 payload (RGB size, or zlib compressed size) |
| `ms_tx=` | Client wall time for compress + encode + write |
| `ms_ack=` | Host round-trip wait for `Gi=<id>;OK` (`-1` if not measured) |

Enable ack timing with `SDL_KITTY_PERF_ACK=1` (syncs presents to host processing — slower, but includes inflate). Every 60 presents also logs `perf_summary`.

```bash
SDL_KITTY_DEBUG=1 SDL_KITTY_PERF_ACK=1 ./scripts/run-with-kitty-sdl.sh ./build/dirty_rects_demo
python3 scripts/kitty_perf_summary.py /tmp/sdl_kitty_debug.log
```

Compare zlib vs direct: lower `wire` with higher `ms_tx`/`ms_ack` means compression helped the PTY but hurt latency.

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

## Same-`(i,p)` replace (done)

**Verified:** 2026-10-22 — `dvd_sprite` log `live=1 pingpong=0` + `dirty_bw`; visually clean.

**Default:** full `a=T` reuses `live_image_id` with `p=1` (no per-frame delete). Dirty BW already patched that id.

**Kill-switch:** `SDL_KITTY_PINGPONG=1` restores the old swap ids 1↔2 + delete previous.

Debug presents log `pingpong=0|1`.

## Fixed FB size (done)

**Verified:** 2026-10-22 — `SDL_KITTY_FB_FIXED=800x450` + resize logged `action=replace fb 800x450->800x450` (grid changed); colors kept filling.

**Env:** `SDL_KITTY_FB_FIXED=WxH` sets an **exact** framebuffer size after the CSI size query. Stretch (`PLACE_CELLS=1`) still fills the pane via `c`/`r`.

On WINCH, if only the cell grid changes, Step 4 logs `resize action=replace` and sends `a=p` (no pixel re-upload). Aspect-fit `SDL_KITTY_FB_MAX` still changes FB size when the pane aspect changes, so replace is rarer without FIXED.

## Atlas / multi-placement (demo)

**Example:** `examples/atlas_sprites.c` + `examples/kitty_apc.c`

1. Build a 2-row sprite sheet (DVD + Blu-ray × 8 colors)  
2. Upload once with `a=t` (no place)  
3. Bounce 10 placements (5 DVD + 5 Blu-ray, `p=1…10`) with crop + sub-cell `X`/`Y`  
4. Random start positions, velocities, and colors; wall hits advance color when not pulsing  
5. Pairwise AABB collide (edge-triggered): **only the hitters** pulse colors for 10s

SDL is used for events/window size only; graphics APC goes to stdout. Run:

```bash
./build/atlas_sprites
```

## RGBA `f=32` (opt-in FB alpha)

**Env:** `SDL_KITTY_FB_ALPHA=1` → window framebuffer is `SDL_PIXELFORMAT_ARGB8888` and presents use Kitty **`f=32`** (R,G,B,A bytes). Default remains XRGB + `f=24` so demos that write `0x00RRGGBB` literals stay opaque.

- Convert/pack: `KITTY_ConvertSurfaceRectToRGBA` / `KITTY_PackRGBARect`
- Full `a=T` and dirty `a=f` both honor `f=24|32`
- Bandwidth ≈ +33% when enabled; still behind normal `GetWindowSurface` / `UpdateWindowSurface`
- Demo: `examples/alpha_blit.c` (sets `FB_ALPHA=1`, translucent discs over a grid)

**Sparse policy:** no coverage threshold — with a live image, dirty unions stay on `a=f` even when large; full `a=T` on first present / resize / failed edit only.

Atlas multi-placement stays **demo-only** (`atlas_sprites` + `kitty_apc`); no public Kitty placement API (normal SDL3 surface path only).

## Fireworks demo

**Example:** `examples/fireworks.c` — rockets + particle bursts over `examples/assets/lake_night.png` (`UpdateWindowSurfaceRects`, dirty BW). Click or space forces a burst; q/Esc quits.

## Later steps

| Step | Status |
|---|---|
| Prefer dirty BW defaults / more aggressive `a=f` | **Skipped** — probe-default + always-sparse is enough |
| Other soft-surface demos | Optional |
| DynAPI / packaging polish | Optional |
| Upstream checklist / PR | Optional |

**FB model only** (product choice): improve `GetWindowSurface` / `UpdateWindowSurface` presents. Deferred Kitty compositor features: driver atlas/multi-placement API, animation-compose video API, relative placements, Unicode placeholders, z-index/under-text, shared `I=` ids. `atlas_sprites` stays a standalone demo.
