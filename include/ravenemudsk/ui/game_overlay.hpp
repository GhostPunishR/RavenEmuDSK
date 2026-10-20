// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/theme.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <ravenemudsk/ui/badge_cache.hpp>
#include <ravenemudsk/achievements/notifications.hpp>
#include <ravenemudsk/ui/keyboard_bindings.hpp>
#include <ravenemudsk/ui/pad_glyphs.hpp>
#include <deque>
#include <optional>
#include <utility>
#include <SDL.h>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
struct OverlayInfo {
    KeyboardBindings keyboard;
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
enum class OverlayAction { None, Resume, SaveState, LoadState, Reset, ToggleFullscreen, ToggleHardcore, Quit };

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
    void notify(Toast toast) { toasts_.push_back(std::move(toast)); }
    bool has_toast(Toast::Kind kind) const;
    // Reproduces RavenEmu top-center unlock and measured progress cards.
    void retro_event(const ravenemudsk_ra::Event& event);
    void update_retro_progress(const std::vector<ravenemudsk_ra::Achievement>& entries);
    void clear_retro();
    // Draws the overlay on top of the current frame and returns the menu choice.
    OverlayAction draw(const OverlayInfo& info);

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
    BadgeCache badges_;
    ravenemudsk_ra::Notifications notifications_;
    std::optional<ravenemudsk_ra::Progress> last_progress_;
    bool progress_visible_ = false;
    double progress_started_ = 0.0;
    double progress_hidden_at_ = 0.0;
    void draw_retro(double now_seconds, ImVec2 screen);
};
} // namespace ravenemudsk_ui
