// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/input/keyboard_bindings.hpp>
#include <array>
namespace ravenemudsk_ui {
class Settings;
// A controller input: an SDL controller button (SDL_GameControllerButton), or
// one of the analog triggers, which SDL reports as axes.
inline constexpr int pad_unbound = -1, pad_left_trigger = 100, pad_right_trigger = 101;
// The controller inputs of the ten console buttons, KeyAction::Up to
// KeyAction::R, and of the two turbo buttons (unbound at first). The in-game
// menu stays the chord of the inputs bound to Select and Start, pressed
// together.
class PadBindings {
public:
    static constexpr int count = 12;
    PadBindings();
    int input(KeyAction action) const;
    // The action bound to the input, or KeyAction::Count when it is free.
    KeyAction action(int input) const;
    // The console button (RAVENEMU_*) the input presses, or -1.
    int button(int input) const;
    // Exchanges with the action that had the input. The Guide button stays
    // the system's (Xbox Game Bar, Steam); pad_unbound frees the action.
    bool assign(KeyAction action, int input);
    void load(const Settings& settings);
    void store(Settings& settings) const;
private:
    std::array<int, count> inputs_{};
};
} // namespace ravenemudsk_ui
