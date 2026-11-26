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
