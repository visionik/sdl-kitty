# sdl-kitty TODOs

Living list. Add new items as we go; mark done with date when closed.

---

## Per-terminal profile (done baseline 2026-10-23)

- [x] **`docs/terminals.md`** features/fixes matrix
- [x] **`KITTY_TerminalProfile`** + detect (`SDL_kittyprofile.c`)
- [x] Wire FB default cap, transmit prefer, max direct bytes, `a=f` coverage/px, silent `a=f`, Rex ioctl ignore, keys_on_stdin
- [ ] Soak-driven number tweaks (WezTerm row; raise Kitty/Ghostty FB further when SHM proven at 1080p+)
- [ ] Optional unit test for `KITTY_DetectTerminalProfile`

## Larger framebuffers

**Context (2026-10-23):** Default FB cap is now **per host** via `KITTY_TerminalProfile` (Rex/unknown/WezTerm 1280×720; Kitty/Ghostty 1920×1080). Uncapped Kitty ~3k×2k caused SHM→direct base64 dumps (`dvd_sprite`). See `docs/terminals.md`.

**Goal:** Support larger FBs safely without APC/base64 floods.

- [ ] **Transport-aware caps** — Soft-max by mode (e.g. SHM allows 1440p/window size; direct stays ≤ profile `max_direct_bytes`), not only a single global
- [ ] **Probe max safe `a=T` size** — Per-host binary search / ladder (SHM and direct); cache for the session
- [ ] **SHM failure policy** — Never fall back to uncapped direct; retry capped, try zlib if host OK, or hard-error with a clear message
- [ ] **Raise default when SHM OK** — After probe, allow larger default FB on Kitty/Ghostty/WezTerm when `transmit_mode == SHM` (profile already starts Kitty/Ghostty at 1080p)
- [ ] **Sparse upload at large FB** — Rely on app dirty rects + tile-diff + identical-frame skip; consider multi-rect `a=f` merge (not only bbox)
- [ ] **Tune a=f → full SHM threshold** — Profile fields `af_max_coverage` / `af_max_pixels` (default 0.40 / 160k). Revisit per-host after soaks; optional env knob
- [ ] **Soak ladder** — `dvd_sprite` / soft MAME at 1280×720 → 1920×1080 → higher; document per-host results in `docs/terminals.md`
- [ ] **Docs** — `SDL_KITTY_FB_MAX` / `FB_FIXED` / `FB_MAX=0` risk note in `docs/design.md` or a short `docs/framebuffer.md`

**Knobs today:** `SDL_KITTY_FB_MAX=WxH` (or `0` to uncapped), `SDL_KITTY_FB_FIXED=WxH`, `SDL_KITTY_TRANSMIT=shm|direct|zlib`

---

## Backlog (add below)

- [ ] Keyboard Phase 3 — text input (`SDL_SendKeyboardText`, flag 16 / associated text)
- [ ] WezTerm host soak (caps + `key_probe` + soft demo)
- [ ] FOCUS_GAINED/LOST chatter at startup — tighten if it bothers apps
- [ ] Optional: drop MAME `-background_input` once focus is proven solid without it
