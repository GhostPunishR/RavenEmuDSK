// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
class Settings;
// The machine's screens and audio outputs, and SDL's drawing engine, as
// Réglages > Affichage and Son choose them. Shared by the launcher and the game.

// The screens' names, in SDL's order (0 is the main one).
std::vector<std::string> display_names();
// The screen chosen for full screen and TV mode (setting "display"), while it
// is connected; 0, the main one, otherwise.
int chosen_display(const Settings& settings);
// Moves `window` to the middle of `display`, before it goes full screen there.
void move_to_display(SDL_Window* window, int display);

// A drawing engine SDL offers here: its SDL name and what the player reads.
struct RenderDriver {
    std::string name;
    std::string label;
};
// "auto" first, then the engines of this build of SDL.
std::vector<RenderDriver> render_drivers();
// What "auto" means: Direct3D 11 on Windows, where SDL 2 would take Direct3D 9,
// whose pictures go through a copy of the desktop; SDL's choice elsewhere.
std::string automatic_render_driver();
// Before the first renderer: the engine of setting "render_driver". SDL falls
// back to another one when it fails.
void choose_render_driver(const Settings& settings);
// Direct3D 11: at most one picture queued ahead in the driver, instead of
// three, which can delay the picture by two refreshes. Windows only.
void limit_frame_queue(SDL_Renderer* renderer);

// The audio outputs SDL sees (the audio subsystem started).
std::vector<std::string> audio_outputs();
} // namespace ravenemudsk_ui
