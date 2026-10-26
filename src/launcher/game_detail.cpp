// SPDX-License-Identifier: AGPL-3.0-only
// The panel of the selected game, and the path dialog.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
namespace {
// "Jouer à deux": player 2's game, among those the link cable joins to this
// one; both play side by side in one window.
void pair_dialog(State& s, const Library& lib, const Game& game) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f, .5f});
    ImGui::SetNextWindowSize({std::min(560.f, ImGui::GetIO().DisplaySize.x - 48.f), 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {space::xl, space::xl});
    if (ImGui::BeginPopupModal("##pair", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(s.fonts.title);
        ImGui::TextUnformatted("Jouer à deux");
        ImGui::PopFont();
        const std::string intro = "Choisissez le jeu du joueur 2. Il se joue à droite de " + display_title(game) +
                                  ", relié par le câble link, dans la même fenêtre.";
        ImGui::TextWrapped("%s", intro.c_str());
        ImGui::Dummy({0.f, space::xs});
        std::vector<const Game*> partners;
        for (const auto& other : lib.games())
            if (other.path != game.path && linkable(game.system, other.system)) partners.push_back(&other);
        if (partners.empty()) {
            ImGui::PushFont(s.fonts.caption);
            muted(game.system == "gba" ? "Aucun autre jeu Game Boy Advance dans la bibliothèque : ajoutez-en un."
                                       : "Aucun autre jeu Game Boy ou Game Boy Color dans la bibliothèque : ajoutez-en un.");
            ImGui::PopFont();
        } else {
            const float row = ImGui::GetFrameHeightWithSpacing();
            ImGui::BeginChild("##partners", {0.f, std::min(320.f, row * static_cast<float>(partners.size()) + space::sm)});
            for (const Game* other : partners) {
                ImGui::PushID(utf8(other->path).c_str());
                const std::string label = display_title(*other) + "  \xC2\xB7  " + system_name(other->system);
                if (ImGui::Selectable(label.c_str())) {
                    launch_pair(s, game, *other);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::Dummy({0.f, space::xs});
        if (button("Annuler", Icon::Close, ButtonStyle::Ghost)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
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
} // namespace
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
    if (button("Jouer à deux", Icon::Link, ButtonStyle::Secondary, {w, 38.f})) ImGui::OpenPopup("##pair");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Deux jeux reliés par le câble link, côte à côte : un échange de Pokémon, un duel...");
    pair_dialog(s, lib, game);
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
        ImGui::SetTooltip("PNG, JPEG ou BMP, copiée dans le dossier jaquettes.\nAstuce : glissez une image sur la fenêtre.");
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
} // namespace ravenemudsk_ui::shell
