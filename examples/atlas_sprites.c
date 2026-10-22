/* atlas_sprites.c — Kitty atlas / multi-placement demo.
 *
 * Builds a 2-row sprite sheet (DVD + Blu-ray × N color frames), uploads once
 * with a=t, then bounces many placements (a=p crop + X/Y). Pairwise AABB
 * collide starts a 10s color pulse on only the sprites that hit.
 *
 * SDL provides events + window size; graphics go through examples/kitty_apc.c
 * on stdout (the tty). q / Esc quits.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "kitty_apc.h"

#define ATLAS_ID 42
#define NFRAMES 8
#define NLOGOS 2
#define PER_LOGO 5
#define NSPRITES (NLOGOS * PER_LOGO)
#define PULSE_MS 10000
#define PULSE_STEP_MS 80

enum {
    LOGO_DVD = 0,
    LOGO_BD = 1
};

static const unsigned char FONT_D[7] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
static const unsigned char FONT_V[7] = { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 };
static const unsigned char FONT_B[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
static const unsigned char FONT_L[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
static const unsigned char FONT_U[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };

static const struct { uint8_t r, g, b; } COLORS[NFRAMES] = {
    { 255, 0, 255 },
    { 0, 255, 255 },
    { 255, 255, 0 },
    { 0, 255, 0 },
    { 255, 102, 0 },
    { 255, 0, 102 },
    { 102, 102, 255 },
    { 255, 255, 255 },
};

typedef struct {
    int logo;          /* LOGO_DVD or LOGO_BD */
    uint32_t place_id;
    float x, y;
    float vx, vy;
    int frame_i;
    Uint64 pulse_until;
    Uint64 last_pulse_step;
} Sprite;

static void fill_ellipse_rgb(uint8_t *rgb, int aw, int ah, int cx, int cy, int rx, int ry,
                             uint8_t r, uint8_t g, uint8_t b)
{
    int y;
    for (y = -ry; y <= ry; ++y) {
        const int yy = cy + y;
        int xspan, x;
        double t;
        if (yy < 0 || yy >= ah) {
            continue;
        }
        t = 1.0 - ((double)y * (double)y) / ((double)ry * (double)ry);
        if (t <= 0.0) {
            continue;
        }
        xspan = (int)(rx * SDL_sqrt(t) + 0.5);
        for (x = cx - xspan; x <= cx + xspan; ++x) {
            uint8_t *p;
            if (x < 0 || x >= aw) {
                continue;
            }
            p = rgb + ((size_t)yy * (size_t)aw + (size_t)x) * 3u;
            p[0] = r;
            p[1] = g;
            p[2] = b;
        }
    }
}

static void blit_glyph_rgb(uint8_t *rgb, int aw, int ah, int ox, int oy,
                           const unsigned char *rows, int scale,
                           uint8_t r, uint8_t g, uint8_t b)
{
    int row, c, sy, sx;
    for (row = 0; row < 7; ++row) {
        for (c = 0; c < 5; ++c) {
            if (!(rows[row] & (1u << (4 - c)))) {
                continue;
            }
            for (sy = 0; sy < scale; ++sy) {
                const int y = oy + row * scale + sy;
                if (y < 0 || y >= ah) {
                    continue;
                }
                for (sx = 0; sx < scale; ++sx) {
                    const int x = ox + c * scale + sx;
                    uint8_t *p;
                    if (x < 0 || x >= aw) {
                        continue;
                    }
                    p = rgb + ((size_t)y * (size_t)aw + (size_t)x) * 3u;
                    p[0] = r;
                    p[1] = g;
                    p[2] = b;
                }
            }
        }
    }
}

static void clear_cell(uint8_t *rgb, int aw, int ah, int ox, int oy, int cw, int ch)
{
    int x, y;
    (void)ah;
    for (y = oy; y < oy + ch; ++y) {
        for (x = ox; x < ox + cw; ++x) {
            uint8_t *p = rgb + ((size_t)y * (size_t)aw + (size_t)x) * 3u;
            p[0] = p[1] = p[2] = 0;
        }
    }
}

static void draw_logo_cell(uint8_t *rgb, int aw, int ah, int ox, int oy, int cw, int ch,
                           int logo, uint8_t r, uint8_t g, uint8_t b)
{
    const int cx = ox + cw / 2;
    const int cy = oy + ch / 2;
    const int rx = cw / 2 - 2;
    const int ry = ch / 2 - 2;
    const int scale = SDL_max(2, ch / 18);
    const int glyph_w = 5 * scale;
    const int gap = scale;
    const int nletters = 3;
    const int text_w = glyph_w * nletters + gap * (nletters - 1);
    const int text_x = cx - text_w / 2;
    const int text_y = cy - (7 * scale) / 2 - scale;

    clear_cell(rgb, aw, ah, ox, oy, cw, ch);
    fill_ellipse_rgb(rgb, aw, ah, cx, cy, rx, ry, r, g, b);
    if (logo == LOGO_BD) {
        fill_ellipse_rgb(rgb, aw, ah, cx, cy, (rx * 4) / 5, (ry * 4) / 5, 0, 0, 0);
        fill_ellipse_rgb(rgb, aw, ah, cx, cy, (rx * 3) / 5, (ry * 3) / 5, r, g, b);
        fill_ellipse_rgb(rgb, aw, ah, cx, cy, (rx * 2) / 5, (ry * 2) / 5, 0, 0, 0);
    } else {
        fill_ellipse_rgb(rgb, aw, ah, cx, cy, (rx * 5) / 6, (ry * 5) / 6, 0, 0, 0);
    }

    if (logo == LOGO_DVD) {
        blit_glyph_rgb(rgb, aw, ah, text_x, text_y, FONT_D, scale, r, g, b);
        blit_glyph_rgb(rgb, aw, ah, text_x + glyph_w + gap, text_y, FONT_V, scale, r, g, b);
        blit_glyph_rgb(rgb, aw, ah, text_x + 2 * (glyph_w + gap), text_y, FONT_D, scale, r, g, b);
    } else {
        blit_glyph_rgb(rgb, aw, ah, text_x, text_y, FONT_B, scale, r, g, b);
        blit_glyph_rgb(rgb, aw, ah, text_x + glyph_w + gap, text_y, FONT_L, scale, r, g, b);
        blit_glyph_rgb(rgb, aw, ah, text_x + 2 * (glyph_w + gap), text_y, FONT_U, scale, r, g, b);
    }
}

static uint8_t *build_atlas(int cell_w, int cell_h, int *out_w, int *out_h)
{
    const int aw = cell_w * NFRAMES;
    const int ah = cell_h * NLOGOS;
    uint8_t *rgb = (uint8_t *)calloc((size_t)aw * (size_t)ah * 3u, 1);
    int logo, i;
    if (!rgb) {
        return NULL;
    }
    for (logo = 0; logo < NLOGOS; ++logo) {
        for (i = 0; i < NFRAMES; ++i) {
            draw_logo_cell(rgb, aw, ah, i * cell_w, logo * cell_h, cell_w, cell_h,
                           logo, COLORS[i].r, COLORS[i].g, COLORS[i].b);
        }
    }
    *out_w = aw;
    *out_h = ah;
    return rgb;
}

static void tty_grid(int *cols, int *rows, int *cw, int *ch)
{
    struct winsize ws;
    *cols = 80;
    *rows = 24;
    *cw = 10;
    *ch = 20;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) {
            *cols = ws.ws_col;
        }
        if (ws.ws_row > 0) {
            *rows = ws.ws_row;
        }
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0 && *cols > 0 && *rows > 0) {
            const int c = ws.ws_xpixel / *cols;
            const int r = ws.ws_ypixel / *rows;
            if (c >= 4 && c <= 64 && r >= 8 && r <= 128) {
                *cw = c;
                *ch = r;
            } else {
                *cw = 18;
                *ch = 36;
            }
        }
    }
}

static float rand_speed(float lo, float hi)
{
    /* SDL_rand(n) → [0, n). Map to [lo, hi]. */
    const float t = (float)SDL_rand(10000) / 10000.0f;
    float s = lo + t * (hi - lo);
    if (SDL_rand(2) == 0) {
        s = -s;
    }
    return s;
}

static void bounce_sprite(Sprite *s, float max_x, float max_y, Uint64 now)
{
    const int pulsing = (now < s->pulse_until);
    int bounced = 0;

    s->x += s->vx;
    s->y += s->vy;
    if (s->x <= 0.0f) {
        s->x = 0.0f;
        s->vx = SDL_fabsf(s->vx);
        bounced = 1;
    } else if (s->x >= max_x) {
        s->x = max_x;
        s->vx = -SDL_fabsf(s->vx);
        bounced = 1;
    }
    if (s->y <= 0.0f) {
        s->y = 0.0f;
        s->vy = SDL_fabsf(s->vy);
        bounced = 1;
    } else if (s->y >= max_y) {
        s->y = max_y;
        s->vy = -SDL_fabsf(s->vy);
        bounced = 1;
    }
    if (bounced && !pulsing) {
        s->frame_i = (s->frame_i + 1) % NFRAMES;
    }
}

static int aabb_overlap(const Sprite *a, const Sprite *b, int cell_w, int cell_h)
{
    return a->x < b->x + (float)cell_w &&
           a->x + (float)cell_w > b->x &&
           a->y < b->y + (float)cell_h &&
           a->y + (float)cell_h > b->y;
}

static void start_pulse(Sprite *s, Uint64 now)
{
    s->pulse_until = now + (Uint64)PULSE_MS;
    s->last_pulse_step = now;
}

static void place_sprite(int fd, const Sprite *s, int cell_w, int cell_h, int cw, int ch)
{
    int col = (int)s->x / cw + 1;
    int row = (int)s->y / ch + 1;
    int off_x = (int)s->x % cw;
    int off_y = (int)s->y % ch;
    int src_x = s->frame_i * cell_w;
    int src_y = s->logo * cell_h;

    if (col < 1) {
        col = 1;
    }
    if (row < 1) {
        row = 1;
    }
    kitty_apc_cup(fd, col, row);
    kitty_apc_place_crop(fd, ATLAS_ID, s->place_id,
                         src_x, src_y, cell_w, cell_h,
                         0, 0, off_x, off_y);
}

static void spawn_sprites(Sprite *sprites, int pane_w, int pane_h, int cell_w, int cell_h)
{
    const float max_x = (float)SDL_max(0, pane_w - cell_w);
    const float max_y = (float)SDL_max(0, pane_h - cell_h);
    int i;

    for (i = 0; i < NSPRITES; ++i) {
        const int logo = (i < PER_LOGO) ? LOGO_DVD : LOGO_BD;
        sprites[i].logo = logo;
        sprites[i].place_id = (uint32_t)(i + 1);
        sprites[i].x = (float)SDL_rand((int)max_x + 1);
        sprites[i].y = (float)SDL_rand((int)max_y + 1);
        sprites[i].vx = rand_speed(1.8f, 4.2f);
        sprites[i].vy = rand_speed(1.6f, 3.8f);
        sprites[i].frame_i = SDL_rand(NFRAMES);
        sprites[i].pulse_until = 0;
        sprites[i].last_pulse_step = 0;
    }
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    int cols, rows, cw, ch;
    int pane_w, pane_h;
    int cell_w = 120, cell_h = 60;
    int atlas_w = 0, atlas_h = 0;
    uint8_t *atlas = NULL;
    Sprite sprites[NSPRITES];
    int was_overlap[NSPRITES][NSPRITES];
    int fd = STDOUT_FILENO;
    int i, j;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    SDL_setenv_unsafe("SDL_KITTY_MOUSE", "0", 0);
    SDL_srand(0); /* fresh seed from SDL */

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "video driver: %s\n", SDL_GetCurrentVideoDriver());
    fprintf(stderr,
            "atlas_sprites: %d DVD + %d BLU; pairwise collide = 10s pulse on hitters; q quits\n",
            PER_LOGO, PER_LOGO);

    window = SDL_CreateWindow("sdl-kitty atlas", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    tty_grid(&cols, &rows, &cw, &ch);
    pane_w = cols * cw;
    pane_h = rows * ch;

    /* Slightly smaller cells so 10 logos fit the pane comfortably. */
    cell_w = SDL_max(48, SDL_min(120, pane_w / 10));
    cell_h = SDL_max(28, cell_w / 2);

    atlas = build_atlas(cell_w, cell_h, &atlas_w, &atlas_h);
    if (!atlas) {
        fprintf(stderr, "atlas alloc failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    kitty_apc_write(fd, "\033[2J\033[H", 6);
    if (!kitty_apc_transmit_rgb(fd, ATLAS_ID, atlas, atlas_w, atlas_h)) {
        fprintf(stderr, "atlas upload failed\n");
        free(atlas);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    fprintf(stderr,
            "uploaded atlas %dx%d (%d logos x %d colors, cell %dx%d); tty cell=%dx%d pane=%dx%d\n",
            atlas_w, atlas_h, NLOGOS, NFRAMES, cell_w, cell_h, cw, ch, pane_w, pane_h);

    memset(was_overlap, 0, sizeof(was_overlap));
    spawn_sprites(sprites, pane_w, pane_h, cell_w, cell_h);

    for (;;) {
        SDL_Event e;
        float max_x, max_y;
        Uint64 now;

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
                tty_grid(&cols, &rows, &cw, &ch);
                pane_w = cols * cw;
                pane_h = rows * ch;
            }
        }

        max_x = (float)SDL_max(0, pane_w - cell_w);
        max_y = (float)SDL_max(0, pane_h - cell_h);
        now = SDL_GetTicks();

        for (i = 0; i < NSPRITES; ++i) {
            bounce_sprite(&sprites[i], max_x, max_y, now);
        }

        /* Edge-triggered pairwise collide → pulse only the hitters. */
        for (i = 0; i < NSPRITES; ++i) {
            for (j = i + 1; j < NSPRITES; ++j) {
                const int hit = aabb_overlap(&sprites[i], &sprites[j], cell_w, cell_h);
                if (hit && !was_overlap[i][j]) {
                    start_pulse(&sprites[i], now);
                    start_pulse(&sprites[j], now);
                    fprintf(stderr, "collide p%u↔p%u → pulse %d ms\n",
                            (unsigned)sprites[i].place_id,
                            (unsigned)sprites[j].place_id,
                            PULSE_MS);
                }
                was_overlap[i][j] = hit;
            }
        }

        for (i = 0; i < NSPRITES; ++i) {
            if (now < sprites[i].pulse_until &&
                now - sprites[i].last_pulse_step >= (Uint64)PULSE_STEP_MS) {
                sprites[i].frame_i = (sprites[i].frame_i + 1) % NFRAMES;
                sprites[i].last_pulse_step = now;
            }
        }

        for (i = 0; i < NSPRITES; ++i) {
            place_sprite(fd, &sprites[i], cell_w, cell_h, cw, ch);
        }

        SDL_Delay(16);
    }

done:
    kitty_apc_delete_image(fd, ATLAS_ID);
    kitty_apc_write(fd, "\033[2J\033[H", 6);
    free(atlas);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
