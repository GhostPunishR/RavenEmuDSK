// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <array>
#include <functional>
#include <ravenemudsk/input/keyboard_bindings.hpp>
#include <ravenemudsk/input/pad_bindings.hpp>

namespace ravenemudsk_ui {
// Routes gameplay input to the core while keeping menu input separate.
class GameInput {
public:
    explicit GameInput(std::function<void(int, bool)> set_button,KeyboardBindings keys={},PadBindings pads={});
    void set_menu_open(bool open);
    void release_all();
    // True when the controller's Select and Start (as bound) are pressed
    // together, in either order: the menu chord, which the game never receives.
    bool process_event(const SDL_Event& event, SDL_JoystickID controller = -1);
    // Call every frame with SDL_GetTicks(): a lone Select or Start reaches the
    // game once the chord window has passed.
    void update(Uint32 now);
    // Held: its key, or the controller's right trigger when no console button
    // uses it. Never while a menu is open.
    bool fast_forward() const { return !menu_open_ && (fast_key_ || fast_trigger_); }
    // Held: its key, or the left trigger when no console button uses it.
    bool rewind() const { return !menu_open_ && (rewind_key_ || rewind_trigger_); }
    // The controller's left stick as the D-pad, pushed past its dead zone, a
    // share of its travel (0-100 %); 0 leaves the stick to the games it tilts.
    void set_stick_dpad(int dead_zone);
    // After each emulated frame: a held turbo button presses its console
    // button turbo_frames frames, releases it as many, and so on, ten times
    // a second at the console's 60 frames.
    void next_frame();
    static constexpr int turbo_frames = 3;
    // How long a lone Select or Start waits for the other one.
    static constexpr Uint32 chord_window_ms = 150;
    // A quick tap of either, sent when released, stays down this long so the
    // game sees it on at least one frame.
    static constexpr Uint32 tap_ms = 50;
    // An analog trigger counts as pressed past this, released below the second.
    static constexpr Sint16 trigger_press = 16000, trigger_release = 8000;
private:
    // Sends the console button as all its sources hold it: keyboard,
    // controller, stick and turbo.
    void refresh(int button);
    void hold_controller(int button, bool pressed);
    bool controller_input(int button, bool pressed, Uint32 timestamp);
    void set_turbo(std::array<bool,2>& source, KeyAction action, bool pressed);
    void update_stick();
    std::function<void(int, bool)> set_button_;
    bool menu_open_ = false;
    KeyboardBindings keys_;
    PadBindings pads_;
    std::array<bool,2> trigger_down_{};
    bool fast_key_ = false, fast_trigger_ = false, rewind_key_ = false, rewind_trigger_ = false;
    std::array<bool,10> keyboard_held_{},controller_held_{},stick_held_{};
    // Turbo A and B, held on the keyboard and on the controller.
    std::array<bool,2> turbo_key_{},turbo_pad_{};
    int turbo_frame_ = 0;               // 0 to 2 * turbo_frames - 1: pressed in the first half
    int stick_zone_ = 0;                // percent, 0 when the stick is not the D-pad
    Sint16 stick_x_ = 0, stick_y_ = 0;
    bool stick_pushed_ = false;         // past the dead zone
    bool start_down_ = false, select_down_ = false; // on the controller, since release_all
    bool chord_ = false;                            // until both are released
    int pending_ = -1;                              // Select or Start held back
    Uint32 pending_since_ = 0;
    int tap_ = -1;
    Uint32 tap_until_ = 0;
};
} // namespace ravenemudsk_ui
