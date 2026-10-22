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

`SDL_UpdateWindowSurface` → `UpdateWindowFramebuffer` → convert pixels → `KITTY_TransmitRGB`:

1. Try POSIX SHM (`t=s`) unless `SDL_KITTY_TRANSMIT` forces otherwise
2. Fall back to zlib-compressed direct (`t=d,o=z`)
3. Fall back to raw chunked base64 (`t=d`)

Uses alternating image ids and deletes the previous id after a successful place (`q=2`).

## Input

- Kitty keyboard protocol (`CSI > 1 u`) plus ASCII fallback; q/Esc synthesize quit
- SGR mouse (`1000/1002/1006`): motion, buttons, wheel → SDL mouse events
- Cell→pixel mapping via `TIOCGWINSZ` cell size
