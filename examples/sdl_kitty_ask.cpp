/* sdl_kitty_ask.cpp — ImGui question card for sdl-kitty (docs/tui-card.md)
 *
 * Usage (via `sdlk ask` or scripts/sdl-kitty-ask.sh):
 *   sdl_kitty_ask [--multi|-m] [--out FILE] "Question?" "Opt1" "Opt2" [...]
 *   sdl_kitty_ask [--multi|-m] [--out FILE] "Question?" \
 *       --tab "Tab A" "Opt1" "Opt2" --tab "Tab B" "Opt3" "Opt4"
 *
 * Single-select (default): radio buttons. --multi: checkboxes.
 * Confirm with OK / Enter. Cancel with Esc / Cancel.
 *
 * Output:
 *   With --out FILE:
 *     Writes selected 0-based global option indices (one per line) to FILE.
 *     Exit status: 0 = OK, 255 = Cancel/Esc, 2 = usage/runtime error.
 *   Without --out (legacy Unix exit-status mode):
 *     single: 0..n-1  = 0-based index of the chosen option (max 16)
 *     multi:  bitmask (0..127, max 7 options so it never collides with 255)
 *     cancel / Esc: 255
 */
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "roboto_medium_compressed.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

enum {
    kMinOptions = 2,
    kMaxExitSingleOptions = 16,
    kMaxExitMultiOptions = 7,
    kMaxFileOptions = 128,
    kExitCancel = 255
};

struct AskTab {
    std::string name;
    std::vector<int> indices; /* indices into AskConfig::options */
};

struct AskConfig {
    std::string question;
    std::vector<std::string> options;
    std::vector<AskTab> tabs;
    std::string out_path;
    std::string theme;
    float font_size = 0.0f; /* 0 = auto from framebuffer height */
    bool multi = false;
};

struct AskTheme {
    ImVec4 bg;
    ImVec4 surface;
    ImVec4 frame;
    ImVec4 frame_hover;
    ImVec4 frame_active;
    ImVec4 border;
    ImVec4 text;
    ImVec4 text_muted;
    ImVec4 heading;
    ImVec4 accent;
    ImVec4 accent_hover;
    ImVec4 accent_active;
    ImVec4 accent_text;
    ImVec4 tab;
    ImVec4 tab_hover;
    ImVec4 tab_active;
    ImVec4 btn;
    ImVec4 btn_hover;
    ImVec4 btn_active;
};

static ImVec4 rgb_hex(unsigned int rgb, float a = 1.0f)
{
    return ImVec4(
        (float)((rgb >> 16) & 0xFFu) / 255.0f,
        (float)((rgb >> 8) & 0xFFu) / 255.0f,
        (float)(rgb & 0xFFu) / 255.0f,
        a);
}

static AskTheme resolve_theme(const std::string &name)
{
    AskTheme t;
    if (name == "catppuccin" || name == "mocha") {
        t.bg            = rgb_hex(0x1E1E2E);
        t.surface       = rgb_hex(0x26263A);
        t.frame         = rgb_hex(0x313244);
        t.frame_hover   = rgb_hex(0x3E4057);
        t.frame_active  = rgb_hex(0x4B4E69);
        t.border        = rgb_hex(0x45475A);
        t.text          = rgb_hex(0xCDD6F4);
        t.text_muted    = rgb_hex(0x9399B2);
        t.heading       = rgb_hex(0x89DCEB);
        t.accent        = rgb_hex(0xCBA6F7);
        t.accent_hover  = rgb_hex(0xD8BCFA);
        t.accent_active = rgb_hex(0xB48BE8);
        t.accent_text   = rgb_hex(0x11111B);
        t.tab           = rgb_hex(0x28283D);
        t.tab_hover     = rgb_hex(0x393B52);
        t.tab_active    = rgb_hex(0x453C63);
        t.btn           = rgb_hex(0x313244);
        t.btn_hover     = rgb_hex(0x41435A);
        t.btn_active    = rgb_hex(0x4F526D);
    } else if (name == "nord") {
        t.bg            = rgb_hex(0x1E2430);
        t.surface       = rgb_hex(0x262E3D);
        t.frame         = rgb_hex(0x2E3748);
        t.frame_hover   = rgb_hex(0x3B465C);
        t.frame_active  = rgb_hex(0x46536C);
        t.border        = rgb_hex(0x434C5E);
        t.text          = rgb_hex(0xECEFF4);
        t.text_muted    = rgb_hex(0x9AA7BC);
        t.heading       = rgb_hex(0x88C0D0);
        t.accent        = rgb_hex(0x88C0D0);
        t.accent_hover  = rgb_hex(0x9FD0DF);
        t.accent_active = rgb_hex(0x6BA9BC);
        t.accent_text   = rgb_hex(0x161B24);
        t.tab           = rgb_hex(0x283040);
        t.tab_hover     = rgb_hex(0x384359);
        t.tab_active    = rgb_hex(0x3E5269);
        t.btn           = rgb_hex(0x2E3748);
        t.btn_hover     = rgb_hex(0x3D4960);
        t.btn_active    = rgb_hex(0x4C5B75);
    } else if (name == "amber" || name == "arcade" || name == "crt") {
        t.bg            = rgb_hex(0x141210);
        t.surface       = rgb_hex(0x1D1915);
        t.frame         = rgb_hex(0x2A231C);
        t.frame_hover   = rgb_hex(0x3A2F24);
        t.frame_active  = rgb_hex(0x4A3C2C);
        t.border        = rgb_hex(0x4B3D2E);
        t.text          = rgb_hex(0xF6EDE0);
        t.text_muted    = rgb_hex(0xA6937C);
        t.heading       = rgb_hex(0xFFD166);
        t.accent        = rgb_hex(0xF5A623);
        t.accent_hover  = rgb_hex(0xFFB845);
        t.accent_active = rgb_hex(0xD98D14);
        t.accent_text   = rgb_hex(0x16110A);
        t.tab           = rgb_hex(0x221C17);
        t.tab_hover     = rgb_hex(0x352B20);
        t.tab_active    = rgb_hex(0x4D391D);
        t.btn           = rgb_hex(0x2A231C);
        t.btn_hover     = rgb_hex(0x3D3227);
        t.btn_active    = rgb_hex(0x4F4031);
    } else if (name == "light") {
        t.bg            = rgb_hex(0xF4F6FA);
        t.surface       = rgb_hex(0xFFFFFF);
        t.frame         = rgb_hex(0xE8EDF5);
        t.frame_hover   = rgb_hex(0xDCE4F0);
        t.frame_active  = rgb_hex(0xCCD7EA);
        t.border        = rgb_hex(0xCBD4E1);
        t.text          = rgb_hex(0x1B2230);
        t.text_muted    = rgb_hex(0x5B667A);
        t.heading       = rgb_hex(0x0958C7);
        t.accent        = rgb_hex(0x0969DA);
        t.accent_hover  = rgb_hex(0x1F7AE6);
        t.accent_active = rgb_hex(0x0550AE);
        t.accent_text   = rgb_hex(0xFFFFFF);
        t.tab           = rgb_hex(0xE6ECF5);
        t.tab_hover     = rgb_hex(0xD6E1F2);
        t.tab_active    = rgb_hex(0xC6DAF8);
        t.btn           = rgb_hex(0xE5EBF4);
        t.btn_hover     = rgb_hex(0xD6DFEE);
        t.btn_active    = rgb_hex(0xC6D3E8);
    } else {
        /* Default: "tokyo" — deep midnight indigo + luminous periwinkle/cyan */
        t.bg            = rgb_hex(0x151828);
        t.surface       = rgb_hex(0x1B1F34);
        t.frame         = rgb_hex(0x242A45);
        t.frame_hover   = rgb_hex(0x2F375B);
        t.frame_active  = rgb_hex(0x3A4470);
        t.border        = rgb_hex(0x323B60);
        t.text          = rgb_hex(0xE6ECF8);
        t.text_muted    = rgb_hex(0x8894B8);
        t.heading       = rgb_hex(0x7DCFFF);
        t.accent        = rgb_hex(0x7AA2F7);
        t.accent_hover  = rgb_hex(0x8FB4FF);
        t.accent_active = rgb_hex(0x5E88E5);
        t.accent_text   = rgb_hex(0x0E1222);
        t.tab           = rgb_hex(0x1F243D);
        t.tab_hover     = rgb_hex(0x2D3559);
        t.tab_active    = rgb_hex(0x34437A);
        t.btn           = rgb_hex(0x242A45);
        t.btn_hover     = rgb_hex(0x31395E);
        t.btn_active    = rgb_hex(0x3C4673);
    }
    return t;
}

static void apply_theme(ImGuiStyle &style, const AskTheme &t)
{
    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text]                 = t.text;
    c[ImGuiCol_TextDisabled]         = t.text_muted;
    c[ImGuiCol_WindowBg]             = t.bg;
    c[ImGuiCol_ChildBg]              = t.surface;
    c[ImGuiCol_PopupBg]              = t.surface;
    c[ImGuiCol_Border]               = t.border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg]              = t.frame;
    c[ImGuiCol_FrameBgHovered]       = t.frame_hover;
    c[ImGuiCol_FrameBgActive]        = t.frame_active;
    c[ImGuiCol_CheckMark]            = t.accent;
    c[ImGuiCol_SliderGrab]           = t.accent;
    c[ImGuiCol_SliderGrabActive]     = t.accent_active;
    c[ImGuiCol_Button]               = t.btn;
    c[ImGuiCol_ButtonHovered]        = t.btn_hover;
    c[ImGuiCol_ButtonActive]         = t.btn_active;
    c[ImGuiCol_Header]               = t.frame;
    c[ImGuiCol_HeaderHovered]        = t.frame_hover;
    c[ImGuiCol_HeaderActive]         = t.frame_active;
    c[ImGuiCol_Separator]            = t.border;
    c[ImGuiCol_SeparatorHovered]     = t.accent;
    c[ImGuiCol_SeparatorActive]      = t.accent_active;
    c[ImGuiCol_Tab]                  = t.tab;
    c[ImGuiCol_TabHovered]           = t.tab_hover;
    c[ImGuiCol_TabSelected]          = t.tab_active;
    c[ImGuiCol_TabSelectedOverline]  = t.accent;
    c[ImGuiCol_TabDimmed]            = t.tab;
    c[ImGuiCol_TabDimmedSelected]    = t.tab_active;
    c[ImGuiCol_ScrollbarBg]          = t.surface;
    c[ImGuiCol_ScrollbarGrab]        = t.border;
    c[ImGuiCol_ScrollbarGrabHovered] = t.frame_hover;
    c[ImGuiCol_ScrollbarGrabActive]  = t.accent;
    c[ImGuiCol_NavCursor]            = t.accent;
}

static void usage(const char *argv0)
{
    std::fprintf(stderr,
                 "Usage: %s [--multi|-m] [--out FILE] [--font-size PX] [--theme NAME] \"Question\" [--tab \"Name\"] \"Opt1\" \"Opt2\" ...\n"
                 "  With --out FILE: up to %d options across any number of --tab groups;\n"
                 "    writes 0-based indices (one per line) to FILE, exits 0 (OK) or %d (cancel).\n"
                 "  Without --out: single-select 2–%d (exit 0..n-1), --multi 2–%d (bitmask 0..127).\n"
                 "  Themes: tokyo (default), catppuccin, nord, amber, light.\n"
                 "  Env: SDL_KITTY_ASK_THEME, SDL_KITTY_ASK_FONT_SIZE (px), SDL_KITTY_ASK_FONT (/path/to/font.ttf).\n"
                 "  Cancel / Esc: %d\n",
                 argv0, kMaxFileOptions, kExitCancel,
                 kMaxExitSingleOptions, kMaxExitMultiOptions, kExitCancel);
}

static bool parse_args(int argc, char **argv, AskConfig *cfg)
{
    bool have_question = false;
    int cur_tab = -1;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strcmp(a, "--multi") == 0 || std::strcmp(a, "-m") == 0) {
            cfg->multi = true;
        } else if (std::strcmp(a, "--out") == 0 || std::strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--out requires a path\n");
                return false;
            }
            cfg->out_path = argv[++i];
        } else if (std::strcmp(a, "--font-size") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--font-size requires a pixel size\n");
                return false;
            }
            cfg->font_size = (float)std::atof(argv[++i]);
        } else if (std::strcmp(a, "--theme") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--theme requires a theme name\n");
                return false;
            }
            cfg->theme = argv[++i];
        } else if (std::strcmp(a, "--tab") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--tab requires a label\n");
                return false;
            }
            AskTab t;
            t.name = argv[++i];
            cfg->tabs.push_back(t);
            cur_tab = (int)cfg->tabs.size() - 1;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            usage(argv[0]);
            std::exit(0);
        } else if (a[0] == '-' && a[1] != '\0') {
            std::fprintf(stderr, "unknown flag: %s\n", a);
            return false;
        } else if (!have_question) {
            cfg->question = a;
            have_question = true;
        } else {
            const int idx = (int)cfg->options.size();
            cfg->options.push_back(a);
            if (cur_tab < 0) {
                AskTab t;
                t.name = "Options";
                cfg->tabs.push_back(t);
                cur_tab = 0;
            }
            cfg->tabs[(size_t)cur_tab].indices.push_back(idx);
        }
    }

    if (!have_question) {
        std::fprintf(stderr, "need a question and at least %d options\n", kMinOptions);
        return false;
    }

    /* Drop empty tabs if any were declared without options. */
    std::vector<AskTab> nonempty;
    for (size_t i = 0; i < cfg->tabs.size(); ++i) {
        if (!cfg->tabs[i].indices.empty()) {
            nonempty.push_back(cfg->tabs[i]);
        }
    }
    cfg->tabs.swap(nonempty);

    const size_t nopt = cfg->options.size();
    const size_t max_opt = !cfg->out_path.empty()
                               ? (size_t)kMaxFileOptions
                               : (cfg->multi ? (size_t)kMaxExitMultiOptions
                                             : (size_t)kMaxExitSingleOptions);

    if (nopt < (size_t)kMinOptions) {
        std::fprintf(stderr, "need at least %d options\n", kMinOptions);
        return false;
    }
    if (nopt > max_opt) {
        std::fprintf(stderr, "at most %zu options%s (use --out FILE for more)\n",
                     max_opt, cfg->multi ? " in --multi exit-code mode" : "");
        return false;
    }
    return true;
}

static int write_out_file(const AskConfig &cfg, const std::vector<bool> &selected)
{
    FILE *fp = std::fopen(cfg.out_path.c_str(), "w");
    if (!fp) {
        std::fprintf(stderr, "failed to open --out file: %s\n", cfg.out_path.c_str());
        return 2;
    }
    for (size_t i = 0; i < selected.size(); ++i) {
        if (selected[i]) {
            std::fprintf(fp, "%zu\n", i);
        }
    }
    if (std::fclose(fp) != 0) {
        std::fprintf(stderr, "failed to close --out file: %s\n", cfg.out_path.c_str());
        return 2;
    }
    return 0;
}

static int exit_code_for_selection(const AskConfig &cfg, const std::vector<bool> &selected)
{
    if (!cfg.out_path.empty()) {
        return write_out_file(cfg, selected);
    }
    const int n = (int)selected.size();
    if (cfg.multi) {
        int mask = 0;
        for (int i = 0; i < n; ++i) {
            if (selected[(size_t)i]) {
                mask |= (1 << i);
            }
        }
        return mask;
    }
    for (int i = 0; i < n; ++i) {
        if (selected[(size_t)i]) {
            return i;
        }
    }
    return kExitCancel;
}

int main(int argc, char **argv)
{
    AskConfig cfg;
    if (!parse_args(argc, argv, &cfg)) {
        usage(argv[0]);
        return 2;
    }

    const int nopt = (int)cfg.options.size();
    const int ntabs = (int)cfg.tabs.size();
    const bool use_tabs = (ntabs > 1);
    std::vector<bool> selected((size_t)nopt, false);
    int radio = 0; /* global single-select index */
    if (!cfg.multi) {
        selected[0] = true;
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kitty");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }

    SDL_Window *window = SDL_CreateWindow("sdl-kitty-ask", 0, 0, 0);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    std::string theme_name = cfg.theme;
    if (theme_name.empty()) {
        const char *env_theme = std::getenv("SDL_KITTY_ASK_THEME");
        if (env_theme && env_theme[0]) {
            theme_name = env_theme;
        }
    }
    const AskTheme theme = resolve_theme(theme_name);
    ImGuiStyle &style = ImGui::GetStyle();
    apply_theme(style, theme);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.TabBarOverlineSize = 2.5f;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    int fb_w = 0, fb_h = 0;
    SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);

    float font_size = cfg.font_size;
    if (font_size <= 0.0f) {
        const char *env_sz = std::getenv("SDL_KITTY_ASK_FONT_SIZE");
        if (env_sz && env_sz[0]) {
            font_size = (float)std::atof(env_sz);
        }
    }
    if (font_size <= 0.0f) {
        /* Size text relative to terminal cell rows when SDL_KITTY_VIEWPORT is set,
         * targeting ~1.45x terminal cell height (~46px at 32px/row) so Roboto's
         * x-height reads larger and more comfortably than plain terminal text. */
        int vp_rows = 0;
        const char *vp_env = std::getenv("SDL_KITTY_VIEWPORT");
        if (vp_env && vp_env[0]) {
            int c = 0, r = 0, w = 0, h = 0;
            if (std::sscanf(vp_env, "%d,%d,%d,%d", &c, &r, &w, &h) == 4 && h > 0) {
                vp_rows = h;
            }
        }
        if (vp_rows > 0 && fb_h > 0) {
            const float px_per_row = (float)fb_h / (float)vp_rows;
            font_size = px_per_row * 1.45f;
            const float max_fit = (float)fb_h / 8.5f;
            if (font_size > max_fit) {
                font_size = max_fit;
            }
            if (font_size < 28.0f) {
                font_size = 28.0f;
            }
        } else if (fb_h >= 720) {
            font_size = 46.0f;
        } else if (fb_h >= 520) {
            font_size = 40.0f;
        } else {
            font_size = 34.0f;
        }
    }
    if (font_size < 14.0f) {
        font_size = 14.0f;
    } else if (font_size > 72.0f) {
        font_size = 72.0f;
    }

    ImFontConfig fcfg;
    fcfg.OversampleH = 2;
    fcfg.OversampleV = 2;
    fcfg.PixelSnapH = true;
    ImFont *font = nullptr;
    const char *custom_font = std::getenv("SDL_KITTY_ASK_FONT");
    if (custom_font && custom_font[0]) {
        font = io.Fonts->AddFontFromFileTTF(custom_font, font_size, &fcfg);
    }
    if (!font) {
        font = io.Fonts->AddFontFromMemoryCompressedTTF(
            RobotoMedium_compressed_data, (int)RobotoMedium_compressed_size,
            font_size, &fcfg);
    }
    if (!font) {
        io.Fonts->AddFontDefaultVector(&fcfg);
    }
    style.FontSizeBase = font_size;

    const float ui_scale = (font_size > 26.0f) ? (font_size / 26.0f) : 1.0f;
    style.ScaleAllSizes(ui_scale);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool done = false;
    bool confirmed = false;
    bool cancelled = false;
    bool first_frame = true;
    int active_tab = 0;
    int pending_tab = -1;
    const ImVec4 clear_color = theme.bg;

    while (!done) {
        SDL_Event event;
        if (!first_frame) {
            /* Pace idle frames (~60 Hz) since software renderer ignores VSync. */
            if (SDL_WaitEventTimeout(&event, 16)) {
                do {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                    if (event.type == SDL_EVENT_QUIT) {
                        cancelled = true;
                        done = true;
                    }
                    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                        event.window.windowID == SDL_GetWindowID(window)) {
                        cancelled = true;
                        done = true;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN &&
                        event.key.scancode == SDL_SCANCODE_ESCAPE) {
                        cancelled = true;
                        done = true;
                    }
                } while (SDL_PollEvent(&event));
            }
        } else {
            first_frame = false;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("ask", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);

        ImGui::PushFont(nullptr, font_size * 1.12f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.heading);
        ImGui::TextWrapped("%s", cfg.question.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Keyboard tab switching: Tab / [ / ] / Left / Right */
        if (use_tabs) {
            const bool prev_tab =
                ImGui::IsKeyPressed(ImGuiKey_LeftBracket) ||
                ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ||
                (ImGui::IsKeyPressed(ImGuiKey_Tab) && io.KeyShift);
            const bool next_tab =
                ImGui::IsKeyPressed(ImGuiKey_RightBracket) ||
                ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
                (ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyShift);
            if (next_tab) {
                pending_tab = (active_tab + 1) % ntabs;
            } else if (prev_tab) {
                pending_tab = (active_tab + ntabs - 1) % ntabs;
            }
        }

        const float footer_h = ImGui::GetFrameHeightWithSpacing() + 14.0f * ui_scale;

        auto draw_tab_options = [&](const AskTab &tab) {
            const int tn = (int)tab.indices.size();
            /* Number keys 1..9 toggle/select options within the active tab. */
            for (int j = 0; j < tn && j < 9; ++j) {
                const ImGuiKey digit = (ImGuiKey)(ImGuiKey_1 + j);
                if (ImGui::IsKeyPressed(digit)) {
                    const int gi = tab.indices[(size_t)j];
                    if (cfg.multi) {
                        selected[(size_t)gi] = !selected[(size_t)gi];
                    } else {
                        radio = gi;
                    }
                }
            }

            ImGui::BeginChild("options", ImVec2(0.0f, -footer_h), ImGuiChildFlags_Borders);
            if (cfg.multi) {
                for (int j = 0; j < tn; ++j) {
                    const int gi = tab.indices[(size_t)j];
                    bool val = selected[(size_t)gi];
                    ImGui::PushID(gi);
                    char label[280];
                    std::snprintf(label, sizeof(label), "[%d] %s",
                                  j + 1, cfg.options[(size_t)gi].c_str());
                    if (ImGui::Checkbox(label, &val)) {
                        selected[(size_t)gi] = val;
                    }
                    ImGui::PopID();
                }
            } else {
                for (int j = 0; j < tn; ++j) {
                    const int gi = tab.indices[(size_t)j];
                    ImGui::PushID(gi);
                    char label[280];
                    std::snprintf(label, sizeof(label), "[%d] %s",
                                  j + 1, cfg.options[(size_t)gi].c_str());
                    if (ImGui::RadioButton(label, &radio, gi)) {
                        /* radio updated */
                    }
                    ImGui::PopID();
                }
                for (int i = 0; i < nopt; ++i) {
                    selected[(size_t)i] = (i == radio);
                }
            }
            ImGui::EndChild();
        };

        if (use_tabs) {
            if (ImGui::BeginTabBar("ask_tabs", ImGuiTabBarFlags_None)) {
                for (int t = 0; t < ntabs; ++t) {
                    const AskTab &tab = cfg.tabs[(size_t)t];
                    int tab_sel = 0;
                    if (cfg.multi) {
                        for (size_t k = 0; k < tab.indices.size(); ++k) {
                            if (selected[(size_t)tab.indices[k]]) {
                                ++tab_sel;
                            }
                        }
                    }
                    char tab_label[128];
                    if (cfg.multi && tab_sel > 0) {
                        std::snprintf(tab_label, sizeof(tab_label), "%s (%d)###tab_%d",
                                      tab.name.c_str(), tab_sel, t);
                    } else {
                        std::snprintf(tab_label, sizeof(tab_label), "%s###tab_%d",
                                      tab.name.c_str(), t);
                    }
                    ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                    if (pending_tab == t) {
                        flags |= ImGuiTabItemFlags_SetSelected;
                    }
                    if (ImGui::BeginTabItem(tab_label, nullptr, flags)) {
                        if (pending_tab == t) {
                            pending_tab = -1;
                        }
                        if (active_tab != t) {
                            active_tab = t;
                            /* In single-select, auto-select the first item on the newly focused tab
                             * so pressing Enter immediately launches what you're looking at. */
                            if (!cfg.multi && !tab.indices.empty()) {
                                bool in_tab = false;
                                for (size_t k = 0; k < tab.indices.size(); ++k) {
                                    if (tab.indices[k] == radio) {
                                        in_tab = true;
                                        break;
                                    }
                                }
                                if (!in_tab) {
                                    radio = tab.indices[0];
                                    for (int i = 0; i < nopt; ++i) {
                                        selected[(size_t)i] = (i == radio);
                                    }
                                }
                            }
                        }
                        draw_tab_options(tab);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        } else {
            draw_tab_options(cfg.tabs[0]);
        }

        ImGui::Separator();
        ImGui::Spacing();

        int nsel = 0;
        for (int i = 0; i < nopt; ++i) {
            if (selected[(size_t)i]) {
                ++nsel;
            }
        }
        const bool can_ok = (nsel > 0);

        char ok_label[48];
        if (cfg.multi && nsel > 0) {
            std::snprintf(ok_label, sizeof(ok_label), "OK (%d)", nsel);
        } else {
            std::snprintf(ok_label, sizeof(ok_label), "OK");
        }

        const float btn_w = 110.0f * ui_scale;
        if (!can_ok) {
            ImGui::BeginDisabled();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, theme.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.accent_active);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.accent_text);
        if (ImGui::Button(ok_label, ImVec2(btn_w, 0)) ||
            (can_ok && (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))) {
            confirmed = true;
            done = true;
        }
        ImGui::PopStyleColor(4);
        if (!can_ok) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (!confirmed && ImGui::Button("Cancel", ImVec2(btn_w, 0))) {
            cancelled = true;
            done = true;
        }
        ImGui::SameLine();
        if (use_tabs) {
            ImGui::TextDisabled("  Tab/[/] switch tabs · 1-9 pick · Esc cancel");
        } else {
            ImGui::TextDisabled(cfg.multi ? "  multi-select · 1-9 toggle · Esc cancel"
                                          : "  single-select · 1-9 pick · Esc cancel");
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (cancelled || !confirmed) {
        return kExitCancel;
    }
    return exit_code_for_selection(cfg, selected);
}
