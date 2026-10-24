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
    bool integer_scale = false; // Réglages > Affichage: a whole number of times the console's size
    bool lcd_colors = false;    // Réglages > Affichage: the GBC and GBA LCD's colors
    int frame_blending = 0;     // Réglages > Affichage: 0 off, 1 mix, 2 ghosting
    bool pause_unfocused = true; // Réglages > Affichage: pause while the game's window is in the background
    bool wireless_link = true;  // Réglages > Câble link: link with the devices of the local network
    // Réglages > Commandes: the controller rumbles with the cartridge's motor,
    // its motion sensors tilt and turn the games that have some, and Boktai's
    // solar sensor gets this share of full sunlight (0-100 %).
    // Réglages > Console: Game Boy games on a Game Boy Color, in these colors
    // (RAVENEMU_GBC_COLORS_*); color games on a Game Boy Advance's GBC; the
    // GBA's BIOS: 0 RavenCore's, 1 the real one, 2 the real one without its intro.
    bool gb_in_color = false;
    int gb_colors = 0;
    bool gbc_on_gba = false;
    int gba_bios = 0;
    bool rumble = true;
    bool rewind = true;         // Réglages > Commandes: keep the last moments to play them backwards
    bool motion_sensors = true;
    int solar_light = 50;
    KeyboardBindings keyboard;
    PadBindings pads;
};
GamePreferences game_preferences(const Settings& settings);
} // namespace ravenemudsk_ui
