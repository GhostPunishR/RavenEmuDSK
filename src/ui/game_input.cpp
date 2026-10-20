// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_input.hpp>
#include <ravenemu/bridge.h>
#include <utility>

namespace ravenemudsk_ui {
namespace {
int pad_of(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return RAVENEMU_A;
    case SDL_CONTROLLER_BUTTON_B: return RAVENEMU_B;
    case SDL_CONTROLLER_BUTTON_START: return RAVENEMU_START;
    case SDL_CONTROLLER_BUTTON_BACK: return RAVENEMU_SELECT;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return RAVENEMU_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return RAVENEMU_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return RAVENEMU_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return RAVENEMU_RIGHT;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return RAVENEMU_L;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return RAVENEMU_R;
    default: return -1;
    }
}
}
GameInput::GameInput(std::function<void(int, bool)> set_button,KeyboardBindings keys)
    : set_button_(std::move(set_button)),keys_(std::move(keys)) {}

void GameInput::release_all() {
    keyboard_held_.fill(false);controller_held_.fill(false);
    // Releases may never arrive (focus lost, controller gone): start afresh.
    start_down_ = select_down_ = chord_ = false;
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
        const int button = keys_.button(event.key.keysym.sym);
        // Releases must always reach the core, including while a menu is open.
        if (button >= 0 && (!pressed || !menu_open_)) {
            keyboard_held_[button]=pressed;set_button_(button,pressed||controller_held_[button]);
        }
    }
    if ((event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) &&
        controller >= 0 && event.cbutton.which == controller) {
        const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
        const int button = pad_of(event.cbutton.button);
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
                if (!menu_open_) { pending_ = button; pending_since_ = event.cbutton.timestamp; }
                return false;
            }
            if (pending_ == button) {
                pending_ = -1;
                hold_controller(button, true);
                tap_ = button;
                tap_until_ = event.cbutton.timestamp + tap_ms;
                return false;
            }
        }
        if (button >= 0 && (!pressed || !menu_open_)) hold_controller(button, pressed);
    }
    return false;
}
} // namespace ravenemudsk_ui
