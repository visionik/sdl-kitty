# sdl-kitty Performance Optimization Journal

- **Branch:** `perf/50-pass-opt` (both `sdl-kitty` and `third_party/SDL`)
- **Methodology:** Up to 50 passes alternating **ODD = Detail** (micro-architecture, SIMD, syscalls, hot loops, memory layout) and **EVEN = High-Level** (algorithmic/pipeline/architecture, redundant work elimination, batching, adaptive wire reduction).
- **Benchmark:** `tests/kitty_perf_bench` (deterministic microbenchmarks + real `libSDL3` end-to-end PTY benchmarks via `openpty()` measuring median `ms/frame` and exact wire `KB/frame`).
- **Rule:** Every pass must pass all 5 `ctest` suites (`^kitty_`) and show a real measurable gain; regressions or non-gains are reverted and recorded.

---

## Pass 0 — Baseline (`6e6d0d64d` / `ed34d5b`)

Machine: Apple Silicon ARM64 macOS, Release build (`-DCMAKE_BUILD_TYPE=Release`).

| Metric | Baseline | Wire (KB/f) |
|---|---:|---:|
| `micro.conv_rgb_720p` | 71.3 us | — |
| `micro.conv_rgb_1080p` | 153.2 us | — |
| `micro.conv_rgba_720p` | 87.4 us | — |
| `micro.tile_diff_static_720p` | 80.8 us | — |
| `micro.tile_diff_sparse_720p` | 87.4 us | — |
| `micro.tile_diff_static_1080p` | 205.7 us | — |
| `micro.fnv1a_720p` | **2505.0 us** | — |
| `micro.pack_rgb_band` | 6.8 us | — |
| `micro.pack_rgb_subrect` | 3.0 us | — |
| `micro.coalesce_24` | 0.5 us | — |
| `e2e.static_720p` | 0.146 ms/f | 0.0 |
| `e2e.mame_sparse_720p_af_z` | **5.244 ms/f** | 2.7 |
| `e2e.mame_2corner_720p_af_z` | **7.517 ms/f** | **43.1** |
| `e2e.mame_scroll_720p_zlib` | 7.481 ms/f | 43.1 |
| `e2e.mame_scroll_720p_direct` | **28.437 ms/f** | 3600.1 |
| `e2e.mame_scroll_1080p_zlib` | **16.881 ms/f** | 94.0 |
| `e2e.imgui_dirty_720p_af_z` | **7.473 ms/f** | 50.1 |
| `e2e.fireworks_rgba_720p_af` | **12.212 ms/f** | 187.2 |

### Key initial bottlenecks spotted from Pass 0
1. **`KITTY_Fnv1a64` takes 2.5 ms/call and runs TWICE per frame (= 5.0 ms/f!)** — accounting for ~95% of `e2e.mame_sparse_720p_af_z` and ~67% of `e2e.imgui_dirty_720p_af_z`!
2. **`e2e.mame_2corner_720p_af_z` sends full-screen 43.1 KB/f** because `KITTY_TileDiffBBox` collapses two small corner changes into a 100%-coverage bounding box.
3. **Per-frame `SDL_malloc`/`SDL_free` + `compress2()`** (`deflateInit`/`deflateEnd`) on every frame & dirty rect, plus scalar Base64 and 3× `write()` syscalls per APC chunk.
4. **12–15 `SDL_getenv()` calls per frame** on the present hot path.

---
## Pass 1 (ODD — Detail) — NEON 16-pixel `vld4q_u8`/`vst3q_u8`/`vst4q_u8` surface conversion
- **Target:** `KITTY_ConvertSurfaceRectToRGB` & `KITTY_ConvertSurfaceRectToRGBA` in `third_party/SDL/src/video/kitty/SDL_kittyrect.c`.
- **Change:** Added ARM64 little-endian NEON 16-pixel vector path (`vld4q_u8` → channel reorder → `vst3q_u8` / `vst4q_u8`) with scalar tail.
- **Results:**
  - `micro.conv_rgb_720p`: `71.3 us` → **`50.8 us`** (**-28.8%**)
  - `micro.conv_rgb_1080p`: `153.2 us` → **`118.2 us`** (**-22.8%**)
  - `micro.conv_rgba_720p`: `87.4 us` → **`58.7 us`** (**-32.8%**)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT** (SDL commit committed on `perf/50-pass-opt`).

## Pass 2 (EVEN — High-Level) — Eliminate redundant `upload_skip` double-hashing
- **Target:** `SDL_KITTY_UpdateWindowFramebuffer` in `third_party/SDL/src/video/kitty/SDL_kittyframebuffer.c`.
- **Change:** Skip `upload_skip` full-FB hashing when `tile-diff` already proved pixels changed (since `tile-diff` already handles identical full-surface frames in ~80 us), skip post-upload hashing when `upload_skip` is disabled, and reuse the pre-upload hash instead of hashing the unmodified scratch buffer a second time after upload.
- **Results:**
  - `e2e.mame_sparse_720p_af_z`: `5.244 ms/f` → **`0.202 ms/f`** (**-96.1% / 26.0× faster**)
  - `e2e.mame_2corner_720p_af_z`: `7.517 ms/f` → **`2.603 ms/f`** (**-65.4% / 2.89×**)
  - `e2e.mame_scroll_720p_zlib`: `7.481 ms/f` → **`2.551 ms/f`** (**-65.9% / 2.93×**)
  - `e2e.mame_scroll_1080p_zlib`: `16.881 ms/f` → **`5.667 ms/f`** (**-66.4% / 2.98×**)
  - `e2e.imgui_dirty_720p_af_z`: `7.473 ms/f` → **`5.122 ms/f`** (**-31.5%**)
  - `e2e.fireworks_rgba_720p_af`: `12.212 ms/f` → **`8.922 ms/f`** (**-26.9%**)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 3 (ODD — Detail) — 4-lane 64-bit parallel hash (`KITTY_FastHash64`) for `upload_skip`
- **Target:** `KITTY_FastHash64` in `SDL_kittyrect.c` + `SDL_kittyframebuffer.c` (preserving exact `KITTY_Fnv1a64` for public API/tests).
- **Change:** Added a 4-lane 32-bytes/iter multiply-rotate hash breaking FNV-1a's serial 3-cycle-per-byte dependency chain.
- **Results:**
  - Hash 720p RGB24 (2.76 MB): `2507.7 us` (FNV-1a) → **`104.9 us`** (**23.9× faster**)
  - `e2e.imgui_dirty_720p_af_z`: `5.122 ms/f` → **`2.551 ms/f`** (**-50.2%**)
  - `e2e.fireworks_rgba_720p_af`: `8.922 ms/f` → **`5.667 ms/f`** (**-36.5%**)
  - `e2e.mame_sparse_720p_af_z`: `0.202 ms/f` → **`0.186 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 4 (EVEN — High-Level) — Persistent `z_stream` (`deflateReset`) + zero-malloc transmit/pack scratch buffers
- **Target:** `SDL_kittygraphics.c`, `SDL_kittyframebuffer.c`, `SDL_kittyvideo.h`, `SDL_kittyvideo.c`, `SDL_kittywindow.c`.
- **Change:** Replaced per-frame/per-rect `compress2()` (`deflateInit` + ~256 KB alloc/memset + `deflateEnd`) with a persistent `z_stream` reused via `deflateReset()`, and replaced per-frame `SDL_malloc`/`SDL_free` for `pack_scratch`, `tx_z_buf`, and `tx_b64_buf` with grow-only persistent buffers freed on quit/destroy.
- **Results:**
  - `e2e.static_720p`: `0.129` → **`0.123 ms/f`**
  - `e2e.mame_sparse_720p_af_z`: `0.186` → **`0.180 ms/f`**
  - `e2e.mame_scroll_720p_zlib`: `2.441` → **`2.409 ms/f`**
  - `e2e.mame_scroll_1080p_zlib`: `5.501` → **`5.491 ms/f`**
  - `e2e.imgui_dirty_720p_af_z`: `2.551` → **`2.539 ms/f`**
  - `e2e.fireworks_rgba_720p_af`: `5.667` → **`5.603 ms/f`**
  - Zero per-frame heap allocations on the present hot path (eliminates allocator jitter and macOS VM page faults on large frames).
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.


## Pass 5 (ODD — Detail) — ARM64 NEON64 vectorized Base64 encoder
- **Target:** `KITTY_Base64Encode` in `third_party/SDL/src/video/kitty/SDL_kittygraphics.c`.
- **Change:** Implemented 48→64 byte NEON64 path (`vld3q_u8` → 6-bit index extraction → `vqtbl4q_u8` lookup → `vst4q_u8`) with hoisted triple-loop bound and scalar tail.
- **Results:**
  - `e2e.mame_scroll_720p_direct`: `23.137 ms/f` → **`21.863 ms/f`** (**-5.5%**, saving ~1.27 ms on 3.6 MB base64)
  - `e2e.mame_scroll_1080p_zlib`: `5.491 ms/f` → **`5.456 ms/f`**
  - `e2e.mame_2corner_720p_af_z`: `2.508 ms/f` → **`2.489 ms/f`**
  - `e2e.imgui_dirty_720p_af_z`: `2.539 ms/f` → **`2.531 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT** (`third_party/SDL` commit `322f6ac14`).

## Pass 6 (EVEN — High-Level) — Multi-rect tile-diff (`KITTY_TileDiffRects`) & actual-area multi-`a=f` gating
- **Target:** `SDL_kittyrect.h`/`.c` and `SDL_kittyframebuffer.c` (+ unit tests in `tests/kitty_rect_utils_test.c`).
- **Change:**
  1. Added `KITTY_TileDiffRects()` which merges contiguous dirty tiles into horizontal runs and extends matching runs vertically across tile rows, emitting up to 16 disjoint dirty rectangles instead of collapsing scattered changes into one full-screen bounding box.
  2. Gated multi-rect `a=f` on actual total dirty pixel coverage (`multi_total_px / fb_area`) rather than the bloated union bounding-box coverage (while preserving `max_af_px` PTY-flood protection).
- **Results:**
  - `e2e.mame_2corner_720p_af_z`: `2.489 ms/f` (`43.1 KB/f`) → **`0.179 ms/f` (`1.6 KB/f`)** (**13.9× faster, 26.9× less wire**)
  - `e2e.imgui_dirty_720p_af_z`: `2.531 ms/f` (`50.1 KB/f`) → **`0.195 ms/f` (`3.2 KB/f`)** (**13.0× faster, 15.7× less wire**)
  - `e2e.fireworks_rgba_720p_af`: `5.603 ms/f` (`187.2 KB/f`) → **`0.300 ms/f` (`6.0 KB/f`)** (**18.7× faster, 31.2× less wire**)
  - `e2e.mame_scroll_720p_zlib`: `2.409` → **`2.385 ms/f`**; `1080p_zlib`: `5.456` → **`5.429 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed (including new `test_tile_diff_rects` & `KITTY_FastHash64` tests).
- **Decision:** **KEPT** (massive win on scattered UI, sprite, and 2-corner workloads).

## Pass 7 (ODD — Detail) — Single-syscall `writev()` APC framing & SHM pre-unlink skip
- **Target:** `SDL_kittygraphics.c` (`KITTY_WritevAll`, `KITTY_TransmitDirectPayload`, `KITTY_TransmitRGBRectEdit`, `KITTY_TransmitShm`).
- **Change:** Replaced 3 separate `write()` syscalls per APC chunk (`header`, `b64`, `\033\\`) with a single scatter-gather `writev()` syscall; formatted SHM APC into a single stack buffer + single `write()`; removed guaranteed-`ENOENT` `shm_unlink()` before `shm_open(O_CREAT|O_EXCL)` (only unlinking on `EEXIST`).
- **Results:**
  - `e2e.static_720p`: `0.138` → **`0.121 ms/f`** (-12.3%)
  - `e2e.mame_2corner_720p_af_z`: `0.179` → **`0.163 ms/f`** (-8.9%)
  - `e2e.imgui_dirty_720p_af_z`: `0.195` → **`0.188 ms/f`** (-3.6%)
  - `e2e.fireworks_rgba_720p_af`: `0.300` → **`0.290 ms/f`** (-3.3%)
  - `e2e.mame_sparse_720p_af_z`: `0.189` → **`0.186 ms/f`** (-1.6%)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 8 (EVEN — High-Level) — Cache hot-path env & profile policy on `SDL_VideoData`
- **Target:** `SDL_kittyvideo.h`, `SDL_kittyvideo.c`, `SDL_kittyframebuffer.c`, `SDL_kittygraphics.c`.
- **Change:** Resolved all 15+ per-frame `SDL_getenv()` + `strcasecmp()` + `strtol()` + `atof()` lookups (`SDL_KITTY_DIRTY`, `FB_DIFF*`, `UPLOAD_SKIP`, `AF_MAX_COVERAGE`, `DIRTY_BW`, `COALESCE_GAP`, `DIRTY_UNION_AT`, `DIRTY_AT`, `PINGPONG`, `AF_ZLIB`, `TRANSMIT`, `PERF_ACK`, chunk/direct limits) once during `KITTY_VideoInit` into cached fields on `SDL_VideoData` (with fallback when `!cfg_cached` for standalone unit tests).
- **Results:**
  - `e2e.mame_sparse_720p_af_z`: `0.186` → **`0.181 ms/f`** (-2.7%)
  - `e2e.imgui_dirty_720p_af_z`: `0.188` → **`0.184 ms/f`** (-2.1%)
  - `e2e.mame_scroll_720p_direct`: `22.036` → **`21.734 ms/f`** (-1.4%)
  - `e2e.mame_scroll_1080p_zlib`: `5.441` → **`5.356 ms/f`** (-1.6%)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 9 (ODD — Detail) — Row-major NEON streaming tile-diff with early band exit
- **Target:** `KITTY_TileDiffBBox` and `KITTY_TileDiffRects` in `SDL_kittyrect.c`.
- **Change:** Replaced tile-by-tile strided `memcmp` (28,800 calls with 3840-byte stride thrashing L1d) with a shared row-major streaming scanner (`KITTY_ScanTileRowBand` + 64-byte/iter ARM64 NEON `KITTY_FindFirstDiff`) that scans linear spans of clean tiles and exits a 32-row tile band immediately once all tiles in the band are dirty.
- **Results:**
  - `micro.tile_diff_static_720p`: `80.0 us` → **`48.4 us`** (**-39.5%**)
  - `micro.tile_diff_sparse_720p`: `82.0 us` → **`47.3 us`** (**-42.3%**)
  - `micro.tile_diff_static_1080p`: `206.1 us` → **`116.7 us`** (**-43.4%**)
  - `e2e.static_720p`: `0.126` → **`0.096 ms/f`** (**-23.8%**)
  - `e2e.mame_sparse_720p_af_z`: `0.181` → **`0.165 ms/f`** (**-8.8%**)
  - `e2e.mame_2corner_720p_af_z`: `0.166` → **`0.148 ms/f`** (**-10.8%**)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 10 (EVEN — High-Level) — Fused single-pass NEON convert + tile-diff (eliminates `rgb_back`)
- **Target:** `KITTY_ConvertAndTileDiffRects` in `SDL_kittyrect.h`/`.c`, `SDL_kittyframebuffer.c`, `SDL_kittyvideo.h`, `SDL_kittywindow.c` (+ unit test `test_fused_convert_diff`).
- **Change:** Fused `ConvertSurfaceRectToRGB(A)` and `KITTY_TileDiffRects` into a single pass comparing surface pixels against `rgb_scratch` directly in NEON registers. Unchanged tiles stay strictly read-only (zero writes to `rgb_scratch`), changed tiles update in-place, and already-dirty tiles/bands switch to write-only NEON conversion. Eliminated the entire `rgb_back` framebuffer copy (halving per-window scratch memory).
- **Results:**
  - `e2e.static_720p`: `0.096` → **`0.078 ms/f`** (**-18.8%**, 78 us for full 720p check!)
  - `e2e.mame_sparse_720p_af_z`: `0.165` → **`0.139 ms/f`** (**-15.8%**)
  - `e2e.mame_2corner_720p_af_z`: `0.148` → **`0.118 ms/f`** (**-20.3%**)
  - `e2e.mame_scroll_1080p_zlib`: `5.334` → **`5.237 ms/f`** (**-1.8%**)
  - 50% less scratch RAM per window (`rgb_back` removed).
- **Tests:** 5/5 `^kitty_` ctest passed (including `test_fused_convert_diff`).
- **Decision:** **KEPT**.

## Pass 11 (ODD — Detail) — Contiguous band `memcpy` in rect pack & flat-strip NEON convert
- **Target:** `KITTY_PackRGBRect`, `KITTY_PackRGBARect`, `KITTY_ConvertSurfaceRectToRGB`, `KITTY_ConvertSurfaceRectToRGBA` in `SDL_kittyrect.c`.
- **Change:** Added single-`memcpy` fast path for full-width horizontal bands (`area.x == 0 && area.w == fb_w`), hoisted row pointer stride arithmetic in subrect packing, and unified surface conversion onto `KITTY_ConvertRowRGB(A)` with a single-loop flat-strip fast path when `pitch == w * 4`.
- **Results:**
  - `micro.conv_rgb_720p`: `52.0` → **`49.0 us`** (-5.8%)
  - `micro.tile_diff_static_1080p`: `116.7` → **`105.5 us`** (-9.6%)
  - `e2e.static_720p`: `0.078` → **`0.074 ms/f`**
  - `e2e.mame_scroll_720p_zlib`: `2.387` → **`2.350 ms/f`** (-1.5%)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 12 (EVEN — High-Level) — Per-rect convert+diff for app-supplied dirty rects (eliminates union-bbox over-conversion & full-FB hash)
- **Target:** `KITTY_ConvertSurfaceRectCheckChanged` in `SDL_kittyrect.h`/`.c` and `SDL_kittyframebuffer.c` (+ unit test in `tests/kitty_rect_utils_test.c`).
- **Change:** When an app passes explicit dirty rects (`UpdateWindowSurfaceRects`), convert ONLY each dirty rect (instead of their bloated union bounding box) while checking in NEON registers whether any pixel actually changed vs `rgb_scratch`. If zero pixels changed, skip the wire immediately without hashing the full framebuffer; if pixels changed, filter to only changed rects and skip the redundant full-FB `KITTY_FastHash64` pass.
- **Results:**
  - `e2e.imgui_dirty_720p_af_z`: `0.189` → **`0.052 ms/f`** (**3.63× faster**, **143.7× vs Pass 0!**)
  - `e2e.fireworks_rgba_720p_af`: `0.307` → **`0.092 ms/f`** (**3.34× faster**, **132.7× vs Pass 0!**)
  - `e2e.mame_sparse_720p_af_z`: `0.139` → **`0.131 ms/f`** (-5.8%)
  - `e2e.mame_2corner_720p_af_z`: `0.119` → **`0.113 ms/f`** (-5.0%)
  - `e2e.static_720p`: `0.074` → **`0.073 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.
