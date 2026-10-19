// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/storage.hpp>
#include <optional>
#include <string>
namespace ravenemudsk_ui {
struct LaunchRequest {
    std::string system;
    std::string rom;  // UTF-8 path; native path conversion in the player.
    bool fullscreen = false;  // chosen from TV mode: play in full screen
};
// Shows the library until a game is chosen. `error` is displayed on open,
// e.g. when the previous game could not be started.
std::optional<LaunchRequest> run_launcher(const std::string& error = {});
void record_session(const LaunchRequest& selection, unsigned long long seconds);
// Sauvegardes and Jaquettes next to the program, or in the user data
// folder when the program folder is read-only.
Folders app_folders();
bool remember_hardcore(bool enabled);
bool hardcore_preference();
} // namespace ravenemudsk_ui
