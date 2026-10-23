// SPDX-License-Identifier: AGPL-3.0-only
// The window frame: sidebar, title bar and window controls.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
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
} // namespace ravenemudsk_ui::shell
