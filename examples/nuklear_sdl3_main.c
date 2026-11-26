/* Parent-owned wrapper around third_party/Nuklear/demo/sdl3_renderer/main.c:
 * adds Esc to quit (upstream only checks Ctrl+Q) and paces idle frames under
 * SDL's software renderer so Nuklear stays responsive in a terminal. */
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#define SDL_AppEvent   nk_upstream_AppEvent
#define SDL_AppIterate nk_upstream_AppIterate
#include "main.c"
#undef SDL_AppEvent
#undef SDL_AppIterate

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_KEY_DOWN &&
        event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return SDL_APP_SUCCESS;
    }
    return nk_upstream_AppEvent(appstate, event);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    const SDL_AppResult r = nk_upstream_AppIterate(appstate);
    if (r == SDL_APP_CONTINUE) {
        SDL_Delay(16);
    }
    return r;
}
