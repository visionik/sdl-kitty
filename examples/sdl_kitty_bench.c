/*
 * sdl_kitty_bench — real-terminal framebuffer benchmark for `sdlk bench`.
 *
 * Renders deterministic synthetic workloads (sparse dirty sprites or full-frame
 * smooth scroll) through the live Kitty SDL3 video driver, measures wall-clock
 * frame times, and prints machine-readable JSON/key-value stats to stderr so
 * `sdlk bench` can compare modes on the user's real terminal.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_double(const void *a, const void *b)
{
    const double da = *(const double *)a;
    const double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void draw_gradient_bg(SDL_Surface *s, int shift)
{
    int y, x;
    for (y = 0; y < s->h; ++y) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch);
        const Uint8 g = (Uint8)((y * 120) / (s->h > 0 ? s->h : 1) + 18);
        for (x = 0; x < s->w; ++x) {
            const Uint8 r = (Uint8)(((x + shift * 5) & 255) / 3 + 16);
            const Uint8 b = (Uint8)(((x * 2 + y + shift * 3) & 255) / 2 + 48);
            row[x] = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
        }
    }
}

static void fill_box(SDL_Surface *s, int bx, int by, int bw, int bh, Uint32 col)
{
    int y, x;
    int x0 = bx < 0 ? 0 : bx;
    int y0 = by < 0 ? 0 : by;
    int x1 = (bx + bw > s->w) ? s->w : (bx + bw);
    int y1 = (by + bh > s->h) ? s->h : (by + bh);
    for (y = y0; y < y1; ++y) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * (size_t)s->pitch);
        for (x = x0; x < x1; ++x) {
            row[x] = col;
        }
    }
}

int main(int argc, char **argv)
{
    const char *workload = "scroll";
    int frames = 60;
    int warmup = 8;
    int i;
    SDL_Window *win = NULL;
    SDL_Surface *surf = NULL;
    double *samples = NULL;
    Uint64 total_t0, total_t1;
    double total_s, fps, med_ms, p95_ms;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--workload") == 0 && i + 1 < argc) {
            workload = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
            if (frames < 10) frames = 10;
            if (frames > 600) frames = 600;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "BENCH_ERR init=%s\n", SDL_GetError());
        return 1;
    }

    win = SDL_CreateWindow("sdl-kitty-bench", 960, 540, 0);
    if (!win) {
        fprintf(stderr, "BENCH_ERR window=%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    surf = SDL_GetWindowSurface(win);
    if (!surf) {
        fprintf(stderr, "BENCH_ERR surface=%s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    samples = (double *)calloc((size_t)frames, sizeof(double));
    if (!samples) {
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    draw_gradient_bg(surf, 0);
    SDL_UpdateWindowSurface(win);

    for (i = 0; i < warmup; ++i) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) goto done;
        }
        if (strcmp(workload, "sparse") == 0) {
            int px = (i * 29) % (surf->w > 96 ? surf->w - 96 : 1);
            int py = (i * 19) % (surf->h > 96 ? surf->h - 96 : 1);
            SDL_Rect r = { px, py, 64, 64 };
            fill_box(surf, r.x, r.y, r.w, r.h, 0xFF2AC3DEu);
            SDL_UpdateWindowSurfaceRects(win, &r, 1);
            fill_box(surf, r.x, r.y, r.w, r.h, 0xFF151828u);
        } else {
            draw_gradient_bg(surf, i);
            SDL_UpdateWindowSurface(win);
        }
    }

    total_t0 = SDL_GetTicksNS();
    for (i = 0; i < frames; ++i) {
        SDL_Event ev;
        Uint64 f0, f1;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) goto done;
        }
        f0 = SDL_GetTicksNS();
        if (strcmp(workload, "sparse") == 0) {
            int px = ((i + warmup) * 37) % (surf->w > 96 ? surf->w - 96 : 1);
            int py = ((i + warmup) * 23) % (surf->h > 96 ? surf->h - 96 : 1);
            SDL_Rect r = { px, py, 72, 72 };
            fill_box(surf, r.x, r.y, r.w, r.h, (i & 1) ? 0xFF7AA2F7u : 0xFF2AC3DEu);
            SDL_UpdateWindowSurfaceRects(win, &r, 1);
            fill_box(surf, r.x, r.y, r.w, r.h, 0xFF151828u);
        } else {
            draw_gradient_bg(surf, i + warmup);
            SDL_UpdateWindowSurface(win);
        }
        f1 = SDL_GetTicksNS();
        samples[i] = (double)(f1 - f0) / 1000000.0;
    }
    total_t1 = SDL_GetTicksNS();

    total_s = (double)(total_t1 - total_t0) / 1e9;
    fps = (total_s > 1e-6) ? ((double)frames / total_s) : 0.0;
    qsort(samples, (size_t)frames, sizeof(double), cmp_double);
    med_ms = samples[frames / 2];
    p95_ms = samples[(frames * 95) / 100];

    fprintf(stderr, "BENCH_OK workload=%s w=%d h=%d frames=%d fps=%.1f med_ms=%.3f p95_ms=%.3f\n",
            workload, surf->w, surf->h, frames, fps, med_ms, p95_ms);

done:
    free(samples);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
