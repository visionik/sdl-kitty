# sdl-kitty design notes

See the session plan for the full roadmap. Short form:

- **API:** SDL3 public video/renderer API
- **Backend:** Kitty graphics protocol APC over the tty
- **MVP:** window framebuffer → RGB24 chunked base64 → `a=T`
- **Selection:** only when `SDL_VIDEO_DRIVER=kitty` (never auto)
- **Ship:** out-of-tree SDL branch → DynAPI lib → upstream PR

## Driver files (in `third_party/SDL/src/video/kitty/`)

| File | Role |
|---|---|
| `SDL_kittyvideo.c` | bootstrap, VideoInit/Quit, display from `TIOCGWINSZ` |
| `SDL_kittywindow.c` | Create/Destroy/SetSize |
| `SDL_kittyframebuffer.c` | surface + XRGB→RGB present |
| `SDL_kittygraphics.c` | tty raw mode, APC encode/transmit |
| `SDL_kittyevents.c` | minimal keyboard pump |

## Present path

`SDL_UpdateWindowSurface` → `UpdateWindowFramebuffer` → convert pixels → `KITTY_TransmitRGB` (`a=T,f=24,t=d`, 4K base64 chunks, `q=2`).
