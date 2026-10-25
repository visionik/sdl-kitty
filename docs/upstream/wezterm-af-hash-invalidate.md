# WezTerm: invalidate image after Kitty `a=f` (frame edit)

Upstream issue: https://github.com/wezterm/wezterm/issues/8281  
Filed 2026-10-25 against 20240203; tip source still has the same `ImageData.hash` design.

## Summary

Kitty graphics `a=f` (edit root frame in place) mutates pixel buffers in WezTerm but the **display never refreshes**. Sparse clients (sdl-kitty `dirty_bw`) therefore look like a full-frame-only path on WezTerm and flood the PTY with full `a=T` instead.

## Root cause

`wezterm-cell::image::ImageData` stores:

- `data: Mutex<ImageDataType>` — pixels (inner `Rgba8.hash` / `AnimRgba8.hashes` are updated on edit)
- `hash: [u8; 32]` — **content identity captured at creation only**

Docs on the struct:

> with an immutable content-identity hash captured at creation-time only.
> The hash is the dedup key for this image: `PartialEq` compares hashes only.

`ImageCell::compute_shape_hash` hashes `self.data.hash` (the outer identity), not the live pixels. GPU / texture / placement dedup therefore keeps serving the **first** frame after `kitty_frame_transmit` blits new pixels and refreshes only the **inner** `ImageDataType` hash (`term/src/terminalstate/kitty.rs`, edit-in-place arm sets `*hash = ImageDataType::hash_bytes(data)` on the `Rgba8` variant).

## Suggested fix

After a successful in-place `a=f` (and any path that mutates `ImageDataType` pixels):

1. Recompute the outer `ImageData.hash` from the new pixel content, **or**
2. Replace the `Arc<ImageData>` with a fresh `ImageData::with_data(...)` so identity changes, and invalidate attached placements / texture cache that keyed on the old hash.

Also emit a success reply for `a=f` (today only errors call `kitty_send_response`) so clients need not rely on `silent_af_ok`.

Related (separate issues): macOS SHM `t=s` ([#8063](https://github.com/wezterm/wezterm/pull/8063)); `a=T` with `i=`-only is silent (blocks `PERF_ACK`); `assign_image_to_cells` overflow/hang with `C=1` edge placements.

## Repro (sdl-kitty)

```bash
# WezTerm 20240203, from sdl-kitty
SDL_KITTY_DEBUG=1 SDL_KITTY_DIRTY_BW=1 ./scripts/run-with-kitty-sdl.sh ./build/dirty_rects_demo
```

Expect without fix: bouncing rect only updates on full `a=T` (or never via `a=f`). With working invalidate: `mode=dirty_bw` and visible sparse updates.

Client workaround attempted: stacked `a=T` overlays (`dirty_at` / `dirty_union_at`) — hang/panic or dual-box/trails; **not** a substitute for hash invalidate. See `docs/optimizations.md` Steps 1c / 1f and `TODO.md` Upstream → WezTerm.

## Files

| Path | Role |
|---|---|
| `wezterm-cell/src/image.rs` | `ImageData.hash` creation-time; `PartialEq` / `hash()` |
| `term/src/terminalstate/kitty.rs` | `kitty_frame_transmit` updates inner type hash only |
| `wezterm-cell/src/image.rs` | `ImageCell::compute_shape_hash` uses outer `data.hash` |
