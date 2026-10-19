// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <functional>

namespace ravenemudsk_ui {
// Routes gameplay input to the core while keeping menu input separate.
class GameInput {
public:
    explicit GameInput(std::function<void(int, bool)> set_button);
    void set_menu_open(bool open);
    void release_all();
    void process_event(const SDL_Event& event, SDL_JoystickID controller = -1);
private:
    std::function<void(int, bool)> set_button_;
    bool menu_open_ = false;
};
} // namespace ravenemudsk_ui
