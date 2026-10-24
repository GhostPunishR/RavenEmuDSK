// SPDX-License-Identifier: AGPL-3.0-only
// The launcher's screens share this state and these functions. Internal to
// the launcher library and its UI tests; the program sees app_shell.hpp.
#pragma once
#include <ravenemudsk/launcher/app_shell.hpp>
#include <ravenemudsk/ui/image_loader.hpp>
#include <ravenemudsk/library/library.hpp>
#include <ravenemudsk/settings/settings.hpp>
#include <ravenemudsk/library/storage.hpp>
#include <ravenemudsk/ui/theme.hpp>
#include <ravenemudsk/updates/updates.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <ravenemudsk/achievements/retro_widgets.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemudsk/achievements/achievement_list.hpp>
#include <ravenemudsk/achievements/library_progress.hpp>
#include <ravenemudsk/input/pad_glyphs.hpp>
#include <ravenemudsk/input/game_input.hpp>
#include <rc_client.h>
#include <ravenemudsk/achievements/badge_cache.hpp>
#include <SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef RAVENEMUDSK_VERSION
#define RAVENEMUDSK_VERSION "0.0.0"
#endif

namespace ravenemudsk_ui::shell {
namespace fs = std::filesystem;

inline constexpr float sidebar_w = 88.f;
inline constexpr float header_h = 76.f;
inline constexpr float card_min_w = 164.f;
inline constexpr float card_gap = 20.f;
inline constexpr float content_pad = 32.f;

enum class Page { Home, Library, Recent, Settings, Profile };
enum class SettingsCategory { Library, Display, Sound, Controls, Console, Link, Updates, About };
// What the "chemin" dialog imports: a ROM or folder, a cover, or the avatar.
enum class PathTarget { Rom, Cover, Avatar };
struct State {
    Page page = Page::Home;
    SettingsCategory settings_category = SettingsCategory::Library;
    int chosen = -1;
    int system_filter = 0;
    int sort = 0;
    char search[192]{};
    char manual_path[1024]{};
    bool done = false;
    bool open_manual = false;
    PathTarget path_target = PathTarget::Rom;
    char ra_user[128]{};
    char ra_password[128]{};
    char pseudo[128]{};  // 24 characters of up to 4 UTF-8 bytes each
    Settings settings;
    int editing_key=-1;
    std::string key_error;
    // Controller button being chosen in Commandes, as for editing_key.
    int editing_pad=-1;
    std::string pad_error;
    // After a choice, the controller stays away from the interface until all
    // its buttons are released: none reaches it as a new press.
    bool pad_release_pending=false;
    Library* library = nullptr;
    bool tv = false;          // full screen TV / gamepad mode
    int tv_category = 0;
    int tv_index = 0;
    float tv_scroll = 0.f;    // animated towards tv_index
    int tv_menu = -1;         // TV menu: highlighted entry, -1 while closed
    bool focus_search = false;
    std::optional<LaunchRequest> launch;
    Fonts fonts;
    Folders folders;
    SDL_Texture* logo = nullptr;
    SDL_Texture* ra_logo = nullptr;
    std::unique_ptr<BadgeCache> ra_images;
    int ra_filter = 0;
    int ra_tab = 0;
    int ra_visible_count = 10;
    int ra_sort = 0;
    char ra_search[128]{};
    bool ra_confirm_disconnect = false;
    // "Mes jeux": progress read from the per-ROM cache, rebuilt only when its key changes.
    int ra_games_sort = 0, ra_games_visible = 20;
    std::vector<ravenemudsk_ra::GameProgress> ra_games;
    std::size_t ra_games_unsynced = 0;
    std::string ra_games_key;
    std::string ra_selected_achievement;
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    std::unordered_map<std::string, SDL_Texture*> images;
    ravenemudsk_ra::Client* retro = nullptr; // Optional injected client for UI regression tests.
    std::string ra_game_path;
    std::vector<Toast> toasts;
    // Clickable rectangles inside the draggable title area, for hit testing.
    std::vector<ImVec4> interactive;
};

// preferences.cpp
fs::path settings_file();
// common.cpp
std::string ago(std::int64_t stamp);
std::string duration_of(std::uint64_t seconds);
std::string lower(std::string text);
std::optional<fs::path> pick_image();
fs::path data_file();
ravenemudsk_ra::Client& retro_client(State& s);
std::optional<ravenemudsk_ra::Snapshot> game_achievements(State& s, const Game& game);
std::string achievement_count(const ravenemudsk_ra::Snapshot& snap);
ravenemudsk_ra::Snapshot profile_snapshot(State& s);
SDL_Texture* image_for(State& s, const std::string& key, std::initializer_list<fs::path> paths);
SDL_Texture* cover_for(State& s, const Game& game);
void forget_image(State& s, const std::string& key);
SDL_Texture* preview_for(State& s, const Game& game);
SDL_Texture* load_brand_asset(SDL_Renderer* renderer, const char* name);
SDL_Texture* load_logo(SDL_Renderer* renderer);
std::vector<int> library_order(const Library& lib, int sort, const State* filter = nullptr);
std::vector<int> recent_order(const Library& lib);
const Game* chosen_game(const State& s, const Library& lib);
void launch(State& s, const Game& game);
void notify(State& s, Toast::Kind kind, std::string title, std::string body = {});
void add_game(State& s, Library& lib);
void import_folder(State& s, Library& lib, const fs::path& folder);
void set_cover(State& s, const Game& game, const fs::path& image);
void open_folder(const fs::path& folder);
void pick_and_import_folder(State& s, Library& lib);
void text_at(ImDrawList* d, ImFont* font, ImVec2 pos, ImU32 col, const std::string& text);
float text_width(ImFont* font, const char* text);
bool section_header(State& s, const char* title, const char* action = nullptr);
float card_height(float width);
void game_title_tooltip(const std::string& title, bool play_action = false);
void empty_state(State& s, Library& lib, Icon icon, const char* title, const char* text, bool actions);
void begin_card(const char* id, float width = 0.f);
void end_card(bool spacing = true);
void card_title(State& s, Icon icon, const char* title);
void muted(const char* text);
// chrome.cpp
void sidebar(State& s);
void window_controls(State& s);
void header(State& s);
// library_view.cpp
void game_card(State& s, const Game& game, int index, float w);
void home_view(State& s, Library& lib);
void library_view(State& s, Library& lib);
void recent_view(State& s, Library& lib);
// profile_view.cpp
void ra_open_game(State& s, const std::string& path);
void achievements_view(State& s, Library& lib);
void ra_mode_controls(State& s);
void ra_login_controls(State& s, ravenemudsk_ra::Client& ra);
std::string player_name(const State& s);
void draw_player_avatar(State& s, ImDrawList* d, ImVec2 center, float r);
void draw_ra_brand(State& s, ImDrawList* d, ImVec2 center, float width);
void draw_ra_identity(State& s, ImDrawList* d, ImVec2 center, float radius);
void retro_events(State& s);
void save_settings(State& s);
void set_avatar(State& s, const fs::path& image);
void profile_banner(State& s, Library&);
void profile_view(State& s, Library& lib);
// tv_view.cpp
bool& tv_this_run();
void set_tv(State& s, bool on, bool remember = true);
bool pressed(std::initializer_list<ImGuiKey> keys, bool repeat);
void tv_view(State& s, Library& lib);
// updates_view.cpp
UpdateCheck& updates();
void update_banner(State& s);
void updates_card(State& s);
// settings_view.cpp
void settings_controls(State& s);
void key_capture_dialog(State& s);
void pad_capture_dialog(State& s);
bool capture_pad_event(State& s,const SDL_Event& event);
bool pad_input_held();
bool capture_keyboard_event(State& s,const SDL_Event& event);
void settings_view(State& s, Library& lib);
// game_detail.cpp
void detail_panel(State& s, Library& lib, int index, ImVec2 pos, ImVec2 size);
void manual_path_dialog(State& s, Library& lib);
// launcher_frame.cpp
void render(State& s, Library& lib);
SDL_HitTestResult SDLCALL hit_test(SDL_Window* window, const SDL_Point* pt, void* data);
Page smoke_page();
std::string verify_smoke_frame(SDL_Renderer* renderer, const char* screenshot);
} // namespace ravenemudsk_ui::shell
