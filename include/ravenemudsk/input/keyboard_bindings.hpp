// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <array>
#include <string>
namespace ravenemudsk_ui {
class Settings;
enum class KeyAction { Up, Down, Left, Right, A, B, Start, Select, L, R,
                       Menu, Pause, SaveState, LoadState, Reset, Fullscreen, FastForward, Count };
struct KeyCommand {
    const char* setting;
    const char* label;
    const char* controller;
    SDL_Keycode default_key;
    int button;
};
const std::array<KeyCommand,static_cast<int>(KeyAction::Count)>& key_commands();
class KeyboardBindings {
public:
    KeyboardBindings();
    SDL_Keycode key(KeyAction action) const;
    std::string label(KeyAction action) const;
    KeyAction action(SDL_Keycode key) const;
    int button(SDL_Keycode key) const;
    // Exchanges existing assignments; Escape always remains a menu/cancel key.
    bool assign(KeyAction action,SDL_Keycode key);
    void load(const Settings& settings);
    void store(Settings& settings) const;
private:
    std::array<SDL_Keycode,static_cast<int>(KeyAction::Count)> keys_{};
};
}
