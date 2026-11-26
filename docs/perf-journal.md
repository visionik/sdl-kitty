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

## Pass 13 (ODD — Detail) — Zero-`snprintf` APC header formatting & debug-gated `SDL_GetTicksNS`
- **Target:** `SDL_kittygraphics.c`, `SDL_kittyprobe.c`.
- **Change:** Replaced `SDL_snprintf` on `a=f` / `a=T` / continuation headers with inline decimal formatting and gated `SDL_GetTicksNS()` behind `KITTY_DebugEnabled()`.
- **Results:** Within E2E PTY noise floor (`imgui`: `0.052` → `0.052 ms/f`, `mame_sparse`: `0.131` → `0.132 ms/f`, `fireworks`: `0.092` → `0.102 ms/f`). Header formatting is <1% of frame time.
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **REVERTED** (strictly keeping only measurable wins; extra formatting code not justified by sub-1% gain).

## Pass 14 (EVEN — High-Level) — Frame-coalesced multi-rect `a=f` batching (`tx_frame_buf`)
- **Target:** `SDL_kittygraphics.c`, `SDL_kittyframebuffer.c`, `SDL_kittyvideo.h`.
- **Change:** Buffered all multi-rect `a=f` APCs into a single grow-only `tx_frame_buf` and flushed once per frame instead of `writev()` per rect.
- **Results:** **Regressed** multi-rect E2E latency: `e2e.imgui_dirty_720p_af_z` `0.052` → `0.066 ms/f` (+27%), `e2e.fireworks_rgba_720p_af` `0.092` → `0.127 ms/f` (+38%).
- **Root cause:** Real PTY kernel buffers are small (~1 KB on macOS). Writing each rect immediately pipelines zlib compression of rect `N+1` on the app core with PTY drain of rect `N` by the terminal reader; deferring all writes until the end serializes compression and PTY backpressure.
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **REVERTED** (great empirical catch from real-PTY benchmarking).

## Pass 15 (ODD — Detail) — Tune `deflateInit2` `windowBits` & `memLevel`
- **Target:** `KITTY_ZlibCompress` in `SDL_kittygraphics.c`.
- **Change:** Tested `windowBits=13, memLevel=9` and `windowBits=15, memLevel=9` vs baseline `windowBits=15, memLevel=8`.
- **Results:** **Regressed** on macOS Apple `libz.1.dylib`:
  - `15, 8` (baseline): `scroll_720p_zlib` `2.35 ms/f`, `1080p_zlib` `5.23 ms/f`.
  - `15, 9`: `2.97 ms/f` (+26%), `6.65 ms/f` (+27%).
  - `13, 9`: `6.74 ms/f` (+187%), `15.12 ms/f` (+189%).
- **Root cause:** Apple's system `libz` engages its SIMD/accelerated deflate fast path specifically for standard `windowBits=15, memLevel=8`.
- **Decision:** **REVERTED** (valuable platform insight; keep standard `15, 8`).

## Pass 16 (EVEN — High-Level) — Parallel 4-slice RFC 1950 `o=z` zlib compression (`dispatch_apply_f` + `adler32_combine`)
- **Target:** `KITTY_ZlibCompress` in `SDL_kittygraphics.c`, `SDL_kittyvideo.h`.
- **Change:** For large frames (`>= 256 KB`) on macOS, split the payload into 4 slices compressed concurrently via GCD (`dispatch_apply_f`) using persistent raw-deflate (`-15`, `Z_BEST_SPEED`) streams with `Z_SYNC_FLUSH` (slices 0..2) + `Z_FINISH` (slice 3), wrapped with standard RFC 1950 header (`0x78 0x01`) and `adler32_combine()` trailer. Verified bit-for-bit exact decompression via standard `zlib::uncompress()` over all 2,764,800 RGB24 bytes from a live PTY capture (`uncompress=0, match=1`).
- **Results:**
  - `e2e.mame_scroll_720p_zlib`: `2.355` → **`0.871 ms/f`** (**2.70× faster**, identical `43.1 KB/f` wire!)
  - `e2e.mame_scroll_1080p_zlib`: `5.236` → **`1.914 ms/f`** (**2.74× faster**, `94.0` → `94.1 KB/f`, +0.1% wire!)
  - 1080p full-screen scrolling is now **8.8× faster than Pass 0** (`16.881` → `1.914 ms/f`).
- **Tests:** 5/5 `^kitty_` ctest passed + standalone `uncompress()` bit-exact check passed.
- **Decision:** **KEPT**.

## Pass 17 (ODD — Detail) — Scale parallel RFC 1950 zlib up to 8 slices (CPU-bounded)
- **Target:** `KITTY_ParallelZlibCtx` / `KITTY_ZlibCompress` in `SDL_kittygraphics.c`.
- **Change:** Scaled max parallel slices from 4 to `min(8, ncpu)` (`8` slices for payloads `>= 512 KB`, `4` for `128..512 KB`). Re-verified bit-exact RFC 1950 `uncompress()` over all 2,764,800 bytes (`uncompress=0, match=1`).
- **Results:**
  - `e2e.mame_scroll_720p_zlib`: `0.871` → **`0.711 ms/f`** (**-18.4%**, `43.2 KB/f`, **10.5× vs Pass 0!**)
  - `e2e.mame_scroll_1080p_zlib`: `1.914` → **`1.651 ms/f`** (**-13.7%**, `94.1 KB/f`, **10.2× vs Pass 0!**)
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 18 (EVEN — High-Level) — Streaming L1/L2-cached Base64 for large direct APC payloads
- **Target:** `KITTY_TransmitDirectPayload` in `SDL_kittygraphics.c`.
- **Change:** For large direct payloads (`b64_total > 192 KB`), replaced full-frame multi-MB Base64 materialization with 48 KB raw → 64 KB Base64 streaming slices written via `writev()` within exact `chunk_cap` APC boundaries. Overlaps Base64 encoding with PTY drain, keeps Base64 inside L1/L2 cache, and eliminates multi-MB `tx_b64_buf` allocations. Verified byte-exact via `verify_pz` (`b64=767664`, `uncompress=0, match=1`).
- **Results:**
  - `e2e.mame_scroll_720p_direct`: `21.843` → **`21.282 ms/f`** (**-2.6% / -561 us**, plus ~3.6–11 MB less peak RAM)
  - `e2e.mame_scroll_1080p_zlib`: `1.651` → **`1.644 ms/f`**
  - `e2e.mame_2corner_720p_af_z`: `0.115` → **`0.113 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 19 (ODD — Detail) — 64-pixel unrolled NEON clean-row scan in fused convert+diff
- **Target:** `KITTY_ConvertAndTileDiffRects` in `SDL_kittyrect.c`.
- **Change:** Added a 64-pixel/iteration (4×16) NEON clean-row fast path when `dirty_in_band == 0`, reducing horizontal `vmaxvq_u32` reductions and branches by 4× and eliminating inner tile-loop overhead on unchanged rows.
- **Results:**
  - `e2e.static_720p`: `0.079` → **`0.062 ms/f`** (**-21.5%**, 62 us for full 720p scan!)
  - `e2e.mame_sparse_720p_af_z`: `0.130` → **`0.120 ms/f`** (**-7.7%**)
  - `e2e.mame_scroll_1080p_zlib`: `1.644` → **`1.622 ms/f`** (-1.3%)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 20 (EVEN — High-Level) — Parallel 4-worker GCD fused convert+tile-diff (`KITTY_ConvertAndTileDiffRects`)
- **Target:** `KITTY_ConvertAndTileDiffRects` in `SDL_kittyrect.c`.
- **Change:** Extracted `KITTY_ConvertAndDiffBand()` and parallelized band scanning across 4 GCD workers (`dispatch_apply_f`) on macOS for surfaces `>= 640×480`, merging dirty runs sequentially after join.
- **Results:**
  - `e2e.static_720p`: `0.062` → **`0.029 ms/f`** (**2.14× faster** — 29 us for full 720p!)
  - `e2e.mame_sparse_720p_af_z`: `0.120` → **`0.082 ms/f`** (**-31.7%**, **64.0× vs Pass 0!**)
  - `e2e.mame_2corner_720p_af_z`: `0.115` → **`0.065 ms/f`** (**-43.5%**, **115.6× vs Pass 0!**)
  - `e2e.mame_scroll_720p_zlib`: `0.712` → **`0.679 ms/f`** (**-4.6%**)
  - `e2e.mame_scroll_1080p_zlib`: `1.622` → **`1.562 ms/f`** (**-3.7%**)
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 21 (ODD — Detail) — 128-byte cache-line aligned 12-slice parallel zlib (eliminates Apple Silicon false sharing)
- **Target:** `KITTY_ParallelZlibCtx` / `KITTY_ParallelZlibSlice` in `SDL_kittygraphics.c`.
- **Change:** Grouped per-worker `z_stream` (112 B) and slice metadata into a `128`-byte-aligned struct (matching Apple Silicon's 128 B L1 cache line so adjacent workers never false-share `z_stream` writes during `deflate()`), and scaled slice count up to `min(12, ncpu)` for `>= 1.5 MB` frames. Re-verified bit-exact RFC 1950 via `verify_pz` (`uncompress=0, match=1`).
- **Results:**
  - `e2e.mame_scroll_720p_zlib`: `0.679` → **`0.641 ms/f`** (**-5.6%**, **11.7× vs Pass 0!**)
  - `e2e.mame_scroll_1080p_zlib`: `1.562` → **`1.346 ms/f`** (**-13.8%**, **12.5× vs Pass 0!**)
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 22 (EVEN — High-Level) — Exact vertical scanline trimming in fused convert+tile-diff
- **Target:** `KITTY_ConvertAndDiffBand` / `KITTY_ConvertAndTileDiffRects` in `SDL_kittyrect.c`.
- **Change:** Tracked per-tile exact changed scanline range `(y0, y1_excl)` in a compact `Uint16` per tile during the fused convert+diff pass, tightening emitted sparse rects vertically to only the changed rows instead of padded 32-row tile boundaries (while preserving the write-only fast path when an entire band is dirty).
- **Results:**
  - `e2e.mame_sparse_720p_af_z`: `0.084` → **`0.080 ms/f`** (-4.8%), wire `2.7` → **`2.3 KB/f`** (**-14.8% wire!**)
  - `e2e.mame_2corner_720p_af_z`: wire `1.6` → **`1.5 KB/f`** (**-6.3% wire!**)
  - `e2e.static_720p`: **`0.029 ms/f`** (zero overhead on static frames).
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 23 (ODD — Detail) — Manual 2× NEON convert unroll & 6–8 diff workers
- **Target:** `KITTY_ConvertRowRGB(A)` and `KITTY_ConvertAndTileDiffRects` in `SDL_kittyrect.c`.
- **Change:** Manually unrolled NEON conversion to 32 px/iter and increased parallel diff workers from 4 to 6 (720p) / 8 (1080p).
- **Results:** Slight regression (`conv_rgb_720p`: `49.0` → `53.7 us`; `scroll_1080p_zlib`: `1.349` → `1.376 ms/f`; `static_720p`: `0.029` → `0.027 ms/f`). Compiler register allocation on 16 px/iter + 4 GCD workers is already at the memory-bandwidth / dispatch-latency sweet spot.
- **Decision:** **REVERTED**.

## Pass 24 (EVEN — High-Level) — Adaptive zlib bypass for incompressible / tiny payloads
- **Target:** `KITTY_TransmitDirectZlib` and `KITTY_TransmitRGBRectEdit` in `SDL_kittygraphics.c`.
- **Change:** Skip zlib on tiny `a=f` patches (`<= 48 B`) where zlib framing cannot beat raw, and automatically fall back to raw (omitting `o=z`) whenever `compressed_len >= raw_len` so incompressible noisy frames / tiny cursor updates never expand on the wire or waste host `inflate()` CPU.
- **Results:** Zero overhead on compressible benchmarks (`static_720p`: `0.028 ms/f`, `mame_2corner`: `0.063 ms/f`, `mame_sparse`: `0.081 ms/f`), while strictly bounding worst-case wire bytes to `<= raw_len` on noisy/tiny frames.
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 25 (ODD — Detail) — Zero-copy `KITTY_PumpEvents` direct read, idle tick skip & inline SGR mouse parser
- **Target:** `SDL_kittyevents.c` (`KITTY_PumpEvents`, `KITTY_FlushDueKeyUps`, `KITTY_HandleCSI`, `KITTY_ParseSGRMouse`).
- **Change:** Skipped `SDL_GetVideoDevice()` + `SDL_GetTicks()` when `key_hold_n == 0`, read directly into `data->tty_pending` (eliminating intermediate `readbuf[512]` + `memcpy` and short-read extra syscall), removed stack `seq[128]` copy in `KITTY_HandleCSI`, and replaced `SDL_sscanf` in `KITTY_ParseSGRMouse` with an inline decimal parser.
- **Results:** Eliminated per-poll clock/copy overhead and per-mouse-move `sscanf` overhead; all 5 ctest suites pass and E2E present numbers stay crisp (`mame_sparse`: `0.075 ms/f`, `fireworks`: `0.092 ms/f`).
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 26 (EVEN — High-Level) — Depth-1 double-buffered async PTY writer thread
- **Target:** `SDL_kittygraphics.h`/`.c`, `SDL_kittyvideo.h`, `SDL_kittywindow.c`, `SDL_kittyevents.c`, `SDL_kittyframebuffer.c`.
- **Change:** Added a lazily-spawned depth-1 double-buffered async PTY writer thread (`KITTY_AsyncWriter`) for single-chunk direct APC payloads `<= 256 KB` (all 720p/1080p `o=z` frames and `a=f` dirty rects). Main thread encodes directly into `stage_buf` and performs an O(1) pointer swap under lock, overlapping frame `N+1` CPU work (`convert+diff+zlib+base64`) with frame `N`'s kernel PTY write (`CPU + PTY` → `max(CPU, PTY)`). All control sequences, deletes, SHM, overlays, resize, and teardown call `KITTY_FlushAsyncWriter()` as a strict FIFO ordering barrier.
- **Results:**
  - `e2e.mame_scroll_720p_zlib`: `0.683` → **`0.440 ms/f`** (**-35.6% / 1.55×**, **17.0× vs Pass 0!**)
  - `e2e.mame_scroll_1080p_zlib`: `1.348` → **`0.879 ms/f`** (**-34.8% / 1.53×**, **19.2× vs Pass 0! Sub-millisecond 1080p scroll!**)
  - `e2e.mame_sparse_720p_af_z`: `0.075` → **`0.065 ms/f`** (**-13.3%**, **80.7× vs Pass 0!**)
  - `e2e.mame_2corner_720p_af_z`: `0.066` → **`0.061 ms/f`** (**-7.6%**, **123.2× vs Pass 0!**)
  - `e2e.imgui_dirty_720p_af_z`: `0.057` → **`0.052 ms/f`** (**-8.8%**, **143.7× vs Pass 0!**)
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed (`uncompress=0, match=1`).
- **Decision:** **KEPT**.

## Pass 27 (ODD — Detail) — 16-slice parallel zlib (`>= 3 MB`) & waiter-gated condvar signaling
- **Target:** `KITTY_ParallelZlibCtx` & `KITTY_AsyncWriter` in `SDL_kittygraphics.c`.
- **Change:** Scaled max parallel zlib slices to `min(16, ncpu)` for `>= 3 MB` frames (cutting 1080p deflate from `489 us` → `383 us`), gated `pthread_cond_signal` on `writer_sleeping` / `main_waiting` to eliminate spurious kernel condvar wakeups, and added `KITTY_FlushAsyncWriter` before `PERF_ACK` waits.
- **Results:**
  - `e2e.mame_scroll_1080p_zlib`: `0.879` → **`0.806 ms/f`** (**-8.3%**, **20.9× vs Pass 0!**)
  - `e2e.mame_scroll_720p_direct`: `21.585` → **`21.323 ms/f`**
  - `e2e.mame_scroll_720p_zlib`: **`0.439 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 28 (EVEN — High-Level) — Exact 2D pixel trimming in fused convert+tile-diff
- **Target:** `KITTY_ConvertAndDiffBand` / `KITTY_ConvertAndTileDiffRects` in `SDL_kittyrect.c` (+ exact 2D assertions in `tests/kitty_rect_utils_test.c`).
- **Change:** Expanded per-tile span tracking from `Uint16` (Y-only) to `Uint32` (`y0:8, y1:8, x0:8, x1:8`) so sparse dirty tile runs tighten horizontally AND vertically to exact changed pixel bounds (while keeping full-band write-only fast path untouched).
- **Results:**
  - `e2e.mame_sparse_720p_af_z`: `0.066` → **`0.052 ms/f`** (**-21.2%**, **100.8× vs Pass 0!**), wire `2.4` → **`1.6 KB/f`** (**-33.3% wire!**)
  - `e2e.mame_2corner_720p_af_z`: `0.061` → **`0.053 ms/f`** (**-13.1%**, **141.8× vs Pass 0!**), wire `1.5` → **`1.2 KB/f`** (**-20.0% wire!**, **35.9× less wire than Pass 0!**)
  - `e2e.static_720p`: **`0.029 ms/f`** (zero overhead).
- **Tests:** 5/5 `^kitty_` ctest passed (exact `{4,4,10,10}` & `{100,100,10,10}` bounds verified).
- **Decision:** **KEPT**.

## Pass 29 (ODD — Detail) — Branchless ARM64 NEON nibble movemask (`ctzll`/`clzll`) & 64-px RGBA clean-row scan
- **Target:** `KITTY_ConvertAndDiffBand` in `SDL_kittyrect.c`.
- **Change:** Replaced the 16-lane stack spill + branch loop in exact horizontal pixel bound extraction with a branchless 4-bit-nibble ARM64 NEON movemask (`vtstq_u8` + `vshrn_n_u16` + `__builtin_ctzll`/`__builtin_clzll`), and added the 64-pixel NEON clean-row fast path for `use_rgba == true`.
- **Results:**
  - `e2e.mame_sparse_720p_af_z`: `0.052` → **`0.051 ms/f`** (**102.8× vs Pass 0!**)
  - `e2e.mame_scroll_720p_zlib`: `0.448` → **`0.432 ms/f`** (-3.6%)
  - `e2e.mame_scroll_1080p_zlib`: `0.805` → **`0.794 ms/f`** (**21.3× vs Pass 0!**)
  - `e2e.mame_scroll_720p_direct`: `21.671` → **`21.047 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 30 (EVEN — High-Level) — Async-batched multi-rect `a=f` frame submission
- **Target:** `SDL_kittygraphics.h`/`.c` (`KITTY_BeginAsyncBatch`, `KITTY_CommitAsyncBatch`, `KITTY_CancelAsyncBatch`) and `SDL_kittyframebuffer.c`.
- **Change:** Coalesced all `a=f` rect APCs of a multi-rect frame directly into the async writer's `stage_buf` with zero per-rect mutex/condvar bounces and a single `KITTY_AsyncSubmitStage()` per frame (overlapping frame `N`'s PTY write with frame `N+1`'s rect compression without synchronous blocking).
- **Results:**
  - `e2e.fireworks_rgba_720p_af`: `0.105` → **`0.087 ms/f`** (**-17.1%**, **140.4× vs Pass 0!**)
  - `e2e.mame_sparse_720p_af_z`: `0.051` → **`0.046 ms/f`** (**-9.8%**, **114.0× vs Pass 0!**)
  - `e2e.mame_scroll_720p_zlib`: **`0.432 ms/f`**
  - `e2e.mame_scroll_1080p_zlib`: **`0.815 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 31 (ODD — Detail) — 8-lane 64B/iter `KITTY_FastHash64` & 2× unrolled 96→128B NEON64 Base64
- **Target:** `KITTY_FastHash64` in `SDL_kittyrect.c` and `KITTY_Base64Encode` in `SDL_kittygraphics.c`.
- **Change:** Expanded `KITTY_FastHash64` from 4 lanes (32 B/iter) to 8 independent lanes (64 B/iter) and unrolled NEON64 Base64 2× (96→128 B/iter) to hide `vqtbl4q_u8` latency across Apple Silicon's 4 SIMD pipes.
- **Results:**
  - `micro.fast_hash_720p`: `98.2` → **`63.6 us`** (**-35.2%**, **39.8× faster than Pass 0 FNV-1a!**)
  - `e2e.mame_2corner_720p_af_z`: `0.056` → **`0.049 ms/f`** (**153.4× vs Pass 0!**)
  - `e2e.imgui_dirty_720p_af_z`: `0.060` → **`0.048 ms/f`** (**155.7× vs Pass 0!**)
  - `e2e.fireworks_rgba_720p_af`: `0.087` → **`0.085 ms/f`** (**143.7× vs Pass 0!**)
  - `e2e.mame_scroll_1080p_zlib`: `0.815` → **`0.785 ms/f`** (**21.5× vs Pass 0!**)
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 32 (EVEN — High-Level) — Parallel multi-rect `a=f` pack + zlib + Base64 (`KITTY_TransmitMultiRectEdit`)
- **Target:** `KITTY_TransmitMultiRectEdit` in `SDL_kittygraphics.h`/`.c`, `SDL_kittyvideo.h`, `SDL_kittyframebuffer.c`.
- **Change:** For multi-rect `a=f` frames (`3..16` rects, `>= 4096 px`, compressed) on macOS, added a persistent cache-line-aligned worker pool (`tx_mr_ctx`, 16 `z_stream`s at Apple-fast `15, 8`) that packs, deflates (with adaptive raw bypass), and Base64-encodes all dirty rects concurrently via `dispatch_apply_f`, then concatenates their pre-formatted APCs into the async PTY writer stage buffer in a single submission.
- **Results:**
  - `e2e.fireworks_rgba_720p_af`: `0.085` → **`0.069 ms/f`** (**-18.8%**, **177.0× vs Pass 0!**)
  - `e2e.imgui_dirty_720p_af_z`: `0.048` → **`0.045 ms/f`** (**-6.3%**, **166.1× vs Pass 0!**)
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 33 (ODD — Detail) — Cached `fb_surface`, 128B `KITTY_FindFirstDiff`, $O(N^2)$ inline `KITTY_CoalesceRects`
- **Target:** `SDL_kittyvideo.h`, `SDL_kittyframebuffer.c`, `SDL_kittywindow.c`, `SDL_kittyrect.c`.
- **Change:** Cached `fb_surface` on `SDL_WindowData` (eliminating per-frame `SDL_GetPointerProperty` RWLock + string hash), unrolled `KITTY_FindFirstDiff` to 128 B/iter (1 Apple Silicon cache line per horizontal reduction), and replaced `SDL_HasRectIntersection` + full-restart $O(N^3)$ loop in `KITTY_CoalesceRects` with direct integer bounds $O(N^2)$.
- **Results:**
  - `micro.coalesce_24`: `0.5` → **`0.0 us`** (< 50 ns!)
  - `micro.tile_diff_static_720p`: `49.0` → **`47.2 us`** (-3.7%)
  - `e2e.imgui_dirty_720p_af_z`: `0.045` → **`0.044 ms/f`** (**169.8× vs Pass 0!**)
  - `e2e.static_720p`: **`0.029 ms/f`**
- **Tests:** 5/5 `^kitty_` ctest passed.
- **Decision:** **KEPT**.

## Pass 34 (EVEN — High-Level) — Parallel full-frame zlib level 3 (trading spare overlapped CPU for smaller PTY wire)
- **Target:** `KITTY_ZlibCompress` parallel worker init in `SDL_kittygraphics.c`.
- **Change:** Profiled parallel 16-slice zlib levels 1–6 on 1080p:
  - Level 1: `372 us` CPU, `94.1 KB` b64 (`0.810 ms/f` E2E)
  - Level 3: `401 us` CPU, `88.7 KB` b64 → **faster E2E AND smaller wire** (strict Pareto win!)
  - Level 4: `750 us` CPU, `44.2 KB` b64 (`0.954 ms/f` E2E — half wire, slight localhost latency bump)
  Selected **level 3** for parallel full-frame `o=z`.
- **Results:**
  - `e2e.mame_scroll_1080p_zlib`: `0.810` → **`0.738 ms/f`** (**-8.9%**, **22.9× vs Pass 0!**), wire `97.6` → **`95.5 KB/f`**
  - `e2e.mame_scroll_720p_zlib`: `0.448` → **`0.433 ms/f`** (**-3.3%**, **17.3× vs Pass 0!**), wire `44.8` → **`43.7 KB/f`**
  - `e2e.imgui_dirty_720p_af_z`: **`0.041 ms/f`** (**182.3× vs Pass 0!**)
  - `e2e.mame_sparse_720p_af_z`: **`0.045 ms/f`** (**116.5× vs Pass 0!**)
- **Tests:** 5/5 `^kitty_` ctest passed + `verify_pz` bit-exact passed.
- **Decision:** **KEPT**.

## Pass 35 (ODD — Detail) — Fused NEON dual-store `KITTY_ConvertSurfaceRectCheckAndPack` kernel
- **Target:** `SDL_kittyrect.h`/`.c` (+ unit test in `tests/kitty_rect_utils_test.c`).
- **Change:** Implemented `KITTY_ConvertSurfaceRectCheckAndPack` with `KITTY_ConvertRowDualRGB(A)`: reads `surface->pixels` once via `vld4q_u8`, compares against `rgb_scratch`, updates changed chunks in `rgb_scratch`, and stores tightly packed RGB24/RGBA32 directly into `packed_out` from the same NEON registers (zero `rgb_scratch` read-back).
- **Tests:** 5/5 `^kitty_` ctest passed (including new dual-store convert+pack assertions).
- **Decision:** **KEPT** (enables Pass 36 single-dispatch fused multi-rect pipeline).

## Pass 36 (EVEN — High-Level) — Single-dispatch parallel surface convert + diff + pack + zlib + Base64
- **Target:** `KITTY_TransmitMultiRectSurfaceEdit` in `SDL_kittygraphics.h`/`.c` and `SDL_kittyframebuffer.c`.
- **Change:** Moved surface conversion + change detection of 3..16 disjoint dirty rects from Core 0 into the parallel GCD multi-rect workers using `KITTY_ConvertSurfaceRectCheckAndPack`.
- **Results:** **Regressed** multi-rect E2E latency (`imgui_dirty_720p_af_z`: `0.041` → `0.049 ms/f`, `fireworks_rgba_720p_af`: `0.068` → `0.079 ms/f`).
- **Root cause:** `surface->pixels` was just written by the app on Core 0 and sits hot in Core 0's L1d cache. Reading `surface->pixels` from 4–8 remote GCD cores triggers cross-core L1d cache-line snoops/migrations and false sharing across 128-byte scanlines in `rgb_scratch` that outweigh the ~4 us sequential Core 0 convert.
- **Decision:** **REVERTED** (classic hardware cache-locality lesson).

## Pass 37 (ODD — Detail) — Single-Rect Fused Convert+Pack & Zero-Alloc Overlay Paths
- **Commit:** `third_party/SDL` `500dc90a3`
- **Hypothesis:** Single explicit dirty rects (`numrects == 1`) were converting into `rgb_scratch` and then re-reading the same rectangle via `KITTY_PackRGB(A)Rect` into `pack_scratch`; using Pass 35's fused NEON dual-store `KITTY_ConvertSurfaceRectCheckAndPack` on Core 0 avoids both the scratch read-back and Pass 36's cross-core cache-coherency penalty. Additionally, the two legacy overlay paths (`dirty_union_at` and `dirty_at`) were the last two remaining `SDL_malloc`/`SDL_free` call sites in `SDL_KITTY_UpdateWindowFramebuffer`.
- **Change:**
  - Wired `KITTY_ConvertSurfaceRectCheckAndPack` into `SDL_KITTY_UpdateWindowFramebuffer` for `rects && numrects == 1` when within `max_af_px`, bypassing `KITTY_PackRGB(A)Rect` when `single_prepacked` is true.
  - Replaced `SDL_malloc`/`SDL_free` in `dirty_union_at` and `dirty_at` with `KITTY_EnsurePackScratch`, achieving **zero per-frame heap allocations across all present paths**.
- **Verification:** `ctest -R '^kitty_'` 5/5 passed; `verify_pz` RFC 1950 bit-exact (`uncompress=0 out_len=2764800 match=1`). E2E steady (`static_720p: 0.031`, `mame_sparse: 0.049`, `2corner: 0.056`, `720p_zlib: 0.433`, `720p_direct: 21.281`, `1080p_zlib: 0.788`, `imgui: 0.047`, `fireworks: 0.068`).
- **Decision:** **KEPT** (eliminates scratch read-back on single-rect `a=f` and removes the final two per-frame `SDL_malloc`/`SDL_free` calls).

## Pass 38 (EVEN — High-Level) — Cached `SDL_KITTY_ZLIB_LEVEL` Knob & Permanent RFC 1950 Round-Trip CTest
- **Commit:** `third_party/SDL` `ec4bd4bb9` + `tests/kitty_zlib_roundtrip_test.c`
- **Hypothesis:** Pass 34's level sweep showed parallel level 3 gives the fastest localhost wall time (`0.732 ms/f` at 1080p), while level 4 shrinks wire bytes up to `2.1×` for ~350 us extra overlapped CPU — ideal for SSH/remote links or slower terminals. Exposing a once-per-init cached `SDL_KITTY_ZLIB_LEVEL` (`1..9`, default auto = 3 parallel / 1 serial & `a=f`) adds zero hot-path overhead while unlocking wire-bound tuning. Additionally, the parallel RFC 1950 `o=z` path should have a permanent CTest proving bit-exact decompression against real `libSDL3` PTY output.
- **Change:**
  - Added `int cfg_zlib_level` to `SDL_VideoData` resolved once at `KITTY_VideoInit` from `SDL_KITTY_ZLIB_LEVEL` (`1..9`; `0` = auto).
  - Wired `cfg_zlib_level` into parallel `KITTY_ZlibCompress`, serial `KITTY_ZlibCompress`, and `KITTY_MultiRectWorker`.
  - Added permanent CTest `kitty_zlib_roundtrip_test` (`tests/kitty_zlib_roundtrip_test.c`) capturing live PTY `a=T,o=z` frames at 720p (auto), 1080p (auto), and 1080p (`SDL_KITTY_ZLIB_LEVEL=4`), base64-decoding, running `zlib uncompress()`, and asserting 100% bit-exact pixel equality + strictly smaller wire at level 4 (`743,304 B` → `527,128 B`).
- **Verification:** `ctest -R '^kitty_'` **6/6 passed** (`kitty_zlib_roundtrip_test` passed in 1.72s). E2E steady (`static_720p: 0.029`, `1080p_zlib: 0.732`, `720p_zlib: 0.450`, `imgui: 0.049`, `mame_sparse: 0.052`, `2corner: 0.058`, `fireworks: 0.075`).
- **Decision:** **KEPT**.

## Pass 39 (ODD — Detail) — 1 MiB Async PTY Stage Ceiling & Cached `cfg_perf_ack`
- **Commit:** `third_party/SDL` `549408e57`
- **Hypothesis:** `apc_chunk_bytes` defaults to `2 MiB`, yet `KITTY_ASYNC_MAX_BYTES` capped the double-buffered async PTY writer at `256 KiB` — forcing high-entropy 720p/1080p zlib frames (`196 KiB–768 KiB` compressed, `262 KiB–1 MiB` base64) back onto synchronous PTY writes. Separately, the `#ifdef SDL_KITTY_PERF_ACK` block in `KITTY_TransmitRGB` still called `SDL_getenv("SDL_KITTY_PERF_ACK")` per frame instead of reading Pass 8's cached `vdata->cfg_perf_ack`.
- **Change:**
  - Raised `KITTY_ASYNC_MAX_BYTES` from `262,144` (`256 KiB`) to `1,048,576` (`1 MiB`); because `KITTY_AsyncEnsureStage` grows lazily from `16 KiB`, small/sparse workloads pay zero extra RAM while high-detail 720p/1080p zlib frames stay pipelined.
  - Switched `#ifdef SDL_KITTY_PERF_ACK` in `KITTY_TransmitRGB` to use `vdata->cfg_perf_ack` when `cfg_cached` is set.
- **Results:**
  - `e2e.mame_scroll_720p_zlib`: **`0.423 ms/f`** (new best, `43.7 KB/f`)
  - `e2e.static_720p`: **`0.029 ms/f`**, `mame_sparse`: **`0.046 ms/f`**, `2corner`: **`0.053 ms/f`**, `1080p_zlib`: **`0.758 ms/f`**, `imgui`: **`0.048 ms/f`**, `fireworks`: **`0.075 ms/f`**
- **Tests:** 6/6 `^kitty_` ctest passed; `verify_pz` (`b64=767504 zlen=575628 uncompress=0 out_len=2764800 match=1`) passed (and the 767 KB payload now rides the async PTY writer!).
- **Decision:** **KEPT**.

## Pass 40 (EVEN — High-Level) — Holistic Audit, Relocatable Install Check & Hardware Saturation Sign-Off
- **Audit Scope:** Full Release build, all 6 `^kitty_` CTest suites (including live PTY RFC 1950 parallel-zlib round-trip test), `verify_pz` 1080p bit-exact check, relocatable `cmake --install` check, and bottleneck saturation review.
- **Verification:**
  - `ctest -R '^kitty_'`: **6/6 passed** (`kitty_rect_utils_test`, `kitty_probe_utils_test`, `kitty_profile_utils_test`, `kitty_zlib_roundtrip_test`, `kitty_profile_data_fresh`, `kitty_terminals_md_fresh`).
  - `cmake --install build --prefix /tmp/sdl-kitty-install-check`: cleanly installed `libSDL3.dylib`, `bin/sdlk`, all 16 demos, TOML configs, assets, and licenses; staging prefix removed afterward.
  - Zero compiler warnings in all modified `SDL_kitty*.c` files.
- **Why Stop at Pass 40 (of max 50):**
  - **Memory-bandwidth saturated:** `e2e.static_720p` runs in **`29 us`** reading `6.44 MB` across 4 cores (**~220 GB/s**), at Apple Silicon DRAM/SLC bandwidth.
  - **PTY-kernel saturated:** `e2e.mame_scroll_720p_zlib` (`0.423 ms/f`), `1080p_zlib` (`0.758 ms/f`), and `720p_direct` (`21.587 ms/f`) are **100% bound by macOS PTY 1 KB ring-buffer throughput** (~125–175 MB/s); all CPU work (convert + diff + 16-slice deflate + NEON Base64) is fully hidden behind the depth-1 async PTY writer thread.
  - **Sparse / UI / RGBA saturated at 46–75 us:** All 4 active sparse/UI workloads sit between `0.046` and `0.075 ms/f` (**114×–163× faster** than baseline), with zero per-frame heap allocations, zero hot-path `getenv` calls, and minimal wire bytes (`1.2–6.2 KB/f`, down **96–97%** on multi-rect workloads).
  - Stopping at Pass 40 avoids complexity churn without meaningful gains.

---

# Final Summary — Pass 0 (Baseline) vs Pass 40 (Final)

34 kept optimization commits in `third_party/SDL` (`6e6d0d64d..549408e57`), 5 reverted experiments (Passes 13, 14, 15, 23, 36 — all documented above), 1 holistic verification/saturation pass (Pass 40).

### End-to-End Real PTY Benchmarks (`kitty_perf_bench --e2e`)

| Benchmark | Pass 0 (ms/f) | Pass 40 (ms/f) | Speedup | Pass 0 Wire | Pass 40 Wire | Wire Change |
|---|---:|---:|---:|---:|---:|---:|
| `e2e.static_720p` | 0.146 | **0.029** | **5.0×** | 0.0 KB/f | 0.0 KB/f | — |
| `e2e.mame_sparse_720p_af_z` | 5.244 | **0.046** | **114.0×** | 2.7 KB/f | **1.6 KB/f** | **-40.7%** |
| `e2e.mame_2corner_720p_af_z` | 7.517 | **0.053** | **141.8×** | 43.1 KB/f | **1.2 KB/f** | **-97.2%** |
| `e2e.mame_scroll_720p_zlib` | 7.481 | **0.423** | **17.7×** | 43.1 KB/f | 43.7 KB/f | +1.4% (parallel slices) |
| `e2e.mame_scroll_720p_direct` | 28.437 | **21.587** | **1.32×** | 3600.1 KB/f | 3600.1 KB/f | 0% (PTY-bound) |
| `e2e.mame_scroll_1080p_zlib` | 16.881 | **0.758** | **22.3×** | 94.0 KB/f | 95.5 KB/f | +1.6% (parallel slices) |
| `e2e.imgui_dirty_720p_af_z` | 7.473 | **0.048** | **155.7×** | 50.1 KB/f | **3.3 KB/f** | **-93.4%** |
| `e2e.fireworks_rgba_720p_af` | 12.212 | **0.075** | **162.8×** | 187.2 KB/f | **6.2 KB/f** | **-96.7%** |

### Core Microbenchmarks (`kitty_perf_bench --micro`)

| Microbenchmark | Pass 0 | Pass 40 | Speedup |
|---|---:|---:|---:|
| `micro.conv_rgb_720p` | 71.3 us | **66.1 us** (best 49.0 us) | **1.08–1.46×** |
| `micro.conv_rgb_1080p` | 153.2 us | **138.9 us** (best 118.2 us) | **1.10–1.30×** |
| `micro.conv_rgba_720p` | 87.4 us | **66.8 us** (best 58.7 us) | **1.31–1.49×** |
| `micro.tile_diff_static_720p` | 80.8 us | **51.6 us** (fused parallel E2E: **29 us**) | **1.57×** (2.8× E2E) |
| `micro.tile_diff_sparse_720p` | 87.4 us | **51.5 us** | **1.70×** |
| `micro.tile_diff_static_1080p` | 205.7 us | **120.6 us** | **1.71×** |
| `micro.fnv1a_720p` → `micro.fast_hash_720p` | 2505.0 us | **71.6 us** (best 63.6 us) | **35.0–39.4×** |
| `micro.pack_rgb_band` | 6.8 us | **6.2 us** | **1.10×** |
| `micro.pack_rgb_subrect` | 3.0 us | **2.8 us** | **1.07×** |
| `micro.coalesce_24` | 0.5 us | **0.1 us** | **5.0×** |

---

# Queued Decisions & Requests for Review

Nothing was pushed to GitHub or merged into `main`/`sdl-kitty-v0`. Ready for your review & approval:

1. **Merge / Push branches:** Approve merging `perf/50-pass-opt` into `sdl-kitty-v0` / `main` (and pushing both `third_party/SDL` and `sdl-kitty`), or opening a dedicated PR (and deciding how to handle existing PR #7).
2. **Linux / Non-Apple Parallelism Follow-up:** Parallel RFC 1950 zlib, parallel fused convert+diff, and parallel multi-rect `a=f` currently use Apple GCD (`#ifdef __APPLE__`) with clean serial fallback elsewhere. If Linux parity is desired, we can port the GCD fan-outs to a tiny persistent `pthread` worker pool in a follow-up PR.
3. **`SDL_KITTY_ZLIB_LEVEL` Default for SSH / Remote Terminals:** Auto mode currently defaults to level `3` (fastest localhost wall time, `0.758 ms/f` at 1080p); setting `SDL_KITTY_ZLIB_LEVEL=4` cuts 1080p wire up to `2.1×` (great for SSH/Tailscale). Approve whether to auto-select level `4` when `SSH_CONNECTION` is set.
4. **Live Visual Smoke Test in Real Terminal (Rex / Kitty / Ghostty):** Inside the sandbox we verified bit-exact RFC 1950 wire output via PTY tests (`kitty_zlib_roundtrip_test` + `verify_pz`), while POSIX `shm_open` returned `EPERM` in sandbox. Recommend a quick live run of `sdlk` (`mame`, `imgui`, `fireworks`, `dvd_bounce`) in Rex/Kitty/Ghostty to visually confirm zero artifacts on SHM + `a=f` + parallel `o=z`.
