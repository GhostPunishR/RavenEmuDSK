// SPDX-License-Identifier: AGPL-3.0-only
// Exercise the production home view, including its private grid composition.
#include "../src/launcher/shell.hpp"
#include <imgui_internal.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace ravenemudsk_ui { using namespace shell; }

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
class PanelSession final : public ravenemudsk_ra::SessionStore {
public:
    std::optional<ravenemudsk_ra::SessionCredentials> load() override { return ravenemudsk_ra::SessionCredentials{"PanelUser","SyntheticToken"}; }
    bool save(const ravenemudsk_ra::SessionCredentials&) override { return true; }
    bool clear() override { return true; }
};
void exercise_detail_achievements(ravenemudsk_ui::State& state, const std::filesystem::path& root) {
    using namespace ravenemudsk_ui;
    auto profiles=ravenemudsk_ra::make_profile_store(root / "profiles");
    Library library(root / "detail-library.txt");
    for(const auto* directory:{"first","second","unknown"}) {
        const auto rom=root / directory / "Same title.gba";fs::create_directories(rom.parent_path());
        {std::ofstream out(rom);out<<"Different ROM fixture";}
        require(library.add(rom),"Could not add panel fixture");
    }
    ravenemudsk_ra::Snapshot a;a.username="PanelUser";a.rom_path=ravenemudsk_ra::rom_identity(library.games()[0].path);
    a.game_title="First edition";a.unlocked_count=1;a.total_count=3;a.unlocked_points=5;a.total_points=15;a.softcore_points=10;
    for(int i=0;i<3;++i) a.achievements.push_back({std::to_string(i),i==0?"First unlock":"Next goal","","","",5,i==0?2:1,0,i==0});
    require(profiles->save(a),"Could not cache first edition");
    auto b=a;b.rom_path=ravenemudsk_ra::rom_identity(library.games()[1].path);b.game_title="Second edition";
    b.unlocked_count=2;b.total_count=4;b.unlocked_points=10;b.total_points=20;b.softcore_points=42;
    b.achievements[0].title="Other edition unlock";b.achievements[1].unlocked=true;
    b.achievements.push_back({"last","Last goal","","","",5,1,0,false});
    require(profiles->save(b),"Could not cache second edition");
    ravenemudsk_ra::Client client(std::make_shared<PanelSession>(),
        [](const std::string&,const std::string&,const std::string&){return ravenemudsk_ra::ServerReply{"",-2};},profiles);
    state.retro=&client;
    auto render_detail=[&](int index) {
        ImGui::GetIO().DisplaySize={480.f,1100.f};
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({480,1100});
        ImGui::Begin("Panel regression",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGui::LogToBuffer();detail_panel(state,library,index,{0,0},{380,1050});
        const std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();ImGui::End();ImGui::Render();return text;
    };
    const auto first=render_detail(0);
    require(first.find("1 / 3")!=std::string::npos && first.find("5 / 15 points")!=std::string::npos &&
        first.find("First unlock")!=std::string::npos && first.find("Other edition") == std::string::npos,
        "Right panel must display the selected ROM's official counts and achievements");
    const auto second=render_detail(1);
    require(second.find("2 / 4")!=std::string::npos && second.find("Other edition unlock")!=std::string::npos &&
        second.find("First unlock")==std::string::npos,"Changing selection must switch the achievement data too");
    const auto missing=render_detail(2);
    require(missing.find("synchroniser ses succès")!=std::string::npos && missing.find("First unlock")==std::string::npos,
        "Unknown library ROMs must not display another game's successes or a false zero");
    state.ra_game_path=utf8(library.games()[0].path);
    const auto profile=profile_snapshot(state);
    require(profile.game_title==a.game_title && profile.unlocked_count==1 && profile.softcore_points==42,
        "The selected-game profile must keep current account points while showing the selected game");
    client.disconnect();
    const auto signed_out=render_detail(0);
    require(signed_out.find("Connectez votre compte")!=std::string::npos && signed_out.find("First unlock")==std::string::npos,
        "Logout must remove the former account's cached achievements from the panel");
    state.retro=nullptr;state.ra_game_path.clear();
}
void exercise_keyboard_capture(ravenemudsk_ui::State& state,const std::filesystem::path& root){
    using namespace ravenemudsk_ui;
    state.settings=Settings(root/"keyboard-settings.txt");state.settings.set("pseudo","Local profile");
    const auto render_capture=[&]{
        ImGui::GetIO().DisplaySize={1024,680};ImGui::NewFrame();
        ImGui::SetNextWindowSize({900,620});ImGui::Begin("Keyboard settings",nullptr,ImGuiWindowFlags_NoSavedSettings);
        settings_controls(state);ImGui::End();key_capture_dialog(state);ImGui::Render();
    };
    state.editing_key=static_cast<int>(KeyAction::A);render_capture();render_capture();
    auto* popup=ImGui::FindWindowByName("##key-capture");
    require(popup&&popup->Active&&!popup->Hidden,"Key capture dialog did not open");
    SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_c;event.key.repeat=1;
    require(capture_keyboard_event(state,event)&&state.editing_key>=0,"Held key repeat must not assign a binding");
    event.key.repeat=0;require(capture_keyboard_event(state,event)&&state.editing_key==-1,"New key was not captured exclusively");
    render_capture();Settings saved(state.settings.file());saved.load();KeyboardBindings bindings;bindings.load(saved);
    require(bindings.key(KeyAction::A)==SDLK_c&&saved.get("pseudo")=="Local profile","Capture did not persist the key or changed the profile");
    state.editing_key=static_cast<int>(KeyAction::LoadState);render_capture();
    event.key.keysym.sym=SDLK_ESCAPE;require(capture_keyboard_event(state,event)&&state.editing_key==-1,"Escape did not cancel capture");
    render_capture();saved.load();bindings.load(saved);require(bindings.key(KeyAction::LoadState)==SDLK_F9,"Cancelled key capture changed the shortcut");
    state.editing_key=static_cast<int>(KeyAction::B);render_capture();
    event.type=SDL_KEYUP;event.key.keysym.sym=SDLK_RETURN;
    require(!capture_keyboard_event(state,event)&&state.editing_key>=0,"Key releases must reach ImGui so activation keys cannot remain held");
    event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;capture_keyboard_event(state,event);
    require(state.editing_key==-1,"Focus loss must cancel key capture");render_capture();
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
    // "Jouer à deux": the games the cable joins, and the request it makes.
    require(linkable("gb", "gbc") && linkable("gbc", "gb") && linkable("gb", "gb") && linkable("gba", "gba") &&
                !linkable("gb", "gba") && !linkable("gba", "gbc") && !linkable("gba", "nes"),
            "Game Boy and Game Boy Color link together, the Game Boy Advance with itself");
    {
        State pairing;
        launch_pair(pairing, library.games()[0], library.games()[1]);
        require(pairing.done && pairing.launch && pairing.launch->rom == utf8(library.games()[0].path) &&
                    pairing.launch->partner_rom == utf8(library.games()[1].path) &&
                    pairing.launch->partner_system == "gba",
                "Jouer à deux launches both games");
        State single;
        launch(single, library.games()[0]);
        require(single.launch && single.launch->partner_rom.empty(), "A single game has no partner");
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
    exercise_detail_achievements(state, tmp.path);
    exercise_keyboard_capture(state,tmp.path);
    ImGui::DestroyContext();
    std::cout << "Home hover, selection and full title tooltips passed without ID conflicts\n";
}
