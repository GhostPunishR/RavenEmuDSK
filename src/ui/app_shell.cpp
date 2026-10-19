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
#include <ravenemudsk/achievements/client.hpp>
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
// What the "chemin" dialog imports: a ROM or folder, a cover, or the avatar.
enum class PathTarget { Rom, Cover, Avatar };
struct AchievementInfo {
    std::string id;
    std::string title;
    int points = 0;
    bool unlocked = false;
    std::int64_t unlocked_at = 0;  // first unlock in the local journal
};
struct State {
    Page page = Page::Home;
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
    Library* library = nullptr;
    bool tv = false;          // full screen TV / gamepad mode
    int tv_category = 0;
    int tv_index = 0;
    float tv_scroll = 0.f;    // animated towards tv_index
    bool focus_search = false;
    std::optional<LaunchRequest> launch;
    Fonts fonts;
    Folders folders;
    SDL_Texture* logo = nullptr;
    SDL_Texture* ra_logo = nullptr;
    std::unique_ptr<BadgeCache> ra_images;
    int ra_filter = 0;
    int ra_visible_count = 10;
    std::string ra_selected_achievement;
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    std::unordered_map<std::string, SDL_Texture*> images;
    std::unordered_map<std::string, std::optional<AchievementInfo>> achievements;
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
std::optional<AchievementInfo> read_achievement(const Folders&, const Game&) {
    // No local unlock rules: official RetroAchievements only.
    return std::nullopt;
}
const std::optional<AchievementInfo>& achievement_for(State& s, const Game& game) {
    const auto key = utf8(game.path);
    auto it = s.achievements.find(key);
    if (it == s.achievements.end()) it = s.achievements.emplace(key, read_achievement(s.folders, game)).first;
    return it->second;
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
void game_card(State& s, const Game& game, int index, float w) {
    ImGui::PushID(index);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = card_height(w);
    ImGui::InvisibleButton("##card", {w, h});
    const bool hovered = ImGui::IsItemHovered();
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
    if (hovered) {
        const ImVec2 c{cb.x - 28.f, cb.y - 28.f};
        const auto mouse = ImGui::GetMousePos();
        const bool on_play = (mouse.x - c.x) * (mouse.x - c.x) + (mouse.y - c.y) * (mouse.y - c.y) < 22.f * 22.f;
        d->AddCircleFilled({c.x, c.y + 3}, 22.f, color::shadow, 32);
        d->AddCircleFilled(c, 22.f, on_play ? color::accent_hover : color::accent, 32);
        draw_icon(d, Icon::Play, {c.x + 2.f, c.y}, 16.f, IM_COL32_WHITE);
        if (on_play && ImGui::IsItemClicked()) play = true;
        if (on_play) ImGui::SetTooltip("Jouer");
    }
    const auto title = display_title(game);
    text_at(d, s.fonts.strong, {p.x + 2.f, cb.y + 10.f}, color::text, ellipsize(s.fonts.strong, title, w - 4.f));
    const char* tag = system_short(game.system);
    pill(d, {p.x, cb.y + 35.f}, tag, system_color(game.system), s.fonts.caption);
    const float tx = p.x + pill_size(tag, s.fonts.caption).x + space::sm;
    text_at(d, s.fonts.caption, {tx, cb.y + 37.f}, color::text_faint,
            ellipsize(s.fonts.caption, game.last_played ? ago(game.last_played) : "Nouveau", p.x + w - tx));
    focus_ring(d, {p.x - 8, p.y - 8}, {p.x + w + 8, p.y + h + 2}, radius::lg + 4.f);
    if (hovered && !ImGui::IsItemClicked() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
        ellipsize(s.fonts.strong, title, w - 4.f) != title)
        ImGui::SetTooltip("%s", title.c_str());
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
    struct Item { Page page; Icon icon; const char* label; };
    const Item items[] = {{Page::Home, Icon::Home, "Accueil"}, {Page::Library, Icon::Library, "Jeux"},
                          {Page::Recent, Icon::Recent, "Récents"},
                          {Page::Profile, Icon::Home, "Profil"},
                          {Page::Settings, Icon::Settings, "Réglages"}};
    for (int i = 0; i < 5; ++i) {
        const float y = i < 3 ? 96.f + i * 72.f : height - 84.f - (4 - i) * 72.f;
        const ImVec2 a{12.f, y}, b{sidebar_w - 12.f, y + 64.f};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##nav", {b.x - a.x, b.y - a.y})) s.page = items[i].page;
        const bool hovered = ImGui::IsItemHovered();
        const bool active = s.page == items[i].page;
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
    text_at(d, s.fonts.display, {sidebar_w + content_pad, 26.f}, color::text,
            titles[static_cast<int>(s.page)]);
    // Search, right-aligned before the window controls.
    const float sw = std::clamp(width * .26f, 240.f, 380.f);
    const ImVec2 a{width - sw - 46.f * 3.f - space::xl, 24.f}, b{a.x + sw, a.y + 40.f};
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
    ImGui::SetCursorScreenPos({a.x - 52.f, a.y + 2.f});
    if (icon_button("##tv", Icon::Gamepad, "Mode TV / manette (Start)")) set_tv(s, true);
    s.interactive.push_back({a.x - 52.f, a.y, a.x - 16.f, b.y});
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
    if (const auto& info = achievement_for(s, game)) {
        const float y = p.y + 132.f;
        draw_icon(d, Icon::Trophy, {x + 8.f, y + 9.f}, 16.f, info->unlocked ? color::gold : color::text_faint);
        text_at(d, s.fonts.body, {x + 24.f, y}, info->unlocked ? color::gold : color::text_muted,
                info->unlocked ? "1 succès sur 1 débloqué" : "1 succès à débloquer");
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
void achievements_view(State& s, Library&) {
    auto& ra = ravenemudsk_ra::client();
    const auto snap = ra.snapshot();
    begin_card("##ra-account");
    // Official RA artwork stays visible as a small brand icon.
    const ImVec2 title_pos=ImGui::GetCursorScreenPos();
    draw_ra_brand(s,ImGui::GetWindowDrawList(),{title_pos.x+11.f,title_pos.y+12.f},24.f);
    ImGui::SetCursorScreenPos({title_pos.x+31.f,title_pos.y});
    ImGui::PushFont(s.fonts.title);
    ImGui::TextUnformatted("RetroAchievements");
    ImGui::PopFont();
    ImGui::Dummy({0.f, 4.f});
    muted(snap.status.c_str());
    ImGui::Dummy({0.f, 8.f});
    if (!snap.logged_in) {
        muted("Connectez votre compte pour obtenir les succès officiels pendant vos parties Game Boy, Game Boy Color et Game Boy Advance.");
        ra_login_controls(s, ra);
    } else {
        // Exactly the account information from RavenEmu: avatar, name, score,
        // active game summary, then session controls. The logo is only an icon.
        const ImVec2 avatar = ImGui::GetCursorScreenPos();
        SDL_Texture* image = s.ra_images ? s.ra_images->image(snap.avatar_url) : nullptr;
        draw_avatar(ImGui::GetWindowDrawList(), {avatar.x + 29.f,avatar.y + 29.f},
                    29.f, snap.display_name, 0, image, s.fonts);
        const float caption_x = avatar.x + 73.f;
        text_at(ImGui::GetWindowDrawList(), s.fonts.title, {caption_x, avatar.y + 5.f},
                color::text, snap.display_name.empty() ? snap.username : snap.display_name);
        text_at(ImGui::GetWindowDrawList(), s.fonts.caption, {caption_x, avatar.y + 35.f},
                color::text_muted, "@" + snap.username);
        ImGui::Dummy({0.f, 66.f});
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::accent),
                           "Softcore : %u points  |  Hardcore : %u points",
                           snap.softcore_points, snap.hardcore_points);
        ImGui::Dummy({0.f, 4.f});
        if (!snap.game_title.empty()) {
            ImGui::Separator();
            ImGui::Dummy({0.f, 5.f});
            ImGui::PushFont(s.fonts.strong);
            ImGui::TextUnformatted(snap.game_title.c_str());
            ImGui::PopFont();
            ImGui::TextDisabled("%d/%d succès  |  %u/%u points",
                                snap.unlocked_count, snap.total_count,
                                snap.unlocked_points, snap.total_points);
        } else {
            muted("Aucun jeu reconnu pour le moment.");
        }
    }
    end_card();

    if (snap.logged_in) {
        if (button("Réessayer", Icon::Reset, ButtonStyle::Secondary, {152.f, 40.f}))
            ra.retry();
        ImGui::SameLine(0.f, 12.f);
        if (button("Déconnexion", Icon::Exit, ButtonStyle::Secondary, {160.f, 40.f})) {
            ra.disconnect();
            s.ra_selected_achievement.clear();
            s.ra_filter = 0;
            s.ra_visible_count = 10;
        }
        ImGui::Dummy({0.f, 10.f});
    }

    if (!snap.game_title.empty() && !snap.achievements.empty()) {
        ImGui::PushFont(s.fonts.title);
        ImGui::TextUnformatted("Succès du jeu");
        ImGui::PopFont();
        const char* labels[] = {"Tous", "À obtenir", "Obtenus"};
        for (int i=0;i<3;++i) {
            if (i) ImGui::SameLine(0.f, 6.f);
            ImGui::PushID(i);
            if (button(labels[i], Icon::Check,
                       s.ra_filter==i ? ButtonStyle::Primary : ButtonStyle::Ghost,
                       {i==1 ? 128.f : 104.f, 36.f}, false)) {
                s.ra_filter=i;
                s.ra_visible_count=10;
            }
            ImGui::PopID();
        }
        ImGui::Dummy({0.f, 6.f});
        int shown=0;
        int total=0;
        for (const auto& a : snap.achievements) {
            if ((s.ra_filter==1 && a.unlocked) || (s.ra_filter==2 && !a.unlocked))
                continue;
            ++total;
            if (shown++ >= s.ra_visible_count) continue;
            const ImVec2 pos=ImGui::GetCursorScreenPos();
            const float width=ImGui::GetContentRegionAvail().x;
            ImGui::PushID(a.id.c_str());
            const bool clicked=ImGui::InvisibleButton("##ra-row", {width,75.f});
            const bool hovered=ImGui::IsItemHovered();
            auto* d=ImGui::GetWindowDrawList();
            d->AddRectFilled(pos,{pos.x+width,pos.y+75.f},
                             hovered ? color::surface_hover : color::surface,radius::md);
            const auto badge = s.ra_images ? s.ra_images->image(a.badge_url) : nullptr;
            if (badge)
                d->AddImageRounded(reinterpret_cast<ImTextureID>(badge),
                                   {pos.x+12.f,pos.y+15.f},{pos.x+54.f,pos.y+57.f},
                                   {0,0},{1,1},IM_COL32_WHITE,6.f);
            else {
                d->AddRectFilled({pos.x+12.f,pos.y+15.f},
                                 {pos.x+54.f,pos.y+57.f},color::accent_soft,6.f);
                draw_ra_brand(s,d,{pos.x+33.f,pos.y+36.f},30.f);
            }
            const float right=std::max(110.f,width-230.f);
            text_at(d,s.fonts.strong,{pos.x+68.f,pos.y+14.f},color::text,
                    ellipsize(s.fonts.strong,a.title,right));
            const std::string status=a.unlocked ? "Débloqué" :
                a.state==3 ? "Non pris en charge" :
                !a.progress.empty() ? a.progress : "Verrouillé";
            text_at(d,s.fonts.caption,{pos.x+68.f,pos.y+44.f},color::text_muted,
                    ellipsize(s.fonts.caption,status,right));
            const std::string points=std::to_string(a.points)+" pts";
            text_at(d,s.fonts.caption,
                    {pos.x+width-18.f-text_width(s.fonts.caption,points.c_str()),pos.y+23.f},
                    color::accent,points);
            ImGui::PopID();
            if (clicked) {
                s.ra_selected_achievement=a.id;
                ImGui::OpenPopup("##ra-detail");
            }
            ImGui::Dummy({0.f,3.f});
        }
        if (total>s.ra_visible_count) {
            const auto label="Afficher plus ("+std::to_string(total-s.ra_visible_count)+" restants)";
            if (button(label.c_str(),Icon::Plus,ButtonStyle::Ghost))
                s.ra_visible_count+=15;
        }
    } else {
        begin_card("##ra-no-game");
        card_title(s,Icon::Gamepad,"Succès du jeu");
        muted(snap.logged_in ? "Aucun jeu reconnu pour le moment." :
              "Connectez-vous pour retrouver les succès officiels.");
        end_card();
    }
    // One dialog for all achievements, like RavenEmu's detail sheet.
    if (ImGui::BeginPopupModal("##ra-detail",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto it=std::find_if(snap.achievements.begin(),snap.achievements.end(),
            [&](const ravenemudsk_ra::Achievement& a){return a.id==s.ra_selected_achievement;});
        if (it==snap.achievements.end()) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::PushFont(s.fonts.title);
            ImGui::TextUnformatted(it->title.c_str());
            ImGui::PopFont();
            SDL_Texture* badge=s.ra_images ? s.ra_images->image(it->badge_url) : nullptr;
            if (badge)ImGui::Image(reinterpret_cast<ImTextureID>(badge),{88.f,88.f});
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+430.f);
            ImGui::TextWrapped("%s",it->description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Text("%d points  |  %s",it->points,it->unlocked ? "Débloqué" : "À obtenir");
            if (button("Fermer",Icon::Close,ButtonStyle::Secondary))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            const bool digits=!it->id.empty() && std::all_of(it->id.begin(),it->id.end(),
                [](unsigned char c){return c>='0' && c<='9';});
            if (digits && button("Voir sur le site",Icon::Info,ButtonStyle::Secondary)) {
                const auto url="https://retroachievements.org/achievement/"+it->id;
                if (SDL_OpenURL(url.c_str())!=0)
                    notify(s,Toast::Kind::Error,"Ouverture du navigateur impossible",SDL_GetError());
            }
        }
        ImGui::EndPopup();
    }
}
void ra_login_controls(State& s, ravenemudsk_ra::Client& ra) {
    ImGui::SetNextItemWidth(std::min(380.f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##ra-user", "Identifiant RetroAchievements", s.ra_user, sizeof(s.ra_user));
    ImGui::SetNextItemWidth(std::min(380.f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##ra-password", "Mot de passe", s.ra_password, sizeof(s.ra_password),
                             ImGuiInputTextFlags_Password);
    if (button("Se connecter", Icon::Play, ButtonStyle::Primary)) {
        ra.login(s.ra_user, s.ra_password);
        std::fill(std::begin(s.ra_password), std::end(s.ra_password), '\0');
    }
    muted("Connexion HTTPS officielle. Aucun mot de passe n'est conservé sur le PC.");
}
// ---------------------------------------------------------------- profile
struct Stats {
    std::uint64_t points = 0, possible = 0, seconds = 0;
    int unlocked = 0, achievements = 0, played = 0;
};
Stats stats_for(State& s, const Library& lib) {
    Stats st;
    for (const auto& game : lib.games()) {
        st.seconds += game.seconds_played;
        if (game.last_played) ++st.played;
        if (const auto& info = achievement_for(s, game)) {
            ++st.achievements;
            st.possible += static_cast<std::uint64_t>(info->points);
            if (info->unlocked) { ++st.unlocked; st.points += static_cast<std::uint64_t>(info->points); }
        }
    }
    return st;
}
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
    const auto snap=ravenemudsk_ra::client().snapshot();
    if (snap.logged_in) {
        SDL_Texture* avatar=s.ra_images ? s.ra_images->image(snap.avatar_url) : nullptr;
        const std::string name=snap.display_name.empty() ? snap.username : snap.display_name;
        draw_avatar(d,center,radius,name,0,avatar,s.fonts);
    } else {
        draw_ra_brand(s,d,center,radius*2.f);
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
    const auto snap=ravenemudsk_ra::client().snapshot();
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
    if (s.window) SDL_SetWindowFullscreen(s.window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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
bool gamepad_connected() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) return true;
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
void tv_view(State& s, Library& lib) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    auto* d = ImGui::GetWindowDrawList();
    if (pressed({ImGuiKey_GamepadStart, ImGuiKey_Escape}, false)) {
        set_tv(s, false);
        return;
    }
    const int previous = s.tv_category;
    if (pressed({ImGuiKey_GamepadL1, ImGuiKey_Q, ImGuiKey_PageUp}, false))
        s.tv_category = (s.tv_category + tv_category_count - 1) % tv_category_count;
    if (pressed({ImGuiKey_GamepadR1, ImGuiKey_E, ImGuiKey_PageDown}, false))
        s.tv_category = (s.tv_category + 1) % tv_category_count;
    if (s.tv_category != previous) { s.tv_index = 0; s.tv_scroll = 0.f; }
    const auto ids = tv_games(lib, s.tv_category);
    const int count = static_cast<int>(ids.size());
    if (pressed({ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft}, true)) --s.tv_index;
    if (pressed({ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight}, true)) ++s.tv_index;
    if (io.MouseWheel > 0.f) --s.tv_index;
    if (io.MouseWheel < 0.f) ++s.tv_index;
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
    const std::string name = player_name(s);
    const float nw = text_width(s.fonts.tv_body, name.c_str());
    draw_player_avatar(s, d, {rx - 22.f, 58.f}, 22.f);
    text_at(d, s.fonts.tv_body, {rx - 56.f - nw, 45.f}, color::text_muted, name);

    // Category tabs between LB and RB.
    const bool pad_in = gamepad_connected();
    // Tab padding shrinks on narrow screens so that LB / RB stay visible.
    float labels_w = 0.f;
    for (const char* label : tv_categories) labels_w += text_width(s.fonts.tv_body, label);
    const float tab_pad = std::clamp((screen.x - 2.f * pad - 160.f - labels_w) / (2.f * tv_category_count + 2.f),
                                     8.f, 20.f);
    const float tabs_w = labels_w + tv_category_count * (tab_pad * 2.f + 8.f) - 8.f;
    float tx = (screen.x - tabs_w) * .5f;
    const float ty = 124.f;
    key_glyph(d, {tx - 56.f, ty + 6.f}, pad_in ? "LB" : "Q", s.fonts.caption);
    for (int i = 0; i < tv_category_count; ++i) {
        const float w = text_width(s.fonts.tv_body, tv_categories[i]) + tab_pad * 2.f;
        const bool on = i == s.tv_category;
        if (on) d->AddRectFilled({tx, ty}, {tx + w, ty + 44.f}, color::accent, 22.f);
        text_at(d, s.fonts.tv_body, {tx + tab_pad, ty + 9.f}, on ? IM_COL32_WHITE : color::text_muted,
                tv_categories[i]);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect({tx, ty}, {tx + w, ty + 44.f})) {
            s.tv_category = i; s.tv_index = 0; s.tv_scroll = 0.f;
        }
        tx += w + 8.f;
    }
    key_glyph(d, {tx + 16.f, ty + 6.f}, pad_in ? "RB" : "E", s.fonts.caption);

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
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(a, b)) {
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
        if (const auto& info = achievement_for(s, *game))
            meta += info->unlocked ? "  \xC2\xB7  1 / 1 succès" : "  \xC2\xB7  0 / 1 succès";
        text_at(d, s.fonts.tv_body, {(screen.x - text_width(s.fonts.tv_body, meta.c_str())) * .5f, y + 66.f},
                color::text_muted, meta);
    }
    // Hints, adapted to the connected controller.
    struct Hint { const char* keys[2]; const char* action; };
    const Hint hints[] = {
        {{pad_in ? "A" : "Entrée", nullptr}, "Jouer"},
        {{pad_in ? "LB" : "Q", pad_in ? "RB" : "E"}, "Catégorie"},
        {{pad_in ? "Start" : "Échap", nullptr}, "Mode bureau"}};
    float hint_w = 0.f;
    for (const auto& h : hints) {
        for (const char* k : h.keys) if (k) hint_w += text_width(s.fonts.caption, k) + 28.f;
        hint_w += text_width(s.fonts.tv_body, h.action) + 48.f;
    }
    float hx = (screen.x - hint_w) * .5f;
    const float hy = screen.y - 68.f;
    for (const auto& h : hints) {
        for (const char* k : h.keys) if (k) hx += key_glyph(d, {hx, hy + 4.f}, k, s.fonts.caption) + 8.f;
        text_at(d, s.fonts.tv_body, {hx + 4.f, hy + 2.f}, color::text_muted, h.action);
        hx += text_width(s.fonts.tv_body, h.action) + 44.f;
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
void settings_view(State& s, Library& lib) {
    begin_card("##folders");
    card_title(s, Icon::Folder, "Dossiers");
    muted("RavenEmu range automatiquement vos fichiers dans deux dossiers à côté du programme. "
          "Copiez-les pour sauvegarder ou transférer votre progression.");
    ImGui::Dummy({0.f, space::xs});
    folder_row(s, "Sauvegardes", "sauvegardes de jeu, états et dernières captures",
               s.folders.saves);
    folder_row(s, "Jaquettes", "nommez l'image comme la ROM, par exemple MonJeu.png", s.folders.covers);
    end_card();

    begin_card("##library");
    card_title(s, Icon::Library, "Bibliothèque");
    muted("RavenEmu mémorise seulement l'emplacement de vos jeux : les ROMs restent dans leur dossier, "
          "sur ce PC.");
    ImGui::Dummy({0.f, space::xs});
    ImGui::PushFont(s.fonts.caption);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_faint), "Fichier de bibliothèque : %s",
                       utf8(data_file()).c_str());
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    if (button("Ajouter un jeu", Icon::Plus, ButtonStyle::Primary)) add_game(s, lib);
    ImGui::SameLine(0.f, space::sm);
    if (button("Importer un dossier", Icon::Folder)) pick_and_import_folder(s, lib);
    ImGui::SameLine(0.f, space::sm);
    if (button("Saisir un chemin", Icon::Keyboard, ButtonStyle::Ghost)) s.open_manual = true;
    end_card();

    updates_card(s);

    begin_card("##tv");
    card_title(s, Icon::Gamepad, "Mode TV / manette");
    muted("Plein écran, grandes jaquettes et navigation à la manette depuis le canapé. "
          "Start ou Échap revient au mode bureau ; RavenEmu s'en souvient au prochain lancement.");
    ImGui::Dummy({0.f, space::xs});
    if (button("Passer en mode TV", Icon::Fullscreen, ButtonStyle::Primary)) set_tv(s, true);
    end_card();

    begin_card("##controls");
    card_title(s, Icon::Gamepad, "Commandes en jeu");
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
        const char* rows[][3] = {
            {"Croix directionnelle", "Flèches", "Croix / stick gauche"},
            {"Boutons A / B", "Z / X", "A / B"},
            {"Start / Select", "Entrée / Retour arrière", "Start / Back"},
            {"Gâchettes L / R (GBA)", "A / S", "LB / RB"},
            {"Menu de pause", "Échap", "Guide ou Back + Start"},
            {"Pause rapide", "P", "-"},
            {"Sauvegarder / charger l'état", "F5 / F9", "Menu de pause"},
            {"Réinitialiser", "F4", "Menu de pause"},
            {"Plein écran", "F11", "Menu de pause"}};
        for (const auto& r : rows) {
            ImGui::TableNextRow(0, 34.f);
            for (int i = 0; i < 3; ++i) {
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (i) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_muted), "%s", r[i]);
                else ImGui::TextUnformatted(r[i]);
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
    end_card();

    begin_card("##about");
    card_title(s, Icon::Info, "À propos");
    ImGui::TextUnformatted("RavenEmu - émulateur Game Boy, Game Boy Color et Game Boy Advance");
    muted("Préversion " RAVENEMUDSK_VERSION ". Licence AGPL-3.0-only. Cœurs d'émulation RavenCore (AGPL-3.0-only), "
          "succès RetroAchievements via rcheevos (MIT), interface Dear ImGui (MIT) "
          "et SDL2 (zlib). Aucun compte en ligne n'est nécessaire et aucune donnée n'est envoyée.");
    end_card();
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
    // Achievements of this game.
    p = ImGui::GetCursorScreenPos();
    const auto& info = achievement_for(s, game);
    text_at(d, s.fonts.strong, p, color::text, "Succès");
    const std::string count = info ? (info->unlocked ? "1 / 1" : "0 / 1") : "0";
    text_at(d, s.fonts.caption, {p.x + w - text_width(s.fonts.caption, count.c_str()), p.y + 2.f},
            color::text_muted, count);
    ImGui::Dummy({w, 24.f});
    p = ImGui::GetCursorScreenPos();
    if (info) {
        progress_bar(d, p, {p.x + w, p.y + 6.f}, info->unlocked ? 1.f : 0.f, color::gold);
        ImGui::Dummy({w, 14.f});
        p = ImGui::GetCursorScreenPos();
        d->AddRectFilled(p, {p.x + w, p.y + 64.f}, color::surface, radius::md);
        const ImVec2 c{p.x + 30.f, p.y + 32.f};
        d->AddCircleFilled(c, 18.f, info->unlocked ? color::gold_soft : color::elevated, 32);
        draw_icon(d, info->unlocked ? Icon::Trophy : Icon::Lock, c, 16.f,
                  info->unlocked ? color::gold : color::text_faint);
        text_at(d, s.fonts.strong, {p.x + 58.f, p.y + 12.f}, color::text,
                ellipsize(s.fonts.strong, info->title, w - 70.f));
        text_at(d, s.fonts.caption, {p.x + 58.f, p.y + 36.f}, info->unlocked ? color::gold : color::text_muted,
                std::to_string(info->points) + " pts  \xC2\xB7  " + (info->unlocked ? "Débloqué" : "À débloquer"));
        ImGui::Dummy({w, 64.f});
    } else {
        ImGui::PushFont(s.fonts.caption);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_muted),
                           "Les succès RetroAchievements sont disponibles en lançant une ROM reconnue.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
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

std::optional<LaunchRequest> run_launcher(const std::string& error) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
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
    SDL_Window* window = SDL_CreateWindow("RavenEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1480, 900, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!window) {
        const std::string reason = SDL_GetError();
        SDL_Quit();
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
        SDL_Quit();
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
    if (ci_smoke) state.page = smoke_page();
    state.library = &lib;
    static bool update_started = false;  // once per run, not per return from a game
    if (!ci_smoke && !update_started && state.settings.flag("update_check", true)) {
        update_started = true;
        updates().start(RAVENEMUDSK_VERSION);
    }
    const char* smoke_name = SDL_getenv("RAVENEMUDSK_UI_SMOKE_PAGE");
    if (state.settings.flag("tv_mode", false) || (ci_smoke && smoke_name && std::string(smoke_name) == "tv"))
        set_tv(state, true, false);
    int frames = 0;
    bool smoke_rendered = false;
    std::string smoke_error;
    bool running = true;
    while (running && !state.done) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
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
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ravenemudsk_ra::client().pump(false);
        render(state, lib);
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
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    state.ra_images.reset();
    if (state.ra_logo) SDL_DestroyTexture(state.ra_logo);
    if (state.logo) SDL_DestroyTexture(state.logo);
    for (auto& [key, texture] : state.images) if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (ci_smoke && !smoke_rendered)
        throw std::runtime_error(smoke_error.empty() ? "Test de l'interface interrompu avant le rendu" : smoke_error);
    return state.launch;
}
Folders app_folders() {
    std::vector<fs::path> roots;
    if (char* base = SDL_GetBasePath()) {
        roots.push_back(from_utf8(base));
        SDL_free(base);
    }
    if (char* pref = SDL_GetPrefPath("RavenEmu", "RavenEmuDSK")) {
        roots.push_back(from_utf8(pref));
        SDL_free(pref);
    }
    return prepare_folders(roots);
}
void record_session(const LaunchRequest& request, unsigned long long seconds) {
    if (SDL_Init(0) != 0) return;
    try {
        Library lib(data_file());
        lib.load();
        lib.mark_played(from_utf8(request.rom), seconds);
        lib.save();
    } catch (...) {}
    SDL_Quit();
}
} // namespace ravenemudsk_ui
