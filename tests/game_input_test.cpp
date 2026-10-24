// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/input/game_input.hpp>
#include <ravenemu/bridge.h>
#include <algorithm>
#include <array>
#include <ravenemudsk/settings/settings.hpp>
#include <ravenemudsk/settings/game_preferences.hpp>
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

    // Controller buttons: defaults, exchanges, the reserved Guide button, and
    // Select and Start, the menu's chord, never left unbound.
    PadBindings pads;
    require(pads.input(KeyAction::A)==SDL_CONTROLLER_BUTTON_A&&pads.button(SDL_CONTROLLER_BUTTON_BACK)==RAVENEMU_SELECT&&
            pads.button(SDL_CONTROLLER_BUTTON_X)==-1&&pads.button(pad_left_trigger)==-1,"Controller defaults");
    require(pads.assign(KeyAction::A,SDL_CONTROLLER_BUTTON_X)&&pads.button(SDL_CONTROLLER_BUTTON_X)==RAVENEMU_A&&
            pads.button(SDL_CONTROLLER_BUTTON_A)==-1,"Moving A to X");
    require(pads.assign(KeyAction::B,SDL_CONTROLLER_BUTTON_X)&&pads.input(KeyAction::A)==SDL_CONTROLLER_BUTTON_B,
        "A button taken by another action must be exchanged");
    require(pads.assign(KeyAction::L,pad_left_trigger)&&pads.button(pad_left_trigger)==RAVENEMU_L,"Trigger binding");
    require(!pads.assign(KeyAction::A,SDL_CONTROLLER_BUTTON_GUIDE),"The Guide button stays the system's");
    require(!pads.assign(KeyAction::A,SDL_CONTROLLER_BUTTON_MAX)&&!pads.assign(KeyAction::A,99),"Unknown inputs refused");
    require(!pads.assign(KeyAction::Start,pad_unbound)&&!pads.assign(KeyAction::Select,pad_unbound),
        "Select and Start must stay bound");
    require(pads.assign(KeyAction::R,pad_unbound)&&pads.input(KeyAction::R)==pad_unbound,"Unbinding R");
    require(!pads.assign(KeyAction::R,SDL_CONTROLLER_BUTTON_BACK)&&pads.input(KeyAction::Select)==SDL_CONTROLLER_BUTTON_BACK,
        "An unbound action must not leave Select unbound by taking its button");
    require(!pads.assign(KeyAction::Menu,SDL_CONTROLLER_BUTTON_Y),"Only the console buttons are bound on the controller");
    require(pads.assign(KeyAction::Select,SDL_CONTROLLER_BUTTON_Y)&&pads.assign(KeyAction::Start,pad_right_trigger),
        "Moving the menu's chord");
    pads.store(settings);settings.save();
    Settings again(file);again.load();PadBindings kept;kept.load(again);
    for(int i=0;i<PadBindings::count;++i)
        require(kept.input(static_cast<KeyAction>(i))==pads.input(static_cast<KeyAction>(i)),"Restart lost a controller binding");
    require(again.get("pseudo")=="Keep my profile"&&again.get("key_a")==settings.get("key_a"),
        "Saving controller bindings changed other settings");
    again.set("pad_start","-1");again.set("pad_a","5");again.set("pad_b","nonsense");kept.load(again);
    require(kept.input(KeyAction::Start)==SDL_CONTROLLER_BUTTON_START&&kept.input(KeyAction::A)==SDL_CONTROLLER_BUTTON_A&&
            kept.input(KeyAction::B)==SDL_CONTROLLER_BUTTON_B,"Invalid saved controller bindings fall back to the defaults");

    // In game: the bound buttons reach the core, triggers press past their
    // threshold, and the menu is the bound Select + Start.
    std::array<bool, RAVENEMU_R + 1> pad_game{};
    GameInput remapped([&](int button,bool pressed){pad_game.at(button)=pressed;},KeyboardBindings{},pads);
    auto button=[&](Uint8 b,bool down,Uint32 at){
        SDL_Event e{};e.type=down?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;e.cbutton.which=3;e.cbutton.button=b;
        e.cbutton.timestamp=at;return remapped.process_event(e,3);
    };
    auto trigger=[&](bool left,Sint16 value,Uint32 at){
        SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.which=3;e.caxis.value=value;e.caxis.timestamp=at;
        e.caxis.axis=left?SDL_CONTROLLER_AXIS_TRIGGERLEFT:SDL_CONTROLLER_AXIS_TRIGGERRIGHT;return remapped.process_event(e,3);
    };
    button(SDL_CONTROLLER_BUTTON_X,true,10);
    require(pad_game[RAVENEMU_B]&&!pad_game[RAVENEMU_A],"A remapped button must press its console button");
    button(SDL_CONTROLLER_BUTTON_X,false,20);require(!pad_game[RAVENEMU_B],"Its release too");
    button(SDL_CONTROLLER_BUTTON_A,true,30);require(!pad_game[RAVENEMU_A],"A button bound elsewhere no longer presses A");
    button(SDL_CONTROLLER_BUTTON_A,false,40);
    trigger(true,GameInput::trigger_press-1,50);require(!pad_game[RAVENEMU_L],"Below the threshold, a trigger is not pressed");
    trigger(true,GameInput::trigger_press,60);require(pad_game[RAVENEMU_L],"Past the threshold, it presses L");
    trigger(true,(GameInput::trigger_press+GameInput::trigger_release)/2,70);require(pad_game[RAVENEMU_L],"No flicker halfway back");
    trigger(true,GameInput::trigger_release,80);require(!pad_game[RAVENEMU_L],"Released below the lower level");
    require(!button(SDL_CONTROLLER_BUTTON_Y,true,100),"Select alone waits for Start");
    require(trigger(false,32767,120),"The bound Select + Start opens the menu");
    require(!pad_game[RAVENEMU_SELECT]&&!pad_game[RAVENEMU_START],"The chord never reaches the game");
    button(SDL_CONTROLLER_BUTTON_BACK,true,130);require(!pad_game[RAVENEMU_SELECT],"The old Select no longer presses Select");

    // Fast forward: held on its key (Space by default), or on the right trigger
    // while no console button uses it; never through a menu.
    {
        std::array<bool, RAVENEMU_R + 1> held{};
        GameInput fast([&](int button, bool pressed) { held.at(button) = pressed; });
        SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_SPACE;
        fast.process_event(key);require(fast.fast_forward(),"Space held: fast forward");
        key.type=SDL_KEYUP;fast.process_event(key);require(!fast.fast_forward(),"Space released: normal speed");
        SDL_Event axis{};axis.type=SDL_CONTROLLERAXISMOTION;axis.caxis.which=3;
        axis.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;axis.caxis.value=32767;
        fast.process_event(axis,3);require(fast.fast_forward(),"Right trigger held: fast forward");
        require(std::none_of(held.begin(),held.end(),[](bool b){return b;}),"The free trigger presses no console button");
        fast.set_menu_open(true);require(!fast.fast_forward(),"Never while a menu is open");
        fast.set_menu_open(false);require(!fast.fast_forward(),"Released by the menu, until the trigger moves again");
        fast.process_event(axis,3);require(fast.fast_forward(),"Pressed again: fast forward");
        // Rewind: R, or the left trigger while free.
        SDL_Event r{};r.type=SDL_KEYDOWN;r.key.keysym.sym=SDLK_r;
        fast.process_event(r);require(fast.rewind(),"R held: rewind");
        r.type=SDL_KEYUP;fast.process_event(r);require(!fast.rewind(),"R released");
        SDL_Event left=axis;left.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERLEFT;left.caxis.value=32767;
        fast.process_event(left,3);require(fast.rewind()&&fast.fast_forward(),"Left trigger: rewind, apart from the right one");
        left.caxis.value=0;fast.process_event(left,3);require(!fast.rewind(),"Left trigger released");
        PadBindings taken;require(taken.assign(KeyAction::R,pad_right_trigger),"Right trigger bound to R");
        GameInput bound([&](int button, bool pressed) { held.at(button) = pressed; },KeyboardBindings{},taken);
        bound.process_event(axis,3);
        require(!bound.fast_forward()&&held[RAVENEMU_R],"A trigger bound to R presses R, not fast forward");
    }

    // A game's preferences, read once: defaults, then each setting.
    Settings fresh;
    const auto defaults=game_preferences(fresh);
    require(!defaults.hardcore&&defaults.unlock_sound&&defaults.volume==100&&!defaults.smooth_image&&defaults.wireless_link&&
            defaults.pause_unfocused&&defaults.rumble&&defaults.motion_sensors&&defaults.solar_light==50&&
            !defaults.integer_scale&&!defaults.lcd_colors&&defaults.frame_blending==0&&defaults.rewind&&
            !defaults.gb_in_color&&defaults.gb_colors==0&&!defaults.gbc_on_gba&&defaults.gba_bios==0,
            "Default game preferences");
    require(defaults.keyboard.key(KeyAction::A)==KeyboardBindings{}.key(KeyAction::A)&&
            defaults.pads.input(KeyAction::A)==SDL_CONTROLLER_BUTTON_A,"Default bindings");
    Settings chosen;
    chosen.set_flag("ra_hardcore",true);chosen.set_flag("ra_unlock_sound",false);chosen.set("volume","35");
    chosen.set_flag("smooth_image",true);chosen.set("pad_a",std::to_string(SDL_CONTROLLER_BUTTON_X));
    chosen.set_flag("wireless_link",false);chosen.set_flag("pause_unfocused",false);
    chosen.set_flag("rumble",false);chosen.set_flag("motion_sensors",false);chosen.set("solar_light","80");
    chosen.set_flag("integer_scale",true);chosen.set_flag("lcd_colors",true);chosen.set("frame_blending","2");
    chosen.set_flag("rewind",false);chosen.set_flag("gb_in_color",true);chosen.set("gb_colors","13");
    chosen.set_flag("gbc_on_gba",true);chosen.set("gba_bios","2");
    const auto prefs=game_preferences(chosen);
    require(prefs.hardcore&&!prefs.unlock_sound&&prefs.volume==35&&prefs.smooth_image&&!prefs.wireless_link&&
            !prefs.pause_unfocused&&!prefs.rumble&&!prefs.motion_sensors&&prefs.solar_light==80&&prefs.integer_scale&&
            prefs.lcd_colors&&prefs.frame_blending==2&&!prefs.rewind&&prefs.gb_in_color&&prefs.gb_colors==13&&
            prefs.gbc_on_gba&&prefs.gba_bios==2,"Chosen game preferences");
    chosen.set("gb_colors","40");chosen.set("gba_bios","-1");
    require(game_preferences(chosen).gb_colors==13&&game_preferences(chosen).gba_bios==0,"Out-of-range choices are clamped");
    chosen.set("frame_blending","7");require(game_preferences(chosen).frame_blending==2,"An unknown blending is the last one");
    chosen.set("solar_light","-3");require(game_preferences(chosen).solar_light==0,"Sunlight below 0 % is darkness");
    require(prefs.pads.input(KeyAction::A)==SDL_CONTROLLER_BUTTON_X,"Chosen controller bindings");
    chosen.set("volume","250");require(game_preferences(chosen).volume==100,"Volume above 100 % is full volume");
    chosen.set_flag("mute",true);require(game_preferences(chosen).volume==0,"Muted means silent, whatever the volume");
}
