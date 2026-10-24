// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/input/game_input.hpp>
#include <ravenemu/bridge.h>
#include <utility>

namespace ravenemudsk_ui {
GameInput::GameInput(std::function<void(int, bool)> set_button,KeyboardBindings keys,PadBindings pads)
    : set_button_(std::move(set_button)),keys_(std::move(keys)),pads_(pads) {}

void GameInput::release_all() {
    keyboard_held_.fill(false);controller_held_.fill(false);
    // Releases may never arrive (focus lost, controller gone): start afresh.
    start_down_ = select_down_ = chord_ = false;
    trigger_down_.fill(false);
    fast_key_ = fast_trigger_ = rewind_key_ = rewind_trigger_ = false;
    pending_ = tap_ = -1;
    for (int button = RAVENEMU_UP; button <= RAVENEMU_R; ++button)
        set_button_(button, false);
}
void GameInput::hold_controller(int button, bool pressed) {
    controller_held_[button] = pressed;
    set_button_(button, pressed || keyboard_held_[button]);
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
        const int button = keys_.button(event.key.keysym.sym);
        // Releases must always reach the core, including while a menu is open.
        if (button >= 0 && (!pressed || !menu_open_)) {
            keyboard_held_[button]=pressed;set_button_(button,pressed||controller_held_[button]);
        }
    }
    if ((event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) &&
        controller >= 0 && event.cbutton.which == controller)
        return controller_input(pads_.button(event.cbutton.button), event.type == SDL_CONTROLLERBUTTONDOWN,
                                event.cbutton.timestamp);
    // The analog triggers, as buttons: pressed past one level, released below
    // a lower one, so a trigger resting halfway does not flicker.
    if (event.type == SDL_CONTROLLERAXISMOTION && controller >= 0 && event.caxis.which == controller &&
        (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        const bool left = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        bool& down = trigger_down_[left ? 0 : 1];
        const bool now = down ? event.caxis.value > trigger_release : event.caxis.value >= trigger_press;
        if (now == down) return false;
        down = now;
        const int button = pads_.button(left ? pad_left_trigger : pad_right_trigger);
        if (button < 0) (left ? rewind_trigger_ : fast_trigger_) = now && !menu_open_;
        return controller_input(button, now, event.caxis.timestamp);
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
