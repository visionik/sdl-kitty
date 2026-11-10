/* Unit tests for terminal profiles: shared facts (terminals.toml) + tuning (sdl-kitty.toml). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void expect_int(long long got, long long want, const char *msg)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %lld want %lld)\n", msg, got, want);
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

/* Everything terminals.toml matches on, so the shell running ctest can't leak in. */
static void clear_term_env(void)
{
    static const char *vars[] = {
        "SDL_KITTY_PROFILES", "SDL_KITTY_FACTS", "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "TERM",
        "KITTY_WINDOW_ID", "GHOSTTY_RESOURCES_DIR", "REX_SESSION", "WEZTERM_EXECUTABLE",
        "ITERM_SESSION_ID", NULL
    };
    int i;
    for (i = 0; vars[i]; ++i) {
        SDL_unsetenv_unsafe(vars[i]);
    }
}

static void fresh(void)
{
    clear_term_env();
    KITTY_ResetTerminalProfilesToBuiltin();
}

static const char *write_temp(const char *name, const char *text)
{
    static char path[256];
    FILE *fp;
    snprintf(path, sizeof(path), "/tmp/sdl-kitty-test-%d-%s", (int)getpid(), name);
    fp = fopen(path, "wb");
    if (!fp) {
        return NULL;
    }
    fputs(text, fp);
    fclose(fp);
    return path;
}

static void test_rex_from_environment(void)
{
    const KITTY_TerminalProfile *p;

    fresh();
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "rex", "rex name");
    expect_str(p->rows, "rex", "rex rows");
    expect_str(p->recognised_by, "term_program", "rex by TERM_PROGRAM");
    expect_true(p->kind == KITTY_TERM_REX, "rex kind");
    expect_str(KITTY_TerminalProfilesSource(), "builtin", "compiled-in files only");
    /* Facts decide these. */
    expect_true(p->facts.shm == KITTY_FACT_NO, "rex fact: no t=s");
    expect_true(p->prefer_direct, "rex prefer_direct (kitty.shm = false)");
    expect_true(p->silent_af_ok, "rex silent_af_ok (kitty.frame_edit_reply = false)");
    expect_true(p->ignore_ioctl_pixels, "rex ignore_ioctl (window.ioctl_pixels = false)");
    expect_true(!p->skip_af_probe, "rex has a=f");
    expect_int((long long)p->apc_chunk_bytes, 2 * 1024 * 1024, "rex chunk: fact unknown, tuning default");
    /* Tuning. */
    expect_true(p->prefer_zlib, "rex prefer_zlib");
    expect_true(p->direct_af_when_shm, "rex direct_af_when_shm");
    expect_float_near(p->af_max_coverage, 0.40f, "rex af_max_coverage");
    expect_int((long long)p->af_max_pixels, 1920 * 1080, "rex af pixels");
    expect_int(p->coalesce_gap_px, 2, "rex coalesce");
    expect_int(p->default_fb_max_w, 1280, "rex fb w");
    expect_int(p->default_fb_max_h, 720, "rex fb h");
    expect_true(!p->shm_unlink_after_send, "shm_unlink_after_send false");

    fresh();
    SDL_setenv_unsafe("REX_SESSION", "session:1", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "rex", "rex from REX_SESSION");
    expect_str(p->recognised_by, "env", "rex by env");
}

static void test_kitty(void)
{
    const KITTY_TerminalProfile *p;

    fresh();
    SDL_setenv_unsafe("TERM_PROGRAM", "kitty", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "kitty", "kitty name");
    expect_true(!p->prefer_direct, "kitty prefers shm");
    expect_true(!p->silent_af_ok, "kitty strict a=f probe");
    expect_int((long long)p->apc_chunk_bytes, 262144 - 256, "kitty chunk from kitty.max_escape_bytes");
    expect_int(p->default_fb_max_w, 1920, "kitty fb w");
    expect_int(p->default_fb_max_shm_w, 1920, "kitty fb shm w");
    expect_int(p->default_fb_max_direct_w, 1280, "kitty fb direct w");
    expect_true(p->direct_af_when_shm, "kitty direct_af_when_shm");
    expect_int(p->drain_passes, 2, "kitty drain passes");

    fresh();
    SDL_setenv_unsafe("TERM", "xterm-kitty", 1);
    expect_str(KITTY_DetectTerminalProfile()->name, "kitty", "TERM=xterm-kitty");

    fresh();
    SDL_setenv_unsafe("KITTY_WINDOW_ID", "1", 1);
    expect_str(KITTY_DetectTerminalProfile()->recognised_by, "env", "KITTY_WINDOW_ID");
}

static void test_ghostty_versions(void)
{
    const KITTY_TerminalProfile *p;

    fresh();
    SDL_setenv_unsafe("TERM_PROGRAM", "ghostty", 1);
    /* No version: version rows never match an unknown version. */
    p = KITTY_DetectTerminalProfile();
    expect_str(p->rows, "ghostty", "ghostty, version unknown");
    expect_true(!p->skip_af_probe, "ghostty probes a=f");
    expect_true(p->direct_af_when_shm, "ghostty direct_af_when_shm");
    expect_int((long long)p->apc_chunk_bytes, 68157440 - 256, "ghostty chunk (65 MiB)");

    SDL_setenv_unsafe("TERM_PROGRAM_VERSION", "1.3.1", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "ghostty", "1.3.1 is still ghostty");
    expect_str(p->rows, "ghostty,ghostty-1.3", "1.3.1 stacks ghostty-1.3");
    expect_str(p->version, "1.3.1", "version from TERM_PROGRAM_VERSION");
    expect_true(p->facts.frame_edit == KITTY_FACT_NO, "1.3.1 fact: no a=f");
    expect_true(p->skip_af_probe, "1.3.1 skips the a=f probe");
    expect_true(!p->direct_af_when_shm, "1.3.1 no direct_af_when_shm");
    expect_int(p->default_fb_max_w, 1920, "1.3.1 keeps ghostty's tuning");
    expect_true(p->facts.shm == KITTY_FACT_YES, "1.3.1 keeps ghostty's facts");

    SDL_setenv_unsafe("TERM_PROGRAM_VERSION", "1.3.2-main+22f100c", 1);
    expect_str(KITTY_DetectTerminalProfile()->rows, "ghostty", "1.3.2-main is after 1.3.1");
}

static void test_xtversion_beats_environment(void)
{
    const KITTY_TerminalProfile *p;

    /* Ghostty started from a Rex pane inherits TERM_PROGRAM=rex. */
    fresh();
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    SDL_setenv_unsafe("REX_SESSION", "session:1", 1);
    p = KITTY_IdentifyTerminalProfile("ghostty 1.3.1", "linux");
    expect_str(p->name, "ghostty", "xtversion names ghostty");
    expect_str(p->recognised_by, "xtversion", "by xtversion");
    expect_str(p->version, "1.3.1", "version from xtversion");
    expect_str(p->rows, "ghostty,ghostty-1.3", "xtversion version picks the version row");
    expect_true(p->kind == KITTY_TERM_GHOSTTY, "ghostty kind");

    /* An answer naming no row falls back to the environment. */
    p = KITTY_IdentifyTerminalProfile("foot(1.18.0)", "linux");
    expect_str(p->name, "rex", "unknown xtversion -> environment");
    expect_str(p->version, "", "no version from the environment");

    p = KITTY_IdentifyTerminalProfile("kitty(0.50.0)", "linux");
    expect_str(p->version, "0.50.0", "kitty(0.50.0) version");
    p = KITTY_IdentifyTerminalProfile("rex 0.0.1793072516-g956d74", "macos");
    expect_str(p->name, "rex", "rex answer");
}

static void test_wezterm_os_rows(void)
{
    const KITTY_TerminalProfile *p;

    fresh();
    p = KITTY_IdentifyTerminalProfile("WezTerm 20240203-110809-5046fc22", "macos");
    expect_str(p->rows, "wezterm,wezterm-macos", "wezterm on macOS");
    expect_true(p->facts.shm == KITTY_FACT_NO, "macOS fact: no t=s");
    expect_true(p->prefer_direct, "wezterm prefer_direct");
    expect_true(p->prefer_zlib, "wezterm prefer_zlib");
    expect_true(p->skip_af_probe, "wezterm a=f never shows");
    expect_true(!p->dirty_at_overlays, "wezterm dirty_at off (host crash)");
    expect_true(p->facts.keyboard_config[0] != '\0', "wezterm keyboard config fact");

    p = KITTY_IdentifyTerminalProfile("WezTerm 20240203-110809-5046fc22", "linux");
    expect_str(p->rows, "wezterm", "wezterm on Linux");
    expect_true(p->facts.shm == KITTY_FACT_UNKNOWN, "Linux t=s unknown");
    expect_true(p->prefer_direct, "wezterm tuning still prefers direct");

    SDL_setenv_unsafe("TERM_PROGRAM", "WezTerm", 1);
    expect_str(KITTY_DetectTerminalProfile()->name, "wezterm", "TERM_PROGRAM=WezTerm, any case");
}

static void test_unknown_and_other_terminals(void)
{
    const KITTY_TerminalProfile *p;

    fresh();
    SDL_setenv_unsafe("TERM_PROGRAM", "not-a-real-term", 1);
    p = KITTY_DetectTerminalProfile();
    expect_str(p->name, "unknown", "unknown name");
    expect_str(p->rows, "", "no rows");
    expect_true(p->silent_af_ok, "unknown silent_af_ok ([terminal.unknown])");
    expect_true(p->facts.frame_edit == KITTY_FACT_UNKNOWN, "unknown facts");
    expect_true(!KITTY_EnvMatchesLoadedProfile(), "env names no terminal");

    /* In terminals.toml, with no tuning of its own: [defaults] and facts. */
    p = KITTY_IdentifyTerminalProfile("iTerm2 3.7.7", "macos");
    expect_str(p->name, "iterm2", "iterm2");
    expect_true(p->kind == KITTY_TERM_UNKNOWN, "iterm2 has no kind");
    expect_true(p->skip_af_probe, "iterm2: a=f stays on the first frame");
    expect_true(!p->silent_af_ok, "iterm2: [defaults]");
}

static void test_tuning_overlay(void)
{
    const char *path;
    const KITTY_TerminalProfile *p;

    fresh();
    path = write_temp("tuning.toml", "[terminal.rex]\ncoalesce_gap_px = 7\n"
                                     "[terminal.\"ghostty-1.3\"]\ndefault_fb_max = \"800x600\"\n");
    expect_true(path != NULL, "write tuning");
    expect_true(KITTY_LoadTerminalProfiles(path), "load tuning overlay");
    expect_true(strstr(KITTY_TerminalProfilesSource(), "tuning.toml") != NULL, "source names it");

    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    p = KITTY_DetectTerminalProfile();
    expect_int(p->coalesce_gap_px, 7, "overlay wins");
    expect_true(p->prefer_zlib, "the rest of rex's tuning stays");

    /* A row id with a dot. */
    p = KITTY_IdentifyTerminalProfile("ghostty 1.3.1", "linux");
    expect_int(p->default_fb_max_w, 800, "[terminal.\"ghostty-1.3\"] applies after ghostty");
    remove(path);
}

static void test_facts_files(void)
{
    const char *path;
    const KITTY_TerminalProfile *p;

    /* A file that breaks the spec is refused; the facts already loaded stay. */
    fresh();
    path = write_temp("typo.toml", "schema = 1\n[[terminal]]\nid = \"t\"\nname = \"T\"\n"
                                   "match.xtversion = [\"t\"]\nkitty.typo = true\n");
    expect_true(!KITTY_LoadTerminalFacts(path), "unknown fact refused");
    remove(path);
    path = write_temp("bad.toml", "this is not = valid toml [[[\n");
    expect_true(!KITTY_LoadTerminalFacts(path), "bad toml refused");
    remove(path);
    path = write_temp("schema.toml", "schema = 2\n");
    expect_true(!KITTY_LoadTerminalFacts(path), "schema 2 refused");
    remove(path);
    path = write_temp("noparent.toml", "schema = 1\n[[terminal]]\nid = \"u\"\nextends = \"t\"\n"
                                       "match.os = [\"linux\"]\n");
    expect_true(!KITTY_LoadTerminalFacts(path), "extends an unknown row refused");
    remove(path);
    path = write_temp("nocond.toml", "schema = 1\n[[terminal]]\nid = \"t\"\nname = \"T\"\n"
                                     "match.xtversion = [\"t\"]\n[[terminal]]\nid = \"u\"\nextends = \"t\"\n");
    expect_true(!KITTY_LoadTerminalFacts(path), "extending row without a condition refused");
    remove(path);
    expect_str(KITTY_TerminalProfilesSource(), "builtin", "nothing taken");
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    expect_str(KITTY_DetectTerminalProfile()->name, "rex", "compiled-in facts still there");

    /* A valid file replaces them. */
    fresh();
    path = write_temp("facts.toml",
                      "schema = 1\n"
                      "[[terminal]]\nid = \"foo\"\nname = \"Foo\"\nmatch.xtversion = [\"foo\"]\n"
                      "kitty.frame_edit = true\nkitty.max_escape_bytes = 4096\n"
                      "[terminal.evidence]\n\"kitty.frame_edit\" = \"test\"\n"
                      "[[terminal]]\nid = \"foo-old\"\nextends = \"foo\"\nmatch.version_max = \"1.9\"\n"
                      "kitty.frame_edit = false\n"
                      "[[terminal]]\nid = \"foo-old-mac\"\nextends = \"foo-old\"\nmatch.os = [\"macos\"]\n"
                      "kitty.shm = false\n");
    expect_true(KITTY_LoadTerminalFacts(path), "valid facts file");
    p = KITTY_IdentifyTerminalProfile("foo 1.2", "macos");
    expect_str(p->rows, "foo,foo-old,foo-old-mac", "rows stack in file order");
    expect_true(p->skip_af_probe && p->prefer_direct, "stacked facts apply");
    expect_int((long long)p->apc_chunk_bytes, 4096 - 256, "chunk from the new file");
    expect_str(KITTY_IdentifyTerminalProfile("foo 2.0", "macos")->rows, "foo", "newer: base only");
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    expect_str(KITTY_DetectTerminalProfile()->name, "unknown", "rex isn't in the new file");
    remove(path);
}

static void test_frame_edit_zlib(void)
{
    const char *path;

    /* Rex, Kitty and Ghostty 1.4 draw them (compiled-in facts); Ghostty 1.3.1 has no a=f at all. */
    fresh();
    SDL_setenv_unsafe("TERM_PROGRAM", "rex", 1);
    expect_true(KITTY_DetectTerminalProfile()->af_zlib, "rex: kitty.frame_edit_zlib = true");
    expect_true(KITTY_IdentifyTerminalProfile("kitty(0.50.0)", "macos")->af_zlib, "kitty: kitty.frame_edit_zlib = true");
    expect_true(KITTY_IdentifyTerminalProfile("ghostty 1.4.0", "macos")->af_zlib, "ghostty 1.4: true");
    expect_true(!KITTY_IdentifyTerminalProfile("ghostty 1.3.1", "macos")->af_zlib, "ghostty 1.3.1: false");
    expect_true(!KITTY_IdentifyTerminalProfile("WezTerm 20240203-110809-5046fc22", "macos")->af_zlib,
                "wezterm: unknown, so raw, as [defaults] says");

    /* The fact decides, over the tuning. */
    path = write_temp("zlib.toml",
                      "schema = 1\n"
                      "[[terminal]]\nid = \"foo\"\nname = \"Foo\"\nmatch.xtversion = [\"foo\"]\n"
                      "kitty.frame_edit_zlib = true\n"
                      "[[terminal]]\nid = \"foo-old\"\nextends = \"foo\"\nmatch.version_max = \"1.9\"\n"
                      "kitty.frame_edit_zlib = false\n");
    expect_true(KITTY_LoadTerminalFacts(path), "facts with frame_edit_zlib");
    expect_true(KITTY_IdentifyTerminalProfile("foo 2.0", "macos")->af_zlib, "frame_edit_zlib = true");
    expect_true(!KITTY_IdentifyTerminalProfile("foo 1.2", "macos")->af_zlib, "frame_edit_zlib = false");
    remove(path);
}

static void test_shipped_files(void)
{
    /* ctest runs from the repo root: the vendored files load as overlays too. */
    fresh();
    expect_true(KITTY_LoadTerminalFacts("config/terminals.toml"), "config/terminals.toml");
    expect_true(KITTY_LoadTerminalProfiles("config/sdl-kitty.toml"), "config/sdl-kitty.toml");
    SDL_setenv_unsafe("TERM_PROGRAM", "ghostty", 1);
    SDL_setenv_unsafe("TERM_PROGRAM_VERSION", "1.3.1", 1);
    expect_str(KITTY_DetectTerminalProfile()->rows, "ghostty,ghostty-1.3", "shipped files resolve");
}

int main(void)
{
    failures = 0;
    test_rex_from_environment();
    test_kitty();
    test_ghostty_versions();
    test_xtversion_beats_environment();
    test_wezterm_os_rows();
    test_unknown_and_other_terminals();
    test_tuning_overlay();
    test_facts_files();
    test_frame_edit_zlib();
    test_shipped_files();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("kitty_profile_utils_test: ok\n");
    return 0;
}
