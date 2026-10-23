// SPDX-License-Identifier: AGPL-3.0-only
// One launcher frame, window hit testing and the UI smoke checks.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
void render(State& s, Library& lib) {
    s.interactive.clear();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(screen);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("RavenEmu##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (s.chosen >= static_cast<int>(lib.games().size())) s.chosen = lib.games().empty() ? -1 : 0;
    static bool update_told = false;  // one toast per run of RavenEmu
    if (!update_told && updates().status() == UpdateCheck::Status::Available) {
        update_told = true;
        if (const auto latest = updates().latest())
            notify(s, Toast::Kind::Info, "RavenEmu " + to_string(*latest) + " est disponible",
                   "Téléchargement depuis l'accueil ou Réglages, Mises à jour");
    }
    if (s.tv) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        tv_view(s, lib);
        ImGui::End();
        draw_toasts(s.toasts, s.fonts, 24.f);
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_RouteGlobal)) s.focus_search = true;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) set_tv(s, true);
    sidebar(s);
    header(s);
    const float detail_w = std::clamp(screen.x * .23f, 280.f, 360.f);
    const bool detail = chosen_game(s, lib) &&
        (s.page == Page::Home || s.page == Page::Library || s.page == Page::Recent);
    const ImVec2 content_pos{sidebar_w, header_h};
    const ImVec2 content_size{screen.x - sidebar_w - (detail ? detail_w : 0.f), screen.y - header_h};
    ImGui::SetCursorScreenPos(content_pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {content_pad, space::sm});
    ImGui::BeginChild("##content", content_size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::Dummy({0.f, space::xs});
    switch (s.page) {
    case Page::Home: home_view(s, lib); break;
    case Page::Library: library_view(s, lib); break;
    case Page::Recent: recent_view(s, lib); break;
    case Page::Settings: settings_view(s, lib); break;
    case Page::Profile: profile_view(s, lib); break;
    }
    ImGui::Dummy({0.f, content_pad});
    ImGui::EndChild();
    if (detail)
        detail_panel(s, lib, s.chosen, {screen.x - detail_w, header_h}, {detail_w, screen.y - header_h});
    window_controls(s);
    manual_path_dialog(s, lib);
    ImGui::End();
    draw_toasts(s.toasts, s.fonts, header_h);
}

SDL_HitTestResult SDLCALL hit_test(SDL_Window* window, const SDL_Point* pt, void* data) {
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) return SDL_HITTEST_NORMAL;
    constexpr int border = 6;
    const bool left = pt->x < border, right = pt->x >= width - border;
    const bool top = pt->y < border, bottom = pt->y >= height - border;
    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    const auto* state = static_cast<const State*>(data);
    if (state->tv || pt->y >= header_h || pt->x < sidebar_w) return SDL_HITTEST_NORMAL;
    // The header is the title bar, except for its interactive controls.
    for (const auto& r : state->interactive)
        if (pt->x >= r.x && pt->x < r.z && pt->y >= r.y && pt->y < r.w) return SDL_HITTEST_NORMAL;
    return SDL_HITTEST_DRAGGABLE;
}
Page smoke_page() {
    const char* page = SDL_getenv("RAVENEMUDSK_UI_SMOKE_PAGE");
    const std::string name = page ? page : "";
    if (name == "library") return Page::Library;
    if (name == "recent") return Page::Recent;
    // Keep the old smoke-test route pointing to the single Profile dashboard.
    if (name == "achievements") return Page::Profile;
    if (name == "settings") return Page::Settings;
    if (name == "profile") return Page::Profile;
    return Page::Home;
}
std::string verify_smoke_frame(SDL_Renderer* renderer, const char* screenshot) {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 || height <= 0)
        return std::string("Dimensions du rendu SDL invalides : ") + SDL_GetError();
    const auto free_surface = [](SDL_Surface* surface) { SDL_FreeSurface(surface); };
    std::unique_ptr<SDL_Surface, decltype(free_surface)> image(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888), free_surface);
    if (!image) return std::string("Capture SDL impossible : ") + SDL_GetError();
    if (SDL_RenderReadPixels(renderer, nullptr, image->format->format, image->pixels, image->pitch) != 0)
        return std::string("Lecture du rendu SDL impossible : ") + SDL_GetError();
    std::unordered_set<Uint32> colors;
    for (int y = 0; y < height && colors.size() < 16; ++y) {
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(image->pixels) + y * image->pitch);
        for (int x = 0; x < width && colors.size() < 16; ++x) colors.insert(row[x]);
    }
    if (colors.size() < 16) return "Rendu SDL vide ou incomplet";
    if (screenshot && SDL_SaveBMP(image.get(), screenshot) != 0)
        return std::string("Enregistrement de la capture impossible : ") + SDL_GetError();
    return {};
}
} // namespace ravenemudsk_ui::shell
