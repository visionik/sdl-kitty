/* Unit tests for SDL_kittyrect helpers (Step 1a). No tty required. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define KITTY_RECT_STANDALONE 1
#include "../third_party/SDL/src/video/kitty/SDL_kittyrect.h"

static int failures;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    }
}

static void expect_int(int got, int want, const char *msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, got, want);
        failures++;
    }
}

static void expect_float_near(float got, float want, float eps, const char *msg)
{
    const float d = got > want ? got - want : want - got;
    if (d > eps) {
        fprintf(stderr, "FAIL: %s (got %f want %f)\n", msg, (double)got, (double)want);
        failures++;
    }
}

static void test_union_basic(void)
{
    SDL_Rect rects[2] = { { 10, 10, 20, 20 }, { 25, 25, 20, 20 } };
    SDL_Rect out;
    expect_true(KITTY_UnionRects(100, 100, rects, 2, &out), "union two rects");
    expect_int(out.x, 10, "union.x");
    expect_int(out.y, 10, "union.y");
    expect_int(out.w, 35, "union.w");
    expect_int(out.h, 35, "union.h");
}

static void test_union_clamp(void)
{
    SDL_Rect rects[1] = { { -10, -5, 30, 20 } };
    SDL_Rect out;
    expect_true(KITTY_UnionRects(100, 80, rects, 1, &out), "union clamp");
    expect_int(out.x, 0, "clamp.x");
    expect_int(out.y, 0, "clamp.y");
    expect_int(out.w, 20, "clamp.w");
    expect_int(out.h, 15, "clamp.h");
}

static void test_union_empty_list_is_full(void)
{
    SDL_Rect out;
    expect_true(KITTY_UnionRects(64, 48, NULL, 0, &out), "null rects -> full");
    expect_int(out.w, 64, "full.w");
    expect_int(out.h, 48, "full.h");
}

static void test_union_outside_fails(void)
{
    SDL_Rect rects[1] = { { 200, 200, 10, 10 } };
    SDL_Rect out;
    expect_true(!KITTY_UnionRects(100, 100, rects, 1, &out), "outside -> false");
}

static void test_coverage(void)
{
    SDL_Rect u = { 0, 0, 50, 50 };
    expect_float_near(KITTY_CoverageRatio(100, 100, &u), 0.25f, 0.001f, "25% coverage");
    u.w = 100;
    u.h = 100;
    expect_float_near(KITTY_CoverageRatio(100, 100, &u), 1.0f, 0.001f, "100% coverage");
}

static void test_choose_mode(void)
{
    SDL_Rect small = { 0, 0, 10, 10 };
    SDL_Rect dirty;
    KITTY_PresentConvertMode mode;

    SDL_unsetenv_unsafe("SDL_KITTY_DIRTY");
    mode = KITTY_ChooseConvertMode(100, 100, &small, 1, true, &dirty);
    expect_int((int)mode, (int)KITTY_CONVERT_DIRTY, "small dirty when valid scratch");
    expect_int(dirty.w, 10, "dirty.w");

    mode = KITTY_ChooseConvertMode(100, 100, &small, 1, false, &dirty);
    expect_int((int)mode, (int)KITTY_CONVERT_FULL, "invalid scratch -> full");

    {
        SDL_Rect big = { 0, 0, 80, 80 }; /* 64% — stay dirty for sparse a=f */
        mode = KITTY_ChooseConvertMode(100, 100, &big, 1, true, &dirty);
        expect_int((int)mode, (int)KITTY_CONVERT_DIRTY, "high coverage -> dirty");
        expect_int(dirty.w, 80, "big dirty.w");
    }

    {
        SDL_Rect whole = { 0, 0, 100, 100 };
        mode = KITTY_ChooseConvertMode(100, 100, &whole, 1, true, &dirty);
        expect_int((int)mode, (int)KITTY_CONVERT_DIRTY, "full-frame union -> dirty");
    }

    SDL_setenv_unsafe("SDL_KITTY_DIRTY", "0", 1);
    mode = KITTY_ChooseConvertMode(100, 100, &small, 1, true, &dirty);
    expect_int((int)mode, (int)KITTY_CONVERT_FULL, "SDL_KITTY_DIRTY=0 -> full");
    SDL_unsetenv_unsafe("SDL_KITTY_DIRTY");
}

static void test_pack_rgb_rect(void)
{
    Uint8 scratch[4 * 4 * 3];
    Uint8 out[2 * 2 * 3];
    size_t out_len = 0;
    SDL_Rect rect = { 1, 1, 2, 2 };
    int i;

    memset(scratch, 0, sizeof(scratch));
    /* Put distinct RGB at (1,1),(2,1),(1,2),(2,2) */
    scratch[(1 * 4 + 1) * 3 + 0] = 10;
    scratch[(1 * 4 + 1) * 3 + 1] = 20;
    scratch[(1 * 4 + 1) * 3 + 2] = 30;
    scratch[(1 * 4 + 2) * 3 + 0] = 11;
    scratch[(1 * 4 + 2) * 3 + 1] = 21;
    scratch[(1 * 4 + 2) * 3 + 2] = 31;
    scratch[(2 * 4 + 1) * 3 + 0] = 12;
    scratch[(2 * 4 + 1) * 3 + 1] = 22;
    scratch[(2 * 4 + 1) * 3 + 2] = 32;
    scratch[(2 * 4 + 2) * 3 + 0] = 13;
    scratch[(2 * 4 + 2) * 3 + 1] = 23;
    scratch[(2 * 4 + 2) * 3 + 2] = 33;

    expect_true(KITTY_PackRGBRect(scratch, 4, 4, &rect, out, sizeof(out), &out_len), "pack ok");
    expect_int((int)out_len, 12, "pack len 2x2x3");
    expect_int(out[0], 10, "pack[0]");
    expect_int(out[3], 11, "pack[3]");
    expect_int(out[6], 12, "pack[6]");
    expect_int(out[9], 13, "pack[9]");
    for (i = 0; i < 12; ++i) {
        /* silence unused if expects pass */
    }
}

static void test_convert_preserves_outside(void)
{
    SDL_Surface *surf = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_XRGB8888);
    Uint8 *scratch;
    SDL_Rect rect = { 2, 2, 2, 2 };
    int i;
    expect_true(surf != NULL, "create surface");
    scratch = (Uint8 *)calloc(8u * 8u * 3u, 1);
    expect_true(scratch != NULL, "scratch alloc");

    /* Fill scratch with 0x11 marker, surface with red in dirty rect only. */
    memset(scratch, 0x11, 8u * 8u * 3u);
    SDL_FillSurfaceRect(surf, NULL, SDL_MapSurfaceRGB(surf, 0, 0, 0));
    SDL_FillSurfaceRect(surf, &rect, SDL_MapSurfaceRGB(surf, 255, 0, 0));

    expect_true(KITTY_ConvertSurfaceRectToRGB(surf, &rect, scratch), "convert dirty");

    /* Outside (0,0) still marker */
    expect_int(scratch[0], 0x11, "outside R preserved");
    expect_int(scratch[1], 0x11, "outside G preserved");
    expect_int(scratch[2], 0x11, "outside B preserved");

    /* Inside (2,2) is red */
    i = (2 * 8 + 2) * 3;
    expect_int(scratch[i + 0], 255, "inside R");
    expect_int(scratch[i + 1], 0, "inside G");
    expect_int(scratch[i + 2], 0, "inside B");

    free(scratch);
    SDL_DestroySurface(surf);
}

static void test_pack_rgba_rect(void)
{
    Uint8 scratch[4 * 4 * 4];
    Uint8 out[2 * 2 * 4];
    size_t out_len = 0;
    SDL_Rect rect = { 1, 1, 2, 2 };

    memset(scratch, 0, sizeof(scratch));
    scratch[(1 * 4 + 1) * 4 + 0] = 10;
    scratch[(1 * 4 + 1) * 4 + 1] = 20;
    scratch[(1 * 4 + 1) * 4 + 2] = 30;
    scratch[(1 * 4 + 1) * 4 + 3] = 40;
    scratch[(1 * 4 + 2) * 4 + 0] = 11;
    scratch[(1 * 4 + 2) * 4 + 3] = 41;
    scratch[(2 * 4 + 1) * 4 + 0] = 12;
    scratch[(2 * 4 + 1) * 4 + 3] = 42;
    scratch[(2 * 4 + 2) * 4 + 0] = 13;
    scratch[(2 * 4 + 2) * 4 + 3] = 43;

    expect_true(KITTY_PackRGBARect(scratch, 4, 4, &rect, out, sizeof(out), &out_len), "pack rgba ok");
    expect_int((int)out_len, 16, "pack rgba len 2x2x4");
    expect_int(out[0], 10, "rgba pack R");
    expect_int(out[3], 40, "rgba pack A");
    expect_int(out[4], 11, "rgba pack next R");
    expect_int(out[7], 41, "rgba pack next A");
}

static void test_convert_rgba(void)
{
    SDL_Surface *surf = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_ARGB8888);
    Uint8 *scratch;
    SDL_Rect rect = { 1, 1, 1, 1 };
    int i;
    expect_true(surf != NULL, "create argb surface");
    scratch = (Uint8 *)calloc(4u * 4u * 4u, 1);
    expect_true(scratch != NULL, "rgba scratch");

    SDL_FillSurfaceRect(surf, NULL, SDL_MapSurfaceRGBA(surf, 0, 0, 0, 0));
    SDL_FillSurfaceRect(surf, &rect, SDL_MapSurfaceRGBA(surf, 255, 128, 64, 200));
    expect_true(KITTY_ConvertSurfaceRectToRGBA(surf, &rect, scratch), "convert rgba");

    i = (1 * 4 + 1) * 4;
    expect_int(scratch[i + 0], 255, "rgba R");
    expect_int(scratch[i + 1], 128, "rgba G");
    expect_int(scratch[i + 2], 64, "rgba B");
    expect_int(scratch[i + 3], 200, "rgba A");
    expect_int(scratch[0], 0, "outside stays 0");

    free(scratch);
    SDL_DestroySurface(surf);
}

static void test_tile_diff(void)
{
    const int w = 64, h = 64, bpp = 3;
    Uint8 *a = (Uint8 *)calloc((size_t)w * (size_t)h * (size_t)bpp, 1);
    Uint8 *b = (Uint8 *)calloc((size_t)w * (size_t)h * (size_t)bpp, 1);
    SDL_Rect dirty;
    KITTY_DiffOutcome out;

    expect_true(a && b, "diff buffers");

    out = KITTY_TileDiffBBox(a, b, w, h, bpp, 32, 0.45f, &dirty);
    expect_int((int)out, (int)KITTY_DIFF_NONE, "identical -> NONE");

    /* Change one pixel in the top-left tile. */
    b[0] = 1;
    out = KITTY_TileDiffBBox(a, b, w, h, bpp, 32, 0.45f, &dirty);
    expect_int((int)out, (int)KITTY_DIFF_RECT, "one tile -> RECT");
    expect_int(dirty.x, 0, "dirty.x");
    expect_int(dirty.y, 0, "dirty.y");
    expect_int(dirty.w, 32, "dirty.w tile");
    expect_int(dirty.h, 32, "dirty.h tile");

    /* Opposite corners → large bbox → FULL at 0.45 cover. */
    b[(63 * w + 63) * bpp] = 2;
    out = KITTY_TileDiffBBox(a, b, w, h, bpp, 32, 0.45f, &dirty);
    expect_int((int)out, (int)KITTY_DIFF_FULL, "corners -> FULL by coverage");
    expect_int(dirty.w, 64, "full bbox.w");
    expect_int(dirty.h, 64, "full bbox.h");

    expect_true(KITTY_SelectFbDiff(NULL, true), "diff default on");
    expect_true(!KITTY_SelectFbDiff(NULL, false), "diff profile default off");
    expect_true(!KITTY_SelectFbDiff("0", true), "diff 0 off");
    expect_int(KITTY_SelectFbDiffTile(NULL, 32), 32, "tile default 32");
    expect_int(KITTY_SelectFbDiffTile(NULL, 16), 16, "tile profile default 16");
    expect_float_near(KITTY_SelectFbDiffCover(NULL, 0.45f), 0.45f, 0.001f, "cover default");

    free(a);
    free(b);
}

static void test_fb_rect_to_place_cells(void)
{
    SDL_Rect r = { 100, 50, 200, 100 };
    int col0 = -1, row0 = -1, pc = -1, pr = -1;

    expect_true(KITTY_FbRectToPlaceCells(1000, 500, &r, 100, 50,
                                         &col0, &row0, &pc, &pr),
                "place cells map ok");
    expect_int(col0, 10, "col0");
    expect_int(row0, 5, "row0");
    expect_int(pc, 20, "place_cols");
    expect_int(pr, 10, "place_rows");

    /* Tiny rect still covers at least one cell. */
    r.x = 0;
    r.y = 0;
    r.w = 1;
    r.h = 1;
    expect_true(KITTY_FbRectToPlaceCells(1000, 500, &r, 100, 50,
                                         &col0, &row0, &pc, &pr),
                "1px maps");
    expect_int(col0, 0, "1px col0");
    expect_int(row0, 0, "1px row0");
    expect_int(pc, 1, "1px place_cols");
    expect_int(pr, 1, "1px place_rows");
}

static void test_fb_pixel_to_cell_cursor(void)
{
    int col0 = -1, row0 = -1, X = -1, Y = -1;
    expect_true(KITTY_FbPixelToCellCursor(25, 40, 10, 20, &col0, &row0, &X, &Y),
                "cell cursor ok");
    expect_int(col0, 2, "cursor col");
    expect_int(row0, 2, "cursor row");
    expect_int(X, 5, "cell X");
    expect_int(Y, 0, "cell Y");
}

static void test_fnv1a64(void)
{
    const Uint8 a[] = { 1, 2, 3 };
    const Uint8 b[] = { 1, 2, 4 };
    const Uint64 ha = KITTY_Fnv1a64(a, sizeof(a));
    const Uint64 hb = KITTY_Fnv1a64(b, sizeof(b));
    const Uint64 ha2 = KITTY_Fnv1a64(a, sizeof(a));
    expect_true(ha == ha2, "fnv stable");
    expect_true(ha != hb, "fnv differs on change");
}

static void test_coalesce_rects(void)
{
    SDL_Rect in[3];
    SDL_Rect out[8];
    int n;

    /* Overlapping → one. */
    in[0] = (SDL_Rect){ 10, 10, 20, 20 };
    in[1] = (SDL_Rect){ 20, 20, 20, 20 };
    n = KITTY_CoalesceRects(200, 200, in, 2, 0, out, 8);
    expect_int(n, 1, "overlap coalesce count");
    expect_int(out[0].x, 10, "overlap x");
    expect_int(out[0].y, 10, "overlap y");
    expect_int(out[0].w, 30, "overlap w");
    expect_int(out[0].h, 30, "overlap h");

    /* Near within gap=2 → one; gap=0 stays two. */
    in[0] = (SDL_Rect){ 0, 0, 10, 10 };
    in[1] = (SDL_Rect){ 11, 0, 10, 10 }; /* 1px gap */
    n = KITTY_CoalesceRects(200, 200, in, 2, 0, out, 8);
    expect_int(n, 2, "gap0 keeps two");
    n = KITTY_CoalesceRects(200, 200, in, 2, 2, out, 8);
    expect_int(n, 1, "gap2 merges near");

    /* Far apart stay separate. */
    in[0] = (SDL_Rect){ 0, 0, 10, 10 };
    in[1] = (SDL_Rect){ 100, 100, 10, 10 };
    n = KITTY_CoalesceRects(200, 200, in, 2, 2, out, 8);
    expect_int(n, 2, "far stay two");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!SDL_Init(0)) {
        /* SDL3 may need INIT_VIDEO for surfaces on some builds; try video. */
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
    }

    test_union_basic();
    test_union_clamp();
    test_union_empty_list_is_full();
    test_union_outside_fails();
    test_coverage();
    test_choose_mode();
    test_pack_rgb_rect();
    test_convert_preserves_outside();
    test_pack_rgba_rect();
    test_convert_rgba();
    test_tile_diff();
    test_fb_rect_to_place_cells();
    test_fb_pixel_to_cell_cursor();
    test_fnv1a64();
    test_coalesce_rects();

    SDL_Quit();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("kitty_rect_utils_test: OK\n");
    return 0;
}
