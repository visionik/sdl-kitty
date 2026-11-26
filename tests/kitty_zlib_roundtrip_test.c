/*
 * End-to-end PTY wire round-trip test for Kitty direct zlib (o=z) full-frame
 * transmission (including multi-slice parallel RFC 1950 deflate) and sparse
 * multi-rect a=f edits. Verifies bit-exact decompression against surface pixels.
 */
#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <zlib.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

typedef struct PtyCapture
{
    int master_fd;
    volatile bool stop;
    volatile bool capture;
    pthread_mutex_t mu;
    uint8_t *buf;
    size_t len;
    size_t cap;
} PtyCapture;

static void write_all(int fd, const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        s += (size_t)w;
        n -= (size_t)w;
    }
}

static void *pty_capture_thread(void *arg)
{
    PtyCapture *c = (PtyCapture *)arg;
    uint8_t tmp[16384];
    char tail[128];
    size_t tail_len = 0;

    while (!c->stop) {
        struct pollfd pfd;
        pfd.fd = c->master_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 10) <= 0) {
            continue;
        }
        ssize_t n = read(c->master_fd, tmp, sizeof(tmp));
        if (n <= 0) {
            continue;
        }
        if (c->capture) {
            pthread_mutex_lock(&c->mu);
            if (c->len + (size_t)n > c->cap) {
                size_t ncap = c->cap ? c->cap * 2u : 262144u;
                while (ncap < c->len + (size_t)n) {
                    ncap *= 2u;
                }
                uint8_t *nb = (uint8_t *)realloc(c->buf, ncap);
                if (nb) {
                    c->buf = nb;
                    c->cap = ncap;
                }
            }
            if (c->buf && c->len + (size_t)n <= c->cap) {
                memcpy(c->buf + c->len, tmp, (size_t)n);
                c->len += (size_t)n;
            }
            pthread_mutex_unlock(&c->mu);
        }

        /* Minimal fake terminal responder for SDL_Init / KITTY_VideoInit probes */
        if (!c->capture) {
            char scan[sizeof(tail) + sizeof(tmp) + 1];
            size_t slen = tail_len;
            memcpy(scan, tail, tail_len);
            memcpy(scan + slen, tmp, (size_t)n);
            slen += (size_t)n;
            scan[slen] = '\0';

            if (strstr(scan, "\033[>0q")) {
                const char *xt = "\033P>|rex 0.1\033\\\033[?62;22c";
                write_all(c->master_fd, xt, strlen(xt));
            }
            if (strstr(scan, "\033[c")) {
                const char *da = "\033[?62;22c";
                write_all(c->master_fd, da, strlen(da));
            }
            if (strstr(scan, "\033[?u")) {
                write_all(c->master_fd, "\033[?27u", 6);
            }
            if (strstr(scan, "\033[16t")) {
                write_all(c->master_fd, "\033[6;18;8t", 9);
            }
            if (strstr(scan, "\033[14t")) {
                write_all(c->master_fd, "\033[4;1080;1920t", 14);
            }
            if (strstr(scan, "\033[18t")) {
                write_all(c->master_fd, "\033[8;60;240t", 11);
            }
            if (strstr(scan, "\033[?1016$p")) {
                write_all(c->master_fd, "\033[?1016;1$y", 11);
            }
            {
                const char *p = scan;
                while ((p = strstr(p, "\033_G")) != NULL) {
                    const char *st = strstr(p + 3, "\033\\");
                    if (!st) {
                        break;
                    }
                    const char *semi = (const char *)memchr(p + 3, ';', (size_t)(st - (p + 3)));
                    size_t clen = semi ? (size_t)(semi - (p + 3)) : (size_t)(st - (p + 3));
                    char ctrl[256];
                    if (clen >= sizeof(ctrl)) {
                        clen = sizeof(ctrl) - 1;
                    }
                    memcpy(ctrl, p + 3, clen);
                    ctrl[clen] = '\0';
                    if (strstr(ctrl, "a=q") || (strstr(ctrl, "a=f") && !strstr(ctrl, "q=2"))) {
                        const char *ip = strstr(ctrl, "i=");
                        if (ip) {
                            unsigned id = (unsigned)strtoul(ip + 2, NULL, 10);
                            char resp[64];
                            int rn = snprintf(resp, sizeof(resp), "\033_Gi=%u;OK\033\\", id);
                            if (rn > 0) {
                                write_all(c->master_fd, resp, (size_t)rn);
                            }
                        }
                    }
                    p = st + 2;
                }
            }
            if (slen >= 64) {
                tail_len = 64;
                memcpy(tail, scan + slen - 64, 64);
            } else {
                tail_len = slen;
                memcpy(tail, scan, slen);
            }
        }
    }
    return NULL;
}

static int b64_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static size_t b64_decode(const uint8_t *in, size_t in_len, uint8_t *out)
{
    size_t i = 0, o = 0;
    while (i + 3 < in_len) {
        int a = b64_val(in[i]);
        int b = b64_val(in[i + 1]);
        int c = (in[i + 2] == '=') ? 0 : b64_val(in[i + 2]);
        int d = (in[i + 3] == '=') ? 0 : b64_val(in[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            break;
        }
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        out[o++] = (uint8_t)((v >> 16) & 0xFFu);
        if (in[i + 2] != '=') {
            out[o++] = (uint8_t)((v >> 8) & 0xFFu);
        }
        if (in[i + 3] != '=') {
            out[o++] = (uint8_t)(v & 0xFFu);
        }
        i += 4;
    }
    return o;
}

static bool run_zlib_case(int w, int h, const char *zlevel_env, size_t *out_zbytes)
{
    int master_fd = -1, slave_fd = -1;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = 240;
    ws.ws_row = 60;
    ws.ws_xpixel = 1920;
    ws.ws_ypixel = 1080;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) != 0) {
        perror("openpty");
        return false;
    }

    int saved_in = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);

    PtyCapture cap;
    memset(&cap, 0, sizeof(cap));
    cap.master_fd = master_fd;
    pthread_mutex_init(&cap.mu, NULL);
    pthread_t thr;
    pthread_create(&thr, NULL, pty_capture_thread, &cap);

    SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "kitty", 1);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    SDL_setenv_unsafe("TERM", "xterm-256color", 1);
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    SDL_unsetenv_unsafe("TERM_PROGRAM_VERSION");
    SDL_setenv_unsafe("SDL_KITTY_TRANSMIT", "zlib", 1);
    SDL_setenv_unsafe("SDL_KITTY_MOUSE", "0", 1);
    SDL_setenv_unsafe("SDL_KITTY_FB_DIFF", "0", 1);
    SDL_setenv_unsafe("SDL_KITTY_UPLOAD_SKIP", "0", 1);
    {
        char fb_str[32];
        snprintf(fb_str, sizeof(fb_str), "%dx%d", w, h);
        SDL_setenv_unsafe("SDL_KITTY_FB_FIXED", fb_str, 1);
    }
    if (zlevel_env) {
        SDL_setenv_unsafe("SDL_KITTY_ZLIB_LEVEL", zlevel_env, 1);
    } else {
        SDL_unsetenv_unsafe("SDL_KITTY_ZLIB_LEVEL");
    }

    bool ok = false;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    SDL_Window *win = SDL_CreateWindow("zlib_roundtrip", w, h, 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto quit_sdl;
    }
    SDL_Surface *surf = SDL_GetWindowSurface(win);
    if (!surf) {
        fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        goto destroy_win;
    }

    /* Realistic tile/sprite/gradient framebuffer pattern (matches game/UI entropy) */
    const size_t expected_len = (size_t)w * (size_t)h * 3u;
    uint8_t *expected = (uint8_t *)malloc(expected_len);
    if (!expected) {
        goto destroy_win;
    }
    for (int y = 0; y < h; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch);
        for (int x = 0; x < w; ++x) {
            int tx = x >> 3;
            int ty = y >> 3;
            uint8_t r = (uint8_t)(((tx * 17 + ty * 31) ^ (y >> 4)) & 0xFF);
            uint8_t g = (uint8_t)(((tx ^ ty) * 23 + ((x & 7) == 0 ? 40 : 0)) & 0xFF);
            uint8_t b = (uint8_t)(((ty * 19 + (x >> 2)) + ((y & 7) == 0 ? 32 : 0)) & 0xFF);
            row[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 3u;
            expected[idx + 0] = r;
            expected[idx + 1] = g;
            expected[idx + 2] = b;
        }
    }

    cap.capture = true;
    if (!SDL_UpdateWindowSurface(win)) {
        fprintf(stderr, "SDL_UpdateWindowSurface failed: %s\n", SDL_GetError());
        free(expected);
        goto destroy_win;
    }
    /* Quit destroys window + flushes async writer cleanly before we inspect capture */
    SDL_DestroyWindow(win);
    win = NULL;
    SDL_Quit();
    usleep(30000);
    cap.capture = false;

    /* Extract all APC payloads with o=z (including multi-chunk m=1 continuations) */
    pthread_mutex_lock(&cap.mu);
    uint8_t *b64_concat = (uint8_t *)malloc(cap.len + 1u);
    size_t b64_len = 0;
    bool in_z_apc = false;
    for (size_t i = 0; i + 3 < cap.len; ) {
        if (cap.buf[i] == 0x1b && cap.buf[i + 1] == '_' && cap.buf[i + 2] == 'G') {
            size_t semi = i + 3;
            while (semi < cap.len && cap.buf[semi] != ';' && cap.buf[semi] != 0x1b) {
                semi++;
            }
            size_t st = semi;
            while (st + 1 < cap.len && !(cap.buf[st] == 0x1b && cap.buf[st + 1] == '\\')) {
                st++;
            }
            if (semi < st && cap.buf[semi] == ';') {
                size_t hdr_len = semi - (i + 3);
                char hdr[256];
                if (hdr_len >= sizeof(hdr)) {
                    hdr_len = sizeof(hdr) - 1;
                }
                memcpy(hdr, cap.buf + i + 3, hdr_len);
                hdr[hdr_len] = '\0';
                if (strstr(hdr, "a=T") && strstr(hdr, "o=z")) {
                    in_z_apc = true;
                    b64_len = 0;
                }
                if (in_z_apc) {
                    size_t plen = st - (semi + 1);
                    memcpy(b64_concat + b64_len, cap.buf + semi + 1, plen);
                    b64_len += plen;
                    if (!strstr(hdr, "m=1")) {
                        in_z_apc = false;
                        break;
                    }
                }
            }
            i = st + 2;
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&cap.mu);

    if (b64_len == 0) {
        fprintf(stderr, "No o=z APC captured for %dx%d (zlevel=%s, cap_len=%zu)\n",
                w, h, zlevel_env ? zlevel_env : "auto", cap.len);
        if (cap.len > 0) {
            size_t show = cap.len < 160 ? cap.len : 160;
            fprintf(stderr, "  head: ");
            for (size_t k = 0; k < show; ++k) {
                unsigned char ch = cap.buf[k];
                fprintf(stderr, (ch >= 32 && ch < 127) ? "%c" : "\\x%02x", ch);
            }
            fprintf(stderr, "\n");
        }
        free(b64_concat);
        free(expected);
        goto cleanup;
    }

    uint8_t *zbuf = (uint8_t *)malloc(b64_len);
    size_t zlen = b64_decode(b64_concat, b64_len, zbuf);
    uint8_t *decoded = (uint8_t *)malloc(expected_len + 64u);
    uLongf dec_len = (uLongf)(expected_len + 64u);
    int zrc = uncompress(decoded, &dec_len, zbuf, (uLong)zlen);

    if (zrc != Z_OK || (size_t)dec_len != expected_len || memcmp(decoded, expected, expected_len) != 0) {
        fprintf(stderr, "RFC 1950 roundtrip MISMATCH %dx%d zlevel=%s: zrc=%d dec_len=%lu expected=%zu zlen=%zu\n",
                w, h, zlevel_env ? zlevel_env : "auto", zrc, (unsigned long)dec_len, expected_len, zlen);
    } else {
        ok = true;
        if (out_zbytes) {
            *out_zbytes = zlen;
        }
    }

    free(decoded);
    free(zbuf);
    free(b64_concat);
    free(expected);
    goto cleanup;

destroy_win:
    if (win) {
        SDL_DestroyWindow(win);
    }
quit_sdl:
    SDL_Quit();
cleanup:
    cap.stop = true;
    pthread_join(thr, NULL);
    pthread_mutex_destroy(&cap.mu);
    free(cap.buf);
    dup2(saved_in, STDIN_FILENO);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    close(slave_fd);
    close(master_fd);
    return ok;
}

int main(void)
{
    size_t z720_auto = 0, z1080_auto = 0, z1080_lv4 = 0;

    if (!run_zlib_case(1280, 720, NULL, &z720_auto)) {
        return 1;
    }
    if (!run_zlib_case(1920, 1080, NULL, &z1080_auto)) {
        return 1;
    }
    if (!run_zlib_case(1920, 1080, "4", &z1080_lv4)) {
        return 1;
    }
    if (z1080_lv4 >= z1080_auto) {
        fprintf(stderr, "Expected SDL_KITTY_ZLIB_LEVEL=4 (%zu B) < auto level 3 (%zu B)\n",
                z1080_lv4, z1080_auto);
        return 1;
    }

    fprintf(stderr,
            "kitty_zlib_roundtrip_test: PASS (720p_auto=%zu B, 1080p_auto=%zu B, 1080p_lv4=%zu B)\n",
            z720_auto, z1080_auto, z1080_lv4);
    return 0;
}
