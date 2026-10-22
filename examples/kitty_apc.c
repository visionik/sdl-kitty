#include "kitty_apc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KITTY_APC_CHUNK 4096

bool kitty_apc_write(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        const ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

static size_t kitty_b64(const uint8_t *in, size_t in_len, char *out, size_t out_cap)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i < in_len) {
        const size_t remain = in_len - i;
        const uint32_t b0 = in[i++];
        const uint32_t b1 = (remain > 1) ? in[i++] : 0;
        const uint32_t b2 = (remain > 2) ? in[i++] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        if (o + 4 >= out_cap) {
            return 0;
        }
        out[o++] = tbl[(triple >> 18) & 63];
        out[o++] = tbl[(triple >> 12) & 63];
        out[o++] = (remain > 1) ? tbl[(triple >> 6) & 63] : '=';
        out[o++] = (remain > 2) ? tbl[triple & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

bool kitty_apc_transmit_rgb(int fd, uint32_t image_id, const uint8_t *rgb, int w, int h)
{
    const size_t payload_len = (size_t)w * (size_t)h * 3u;
    size_t b64_len;
    char *b64;
    size_t encoded;
    size_t offset = 0;
    bool first = true;

    if (fd < 0 || !rgb || image_id == 0 || w <= 0 || h <= 0) {
        return false;
    }
    b64_len = ((payload_len + 2) / 3) * 4;
    b64 = (char *)malloc(b64_len + 1);
    if (!b64) {
        return false;
    }
    encoded = kitty_b64(rgb, payload_len, b64, b64_len + 1);
    if (encoded == 0) {
        free(b64);
        return false;
    }

    while (offset < encoded) {
        size_t chunk = encoded - offset;
        char header[160];
        int header_len;
        int more;
        if (chunk > KITTY_APC_CHUNK) {
            chunk = KITTY_APC_CHUNK;
        }
        more = (offset + chunk < encoded) ? 1 : 0;
        if (first) {
            if (more) {
                header_len = snprintf(header, sizeof(header),
                                      "\033_Ga=t,f=24,t=d,i=%u,s=%d,v=%d,m=1;",
                                      (unsigned)image_id, w, h);
            } else {
                header_len = snprintf(header, sizeof(header),
                                      "\033_Ga=t,f=24,t=d,i=%u,s=%d,v=%d;",
                                      (unsigned)image_id, w, h);
            }
            first = false;
        } else {
            header_len = snprintf(header, sizeof(header), "\033_Gm=%d;", more);
        }
        if (header_len <= 0 ||
            !kitty_apc_write(fd, header, (size_t)header_len) ||
            !kitty_apc_write(fd, b64 + offset, chunk) ||
            !kitty_apc_write(fd, "\033\\", 2)) {
            free(b64);
            return false;
        }
        offset += chunk;
    }
    free(b64);
    return true;
}

bool kitty_apc_place_crop(int fd, uint32_t image_id, uint32_t placement_id,
                          int src_x, int src_y, int src_w, int src_h,
                          int place_cols, int place_rows,
                          int off_x, int off_y)
{
    char cmd[288];
    int n;
    char crop[80];
    char stretch[40];
    char xy[40];

    crop[0] = '\0';
    stretch[0] = '\0';
    xy[0] = '\0';
    if (src_w > 0 && src_h > 0) {
        snprintf(crop, sizeof(crop), ",x=%d,y=%d,w=%d,h=%d", src_x, src_y, src_w, src_h);
    }
    if (place_cols > 0 && place_rows > 0) {
        snprintf(stretch, sizeof(stretch), ",c=%d,r=%d", place_cols, place_rows);
    }
    if (off_x > 0 || off_y > 0) {
        snprintf(xy, sizeof(xy), ",X=%d,Y=%d", off_x, off_y);
    }

    n = snprintf(cmd, sizeof(cmd),
                 "\033_Ga=p,i=%u,p=%u,C=1,q=2%s%s%s\033\\",
                 (unsigned)image_id, (unsigned)placement_id, crop, stretch, xy);
    if (n <= 0) {
        return false;
    }
    return kitty_apc_write(fd, cmd, (size_t)n);
}

bool kitty_apc_delete_image(int fd, uint32_t image_id)
{
    char cmd[64];
    const int n = snprintf(cmd, sizeof(cmd), "\033_Ga=d,d=I,i=%u,q=2\033\\", (unsigned)image_id);
    return n > 0 && kitty_apc_write(fd, cmd, (size_t)n);
}

bool kitty_apc_delete_placement(int fd, uint32_t image_id, uint32_t placement_id)
{
    char cmd[80];
    const int n = snprintf(cmd, sizeof(cmd),
                           "\033_Ga=d,d=i,i=%u,p=%u,q=2\033\\",
                           (unsigned)image_id, (unsigned)placement_id);
    return n > 0 && kitty_apc_write(fd, cmd, (size_t)n);
}

bool kitty_apc_cup(int fd, int col, int row)
{
    char cmd[32];
    const int n = snprintf(cmd, sizeof(cmd), "\033[%d;%dH", row, col);
    return n > 0 && kitty_apc_write(fd, cmd, (size_t)n);
}
