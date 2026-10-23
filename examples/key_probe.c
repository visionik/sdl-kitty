/* key_probe.c - soft-surface keyboard harness for sdl-kitty
 *
 * Shows last key down/up/repeat, scancode, keycode, mods, and text events.
 * Quit with q or Esc (or two Esc in a row).
 *
 *   scripts/run-with-kitty-sdl.sh build/key_probe
 *   SDL_KITTY_KEY_DEBUG=1 scripts/run-with-kitty-sdl.sh build/key_probe
 */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

#define LINE_H 18
#define MAX_LOG 12

typedef struct KeyLogLine {
    char text[96];
} KeyLogLine;

static KeyLogLine g_log[MAX_LOG];
static int g_log_n;
static char g_text_accum[128];

static void log_line(const char *s)
{
    if (g_log_n < MAX_LOG) {
        SDL_strlcpy(g_log[g_log_n].text, s, sizeof(g_log[g_log_n].text));
        g_log_n++;
    } else {
        int i;
        for (i = 1; i < MAX_LOG; ++i) {
            g_log[i - 1] = g_log[i];
        }
        SDL_strlcpy(g_log[MAX_LOG - 1].text, s, sizeof(g_log[MAX_LOG - 1].text));
    }
    /* Raw tty: LF alone does not return the cursor; use CRLF. */
    fprintf(stderr, "key_probe: %s\r\n", s);
}

static void fill_rect(SDL_Surface *s, int x, int y, int w, int h, Uint32 c)
{
    SDL_Rect r = { x, y, w, h };
    SDL_FillSurfaceRect(s, &r, c);
}

/* No TTF: encode each char as a short vertical bar pattern so the pane
 * shows *that something changed*. Real detail is on stderr. */
static void draw_char(SDL_Surface *s, int x, int y, char ch, Uint32 fg)
{
    const int cw = 6, chh = 10;
    fill_rect(s, x, y, cw, chh, 0x00111111u);
    if (ch >= 32 && ch < 127) {
        const int bar = 2 + (ch % 7);
        fill_rect(s, x + 1, y + (chh - bar), cw - 2, bar, fg);
        if (ch >= '0' && ch <= '9') {
            int i;
            for (i = 0; i < (ch - '0') + 1 && i < 9; ++i) {
                fill_rect(s, x + 1, y + 1 + i, 1, 1, fg);
            }
        }
    }
}

static void draw_string(SDL_Surface *s, int x, int y, const char *str, Uint32 fg)
{
    int cx = x;
    while (*str) {
        draw_char(s, cx, y, *str++, fg);
        cx += 7;
    }
}

static const char *event_name(SDL_EventType t)
{
    switch (t) {
    case SDL_EVENT_KEY_DOWN:
        return "DOWN";
    case SDL_EVENT_KEY_UP:
        return "UP";
    case SDL_EVENT_TEXT_INPUT:
        return "TEXT";
    default:
        return "?";
    }
}

static void mods_str(SDL_Keymod m, char *buf, size_t n)
{
    buf[0] = '\0';
    if (m & SDL_KMOD_SHIFT) {
        SDL_strlcat(buf, "S", n);
    }
    if (m & SDL_KMOD_CTRL) {
        SDL_strlcat(buf, "C", n);
    }
    if (m & SDL_KMOD_ALT) {
        SDL_strlcat(buf, "A", n);
    }
    if (m & SDL_KMOD_GUI) {
        SDL_strlcat(buf, "G", n);
    }
    if (m & SDL_KMOD_CAPS) {
        SDL_strlcat(buf, "c", n);
    }
    if (m & SDL_KMOD_NUM) {
        SDL_strlcat(buf, "n", n);
    }
    if (buf[0] == '\0') {
        SDL_strlcpy(buf, "-", n);
    }
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_Surface *surface = NULL;
    int running = 1;
    char line[96];
    char mbuf[16];

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    /* Always enable driver key debug for this harness. */
    if (!SDL_getenv("SDL_KITTY_KEY_DEBUG")) {
        SDL_setenv_unsafe("SDL_KITTY_KEY_DEBUG", "1", 1);
    }
    /* File-redirected stderr is fully buffered by default — looks like a
     * hang when the pane is graphics-only. Force line buffering. */
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\r\n", SDL_GetError());
        return 1;
    }

    fprintf(stderr, "key_probe: video=%s  press q or Esc to quit (Ctrl+C also works)\r\n",
            SDL_GetCurrentVideoDriver());

    window = SDL_CreateWindow("sdl-kitty key_probe", 0, 0, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\r\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_StartTextInput(window);
    g_text_accum[0] = '\0';
    log_line("ready — type keys");

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = 0;
            } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                mods_str(e.key.mod, mbuf, sizeof(mbuf));
                SDL_snprintf(line, sizeof(line), "%s sc=%d kc=%d rpt=%d mod=%s",
                             event_name(e.type), (int)e.key.scancode, (int)e.key.key,
                             e.key.repeat ? 1 : 0, mbuf);
                log_line(line);

                if (e.type == SDL_EVENT_KEY_DOWN) {
                    /* Single Esc or q exits immediately (no Esc-Esc streak). */
                    if (e.key.scancode == SDL_SCANCODE_ESCAPE ||
                        e.key.scancode == SDL_SCANCODE_Q) {
                        running = 0;
                        break;
                    }
                }
            } else if (e.type == SDL_EVENT_TEXT_INPUT) {
                SDL_snprintf(line, sizeof(line), "TEXT '%s'", e.text.text);
                log_line(line);
                if (SDL_strlen(g_text_accum) + SDL_strlen(e.text.text) < sizeof(g_text_accum) - 1) {
                    SDL_strlcat(g_text_accum, e.text.text, sizeof(g_text_accum));
                }
            } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                log_line("FOCUS_GAINED");
            } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                log_line("FOCUS_LOST");
            }
        }

        surface = SDL_GetWindowSurface(window);
        if (!surface) {
            fprintf(stderr, "GetWindowSurface: %s\r\n", SDL_GetError());
            break;
        }

        fill_rect(surface, 0, 0, surface->w, surface->h, 0x00101018u);
        /* Header bar */
        fill_rect(surface, 0, 0, surface->w, 28, 0x00203040u);
        draw_string(surface, 8, 8, "KEY PROBE", 0x00E8F0FFu);
        draw_string(surface, 100, 8, g_text_accum[0] ? g_text_accum : "(no text yet)", 0x00A0FFA0u);

        {
            int i;
            for (i = 0; i < g_log_n; ++i) {
                const int y = 36 + i * LINE_H;
                if (y + LINE_H > surface->h) {
                    break;
                }
                fill_rect(surface, 4, y, surface->w - 8, LINE_H - 2, 0x00202830u);
                draw_string(surface, 8, y + 2, g_log[i].text, 0x00F0F0F0u);
            }
        }

        /* Held keys strip */
        {
            const bool *state = NULL;
            int n = 0, x = 8, count = 0;
            state = SDL_GetKeyboardState(&n);
            fill_rect(surface, 0, surface->h - 24, surface->w, 24, 0x00301010u);
            draw_string(surface, 8, surface->h - 18, "HELD", 0x00FF8080u);
            x = 50;
            if (state) {
                int sc;
                for (sc = 0; sc < n && count < 24; ++sc) {
                    if (state[sc]) {
                        char tmp[8];
                        SDL_snprintf(tmp, sizeof(tmp), "%d", sc);
                        draw_string(surface, x, surface->h - 18, tmp, 0x00FFE0E0u);
                        x += (int)SDL_strlen(tmp) * 7 + 8;
                        count++;
                    }
                }
            }
        }

        if (!SDL_UpdateWindowSurface(window)) {
            fprintf(stderr, "UpdateWindowSurface: %s\r\n", SDL_GetError());
            break;
        }
        SDL_Delay(16);
    }

    SDL_StopTextInput(window);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
