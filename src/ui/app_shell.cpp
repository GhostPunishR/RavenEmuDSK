// SPDX-License-Identifier: AGPL-3.0-only
// RavenEmu launcher: library, recent games, achievements and settings.
#include <ravenemudsk/ui/app_shell.hpp>
#include <ravenemudsk/ui/image_loader.hpp>
#include <ravenemudsk/ui/library.hpp>
#include <ravenemudsk/ui/settings.hpp>
#include <ravenemudsk/ui/storage.hpp>
#include <ravenemudsk/ui/theme.hpp>
#include <ravenemudsk/ui/updates.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <ravenemudsk/ui/retro_widgets.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemudsk/achievements/achievement_list.hpp>
#include <ravenemudsk/achievements/library_progress.hpp>
#include <ravenemudsk/ui/pad_glyphs.hpp>
#include <rc_client.h>
#include <ravenemudsk/ui/badge_cache.hpp>
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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#ifndef RAVENEMUDSK_VERSION
#define RAVENEMUDSK_VERSION "0.0.0"
#endif

namespace ravenemudsk_ui {
namespace {
namespace fs = std::filesystem;

constexpr float sidebar_w = 88.f;
constexpr float header_h = 76.f;
constexpr float card_min_w = 164.f;
constexpr float card_gap = 20.f;
constexpr float content_pad = 32.f;

enum class Page { Home, Library, Recent, Settings, Profile };
enum class SettingsCategory { Library, Display, Controls, Retro, Updates, About };
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

// Defined with the profile page, used by the sidebar and the home page.
void draw_player_avatar(State& s, ImDrawList* d, ImVec2 center, float r);
void draw_ra_identity(State& s, ImDrawList* draw, ImVec2 center, float radius);
void draw_ra_brand(State& s, ImDrawList* draw, ImVec2 center, float width);
void profile_banner(State& s, Library& lib);
void set_tv(State& s, bool on, bool remember = true);
void text_at(ImDrawList* d, ImFont* font, ImVec2 pos, ImU32 col, const std::string& value);
float text_width(ImFont* font, const char* value);
void ra_login_controls(State& s, ravenemudsk_ra::Client& ra);
void update_banner(State& s);
void save_settings(State& s);
void ra_mode_controls(State& s);

// ---------------------------------------------------------------- data
std::string date_of(std::int64_t stamp) {
    if (!stamp) return "Jamais";
    const auto raw = static_cast<std::time_t>(stamp);
    std::tm date{};
#ifdef _WIN32
    if (localtime_s(&date, &raw) != 0) return "Date inconnue";
#else
    if (!localtime_r(&raw, &date)) return "Date inconnue";
#endif
    std::array<char, 40> chars{};
    if (!std::strftime(chars.data(), chars.size(), "%d/%m/%Y", &date)) return "Date inconnue";
    return chars.data();
}
std::string ago(std::int64_t stamp) {
    if (!stamp) return "Jamais joué";
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    const auto delta = std::max<std::int64_t>(0, now - stamp);
    if (delta < 60) return "À l'instant";
    if (delta < 3600) return "Il y a " + std::to_string(delta / 60) + " min";
    if (delta < 86400) return "Il y a " + std::to_string(delta / 3600) + " h";
    if (delta < 2 * 86400) return "Hier";
    if (delta < 7 * 86400) return "Il y a " + std::to_string(delta / 86400) + " jours";
    return "Le " + date_of(stamp);
}
std::string duration_of(std::uint64_t seconds) {
    if (!seconds) return "Pas encore joué";
    if (seconds < 60) return "Moins d'1 min";
    const auto hours = seconds / 3600, mins = (seconds / 60) % 60;
    if (!hours) return std::to_string(mins) + " min";
    return std::to_string(hours) + " h " + (mins < 10 ? "0" : "") + std::to_string(mins);
}
std::string lower(std::string text) {
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}
std::optional<fs::path> pick_rom() {
#ifdef _WIN32
    std::array<wchar_t, 32768> filename{};
    OPENFILENAMEW dlg{};
    dlg.lStructSize = sizeof(dlg);
    dlg.hwndOwner = GetActiveWindow();
    dlg.lpstrFile = filename.data();
    dlg.nMaxFile = static_cast<DWORD>(filename.size());
    dlg.lpstrTitle = L"RavenEmu - Ajouter un jeu";
    dlg.lpstrFilter = L"ROM Game Boy (*.gb;*.gbc;*.gba)\0*.gb;*.gbc;*.gba\0Tous les fichiers (*.*)\0*.*\0";
    dlg.nFilterIndex = 1;
    dlg.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dlg)) return fs::path(filename.data());
#endif
    return std::nullopt;
}
std::optional<fs::path> pick_image() {
#ifdef _WIN32
    std::array<wchar_t, 32768> filename{};
    OPENFILENAMEW dlg{};
    dlg.lStructSize = sizeof(dlg);
    dlg.hwndOwner = GetActiveWindow();
    dlg.lpstrFile = filename.data();
    dlg.nMaxFile = static_cast<DWORD>(filename.size());
    dlg.lpstrTitle = L"RavenEmu - Choisir une jaquette";
    dlg.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0";
    dlg.nFilterIndex = 1;
    dlg.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dlg)) return fs::path(filename.data());
#endif
    return std::nullopt;
}
std::optional<fs::path> pick_folder() {
#ifdef _WIN32
    BROWSEINFOW info{};
    info.hwndOwner = GetActiveWindow();
    info.lpszTitle = L"RavenEmu - Sélectionner un dossier de ROMs";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    const auto item = SHBrowseForFolderW(&info);
    if (!item) return std::nullopt;
    std::array<wchar_t, MAX_PATH> out{};
    const bool ok = SHGetPathFromIDListW(item, out.data()) != 0;
    CoTaskMemFree(item);
    if (ok) return fs::path(out.data());
#endif
    return std::nullopt;
}
fs::path data_file() {
    char* ptr = SDL_GetPrefPath("RavenEmuDSK", "RavenEmuDSK");
    if (!ptr) return "ravenemudsk-library.txt";
    const auto result = from_utf8(ptr) / "bibliotheque.txt";
    SDL_free(ptr);
    return result;
}
ravenemudsk_ra::Client& retro_client(State& s) {
    return s.retro ? *s.retro : ravenemudsk_ra::client();
}
std::optional<ravenemudsk_ra::Snapshot> game_achievements(State& s, const Game& game) {
    return retro_client(s).game_snapshot(utf8(game.path));
}
std::string achievement_count(const ravenemudsk_ra::Snapshot& snap) {
    return std::to_string(snap.unlocked_count) + " / " + std::to_string(snap.total_count);
}
ravenemudsk_ra::Snapshot profile_snapshot(State& s) {
    auto snap = retro_client(s).snapshot();
    if (!s.ra_game_path.empty()) {
        if (const auto game = retro_client(s).game_snapshot(s.ra_game_path)) {
            snap.game_title = game->game_title;
            snap.rom_path = game->rom_path;
            snap.achievements = game->achievements;
            snap.unlocked_count = game->unlocked_count; snap.total_count = game->total_count;
            snap.unlocked_points = game->unlocked_points; snap.total_points = game->total_points;
            snap.game_hash=game->game_hash;snap.rich_presence=game->rich_presence;snap.leaderboards=game->leaderboards;
            snap.game_hardcore=game->game_hardcore;
            snap.cached_profile = snap.cached_profile || game->cached_profile;
        } else s.ra_game_path.clear();
    }
    return snap;
}
SDL_Texture* image_for(State& s, const std::string& key, std::initializer_list<fs::path> paths) {
    if (auto it = s.images.find(key); it != s.images.end()) return it->second;
    SDL_Texture* found = nullptr;
    for (const auto& path : paths)
        if ((found = load_local_image(s.renderer, path))) break;
    s.images.emplace(key, found);
    return found;
}
SDL_Texture* cover_for(State& s, const Game& game) {
    const auto key = utf8(game.path) + ":cover";
    if (auto it = s.images.find(key); it != s.images.end()) return it->second;
    SDL_Texture* found = nullptr;
    for (const auto& path : cover_candidates(s.folders, game.path))
        if ((found = load_local_image(s.renderer, path))) break;
    s.images.emplace(key, found);
    return found;
}
void forget_image(State& s, const std::string& key) {
    if (auto it = s.images.find(key); it != s.images.end()) {
        if (it->second) SDL_DestroyTexture(it->second);
        s.images.erase(it);
    }
}
SDL_Texture* preview_for(State& s, const Game& game) {
    return image_for(s, utf8(game.path) + ":preview",
                     {preview_path(s.folders, game.path)});
}
SDL_Texture* load_brand_asset(SDL_Renderer* renderer, const char* name) {
    char* base = SDL_GetBasePath();
    if (!base) return nullptr;
    const auto path = from_utf8(base) / "assets" / name;
    SDL_free(base);
    return load_local_image(renderer, path);
}
SDL_Texture* load_logo(SDL_Renderer* renderer) {
    return load_brand_asset(renderer, "ravenemu-logo.png");
}
bool passes(const Game& g, const State& s) {
    static const char* systems[] = {"", "gb", "gbc", "gba"};
    if (s.system_filter > 0 && g.system != systems[s.system_filter]) return false;
    return !s.search[0] || lower(display_title(g)).find(lower(s.search)) != std::string::npos;
}
// Sort 0 = title, 1 = last played, 2 = play time. `filter` applies search and system.
std::vector<int> library_order(const Library& lib, int sort, const State* filter = nullptr) {
    std::vector<int> ids;
    for (std::size_t i = 0; i < lib.games().size(); ++i)
        if (!filter || passes(lib.games()[i], *filter)) ids.push_back(static_cast<int>(i));
    const auto& g = lib.games();
    std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
        if (sort == 1 && g[a].last_played != g[b].last_played) return g[a].last_played > g[b].last_played;
        if (sort == 2 && g[a].seconds_played != g[b].seconds_played)
            return g[a].seconds_played > g[b].seconds_played;
        return lower(display_title(g[a])) < lower(display_title(g[b]));
    });
    return ids;
}
std::vector<int> recent_order(const Library& lib) {
    std::vector<int> ids;
    for (std::size_t i = 0; i < lib.games().size(); ++i)
        if (lib.games()[i].last_played > 0) ids.push_back(static_cast<int>(i));
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        return lib.games()[a].last_played > lib.games()[b].last_played;
    });
    return ids;
}
const Game* chosen_game(const State& s, const Library& lib) {
    return s.chosen >= 0 && static_cast<std::size_t>(s.chosen) < lib.games().size()
        ? &lib.games()[s.chosen] : nullptr;
}
void launch(State& s, const Game& game) {
    s.launch = LaunchRequest{game.system, utf8(game.path), s.tv};
    s.done = true;
}
void notify(State& s, Toast::Kind kind, std::string title, std::string body = {}) {
    s.toasts.push_back({kind, std::move(title), std::move(body)});
}
void add_game(State& s, Library& lib) {
    const auto path = pick_rom();
    if (!path) { s.open_manual = true; return; }
    if (lib.add(*path)) {
        lib.save();
        s.chosen = static_cast<int>(lib.games().size()) - 1;
        notify(s, Toast::Kind::Success, "Jeu ajouté", display_title(lib.games().back()));
    } else {
        notify(s, Toast::Kind::Error, "Jeu non ajouté", "Déjà présent ou format non pris en charge.");
    }
}
void import_folder(State& s, Library& lib, const fs::path& folder) {
    const auto count = lib.import_folder(folder);
    lib.save();
    if (count) notify(s, Toast::Kind::Success,
                      std::to_string(count) + (count > 1 ? " jeux ajoutés" : " jeu ajouté"));
    else notify(s, Toast::Kind::Info, "Aucun nouveau jeu trouvé", "Formats acceptés : .gb, .gbc, .gba");
}
void set_cover(State& s, const Game& game, const fs::path& image) {
    try {
        install_cover(s.folders, game.path, image);
        forget_image(s, utf8(game.path) + ":cover");
        notify(s, Toast::Kind::Success, "Jaquette mise à jour", display_title(game));
    } catch (const std::exception& e) {
        notify(s, Toast::Kind::Error, "Jaquette non modifiée", e.what());
    }
}
// Opens a folder in the system file manager.
void open_folder(const fs::path& folder) {
    std::string url = "file://";
    auto text = utf8(folder);
#ifdef _WIN32
    std::replace(text.begin(), text.end(), '\\', '/');
    url += '/';
#endif
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : text) {
        if ((c < 0x80 && std::isalnum(c)) || std::strchr("/-_.~:", c)) {
            url += static_cast<char>(c);
        } else {
            url += '%'; url += hex[c >> 4]; url += hex[c & 15];
        }
    }
    SDL_OpenURL(url.c_str());
}
void pick_and_import_folder(State& s, Library& lib) {
    if (const auto folder = pick_folder()) import_folder(s, lib, *folder);
    else s.open_manual = true;
}

// ---------------------------------------------------------------- pieces
void text_at(ImDrawList* d, ImFont* font, ImVec2 pos, ImU32 col, const std::string& text) {
    d->AddText(font, font->FontSize, pos, col, text.c_str());
}
float text_width(ImFont* font, const char* text) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text).x;
}
bool section_header(State& s, const char* title, const char* action = nullptr) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(s.fonts.title);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (!action) return false;
    ImGui::SameLine();
    const float bw = text_width(s.fonts.body, action) + space::lg * 2.f;
    ImGui::SetCursorScreenPos({p.x + width - bw, p.y - 4.f});
    ImGui::PushID(title);
    const bool clicked = button(action, Icon::Play, ButtonStyle::Ghost, {bw, 32.f}, false);
    ImGui::PopID();
    return clicked;
}
float card_height(float width) { return width + 60.f; }
void game_title_tooltip(const std::string& title, bool play_action = false) {
    if (!ImGui::BeginTooltip()) return;
    const float wrap_width = std::max(1.f, std::min(480.f, ImGui::GetIO().DisplaySize.x - 32.f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(title.c_str());
    if (play_action) {
        ImGui::Separator();
        ImGui::TextUnformatted("Jouer");
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
void game_card(State& s, const Game& game, int index, float w) {
    ImGui::PushID(index);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = card_height(w);
    ImGui::InvisibleButton("##card", {w, h});
    const bool hovered = ImGui::IsItemHovered();
    // Read the card's hover state before submitting any tooltip content.
    const bool show_title = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    if (ImGui::IsItemClicked() || focused) s.chosen = index;
    bool play = (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
        (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)));
    const bool selected = s.chosen == index;
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 ca = p, cb{p.x + w, p.y + w};
    if (hovered || selected)
        d->AddRectFilled({p.x - 8, p.y - 8}, {p.x + w + 8, p.y + h + 2},
                         selected ? color::accent_soft : color::surface, radius::lg + 4.f);
    if (hovered) d->AddRectFilled({ca.x + 4, ca.y + 8}, {cb.x - 4, cb.y + 8}, color::shadow, radius::md);
    draw_cover(d, ca, cb, display_title(game), game.system, cover_for(s, game), s.fonts);
    if (selected) d->AddRect({ca.x - 1, ca.y - 1}, {cb.x + 1, cb.y + 1}, color::accent, radius::md + 1, 0, 2.f);
    bool on_play = false;
    if (hovered) {
        const ImVec2 c{cb.x - 28.f, cb.y - 28.f};
        const auto mouse = ImGui::GetMousePos();
        on_play = (mouse.x - c.x) * (mouse.x - c.x) + (mouse.y - c.y) * (mouse.y - c.y) < 22.f * 22.f;
        d->AddCircleFilled({c.x, c.y + 3}, 22.f, color::shadow, 32);
        d->AddCircleFilled(c, 22.f, on_play ? color::accent_hover : color::accent, 32);
        draw_icon(d, Icon::Play, {c.x + 2.f, c.y}, 16.f, IM_COL32_WHITE);
        if (on_play && ImGui::IsItemClicked()) play = true;
    }
    const auto title = display_title(game);
    text_at(d, s.fonts.strong, {p.x + 2.f, cb.y + 10.f}, color::text, ellipsize(s.fonts.strong, title, w - 4.f));
    const char* tag = system_short(game.system);
    pill(d, {p.x, cb.y + 35.f}, tag, system_color(game.system), s.fonts.caption);
    const float tx = p.x + pill_size(tag, s.fonts.caption).x + space::sm;
    text_at(d, s.fonts.caption, {tx, cb.y + 37.f}, color::text_faint,
            ellipsize(s.fonts.caption, game.last_played ? ago(game.last_played) : "Nouveau", p.x + w - tx));
    focus_ring(d, {p.x - 8, p.y - 8}, {p.x + w + 8, p.y + h + 2}, radius::lg + 4.f);
    if (show_title) game_title_tooltip(title, on_play);
    ImGui::PopID();
    if (play) launch(s, game);
}
void game_grid(const char* id, State& s, const Library& lib, const std::vector<int>& ids, int max_rows = 0) {
    // A game can appear in multiple grids on the same page. Scope the grid
    // as well as each card so those copies have distinct interaction IDs.
    ImGui::PushID(id);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, static_cast<int>((avail + card_gap) / (card_min_w + card_gap)));
    const float w = std::floor((avail - card_gap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {card_gap, 28.f});
    int n = 0;
    for (int id : ids) {
        if (max_rows && n >= cols * max_rows) break;
        if (n % cols) ImGui::SameLine(0.f, card_gap);
        game_card(s, lib.games()[id], id, w);
        ++n;
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}
void empty_state(State& s, Library& lib, Icon icon, const char* title, const char* text, bool actions) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = actions ? 300.f : 220.f;
    auto* d = ImGui::GetWindowDrawList();
    d->AddRect(p, {p.x + w, p.y + h}, color::border_strong, radius::lg, 0, 1.f);
    const float cx = p.x + w * .5f;
    d->AddCircleFilled({cx, p.y + 66.f}, 34.f, color::accent_soft, 40);
    draw_icon(d, icon, {cx, p.y + 66.f}, 28.f, color::accent_hover);
    const float tw = text_width(s.fonts.title, title);
    text_at(d, s.fonts.title, {cx - tw * .5f, p.y + 118.f}, color::text, title);
    const float bw = text_width(s.fonts.body, text);
    text_at(d, s.fonts.body, {cx - bw * .5f, p.y + 152.f}, color::text_muted, text);
    if (actions) {
        const float b1 = text_width(s.fonts.body, "Ajouter un jeu") + 56.f;
        const float b2 = text_width(s.fonts.body, "Importer un dossier") + 56.f;
        ImGui::SetCursorScreenPos({cx - (b1 + b2 + space::md) * .5f, p.y + 196.f});
        if (button("Ajouter un jeu", Icon::Plus, ButtonStyle::Primary, {b1, 44.f})) add_game(s, lib);
        ImGui::SameLine(0.f, space::md);
        if (button("Importer un dossier", Icon::Folder, ButtonStyle::Secondary, {b2, 44.f}))
            pick_and_import_folder(s, lib);
        const char* hint = "ou glissez-déposez vos ROMs dans cette fenêtre";
        const float hw = text_width(s.fonts.caption, hint);
        text_at(d, s.fonts.caption, {cx - hw * .5f, p.y + 256.f}, color::text_faint, hint);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}
void begin_card(const char* id, float width = 0.f) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(color::surface));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {space::xl, space::xl - 4.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::BeginChild(id, {width, 0.f}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding |
                      ImGuiChildFlags_Borders);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
// `spacing` is false when another card follows on the same line.
void end_card(bool spacing = true) {
    ImGui::EndChild();
    if (spacing) ImGui::Dummy({0.f, space::sm});
}
void card_title(State& s, Icon icon, const char* title) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    draw_icon(ImGui::GetWindowDrawList(), icon, {p.x + 10.f, p.y + 12.f}, 18.f, color::accent_hover);
    ImGui::SetCursorScreenPos({p.x + 30.f, p.y});
    ImGui::PushFont(s.fonts.title);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
}
void muted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::text_muted));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- frame
void sidebar(State& s) {
    auto* d = ImGui::GetWindowDrawList();
    const float height = ImGui::GetIO().DisplaySize.y;
    d->AddRectFilled({0, 0}, {sidebar_w, height}, color::sidebar);
    d->AddLine({sidebar_w - .5f, 0}, {sidebar_w - .5f, height}, color::border);
    if (s.logo) d->AddImage(reinterpret_cast<ImTextureID>(s.logo), {20, 16}, {68, 64});
    struct Item { Page page; Icon icon; const char* label; bool tv_mode = false; };
    const Item items[] = {{Page::Home, Icon::Home, "Accueil"}, {Page::Library, Icon::Library, "Jeux"},
                          {Page::Recent, Icon::Recent, "Récents"},
                          {Page::Home, Icon::Gamepad, "Mode TV", true},
                          {Page::Profile, Icon::Home, "Profil"},
                          {Page::Settings, Icon::Settings, "Réglages"}};
    for (int i = 0; i < 6; ++i) {
        const float y = i < 4 ? 96.f + i * 72.f : height - 84.f - (5 - i) * 72.f;
        const ImVec2 a{12.f, y}, b{sidebar_w - 12.f, y + 64.f};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##nav", {b.x - a.x, b.y - a.y})) {
            if (items[i].tv_mode) set_tv(s, true);
            else {
                s.page = items[i].page;
                if (s.page == Page::Profile) s.ra_game_path.clear();
            }
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool active = !items[i].tv_mode && s.page == items[i].page;
        if (active) {
            d->AddRectFilled(a, b, color::accent_soft, radius::md);
            d->AddRectFilled({0, y + 16.f}, {3.f, y + 48.f}, color::accent, 2.f);
        } else if (hovered) {
            d->AddRectFilled(a, b, color::surface_hover, radius::md);
        }
        const ImU32 fg = active ? color::text : hovered ? color::text : color::text_muted;
        if (items[i].page == Page::Profile) {
            draw_ra_identity(s, d, {sidebar_w * .5f, y + 22.f}, 14.f);
            if (active) d->AddCircle({sidebar_w * .5f, y + 22.f}, 16.f, color::accent_hover, 0, 1.5f);
        } else {
            draw_icon(d, items[i].icon, {sidebar_w * .5f, y + 24.f}, 22.f, active ? color::accent_hover : fg);
        }
        const float lw = text_width(s.fonts.caption, items[i].label);
        text_at(d, s.fonts.caption, {(sidebar_w - lw) * .5f, y + 41.f}, fg, items[i].label);
        focus_ring(d, a, b, radius::md);
        if (items[i].tv_mode && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Mode TV / manette (%s)",
                pad_button_name(connected_pad().value_or(PadFamily::Generic), PadButton::Start));
        ImGui::PopID();
    }
}
void window_controls(State& s) {
    if (!s.window) return;
    const float width = ImGui::GetIO().DisplaySize.x;
    constexpr float bw = 46.f, bh = 34.f;
    const bool maximized = (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MAXIMIZED) != 0;
    const Icon icons[] = {Icon::Minimize, maximized ? Icon::Restore : Icon::Maximize, Icon::Close};
    const char* tips[] = {"Réduire", maximized ? "Restaurer" : "Agrandir", "Fermer"};
    auto* d = ImGui::GetWindowDrawList();
    for (int i = 0; i < 3; ++i) {
        const ImVec2 a{width - bw * static_cast<float>(3 - i), 0.f};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i + 900);
        const bool pressed = ImGui::InvisibleButton("##window", {bw, bh});
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) d->AddRectFilled(a, {a.x + bw, bh}, i == 2 ? color::danger : color::surface_hover);
        draw_icon(d, icons[i], {a.x + bw * .5f, bh * .5f}, 14.f,
                  hovered ? IM_COL32_WHITE : color::text_muted);
        if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tips[i]);
        ImGui::PopID();
        if (!pressed) continue;
        if (i == 0) SDL_MinimizeWindow(s.window);
        else if (i == 1) maximized ? SDL_RestoreWindow(s.window) : SDL_MaximizeWindow(s.window);
        else { s.launch.reset(); s.done = true; }
    }
    s.interactive.push_back({width - bw * 3.f, 0.f, width, bh});
}
void header(State& s) {
    const float width = ImGui::GetIO().DisplaySize.x;
    auto* d = ImGui::GetWindowDrawList();
    const char* titles[] = {"Accueil", "Bibliothèque", "Joués récemment", "Réglages", "Profil"};
    const char* title = titles[static_cast<int>(s.page)];
    text_at(d, s.fonts.display, {sidebar_w + content_pad, 26.f}, color::text, title);
    // Center search in the content area, leaving room for the page title
    // and the window controls when the window is narrow.
    const float left = sidebar_w + content_pad + text_width(s.fonts.display, title) + space::xl;
    const float right = width - 46.f * 3.f - space::xl;
    const float sw = std::min(std::clamp(width * .26f, 240.f, 380.f), std::max(1.f, right - left));
    const float centered = sidebar_w + (width - sidebar_w - sw) * .5f;
    const ImVec2 a{std::clamp(centered, left, std::max(left, right - sw)), 24.f};
    const ImVec2 b{a.x + sw, a.y + 40.f};
    d->AddRectFilled(a, b, color::surface, radius::md);
    draw_icon(d, Icon::Search, {a.x + 20.f, a.y + 20.f}, 15.f, color::text_muted);
    ImGui::SetCursorScreenPos({a.x + 36.f, a.y + (40.f - ImGui::GetFrameHeight()) * .5f});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(sw - 48.f);
    if (s.focus_search) { ImGui::SetKeyboardFocusHere(); s.focus_search = false; }
    if (ImGui::InputTextWithHint("##search", "Rechercher un jeu\xE2\x80\xA6", s.search, sizeof(s.search),
                                 ImGuiInputTextFlags_EscapeClearsAll) &&
        s.search[0] && s.page != Page::Library)
        s.page = Page::Library;
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleColor(3);
    d->AddRect(a, b, active ? color::accent : color::border_strong, radius::md, 0, active ? 1.5f : 1.f);
    if (!active && !s.search[0]) {
        const float kw = text_width(s.fonts.caption, "Ctrl+F");
        text_at(d, s.fonts.caption, {b.x - kw - space::md, a.y + 12.f}, color::text_faint, "Ctrl+F");
    }
    s.interactive.push_back({a.x, a.y, b.x, b.y});
}

void hero(State& s, Library& lib) {
    const auto recent = recent_order(lib);
    const int index = !recent.empty() ? recent.front() : s.chosen >= 0 ? s.chosen : 0;
    const Game& game = lib.games()[static_cast<std::size_t>(index)];
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 248.f;
    auto* d = ImGui::GetWindowDrawList();
    const ImU32 tint = system_color(game.system);
    gradient_rect(d, p, {p.x + w, p.y + h}, mix(color::surface, tint, .22f), color::surface, radius::lg);
    d->PushClipRect(p, {p.x + w, p.y + h}, true);
    d->AddCircleFilled({p.x + w * .78f, p.y - h * .2f}, h * 1.1f, with_alpha(tint, .06f), 64);
    d->AddCircleFilled({p.x + w * .9f, p.y + h * .9f}, h * .6f, with_alpha(color::accent, .07f), 64);
    d->PopClipRect();
    d->AddRect(p, {p.x + w, p.y + h}, color::border, radius::lg);
    const float cover = h - 48.f;
    draw_cover(d, {p.x + 24.f, p.y + 24.f}, {p.x + 24.f + cover, p.y + 24.f + cover},
               display_title(game), game.system, cover_for(s, game), s.fonts);
    SDL_Texture* shot = preview_for(s, game);
    const float shot_w = shot && w > 820.f ? cover * 160.f / 144.f : 0.f;
    const float x = p.x + 24.f + cover + 28.f;
    const float room = p.x + w - x - (shot_w > 0 ? shot_w + 48.f : 24.f);
    text_at(d, s.fonts.caption, {x, p.y + 38.f}, color::accent_hover,
            recent.empty() ? "PRÊT À JOUER" : "REPRENDRE LA PARTIE");
    text_at(d, s.fonts.display, {x, p.y + 58.f}, color::text,
            ellipsize(s.fonts.display, display_title(game), room));
    std::string meta = std::string(system_name(game.system));
    if (game.last_played) meta += "  \xC2\xB7  " + ago(game.last_played);
    if (game.seconds_played) meta += "  \xC2\xB7  " + duration_of(game.seconds_played) + " de jeu";
    text_at(d, s.fonts.body, {x, p.y + 102.f}, color::text_muted, ellipsize(s.fonts.body, meta, room));
    if (const auto& info = game_achievements(s, game)) {
        const float y = p.y + 132.f;
        draw_icon(d, Icon::Trophy, {x + 8.f, y + 9.f}, 16.f, info->unlocked_count ? color::gold : color::text_faint);
        text_at(d, s.fonts.body, {x + 24.f, y}, info->unlocked_count ? color::gold : color::text_muted,
                achievement_count(*info) + " succès débloqués");
    }
    ImGui::SetCursorScreenPos({x, p.y + h - 24.f - 46.f});
    ImGui::PushID("hero");
    if (button("Jouer", Icon::Play, ButtonStyle::Primary, {148.f, 46.f})) launch(s, game);
    const float more_w = text_width(s.fonts.body, "Voir la fiche") + 56.f;
    if (148.f + space::md + more_w <= room) {
        ImGui::SameLine(0.f, space::md);
        if (button("Voir la fiche", Icon::Info, ButtonStyle::Secondary, {more_w, 46.f})) {
            s.chosen = index;
            s.page = Page::Library;
        }
    }
    ImGui::PopID();
    if (shot_w > 0.f) {
        const ImVec2 sa{p.x + w - 24.f - shot_w, p.y + 24.f}, sb{p.x + w - 24.f, p.y + 24.f + cover};
        d->AddRectFilled({sa.x, sa.y + 6}, {sb.x, sb.y + 6}, color::shadow, radius::md);
        d->AddImageRounded(reinterpret_cast<ImTextureID>(shot), sa, sb, {0, 0}, {1, 1}, IM_COL32_WHITE, radius::md);
        d->AddRectFilled({sa.x, sb.y - 30.f}, sb, IM_COL32(0, 0, 0, 150), radius::md, ImDrawFlags_RoundCornersBottom);
        text_at(d, s.fonts.caption, {sa.x + space::md, sb.y - 23.f}, color::text, "Dernière capture");
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}
void home_view(State& s, Library& lib) {
    if (lib.games().empty()) {
        empty_state(s, lib, Icon::Gamepad, "Bienvenue sur RavenEmu",
                    "Ajoutez vos jeux Game Boy, Game Boy Color et Game Boy Advance pour commencer.", true);
        return;
    }
    update_banner(s);
    profile_banner(s, lib);
    ImGui::Dummy({0.f, space::xs});
    hero(s, lib);
    ImGui::Dummy({0.f, space::sm});
    const auto recent = recent_order(lib);
    if (recent.size() > 1) {
        if (section_header(s, "Joués récemment", "Tout voir")) s.page = Page::Recent;
        game_grid("home-recent", s, lib, recent, 1);
        ImGui::Dummy({0.f, space::sm});
    }
    if (section_header(s, "Votre bibliothèque", "Tout voir")) s.page = Page::Library;
    game_grid("home-library", s, lib, library_order(lib, 0), 1);
}
void library_view(State& s, Library& lib) {
    static const char* filters[] = {"Tous", "Game Boy", "Game Boy Color", "Game Boy Advance"};
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 row = ImGui::GetCursorScreenPos();
    float chips_w = space::sm * 3.f;
    for (const char* f : filters) chips_w += text_width(s.fonts.body, f) + space::lg * 2.f;
    const float add_w = text_width(s.fonts.body, "Ajouter") + 56.f;
    const float folder_w = text_width(s.fonts.body, "Dossier") + 56.f;
    const float sort_w = 170.f;
    const float actions_w = add_w + folder_w + sort_w + space::sm * 2.f;
    const bool stacked = chips_w + actions_w + space::xl > width;
    chips("systems", filters, 4, &s.system_filter);
    // Actions sit right of the filters, or on their own row when narrow.
    const ImVec2 at = stacked ? ImVec2{row.x, row.y + 48.f} : ImVec2{row.x + width - actions_w, row.y - 4.f};
    ImGui::SetCursorScreenPos(at);
    ImGui::SetNextItemWidth(sort_w);
    static const char* sorts[] = {"Titre A-Z", "Dernière partie", "Temps de jeu"};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {space::md, 11.f});
    ImGui::Combo("##sort", &s.sort, sorts, 3);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Trier par");
    ImGui::SameLine(0.f, space::sm);
    if (button("Dossier", Icon::Folder, ButtonStyle::Secondary, {folder_w, 40.f})) pick_and_import_folder(s, lib);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Importer tout un dossier de ROMs");
    ImGui::SameLine(0.f, space::sm);
    if (button("Ajouter", Icon::Plus, ButtonStyle::Primary, {add_w, 40.f})) add_game(s, lib);
    ImGui::SetCursorScreenPos({row.x, row.y + (stacked ? 100.f : 52.f)});
    const auto ids = library_order(lib, s.sort, &s);
    if (lib.games().empty()) {
        empty_state(s, lib, Icon::Library, "Votre bibliothèque est vide",
                    "Ajoutez une ROM ou importez un dossier : les fichiers restent où ils sont.", true);
        return;
    }
    const std::string count = std::to_string(ids.size()) + (ids.size() > 1 ? " jeux" : " jeu");
    ImGui::PushFont(s.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::text_faint));
    ImGui::TextUnformatted(count.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    if (ids.empty()) {
        const std::string text = s.search[0] ? "Aucun jeu ne correspond à « " + std::string(s.search) + " »."
                                             : "Aucun jeu pour ce système.";
        empty_state(s, lib, Icon::Search, "Aucun résultat", text.c_str(), false);
        return;
    }
    game_grid("library", s, lib, ids);
}
void recent_view(State& s, Library& lib) {
    const auto ids = recent_order(lib);
    if (ids.empty()) {
        empty_state(s, lib, Icon::Recent, "Aucune partie récente",
                    "Les jeux lancés apparaîtront ici, du plus récent au plus ancien.", false);
        return;
    }
    const float w = ImGui::GetContentRegionAvail().x;
    auto* d = ImGui::GetWindowDrawList();
    for (int index : ids) {
        const Game& game = lib.games()[static_cast<std::size_t>(index)];
        ImGui::PushID(index);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        constexpr float h = 80.f;
        ImGui::InvisibleButton("##row", {w, h});
        const bool hovered = ImGui::IsItemHovered();
        const bool show_title = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemClicked();
        if (ImGui::IsItemClicked() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)) s.chosen = index;
        bool play = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) play = true;
        const bool selected = s.chosen == index;
        if (selected || hovered)
            d->AddRectFilled(p, {p.x + w, p.y + h}, selected ? color::accent_soft : color::surface, radius::lg);
        draw_cover(d, {p.x + 12.f, p.y + 12.f}, {p.x + 68.f, p.y + 68.f}, display_title(game), game.system,
                   cover_for(s, game), s.fonts, radius::sm);
        const float x = p.x + 88.f;
        text_at(d, s.fonts.strong, {x, p.y + 18.f}, color::text,
                ellipsize(s.fonts.strong, display_title(game), w * .5f));
        const char* tag = system_short(game.system);
        pill(d, {x, p.y + 44.f}, tag, system_color(game.system), s.fonts.caption);
        text_at(d, s.fonts.caption, {x + pill_size(tag, s.fonts.caption).x + space::sm, p.y + 46.f},
                color::text_muted, ago(game.last_played));
        const std::string time = duration_of(game.seconds_played);
        const float right = p.x + w - (hovered ? 150.f : 24.f);
        text_at(d, s.fonts.strong, {right - text_width(s.fonts.strong, time.c_str()), p.y + 20.f},
                color::text, time);
        text_at(d, s.fonts.caption, {right - text_width(s.fonts.caption, "de jeu"), p.y + 44.f},
                color::text_faint, "de jeu");
        focus_ring(d, p, {p.x + w, p.y + h}, radius::lg);
        if (hovered) {
            ImGui::SetCursorScreenPos({p.x + w - 124.f, p.y + 20.f});
            if (button("Jouer", Icon::Play, ButtonStyle::Primary, {108.f, 40.f})) play = true;
        }
        if (show_title) game_title_tooltip(display_title(game));
        ImGui::SetCursorScreenPos({p.x, p.y + h + space::xs});
        ImGui::Dummy({0.f, 0.f});
        ImGui::PopID();
        if (play) launch(s, game);
    }
}
void stat_card(State& s, const char* label, const std::string& value, float fraction, ImU32 tint, float w) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    auto* d = ImGui::GetWindowDrawList();
    constexpr float h = 112.f;
    d->AddRectFilled(p, {p.x + w, p.y + h}, color::surface, radius::lg);
    d->AddRect(p, {p.x + w, p.y + h}, color::border, radius::lg);
    text_at(d, s.fonts.caption, {p.x + 20.f, p.y + 18.f}, color::text_muted, label);
    text_at(d, s.fonts.display, {p.x + 20.f, p.y + 38.f}, color::text, value);
    if (fraction >= 0.f) progress_bar(d, {p.x + 20.f, p.y + h - 26.f}, {p.x + w - 20.f, p.y + h - 18.f}, fraction, tint);
    ImGui::Dummy({w, h});
}
// ---------------------------------------------------------------- RetroAchievements profile
std::string ra_name(const ravenemudsk_ra::Snapshot& snap) {
    return snap.display_name.empty() ? snap.username : snap.display_name;
}
void open_url(State& s, const std::string& url) {
    if (SDL_OpenURL(url.c_str()) != 0)
        notify(s, Toast::Kind::Error, "Ouverture du navigateur impossible", SDL_GetError());
}
// RetroAchievements user names are letters and digits: anything else is not
// put into a URL.
bool ra_public_name(const std::string& name) {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}
// Signing out also erases unsent unlocks: it is always confirmed, with their count.
void ra_disconnect_dialog(State& s, ravenemudsk_ra::Client& ra) {
    if (s.ra_confirm_disconnect) {
        ImGui::OpenPopup("##ra-disconnect");
        s.ra_confirm_disconnect = false;
    }
    if (!ImGui::BeginPopupModal("##ra-disconnect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto snap = ra.snapshot();
    ImGui::PushFont(s.fonts.title);
    ImGui::TextUnformatted("Se déconnecter de RetroAchievements ?");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.f);
    muted("La session et le profil mémorisés sur cet ordinateur seront effacés. "
          "Les succès déjà envoyés restent sur votre compte.");
    if (snap.pending_submissions) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::gold));
        ImGui::TextWrapped("%u envoi%s pas encore transmis (succès ou scores) sera%s supprimé%s définitivement. "
            "Pour %s garder, restez connecté jusqu'à leur envoi.", snap.pending_submissions,
            snap.pending_submissions > 1 ? "s" : "", snap.pending_submissions > 1 ? "nt" : "",
            snap.pending_submissions > 1 ? "s" : "", snap.pending_submissions > 1 ? "les" : "le");
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0.f, space::xs});
    if (button("Annuler", Icon::Close, ButtonStyle::Secondary)) ImGui::CloseCurrentPopup();
    ImGui::SameLine(0.f, space::sm);
    if (button("Se déconnecter", Icon::Exit, ButtonStyle::Danger)) {
        ra.disconnect();
        s.ra_selected_achievement.clear();
        s.ra_filter = 0;
        s.ra_visible_count = 10;
        s.ra_search[0] = '\0';
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
// Connection state and unsent submissions, as pills after the account name.
// Same header on every tab: avatar, name, points and account actions, all as text.
void ra_account_header(State& s, ravenemudsk_ra::Client& ra, const ravenemudsk_ra::Snapshot& snap) {
    begin_card("##ra-account");
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    SDL_Texture* image = s.ra_images ? s.ra_images->image(snap.avatar_url) : nullptr;
    draw_avatar(d, {p.x + 34.f, p.y + 34.f}, 34.f, ra_name(snap), 0, image, s.fonts);
    const float x = p.x + 86.f;
    // Account actions sit on the right when there is room, otherwise below.
    const bool site = ra_public_name(snap.username);
    const auto action_w = [](const char* label) { return ImGui::CalcTextSize(label).x + 16.f + space::sm + space::lg * 2.f; };
    const float actions = (site ? action_w("Profil sur le site") + space::sm : 0.f) +
        action_w("Actualiser") + space::sm + action_w("Déconnexion");
    const bool inline_actions = w - 86.f - actions >= 260.f;
    const float text_w = std::max(120.f, (inline_actions ? w - actions - space::lg : w) - 86.f);
    text_at(d, s.fonts.title, {x, p.y + 2.f}, color::text, ellipsize(s.fonts.title, ra_name(snap), text_w));
    const auto handle = ellipsize(s.fonts.caption, "@" + snap.username, text_w * .5f);
    text_at(d, s.fonts.caption, {x, p.y + 30.f}, color::text_muted, handle);
    // Same wording and order as the home banner: Softcore, then Hardcore.
    const auto points = std::to_string(snap.softcore_points) + " points Softcore  |  " +
        std::to_string(snap.hardcore_points) + " points Hardcore";
    text_at(d, s.fonts.caption, {x, p.y + 50.f}, color::text_muted, ellipsize(s.fonts.caption, points, text_w));
    const ImVec2 after = {p.x, p.y + 82.f};
    if (inline_actions) ImGui::SetCursorScreenPos({p.x + w - actions, p.y + 4.f});
    else ImGui::SetCursorScreenPos(after);
    if (site) {
        if (button("Profil sur le site", Icon::Info, ButtonStyle::Ghost))
            open_url(s, "https://retroachievements.org/user/" + snap.username);
        ImGui::SameLine(0.f, space::sm);
    }
    ImGui::BeginDisabled(snap.login_pending);
    if (button("Actualiser", Icon::Reset, ButtonStyle::Secondary))
        ra.refresh_game(s.ra_game_path.empty() ? snap.rom_path : s.ra_game_path);
    ImGui::EndDisabled();
    ImGui::SameLine(0.f, space::sm);
    if (button("Déconnexion", Icon::Exit, ButtonStyle::Ghost)) s.ra_confirm_disconnect = true;
    ImGui::SetCursorScreenPos(inline_actions ? after : ImVec2{p.x, ImGui::GetCursorScreenPos().y + space::xs});
    muted(snap.status.c_str());
    if (snap.pending_submissions) {
        const auto n = snap.pending_submissions;
        const auto pending = std::to_string(n) + (n > 1 ? " envois en attente" : " envoi en attente") +
            (snap.durable_submissions ? (n > 1 ? " : ils sont conservés après fermeture." : " : il est conservé après fermeture.") :
             " : gardez l'application ouverte jusqu'à leur envoi.");
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::gold), "%s", pending.c_str());
    }
    if (snap.reconnect_scheduled) muted("Nouvelle tentative automatique, avec un délai croissant jusqu'à une minute.");
    if (!snap.online || snap.game_status == ravenemudsk_ra::GameStatus::Offline) {
        if (button("Réessayer", Icon::Reset, ButtonStyle::Secondary)) ra.retry();
    }
    end_card();
}
void ra_sign_in_card(State& s, ravenemudsk_ra::Client& ra, const ravenemudsk_ra::Snapshot& snap) {
    begin_card("##ra-account");
    const ImVec2 title_pos = ImGui::GetCursorScreenPos();
    draw_ra_brand(s, ImGui::GetWindowDrawList(), {title_pos.x + 11.f, title_pos.y + 12.f}, 24.f);
    ImGui::SetCursorScreenPos({title_pos.x + 31.f, title_pos.y});
    ImGui::PushFont(s.fonts.title);
    ImGui::TextUnformatted("RetroAchievements");
    ImGui::PopFont();
    ImGui::Dummy({0.f, 4.f});
    muted(snap.status.c_str());
    if (snap.pending_submissions) ImGui::TextWrapped("%u envoi(s) en attente%s", snap.pending_submissions,
        snap.durable_submissions ? ", conservés après fermeture." : " : gardez l'application ouverte pour synchroniser.");
    if (snap.reconnect_scheduled) muted("Nouvelle tentative automatique, avec un délai croissant jusqu'à une minute.");
    if (snap.cached_profile)
        ImGui::Text("@%s : %u points Softcore, %u points Hardcore (mémorisés)", snap.username.c_str(),
                    snap.softcore_points, snap.hardcore_points);
    ImGui::Dummy({0.f, space::xs});
    muted("Connectez votre compte pour obtenir les succès officiels pendant vos parties Game Boy, Game Boy Color et Game Boy Advance.");
    ra_login_controls(s, ra);
    end_card();
}
void ra_game_card(State& s, const ravenemudsk_ra::Snapshot& snap, float width) {
    begin_card("##ra-game", width);
    card_title(s, Icon::Gamepad, "Jeu");
    ImGui::Dummy({0.f, space::xs});
    if (snap.game_title.empty()) {
        muted(snap.logged_in || snap.cached_profile ? "Aucun jeu reconnu pour le moment." :
              "Connectez-vous pour retrouver les succès officiels de vos jeux.");
        muted("Lancez un jeu, ou actualisez ses succès depuis sa fiche dans la bibliothèque.");
        end_card();
        return;
    }
    ImGui::PushFont(s.fonts.strong);
    ImGui::TextWrapped("%s", snap.game_title.c_str());
    ImGui::PopFont();
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    progress_bar(d, {p.x, p.y + 4.f}, {p.x + w, p.y + 10.f},
                 snap.total_count ? static_cast<float>(snap.unlocked_count) / snap.total_count : 0.f, color::gold);
    ImGui::Dummy({w, 16.f});
    ImGui::Text("%d / %d succès", snap.unlocked_count, snap.total_count);
    ImGui::TextDisabled("%u / %u points  |  %s", snap.unlocked_points, snap.total_points,
                        snap.game_hardcore ? "Progression Hardcore" : "Progression Softcore");
    if (snap.cached_profile) muted("Données mémorisées : lancez le jeu ou actualisez ses succès pour les mettre à jour.");
    end_card();
}
void ra_mode_card(State& s, float width) {
    begin_card("##ra-mode", width);
    card_title(s, Icon::Lock, "Mode de jeu");
    ImGui::Dummy({0.f, space::xs});
    ra_mode_controls(s);
    end_card();
}
// One row of the achievement list: badge, title and type, state or progress,
// points and rarity. Locked achievements are dimmed.
// Where the unlock stands with RetroAchievements; empty when the service reports it.
const char* ra_sync_sentence(const ravenemudsk_ra::Achievement& a) {
    switch (a.sync) {
    case ravenemudsk_ra::AchievementSync::Pending:
        return "Obtenu sur cet ordinateur, pas encore confirmé par RetroAchievements.";
    case ravenemudsk_ra::AchievementSync::Refused: return "RetroAchievements a refusé cet envoi.";
    default: return a.unlocked ? "Confirmé par RetroAchievements." : "";
    }
}
bool ra_achievement_row(State& s, const ravenemudsk_ra::Achievement& a, bool hardcore) {
    constexpr float h = 78.f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(a.id.c_str());
    const bool clicked = ImGui::InvisibleButton("##ra-row", {width, h}, ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    const bool synced = a.sync == ravenemudsk_ra::AchievementSync::Server;
    if (hovered && (!a.description.empty() || !synced)) {
        // The description, the rarity and the sync state in words, without opening the sheet.
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.f);
        ImGui::TextUnformatted(a.description.c_str());
        if (const auto rarity = ravenemudsk_ra::rarity_sentence(a); !rarity.empty()) muted(rarity.c_str());
        if (!synced) muted(ra_sync_sentence(a));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(pos, {pos.x + width, pos.y + h}, hovered ? color::surface_hover :
                     a.unlocked ? color::surface : with_alpha(color::surface, .55f), radius::md);
    if (a.unlocked) d->AddRectFilled({pos.x, pos.y + 14.f}, {pos.x + 3.f, pos.y + h - 14.f}, color::gold, 2.f);
    focus_ring(d, pos, {pos.x + width, pos.y + h}, radius::md);
    const ImVec2 badge_min{pos.x + 14.f, pos.y + 15.f}, badge_max{pos.x + 62.f, pos.y + 63.f};
    if (auto* badge = s.ra_images ? s.ra_images->image(a.badge_url) : nullptr) {
        d->AddImageRounded(reinterpret_cast<ImTextureID>(badge), badge_min, badge_max, {0, 0}, {1, 1},
                           a.unlocked ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 170), 6.f);
    } else {
        d->AddRectFilled(badge_min, badge_max, a.unlocked ? color::gold_soft : color::elevated, 6.f);
        draw_icon(d, a.unlocked ? Icon::Trophy : Icon::Lock, {pos.x + 38.f, pos.y + 39.f}, 22.f,
                  a.unlocked ? color::gold : color::text_faint);
    }
    // Points and rarity on the right, then the title fills the remaining width.
    const std::string points = std::to_string(a.points) + " pts";
    const std::string rarity = ravenemudsk_ra::format_rarity(hardcore ? a.rarity_hardcore : a.rarity);
    const float right_w = std::max(text_width(s.fonts.caption, points.c_str()),
                                   rarity.empty() ? 0.f : text_width(s.fonts.caption, rarity.c_str()));
    text_at(d, s.fonts.caption, {pos.x + width - 18.f - text_width(s.fonts.caption, points.c_str()), pos.y + 16.f},
            color::accent, points);
    if (!rarity.empty())
        text_at(d, s.fonts.caption, {pos.x + width - 18.f - text_width(s.fonts.caption, rarity.c_str()), pos.y + 42.f},
                color::text_faint, rarity);
    const float x = pos.x + 76.f;
    const float text_w = std::max(80.f, width - 76.f - right_w - 36.f);
    const auto title = ellipsize(s.fonts.strong, a.title, std::max(40.f, text_w));
    text_at(d, s.fonts.strong, {x, pos.y + 14.f}, a.unlocked ? color::text : color::text_muted, title);
    std::string line;
    ImU32 line_color = a.unlocked ? color::text_muted : color::text_faint;
    if (a.unlocked) {
        const auto date = ravenemudsk_ra::format_unlock_date(a.unlock_time);
        line = date.empty() ? "Débloqué" : "Débloqué le " + date;
    } else if (a.state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED) line = "Non pris en charge";
    else if (!a.progress.empty()) line = a.progress;
    else line = "Verrouillé";
    if (a.sync == ravenemudsk_ra::AchievementSync::Pending) {
        line = a.unlocked ? line + " \xC2\xB7 envoi en attente" : "Obtenu, envoi en attente";
        line_color = color::gold;
    } else if (a.sync == ravenemudsk_ra::AchievementSync::Refused) {
        line = "Envoi refusé par RetroAchievements";
        line_color = color::danger;
    }
    line = ellipsize(s.fonts.caption, line, text_w);
    text_at(d, s.fonts.caption, {x, pos.y + 46.f}, line_color, line);
    if (synced && !a.unlocked && !a.progress.empty() && a.measured_percent > 0.f) {
        const float bar_x = x + text_width(s.fonts.caption, line.c_str()) + space::md;
        const float bar_end = std::min(bar_x + 140.f, x + text_w);
        if (bar_end - bar_x >= 40.f)
            progress_bar(d, {bar_x, pos.y + 51.f}, {bar_end, pos.y + 57.f}, a.measured_percent / 100.f, color::accent);
    }
    return clicked;
}
void ra_achievement_detail(State& s, const ravenemudsk_ra::Snapshot& snap) {
    if (!ImGui::BeginPopupModal("##ra-detail", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto it = std::find_if(snap.achievements.begin(), snap.achievements.end(),
        [&](const ravenemudsk_ra::Achievement& a) { return a.id == s.ra_selected_achievement; });
    if (it == snap.achievements.end()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const auto& a = *it;
    ImGui::PushFont(s.fonts.title);
    ImGui::TextUnformatted(a.title.c_str());
    ImGui::PopFont();
    if (const char* type = ravenemudsk_ra::achievement_type_label(a.type)) {
        muted((std::string("Type : ") + type).c_str());
        if (a.type == RC_CLIENT_ACHIEVEMENT_TYPE_MISSABLE) muted("Peut devenir impossible à obtenir si un moment du jeu est passé.");
    }
    if (SDL_Texture* badge = s.ra_images ? s.ra_images->image(a.badge_url) : nullptr)
        ImGui::Image(reinterpret_cast<ImTextureID>(badge), {88.f, 88.f});
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.f);
    ImGui::TextWrapped("%s", a.description.c_str());
    ImGui::Dummy({0.f, space::xs});
    ImGui::Text("%d points", a.points);
    if (a.unlocked) {
        const auto date = ravenemudsk_ra::format_unlock_date(a.unlock_time);
        ImGui::TextUnformatted(date.empty() ? "Débloqué" : ("Débloqué le " + date).c_str());
    } else if (!a.progress.empty()) {
        ImGui::Text("Progression : %s", a.progress.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        progress_bar(ImGui::GetWindowDrawList(), p, {p.x + 260.f, p.y + 6.f}, a.measured_percent / 100.f, color::accent);
        ImGui::Dummy({260.f, 12.f});
    } else {
        ImGui::TextUnformatted(a.state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED ? "Non pris en charge par l'émulateur" : "À obtenir");
    }
    if (const auto rarity = ravenemudsk_ra::rarity_sentence(a); !rarity.empty()) muted(rarity.c_str());
    if (const char* sync = ra_sync_sentence(a); *sync) {
        if (a.sync == ravenemudsk_ra::AchievementSync::Server) muted(sync);
        else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
            a.sync == ravenemudsk_ra::AchievementSync::Pending ? color::gold : color::danger), "%s", sync);
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0.f, space::xs});
    if (button("Fermer", Icon::Close, ButtonStyle::Secondary)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    const bool digits = !a.id.empty() && std::all_of(a.id.begin(), a.id.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (digits && button("Voir sur le site", Icon::Info, ButtonStyle::Secondary))
        open_url(s, "https://retroachievements.org/achievement/" + a.id);
    ImGui::EndPopup();
}
void ra_achievement_list(State& s, const ravenemudsk_ra::Snapshot& snap) {
    if (snap.game_title.empty() || snap.achievements.empty()) {
        begin_card("##ra-no-game");
        card_title(s, Icon::Gamepad, "Succès du jeu");
        muted(snap.logged_in || snap.cached_profile ? "Aucun jeu reconnu pour le moment." :
              "Connectez-vous pour retrouver les succès officiels.");
        end_card();
        return;
    }
    // Search and sort on one line, then the filters with their counts.
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::clamp(w - 220.f - space::sm, 160.f, 360.f));
    if (ImGui::InputTextWithHint("##ra-search", "Rechercher un succès", s.ra_search, sizeof(s.ra_search)))
        s.ra_visible_count = 10;
    ImGui::SameLine(0.f, space::sm);
    ImGui::SetNextItemWidth(220.f);
    s.ra_sort = std::clamp(s.ra_sort, 0, static_cast<int>(ravenemudsk_ra::achievement_sort_labels.size()) - 1);
    const std::string sort_preview = std::string("Tri : ") + ravenemudsk_ra::achievement_sort_labels[static_cast<std::size_t>(s.ra_sort)];
    if (ImGui::BeginCombo("##ra-sort", sort_preview.c_str())) {
        for (int i = 0; i < static_cast<int>(ravenemudsk_ra::achievement_sort_labels.size()); ++i)
            if (ImGui::Selectable(ravenemudsk_ra::achievement_sort_labels[static_cast<std::size_t>(i)], s.ra_sort == i)) {
                s.ra_sort = i;
                s.ra_visible_count = 10;
            }
        ImGui::EndCombo();
    }
    ImGui::Dummy({0.f, space::xs});
    const auto counts = ravenemudsk_ra::count_achievements(snap.achievements, s.ra_search);
    const std::array<std::string, 3> labels{"Tous (" + std::to_string(counts.all) + ")",
        "À obtenir (" + std::to_string(counts.locked) + ")", "Obtenus (" + std::to_string(counts.unlocked) + ")"};
    const char* filters[] = {labels[0].c_str(), labels[1].c_str(), labels[2].c_str()};
    if (chips("##ra-filter", filters, 3, &s.ra_filter)) s.ra_visible_count = 10;
    ImGui::Dummy({0.f, space::sm});
    const auto list = ravenemudsk_ra::list_achievements(snap.achievements, s.ra_search,
        static_cast<ravenemudsk_ra::AchievementFilter>(std::clamp(s.ra_filter, 0, 2)),
        static_cast<ravenemudsk_ra::AchievementSort>(s.ra_sort), snap.game_hardcore);
    if (list.empty()) muted("Aucun succès ne correspond à cette recherche.");
    const int total = static_cast<int>(list.size());
    for (int i = 0; i < std::min(total, s.ra_visible_count); ++i) {
        if (ra_achievement_row(s, *list[static_cast<std::size_t>(i)], snap.game_hardcore)) {
            s.ra_selected_achievement = list[static_cast<std::size_t>(i)]->id;
            ImGui::OpenPopup("##ra-detail");
        }
    }
    if (total > s.ra_visible_count) {
        const auto label = "Afficher plus (" + std::to_string(total - s.ra_visible_count) + " restants)";
        if (button(label.c_str(), Icon::Plus, ButtonStyle::Ghost)) s.ra_visible_count += 15;
    }
    ra_achievement_detail(s, snap);
}
// The library's recognized games with their cached progress for the current account.
// `unsynced` counts the others: unknown ROMs, games without achievements, never synced.
std::vector<ravenemudsk_ra::GameProgress> ra_library_progress(State& s, const Library& lib, std::size_t* unsynced = nullptr) {
    std::vector<ravenemudsk_ra::GameProgress> games;
    std::size_t missing = 0;
    auto& ra = retro_client(s);
    for (const auto& game : lib.games()) {
        const auto path = utf8(game.path);
        const auto info = ra.game_snapshot(path);
        if (!info || info->game_title.empty() || info->total_count <= 0) {
            ++missing;
            continue;
        }
        games.push_back({path, info->game_title, info->unlocked_count, info->total_count,
                         info->unlocked_points, info->total_points, info->game_hardcore, game.last_played});
    }
    if (unsynced) *unsynced = missing;
    return games;
}
// Shows a library game's achievements in the profile.
void ra_open_game(State& s, const std::string& path) {
    s.ra_game_path = path;
    s.ra_tab = 0;
    s.ra_filter = 0;
    s.ra_visible_count = 10;
    s.ra_search[0] = '\0';
    s.ra_selected_achievement.clear();
}
bool ra_game_row(State& s, const ravenemudsk_ra::GameProgress& g) {
    constexpr float h = 68.f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(g.path.c_str());
    const bool clicked = ImGui::InvisibleButton("##ra-game-row", {width, h}, ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Voir les succès de ce jeu");
        muted((ago(g.last_played) + (g.hardcore ? " \xC2\xB7 compteurs Hardcore" : " \xC2\xB7 compteurs Softcore")).c_str());
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    const char* done = ravenemudsk_ra::completion_label(g);
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(pos, {pos.x + width, pos.y + h}, hovered ? color::surface_hover : color::surface, radius::md);
    if (done) d->AddRectFilled({pos.x, pos.y + 12.f}, {pos.x + 3.f, pos.y + h - 12.f}, color::gold, 2.f);
    focus_ring(d, pos, {pos.x + width, pos.y + h}, radius::md);
    const ImVec2 cover_min{pos.x + 14.f, pos.y + 10.f}, cover_max{pos.x + 62.f, pos.y + 58.f};
    Game game;
    game.path = from_utf8(g.path);
    if (SDL_Texture* cover = cover_for(s, game))
        d->AddImageRounded(reinterpret_cast<ImTextureID>(cover), cover_min, cover_max, {0, 0}, {1, 1}, IM_COL32_WHITE, 6.f);
    else {
        d->AddRectFilled(cover_min, cover_max, color::elevated, 6.f);
        draw_icon(d, Icon::Gamepad, {pos.x + 38.f, pos.y + 34.f}, 22.f, color::text_faint);
    }
    const int percent = g.total ? g.unlocked * 100 / g.total : 0;
    const std::string share = std::to_string(percent) + " %";
    const char* mode = g.hardcore ? "Hardcore" : "Softcore";
    const float right_w = std::max(text_width(s.fonts.caption, share.c_str()), text_width(s.fonts.caption, mode));
    text_at(d, s.fonts.caption, {pos.x + width - 18.f - text_width(s.fonts.caption, share.c_str()), pos.y + 14.f},
            done ? color::gold : color::accent, share);
    text_at(d, s.fonts.caption, {pos.x + width - 18.f - text_width(s.fonts.caption, mode), pos.y + 38.f},
            color::text_faint, mode);
    const float x = pos.x + 76.f;
    const float text_w = std::max(80.f, width - 76.f - right_w - 36.f);
    const float done_w = done ? text_width(s.fonts.caption, done) + space::md : 0.f;
    const auto title = ellipsize(s.fonts.strong, g.title, std::max(40.f, text_w - done_w));
    text_at(d, s.fonts.strong, {x, pos.y + 10.f}, color::text, title);
    if (done)
        text_at(d, s.fonts.caption, {x + text_width(s.fonts.strong, title.c_str()) + space::md, pos.y + 12.f},
                g.hardcore ? color::gold : color::accent, done);
    const auto line = ellipsize(s.fonts.caption, std::to_string(g.unlocked) + " / " + std::to_string(g.total) +
        " succès \xC2\xB7 " + std::to_string(g.points) + " / " + std::to_string(g.total_points) + " points", text_w);
    text_at(d, s.fonts.caption, {x, pos.y + 34.f}, color::text_muted, line);
    progress_bar(d, {x, pos.y + 54.f}, {x + std::min(text_w, 260.f), pos.y + 58.f},
                 g.total ? static_cast<float>(g.unlocked) / g.total : 0.f, color::gold);
    return clicked;
}
void ra_my_games(State& s, Library& lib) {
    // Rebuild only when the account, the library, the live game or the sort change.
    const auto live = retro_client(s).snapshot();
    std::int64_t played = 0;
    for (const auto& game : lib.games()) played += game.last_played;
    s.ra_games_sort = std::clamp(s.ra_games_sort, 0, static_cast<int>(ravenemudsk_ra::game_sort_labels.size()) - 1);
    const auto key = live.username + '\n' + std::to_string(lib.games().size()) + '/' + std::to_string(played) + '\n' +
        live.rom_path + '\n' + std::to_string(live.unlocked_count) + '/' + std::to_string(live.total_count) +
        (live.game_hardcore ? 'h' : 's') + std::to_string(s.ra_games_sort);
    if (key != s.ra_games_key) {
        s.ra_games = ra_library_progress(s, lib, &s.ra_games_unsynced);
        ravenemudsk_ra::sort_games(s.ra_games, static_cast<ravenemudsk_ra::GameSort>(s.ra_games_sort));
        s.ra_games_key = key;
    }
    begin_card("##ra-my-games");
    card_title(s, Icon::Trophy, "Mes jeux RetroAchievements");
    if (s.ra_games.empty()) {
        muted(live.username.empty() ? "Connectez votre compte pour suivre la progression de vos jeux." :
              "Aucun jeu de la bibliothèque n'a encore de succès mémorisés. Lancez un jeu, ou actualisez ses succès depuis sa fiche.");
        end_card();
        return;
    }
    ImGui::TextUnformatted(ravenemudsk_ra::summary_text(ravenemudsk_ra::summarize(s.ra_games)).c_str());
    muted("Progression mémorisée sur cet ordinateur, consultable hors ligne. Chaque jeu garde le mode de sa dernière synchronisation.");
    if (s.ra_games_unsynced == 1)
        muted("1 jeu de la bibliothèque n'a pas de succès mémorisés : ROM non reconnue, jeu sans succès ou jamais synchronisé.");
    else if (s.ra_games_unsynced > 1)
        muted((std::to_string(s.ra_games_unsynced) + " jeux de la bibliothèque n'ont pas de succès mémorisés : "
               "ROM non reconnues, jeux sans succès ou jamais synchronisés.").c_str());
    end_card();
    ImGui::SetNextItemWidth(240.f);
    const std::string preview = std::string("Tri : ") + ravenemudsk_ra::game_sort_labels[static_cast<std::size_t>(s.ra_games_sort)];
    if (ImGui::BeginCombo("##ra-games-sort", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(ravenemudsk_ra::game_sort_labels.size()); ++i)
            if (ImGui::Selectable(ravenemudsk_ra::game_sort_labels[static_cast<std::size_t>(i)], s.ra_games_sort == i))
                s.ra_games_sort = i;
        ImGui::EndCombo();
    }
    ImGui::Dummy({0.f, space::xs});
    const int total = static_cast<int>(s.ra_games.size());
    std::string opened;
    for (int i = 0; i < std::min(total, s.ra_games_visible); ++i)
        if (ra_game_row(s, s.ra_games[static_cast<std::size_t>(i)])) opened = s.ra_games[static_cast<std::size_t>(i)].path;
    if (total > s.ra_games_visible) {
        const auto label = "Afficher plus (" + std::to_string(total - s.ra_games_visible) + " restants)";
        if (button(label.c_str(), Icon::Plus, ButtonStyle::Ghost)) s.ra_games_visible += 20;
    }
    if (!opened.empty()) ra_open_game(s, opened);
}
void ra_sections(State& s, ravenemudsk_ra::Client& ra, const ravenemudsk_ra::Snapshot& snap, Library& lib) {
    const char* tabs[] = {"Succès", "Classements", "Activité", "Mes jeux"};
    chips("##ra-sections", tabs, 4, &s.ra_tab);
    ImGui::Dummy({0.f, space::sm});
    if (s.ra_tab == 1) {
        begin_card("##ra-leaderboards");
        card_title(s, Icon::Trophy, "Classements");
        muted("Les scores sont envoyés en mode Hardcore.");
        draw_leaderboards(ra, snap, s.fonts);
        end_card();
    } else if (s.ra_tab == 2) {
        begin_card("##ra-presence");
        card_title(s, Icon::Gamepad, "Activité en cours");
        muted(snap.cached_profile ? "Dernière activité mémorisée : actualisez le jeu pour obtenir son état actuel." :
              "Activité partagée avec RetroAchievements pendant la partie.");
        ImGui::TextWrapped("%s", snap.rich_presence.empty() ? "Aucune activité détaillée disponible pour ce jeu." :
                           snap.rich_presence.c_str());
        end_card();
    } else if (s.ra_tab == 3) {
        ra_my_games(s, lib);
    } else {
        ra_achievement_list(s, snap);
    }
}
// The account stays on top; below, the game and the mode on the left and the
// achievements on the right, side by side when the window is wide enough.
void achievements_view(State& s, Library& lib) {
    auto& ra = retro_client(s);
    const auto snap = profile_snapshot(s);
    if (snap.logged_in) ra_account_header(s, ra, snap);
    else ra_sign_in_card(s, ra, snap);
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool columns = avail >= 900.f;
    const float left = columns ? std::clamp(avail * .32f, 300.f, 380.f) : 0.f;
    if (columns) ImGui::BeginGroup();
    ra_game_card(s, snap, left);
    ra_mode_card(s, left);
    if (columns) {
        ImGui::EndGroup();
        ImGui::SameLine(0.f, space::lg);
        ImGui::BeginGroup();
    }
    ra_sections(s, ra, snap, lib);
    if (columns) ImGui::EndGroup();
    ra_disconnect_dialog(s, ra);
}
void ra_mode_controls(State& s) {
    auto& client=retro_client(s);const auto snap=client.snapshot();
    bool hardcore=snap.hardcore_preferred;
    if(toggle("##ra-hardcore",&hardcore)){
        client.set_hardcore(hardcore);
        s.settings.set_flag("ra_hardcore",client.snapshot().hardcore_preferred);save_settings(s);
    }
    ImGui::SameLine(0.f,space::md);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mode Hardcore");
    ImGui::PushFont(s.fonts.caption);
    if(snap.hardcore)ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::gold),"Hardcore actif");
    else if(snap.hardcore_preferred)muted("Hardcore sélectionné : une connexion et un coffre système sont nécessaires.");
    else muted("Softcore actif : chargement des états autorisé.");
    muted("Le Hardcore interdit le chargement des états. Son activation pendant une partie réinitialise la console.");
    ImGui::PopFont();
}
void ra_login_controls(State& s, ravenemudsk_ra::Client& ra) {
    const auto snap = ra.snapshot();
    ImGui::BeginDisabled(snap.login_pending);
    ImGui::SetNextItemWidth(std::min(380.f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##ra-user", "Identifiant RetroAchievements", s.ra_user, sizeof(s.ra_user));
    ImGui::SetNextItemWidth(std::min(380.f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##ra-password", "Mot de passe", s.ra_password, sizeof(s.ra_password),
                             ImGuiInputTextFlags_Password);
    if (button("Se connecter", Icon::Play, ButtonStyle::Primary)) {
        ra.login(s.ra_user, s.ra_password);
        std::fill(std::begin(s.ra_password), std::end(s.ra_password), '\0');
    }
    ImGui::EndDisabled();
    if (snap.can_retry) {
        if (!snap.login_pending && button("Réessayer", Icon::Reset, ButtonStyle::Secondary)) ra.retry();
        if (button("Oublier la session", Icon::Exit, ButtonStyle::Ghost)) s.ra_confirm_disconnect = true;
    }
    muted("Le mot de passe n'est jamais enregistré. La session peut être mémorisée dans le coffre de votre système.");
}
// ---------------------------------------------------------------- profile
std::string player_name(const State& s) {
    const auto name = s.settings.get("pseudo");
    return name.empty() ? "Joueur" : name;
}
fs::path data_dir() { return data_file().parent_path(); }
void draw_player_avatar(State& s, ImDrawList* d, ImVec2 center, float r) {
    const auto file = s.settings.get("avatar_image");
    SDL_Texture* image = file.empty() ? nullptr : image_for(s, "avatar:" + file, {data_dir() / from_utf8(file)});
    draw_avatar(d, center, r, player_name(s), s.settings.number("avatar_color", 0), image, s.fonts);
}
void draw_ra_brand(State& s, ImDrawList* d, ImVec2 center, float width) {
    if (s.ra_logo) {
        int iw=0, ih=0;
        if (SDL_QueryTexture(s.ra_logo,nullptr,nullptr,&iw,&ih)==0 && iw>0 && ih>0) {
            const float height=width*static_cast<float>(ih)/static_cast<float>(iw);
            d->AddImage(reinterpret_cast<ImTextureID>(s.ra_logo),
                {center.x-width*.5f,center.y-height*.5f},
                {center.x+width*.5f,center.y+height*.5f});
            return;
        }
    }
    // Only a simple text fallback is needed if the packaged PNG is missing.
    const char* fallback="RA";
    const float fsize=std::max(10.f,width*.45f);
    const ImVec2 sz=s.fonts.caption->CalcTextSizeA(fsize,FLT_MAX,0.f,fallback);
    d->AddText(s.fonts.caption,fsize,
        {center.x-sz.x*.5f,center.y-sz.y*.5f},color::accent,fallback);
}
void draw_ra_identity(State& s, ImDrawList* d, ImVec2 center, float radius) {
    const auto snap=retro_client(s).snapshot();
    if (snap.logged_in) {
        SDL_Texture* avatar=s.ra_images ? s.ra_images->image(snap.avatar_url) : nullptr;
        const std::string name=snap.display_name.empty() ? snap.username : snap.display_name;
        draw_avatar(d,center,radius,name,0,avatar,s.fonts);
    } else {
        draw_ra_brand(s,d,center,radius*2.f);
    }
}
// RetroAchievements events become toasts; a Hardcore refusal also updates the setting.
void retro_events(State& s) {
    for (const auto& event : retro_client(s).drain_events()) {
        if (event.kind == "hardcore_refused") {
            // Keep the setting in line, so the refusal is not repeated at every launch.
            s.settings.set_flag("ra_hardcore", false);
            save_settings(s);
            continue;
        }
        if (event.kind == "login" || event.kind == "info" || event.kind == "notice" ||
            event.kind == "offline" || event.kind == "online" || event.kind == "error")
            notify(s, event.kind == "error" ? Toast::Kind::Error : Toast::Kind::Info, event.title, event.detail);
    }
}
void save_settings(State& s) {
    try {
        s.settings.save();
    } catch (const std::exception& e) {
        notify(s, Toast::Kind::Error, "Réglages non enregistrés", e.what());
    }
}
void set_avatar(State& s, const fs::path& image) {
    try {
        if (!is_image(image)) throw std::invalid_argument("Image PNG, JPEG ou BMP attendue");
        std::error_code ec;
        if (!fs::is_regular_file(image, ec)) throw std::invalid_argument("Image introuvable");
        const std::string name = "avatar" + lower(utf8(image.extension()));
        const auto target = data_dir() / name;
        if (!fs::exists(target, ec) || !fs::equivalent(image, target, ec))
            fs::copy_file(image, target, fs::copy_options::overwrite_existing);
        for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp"})
            if (std::string("avatar") + ext != name) fs::remove(data_dir() / (std::string("avatar") + ext), ec);
        forget_image(s, "avatar:" + s.settings.get("avatar_image"));
        forget_image(s, "avatar:" + name);
        s.settings.set("avatar_image", name);
        save_settings(s);
        notify(s, Toast::Kind::Success, "Avatar mis à jour");
    } catch (const std::exception& e) {
        notify(s, Toast::Kind::Error, "Avatar non modifié", e.what());
    }
}
// Level badge, XP bar and "x / y XP" caption on one line.
void level_line(State& s, ImDrawList* d, ImVec2 p, float bar_w, const Level& level) {
    const std::string label = "Niveau " + std::to_string(level.level);
    pill(d, p, label.c_str(), color::gold, s.fonts.caption);
    const float x = p.x + pill_size(label.c_str(), s.fonts.caption).x + space::md;
    progress_bar(d, {x, p.y + 8.f}, {x + bar_w, p.y + 14.f}, level.progress(), color::accent);
    const std::string xp = std::to_string(level.xp) + " / " + std::to_string(level.next_level) + " XP";
    text_at(d, s.fonts.caption, {x + bar_w + space::md, p.y + 3.f}, color::text_faint, xp);
}
void profile_banner(State& s, Library&) {
    const auto snap=retro_client(s).snapshot();
    const ImVec2 pos=ImGui::GetCursorScreenPos();
    const float w=ImGui::GetContentRegionAvail().x;
    const float h=88.f;
    if(ImGui::InvisibleButton("##ra-account-banner",{w,h}))s.page=Page::Profile;
    auto* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(pos,{pos.x+w,pos.y+h},
                     ImGui::IsItemHovered() ? color::surface_hover : color::surface,radius::lg);
    d->AddRect(pos,{pos.x+w,pos.y+h},color::border,radius::lg);
    draw_ra_identity(s,d,{pos.x+43.f,pos.y+44.f},28.f);
    const auto name=snap.logged_in
        ? (snap.display_name.empty()?snap.username:snap.display_name)
        : std::string("Connexion RetroAchievements");
    text_at(d,s.fonts.title,{pos.x+82.f,pos.y+18.f},color::text,
            ellipsize(s.fonts.title,name,std::max(110.f,w-106.f)));
    const auto desc=snap.logged_in
        ? std::to_string(snap.softcore_points)+" pts Softcore  |  "+
          std::to_string(snap.hardcore_points)+" pts Hardcore"
        : "Connectez votre compte pour obtenir les succès officiels";
    text_at(d,s.fonts.caption,{pos.x+82.f,pos.y+52.f},color::text_muted,
            ellipsize(s.fonts.caption,desc,std::max(110.f,w-106.f)));
}
void profile_view(State& s, Library& lib) {
    // RavenEmu presents one RA account dashboard, reused by the Profile entry.
    achievements_view(s,lib);
}
// ---------------------------------------------------------------- TV mode
// Full screen, couch-friendly library driven by a gamepad or the keyboard.
constexpr int tv_category_count = 5;
const char* const tv_categories[tv_category_count] = {
    "Récents", "Tous les jeux", "Game Boy", "Game Boy Color", "Game Boy Advance"};
std::vector<int> tv_games(const Library& lib, int category) {
    if (category == 0) return recent_order(lib);
    auto ids = library_order(lib, 0);
    static const char* const systems[tv_category_count] = {"", "", "gb", "gbc", "gba"};
    if (category > 1)
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int i) {
            return lib.games()[static_cast<std::size_t>(i)].system != systems[category];
        }), ids.end());
    return ids;
}
// `remember` stores the mode so that the next launch reopens it.
void set_tv(State& s, bool on, bool remember) {
    s.tv = on;
    s.tv_menu = -1;
    if (s.window) SDL_SetWindowFullscreen(s.window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    // TV mode is driven by the controller or the keyboard: no mouse, no cursor.
    if (ImGui::GetCurrentContext()) {
        auto& io = ImGui::GetIO();
        if (on) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
    if (on) {
        // Open on the recent games when there are any.
        s.tv_category = recent_order(*s.library).empty() ? 1 : 0;
        s.tv_index = 0;
        s.tv_scroll = 0.f;
    }
    if (!remember) return;
    s.settings.set_flag("tv_mode", on);
    save_settings(s);
}
bool pressed(std::initializer_list<ImGuiKey> keys, bool repeat) {
    for (const ImGuiKey key : keys)
        if (ImGui::IsKeyPressed(key, repeat)) return true;
    return false;
}
// Button glyph such as "A", "LB" or "Échap"; returns its width.
float key_glyph(ImDrawList* d, ImVec2 p, const char* label, ImFont* font) {
    const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, label);
    const float h = ts.y + 10.f, w = std::max(h, ts.x + 20.f);
    d->AddRectFilled(p, {p.x + w, p.y + h}, color::elevated, h * .5f);
    d->AddRect(p, {p.x + w, p.y + h}, color::border_strong, h * .5f, 0, 1.5f);
    d->AddText(font, font->FontSize, {p.x + (w - ts.x) * .5f, p.y + 5.f}, color::text, label);
    return w;
}
// A hint key centred on `cy`: the connected controller's button as printed on
// it, otherwise the keyboard key. With no draw list it only measures.
float hint_key(ImDrawList* d, float x, float cy, const std::optional<PadFamily>& pad, PadButton button,
               const char* key, const Fonts& fonts) {
    if (pad) {
        constexpr float h = 30.f;
        return pad_glyph(d, {x, cy - h * .5f}, *pad, button, fonts.strong, h);
    }
    const ImVec2 ts = fonts.caption->CalcTextSizeA(fonts.caption->FontSize, FLT_MAX, 0.f, key);
    if (!d) return std::max(ts.y + 10.f, ts.x + 20.f);
    return key_glyph(d, {x, cy - (ts.y + 10.f) * .5f}, key, fonts.caption);
}
// TV menu over the carousel: back to it, desktop mode, or quit.
void tv_menu_panel(State& s, ImDrawList* d, ImVec2 screen, bool input) {
    struct Entry { Icon icon; const char* label; };
    static constexpr Entry entries[] = {
        {Icon::Play, "Reprendre"}, {Icon::Restore, "Mode bureau"}, {Icon::Exit, "Quitter RavenEmu"}};
    constexpr int count = static_cast<int>(sizeof(entries) / sizeof(entries[0]));
    if (input) {
        if (pressed({ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp}, true))
            s.tv_menu = (s.tv_menu + count - 1) % count;
        if (pressed({ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown}, true))
            s.tv_menu = (s.tv_menu + 1) % count;
        if (pressed({ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadStart, ImGuiKey_Escape, ImGuiKey_Backspace}, false)) {
            s.tv_menu = -1;
            return;
        }
        if (pressed({ImGuiKey_GamepadFaceDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter}, false)) {
            const int choice = s.tv_menu;
            s.tv_menu = -1;
            if (choice == 1) set_tv(s, false);
            if (choice == 2) { s.launch.reset(); s.done = true; }
            return;
        }
    }
    d->AddRectFilled({0, 0}, screen, with_alpha(color::background, .78f));
    const float w = std::min(560.f, screen.x - 64.f), row = 68.f;
    const float h = 104.f + count * row + 76.f;
    const ImVec2 a{(screen.x - w) * .5f, (screen.y - h) * .5f}, b{a.x + w, a.y + h};
    d->AddRectFilled({a.x, a.y + 12.f}, {b.x, b.y + 12.f}, color::shadow, radius::lg);
    d->AddRectFilled(a, b, color::elevated, radius::lg);
    d->AddRect(a, b, color::border_strong, radius::lg);
    text_at(d, s.fonts.tv_title, {a.x + 36.f, a.y + 32.f}, color::text, "Menu");
    for (int i = 0; i < count; ++i) {
        const ImVec2 ra{a.x + 20.f, a.y + 100.f + i * row}, rb{b.x - 20.f, a.y + 100.f + i * row + row - 8.f};
        const bool on = i == s.tv_menu;
        if (on) d->AddRectFilled(ra, rb, color::accent, radius::md);
        const float mid = (ra.y + rb.y) * .5f;
        draw_icon(d, entries[i].icon, {ra.x + 36.f, mid}, 24.f, on ? IM_COL32_WHITE : color::accent_hover);
        text_at(d, s.fonts.tv_body, {ra.x + 68.f, mid - s.fonts.tv_body->FontSize * .5f - 2.f},
                on ? IM_COL32_WHITE : color::text, entries[i].label);
    }
    const auto controller = connected_pad();
    float hx = a.x + 36.f;
    const float hy = b.y - 52.f;
    hx += hint_key(d, hx, hy + 14.f, controller, PadButton::South, "Entrée", s.fonts) + 8.f;
    text_at(d, s.fonts.tv_body, {hx + 4.f, hy + 2.f}, color::text_muted, "Choisir");
    hx += text_width(s.fonts.tv_body, "Choisir") + 44.f;
    hx += hint_key(d, hx, hy + 14.f, controller, PadButton::East, "Échap", s.fonts) + 8.f;
    text_at(d, s.fonts.tv_body, {hx + 4.f, hy + 2.f}, color::text_muted, "Retour");
}
void tv_view(State& s, Library& lib) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    auto* d = ImGui::GetWindowDrawList();
    // Start opens the TV menu, also with Select held as in a game; leaving TV
    // mode is one of its entries, never a single press.
    const bool menu_was_open = s.tv_menu >= 0;
    if (!menu_was_open && pressed({ImGuiKey_GamepadStart, ImGuiKey_Escape}, false)) s.tv_menu = 0;
    const bool browsing = s.tv_menu < 0;
    const int previous = s.tv_category;
    if (browsing && pressed({ImGuiKey_GamepadL1, ImGuiKey_Q, ImGuiKey_PageUp}, false))
        s.tv_category = (s.tv_category + tv_category_count - 1) % tv_category_count;
    if (browsing && pressed({ImGuiKey_GamepadR1, ImGuiKey_E, ImGuiKey_PageDown}, false))
        s.tv_category = (s.tv_category + 1) % tv_category_count;
    if (s.tv_category != previous) { s.tv_index = 0; s.tv_scroll = 0.f; }
    const auto ids = tv_games(lib, s.tv_category);
    const int count = static_cast<int>(ids.size());
    if (browsing) {
        if (pressed({ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft}, true)) --s.tv_index;
        if (pressed({ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight}, true)) ++s.tv_index;
        if (io.MouseWheel > 0.f) --s.tv_index;
        if (io.MouseWheel < 0.f) ++s.tv_index;
    }
    s.tv_index = std::clamp(s.tv_index, 0, std::max(0, count - 1));
    s.tv_scroll += (static_cast<float>(s.tv_index) - s.tv_scroll) * std::min(1.f, io.DeltaTime * 12.f);
    const Game* game = count ? &lib.games()[static_cast<std::size_t>(ids[static_cast<std::size_t>(s.tv_index)])] : nullptr;
    if (game) s.chosen = ids[static_cast<std::size_t>(s.tv_index)];
    const ImU32 tint = game ? system_color(game->system) : color::accent;

    gradient_rect(d, {0, 0}, screen, mix(color::background, tint, .20f), color::background, 0.f);
    for (int i = 0; i < 3; ++i)
        d->AddCircleFilled({screen.x * .5f, screen.y * .45f}, screen.y * (.35f + i * .12f),
                           with_alpha(tint, .035f), 96);
    // Top bar: brand on the left, player and clock on the right.
    const float pad = std::max(48.f, screen.x * .04f);
    if (s.logo) d->AddImage(reinterpret_cast<ImTextureID>(s.logo), {pad, 30.f}, {pad + 56.f, 86.f});
    text_at(d, s.fonts.tv_title, {pad + 72.f, 42.f}, color::text, "RAVENEMU");
    std::array<char, 8> clock{};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(clock.data(), clock.size(), "%H:%M", &local);
    float rx = screen.x - pad - text_width(s.fonts.tv_title, clock.data());
    text_at(d, s.fonts.tv_title, {rx, 42.f}, color::text, clock.data());
    rx -= 48.f;
    const auto account = retro_client(s).snapshot();
    const std::string full_name = account.logged_in ?
        (account.display_name.empty() ? account.username : account.display_name) : player_name(s);
    const float brand_right = pad + 72.f + text_width(s.fonts.tv_title, "RAVENEMU");
    const std::string name = ellipsize(s.fonts.tv_body, full_name,
        std::max(0.f, rx - 56.f - brand_right - 32.f));
    const float nw = text_width(s.fonts.tv_body, name.c_str());
    if (account.logged_in) draw_ra_identity(s, d, {rx - 22.f, 58.f}, 22.f);
    else draw_player_avatar(s, d, {rx - 22.f, 58.f}, 22.f);
    text_at(d, s.fonts.tv_body, {rx - 56.f - nw, 45.f}, color::text_muted, name);

    // Category tabs between LB and RB.
    const auto controller = connected_pad();
    // Tab padding shrinks on narrow screens so that LB / RB stay visible.
    float labels_w = 0.f;
    for (const char* label : tv_categories) labels_w += text_width(s.fonts.tv_body, label);
    const float tab_pad = std::clamp((screen.x - 2.f * pad - 160.f - labels_w) / (2.f * tv_category_count + 2.f),
                                     8.f, 20.f);
    const float tabs_w = labels_w + tv_category_count * (tab_pad * 2.f + 8.f) - 8.f;
    float tx = (screen.x - tabs_w) * .5f;
    const float ty = 124.f;
    const float lb_w = hint_key(nullptr, 0.f, 0.f, controller, PadButton::LeftShoulder, "Q", s.fonts);
    hint_key(d, tx - 16.f - lb_w, ty + 22.f, controller, PadButton::LeftShoulder, "Q", s.fonts);
    for (int i = 0; i < tv_category_count; ++i) {
        const float w = text_width(s.fonts.tv_body, tv_categories[i]) + tab_pad * 2.f;
        const bool on = i == s.tv_category;
        if (on) d->AddRectFilled({tx, ty}, {tx + w, ty + 44.f}, color::accent, 22.f);
        text_at(d, s.fonts.tv_body, {tx + tab_pad, ty + 9.f}, on ? IM_COL32_WHITE : color::text_muted,
                tv_categories[i]);
        if (browsing && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect({tx, ty}, {tx + w, ty + 44.f})) {
            s.tv_category = i; s.tv_index = 0; s.tv_scroll = 0.f;
        }
        tx += w + 8.f;
    }
    hint_key(d, tx + 16.f, ty + 22.f, controller, PadButton::RightShoulder, "E", s.fonts);

    // Carousel: the selected cover is large, its neighbours smaller and dimmed.
    // "small" is a Win32 macro (rpcndr.h), hence these names.
    const float big = std::clamp(screen.y * .36f, 180.f, 420.f), side = big * .66f;
    const float gap = side + 32.f;
    const float cy = ty + 80.f + big * .5f;
    if (!count) {
        const char* empty = s.tv_category == 0 ? "Aucune partie récente : choisissez « Tous les jeux »."
                                                : "Aucun jeu dans cette catégorie.";
        const float ew = text_width(s.fonts.tv_body, empty);
        text_at(d, s.fonts.tv_body, {(screen.x - ew) * .5f, cy - 12.f}, color::text_muted, empty);
    }
    std::vector<int> order;
    for (int i = 0; i < count; ++i)
        if (std::abs(static_cast<float>(i) - s.tv_scroll) * gap < screen.x * .5f + big) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::abs(static_cast<float>(a) - s.tv_scroll) > std::abs(static_cast<float>(b) - s.tv_scroll);
    });
    bool launch_now = false;
    for (int i : order) {
        const float off = static_cast<float>(i) - s.tv_scroll;
        const float t = std::clamp(1.f - std::abs(off), 0.f, 1.f);
        const float size = side + (big - side) * t;
        const float shift = std::copysign(std::min(1.f, std::abs(off)) * (big - side) * .5f, off);
        const float x = screen.x * .5f + off * gap + shift;
        const ImVec2 a{x - size * .5f, cy - size * .5f}, b{x + size * .5f, cy + size * .5f};
        const Game& g = lib.games()[static_cast<std::size_t>(ids[static_cast<std::size_t>(i)])];
        d->AddRectFilled({a.x + 8.f, a.y + 14.f}, {b.x - 8.f, b.y + 14.f}, color::shadow, radius::lg);
        draw_cover(d, a, b, display_title(g), g.system, cover_for(s, g), s.fonts, radius::lg);
        if (i == s.tv_index) {
            d->AddRect({a.x - 5.f, a.y - 5.f}, {b.x + 5.f, b.y + 5.f}, color::accent_hover, radius::lg + 5.f, 0, 3.f);
        } else {
            d->AddRectFilled(a, b, with_alpha(color::background, .5f * (1.f - t)), radius::lg);
        }
        if (browsing && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(a, b)) {
            if (i == s.tv_index) launch_now = true;
            else s.tv_index = i;
        }
    }
    if (game) {
        const std::string title = ellipsize(s.fonts.tv_display, display_title(*game), screen.x - pad * 2.f);
        const float y = cy + big * .5f + 36.f;
        text_at(d, s.fonts.tv_display, {(screen.x - text_width(s.fonts.tv_display, title.c_str())) * .5f, y},
                color::text, title);
        std::string meta = system_name(game->system);
        meta += "  \xC2\xB7  " + ago(game->last_played);
        if (game->seconds_played) meta += "  \xC2\xB7  " + duration_of(game->seconds_played) + " de jeu";
        if (const auto& info = game_achievements(s, *game))
            meta += "  \xC2\xB7  " + achievement_count(*info) + " succès";
        text_at(d, s.fonts.tv_body, {(screen.x - text_width(s.fonts.tv_body, meta.c_str())) * .5f, y + 66.f},
                color::text_muted, meta);
    }
    // Hints, adapted to the connected controller.
    struct Hint { int count; PadButton buttons[2]; const char* keys[2]; const char* action; };
    const Hint hints[] = {
        {1, {PadButton::South, PadButton::South}, {"Entrée", nullptr}, "Jouer"},
        {2, {PadButton::LeftShoulder, PadButton::RightShoulder}, {"Q", "E"}, "Catégorie"},
        {1, {PadButton::Start, PadButton::Start}, {"Échap", nullptr}, "Menu"}};
    float hint_w = -40.f;
    for (const auto& h : hints) {
        for (int k = 0; k < h.count; ++k) hint_w += hint_key(nullptr, 0.f, 0.f, controller, h.buttons[k], h.keys[k], s.fonts) + 8.f;
        hint_w += text_width(s.fonts.tv_body, h.action) + 44.f;
    }
    float hx = (screen.x - hint_w) * .5f;
    const float hy = screen.y - 68.f;
    for (const auto& h : hints) {
        for (int k = 0; k < h.count; ++k) hx += hint_key(d, hx, hy + 14.f, controller, h.buttons[k], h.keys[k], s.fonts) + 8.f;
        text_at(d, s.fonts.tv_body, {hx + 4.f, hy + 2.f}, color::text_muted, h.action);
        hx += text_width(s.fonts.tv_body, h.action) + 44.f;
    }
    if (!browsing) {
        // The press that opened the menu does not act in it.
        tv_menu_panel(s, d, screen, menu_was_open);
        return;
    }
    if (game && (launch_now || pressed({ImGuiKey_GamepadFaceDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter}, false)))
        launch(s, *game);
}
// ---------------------------------------------------------------- updates
// One check per run of RavenEmu, shared by every launcher session.
UpdateCheck& updates() {
    static UpdateCheck check;
    return check;
}
bool& update_dismissed() {
    static bool dismissed = false;  // "Plus tard": hidden until the next start
    return dismissed;
}
void open_release(State& s) {
    if (const auto latest = updates().latest()) {
        if (SDL_OpenURL(release_page(*latest).c_str()) != 0)
            notify(s, Toast::Kind::Error, "Navigateur indisponible", release_page(*latest));
    }
}
void update_banner(State& s) {
    const auto latest = updates().latest();
    if (updates().status() != UpdateCheck::Status::Available || !latest || update_dismissed()) return;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 72.f;
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, {p.x + w, p.y + h}, color::accent_soft, radius::lg);
    d->AddRect(p, {p.x + w, p.y + h}, with_alpha(color::accent, .7f), radius::lg);
    d->AddCircleFilled({p.x + 36.f, p.y + h * .5f}, 18.f, color::accent, 32);
    draw_icon(d, Icon::Save, {p.x + 36.f, p.y + h * .5f}, 16.f, IM_COL32_WHITE);
    text_at(d, s.fonts.strong, {p.x + 68.f, p.y + 16.f}, color::text,
            "RavenEmu " + to_string(*latest) + " est disponible");
    text_at(d, s.fonts.caption, {p.x + 68.f, p.y + 40.f}, color::text_muted,
            std::string("Vous utilisez la version ") + RAVENEMUDSK_VERSION + ". Vos sauvegardes sont conservées.");
    const float later_w = text_width(s.fonts.body, "Plus tard") + 32.f;
    const float get_w = text_width(s.fonts.body, "Télécharger") + 56.f;
    ImGui::SetCursorScreenPos({p.x + w - later_w - get_w - space::lg - space::sm, p.y + 16.f});
    ImGui::PushID("update");
    if (button("Télécharger", Icon::Save, ButtonStyle::Primary, {get_w, 40.f})) open_release(s);
    ImGui::SameLine(0.f, space::sm);
    if (button("Plus tard", Icon::Close, ButtonStyle::Ghost, {later_w, 40.f}, false)) update_dismissed() = true;
    ImGui::PopID();
    ImGui::SetCursorScreenPos({p.x, p.y + h + space::md});
    ImGui::Dummy({0.f, 0.f});
}
void updates_card(State& s) {
    begin_card("##updates");
    card_title(s, Icon::Save, "Mises à jour");
    ImGui::Text("Version installée : %s", RAVENEMUDSK_VERSION);
    bool automatic = s.settings.flag("update_check", true);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(color::elevated));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    if (ImGui::Checkbox("Rechercher les nouvelles versions au démarrage", &automatic)) {
        s.settings.set_flag("update_check", automatic);
        save_settings(s);
    }
    ImGui::PopStyleColor(2);
    const auto status = updates().status();
    const auto latest = updates().latest();
    std::string line;
    switch (status) {
    case UpdateCheck::Status::Idle: line = "Aucune vérification depuis le lancement."; break;
    case UpdateCheck::Status::Checking: line = "Vérification en cours\xE2\x80\xA6"; break;
    case UpdateCheck::Status::UpToDate: line = "RavenEmu est à jour."; break;
    case UpdateCheck::Status::NoRelease: line = "Aucune version n'est encore publiée sur GitHub."; break;
    case UpdateCheck::Status::Available:
        line = "La version " + (latest ? to_string(*latest) : std::string("?")) + " est disponible.";
        break;
    case UpdateCheck::Status::Failed: line = "Vérification impossible : " + updates().error(); break;
    }
    muted(line.c_str());
    ImGui::Dummy({0.f, space::xs});
    if (status == UpdateCheck::Status::Available) {
        if (button("Télécharger", Icon::Save, ButtonStyle::Primary)) open_release(s);
        ImGui::SameLine(0.f, space::sm);
    }
    if (UpdateCheck::supported() && status != UpdateCheck::Status::Checking &&
        button("Vérifier maintenant", Icon::Reset, ButtonStyle::Secondary))
        updates().start(RAVENEMUDSK_VERSION);
    ImGui::PushFont(s.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::text_faint));
    ImGui::TextWrapped("RavenEmu lit seulement le numéro de la dernière version sur api.github.com ; aucune "
                       "donnée sur vous ou vos jeux n'est envoyée. Le téléchargement s'ouvre dans votre navigateur.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    end_card();
}
void folder_row(State& s, const char* label, const char* detail, const fs::path& folder) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, {p.x + w, p.y + 64.f}, color::elevated, radius::md);
    draw_icon(d, Icon::Folder, {p.x + 28.f, p.y + 32.f}, 20.f, color::accent_hover);
    text_at(d, s.fonts.strong, {p.x + 56.f, p.y + 12.f}, color::text, label);
    const std::string path = utf8(folder);
    text_at(d, s.fonts.caption, {p.x + 56.f, p.y + 36.f}, color::text_faint,
            ellipsize(s.fonts.caption, path + "  \xC2\xB7  " + detail, w - 200.f));
    ImGui::SetCursorScreenPos({p.x + w - 116.f, p.y + 12.f});
    ImGui::PushID(label);
    if (button("Ouvrir", Icon::Folder, ButtonStyle::Secondary, {104.f, 40.f})) open_folder(folder);
    ImGui::PopID();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", path.c_str());
    ImGui::SetCursorScreenPos({p.x, p.y + 64.f + space::sm});
    ImGui::Dummy({0.f, 0.f});
}
void settings_library(State& s, Library& lib) {
    begin_card("##library");
    card_title(s, Icon::Library, "Bibliothèque");
    muted("RavenEmu mémorise seulement l'emplacement de vos jeux : les ROMs restent dans leur dossier, "
          "sur ce PC.");
    ImGui::Dummy({0.f, space::xs});
    ImGui::PushFont(s.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::text_faint));
    ImGui::TextWrapped("Fichier de bibliothèque : %s", utf8(data_file()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    if (button("Ajouter un jeu", Icon::Plus, ButtonStyle::Primary)) add_game(s, lib);
    ImGui::SameLine(0.f, space::sm);
    if (button("Importer un dossier", Icon::Folder)) pick_and_import_folder(s, lib);
    if (ImGui::GetContentRegionAvail().x >= 720.f) ImGui::SameLine(0.f, space::sm);
    if (button("Saisir un chemin", Icon::Keyboard, ButtonStyle::Ghost)) s.open_manual = true;
    end_card();

    begin_card("##folders");
    card_title(s, Icon::Folder, "Sauvegardes et jaquettes");
    muted("RavenEmu range automatiquement vos fichiers dans deux dossiers à côté du programme. "
          "Copiez-les pour sauvegarder ou transférer votre progression.");
    ImGui::Dummy({0.f, space::xs});
    folder_row(s, "Sauvegardes", "sauvegardes de jeu, états et dernières captures",
               s.folders.saves);
    folder_row(s, "Jaquettes", "nommez l'image comme la ROM, par exemple MonJeu.png", s.folders.covers);
    end_card();
}

// Controller column of the controls table, named as on the connected controller.
std::string controller_label(KeyAction action, const char* fallback, const std::optional<PadFamily>& pad) {
    if (!pad) return fallback;
    switch (action) {
    case KeyAction::A: return pad_button_name(*pad, PadButton::South);
    case KeyAction::B: return pad_button_name(*pad, PadButton::East);
    case KeyAction::Start: return pad_button_name(*pad, PadButton::Start);
    case KeyAction::Select: return pad_button_name(*pad, PadButton::Back);
    case KeyAction::L: return pad_button_name(*pad, PadButton::LeftShoulder);
    case KeyAction::R: return pad_button_name(*pad, PadButton::RightShoulder);
    case KeyAction::Menu:
        return std::string(pad_button_name(*pad, PadButton::Back)) + " + " + pad_button_name(*pad, PadButton::Start);
    default: return fallback;
    }
}
void settings_display(State& s) {
    begin_card("##tv");
    card_title(s, Icon::Gamepad, "Mode TV / manette");
    const char* start = pad_button_name(connected_pad().value_or(PadFamily::Generic), PadButton::Start);
    muted((std::string("Plein écran, grandes jaquettes et navigation à la manette depuis le canapé, sans curseur de souris. ") +
           start + " ou Échap ouvre son menu, qui ramène au mode bureau ; RavenEmu s'en souvient au prochain lancement.").c_str());
    ImGui::Dummy({0.f, space::xs});
    if (button("Passer en mode TV", Icon::Fullscreen, ButtonStyle::Primary)) set_tv(s, true);
    end_card();
}

void settings_controls(State& s) {
    KeyboardBindings keys;keys.load(s.settings);
    const auto pad=connected_pad();
    begin_card("##controls");
    card_title(s, Icon::Gamepad, "Commandes en jeu");
    muted("Cliquez sur une touche pour la modifier. Les changements sont enregistrés automatiquement.");
    muted("Les touches déjà utilisées sont échangées. Échap reste toujours disponible pour ouvrir le menu.");
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImGui::ColorConvertU32ToFloat4(with_alpha(color::elevated, .45f)));
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Clavier", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Manette", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::PushFont(s.fonts.caption);
        ImGui::TableNextRow();
        for (const char* label : {"ACTION", "CLAVIER", "MANETTE"}) {
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_faint), "%s", label);
        }
        ImGui::PopFont();
        for (std::size_t i=0;i<key_commands().size();++i) {
            const auto& command=key_commands()[i];ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow(0,42.f);
            ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextWrapped("%s",command.label);
            ImGui::TableNextColumn();
            const auto key_name=keys.label(static_cast<KeyAction>(i));
            const float key_width=ImGui::GetContentRegionAvail().x;
            const auto key_caption=ellipsize(s.fonts.body,key_name,std::max(16.f,key_width-64.f));
            if(button(key_caption.c_str(),Icon::Keyboard,ButtonStyle::Secondary,{key_width,34.f})){
                s.editing_key=static_cast<int>(i);s.key_error.clear();
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",key_name.c_str());
            ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s",controller_label(static_cast<KeyAction>(i),command.controller,pad).c_str());
            ImGui::PopID();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
    if(button("Rétablir les touches par défaut",Icon::Reset,ButtonStyle::Secondary)){
        auto settings=s.settings;KeyboardBindings{}.store(settings);
        try{settings.save();s.settings=std::move(settings);notify(s,Toast::Kind::Success,"Touches réinitialisées");}
        catch(const std::exception& e){notify(s,Toast::Kind::Error,"Touches non enregistrées",e.what());}
    }
    end_card();
}

void key_capture_dialog(State& s){
    // One stable popup ID, independent of the edited row/category scroll.
    if(s.editing_key>=0&&!ImGui::IsPopupOpen("##key-capture"))ImGui::OpenPopup("##key-capture");
    ImGui::SetNextWindowSize({std::min(500.f,ImGui::GetIO().DisplaySize.x-48.f),0.f});
    if(ImGui::BeginPopupModal("##key-capture",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize)){
        if(s.editing_key<0)ImGui::CloseCurrentPopup();
        else{
            ImGui::TextWrapped("Nouvelle touche : %s",key_commands()[s.editing_key].label);
            ImGui::TextWrapped("Appuyez sur une touche du clavier, sans combinaison. Échap annule.");
            if(!s.key_error.empty())ImGui::TextWrapped("%s",s.key_error.c_str());
            if(button("Non attribuée",Icon::Close,ButtonStyle::Secondary)){
                auto settings=s.settings;KeyboardBindings keys;keys.load(settings);
                keys.assign(static_cast<KeyAction>(s.editing_key),SDLK_UNKNOWN);keys.store(settings);
                try{settings.save();s.settings=std::move(settings);s.editing_key=-1;ImGui::CloseCurrentPopup();}
                catch(const std::exception& e){s.key_error=e.what();}
            }
            ImGui::SameLine();
            if(button("Annuler",Icon::Close,ButtonStyle::Ghost)){s.editing_key=-1;ImGui::CloseCurrentPopup();}
        }
        ImGui::EndPopup();
    }
}
bool capture_keyboard_event(State& s,const SDL_Event& event){
    if(s.editing_key<0)return false;
    if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){s.editing_key=-1;return false;}
    if(event.type==SDL_KEYDOWN&&!event.key.repeat){
        if(event.key.keysym.sym==SDLK_ESCAPE)s.editing_key=-1;
        else{
            auto settings=s.settings;KeyboardBindings keys;keys.load(settings);
            if(!keys.assign(static_cast<KeyAction>(s.editing_key),event.key.keysym.sym))
                s.key_error="Touche indisponible. Choisissez une autre touche ou libérez son affectation.";
            else{
                keys.store(settings);
                try{settings.save();s.settings=std::move(settings);s.editing_key=-1;}
                catch(const std::exception& e){s.key_error=e.what();}
            }
        }
    }
    return event.type==SDL_KEYDOWN||event.type==SDL_TEXTINPUT||event.type==SDL_TEXTEDITING;
}

void settings_about(State& s) {
    begin_card("##about");
    card_title(s, Icon::Info, "À propos");
    ImGui::TextWrapped("RavenEmu - émulateur Game Boy, Game Boy Color et Game Boy Advance");
    muted("Préversion " RAVENEMUDSK_VERSION ". Licence AGPL-3.0-only. Cœurs d'émulation RavenCore (AGPL-3.0-only), "
          "succès RetroAchievements via rcheevos (MIT), interface Dear ImGui (MIT) "
          "et SDL2 (zlib). Aucun compte en ligne n'est nécessaire pour l'émulation. Pas de télémétrie. "
          "Les fonctions RetroAchievements transmettent au service les données du compte, du jeu et des succès. "
          "La recherche de mises à jour contacte GitHub.");
    end_card();
}
void settings_view(State& s, Library& lib) {
    struct Category {
        SettingsCategory id;
        Icon icon;
        const char* label;
        const char* description;
    };
    const Category categories[] = {
        {SettingsCategory::Library, Icon::Library, "Bibliothèque",
         "Importez vos jeux et retrouvez vos sauvegardes et jaquettes."},
        {SettingsCategory::Display, Icon::Fullscreen, "Affichage",
         "Choisissez comment utiliser RavenEmu sur votre écran."},
        {SettingsCategory::Controls, Icon::Gamepad, "Commandes",
         "Retrouvez les commandes au clavier et à la manette."},
        {SettingsCategory::Retro, Icon::Trophy, "RetroAchievements",
         "Choisissez votre mode de jeu et gérez la synchronisation des succès."},
        {SettingsCategory::Updates, Icon::Reset, "Mises à jour",
         "Vérifiez les nouvelles versions et gérez la recherche au démarrage."},
        {SettingsCategory::About, Icon::Info, "À propos",
         "Informations sur l’application et ses composants."}};
    const float height = std::max(1.f, ImGui::GetContentRegionAvail().y - content_pad -
                                     ImGui::GetStyle().ItemSpacing.y);
    ImGui::BeginChild("##settings-navigation", {208.f, height});
    ImGui::PushFont(s.fonts.caption);
    ImGui::TextDisabled("Catégories");
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    for (const auto& category : categories) {
        if (button(category.label, category.icon,
                   s.settings_category == category.id ? ButtonStyle::Primary : ButtonStyle::Ghost,
                   {ImGui::GetContentRegionAvail().x, 48.f}))
            s.settings_category = category.id;
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, space::xl);
    // Each category keeps its own scroll position, independent of the menu.
    ImGui::PushID(static_cast<int>(s.settings_category));
    ImGui::BeginChild("##settings-category", {0.f, height});
    for (const auto& category : categories) {
        if (category.id != s.settings_category) continue;
        ImGui::PushFont(s.fonts.display);
        ImGui::TextUnformatted(category.label);
        ImGui::PopFont();
        muted(category.description);
    }
    ImGui::Dummy({0.f, space::sm});
    switch (s.settings_category) {
    case SettingsCategory::Library: settings_library(s, lib); break;
    case SettingsCategory::Display: settings_display(s); break;
    case SettingsCategory::Controls: settings_controls(s); break;
    case SettingsCategory::Retro:
        begin_card("##ra-settings");card_title(s,Icon::Trophy,"Mode et synchronisation");ra_mode_controls(s);
        muted(retro_client(s).snapshot().status.c_str());
        if(button("Ouvrir mon profil",Icon::Trophy,ButtonStyle::Primary))s.page=Page::Profile;
        if(button("Confidentialité",Icon::Info,ButtonStyle::Ghost)){
            if(SDL_OpenURL("https://github.com/GhostPunishR/RavenEmuDSK/blob/main/docs/confidentialite.md")!=0)
                notify(s,Toast::Kind::Error,"Ouverture du navigateur impossible",SDL_GetError());
        }
        end_card();break;
    case SettingsCategory::Updates: updates_card(s); break;
    case SettingsCategory::About: settings_about(s); break;
    }
    ImGui::EndChild();
    ImGui::PopID();
}
void detail_achievements(State& s, const Game& game) {
    const auto info = game_achievements(s, game);
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID("detail-achievements");
    ImGui::PushFont(s.fonts.strong);
    ImGui::TextUnformatted("Succès");
    ImGui::PopFont();
    const std::string count = info ? achievement_count(*info) : "-";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f,
        ImGui::GetContentRegionAvail().x - text_width(s.fonts.caption, count.c_str())));
    ImGui::PushFont(s.fonts.caption);
    ImGui::TextUnformatted(count.c_str());
    ImGui::PopFont();
    if (info) {
        auto* d = ImGui::GetWindowDrawList();
        auto p = ImGui::GetCursorScreenPos();
        progress_bar(d, p, {p.x + w, p.y + 6.f}, info->total_count ?
            static_cast<float>(info->unlocked_count) / info->total_count : 0.f, color::gold);
        ImGui::Dummy({w, 14.f});
        ImGui::PushFont(s.fonts.caption);
        ImGui::TextDisabled("%u / %u points", info->unlocked_points, info->total_points);
        ImGui::TextDisabled("%s",info->game_hardcore?"Progression Hardcore":"Progression Softcore");
        if (info->cached_profile) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::TextDisabled("Données mémorisées. Actualisez les succès ou lancez le jeu.");
            ImGui::PopTextWrapPos();
        }
        ImGui::PopFont();
        if (info->total_count == 0) muted("Aucun succès officiel disponible pour ce jeu.");
        const auto limit = std::min<std::size_t>(info->achievements.size(), 3);
        for (std::size_t i = 0; i < limit; ++i) {
            const auto& achievement = info->achievements[i];
            p = ImGui::GetCursorScreenPos();
            d->AddRectFilled(p, {p.x + w, p.y + 64.f}, color::surface, radius::md);
            auto* image = s.ra_images ? s.ra_images->image(achievement.badge_url) : nullptr;
            if (image) d->AddImage(reinterpret_cast<ImTextureID>(image),
                {p.x + 12.f, p.y + 12.f}, {p.x + 52.f, p.y + 52.f});
            else draw_icon(d, achievement.unlocked ? Icon::Trophy : Icon::Lock,
                {p.x + 32.f, p.y + 32.f}, 20.f, achievement.unlocked ? color::gold : color::text_faint);
            ImGui::SetCursorScreenPos({p.x + 64.f, p.y + 11.f});
            ImGui::PushFont(s.fonts.strong);
            ImGui::TextUnformatted(ellipsize(s.fonts.strong, achievement.title, w - 76.f).c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos({p.x + 64.f, p.y + 36.f});
            ImGui::PushFont(s.fonts.caption);
            ImGui::TextDisabled("%d pts - %s", achievement.points,
                achievement.unlocked ? "Débloqué" : "À débloquer");
            ImGui::PopFont();
            ImGui::SetCursorScreenPos({p.x, p.y + 72.f});
        }
        if (!info->achievements.empty() && button("Voir tous les succès", Icon::Trophy,
            ButtonStyle::Secondary, {w, 38.f})) {
            s.ra_game_path = utf8(game.path);
            s.ra_filter = 0; s.ra_visible_count = 10; s.ra_selected_achievement.clear();
            s.page = Page::Profile;
        }
    } else {
        ImGui::PushFont(s.fonts.caption);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        const auto account = retro_client(s).snapshot();
        ImGui::TextDisabled(account.logged_in || account.cached_profile ?
            "Lancez ce jeu pour synchroniser ses succès RetroAchievements." :
            "Connectez votre compte dans Profil, puis lancez ce jeu pour synchroniser ses succès.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    ImGui::BeginDisabled(retro_client(s).snapshot().login_pending);
    if(button("Actualiser les succès",Icon::Reset,ButtonStyle::Ghost,{w,36.f})){
        if(!retro_client(s).refresh_game(utf8(game.path)))
            notify(s,Toast::Kind::Info,"Actualisation non démarrée",retro_client(s).snapshot().status);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}
void detail_panel(State& s, Library& lib, int index, ImVec2 pos, ImVec2 size) {
    const Game& game = lib.games()[static_cast<std::size_t>(index)];
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(color::sidebar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {space::xl, space::xl});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::BeginChild("##detail", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    auto* d = ImGui::GetWindowDrawList();
    d->AddLine({pos.x + .5f, pos.y}, {pos.x + .5f, pos.y + size.y}, color::border);
    const float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    d->AddRectFilled({p.x + 6, p.y + 10}, {p.x + w - 6, p.y + w + 10}, color::shadow, radius::lg);
    draw_cover(d, p, {p.x + w, p.y + w}, display_title(game), game.system, cover_for(s, game), s.fonts, radius::lg);
    ImGui::Dummy({w, w});
    ImGui::Dummy({0.f, space::xs});
    ImGui::PushFont(s.fonts.title);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    ImGui::TextUnformatted(display_title(game).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    p = ImGui::GetCursorScreenPos();
    pill(d, p, system_name(game.system), system_color(game.system), s.fonts.caption);
    ImGui::Dummy({w, pill_size("G", s.fonts.caption).y});
    ImGui::Dummy({0.f, space::xs});
    ImGui::PushID("detail");
    if (button("Jouer", Icon::Play, ButtonStyle::Primary, {w, 48.f})) launch(s, game);
    ImGui::PopID();
    ImGui::Dummy({0.f, space::xs});
    // Statistics as label / value rows.
    p = ImGui::GetCursorScreenPos();
    const std::pair<const char*, std::string> stats[] = {
        {"Dernière partie", game.last_played ? ago(game.last_played) : "Jamais"},
        {"Temps de jeu", game.seconds_played ? duration_of(game.seconds_played) : "-"}};
    d->AddRectFilled(p, {p.x + w, p.y + 88.f}, color::surface, radius::md);
    for (int i = 0; i < 2; ++i) {
        const float y = p.y + 14.f + i * 36.f;
        text_at(d, s.fonts.body, {p.x + space::md, y}, color::text_muted, stats[i].first);
        const auto value = ellipsize(s.fonts.strong, stats[i].second, w * .5f);
        text_at(d, s.fonts.strong, {p.x + w - space::md - text_width(s.fonts.strong, value.c_str()), y},
                color::text, value);
    }
    d->AddLine({p.x + space::md, p.y + 44.f}, {p.x + w - space::md, p.y + 44.f}, color::border);
    ImGui::Dummy({w, 88.f});
    ImGui::Dummy({0.f, space::xs});
    detail_achievements(s, game);
    if (SDL_Texture* shot = preview_for(s, game)) {
        ImGui::Dummy({0.f, space::xs});
        text_at(d, s.fonts.strong, ImGui::GetCursorScreenPos(), color::text, "Dernière capture");
        ImGui::Dummy({w, 24.f});
        p = ImGui::GetCursorScreenPos();
        const float h = w * 144.f / 160.f;
        d->AddImageRounded(reinterpret_cast<ImTextureID>(shot), p, {p.x + w, p.y + h}, {0, 0}, {1, 1},
                           IM_COL32_WHITE, radius::md);
        ImGui::Dummy({w, h});
    }
    ImGui::Dummy({0.f, space::sm});
    ImGui::PushFont(s.fonts.caption);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_faint), "FICHIER");
    const auto file = utf8(game.path.filename());
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_muted), "%s",
                       ellipsize(s.fonts.caption, file, w).c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", utf8(game.path).c_str());
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    if (button("Changer la jaquette", Icon::Folder, ButtonStyle::Secondary, {w, 38.f})) {
        if (const auto image = pick_image()) set_cover(s, game, *image);
        else { s.path_target = PathTarget::Cover; s.open_manual = true; }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("PNG, JPEG ou BMP, copiée dans le dossier Jaquettes.\nAstuce : glissez une image sur la fenêtre.");
    if (button("Retirer de la bibliothèque", Icon::Trash, ButtonStyle::Danger, {w, 38.f})) {
        const auto title = display_title(game);
        lib.remove(game.path);
        lib.save();
        s.chosen = lib.games().empty() ? -1 : 0;
        notify(s, Toast::Kind::Info, "Jeu retiré de la bibliothèque", title + " reste sur votre disque.");
    }
    ImGui::EndChild();
}
void manual_path_dialog(State& s, Library& lib) {
    if (s.open_manual) { ImGui::OpenPopup("Ajouter depuis un chemin"); s.open_manual = false; }
    const Game* cover_game = s.path_target == PathTarget::Cover ? chosen_game(s, lib) : nullptr;
    const bool avatar = s.path_target == PathTarget::Avatar;
    const bool image = cover_game || avatar;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {.5f, .5f});
    ImGui::SetNextWindowSize({560.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {space::xl, space::xl});
    if (ImGui::BeginPopupModal("Ajouter depuis un chemin", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(s.fonts.title);
        ImGui::TextUnformatted(avatar ? "Choisir un avatar" : cover_game ? "Choisir une jaquette"
                                                                         : "Ajouter depuis un chemin");
        ImGui::PopFont();
        muted(image ? "Collez le chemin d'une image PNG, JPEG ou BMP."
                    : "Collez le chemin d'une ROM (.gb, .gbc, .gba) ou d'un dossier à importer.");
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##path", "/chemin/vers/MonJeu.gb", s.manual_path,
                                                    sizeof(s.manual_path), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Dummy({0.f, space::xs});
        const float bw = 120.f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.f - space::sm);
        if (button("Annuler", Icon::Close, ButtonStyle::Ghost, {bw, 40.f}, false)) {
            s.path_target = PathTarget::Rom;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.f, space::sm);
        if (button(image ? "Valider" : "Importer", Icon::Plus, ButtonStyle::Primary, {bw, 40.f}) || enter) {
            const fs::path path = from_utf8(s.manual_path);
            std::error_code ec;
            if (avatar) set_avatar(s, path);
            else if (cover_game) set_cover(s, *cover_game, path);
            else if (fs::is_directory(path, ec)) import_folder(s, lib, path);
            else if (lib.add(path)) {
                lib.save();
                s.chosen = static_cast<int>(lib.games().size()) - 1;
                notify(s, Toast::Kind::Success, "Jeu ajouté", display_title(lib.games().back()));
            } else notify(s, Toast::Kind::Error, "Chemin non importé", "Introuvable, déjà présent ou format inconnu.");
            s.manual_path[0] = '\0';
            s.path_target = PathTarget::Rom;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

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
} // namespace

namespace {
SDL_Window* kept_screen = nullptr;
constexpr Uint32 launcher_systems = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS;
}
void keep_screen(SDL_Window* window) {
    if (kept_screen && kept_screen != window) SDL_DestroyWindow(kept_screen);
    kept_screen = window;
}
void close_screen() {
    if (SDL_Window* window = std::exchange(kept_screen, nullptr)) SDL_DestroyWindow(window);
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
        state.settings = Settings(data_file().parent_path() / "reglages.txt");
        state.settings.load();
        std::snprintf(state.pseudo, sizeof(state.pseudo), "%s", state.settings.get("pseudo").c_str());
    } catch (const std::exception& e) {
        notify(state, Toast::Kind::Error, "Dossiers indisponibles", e.what());
    }
    const auto recent = recent_order(lib);
    state.chosen = !recent.empty() ? recent.front() : lib.games().empty() ? -1 : 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    // TV mode reopens in full screen at once; the game's black cover, if
    // any, is closed once the first launcher frame is up.
    const bool tv_start = state.settings.flag("tv_mode", false);
    SDL_Window* window = SDL_CreateWindow("RavenEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1480, 900, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN |
        (tv_start ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
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
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        retro_client(state).pump(false);
        retro_events(state);
        render(state, lib);
        key_capture_dialog(state);
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
        close_screen();
        if (ci_smoke && ++frames >= 12) running = false;
        SDL_Delay(8);
    }
    const bool cover = state.launch && state.launch->fullscreen;
    if (cover) {
        // Left on screen, black, until the game's own window is up.
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
    SDL_DestroyRenderer(renderer);
    if (cover) {
        (void)SDL_SetWindowHitTest(window, nullptr, nullptr);  // it points into `state`
        keep_screen(window);
    } else SDL_DestroyWindow(window);
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
bool remember_hardcore(bool enabled){
    try{Settings settings(data_file().parent_path()/"reglages.txt");settings.load();
        settings.set_flag("ra_hardcore",enabled);settings.save();return true;
    }catch(...){return false;}
}
bool hardcore_preference(){
    try{Settings settings(data_file().parent_path()/"reglages.txt");settings.load();return settings.flag("ra_hardcore",false);}
    catch(...){return false;}
}
KeyboardBindings keyboard_preferences(){
    KeyboardBindings keys;
    try{Settings settings(data_file().parent_path()/"reglages.txt");settings.load();keys.load(settings);}
    catch(...){ }
    return keys;
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
