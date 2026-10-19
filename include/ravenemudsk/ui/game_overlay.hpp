// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/theme.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <ravenemudsk/ui/badge_cache.hpp>
#include <ravenemudsk/achievements/notifications.hpp>
#include <deque>
#include <optional>
#include <utility>
#include <SDL.h>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
struct OverlayInfo {
    std::string game_title;
    std::string system;
    bool hardcore = false;
    bool paused = false;          // quick pause with P
    bool fullscreen = false;
    bool has_state = false;       // a save state exists for "Charger l'état"
    bool has_achievement = false;
    std::string achievement_title;
    int achievement_points = 0;
    bool achievement_unlocked = false;
    bool demo = false;            // bundled demo ROM: show the guided panel
};
enum class OverlayAction { None, Resume, SaveState, LoadState, Reset, ToggleFullscreen, Quit };

// Dear ImGui layer drawn over the game: pause menu, toasts and hints.
// Owns the ImGui context for the lifetime of a game session.
class GameOverlay {
public:
    GameOverlay(SDL_Window* window, SDL_Renderer* renderer);
    ~GameOverlay();
    GameOverlay(const GameOverlay&) = delete;
    GameOverlay& operator=(const GameOverlay&) = delete;

    void process_event(const SDL_Event& event);
    // True while the overlay uses the mouse, so clicks must not reach the game.
    bool wants_mouse() const;
    bool menu_open() const { return menu_open_; }
    void open_menu();
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
    Fonts fonts_;
    std::vector<Toast> toasts_;
    bool menu_open_ = false;
    bool focus_first_ = false;
    BadgeCache badges_;
    ravenemudsk_ra::Notifications notifications_;
    std::optional<ravenemudsk_ra::Progress> last_progress_;
    bool progress_visible_ = false;
    double progress_started_ = 0.0;
    double progress_hidden_at_ = 0.0;
    void draw_retro(double now_seconds, ImVec2 screen);
};
} // namespace ravenemudsk_ui
