/* Unit tests for terminal profile TOML load + detect. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KITTY_PROFILE_STANDALONE 1
#include "../third_party/SDL/src/video/kitty/SDL_kittyprofile.h"

static int failures;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    }
}

static void expect_str(const char *got, const char *want, const char *msg)
{
    if (!got || !want || strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: %s (got '%s' want '%s')\n", msg, got ? got : "(null)",
                want ? want : "(null)");
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

static void expect_float_near(float got, float want, const char *msg)
{
    float d = got - want;
    if (d < 0) {
        d = -d;
    }
    if (d > 0.001f) {
        fprintf(stderr, "FAIL: %s (got %f want %f)\n", msg, (double)got, (double)want);
        failures++;
    }
}

static void clear_term_env(void)
{
    SDL_unsetenv_unsafe("SDL_KITTY_PROFILES");
    SDL_unsetenv_unsafe("TERM_PROGRAM");
    SDL_unsetenv_unsafe("TERM_PROGRAM_VERSION");
    SDL_unsetenv_unsafe("TERM");
    SDL_unsetenv_unsafe("KITTY_WINDOW_ID");
}

static void test_builtin_fallback(void)
{
    const KITTY_TerminalProfile *p;

    KITTY_ResetTerminalProfilesToBuiltin();
    clear_term_env();

    /* Ensure with no file → builtin detect. */
    KITTY_EnsureTerminalProfilesLoaded();
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    p = KITTY_DetectTerminalProfile();
    expect_true(p != NULL, "builtin rex non-null");
    expect_str(p->name, "rex", "builtin rex name");
    expect_true(p->prefer_direct, "builtin rex prefer_direct");
    expect_true(p->ignore_ioctl_pixels, "builtin rex ignore_ioctl");
    expect_true(p->direct_af_when_shm, "builtin rex direct_af_when_shm");
    expect_float_near(p->af_max_coverage, 0.95f, "builtin rex af_max_coverage");
    expect_str(KITTY_TerminalProfilesSource(), "builtin", "source builtin");
}

static void test_load_shipped_toml(void)
{
    const char *path = NULL;
    const char *candidates[] = {
        "../config/terminals.toml",
        "config/terminals.toml",
        "../../config/terminals.toml",
        NULL
    };
    const KITTY_TerminalProfile *p;
    int i;

    for (i = 0; candidates[i]; ++i) {
        FILE *fp = fopen(candidates[i], "rb");
        if (fp) {
            fclose(fp);
            path = candidates[i];
            break;
        }
    }
    if (!path) {
        fprintf(stderr, "SKIP: config/terminals.toml not found from cwd\n");
        return;
    }

    KITTY_ResetTerminalProfilesToBuiltin();
    clear_term_env();

    expect_true(KITTY_LoadTerminalProfiles(path), "load terminals.toml");
    expect_true(strcmp(KITTY_TerminalProfilesSource(), "builtin") != 0, "source is path");

    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    p = KITTY_DetectTerminalProfile();
    expect_true(p != NULL, "loaded rex non-null");
    expect_str(p->name, "rex", "loaded rex name");
    expect_true(p->prefer_direct, "loaded rex prefer_direct");
    expect_true(p->silent_af_ok, "loaded rex silent_af_ok");
    expect_true(p->ignore_ioctl_pixels, "loaded rex ignore_ioctl");
    expect_true(p->direct_af_when_shm, "loaded rex direct_af_when_shm");
    expect_float_near(p->af_max_coverage, 1.01f, "loaded rex af coverage");
    expect_int((int)p->af_max_pixels, 1920 * 1080, "loaded rex af pixels");
    expect_int(p->default_fb_max_w, 1280, "loaded rex fb w from defaults");
    expect_int(p->default_fb_max_h, 720, "loaded rex fb h from defaults");
    expect_true(!p->shm_unlink_after_send, "shm_unlink_after_send false");

    SDL_setenv_unsafe("TERM_PROGRAM", "kitty", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "kitty", "loaded kitty name");
    expect_true(!p->prefer_direct, "loaded kitty prefer shm");
    expect_int(p->default_fb_max_w, 1920, "loaded kitty fb w");
    expect_int(p->default_fb_max_h, 1080, "loaded kitty fb h");
    expect_float_near(p->af_max_coverage, 0.40f, "loaded kitty af coverage");
    expect_true(p->direct_af_when_shm, "loaded kitty direct_af_when_shm");
    expect_true(!p->dirty_at_overlays, "loaded kitty no dirty_at");

    SDL_unsetenv_unsafe("TERM_PROGRAM");
    SDL_setenv_unsafe("TERM", "xterm-kitty", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "kitty", "empty TERM_PROGRAM + xterm-kitty");

    SDL_unsetenv_unsafe("TERM");
    SDL_setenv_unsafe("TERM_PROGRAM", "ghostty", 1);
    /* No TERM_PROGRAM_VERSION → skip version-gated ghostty-1.3, hit tip catch-all. */
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "ghostty", "loaded ghostty tip catch-all");
    expect_int(p->default_fb_max_w, 1920, "ghostty fb w");
    expect_true(p->direct_af_when_shm, "tip ghostty direct_af_when_shm");

    SDL_setenv_unsafe("TERM_PROGRAM_VERSION", "1.3.1", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "ghostty-1.3", "ghostty 1.3.1 → ghostty-1.3");
    expect_true(!p->direct_af_when_shm, "1.3.1 no direct_af_when_shm");
    expect_true(!p->silent_af_ok, "1.3.1 silent_af_ok false");

    SDL_setenv_unsafe("TERM_PROGRAM_VERSION", "1.3.2-main+22f100c", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "ghostty", "ghostty tip version → ghostty");
    expect_true(p->direct_af_when_shm, "tip version direct_af_when_shm");

    SDL_unsetenv_unsafe("TERM_PROGRAM_VERSION");
    SDL_setenv_unsafe("TERM_PROGRAM", "WezTerm", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "wezterm", "loaded wezterm");
    expect_true(p->prefer_direct, "loaded wezterm prefer_direct");
    expect_true(!p->dirty_at_overlays, "loaded wezterm dirty_at off (host crash)");
    expect_int(p->default_fb_max_w, 960, "loaded wezterm fb w");
    expect_int(p->default_fb_max_h, 540, "loaded wezterm fb h");

    SDL_setenv_unsafe("TERM_PROGRAM", "not-a-real-term", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "unknown", "fallback unknown");
    expect_true(p->silent_af_ok, "unknown silent_af_ok");
}

static void test_bad_toml_keeps_builtin(void)
{
    const char *bad = "/tmp/sdl-kitty-bad-profiles.toml";
    FILE *fp = fopen(bad, "wb");
    const KITTY_TerminalProfile *p;

    if (!fp) {
        fprintf(stderr, "SKIP: cannot write %s\n", bad);
        return;
    }
    fputs("this is not = valid toml [[[\n", fp);
    fclose(fp);

    KITTY_ResetTerminalProfilesToBuiltin();
    clear_term_env();
    expect_true(!KITTY_LoadTerminalProfiles(bad), "bad toml fails");
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "rex", "still builtin after bad load");
    expect_str(KITTY_TerminalProfilesSource(), "builtin", "source still builtin");
    remove(bad);
}

int main(void)
{
    failures = 0;
    test_builtin_fallback();
    test_load_shipped_toml();
    test_bad_toml_keeps_builtin();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("kitty_profile_utils_test: ok\n");
    return 0;
}
