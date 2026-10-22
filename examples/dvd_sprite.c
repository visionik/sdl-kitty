/* dvd_sprite.c — bouncing DVD logo sprite over an isometric DVD-player backdrop.
 *
 * SDL has no separate "Sprite" type; the usual pattern is a surface drawn with
 * SDL_BlitSurface. This demo:
 *   - loads examples/assets/dvd_player_iso.png as the background
 *   - builds a logo surface once, color-mods it on wall hits
 *   - restores dirty rects from the scaled background, then blits the logo
 *
 * q / Esc quits. Space forces a color change.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const unsigned char FONT_D[7] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
static const unsigned char FONT_V[7] = { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 };

static const struct { Uint8 r, g, b; } BOUNCE_COLORS[] = {
    { 255, 0, 255 },
    { 0, 255, 255 },
    { 255, 255, 0 },
    { 0, 255, 0 },
    { 255, 102, 0 },
    { 255, 0, 102 },
    { 102, 102, 255 },
    { 255, 255, 255 },
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
        int xspan, x;
        if (yy < 0 || yy >= s->h) {
            continue;
        }
        {
            const double t = 1.0 - ((double)y * (double)y) / ((double)ry * (double)ry);
            if (t <= 0.0) {
                continue;
            }
            xspan = (int)(rx * SDL_sqrt(t) + 0.5);
        }
        {
            Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)yy * (size_t)s->pitch);
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
                Uint32 *row;
                if (y < 0 || y >= s->h) {
                    continue;
                }
                row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch);
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

static SDL_Surface *create_dvd_sprite(int logo_w, int logo_h)
{
    SDL_Surface *s;
    const Uint32 white = 0x00FFFFFFu;
    const Uint32 key = 0x00000000u;
    const int cx = logo_w / 2;
    const int cy = logo_h / 2;
    const int rx = logo_w / 2 - 1;
    const int ry = logo_h / 2 - 1;
    const int scale = SDL_max(2, logo_h / 18);
    const int glyph_w = 5 * scale;
    const int glyph_h = 7 * scale;
    const int gap = scale;
    const int text_w = glyph_w * 3 + gap * 2;
    const int text_x = cx - text_w / 2;
    const int text_y = cy - glyph_h / 2 - scale;
    SDL_Rect bar;

    s = SDL_CreateSurface(logo_w, logo_h, SDL_PIXELFORMAT_XRGB8888);
    if (!s) {
        return NULL;
    }
    SDL_FillSurfaceRect(s, NULL, key);
    fill_ellipse(s, cx, cy, rx, ry, white);
    fill_ellipse(s, cx, cy, (rx * 5) / 6, (ry * 5) / 6, key);
    blit_glyph(s, text_x, text_y, FONT_D, scale, white);
    blit_glyph(s, text_x + glyph_w + gap, text_y, FONT_V, scale, white);
    blit_glyph(s, text_x + 2 * (glyph_w + gap), text_y, FONT_D, scale, white);
    bar.x = text_x;
    bar.y = text_y + glyph_h + scale / 2;
    bar.w = text_w;
    bar.h = SDL_max(2, scale / 2);
    SDL_FillSurfaceRect(s, &bar, white);

    if (!SDL_SetSurfaceColorKey(s, true, key)) {
        SDL_DestroySurface(s);
        return NULL;
    }
    SDL_SetSurfaceColorMod(s, 255, 255, 255);
    return s;
}

static void apply_bounce_color(SDL_Surface *sprite, int color_i)
{
    const int i = ((color_i % NCOLORS) + NCOLORS) % NCOLORS;
    SDL_SetSurfaceColorMod(sprite, BOUNCE_COLORS[i].r, BOUNCE_COLORS[i].g, BOUNCE_COLORS[i].b);
}

static SDL_Surface *load_background_png(void)
{
    static const char *candidates[] = {
        "examples/assets/dvd_player_iso.png",
        "../examples/assets/dvd_player_iso.png",
        "assets/dvd_player_iso.png",
        NULL
    };
    char *base = (char *)SDL_GetBasePath();
    SDL_Surface *img = NULL;
    int i;

    if (base) {
        char path[1024];
        SDL_snprintf(path, sizeof(path), "%sassets/dvd_player_iso.png", base);
        img = SDL_LoadPNG(path);
        if (!img) {
            SDL_snprintf(path, sizeof(path), "%sexamples/assets/dvd_player_iso.png", base);
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

static void clamp_rect_to_surface(SDL_Rect *r, int w, int h)
{
    if (r->x < 0) {
        r->w += r->x;
        r->x = 0;
    }
    if (r->y < 0) {
        r->h += r->y;
        r->y = 0;
    }
    if (r->x + r->w > w) {
        r->w = w - r->x;
    }
    if (r->y + r->h > h) {
        r->h = h - r->y;
    }
    if (r->w < 0) {
        r->w = 0;
    }
    if (r->h < 0) {
        r->h = 0;
    }
}

static void restore_bg_rect(SDL_Surface *screen, SDL_Surface *scaled_bg, SDL_Rect area)
{
    SDL_Rect r = area;
    if (!screen || !scaled_bg) {
        return;
    }
    clamp_rect_to_surface(&r, screen->w, screen->h);
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    SDL_BlitSurface(scaled_bg, &r, screen, &r);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *screen = NULL;
    SDL_Surface *sprite = NULL;
    SDL_Surface *bg_src = NULL;
    SDL_Surface *bg_scaled = NULL;
    int sw = 0, sh = 0;
    int logo_w, logo_h;
    float x, y, vx, vy;
    int color_i = 0;
    int frame;
    SDL_Rect prev;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "video driver: %s\n", SDL_GetCurrentVideoDriver());
    fprintf(stderr, "dvd_sprite: isometric DVD player bg + logo sprite; space=recolor; q quits\n");
    fprintf(stderr, "  dirty BW follows a=f probe (SDL_KITTY_DIRTY_BW=0|1 to force)\n");

    window = SDL_CreateWindow("sdl-kitty dvd sprite", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    screen = SDL_GetWindowSurface(window);
    if (!screen) {
        fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    sw = screen->w;
    sh = screen->h;

    bg_src = load_background_png();
    if (!bg_src) {
        fprintf(stderr, "warning: could not load dvd_player_iso.png (%s); using black bg\n",
                SDL_GetError());
    } else {
        bg_scaled = scale_background(bg_src, sw, sh);
        if (!bg_scaled) {
            fprintf(stderr, "warning: scale background failed: %s\n", SDL_GetError());
        }
    }

    logo_w = SDL_max(96, sw / 5);
    logo_h = SDL_max(48, logo_w / 2);
    sprite = create_dvd_sprite(logo_w, logo_h);
    if (!sprite) {
        fprintf(stderr, "create_dvd_sprite failed: %s\n", SDL_GetError());
        SDL_DestroySurface(bg_scaled);
        SDL_DestroySurface(bg_src);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    apply_bounce_color(sprite, color_i);

    x = (float)(sw / 8);
    y = (float)(sh / 8);
    vx = 3.2f;
    vy = 2.4f;
    prev.x = (int)x;
    prev.y = (int)y;
    prev.w = logo_w;
    prev.h = logo_h;

    if (bg_scaled) {
        SDL_BlitSurface(bg_scaled, NULL, screen, NULL);
    } else {
        SDL_FillSurfaceRect(screen, NULL, 0x00000000u);
    }
    SDL_UpdateWindowSurface(window);

    for (frame = 0;; ++frame) {
        SDL_Event e;
        SDL_Rect next, dirty[2];
        int bounced = 0;
        bool force_color = false;
        bool need_full = false;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                goto done;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_Q || e.key.key == SDLK_ESCAPE) {
                    goto done;
                }
                if (e.key.key == SDLK_SPACE) {
                    force_color = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                screen = SDL_GetWindowSurface(window);
                if (!screen) {
                    continue;
                }
                sw = screen->w;
                sh = screen->h;
                if (x + logo_w > sw) {
                    x = (float)SDL_max(0, sw - logo_w);
                }
                if (y + logo_h > sh) {
                    y = (float)SDL_max(0, sh - logo_h);
                }
                if (bg_src) {
                    SDL_DestroySurface(bg_scaled);
                    bg_scaled = scale_background(bg_src, sw, sh);
                }
                need_full = true;
            }
        }

        screen = SDL_GetWindowSurface(window);
        if (!screen) {
            SDL_Delay(16);
            continue;
        }
        sw = screen->w;
        sh = screen->h;

        x += vx;
        y += vy;
        if (x <= 0.0f) {
            x = 0.0f;
            vx = SDL_fabsf(vx);
            bounced = 1;
        } else if (x + (float)logo_w >= (float)sw) {
            x = (float)(sw - logo_w);
            vx = -SDL_fabsf(vx);
            bounced = 1;
        }
        if (y <= 0.0f) {
            y = 0.0f;
            vy = SDL_fabsf(vy);
            bounced = 1;
        } else if (y + (float)logo_h >= (float)sh) {
            y = (float)(sh - logo_h);
            vy = -SDL_fabsf(vy);
            bounced = 1;
        }
        if (bounced || force_color) {
            color_i = (color_i + 1) % NCOLORS;
            apply_bounce_color(sprite, color_i);
        }

        next.x = (int)x;
        next.y = (int)y;
        next.w = logo_w;
        next.h = logo_h;

        if (need_full || frame == 0) {
            if (bg_scaled) {
                SDL_BlitSurface(bg_scaled, NULL, screen, NULL);
            } else {
                SDL_FillSurfaceRect(screen, NULL, 0x00000000u);
            }
            {
                SDL_Rect dst = next;
                SDL_BlitSurface(sprite, NULL, screen, &dst);
            }
            if (!SDL_UpdateWindowSurface(window)) {
                fprintf(stderr, "UpdateWindowSurface failed: %s\n", SDL_GetError());
            }
        } else {
            if (bg_scaled) {
                restore_bg_rect(screen, bg_scaled, prev);
            } else {
                SDL_FillSurfaceRect(screen, &prev, 0x00000000u);
            }
            {
                SDL_Rect dst = next;
                if (!SDL_BlitSurface(sprite, NULL, screen, &dst)) {
                    fprintf(stderr, "SDL_BlitSurface failed: %s\n", SDL_GetError());
                }
            }
            dirty[0] = prev;
            dirty[1] = next;
            if (!SDL_UpdateWindowSurfaceRects(window, dirty, 2)) {
                fprintf(stderr, "UpdateWindowSurfaceRects failed: %s\n", SDL_GetError());
            }
        }
        prev = next;
        SDL_Delay(16);
    }

done:
    SDL_DestroySurface(sprite);
    SDL_DestroySurface(bg_scaled);
    SDL_DestroySurface(bg_src);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
