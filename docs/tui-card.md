# TUI card mode (design)

Run an SDL kitty framebuffer as a **fixed cell rectangle** inside a text UI,
instead of owning the whole pane.

Related: [framebuffer-placement-and-mouse.md](framebuffer-placement-and-mouse.md),
[design.md](design.md).

## Goal

```text
┌─ host TUI chrome ─────────────────────────────┐
│ menu / status                                  │
│ ┌───────────────────────────────────────────┐ │
│ │         SDL card (Kitty image)            │ │
│ │         CUP + place c×r only here         │ │
│ └───────────────────────────────────────────┘ │
│ status line                                    │
└───────────────────────────────────────────────┘
```

## Today vs card

| | Full-pane (today) | Card |
|---|---|---|
| Alt screen / clear | Yes (`smcup`, home/clear) | Optional / off by default |
| Place | `c=cols,r=rows` from home | `CUP(row,col)` then `c=w,r=h` |
| Mouse | Whole pane → FB | Clip to viewport; map into FB |
| Keyboard | Driver owns kitty keyboard | Focus policy (see below) |
| Coexistence | One app owns tty | TUI + SDL share tty |

## Phase 1 — viewport env (spike)

Same process; existing `SDL_VIDEO_DRIVER=kitty` apps.

```bash
SDL_KITTY_ALTSCREEN=0
SDL_KITTY_VIEWPORT=col,row,w,h   # cell origin + size (document 1-based vs 0-based in impl)
SDL_KITTY_FB_FIXED=WxH           # optional card pixel size
SDL_KITTY_PLACE_CELLS=1          # stretch FB into that cell box
```

Driver duties:

1. Skip alt-screen / full clear when `ALTSCREEN=0`
2. Before `a=T` / `a=p`: move cursor to viewport origin; place with viewport `c`/`r` only
3. On layout/WINCH: if FB size unchanged, `a=p` with new `c`/`r` (existing stretch re-place path)
4. Mouse: accept events only inside the viewport; scale like stretch into `pixel_w×pixel_h`
5. Do not erase cells outside the viewport

In-tree demo: thin text chrome (title + border + quit) + `solid_present` / `mouse_paint` in the card.

## Phase 2 — embed helper

Split “own the terminal” from “present pixels into a rect”:

- **Helper:** upload + place into `(col,row,w,h)` on a given fd (no VideoInit ownership)
- **Host TUI:** layout, text redraw, input routing; calls helper each frame / on dirty
- **Video driver:** full-pane mode *or* thin wrapper around the helper

Use when a real TUI (e.g. ratatui) owns the session and SDL is a widget.

## Input policy (pick one for Phase 1)

| Policy | Behavior |
|---|---|
| **Click-focus** (preferred) | Pointer in card → SDL gets mouse + keys; outside → host TUI |
| Mouse-only split | Hit-test mouse; keys always to one owner |
| SDL-exclusive | Card demo only; no competing TUI keyboard |

Kitty keyboard and SGR mouse are tty-global; focus is a software filter in the pump.

## Redraw coexistence

- Host must **not** clear/overwrite the card cell rect (or must signal SDL to re-place after)
- Prefer primary screen + surgical TUI redraw over full `ED`
- After host paint that touches the card, refresh with `a=p` (cheap) or full `a=T`

## Non-goals (for now)

- Multiplex split panes (already works via Rex/tmux without this)
- True compositor z-order for overlapping text and images
- GPU / OpenGL card path
- Multi-card / multi-window without a host

## Implementation touch list (Phase 1)

| Area | Change |
|---|---|
| `SDL_kittygraphics.c` | Parse viewport; conditional alt-screen; CUP + place size |
| `SDL_kittyevents.c` | Mouse clip + origin offset before existing scale |
| Profile / env | `VIEWPORT`, `ALTSCREEN` (document in `terminal-profiles.md` if promoted) |
| Example | `examples/tui_card/` or chrome wrapper around an existing demo |
| Docs | This file; short pointer from `design.md` |

## Success criteria

1. Text chrome visible; SDL image only in the reserved cell rect  
2. Mouse paints / hits correctly inside the card; ignored outside  
3. Resize updates place without blanking the whole screen  
4. stderr still redirected; stdout stays the graphics tty  

## Spike status (Phase 1)

Landed in-tree (driver + `examples/tui_card`):

- `SDL_KITTY_VIEWPORT=col,row,w,h` (1-based cells); implies `ALTSCREEN` off unless forced on
- `SDL_KITTY_ALTSCREEN=0` skips smcup/clear/rmcup; setup only hides cursor + kitty keyboard
- Place path: `KITTY_WritePlaceOrigin` (CUP) + `KITTY_GetPlaceCells` for `c×r`
- Mouse: clip to viewport; map into FB (1006 cells and 1016 pixels)
- Dirty overlay place grid uses viewport origin/size when active
- Demo: `build/tui_card` (chrome border + paint card). Run via
  `SDL_KITTY_ALTSCREEN=0 ./scripts/run-with-kitty-sdl.sh ./build/tui_card`
  with stderr redirected away from the graphics tty

Rex smoke (2026-10-26): chrome intact around viewport `3,3,76,19`; present `#0`
`608x304 place=1 mode=full -> ok`; pulse `dirty_bw` updates without wiping chrome;
`q` quits cleanly. Interactive drag-paint in the card is the remaining manual check.

ImGui card: `./scripts/run-imgui-card.sh` draws the same chrome pattern and runs
`imgui_sdlrenderer3` with `VIEWPORT` + `FB_FIXED` + software renderer (stderr →
`/tmp/imgui-kitty-card.err`). Rex smoke: present `#0/#1` ok into `608x304` card.  
