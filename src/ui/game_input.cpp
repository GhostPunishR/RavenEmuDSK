// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_input.hpp>
#include <ravenemu/bridge.h>
#include <utility>

namespace ravenemudsk_ui {
namespace {
int key_of(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return RAVENEMU_UP;
    case SDLK_DOWN: return RAVENEMU_DOWN;
    case SDLK_LEFT: return RAVENEMU_LEFT;
    case SDLK_RIGHT: return RAVENEMU_RIGHT;
    case SDLK_z: return RAVENEMU_A;
    case SDLK_x: return RAVENEMU_B;
    case SDLK_RETURN: return RAVENEMU_START;
    case SDLK_BACKSPACE: return RAVENEMU_SELECT;
    case SDLK_a: return RAVENEMU_L;
    case SDLK_s: return RAVENEMU_R;
    default: return -1;
    }
}
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
GameInput::GameInput(std::function<void(int, bool)> set_button)
    : set_button_(std::move(set_button)) {}

void GameInput::release_all() {
    for (int button = RAVENEMU_UP; button <= RAVENEMU_R; ++button)
        set_button_(button, false);
}
void GameInput::set_menu_open(bool open) {
    if (open && !menu_open_) release_all();
    menu_open_ = open;
}
void GameInput::process_event(const SDL_Event& event, SDL_JoystickID controller) {
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        release_all();
    if (event.type == SDL_CONTROLLERDEVICEREMOVED && event.cdevice.which == controller)
        release_all();
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && !event.key.repeat) {
        const bool pressed = event.type == SDL_KEYDOWN;
        const int button = key_of(event.key.keysym.sym);
        // Releases must always reach the core, including while a menu is open.
        if (button >= 0 && (!pressed || !menu_open_)) set_button_(button, pressed);
    }
    if ((event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) &&
        controller >= 0 && event.cbutton.which == controller) {
        const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
        const int button = pad_of(event.cbutton.button);
        if (button >= 0 && (!pressed || !menu_open_)) set_button_(button, pressed);
    }
}
} // namespace ravenemudsk_ui
