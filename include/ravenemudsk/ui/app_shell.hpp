// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/storage.hpp>
#include <ravenemudsk/ui/keyboard_bindings.hpp>
#include <optional>
#include <string>
struct SDL_Window;
namespace ravenemudsk_ui {
struct LaunchRequest {
    std::string system;
    std::string rom;  // UTF-8 path; native path conversion in the player.
    bool fullscreen = false;  // chosen from TV mode: play in full screen
};
// TV mode keeps one full-screen window from the launcher to the game and back,
// so the desktop never shows in between: each side takes it, renders with its
// own renderer, and keeps it again when it leaves. The video subsystem must
// stay initialized while a window is kept.
SDL_Window* take_screen();             // the kept window, or nullptr
void keep_screen(SDL_Window* window);  // destroys any other kept window
void close_screen();                   // destroys the kept window, if any
// Shows the library until a game is chosen. `error` is displayed on open,
// e.g. when the previous game could not be started.
std::optional<LaunchRequest> run_launcher(const std::string& error = {});
void record_session(const LaunchRequest& selection, unsigned long long seconds);
// Sauvegardes and Jaquettes next to the program, or in the user data
// folder when the program folder is read-only.
Folders app_folders();
bool remember_hardcore(bool enabled);
bool hardcore_preference();
KeyboardBindings keyboard_preferences();
} // namespace ravenemudsk_ui
