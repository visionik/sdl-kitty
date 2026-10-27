/* sdl_kitty_ask.cpp — ImGui question card for sdl-kitty (docs/tui-card.md)
 *
 * Usage (via scripts/sdl-kitty-ask.sh):
 *   sdl-kitty-ask [--multi] "Question?" "Opt1" "Opt2" [... up to 8]
 *
 * Single-select (default): radio buttons. Multi: checkboxes.
 * Confirm with OK / Enter. Cancel with Esc / Cancel.
 *
 * Exit status (no answer on stdout — Kitty APC owns the tty):
 *   single: 0 .. n-1  = 0-based index of the chosen option
 *   multi:  bitmask     bit i set ⇒ option i selected
 *   cancel / Esc: 255
 */
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

enum { kMinOptions = 2, kMaxOptions = 8 };

struct AskConfig {
    std::string question;
    std::vector<std::string> options;
    bool multi = false;
};

enum { kExitCancel = 255 };

static void usage(const char *argv0)
{
    std::fprintf(stderr,
                 "Usage: %s [--multi|-m] \"Question\" \"Opt1\" \"Opt2\" [...Opt8]\n"
                 "  2–8 options. Default: single-select (radio). --multi: checkboxes.\n"
                 "  Exit status:\n"
                 "    single: 0..n-1 = 0-based index of the choice\n"
                 "    multi:  bitmask (bit i ⇒ option i selected)\n"
                 "    cancel / Esc: %d\n",
                 argv0, kExitCancel);
}

static bool parse_args(int argc, char **argv, AskConfig *cfg)
{
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strcmp(a, "--multi") == 0 || std::strcmp(a, "-m") == 0) {
            cfg->multi = true;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            usage(argv[0]);
            std::exit(0);
        } else if (a[0] == '-' && a[1] != '\0') {
            std::fprintf(stderr, "unknown flag: %s\n", a);
            return false;
        } else {
            positional.push_back(a);
        }
    }

    if (positional.size() < 1u + (unsigned)kMinOptions) {
        std::fprintf(stderr, "need a question and at least %d options\n", kMinOptions);
        return false;
    }
    if (positional.size() > 1u + (unsigned)kMaxOptions) {
        std::fprintf(stderr, "at most %d options\n", kMaxOptions);
        return false;
    }

    cfg->question = positional[0];
    cfg->options.assign(positional.begin() + 1, positional.end());
    return true;
}

static int exit_code_for_selection(const AskConfig &cfg, const bool *selected, int n)
{
    if (cfg.multi) {
        int mask = 0;
        for (int i = 0; i < n; ++i) {
            if (selected[i]) {
                mask |= (1 << i);
            }
        }
        return mask;
    }
    for (int i = 0; i < n; ++i) {
        if (selected[i]) {
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
    bool selected[kMaxOptions] = {};
    int radio = 0; /* single-select index */
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 1.0f;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool done = false;
    bool confirmed = false;
    bool cancelled = false;
    const ImVec4 clear_color = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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

        ImGui::TextWrapped("%s", cfg.question.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        if (cfg.multi) {
            for (int i = 0; i < nopt; ++i) {
                ImGui::Checkbox(cfg.options[(size_t)i].c_str(), &selected[i]);
            }
        } else {
            for (int i = 0; i < nopt; ++i) {
                if (ImGui::RadioButton(cfg.options[(size_t)i].c_str(), &radio, i)) {
                    /* radio index updated by RadioButton */
                }
            }
            for (int i = 0; i < nopt; ++i) {
                selected[i] = (i == radio);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        int nsel = 0;
        for (int i = 0; i < nopt; ++i) {
            if (selected[i]) {
                ++nsel;
            }
        }
        const bool can_ok = (nsel > 0);

        if (!can_ok) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("OK", ImVec2(120, 0)) ||
            (can_ok && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            confirmed = true;
            done = true;
        }
        if (!can_ok) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            cancelled = true;
            done = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(cfg.multi ? "  multi-select · Esc cancel" : "  single-select · Esc cancel");

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
    return exit_code_for_selection(cfg, selected, nopt);
}
