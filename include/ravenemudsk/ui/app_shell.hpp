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
// TV mode: the side that leaves (launcher or game) keeps its full-screen
// window, cleared to black, as a cover; the next side opens its own window and
// closes the cover once its first frame is up, so the desktop never shows in
// between. SDL must stay initialized meanwhile: entry() holds its subsystems.
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
