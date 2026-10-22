/* dirty_rects_demo — sparse UpdateWindowSurfaceRects vs occasional full clear.
 * With SDL_KITTY_DEBUG=1, expect present mode=dirty_cpu for the bouncing rect
 * and mode=full after fullscreen fills / first frame.
 */
#include <SDL3/SDL.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *surface = NULL;
    int frame;
    int bx = 40, by = 40, bvx = 5, bvy = 3;
    const int bw = 48, bh = 36;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "video driver: %s\n", SDL_GetCurrentVideoDriver());
    fprintf(stderr, "dirty_rects_demo: bounce=small rect updates; space=fullscreen flash; q quits\n");
    fprintf(stderr, "  SDL_KITTY_DIRTY=0 disables dirty CPU; SDL_KITTY_DIRTY_BW=1 enables a=f bandwidth path\n");

    window = SDL_CreateWindow("sdl-kitty dirty rects", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    for (frame = 0;; ++frame) {
        SDL_Event e;
        SDL_Rect prev, next, dirty[2];
        int ndirty;
        bool flash = false;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                goto done;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_Q || e.key.key == SDLK_ESCAPE) {
                    goto done;
                }
                if (e.key.key == SDLK_SPACE) {
                    flash = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                surface = NULL;
            }
        }

        surface = SDL_GetWindowSurface(window);
        if (!surface) {
            fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
            break;
        }

        prev.x = bx;
        prev.y = by;
        prev.w = bw;
        prev.h = bh;

        bx += bvx;
        by += bvy;
        if (bx < 0 || bx + bw >= surface->w) {
            bvx = -bvx;
            bx += bvx;
        }
        if (by < 0 || by + bh >= surface->h) {
            bvy = -bvy;
            by += bvy;
        }
        next.x = bx;
        next.y = by;
        next.w = bw;
        next.h = bh;

        if (flash || (frame % 120) == 0) {
            const Uint8 r = (Uint8)((frame * 3) & 255);
            const Uint8 g = (Uint8)((frame * 5) & 255);
            const Uint8 b = (Uint8)((frame * 7) & 255);
            SDL_FillSurfaceRect(surface, NULL, SDL_MapSurfaceRGB(surface, r / 8, g / 8, b / 8));
            if (!SDL_UpdateWindowSurface(window)) {
                fprintf(stderr, "UpdateWindowSurface failed: %s\n", SDL_GetError());
                break;
            }
        } else {
            SDL_FillSurfaceRect(surface, &prev, SDL_MapSurfaceRGB(surface, 16, 16, 24));
            SDL_FillSurfaceRect(surface, &next, SDL_MapSurfaceRGB(surface, 240, 200, 40));
            dirty[0] = prev;
            dirty[1] = next;
            ndirty = 2;
            if (!SDL_UpdateWindowSurfaceRects(window, dirty, ndirty)) {
                fprintf(stderr, "UpdateWindowSurfaceRects failed: %s\n", SDL_GetError());
                break;
            }
        }
        SDL_Delay(16);
    }

done:
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
