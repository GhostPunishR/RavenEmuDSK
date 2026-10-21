// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/storage.hpp>
#include <ravenemudsk/ui/keyboard_bindings.hpp>
#include <optional>
#include <string>
struct SDL_Window;
struct SDL_Renderer;
namespace ravenemudsk_ui {
struct LaunchRequest {
    std::string system;
    std::string rom;  // UTF-8 path; native path conversion in the player.
    bool fullscreen = false;  // chosen from TV mode: play in full screen
};
// TV mode: the launcher and the game draw in one full-screen window with one
// renderer. The side that leaves clears it to black and keeps it; the next
// side takes it over. No window opens or closes in between, so the desktop
// never shows. SDL must stay initialized meanwhile: entry() holds it.
struct SharedScreen {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
};
void keep_screen(SharedScreen screen);  // destroys any other kept screen
SharedScreen take_screen();             // the kept screen, now the caller's; empty if none
void close_screen();                    // destroys the kept screen, if any
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
