/* fireworks.c — particle fireworks over a night-lake backdrop (kitty FB).
 *
 * Normal GetWindowSurface / UpdateWindowSurfaceRects only. Auto-launches
 * rockets; click or space to force a burst. q / Esc quits.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROCKETS 8
#define MAX_PARTICLES 1200
#define PARTS_PER_BURST 90

typedef struct {
    float x, y, vx, vy;
    int life;
    int alive;
    Uint8 r, g, b;
} Rocket;

typedef struct {
    float x, y, vx, vy;
    int life;
    int max_life;
    int alive;
    Uint8 r, g, b;
} Particle;

static const struct { Uint8 r, g, b; } PALETTE[] = {
    { 255, 80, 80 },
    { 255, 180, 60 },
    { 255, 255, 100 },
    { 80, 255, 120 },
    { 80, 200, 255 },
    { 180, 100, 255 },
    { 255, 100, 200 },
    { 255, 255, 255 },
};
#define NPALETTE (int)(sizeof(PALETTE) / sizeof(PALETTE[0]))

static void put_pixel(SDL_Surface *s, int x, int y, Uint32 color)
{
    Uint32 *row;
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) {
        return;
    }
    row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch);
    row[x] = color;
}

static void put_dot(SDL_Surface *s, int x, int y, int rad, Uint32 color)
{
    int dy, dx, r2 = rad * rad;
    for (dy = -rad; dy <= rad; ++dy) {
        for (dx = -rad; dx <= rad; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                put_pixel(s, x + dx, y + dy, color);
            }
        }
    }
}

static Uint32 map_rgb(SDL_Surface *s, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapSurfaceRGB(s, r, g, b);
}

static SDL_Surface *load_lake_png(void)
{
    static const char *candidates[] = {
        "examples/assets/lake_night.png",
        "../examples/assets/lake_night.png",
        "assets/lake_night.png",
        NULL
    };
    char *base = (char *)SDL_GetBasePath();
    SDL_Surface *img = NULL;
    int i;

    if (base) {
        char path[1024];
        SDL_snprintf(path, sizeof(path), "%sassets/lake_night.png", base);
        img = SDL_LoadPNG(path);
        if (!img) {
            SDL_snprintf(path, sizeof(path), "%sexamples/assets/lake_night.png", base);
            img = SDL_LoadPNG(path);
        }
        SDL_free(base);
    }
    for (i = 0; !img && candidates[i]; ++i) {
        img = SDL_LoadPNG(candidates[i]);
    }
    return img;
}

static SDL_Surface *scale_background(SDL_Surface *src, int w, int h)
{
    SDL_Surface *dst;
    SDL_Rect dr;
    if (!src || w <= 0 || h <= 0) {
        return NULL;
    }
    dst = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    if (!dst) {
        return NULL;
    }
    dr.x = 0;
    dr.y = 0;
    dr.w = w;
    dr.h = h;
    if (!SDL_BlitSurfaceScaled(src, NULL, dst, &dr, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroySurface(dst);
        return NULL;
    }
    return dst;
}

static void restore_bg(SDL_Surface *dst, SDL_Surface *bg, const SDL_Rect *clip)
{
    SDL_Rect fb, area, src;
    if (!dst || !bg) {
        return;
    }
    fb.x = 0;
    fb.y = 0;
    fb.w = dst->w;
    fb.h = dst->h;
    if (!clip) {
        SDL_BlitSurface(bg, NULL, dst, NULL);
        return;
    }
    if (!SDL_GetRectIntersection(clip, &fb, &area) || area.w <= 0) {
        return;
    }
    src = area;
    SDL_BlitSurface(bg, &src, dst, &area);
}

static void expand_dirty(SDL_Rect *d, int x, int y, int rad, int fb_w, int fb_h)
{
    SDL_Rect r = { x - rad - 1, y - rad - 1, rad * 2 + 3, rad * 2 + 3 };
    SDL_Rect fb = { 0, 0, fb_w, fb_h };
    SDL_Rect clipped;
    if (!SDL_GetRectIntersection(&r, &fb, &clipped) || clipped.w <= 0) {
        return;
    }
    if (d->w <= 0) {
        *d = clipped;
        return;
    }
    {
        const int x2 = SDL_max(d->x + d->w, clipped.x + clipped.w);
        const int y2 = SDL_max(d->y + d->h, clipped.y + clipped.h);
        d->x = SDL_min(d->x, clipped.x);
        d->y = SDL_min(d->y, clipped.y);
        d->w = x2 - d->x;
        d->h = y2 - d->y;
    }
}

static int spawn_rocket(Rocket *rockets, int w, int h)
{
    int i;
    const int pi = SDL_rand(NPALETTE);
    for (i = 0; i < MAX_ROCKETS; ++i) {
        if (rockets[i].alive) {
            continue;
        }
        rockets[i].alive = 1;
        rockets[i].x = (float)(w / 8 + SDL_rand((w * 3) / 4));
        rockets[i].y = (float)(h - 4);
        rockets[i].vx = ((float)SDL_rand(100) - 50.0f) * 0.03f;
        /* Stronger ascent so bursts land higher in the sky. */
        rockets[i].vy = -8.5f - (float)SDL_rand(50) * 0.1f;
        rockets[i].life = 45 + SDL_rand(35);
        rockets[i].r = PALETTE[pi].r;
        rockets[i].g = PALETTE[pi].g;
        rockets[i].b = PALETTE[pi].b;
        return 1;
    }
    return 0;
}

static void explode(Particle *parts, float x, float y, Uint8 r, Uint8 g, Uint8 b)
{
    int n = 0;
    int i;
    for (i = 0; i < MAX_PARTICLES && n < PARTS_PER_BURST; ++i) {
        float ang, spd;
        if (parts[i].alive) {
            continue;
        }
        ang = (float)SDL_rand(628) * 0.01f;
        spd = 1.2f + (float)SDL_rand(280) * 0.01f;
        parts[i].alive = 1;
        parts[i].x = x;
        parts[i].y = y;
        parts[i].vx = SDL_cosf(ang) * spd;
        parts[i].vy = SDL_sinf(ang) * spd;
        parts[i].max_life = 40 + SDL_rand(50);
        parts[i].life = parts[i].max_life;
        parts[i].r = (Uint8)SDL_min(255, (int)r + SDL_rand(40) - 20);
        parts[i].g = (Uint8)SDL_min(255, (int)g + SDL_rand(40) - 20);
        parts[i].b = (Uint8)SDL_min(255, (int)b + SDL_rand(40) - 20);
        n++;
    }
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *lake_src = NULL;
    SDL_Surface *lake_scaled = NULL;
    Rocket rockets[MAX_ROCKETS];
    Particle parts[MAX_PARTICLES];
    int force_burst = 0;
    int need_full = 1;
    int bg_w = 0, bg_h = 0;
    Uint64 next_auto_ms = 0;

    (void)argc;
    (void)argv;

    {
        FILE *log = fopen("/tmp/fireworks.log", "w");
        if (log) {
            fprintf(log, "fireworks: lake backdrop + rockets; click/space burst; q quits\n");
            fclose(log);
        }
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    SDL_setenv_unsafe("SDL_KITTY_TRANSMIT", "direct", 0);
    SDL_setenv_unsafe("SDL_KITTY_DIRTY_BW", "1", 0);
    SDL_srand(0);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    lake_src = load_lake_png();
    if (!lake_src) {
        fprintf(stderr, "failed to load lake_night.png: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow("sdl-kitty fireworks", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_DestroySurface(lake_src);
        SDL_Quit();
        return 1;
    }

    SDL_zeroa(rockets);
    SDL_zeroa(parts);
    next_auto_ms = SDL_GetTicks() + 400;

    for (;;) {
        SDL_Event e;
        SDL_Surface *surface;
        SDL_Rect dirty;
        int i, w, h;
        Uint64 now;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                goto done;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_Q || e.key.key == SDLK_ESCAPE) {
                    goto done;
                }
                if (e.key.key == SDLK_SPACE) {
                    force_burst = 1;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                e.button.button == SDL_BUTTON_LEFT) {
                force_burst = 1;
            }
            if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                need_full = 1;
            }
        }

        surface = SDL_GetWindowSurface(window);
        if (!surface) {
            fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
            break;
        }
        w = surface->w;
        h = surface->h;
        dirty.x = dirty.y = dirty.w = dirty.h = 0;
        now = SDL_GetTicks();

        if (!lake_scaled || bg_w != w || bg_h != h) {
            SDL_DestroySurface(lake_scaled);
            lake_scaled = scale_background(lake_src, w, h);
            bg_w = w;
            bg_h = h;
            need_full = 1;
            if (!lake_scaled) {
                fprintf(stderr, "scale lake failed: %s\n", SDL_GetError());
                break;
            }
        }

        for (i = 0; i < MAX_ROCKETS; ++i) {
            if (rockets[i].alive) {
                expand_dirty(&dirty, (int)rockets[i].x, (int)rockets[i].y, 3, w, h);
            }
        }
        for (i = 0; i < MAX_PARTICLES; ++i) {
            if (parts[i].alive) {
                expand_dirty(&dirty, (int)parts[i].x, (int)parts[i].y, 2, w, h);
            }
        }

        if (need_full) {
            restore_bg(surface, lake_scaled, NULL);
        } else if (dirty.w > 0) {
            restore_bg(surface, lake_scaled, &dirty);
        }

        if (force_burst || now >= next_auto_ms) {
            spawn_rocket(rockets, w, h);
            force_burst = 0;
            next_auto_ms = now + (Uint64)(500 + SDL_rand(900));
        }

        for (i = 0; i < MAX_ROCKETS; ++i) {
            Rocket *rk = &rockets[i];
            if (!rk->alive) {
                continue;
            }
            rk->x += rk->vx;
            rk->y += rk->vy;
            rk->vy += 0.055f;
            rk->life--;
            expand_dirty(&dirty, (int)rk->x, (int)rk->y, 3, w, h);
            /* Burst in the upper sky (~8% from top) or when the rocket stalls. */
            if (rk->life <= 0 || rk->vy >= -0.35f || rk->y < h * 0.08f) {
                explode(parts, rk->x, rk->y, rk->r, rk->g, rk->b);
                expand_dirty(&dirty, (int)rk->x, (int)rk->y, 4, w, h);
                rk->alive = 0;
            } else {
                put_dot(surface, (int)rk->x, (int)rk->y, 2,
                        map_rgb(surface, rk->r, rk->g, rk->b));
            }
        }

        for (i = 0; i < MAX_PARTICLES; ++i) {
            Particle *p = &parts[i];
            int rad;
            float fade;
            Uint8 r, g, b;
            if (!p->alive) {
                continue;
            }
            p->x += p->vx;
            p->y += p->vy;
            p->vy += 0.06f;
            p->vx *= 0.99f;
            p->life--;
            expand_dirty(&dirty, (int)p->x, (int)p->y, 2, w, h);
            if (p->life <= 0 || p->y >= h || p->x < 0 || p->x >= w) {
                p->alive = 0;
                continue;
            }
            fade = (float)p->life / (float)p->max_life;
            r = (Uint8)((float)p->r * fade);
            g = (Uint8)((float)p->g * fade);
            b = (Uint8)((float)p->b * fade);
            rad = fade > 0.55f ? 2 : 1;
            put_dot(surface, (int)p->x, (int)p->y, rad, map_rgb(surface, r, g, b));
        }

        if (need_full) {
            if (!SDL_UpdateWindowSurface(window)) {
                fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
                break;
            }
            need_full = 0;
        } else if (dirty.w > 0) {
            if (!SDL_UpdateWindowSurfaceRects(window, &dirty, 1)) {
                fprintf(stderr, "SDL_UpdateWindowSurfaceRects failed: %s\n", SDL_GetError());
                break;
            }
        }

        SDL_Delay(16);
    }

done:
    SDL_DestroySurface(lake_scaled);
    SDL_DestroySurface(lake_src);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
