// SPDX-License-Identifier: AGPL-3.0-only
// Home, library and recent pages, and their game cards.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
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
namespace {
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
} // namespace
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
} // namespace ravenemudsk_ui::shell
