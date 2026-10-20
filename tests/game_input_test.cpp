// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_input.hpp>
#include <ravenemu/bridge.h>
#include <array>
#include <ravenemudsk/ui/settings.hpp>
#include <chrono>
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

    using namespace ravenemudsk_ui;
    const auto file=std::filesystem::temp_directory_path()/("raven-keys-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
    struct Cleanup{std::filesystem::path file;~Cleanup(){std::error_code ec;std::filesystem::remove(file,ec);}} cleanup{file};
    Settings settings(file);settings.set("pseudo","Keep my profile");
    KeyboardBindings keys;
    require(keys.assign(KeyAction::Up,SDLK_z)&&keys.key(KeyAction::A)==SDLK_UP,
        "Conflicting keys must be exchanged without assigning two actions to one key");
    require(keys.assign(KeyAction::A,SDLK_c)&&keys.assign(KeyAction::LoadState,SDLK_v),"Could not customize game/shortcut keys");
    require(keys.assign(KeyAction::Select,SDLK_UNKNOWN),"Could not unassign a key");
    require(!keys.assign(KeyAction::A,SDLK_ESCAPE),"Escape must remain reserved for the menu");
    require(!keys.assign(KeyAction::A,1),"Unreachable control codes must not be accepted as key bindings");
    require(keys.assign(KeyAction::Menu,SDLK_m)&&keys.action(SDLK_ESCAPE)==KeyAction::Menu,
        "Custom menu binding must preserve the Escape fallback");
    keys.store(settings);settings.save();
    Settings restarted(file);restarted.load();KeyboardBindings restored;restored.load(restarted);
    for(int i=0;i<static_cast<int>(KeyAction::Count);++i)
        require(restored.key(static_cast<KeyAction>(i))==keys.key(static_cast<KeyAction>(i)),"Restart lost a custom key assignment");
    require(restarted.get("pseudo")=="Keep my profile","Saving keys changed unrelated settings");
    require(restored.action(SDLK_v)==KeyAction::LoadState&&restored.button(SDLK_v)==-1,
        "Remapped shortcuts must route to emulator actions, not console buttons");
    GameInput custom([&](int button,bool pressed){buttons.at(button)=pressed;},restored);
    event={};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_c;custom.process_event(event);
    require(buttons[RAVENEMU_A],"Custom gameplay key did not reach the core");
    event.type=SDL_KEYUP;custom.process_event(event);require(!buttons[RAVENEMU_A],"Custom key release was lost");
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_z;custom.process_event(event);
    require(buttons[RAVENEMU_UP]&&!buttons[RAVENEMU_A],"Old A key did not become its remapped direction");
    custom.release_all();event.key.keysym.sym=SDLK_BACKSPACE;custom.process_event(event);
    require(!buttons[RAVENEMU_SELECT],"Unassigned keyboard button still reached the core");
    // A keyboard release must not cancel a still-held controller button.
    event={};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.which=7;event.cbutton.button=SDL_CONTROLLER_BUTTON_A;
    custom.process_event(event,7);
    event={};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_c;custom.process_event(event,7);
    event.type=SDL_KEYUP;custom.process_event(event,7);require(buttons[RAVENEMU_A],"Keyboard release cancelled the held controller input");
    event={};event.type=SDL_CONTROLLERBUTTONUP;event.cbutton.which=7;event.cbutton.button=SDL_CONTROLLER_BUTTON_A;
    custom.process_event(event,7);require(!buttons[RAVENEMU_A],"Final controller release left the button stuck");
    settings.set("key_a","-15");settings.set("key_load","invalid");settings.set("key_menu","2147483647");
    restored.load(settings);
    require(restored.key(KeyAction::A)!=-15&&restored.key(KeyAction::LoadState)==SDLK_F9&&restored.key(KeyAction::Menu)==SDLK_ESCAPE,
        "Invalid saved bindings must fall back safely");
    KeyboardBindings{}.store(restarted);restarted.save();settings.load();restored.load(settings);
    require(restored.key(KeyAction::A)==SDLK_z&&restored.key(KeyAction::LoadState)==SDLK_F9&&settings.get("pseudo")=="Keep my profile",
        "Restoring defaults lost bindings or changed unrelated settings");
}
