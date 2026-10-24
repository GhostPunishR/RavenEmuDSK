// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/input/keyboard_bindings.hpp>
#include <ravenemudsk/input/pad_bindings.hpp>
namespace ravenemudsk_ui {
class Settings;
// What a game reads from the settings when it starts, in one read.
struct GamePreferences {
    bool hardcore = false;      // RetroAchievements Hardcore, as last chosen
    bool unlock_sound = true;   // "Son des succès": on unless turned off
    int volume = 100;           // Réglages > Son: 0-100, 0 when the sound is cut
    bool smooth_image = false;  // Réglages > Affichage: smooth scaling instead of sharp pixels
    bool pause_unfocused = true; // Réglages > Affichage: pause while the game's window is in the background
    bool wireless_link = true;  // Réglages > Câble link: link with the devices of the local network
    KeyboardBindings keyboard;
    PadBindings pads;
};
GamePreferences game_preferences(const Settings& settings);
} // namespace ravenemudsk_ui
