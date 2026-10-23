// SPDX-License-Identifier: AGPL-3.0-only
// The RetroAchievements profile: account, achievements, my games.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
namespace {
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
} // namespace
// Shows a library game's achievements in the profile.
void ra_open_game(State& s, const std::string& path) {
    s.ra_game_path = path;
    s.ra_tab = 0;
    s.ra_filter = 0;
    s.ra_visible_count = 10;
    s.ra_search[0] = '\0';
    s.ra_selected_achievement.clear();
}
namespace {
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
} // namespace
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
    ImGui::Dummy({0.f,space::sm});
    bool sound=s.settings.flag("ra_unlock_sound",true);
    if(toggle("##ra-unlock-sound",&sound)){s.settings.set_flag("ra_unlock_sound",sound);save_settings(s);}
    ImGui::SameLine(0.f,space::md);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Son des succès");
    ImGui::PushFont(s.fonts.caption);
    muted("Un court carillon, créé par RavenEmu, accompagne chaque succès débloqué et chaque jeu maîtrisé. Pris en compte à la prochaine partie.");
    ImGui::PopFont();
}
void ra_login_controls(State& s, ravenemudsk_ra::Client& ra) {
    const auto snap = ra.snapshot();
    ImGui::BeginDisabled(snap.login_pending);
    // The card is drawn in the fields' own colour: a darker, outlined well shows them.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(color::background));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::SetNextItemWidth(std::min(380.f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##ra-user", "Identifiant RetroAchievements", s.ra_user, sizeof(s.ra_user));
    ImGui::SetNextItemWidth(std::min(380.f, ImGui::GetContentRegionAvail().x));
    // Enter in the password field signs in, as the button does.
    const bool entered = ImGui::InputTextWithHint("##ra-password", "Mot de passe", s.ra_password, sizeof(s.ra_password),
                                                  ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (button("Se connecter", Icon::Play, ButtonStyle::Primary) || entered) {
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
namespace {
fs::path data_dir() { return data_file().parent_path(); }
} // namespace
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
namespace {
// Level badge, XP bar and "x / y XP" caption on one line.
void level_line(State& s, ImDrawList* d, ImVec2 p, float bar_w, const Level& level) {
    const std::string label = "Niveau " + std::to_string(level.level);
    pill(d, p, label.c_str(), color::gold, s.fonts.caption);
    const float x = p.x + pill_size(label.c_str(), s.fonts.caption).x + space::md;
    progress_bar(d, {x, p.y + 8.f}, {x + bar_w, p.y + 14.f}, level.progress(), color::accent);
    const std::string xp = std::to_string(level.xp) + " / " + std::to_string(level.next_level) + " XP";
    text_at(d, s.fonts.caption, {x + bar_w + space::md, p.y + 3.f}, color::text_faint, xp);
}
} // namespace
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
} // namespace ravenemudsk_ui::shell
