// SPDX-License-Identifier: AGPL-3.0-only
// The launcher's loop and its public entry points (app_shell.hpp).
#include "shell.hpp"

namespace ravenemudsk_ui {
using namespace shell;
namespace {
SharedScreen kept_screen;
constexpr Uint32 launcher_systems = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS;
}
void keep_screen(SharedScreen screen) {
    if (kept_screen.window != screen.window) close_screen();
    kept_screen = screen;
}
SharedScreen take_screen() {
    return std::exchange(kept_screen, SharedScreen{});
}
void close_screen() {
    const SharedScreen screen = take_screen();
    if (screen.renderer) SDL_DestroyRenderer(screen.renderer);
    if (screen.window) SDL_DestroyWindow(screen.window);
}
std::optional<LaunchRequest> run_launcher(const std::string& error) {
    // Subsystems are reference-counted: a window kept for TV mode survives.
    if (SDL_InitSubSystem(launcher_systems) != 0)
        throw std::runtime_error(std::string("Initialisation SDL impossible : ") + SDL_GetError());
    Library lib(data_file());
    lib.load();
    if (lib.games().empty()) {
        // Only the original, bundled homebrew demo is imported by default.
        if (char* base = SDL_GetBasePath()) {
            if (lib.add(from_utf8(base) / "RavenEmuDSK-Demo.gb")) lib.save();
            SDL_free(base);
        }
    }
    State state{};
    try {
        state.folders = app_folders();
        state.settings = Settings(settings_file());
        state.settings.load();
        std::snprintf(state.pseudo, sizeof(state.pseudo), "%s", state.settings.get("pseudo").c_str());
    } catch (const std::exception& e) {
        notify(state, Toast::Kind::Error, "Dossiers indisponibles", e.what());
    }
    const auto recent = recent_order(lib);
    state.chosen = !recent.empty() ? recent.front() : lib.games().empty() ? -1 : 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    choose_render_driver(state.settings);
    const int display = chosen_display(state.settings);
    // Back from a game played in TV mode, TV mode reopens at once, and the
    // game's full-screen window and renderer carry on here.
    const bool tv_start = tv_this_run();
    const SharedScreen shared = take_screen();
    SDL_Window* window = shared.window;
    if (window) {
        SDL_SetWindowTitle(window, "RavenEmu");
        if (tv_start && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            move_to_display(window, display);
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        if (!tv_start) {
            SDL_SetWindowFullscreen(window, 0);
            SDL_SetWindowSize(window, 1480, 900);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    } else {
        // TV mode opens on the screen chosen for it; the desktop launcher on the main one.
        const int at = tv_start ? display : 0;
        window = SDL_CreateWindow("RavenEmu", SDL_WINDOWPOS_CENTERED_DISPLAY(at), SDL_WINDOWPOS_CENTERED_DISPLAY(at),
            1480, 900, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN |
            (tv_start ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    }
    if (!window) {
        const std::string reason = SDL_GetError();
        SDL_QuitSubSystem(launcher_systems);
        throw std::runtime_error("Création de la fenêtre impossible : " + reason);
    }
    if (char* base = SDL_GetBasePath()) {
        const auto path = from_utf8(base) / "assets" / "ravenemu-logo.png";
        SDL_free(base);
        if (SDL_Surface* icon = load_local_surface(path)) {
            SDL_SetWindowIcon(window, icon);
            SDL_FreeSurface(icon);
        }
    }
    SDL_SetWindowMinimumSize(window, 1024, 680);
    (void)SDL_SetWindowHitTest(window, &hit_test, &state);
    SDL_Renderer* renderer = shared.renderer;
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        limit_frame_queue(renderer);
    }
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        const std::string reason = SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(launcher_systems);
        throw std::runtime_error("Création du rendu SDL impossible : " + reason);
    }
    state.window = window;
    state.renderer = renderer;
    state.logo = load_logo(renderer);
    state.ra_logo = load_brand_asset(renderer,"ra-logo.png");
    state.ra_images = std::make_unique<BadgeCache>(renderer);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    state.fonts = load_fonts(true);
    apply_theme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    if (!error.empty()) state.toasts.push_back({Toast::Kind::Error, "Impossible de lancer le jeu", error, -1.0, 8.0});
    const bool ci_smoke = SDL_getenv("RAVENEMUDSK_UI_SMOKE") != nullptr;
    retro_client(state).set_hardcore(ci_smoke?false:state.settings.flag("ra_hardcore",false));
    if (ci_smoke) state.page = smoke_page();
    state.library = &lib;
    static bool update_started = false;  // once per run, not per return from a game
    if (!ci_smoke && !update_started && state.settings.flag("update_check", true)) {
        update_started = true;
        updates().start(RAVENEMUDSK_VERSION);
    }
    const char* smoke_name = SDL_getenv("RAVENEMUDSK_UI_SMOKE_PAGE");
    if (tv_start || (ci_smoke && smoke_name && std::string(smoke_name) == "tv"))
        set_tv(state, true, false);
    int frames = 0;
    bool smoke_rendered = false;
    std::string smoke_error;
    bool running = true;
    while (running && !state.done) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if(capture_keyboard_event(state,event))continue;
            if(capture_pad_event(state,event))continue;
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_DROPFILE && event.drop.file) {
                const auto dropped = from_utf8(event.drop.file);
                SDL_free(event.drop.file);
                std::error_code ec;
                try {
                    if (is_image(dropped)) {
                        if (const Game* game = chosen_game(state, lib)) set_cover(state, *game, dropped);
                        else notify(state, Toast::Kind::Info, "Sélectionnez d'abord un jeu",
                                    "L'image deviendra sa jaquette.");
                    } else if (fs::is_directory(dropped, ec)) {
                        import_folder(state, lib, dropped);
                    } else if (lib.add(dropped)) {
                        lib.save();
                        state.chosen = static_cast<int>(lib.games().size()) - 1;
                        notify(state, Toast::Kind::Success, "Jeu ajouté", display_title(lib.games().back()));
                    } else {
                        notify(state, Toast::Kind::Error, "Fichier non importé", "Formats acceptés : .gb, .gbc, .gba");
                    }
                } catch (const std::exception& e) {
                    notify(state, Toast::Kind::Error, "Bibliothèque non enregistrée", e.what());
                }
            }
        }
        if (state.ra_images) state.ra_images->begin_frame();
        // ImGui stops reading the controller while a button is being chosen,
        // and until it is released.
        if (state.pad_release_pending && state.editing_pad < 0 && !pad_input_held()) state.pad_release_pending = false;
        if (state.editing_pad >= 0 || state.pad_release_pending)
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        else
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        retro_client(state).pump(false);
        retro_events(state);
        render(state, lib);
        key_capture_dialog(state);
        pad_capture_dialog(state);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 11, 12, 18, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        const char* screenshot = SDL_getenv("RAVENEMUDSK_UI_SCREENSHOT");
        if (ci_smoke && frames == 9) {
            smoke_error = verify_smoke_frame(renderer, screenshot);
            smoke_rendered = smoke_error.empty();
            if (!smoke_rendered) running = false;
        }
        SDL_RenderPresent(renderer);
        if (ci_smoke && ++frames >= 12) running = false;
        SDL_Delay(8);
    }
    const bool cover = state.launch && state.launch->fullscreen;
    if (cover) {
        // Black on screen until the game draws its first frame in this window.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    state.ra_images.reset();
    if (state.ra_logo) SDL_DestroyTexture(state.ra_logo);
    if (state.logo) SDL_DestroyTexture(state.logo);
    for (auto& [key, texture] : state.images) if (texture) SDL_DestroyTexture(texture);
    if (cover) {
        (void)SDL_SetWindowHitTest(window, nullptr, nullptr);  // it points into `state`
        keep_screen({window, renderer});
    } else {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    SDL_QuitSubSystem(launcher_systems);
    if (ci_smoke && !smoke_rendered)
        throw std::runtime_error(smoke_error.empty() ? "Test de l'interface interrompu avant le rendu" : smoke_error);
    return state.launch;
}
Folders app_folders() {
    std::vector<fs::path> roots;
    if (char* base = SDL_GetBasePath()) {
        const auto path = from_utf8(base);
#ifdef __APPLE__
        // Keep raw executable builds portable, but never write into a signed app.
        if (utf8(path).find(".app/Contents/") == std::string::npos)
#endif
            roots.push_back(path);
        SDL_free(base);
    }
    if (char* pref = SDL_GetPrefPath("RavenEmu", "RavenEmuDSK")) {
        roots.push_back(from_utf8(pref));
        SDL_free(pref);
    }
    return prepare_folders(roots);
}
// Needs no SDL subsystem; never quits SDL, which would close a kept TV window.
void record_session(const LaunchRequest& request, unsigned long long seconds) {
    try {
        Library lib(data_file());
        lib.load();
        lib.mark_played(from_utf8(request.rom), seconds);
        lib.save();
    } catch (...) {}
}
} // namespace ravenemudsk_ui
