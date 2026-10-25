# sdl-kitty design notes

For placement, FB capping, and mouse mapping (including Rex lessons), see
[framebuffer-placement-and-mouse.md](framebuffer-placement-and-mouse.md).

See the session plan for the full roadmap. Short form:

- **API:** SDL3 public video/renderer API
- **Backend:** Kitty graphics protocol APC over the tty
- **MVP+:** window framebuffer → RGB24 → SHM / zlib / direct base64 → `a=T`
- **Selection:** only when `SDL_VIDEO_DRIVER=kitty` (never auto)
- **Ship:** out-of-tree SDL branch → DynAPI lib → upstream PR

## Driver files (in `third_party/SDL/src/video/kitty/`)

| File | Role |
|---|---|
| `SDL_kittyvideo.c` | bootstrap, VideoInit/Quit, display from `TIOCGWINSZ`, SIGWINCH |
| `SDL_kittywindow.c` | Create/Destroy/SetSize, focus |
| `SDL_kittyframebuffer.c` | surface + XRGB→RGB present, double-buffer image ids |
| `SDL_kittygraphics.c` | tty raw mode, APC encode/transmit (SHM/zlib/direct), SGR mouse setup |
| `SDL_kittyevents.c` | keyboard + SGR mouse pump, resize recreate |

## Present path

`SDL_UpdateWindowSurface` → `UpdateWindowFramebuffer` → convert pixels → transmit (profile + probe):

1. Probe `a=q` / SHM / optional `a=f`; honor `prefer_direct` / `prefer_zlib` / `SDL_KITTY_TRANSMIT`
2. Prefer POSIX SHM (`t=s`) when the host accepts it; else zlib (`t=d,o=z`) or raw base64 (`t=d`)
3. Sparse dirty via `a=f` when `frame_edit_ok` and coverage under `af_max_*`; else full `a=T`
4. Hot-path APC uses `q=2` (quiet); optional `PERF_ACK` waits for `Gi=;OK` only when compiled in

Uses alternating image ids and deletes the previous id after a successful place.

## Input

- Kitty keyboard protocol (`CSI > 27 u` = flags 1|2|8|16, then probe `CSI ? u` → `kitty_keyboard_ok`); see `docs/keyboard.md`
- Associated text (flag 16) → `SDL_SendKeyboardText` when `SDL_StartTextInput` is active
- WezTerm: requires `enable_kitty_keyboard = true` or warn + OS-repeat fallback for arrows
- Rex: read keys from stdin (`tty_in`); write APC/CSI on stdout (`tty_out`)
- Focus re-announce + min-hold for state-polling games (e.g. MAME)
- FB tile-diff (`SDL_KITTY_FB_DIFF`, default on): when the app presents a near-full surface, 32×32 `memcmp` vs last frame invents a dirty bbox for `a=f` (skip upload if identical; full present if coverage ≥ `SDL_KITTY_FB_DIFF_COVER`, default 0.45)
- SGR mouse (`1000/1002/1006` / `1016` when available): motion, buttons, wheel → SDL mouse events
- Cell→pixel mapping via `TIOCGWINSZ` cell size / CSI 16 t
