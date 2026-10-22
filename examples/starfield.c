/* starfield.c - classic warp starfield for SDL_VIDEO_DRIVER=kitty
 *
 * Press any key on the waiting screen to start. Q / Esc quits.
 * Hold left mouse or space for faster warp.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_STARS 400

typedef struct {
    float x, y, z;
} Star;

static Uint32 star_color(float z, float zmax)
{
    /* Brighter / whiter as stars approach the camera (small z). */
    float t = 1.0f - (z / zmax);
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    const int c = 40 + (int)(t * 215.0f);
    const int b = 40 + (int)(t * t * 215.0f);
    return ((Uint32)c << 16) | ((Uint32)c << 8) | (Uint32)b;
}

static void put_pixel(SDL_Surface *s, int x, int y, Uint32 color)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) {
        return;
    }
    Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
    row[x] = color;
}

static void put_dot(SDL_Surface *s, int x, int y, int r, Uint32 color)
{
    int dy, dx;
    const int r2 = r * r;
    for (dy = -r; dy <= r; ++dy) {
        for (dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                put_pixel(s, x + dx, y + dy, color);
            }
        }
    }
}

static void reset_star(Star *st, float zmax, int spread)
{
    /* Spawn ahead of the camera in a volume around the view axis. */
    st->x = ((float)(SDL_rand(spread * 2) - spread));
    st->y = ((float)(SDL_rand(spread * 2) - spread));
    st->z = (float)(SDL_rand((int)zmax - 50) + 50);
}

static void init_stars(Star *stars, int n, float zmax, int spread)
{
    int i;
    for (i = 0; i < n; ++i) {
        reset_star(&stars[i], zmax, spread);
        /* Stagger depth so the first frame isn't empty. */
        stars[i].z = (float)(SDL_rand((int)zmax) + 1);
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
    Star stars[MAX_STARS];
    int ww = 0, wh = 0;
    int phase = 0; /* 0 = wait, 1 = fly */
    int run = 1;
    int boost = 0;
    float zmax = 400.0f;
    float speed = 180.0f;
    int spread = 200;
    Uint64 prev_ms;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    setenv("SDL_KITTY_TRANSMIT", "direct", 1);
    SDL_srand(0);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("starfield", 0, 0, 0);
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

    write(STDOUT_FILENO, "ASCII: press any key for starfield (hold space to boost, q quits)\r\n", 68);

    SDL_FillSurfaceRect(surf, NULL, 0x00000000u);
    {
        SDL_Rect bar = { ww / 4, wh / 2 - 8, ww / 2, 16 };
        SDL_FillSurfaceRect(surf, &bar, 0x00222222u);
    }
    SDL_UpdateWindowSurface(window);

    init_stars(stars, MAX_STARS, zmax, spread);
    prev_ms = SDL_GetTicks();

    while (run) {
        SDL_Event e;
        Uint64 now;
        float dt;
        float sp;
        int i;
        const float cx = ww * 0.5f;
        const float cy = wh * 0.5f;
        const float fov = (float)SDL_min(ww, wh);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                run = 0;
            } else if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                       e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                surf = get_surface(window, &ww, &wh);
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_Q || e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    run = 0;
                } else if (e.key.scancode == SDL_SCANCODE_SPACE) {
                    boost = 1;
                    if (phase == 0) {
                        phase = 1;
                        write(STDOUT_FILENO, "ASCII: warp engaged\r\n", 22);
                        prev_ms = SDL_GetTicks();
                    }
                } else if (phase == 0) {
                    phase = 1;
                    write(STDOUT_FILENO, "ASCII: warp engaged\r\n", 22);
                    prev_ms = SDL_GetTicks();
                }
            } else if (e.type == SDL_EVENT_KEY_UP) {
                if (e.key.scancode == SDL_SCANCODE_SPACE) {
                    boost = 0;
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (e.button.button == 1) {
                    boost = 1;
                    if (phase == 0) {
                        phase = 1;
                        prev_ms = SDL_GetTicks();
                    }
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.button == 1) {
                    boost = 0;
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

        SDL_FillSurfaceRect(surf, NULL, 0x00000000u);

        if (phase == 0) {
            SDL_Rect bar = { ww / 4, wh / 2 - 8, ww / 2, 16 };
            SDL_FillSurfaceRect(surf, &bar, 0x00222222u);
        } else {
            sp = speed * (boost ? 3.5f : 1.0f);
            for (i = 0; i < MAX_STARS; ++i) {
                float sx, sy, prev_z;
                int px, py, size;
                Uint32 col;

                prev_z = stars[i].z;
                stars[i].z -= sp * dt;
                if (stars[i].z <= 1.0f) {
                    reset_star(&stars[i], zmax, spread);
                    continue;
                }

                /* Perspective project. */
                sx = cx + (stars[i].x / stars[i].z) * fov;
                sy = cy + (stars[i].y / stars[i].z) * fov;
                px = (int)(sx + 0.5f);
                py = (int)(sy + 0.5f);

                if (px < -4 || py < -4 || px >= ww + 4 || py >= wh + 4) {
                    reset_star(&stars[i], zmax, spread);
                    continue;
                }

                /* Motion streak toward center when boosting. */
                if (boost && prev_z > stars[i].z + 1.0f) {
                    const float psx = cx + (stars[i].x / prev_z) * fov;
                    const float psy = cy + (stars[i].y / prev_z) * fov;
                    int x0 = (int)psx, y0 = (int)psy;
                    int x1 = px, y1 = py;
                    int steps, s;
                    const int dx = x1 - x0;
                    const int dy = y1 - y0;
                    steps = SDL_max(SDL_abs(dx), SDL_abs(dy));
                    if (steps > 40) {
                        steps = 40;
                    }
                    for (s = 0; s <= steps; ++s) {
                        const float t = steps ? (float)s / (float)steps : 1.0f;
                        put_pixel(surf,
                                  x0 + (int)(dx * t),
                                  y0 + (int)(dy * t),
                                  star_color(stars[i].z + (prev_z - stars[i].z) * (1.0f - t), zmax));
                    }
                }

                size = 1 + (int)((1.0f - stars[i].z / zmax) * 2.5f);
                col = star_color(stars[i].z, zmax);
                put_dot(surf, px, py, size, col);
            }
        }

        SDL_UpdateWindowSurface(window);
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
