// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/settings/game_preferences.hpp>
#include <ravenemudsk/settings/settings.hpp>
#include <algorithm>
#include <cstdlib>
namespace ravenemudsk_ui {
GamePreferences game_preferences(const Settings& settings) {
    GamePreferences prefs;
    prefs.hardcore = settings.flag("ra_hardcore", false);
    prefs.unlock_sound = settings.flag("ra_unlock_sound", true);
    prefs.volume = settings.flag("mute", false) ? 0 : std::clamp(settings.number("volume", 100), 0, 100);
    prefs.audio_output = settings.get("audio_output");
    // The nearest of the choices, for a value written by hand.
    const int latency = settings.number("audio_latency", 50);
    for (const int choice : audio_latencies)
        if (std::abs(choice - latency) < std::abs(prefs.audio_latency - latency)) prefs.audio_latency = choice;
    prefs.smooth_image = settings.flag("smooth_image", false);
    prefs.integer_scale = settings.flag("integer_scale", false);
    prefs.lcd_colors = settings.flag("lcd_colors", false);
    prefs.frame_blending = std::clamp(settings.number("frame_blending", 0), 0, 2);
    prefs.pause_unfocused = settings.flag("pause_unfocused", true);
    prefs.display = settings.number("display", 0);
    prefs.refresh_switch = settings.flag("refresh_switch", true);
    prefs.variable_refresh = settings.flag("variable_refresh", false);
    prefs.frame_delay = settings.flag("frame_delay", false);
    prefs.wireless_link = settings.flag("wireless_link", true);
    prefs.gb_in_color = settings.flag("gb_in_color", false);
    prefs.gb_colors = std::clamp(settings.number("gb_colors", 0), 0, 13);
    prefs.gbc_on_gba = settings.flag("gbc_on_gba", false);
    prefs.gba_bios = std::clamp(settings.number("gba_bios", 0), 0, 2);
    prefs.rumble = settings.flag("rumble", true);
    prefs.rumble_strength = std::clamp(settings.number("rumble_strength", 100), 10, 100);
    prefs.stick_dpad = settings.flag("stick_dpad", true);
    prefs.stick_dead_zone = std::clamp(settings.number("stick_dead_zone", 30), 10, 90);
    prefs.rewind = settings.flag("rewind", true);
    prefs.motion_sensors = settings.flag("motion_sensors", true);
    prefs.solar_light = std::clamp(settings.number("solar_light", 50), 0, 100);
    prefs.keyboard.load(settings);
    prefs.pads.load(settings);
    return prefs;
}
} // namespace ravenemudsk_ui
