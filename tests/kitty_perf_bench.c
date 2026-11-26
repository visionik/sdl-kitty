/* kitty_perf_bench.c — Deterministic micro + end-to-end PTY benchmark suite
 * for sdl-kitty. No real terminal required (uses openpty + fast mock host). */
#define KITTY_RECT_STANDALONE 1
#include "../third_party/SDL/src/video/kitty/SDL_kittyrect.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

static int cmp_double(const void *a, const void *b)
{
    const double da = *(const double *)a;
    const double db = *(const double *)b;
    return (da < db) ? -1 : (da > db) ? 1 : 0;
}

static double median_of(double *vals, int n)
{
    qsort(vals, (size_t)n, sizeof(double), cmp_double);
    if (n <= 0) {
        return 0.0;
    }
    if ((n & 1) == 1) {
        return vals[n / 2];
    }
    return 0.5 * (vals[n / 2 - 1] + vals[n / 2]);
}

/* Fill a synthetic arcade/UI surface: flat gradient sky + grid + crisp blocks. */
static void fill_scene(Uint32 *px, int w, int h, int frame_seed)
{
    int y, x;
    for (y = 0; y < h; ++y) {
        const Uint32 bg_r = (Uint32)((y * 40) / h);
        const Uint32 bg_g = (Uint32)(16 + ((y * 60) / h));
        const Uint32 bg_b = (Uint32)(48 + ((y * 90) / h));
        const Uint32 base = 0xFF000000u | (bg_r << 16) | (bg_g << 8) | bg_b;
        Uint32 *row = px + (size_t)y * (size_t)w;
        for (x = 0; x < w; ++x) {
            Uint32 c = base;
            if (((x + frame_seed) & 63) == 0 || ((y + frame_seed) & 63) == 0) {
                c = 0xFF305070u;
            }
            row[x] = c;
        }
    }
    /* Crisp HUD bar across top */
    for (y = 8; y < 28 && y < h; ++y) {
        Uint32 *row = px + (size_t)y * (size_t)w;
        for (x = 16; x < 240 && x < w; ++x) {
            row[x] = ((x >> 3) & 1) ? 0xFFE8D040u : 0xFF202028u;
        }
    }
}

static void draw_sprite(Uint32 *px, int w, int h, int sx, int sy, int sw, int sh, Uint32 color)
{
    int y, x;
    for (y = sy; y < sy + sh && y < h; ++y) {
        if (y < 0) continue;
        Uint32 *row = px + (size_t)y * (size_t)w;
        for (x = sx; x < sx + sw && x < w; ++x) {
            if (x < 0) continue;
            row[x] = color ^ (Uint32)(((x - sx) * 7 + (y - sy) * 13) & 0x3F);
        }
    }
}

/* ---------------- Microbenchmarks ---------------- */

static void run_microbenchmarks(void)
{
    enum { ITERS = 41 };
    double samples[ITERS];
    int it;

    /* 720p & 1080p surfaces */
    SDL_Surface *s720 = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_XRGB8888);
    SDL_Surface *s1080 = SDL_CreateSurface(1920, 1080, SDL_PIXELFORMAT_XRGB8888);
    SDL_Surface *s720a = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_ARGB8888);
    Uint8 *rgb720_a = (Uint8 *)malloc(1280u * 720u * 3u);
    Uint8 *rgb720_b = (Uint8 *)malloc(1280u * 720u * 3u);
    Uint8 *rgb1080_a = (Uint8 *)malloc(1920u * 1080u * 3u);
    Uint8 *rgb1080_b = (Uint8 *)malloc(1920u * 1080u * 3u);
    Uint8 *rgba720 = (Uint8 *)malloc(1280u * 720u * 4u);
    Uint8 *pack_out = (Uint8 *)malloc(1280u * 720u * 3u);
    SDL_Rect r720 = { 0, 0, 1280, 720 };
    SDL_Rect r1080 = { 0, 0, 1920, 1080 };
    SDL_Rect dirty;
    volatile Uint64 sink = 0;

    fill_scene((Uint32 *)s720->pixels, 1280, 720, 0);
    fill_scene((Uint32 *)s1080->pixels, 1920, 1080, 0);
    fill_scene((Uint32 *)s720a->pixels, 1280, 720, 0);

    printf("MICROBENCHMARKS (median over %d runs)\n", ITERS);

    /* 1. conv_rgb_720p */
    KITTY_ConvertSurfaceRectToRGB(s720, &r720, rgb720_a);
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        KITTY_ConvertSurfaceRectToRGB(s720, &r720, rgb720_a);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.conv_rgb_720p", median_of(samples, ITERS));

    /* 2. conv_rgb_1080p */
    KITTY_ConvertSurfaceRectToRGB(s1080, &r1080, rgb1080_a);
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        KITTY_ConvertSurfaceRectToRGB(s1080, &r1080, rgb1080_a);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.conv_rgb_1080p", median_of(samples, ITERS));

    /* 3. conv_rgba_720p */
    KITTY_ConvertSurfaceRectToRGBA(s720a, &r720, rgba720);
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        KITTY_ConvertSurfaceRectToRGBA(s720a, &r720, rgba720);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.conv_rgba_720p", median_of(samples, ITERS));

    /* 4. tile_diff_static_720p */
    memcpy(rgb720_b, rgb720_a, 1280u * 720u * 3u);
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        sink += (Uint64)KITTY_TileDiffBBox(rgb720_a, rgb720_b, 1280, 720, 3, 32, 0.45f, &dirty);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.tile_diff_static_720p", median_of(samples, ITERS));

    /* 5. tile_diff_sparse_720p */
    draw_sprite((Uint32 *)s720->pixels, 1280, 720, 500, 300, 64, 64, 0xFFFF4040u);
    KITTY_ConvertSurfaceRectToRGB(s720, &r720, rgb720_b);
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        sink += (Uint64)KITTY_TileDiffBBox(rgb720_a, rgb720_b, 1280, 720, 3, 32, 0.45f, &dirty);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.tile_diff_sparse_720p", median_of(samples, ITERS));

    /* 6. tile_diff_1080p_static */
    memcpy(rgb1080_b, rgb1080_a, 1920u * 1080u * 3u);
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        sink += (Uint64)KITTY_TileDiffBBox(rgb1080_a, rgb1080_b, 1920, 1080, 3, 32, 0.45f, &dirty);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.tile_diff_static_1080p", median_of(samples, ITERS));

    /* 7. fnv1a_720p & fast_hash_720p */
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        sink ^= KITTY_Fnv1a64(rgb720_a, 1280u * 720u * 3u);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.fnv1a_720p", median_of(samples, ITERS));
    for (it = 0; it < ITERS; ++it) {
        const Uint64 t0 = SDL_GetTicksNS();
        sink ^= KITTY_FastHash64(rgb720_a, 1280u * 720u * 3u);
        samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
    }
    printf("  %-28s %8.1f us\n", "micro.fast_hash_720p", median_of(samples, ITERS));

    /* 8. pack_rgb_band & subrect */
    {
        SDL_Rect band = { 0, 120, 1280, 160 };
        SDL_Rect sub = { 200, 180, 320, 240 };
        size_t olen = 0;
        for (it = 0; it < ITERS; ++it) {
            const Uint64 t0 = SDL_GetTicksNS();
            KITTY_PackRGBRect(rgb720_a, 1280, 720, &band, pack_out, 1280u * 720u * 3u, &olen);
            samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
        }
        printf("  %-28s %8.1f us\n", "micro.pack_rgb_band", median_of(samples, ITERS));
        for (it = 0; it < ITERS; ++it) {
            const Uint64 t0 = SDL_GetTicksNS();
            KITTY_PackRGBRect(rgb720_a, 1280, 720, &sub, pack_out, 1280u * 720u * 3u, &olen);
            samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
        }
        printf("  %-28s %8.1f us\n", "micro.pack_rgb_subrect", median_of(samples, ITERS));
    }

    /* 9. coalesce_24 */
    {
        SDL_Rect in[24], out[24];
        int k;
        for (k = 0; k < 24; ++k) {
            in[k].x = (k * 47) % 1100;
            in[k].y = (k * 29) % 620;
            in[k].w = 48;
            in[k].h = 36;
        }
        for (it = 0; it < ITERS; ++it) {
            const Uint64 t0 = SDL_GetTicksNS();
            sink += (Uint64)KITTY_CoalesceRects(1280, 720, in, 24, 4, out, 24);
            samples[it] = (double)(SDL_GetTicksNS() - t0) / 1e3;
        }
        printf("  %-28s %8.1f us\n", "micro.coalesce_24", median_of(samples, ITERS));
    }

    (void)sink;
    free(pack_out);
    free(rgba720);
    free(rgb1080_b);
    free(rgb1080_a);
    free(rgb720_b);
    free(rgb720_a);
    SDL_DestroySurface(s720a);
    SDL_DestroySurface(s1080);
    SDL_DestroySurface(s720);
}

/* ---------------- End-to-End PTY Mock Host ---------------- */

typedef struct MockHost {
    int master_fd;
    int px_w;
    int px_h;
    int cell_w;
    int cell_h;
    atomic_bool stop;
    atomic_size_t wire_bytes;
    atomic_bool counting;
    char term_xtversion[64];
} MockHost;

/* Minimal base64 decode for unlinking SHM names sent by the driver. */
static int b64_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64_decode(const char *in, size_t len, char *out, size_t cap)
{
    size_t i = 0, o = 0;
    while (i + 3 < len && o + 3 < cap) {
        int a = b64_val((unsigned char)in[i]);
        int b = b64_val((unsigned char)in[i + 1]);
        int c = (in[i + 2] == '=') ? 0 : b64_val((unsigned char)in[i + 2]);
        int d = (in[i + 3] == '=') ? 0 : b64_val((unsigned char)in[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) break;
        out[o++] = (char)((a << 2) | (b >> 4));
        if (in[i + 2] != '=') out[o++] = (char)(((b & 15) << 4) | (c >> 2));
        if (in[i + 3] != '=') out[o++] = (char)(((c & 3) << 6) | d);
        i += 4;
    }
    out[o] = '\0';
    return o;
}

static void reply_str(int fd, const char *s)
{
    const size_t len = strlen(s);
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, s + off, len - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
}

static void handle_apc_payload(MockHost *h, const char *apc, size_t len)
{
    /* apc starts right after "\033_G" and ends before "\033\\" */
    const char *semi = (const char *)memchr(apc, ';', len);
    const size_t ctrl_len = semi ? (size_t)(semi - apc) : len;
    char ctrl[256];
    const size_t cl = ctrl_len < sizeof(ctrl) - 1 ? ctrl_len : sizeof(ctrl) - 1;
    memcpy(ctrl, apc, cl);
    ctrl[cl] = '\0';

    /* If t=s, unlink the SHM object just like a real terminal does! */
    if (strstr(ctrl, "t=s") && semi) {
        const char *pay = semi + 1;
        const size_t plen = len - (size_t)(pay - apc);
        char shm_name[128];
        if (plen > 0 && plen < 120 && b64_decode(pay, plen, shm_name, sizeof(shm_name)) > 0) {
            if (shm_name[0] == '/') {
                shm_unlink(shm_name);
            }
        }
    }

    /* Reply OK to a=q probes and non-quiet a=f probes (without q=2). */
    if (strstr(ctrl, "a=q") || (strstr(ctrl, "a=f") && !strstr(ctrl, "q=2"))) {
        const char *ip = strstr(ctrl, "i=");
        if (ip) {
            unsigned id = (unsigned)strtoul(ip + 2, NULL, 10);
            char resp[64];
            snprintf(resp, sizeof(resp), "\033_Gi=%u;OK\033\\", id);
            reply_str(h->master_fd, resp);
        }
    }
}

static void *mock_host_thread(void *arg)
{
    MockHost *h = (MockHost *)arg;
    unsigned char buf[65536];
    char apc_buf[1024];
    size_t apc_len = 0;
    int in_apc = 0;
    int apc_skip_payload = 0;
    unsigned char prev = 0;

    while (!atomic_load(&h->stop)) {
        struct pollfd pfd;
        pfd.fd = h->master_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 10) <= 0) {
            continue;
        }
        ssize_t n = read(h->master_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            break;
        }
        if (atomic_load(&h->counting)) {
            atomic_fetch_add(&h->wire_bytes, (size_t)n);
        }

        /* Lightweight scan for startup queries & APCs (while never slowing wire drain). */
        for (ssize_t i = 0; i < n; ++i) {
            unsigned char c = buf[i];
            if (in_apc) {
                if (prev == 0x1b && c == '\\') {
                    if (apc_len > 0) apc_len--; /* drop trailing ESC */
                    if (!apc_skip_payload && apc_len >= 1 && apc_buf[0] == 'G') {
                        handle_apc_payload(h, apc_buf + 1, apc_len - 1);
                    }
                    in_apc = 0;
                    apc_len = 0;
                    apc_skip_payload = 0;
                    prev = 0;
                    continue;
                }
                if (apc_len + 1 < sizeof(apc_buf)) {
                    apc_buf[apc_len++] = (char)c;
                } else {
                    apc_skip_payload = 1;
                }
                prev = c;
                continue;
            }
            if (prev == 0x1b && c == '_') {
                in_apc = 1;
                apc_len = 0;
                apc_skip_payload = 0;
                prev = 0;
                continue;
            }
            /* Only scan CSI queries during init (when not counting frames). */
            if (!atomic_load(&h->counting) && prev == 0x1b && c == '[') {
                size_t rem = (size_t)(n - i);
                const char *s = (const char *)(buf + i);
                if (rem >= 4 && memcmp(s, "[16t", 4) == 0) {
                    char r[64];
                    snprintf(r, sizeof(r), "\033[6;%d;%dt", h->cell_h, h->cell_w);
                    reply_str(h->master_fd, r);
                } else if (rem >= 4 && memcmp(s, "[14t", 4) == 0) {
                    char r[64];
                    snprintf(r, sizeof(r), "\033[4;%d;%dt", h->px_h, h->px_w);
                    reply_str(h->master_fd, r);
                } else if (rem >= 4 && memcmp(s, "[>0q", 4) == 0) {
                    char r[128];
                    snprintf(r, sizeof(r), "\033P>|%s\033\\\033[?62;22c", h->term_xtversion);
                    reply_str(h->master_fd, r);
                } else if (rem >= 3 && memcmp(s, "[?u", 3) == 0) {
                    reply_str(h->master_fd, "\033[?27u");
                } else if (rem >= 8 && memcmp(s, "[?1016$p", 8) == 0) {
                    reply_str(h->master_fd, "\033[?1016;2$y");
                } else if (rem >= 2 && memcmp(s, "[c", 2) == 0) {
                    reply_str(h->master_fd, "\033[?62;22c");
                }
            }
            prev = c;
        }
    }
    return NULL;
}

typedef enum E2ECase {
    E2E_STATIC_720P,
    E2E_MAME_SPARSE_720P,
    E2E_MAME_2CORNER_720P,
    E2E_MAME_SCROLL_720P_ZLIB,
    E2E_MAME_SCROLL_720P_DIRECT,
    E2E_MAME_SCROLL_1080P_ZLIB,
    E2E_IMGUI_DIRTY_720P_AF,
    E2E_FIREWORKS_RGBA_720P
} E2ECase;

static void run_e2e_case(const char *label, E2ECase kind, int w, int h,
                         const char *transmit, const char *af_zlib, const char *fb_alpha,
                         const char *term_prog, const char *xtver)
{
    int master_fd = -1, slave_fd = -1;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)(w / 10);
    ws.ws_row = (unsigned short)(h / 20);
    ws.ws_xpixel = (unsigned short)w;
    ws.ws_ypixel = (unsigned short)h;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) != 0) {
        fprintf(stderr, "openpty failed: %s\n", strerror(errno));
        return;
    }

    int saved_in = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);
    close(slave_fd);

    MockHost host;
    memset(&host, 0, sizeof(host));
    host.master_fd = master_fd;
    host.px_w = w;
    host.px_h = h;
    host.cell_w = 10;
    host.cell_h = 20;
    atomic_store(&host.stop, false);
    atomic_store(&host.wire_bytes, 0);
    atomic_store(&host.counting, false);
    snprintf(host.term_xtversion, sizeof(host.term_xtversion), "%s", xtver);

    pthread_t thr;
    pthread_create(&thr, NULL, mock_host_thread, &host);

    SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "kitty", 1);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    SDL_setenv_unsafe("TERM", "xterm-256color", 1);
    SDL_setenv_unsafe("TERM_PROGRAM", term_prog, 1);
    SDL_unsetenv_unsafe("TERM_PROGRAM_VERSION");
    SDL_setenv_unsafe("SDL_KITTY_MOUSE", "0", 1);
    char fb_str[32];
    snprintf(fb_str, sizeof(fb_str), "%dx%d", w, h);
    SDL_setenv_unsafe("SDL_KITTY_FB_FIXED", fb_str, 1);
    if (transmit) SDL_setenv_unsafe("SDL_KITTY_TRANSMIT", transmit, 1); else SDL_unsetenv_unsafe("SDL_KITTY_TRANSMIT");
    if (af_zlib) SDL_setenv_unsafe("SDL_KITTY_AF_ZLIB", af_zlib, 1); else SDL_unsetenv_unsafe("SDL_KITTY_AF_ZLIB");
    if (fb_alpha) SDL_setenv_unsafe("SDL_KITTY_FB_ALPHA", fb_alpha, 1); else SDL_unsetenv_unsafe("SDL_KITTY_FB_ALPHA");
    SDL_setenv_unsafe("SDL_KITTY_DIRTY_BW", "1", 1);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        dprintf(saved_out, "  %-30s INIT FAILED: %s\n", label, SDL_GetError());
        goto cleanup;
    }

    SDL_Window *win = SDL_CreateWindow("bench", w, h, 0);
    SDL_Surface *surf = SDL_GetWindowSurface(win);
    if (!surf) {
        dprintf(saved_out, "  %-30s SURFACE FAILED: %s\n", label, SDL_GetError());
        SDL_Quit();
        goto cleanup;
    }

    /* Prime frame 0 */
    fill_scene((Uint32 *)surf->pixels, surf->w, surf->h, 0);
    SDL_UpdateWindowSurface(win);
    SDL_Delay(20);

    /* Warmup + measure */
    {
        enum { WARMUP = 5, FRAMES = 31 };
        double samples[FRAMES];
        int f;

        for (f = 0; f < WARMUP + FRAMES; ++f) {
            SDL_Rect rects[8];
            int nrects = 0;

            if (f == WARMUP) {
                atomic_store(&host.wire_bytes, 0);
                atomic_store(&host.counting, true);
            }

            switch (kind) {
            case E2E_STATIC_720P:
                break;
            case E2E_MAME_SPARSE_720P:
                /* Erase previous sprite, draw next */
                draw_sprite((Uint32 *)surf->pixels, surf->w, surf->h, 200 + ((f - 1) & 15) * 16, 280, 64, 64, 0xFF103050u);
                draw_sprite((Uint32 *)surf->pixels, surf->w, surf->h, 200 + (f & 15) * 16, 280, 64, 64, 0xFFFF4040u);
                break;
            case E2E_MAME_2CORNER_720P:
                /* Top-left HUD score + bottom-right player sprite */
                draw_sprite((Uint32 *)surf->pixels, surf->w, surf->h, 16, 8, 48, 20, 0xFFE8D040u + (Uint32)(f * 17));
                draw_sprite((Uint32 *)surf->pixels, surf->w, surf->h, surf->w - 96, surf->h - 80, 48, 48, 0xFF40E880u + (Uint32)(f * 31));
                break;
            case E2E_MAME_SCROLL_720P_ZLIB:
            case E2E_MAME_SCROLL_720P_DIRECT:
            case E2E_MAME_SCROLL_1080P_ZLIB:
                fill_scene((Uint32 *)surf->pixels, surf->w, surf->h, f * 3);
                break;
            case E2E_IMGUI_DIRTY_720P_AF:
                nrects = 6;
                for (int r = 0; r < 6; ++r) {
                    rects[r].x = 40 + r * 180;
                    rects[r].y = 80 + ((r * 73) % 400);
                    rects[r].w = 96;
                    rects[r].h = 32;
                    draw_sprite((Uint32 *)surf->pixels, surf->w, surf->h,
                                rects[r].x, rects[r].y, rects[r].w, rects[r].h,
                                0xFF60A0E0u + (Uint32)(f * 19 + r * 7));
                }
                break;
            case E2E_FIREWORKS_RGBA_720P:
                nrects = 8;
                for (int r = 0; r < 8; ++r) {
                    rects[r].x = 60 + ((r * 149 + f * 7) % (surf->w - 100));
                    rects[r].y = 60 + ((r * 97 + f * 5) % (surf->h - 100));
                    rects[r].w = 48;
                    rects[r].h = 48;
                    draw_sprite((Uint32 *)surf->pixels, surf->w, surf->h,
                                rects[r].x, rects[r].y, rects[r].w, rects[r].h,
                                0xC0FF6020u + (Uint32)(f * 23 + r * 11));
                }
                break;
            }

            const Uint64 t0 = SDL_GetTicksNS();
            bool ok_upd;
            if (nrects > 0) {
                ok_upd = SDL_UpdateWindowSurfaceRects(win, rects, nrects);
            } else {
                ok_upd = SDL_UpdateWindowSurface(win);
            }
            const double us = (double)(SDL_GetTicksNS() - t0) / 1e3;
            if (!ok_upd && f == WARMUP) {
                dprintf(saved_out, "  %-30s UPDATE ERR: %s\n", label, SDL_GetError());
            }
            if (f >= WARMUP) {
                samples[f - WARMUP] = us;
            }
        }
        SDL_Delay(15); /* let reader drain last frame bytes */
        atomic_store(&host.counting, false);
        const size_t total_wire = atomic_load(&host.wire_bytes);
        const double med_ms = median_of(samples, FRAMES) / 1000.0;
        const double kb_per_frame = (double)total_wire / (double)FRAMES / 1024.0;
        dprintf(saved_out, "  %-30s %7.3f ms/f   %8.1f KB/f\n", label, med_ms, kb_per_frame);
    }

    SDL_DestroyWindow(win);
    SDL_Quit();

cleanup:
    atomic_store(&host.stop, true);
    dup2(saved_in, STDIN_FILENO);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    pthread_join(thr, NULL);
    close(master_fd);
}

int main(int argc, char **argv)
{
    int only_micro = 0;
    int only_e2e = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--micro") == 0) only_micro = 1;
        if (strcmp(argv[i], "--e2e") == 0) only_e2e = 1;
    }

    if (!only_e2e) {
        run_microbenchmarks();
    }
    if (!only_micro) {
        printf("\nEND-TO-END PTY BENCHMARKS (median ms/frame, avg wire KB/frame)\n");
        fflush(stdout);
        run_e2e_case("e2e.static_720p",             E2E_STATIC_720P,             1280, 720,  "zlib",   "1", "0", "rex",     "rex 0.1.0");
        run_e2e_case("e2e.mame_sparse_720p_af_z",   E2E_MAME_SPARSE_720P,        1280, 720,  "zlib",   "1", "0", "rex",     "rex 0.1.0");
        run_e2e_case("e2e.mame_2corner_720p_af_z",  E2E_MAME_2CORNER_720P,       1280, 720,  "zlib",   "1", "0", "rex",     "rex 0.1.0");
        run_e2e_case("e2e.mame_scroll_720p_zlib",   E2E_MAME_SCROLL_720P_ZLIB,   1280, 720,  "zlib",   "1", "0", "rex",     "rex 0.1.0");
        run_e2e_case("e2e.mame_scroll_720p_direct", E2E_MAME_SCROLL_720P_DIRECT, 1280, 720,  "direct", "0", "0", "rex",     "rex 0.1.0");
        run_e2e_case("e2e.mame_scroll_1080p_zlib",  E2E_MAME_SCROLL_1080P_ZLIB,  1920, 1080, "zlib",   "1", "0", "ghostty", "ghostty 1.4.0");
        run_e2e_case("e2e.imgui_dirty_720p_af_z",   E2E_IMGUI_DIRTY_720P_AF,     1280, 720,  "zlib",   "1", "0", "rex",     "rex 0.1.0");
        run_e2e_case("e2e.fireworks_rgba_720p_af",  E2E_FIREWORKS_RGBA_720P,     1280, 720,  "zlib",   "1", "1", "rex",     "rex 0.1.0");
    }
    return 0;
}
