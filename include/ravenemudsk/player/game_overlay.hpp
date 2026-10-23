// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/theme.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <ravenemudsk/achievements/badge_cache.hpp>
#include <ravenemudsk/achievements/notifications.hpp>
#include <ravenemudsk/input/keyboard_bindings.hpp>
#include <ravenemudsk/input/pad_bindings.hpp>
#include <ravenemudsk/input/pad_glyphs.hpp>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <SDL.h>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
// A device on the local network ready for the wireless link cable.
struct LinkChoiceItem {
    std::uint64_t id = 0;
    std::string label;  // "Pixel 7 · 4F2A"
};
struct OverlayInfo {
    KeyboardBindings keyboard;
    PadBindings pads;           // the menu chord is the bound Select + Start
    std::string game_title;
    std::string system;
    bool hardcore = false;
    bool hardcore_preferred = false;
    bool hardcore_waiting = false;
    std::string retro_status;
    bool paused = false;          // quick pause with P
    bool fullscreen = false;
    bool has_state = false;       // a save state exists for "Charger l'état"
    bool has_achievement = false;
    std::string achievement_title;
    int achievement_points = 0;
    bool achievement_unlocked = false;
    bool demo = false;            // bundled demo ROM: show the guided panel
    std::optional<PadFamily> pad; // controller in use: hints show its buttons
    // Wireless link cable.
    std::string link_partner;          // "Pixel 7 (Pokémon Bleu)" while linked, empty otherwise
    bool link_partner_paused = false;  // the other game's menu is open: this one waits
    std::vector<LinkChoiceItem> link_choices; // several devices wait: the player picks one
    std::string link_own_label;        // this computer, as the others list it
};
// Top-center RetroAchievements cards (unlocks, progress) and the challenge
// list: desktop sizes, or larger ones read from a couch in TV mode, inside a
// 5 % safe area since televisions may crop the picture's edges.
struct RetroCardStyle {
    float top = 10.f, bottom = 10.f; // Margins from the screen edges.
    float height = 60.f, badge = 40.f, pad = 14.f, gap = 8.f, line = 19.f, radius = 12.f;
    float max_width = 520.f;
    bool tv = false;
};
RetroCardStyle retro_card_style(ImVec2 screen, bool tv);
// Unlock banner, top center: a badge square that drops in, then opens into a
// band with the heading and the title beside the badge, centered all along;
// the band folds back into the badge, which rises away. TV mode sizes it for a
// couch, inside the safe area; the desktop keeps it compact.
struct UnlockBannerStyle {
    float margin = 54.f;     // From the top edge (5 % safe area on TV); the sides keep it too.
    float size = 118.f;      // Badge square, also the band's height.
    float pad = 22.f;        // Text inset inside the band.
    float radius = 14.f;
    float max_width = 1100.f;
};
UnlockBannerStyle unlock_banner_style(ImVec2 screen, bool tv = true);
// `drop`: 0 above the screen, 1 in place. `unfold`: 0 badge alone, 1 whole band.
struct UnlockBannerFrame { float drop = 0.f, unfold = 0.f; };
UnlockBannerFrame unlock_banner_frame(double elapsed_seconds);
// ChooseLink: the device is chosen_link(); ChooseLinkLater: none of them.
enum class OverlayAction { None, Resume, SaveState, LoadState, Reset, ToggleFullscreen, ToggleHardcore, Quit,
                           UnplugLink, ChooseLink, ChooseLinkLater };

// Dear ImGui layer drawn over the game: pause menu, toasts and hints.
// Owns the ImGui context for the lifetime of a game session.
class GameOverlay {
public:
    // `tv`: the game was started from TV mode; its cards use the TV sizes.
    GameOverlay(SDL_Window* window, SDL_Renderer* renderer,ravenemudsk_ra::Client* retro=nullptr, bool tv=false);
    ~GameOverlay();
    GameOverlay(const GameOverlay&) = delete;
    GameOverlay& operator=(const GameOverlay&) = delete;

    void process_event(const SDL_Event& event);
    // True while the overlay uses the mouse, so clicks must not reach the game.
    bool wants_mouse() const;
    bool menu_open() const { return menu_open_; }
    // `from_pad`: opened with the controller, so keyboard shortcuts are not listed.
    void open_menu(bool from_pad = false);
    void close_menu();
    // Opens "Succès et classements" over the menu, as its entry does.
    void open_achievements();
    bool achievements_open() const { return retro_open_; }
    void notify(Toast toast) { toasts_.push_back(std::move(toast)); }
    bool has_toast(Toast::Kind kind) const;
    // Reproduces RavenEmu top-center unlock and measured progress cards.
    void retro_event(const ravenemudsk_ra::Event& event);
    void update_retro_progress(const std::vector<ravenemudsk_ra::Achievement>& entries);
    void clear_retro();
    // True once when an unlock or a completed game appears on screen: the
    // moment its chime plays.
    bool take_unlock_sound() { return std::exchange(unlock_sound_, false); }
    // Draws the overlay on top of the current frame and returns the menu choice.
    OverlayAction draw(const OverlayInfo& info);
    // The device picked in "Relier le câble à…", with OverlayAction::ChooseLink.
    std::uint64_t chosen_link() const { return chosen_link_; }

private:
    SDL_Renderer* renderer_;
    ravenemudsk_ra::Client* retro_;
    bool tv_;
    Fonts fonts_;
    std::vector<Toast> toasts_;
    bool menu_open_ = false;
    bool pad_menu_ = false;
    bool focus_first_ = false;
    bool retro_open_ = false;
    bool focus_retro_entry_ = false; // back from the achievements: their entry keeps the focus
    bool focus_achievement_ = false; // achievements just opened: the first row takes the focus
    int retro_tab_ = 0;              // TV: tab shown, switched with LB / RB
    bool retro_tab_requested_ = false;
    BadgeCache badges_;
    ravenemudsk_ra::Notifications notifications_;
    std::optional<ravenemudsk_ra::Progress> last_progress_;
    bool progress_visible_ = false;
    double progress_started_ = 0.0;
    double progress_hidden_at_ = 0.0;
    double shown_notice_since_ = -1.0;
    bool unlock_sound_ = false;
    bool chooser_shown_ = false;     // "Relier le câble à…" was up last frame
    std::uint64_t chosen_link_ = 0;
    // Returns the lowest edge it drew, 0 when nothing; toasts go below it.
    float draw_retro(double now_seconds, ImVec2 screen);
    void draw_achievements_tv(const OverlayInfo& info, ImVec2 screen);
    // The parts of draw(), in drawing order.
    void draw_status(const OverlayInfo& info, ImDrawList* bg);
    void draw_start_hint(const OverlayInfo& info, ImVec2 screen, ImDrawList* bg, float time);
    OverlayAction draw_pause_menu(const OverlayInfo& info, ImVec2 screen, ImDrawList* bg);
    OverlayAction draw_link_chooser(const OverlayInfo& info, ImVec2 screen, ImDrawList* bg);
    void draw_achievements_window(const OverlayInfo& info, ImVec2 screen);
};
} // namespace ravenemudsk_ui
