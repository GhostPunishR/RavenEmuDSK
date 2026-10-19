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
    ImGui::DestroyContext();
    std::cout << "Home game hover and selection passed without ID conflicts\n";
}
