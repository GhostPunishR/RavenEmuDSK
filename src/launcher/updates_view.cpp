// SPDX-License-Identifier: AGPL-3.0-only
// Update checks: the banner and the settings card.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
// ---------------------------------------------------------------- updates
// One check per run of RavenEmu, shared by every launcher session.
UpdateCheck& updates() {
    static UpdateCheck check;
    return check;
}
namespace {
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
} // namespace
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
} // namespace ravenemudsk_ui::shell
