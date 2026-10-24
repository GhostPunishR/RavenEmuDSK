// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/player/game_overlay.hpp>
#include <ravenemudsk/achievements/retro_widgets.hpp>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
namespace ravenemudsk_ui {
namespace {
float text_width(ImFont* font, const char* text) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text).x;
}
// Menu rows: desktop sizes, or larger ones read from a couch in TV mode.
struct MenuStyle {
    bool tv = false;
    float row = 46.f, icon = 17.f, icon_x = 24.f, text_x = 48.f;
    ImFont* font = nullptr;
};
// `compact`: a TV of less than 900 lines (720p, 768p) keeps the whole menu on screen.
MenuStyle menu_style(const Fonts& fonts, bool tv, bool compact) {
    if (!tv) return {false, 46.f, 17.f, 24.f, 48.f, fonts.body};
    return {true, compact ? 50.f : 66.f, compact ? 24.f : 26.f, 36.f, 74.f, fonts.tv_body ? fonts.tv_body : fonts.body};
}
// Full-width menu row: icon, label and keyboard shortcut. In TV mode the row
// under the controller's focus is filled, as in the launcher's TV menu.
bool menu_item(const Fonts& fonts, const MenuStyle& m, const char* label, Icon icon, const char* shortcut,
               bool enabled = true, bool danger = false) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton(label, {w, m.row},ImGuiButtonFlags_EnableNav);
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool selected = m.tv && enabled && ImGui::IsItemFocused();
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 max{p.x + w, p.y + m.row};
    if (selected) d->AddRectFilled(p, max, danger ? color::danger : color::accent, radius::md);
    else if (hovered) d->AddRectFilled(p, max, danger ? with_alpha(color::danger, .16f) : color::surface_hover,
                                       radius::md);
    const ImU32 fg = selected ? IM_COL32_WHITE : !enabled ? color::text_faint : danger ? color::danger : color::text;
    draw_icon(d, icon, {p.x + m.icon_x, p.y + m.row * .5f}, m.icon, selected ? IM_COL32_WHITE : danger ? color::danger :
              enabled ? color::accent_hover : color::text_faint);
    d->AddText(m.font, m.font->FontSize, {p.x + m.text_x, p.y + (m.row - m.font->FontSize) * .5f - 1.f}, fg, label);
    if (!m.tv && shortcut && *shortcut) {
        const auto caption=ellipsize(fonts.caption,shortcut,std::max(16.f,w-text_width(fonts.body,label)-92.f));
        const float sw = text_width(fonts.caption, caption.c_str());
        const ImVec2 k{max.x - sw - 26.f, p.y + 12.f};
        d->AddRectFilled(k, {k.x + sw + 14.f, k.y + 22.f}, color::surface, radius::sm);
        d->AddText(fonts.caption, fonts.caption->FontSize, {k.x + 7.f, k.y + 3.f}, color::text_muted, caption.c_str());
    }
    if (!m.tv) focus_ring(d, p, max, radius::md);
    return pressed;
}
// One control of a hint row: the controller's buttons as it prints them, or
// the keyboard key when no controller is in use.
struct Hint {
    PadButton button;
    const char* key;
    const char* action;
    bool with_next = false;  // drawn with the following button, e.g. LB RB
};
// Draws "[A] Choisir   [B] Reprendre" from `p`, or only measures it without
// a draw list; returns its width.
float hint_row(ImDrawList* d, ImVec2 p, const std::optional<PadFamily>& pad, const Fonts& fonts,
               std::initializer_list<Hint> hints) {
    constexpr float h = 34.f;
    ImFont* label = fonts.tv_body ? fonts.tv_body : fonts.body;
    float x = p.x;
    for (const Hint& hint : hints) {
        if (pad) {
            x += pad_glyph(d, {x, p.y}, *pad, hint.button, fonts.strong, h);
        } else {
            const float kw = std::max(h, text_width(fonts.strong, hint.key) + 20.f);
            if (d) {
                d->AddRectFilled({x, p.y}, {x + kw, p.y + h}, color::surface, radius::sm);
                d->AddRect({x, p.y}, {x + kw, p.y + h}, color::border_strong, radius::sm);
                d->AddText(fonts.strong, fonts.strong->FontSize,
                           {x + (kw - text_width(fonts.strong, hint.key)) * .5f, p.y + (h - fonts.strong->FontSize) * .5f},
                           color::text_muted, hint.key);
            }
            x += kw;
        }
        if (hint.with_next && pad) { x += 6.f; continue; }
        x += 10.f;
        if (d) d->AddText(label, label->FontSize, {x, p.y + (h - label->FontSize) * .5f}, color::text_muted, hint.action);
        x += text_width(label, hint.action) + 36.f;
    }
    return x - 36.f - p.x;
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
                                  : "Appuyez sur "+info.keyboard.label(KeyAction::A)+" (bouton A) ou cliquez sur l'écran.";
    d->AddText(fonts.body, fonts.body->FontSize, {x, a.y + 84.f}, color::text_muted,
               ellipsize(fonts.body, line, b.x - x - 24.f).c_str());
    const char* hint = done ? "Échap ouvre le menu pour revenir à la bibliothèque."
                            : "Le moteur de succès lit la mémoire de la console après chaque image.";
    d->AddText(fonts.caption, fonts.caption->FontSize, {a.x + 24.f, b.y - 36.f}, color::text_faint,
               ellipsize(fonts.caption, hint, w - 48.f).c_str());
}
} // namespace

GameOverlay::GameOverlay(SDL_Window* window, SDL_Renderer* renderer,ravenemudsk_ra::Client* retro, bool tv)
    : renderer_(renderer),retro_(retro),tv_(tv),badges_(renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    fonts_ = load_fonts(tv);
    // A game started from TV mode is played from the couch: no mouse, no cursor.
    if (tv) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
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
void GameOverlay::open_menu(bool from_pad) {
    notifications_.set_paused(static_cast<std::int64_t>(SDL_GetTicks64()), true);
    menu_open_ = true;
    pad_menu_ = from_pad;
    focus_first_ = true;
}
bool GameOverlay::has_toast(Toast::Kind kind) const {
    return std::any_of(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.kind == kind; });
}

RetroCardStyle retro_card_style(ImVec2 screen, bool tv) {
    RetroCardStyle style;
    if (!tv) {
        style.max_width = std::max(200.f, std::min(520.f, screen.x - 24.f));
        return style;
    }
    style.tv = true;
    style.top = style.bottom = std::max(16.f, screen.y * .05f);
    style.height = 104.f;
    style.badge = 72.f;
    style.pad = 22.f;
    style.gap = 18.f;
    style.line = 30.f;
    style.radius = 18.f;
    style.max_width = std::max(320.f, std::min(1100.f, std::min(screen.x * .6f, screen.x - 2.f * style.top)));
    return style;
}
UnlockBannerStyle unlock_banner_style(ImVec2 screen, bool tv) {
    UnlockBannerStyle style;
    if (!tv) {
        style.margin = 10.f;
        style.size = 64.f;
        style.pad = 14.f;
        style.radius = 10.f;
        style.max_width = std::max(style.size * 3.f, std::min(560.f, screen.x - 2.f * style.margin));
        return style;
    }
    style.margin = std::max(16.f, std::min(screen.x, screen.y) * .05f);
    style.size = std::clamp(screen.y * .11f, 72.f, 160.f);
    style.pad = style.size * .19f;
    style.radius = style.size * .12f;
    style.max_width = std::max(style.size * 3.f, std::min(screen.x * .7f, screen.x - 2.f * style.margin));
    return style;
}
UnlockBannerFrame unlock_banner_frame(double t) {
    // Seconds: drop, unfold, hold, fold, rise. Within the 5.2 s a notice lasts.
    constexpr double dropped = .30, unfolded = .65, folding = 4.40, folded = 4.75, gone = 5.05;
    if (t < 0. || t >= gone) return {};
    const auto ease_out = [](double x) { return static_cast<float>(1. - (1. - x) * (1. - x) * (1. - x)); };
    const auto smooth = [](double x) { return static_cast<float>(x * x * (3. - 2. * x)); };
    UnlockBannerFrame frame;
    frame.drop = t < dropped ? ease_out(t / dropped) : t < folded ? 1.f : 1.f - ease_out((t - folded) / (gone - folded));
    frame.unfold = t < dropped ? 0.f : t < unfolded ? smooth((t - dropped) / (unfolded - dropped)) :
        t < folding ? 1.f : t < folded ? 1.f - smooth((t - folding) / (folded - folding)) : 0.f;
    return frame;
}
namespace {
// Five-pointed star, filled as a fan from its center.
void draw_star(ImDrawList* d,ImVec2 c,float r,ImU32 col){
    ImVec2 p[10];
    for(int i=0;i<10;++i){
        const float a=-1.5707963f+static_cast<float>(i)*0.6283185f;
        const float radius=i%2?r*.45f:r;
        p[i]={c.x+std::cos(a)*radius,c.y+std::sin(a)*radius};
    }
    // Anti-aliased triangles would leave seams: fill plainly, then smooth the outline.
    const auto flags=d->Flags;
    d->Flags&=~ImDrawListFlags_AntiAliasedFill;
    for(int i=0;i<10;++i)d->AddTriangleFilled(c,p[i],p[(i+1)%10],col);
    d->Flags=flags;
    d->AddPolyline(p,10,col,ImDrawFlags_Closed,1.f);
}
// Desktop text is rasterized at 13 px to match RavenEmu's 12 sp layout; TV text at 22 px.
ImFont* card_font(const Fonts& fonts, const RetroCardStyle& style) {
    return style.tv && fonts.tv_body ? fonts.tv_body : fonts.caption;
}
void retro_card(ImDrawList* d, const Fonts& fonts, BadgeCache& cache, const RetroCardStyle& style,
                const ImVec2& screen, float top, const std::string& label,
                const std::string& detail, const std::string& badge_url, float alpha) {
    if(alpha<=0.f)return;
    ImFont* font=card_font(fonts,style);
    const float chrome=style.pad*2+style.badge+style.gap;
    const float text_space=std::max(80.f,style.max_width-chrome);
    const auto line1=ellipsize(font,label,text_space);
    const auto line2=detail.empty()?std::string{}:ellipsize(font,detail,text_space);
    const float line1w=text_width(font,line1.c_str());
    const float line2w=detail.empty()?0.f:text_width(font,line2.c_str());
    const float width=std::min(style.max_width,chrome+std::max(line1w,line2w));
    const float height=style.height;
    const float x=(screen.x-width)*.5f;
    const ImVec2 from{x,top},to{x+width,top+height};
    d->AddRectFilled(from,to,with_alpha(IM_COL32(25,27,40,255),alpha),style.radius);
    d->AddRect(from,to,with_alpha(IM_COL32(167,139,250,255),alpha),style.radius,0,style.tv?2.f:1.f);
    const ImVec2 badge_min{x+style.pad,top+(height-style.badge)*.5f},badge_max{badge_min.x+style.badge,badge_min.y+style.badge};
    SDL_Texture* image=cache.image(badge_url);
    if(image) {
        d->AddImageRounded(reinterpret_cast<ImTextureID>(image),badge_min,badge_max,
                            {0,0},{1,1},with_alpha(IM_COL32_WHITE,alpha),6.f);
    } else {
        // Same fixed badge slot as RavenEmu, with a violet star drawn as a
        // shape: the fonts carry no star glyph.
        draw_star(d,{(badge_min.x+badge_max.x)*.5f,(badge_min.y+badge_max.y)*.5f},style.badge*.4f,
                  with_alpha(color::accent,alpha));
    }
    const float tx=badge_max.x+style.gap;
    const float lines=line2.empty()?font->FontSize:style.line+font->FontSize;
    const float ty=top+(height-lines)*.5f;
    d->AddText(font,font->FontSize,{tx,ty},with_alpha(color::text,alpha),line1.c_str());
    if(!line2.empty())
        d->AddText(font,font->FontSize,{tx,ty+style.line},with_alpha(color::text,alpha),line2.c_str());
}
// Draws the unlock banner at `elapsed` seconds into its notice; returns the
// bottom edge it reaches, or 0 when nothing is drawn.
float unlock_banner(ImDrawList* d, const Fonts& fonts, BadgeCache& cache, ImVec2 screen,
                    const ravenemudsk_ra::Popup& popup, double elapsed, bool tv) {
    const auto style = unlock_banner_style(screen, tv);
    const auto frame = unlock_banner_frame(elapsed);
    if (frame.drop <= 0.f) return 0.f;
    ImFont* heading_font = tv ? fonts.tv_body : fonts.caption;
    ImFont* title_font = tv ? fonts.tv_title : fonts.strong;
    const float space = std::max(80.f, style.max_width - style.size - 2.f * style.pad);
    const auto heading = ellipsize(heading_font, popup.heading, space);
    const auto title = ellipsize(title_font, popup.title, space);
    const float text = std::max(text_width(heading_font, heading.c_str()), text_width(title_font, title.c_str()));
    const float full = style.size + (text > 0.f ? 2.f * style.pad + text : 0.f);
    const float width = style.size + (full - style.size) * frame.unfold;
    const float x = (screen.x - width) * .5f;
    const float y = style.margin - (style.size + style.margin) * (1.f - frame.drop);
    d->AddRectFilled({x + 4.f, y + 6.f}, {x + width + 4.f, y + style.size + 6.f}, color::shadow, style.radius);
    d->AddRectFilled({x, y}, {x + width, y + style.size}, with_alpha(color::background, .94f), style.radius);
    // The badge fills its square; a gold frame marks an unlock or a completed game.
    const float inset = style.size * .08f;
    const ImVec2 badge_min{x + inset, y + inset}, badge_max{x + style.size - inset, y + style.size - inset};
    if (SDL_Texture* image = cache.image(popup.badge)) {
        d->AddImageRounded(reinterpret_cast<ImTextureID>(image), badge_min, badge_max, {0, 0}, {1, 1},
                           IM_COL32_WHITE, style.radius * .6f);
    } else {
        d->AddRectFilled(badge_min, badge_max, color::surface, style.radius * .6f);
        draw_star(d, {(badge_min.x + badge_max.x) * .5f, (badge_min.y + badge_max.y) * .5f},
                  (badge_max.x - badge_min.x) * .34f, popup.trophy ? color::gold : color::accent);
    }
    d->AddRect(badge_min, badge_max, popup.trophy ? color::gold : color::border_strong, style.radius * .6f, 0, 2.f);
    if (frame.unfold > 0.f && text > 0.f) {
        // The text is uncovered as the band unfolds, never drawn past it.
        d->PushClipRect({x + style.size, y}, {x + width, y + style.size}, true);
        const float tx = x + style.size + style.pad * .6f;
        const float gap = style.size * .06f;
        const bool two = !title.empty() && !heading.empty();
        const float block = two ? heading_font->FontSize + gap + title_font->FontSize : (title.empty() ? heading_font : title_font)->FontSize;
        float ty = y + (style.size - block) * .5f;
        if (!heading.empty()) {
            d->AddText(heading_font, heading_font->FontSize, {tx, ty}, popup.trophy ? color::gold : color::text_muted, heading.c_str());
            ty += heading_font->FontSize + gap;
        }
        if (!title.empty()) d->AddText(title_font, title_font->FontSize, {tx, ty}, color::text, title.c_str());
        d->PopClipRect();
    }
    return y + style.size;
}
double seconds_now(){return static_cast<double>(SDL_GetTicks64())/1000.0;}
}
void GameOverlay::open_achievements() {
    retro_open_ = true;
    focus_achievement_ = true;
    retro_tab_ = 0;
    retro_tab_requested_ = true;
}
void GameOverlay::close_menu() {
    notifications_.set_paused(static_cast<std::int64_t>(SDL_GetTicks64()), false);
    menu_open_ = false;
    retro_open_=false;
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
float GameOverlay::draw_retro(double wall,const ImVec2 screen) {
    notifications_.advance(static_cast<std::int64_t>(wall*1000), [&](const std::string& url) {
        return badges_.image(url) != nullptr;
    });
    const auto& notice_ = notifications_.notice();
    const auto notice_since_ = notifications_.notice_since();
    if(notice_ && notice_since_!=shown_notice_since_) {
        shown_notice_since_=notice_since_;
        if(notice_->trophy) unlock_sound_=true;
    }
    if(menu_open_)return 0.f;
    const double now = notifications_.time();
    const auto& indicators_ = notifications_.indicators();
    float lowest=0.f;
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
    const auto style=retro_card_style(screen,tv_);
    float top=style.top;
    if(notice_) {
        // The unlock banner, top center, in both modes; the progress card goes below it.
        const float bottom=unlock_banner(d,fonts_,badges_,screen,*notice_,now-notice_since_,tv_);
        if(bottom>0.f){top=std::max(top,bottom+(tv_?14.f:8.f));lowest=bottom;}
    }
    if(last_progress_) {
        const float alpha=progress_visible_?
            static_cast<float>(std::clamp((now-progress_started_)/0.3,0.0,1.0)):
            static_cast<float>(std::clamp(1.0-(now-progress_hidden_at_)/0.3,0.0,1.0));
        if(alpha>0.f){
            retro_card(d,fonts_,badges_,style,screen,top,
                "Progression \xC2\xB7 "+last_progress_->title,
                last_progress_->detail,last_progress_->badge_url,alpha);
            lowest=top+style.height;
        }
    }
    if(indicators_.empty())return lowest;
    // Active challenges, at the bottom center.
    ImFont* font=card_font(fonts_,style);
    const std::size_t count=std::min<std::size_t>(indicators_.size(),4);
    const float row=style.tv?34.f:18.f,inset=style.tv?20.f:12.f;
    const float w=style.tv?std::min(style.max_width,screen.x*.5f):std::min(screen.x-24.f,440.f);
    const float h=style.tv?inset+count*row:16.f+count*18.f;
    const float first=style.tv?inset*.5f+(row-font->FontSize)*.5f:8.f;
    const float x=(screen.x-w)*.5f,y=screen.y-style.bottom-h;
    d->AddRectFilled({x,y},{x+w,y+h},with_alpha(color::surface,.88f),style.tv?14.f:10.f);
    for(std::size_t i=0;i<count;++i){
        const auto label=ellipsize(font,indicators_[i].second,w-inset*2.f);
        d->AddText(font,font->FontSize,{x+inset,y+first+static_cast<float>(i)*row},color::text,label.c_str());
    }
    return lowest;
}

// "Succès et classements" read from a couch: large panel and text, each
// achievement a row the D-pad walks through (the list follows it), tabs on
// LB / RB, B back to the menu.
void GameOverlay::draw_achievements_tv(const OverlayInfo& info, ImVec2 screen) {
    auto& client = retro_ ? *retro_ : ravenemudsk_ra::client();
    const auto snapshot = client.snapshot();
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, screen, IM_COL32(5, 7, 14, 215));
    constexpr int tabs = 3;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
        retro_tab_ = (retro_tab_ + tabs - 1) % tabs;
        retro_tab_requested_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
        retro_tab_ = (retro_tab_ + 1) % tabs;
        retro_tab_requested_ = true;
    }
    const float margin = std::max(40.f, screen.y * .06f);
    ImGui::SetNextWindowPos({screen.x * .5f, screen.y * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({std::min(1280.f, screen.x - 2.f * margin), screen.y - 2.f * margin});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(color::elevated));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    // The rows draw their own focus; tabs in the accent colour, not ImGui's blue.
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::ColorConvertU32ToFloat4(color::surface));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImGui::ColorConvertU32ToFloat4(color::surface_hover));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, ImGui::ColorConvertU32ToFloat4(color::accent));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImGui::ColorConvertU32ToFloat4(color::accent));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius::lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {40.f, 32.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {20.f, 10.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12.f, 8.f});
    ImGui::PushFont(fonts_.tv_body);
    ImGui::Begin("##retro-tv", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    auto* d = ImGui::GetWindowDrawList();
    const float inner = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    const std::string heading = snapshot.game_title.empty() ? snapshot.status : snapshot.game_title;
    d->AddText(fonts_.tv_title, fonts_.tv_title->FontSize, p, color::text,
               ellipsize(fonts_.tv_title, heading, inner).c_str());
    const std::string summary = std::string(info.hardcore ? "Mode Hardcore" : "Mode Softcore") + "  \xC2\xB7  " +
        std::to_string(snapshot.unlocked_count) + " / " + std::to_string(snapshot.total_count) + " succès";
    d->AddText(fonts_.tv_body, fonts_.tv_body->FontSize, {p.x, p.y + 42.f}, color::text_muted, summary.c_str());
    ImGui::Dummy({inner, 84.f});
    constexpr float hint_h = 58.f;
    static const char* const labels[tabs] = {"Succès", "Classements", "Activité"};
    if (ImGui::BeginTabBar("##retro-tabs-tv")) {
        for (int i = 0; i < tabs; ++i) {
            const ImGuiTabItemFlags flags = retro_tab_requested_ && retro_tab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (!ImGui::BeginTabItem(labels[i], nullptr, flags)) continue;
            if (!retro_tab_requested_) retro_tab_ = i;
            ImGui::BeginChild("##retro-tv-list", {0.f, -hint_h}, ImGuiChildFlags_NavFlattened);
            if (i == 0) {
                if (snapshot.achievements.empty())
                    ImGui::TextWrapped("%s", snapshot.status.c_str());
                for (const auto& a : snapshot.achievements) {
                    ImGui::PushID(a.id.c_str());
                    if (focus_achievement_) {
                        ImGui::SetKeyboardFocusHere();
                        ImGui::SetNavCursorVisible(true);
                        focus_achievement_ = false;
                    }
                    const ImVec2 r = ImGui::GetCursorScreenPos();
                    const float w = ImGui::GetContentRegionAvail().x, h = 112.f;
                    ImGui::Selectable("##achievement", false, ImGuiSelectableFlags_None, {w, h});
                    auto* rd = ImGui::GetWindowDrawList();
                    if (ImGui::IsItemFocused()) {
                        rd->AddRectFilled(r, {r.x + w, r.y + h}, color::surface_hover, radius::md);
                        rd->AddRect(r, {r.x + w, r.y + h}, color::accent_hover, radius::md, 0, 2.f);
                    }
                    constexpr float badge = 76.f;
                    const ImVec2 ba{r.x + 18.f, r.y + (h - badge) * .5f}, bb{ba.x + badge, ba.y + badge};
                    if (SDL_Texture* t = a.badge_url.empty() ? nullptr : badges_.image(a.badge_url)) {
                        rd->AddImageRounded(reinterpret_cast<ImTextureID>(t), ba, bb, {0, 0}, {1, 1},
                                            a.unlocked ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110), radius::md);
                    } else {
                        rd->AddRectFilled(ba, bb, color::surface, radius::md);
                        draw_icon(rd, a.unlocked ? Icon::Trophy : Icon::Lock, {ba.x + badge * .5f, ba.y + badge * .5f},
                                  28.f, a.unlocked ? color::gold : color::text_faint);
                    }
                    const float x = bb.x + 22.f, right = r.x + w - 22.f;
                    const std::string points = std::to_string(a.points) + " pts";
                    const float pw = text_width(fonts_.tv_body, points.c_str());
                    rd->AddText(fonts_.tv_body, fonts_.tv_body->FontSize, {right - pw, r.y + 14.f},
                                a.unlocked ? color::gold : color::text_muted, points.c_str());
                    rd->AddText(fonts_.tv_body, fonts_.tv_body->FontSize, {x, r.y + 14.f}, color::text,
                                ellipsize(fonts_.tv_body, a.title, right - pw - 24.f - x).c_str());
                    rd->AddText(fonts_.tv_body, fonts_.tv_body->FontSize, {x, r.y + 44.f}, color::text_muted,
                                ellipsize(fonts_.tv_body, a.description, right - x).c_str());
                    const char* state = a.unlocked ? "Débloqué" : a.progress.empty() ? "À obtenir" : a.progress.c_str();
                    rd->AddText(fonts_.strong, fonts_.strong->FontSize, {x, r.y + 80.f},
                                a.unlocked ? color::gold : color::text_faint, state);
                    if (!a.unlocked && a.measured_percent > 0.f) {
                        const float bx = x + text_width(fonts_.strong, state) + 16.f;
                        progress_bar(rd, {bx, r.y + 86.f}, {std::min(bx + 320.f, right), r.y + 94.f},
                                     std::clamp(a.measured_percent / 100.f, 0.f, 1.f), color::accent);
                    }
                    ImGui::PopID();
                }
            } else if (i == 1) {
                draw_leaderboards(client, snapshot, fonts_, true);
            } else {
                ImGui::TextWrapped("%s", snapshot.rich_presence.empty()
                    ? "Aucune activité détaillée disponible pour ce jeu." : snapshot.rich_presence.c_str());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    retro_tab_requested_ = false;
    p = {ImGui::GetWindowPos().x + 40.f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 32.f - 34.f};
    if (info.pad) {
        hint_row(d, p, info.pad, fonts_,
                 {{PadButton::LeftShoulder, "", "", true}, {PadButton::RightShoulder, "", "Onglet"},
                  {PadButton::East, "Échap", "Retour"}});
    } else {
        hint_row(d, p, info.pad, fonts_, {{PadButton::East, "Échap", "Fermer le menu"}});
    }
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(8);
}

// The Hardcore badge, the demo panel and the pause line, at the top left.
void GameOverlay::draw_status(const OverlayInfo& info, ImDrawList* bg) {
    if(info.hardcore_preferred){
        pill(bg,{16.f,16.f},info.hardcore&&!info.hardcore_waiting?"HARDCORE":"HARDCORE EN ATTENTE",color::gold,fonts_.caption);
    }
    if (info.demo && !menu_open_) demo_panel(fonts_, info);
    if (menu_open_) return;
    float top=info.hardcore_waiting?76.f:info.hardcore_preferred?54.f:16.f;
    const auto status_pill=[&](const std::string& text) {
        const auto fitted=ellipsize(fonts_.body,text,std::max(40.f,ImGui::GetIO().DisplaySize.x-104.f));
        const char* label=fitted.c_str();
        const ImVec2 size{text_width(fonts_.body, label) + 56.f, 40.f};
        bg->AddRectFilled({16,top}, {16+size.x,top+size.y}, with_alpha(color::elevated,.95f),20.f);
        draw_icon(bg,Icon::Pause,{38,top+20.f},14.f,color::accent_hover);
        bg->AddText(fonts_.body,fonts_.body->FontSize,{54,top+11.f},color::text,label);
        top+=size.y+8.f;
    };
    if (info.fast_forward) status_pill("Avance rapide  \xC2\xB7  \xC3\x97" + std::to_string(fast_forward_speed));
    if (info.paused_unfocused) status_pill("Pause  \xC2\xB7  la fenêtre est en arrière-plan");
    else if (info.paused) status_pill("Pause  \xC2\xB7  "+info.keyboard.label(KeyAction::Pause)+" pour reprendre");
    // The other game's menu is open: this one waits for it, as two consoles do.
    if (info.link_partner_paused) status_pill("Partenaire en pause  \xC2\xB7  "+info.link_partner);
}
// Short reminder of the essential keys when a game starts, with the
// buttons of the controller in use when there is one.
void GameOverlay::draw_start_hint(const OverlayInfo& info, ImVec2 screen, ImDrawList* bg, float time) {
    const float alpha = std::clamp(6.f - time, 0.f, 1.f) * std::clamp(time * 2.f, 0.f, 1.f);
    ImFont* font = tv_ && fonts_.tv_body ? fonts_.tv_body : fonts_.caption;
    const float bar_h = tv_ ? 52.f : 32.f;
    const float bottom = tv_ ? retro_card_style(screen, true).bottom : 20.f;
    const bool usual_chord = info.pads.input(KeyAction::Select) == SDL_CONTROLLER_BUTTON_BACK &&
                             info.pads.input(KeyAction::Start) == SDL_CONTROLLER_BUTTON_START;
    if (info.pad && !info.hardcore && usual_chord) {
        const float gh = tv_ ? 34.f : 22.f;
        const char* label = "  Menu";
        const float gw = pad_menu_combo(nullptr, {}, *info.pad, fonts_.strong, gh);
        const float w = gw + text_width(font, label) + 32.f;
        const ImVec2 a{(screen.x - w) * .5f, screen.y - bottom - bar_h};
        bg->AddRectFilled(a, {a.x + w, a.y + bar_h}, with_alpha(color::elevated, .92f * alpha), bar_h * .5f);
        pad_menu_combo(bg, {a.x + 16.f, a.y + (bar_h - gh) * .5f}, *info.pad, fonts_.strong, gh, alpha);
        bg->AddText(font, font->FontSize, {a.x + 16.f + gw, a.y + (bar_h - font->FontSize) * .5f},
                    with_alpha(color::text_muted, alpha), label);
    } else {
        // A controller whose Select or Start was moved: its chord, in words.
        const std::string hint_text=info.hardcore?"Hardcore actif - chargement des états désactivé":
            info.pad?pad_input_name(info.pads.input(KeyAction::Select),*info.pad)+" + "+
                pad_input_name(info.pads.input(KeyAction::Start),*info.pad)+" : menu":
            "Échap : menu   \xC2\xB7   "+info.keyboard.label(KeyAction::Fullscreen)+" : plein écran   \xC2\xB7   "+
            info.keyboard.label(KeyAction::SaveState)+" / "+info.keyboard.label(KeyAction::LoadState)+" : états";
        const auto fitted=ellipsize(font,hint_text,std::max(40.f,screen.x-64.f));
        const char* hint=fitted.c_str();
        const float w = text_width(font, hint) + 32.f;
        const ImVec2 a{(screen.x - w) * .5f, screen.y - bottom - bar_h};
        bg->AddRectFilled(a, {a.x + w, a.y + bar_h}, with_alpha(color::elevated, .92f * alpha), bar_h * .5f);
        bg->AddText(font, font->FontSize, {a.x + 16.f, a.y + (bar_h - font->FontSize) * .5f},
                    with_alpha(color::text_muted, alpha), hint);
    }
}
// The pause menu; returns the entry chosen this frame, or None.
OverlayAction GameOverlay::draw_pause_menu(const OverlayInfo& info, ImVec2 screen, ImDrawList* bg) {
    OverlayAction action = OverlayAction::None;
    const bool compact = tv_ && screen.y < 900.f;
    const MenuStyle m = menu_style(fonts_, tv_, compact);
    bg->AddRectFilled({0, 0}, screen, IM_COL32(5, 7, 14, tv_ ? 215 : 200));
    const float w = std::min(tv_ ? 760.f : 440.f, screen.x - 32.f);
    ImGui::SetNextWindowPos({screen.x * .5f, screen.y * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({w, 0.f});
    ImGui::SetNextWindowSizeConstraints({w,100.f},{w,std::max(100.f,screen.y-32.f)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(color::elevated));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius::lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, !tv_ ? ImVec2{space::lg, space::xl} :
                        ImVec2{36.f, compact ? 24.f : 32.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, !tv_ ? 2.f : compact ? 4.f : 6.f});
    // TV rows show their focus by their fill alone.
    ImGui::PushStyleColor(ImGuiCol_NavCursor, tv_ ? ImVec4(0, 0, 0, 0) : ImGui::GetStyleColorVec4(ImGuiCol_NavCursor));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    auto* d = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float inner = ImGui::GetContentRegionAvail().x;
    if (tv_) {
        d->AddText(fonts_.strong, fonts_.strong->FontSize, {p.x + space::sm, p.y}, color::accent_hover, "PAUSE");
        d->AddText(fonts_.tv_title, fonts_.tv_title->FontSize, {p.x + space::sm, p.y + (compact ? 22.f : 26.f)},
                   color::text, ellipsize(fonts_.tv_title, info.game_title, inner - space::lg).c_str());
        pill(d, {p.x + space::sm, p.y + (compact ? 62.f : 72.f)}, system_name(info.system), system_color(info.system),
             fonts_.strong);
        ImGui::Dummy({inner, compact ? 96.f : 124.f});
    } else {
        d->AddText(fonts_.caption, fonts_.caption->FontSize, {p.x + space::sm, p.y}, color::accent_hover, "PAUSE");
        d->AddText(fonts_.title, fonts_.title->FontSize, {p.x + space::sm, p.y + 20.f}, color::text,
                   ellipsize(fonts_.title, info.game_title, inner - space::lg).c_str());
        pill(d, {p.x + space::sm, p.y + 52.f}, system_name(info.system), system_color(info.system), fonts_.caption);
        ImGui::Dummy({inner, 84.f});
    }
    auto focus_here = [](bool& request) {
        if (!request) return;
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNavCursorVisible(true);
        request = false;
    };
    focus_here(focus_first_);
    // Opened with the controller: the keyboard shortcuts would not apply.
    const auto shortcut = [&](KeyAction a) { return pad_menu_ ? std::string() : info.keyboard.label(a); };
    if (menu_item(fonts_, m, "Reprendre la partie", Icon::Play, pad_menu_ ? nullptr : "Échap")) action = OverlayAction::Resume;
    if (menu_item(fonts_, m, "Sauvegarder l'état", Icon::Save, shortcut(KeyAction::SaveState).c_str())) action = OverlayAction::SaveState;
    // A state loaded on one side only would part the two linked games.
    if (menu_item(fonts_, m, "Charger l'état", Icon::Load, shortcut(KeyAction::LoadState).c_str(),
                  info.has_state && !info.hardcore_preferred && info.link_partner.empty()))
        action = OverlayAction::LoadState;
    focus_here(focus_retro_entry_);
    if (menu_item(fonts_, m, "Succès et classements", Icon::Trophy, nullptr)) open_achievements();
    if (menu_item(fonts_, m, info.hardcore_preferred ? "Passer en Softcore" : "Activer Hardcore (réinitialise)", Icon::Lock, nullptr))
        action = OverlayAction::ToggleHardcore;
    if (menu_item(fonts_, m, "Réinitialiser la console", Icon::Reset, shortcut(KeyAction::Reset).c_str())) action = OverlayAction::Reset;
    // TV mode stays in full screen, from the launcher to the game and back.
    if (!tv_ && menu_item(fonts_, m, info.fullscreen ? "Quitter le plein écran" : "Plein écran", Icon::Fullscreen,
                          shortcut(KeyAction::Fullscreen).c_str()))
        action = OverlayAction::ToggleFullscreen;
    if (!info.link_partner.empty()) {
        ImGui::Dummy({inner, space::sm});
        p = ImGui::GetCursorScreenPos();
        ImFont* note = tv_ ? fonts_.strong : fonts_.caption;
        d->AddText(note, note->FontSize, {p.x + space::sm, p.y}, color::text_muted,
                   ellipsize(note, "Câble sans fil relié à " + info.link_partner, inner - space::lg).c_str());
        ImGui::Dummy({inner, note->FontSize + 4.f});
        if (menu_item(fonts_, m, "Débrancher le câble", Icon::Close, nullptr)) action = OverlayAction::UnplugLink;
    }
    ImGui::Dummy({inner, space::sm});
    p = ImGui::GetCursorScreenPos();
    // One whole pixel row: a line between two rows is drawn in two halves.
    const float rule = std::floor(p.y);
    d->AddRectFilled({p.x + space::sm, rule}, {p.x + inner - space::sm, rule + 1.f}, color::border);
    ImGui::Dummy({inner, space::sm});
    if (menu_item(fonts_, m, "Quitter vers la bibliothèque", Icon::Exit, nullptr, true, true))
        action = OverlayAction::Quit;
    if (info.has_achievement) {
        ImGui::Dummy({inner, space::md});
        p = ImGui::GetCursorScreenPos();
        // Larger text in TV mode; the 720p menu still fits with it.
        const float h = tv_ ? 66.f : 62.f, text_x = tv_ ? 68.f : 58.f;
        ImFont* title = tv_ ? fonts_.tv_body : fonts_.strong;
        ImFont* detail = tv_ ? fonts_.strong : fonts_.caption;
        const ImVec2 b{p.x + inner, p.y + h};
        d->AddRectFilled(p, b, color::surface, radius::md);
        const bool done = info.achievement_unlocked;
        const ImVec2 c{p.x + (tv_ ? 34.f : 30.f), p.y + h * .5f};
        d->AddCircleFilled(c, tv_ ? 22.f : 18.f, done ? color::gold_soft : color::elevated, 32);
        draw_icon(d, done ? Icon::Trophy : Icon::Lock, c, tv_ ? 20.f : 16.f, done ? color::gold : color::text_faint);
        d->AddText(title, title->FontSize, {p.x + text_x, p.y + (tv_ ? 8.f : 11.f)}, color::text,
                   ellipsize(title, info.achievement_title, inner - text_x - 12.f).c_str());
        const std::string state = std::to_string(info.achievement_points) + " pts  \xC2\xB7  " +
                                  (done ? "Débloqué" : "À débloquer");
        d->AddText(detail, detail->FontSize, {p.x + text_x, p.y + (tv_ ? 38.f : 35.f)},
                   done ? color::gold : color::text_muted, state.c_str());
        ImGui::Dummy({inner, h});
    }
    if (tv_) {
        ImGui::Dummy({inner, space::md});
        p = ImGui::GetCursorScreenPos();
        hint_row(d, {p.x + space::sm, p.y}, info.pad, fonts_,
                 {{PadButton::South, "Entrée", "Choisir"}, {PadButton::East, "Échap", "Reprendre"}});
        ImGui::Dummy({inner, 34.f});
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
    return action;
}
// "Relier le câble à…": several devices wait for the wireless link cable,
// the player picks one. The game waits meanwhile, as in the pause menu.
OverlayAction GameOverlay::draw_link_chooser(const OverlayInfo& info, ImVec2 screen, ImDrawList* bg) {
    OverlayAction action = OverlayAction::None;
    const bool compact = tv_ && screen.y < 900.f;
    const MenuStyle m = menu_style(fonts_, tv_, compact);
    bg->AddRectFilled({0, 0}, screen, IM_COL32(5, 7, 14, tv_ ? 215 : 200));
    const float w = std::min(tv_ ? 760.f : 440.f, screen.x - 32.f);
    ImGui::SetNextWindowPos({screen.x * .5f, screen.y * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({w, 0.f});
    ImGui::SetNextWindowSizeConstraints({w,100.f},{w,std::max(100.f,screen.y-32.f)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(color::elevated));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius::lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, !tv_ ? ImVec2{space::lg, space::xl} :
                        ImVec2{36.f, compact ? 24.f : 32.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, !tv_ ? 2.f : compact ? 4.f : 6.f});
    ImGui::PushStyleColor(ImGuiCol_NavCursor, tv_ ? ImVec4(0, 0, 0, 0) : ImGui::GetStyleColorVec4(ImGuiCol_NavCursor));
    ImGui::Begin("##link-chooser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    auto* d = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float inner = ImGui::GetContentRegionAvail().x;
    ImFont* heading = tv_ ? fonts_.tv_title : fonts_.title;
    d->AddText(tv_ ? fonts_.strong : fonts_.caption, (tv_ ? fonts_.strong : fonts_.caption)->FontSize,
               {p.x + space::sm, p.y}, color::accent_hover, "CÂBLE LINK");
    d->AddText(heading, heading->FontSize, {p.x + space::sm, p.y + (tv_ ? 26.f : 20.f)}, color::text,
               "Relier le câble à\xE2\x80\xA6");
    ImGui::Dummy({inner, (tv_ ? 26.f : 20.f) + heading->FontSize + space::sm});
    ImGui::PushFont(tv_ && fonts_.tv_body ? fonts_.tv_body : fonts_.body);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::text_muted));
    ImGui::Indent(space::sm);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner - space::lg);
    ImGui::TextUnformatted(("Plusieurs appareils attendent un câble link. Cet ordinateur apparaît chez eux sous "
                            "le nom " + info.link_own_label + ".").c_str());
    ImGui::PopTextWrapPos();
    ImGui::Unindent(space::sm);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy({inner, space::sm});
    // Just shown: the first device takes the focus, for the controller.
    if (!chooser_shown_) {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNavCursorVisible(true);
    }
    for (const auto& choice : info.link_choices) {
        ImGui::PushID(static_cast<int>(choice.id ^ (choice.id >> 32)));
        if (menu_item(fonts_, m, choice.label.c_str(), Icon::Gamepad, nullptr)) {
            chosen_link_ = choice.id;
            action = OverlayAction::ChooseLink;
        }
        ImGui::PopID();
    }
    ImGui::Dummy({inner, space::sm});
    p = ImGui::GetCursorScreenPos();
    const float rule = std::floor(p.y);
    d->AddRectFilled({p.x + space::sm, rule}, {p.x + inner - space::sm, rule + 1.f}, color::border);
    ImGui::Dummy({inner, space::sm});
    if (menu_item(fonts_, m, "Plus tard", Icon::Close, nullptr)) action = OverlayAction::ChooseLinkLater;
    if (tv_) {
        ImGui::Dummy({inner, space::md});
        p = ImGui::GetCursorScreenPos();
        hint_row(d, {p.x + space::sm, p.y}, info.pad, fonts_,
                 {{PadButton::South, "Entrée", "Choisir"}, {PadButton::East, "Échap", "Plus tard"}});
        ImGui::Dummy({inner, 34.f});
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
    return action;
}
// "Succès et classements" in a desktop window over the pause menu.
void GameOverlay::draw_achievements_window(const OverlayInfo& info, ImVec2 screen) {
    ImGui::SetNextWindowPos({screen.x*.5f,screen.y*.5f},ImGuiCond_Always,{.5f,.5f});
    ImGui::SetNextWindowSize({std::min(680.f,screen.x-32.f),std::min(620.f,screen.y-48.f)});
    ImGui::Begin("Succès et classements",&retro_open_,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoCollapse);
    auto& client=retro_?*retro_:ravenemudsk_ra::client();const auto snapshot=client.snapshot();
    ImGui::TextWrapped("%s",snapshot.game_title.empty()?snapshot.status.c_str():snapshot.game_title.c_str());
    ImGui::TextDisabled("%s",info.hardcore?"Mode Hardcore":"Mode Softcore");
    if(button("Retour au menu",Icon::Close,ButtonStyle::Ghost))retro_open_=false;
    if(ImGui::BeginTabBar("##retro-tabs")){
        if(ImGui::BeginTabItem("Succès")){
            ImGui::Text("%d / %d succès",snapshot.unlocked_count,snapshot.total_count);
            for(const auto& a:snapshot.achievements){
                ImGui::PushID(a.id.c_str());ImGui::Separator();
                ImGui::TextWrapped("%s  (%d pts)",a.title.c_str(),a.points);
                ImGui::TextWrapped("%s",a.description.c_str());
                ImGui::TextDisabled("%s",a.unlocked?"Débloqué":a.progress.empty()?"À obtenir":a.progress.c_str());
                if(!a.unlocked&&a.measured_percent>0)ImGui::ProgressBar(std::clamp(a.measured_percent/100.f,0.f,1.f));
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Classements")){draw_leaderboards(client,snapshot,fonts_);ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Activité")){
            ImGui::TextWrapped("%s",snapshot.rich_presence.empty()?"Aucune activité détaillée disponible pour ce jeu.":snapshot.rich_presence.c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
OverlayAction GameOverlay::draw(const OverlayInfo& info) {
    badges_.begin_frame();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    OverlayAction action = OverlayAction::None;
    if (tv_) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    auto* bg = ImGui::GetBackgroundDrawList();
    const auto time = static_cast<float>(ImGui::GetTime());
    draw_status(info, bg);
    if (time < 6.f && !menu_open_) draw_start_hint(info, screen, bg, time);
    // B (Circle on PlayStation) goes back: from the achievements to the menu,
    // from the menu to the game.
    bool back = menu_open_ && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
    if (back && retro_open_) {
        retro_open_ = false;
        focus_retro_entry_ = true;
        back = false;
    }
    if (back) action = OverlayAction::Resume;
    if (menu_open_ && !retro_open_) {
        // An entry chosen this frame wins over B going back.
        const OverlayAction chosen = draw_pause_menu(info, screen, bg);
        if (chosen != OverlayAction::None) action = chosen;
    }
    // Over the game, not over the menu: the menu goes first.
    const bool choosing = !info.link_choices.empty() && !menu_open_;
    if (choosing) {
        const OverlayAction chosen = draw_link_chooser(info, screen, bg);
        if (chosen != OverlayAction::None) action = chosen;
        else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            action = OverlayAction::ChooseLinkLater;
    }
    chooser_shown_ = choosing;
    if (retro_open_ && menu_open_ && tv_) draw_achievements_tv(info, screen);
    if (retro_open_ && menu_open_ && !tv_) draw_achievements_window(info, screen);
    if(!info.retro_status.empty() && info.hardcore_preferred && info.hardcore_waiting){
        bg->AddText(fonts_.caption,fonts_.caption->FontSize,{16.f,48.f},color::text_muted,
            ellipsize(fonts_.caption,info.retro_status,screen.x-32.f).c_str());
    }
    // RetroAchievements cards use RavenEmu's dedicated top-center style.
    const float retro_bottom=draw_retro(seconds_now(),screen);
    // Local notifications join them at the top center, just below.
    if (!menu_open_)
        draw_toasts(toasts_, fonts_, retro_bottom>0.f?retro_bottom+space::sm:space::lg, true);
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
    return action;
}
} // namespace ravenemudsk_ui
