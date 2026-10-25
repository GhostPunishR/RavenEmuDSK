// SPDX-License-Identifier: AGPL-3.0-only
// Shared helpers: dates, file pickers, images, library order and actions, drawing pieces.
#include "shell.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace ravenemudsk_ui::shell {
namespace {
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
} // namespace
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
namespace {
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
} // namespace
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
namespace {
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
} // namespace
fs::path data_root() {
    char* ptr = SDL_GetPrefPath("", "RavenEmu");
    if (!ptr) return {};
    const auto root = from_utf8(ptr).parent_path();
    SDL_free(ptr);
    static std::once_flag moved;
    std::call_once(moved, [&] { move_legacy_data(root); });
    return root;
}
fs::path data_file() {
    const auto root = data_root();
    return root.empty() ? fs::path("ravenemudsk-library.txt") : root / "bibliotheque.txt";
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
namespace {
bool passes(const Game& g, const State& s) {
    static const char* systems[] = {"", "gb", "gbc", "gba"};
    if (s.system_filter > 0 && g.system != systems[s.system_filter]) return false;
    return !s.search[0] || lower(display_title(g)).find(lower(s.search)) != std::string::npos;
}
} // namespace
// Sort 0 = title, 1 = last played, 2 = play time. `filter` applies search and system.
std::vector<int> library_order(const Library& lib, int sort, const State* filter) {
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
void launch_pair(State& s, const Game& game, const Game& partner) {
    s.launch = LaunchRequest{game.system, utf8(game.path), s.tv, partner.system, utf8(partner.path)};
    s.done = true;
}
void notify(State& s, Toast::Kind kind, std::string title, std::string body) {
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
bool section_header(State& s, const char* title, const char* action) {
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
void game_title_tooltip(const std::string& title, bool play_action) {
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
void begin_card(const char* id, float width) {
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
void end_card(bool spacing) {
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
} // namespace ravenemudsk_ui::shell
