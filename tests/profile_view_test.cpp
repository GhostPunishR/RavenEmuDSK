// SPDX-License-Identifier: AGPL-3.0-only
// Render the production Profile page with a synthetic RetroAchievements
// account: stable account and game columns on every tab and width, search,
// and the sign-out confirmation that announces unsent submissions.
#include "../src/ui/app_shell.cpp"
#include "retro_test_storage.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
ravenemudsk_ra::ServerReply serve(const std::string& post) {
    if (post.starts_with("r=login"))
        return {R"({"Success":true,"User":"GameUser","Token":"Token","Score":40,"SoftcoreScore":12,"Messages":0})", 200};
    if (post.starts_with("r=achievementsets"))
        return {R"({"Success":true,"GameId":1234,"Title":"Synthetic game","ConsoleId":4,"ImageIconUrl":"https://media.retroachievements.org/Images/1.png","RichPresenceGameId":1234,"RichPresencePatch":"Display:\nSynthetic test","Sets":[{"AchievementSetId":1111,"GameId":1234,"Title":null,"Type":"core","ImageIconUrl":"https://media.retroachievements.org/Images/1.png","Achievements":[)"
                R"({"ID":11,"Title":"Premier pas","Description":"Commencer","Flags":3,"Points":5,"MemAddr":"0xH00c001=1","Author":"T","BadgeName":"1","Created":1,"Modified":1,"Type":"progression","Rarity":80.5,"RarityHardcore":40.2},)"
                R"({"ID":12,"Title":"Pêcheur confirmé","Description":"Pêcher","Flags":3,"Points":10,"MemAddr":"0xH00c001=2","Author":"T","BadgeName":"2","Created":1,"Modified":1,"Type":"missable","Rarity":12.3,"RarityHardcore":3.1},)"
                R"({"ID":13,"Title":"Maître","Description":"Gagner","Flags":3,"Points":25,"MemAddr":"0xH00c001=3","Author":"T","BadgeName":"3","Created":1,"Modified":1,"Type":"win_condition"}],"Leaderboards":[]}]})", 200};
    // The profile refresh reads unlocks without opening a game session.
    if (post.starts_with("r=unlocks"))
        return {post.find("&h=1") != std::string::npos ? R"({"Success":true,"UserUnlocks":[]})" :
                R"({"Success":true,"UserUnlocks":[11]})", 200};
    if (post.starts_with("r=awardachievement"))
        return {R"({"Success":false,"Error":"Access denied.","Code":"access_denied","Status":403})", 403};
    return {R"({"Success":true})", 200};
}
template<class Done> void pump_until(ravenemudsk_ra::Client& client, Done done, const char* what) {
    for (int i = 0; i < 3000; ++i) {
        client.pump(false);
        if (done()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error(std::string("Timed out: ") + what + " (" + client.snapshot().status + ")");
}
// One frame of the profile; returns the text that ImGui logged, popups included.
std::string frame(ravenemudsk_ui::State& state, ravenemudsk_ui::Library& library, ImVec2 size) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60.f;
    io.AddMousePosEvent(size.x * .6f, size.y * .6f);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("Profile regression", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::LogToBuffer();
    ravenemudsk_ui::achievements_view(state, library);
    const std::string logged = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::End();
    ImGui::Render();
    require(ImGui::GetCurrentContext()->DebugDrawIdConflicts == 0, "The profile has conflicting widget IDs");
    return logged;
}
} // namespace

int main() {
    using namespace ravenemudsk_ui;
    retro_test::Directory root;
    retro_test::Vaults keys;
    auto queue = std::make_shared<ravenemudsk_ra::Outbox>(root.path / "pending", keys.factory());
    ravenemudsk_ra::Client client(std::make_shared<retro_test::Vault>(), [](const std::string&, const std::string& post, const std::string&) {
        return serve(post);
    }, {}, {}, queue);

    std::vector<std::uint8_t> rom(0x8000);
    const auto rom_path = root.path / "Synthetic.gb";
    { std::ofstream out(rom_path, std::ios::binary); out.write(reinterpret_cast<const char*>(rom.data()), rom.size()); }
    Library library(root.path / "library.txt");
    require(library.add(rom_path), "Could not add the ROM fixture");

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigDebugHighlightIdConflicts = true;
    State state;
    state.folders = prepare_folders({root.path / "data"});
    state.fonts = load_fonts(false);
    state.ra_images = std::make_unique<BadgeCache>(nullptr);
    state.retro = &client;
    state.library = &library;
    apply_theme();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Signed out: the sign-in form and the mode card are shown.
    auto logged = frame(state, library, {1280.f, 1000.f});
    require(logged.find("Mode Hardcore") != std::string::npos, "Signed-out profile lost the mode card");

    client.login("GameUser", "Password");
    pump_until(client, [&] { return client.snapshot().logged_in; }, "login");
    require(client.refresh_game(utf8(rom_path)), "Profile refresh refused");
    pump_until(client, [&] { return client.snapshot().game_status == ravenemudsk_ra::GameStatus::Ready; }, "game");
    // An unlock the service keeps refusing stays queued, and pending.
    ravenemudsk_ra::Submission refused;
    refused.username = "GameUser"; refused.game_hash = std::string(32, 'a'); refused.object_id = 99; refused.earned_at = 1700000000;
    require(!queue->put(refused).empty(), "Could not queue the unsent submission");
    pump_until(client, [&] { return client.snapshot().pending_submissions == 1; }, "pending submission");

    // The game and the mode stay visible on every tab, wide or narrow.
    for (const float window_width : {1280.f, 860.f}) {
        for (int tab = 0; tab < 3; ++tab) {
            state.ra_tab = tab;
            logged = frame(state, library, {window_width, 1000.f});
            require(logged.find("1 / 3 succès") != std::string::npos && logged.find("Mode Hardcore") != std::string::npos,
                    "Game progress or mode missing on tab " + std::to_string(tab) + " at " + std::to_string(int(window_width)));
        }
    }
    state.ra_tab = 0;
    std::snprintf(state.ra_search, sizeof(state.ra_search), "%s", "introuvable");
    logged = frame(state, library, {1280.f, 1000.f});
    require(logged.find("Aucun succès ne correspond") != std::string::npos, "An empty search must say so");
    state.ra_search[0] = '\0';

    // Signing out asks first, and says that the unsent submission would be lost.
    state.ra_confirm_disconnect = true;
    frame(state, library, {1280.f, 1000.f});
    logged = frame(state, library, {1280.f, 1000.f});
    require(logged.find("Se déconnecter de RetroAchievements ?") != std::string::npos &&
            logged.find("1 envoi pas encore transmis") != std::string::npos,
            "The sign-out confirmation must announce unsent submissions");
    require(client.snapshot().logged_in && queue->entries("GameUser").size() == 1,
            "Asking to sign out must not sign out before confirmation");

    // A Hardcore preference the client refuses (no journal here) is saved as Softcore,
    // so the refusal is not repeated at every launch.
    ravenemudsk_ra::Client plain(std::make_shared<retro_test::Vault>(), [](const std::string&, const std::string& post,
                                 const std::string&) { return serve(post); });
    plain.login("GameUser", "Password");
    pump_until(plain, [&] { return plain.snapshot().logged_in; }, "login without journal");
    state.retro = &plain;
    state.settings = Settings(root.path / "reglages.txt");
    state.settings.set_flag("ra_hardcore", true);
    plain.drain_events();
    plain.set_hardcore(true);
    retro_events(state);
    Settings saved(state.settings.file());
    saved.load();
    require(!saved.flag("ra_hardcore", true), "A refused Hardcore preference must be saved as Softcore");

    // "Mes jeux" reads the per-ROM cache, offline: a saved session whose reconnection fails.
    auto profiles = ravenemudsk_ra::make_profile_store(root.path / "profiles");
    Library games(root.path / "games.txt");
    std::vector<std::filesystem::path> roms;
    for (const char* name : {"Mastered.gb", "Started.gb", "Never synced.gb"}) {
        roms.push_back(root.path / name);
        { std::ofstream out(roms.back(), std::ios::binary); out << "Library fixture " << name; }
        require(games.add(roms.back()), "Could not add a library fixture");
    }
    games.mark_played(roms[1], 60);
    const auto cache = [&](const std::filesystem::path& rom, const char* title, int unlocked, int total, bool hardcore) {
        ravenemudsk_ra::Snapshot game;
        game.username = "GameUser"; game.rom_path = ravenemudsk_ra::rom_identity(rom); game.game_title = title;
        game.unlocked_count = unlocked; game.total_count = total; game.game_hardcore = hardcore;
        game.unlocked_points = unlocked * 5; game.total_points = total * 5;
        for (int i = 0; i < total; ++i)
            game.achievements.push_back({title + std::to_string(i), "Goal", "", "", "", 5, 1, 0.f, i < unlocked});
        require(profiles->save(game), "Could not cache a library game");
    };
    cache(roms[0], "Jeu maîtrisé", 3, 3, true);
    cache(roms[1], "Jeu commencé", 1, 4, false);
    auto saved_session = std::make_shared<retro_test::Vault>();
    saved_session->value = ravenemudsk_ra::SessionCredentials{"GameUser", "Token"};
    ravenemudsk_ra::Client offline(saved_session, [](const std::string&, const std::string&, const std::string&) {
        return ravenemudsk_ra::ServerReply{"", -2};
    }, profiles);
    state.retro = &offline;
    state.ra_tab = 3;
    logged = frame(state, games, {1280.f, 1000.f});
    require(!offline.snapshot().logged_in && logged.find("Mes jeux RetroAchievements") != std::string::npos &&
            logged.find("2 jeux \xC2\xB7 1 maîtrisé \xC2\xB7 4 / 7 succès") != std::string::npos &&
            logged.find("1 jeu de la bibliothèque n'a pas de succès mémorisés") != std::string::npos,
            "My games must summarise the cached library offline");
    require(state.ra_games.size() == 2 && state.ra_games[0].title == "Jeu commencé" &&
            std::string(ravenemudsk_ra::completion_label(state.ra_games[1])) == "Maîtrisé",
            "My games must list recognized games, most recently played first");
    ra_open_game(state, state.ra_games[1].path);
    logged = frame(state, games, {1280.f, 1000.f});
    require(state.ra_tab == 0 && logged.find("Jeu maîtrisé") != std::string::npos && logged.find("3 / 3 succès") != std::string::npos,
            "Opening a game from My games must show its achievements");

    // TV mode is driven by the controller: no mouse while it lasts.
    state.library = &games;
    set_tv(state, true, false);
    require((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) != 0, "TV mode must ignore the mouse");
    set_tv(state, false, false);
    require((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) == 0, "Desktop mode must use the mouse again");

    // One window passes from the launcher to the game and back in TV mode.
    require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "SDL video unavailable");
    SDL_Window* screen = SDL_CreateWindow("TV", 0, 0, 320, 240, SDL_WINDOW_HIDDEN);
    require(screen != nullptr && take_screen() == nullptr, "No window is kept at first");
    keep_screen(screen);
    require(take_screen() == screen && take_screen() == nullptr, "A kept window is taken once");
    keep_screen(screen);
    close_screen();
    require(take_screen() == nullptr, "Closing destroys the kept window");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    ImGui::DestroyContext();
    std::cout << "Profile columns, tabs, search, sign-out confirmation, Hardcore refusal, My games, TV mouse and window handover passed\n";
}
