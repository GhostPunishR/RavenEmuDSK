// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_overlay.hpp>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
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
               done ? "OBJECTIF ATTEINT" : "DÉMO RAVENEMU");
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

GameOverlay::GameOverlay(SDL_Window* window, SDL_Renderer* renderer) : renderer_(renderer), badges_(renderer) {
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
    notifications_.set_paused(static_cast<std::int64_t>(SDL_GetTicks64()), true);
    menu_open_ = true;
    focus_first_ = true;
}
bool GameOverlay::has_toast(Toast::Kind kind) const {
    return std::any_of(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.kind == kind; });
}

namespace {
// Font rasterized at 13 px to match RavenEmu's 12 sp layout.
void retro_card(ImDrawList* d, const Fonts& fonts, BadgeCache& cache,
                const ImVec2& screen, float top, const std::string& label,
                const std::string& detail, const std::string& badge_url, float alpha) {
    if(alpha<=0.f)return;
    const float max_width=std::max(200.f,std::min(520.f,screen.x-24.f));
    const float text_space=std::max(80.f,max_width-14.f*2-40.f-8.f);
    const auto line1=ellipsize(fonts.caption,label,text_space);
    const auto line2=detail.empty()?std::string{}:ellipsize(fonts.caption,detail,text_space);
    const float line1w=text_width(fonts.caption,line1.c_str());
    const float line2w=detail.empty()?0.f:text_width(fonts.caption,line2.c_str());
    const float width=std::min(max_width,14.f*2+40.f+8.f+std::max(line1w,line2w));
    const float height=60.f;
    const float x=(screen.x-width)*.5f;
    const ImVec2 from{x,top},to{x+width,top+height};
    d->AddRectFilled(from,to,with_alpha(IM_COL32(25,27,40,255),alpha),12.f);
    d->AddRect(from,to,with_alpha(IM_COL32(167,139,250,255),alpha),12.f,0,1.f);
    const ImVec2 badge_min{x+14.f,top+10.f},badge_max{badge_min.x+40.f,badge_min.y+40.f};
    SDL_Texture* image=cache.image(badge_url);
    if(image) {
        d->AddImageRounded(reinterpret_cast<ImTextureID>(image),badge_min,badge_max,
                            {0,0},{1,1},with_alpha(IM_COL32_WHITE,alpha),6.f);
    } else {
        // Same fixed 40 px slot as RavenEmu, with violet star fallback.
        const char* star="\xE2\x98\x85";
        const ImVec2 size=fonts.title->CalcTextSizeA(fonts.title->FontSize,FLT_MAX,0.f,star);
        d->AddText(fonts.title,fonts.title->FontSize,
                   {badge_min.x+(40.f-size.x)*.5f,badge_min.y+(40.f-size.y)*.5f},
                   with_alpha(color::accent,alpha),star);
    }
    const float tx=badge_max.x+8.f;
    const float ty=line2.empty()?top+23.f:top+13.f;
    d->AddText(fonts.caption,fonts.caption->FontSize,{tx,ty},
               with_alpha(color::text,alpha),line1.c_str());
    if(!line2.empty())
        d->AddText(fonts.caption,fonts.caption->FontSize,{tx,ty+19.f},
                   with_alpha(color::text,alpha),line2.c_str());
}
double seconds_now(){return static_cast<double>(SDL_GetTicks64())/1000.0;}
}
void GameOverlay::close_menu() {
    notifications_.set_paused(static_cast<std::int64_t>(SDL_GetTicks64()), false);
    menu_open_ = false;
}
void GameOverlay::retro_event(const ravenemudsk_ra::Event& event) { notifications_.event(event); }
void GameOverlay::update_retro_progress(const std::vector<ravenemudsk_ra::Achievement>& entries) {
    notifications_.update(entries, static_cast<std::int64_t>(SDL_GetTicks64()));
}
void GameOverlay::clear_retro() {
    notifications_.clear();
    last_progress_.reset();
    progress_visible_=false;
    progress_hidden_at_=0.0;
}
void GameOverlay::draw_retro(double wall,const ImVec2 screen) {
    notifications_.advance(static_cast<std::int64_t>(wall*1000), [&](const std::string& url) {
        return badges_.image(url) != nullptr;
    });
    if(menu_open_)return;
    const double now = notifications_.time();
    const auto& notice_ = notifications_.notice();
    const auto& indicators_ = notifications_.indicators();
    const auto notice_since_ = notifications_.notice_since();
    const auto& current=notifications_.progress();
    if(current) {
        if(!progress_visible_ || !last_progress_ || last_progress_->id!=current->id) {
            progress_started_=now;
            progress_visible_=true;
        }
        last_progress_=*current;
    } else if(progress_visible_) {
        progress_visible_=false;
        progress_hidden_at_=now;
    }
    auto* d=ImGui::GetForegroundDrawList();
    float top=10.f;
    if(notice_) {
        const double elapsed=now-notice_since_;
        const float alpha=static_cast<float>(std::clamp(
            std::min(elapsed/0.3,(5.1-elapsed)/0.3),0.0,1.0));
        if(alpha>0.f) {
            retro_card(d,fonts_,badges_,screen,top,
                notice_->text,notice_->detail,notice_->badge,alpha);
            top+=68.f;
        }
    }
    if(last_progress_) {
        const float alpha=progress_visible_?
            static_cast<float>(std::clamp((now-progress_started_)/0.3,0.0,1.0)):
            static_cast<float>(std::clamp(1.0-(now-progress_hidden_at_)/0.3,0.0,1.0));
        if(alpha>0.f)
            retro_card(d,fonts_,badges_,screen,top,
                "Progression \xC2\xB7 "+last_progress_->title,
                last_progress_->detail,last_progress_->badge_url,alpha);
    }
    if(indicators_.empty())return;
    const std::size_t count=std::min<std::size_t>(indicators_.size(),4);
    const float w=std::min(screen.x-24.f,440.f),h=16.f+count*18.f;
    const float x=(screen.x-w)*.5f,y=screen.y-10.f-h;
    d->AddRectFilled({x,y},{x+w,y+h},with_alpha(color::surface,.88f),10.f);
    for(std::size_t i=0;i<count;++i){
        const auto label=ellipsize(fonts_.caption,indicators_[i].second,w-24.f);
        d->AddText(fonts_.caption,fonts_.caption->FontSize,
                   {x+12.f,y+8.f+static_cast<float>(i)*18.f},color::text,label.c_str());
    }
}

OverlayAction GameOverlay::draw(const OverlayInfo& info) {
    badges_.begin_frame();
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
        const char* hint = info.hardcore ? "Hardcore actif - chargement des états désactivé" : "Échap : menu   \xC2\xB7   F11 : plein écran   \xC2\xB7   F5 / F9 : états";
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
        if (menu_item(fonts_, "Sauvegarder l'état", Icon::Save, "F5", !info.hardcore)) action = OverlayAction::SaveState;
        if (menu_item(fonts_, "Charger l'état", Icon::Load, "F9", info.has_state && !info.hardcore))
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
    // RetroAchievements cards use RavenEmu's dedicated top-center style.
    draw_retro(seconds_now(),screen);
    // Non-achievement local notifications still use the desktop toast style.
    if (!menu_open_) draw_toasts(toasts_, fonts_);
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
    return action;
}
} // namespace ravenemudsk_ui
