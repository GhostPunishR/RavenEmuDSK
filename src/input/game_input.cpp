// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/input/game_input.hpp>
#include <ravenemu/bridge.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace ravenemudsk_ui {
GameInput::GameInput(std::function<void(int, bool)> set_button,KeyboardBindings keys,PadBindings pads)
    : set_button_(std::move(set_button)),keys_(std::move(keys)),pads_(pads) {}

void GameInput::release_all() {
    keyboard_held_.fill(false);controller_held_.fill(false);stick_held_.fill(false);
    // Releases may never arrive (focus lost, controller gone): start afresh.
    start_down_ = select_down_ = chord_ = false;
    trigger_down_.fill(false);
    turbo_key_.fill(false);turbo_pad_.fill(false);
    stick_x_ = stick_y_ = 0;stick_pushed_ = false;
    fast_key_ = fast_trigger_ = rewind_key_ = rewind_trigger_ = false;
    pending_ = tap_ = -1;
    for (int button = RAVENEMU_UP; button <= RAVENEMU_R; ++button)
        set_button_(button, false);
}
void GameInput::refresh(int button) {
    const int turbo = button == RAVENEMU_A ? 0 : button == RAVENEMU_B ? 1 : -1;
    const bool pulse = turbo >= 0 && (turbo_key_[turbo] || turbo_pad_[turbo]) && turbo_frame_ < turbo_frames;
    set_button_(button, keyboard_held_[button] || controller_held_[button] || stick_held_[button] || pulse);
}
void GameInput::hold_controller(int button, bool pressed) {
    controller_held_[button] = pressed;
    refresh(button);
}
void GameInput::next_frame() {
    turbo_frame_ = (turbo_frame_ + 1) % (2 * turbo_frames);
    for (const int button : {RAVENEMU_A, RAVENEMU_B})
        if (turbo_key_[button - RAVENEMU_A] || turbo_pad_[button - RAVENEMU_A]) refresh(button);
}
// A turbo button pressed while none was starts with a press, on the next frame.
void GameInput::set_turbo(std::array<bool,2>& source, KeyAction action, bool pressed) {
    if (pressed && menu_open_) return;
    const int which = action == KeyAction::TurboA ? 0 : 1;
    if (pressed && std::none_of(turbo_key_.begin(), turbo_key_.end(), [](bool b) { return b; }) &&
        std::none_of(turbo_pad_.begin(), turbo_pad_.end(), [](bool b) { return b; }))
        turbo_frame_ = 0;
    source[which] = pressed;
    refresh(which == 0 ? RAVENEMU_A : RAVENEMU_B);
}
void GameInput::set_stick_dpad(int dead_zone) {
    stick_zone_ = std::clamp(dead_zone, 0, 100);
    if (!stick_zone_) {
        stick_pushed_ = false;
        for (const int button : {RAVENEMU_UP, RAVENEMU_DOWN, RAVENEMU_LEFT, RAVENEMU_RIGHT})
            if (stick_held_[button]) { stick_held_[button] = false; refresh(button); }
    }
}
// Past the dead zone, the stick presses each side it points within 67.5° of:
// eight sectors of 45°, the diagonals pressing two. It lets go below nine
// tenths of the dead zone, so a stick held on its edge does not flicker.
void GameInput::update_stick() {
    if (!stick_zone_) return;
    const double x = stick_x_, y = stick_y_, length = std::hypot(x, y);
    const double zone = stick_zone_ * 32767.0 / 100.0;
    stick_pushed_ = length > (stick_pushed_ ? .9 * zone : zone);
    const double side = .38268343 * length; // sin 22.5°
    const bool sides[4] = {y < -side, y > side, x < -side, x > side};
    const int buttons[4] = {RAVENEMU_UP, RAVENEMU_DOWN, RAVENEMU_LEFT, RAVENEMU_RIGHT};
    for (int i = 0; i < 4; ++i) {
        const bool held = stick_pushed_ && sides[i] && !menu_open_;
        if (held != stick_held_[buttons[i]]) { stick_held_[buttons[i]] = held; refresh(buttons[i]); }
    }
}
void GameInput::update(Uint32 now) {
    if (pending_ >= 0 && now - pending_since_ >= chord_window_ms) {
        hold_controller(pending_, true);
        pending_ = -1;
    }
    if (tap_ >= 0 && static_cast<Sint32>(now - tap_until_) >= 0) {
        hold_controller(tap_, false);
        tap_ = -1;
    }
}
void GameInput::set_menu_open(bool open) {
    if (open && !menu_open_) release_all();
    menu_open_ = open;
}
bool GameInput::process_event(const SDL_Event& event, SDL_JoystickID controller) {
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        release_all();
    if (event.type == SDL_CONTROLLERDEVICEREMOVED && event.cdevice.which == controller)
        release_all();
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && !event.key.repeat) {
        const bool pressed = event.type == SDL_KEYDOWN;
        const auto action = keys_.action(event.key.keysym.sym);
        if (action == KeyAction::FastForward) fast_key_ = pressed && !menu_open_;
        if (action == KeyAction::Rewind) rewind_key_ = pressed && !menu_open_;
        if (action == KeyAction::TurboA || action == KeyAction::TurboB) set_turbo(turbo_key_, action, pressed);
        const int button = keys_.button(event.key.keysym.sym);
        // Releases must always reach the core, including while a menu is open.
        if (button >= 0 && (!pressed || !menu_open_)) {
            keyboard_held_[button]=pressed;refresh(button);
        }
    }
    if ((event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) &&
        controller >= 0 && event.cbutton.which == controller) {
        const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
        const auto action = pads_.action(event.cbutton.button);
        if (action == KeyAction::TurboA || action == KeyAction::TurboB) {
            set_turbo(turbo_pad_, action, pressed);
            return false;
        }
        return controller_input(pads_.button(event.cbutton.button), pressed, event.cbutton.timestamp);
    }
    if (event.type == SDL_CONTROLLERAXISMOTION && controller >= 0 && event.caxis.which == controller &&
        (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)) {
        (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? stick_x_ : stick_y_) = event.caxis.value;
        update_stick();
        return false;
    }
    // The analog triggers, as buttons: pressed past one level, released below
    // a lower one, so a trigger resting halfway does not flicker.
    if (event.type == SDL_CONTROLLERAXISMOTION && controller >= 0 && event.caxis.which == controller &&
        (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        const bool left = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        bool& down = trigger_down_[left ? 0 : 1];
        const bool now = down ? event.caxis.value > trigger_release : event.caxis.value >= trigger_press;
        if (now == down) return false;
        down = now;
        const int input = left ? pad_left_trigger : pad_right_trigger;
        const auto action = pads_.action(input);
        if (action == KeyAction::Count) (left ? rewind_trigger_ : fast_trigger_) = now && !menu_open_;
        if (action == KeyAction::TurboA || action == KeyAction::TurboB) {
            set_turbo(turbo_pad_, action, now);
            return false;
        }
        return controller_input(pads_.button(input), now, event.caxis.timestamp);
    }
    return false;
}
bool GameInput::controller_input(int button, bool pressed, Uint32 timestamp) {
    if (button == RAVENEMU_START || button == RAVENEMU_SELECT) {
        bool& down = button == RAVENEMU_START ? start_down_ : select_down_;
        const bool other = button == RAVENEMU_START ? select_down_ : start_down_;
        down = pressed;
        if (pressed && other) {
            // Select + Start: the menu. Whatever already reached the game
            // is released; the rest never will.
            chord_ = true;
            pending_ = -1;
            for (const int b : {RAVENEMU_START, RAVENEMU_SELECT})
                if (controller_held_[b] || tap_ == b) hold_controller(b, false);
            tap_ = -1;
            return true;
        }
        if (chord_) {
            if (!start_down_ && !select_down_) chord_ = false;
            return false;
        }
        if (pressed) {
            // Held back: the other half of the chord may follow.
            if (!menu_open_) { pending_ = button; pending_since_ = timestamp; }
            return false;
        }
        if (pending_ == button) {
            pending_ = -1;
            hold_controller(button, true);
            tap_ = button;
            tap_until_ = timestamp + tap_ms;
            return false;
        }
    }
    if (button >= 0 && (!pressed || !menu_open_)) hold_controller(button, pressed);
    return false;
}
} // namespace ravenemudsk_ui
