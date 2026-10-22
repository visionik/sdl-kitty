/* mouse_paint.c - click/drag to paint; wheel changes color; q/Esc quits */
#include <SDL3/SDL.h>
#include <stdio.h>

static Uint32 g_color = 0x00FF6644u;
static int g_radius = 8;

static void paint_disk(SDL_Surface *surface, int cx, int cy, Uint32 color)
{
    const int r2 = g_radius * g_radius;
    int y;
    for (y = cy - g_radius; y <= cy + g_radius; ++y) {
        int x;
        if (y < 0 || y >= surface->h) {
            continue;
        }
        Uint32 *row = (Uint32 *)((Uint8 *)surface->pixels + y * surface->pitch);
        for (x = cx - g_radius; x <= cx + g_radius; ++x) {
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

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *surface = NULL;
    int running = 1;
    int painting = 0;
    int dirty = 1;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());
    printf("drag to paint; wheel recolors; q/Esc quits\n");

    window = SDL_CreateWindow("sdl-kitty paint", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    surface = SDL_GetWindowSurface(window);
    if (!surface) {
        fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_FillSurfaceRect(surface, NULL, 0x00111111u);

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
                if (e.button.button == SDL_BUTTON_LEFT) {
                    painting = 1;
                    paint_disk(surface, (int)e.button.x, (int)e.button.y, g_color);
                    dirty = 1;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    SDL_FillSurfaceRect(surface, NULL, 0x00111111u);
                    dirty = 1;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    painting = 0;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (painting) {
                    paint_disk(surface, (int)e.motion.x, (int)e.motion.y, g_color);
                    dirty = 1;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                static const Uint32 palette[] = {
                    0x00FF6644u, 0x0044AAFFu, 0x00F0D050u, 0x00AA66FFu, 0x00FFFFFFu
                };
                static int idx = 0;
                if (e.wheel.y > 0) {
                    idx = (idx + 1) % (int)(sizeof(palette) / sizeof(palette[0]));
                } else if (e.wheel.y < 0) {
                    idx = (idx + (int)(sizeof(palette) / sizeof(palette[0])) - 1) %
                          (int)(sizeof(palette) / sizeof(palette[0]));
                }
                g_color = palette[idx];
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED:
                surface = SDL_GetWindowSurface(window);
                if (surface) {
                    SDL_FillSurfaceRect(surface, NULL, 0x00111111u);
                    dirty = 1;
                }
                break;
            default:
                break;
            }
        }

        if (dirty) {
            if (!SDL_UpdateWindowSurface(window)) {
                fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
                break;
            }
            dirty = 0;
        }
        SDL_Delay(8);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
