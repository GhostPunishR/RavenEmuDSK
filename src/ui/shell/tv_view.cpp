// SPDX-License-Identifier: AGPL-3.0-only
// TV mode: the full-screen launcher read from a couch.
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
} // namespace
// TV mode lasts for this run of RavenEmu: back from a game it reopens, but
// the application itself always starts in desktop mode.
bool& tv_this_run() { static bool on = false; return on; }
// `remember` keeps the mode for the next launcher of this run.
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
    if (remember) tv_this_run() = on;
}
bool pressed(std::initializer_list<ImGuiKey> keys, bool repeat) {
    for (const ImGuiKey key : keys)
        if (ImGui::IsKeyPressed(key, repeat)) return true;
    return false;
}
namespace {
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
} // namespace
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
} // namespace ravenemudsk_ui::shell
