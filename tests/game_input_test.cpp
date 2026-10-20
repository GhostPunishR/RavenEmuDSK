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

    // Select + Start, in either order, is the menu chord: the game sees neither.
    {
        std::array<bool, RAVENEMU_R + 1> game{};
        ravenemudsk_ui::GameInput chord([&](int button, bool pressed) { game.at(button) = pressed; });
        auto pad = [&](Uint8 button, bool down, Uint32 at) {
            SDL_Event e{};
            e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            e.cbutton.which = 3;
            e.cbutton.button = button;
            e.cbutton.timestamp = at;
            return chord.process_event(e, 3);
        };
        constexpr Uint8 start = SDL_CONTROLLER_BUTTON_START, select = SDL_CONTROLLER_BUTTON_BACK;
        require(!pad(start, true, 1000), "Start alone is not the chord");
        chord.update(1100);
        require(!game[RAVENEMU_START], "Start reached the game before Select could join it");
        require(pad(select, true, 1120), "Start then Select must open the menu");
        chord.update(1400);
        require(!game[RAVENEMU_START] && !game[RAVENEMU_SELECT], "The chord's buttons reached the game");
        require(!pad(start, false, 1500) && !pad(select, false, 1510), "Releasing the chord is not a chord");
        chord.update(1700);
        require(!game[RAVENEMU_START] && !game[RAVENEMU_SELECT], "Releasing the chord sent a button to the game");

        require(!pad(select, true, 2000) && pad(start, true, 2040), "Select then Start must open the menu");
        pad(select, false, 2100); pad(start, false, 2110);
        chord.update(2400);
        require(!game[RAVENEMU_START] && !game[RAVENEMU_SELECT], "Select then Start reached the game");

        // A Start held on its own reaches the game once the window has passed.
        pad(start, true, 3000);
        chord.update(3000 + ravenemudsk_ui::GameInput::chord_window_ms - 1);
        require(!game[RAVENEMU_START], "Start was sent before the end of the chord window");
        chord.update(3000 + ravenemudsk_ui::GameInput::chord_window_ms);
        require(game[RAVENEMU_START], "A held Start never reached the game");
        // Select joining late still opens the menu and takes Start back.
        require(pad(select, true, 3600) && !game[RAVENEMU_START], "A late chord left Start held in the game");
        pad(start, false, 3700); pad(select, false, 3710);

        // A quick tap is sent on release and kept long enough for a frame.
        pad(start, true, 4000);
        pad(start, false, 4060);
        require(game[RAVENEMU_START], "A quick Start tap was lost");
        chord.update(4060 + ravenemudsk_ui::GameInput::tap_ms - 1);
        require(game[RAVENEMU_START], "A quick tap was released before the game could see it");
        chord.update(4060 + ravenemudsk_ui::GameInput::tap_ms);
        require(!game[RAVENEMU_START], "A quick tap stayed held");
    }

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
