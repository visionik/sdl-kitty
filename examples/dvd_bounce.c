/* dvd_bounce.c - classic bouncing DVD logo screensaver for SDL_VIDEO_DRIVER=kitty
 *
 * Press any key on the waiting screen to start. Q / Esc quits.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* 5x7 digits/letters for "DVD" — bit 0 is leftmost. */
static const unsigned char FONT_D[7] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
static const unsigned char FONT_V[7] = { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 };

static const Uint32 BOUNCE_COLORS[] = {
    0x00FF00FFu, /* magenta */
    0x0000FFFFu, /* cyan */
    0x00FFFF00u, /* yellow */
    0x0000FF00u, /* green */
    0x00FF6600u, /* orange */
    0x00FF0066u, /* pink */
    0x006666FFu, /* blue */
    0x00FFFFFFu, /* white */
};
static const int NCOLORS = (int)(sizeof(BOUNCE_COLORS) / sizeof(BOUNCE_COLORS[0]));

static void fill_ellipse(SDL_Surface *s, int cx, int cy, int rx, int ry, Uint32 color)
{
    int y;
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (y = -ry; y <= ry; ++y) {
        const int yy = cy + y;
        int xspan;
        int x;
        if (yy < 0 || yy >= s->h) {
            continue;
        }
        /* x^2/rx^2 + y^2/ry^2 <= 1 */
        {
            const double t = 1.0 - ((double)y * (double)y) / ((double)ry * (double)ry);
            if (t <= 0.0) {
                continue;
            }
            xspan = (int)(rx * SDL_sqrt(t) + 0.5);
        }
        {
            Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + yy * s->pitch);
            for (x = cx - xspan; x <= cx + xspan; ++x) {
                if (x >= 0 && x < s->w) {
                    row[x] = color;
                }
            }
        }
    }
}

static void blit_glyph(SDL_Surface *s, int ox, int oy, const unsigned char *rows, int scale, Uint32 color)
{
    int r, c, sy, sx;
    for (r = 0; r < 7; ++r) {
        for (c = 0; c < 5; ++c) {
            if (!(rows[r] & (1u << (4 - c)))) {
                continue;
            }
            for (sy = 0; sy < scale; ++sy) {
                const int y = oy + r * scale + sy;
                if (y < 0 || y >= s->h) {
                    continue;
                }
                Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
                for (sx = 0; sx < scale; ++sx) {
                    const int x = ox + c * scale + sx;
                    if (x >= 0 && x < s->w) {
                        row[x] = color;
                    }
                }
            }
        }
    }
}

static void draw_dvd_logo(SDL_Surface *s, int cx, int cy, int logo_w, int logo_h, Uint32 color)
{
    const int rx = logo_w / 2;
    const int ry = logo_h / 2;
    const int scale = SDL_max(2, logo_h / 18);
    const int glyph_w = 5 * scale;
    const int glyph_h = 7 * scale;
    const int gap = scale;
    const int text_w = glyph_w * 3 + gap * 2;
    const int text_x = cx - text_w / 2;
    const int text_y = cy - glyph_h / 2 - scale;

    fill_ellipse(s, cx, cy, rx, ry, color);

    /* Cut a darker inner oval so the letters sit in a "badge". */
    fill_ellipse(s, cx, cy, (rx * 5) / 6, (ry * 5) / 6, 0x00111111u);

    blit_glyph(s, text_x, text_y, FONT_D, scale, color);
    blit_glyph(s, text_x + glyph_w + gap, text_y, FONT_V, scale, color);
    blit_glyph(s, text_x + 2 * (glyph_w + gap), text_y, FONT_D, scale, color);

    /* Thin underline bar under the letters (classic DVD look). */
    {
        SDL_Rect bar = {
            text_x,
            text_y + glyph_h + scale / 2,
            text_w,
            SDL_max(2, scale / 2)
        };
        SDL_FillSurfaceRect(s, &bar, color);
    }
}

static SDL_Surface *get_surface(SDL_Window *w, int *ww, int *wh)
{
    SDL_Surface *surf = SDL_GetWindowSurface(w);
    if (!surf) {
        return NULL;
    }
    SDL_GetWindowSize(w, ww, wh);
    return surf;
}

int main(void)
{
    SDL_Window *window = NULL;
    SDL_Surface *surf = NULL;
    int ww = 0, wh = 0;
    int logo_w, logo_h;
    float x, y, vx, vy;
    int color_i = 0;
    int phase = 0; /* 0 = wait for key, 1 = bounce */
    int run = 1;
    Uint64 prev_ms;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    setenv("SDL_KITTY_TRANSMIT", "direct", 1);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("dvd-bounce", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    surf = get_surface(window, &ww, &wh);
    if (!surf) {
        fprintf(stderr, "surface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    write(STDOUT_FILENO, "ASCII: press any key to start DVD bounce (q quits)\r\n", 54);

    /* Waiting screen */
    SDL_FillSurfaceRect(surf, NULL, 0x00101010u);
    {
        SDL_Rect bar = { ww / 4, wh / 2 - 8, ww / 2, 16 };
        SDL_FillSurfaceRect(surf, &bar, 0x00444444u);
    }
    SDL_UpdateWindowSurface(window);

    logo_w = SDL_max(80, ww / 5);
    logo_h = SDL_max(40, logo_w / 2);
    x = (float)(ww / 2 - logo_w / 2);
    y = (float)(wh / 2 - logo_h / 2);
    vx = 120.0f;
    vy = 90.0f;
    prev_ms = SDL_GetTicks();

    while (run) {
        SDL_Event e;
        Uint64 now;
        float dt;
        int bounced = 0;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                run = 0;
            } else if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                       e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                surf = get_surface(window, &ww, &wh);
                if (surf) {
                    logo_w = SDL_max(80, ww / 5);
                    logo_h = SDL_max(40, logo_w / 2);
                    if (x + logo_w > ww) {
                        x = (float)(ww - logo_w);
                    }
                    if (y + logo_h > wh) {
                        y = (float)(wh - logo_h);
                    }
                    if (x < 0) {
                        x = 0;
                    }
                    if (y < 0) {
                        y = 0;
                    }
                }
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_Q || e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    run = 0;
                } else if (phase == 0) {
                    phase = 1;
                    write(STDOUT_FILENO, "ASCII: bouncing — q to quit\r\n", 30);
                    prev_ms = SDL_GetTicks();
                }
            }
        }

        surf = get_surface(window, &ww, &wh);
        if (!surf) {
            SDL_Delay(16);
            continue;
        }

        now = SDL_GetTicks();
        dt = (float)(now - prev_ms) / 1000.0f;
        if (dt > 0.05f) {
            dt = 0.05f;
        }
        prev_ms = now;

        if (phase == 1) {
            x += vx * dt;
            y += vy * dt;

            if (x <= 0.0f) {
                x = 0.0f;
                vx = SDL_fabsf(vx);
                bounced = 1;
            } else if (x + logo_w >= ww) {
                x = (float)(ww - logo_w);
                vx = -SDL_fabsf(vx);
                bounced = 1;
            }
            if (y <= 0.0f) {
                y = 0.0f;
                vy = SDL_fabsf(vy);
                bounced = 1;
            } else if (y + logo_h >= wh) {
                y = (float)(wh - logo_h);
                vy = -SDL_fabsf(vy);
                bounced = 1;
            }
            if (bounced) {
                color_i = (color_i + 1) % NCOLORS;
            }
        }

        SDL_FillSurfaceRect(surf, NULL, 0x00000000u);
        if (phase == 0) {
            SDL_Rect bar = { ww / 4, wh / 2 - 8, ww / 2, 16 };
            SDL_FillSurfaceRect(surf, NULL, 0x00101010u);
            SDL_FillSurfaceRect(surf, &bar, 0x00444444u);
        } else {
            draw_dvd_logo(surf,
                          (int)(x + logo_w * 0.5f),
                          (int)(y + logo_h * 0.5f),
                          logo_w, logo_h,
                          BOUNCE_COLORS[color_i]);
        }

        if (!SDL_UpdateWindowSurface(window)) {
            /* keep going; resize may invalidate briefly */
        }
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
