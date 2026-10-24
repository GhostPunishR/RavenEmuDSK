// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/settings/game_preferences.hpp>
#include <ravenemudsk/settings/settings.hpp>
#include <algorithm>
namespace ravenemudsk_ui {
GamePreferences game_preferences(const Settings& settings) {
    GamePreferences prefs;
    prefs.hardcore = settings.flag("ra_hardcore", false);
    prefs.unlock_sound = settings.flag("ra_unlock_sound", true);
    prefs.volume = settings.flag("mute", false) ? 0 : std::clamp(settings.number("volume", 100), 0, 100);
    prefs.smooth_image = settings.flag("smooth_image", false);
    prefs.integer_scale = settings.flag("integer_scale", false);
    prefs.lcd_colors = settings.flag("lcd_colors", false);
    prefs.frame_blending = std::clamp(settings.number("frame_blending", 0), 0, 2);
    prefs.pause_unfocused = settings.flag("pause_unfocused", true);
    prefs.wireless_link = settings.flag("wireless_link", true);
    prefs.rumble = settings.flag("rumble", true);
    prefs.motion_sensors = settings.flag("motion_sensors", true);
    prefs.solar_light = std::clamp(settings.number("solar_light", 50), 0, 100);
    prefs.keyboard.load(settings);
    prefs.pads.load(settings);
    return prefs;
}
} // namespace ravenemudsk_ui
