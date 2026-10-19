// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_input.hpp>
#include <ravenemu/bridge.h>
#include <array>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    std::array<bool, RAVENEMU_R + 1> buttons{};
    ravenemudsk_ui::GameInput input([&](int button, bool pressed) { buttons.at(button) = pressed; });
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_z;
    input.process_event(event);
    require(buttons[RAVENEMU_A], "Keyboard press must reach the core");
    input.set_menu_open(true);
    require(!buttons[RAVENEMU_A], "Opening a menu must release held buttons");
    // Even a release from an already-held core state must pass through a menu.
    buttons[RAVENEMU_A] = true;
    event.type = SDL_KEYUP;
    input.process_event(event);
    require(!buttons[RAVENEMU_A], "Keyboard release was lost inside the menu");
    event.type = SDL_KEYDOWN;
    input.process_event(event);
    require(!buttons[RAVENEMU_A], "Menu key presses must not reach the game");
    input.set_menu_open(false);
    require(!buttons[RAVENEMU_A], "Resuming must not restore an obsolete key press");

    event = {};
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.which = 7;
    event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
    input.process_event(event, 9);
    require(!buttons[RAVENEMU_B], "Events from another controller must be ignored");
    input.process_event(event, 7);
    require(buttons[RAVENEMU_B], "Controller press must reach the core");
    input.set_menu_open(true);
    require(!buttons[RAVENEMU_B], "Opening a menu must release controller buttons");
    buttons[RAVENEMU_B] = true;
    event.type = SDL_CONTROLLERBUTTONUP;
    input.process_event(event, 7);
    require(!buttons[RAVENEMU_B], "Controller release was lost inside the menu");
    event.type = SDL_CONTROLLERBUTTONDOWN;
    input.process_event(event, 7);
    require(!buttons[RAVENEMU_B], "Menu controller presses must not reach the game");

    input.set_menu_open(false);
    input.process_event(event, 7);
    event = {};
    event.type = SDL_CONTROLLERDEVICEREMOVED;
    event.cdevice.which = 7;
    input.process_event(event, 7);
    require(!buttons[RAVENEMU_B], "Disconnecting a controller must release its buttons");
    buttons[RAVENEMU_A] = true;
    event = {};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    input.process_event(event);
    require(!buttons[RAVENEMU_A], "Losing focus must release held input");
}
