// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/input/pad_bindings.hpp>
#include <ravenemudsk/settings/settings.hpp>
#include <SDL.h>
namespace ravenemudsk_ui {
namespace {
// Stored next to the keyboard's "key_up"... as "pad_up"...
std::string setting_of(int i) { return std::string("pad_") + (key_commands()[i].setting + 4); }
bool valid(int input) {
    return input == pad_unbound || input == pad_left_trigger || input == pad_right_trigger ||
           (input >= 0 && input < SDL_CONTROLLER_BUTTON_MAX && input != SDL_CONTROLLER_BUTTON_GUIDE);
}
} // namespace
PadBindings::PadBindings()
    : inputs_{SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
              SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
              SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
              SDL_CONTROLLER_BUTTON_RIGHTSHOULDER} {}
int PadBindings::input(KeyAction action) const {
    const auto i = static_cast<std::size_t>(action);
    return i < inputs_.size() ? inputs_[i] : pad_unbound;
}
int PadBindings::button(int input) const {
    if (input == pad_unbound) return -1;
    for (std::size_t i = 0; i < inputs_.size(); ++i)
        if (inputs_[i] == input) return key_commands()[i].button;
    return -1;
}
bool PadBindings::assign(KeyAction action, int input) {
    const auto i = static_cast<std::size_t>(action);
    if (i >= inputs_.size() || !valid(input)) return false;
    // Select and Start are the menu's chord: never left unbound.
    const auto menu = [](std::size_t a) {
        return a == static_cast<std::size_t>(KeyAction::Select) || a == static_cast<std::size_t>(KeyAction::Start);
    };
    if (input == pad_unbound && menu(i)) return false;
    if (input != pad_unbound)
        for (std::size_t j = 0; j < inputs_.size(); ++j)
            if (j != i && inputs_[j] == input) {
                // Nor by taking their input from an unbound action.
                if (inputs_[i] == pad_unbound && menu(j)) return false;
                inputs_[j] = inputs_[i];
                break;
            }
    inputs_[i] = input;
    return true;
}
void PadBindings::load(const Settings& settings) {
    *this = PadBindings{};
    for (int i = 0; i < count; ++i)
        if (!settings.get(setting_of(i)).empty())
            assign(static_cast<KeyAction>(i), settings.number(setting_of(i), inputs_[static_cast<std::size_t>(i)]));
}
void PadBindings::store(Settings& settings) const {
    for (int i = 0; i < count; ++i) settings.set(setting_of(i), std::to_string(inputs_[static_cast<std::size_t>(i)]));
}
} // namespace ravenemudsk_ui
