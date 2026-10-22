/* solid_present.c - smoke test for SDL_VIDEO_DRIVER=kitty */
#include <SDL3/SDL.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *surface = NULL;
    int i;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());

    window = SDL_CreateWindow("sdl-kitty solid", 640, 480, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    for (i = 0; i < 90; ++i) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                goto done;
            }
        }

        surface = SDL_GetWindowSurface(window);
        if (!surface) {
            fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
            break;
        }

        const Uint8 r = (Uint8)((i * 7) & 255);
        const Uint8 g = (Uint8)((i * 3) & 255);
        const Uint8 b = (Uint8)((i * 11) & 255);
        SDL_FillSurfaceRect(surface, NULL, SDL_MapSurfaceRGB(surface, r, g, b));
        if (!SDL_UpdateWindowSurface(window)) {
            fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        SDL_Delay(33);
    }

done:
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
