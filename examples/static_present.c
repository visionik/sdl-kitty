/* static_present.c - same pixels every frame (Exp 6 upload_skip soak). */
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

    window = SDL_CreateWindow("sdl-kitty static", 640, 480, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    for (i = 0; i < 120; ++i) {
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

        /* Identical fill every frame → upload_skip after the first upload. */
        SDL_FillSurfaceRect(surface, NULL, SDL_MapSurfaceRGB(surface, 40, 80, 160));
        if (!SDL_UpdateWindowSurface(window)) {
            fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
            break;
        }
        SDL_Delay(16);
    }

done:
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
