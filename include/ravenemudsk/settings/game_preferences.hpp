// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/input/keyboard_bindings.hpp>
#include <ravenemudsk/input/pad_bindings.hpp>
#include <string>
namespace ravenemudsk_ui {
class Settings;
// What a game reads from the settings when it starts, in one read.
struct GamePreferences {
    bool hardcore = false;      // RetroAchievements Hardcore, as last chosen
    bool unlock_sound = true;   // "Son des succès": on unless turned off
    int volume = 100;           // Réglages > Son: 0-100, 0 when the sound is cut
    // Réglages > Son: the output device by its name (empty: the system's),
    // and the sound kept ahead of it, in milliseconds (audio_latencies).
    std::string audio_output;
    int audio_latency = 50;
    bool smooth_image = false;  // Réglages > Affichage: smooth scaling instead of sharp pixels
    bool integer_scale = false; // Réglages > Affichage: a whole number of times the console's size
    bool lcd_colors = false;    // Réglages > Affichage: the GBC and GBA LCD's colors
    int frame_blending = 0;     // Réglages > Affichage: 0 off, 1 mix, 2 ghosting
    bool pause_unfocused = true; // Réglages > Affichage: pause while the game's window is in the background
    // Réglages > Affichage, the screen: in full screen, the refresh rate that
    // the game's frames fall on (120 or 60 Hz on a 144 Hz screen); a screen
    // with a variable refresh (G-Sync, FreeSync) shows each frame when it is
    // ready, without vsync; the input read later in each refresh.
    int display = 0;            // the screen of the game and of TV mode
    bool refresh_switch = true;
    bool variable_refresh = false;
    bool frame_delay = false;
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
    int rumble_strength = 100;  // 10-100 % of the controller's motors
    // The left stick as the D-pad, past this share of its travel (10-90 %),
    // in the games it does not tilt.
    bool stick_dpad = true;
    int stick_dead_zone = 30;
    bool rewind = true;         // Réglages > Commandes: keep the last moments to play them backwards
    bool motion_sensors = true;
    int solar_light = 50;
    KeyboardBindings keyboard;
    PadBindings pads;
};
// Réglages > Son's choices, from the quickest to answer to the steadiest.
inline constexpr int audio_latencies[] = {25, 50, 100};
GamePreferences game_preferences(const Settings& settings);
} // namespace ravenemudsk_ui
