/* Unit tests for transmit probe parse + mode selection (Step 2). */
#include <stdio.h>
#include <string.h>

#define KITTY_PROBE_STANDALONE 1
#include "../third_party/SDL/src/video/kitty/SDL_kittyprobe.h"

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

static void test_parse_ok(void)
{
    const char *ok = "\033_Gi=98;OK\033\\";
    expect_int((int)KITTY_ParseGraphicsQueryReply(ok, strlen(ok), 98),
               (int)KITTY_GQ_OK, "parse OK");
}

static void test_parse_ok_with_frame(void)
{
    /* Ghostty tip a=f success: echoes r=<frame> before ;OK */
    const char *ok = "\033_Gi=97,r=1;OK\033\\";
    expect_int((int)KITTY_ParseGraphicsQueryReply(ok, strlen(ok), 97),
               (int)KITTY_GQ_OK, "parse OK with r=");
}

static void test_parse_ok_with_extra_keys(void)
{
    const char *ok = "\033_Gi=1,I=2,p=3;OK\033\\";
    expect_int((int)KITTY_ParseGraphicsQueryReply(ok, strlen(ok), 1),
               (int)KITTY_GQ_OK, "parse OK with I= p=");
}

static void test_parse_error(void)
{
    const char *err = "\033_Gi=99;EBADF:Failed to read image file\033\\";
    expect_int((int)KITTY_ParseGraphicsQueryReply(err, strlen(err), 99),
               (int)KITTY_GQ_ERROR, "parse error");
}

static void test_parse_error_with_frame(void)
{
    const char *err = "\033_Gi=97,r=1;EINVAL: unimplemented action\033\\";
    expect_int((int)KITTY_ParseGraphicsQueryReply(err, strlen(err), 97),
               (int)KITTY_GQ_ERROR, "parse error with r=");
}

static void test_parse_wrong_id(void)
{
    const char *ok = "\033_Gi=98;OK\033\\";
    expect_int((int)KITTY_ParseGraphicsQueryReply(ok, strlen(ok), 99),
               (int)KITTY_GQ_NONE, "wrong id -> none");
}

static void test_parse_none(void)
{
    const char *noise = "\033[?1;2c";
    expect_int((int)KITTY_ParseGraphicsQueryReply(noise, strlen(noise), 98),
               (int)KITTY_GQ_NONE, "DA1 only -> none");
}

static void test_format_place_keys(void)
{
    char buf[256];
    int n;

    n = KITTY_FormatTransmitHeader(buf, sizeof(buf), 'd', 1, 100, 50, 80, 24, false, NULL, 0, 24);
    expect_true(n > 0, "format direct stretch");
    expect_true(strstr(buf, "C=1") != NULL, "has C=1");
    expect_true(strstr(buf, "N=1") != NULL, "has N=1");
    expect_true(strstr(buf, "p=1") != NULL, "has p=1");
    expect_true(strstr(buf, "q=2") != NULL, "has q=2 quiet");
    expect_true(strstr(buf, "c=80") != NULL, "has c=80");
    expect_true(strstr(buf, "r=24") != NULL, "has r=24");
    expect_true(strstr(buf, "f=24") != NULL, "has f=24");
    expect_true(strstr(buf, "m=1") == NULL, "no m=1 when more=false");

    n = KITTY_FormatTransmitHeader(buf, sizeof(buf), 'd', 2, 10, 10, 0, 0, true, "o=z", 0, 24);
    expect_true(n > 0, "format direct 1:1 zlib more");
    expect_true(strstr(buf, "o=z") != NULL, "has o=z");
    expect_true(strstr(buf, "m=1") != NULL, "has m=1");
    expect_true(strstr(buf, ",c=") == NULL && strstr(buf, "c=") == NULL, "no cell stretch");

    n = KITTY_FormatTransmitHeader(buf, sizeof(buf), 's', 3, 8, 8, 40, 20, false, NULL, 192, 24);
    expect_true(n > 0, "format shm");
    expect_true(strstr(buf, "t=s") != NULL, "t=s");
    expect_true(strstr(buf, "S=192") != NULL, "S=192");
    expect_true(strstr(buf, KITTY_AT_PLACE_KEYS) != NULL, "place keys block");

    n = KITTY_FormatTransmitHeader(buf, sizeof(buf), 'd', 4, 16, 16, 0, 0, false, NULL, 0, 32);
    expect_true(n > 0, "format rgba");
    expect_true(strstr(buf, "f=32") != NULL, "has f=32");
}

static void test_select_fb_alpha(void)
{
    expect_true(!KITTY_SelectFbAlpha(NULL), "unset -> off");
    expect_true(!KITTY_SelectFbAlpha(""), "empty -> off");
    expect_true(KITTY_SelectFbAlpha("1"), "1 -> on");
    expect_true(KITTY_SelectFbAlpha("true"), "true -> on");
    expect_true(!KITTY_SelectFbAlpha("0"), "0 -> off");
}

static void test_classify_resize(void)
{
    expect_int((int)KITTY_ClassifyResize(true, true, 800, 600, 800, 600, 100, 40, 100, 40),
               (int)KITTY_RESIZE_NONE, "unchanged -> none");
    expect_int((int)KITTY_ClassifyResize(true, true, 800, 600, 800, 600, 100, 40, 120, 50),
               (int)KITTY_RESIZE_REPLACE, "stretch same FB new grid -> replace");
    expect_int((int)KITTY_ClassifyResize(false, true, 800, 600, 800, 600, 100, 40, 120, 50),
               (int)KITTY_RESIZE_NONE, "1:1 same FB -> none");
    expect_int((int)KITTY_ClassifyResize(true, false, 800, 600, 800, 600, 100, 40, 120, 50),
               (int)KITTY_RESIZE_NONE, "stretch no live + same FB -> none");
    expect_int((int)KITTY_ClassifyResize(true, true, 800, 600, 1000, 700, 100, 40, 120, 50),
               (int)KITTY_RESIZE_FULL, "FB size changed -> full");
}

static void test_select_dirty_bw(void)
{
    expect_true(!KITTY_SelectDirtyBW(NULL, false), "unset + no probe -> off");
    expect_true(KITTY_SelectDirtyBW(NULL, true), "unset + probe ok -> on");
    expect_true(KITTY_SelectDirtyBW("1", false), "env 1 forces on");
    expect_true(!KITTY_SelectDirtyBW("0", true), "env 0 forces off");
    expect_true(!KITTY_SelectDirtyBW("off", true), "env off forces off");
}

static void test_select_dirty_at(void)
{
    expect_true(!KITTY_SelectDirtyAT(NULL, false), "unset + profile off -> off");
    expect_true(KITTY_SelectDirtyAT(NULL, true), "unset + profile on -> on");
    expect_true(KITTY_SelectDirtyAT("1", false), "env 1 forces on");
    expect_true(!KITTY_SelectDirtyAT("0", true), "env 0 forces off");
    expect_true(!KITTY_SelectDirtyAT("off", true), "env off forces off");
}

static void test_select_dirty_union_at(void)
{
    expect_true(!KITTY_SelectDirtyUnionAT(NULL, false), "unset + profile off");
    expect_true(KITTY_SelectDirtyUnionAT(NULL, true), "unset + profile on");
    expect_true(KITTY_SelectDirtyUnionAT("1", false), "env 1");
    expect_true(!KITTY_SelectDirtyUnionAT("0", true), "env 0");
    expect_true(!KITTY_SelectDirtyUnionAT("false", true), "env false");
}

static void test_select_upload_skip(void)
{
    expect_true(!KITTY_SelectUploadSkip(NULL, false), "unset + profile off");
    expect_true(KITTY_SelectUploadSkip(NULL, true), "unset + profile on");
    expect_true(KITTY_SelectUploadSkip("1", false), "env 1");
    expect_true(!KITTY_SelectUploadSkip("0", true), "env 0");
}

static void test_select_coalesce_gap(void)
{
    expect_int(KITTY_SelectCoalesceGap(NULL, 2), 2, "unset uses profile");
    expect_int(KITTY_SelectCoalesceGap("0", 2), 0, "env 0 off");
    expect_int(KITTY_SelectCoalesceGap("off", 2), 0, "env off");
    expect_int(KITTY_SelectCoalesceGap("4", 2), 4, "env 4");
}

static void test_select_pingpong(void)
{
    expect_true(!KITTY_SelectPingPong(NULL, false), "default replace (no pingpong)");
    expect_true(KITTY_SelectPingPong(NULL, true), "profile default pingpong on");
    expect_true(!KITTY_SelectPingPong("0", true), "PINGPONG=0");
    expect_true(KITTY_SelectPingPong("1", false), "PINGPONG=1");
    expect_true(!KITTY_SelectPingPong("off", true), "PINGPONG=off");
}

static void test_select_modes(void)
{
    /* Signature: force_env, force_profile, prefer_direct, prefer_zlib, shm_ok, direct_ok */
    expect_int((int)KITTY_SelectTransmitMode("direct", NULL, false, false, true, true),
               (int)KITTY_TRANSMIT_DIRECT, "force direct");
    expect_int((int)KITTY_SelectTransmitMode("shm", NULL, true, false, false, true),
               (int)KITTY_TRANSMIT_SHM, "force shm even when prefer_direct");
    expect_int((int)KITTY_SelectTransmitMode("zlib", NULL, false, false, true, true),
               (int)KITTY_TRANSMIT_DIRECT_ZLIB, "force zlib");
    expect_int((int)KITTY_SelectTransmitMode(NULL, "zlib", false, false, true, true),
               (int)KITTY_TRANSMIT_DIRECT_ZLIB, "profile force zlib");
    expect_int((int)KITTY_SelectTransmitMode("direct", "zlib", false, false, true, true),
               (int)KITTY_TRANSMIT_DIRECT, "env force beats profile");
    expect_int((int)KITTY_SelectTransmitMode(NULL, NULL, true, false, true, true),
               (int)KITTY_TRANSMIT_DIRECT, "prefer_direct ignores shm_ok");
    expect_int((int)KITTY_SelectTransmitMode(NULL, NULL, true, true, true, true),
               (int)KITTY_TRANSMIT_DIRECT_ZLIB, "prefer_direct+prefer_zlib");
    expect_int((int)KITTY_SelectTransmitMode(NULL, NULL, false, false, true, true),
               (int)KITTY_TRANSMIT_SHM, "shm preferred when probe ok");
    expect_int((int)KITTY_SelectTransmitMode(NULL, NULL, false, true, false, true),
               (int)KITTY_TRANSMIT_DIRECT_ZLIB, "shm fail + prefer_zlib");
    expect_int((int)KITTY_SelectTransmitMode(NULL, NULL, false, false, false, true),
               (int)KITTY_TRANSMIT_DIRECT, "shm fail -> direct");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!SDL_Init(0) && !SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    test_parse_ok();
    test_parse_ok_with_frame();
    test_parse_ok_with_extra_keys();
    test_parse_error();
    test_parse_error_with_frame();
    test_parse_wrong_id();
    test_parse_none();
    test_format_place_keys();
    test_select_fb_alpha();
    test_classify_resize();
    test_select_dirty_bw();
    test_select_dirty_at();
    test_select_dirty_union_at();
    test_select_upload_skip();
    test_select_coalesce_gap();
    test_select_pingpong();
    test_select_modes();
    SDL_Quit();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("kitty_probe_utils_test: OK\n");
    return 0;
}
