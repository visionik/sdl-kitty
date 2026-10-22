/* checker_keyboard.c - animated checkerboard + quit on q/Esc */
#include <SDL3/SDL.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *surface = NULL;
    int running = 1;
    int frame = 0;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());
    printf("press q or Esc to quit\n");

    window = SDL_CreateWindow("sdl-kitty checker", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = 0;
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_Q || e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = 0;
                }
            }
        }

        surface = SDL_GetWindowSurface(window);
        if (!surface) {
            fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
            break;
        }

        {
            const int tile = 32;
            const int ox = frame % tile;
            int y, x;
            for (y = 0; y < surface->h; ++y) {
                Uint32 *row = (Uint32 *)((Uint8 *)surface->pixels + y * surface->pitch);
                for (x = 0; x < surface->w; ++x) {
                    const int cx = (x + ox) / tile;
                    const int cy = y / tile;
                    const int on = (cx + cy) & 1;
                    row[x] = on ? 0x0022AA44u : 0x00222222u;
                }
            }
        }

        if (!SDL_UpdateWindowSurface(window)) {
            fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
            break;
        }
        frame++;
        SDL_Delay(33);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
