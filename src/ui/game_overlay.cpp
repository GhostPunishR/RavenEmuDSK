// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_overlay.hpp>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <cfloat>
namespace ravenemudsk_ui {
namespace {
float text_width(ImFont* font, const char* text) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text).x;
}
// Full-width menu row: icon, label and keyboard shortcut.
bool menu_item(const Fonts& fonts, const char* label, Icon icon, const char* shortcut,
               bool enabled = true, bool danger = false) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton(label, {w, 46.f});
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 max{p.x + w, p.y + 46.f};
    if (hovered) d->AddRectFilled(p, max, danger ? with_alpha(color::danger, .16f) : color::surface_hover,
                                  radius::md);
    const ImU32 fg = !enabled ? color::text_faint : danger ? color::danger : color::text;
    draw_icon(d, icon, {p.x + 24.f, p.y + 23.f}, 17.f, danger ? color::danger :
              enabled ? color::accent_hover : color::text_faint);
    d->AddText(fonts.body, fonts.body->FontSize, {p.x + 48.f, p.y + 13.f}, fg, label);
    if (shortcut) {
        const float sw = text_width(fonts.caption, shortcut);
        const ImVec2 k{max.x - sw - 26.f, p.y + 12.f};
        d->AddRectFilled(k, {k.x + sw + 14.f, k.y + 22.f}, color::surface, radius::sm);
        d->AddText(fonts.caption, fonts.caption->FontSize, {k.x + 7.f, k.y + 3.f}, color::text_muted, shortcut);
    }
    focus_ring(d, p, max, radius::md);
    return pressed;
}
void demo_panel(const Fonts& fonts, const OverlayInfo& info) {
    auto* d = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float w = std::min(480.f, screen.x - 32.f), h = 176.f;
    const ImVec2 a{(screen.x - w) * .5f, std::max(16.f, (screen.y - h) * .5f)}, b{a.x + w, a.y + h};
    const bool done = info.achievement_unlocked;
    d->AddRectFilled({a.x, a.y + 8}, {b.x, b.y + 8}, color::shadow, radius::lg);
    d->AddRectFilled(a, b, with_alpha(color::elevated, .97f), radius::lg);
    d->AddRect(a, b, done ? with_alpha(color::gold, .8f) : color::border_strong, radius::lg, 0, done ? 2.f : 1.f);
    const ImVec2 c{a.x + 48.f, a.y + 56.f};
    d->AddCircleFilled(c, 26.f, done ? color::gold_soft : color::accent_soft, 40);
    draw_icon(d, done ? Icon::Trophy : Icon::Gamepad, c, 24.f, done ? color::gold : color::accent_hover);
    const float x = a.x + 92.f;
    d->AddText(fonts.caption, fonts.caption->FontSize, {x, a.y + 28.f}, done ? color::gold : color::accent_hover,
               done ? "OBJECTIF ATTEINT" : "DÉMO RAVENEMUDSK");
    d->AddText(fonts.title, fonts.title->FontSize, {x, a.y + 48.f}, color::text,
               done ? "Succès débloqué !" : "Débloquez votre premier succès");
    const std::string line = done ? info.achievement_title + "  \xC2\xB7  " +
                                        std::to_string(info.achievement_points) + " points"
                                  : "Appuyez sur Z (bouton A) ou cliquez sur l'écran.";
    d->AddText(fonts.body, fonts.body->FontSize, {x, a.y + 84.f}, color::text_muted,
               ellipsize(fonts.body, line, b.x - x - 24.f).c_str());
    const char* hint = done ? "Échap ouvre le menu pour revenir à la bibliothèque."
                            : "Le moteur de succès lit la mémoire de la console après chaque image.";
    d->AddText(fonts.caption, fonts.caption->FontSize, {a.x + 24.f, b.y - 36.f}, color::text_faint,
               ellipsize(fonts.caption, hint, w - 48.f).c_str());
}
} // namespace

GameOverlay::GameOverlay(SDL_Window* window, SDL_Renderer* renderer) : renderer_(renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    fonts_ = load_fonts();
    apply_theme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
}
GameOverlay::~GameOverlay() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
void GameOverlay::process_event(const SDL_Event& event) { ImGui_ImplSDL2_ProcessEvent(&event); }
bool GameOverlay::wants_mouse() const { return menu_open_ || ImGui::GetIO().WantCaptureMouse; }
void GameOverlay::open_menu() {
    menu_open_ = true;
    focus_first_ = true;
}
bool GameOverlay::has_toast(Toast::Kind kind) const {
    return std::any_of(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.kind == kind; });
}

OverlayAction GameOverlay::draw(const OverlayInfo& info) {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    OverlayAction action = OverlayAction::None;
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    auto* bg = ImGui::GetBackgroundDrawList();
    const auto time = static_cast<float>(ImGui::GetTime());

    if (info.demo && !menu_open_) demo_panel(fonts_, info);
    if (info.paused && !menu_open_) {
        const char* label = "Pause  \xC2\xB7  P pour reprendre";
        const ImVec2 size{text_width(fonts_.body, label) + 56.f, 40.f};
        bg->AddRectFilled({16, 16}, {16 + size.x, 16 + size.y}, with_alpha(color::elevated, .95f), 20.f);
        draw_icon(bg, Icon::Pause, {38, 36}, 14.f, color::accent_hover);
        bg->AddText(fonts_.body, fonts_.body->FontSize, {54, 27}, color::text, label);
    }
    if (time < 6.f && !menu_open_) {
        // Short reminder of the essential keys when a game starts.
        const float alpha = std::clamp(6.f - time, 0.f, 1.f) * std::clamp(time * 2.f, 0.f, 1.f);
        const char* hint = "Échap : menu   \xC2\xB7   F11 : plein écran   \xC2\xB7   F5 / F9 : états";
        const float w = text_width(fonts_.caption, hint) + 32.f;
        const ImVec2 a{(screen.x - w) * .5f, screen.y - 52.f};
        bg->AddRectFilled(a, {a.x + w, a.y + 32.f}, with_alpha(color::elevated, .92f * alpha), 16.f);
        bg->AddText(fonts_.caption, fonts_.caption->FontSize, {a.x + 16.f, a.y + 8.f},
                    with_alpha(color::text_muted, alpha), hint);
    }
    if (menu_open_) {
        bg->AddRectFilled({0, 0}, screen, IM_COL32(5, 7, 14, 200));
        const float w = std::min(440.f, screen.x - 32.f);
        ImGui::SetNextWindowPos({screen.x * .5f, screen.y * .5f}, ImGuiCond_Always, {.5f, .5f});
        ImGui::SetNextWindowSize({w, 0.f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(color::elevated));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::border_strong));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius::lg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {space::lg, space::xl});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 2.f});
        ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        auto* d = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float inner = ImGui::GetContentRegionAvail().x;
        d->AddText(fonts_.caption, fonts_.caption->FontSize, {p.x + space::sm, p.y}, color::accent_hover, "PAUSE");
        d->AddText(fonts_.title, fonts_.title->FontSize, {p.x + space::sm, p.y + 20.f}, color::text,
                   ellipsize(fonts_.title, info.game_title, inner - space::lg).c_str());
        pill(d, {p.x + space::sm, p.y + 52.f}, system_name(info.system), system_color(info.system), fonts_.caption);
        ImGui::Dummy({inner, 84.f});
        if (focus_first_) {
            ImGui::SetKeyboardFocusHere();
            ImGui::SetNavCursorVisible(true);
            focus_first_ = false;
        }
        if (menu_item(fonts_, "Reprendre la partie", Icon::Play, "Échap")) action = OverlayAction::Resume;
        if (menu_item(fonts_, "Sauvegarder l'état", Icon::Save, "F5")) action = OverlayAction::SaveState;
        if (menu_item(fonts_, "Charger l'état", Icon::Load, "F9", info.has_state))
            action = OverlayAction::LoadState;
        if (menu_item(fonts_, "Réinitialiser la console", Icon::Reset, "F4")) action = OverlayAction::Reset;
        if (menu_item(fonts_, info.fullscreen ? "Quitter le plein écran" : "Plein écran", Icon::Fullscreen, "F11"))
            action = OverlayAction::ToggleFullscreen;
        ImGui::Dummy({inner, space::sm});
        p = ImGui::GetCursorScreenPos();
        d->AddLine({p.x + space::sm, p.y}, {p.x + inner - space::sm, p.y}, color::border);
        ImGui::Dummy({inner, space::sm});
        if (menu_item(fonts_, "Quitter vers la bibliothèque", Icon::Exit, nullptr, true, true))
            action = OverlayAction::Quit;
        if (info.has_achievement) {
            ImGui::Dummy({inner, space::md});
            p = ImGui::GetCursorScreenPos();
            const ImVec2 b{p.x + inner, p.y + 62.f};
            d->AddRectFilled(p, b, color::surface, radius::md);
            const bool done = info.achievement_unlocked;
            const ImVec2 c{p.x + 30.f, p.y + 31.f};
            d->AddCircleFilled(c, 18.f, done ? color::gold_soft : color::elevated, 32);
            draw_icon(d, done ? Icon::Trophy : Icon::Lock, c, 16.f, done ? color::gold : color::text_faint);
            d->AddText(fonts_.strong, fonts_.strong->FontSize, {p.x + 58.f, p.y + 11.f}, color::text,
                       ellipsize(fonts_.strong, info.achievement_title, inner - 70.f).c_str());
            const std::string state = std::to_string(info.achievement_points) + " pts  \xC2\xB7  " +
                                      (done ? "Débloqué" : "À débloquer");
            d->AddText(fonts_.caption, fonts_.caption->FontSize, {p.x + 58.f, p.y + 35.f},
                       done ? color::gold : color::text_muted, state.c_str());
            ImGui::Dummy({inner, 62.f});
        }
        ImGui::End();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(2);
    }
    // Toasts would cover the menu; they resume (if still alive) on close.
    if (!menu_open_) draw_toasts(toasts_, fonts_);
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
    return action;
}
} // namespace ravenemudsk_ui
