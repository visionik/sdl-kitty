/* alpha_blit.c — SDL_KITTY_FB_ALPHA=1 smoke: translucent discs over a grid.
 *
 * Uses normal GetWindowSurface / UpdateWindowSurfaceRects only (ARGB FB + Kitty
 * f=32). Dirty rects + DIRTY_BW keep static checker cells from full-frame flicker.
 * q / Esc quits.
 */
#include <SDL3/SDL.h>
#include <stdio.h>

typedef struct {
    int cx, cy, radius;
} Disc;

static void fill_grid_rect(SDL_Surface *s, const SDL_Rect *clip)
{
    const int cell = 32;
    int x0, y0, x1, y1, x, y;
    SDL_Rect area, fb;

    fb.x = 0;
    fb.y = 0;
    fb.w = s->w;
    fb.h = s->h;
    if (!SDL_GetRectIntersection(clip, &fb, &area) || area.w <= 0 || area.h <= 0) {
        return;
    }
    x0 = (area.x / cell) * cell;
    y0 = (area.y / cell) * cell;
    x1 = area.x + area.w;
    y1 = area.y + area.h;
    for (y = y0; y < y1; y += cell) {
        for (x = x0; x < x1; x += cell) {
            SDL_Rect r = { x, y, cell, cell };
            SDL_Rect paint;
            const int parity = ((x / cell) + (y / cell)) & 1;
            const Uint32 c = parity
                ? SDL_MapSurfaceRGBA(s, 40, 44, 60, 255)
                : SDL_MapSurfaceRGBA(s, 24, 28, 40, 255);
            if (!SDL_GetRectIntersection(&r, &area, &paint)) {
                continue;
            }
            SDL_FillSurfaceRect(s, &paint, c);
        }
    }
}

static void fill_grid(SDL_Surface *s)
{
    SDL_Rect full = { 0, 0, s->w, s->h };
    fill_grid_rect(s, &full);
}

static void disc_bounds(const Disc *d, SDL_Rect *out, int fb_w, int fb_h)
{
    SDL_Rect r = { d->cx - d->radius, d->cy - d->radius, d->radius * 2 + 1, d->radius * 2 + 1 };
    SDL_Rect fb = { 0, 0, fb_w, fb_h };
    if (!SDL_GetRectIntersection(&r, &fb, out)) {
        out->x = out->y = out->w = out->h = 0;
    }
}

static void fill_disc(SDL_Surface *s, const Disc *d, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    int y;
    for (y = -d->radius; y <= d->radius; ++y) {
        const int yy = d->cy + y;
        int xspan, x;
        double t;
        if (yy < 0 || yy >= s->h) {
            continue;
        }
        t = 1.0 - ((double)y * (double)y) / ((double)d->radius * (double)d->radius);
        if (t <= 0.0) {
            continue;
        }
        xspan = (int)(d->radius * SDL_sqrt(t) + 0.5);
        for (x = d->cx - xspan; x <= d->cx + xspan; ++x) {
            Uint32 *pix;
            Uint32 dst;
            Uint8 da, dr, dg, db;
            int oa, ia;
            Uint8 out_a, out_r, out_g, out_b;
            if (x < 0 || x >= s->w) {
                continue;
            }
            pix = (Uint32 *)((Uint8 *)s->pixels + (size_t)yy * (size_t)s->pitch) + x;
            dst = *pix;
            da = (Uint8)((dst >> 24) & 0xFF);
            dr = (Uint8)((dst >> 16) & 0xFF);
            dg = (Uint8)((dst >> 8) & 0xFF);
            db = (Uint8)(dst & 0xFF);
            oa = (int)a;
            ia = 255 - oa;
            out_a = (Uint8)SDL_min(255, oa + (da * ia) / 255);
            out_r = (Uint8)((r * oa + dr * ia) / 255);
            out_g = (Uint8)((g * oa + dg * ia) / 255);
            out_b = (Uint8)((b * oa + db * ia) / 255);
            *pix = ((Uint32)out_a << 24) | ((Uint32)out_r << 16) | ((Uint32)out_g << 8) | (Uint32)out_b;
        }
    }
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    float t = 0.0f;
    Disc prev[3];
    int have_prev = 0;
    const Uint8 colors[3][4] = {
        { 255, 64, 160, 140 },
        { 64, 200, 255, 120 },
        { 255, 220, 64, 160 },
    };

    (void)argc;
    (void)argv;

    /* Log before SDL_Init: kitty owns the tty/alt-screen after init. */
    {
        FILE *log = fopen("/tmp/alpha_blit.log", "w");
        if (log) {
            fprintf(log, "alpha_blit: FB_ALPHA=1 + dirty rects; translucent discs; q quits\n");
            fclose(log);
        }
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    SDL_setenv_unsafe("SDL_KITTY_FB_ALPHA", "1", 1);
    /* Do not force TRANSMIT or DIRTY_BW — host profile + probe choose
     * SHM/direct and whether a=f is safe (Kitty frame_edit probe often fails). */

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("sdl-kitty alpha", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_zeroa(prev);

    for (;;) {
        SDL_Event e;
        SDL_Surface *surface;
        Disc cur[3];
        SDL_Rect dirty[6];
        int ndirty = 0;
        int i, w, h, base_r;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                goto done;
            }
            if (e.type == SDL_EVENT_KEY_DOWN &&
                (e.key.key == SDLK_Q || e.key.key == SDLK_ESCAPE)) {
                goto done;
            }
            if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                have_prev = 0;
            }
        }

        surface = SDL_GetWindowSurface(window);
        if (!surface) {
            fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
            break;
        }
        if (!SDL_ISPIXELFORMAT_ALPHA(surface->format)) {
            fprintf(stderr, "expected ARGB FB (set SDL_KITTY_FB_ALPHA=1); format=%u\n",
                    (unsigned)surface->format);
            break;
        }

        w = surface->w;
        h = surface->h;
        base_r = SDL_min(w, h);
        cur[0].cx = w / 2 + (int)(SDL_sin(t) * (w / 4));
        cur[0].cy = h / 2 + (int)(SDL_cos(t * 0.9f) * (h / 5));
        cur[0].radius = base_r / 6;
        cur[1].cx = w / 2 + (int)(SDL_cos(t * 1.1f) * (w / 5));
        cur[1].cy = h / 2 + (int)(SDL_sin(t * 0.8f) * (h / 4));
        cur[1].radius = base_r / 7;
        cur[2].cx = w / 3 + (int)(SDL_sin(t * 1.3f) * (w / 6));
        cur[2].cy = (2 * h) / 3 + (int)(SDL_cos(t * 1.2f) * (h / 6));
        cur[2].radius = base_r / 8;

        if (!have_prev) {
            fill_grid(surface);
            for (i = 0; i < 3; ++i) {
                fill_disc(surface, &cur[i], colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
            }
            if (!SDL_UpdateWindowSurface(window)) {
                fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
                break;
            }
            have_prev = 1;
        } else {
            /* Restore old footprints, paint new discs, present dirty union only. */
            for (i = 0; i < 3; ++i) {
                disc_bounds(&prev[i], &dirty[ndirty], w, h);
                if (dirty[ndirty].w > 0 && dirty[ndirty].h > 0) {
                    fill_grid_rect(surface, &dirty[ndirty]);
                    ndirty++;
                }
            }
            for (i = 0; i < 3; ++i) {
                disc_bounds(&cur[i], &dirty[ndirty], w, h);
                if (dirty[ndirty].w > 0 && dirty[ndirty].h > 0) {
                    fill_grid_rect(surface, &dirty[ndirty]);
                    ndirty++;
                }
            }
            for (i = 0; i < 3; ++i) {
                fill_disc(surface, &cur[i], colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
            }
            if (ndirty > 0) {
                if (!SDL_UpdateWindowSurfaceRects(window, dirty, ndirty)) {
                    fprintf(stderr, "SDL_UpdateWindowSurfaceRects failed: %s\n", SDL_GetError());
                    break;
                }
            }
        }

        for (i = 0; i < 3; ++i) {
            prev[i] = cur[i];
        }
        t += 0.04f;
        SDL_Delay(16);
    }

done:
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
