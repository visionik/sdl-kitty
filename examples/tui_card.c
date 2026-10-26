/* tui_card.c - Phase 1 TUI card spike (docs/tui-card.md)
 *
 * Draws text chrome on the primary screen, then presents an SDL kitty
 * framebuffer into SDL_KITTY_VIEWPORT (CUP + place c×r). Mouse paints
 * inside the card; pointer outside is ignored by the driver.
 *
 * Env (set here if unset):
 *   SDL_KITTY_ALTSCREEN=0
 *   SDL_KITTY_VIEWPORT=col,row,w,h   (1-based cells)
 *   SDL_KITTY_FB_FIXED=WxH
 *   SDL_KITTY_PLACE_CELLS=1
 *
 * q / Esc quits.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static Uint32 g_color = 0x00FF6644u;
static int g_radius = 6;

static void paint_disk(SDL_Surface *surface, int cx, int cy, Uint32 color, int radius)
{
    const int r2 = radius * radius;
    int y;
    for (y = cy - radius; y <= cy + radius; ++y) {
        int x;
        if (y < 0 || y >= surface->h) {
            continue;
        }
        Uint32 *row = (Uint32 *)((Uint8 *)surface->pixels + y * surface->pitch);
        for (x = cx - radius; x <= cx + radius; ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            if (x < 0 || x >= surface->w) {
                continue;
            }
            if (dx * dx + dy * dy <= r2) {
                row[x] = color;
            }
        }
    }
}

static void draw_chrome(int cols, int rows, int vp_col, int vp_row, int vp_w, int vp_h)
{
    int r, c;
    const int x0 = vp_col - 1; /* 0-based border around viewport */
    const int y0 = vp_row - 1;
    const int x1 = vp_col + vp_w; /* cell after right edge */
    const int y1 = vp_row + vp_h;

    /* Clear primary screen once; driver will not smcup/clear in card mode. */
    fputs("\033[2J\033[H\033[?25l", stdout);

    /* Title row */
    fprintf(stdout, "\033[1;1H\033[1m sdl-kitty TUI card \033[0m  viewport=%d,%d,%d,%d  q/Esc quit",
            vp_col, vp_row, vp_w, vp_h);

    /* Simple ASCII border one cell outside the viewport when space allows. */
    if (x0 >= 1 && y0 >= 2 && x1 <= cols && y1 < rows) {
        fprintf(stdout, "\033[%d;%dH+", y0, x0);
        for (c = 0; c < vp_w; ++c) {
            fputc('-', stdout);
        }
        fputc('+', stdout);
        for (r = 0; r < vp_h; ++r) {
            fprintf(stdout, "\033[%d;%dH|", vp_row + r, x0);
            fprintf(stdout, "\033[%d;%dH|", vp_row + r, x1);
        }
        fprintf(stdout, "\033[%d;%dH+", y1, x0);
        for (c = 0; c < vp_w; ++c) {
            fputc('-', stdout);
        }
        fputc('+', stdout);
    }

    fprintf(stdout, "\033[%d;1H status: drag to paint · wheel recolors · card only receives mouse",
            rows > 0 ? rows : (y1 + 1));
    fflush(stdout);
}

static int term_size(int *cols, int *rows)
{
    struct winsize ws;
    SDL_zero(ws);
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col < 8 || ws.ws_row < 6) {
        *cols = 80;
        *rows = 24;
        return 0;
    }
    *cols = (int)ws.ws_col;
    *rows = (int)ws.ws_row;
    return 1;
}

static void ensure_card_env(int cols, int rows, int *vp_col, int *vp_row, int *vp_w, int *vp_h)
{
    const char *vp = getenv("SDL_KITTY_VIEWPORT");
    int col = 3, row = 3, w = 0, h = 0;

    if (vp && *vp && sscanf(vp, "%d,%d,%d,%d", &col, &row, &w, &h) == 4 &&
        col > 0 && row > 0 && w > 0 && h > 0) {
        *vp_col = col;
        *vp_row = row;
        *vp_w = w;
        *vp_h = h;
    } else {
        /* Inset card: leave room for title, border, status. */
        *vp_col = 3;
        *vp_row = 3;
        *vp_w = cols - 4;
        *vp_h = rows - 5;
        if (*vp_w < 10) {
            *vp_w = 10;
        }
        if (*vp_h < 5) {
            *vp_h = 5;
        }
        if (*vp_col + *vp_w - 1 > cols) {
            *vp_w = cols - *vp_col + 1;
        }
        if (*vp_row + *vp_h - 1 > rows) {
            *vp_h = rows - *vp_row + 1;
        }
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d,%d,%d,%d", *vp_col, *vp_row, *vp_w, *vp_h);
            setenv("SDL_KITTY_VIEWPORT", buf, 1);
        }
    }

    if (!getenv("SDL_KITTY_ALTSCREEN")) {
        setenv("SDL_KITTY_ALTSCREEN", "0", 1);
    }
    if (!getenv("SDL_KITTY_PLACE_CELLS")) {
        setenv("SDL_KITTY_PLACE_CELLS", "1", 1);
    }
    if (!getenv("SDL_KITTY_FB_FIXED")) {
        /* Modest FB; place_cells stretches into the viewport. */
        char buf[32];
        const int fw = (*vp_w) * 8;
        const int fh = (*vp_h) * 16;
        snprintf(buf, sizeof(buf), "%dx%d", fw > 64 ? fw : 64, fh > 64 ? fh : 64);
        setenv("SDL_KITTY_FB_FIXED", buf, 1);
    }
    (void)rows;
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *surface = NULL;
    SDL_Surface *canvas = NULL;
    int running = 1;
    int painting = 0;
    int dirty = 1;
    int mx = -1, my = -1;
    int ww = 0, wh = 0;
    int cols = 80, rows = 24;
    int vp_col = 3, vp_row = 3, vp_w = 40, vp_h = 12;
    int frame = 0;

    (void)argc;
    (void)argv;

    term_size(&cols, &rows);
    ensure_card_env(cols, rows, &vp_col, &vp_row, &vp_w, &vp_h);
    draw_chrome(cols, rows, vp_col, vp_row, vp_w, vp_h);

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    fprintf(stderr, "tui_card: driver=%s viewport=%d,%d,%d,%d fb_fixed=%s\n",
            SDL_GetCurrentVideoDriver(),
            vp_col, vp_row, vp_w, vp_h,
            getenv("SDL_KITTY_FB_FIXED") ? getenv("SDL_KITTY_FB_FIXED") : "?");

    window = SDL_CreateWindow("sdl-kitty tui card", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GetWindowSize(window, &ww, &wh);
    fprintf(stderr, "tui_card: window %dx%d\n", ww, wh);

    surface = SDL_GetWindowSurface(window);
    if (!surface) {
        fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    canvas = SDL_CreateSurface(surface->w, surface->h, surface->format);
    if (!canvas) {
        fprintf(stderr, "SDL_CreateSurface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_FillSurfaceRect(canvas, NULL, 0x00221833u); /* dark purple card */

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = 0;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.scancode == SDL_SCANCODE_Q || e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = 0;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mx = (int)e.button.x;
                my = (int)e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) {
                    painting = 1;
                    paint_disk(canvas, mx, my, g_color, g_radius);
                    dirty = 1;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    SDL_FillSurfaceRect(canvas, NULL, 0x00221833u);
                    dirty = 1;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    painting = 0;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mx = (int)e.motion.x;
                my = (int)e.motion.y;
                if (painting) {
                    paint_disk(canvas, mx, my, g_color, g_radius);
                    dirty = 1;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                g_color = (g_color + 0x002a1f07u) | 0x000000ffu;
                dirty = 1;
                break;
            default:
                break;
            }
        }

        /* Soft pulse so a card with no mouse still shows life. */
        if ((frame++ % 15) == 0) {
            const Uint8 pulse = (Uint8)(40 + ((frame / 15) % 5) * 12);
            SDL_Rect bar = { 8, 8, canvas->w / 4, 10 };
            SDL_FillSurfaceRect(canvas, &bar, SDL_MapSurfaceRGB(canvas, pulse, 80, 160));
            dirty = 1;
        }

        if (dirty) {
            surface = SDL_GetWindowSurface(window);
            if (!surface) {
                break;
            }
            SDL_BlitSurface(canvas, NULL, surface, NULL);
            if (!SDL_UpdateWindowSurface(window)) {
                fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
                break;
            }
            dirty = 0;
        }
        SDL_Delay(16);
    }

    SDL_DestroySurface(canvas);
    SDL_DestroyWindow(window);
    SDL_Quit();
    /* KITTY_RestoreTerminal already shows the cursor and parks it on the
     * row below the card chrome. */
    return 0;
}
