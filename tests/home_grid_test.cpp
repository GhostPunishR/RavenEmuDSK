// SPDX-License-Identifier: AGPL-3.0-only
// Exercise the production home view, including its private grid composition.
#include "../src/ui/app_shell.cpp"
#include <imgui_internal.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TemporaryFolder {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("raven-home-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryFolder() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void exercise_title_tooltip(ravenemudsk_ui::State& state, ravenemudsk_ui::Library& library,
                            const char* region) {
    using namespace ravenemudsk_ui;
    auto& io = ImGui::GetIO();
    const auto expected = display_title(library.games().front());
    const bool recent = std::string(region) == "recent-row";
    io.DisplaySize = {800.f, 500.f};
    io.DeltaTime = .1f;
    ImVec2 hover{-100.f, -100.f};
    bool title_seen = false;
    bool action_seen = false;
    for (int frame = 0; frame < 12; ++frame) {
        io.AddMousePosEvent(hover.x, hover.y);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.f, 0.f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin(region, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        const auto origin = ImGui::GetCursorScreenPos();
        constexpr float card_width = 180.f;
        if (recent) hover = {origin.x + 100.f, origin.y + 30.f};
        else if (std::string(region) == "play-area")
            hover = {origin.x + card_width - 28.f, origin.y + card_width - 28.f};
        else if (std::string(region) == "title-area")
            hover = {origin.x + 20.f, origin.y + card_width + 18.f};
        else hover = {origin.x + 40.f, origin.y + 40.f};
        // Capture the text actually submitted by the production tooltip.
        // Card labels use draw-list text and do not enter ImGui's log buffer.
        ImGui::LogToBuffer();
        if (recent) recent_view(state, library);
        else game_card(state, library.games().front(), 0, card_width);
        const auto& context = *ImGui::GetCurrentContext();
        const std::string logged = context.LogBuffer.c_str();
        bool tooltip_visible = false;
        for (const auto* window : context.Windows)
            if ((window->Flags & ImGuiWindowFlags_Tooltip) && window->Active && !window->Hidden) {
                tooltip_visible = true;
                require(window->Size.x <= io.DisplaySize.x, "The full title tooltip must fit on screen");
            }
        title_seen |= tooltip_visible && logged.find(expected) != std::string::npos;
        action_seen |= tooltip_visible && logged.find("Jouer") != std::string::npos;
        ImGui::LogFinish();
        ImGui::End();
        ImGui::Render();
        require(context.HoveredIdPreviousFrameItemCount <= 1 && context.DebugDrawIdConflicts == 0,
                "Title tooltips must not reintroduce ImGui ID conflicts");
    }
    if (!title_seen)
        throw std::runtime_error(std::string("Missing full game title tooltip over ") + region + ": " + expected);
    if (std::string(region) == "play-area")
        require(action_seen, "The play area must also explain its action");
}
}
int main() {
    using namespace ravenemudsk_ui;
    TemporaryFolder tmp;
    fs::create_directories(tmp.path);
    Library library(tmp.path / "library.txt");
    for (const char* name : {"Game A.gba", "Game B.gba"}) {
        const auto rom = tmp.path / name;
        { std::ofstream out(rom); out << "UI fixture, not a playable ROM"; }
        require(library.add(rom), "Could not add the game fixture");
        library.mark_played(rom, 60);
    }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1400.f, 1600.f};
    io.DeltaTime = 1.f / 60.f;
    // Keep ImGui's own conflict detection enabled, just as in the application.
    io.ConfigDebugHighlightIdConflicts = true;
    State state;
    state.chosen = 0;
    state.folders = prepare_folders({tmp.path / "data"});
    state.fonts = load_fonts(true);
    state.ra_images = std::make_unique<BadgeCache>(nullptr);
    apply_theme();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    require(pixels && width > 0 && height > 0, "Font atlas is missing");

    ImVec2 hover{};
    bool hovered = false;
    for (int frame = 0; frame < 8; ++frame) {
        if (frame >= 2) io.AddMousePosEvent(hover.x, hover.y);
        if (frame == 5) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        if (frame == 6) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.f, 0.f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Home regression", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        home_view(state, library);
        // The last item is Game B in the library row; the same game is also
        // displayed in the recent row above. Both copies are visible here.
        if (frame == 1) {
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            hover = {(min.x + max.x) * .5f, (min.y + max.y) * .5f};
            require(hover.y < io.DisplaySize.y, "The duplicate-card fixture must be visible");
        }
        hovered |= ImGui::IsItemHovered();
        ImGui::End();
        ImGui::Render();
        const auto& context = *ImGui::GetCurrentContext();
        require(context.HoveredIdPreviousFrameItemCount <= 1 && context.DebugDrawIdConflicts == 0,
                "Hovering a game shared by the recent and library rows caused an ImGui ID conflict");
    }
    require(hovered, "The regression test did not actually hover the game");
    require(state.chosen == 1, "Clicking the library copy must select the intended game");
    for (const char* name : {"Short.gba",
            "Pokemon - Edition speciale 100% - Un titre vraiment tres long avec toutes les informations du jeu.gba"}) {
        const auto rom = tmp.path / name;
        { std::ofstream out(rom); out << "UI fixture, not a playable ROM"; }
        Library single(tmp.path / "tooltip-library.txt");
        require(single.add(rom), "Could not add tooltip fixture");
        single.mark_played(rom, 60);
        for (const char* region : {"cover-area", "title-area", "play-area", "recent-row"})
            exercise_title_tooltip(state, single, region);
    }
    ImGui::DestroyContext();
    std::cout << "Home hover, selection and full title tooltips passed without ID conflicts\n";
}
